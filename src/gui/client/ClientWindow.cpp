#include "ClientWindow.h"

#include <QImage>
#include <QPixmap>

#include <iostream>

#include "gstreamer/NalDecoderPipeline.h"
#include "rtsp_native/RtspClient.h"

ClientWindow::ClientWindow(const std::string& serverIp,
                           int rtsp_port, const std::string& rtsp_path,
                           bool rtp_tcp, QWidget* parent)
    : QMainWindow(parent), rtspPort_(rtsp_port), rtspPath_(rtsp_path),
      rtpTcp_(rtp_tcp) {
    this->setWindowTitle("Hailo RTSP Client");
    this->resize(960, 720);

    // ── 레이아웃 구성 ────────────────────────────────────────────────────
    this->central_    = new QWidget(this);
    this->mainLayout_ = new QVBoxLayout(this->central_);

    // 컨트롤 행: [IP 입력] [TCP/UDP] [Connect] [Disconnect] [상태]
    this->controlLayout_ = new QHBoxLayout();
    this->ipEdit_         = new QLineEdit(this->central_);
    this->transportCombo_ = new QComboBox(this->central_);
    this->connectBtn_     = new QPushButton("Connect",    this->central_);
    this->disconnectBtn_  = new QPushButton("Disconnect", this->central_);
    this->statusLabel_    = new QLabel("연결 안됨",       this->central_);

    this->ipEdit_->setPlaceholderText("Server IP (예: 192.168.0.10)");
    this->ipEdit_->setMinimumWidth(220);
    if (!serverIp.empty()) {
        this->ipEdit_->setText(QString::fromStdString(serverIp));
    }

    this->transportCombo_->addItem("UDP", false);
    this->transportCombo_->addItem("TCP", true);
    this->transportCombo_->setCurrentIndex(rtp_tcp ? 1 : 0);

    this->disconnectBtn_->setEnabled(false);

    this->controlLayout_->addWidget(this->ipEdit_);
    this->controlLayout_->addWidget(this->transportCombo_);
    this->controlLayout_->addWidget(this->connectBtn_);
    this->controlLayout_->addWidget(this->disconnectBtn_);
    this->controlLayout_->addWidget(this->statusLabel_);
    this->controlLayout_->addStretch();

    // 영상 표시 영역
    this->videoLabel_ = new QLabel(this->central_);
    this->videoLabel_->setAlignment(Qt::AlignCenter);
    this->videoLabel_->setMinimumSize(640, 480);
    this->videoLabel_->setStyleSheet("background-color: black; color: white;");
    this->videoLabel_->setText("서버에 연결되지 않음");

    this->mainLayout_->addLayout(this->controlLayout_);
    this->mainLayout_->addWidget(this->videoLabel_, 1);
    this->setCentralWidget(this->central_);

    // ── NAL 디코더 (Qt parent-child → 소멸 시 자동 정리) ──
    this->decoder_ = new NalDecoderPipeline(this);

    // NalDecoderPipeline::frameReady → ClientWindow::onFrameReady
    connect(this->decoder_,  &NalDecoderPipeline::frameReady,
            this,            &ClientWindow::onFrameReady);

    // ── 버튼 시그널 연결 ─────────────────────────────────────────────────
    connect(this->connectBtn_,    &QPushButton::clicked,
            this,                 &ClientWindow::onConnectClicked);
    connect(this->disconnectBtn_, &QPushButton::clicked,
            this,                 &ClientWindow::onDisconnectClicked);

    // serverIp가 미리 지정된 경우 즉시 연결 시도
    if (!serverIp.empty()) {
        QMetaObject::invokeMethod(this, &ClientWindow::onConnectClicked,
                                  Qt::QueuedConnection);
    }
}

ClientWindow::~ClientWindow() {
    if (this->rtspClient_) {
        this->rtspClient_->stop();
    }
    if (this->decoder_) {
        this->decoder_->stop();
    }
}

// ============================================================================
// onConnectClicked
// ============================================================================
void ClientWindow::onConnectClicked() {
    const QString ip = this->ipEdit_->text().trimmed();
    if (ip.isEmpty()) {
        this->statusLabel_->setText("IP를 입력하세요");
        return;
    }

    bool useTcp = this->transportCombo_->currentData().toBool();

    const std::string url =
        "rtsp://" + ip.toStdString() + ":" +
        std::to_string(this->rtspPort_) + this->rtspPath_;

    this->statusLabel_->setText("연결 중...");
    std::cout << "[ClientWindow] RTSP 연결 시도: " << url
              << " (RTP " << (useTcp ? "TCP" : "UDP") << ")" << std::endl;

    // 이전 클라이언트가 있으면 정리
    if (this->rtspClient_) {
        this->rtspClient_->stop();
        delete this->rtspClient_;
        this->rtspClient_ = nullptr;
    }

    // 선택된 전송 모드로 RtspClient 생성
    this->rtspClient_ = new RtspClient(useTcp, this);
    connect(this->rtspClient_, &RtspClient::nalReceived,
            this->decoder_,    &NalDecoderPipeline::pushNal);

    // 디코더 먼저 시작
    if (!this->decoder_->start()) {
        this->statusLabel_->setText("디코더 시작 실패");
        std::cerr << "[ClientWindow] 디코더 시작 실패" << std::endl;
        return;
    }

    if (!this->rtspClient_->start(url)) {
        this->statusLabel_->setText("연결 실패");
        this->decoder_->stop();
        std::cerr << "[ClientWindow] RTSP 연결 실패: " << url << std::endl;
        return;
    }

    this->setConnected(true);
    this->statusLabel_->setText("연결됨: " + ip);
}

// ============================================================================
// onDisconnectClicked
// ============================================================================
void ClientWindow::onDisconnectClicked() {
    this->rtspClient_->stop();
    this->decoder_->stop();
    this->setConnected(false);
    this->statusLabel_->setText("연결 안됨");
    this->videoLabel_->setText("서버에 연결되지 않음");
    std::cout << "[ClientWindow] 연결 해제" << std::endl;
}

// ============================================================================
// onFrameReady: 디코딩된 RGB 프레임을 QLabel에 표시
// ============================================================================
void ClientWindow::onFrameReady(const QImage& image) {
    QPixmap pixmap = QPixmap::fromImage(image);
    this->videoLabel_->setPixmap(
        pixmap.scaled(this->videoLabel_->size(),
                      Qt::KeepAspectRatio,
                      Qt::SmoothTransformation));
}

// ============================================================================
// setConnected: 버튼 활성/비활성 전환
// ============================================================================
void ClientWindow::setConnected(bool connected) {
    this->connectBtn_->setEnabled(!connected);
    this->disconnectBtn_->setEnabled(connected);
    this->ipEdit_->setEnabled(!connected);
    this->transportCombo_->setEnabled(!connected);
}
