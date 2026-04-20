#include "RtspClient.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <QDebug>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <random>
#include <sstream>

#include <openssl/hmac.h>

#include "rtsp_native/TlsHandshake.h"

// ============================================================================
// 생성자 / 소멸자
// ============================================================================
RtspClient::RtspClient(bool rtpTcp, QObject* parent)
    : QObject(parent), rtpTcp_(rtpTcp) {}

RtspClient::~RtspClient() {
    this->stop();
}

// ============================================================================
// start — URL 파싱 → 제어 연결 → RTP 바인드 → 시그널링 → 수신 루프 가동
// ============================================================================
bool RtspClient::start(const std::string& url) {
    this->stop();

    if (!this->parseUrl(url)) {
        qWarning() << "[RtspClient] URL 파싱 실패:" << url.c_str();
        return false;
    }
    this->baseUrl_ = url;

    if (!this->openControl()) return false;

    // TLS 1.3 핸드셰이크 — RTSP 시그널링 전에 키 교환 수행
    if (!this->performTlsHandshake()) {
        ::close(this->controlFd_);
        this->controlFd_ = -1;
        return false;
    }

    if (!this->rtpTcp_) {
        if (!this->bindRtpSocket()) {
            ::close(this->controlFd_);
            this->controlFd_ = -1;
            return false;
        }
    }
    if (!this->performSignaling()) {
        if (this->rtpFd_     >= 0) { ::close(this->rtpFd_);     this->rtpFd_     = -1; }
        if (this->controlFd_ >= 0) { ::close(this->controlFd_); this->controlFd_ = -1; }
        return false;
    }

    // TCP 모드에서는 controlFd_ 에 recv timeout 을 제거하여
    // interleaved RTP 수신이 블로킹되지 않도록 한다.
    // (rtpLoop 종료는 running_ 플래그 + 소켓 close 로 처리)
    if (this->rtpTcp_) {
        timeval tv{};
        tv.tv_sec  = 0;
        tv.tv_usec = 0;  // 무한 대기
        ::setsockopt(this->controlFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    this->running_.store(true);
    this->rtpThread_ = std::thread([this] { this->rtpLoop(); });

    qInfo() << "[RtspClient] 연결 시작:" << url.c_str();
    return true;
}

// ============================================================================
// stop — 수신 스레드 종료 후 소켓 정리
// ============================================================================
void RtspClient::stop() {
    this->running_.store(false);
    if (this->rtpThread_.joinable()) this->rtpThread_.join();

    if (this->rtpFd_ >= 0) {
        ::close(this->rtpFd_);
        this->rtpFd_ = -1;
    }
    if (this->controlFd_ >= 0) {
        // 가능하면 TEARDOWN 을 보내고 싶지만 타임아웃/오류 처리가 번거로우므로
        // 여기서는 단순히 소켓만 닫는다. 서버측 세션 타임아웃에 의해 정리된다.
        ::close(this->controlFd_);
        this->controlFd_ = -1;
    }

    this->sessionId_.clear();
    this->cseq_         = 1;
    this->fuBuffer_.clear();
    this->fuInProgress_ = false;

    // TLS 유도 키 초기화
    this->srtpCipher_.reset();
    this->rtspCipher_.reset();
    this->srtpAuthKey_.clear();
}

// ============================================================================
// parseUrl — "rtsp://host[:port]/path" 분해
// ============================================================================
bool RtspClient::parseUrl(const std::string& url) {
    const std::string prefix = "rtsp://";
    if (url.compare(0, prefix.size(), prefix) != 0) return false;

    size_t hostStart = prefix.size();
    size_t pathStart = url.find('/', hostStart);
    std::string hostPort = (pathStart == std::string::npos)
                               ? url.substr(hostStart)
                               : url.substr(hostStart, pathStart - hostStart);
    this->path_ = (pathStart == std::string::npos) ? "/" : url.substr(pathStart);

    size_t colon = hostPort.find(':');
    if (colon == std::string::npos) {
        this->host_ = hostPort;
        this->port_ = 554;
    } else {
        this->host_ = hostPort.substr(0, colon);
        this->port_ = std::atoi(hostPort.c_str() + colon + 1);
        if (this->port_ <= 0) this->port_ = 554;
    }
    return !this->host_.empty();
}

// ============================================================================
// openControl — TCP 연결 + getaddrinfo 로 호스트 해석
// ============================================================================
bool RtspClient::openControl() {
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    char portBuf[16];
    std::snprintf(portBuf, sizeof(portBuf), "%d", this->port_);
    int gai = ::getaddrinfo(this->host_.c_str(), portBuf, &hints, &res);
    if (gai != 0 || !res) {
        qWarning() << "[RtspClient] getaddrinfo 실패:" << gai_strerror(gai);
        return false;
    }

    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        ::freeaddrinfo(res);
        qWarning() << "[RtspClient] socket 실패:" << std::strerror(errno);
        return false;
    }
    if (::connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        qWarning() << "[RtspClient] connect 실패:" << std::strerror(errno);
        ::close(fd);
        ::freeaddrinfo(res);
        return false;
    }
    ::freeaddrinfo(res);

    // send/recv 에 timeout 을 걸어 네트워크 장애 시 stop() 이 오래 블로킹되지 않도록 한다.
    timeval tv{};
    tv.tv_sec  = 2;
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    this->controlFd_ = fd;
    return true;
}

// ============================================================================
// performTlsHandshake — TLS 1.3 키 교환 수행, 클라이언트측 키 설정
// ============================================================================
bool RtspClient::performTlsHandshake() {
    TlsHandshake tls;
    if (!tls.performClientHandshake(this->controlFd_)) {
        qWarning() << "[RtspClient] TLS 핸드셰이크 실패";
        return false;
    }

    this->srtpCipher_ = tls.createSrtpCipher();
    this->rtspCipher_ = tls.createRtspCipher();
    this->srtpAuthKey_ = tls.srtpAuthKey();

    qInfo() << "[RtspClient] TLS 1.3 핸드셰이크 완료";
    return true;
}

// ============================================================================
// bindRtpSocket — 클라이언트 RTP 수신용 UDP 소켓 생성 (임시 포트 자동 할당)
// ============================================================================
bool RtspClient::bindRtpSocket() {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        qWarning() << "[RtspClient] UDP socket 실패:" << std::strerror(errno);
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = 0;  // 커널에 맡김
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        qWarning() << "[RtspClient] UDP bind 실패:" << std::strerror(errno);
        ::close(fd);
        return false;
    }
    socklen_t addrLen = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &addrLen);
    this->clientRtpPort_ = ntohs(addr.sin_port);

    // RTP 수신 타임아웃 — running_ 플래그를 주기적으로 확인하기 위함.
    timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 300 * 1000;  // 300ms
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    this->rtpFd_ = fd;
    return true;
}

// ============================================================================
// sendRequest — 한 개의 RTSP 요청을 보내고 응답을 한 메시지까지 수신
// ============================================================================
bool RtspClient::sendRequest(const std::string& req, std::string* response) {
    // ── TLS 유도 RTSP cipher 로 암호화하여 길이 프리픽스와 함께 전송 ────
    std::vector<uint8_t> encBuf(req.begin(), req.end());
    this->rtspCipher_->encrypt(encBuf.data(), encBuf.size());

    uint32_t netLen = htonl(static_cast<uint32_t>(encBuf.size()));
    if (::send(this->controlFd_, &netLen, 4, 0) != 4) {
        qWarning() << "[RtspClient] send length 실패";
        return false;
    }
    {
        size_t sent = 0;
        while (sent < encBuf.size()) {
            ssize_t n = ::send(this->controlFd_, encBuf.data() + sent,
                               encBuf.size() - sent, 0);
            if (n <= 0) {
                qWarning() << "[RtspClient] send 실패:" << std::strerror(errno);
                return false;
            }
            sent += static_cast<size_t>(n);
        }
    }

    // ── 길이 프리픽스 기반으로 암호화된 응답 수신 ────────────────────────
    uint32_t respNetLen = 0;
    {
        size_t received = 0;
        while (received < 4) {
            ssize_t n = ::recv(this->controlFd_,
                               reinterpret_cast<char*>(&respNetLen) + received,
                               4 - received, 0);
            if (n <= 0) {
                qWarning() << "[RtspClient] recv length 실패";
                return false;
            }
            received += static_cast<size_t>(n);
        }
    }

    uint32_t respLen = ntohl(respNetLen);
    if (respLen == 0 || respLen > 65536) return false;

    std::vector<uint8_t> respBuf(respLen);
    {
        size_t received = 0;
        while (received < respLen) {
            ssize_t n = ::recv(this->controlFd_, respBuf.data() + received,
                               respLen - received, 0);
            if (n <= 0) {
                qWarning() << "[RtspClient] recv 실패:" << std::strerror(errno);
                return false;
            }
            received += static_cast<size_t>(n);
        }
    }

    this->rtspCipher_->decrypt(respBuf.data(), respBuf.size());
    response->assign(respBuf.begin(), respBuf.end());
    return true;
}

// ============================================================================
// performSignaling — OPTIONS → DESCRIBE → SETUP → PLAY 순서로 대화
// ============================================================================
bool RtspClient::performSignaling() {
    auto makeLine = [](const char* method, const std::string& url, int cseq,
                       const std::string& sessionId,
                       const std::string& extra) {
        std::ostringstream oss;
        oss << method << " " << url << " RTSP/1.0\r\n"
            << "CSeq: " << cseq << "\r\n"
            << "User-Agent: hailo-rtsp-client\r\n";
        if (!sessionId.empty()) oss << "Session: " << sessionId << "\r\n";
        oss << extra << "\r\n";
        return oss.str();
    };

    std::string response;

    // ── OPTIONS ──────────────────────────────────────────────────────────
    {
        std::string req = makeLine("OPTIONS", this->baseUrl_,
                                    this->cseq_++, "", "");
        if (!this->sendRequest(req, &response)) return false;
        if (response.find("RTSP/1.0 200") == std::string::npos) {
            qWarning() << "[RtspClient] OPTIONS 실패";
            return false;
        }
    }

    // ── DESCRIBE ─────────────────────────────────────────────────────────
    {
        std::string req = makeLine("DESCRIBE", this->baseUrl_,
                                    this->cseq_++, "", "Accept: application/sdp\r\n");
        if (!this->sendRequest(req, &response)) return false;
        if (response.find("RTSP/1.0 200") == std::string::npos) {
            qWarning() << "[RtspClient] DESCRIBE 실패:\n" << response.c_str();
            return false;
        }
    }

    // SETUP 은 control attribute 를 이용해 <baseUrl>/trackID=0 으로 보내는 것이
    // 가장 호환성 좋다. 서버가 control 을 다르게 주더라도 우리는 고정 URL 을
    // 시도한다 (서버가 "a=control:trackID=0" 을 쓰는 관례에 맞춤).
    std::string setupUrl = this->baseUrl_ + "/trackID=0";

    // ── SETUP ────────────────────────────────────────────────────────────
    {
        std::ostringstream tr;
        if (this->rtpTcp_) {
            tr << "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n";
        } else {
            tr << "Transport: RTP/AVP;unicast;client_port="
               << this->clientRtpPort_ << "-" << (this->clientRtpPort_ + 1) << "\r\n";
        }
        std::string req = makeLine("SETUP", setupUrl, this->cseq_++, "", tr.str());
        if (!this->sendRequest(req, &response)) return false;
        if (response.find("RTSP/1.0 200") == std::string::npos) {
            qWarning() << "[RtspClient] SETUP 실패:\n" << response.c_str();
            return false;
        }
        // Session 헤더 파싱.
        size_t sp = response.find("Session:");
        if (sp == std::string::npos) sp = response.find("session:");
        if (sp != std::string::npos) {
            size_t lineEnd = response.find("\r\n", sp);
            std::string line = response.substr(sp + 8, lineEnd - (sp + 8));
            // 앞쪽 공백 제거
            size_t start = line.find_first_not_of(" \t");
            if (start != std::string::npos) line = line.substr(start);
            size_t semi = line.find(';');
            this->sessionId_ = (semi == std::string::npos) ? line : line.substr(0, semi);
        }
        if (this->sessionId_.empty()) {
            qWarning() << "[RtspClient] SETUP 응답에 Session 없음";
            return false;
        }
    }

    // ── PLAY ─────────────────────────────────────────────────────────────
    {
        std::string req = makeLine("PLAY", this->baseUrl_,
                                    this->cseq_++, this->sessionId_,
                                    "Range: npt=0.000-\r\n");
        if (!this->sendRequest(req, &response)) return false;
        if (response.find("RTSP/1.0 200") == std::string::npos) {
            qWarning() << "[RtspClient] PLAY 실패:\n" << response.c_str();
            return false;
        }
    }

    return true;
}

// ============================================================================
// recvFull — 지정한 바이트 수만큼 반드시 수신 (내부 헬퍼)
// ============================================================================
static bool recvFull(int fd, void* buf, size_t len) {
    size_t received = 0;
    while (received < len) {
        ssize_t n = ::recv(fd, static_cast<char*>(buf) + received,
                           len - received, 0);
        if (n <= 0) return false;
        received += static_cast<size_t>(n);
    }
    return true;
}

// ============================================================================
// rtpLoop — SRTP 패킷 수신 (UDP 또는 TCP interleaved)
//
// SRTP 패킷 구조:
//   [RTP Header (12B)] [Encrypted Payload] [Auth Tag (10B)]
//
// 수신 후 인증 태그를 검증하고, 검증 성공 시 페이로드를 복호화한다.
// ============================================================================
void RtspClient::rtpLoop() {
    uint8_t buf[2048];

    while (this->running_.load()) {
        ssize_t n;
        size_t  srtpLen;

        if (this->rtpTcp_) {
            // TCP interleaved: $ + channel(1) + length(2) + SRTP packet
            uint8_t header[4];
            if (!recvFull(this->controlFd_, header, 4)) break;
            if (header[0] != '$') break;  // 프로토콜 오류
            // header[1] = channel (무시 — 단일 스트림)
            srtpLen = (static_cast<size_t>(header[2]) << 8) | header[3];
            if (srtpLen == 0 || srtpLen > sizeof(buf)) break;
            if (!recvFull(this->controlFd_, buf, srtpLen)) break;
            n = static_cast<ssize_t>(srtpLen);
        } else {
            // UDP
            n = ::recv(this->rtpFd_, buf, sizeof(buf), 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                if (errno == EINTR) continue;
                break;
            }
        }

        // SRTP 최소 크기: RTP 헤더(12) + 인증 태그(10)
        if (static_cast<size_t>(n) < 12 + kSrtpAuthTagLen) continue;

        size_t pktLen = static_cast<size_t>(n);

        // ── SRTP 인증 태그 검증 ──────────────────────────────────────────
        size_t rtpLen = pktLen - kSrtpAuthTagLen;
        const uint8_t* receivedTag = buf + rtpLen;

        unsigned int hmacLen = 0;
        uint8_t hmacBuf[20];  // SHA1 = 20 바이트
        HMAC(EVP_sha1(),
             this->srtpAuthKey_.data(),
             static_cast<int>(this->srtpAuthKey_.size()),
             buf, rtpLen,
             hmacBuf, &hmacLen);

        if (std::memcmp(hmacBuf, receivedTag, kSrtpAuthTagLen) != 0) {
            qWarning() << "[RtspClient] SRTP 인증 태그 불일치 — 패킷 폐기";
            continue;
        }

        // ── RTP 헤더 파싱 (인증 통과 후) ─────────────────────────────────
        uint8_t v0 = buf[0];
        int     cc = v0 & 0x0F;
        bool    x  = (v0 & 0x10) != 0;
        size_t  headerLen = 12 + static_cast<size_t>(cc) * 4;
        if (rtpLen < headerLen) continue;

        if (x) {
            if (rtpLen < headerLen + 4) continue;
            uint16_t extLen = (static_cast<uint16_t>(buf[headerLen + 2]) << 8) |
                              buf[headerLen + 3];
            headerLen += 4 + static_cast<size_t>(extLen) * 4;
            if (rtpLen < headerLen) continue;
        }

        uint8_t* payload = buf + headerLen;
        size_t   payloadSize = rtpLen - headerLen;
        if (payloadSize == 0) continue;
        this->srtpCipher_->decrypt(payload, payloadSize);
        this->handleRtpPayload(payload, payloadSize);
    }
}

// ============================================================================
// handleRtpPayload — RFC 6184 single-NAL / FU-A / STAP-A 분기 및 emit
// ============================================================================
void RtspClient::handleRtpPayload(const uint8_t* payload, size_t size) {
    const uint8_t nalType = payload[0] & 0x1F;

    if (nalType >= 1 && nalType <= 23) {
        // Single NAL unit — payload 전체가 하나의 NAL (헤더 바이트 포함).
        QByteArray nal(reinterpret_cast<const char*>(payload),
                       static_cast<int>(size));
        emit this->nalReceived(nal);
        return;
    }

    if (nalType == 24) {
        // STAP-A: [STAP-A header(1)] [size(2)][NAL][size(2)][NAL]...
        size_t offset = 1;
        while (offset + 2 <= size) {
            uint16_t nalSize = (static_cast<uint16_t>(payload[offset]) << 8) |
                                payload[offset + 1];
            offset += 2;
            if (offset + nalSize > size) break;
            QByteArray nal(reinterpret_cast<const char*>(payload + offset),
                           static_cast<int>(nalSize));
            emit this->nalReceived(nal);
            offset += nalSize;
        }
        return;
    }

    if (nalType == 28) {
        // FU-A 재조립.
        //   payload[0] = FU indicator (F|NRI|type=28)
        //   payload[1] = FU header    (S|E|R|original_type)
        if (size < 2) return;
        const uint8_t fuIndicator = payload[0];
        const uint8_t fuHeader    = payload[1];
        const bool    start = (fuHeader & 0x80) != 0;
        const bool    end   = (fuHeader & 0x40) != 0;
        const uint8_t type  = fuHeader & 0x1F;

        if (start) {
            // 원래 NAL 의 header 바이트를 재구성: F/NRI 는 FU indicator 에서,
            // type 은 FU header 에서 가져온다.
            this->fuBuffer_.clear();
            this->fuBuffer_.push_back(static_cast<uint8_t>(
                (fuIndicator & 0xE0) | (type & 0x1F)));
            this->fuInProgress_ = true;
        }
        if (!this->fuInProgress_) return;

        // 조각 본문(두 헤더 바이트 이후) 을 버퍼에 덧붙인다.
        this->fuBuffer_.insert(this->fuBuffer_.end(),
                                payload + 2, payload + size);

        if (end) {
            QByteArray nal(reinterpret_cast<const char*>(this->fuBuffer_.data()),
                           static_cast<int>(this->fuBuffer_.size()));
            emit this->nalReceived(nal);
            this->fuBuffer_.clear();
            this->fuInProgress_ = false;
        }
        return;
    }

    // 그 외 타입(FU-B, STAP-B 등) 은 이 구현 범위에서 무시한다.
}
