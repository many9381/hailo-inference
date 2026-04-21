#pragma once

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <string>

class RtspClient;
class NalDecoderPipeline;

class ClientWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit ClientWindow(const std::string& serverIp,
                          int rtsp_port, const std::string& rtsp_path,
                          bool rtp_tcp = false,
                          QWidget* parent = nullptr);
    ~ClientWindow() override;

private slots:
    void onFrameReady(const QImage& image);
    void onConnectClicked();
    void onDisconnectClicked();

private:
    void setConnected(bool connected);

    // UI
    QWidget*      central_       = nullptr;
    QVBoxLayout*  mainLayout_    = nullptr;
    QHBoxLayout*  controlLayout_ = nullptr;
    QLineEdit*    ipEdit_         = nullptr;
    QComboBox*    transportCombo_ = nullptr;
    QPushButton*  connectBtn_     = nullptr;
    QPushButton*  disconnectBtn_  = nullptr;
    QLabel*       statusLabel_    = nullptr;
    QLabel*       videoLabel_     = nullptr;

    // RTSP connection settings
    int         rtspPort_ = 8554;
    std::string rtspPath_ = "/stream";
    bool        rtpTcp_   = false;

    // RTSP receiver (native socket) + NAL decoder (GStreamer)
    RtspClient*         rtspClient_ = nullptr;
    NalDecoderPipeline* decoder_    = nullptr;
};
