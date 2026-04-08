#include "GuiApp.h"

#include <QString>

#include "MainWindow.h"

GuiApp::GuiApp(int& argc, char** argv) : app_(argc, argv) {}

int GuiApp::run(const std::string& hef_path, const std::string& video_path) {
    MainWindow window(hef_path);
    window.show();

    if (!video_path.empty()) {
        window.playVideo(QString::fromStdString(video_path));
    }

    return this->app_.exec();
}
