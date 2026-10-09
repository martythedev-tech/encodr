#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVector>

#include "cyclecounter.h"

// Samples the machine once a second while it's being watched: CPU load per
// core, the encoder process's own CPU use, clock speeds against their maximum
// (measured from the encoder's CPU cycles where the kernel allows, to spot
// thermal throttling), and whatever temperature and power sensors the kernel
// exposes. Every source is optional; missing ones simply don't show.
class Telemetry : public QObject {
    Q_OBJECT
    Q_PROPERTY(double cpuTotal READ cpuTotal NOTIFY updated)
    Q_PROPERTY(QVariantList cores READ cores NOTIFY updated)
    Q_PROPERTY(double processCpu READ processCpu NOTIFY updated)
    Q_PROPERTY(QVariantList clocks READ clocks NOTIFY updated)
    Q_PROPERTY(bool throttled READ throttled NOTIFY updated)
    Q_PROPERTY(double clockShare READ clockShare NOTIFY updated)
    Q_PROPERTY(bool clocksMeasured READ clocksMeasured NOTIFY updated)
    Q_PROPERTY(QVariantList temperatures READ temperatures NOTIFY updated)
    Q_PROPERTY(QVariantList power READ power NOTIFY updated)
    Q_PROPERTY(double hottest READ hottest NOTIFY updated)
    Q_PROPERTY(double systemWatts READ systemWatts NOTIFY updated)
    Q_PROPERTY(int coreCount READ coreCount CONSTANT)

public:
    explicit Telemetry(const QString &sysRoot = {}, const QString &procRoot = {}, QObject *parent = nullptr);

    double cpuTotal() const { return m_cpuTotal; }
    QVariantList cores() const { return m_cores; }
    double processCpu() const { return m_processCpu; }
    QVariantList clocks() const { return m_clocks; }
    bool throttled() const { return m_throttled; }
    // How fast the encoder's cores really ran against their top speed, 0-1
    // (0 when nothing was measured).
    double clockShare() const { return m_clockShare; }
    bool clocksMeasured() const { return m_clocksMeasured; }
    QVariantList temperatures() const { return m_temperatures; }
    QVariantList power() const { return m_power; }
    double hottest() const { return m_hottest; }
    double systemWatts() const { return m_systemWatts; }
    int coreCount() const { return m_coreCount; }

    void setProcess(qint64 pid);
    Q_INVOKABLE void sample();

signals:
    void updated();

private:
    void sampleCpu();
    void sampleProcess();
    void sampleClocks();
    void sampleSensors();

    QString m_sys;
    QString m_proc;
    QTimer m_timer;
    int m_coreCount = 0;
    long m_ticksPerSecond = 100;

    QVector<quint64> m_prevBusy;
    QVector<quint64> m_prevTotal;
    qint64 m_pid = 0;
    quint64 m_prevProcTicks = 0;
    qint64 m_prevProcAt = 0;

    double m_cpuTotal = 0;
    QVariantList m_cores;
    double m_processCpu = 0;
    QVariantList m_clocks;
    bool m_throttled = false;
    double m_clockShare = 0;
    bool m_clocksMeasured = false;
    CycleCounter m_cycles;
    QVariantList m_temperatures;
    QVariantList m_power;
    double m_hottest = 0;
    double m_systemWatts = 0;
};
