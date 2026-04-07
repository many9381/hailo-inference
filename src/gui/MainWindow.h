#pragma once

#include <QMainWindow>

class QLabel;
class QVBoxLayout;
class QWidget;
class QImage;
class VideoPipeline;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // 비디오 파일 재생 시작
    void playVideo(const QString& filepath);

private slots:
    void onFrameReady(const QImage& image);

private:
    // Qt parent-child 소유권으로 MainWindow 소멸 시 자동 해제됨.
    QWidget* central_ = nullptr;
    QVBoxLayout* layout_ = nullptr;
    QLabel* videoLabel_ = nullptr;

    // VideoPipeline은 QObject 자식(this 부모)으로 자동 해제되며,
    // 소멸자에서 GStreamer 리소스도 정리됨.
    VideoPipeline* pipeline_ = nullptr;
};
