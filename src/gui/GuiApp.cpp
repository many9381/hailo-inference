#include "GuiApp.h"

#include <QString>

#include "MainWindow.h"

GuiApp::GuiApp(int& argc, char** argv) : app_(argc, argv) {}

int GuiApp::run(const std::string& hef_path, const std::string& video_path) {
    // 메인 윈도우를 만들면서 HailoInference도 함께 초기화한다.
    MainWindow window(hef_path);
    window.show();

    // video_path가 주어지면 즉시 재생 시작(미지정 시 윈도우만 띄운 채 대기).
    if (!video_path.empty()) {
        window.playVideo(QString::fromStdString(video_path));
    }

    // Qt 이벤트 루프 진입. 사용자가 창을 닫을 때까지 블로킹.
    return this->app_.exec();
}
