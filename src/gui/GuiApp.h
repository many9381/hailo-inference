#pragma once

#include <QApplication>

#include <string>

// Qt 애플리케이션 부트스트랩을 캡슐화한다.
// QApplication은 main()의 argc/argv 수명 동안 살아있어야 하므로
// 이 객체도 main 스코프에서 생성/유지되어야 한다.
// (https://doc.qt.io/qt-6/qapplication.html?utm_source=chatgpt.com#QApplication)
class GuiApp {
public:
    GuiApp(int& argc, char** argv);

    // role 값("server" 또는 "client")에 따라 출력할 윈도우를 런타임에 선택한다.
    //   - server: hef_path로 추론 엔진을 초기화한 ServerWindow를 띄우고,
    //             video_path가 비어있지 않으면 즉시 재생을 시작한다.
    //   - client: server_ip로 접속할 ClientWindow를 띄운다
    //             (server_ip가 비어있으면 GUI 에서 직접 입력).
    // rtsp_port / rtsp_path: RTSP 송출·수신에 공통으로 사용.
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
