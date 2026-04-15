#pragma once

// GStreamer 타입 전방 선언 (Qt signals 매크로와 이름 충돌 방지).
typedef struct _GstElement  GstElement;
typedef struct _GstAppSink  GstAppSink;

#include <QImage>
#include <QObject>

#include <atomic>
#include <mutex>

// ----------------------------------------------------------------------------
// NalDecoderPipeline
//
// GStreamer appsrc 기반 H.264 NAL 디코더.
// RtspClient 가 수신한 raw NAL 을 pushNal() 로 넣으면 내부 파이프라인이
//   appsrc → h264parse → avdec_h264 → videoconvert → appsink
// 을 통해 디코딩하고, frameReady 시그널로 RGB QImage 를 전달한다.
// ----------------------------------------------------------------------------
class NalDecoderPipeline : public QObject {
    Q_OBJECT

public:
    explicit NalDecoderPipeline(QObject* parent = nullptr);
    ~NalDecoderPipeline() override;

    bool start();
    void stop();

    // Annex-B start code 없는 raw NAL 한 단위를 파이프라인에 주입.
    void pushNal(const QByteArray& nal);

signals:
    void frameReady(const QImage& image);

private:
    static int onNewSample(GstAppSink* sink, void* userData);

    GstElement*       pipeline_ = nullptr;
    GstElement*       appsrc_   = nullptr;
    std::atomic<bool> running_{false};
    std::mutex        srcMu_;
};
