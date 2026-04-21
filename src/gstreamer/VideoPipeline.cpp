#include "VideoPipeline.h"

#include <QDebug>

#include "GstBootstrap.h"

VideoPipeline::VideoPipeline(QObject* parent) : QObject(parent) {}

VideoPipeline::~VideoPipeline() {
    this->stop();
}

bool VideoPipeline::start(const std::string& filepath) {
    if (!initializeGStreamer()) {
        qWarning() << "GStreamer initialization failed";
        return false;
    }

    // If another video is already playing, clean it up safely and rebuild.
    this->stop();

    // Choose the pipeline front-end (source element) based on the video source type.
    //
    //   1) /dev/video* (V4L2 device): capture from a USB webcam, etc. using v4l2src
    //   2) rtsp://     (RTSP URL):    receive network video using rtspsrc
    //   3) otherwise   (file path):   decode a local file using filesrc
    //
    // Live sources (V4L2, RTSP) use sync=false to process realtime frames
    // without delay, while file sources use sync=true to follow the original framerate.
    std::string source;
    bool is_live = false;

    if (filepath.compare(0, 5, "/dev/") == 0) {
        // V4L2 device (USB webcam, etc.)
        source = "v4l2src device=\"" + filepath + "\" ! decodebin";
        is_live = true;
    } else if (filepath.compare(0, 7, "rtsp://") == 0 ||
               filepath.compare(0, 8, "rtsps://") == 0) {
        // Receive an RTSP stream
        source = "rtspsrc location=\"" + filepath + "\" latency=200 ! decodebin";
        is_live = true;
    } else {
        // Local video file
        source = "filesrc location=\"" + filepath + "\" ! decodebin";
    }

    const std::string sync_val = is_live ? "false" : "true";
    const std::string desc =
        source + " ! videoconvert ! video/x-raw,format=RGB ! "
        "appsink name=sink emit-signals=false sync=" + sync_val +
        " max-buffers=1 drop=true";

    // Parse the desc string to instantiate elements, set properties, and link pads
    // in one shot, then return the completed GstPipeline.
    GError* err = nullptr;
    this->pipeline_ = gst_parse_launch(desc.c_str(), &err);
    if (!this->pipeline_) {
        qWarning() << "Failed to create GStreamer pipeline:"
                   << (err ? err->message : "unknown");
        if (err) g_error_free(err);
        return false;
    }

    // Look up the appsink element in the pipeline by name.
    this->appsink_ = gst_bin_get_by_name(GST_BIN(this->pipeline_), "sink");
    if (!this->appsink_) {
        qWarning() << "appsink element not found";
        this->stop();
        return false;
    }

    // Register the callback invoked whenever appsink receives a new sample (emit-signals=false mode).
    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = &VideoPipeline::onNewSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(this->appsink_), &callbacks, this, nullptr);

    // Switch the pipeline state to PLAYING -> start decoding/callbacks.
    GstStateChangeReturn ret = gst_element_set_state(this->pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "Failed to switch pipeline to PLAYING";
        this->stop();
        return false;
    }

    return true;
}

void VideoPipeline::stop() {
    // Reset so the next start() re-detects the new video's fps.
    this->fps_.store(0);

    // appsink has an extra ref from gst_bin_get_by_name, so release it with unref.
    if (this->appsink_) {
        gst_object_unref(this->appsink_);
        this->appsink_ = nullptr;
    }
    if (this->pipeline_) {
        // Transition PLAYING -> NULL to stop/unlink all elements, then unref the pipeline itself.
        gst_element_set_state(this->pipeline_, GST_STATE_NULL);
        gst_object_unref(this->pipeline_);
        this->pipeline_ = nullptr;
    }
}

GstFlowReturn VideoPipeline::onNewSample(GstAppSink* sink, gpointer user_data) {
    // Static member function, so there is no this -> use the instance pointer passed via user_data
    auto* videoPipeline = static_cast<VideoPipeline*>(user_data);

    // Pull one decoded frame (sample) from appsink (ownership is transferred).
    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) {
        return GST_FLOW_ERROR;
    }

    // Extract frame resolution (width, height) and framerate from caps.
    GstCaps* caps = gst_sample_get_caps(sample);
    int width = 0, height = 0;
    if (caps) {
        GstStructure* s = gst_caps_get_structure(caps, 0);
        gst_structure_get_int(s, "width", &width);
        gst_structure_get_int(s, "height", &height);

        // framerate is a fraction (num/den). Convert it to fps = num / den.
        // Once the value is known, store it only once instead of updating every frame.
        if (videoPipeline->fps_.load() == 0) {
            int fpsNum = 0, fpsDen = 0;
            if (gst_structure_get_fraction(s, "framerate", &fpsNum, &fpsDen) &&
                fpsDen > 0) {
                videoPipeline->fps_.store(fpsNum / fpsDen);
            }
        }
    }

    // Get the GstBuffer containing the actual pixel data and map it for reading.
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (buffer && width > 0 && height > 0 &&
        gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        // Create a deep-copy QImage so it is independent of the buffer lifetime
        QImage frame(map.data, width, height,
                     static_cast<int>(map.size / height),
                     QImage::Format_RGB888);
        QImage copy = frame.copy();
        // Pixel copying is complete, so unmap the GStreamer buffer immediately.
        gst_buffer_unmap(buffer, &map);

        // The callback runs on the GStreamer streaming thread,
        // so deliver it to the GUI thread via QueuedConnection (when the receiver has GUI-thread affinity).
        QMetaObject::invokeMethod(videoPipeline, "frameReady", Qt::QueuedConnection,
                                  Q_ARG(QImage, copy));
    }

    // Release the sample ref returned by pull_sample.
    gst_sample_unref(sample);
    return GST_FLOW_OK;
}
