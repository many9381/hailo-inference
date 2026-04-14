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

    // hef_path로 추론 엔진을 초기화한 MainWindow를 띄우고 이벤트 루프를 실행한다.
    // video_path가 비어있지 않으면 시작 직후 재생을 시작한다.
    // rtsp_port / rtsp_path: RTSP 송출에 사용할 포트와 스트림 경로.
    int run(const std::string& hef_path, const std::string& video_path,
            int rtsp_port, const std::string& rtsp_path);

private:
    QApplication app_;
};
