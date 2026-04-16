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

    // this가 부모 → ServerWindow 소멸 시 자동 delete
    this->central_ = new QWidget(this);

    // central_이 부모 → 소멸 시 자동 delete
    this->layout_ = new QVBoxLayout(this->central_);
    this->videoLabel_ = new QLabel(this->central_);

    this->videoLabel_->setAlignment(Qt::AlignCenter);
    this->videoLabel_->setMinimumSize(640, 480);
    this->videoLabel_->setStyleSheet("background-color: black;");
    this->videoLabel_->setText("No video");
    this->layout_->addWidget(this->videoLabel_);

    this->setCentralWidget(this->central_);

    // HailoRT 추론 엔진 초기화. 실패 시 추론 없이 영상만 표시한다.
    try {
        this->inference_ = std::make_unique<HailoInference>(hef_path);
        std::cout << "HailoInference 초기화 완료: " << hef_path << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "HailoInference 초기화 실패 (" << hef_path << "): " << e.what() << std::endl;
        this->inference_.reset();
    }

    // 추론 엔진이 준비된 경우에만 워커 스레드를 가동.
    if (this->inference_) {
        this->worker_ = std::thread(&ServerWindow::inferenceLoop, this);
    }

    // this가 부모 → ServerWindow 소멸 시 자동 delete (GStreamer 리소스도 함께 정리)
    this->pipeline_ = new VideoPipeline(this);
    connect(this->pipeline_, &VideoPipeline::frameReady,
            this, &ServerWindow::onFrameReady);
}

ServerWindow::~ServerWindow() {
    // 더 이상 새 프레임이 들어오지 않도록 파이프라인을 먼저 정지.
    if (this->pipeline_) {
        this->pipeline_->stop();
    }
    // 워커 스레드 종료 신호 후 join.
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
        this->videoLabel_->setText("비디오 재생 실패: " + filepath);
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
            // hailo_inference()와 동일한 파이프라인: letterbox → BGR2RGB → 추론 → NMS
            LetterboxInfo lb_info;
            cv::Mat input_img = Preprocessor::letterbox(frame, INPUT_W, INPUT_H, lb_info);
            cv::cvtColor(input_img, input_img, cv::COLOR_BGR2RGB);
            if (!input_img.isContinuous()) {
                input_img = input_img.clone();
            }

            auto outputs = this->inference_->run(input_img);
            auto detections = PostProcessor::decode(outputs, CONF_THRESHOLD);

            // 결과 게시 — GUI 스레드는 다음 프레임 그릴 때 이 값을 사용한다.
            {
                std::lock_guard<std::mutex> lock(this->mu_);
                this->latestDets_ = std::move(detections);
                this->latestLb_ = lb_info;
                this->hasResult_ = true;
            }
        } catch (const std::exception& e) {
            std::cerr << "프레임 추론 실패: " << e.what() << std::endl;
        }

        this->busy_.store(false, std::memory_order_release);
    }
}

void ServerWindow::onFrameReady(const QImage& image) {
    // QImage(RGB888) → cv::Mat(BGR) 변환. cvtColor가 새 버퍼를 할당하므로
    // 결과 bgr Mat은 QImage 수명과 독립적이다 (cv::Mat refcount로 관리됨).
    QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    cv::Mat rgbMat(rgb.height(), rgb.width(), CV_8UC3,
                   const_cast<uchar*>(rgb.bits()), rgb.bytesPerLine());
    cv::Mat bgr;
    cv::cvtColor(rgbMat, bgr, cv::COLOR_RGB2BGR);

    // 워커가 idle이면 새 프레임을 dispatch. busy면 건너뛴다.
    if (this->inference_ && !this->busy_.load(std::memory_order_acquire)) {
        {
            std::lock_guard<std::mutex> lock(this->mu_);
            this->pendingFrame_ = bgr;  // cv::Mat은 refcount 공유 — clone 불필요
            this->hasPending_ = true;
        }
        this->cond_.notify_one();
    }

    // 가장 최근 추론 결과를 현재 프레임 위에 그려 표시한다.
    // (워커가 추론 중이면 직전 프레임의 결과가 그려지므로 1~2프레임 지연될 수 있다.)
    cv::Mat displayBgr;
    {
        std::lock_guard<std::mutex> lock(this->mu_);
        if (this->hasResult_) {
            displayBgr = Visualizer::draw(bgr, this->latestDets_, this->latestLb_);
        } else {
            displayBgr = bgr;
        }
    }

    // ── RTSP 송출 ─────────────────────────────────────────────────────
    // 첫 프레임의 해상도가 확정되었을 때 RtspServer + H264Encoder 를 lazy-init.
    // 이후 매 프레임 인코더에 push 하면 NAL 콜백을 통해 server.sendNal() 로 흘러간다.
    if (!this->streamingInit_) {
        // 비디오 caps 에서 추출한 실제 fps 를 사용. 0 이면 (아직 못 읽었거나
        // 컨테이너에 framerate 가 없으면) 30 fps 로 fallback.
        int fps = this->pipeline_ ? this->pipeline_->fps() : 0;
        if (fps <= 0) fps = 30;
        this->rtspServer_ = std::make_unique<RtspServer>(
            this->rtspPort_, this->rtspPath_,
            displayBgr.cols, displayBgr.rows, fps,
            this->rtpTcp_);
        if (!this->rtspServer_->start()) {
            std::cerr << "RtspServer 시작 실패" << std::endl;
            this->rtspServer_.reset();
        } else {
            this->encoder_ = std::make_unique<H264Encoder>(
                displayBgr.cols, displayBgr.rows, fps);
            // NAL 콜백: 인코더가 만드는 access unit(byte-stream, Annex-B start
            // code 포함, 여러 NAL 이 연속) 을 그대로 RtspServer 에 전달한다.
            // RtspServer::sendNal 은 한 호출 = 한 frame(AU) 규약이므로
            // 분할 없이 통째로 넘겨야 appsrc → h264parse → rtph264pay 가
            // access unit 경계를 정확히 인식한다.
            RtspServer* serverPtr = this->rtspServer_.get();
            this->encoder_->setNalCallback(
                [serverPtr](const uint8_t* data, size_t size) {
                    serverPtr->sendNal(data, size);
                });
            if (!this->encoder_->start()) {
                std::cerr << "H264Encoder 시작 실패" << std::endl;
                this->encoder_.reset();
                this->rtspServer_.reset();
            } else {
                std::cout << "RTSP 송출 시작: rtsp://<host>:"
                          << this->rtspPort_ << this->rtspPath_
                          << std::endl;
            }
        }
        this->streamingInit_ = true;
    }
    if (this->encoder_) {
        this->encoder_->pushFrame(displayBgr);
    }

    // [임시 비활성화] BGR → QLabel 표시 생략. 추론·RTSP 송출은 정상 동작.
    // cv::Mat displayRgb;
    // cv::cvtColor(displayBgr, displayRgb, cv::COLOR_BGR2RGB);
    // QImage out(displayRgb.data, displayRgb.cols, displayRgb.rows,
    //            static_cast<int>(displayRgb.step), QImage::Format_RGB888);
    // QPixmap pixmap = QPixmap::fromImage(out.copy());
    // this->videoLabel_->setPixmap(pixmap.scaled(this->videoLabel_->size(),
    //                                            Qt::KeepAspectRatio,
    //                                            Qt::SmoothTransformation));
}
