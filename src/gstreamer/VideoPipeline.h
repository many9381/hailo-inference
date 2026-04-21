#pragma once

#include <QImage>
#include <QObject>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <atomic>
#include <string>

// Class that encapsulates a GStreamer pipeline to decode video
// and deliver decoded frames through Qt signals.
class VideoPipeline : public QObject {
    Q_OBJECT

public:
    explicit VideoPipeline(QObject* parent = nullptr);
    ~VideoPipeline() override;

    // Non-copyable/non-movable (owns GStreamer resources)
    VideoPipeline(const VideoPipeline&) = delete;
    VideoPipeline& operator=(const VideoPipeline&) = delete;

    // Start the pipeline with the given video source. Returns true on success.
    // filepath: local file path, V4L2 device (/dev/video*), or RTSP URL (rtsp://)
    bool start(const std::string& filepath);

    // Stop the pipeline and release all GStreamer resources.
    void stop();

    // Returns a valid value only after the first frame arrives and the caps framerate has been parsed.
    // Before that it returns 0, so the caller must apply a fallback.
    int fps() const { return this->fps_.load(); }

signals:
    void frameReady(const QImage& image);

private:
    static GstFlowReturn onNewSample(GstAppSink* sink, gpointer user_data);

    GstElement* pipeline_ = nullptr;
    GstElement* appsink_ = nullptr;

    // Video fps extracted from caps. Written by onNewSample (streaming thread)
    // and read by the GUI thread, so it is atomic.
    std::atomic<int> fps_{0};
};
