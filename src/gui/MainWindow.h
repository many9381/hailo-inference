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

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // hef_path가 비어있지 않으면 HailoInference와 추론 워커 스레드를 초기화한다.
    // rtsp_port / rtsp_path: RTSP 서버 포트 및 스트림 경로.
    explicit MainWindow(const std::string& hef_path,
                        int rtsp_port, const std::string& rtsp_path,
                        QWidget* parent = nullptr);
    ~MainWindow() override;

    // 비디오 파일 재생 시작
    void playVideo(const QString& filepath);

private slots:
    void onFrameReady(const QImage& image);

private:
    // 추론 워커 스레드 본체. mu_/cond_/stopWorker_로 종료 제어.
    void inferenceLoop();

    // Qt parent-child 소유권으로 MainWindow 소멸 시 자동 해제됨.
    QWidget* central_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
    QLabel* videoLabel_ = nullptr;

    // VideoPipeline은 QObject 자식(this 부모)으로 자동 해제되며,
    // 소멸자에서 GStreamer 리소스도 정리됨.
    VideoPipeline* pipeline_ = nullptr;

    // HailoRT 추론 엔진. unique_ptr로 forward-declared 타입을 보유하므로
    // 소멸자(~MainWindow)는 반드시 .cpp에서 정의되어야 한다.
    std::unique_ptr<HailoInference> inference_;

    // ── 추론 워커 스레드 동기화 ──────────────────────────────────────────
    std::thread worker_;
    std::mutex mu_;
    std::condition_variable cond_;
    cv::Mat pendingFrame_;            // mu_ 보호: 워커가 처리할 다음 프레임
    bool hasPending_ = false;         // mu_ 보호
    bool stopWorker_ = false;         // mu_ 보호: 종료 신호
    std::atomic<bool> busy_{false};   // 워커가 추론 중인지 (lock 없이 빠른 확인용)

    // ── 가장 최근 추론 결과 (mu_ 보호) ───────────────────────────────────
    // GUI 스레드는 이 결과를 매 프레임 덮어 그린다.
    // 프레임-결과 간 1~2프레임 지연이 발생 가능성 존재,
    // 동기 추론으로 인한 UI 정지와의 tradeoff.
    std::vector<Detection> latestDets_;
    LetterboxInfo latestLb_{1.0f, 0, 0};
    bool hasResult_ = false;

    // ── RTSP 송출 ────────────────────────────────────────────────────────
    // 표시 중인 (검출 박스가 그려진) 프레임을 H.264 로 인코딩한 뒤
    // native POSIX 소켓 기반 RtspServer 를 통해 RTP 로 송출한다.
    //
    // 첫 프레임이 들어와 해상도/fps 가 확정될 때 lazy-init 한다.
    // 소멸 순서가 중요: encoder_ 가 server_ 를 콜백으로 참조하므로
    // 선언 순서를 [server_, encoder_] 로 두어 encoder_ 가 먼저 파괴되게 한다.
    int         rtspPort_ = 8554;
    std::string rtspPath_ = "/stream";

    std::unique_ptr<RtspServer> rtspServer_;
    std::unique_ptr<H264Encoder>   encoder_;
    bool streamingInit_ = false;
};
