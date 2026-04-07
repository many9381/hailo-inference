#pragma once

#include <QImage>
#include <QObject>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <string>

// GStreamer 파이프라인을 캡슐화하여 비디오 파일을 디코딩하고
// 디코딩된 프레임을 Qt 신호로 전달하는 클래스.
class VideoPipeline : public QObject {
    Q_OBJECT

public:
    explicit VideoPipeline(QObject* parent = nullptr);
    ~VideoPipeline() override;

    // 복사/이동 금지 (GStreamer 리소스 소유)
    VideoPipeline(const VideoPipeline&) = delete;
    VideoPipeline& operator=(const VideoPipeline&) = delete;

    // 지정된 파일 경로로 파이프라인을 시작한다. 성공 시 true.
    bool start(const std::string& filepath);

    // 파이프라인을 중지하고 모든 GStreamer 리소스를 해제한다.
    void stop();

signals:
    void frameReady(const QImage& image);

private:
    static GstFlowReturn onNewSample(GstAppSink* sink, gpointer user_data);

    GstElement* pipeline_ = nullptr;
    GstElement* appsink_ = nullptr;
};
