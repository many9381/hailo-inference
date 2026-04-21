#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <memory>

#include "crypto/AriaCipher.h"
#include "crypto/ICipher.h"
#include "rtsp_native/MlKemHandshake.h"

// ----------------------------------------------------------------------------
// RtspServer
//
// RTSP/RTP H.264 server implemented directly with POSIX sockets, without GStreamer dependencies.
// It provides exactly the same public API as a gst-rtsp-server-based GstRtspServer,
// so it can be used as a drop-in replacement from the caller's perspective.
//
// Internal structure
//   ┌─────────────────────────┐     ┌──────────────────────────┐
//   │ accept thread           │     │ session thread (per client)│
//   │  - TCP listen/accept    │─►──▶│  - receive/respond RTSP req│
//   │  - create session       │     │  - bind UDP socket on SETUP│
//   └─────────────────────────┘     └──────────────────────────┘
//                                          ▲
//                                          │ sendNal()
//                                          │ (encoder thread)
//                                          │
//                               ┌──────────────────┐
//                               │ RTP packetizer    │
//                               │ - AU -> NAL split │
//                               │ - Single / FU-A  │
//                               │ - sendto(UDP)    │
//                               └──────────────────┘
//
// One sendNal() call = one access unit (byte-stream Annex-B, including start codes). Internally,
// it is split into NAL units and transmitted to each PLAY session's client RTP
// port according to RFC 6184 packetization-mode 1 rules.
// ----------------------------------------------------------------------------
class RtspServer {
public:
    RtspServer(int port, std::string mountPoint, int width, int height, int fps,
               bool rtpTcp = false);
    ~RtspServer();

    RtspServer(const RtspServer&) = delete;
    RtspServer& operator=(const RtspServer&) = delete;

    // Start the RTSP TCP listener and accept thread.
    bool start();

    // Stop the server and clean up all sessions/threads/sockets. (Also called automatically by the destructor.)
    void stop();

    // Send one frame's access unit (Annex-B byte stream) to all PLAY sessions.
    // Multiple NALs must be laid out continuously as [start code|NAL][start code|NAL]...
    // Silently ignore it if there are no PLAY sessions or the server is stopped.
    void sendNal(const uint8_t* nalData, size_t nalSize);

private:
    // Session is an internal struct that stores per-session state (sockets, addresses, sequence counters, etc.).
    // Only a forward declaration is kept in the header, and the actual definition is in the .cpp file
    // (to avoid exposing sockaddr_in in the header).
    struct Session;

    // Accept-thread body - periodically checks the shutdown flag with poll().
    void acceptLoop();

    // Session-thread body - parses RTSP requests and delegates to handleRequest().
    void sessionLoop(std::shared_ptr<Session> session);

    // Handle one complete RTSP request. If false is returned, the session is terminated.
    bool handleRequest(Session& s, const std::string& req);

    // ── Response builders ───────────────────────────────────────────────
    std::string buildOptionsResponse(int cseq);
    std::string buildDescribeResponse(int cseq, const std::string& uri);
    std::string buildSetupResponse(int cseq, const std::string& sessionId,
                                   const std::string& transport);
    std::string buildPlayResponse(int cseq, const std::string& sessionId);
    std::string buildTeardownResponse(int cseq, const std::string& sessionId);
    std::string buildGenericOkResponse(int cseq, const std::string& sessionId);
    std::string buildErrorResponse(int cseq, int code, const std::string& reason);

    // ── ML-KEM handshake ────────────────────────────────────────────────
    bool performKemHandshake(Session& s);

    // ── RTSP encrypted send/receive helpers ─────────────────────────────
    // Send/receive using [4-byte network-order length][encrypted payload] framing.
    // Uses the per-session RTSP cipher.
    bool sendEncrypted(Session& s, const std::string& data);
    bool sendEncryptedLocked(Session& s, const std::string& data);
    bool recvEncrypted(Session& s, std::string& data);

    // ── RTP packetization helpers ───────────────────────────────────────
    // Send one NAL split across multiple RTP packets in Single-NAL-unit or FU-A mode.
    void sendNalToSession(Session& s, uint32_t rtpTs,
                          const uint8_t* nal, size_t size, bool lastNal);

    // Send one RTP packet over UDP.
    void sendRtpPacket(Session& s, uint32_t rtpTs, bool marker,
                       const uint8_t* payload, size_t size);

    // ── Configuration values ────────────────────────────────────────────
    int         port_;
    std::string mountPoint_;
    int         width_;   // For SDP/log output
    int         height_;
    int         fps_;
    bool        rtpTcp_;  // true: TCP interleaved, false: UDP

    // ── Sockets/threads ─────────────────────────────────────────────────
    int               listenFd_ = -1;
    std::thread       acceptThread_;
    std::atomic<bool> running_{false};

    // ── Session management ──────────────────────────────────────────────
    // sessionsMu_ protects sessions_/sessionThreads_ along with each Session's
    // socket/transport state (udpFd, clientRtpAddr, seq).
    std::mutex                            sessionsMu_;
    std::vector<std::shared_ptr<Session>> sessions_;
    std::vector<std::thread>              sessionThreads_;

    // ── Transmission counter ────────────────────────────────────────────
    uint64_t frameIndex_ = 0;  // Frame counter used to compute the 90 kHz RTP timestamp

    // ── SRTP authentication tag length ──────────────────────────────────
    static constexpr size_t kSrtpAuthTagLen = 10;  // RFC 3711: 80-bit truncated HMAC-SHA1
};
