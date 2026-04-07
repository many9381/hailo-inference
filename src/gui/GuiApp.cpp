#include "GuiApp.h"

#include <QApplication>
#include <QString>

#include "MainWindow.h"

namespace GuiApp {

int run(int argc, char* argv[], const std::string& video_path) {
    QApplication app(argc, argv);

    MainWindow window;
    window.show();

    if (!video_path.empty()) {
        window.playVideo(QString::fromStdString(video_path));
    }

    return app.exec();
}

} // namespace GuiApp
