#include "GstRtspServer.h"

// GLib/GStreamer 헤더는 Qt 헤더보다 먼저 include 해야
// Qt 의 signals 매크로와 GLib "signals" 멤버 이름 충돌을 피할 수 있다.
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-media-factory.h>
#include <gst/rtsp-server/rtsp-media.h>
#include <gst/rtsp-server/rtsp-mount-points.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <QDebug>

#include <cstring>
#include <string>

// ============================================================================
// 생성자 / 소멸자
// ============================================================================
GstRtspServer::GstRtspServer(int port, std::string mountPoint,
                               int width, int height, int fps)
    : port_(port), mountPoint_(std::move(mountPoint)),
      width_(width), height_(height), fps_(fps) {
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }
}

GstRtspServer::~GstRtspServer() {
    this->stop();
}

// ============================================================================
// start
// ============================================================================
bool GstRtspServer::start() {
    this->stop();

    // 전용 GLib 컨텍스트/루프를 만들어 기존 Qt/GStreamer 기본 컨텍스트와 격리한다.
    this->mainContext_ = g_main_context_new();
    this->mainLoop_    = g_main_loop_new(this->mainContext_, FALSE);

    this->server_ = gst_rtsp_server_new();
    gst_rtsp_server_set_service(this->server_,
                                std::to_string(this->port_).c_str());

    // ── 미디어 팩토리 설정 ──────────────────────────────────────────────
    // 파이프라인: appsrc(byte-stream NAL 입력) → h264parse → rtph264pay(RTP 패킷화)
    // shared=TRUE: 모든 클라이언트가 동일한 미디어(동일한 appsrc)를 공유.
    // config-interval=-1: SPS/PPS 를 모든 IDR 프레임에 인라인으로 삽입
    //                     → GOP 중간에 연결한 클라이언트도 첫 IDR 부터 복원 가능.
    GstRTSPMountPoints*  mounts  = gst_rtsp_server_get_mount_points(this->server_);
    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();

    gst_rtsp_media_factory_set_launch(factory,
        "( appsrc name=src is-live=true format=time "
        "  caps=\"video/x-h264,stream-format=byte-stream,alignment=au\" ! "
        "  h264parse config-interval=-1 ! "
        "  rtph264pay name=pay0 pt=96 config-interval=-1 )");

    gst_rtsp_media_factory_set_shared(factory, TRUE);

    // 미디어 생성 시 appsrc 핸들을 얻기 위한 콜백.
    g_signal_connect(factory, "media-configure",
                     G_CALLBACK(&GstRtspServer::onMediaConfigure), this);

    gst_rtsp_mount_points_add_factory(mounts, this->mountPoint_.c_str(), factory);
    g_object_unref(mounts);  // mount points 는 server 가 소유

    // 서버를 전용 컨텍스트에 attach.
    if (gst_rtsp_server_attach(this->server_, this->mainContext_) == 0) {
        qWarning() << "[GstRtspServer] 서버 attach 실패";
        g_main_loop_unref(this->mainLoop_);
        this->mainLoop_ = nullptr;
        g_main_context_unref(this->mainContext_);
        this->mainContext_ = nullptr;
        g_object_unref(this->server_);
        this->server_ = nullptr;
        return false;
    }

    this->running_.store(true);
    this->mainLoopThread_ = std::thread([this]() {
        g_main_loop_run(this->mainLoop_);
    });

    qInfo() << "[GstRtspServer] 시작: rtsp://<host>:"
            << this->port_ << this->mountPoint_.c_str()
            << "  (" << this->width_ << "x" << this->height_
            << " @" << this->fps_ << "fps)";
    return true;
}

// ============================================================================
// stop
// ============================================================================
void GstRtspServer::stop() {
    this->running_.store(false);

    // GLib 루프를 종료하고 스레드가 완전히 끝나길 기다린다.
    // 이후에는 GLib 콜백이 더 이상 실행되지 않으므로 appsrc 에 직접 접근 가능.
    if (this->mainLoop_) {
        g_main_loop_quit(this->mainLoop_);
    }
    if (this->mainLoopThread_.joinable()) {
        this->mainLoopThread_.join();
    }

    // appsrc 에 EOS 를 보내 인코딩 파이프라인을 깔끔히 종료한다.
    if (this->appsrc_) {
        gst_app_src_end_of_stream(GST_APP_SRC(this->appsrc_));
        gst_object_unref(this->appsrc_);
        this->appsrc_ = nullptr;
    }

    if (this->server_) {
        g_object_unref(this->server_);
        this->server_ = nullptr;
    }
    if (this->mainLoop_) {
        g_main_loop_unref(this->mainLoop_);
        this->mainLoop_ = nullptr;
    }
    if (this->mainContext_) {
        g_main_context_unref(this->mainContext_);
        this->mainContext_ = nullptr;
    }

    this->frameIndex_ = 0;
}

// ============================================================================
// sendNal: 한 access unit(byte-stream, start code 포함) → appsrc push
//
// 호출자(H264Encoder) 가 한 프레임 분량의 모든 NAL 을 Annex-B start code 와
// 함께 한 덩어리로 전달한다. 우리는 그대로 GstBuffer 에 복사해 push 한다.
//
// per-NAL 로 쪼개서 보내면 같은 PTS 의 buffer 가 여러 개 생기고, 서버 쪽
// h264parse → rtph264pay 가 access unit 경계를 잘못 잡아 RTP marker bit
// 가 깨지면서 원격 디코더에서 영상이 깨진다. AU 단위로 보내면 buffer 한
// 개 = frame 한 개라서 packetization 이 항상 정확하다.
// ============================================================================
void GstRtspServer::sendNal(const uint8_t* nalData, size_t nalSize) {
    if (!this->running_.load() || nalSize == 0) return;

    std::lock_guard<std::mutex> lock(this->appsrcMutex_);
    if (!this->appsrc_) return;  // 아직 클라이언트가 없어 미디어가 생성되지 않은 상태

    GstBuffer* buf = gst_buffer_new_allocate(nullptr, nalSize, nullptr);
    if (!buf) return;

    GstMapInfo map;
    if (gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
        std::memcpy(map.data, nalData, nalSize);
        gst_buffer_unmap(buf, &map);
    }

    // 한 호출 = 한 frame 이므로 매번 frameIndex_ 를 증가시킨다.
    GST_BUFFER_PTS(buf) =
        gst_util_uint64_scale(this->frameIndex_, GST_SECOND, this->fps_);
    GST_BUFFER_DURATION(buf) =
        gst_util_uint64_scale(1, GST_SECOND, this->fps_);
    ++this->frameIndex_;

    gst_app_src_push_buffer(GST_APP_SRC(this->appsrc_), buf);
}

// ============================================================================
// 정적 콜백: 미디어 생성 → appsrc 핸들 확보
// ============================================================================
void GstRtspServer::onMediaConfigure(GstRTSPMediaFactory* /*factory*/,
                                      GstRTSPMedia*         media,
                                      void*                 userData) {
    auto* self = static_cast<GstRtspServer*>(userData);

    // 미디어의 최상위 bin 에서 appsrc 를 이름으로 찾는다.
    // gst_bin_get_by_name_recurse_up 은 refcount 를 증가시키므로 stop() 에서 unref.
    GstElement* element = gst_rtsp_media_get_element(media);
    {
        std::lock_guard<std::mutex> lock(self->appsrcMutex_);
        if (self->appsrc_) {
            gst_object_unref(self->appsrc_);
        }
        self->appsrc_ = gst_bin_get_by_name_recurse_up(GST_BIN(element), "src");
        self->frameIndex_ = 0;
    }
    gst_object_unref(element);

    qInfo() << "[GstRtspServer] 미디어 생성됨, appsrc 획득:"
            << (self->appsrc_ ? "성공" : "실패");
}

