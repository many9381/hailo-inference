#include "RtspServer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <QDebug>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <random>
#include <sstream>

// ============================================================================
// Session — 한 RTSP 클라이언트에 대한 세션 상태
// ============================================================================
struct RtspServer::Session {
    int              tcpFd     = -1;   // RTSP 제어 TCP 소켓
    int              udpFd     = -1;   // RTP 송출 UDP 소켓
    sockaddr_in      clientRtpAddr{};  // 클라이언트 RTP 수신 주소
    std::string      id;               // Session ID
    uint16_t         seq       = 0;    // RTP sequence number
    uint32_t         ssrc      = 0;    // RTP SSRC
    bool             playing   = false;
    std::atomic<bool> alive{true};
};

// ============================================================================
// 생성자 / 소멸자
// ============================================================================
RtspServer::RtspServer(int port, std::string mountPoint,
                       int width, int height, int fps)
    : port_(port), mountPoint_(std::move(mountPoint)),
      width_(width), height_(height), fps_(fps) {}

RtspServer::~RtspServer() {
    this->stop();
}

// ============================================================================
// start — TCP 리스너 + accept 스레드
// ============================================================================
bool RtspServer::start() {
    this->stop();

    this->listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (this->listenFd_ < 0) {
        qWarning() << "[RtspServer] socket 실패:" << std::strerror(errno);
        return false;
    }

    int opt = 1;
    ::setsockopt(this->listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(static_cast<uint16_t>(this->port_));

    if (::bind(this->listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        qWarning() << "[RtspServer] bind 실패:" << std::strerror(errno);
        ::close(this->listenFd_);
        this->listenFd_ = -1;
        return false;
    }
    if (::listen(this->listenFd_, 4) < 0) {
        qWarning() << "[RtspServer] listen 실패:" << std::strerror(errno);
        ::close(this->listenFd_);
        this->listenFd_ = -1;
        return false;
    }

    this->running_.store(true);
    this->acceptThread_ = std::thread(&RtspServer::acceptLoop, this);

    qInfo() << "[RtspServer] 시작: rtsp://<host>:" << this->port_
            << this->mountPoint_.c_str()
            << " (" << this->width_ << "x" << this->height_
            << " @" << this->fps_ << "fps)";
    return true;
}

// ============================================================================
// stop
// ============================================================================
void RtspServer::stop() {
    this->running_.store(false);

    if (this->listenFd_ >= 0) {
        ::close(this->listenFd_);
        this->listenFd_ = -1;
    }
    if (this->acceptThread_.joinable()) {
        this->acceptThread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(this->sessionsMu_);
        for (auto& s : this->sessions_) {
            s->alive.store(false);
            if (s->tcpFd >= 0) { ::close(s->tcpFd); s->tcpFd = -1; }
            if (s->udpFd >= 0) { ::close(s->udpFd); s->udpFd = -1; }
        }
    }
    for (auto& t : this->sessionThreads_) {
        if (t.joinable()) t.join();
    }
    {
        std::lock_guard<std::mutex> lock(this->sessionsMu_);
        this->sessions_.clear();
        this->sessionThreads_.clear();
    }

    this->frameIndex_ = 0;
}

// ============================================================================
// acceptLoop — poll() 로 새 연결 대기
// ============================================================================
void RtspServer::acceptLoop() {
    while (this->running_.load()) {
        pollfd pfd{};
        pfd.fd     = this->listenFd_;
        pfd.events = POLLIN;
        int ret = ::poll(&pfd, 1, 500);  // 500ms 주기로 종료 확인
        if (ret <= 0) continue;

        sockaddr_in clientAddr{};
        socklen_t   addrLen = sizeof(clientAddr);
        int fd = ::accept(this->listenFd_,
                          reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (fd < 0) continue;

        auto session = std::make_shared<Session>();
        session->tcpFd = fd;

        // 랜덤 Session ID + SSRC 생성
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dist;
        session->ssrc = dist(gen);
        {
            std::ostringstream oss;
            oss << std::hex << dist(gen);
            session->id = oss.str();
        }

        std::lock_guard<std::mutex> lock(this->sessionsMu_);
        this->sessions_.push_back(session);
        this->sessionThreads_.emplace_back(&RtspServer::sessionLoop, this, session);

        char ipBuf[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(ipBuf));
        qInfo() << "[RtspServer] 클라이언트 연결:" << ipBuf;
    }
}

// ============================================================================
// sessionLoop — RTSP 요청 수신 및 처리
// ============================================================================
void RtspServer::sessionLoop(std::shared_ptr<Session> session) {
    std::string buffer;
    char chunk[2048];

    while (this->running_.load() && session->alive.load()) {
        pollfd pfd{};
        pfd.fd     = session->tcpFd;
        pfd.events = POLLIN;
        int ret = ::poll(&pfd, 1, 500);
        if (ret <= 0) continue;

        ssize_t n = ::recv(session->tcpFd, chunk, sizeof(chunk), 0);
        if (n <= 0) break;
        buffer.append(chunk, chunk + n);

        // 완전한 요청(\r\n\r\n) 이 들어올 때까지 누적
        size_t pos;
        while ((pos = buffer.find("\r\n\r\n")) != std::string::npos) {
            std::string req = buffer.substr(0, pos + 4);
            buffer.erase(0, pos + 4);
            if (!this->handleRequest(*session, req)) {
                session->alive.store(false);
                break;
            }
        }
    }

    if (session->tcpFd >= 0) { ::close(session->tcpFd); session->tcpFd = -1; }
    if (session->udpFd >= 0) { ::close(session->udpFd); session->udpFd = -1; }
}

// ============================================================================
// handleRequest — RTSP 메서드 분기
// ============================================================================
bool RtspServer::handleRequest(Session& s, const std::string& req) {
    // CSeq 파싱
    int cseq = 1;
    {
        size_t p = req.find("CSeq:");
        if (p == std::string::npos) p = req.find("cseq:");
        if (p != std::string::npos)
            cseq = std::atoi(req.c_str() + p + 5);
    }

    std::string response;

    if (req.compare(0, 7, "OPTIONS") == 0) {
        response = this->buildOptionsResponse(cseq);
    } else if (req.compare(0, 8, "DESCRIBE") == 0) {
        // URI 파싱
        size_t sp = req.find(' ');
        size_t ep = req.find(' ', sp + 1);
        std::string uri = req.substr(sp + 1, ep - sp - 1);
        response = this->buildDescribeResponse(cseq, uri);
    } else if (req.compare(0, 5, "SETUP") == 0) {
        // Transport 헤더에서 client_port 추출
        std::string transport;
        size_t tp = req.find("Transport:");
        if (tp == std::string::npos) tp = req.find("transport:");
        if (tp != std::string::npos) {
            size_t eol = req.find("\r\n", tp);
            transport = req.substr(tp + 10, eol - tp - 10);
            // 앞쪽 공백 제거
            size_t start = transport.find_first_not_of(" \t");
            if (start != std::string::npos) transport = transport.substr(start);
        }

        // client_port 추출
        uint16_t clientRtpPort = 0;
        size_t cp = transport.find("client_port=");
        if (cp != std::string::npos) {
            clientRtpPort = static_cast<uint16_t>(
                std::atoi(transport.c_str() + cp + 12));
        }

        // 클라이언트 IP (TCP 소켓에서 추출)
        sockaddr_in peerAddr{};
        socklen_t   peerLen = sizeof(peerAddr);
        ::getpeername(s.tcpFd, reinterpret_cast<sockaddr*>(&peerAddr), &peerLen);

        // UDP 소켓 생성
        s.udpFd = ::socket(AF_INET, SOCK_DGRAM, 0);
        s.clientRtpAddr.sin_family      = AF_INET;
        s.clientRtpAddr.sin_addr        = peerAddr.sin_addr;
        s.clientRtpAddr.sin_port        = htons(clientRtpPort);

        // server_port 를 알려주기 위해 바인드
        sockaddr_in localAddr{};
        localAddr.sin_family      = AF_INET;
        localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        localAddr.sin_port        = 0;
        ::bind(s.udpFd, reinterpret_cast<sockaddr*>(&localAddr), sizeof(localAddr));
        socklen_t localLen = sizeof(localAddr);
        ::getsockname(s.udpFd, reinterpret_cast<sockaddr*>(&localAddr), &localLen);
        uint16_t serverRtpPort = ntohs(localAddr.sin_port);

        response = this->buildSetupResponse(cseq, s.id, transport +
            ";server_port=" + std::to_string(serverRtpPort) + "-" +
            std::to_string(serverRtpPort + 1));
    } else if (req.compare(0, 4, "PLAY") == 0) {
        s.playing = true;
        response = this->buildPlayResponse(cseq, s.id);
    } else if (req.compare(0, 8, "TEARDOWN") == 0) {
        response = this->buildTeardownResponse(cseq, s.id);
        ::send(s.tcpFd, response.data(), response.size(), 0);
        return false;  // 세션 종료
    } else {
        response = this->buildGenericOkResponse(cseq, s.id);
    }

    ssize_t sent = ::send(s.tcpFd, response.data(), response.size(), 0);
    return sent >= 0;
}

// ============================================================================
// 응답 빌더들
// ============================================================================
std::string RtspServer::buildOptionsResponse(int cseq) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n\r\n";
    return oss.str();
}

std::string RtspServer::buildDescribeResponse(int cseq, const std::string& uri) {
    std::ostringstream sdp;
    sdp << "v=0\r\n"
        << "o=- 0 0 IN IP4 0.0.0.0\r\n"
        << "s=Hailo Stream\r\n"
        << "c=IN IP4 0.0.0.0\r\n"
        << "t=0 0\r\n"
        << "m=video 0 RTP/AVP 96\r\n"
        << "a=rtpmap:96 H264/90000\r\n"
        << "a=control:trackID=0\r\n";
    std::string sdpStr = sdp.str();

    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Content-Base: " << uri << "/\r\n"
        << "Content-Type: application/sdp\r\n"
        << "Content-Length: " << sdpStr.size() << "\r\n\r\n"
        << sdpStr;
    return oss.str();
}

std::string RtspServer::buildSetupResponse(int cseq, const std::string& sessionId,
                                           const std::string& transport) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Session: " << sessionId << "\r\n"
        << "Transport: " << transport << "\r\n\r\n";
    return oss.str();
}

std::string RtspServer::buildPlayResponse(int cseq, const std::string& sessionId) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Session: " << sessionId << "\r\n"
        << "Range: npt=0.000-\r\n\r\n";
    return oss.str();
}

std::string RtspServer::buildTeardownResponse(int cseq, const std::string& sessionId) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Session: " << sessionId << "\r\n\r\n";
    return oss.str();
}

std::string RtspServer::buildGenericOkResponse(int cseq, const std::string& sessionId) {
    return this->buildTeardownResponse(cseq, sessionId);  // 같은 포맷
}

std::string RtspServer::buildErrorResponse(int cseq, int code, const std::string& reason) {
    std::ostringstream oss;
    oss << "RTSP/1.0 " << code << " " << reason << "\r\n"
        << "CSeq: " << cseq << "\r\n\r\n";
    return oss.str();
}

// ============================================================================
// sendNal — 모든 PLAY 세션에 access unit 전송 (RTP 패킷화)
// ============================================================================
void RtspServer::sendNal(const uint8_t* nalData, size_t nalSize) {
    if (!this->running_.load() || nalSize == 0) return;

    uint32_t rtpTs = static_cast<uint32_t>(
        this->frameIndex_ * 90000 / static_cast<uint64_t>(this->fps_));
    ++this->frameIndex_;

    // Annex-B byte-stream 에서 NAL 단위를 추출
    std::vector<std::pair<const uint8_t*, size_t>> nals;
    size_t i = 0;
    while (i < nalSize) {
        // start code 찾기: 0x000001 또는 0x00000001
        size_t scLen = 0;
        if (i + 3 <= nalSize && nalData[i] == 0 && nalData[i+1] == 0 && nalData[i+2] == 1) {
            scLen = 3;
        } else if (i + 4 <= nalSize && nalData[i] == 0 && nalData[i+1] == 0 &&
                   nalData[i+2] == 0 && nalData[i+3] == 1) {
            scLen = 4;
        } else {
            ++i;
            continue;
        }

        size_t nalStart = i + scLen;
        // 다음 start code 또는 끝까지
        size_t nalEnd = nalSize;
        for (size_t j = nalStart; j + 3 <= nalSize; ++j) {
            if (nalData[j] == 0 && nalData[j+1] == 0 &&
                (nalData[j+2] == 1 || (j + 3 < nalSize && nalData[j+2] == 0 && nalData[j+3] == 1))) {
                nalEnd = j;
                break;
            }
        }

        if (nalEnd > nalStart) {
            nals.emplace_back(nalData + nalStart, nalEnd - nalStart);
        }
        i = nalEnd;
    }

    std::lock_guard<std::mutex> lock(this->sessionsMu_);
    for (auto& session : this->sessions_) {
        if (!session->playing || session->udpFd < 0) continue;
        for (size_t n = 0; n < nals.size(); ++n) {
            this->sendNalToSession(*session, rtpTs,
                                   nals[n].first, nals[n].second,
                                   n == nals.size() - 1);
        }
    }
}

// ============================================================================
// sendNalToSession — Single NAL 또는 FU-A 패킷화
// ============================================================================
static constexpr size_t MAX_RTP_PAYLOAD = 1400;

void RtspServer::sendNalToSession(Session& s, uint32_t rtpTs,
                                  const uint8_t* nal, size_t size,
                                  bool lastNal) {
    if (size <= MAX_RTP_PAYLOAD) {
        // Single NAL unit
        this->sendRtpPacket(s, rtpTs, lastNal, nal, size);
    } else {
        // FU-A fragmentation
        uint8_t nalHeader = nal[0];
        uint8_t fnri      = nalHeader & 0xE0;  // F + NRI
        uint8_t type      = nalHeader & 0x1F;

        const uint8_t* ptr       = nal + 1;
        size_t         remaining = size - 1;
        bool           first     = true;

        while (remaining > 0) {
            size_t chunkSize = std::min(remaining, MAX_RTP_PAYLOAD - 2);
            bool   last      = (chunkSize == remaining);

            uint8_t fuIndicator = fnri | 28;  // type = 28 (FU-A)
            uint8_t fuHeader    = type;
            if (first) fuHeader |= 0x80;  // S bit
            if (last)  fuHeader |= 0x40;  // E bit

            uint8_t packet[2 + MAX_RTP_PAYLOAD];
            packet[0] = fuIndicator;
            packet[1] = fuHeader;
            std::memcpy(packet + 2, ptr, chunkSize);

            this->sendRtpPacket(s, rtpTs, last && lastNal, packet, chunkSize + 2);

            ptr       += chunkSize;
            remaining -= chunkSize;
            first      = false;
        }
    }
}

// ============================================================================
// sendRtpPacket — RTP 헤더 + 페이로드 → UDP 송신
// ============================================================================
void RtspServer::sendRtpPacket(Session& s, uint32_t rtpTs, bool marker,
                               const uint8_t* payload, size_t size) {
    uint8_t packet[12 + MAX_RTP_PAYLOAD + 2];

    // RTP header (12 bytes)
    packet[0]  = 0x80;  // V=2
    packet[1]  = static_cast<uint8_t>(96 | (marker ? 0x80 : 0));  // PT=96
    packet[2]  = static_cast<uint8_t>((s.seq >> 8) & 0xFF);
    packet[3]  = static_cast<uint8_t>(s.seq & 0xFF);
    packet[4]  = static_cast<uint8_t>((rtpTs >> 24) & 0xFF);
    packet[5]  = static_cast<uint8_t>((rtpTs >> 16) & 0xFF);
    packet[6]  = static_cast<uint8_t>((rtpTs >> 8)  & 0xFF);
    packet[7]  = static_cast<uint8_t>(rtpTs & 0xFF);
    packet[8]  = static_cast<uint8_t>((s.ssrc >> 24) & 0xFF);
    packet[9]  = static_cast<uint8_t>((s.ssrc >> 16) & 0xFF);
    packet[10] = static_cast<uint8_t>((s.ssrc >> 8)  & 0xFF);
    packet[11] = static_cast<uint8_t>(s.ssrc & 0xFF);

    std::memcpy(packet + 12, payload, size);
    this->cipher_->encrypt(packet + 12, size);
    ++s.seq;

    ::sendto(s.udpFd, packet, 12 + size, 0,
             reinterpret_cast<const sockaddr*>(&s.clientRtpAddr),
             sizeof(s.clientRtpAddr));
}
