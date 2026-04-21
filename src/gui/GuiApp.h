#pragma once

#include <QApplication>

#include <string>

// Encapsulates the Qt application bootstrap.
// QApplication must stay alive for the lifetime of main()'s argc/argv,
// so this object must also be created and kept in main scope.
// (https://doc.qt.io/qt-6/qapplication.html?utm_source=chatgpt.com#QApplication)
class GuiApp {
public:
    GuiApp(int& argc, char** argv);

    // Select the window to show at runtime based on the role value ("server" or "client").
    //   - server: shows ServerWindow, initializes the inference engine with hef_path,
    //             and starts playback immediately if video_path is not empty.
    //   - client: shows ClientWindow for connection to server_ip
    //             (or direct input in the GUI if server_ip is empty).
    // rtsp_port / rtsp_path: used by both RTSP transmit and receive.
    int run(const std::string& role,
            const std::string& hef_path,
            const std::string& video_path,
            const std::string& server_ip,
            int rtsp_port,
            const std::string& rtsp_path,
            bool rtp_tcp);

private:
    QApplication app_;
};
