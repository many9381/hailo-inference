#pragma once

// Forward declarations for GStreamer types (to avoid name conflicts with Qt signals macros).
typedef struct _GstElement  GstElement;
typedef struct _GstAppSink  GstAppSink;

#include <QImage>
#include <QObject>

#include <atomic>
#include <mutex>

// ----------------------------------------------------------------------------
// NalDecoderPipeline
//
// H.264 NAL decoder based on GStreamer appsrc.
// When RtspClient pushes a received raw NAL through pushNal(), the internal pipeline
//   appsrc → h264parse → avdec_h264 → videoconvert → appsink
// decodes it and delivers an RGB QImage through the frameReady signal.
// ----------------------------------------------------------------------------
class NalDecoderPipeline : public QObject {
    Q_OBJECT

public:
    explicit NalDecoderPipeline(QObject* parent = nullptr);
    ~NalDecoderPipeline() override;

    bool start();
    void stop();

    // Push a single raw NAL unit without an Annex-B start code into the pipeline.
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
