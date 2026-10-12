// Encodr — converts video and audio with ffmpeg, and shows everything about the job while it runs.

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QLoggingCategory>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include "appmenu.h"
#include "backend.h"
#include "browser.h"
#include "naming.h"
#include "hardware.h"
#include "portalfilepicker.h"
#include "theme.h"

int main(int argc, char *argv[]) {
    // The player's FFmpeg talks a lot (stream dumps, VDPAU probing); keep the
    // terminal for Encodr's own messages unless someone asks for it.
    if (!qEnvironmentVariableIsSet("QT_LOGGING_RULES"))
        QLoggingCategory::setFilterRules("qt.multimedia.ffmpeg*=false");
    if (!qEnvironmentVariableIsSet("LIBVA_MESSAGING_LEVEL"))
        qputenv("LIBVA_MESSAGING_LEVEL", "1");    // VA-API drivers: errors only
    // Apple Silicon under Linux (Asahi) has no hardware video decoding, but
    // VA-API still offers a decoder; the player tries it, fails on codecs
    // like AV1, and never falls back, so the picture freezes. Decode in
    // software there.
    if (!qEnvironmentVariableIsSet("QT_FFMPEG_DECODING_HW_DEVICE_TYPES")) {
        QFile compatible("/proc/device-tree/compatible");
        if (compatible.open(QIODevice::ReadOnly) && compatible.readAll().contains("apple,arm-platform"))
            qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "none");
    }
    QGuiApplication app(argc, argv);
    app.setApplicationName("encodr");
    app.setOrganizationName("encodr");    // ~/.config/encodr/encodr.conf holds the layout
    app.setApplicationDisplayName("Encodr");
    app.setApplicationVersion("0.7.0");

    // Matches the window to encodr.desktop, so the compositor (Wayland app_id)
    // and the launcher pick up the installed icon.
    app.setDesktopFileName("encodr");
    // The bundled icon when no icon theme has it (an AppImage not yet in the
    // app menu).
    app.setWindowIcon(QIcon::fromTheme("encodr", QIcon(":/encodr.svg")));

    // The AppImage carries its own ffmpeg and ffprobe beside the binary
    // (distribution builds can lack H.264, or be too old for the live
    // preview). Put them first on PATH so every child process finds them.
    const QString here = QCoreApplication::applicationDirPath();
    if (QFileInfo(here + "/ffmpeg").isExecutable() && QFileInfo(here + "/ffprobe").isExecutable())
        qputenv("PATH", (here + ":" + qEnvironmentVariable("PATH")).toLocal8Bit());

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
    // Which GPU encoders really work here; a few test frames, in the background.
    Hardware hardware;
    backend.setHardware(&hardware);
    hardware.detect();

    // The browser marks files that already have an output: anything named
    // the way Encodr names outputs (the pattern now, or the default), beside
    // the original or in Encodr's folders.
    Browser browser;
    auto syncOutputs = [&] {
        QStringList suffixes;
        for (const QVariant &p : backend.presets())
            suffixes << p.toMap().value("suffix").toString();
        browser.setOutputSuffixes(suffixes);
        browser.setOutputPatterns({backend.namePattern(), naming::kDefault});
        QStringList folders{QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/Encodr"};
        if (!backend.outputFolder().isEmpty())
            folders << backend.outputFolder();
        browser.setOutputFolders(folders);
    };
    syncOutputs();
    QObject::connect(&backend, &Backend::presetsChanged, &browser, syncOutputs);
    QObject::connect(&backend, &Backend::settingsChanged, &browser, syncOutputs);
    QObject::connect(&backend, &Backend::outputWritten, &browser, &Browser::refresh);

    // Come back to the folder used last time.
    QSettings state("encodr", "encodr");
    const QString lastFolder = state.value("browser/folder").toString();
    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    browser.open(QFileInfo(lastFolder).isDir() ? lastFolder : QFileInfo(videos).isDir() ? videos : QDir::homePath());
    QObject::connect(&browser, &Browser::pathChanged, &browser, [&] { state.setValue("browser/folder", browser.path()); });

    // Before the engine, so it outlives the QML that reads it.
    AppMenu appMenu;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.rootContext()->setContextProperty("browser", &browser);
    engine.rootContext()->setContextProperty("appMenu", &appMenu);
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
    // starts the queue first.
    const QString snapshot = qEnvironmentVariable("ENCODR_SNAPSHOT");
    if (!snapshot.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        const QStringList size = qEnvironmentVariable("ENCODR_SNAPSHOT_SIZE", "1400x900").split('x');
        if (window && size.size() == 2)
            window->resize(size[0].toInt(), size[1].toInt());
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
