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
#include "crypto/hmac/HmacSha1.h"
#include "crypto/ICipher.h"
#include "rtsp_native/TlsHandshake.h"

// ----------------------------------------------------------------------------
// RtspServer
//
// GStreamer 의존성 없이 POSIX 소켓만 사용해 직접 구현한 RTSP/RTP H.264 서버.
// gst-rtsp-server 기반 GstRtspServer 와 정확히 동일한 공개 API 를 제공하므로
// 호출자 입장에서 드롭-인 교체가 가능하다.
//
// 내부 구조
//   ┌─────────────────────────┐     ┌──────────────────────────┐
//   │ accept thread           │     │ 세션 스레드 (클라이언트당) │
//   │  - TCP listen/accept    │─►──▶│  - RTSP 요청 수신/응답     │
//   │  - 세션 생성             │     │  - SETUP 시 UDP 소켓 바인드│
//   └─────────────────────────┘     └──────────────────────────┘
//                                          ▲
//                                          │ sendNal()
//                                          │ (encoder thread)
//                                          │
//                               ┌──────────────────┐
//                               │ RTP 패킷타이저    │
//                               │ - AU → NAL 분해  │
//                               │ - Single / FU-A  │
//                               │ - sendto(UDP)    │
//                               └──────────────────┘
//
// sendNal() 한 번 = 한 access unit(byte-stream Annex-B, start code 포함). 내부
// 에서 NAL 단위로 쪼갠 뒤 RFC 6184 packetization-mode 1 규칙에 따라 각 PLAY
// 세션의 클라이언트 RTP 포트로 전송한다.
// ----------------------------------------------------------------------------
class RtspServer {
public:
    RtspServer(int port, std::string mountPoint, int width, int height, int fps,
               bool rtpTcp = false);
    ~RtspServer();

    RtspServer(const RtspServer&) = delete;
    RtspServer& operator=(const RtspServer&) = delete;

    // RTSP TCP 리스너 및 accept 스레드를 가동한다.
    bool start();

    // 서버를 정지하고 모든 세션/스레드/소켓을 정리한다. (소멸자에서도 자동 호출)
    void stop();

    // 한 frame 의 access unit(Annex-B byte-stream) 을 모든 PLAY 세션에 전송.
    // 여러 NAL 이 [start code|NAL][start code|NAL]... 형태로 연속되어 있어야 한다.
    // PLAY 중인 세션이 없거나 stop 상태면 조용히 무시한다.
    void sendNal(const uint8_t* nalData, size_t nalSize);

private:
    // Session 은 세션별 상태(소켓, 주소, 시퀀스 카운터 등) 를 담는 내부 구조체.
    // 헤더에는 전방 선언만 두고 실제 정의는 .cpp 에 있다. (sockaddr_in 을 헤더에
    // 노출시키지 않기 위함)
    struct Session;

    // accept 스레드 본체 — poll() 로 종료 플래그를 주기적으로 확인한다.
    void acceptLoop();

    // 세션 스레드 본체 — RTSP 요청을 파싱하고 handleRequest() 로 위임한다.
    void sessionLoop(std::shared_ptr<Session> session);

    // 한 개의 완전한 RTSP 요청을 처리한다. false 를 반환하면 세션을 종료한다.
    bool handleRequest(Session& s, const std::string& req);

    // ── 응답 빌더 ────────────────────────────────────────────────────────
    std::string buildOptionsResponse(int cseq);
    std::string buildDescribeResponse(int cseq, const std::string& uri);
    std::string buildSetupResponse(int cseq, const std::string& sessionId,
                                   const std::string& transport);
    std::string buildPlayResponse(int cseq, const std::string& sessionId);
    std::string buildTeardownResponse(int cseq, const std::string& sessionId);
    std::string buildGenericOkResponse(int cseq, const std::string& sessionId);
    std::string buildErrorResponse(int cseq, int code, const std::string& reason);

    // ── TLS 핸드셰이크 ──────────────────��─────────────────────────────────
    bool performTlsHandshake(Session& s);

    // ── RTSP 암호화 송수신 헬퍼 ─────────────────────────────────────────
    // [4-byte network-order length][encrypted payload] 프레이밍으로 송수신.
    // 세션별 RTSP cipher 를 사용한다.
    bool sendEncrypted(Session& s, const std::string& data);
    bool sendEncryptedLocked(Session& s, const std::string& data);
    bool recvEncrypted(Session& s, std::string& data);

    // ── RTP 패킷화 헬퍼 ──────────────────────────────────────────────────
    // 한 NAL 을 Single-NAL-unit 또는 FU-A 모드로 여러 개의 RTP 패킷에 나눠 전송.
    void sendNalToSession(Session& s, uint32_t rtpTs,
                          const uint8_t* nal, size_t size, bool lastNal);

    // 한 개의 RTP 패킷을 UDP 로 송신.
    void sendRtpPacket(Session& s, uint32_t rtpTs, bool marker,
                       const uint8_t* payload, size_t size);

    // ── 설정값 ────────────────────────────────────────────────────────────
    int         port_;
    std::string mountPoint_;
    int         width_;   // SDP/로그 출력용
    int         height_;
    int         fps_;
    bool        rtpTcp_;  // true: TCP interleaved, false: UDP

    // ── 소켓/스레드 ──────────────────────────────────────────────────────
    int               listenFd_ = -1;
    std::thread       acceptThread_;
    std::atomic<bool> running_{false};

    // ── 세션 관리 ────────────────────────────────────────────────────────
    // sessionsMu_ 는 sessions_/sessionThreads_ 및 각 Session 의 소켓/전송 상태
    // (udpFd, clientRtpAddr, seq) 를 함께 보호한다.
    std::mutex                            sessionsMu_;
    std::vector<std::shared_ptr<Session>> sessions_;
    std::vector<std::thread>              sessionThreads_;

    // ── 전송 카운터 ──────────────────────────────────────────────────────
    uint64_t frameIndex_ = 0;  // 90kHz RTP 타임스탬프 계산용 frame 카운터

    // ── SRTP 인증 태그 길이 ─────────────────────────────────────────────
    static constexpr size_t kSrtpAuthTagLen = 10;  // RFC 3711: 80-bit truncated HMAC-SHA1
};
