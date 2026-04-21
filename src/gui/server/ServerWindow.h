#pragma once

#include <QMainWindow>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <opencv2/opencv.hpp>

#include "YoloTypes.h"

class QLabel;
class QVBoxLayout;
class QWidget;
class QImage;
class VideoPipeline;
class HailoInference;
class H264Encoder;
class RtspServer;

class ServerWindow : public QMainWindow {
    Q_OBJECT

public:
    // If hef_path is not empty, initialize HailoInference and the inference worker thread.
    // rtsp_port / rtsp_path: RTSP server port and stream path.
    explicit ServerWindow(const std::string& hef_path,
                          int rtsp_port, const std::string& rtsp_path,
                          bool rtp_tcp = false,
                          QWidget* parent = nullptr);
    ~ServerWindow() override;

    // Start playback of the video source (file path, /dev/video* device, or rtsp:// URL)
    void playVideo(const QString& filepath);

private slots:
    void onFrameReady(const QImage& image);

private:
    // Main body of the inference worker thread. Shutdown is controlled by mu_/cond_/stopWorker_.
    void inferenceLoop();

    // Automatically released when ServerWindow is destroyed via Qt parent-child ownership.
    QWidget* central_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
    QLabel* videoLabel_ = nullptr;

    // VideoPipeline is automatically released as a QObject child (parented to this),
    // and its destructor also cleans up GStreamer resources.
    VideoPipeline* pipeline_ = nullptr;

    // HailoRT inference engine. Because a forward-declared type is held via unique_ptr,
    // the destructor (~ServerWindow) must be defined in the .cpp file.
    std::unique_ptr<HailoInference> inference_;

    // ── Inference worker thread synchronization ─────────────────────────
    std::thread worker_;
    std::mutex mu_;
    std::condition_variable cond_;
    cv::Mat pendingFrame_;            // Protected by mu_: next frame for the worker to process
    bool hasPending_ = false;         // Protected by mu_
    bool stopWorker_ = false;         // Protected by mu_: shutdown signal
    std::atomic<bool> busy_{false};   // Whether the worker is running inference (for quick lock-free checks)

    // ── Most recent inference result (protected by mu_) ─────────────────
    // The GUI thread overlays this result on every frame.
    // A 1-2 frame delay between frame and result is possible,
    // which is the tradeoff for avoiding UI stalls from synchronous inference.
    std::vector<Detection> latestDets_;
    LetterboxInfo latestLb_{1.0f, 0, 0};
    bool hasResult_ = false;

    // ── RTSP streaming ───────────────────────────────────────────────────
    // The displayed frame (with detection boxes drawn) is encoded as H.264
    // and transmitted over RTP via the native POSIX-socket-based RtspServer.
    //
    // Lazy-initialize when the first frame arrives and the resolution/fps is known.
    // Destruction order matters: encoder_ references server_ in its callback,
    // so declare them in the order [server_, encoder_] so encoder_ is destroyed first.
    int         rtspPort_ = 8554;
    std::string rtspPath_ = "/stream";
    bool        rtpTcp_   = false;

    std::unique_ptr<RtspServer> rtspServer_;
    std::unique_ptr<H264Encoder>   encoder_;
    bool streamingInit_ = false;
};
