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

#include "rtsp_native/MlKemHandshake.h"

// ============================================================================
// Constructor / destructor
// ============================================================================
RtspClient::RtspClient(bool rtpTcp, QObject* parent)
    : QObject(parent), rtpTcp_(rtpTcp) {}

RtspClient::~RtspClient() {
    this->stop();
}

// ============================================================================
// start - URL parsing -> control connection -> RTP bind -> signaling -> start receive loop
// ============================================================================
bool RtspClient::start(const std::string& url) {
    this->stop();

    if (!this->parseUrl(url)) {
        qWarning() << "[RtspClient] Failed to parse URL:" << url.c_str();
        return false;
    }
    this->baseUrl_ = url;

    if (!this->openControl()) return false;

    // ML-KEM handshake - perform key exchange before RTSP signaling
    if (!this->performKemHandshake()) {
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

    // In TCP mode, remove the recv timeout from controlFd_
    // so interleaved RTP reception is not blocked.
    // (rtpLoop termination is handled by the running_ flag + socket close.)
    if (this->rtpTcp_) {
        timeval tv{};
        tv.tv_sec  = 0;
        tv.tv_usec = 0;  // Wait indefinitely
        ::setsockopt(this->controlFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    this->running_.store(true);
    this->rtpThread_ = std::thread([this] { this->rtpLoop(); });

    qInfo() << "[RtspClient] Connection started:" << url.c_str();
    return true;
}

// ============================================================================
// stop - clean up sockets after stopping the receive thread
// ============================================================================
void RtspClient::stop() {
    this->running_.store(false);
    if (this->rtpThread_.joinable()) this->rtpThread_.join();

    if (this->rtpFd_ >= 0) {
        ::close(this->rtpFd_);
        this->rtpFd_ = -1;
    }
    if (this->controlFd_ >= 0) {
        // Ideally TEARDOWN would be sent, but timeout/error handling is cumbersome,
        // so only the socket is closed here. Cleanup happens via the server-side session timeout.
        ::close(this->controlFd_);
        this->controlFd_ = -1;
    }

    this->sessionId_.clear();
    this->cseq_         = 1;
    this->fuBuffer_.clear();
    this->fuInProgress_ = false;

    // Reset ML-KEM-derived keys
    this->srtpCipher_.reset();
    this->rtspCipher_.reset();
    this->srtpAuthKey_.clear();
}

// ============================================================================
// parseUrl - split "rtsp://host[:port]/path"
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
// openControl - TCP connect + host resolution via getaddrinfo
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
        qWarning() << "[RtspClient] getaddrinfo failed:" << gai_strerror(gai);
        return false;
    }

    int fd = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (fd < 0) {
        ::freeaddrinfo(res);
        qWarning() << "[RtspClient] socket failed:" << std::strerror(errno);
        return false;
    }
    if (::connect(fd, res->ai_addr, res->ai_addrlen) < 0) {
        qWarning() << "[RtspClient] connect failed:" << std::strerror(errno);
        ::close(fd);
        ::freeaddrinfo(res);
        return false;
    }
    ::freeaddrinfo(res);

    // Apply send/recv timeouts so stop() does not block for too long on network failures.
    timeval tv{};
    tv.tv_sec  = 2;
    tv.tv_usec = 0;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    this->controlFd_ = fd;
    return true;
}

// ============================================================================
// performKemHandshake - perform ML-KEM-based key exchange and configure the client-side keys
// ============================================================================
bool RtspClient::performKemHandshake() {
    MlKemHandshake hs;
    if (!hs.performClientHandshake(this->controlFd_)) {
        qWarning() << "[RtspClient] ML-KEM handshake failed";
        return false;
    }

    this->srtpCipher_ = hs.createSrtpCipher();
    this->rtspCipher_ = hs.createRtspCipher();
    this->srtpAuthKey_ = hs.srtpAuthKey();

    qInfo() << "[RtspClient] ML-KEM handshake completed";
    return true;
}

// ============================================================================
// bindRtpSocket - create the UDP socket used to receive client RTP (temporary port assigned automatically)
// ============================================================================
bool RtspClient::bindRtpSocket() {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        qWarning() << "[RtspClient] UDP socket failed:" << std::strerror(errno);
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = 0;  // Let the kernel choose
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        qWarning() << "[RtspClient] UDP bind failed:" << std::strerror(errno);
        ::close(fd);
        return false;
    }
    socklen_t addrLen = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &addrLen);
    this->clientRtpPort_ = ntohs(addr.sin_port);

    // RTP receive timeout - used to check the running_ flag periodically.
    timeval tv{};
    tv.tv_sec  = 0;
    tv.tv_usec = 300 * 1000;  // 300ms
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    this->rtpFd_ = fd;
    return true;
}

// ============================================================================
// sendRequest - send one RTSP request and receive one response message
// ============================================================================
bool RtspClient::sendRequest(const std::string& req, std::string* response) {
    // ── Encrypt with the ML-KEM-derived RTSP cipher and send with a length prefix ──
    std::vector<uint8_t> encBuf(req.begin(), req.end());
    this->rtspCipher_->encrypt(encBuf.data(), encBuf.size());

    uint32_t netLen = htonl(static_cast<uint32_t>(encBuf.size()));
    if (::send(this->controlFd_, &netLen, 4, 0) != 4) {
        qWarning() << "[RtspClient] Failed to send length";
        return false;
    }
    {
        size_t sent = 0;
        while (sent < encBuf.size()) {
            ssize_t n = ::send(this->controlFd_, encBuf.data() + sent,
                               encBuf.size() - sent, 0);
            if (n <= 0) {
                qWarning() << "[RtspClient] send failed:" << std::strerror(errno);
                return false;
            }
            sent += static_cast<size_t>(n);
        }
    }

    // ── Receive the encrypted response using length-prefix framing ─────────────────────────
    uint32_t respNetLen = 0;
    {
        size_t received = 0;
        while (received < 4) {
            ssize_t n = ::recv(this->controlFd_,
                               reinterpret_cast<char*>(&respNetLen) + received,
                               4 - received, 0);
            if (n <= 0) {
                qWarning() << "[RtspClient] Failed to receive length";
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
                qWarning() << "[RtspClient] recv failed:" << std::strerror(errno);
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
// performSignaling - communicate in the order OPTIONS -> DESCRIBE -> SETUP -> PLAY
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
            qWarning() << "[RtspClient] OPTIONS failed";
            return false;
        }
    }

    // ── DESCRIBE ─────────────────────────────────────────────────────────
    {
        std::string req = makeLine("DESCRIBE", this->baseUrl_,
                                    this->cseq_++, "", "Accept: application/sdp\r\n");
        if (!this->sendRequest(req, &response)) return false;
        if (response.find("RTSP/1.0 200") == std::string::npos) {
            qWarning() << "[RtspClient] DESCRIBE failed:\n" << response.c_str();
            return false;
        }
    }

    // For SETUP, sending to <baseUrl>/trackID=0 using the control attribute
    // is the most compatible approach. Even if the server advertises a different control,
    // we try the fixed URL to match the common "a=control:trackID=0" convention.
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
            qWarning() << "[RtspClient] SETUP failed:\n" << response.c_str();
            return false;
        }
        // Parse the Session header.
        size_t sp = response.find("Session:");
        if (sp == std::string::npos) sp = response.find("session:");
        if (sp != std::string::npos) {
            size_t lineEnd = response.find("\r\n", sp);
            std::string line = response.substr(sp + 8, lineEnd - (sp + 8));
            // Trim leading whitespace
            size_t start = line.find_first_not_of(" \t");
            if (start != std::string::npos) line = line.substr(start);
            size_t semi = line.find(';');
            this->sessionId_ = (semi == std::string::npos) ? line : line.substr(0, semi);
        }
        if (this->sessionId_.empty()) {
            qWarning() << "[RtspClient] No Session found in SETUP response";
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
            qWarning() << "[RtspClient] PLAY failed:\n" << response.c_str();
            return false;
        }
    }

    return true;
}

// ============================================================================
// recvFull - receive exactly the specified number of bytes (internal helper)
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
// rtpLoop - receive SRTP packets (UDP or TCP interleaved)
//
// SRTP packet structure:
//   [RTP Header (12B)] [Encrypted Payload] [Auth Tag (10B)]
//
// After receiving, verify the authentication tag and decrypt the payload if verification succeeds.
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
            if (header[0] != '$') break;  // Protocol error
            // header[1] = channel (ignored - single stream)
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

        // Minimum SRTP size: RTP header (12) + auth tag (10)
        if (static_cast<size_t>(n) < 12 + kSrtpAuthTagLen) continue;

        size_t pktLen = static_cast<size_t>(n);

        // ── SRTP authentication tag verification ─────────────────────────────────
        size_t rtpLen = pktLen - kSrtpAuthTagLen;
        const uint8_t* receivedTag = buf + rtpLen;

        unsigned int hmacLen = 0;
        uint8_t hmacBuf[20];  // SHA1 = 20 bytes
        HMAC(EVP_sha1(),
             this->srtpAuthKey_.data(),
             static_cast<int>(this->srtpAuthKey_.size()),
             buf, rtpLen,
             hmacBuf, &hmacLen);

        if (std::memcmp(hmacBuf, receivedTag, kSrtpAuthTagLen) != 0) {
            qWarning() << "[RtspClient] SRTP authentication tag mismatch - dropping packet";
            continue;
        }

        // ── RTP header parsing (after authentication passes) ─────────────────────
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
// handleRtpPayload - branch and emit RFC 6184 single-NAL / FU-A / STAP-A payloads
// ============================================================================
void RtspClient::handleRtpPayload(const uint8_t* payload, size_t size) {
    const uint8_t nalType = payload[0] & 0x1F;

    if (nalType >= 1 && nalType <= 23) {
        // Single NAL unit - the entire payload is one NAL (including the header byte).
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
        // FU-A reassembly.
        //   payload[0] = FU indicator (F|NRI|type=28)
        //   payload[1] = FU header    (S|E|R|original_type)
        if (size < 2) return;
        const uint8_t fuIndicator = payload[0];
        const uint8_t fuHeader    = payload[1];
        const bool    start = (fuHeader & 0x80) != 0;
        const bool    end   = (fuHeader & 0x40) != 0;
        const uint8_t type  = fuHeader & 0x1F;

        if (start) {
            // Reconstruct the original NAL header byte: F/NRI comes from the FU indicator,
            // and type comes from the FU header.
            this->fuBuffer_.clear();
            this->fuBuffer_.push_back(static_cast<uint8_t>(
                (fuIndicator & 0xE0) | (type & 0x1F)));
            this->fuInProgress_ = true;
        }
        if (!this->fuInProgress_) return;

        // Append the fragment body (after the two header bytes) to the buffer.
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

    // Other types (FU-B, STAP-B, etc.) are ignored in this implementation.
}
