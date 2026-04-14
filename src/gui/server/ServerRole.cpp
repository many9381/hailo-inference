#include <string>

#include <QApplication>
#include <QString>

#include "ServerWindow.h"

// hailo_server 바이너리 전용: GuiApp.cpp 의 weak 기본 구현을 덮어쓰는
// 강한(strong) 정의. ServerWindow 를 생성하여 server 역할을 수행한다.
int runServerRole(QApplication& app,
                  const std::string& hef_path,
                  const std::string& video_path,
                  int rtsp_port,
                  const std::string& rtsp_path) {
    ServerWindow window(hef_path, rtsp_port, rtsp_path);
    window.show();

    if (!video_path.empty()) {
        window.playVideo(QString::fromStdString(video_path));
    }
    return app.exec();
}
