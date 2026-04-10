#include "GstRtspClient.h"

// GLib/GStreamer 헤더는 Qt 헤더보다 먼저 include 해야
// Qt 의 signals 매크로와 GLib "signals" 멤버 이름 충돌을 피할 수 있다.
#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <QDebug>

// ============================================================================
// 생성자 / 소멸자
// ============================================================================
GstRtspClient::GstRtspClient(QObject* parent) : QObject(parent) {
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }
}

GstRtspClient::~GstRtspClient() {
    this->stop();
}

// ============================================================================
// start: 파이프라인 생성 → 요소 설정 → 링크 → PLAYING 전환
// ============================================================================
bool GstRtspClient::start(const std::string& url) {
    this->stop();

    // ── 요소 생성 ────────────────────────────────────────────────────────
    this->pipeline_        = gst_pipeline_new("rtsp-client");
    GstElement* rtspsrc    = gst_element_factory_make("rtspsrc",      "src");
    GstElement* depay      = gst_element_factory_make("rtph264depay", "depay");
    GstElement* parse      = gst_element_factory_make("h264parse",    "parse");
    GstElement* capsfilter = gst_element_factory_make("capsfilter",   "caps");
    GstElement* appsink    = gst_element_factory_make("appsink",      "sink");

    if (!rtspsrc || !depay || !parse || !capsfilter || !appsink) {
        qWarning() << "[GstRtspClient] 파이프라인 요소 생성 실패";
        if (rtspsrc)    gst_object_unref(rtspsrc);
        if (depay)      gst_object_unref(depay);
        if (parse)      gst_object_unref(parse);
        if (capsfilter) gst_object_unref(capsfilter);
        if (appsink)    gst_object_unref(appsink);
        gst_object_unref(this->pipeline_);
        this->pipeline_ = nullptr;
        return false;
    }

    // ── 요소 설정 ────────────────────────────────────────────────────────
    // rtspsrc: RTSP URL 과 지연 최소화 설정
    g_object_set(rtspsrc, "location", url.c_str(), "latency", 0, nullptr);

    // capsfilter: h264parse 출력을 byte-stream NAL 단위로 강제
    GstCaps* caps = gst_caps_from_string(
        "video/x-h264,stream-format=byte-stream,alignment=nal");
    g_object_set(capsfilter, "caps", caps, nullptr);
    gst_caps_unref(caps);

    // appsink: 실시간 처리, 백로그 발생 시 드롭
    g_object_set(appsink,
                 "emit-signals", FALSE,
                 "sync",         FALSE,
                 "max-buffers",  static_cast<guint>(4),
                 "drop",         TRUE,
                 nullptr);

    // appsink 콜백 등록 (emit-signals=false 이므로 콜백 방식 사용)
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = reinterpret_cast<GstFlowReturn(*)(GstAppSink*, gpointer)>(
        &GstRtspClient::onNewSample);
    gst_app_sink_set_callbacks(GST_APP_SINK(appsink), &callbacks, this, nullptr);

    // ── 파이프라인에 추가 + 링크 ─────────────────────────────────────────
    // 모든 요소를 파이프라인에 추가. 이후 소유권은 파이프라인(bin) 이 가진다.
    gst_bin_add_many(GST_BIN(this->pipeline_),
                     rtspsrc, depay, parse, capsfilter, appsink, nullptr);

    // rtspsrc 는 동적 패드를 사용하므로 depay 이후 요소들만 정적 링크.
    if (!gst_element_link_many(depay, parse, capsfilter, appsink, nullptr)) {
        qWarning() << "[GstRtspClient] 요소 링크 실패";
        gst_element_set_state(this->pipeline_, GST_STATE_NULL);
        gst_object_unref(this->pipeline_);
        this->pipeline_ = nullptr;
        return false;
    }

    // rtspsrc 패드가 준비되면 depay 의 싱크 패드와 연결.
    // pad-added 콜백에 depay 포인터를 전달한다 (파이프라인이 depay 를 소유하므로 수명 안전).
    g_signal_connect(rtspsrc, "pad-added",
                     G_CALLBACK(&GstRtspClient::onPadAdded), depay);

    // ── PLAYING 전환 ─────────────────────────────────────────────────────
    if (gst_element_set_state(this->pipeline_, GST_STATE_PLAYING)
            == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "[GstRtspClient] PLAYING 전환 실패";
        gst_element_set_state(this->pipeline_, GST_STATE_NULL);
        gst_object_unref(this->pipeline_);
        this->pipeline_ = nullptr;
        return false;
    }

    this->running_.store(true);
    qInfo() << "[GstRtspClient] 연결 시작:" << url.c_str();
    return true;
}

// ============================================================================
// stop
// ============================================================================
void GstRtspClient::stop() {
    this->running_.store(false);
    if (this->pipeline_) {
        // GST_STATE_NULL 로 전환하면 내부 스레드가 모두 종료되고 리소스가 해제된다.
        gst_element_set_state(this->pipeline_, GST_STATE_NULL);
        gst_object_unref(this->pipeline_);
        this->pipeline_ = nullptr;
    }
}

// ============================================================================
// 정적 콜백: rtspsrc 동적 패드 → rtph264depay 연결
// ============================================================================
void GstRtspClient::onPadAdded(GstElement* /*src*/, GstPad* pad,
                                void* userData) {
    GstElement* depay   = static_cast<GstElement*>(userData);
    GstPad*     sinkPad = gst_element_get_static_pad(depay, "sink");

    if (!gst_pad_is_linked(sinkPad)) {
        GstPadLinkReturn ret = gst_pad_link(pad, sinkPad);
        if (ret != GST_PAD_LINK_OK) {
            qWarning() << "[GstRtspClient] 패드 링크 실패:" << ret;
        }
    }
    gst_object_unref(sinkPad);
}

// ============================================================================
// 정적 콜백: appsink 새 샘플 → Annex-B start code 제거 후 nalReceived emit
// ============================================================================
int GstRtspClient::onNewSample(GstAppSink* sink, void* userData) {
    auto* self = static_cast<GstRtspClient*>(userData);
    if (!self->running_.load()) return static_cast<int>(GST_FLOW_EOS);

    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) return static_cast<int>(GST_FLOW_ERROR);

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        const uint8_t* data = map.data;
        size_t         size = map.size;

        // h264parse(alignment=nal) 출력에는 Annex-B start code 가 붙어있다.
        // nalReceived 시그널은 start code 없는 raw NAL 을 전달하는 규약이므로 제거한다.
        size_t offset = 0;
        if (size >= 4 &&
            data[0] == 0x00 && data[1] == 0x00 &&
            data[2] == 0x00 && data[3] == 0x01) {
            offset = 4;
        } else if (size >= 3 &&
                   data[0] == 0x00 && data[1] == 0x00 && data[2] == 0x01) {
            offset = 3;
        }

        if (offset < size) {
            QByteArray nal(reinterpret_cast<const char*>(data + offset),
                           static_cast<int>(size - offset));
            emit self->nalReceived(nal);
        }

        gst_buffer_unmap(buffer, &map);
    }

    gst_sample_unref(sample);
    return static_cast<int>(GST_FLOW_OK);
}
