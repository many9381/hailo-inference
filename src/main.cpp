#include <iostream>
#include <filesystem>

#include <QSettings>
#include <QString>

#include "gstreamer/GstBootstrap.h"
#include "gui/GuiApp.h"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    // Qt 초기화 전에 macOS용 GStreamer 환경만 미리 정리한다.
    // 실제 gst_init()은 첫 파이프라인 시작 시점에 수행한다.
    prepareGStreamerEnvironment();

    fs::path exe_dir   = fs::path(argv[0]).parent_path();
    fs::path cfg_path  = (argc >= 2) ? fs::path(argv[1])
                                     : (exe_dir / "config.ini");

    if (!fs::exists(cfg_path)) {
        std::cerr << "[main] config 파일을 찾을 수 없음: " << cfg_path << std::endl;
        std::cerr << "       사용법: " << (argc > 0 ? argv[0] : "hailo")
                  << " [config_path]" << std::endl;
        return 1;
    }

    QSettings cfg(QString::fromStdString(cfg_path.string()), QSettings::IniFormat);

    // config.ini 의 [mode] role 로 동작을 분기한다 (런타임에 GuiApp 이 처리).
    std::string role = cfg.value("mode/role", "server").toString().toStdString();

    int         rtsp_port = cfg.value("rtsp/port",        8554).toInt();
    std::string rtsp_path = cfg.value("rtsp/stream_path", "/stream")
                                .toString().toStdString();
    bool        rtp_tcp   = cfg.value("rtsp/protocol", "udp")
                                .toString().toLower() == "tcp";

    std::string hef_path   = cfg.value("server/hef_path",  "")
                                 .toString().toStdString();
    std::string video_path = cfg.value("server/video_path", "")
                                 .toString().toStdString();
    std::string server_ip  = cfg.value("client/server_ip", "")
                                 .toString().toStdString();

    if (role == "server" && video_path.empty()) {
        fs::path project_root = exe_dir.parent_path();
        video_path = (project_root / "resource" / "sample.mp4").string();
    }

    std::cout << "[main] role=" << role
              << " | hef_path="   << hef_path
              << " | video_path=" << video_path
              << " | server_ip="  << (server_ip.empty() ? "(GUI 입력)" : server_ip)
              << " | rtsp_port="  << rtsp_port
              << " | rtsp_path="  << rtsp_path
              << " | rtp_transport=" << (rtp_tcp ? "tcp" : "udp")
              << std::endl;

    GuiApp gui_app(argc, argv);
    return gui_app.run(role, hef_path, video_path, server_ip,
                       rtsp_port, rtsp_path, rtp_tcp);
}
