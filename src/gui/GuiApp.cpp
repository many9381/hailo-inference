#include "GuiApp.h"

#include <iostream>

#include <QString>

#include "client/ClientWindow.h"

// server 역할 실행 함수. hailo_server 바이너리에서는 ServerRole.cpp 가
// 강한(strong) 정의를 제공하여 ServerWindow 를 띄운다.
// hailo_client 바이너리에서는 아래 약한(weak) 기본 구현이 링크되어,
// role=server 요청 시 에러 메시지를 출력하고 종료한다.
// (HailoRT 의존성을 client 빌드에서 제거하기 위한 분리)
__attribute__((weak))
int runServerRole(QApplication& /*app*/,
                  const std::string& /*hef_path*/,
                  const std::string& /*video_path*/,
                  int /*rtsp_port*/,
                  const std::string& /*rtsp_path*/) {
    std::cerr << "[GuiApp] 이 바이너리는 server 역할을 지원하지 않습니다 "
                 "(hailo_client 빌드). hailo_server 를 사용하세요."
              << std::endl;
    return 1;
}

GuiApp::GuiApp(int& argc, char** argv) : app_(argc, argv) {}

int GuiApp::run(const std::string& role,
                const std::string& hef_path,
                const std::string& video_path,
                const std::string& server_ip,
                int rtsp_port,
                const std::string& rtsp_path) {
    if (role == "server") {
        return runServerRole(this->app_, hef_path, video_path, rtsp_port, rtsp_path);
    }

    if (role == "client") {
        // 클라이언트 모드: RTSP 수신/디코드를 담당.
        ClientWindow window(server_ip, rtsp_port, rtsp_path);
        window.show();
        return this->app_.exec();
    }

    std::cerr << "[GuiApp] 알 수 없는 role: " << role
              << " (server 또는 client 여야 함)" << std::endl;
    return 1;
}
