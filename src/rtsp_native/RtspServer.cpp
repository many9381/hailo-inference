#include "RtspServer.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <utility>

// ============================================================================
// 익명 네임스페이스 — 파일 로컬 유틸리티
// ============================================================================
namespace {

// RTP 페이로드 크기의 상한선(여유 확보용). 일반 이더넷 MTU 1500 에서 IP/UDP
// 헤더를 뺀 수준으로 맞춘다. FU-A 헤더 2 바이트 + RTP 헤더 12 바이트를 고려해
// 실제 페이로드는 이보다 작게 잡힌다.
constexpr size_t kMtu = 1400;

// 문자열을 소문자로 변환 (대소문자 무시 헤더 검색용).
std::string toLower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return out;
}

// RTSP 요청 문자열에서 특정 헤더 값을 추출 (대소문자 무시, 한 번만 매칭).
std::string findHeader(const std::string& req, const std::string& name) {
    std::string lowerReq  = toLower(req);
    std::string lowerName = toLower(name);
    size_t pos = 0;
    while (pos < lowerReq.size()) {
        size_t eol = lowerReq.find("\r\n", pos);
        if (eol == std::string::npos) break;
        if (eol - pos > lowerName.size() &&
            lowerReq.compare(pos, lowerName.size(), lowerName) == 0 &&
            lowerReq[pos + lowerName.size()] == ':') {
            size_t valStart = pos + lowerName.size() + 1;
            while (valStart < eol && (req[valStart] == ' ' || req[valStart] == '\t'))
                ++valStart;
            return req.substr(valStart, eol - valStart);
        }
        pos = eol + 2;
    }
    return {};
}

// CSeq 헤더를 정수로 파싱. 없으면 0.
int parseCSeq(const std::string& req) {
    std::string v = findHeader(req, "CSeq");
    if (v.empty()) return 0;
    return std::atoi(v.c_str());
}

// Transport 헤더에서 "client_port=RTP-RTCP" 값을 찾아 RTP 포트만 반환.
bool parseClientPort(const std::string& transport, uint16_t* rtpPort) {
    size_t p = transport.find("client_port=");
    if (p == std::string::npos) return false;
    p += std::strlen("client_port=");
    int rtp = 0, rtcp = 0;
    if (std::sscanf(transport.c_str() + p, "%d-%d", &rtp, &rtcp) < 1) return false;
    if (rtp <= 0 || rtp > 65535) return false;
    *rtpPort = static_cast<uint16_t>(rtp);
    return true;
}

// 랜덤 세션 ID 생성 (16진수 문자열).
std::string randomSessionId() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%08llx",
                  static_cast<unsigned long long>(rng() & 0xFFFFFFFFull));
    return std::string(buf);
}

// Annex-B byte-stream 에서 연속된 NAL 들의 시작 위치와 길이를 순회한다.
// 구분자는 [00 00 00 01] 또는 [00 00 01]. 콜백에 시작 포인터/길이를 전달한다.
template <typename Fn>
void splitAnnexB(const uint8_t* data, size_t size, Fn&& onNal) {
    // 다음 start code 위치 탐색. scLen 에는 3 또는 4 가 채워진다.
    auto findStart = [&](size_t from, size_t* scLen) -> size_t {
        for (size_t k = from; k + 2 < size; ++k) {
            if (data[k] == 0 && data[k + 1] == 0) {
                if (data[k + 2] == 1) { *scLen = 3; return k; }
                if (k + 3 < size && data[k + 2] == 0 && data[k + 3] == 1) {
                    *scLen = 4;
                    return k;
                }
            }
        }
        return size;
    };

    size_t sc = 0;
    size_t start = findStart(0, &sc);
    if (start == size) return;  // start code 없음 → 빈 AU
    size_t nalStart = start + sc;
    while (nalStart < size) {
        size_t nextSc = 0;
        size_t next   = findStart(nalStart, &nextSc);
        size_t nalEnd = next;
        if (nalEnd > nalStart) onNal(data + nalStart, nalEnd - nalStart);
        if (next == size) break;
        nalStart = next + nextSc;
    }
}

}  // namespace

// ============================================================================
// Session — 세션별 내부 상태
// ============================================================================
struct RtspServer::Session {
    int               tcpFd = -1;   // RTSP 제어 연결(연결 accept 시 생성)
    int               udpFd = -1;   // RTP UDP 소켓 (SETUP 시 생성)
    uint32_t          ssrc  = 0;    // RTP SSRC (세션 생성 시 난수)
    uint16_t          seq   = 0;    // RTP 시퀀스 번호 카운터
    std::string       sessionId;    // RTSP Session 헤더 값
    sockaddr_in       clientRtpAddr{};   // 클라이언트 IP + RTP 수신 포트
    std::atomic<bool> playing{false};    // PLAY 후 true, TEARDOWN/종료 시 false
    std::atomic<bool> alive  {true};     // 세션 스레드 종료 요청 플래그
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
// start — TCP 리스너 생성 + accept 스레드 가동
// ============================================================================
bool RtspServer::start() {
    this->stop();

    this->listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (this->listenFd_ < 0) {
        std::cerr << "[RtspServer] socket 생성 실패: "
                  << std::strerror(errno) << std::endl;
        return false;
    }

    // 재시작 시 이전 소켓의 TIME_WAIT 로 인해 bind 실패하는 경우를 방지한다.
    int one = 1;
    ::setsockopt(this->listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(static_cast<uint16_t>(this->port_));
    if (::bind(this->listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[RtspServer] bind 실패 (port " << this->port_ << "): "
                  << std::strerror(errno) << std::endl;
        ::close(this->listenFd_);
        this->listenFd_ = -1;
        return false;
    }
    if (::listen(this->listenFd_, 4) < 0) {
        std::cerr << "[RtspServer] listen 실패: "
                  << std::strerror(errno) << std::endl;
        ::close(this->listenFd_);
        this->listenFd_ = -1;
        return false;
    }

    this->running_.store(true);
    this->acceptThread_ = std::thread([this] { this->acceptLoop(); });

    std::cout << "[RtspServer] 시작: rtsp://<host>:" << this->port_
              << this->mountPoint_ << "  (" << this->width_ << "x" << this->height_
              << " @" << this->fps_ << "fps)" << std::endl;
    return true;
}

// ============================================================================
// stop — 모든 스레드/세션/소켓을 정리
// ============================================================================
void RtspServer::stop() {
    // running_ 이 이미 false 인 경우(두 번 호출 등) 남아있는 listen fd 만 정리한다.
    if (!this->running_.exchange(false)) {
        if (this->listenFd_ >= 0) {
            ::close(this->listenFd_);
            this->listenFd_ = -1;
        }
        return;
    }

    // accept 루프가 종료 플래그를 관찰하고 빠져나오길 기다린다.
    if (this->acceptThread_.joinable()) this->acceptThread_.join();
    if (this->listenFd_ >= 0) {
        ::close(this->listenFd_);
        this->listenFd_ = -1;
    }

    // 세션들에 종료 신호 후 스레드 컬렉션을 빼낸다.
    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(this->sessionsMu_);
        for (auto& s : this->sessions_) {
            s->alive.store(false);
            s->playing.store(false);
        }
        threads.swap(this->sessionThreads_);
    }

    // 세션 스레드는 SO_RCVTIMEO 만료마다 alive 플래그를 확인하므로 곧 종료된다.
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    {
        std::lock_guard<std::mutex> lock(this->sessionsMu_);
        this->sessions_.clear();
    }
    this->frameIndex_ = 0;
}

// ============================================================================
// acceptLoop — poll() 로 listen fd 를 감시하며 새 연결 수락
// ============================================================================
void RtspServer::acceptLoop() {
    while (this->running_.load()) {
        pollfd pfd{};
        pfd.fd     = this->listenFd_;
        pfd.events = POLLIN;
        int r = ::poll(&pfd, 1, 500);  // 500ms 마다 종료 플래그 재확인
        if (r <= 0) continue;
        if (!(pfd.revents & POLLIN)) continue;

        sockaddr_in peer{};
        socklen_t   peerLen = sizeof(peer);
        int clientFd = ::accept(this->listenFd_,
                                reinterpret_cast<sockaddr*>(&peer), &peerLen);
        if (clientFd < 0) {
            if (!this->running_.load()) break;
            if (errno == EINTR) continue;
            std::cerr << "[RtspServer] accept 실패: "
                      << std::strerror(errno) << std::endl;
            break;
        }

        // 세션 객체 생성 및 기본값 채우기.
        auto s = std::make_shared<Session>();
        s->tcpFd = clientFd;
        {
            static thread_local std::mt19937 rng{std::random_device{}()};
            s->ssrc = static_cast<uint32_t>(rng());
        }
        // 클라이언트 IP 는 accept 된 peer 에서 그대로 가져오고, RTP 포트는
        // SETUP 에서 client_port 헤더로 다시 채운다.
        s->clientRtpAddr = peer;

        // 세션 스레드 등록은 락 안에서 수행해 stop() 과의 경쟁을 피한다.
        {
            std::lock_guard<std::mutex> lock(this->sessionsMu_);
            if (!this->running_.load()) {
                ::close(clientFd);
                break;
            }
            this->sessions_.push_back(s);
            this->sessionThreads_.emplace_back([this, s] { this->sessionLoop(s); });
        }

        char ipBuf[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &peer.sin_addr, ipBuf, sizeof(ipBuf));
        std::cout << "[RtspServer] 세션 연결: " << ipBuf << std::endl;
    }
}

// ============================================================================
// sessionLoop — RTSP 요청을 읽어 handleRequest 로 위임
// ============================================================================
void RtspServer::sessionLoop(std::shared_ptr<Session> s) {
    // recv 블로킹 중 stop() 신호를 놓치지 않도록 읽기 타임아웃을 걸어둔다.
    timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 300 * 1000;  // 300ms
    ::setsockopt(s->tcpFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::string buffer;
    char        chunk[2048];
    bool        keepRunning = true;

    while (keepRunning && this->running_.load() && s->alive.load()) {
        ssize_t n = ::recv(s->tcpFd, chunk, sizeof(chunk), 0);
        if (n == 0) break;                                    // 상대가 닫음
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;  // 타임아웃
            if (errno == EINTR) continue;
            break;
        }
        buffer.append(chunk, chunk + n);

        // "\r\n\r\n" 으로 끝나는 완전한 요청을 하나씩 꺼내 처리한다.
        for (;;) {
            size_t end = buffer.find("\r\n\r\n");
            if (end == std::string::npos) break;
            std::string req = buffer.substr(0, end + 4);
            buffer.erase(0, end + 4);
            if (!this->handleRequest(*s, req)) {
                keepRunning = false;
                break;
            }
        }
    }

    // ── 세션 정리 ────────────────────────────────────────────────────────
    // sessionsMu_ 를 잡은 상태에서 udpFd 를 닫아야 sendNal() 과 경쟁하지 않는다.
    {
        std::lock_guard<std::mutex> lock(this->sessionsMu_);
        s->playing.store(false);
        s->alive.store(false);
        if (s->udpFd >= 0) {
            ::close(s->udpFd);
            s->udpFd = -1;
        }
    }
    if (s->tcpFd >= 0) {
        ::close(s->tcpFd);
        s->tcpFd = -1;
    }
    std::cout << "[RtspServer] 세션 종료 (id="
              << (s->sessionId.empty() ? "-" : s->sessionId) << ")" << std::endl;
}

// ============================================================================
// handleRequest — 한 개의 완전한 RTSP 요청 처리
// ============================================================================
bool RtspServer::handleRequest(Session& s, const std::string& req) {
    // 첫 줄: "METHOD URI RTSP/1.0"
    size_t firstEol = req.find("\r\n");
    if (firstEol == std::string::npos) return true;
    std::string firstLine = req.substr(0, firstEol);

    std::istringstream iss(firstLine);
    std::string method, uri, version;
    iss >> method >> uri >> version;
    if (method.empty()) return true;

    int cseq = parseCSeq(req);
    std::string response;

    if (method == "OPTIONS") {
        response = this->buildOptionsResponse(cseq);

    } else if (method == "DESCRIBE") {
        response = this->buildDescribeResponse(cseq, uri);

    } else if (method == "SETUP") {
        std::string transport     = findHeader(req, "Transport");
        uint16_t    clientRtpPort = 0;

        // UDP 유니캐스트 외 전송(TCP interleaved 등) 은 지원하지 않는다.
        if (transport.find("RTP/AVP") == std::string::npos ||
            transport.find("TCP") != std::string::npos ||
            !parseClientPort(transport, &clientRtpPort)) {
            response = this->buildErrorResponse(cseq, 461, "Unsupported Transport");
        } else {
            // 서버측 RTP UDP 소켓 생성 (포트 0 → 커널 자동 할당).
            int udpFd = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (udpFd < 0) {
                response = this->buildErrorResponse(cseq, 500, "Internal Server Error");
            } else {
                sockaddr_in localAddr{};
                localAddr.sin_family      = AF_INET;
                localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
                localAddr.sin_port        = 0;
                if (::bind(udpFd, reinterpret_cast<sockaddr*>(&localAddr),
                           sizeof(localAddr)) < 0) {
                    ::close(udpFd);
                    response = this->buildErrorResponse(cseq, 500,
                                                         "Internal Server Error");
                } else {
                    socklen_t localLen = sizeof(localAddr);
                    ::getsockname(udpFd,
                                  reinterpret_cast<sockaddr*>(&localAddr),
                                  &localLen);
                    uint16_t serverPort = ntohs(localAddr.sin_port);

                    // Session 상태 갱신 — sendNal() 과의 동시 접근을 막기 위해
                    // sessionsMu_ 를 잡고 한꺼번에 쓴다.
                    std::string sid;
                    {
                        std::lock_guard<std::mutex> lock(this->sessionsMu_);
                        // SETUP 이 여러 번 오면 기존 UDP 를 정리한다.
                        if (s.udpFd >= 0) ::close(s.udpFd);
                        s.udpFd = udpFd;
                        s.clientRtpAddr.sin_port = htons(clientRtpPort);
                        if (s.sessionId.empty()) {
                            s.sessionId = randomSessionId();
                        }
                        sid = s.sessionId;
                    }

                    std::ostringstream tr;
                    tr << "RTP/AVP;unicast;client_port=" << clientRtpPort << "-"
                       << (clientRtpPort + 1)
                       << ";server_port=" << serverPort << "-" << (serverPort + 1)
                       << ";ssrc=" << std::hex << s.ssrc << std::dec;
                    response = this->buildSetupResponse(cseq, sid, tr.str());
                }
            }
        }

    } else if (method == "PLAY") {
        s.playing.store(true);
        response = this->buildPlayResponse(cseq, s.sessionId);
        std::cout << "[RtspServer] PLAY 시작 (session=" << s.sessionId << ")"
                  << std::endl;

    } else if (method == "PAUSE") {
        s.playing.store(false);
        response = this->buildGenericOkResponse(cseq, s.sessionId);

    } else if (method == "TEARDOWN") {
        s.playing.store(false);
        response = this->buildTeardownResponse(cseq, s.sessionId);
        // TEARDOWN 후에는 응답만 보내고 연결을 닫는다.
        ::send(s.tcpFd, response.data(), response.size(), 0);
        return false;

    } else if (method == "GET_PARAMETER" || method == "SET_PARAMETER") {
        // keepalive 용으로만 지원: 본문 없이 200 OK 반환.
        response = this->buildGenericOkResponse(cseq, s.sessionId);

    } else {
        response = this->buildErrorResponse(cseq, 501, "Not Implemented");
    }

    ssize_t sent = ::send(s.tcpFd, response.data(), response.size(), 0);
    if (sent < 0) return false;
    return true;
}

// ============================================================================
// 응답 빌더들
// ============================================================================
std::string RtspServer::buildOptionsResponse(int cseq) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Public: OPTIONS, DESCRIBE, SETUP, PLAY, PAUSE, TEARDOWN, "
        << "GET_PARAMETER, SET_PARAMETER\r\n"
        << "\r\n";
    return oss.str();
}

std::string RtspServer::buildDescribeResponse(int cseq, const std::string& uri) {
    // SDP 본문: H.264 단일 비디오 트랙, RTP 페이로드 타입 96, 90kHz 클럭.
    // packetization-mode=1: single NAL + FU-A 지원. profile-level-id 는
    // constrained baseline/high 중 어느 수준이어도 무방하나 VLC/ffmpeg 호환을
    // 위해 baseline 수준 값을 관용적으로 적어둔다.
    std::ostringstream sdp;
    sdp << "v=0\r\n"
        << "o=- 0 0 IN IP4 127.0.0.1\r\n"
        << "s=Hailo H264 Stream\r\n"
        << "c=IN IP4 0.0.0.0\r\n"
        << "t=0 0\r\n"
        << "a=tool:hailo-rtsp\r\n"
        << "m=video 0 RTP/AVP 96\r\n"
        << "a=rtpmap:96 H264/90000\r\n"
        << "a=fmtp:96 packetization-mode=1;profile-level-id=42e01f\r\n"
        << "a=control:trackID=0\r\n";
    std::string body = sdp.str();

    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Content-Base: " << uri << "/\r\n"
        << "Content-Type: application/sdp\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "\r\n"
        << body;
    return oss.str();
}

std::string RtspServer::buildSetupResponse(int cseq, const std::string& sessionId,
                                            const std::string& transport) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Session: " << sessionId << ";timeout=60\r\n"
        << "Transport: " << transport << "\r\n"
        << "\r\n";
    return oss.str();
}

std::string RtspServer::buildPlayResponse(int cseq, const std::string& sessionId) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Session: " << sessionId << "\r\n"
        << "Range: npt=0.000-\r\n"
        << "\r\n";
    return oss.str();
}

std::string RtspServer::buildTeardownResponse(int cseq, const std::string& sessionId) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "Session: " << sessionId << "\r\n"
        << "\r\n";
    return oss.str();
}

std::string RtspServer::buildGenericOkResponse(int cseq, const std::string& sessionId) {
    std::ostringstream oss;
    oss << "RTSP/1.0 200 OK\r\n"
        << "CSeq: " << cseq << "\r\n";
    if (!sessionId.empty()) oss << "Session: " << sessionId << "\r\n";
    oss << "\r\n";
    return oss.str();
}

std::string RtspServer::buildErrorResponse(int cseq, int code,
                                            const std::string& reason) {
    std::ostringstream oss;
    oss << "RTSP/1.0 " << code << " " << reason << "\r\n"
        << "CSeq: " << cseq << "\r\n"
        << "\r\n";
    return oss.str();
}

// ============================================================================
// sendNal — AU 를 NAL 로 분해하여 모든 PLAY 세션에 RTP 전송
// ============================================================================
void RtspServer::sendNal(const uint8_t* nalData, size_t nalSize) {
    if (!this->running_.load() || nalSize == 0) return;

    // 90kHz RTP 타임스탬프. 한 호출 = 한 frame 규약이므로 frameIndex_ 를 증가.
    // 세션별 초기 오프셋은 상관없으므로 전역 frameIndex_ 를 그대로 사용한다.
    uint32_t rtpTs = static_cast<uint32_t>(
        (this->frameIndex_ * 90000ULL) / static_cast<uint64_t>(this->fps_));
    ++this->frameIndex_;

    // AU → NAL 목록으로 분해.
    std::vector<std::pair<const uint8_t*, size_t>> nals;
    splitAnnexB(nalData, nalSize, [&](const uint8_t* p, size_t sz) {
        if (sz > 0) nals.emplace_back(p, sz);
    });
    if (nals.empty()) return;

    // 모든 PLAY 세션에 동일한 AU 를 뿌린다. 락 범위 내에서 sendto 까지 수행해
    // 세션 정리(close) 와의 경쟁을 단순화한다.
    std::lock_guard<std::mutex> lock(this->sessionsMu_);
    for (auto& sp : this->sessions_) {
        Session& s = *sp;
        if (!s.alive.load() || !s.playing.load() || s.udpFd < 0) continue;
        for (size_t i = 0; i < nals.size(); ++i) {
            const bool lastNal = (i + 1 == nals.size());
            this->sendNalToSession(s, rtpTs, nals[i].first, nals[i].second, lastNal);
        }
    }
}

// ============================================================================
// sendNalToSession — RFC 6184 single NAL / FU-A 분기 후 송신
// ============================================================================
void RtspServer::sendNalToSession(Session& s, uint32_t rtpTs,
                                   const uint8_t* nal, size_t size, bool lastNal) {
    if (size == 0) return;

    // 작은 NAL: Single NAL unit 모드 — RTP payload == NAL 전체.
    if (size + 12 <= kMtu) {
        this->sendRtpPacket(s, rtpTs, lastNal, nal, size);
        return;
    }

    // 큰 NAL: FU-A 로 쪼갠다.
    //   FU indicator (1 byte): [F=원래NAL의 F | NRI=원래NAL의 NRI | type=28]
    //   FU header    (1 byte): [S | E | R=0 | type=원래NAL type]
    //   payload: 원래 NAL 의 header(1B) 를 제외한 RBSP 를 조각별로 복사
    const uint8_t nalHeader = nal[0];
    const uint8_t nri       = nalHeader & 0x60;
    const uint8_t type      = nalHeader & 0x1F;
    const uint8_t* body     = nal + 1;
    const size_t   bodySize = size - 1;

    // RTP(12) + FU ind(1) + FU hdr(1) 만큼을 제외한 최대 조각 크기.
    const size_t maxPayload = kMtu - 12 - 2;

    size_t offset = 0;
    uint8_t frag[kMtu];
    while (offset < bodySize) {
        const size_t chunk = std::min(maxPayload, bodySize - offset);
        const bool   start = (offset == 0);
        const bool   end   = (offset + chunk == bodySize);

        frag[0] = static_cast<uint8_t>(0x1C | nri);  // FU indicator: type = 28
        frag[1] = static_cast<uint8_t>((start ? 0x80 : 0) | (end ? 0x40 : 0) | type);
        std::memcpy(frag + 2, body + offset, chunk);

        // marker bit 은 AU 의 가장 마지막 RTP 패킷에만 세운다.
        const bool marker = (end && lastNal);
        this->sendRtpPacket(s, rtpTs, marker, frag, chunk + 2);
        offset += chunk;
    }
}

// ============================================================================
// sendRtpPacket — RTP 헤더 작성 후 UDP 로 1회 송신
// ============================================================================
void RtspServer::sendRtpPacket(Session& s, uint32_t rtpTs, bool marker,
                                const uint8_t* payload, size_t size) {
    // RTP 고정 헤더 12 바이트
    //   byte 0:  V(2)=2 | P=0 | X=0 | CC=0            → 0x80
    //   byte 1:  M(1)    | PT(7)=96                   → marker ? 0xE0 : 0x60
    //   byte 2-3: sequence number (big-endian)
    //   byte 4-7: timestamp       (big-endian, 90kHz)
    //   byte 8-11: SSRC           (big-endian)
    uint8_t packet[kMtu + 12];
    packet[0] = 0x80;
    packet[1] = static_cast<uint8_t>((marker ? 0x80 : 0) | 96);

    const uint16_t seq = s.seq++;
    packet[2] = static_cast<uint8_t>((seq >> 8) & 0xFF);
    packet[3] = static_cast<uint8_t>(seq & 0xFF);

    packet[4] = static_cast<uint8_t>((rtpTs >> 24) & 0xFF);
    packet[5] = static_cast<uint8_t>((rtpTs >> 16) & 0xFF);
    packet[6] = static_cast<uint8_t>((rtpTs >>  8) & 0xFF);
    packet[7] = static_cast<uint8_t>( rtpTs        & 0xFF);

    packet[8]  = static_cast<uint8_t>((s.ssrc >> 24) & 0xFF);
    packet[9]  = static_cast<uint8_t>((s.ssrc >> 16) & 0xFF);
    packet[10] = static_cast<uint8_t>((s.ssrc >>  8) & 0xFF);
    packet[11] = static_cast<uint8_t>( s.ssrc        & 0xFF);

    std::memcpy(packet + 12, payload, size);

    ::sendto(s.udpFd, packet, size + 12, 0,
             reinterpret_cast<sockaddr*>(&s.clientRtpAddr),
             sizeof(s.clientRtpAddr));
}
