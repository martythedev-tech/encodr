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
    src/theme.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/job.cpp \
    src/mediainfo.cpp \
    src/portalfilepicker.cpp \
    src/preset.cpp \
    src/progress.cpp \
    src/telemetry.cpp \
    src/theme.cpp

RESOURCES += src/resources.qrc
