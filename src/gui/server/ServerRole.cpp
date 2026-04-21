#include <string>

#include <QApplication>
#include <QString>

#include "ServerWindow.h"

// hailo_server-binary-only: strong definition that overrides the weak default
// implementation in GuiApp.cpp. Creates ServerWindow and performs the server role.
int runServerRole(QApplication& app,
                  const std::string& hef_path,
                  const std::string& video_path,
                  int rtsp_port,
                  const std::string& rtsp_path,
                  bool rtp_tcp) {
    ServerWindow window(hef_path, rtsp_port, rtsp_path, rtp_tcp);
    window.show();

    if (!video_path.empty()) {
        window.playVideo(QString::fromStdString(video_path));
    }
    return app.exec();
}
