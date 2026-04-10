#pragma once

#include <QImage>
#include <QObject>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

#include <atomic>
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

    // 첫 프레임이 도착해 caps 의 framerate 가 파싱된 이후에만 유효한 값을 돌려준다.
    // 그 전에는 0 을 돌려주므로 호출 측에서 fallback 처리가 필요.
    int fps() const { return this->fps_.load(); }

signals:
    void frameReady(const QImage& image);

private:
    static GstFlowReturn onNewSample(GstAppSink* sink, gpointer user_data);

    GstElement* pipeline_ = nullptr;
    GstElement* appsink_ = nullptr;

    // caps 에서 추출한 비디오 fps. onNewSample(streaming thread) 가 쓰고
    // GUI thread 가 읽으므로 atomic.
    std::atomic<int> fps_{0};
};
