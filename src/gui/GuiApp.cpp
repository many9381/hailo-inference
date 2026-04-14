#include "GuiApp.h"

#include <QString>

#ifdef BUILD_SERVER
#include "server/ServerWindow.h"
#endif

#ifdef BUILD_CLIENT
#include "client/ClientWindow.h"
#endif

GuiApp::GuiApp(int& argc, char** argv) : app_(argc, argv) {}

int GuiApp::run(const std::string& hef_path, const std::string& video_path,
                int rtsp_port, const std::string& rtsp_path) {
#ifdef BUILD_SERVER
    // 서버 빌드: HailoInference + RTSP 송출을 담당하는 ServerWindow.
    ServerWindow window(hef_path, rtsp_port, rtsp_path);
    window.show();

    // video_path가 주어지면 즉시 재생 시작(미지정 시 윈도우만 띄운 채 대기).
    if (!video_path.empty()) {
        window.playVideo(QString::fromStdString(video_path));
    }
#endif

#ifdef BUILD_CLIENT
    // 클라이언트 빌드: RTSP 수신/디코드를 담당하는 ClientWindow.
    // hef_path/video_path는 서버 전용 인자이므로 클라이언트에서는 무시.
    (void)hef_path;
    // video_path 파라미터를 서버 IP 로 재사용한다(비어있으면 GUI 입력).
    ClientWindow window(video_path, rtsp_port, rtsp_path);
    window.show();
#endif

    // Qt 이벤트 루프 진입. 사용자가 창을 닫을 때까지 블로킹.
    return this->app_.exec();
}
