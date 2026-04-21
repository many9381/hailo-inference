#include "H264Encoder.h"

#include "GstBootstrap.h"

#include <QDebug>

#include <cstdio>
#include <cstring>

#include <opencv2/imgproc.hpp>

// ============================================================================
// 생성자 / 소멸자
// ============================================================================
H264Encoder::H264Encoder(int width, int height, int fps)
    : width_(width), height_(height), fps_(fps) {}

H264Encoder::~H264Encoder() {
    this->stop();
}

// ============================================================================
// start / stop
// ============================================================================
bool H264Encoder::start() {
    if (!initializeGStreamer()) {
        qWarning() << "H264Encoder GStreamer 초기화 실패";
        return false;
    }

    this->stop();

    // gst-launch 형태의 파이프라인 정의를 문자열로 만들어 한 번에 빌드.
    //
    //  - appsrc      : is-live=true (실시간 소스), block=true (큐가 차면 push 대기),
    //                  format=time (PTS 를 ns 단위 시간으로 해석)
    //  - caps        : BGR / 지정 해상도 / 지정 fps 로 흐른다고 선언
    //  - x264enc     : zerolatency + ultrafast → 한 프레임 들어가면 즉시 한 프레임 나옴
    //                  key-int-max=fps → 1초마다 keyframe(IDR) → 신규 클라이언트 빠른 join
    //  - h264parse   : alignment=nal → buffer 1개에 NAL 1개씩 떨어지게 정렬
    //  - appsink     : sync=false (실시간으로 즉시 처리), drop=true (밀리면 버림)
    char desc[1024];
    std::snprintf(desc, sizeof(desc),
        // BGR 대신 RGB 를 사용한다. GStreamer 의 videoconvert 는 RGB→I420 변환을
        // 더 안정적으로 처리하며, Raspberry Pi 환경에서의 BGR 변환 오류를 방지한다.
        "appsrc name=src is-live=true block=true format=time "
        "caps=video/x-raw,format=RGB,width=%d,height=%d,framerate=%d/1 ! "
        "videoconvert ! "
        "x264enc tune=zerolatency speed-preset=ultrafast "
        "key-int-max=%d bitrate=2000 ! "
        "video/x-h264,profile=baseline ! "
        "h264parse config-interval=-1 ! "
        "video/x-h264,stream-format=byte-stream,alignment=au ! "
        "appsink name=sink emit-signals=false sync=false max-buffers=4 drop=true",
        this->width_, this->height_, this->fps_, this->fps_);

    GError* err = nullptr;
    this->pipeline_ = gst_parse_launch(desc, &err);
    if (!this->pipeline_) {
        qWarning() << "H264Encoder pipeline 생성 실패:"
                   << (err ? err->message : "unknown");
        if (err) g_error_free(err);
        return false;
    }

    this->appsrc_  = gst_bin_get_by_name(GST_BIN(this->pipeline_), "src");
    this->appsink_ = gst_bin_get_by_name(GST_BIN(this->pipeline_), "sink");
    if (!this->appsrc_ || !this->appsink_) {
        qWarning() << "H264Encoder element lookup 실패";
        this->stop();
        return false;
    }

    // appsink 에 NAL 단위로 떨어지는 buffer 를 받을 콜백 등록.
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = &H264Encoder::onNewSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(this->appsink_),
                               &callbacks, this, nullptr);

    if (gst_element_set_state(this->pipeline_, GST_STATE_PLAYING)
        == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "H264Encoder PLAYING 전환 실패";
        this->stop();
        return false;
    }

    this->running_.store(true);
    return true;
}

void H264Encoder::stop() {
    this->running_.store(false);

    // appsrc 에 EOS 를 보내 인코더가 큐를 비우게 한다.
    if (this->appsrc_) {
        gst_app_src_end_of_stream(GST_APP_SRC(this->appsrc_));
        gst_object_unref(this->appsrc_);
        this->appsrc_ = nullptr;
    }
    if (this->appsink_) {
        gst_object_unref(this->appsink_);
        this->appsink_ = nullptr;
    }
    if (this->pipeline_) {
        gst_element_set_state(this->pipeline_, GST_STATE_NULL);
        gst_object_unref(this->pipeline_);
        this->pipeline_ = nullptr;
    }
    this->frameIndex_ = 0;
}

// ============================================================================
// pushFrame: BGR cv::Mat → GstBuffer 로 복사 후 appsrc 에 push
// ============================================================================
void H264Encoder::pushFrame(const cv::Mat& bgr) {
    if (!this->running_.load() || !this->appsrc_) return;
    if (bgr.empty() ||
        bgr.cols != this->width_ ||
        bgr.rows != this->height_ ||
        bgr.type() != CV_8UC3) {
        qWarning() << "[H264Encoder] pushFrame 무시 — 사이즈/타입 불일치"
                   << bgr.cols << "x" << bgr.rows
                   << "(기대:" << this->width_ << "x" << this->height_ << ")";
        return;
    }

    // BGR → RGB 변환. GStreamer 파이프라인에 RGB 포맷으로 선언했으므로
    // 채널 순서를 맞춰야 색상이 정상적으로 인코딩된다.
    // cvtColor 는 항상 새 연속 Mat 을 반환하므로 stride 정렬 걱정이 없다.
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);

    const size_t size = static_cast<size_t>(rgb.total() * rgb.elemSize());
    GstBuffer*   buf  = gst_buffer_new_allocate(nullptr, size, nullptr);
    if (!buf) return;

    GstMapInfo map;
    if (gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
        // cvtColor 결과는 항상 연속 배열이므로 한 번에 복사.
        std::memcpy(map.data, rgb.data, size);
        gst_buffer_unmap(buf, &map);
    }

    // PTS = frameIndex / fps (초) → ns 변환.
    GST_BUFFER_PTS(buf) =
        gst_util_uint64_scale(this->frameIndex_, GST_SECOND, this->fps_);
    GST_BUFFER_DURATION(buf) =
        gst_util_uint64_scale(1, GST_SECOND, this->fps_);
    ++this->frameIndex_;

    // buf 의 소유권은 push_buffer 가 가져간다.
    gst_app_src_push_buffer(GST_APP_SRC(this->appsrc_), buf);
}

// ============================================================================
// forceKeyframe: x264enc 에 즉시 IDR 생성 요청
// ============================================================================
void H264Encoder::forceKeyframe() {
    if (!this->running_.load() || !this->appsrc_) return;

    // appsrc 의 src pad 로 GstForceKeyUnit 이벤트를 내려보낸다.
    // 이벤트가 x264enc 까지 전파되면 다음 프레임이 IDR(SPS+PPS+keyframe) 로 출력된다.
    GstPad* pad = gst_element_get_static_pad(this->appsrc_, "src");
    if (!pad) return;

    GstEvent* evt = gst_video_event_new_downstream_force_key_unit(
        GST_CLOCK_TIME_NONE,       // timestamp: 즉시
        GST_CLOCK_TIME_NONE,       // stream_time
        GST_CLOCK_TIME_NONE,       // running_time
        TRUE,                      // all_headers: SPS/PPS 포함
        ++this->forceKeyCount_);   // count: 단조 증가로 중복 방지

    if (!gst_pad_push_event(pad, evt)) {
        qWarning() << "[H264Encoder] forceKeyframe 이벤트 전송 실패";
    }
    gst_object_unref(pad);
    qInfo() << "[H264Encoder] IDR 강제 생성 요청";
}

// ============================================================================
// appsink 콜백 → NAL 콜백 dispatch
// ============================================================================
GstFlowReturn H264Encoder::onNewSample(GstAppSink* sink, gpointer userData) {
    auto* self = static_cast<H264Encoder*>(userData);

    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_ERROR;

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        self->handleEncodedBuffer(map.data, map.size);
        gst_buffer_unmap(buffer, &map);
    }
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

void H264Encoder::handleEncodedBuffer(const uint8_t* data, size_t size) {
    if (!this->nalCb_ || size == 0) return;

    // h264parse 의 alignment=au 모드에서는 buffer 한 개에 한 프레임 분량의
    // 모든 NAL(SPS+PPS+IDR 또는 단일 slice 등)이 Annex-B start code 와 함께
    // 연속으로 들어있다. 콜백 수신측(RtspServer) 은 이 buffer 를 그대로
    // appsrc 에 push 해야 h264parse → rtph264pay 가 access unit 경계를
    // 정확히 알고 RTP marker bit 를 올바르게 세팅한다.
    //
    // 따라서 여기서는 start code 분리/스트립 없이 buffer 전체를 그대로 전달한다.
    if (this->frameIndex_ <= 1) {
        qInfo() << "[H264Encoder] 첫 access unit 출력 — size:" << size;
    }

    this->nalCb_(data, size);
}
