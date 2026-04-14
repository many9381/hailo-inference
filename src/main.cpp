#include <iostream>
#include <filesystem>

#include <gst/gst.h>

#include <QSettings>
#include <QString>

#include "gui/GuiApp.h"

namespace fs = std::filesystem;

// macOS: GStreamer가 로드하는 GTK3/GTK4 비디오 싱크 플러그인이
// Qt의 Cocoa 이벤트 루프와 ObjC 클래스 충돌을 일으킨다.
// 레지스트리에서 해당 플러그인을 제거하여 충돌을 방지한다.
static void removeGtkPlugins() {
    GstRegistry* registry = gst_registry_get();
    const char* names[] = {"gtk", "gtk4"};
    for (const char* name : names) {
        GstPlugin* plugin = gst_registry_find_plugin(registry, name);
        if (plugin) {
            gst_registry_remove_plugin(registry, plugin);
            gst_object_unref(plugin);
        }
    }
}

int main(int argc, char* argv[]) {
    // GStreamer 초기화를 QApplication 생성 전에 수행하여
    // macOS Cocoa 이벤트 루프와의 충돌을 방지한다.
    gst_init(&argc, &argv);
    removeGtkPlugins();

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

    // config.ini 의 [mode] role 로 동작을 분기한다.
    std::string role = cfg.value("mode/role", "server").toString().toStdString();

    int         rtsp_port = cfg.value("rtsp/port",        8554).toInt();
    std::string rtsp_path = cfg.value("rtsp/stream_path", "/stream")
                                .toString().toStdString();

    GuiApp gui_app(argc, argv);

    if (role == "server") {
        std::string hef_path   = cfg.value("server/hef_path",  "")
                                     .toString().toStdString();
        std::string video_path = cfg.value("server/video_path", "")
                                     .toString().toStdString();

        if (video_path.empty()) {
            fs::path project_root = exe_dir.parent_path();
            video_path = (project_root / "resource" / "VIRAT_S_000001.mp4").string();
        }

        std::cout << "[server] 시작"
                  << " | hef_path="   << hef_path
                  << " | video_path=" << video_path
                  << " | rtsp_port="  << rtsp_port
                  << " | rtsp_path="  << rtsp_path
                  << std::endl;

        return gui_app.run(hef_path, video_path, rtsp_port, rtsp_path);
    }

    if (role == "client") {
        std::string server_ip = cfg.value("client/server_ip", "")
                                    .toString().toStdString();

        std::cout << "[client] 시작"
                  << " | server_ip=" << (server_ip.empty() ? "(GUI 입력)" : server_ip)
                  << " | rtsp_port=" << rtsp_port
                  << " | rtsp_path=" << rtsp_path
                  << std::endl;

        // 클라이언트 모드에서는 server_ip 를 GuiApp::run() 의 video_path 인자로 재사용한다.
        // (GuiApp.cpp 의 BUILD_CLIENT 경로에서 ClientWindow 의 서버 주소로 전달됨)
        return gui_app.run("", server_ip, rtsp_port, rtsp_path);
    }

    std::cerr << "[main] 알 수 없는 role: " << role
              << " (server 또는 client 여야 함)" << std::endl;
    return 1;
}
