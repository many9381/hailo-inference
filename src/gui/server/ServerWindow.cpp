#include "ServerWindow.h"

#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>

#include <iostream>
#include <utility>

#include "HailoInference.h"
#include "PostProcessor.h"
#include "Preprocessor.h"
#include "Visualizer.h"
#include "gstreamer/H264Encoder.h"
#include "gstreamer/VideoPipeline.h"
#include "rtsp_native/RtspServer.h"

ServerWindow::ServerWindow(const std::string& hef_path,
                           int rtsp_port, const std::string& rtsp_path,
                           bool rtp_tcp, QWidget* parent)
    : QMainWindow(parent), rtspPort_(rtsp_port), rtspPath_(rtsp_path),
      rtpTcp_(rtp_tcp) {
    this->setWindowTitle("Hailo Inference GUI");
    this->resize(960, 720);

    // Parent is this -> automatically deleted when ServerWindow is destroyed
    this->central_ = new QWidget(this);

    // Parent is central_ -> automatically deleted on destruction
    this->layout_ = new QVBoxLayout(this->central_);
    this->videoLabel_ = new QLabel(this->central_);

    this->videoLabel_->setAlignment(Qt::AlignCenter);
    this->videoLabel_->setMinimumSize(640, 480);
    this->videoLabel_->setStyleSheet("background-color: black;");
    this->videoLabel_->setText("No video");
    this->layout_->addWidget(this->videoLabel_);

    this->setCentralWidget(this->central_);

    // Initialize the HailoRT inference engine. If it fails, only the video is displayed without inference.
    try {
        this->inference_ = std::make_unique<HailoInference>(hef_path);
        std::cout << "HailoInference initialized: " << hef_path << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize HailoInference (" << hef_path << "): " << e.what() << std::endl;
        this->inference_.reset();
    }

    // Start the worker thread only if the inference engine is ready.
    if (this->inference_) {
        this->worker_ = std::thread(&ServerWindow::inferenceLoop, this);
    }

    // Parent is this -> automatically deleted when ServerWindow is destroyed (also cleans up GStreamer resources)
    this->pipeline_ = new VideoPipeline(this);
    connect(this->pipeline_, &VideoPipeline::frameReady,
            this, &ServerWindow::onFrameReady);
}

ServerWindow::~ServerWindow() {
    // Stop the pipeline first so no more new frames arrive.
    if (this->pipeline_) {
        this->pipeline_->stop();
    }
    // Signal worker-thread shutdown, then join.
    {
        std::lock_guard<std::mutex> lock(this->mu_);
        this->stopWorker_ = true;
    }
    this->cond_.notify_all();
    if (this->worker_.joinable()) {
        this->worker_.join();
    }
}

void ServerWindow::playVideo(const QString& filepath) {
    if (!this->pipeline_->start(filepath.toStdString())) {
        this->videoLabel_->setText("Failed to play video: " + filepath);
    }
}

void ServerWindow::inferenceLoop() {
    while (true) {
        cv::Mat frame;
        {
            std::unique_lock<std::mutex> lock(this->mu_);
            this->cond_.wait(lock, [this] { return this->hasPending_ || this->stopWorker_; });
            if (this->stopWorker_) {
                return;
            }
            frame = std::move(this->pendingFrame_);
            this->hasPending_ = false;
        }

        this->busy_.store(true, std::memory_order_release);

        try {
            // Same pipeline as hailo_inference(): letterbox -> BGR2RGB -> inference -> NMS
            LetterboxInfo lb_info;
            cv::Mat input_img = Preprocessor::letterbox(frame, INPUT_W, INPUT_H, lb_info);
            cv::cvtColor(input_img, input_img, cv::COLOR_BGR2RGB);
            if (!input_img.isContinuous()) {
                input_img = input_img.clone();
            }

            auto outputs = this->inference_->run(input_img);
            auto detections = PostProcessor::decode(outputs, CONF_THRESHOLD);

            // Publish the result - the GUI thread uses this value when drawing the next frame.
            {
                std::lock_guard<std::mutex> lock(this->mu_);
                this->latestDets_ = std::move(detections);
                this->latestLb_ = lb_info;
                this->hasResult_ = true;
            }
        } catch (const std::exception& e) {
            std::cerr << "Frame inference failed: " << e.what() << std::endl;
        }

        this->busy_.store(false, std::memory_order_release);
    }
}

void ServerWindow::onFrameReady(const QImage& image) {
    // Convert QImage(RGB888) -> cv::Mat(BGR). Since cvtColor allocates a new buffer,
    // the resulting bgr Mat is independent of the QImage lifetime (managed by cv::Mat refcounting).
    QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    cv::Mat rgbMat(rgb.height(), rgb.width(), CV_8UC3,
                   const_cast<uchar*>(rgb.bits()), rgb.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(rgbMat, bgr, cv::COLOR_RGB2BGR);

    // Dispatch a new frame if the worker is idle. Skip it if the worker is busy.
    if (this->inference_ && !this->busy_.load(std::memory_order_acquire)) {
        {
            std::lock_guard<std::mutex> lock(this->mu_);
            this->pendingFrame_ = bgr;  // cv::Mat shares refcounted storage - no clone needed
            this->hasPending_ = true;
        }
        this->cond_.notify_one();
    }

    // Draw and display the most recent inference result on top of the current frame.
    // (If the worker is inferring, the previous frame's result is drawn, so a 1-2 frame delay is possible.)
    cv::Mat displayBgr;
    {
        std::lock_guard<std::mutex> lock(this->mu_);
        if (this->hasResult_) {
            displayBgr = Visualizer::draw(bgr, this->latestDets_, this->latestLb_);
        } else {
            displayBgr = bgr;
        }
    }

    // ── RTSP streaming ────────────────────────────────────────────────
    // Lazy-initialize RtspServer + H264Encoder once the first frame resolution is known.
    // After that, pushing each frame into the encoder reaches server.sendNal() through the NAL callback.
    if (!this->streamingInit_) {
        // Use the actual fps extracted from the video caps. If it is 0
        // (not read yet, or the container has no framerate), fall back to 30 fps.
        int fps = this->pipeline_ ? this->pipeline_->fps() : 0;
        if (fps <= 0) fps = 30;
        this->rtspServer_ = std::make_unique<RtspServer>(
            this->rtspPort_, this->rtspPath_,
            displayBgr.cols, displayBgr.rows, fps,
            this->rtpTcp_);
        if (!this->rtspServer_->start()) {
            std::cerr << "Failed to start RtspServer" << std::endl;
            this->rtspServer_.reset();
        } else {
            this->encoder_ = std::make_unique<H264Encoder>(
                displayBgr.cols, displayBgr.rows, fps);
            // NAL callback: forward the access unit produced by the encoder
            // (byte-stream, with Annex-B start codes and multiple NALs in sequence) directly to RtspServer.
            // RtspServer::sendNal uses the rule one call = one frame (AU),
            // so it must be passed intact without splitting for appsrc -> h264parse -> rtph264pay
            // to recognize access-unit boundaries correctly.
            RtspServer* serverPtr = this->rtspServer_.get();
            this->encoder_->setNalCallback(
                [serverPtr](const uint8_t* data, size_t size) {
                    serverPtr->sendNal(data, size);
                });
            if (!this->encoder_->start()) {
                std::cerr << "Failed to start H264Encoder" << std::endl;
                this->encoder_.reset();
                this->rtspServer_.reset();
            } else {
                std::cout << "RTSP streaming started: rtsp://<host>:"
                          << this->rtspPort_ << this->rtspPath_
                          << std::endl;
            }
        }
        this->streamingInit_ = true;
    }
    if (this->encoder_) {
        this->encoder_->pushFrame(displayBgr);
    }

    // [Temporarily disabled] Skip BGR -> QLabel display. Inference and RTSP streaming still work normally.
    // cv::Mat displayRgb;
    // cv::cvtColor(displayBgr, displayRgb, cv::COLOR_BGR2RGB);
    // QImage out(displayRgb.data, displayRgb.cols, displayRgb.rows,
    //            static_cast<int>(displayRgb.step), QImage::Format_RGB888);
    // QPixmap pixmap = QPixmap::fromImage(out.copy());
    // this->videoLabel_->setPixmap(pixmap.scaled(this->videoLabel_->size(),
    //                                            Qt::KeepAspectRatio,
    //                                            Qt::SmoothTransformation));
}
