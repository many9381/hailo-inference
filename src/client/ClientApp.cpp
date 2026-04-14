#include "ClientApp.h"

#include "ClientWindow.h"

ClientApp::ClientApp(int& argc, char** argv) : app_(argc, argv) {}

int ClientApp::run(const std::string& serverIp,
                   int rtsp_port, const std::string& rtsp_path) {
    ClientWindow window(serverIp, rtsp_port, rtsp_path);
    window.show();
    return this->app_.exec();
}
