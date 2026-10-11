QT += core gui qml quick quickcontrols2 dbus

CONFIG += c++17 release
TARGET = encodr
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/filepicker.h \
    src/job.h \
    src/mediainfo.h \
    src/portalfilepicker.h \
    src/preset.h \
    src/progress.h \
    src/telemetry.h \
    src/cyclecounter.h \
    src/browser.h \
    src/hardware.h \
    src/theme.h \
    src/appmenu.h \
    src/analysis.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/job.cpp \
    src/mediainfo.cpp \
    src/portalfilepicker.cpp \
    src/preset.cpp \
    src/progress.cpp \
    src/telemetry.cpp \
    src/cyclecounter.cpp \
    src/browser.cpp \
    src/hardware.cpp \
    src/theme.cpp \
    src/appmenu.cpp \
    src/analysis.cpp

RESOURCES += src/resources.qrc
