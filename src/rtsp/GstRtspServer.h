#pragma once

// Qt 의 signals 매크로와 GLib/GIO 의 "signals" 멤버 이름이 충돌하므로
// GLib 타입들을 전방 선언(forward declaration)만 하고 실제 include 는 .cpp 에서만 한다.

// GLib / GStreamer / GstRTSP 전방 선언
// (GLib 의 typedef struct _X X; 패턴과 동일)
typedef struct _GstRTSPServer     GstRTSPServer;
typedef struct _GstRTSPMediaFactory GstRTSPMediaFactory;
typedef struct _GstRTSPMedia      GstRTSPMedia;
typedef struct _GstElement        GstElement;
typedef struct _GMainLoop         GMainLoop;
typedef struct _GMainContext      GMainContext;

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

// ----------------------------------------------------------------------------
// GstRtspServer
//
// gst-rtsp-server 라이브러리를 이용한 RTSP/RTP H.264 서버.
//
// sendNal() 로 한 frame 분량의 access unit(byte-stream, Annex-B start code
// 포함)을 밀어 넣으면 내부 파이프라인을 거쳐 연결된 RTSP 클라이언트에게
// RTP 스트림으로 전달한다.
//
// 내부 미디어 파이프라인 (클라이언트 최초 연결 시 생성):
//   appsrc → h264parse(config-interval=-1) → rtph264pay(pay0, pt=96)
//
// shared=TRUE 이므로 모든 클라이언트가 동일한 미디어(동일한 appsrc)를 공유한다.
// RTSP 시그널링은 별도 GLib 메인 루프 스레드에서 처리한다.
// ----------------------------------------------------------------------------
class GstRtspServer {
public:
    GstRtspServer(int port, std::string mountPoint, int width, int height, int fps);
    ~GstRtspServer();

    GstRtspServer(const GstRtspServer&) = delete;
    GstRtspServer& operator=(const GstRtspServer&) = delete;

    // RTSP 서버를 시작한다. GLib 메인 루프를 백그라운드 스레드에서 가동한다.
    bool start();

    // 서버를 정지하고 모든 리소스를 해제한다. (소멸자에서도 자동 호출)
    void stop();

    // 한 frame 의 access unit(byte-stream, Annex-B start code 포함)을 받아
    // RTP 로 송출. 여러 NAL 이 [start code|NAL][start code|NAL]... 형태로
    // 연속되어 한 buffer 로 들어와야 한다.
    // PLAY 중인 클라이언트가 없거나 stop 상태면 조용히 무시한다.
    void sendNal(const uint8_t* nalData, size_t nalSize);

private:
    // GstRTSPMediaFactory "media-configure" 시그널:
    // 첫 클라이언트 연결 시 미디어가 생성될 때 appsrc 핸들을 확보한다.
    static void onMediaConfigure(GstRTSPMediaFactory* factory,
                                  GstRTSPMedia*         media,
                                  void*                 userData);

    // ── 설정값 ────────────────────────────────────────────────────────────
    int         port_;
    std::string mountPoint_;
    int         width_;   // SDP / 로그 출력용
    int         height_;
    int         fps_;

    // ── GLib / GstRTSP 핸들 ───────────────────────────────────────────────
    GstRTSPServer* server_      = nullptr;
    GMainContext*  mainContext_ = nullptr;
    GMainLoop*     mainLoop_    = nullptr;
    std::thread    mainLoopThread_;
    std::atomic<bool> running_  = false;

    // ── appsrc 핸들 (onMediaConfigure 에서 설정, sendNal 에서 사용) ───────
    // 두 스레드(GLib 루프 스레드 ↔ sendNal 호출 스레드)가 동시에 접근하므로 뮤텍스 보호.
    std::mutex  appsrcMutex_;
    GstElement* appsrc_     = nullptr;
    uint64_t    frameIndex_ = 0;  // VCL NAL 단위 PTS 카운터
};
