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
#include "rtsp_native/TlsHandshake.h"

// ----------------------------------------------------------------------------
// RtspClient
//
// GStreamer 의존성 없이 POSIX 소켓만으로 구현한 RTSP/RTP H.264 클라이언트.
// GstRtspClient 와 동일하게 nalReceived(QByteArray) 시그널을 통해 Annex-B
// start code 가 제거된 raw NAL 한 단위를 전달한다.
//
// 동작 순서
//   1. RTSP URL(host/port/path) 파싱
//   2. TCP 연결 → OPTIONS → DESCRIBE → SETUP → PLAY 순서로 시그널링
//   3. SETUP 에서 클라이언트측 RTP UDP 포트를 바인드하고 Transport 헤더에 싣는다
//   4. PLAY 이후 별도 스레드에서 UDP 패킷을 읽어 RTP 헤더를 벗겨내고
//      single NAL / FU-A 페이로드를 파싱해 NAL 단위로 nalReceived emit
//
// 호출자는 start(url) 만 호출하면 되며, stop() 에서 모든 소켓/스레드가 정리된다.
// ----------------------------------------------------------------------------
class RtspClient : public QObject {
    Q_OBJECT

public:
    explicit RtspClient(bool rtpTcp = false, QObject* parent = nullptr);
    ~RtspClient() override;

    RtspClient(const RtspClient&) = delete;
    RtspClient& operator=(const RtspClient&) = delete;

    // url: "rtsp://host[:port]/path" 형식.
    bool start(const std::string& url);
    void stop();

signals:
    // Annex-B start code 가 없는 raw NAL 한 단위.
    // [NAL header(1B) | RBSP ...] 형태이다.
    void nalReceived(const QByteArray& nal);

private:
    // ── URL/시그널링 헬퍼 ────────────────────────────────────────────────
    bool parseUrl(const std::string& url);
    bool openControl();
    bool performTlsHandshake();                     // TLS 1.3 키 교환
    bool bindRtpSocket();                           // 클라이언트 RTP UDP 포트 할당 (UDP 전용)
    bool sendRequest(const std::string& req,
                     std::string* response);        // 한 요청을 보내고 응답 수신
    bool performSignaling();                        // OPTIONS → DESCRIBE → SETUP → PLAY

    // ── RTP 수신 루프 ────────────────────────────────────────────────────
    void rtpLoop();

    // RTP 페이로드를 NAL 단위로 디먹스 (single NAL / FU-A / STAP-A).
    void handleRtpPayload(const uint8_t* payload, size_t size);

    // ── URL / 세션 상태 ──────────────────────────────────────────────────
    std::string host_;
    int         port_ = 554;
    std::string path_;         // "/stream" 등 마운트 포인트
    std::string baseUrl_;      // Content-Base 대체용 (원본 URL)
    std::string sessionId_;    // Session 헤더 값
    int         cseq_ = 1;     // RTSP CSeq 카운터

    // ── 소켓 / 스레드 ────────────────────────────────────────────────────
    int               controlFd_ = -1;  // RTSP TCP 제어 소켓
    int               rtpFd_     = -1;  // RTP UDP 소켓 (UDP 전용)
    uint16_t          clientRtpPort_ = 0;
    bool              rtpTcp_    = false;  // TCP interleaved 전송 여부
    std::thread       rtpThread_;
    std::atomic<bool> running_{false};

    // ── FU-A 재조립 버퍼 ─────────────────────────────────────────────────
    // FU-A 는 하나의 NAL 이 여러 RTP 패킷으로 나뉘어 오는 모드이므로 재조립 버퍼가 필요.
    std::vector<uint8_t> fuBuffer_;
    bool                 fuInProgress_ = false;

    // ── SRTP 복호화 + 인증 검증 (TLS 핸드셰이크에서 키 유도) ──────────
    static constexpr size_t kSrtpAuthTagLen = 10;  // RFC 3711: 80-bit truncated HMAC-SHA1

    std::unique_ptr<ICipher> srtpCipher_;     // TLS 유도 SRTP 암호화 키
    std::unique_ptr<ICipher> rtspCipher_;     // TLS 유도 RTSP 제어 메시지 암호화 키
    std::vector<uint8_t>     srtpAuthKey_;    // TLS 유도 SRTP 인증 키
};
