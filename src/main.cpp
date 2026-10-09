// Encodr — converts video and audio with ffmpeg, and shows everything about the job while it runs.

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
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

    // Material's compact variant: its stock controls are sized for touch
    // screens and look puffy in a desktop tool.
    if (!qEnvironmentVariableIsSet("QT_QUICK_CONTROLS_MATERIAL_VARIANT"))
        qputenv("QT_QUICK_CONTROLS_MATERIAL_VARIANT", "Dense");
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

    // For development: ENCODR_SNAPSHOT=out.png renders the window to an image
    // after ENCODR_SNAPSHOT_DELAY ms (default 3000) and quits, so the look can
    // be checked offscreen without taking over the screen. ENCODR_START=1
    // starts the queue first; ENCODR_TAB picks the tab (0-2).
    const QString snapshot = qEnvironmentVariable("ENCODR_SNAPSHOT");
    if (!snapshot.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        const QStringList size = qEnvironmentVariable("ENCODR_SNAPSHOT_SIZE", "1400x900").split('x');
        if (window && size.size() == 2)
            window->resize(size[0].toInt(), size[1].toInt());
        if (window && qEnvironmentVariableIsSet("ENCODR_TAB"))
            window->setProperty("tab", qEnvironmentVariableIntValue("ENCODR_TAB"));
        if (qEnvironmentVariableIntValue("ENCODR_START"))
            QTimer::singleShot(1500, &backend, &Backend::startQueue);
        const int delay = qEnvironmentVariableIsSet("ENCODR_SNAPSHOT_DELAY") ? qEnvironmentVariableIntValue("ENCODR_SNAPSHOT_DELAY") : 3000;
        QTimer::singleShot(delay, &app, [window, snapshot, &app] {
            if (window)
                window->grabWindow().save(snapshot);
            app.exit(0);    // quit() would ask first while converting
        });
    }

    return app.exec();
}
