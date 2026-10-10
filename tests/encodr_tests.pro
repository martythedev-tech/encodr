QT += core gui quick quickcontrols2 testlib dbus
CONFIG += c++17 testcase
TARGET = encodr_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += ../src/backend.h ../src/filepicker.h ../src/job.h ../src/mediainfo.h ../src/preset.h \
           ../src/progress.h ../src/telemetry.h ../src/cyclecounter.h ../src/browser.h ../src/hardware.h ../src/theme.h ../src/appmenu.h
SOURCES += encodr_tests.cpp ../src/backend.cpp ../src/job.cpp ../src/mediainfo.cpp ../src/preset.cpp \
           ../src/progress.cpp ../src/telemetry.cpp ../src/cyclecounter.cpp ../src/browser.cpp ../src/hardware.cpp ../src/theme.cpp ../src/appmenu.cpp
RESOURCES += ../src/resources.qrc
