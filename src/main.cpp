// Encodr — converts video and audio with ffmpeg, and shows everything about the job while it runs.

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "portalfilepicker.h"
#include "theme.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("encodr");
    app.setApplicationDisplayName("Encodr");
    app.setApplicationVersion("0.1.0");

    // Matches the window to encodr.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("encodr");
    app.setWindowIcon(QIcon::fromTheme("encodr"));

    QCommandLineParser parser;
    parser.setApplicationDescription("Convert video and audio files with ffmpeg.");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("files", "Files or folders to add to the queue.", "[files...]");
    parser.process(app);

    QQuickStyle::setStyle("Material");

    Theme theme;
    PortalFilePicker picker;
    Backend backend(&picker);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    QStringList files;
    for (const auto &arg : parser.positionalArguments()) {
        const QUrl url(arg);
        files << (url.isLocalFile() ? url.toLocalFile() : arg);
    }
    if (!files.isEmpty())
        backend.addPaths(files);

    return app.exec();
}
