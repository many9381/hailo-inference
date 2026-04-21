#include "GuiApp.h"

#include <iostream>

#include <QString>

#include "client/ClientWindow.h"

// Server-role entry function. In the hailo_server binary, ServerRole.cpp
// provides a strong definition that opens ServerWindow.
// In the hailo_client binary, the weak default implementation below is linked,
// prints an error message on a role=server request, and exits.
// (Separated to remove the HailoRT dependency from the client build.)
__attribute__((weak))
int runServerRole(QApplication& /*app*/,
                  const std::string& /*hef_path*/,
                  const std::string& /*video_path*/,
                  int /*rtsp_port*/,
                  const std::string& /*rtsp_path*/,
                  bool /*rtp_tcp*/) {
    std::cerr << "[GuiApp] This binary does not support the server role "
                 "(hailo_client build). Use hailo_server."
              << std::endl;
    return 1;
}

GuiApp::GuiApp(int& argc, char** argv) : app_(argc, argv) {}

int GuiApp::run(const std::string& role,
                const std::string& hef_path,
                const std::string& video_path,
                const std::string& server_ip,
                int rtsp_port,
                const std::string& rtsp_path,
                bool rtp_tcp) {
    if (role == "server") {
        return runServerRole(this->app_, hef_path, video_path,
                             rtsp_port, rtsp_path, rtp_tcp);
    }

    if (role == "client") {
        // Client mode: handles RTSP receive/decode.
        ClientWindow window(server_ip, rtsp_port, rtsp_path, rtp_tcp);
        window.show();
        return this->app_.exec();
    }

    std::cerr << "[GuiApp] Unknown role: " << role
              << " (must be server or client)" << std::endl;
    return 1;
}
