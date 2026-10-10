#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QVector>

// Measures how fast a process's threads really run, by counting CPU cycles
// with the kernel's perf events. The clock speed in cpufreq is only what was
// requested: on Apple Silicon it reads the maximum even while the chip has
// cut its clocks by a third to cool down, which is exactly what we want to
// show. Counting our own child's user-space cycles needs no privileges.
class CycleCounter {
public:
    struct Cluster {
        QList<int> cpus;     // which cores this counter covers
        double mhz = 0;      // measured over the last interval; 0 = didn't run there
        double seconds = 0;  // thread time spent on these cores in that interval
    };

    // `sysRoot` is where the PMU list lives (normally /sys); tests point it elsewhere.
    explicit CycleCounter(const QString &sysRoot = {}, const QString &procRoot = {});
    ~CycleCounter();

    bool available() const { return !m_pmus.isEmpty(); }
    void setProcess(qint64 pid) { setProcesses(pid > 0 ? QList<qint64>{pid} : QList<qint64>{}); }
    void setProcesses(const QList<qint64> &pids);
    // Opens counters for any new threads, then reads every counter and
    // returns the speed per cluster since the previous call.
    QVector<Cluster> sample();

    static QList<int> parseCpuList(const QString &list);

private:
    struct Pmu {
        quint32 type = 0;
        QList<int> cpus;
    };
    struct Counter {
        int fd = -1;
        quint64 cycles = 0;
        quint64 running = 0;
    };
    void closeAll();

    QString m_proc;
    QVector<Pmu> m_pmus;
    QList<qint64> m_pids;
    QHash<int, QVector<Counter>> m_threads;    // tid -> one counter per PMU
};
