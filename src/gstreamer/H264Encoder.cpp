#include "H264Encoder.h"

#include "GstBootstrap.h"

#include <QDebug>

#include <cstdio>
#include <cstring>

#include <opencv2/imgproc.hpp>

// ============================================================================
// Constructor / destructor
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
        qWarning() << "H264Encoder GStreamer initialization failed";
        return false;
    }

    this->stop();

    // Build the pipeline in one shot by creating a gst-launch-style pipeline definition string.
    //
    //  - appsrc      : is-live=true (realtime source), block=true (wait when the queue is full),
    //                  format=time (interpret PTS as time in ns)
    //  - caps        : declare the stream as BGR / specified resolution / specified fps
    //  - x264enc     : zerolatency + ultrafast -> one frame in, one frame out immediately
    //                  key-int-max=fps -> keyframe (IDR) every second -> faster join for new clients
    //  - h264parse   : alignment=nal -> align so one buffer contains one NAL
    //  - appsink     : sync=false (process immediately in realtime), drop=true (drop when backed up)
    char desc[1024];
    std::snprintf(desc, sizeof(desc),
        // Use RGB instead of BGR. GStreamer's videoconvert handles RGB->I420 conversion
        // more reliably and avoids BGR conversion issues on Raspberry Pi.
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
        qWarning() << "Failed to create H264Encoder pipeline:"
                   << (err ? err->message : "unknown");
        if (err) g_error_free(err);
        return false;
    }

    this->appsrc_  = gst_bin_get_by_name(GST_BIN(this->pipeline_), "src");
    this->appsink_ = gst_bin_get_by_name(GST_BIN(this->pipeline_), "sink");
    if (!this->appsrc_ || !this->appsink_) {
        qWarning() << "H264Encoder element lookup failed";
        this->stop();
        return false;
    }

    // Register the callback that receives buffers emitted by appsink per NAL unit.
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = &H264Encoder::onNewSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(this->appsink_),
                               &callbacks, this, nullptr);

    if (gst_element_set_state(this->pipeline_, GST_STATE_PLAYING)
        == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "Failed to switch H264Encoder to PLAYING";
        this->stop();
        return false;
    }

    this->running_.store(true);
    return true;
}

void H264Encoder::stop() {
    this->running_.store(false);

    // Send EOS to appsrc so the encoder flushes its queue.
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
// pushFrame: copy BGR cv::Mat into a GstBuffer and push it to appsrc
// ============================================================================
void H264Encoder::pushFrame(const cv::Mat& bgr) {
    if (!this->running_.load() || !this->appsrc_) return;
    if (bgr.empty() ||
        bgr.cols != this->width_ ||
        bgr.rows != this->height_ ||
        bgr.type() != CV_8UC3) {
        qWarning() << "[H264Encoder] Ignoring pushFrame - size/type mismatch"
                   << bgr.cols << "x" << bgr.rows
                   << "(expected:" << this->width_ << "x" << this->height_ << ")";
        return;
    }

    // Convert BGR -> RGB. The GStreamer pipeline is declared as RGB,
    // so the channel order must match for correct color encoding.
    // cvtColor always returns a new contiguous Mat, so stride alignment is not a concern.
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);

    const size_t size = static_cast<size_t>(rgb.total() * rgb.elemSize());
    GstBuffer*   buf  = gst_buffer_new_allocate(nullptr, size, nullptr);
    if (!buf) return;

    GstMapInfo map;
    if (gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
        // The cvtColor result is always contiguous, so copy it in one shot.
        std::memcpy(map.data, rgb.data, size);
        gst_buffer_unmap(buf, &map);
    }

    // PTS = frameIndex / fps (seconds) -> convert to ns.
    GST_BUFFER_PTS(buf) =
        gst_util_uint64_scale(this->frameIndex_, GST_SECOND, this->fps_);
    GST_BUFFER_DURATION(buf) =
        gst_util_uint64_scale(1, GST_SECOND, this->fps_);
    ++this->frameIndex_;

    // Ownership of buf is transferred to push_buffer.
    gst_app_src_push_buffer(GST_APP_SRC(this->appsrc_), buf);
}

// ============================================================================
// forceKeyframe: request immediate IDR generation from x264enc
// ============================================================================
void H264Encoder::forceKeyframe() {
    if (!this->running_.load() || !this->appsrc_) return;

    // Send a GstForceKeyUnit event downstream through appsrc's src pad.
    // If the event reaches x264enc, the next frame is output as an IDR (SPS+PPS+keyframe).
    GstPad* pad = gst_element_get_static_pad(this->appsrc_, "src");
    if (!pad) return;

    GstEvent* evt = gst_video_event_new_downstream_force_key_unit(
        GST_CLOCK_TIME_NONE,       // timestamp: immediate
        GST_CLOCK_TIME_NONE,       // stream_time
        GST_CLOCK_TIME_NONE,       // running_time
        TRUE,                      // all_headers: include SPS/PPS
        ++this->forceKeyCount_);   // count: monotonically increase to avoid duplicates

    if (!gst_pad_push_event(pad, evt)) {
        qWarning() << "[H264Encoder] Failed to send forceKeyframe event";
    }
    gst_object_unref(pad);
    qInfo() << "[H264Encoder] Forced IDR generation requested";
}

// ============================================================================
// appsink callback -> dispatch to the NAL callback
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

    // In h264parse alignment=au mode, one buffer contains all NALs for one frame
    // (SPS+PPS+IDR or a single slice, etc.) in sequence with Annex-B start codes.
    // The callback receiver (RtspServer) must push this buffer as-is to appsrc
    // so h264parse -> rtph264pay can correctly recognize access-unit boundaries
    // and set the RTP marker bit properly.
    //
    // Therefore, forward the entire buffer as-is here without splitting or stripping start codes.
    if (this->frameIndex_ <= 1) {
        qInfo() << "[H264Encoder] First access unit output - size:" << size;
    }

    this->nalCb_(data, size);
}
