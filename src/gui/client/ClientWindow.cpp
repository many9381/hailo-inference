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

    // ── Layout setup ─────────────────────────────────────────────────────
    this->central_    = new QWidget(this);
    this->mainLayout_ = new QVBoxLayout(this->central_);

    // Control row: [IP input] [TCP/UDP] [Connect] [Disconnect] [Status]
    this->controlLayout_ = new QHBoxLayout();
    this->ipEdit_         = new QLineEdit(this->central_);
    this->transportCombo_ = new QComboBox(this->central_);
    this->connectBtn_     = new QPushButton("Connect",    this->central_);
    this->disconnectBtn_  = new QPushButton("Disconnect", this->central_);
    this->statusLabel_    = new QLabel("Disconnected",    this->central_);

    this->ipEdit_->setPlaceholderText("Server IP (e.g. 192.168.0.10)");
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

    // Video display area
    this->videoLabel_ = new QLabel(this->central_);
    this->videoLabel_->setAlignment(Qt::AlignCenter);
    this->videoLabel_->setMinimumSize(640, 480);
    this->videoLabel_->setStyleSheet("background-color: black; color: white;");
    this->videoLabel_->setText("Not connected to the server");

    this->mainLayout_->addLayout(this->controlLayout_);
    this->mainLayout_->addWidget(this->videoLabel_, 1);
    this->setCentralWidget(this->central_);

    // ── NAL decoder (Qt parent-child -> automatically cleaned up on destruction) ──
    this->decoder_ = new NalDecoderPipeline(this);

    // NalDecoderPipeline::frameReady → ClientWindow::onFrameReady
    connect(this->decoder_,  &NalDecoderPipeline::frameReady,
            this,            &ClientWindow::onFrameReady);

    // ── Button signal connections ────────────────────────────────────────
    connect(this->connectBtn_,    &QPushButton::clicked,
            this,                 &ClientWindow::onConnectClicked);
    connect(this->disconnectBtn_, &QPushButton::clicked,
            this,                 &ClientWindow::onDisconnectClicked);

    // Try connecting immediately if serverIp is preconfigured
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
        this->statusLabel_->setText("Please enter an IP address");
        return;
    }

    bool useTcp = this->transportCombo_->currentData().toBool();

    const std::string url =
        "rtsp://" + ip.toStdString() + ":" +
        std::to_string(this->rtspPort_) + this->rtspPath_;

    this->statusLabel_->setText("Connecting...");
    std::cout << "[ClientWindow] Attempting RTSP connection: " << url
              << " (RTP " << (useTcp ? "TCP" : "UDP") << ")" << std::endl;

    // Clean up the previous client if one exists
    if (this->rtspClient_) {
        this->rtspClient_->stop();
        delete this->rtspClient_;
        this->rtspClient_ = nullptr;
    }

    // Create RtspClient with the selected transport mode
    this->rtspClient_ = new RtspClient(useTcp, this);
    connect(this->rtspClient_, &RtspClient::nalReceived,
            this->decoder_,    &NalDecoderPipeline::pushNal);

    // Start the decoder first
    if (!this->decoder_->start()) {
        this->statusLabel_->setText("Failed to start decoder");
        std::cerr << "[ClientWindow] Failed to start decoder" << std::endl;
        return;
    }

    if (!this->rtspClient_->start(url)) {
        this->statusLabel_->setText("Connection failed");
        this->decoder_->stop();
        std::cerr << "[ClientWindow] RTSP connection failed: " << url << std::endl;
        return;
    }

    this->setConnected(true);
    this->statusLabel_->setText("Connected: " + ip);
}

// ============================================================================
// onDisconnectClicked
// ============================================================================
void ClientWindow::onDisconnectClicked() {
    this->rtspClient_->stop();
    this->decoder_->stop();
    this->setConnected(false);
    this->statusLabel_->setText("Disconnected");
    this->videoLabel_->setText("Not connected to the server");
    std::cout << "[ClientWindow] Disconnected" << std::endl;
}

// ============================================================================
// onFrameReady: display the decoded RGB frame on the QLabel
// ============================================================================
void ClientWindow::onFrameReady(const QImage& image) {
    QPixmap pixmap = QPixmap::fromImage(image);
    this->videoLabel_->setPixmap(
        pixmap.scaled(this->videoLabel_->size(),
                      Qt::KeepAspectRatio,
                      Qt::SmoothTransformation));
}

// ============================================================================
// setConnected: toggle button enabled/disabled state
// ============================================================================
void ClientWindow::setConnected(bool connected) {
    this->connectBtn_->setEnabled(!connected);
    this->disconnectBtn_->setEnabled(connected);
    this->ipEdit_->setEnabled(!connected);
    this->transportCombo_->setEnabled(!connected);
}
