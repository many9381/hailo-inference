#include "MainWindow.h"

#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>

#include "gstreamer/VideoPipeline.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Hailo Inference GUI");
    resize(960, 720);

    // this가 부모 → MainWindow 소멸 시 자동 delete
    this->central_ = new QWidget(this);

    // central_이 부모 → 소멸 시 자동 delete
    this->layout_ = new QVBoxLayout(this->central_);
    this->videoLabel_ = new QLabel(this->central_);

    this->videoLabel_->setAlignment(Qt::AlignCenter);
    this->videoLabel_->setMinimumSize(640, 480);
    this->videoLabel_->setStyleSheet("background-color: black;");
    this->videoLabel_->setText("No video");
    this->layout_->addWidget(this->videoLabel_);

    setCentralWidget(this->central_);

    // this가 부모 → MainWindow 소멸 시 자동 delete (GStreamer 리소스도 함께 정리)
    this->pipeline_ = new VideoPipeline(this);
    connect(this->pipeline_, &VideoPipeline::frameReady,
            this, &MainWindow::onFrameReady);
}

MainWindow::~MainWindow() = default;

void MainWindow::playVideo(const QString& filepath) {
    if (!this->pipeline_->start(filepath.toStdString())) {
        this->videoLabel_->setText("비디오 재생 실패: " + filepath);
    }
}

void MainWindow::onFrameReady(const QImage& image) {
    // 라벨 크기에 맞춰 비율 유지하며 스케일링
    QPixmap pixmap = QPixmap::fromImage(image);
    this->videoLabel_->setPixmap(pixmap.scaled(this->videoLabel_->size(),
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
}
