#pragma once

#include <QApplication>

#include <string>

// Qt 애플리케이션 부트스트랩을 캡슐화한다.
// QApplication은 main()의 argc/argv 수명 동안 살아있어야 한다.
class ClientApp {
public:
    ClientApp(int& argc, char** argv);

    // serverIp가 비어있지 않으면 창 오픈 즉시 연결 시도.
    // rtsp_port / rtsp_path: RTSP 연결에 사용할 포트와 스트림 경로.
    int run(const std::string& serverIp, int rtsp_port, const std::string& rtsp_path);

private:
    QApplication app_;
};
