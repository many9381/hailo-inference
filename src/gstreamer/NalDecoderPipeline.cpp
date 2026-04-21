#include "NalDecoderPipeline.h"

#include "GstBootstrap.h"

#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include <QDebug>

#include <cstring>

// ============================================================================
// Constructor / destructor
// ============================================================================
NalDecoderPipeline::NalDecoderPipeline(QObject* parent) : QObject(parent) {}

NalDecoderPipeline::~NalDecoderPipeline() {
    this->stop();
}

// ============================================================================
// start - appsrc -> h264parse -> decoder -> videoconvert -> appsink
// ============================================================================
bool NalDecoderPipeline::start() {
    if (!initializeGStreamer()) {
        qWarning() << "[NalDecoderPipeline] GStreamer initialization failed";
        return false;
    }

    this->stop();

    this->pipeline_     = gst_pipeline_new("nal-decoder");
    this->appsrc_       = gst_element_factory_make("appsrc",       "src");
    GstElement* parse   = gst_element_factory_make("h264parse",    "parse");
    GstElement* decoder = gst_element_factory_make("avdec_h264",   "decoder");
    GstElement* convert = gst_element_factory_make("videoconvert", "convert");
    GstElement* capsflt = gst_element_factory_make("capsfilter",   "caps");
    GstElement* sink    = gst_element_factory_make("appsink",      "sink");

    if (!this->appsrc_ || !parse || !decoder || !convert || !capsflt || !sink) {
        qWarning() << "[NalDecoderPipeline] Failed to create pipeline elements";
        if (!this->appsrc_) qWarning() << "  missing element: appsrc";
        if (!parse)         qWarning() << "  missing element: h264parse";
        if (!decoder)       qWarning() << "  missing element: avdec_h264";
        if (!convert)       qWarning() << "  missing element: videoconvert";
        if (!capsflt)       qWarning() << "  missing element: capsfilter";
        if (!sink)          qWarning() << "  missing element: appsink";
        if (this->appsrc_) { gst_object_unref(this->appsrc_); this->appsrc_ = nullptr; }
        if (parse)   gst_object_unref(parse);
        if (decoder) gst_object_unref(decoder);
        if (convert) gst_object_unref(convert);
        if (capsflt) gst_object_unref(capsflt);
        if (sink)    gst_object_unref(sink);
        gst_object_unref(this->pipeline_);
        this->pipeline_ = nullptr;
        return false;
    }

    // Configure appsrc: byte-stream H.264 NAL input
    GstCaps* srcCaps = gst_caps_from_string(
        "video/x-h264,stream-format=byte-stream,alignment=nal");
    g_object_set(this->appsrc_,
                 "caps",      srcCaps,
                 "is-live",   TRUE,
                 "format",    GST_FORMAT_TIME,
                 nullptr);
    gst_caps_unref(srcCaps);

    // capsfilter: force RGB output
    GstCaps* sinkCaps = gst_caps_from_string("video/x-raw,format=RGB");
    g_object_set(capsflt, "caps", sinkCaps, nullptr);
    gst_caps_unref(sinkCaps);

    // Configure appsink
    g_object_set(sink,
                 "emit-signals", FALSE,
                 "sync",         FALSE,
                 "max-buffers",  static_cast<guint>(1),
                 "drop",         TRUE,
                 nullptr);

    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = reinterpret_cast<GstFlowReturn(*)(GstAppSink*, gpointer)>(
        &NalDecoderPipeline::onNewSample);
    gst_app_sink_set_callbacks(GST_APP_SINK(sink), &callbacks, this, nullptr);

    // Assemble and link the pipeline
    gst_bin_add_many(GST_BIN(this->pipeline_),
                     this->appsrc_, parse, decoder, convert, capsflt, sink, nullptr);
    if (!gst_element_link_many(this->appsrc_, parse, decoder, convert, capsflt, sink, nullptr)) {
        qWarning() << "[NalDecoderPipeline] Failed to link elements";
        gst_element_set_state(this->pipeline_, GST_STATE_NULL);
        gst_object_unref(this->pipeline_);
        this->pipeline_ = nullptr;
        this->appsrc_   = nullptr;
        return false;
    }

    if (gst_element_set_state(this->pipeline_, GST_STATE_PLAYING)
            == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "[NalDecoderPipeline] Failed to switch to PLAYING";
        gst_element_set_state(this->pipeline_, GST_STATE_NULL);
        gst_object_unref(this->pipeline_);
        this->pipeline_ = nullptr;
        this->appsrc_   = nullptr;
        return false;
    }

    this->running_.store(true);
    qInfo() << "[NalDecoderPipeline] Decoding pipeline started";
    return true;
}

// ============================================================================
// stop
// ============================================================================
void NalDecoderPipeline::stop() {
    this->running_.store(false);
    if (this->pipeline_) {
        gst_element_set_state(this->pipeline_, GST_STATE_NULL);
        gst_object_unref(this->pipeline_);
        this->pipeline_ = nullptr;
        this->appsrc_   = nullptr;  // Owned by the bin
    }
}

// ============================================================================
// pushNal - prepend an Annex-B start code to a raw NAL and push it to appsrc
// ============================================================================
void NalDecoderPipeline::pushNal(const QByteArray& nal) {
    if (!this->running_.load() || nal.isEmpty()) return;

    std::lock_guard<std::mutex> lock(this->srcMu_);
    if (!this->appsrc_) return;

    // Annex-B start code (0x00000001) + NAL body
    size_t totalSize = 4 + static_cast<size_t>(nal.size());
    GstBuffer* buf = gst_buffer_new_allocate(nullptr, totalSize, nullptr);

    GstMapInfo map;
    if (gst_buffer_map(buf, &map, GST_MAP_WRITE)) {
        map.data[0] = 0x00;
        map.data[1] = 0x00;
        map.data[2] = 0x00;
        map.data[3] = 0x01;
        std::memcpy(map.data + 4, nal.constData(), nal.size());
        gst_buffer_unmap(buf, &map);
    }

    gst_app_src_push_buffer(GST_APP_SRC(this->appsrc_), buf);
}

// ============================================================================
// onNewSample - decoded RGB frame -> emit frameReady
// ============================================================================
int NalDecoderPipeline::onNewSample(GstAppSink* sink, void* userData) {
    auto* self = static_cast<NalDecoderPipeline*>(userData);
    if (!self->running_.load()) return static_cast<int>(GST_FLOW_EOS);

    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) return static_cast<int>(GST_FLOW_ERROR);

    int width = 0, height = 0;
    GstCaps* caps = gst_sample_get_caps(sample);
    if (caps) {
        GstStructure* s = gst_caps_get_structure(caps, 0);
        gst_structure_get_int(s, "width",  &width);
        gst_structure_get_int(s, "height", &height);
    }

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (buffer && width > 0 && height > 0 &&
        gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        QImage frame(map.data, width, height,
                     static_cast<int>(map.size / height),
                     QImage::Format_RGB888);
        QImage copy = frame.copy();
        gst_buffer_unmap(buffer, &map);

        QMetaObject::invokeMethod(self, "frameReady", Qt::QueuedConnection,
                                  Q_ARG(QImage, copy));
    }

    gst_sample_unref(sample);
    return static_cast<int>(GST_FLOW_OK);
}
