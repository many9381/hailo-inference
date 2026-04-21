#pragma once

#include <QByteArray>
#include <QObject>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include <memory>

#include "crypto/AriaCipher.h"
#include "crypto/ICipher.h"
#include "rtsp_native/MlKemHandshake.h"

// ----------------------------------------------------------------------------
// RtspClient
//
// RTSP/RTP H.264 client implemented using only POSIX sockets, without GStreamer dependencies.
// Like GstRtspClient, it delivers a single raw NAL unit with the Annex-B
// start code removed through the nalReceived(QByteArray) signal.
//
// Operation sequence
//   1. Parse the RTSP URL (host/port/path)
//   2. TCP connect -> signal in the order OPTIONS -> DESCRIBE -> SETUP -> PLAY
//   3. In SETUP, bind the client-side RTP UDP port and place it in the Transport header
//   4. After PLAY, a separate thread reads UDP packets, strips the RTP header,
//      parses single NAL / FU-A payloads, and emits nalReceived per NAL unit
//
// The caller only needs to call start(url), and stop() cleans up all sockets/threads.
// ----------------------------------------------------------------------------
class RtspClient : public QObject {
    Q_OBJECT

public:
    explicit RtspClient(bool rtpTcp = false, QObject* parent = nullptr);
    ~RtspClient() override;

    RtspClient(const RtspClient&) = delete;
    RtspClient& operator=(const RtspClient&) = delete;

    // url: in the form "rtsp://host[:port]/path".
    bool start(const std::string& url);
    void stop();

signals:
    // A single raw NAL unit without an Annex-B start code.
    // Format: [NAL header (1B) | RBSP ...].
    void nalReceived(const QByteArray& nal);

private:
    // ── URL/signaling helpers ───────────────────────────────────────────
    bool parseUrl(const std::string& url);
    bool openControl();
    bool performKemHandshake();                     // ML-KEM-based key exchange
    bool bindRtpSocket();                           // Allocate the client RTP UDP port (UDP only)
    bool sendRequest(const std::string& req,
                     std::string* response);        // Send one request and receive the response
    bool performSignaling();                        // OPTIONS -> DESCRIBE -> SETUP -> PLAY

    // ── RTP receive loop ────────────────────────────────────────────────
    void rtpLoop();

    // Demux the RTP payload into NAL units (single NAL / FU-A / STAP-A).
    void handleRtpPayload(const uint8_t* payload, size_t size);

    // ── URL / session state ─────────────────────────────────────────────
    std::string host_;
    int         port_ = 554;
    std::string path_;         // Mount point such as "/stream"
    std::string baseUrl_;      // Used as a Content-Base substitute (original URL)
    std::string sessionId_;    // Session header value
    int         cseq_ = 1;     // RTSP CSeq counter

    // ── Sockets / thread ────────────────────────────────────────────────
    int               controlFd_ = -1;  // RTSP TCP control socket
    int               rtpFd_     = -1;  // RTP UDP socket (UDP only)
    uint16_t          clientRtpPort_ = 0;
    bool              rtpTcp_    = false;  // Whether TCP interleaved transport is used
    std::thread       rtpThread_;
    std::atomic<bool> running_{false};

    // ── FU-A reassembly buffer ──────────────────────────────────────────
    // FU-A is a mode where one NAL arrives split across multiple RTP packets, so a reassembly buffer is required.
    std::vector<uint8_t> fuBuffer_;
    bool                 fuInProgress_ = false;

    // ── SRTP decryption + authentication verification (keys derived from the ML-KEM handshake) ──
    static constexpr size_t kSrtpAuthTagLen = 10;  // RFC 3711: 80-bit truncated HMAC-SHA1

    std::unique_ptr<ICipher> srtpCipher_;     // ML-KEM-derived SRTP encryption key
    std::unique_ptr<ICipher> rtspCipher_;     // ML-KEM-derived RTSP control-message encryption key
    std::vector<uint8_t>     srtpAuthKey_;    // ML-KEM-derived SRTP authentication key
};
