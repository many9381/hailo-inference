#include "VideoPipeline.h"

#include <QDebug>

VideoPipeline::VideoPipeline(QObject* parent) : QObject(parent) {
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }
}

VideoPipeline::~VideoPipeline() {
    stop();
}

bool VideoPipeline::start(const std::string& filepath) {
    stop();

    // filesrc → decodebin → videoconvert → RGB → appsink
    const std::string desc =
        "filesrc location=\"" + filepath + "\" ! "
        "decodebin ! videoconvert ! video/x-raw,format=RGB ! "
        "appsink name=sink emit-signals=false sync=true max-buffers=1 drop=true";

    GError* err = nullptr;
    this->pipeline_ = gst_parse_launch(desc.c_str(), &err);
    if (!this->pipeline_) {
        qWarning() << "GStreamer pipeline 생성 실패:"
                   << (err ? err->message : "unknown");
        if (err) g_error_free(err);
        return false;
    }

    this->appsink_ = gst_bin_get_by_name(GST_BIN(this->pipeline_), "sink");
    if (!this->appsink_) {
        qWarning() << "appsink 요소를 찾을 수 없음";
        stop();
        return false;
    }

    GstAppSinkCallbacks callbacks = {};
    callbacks.new_sample = &VideoPipeline::onNewSample;
    gst_app_sink_set_callbacks(GST_APP_SINK(this->appsink_), &callbacks, this, nullptr);

    GstStateChangeReturn ret = gst_element_set_state(this->pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        qWarning() << "파이프라인 PLAYING 전환 실패";
        stop();
        return false;
    }

    return true;
}

void VideoPipeline::stop() {
    if (this->appsink_) {
        gst_object_unref(this->appsink_);
        this->appsink_ = nullptr;
    }
    if (this->pipeline_) {
        gst_element_set_state(this->pipeline_, GST_STATE_NULL);
        gst_object_unref(this->pipeline_);
        this->pipeline_ = nullptr;
    }
}

GstFlowReturn VideoPipeline::onNewSample(GstAppSink* sink, gpointer user_data) {
    // static 멤버 함수이므로 this가 없음 → user_data로 전달된 인스턴스 포인터 사용
    auto* videoPipeline = static_cast<VideoPipeline*>(user_data);

    GstSample* sample = gst_app_sink_pull_sample(sink);
    if (!sample) {
        return GST_FLOW_ERROR;
    }

    GstCaps* caps = gst_sample_get_caps(sample);
    int width = 0, height = 0;
    if (caps) {
        GstStructure* s = gst_caps_get_structure(caps, 0);
        gst_structure_get_int(s, "width", &width);
        gst_structure_get_int(s, "height", &height);
    }

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (buffer && width > 0 && height > 0 &&
        gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        // QImage는 deep copy로 만들어 buffer 수명과 분리
        QImage frame(map.data, width, height,
                     static_cast<int>(map.size / height),
                     QImage::Format_RGB888);
        QImage copy = frame.copy();
        gst_buffer_unmap(buffer, &map);

        // 콜백은 GStreamer streaming thread에서 호출되므로
        // QueuedConnection으로 GUI 스레드에 전달됨 (수신 객체가 GUI 스레드 affinity인 경우)
        QMetaObject::invokeMethod(videoPipeline, "frameReady", Qt::QueuedConnection,
                                  Q_ARG(QImage, copy));
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}
