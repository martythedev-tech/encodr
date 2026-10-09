#include "telemetry.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QThread>
#include <unistd.h>

namespace {
QByteArray readFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll().trimmed();
}
}

Telemetry::Telemetry(const QString &sysRoot, const QString &procRoot, QObject *parent)
    : QObject(parent), m_sys(sysRoot.isEmpty() ? "/sys" : sysRoot), m_proc(procRoot.isEmpty() ? "/proc" : procRoot),
      m_cycles(m_sys, m_proc) {
    m_coreCount = QThread::idealThreadCount();
    const long ticks = sysconf(_SC_CLK_TCK);
    if (ticks > 0)
        m_ticksPerSecond = ticks;
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &Telemetry::sample);
    m_timer.start();
    sample();
}

void Telemetry::setProcess(qint64 pid) {
    m_pid = pid;
    m_cycles.setProcess(pid);
    m_prevProcTicks = 0;
    m_prevProcAt = 0;
    m_processCpu = 0;
}

void Telemetry::sample() {
    sampleCpu();
    sampleProcess();
    sampleClocks();
    sampleSensors();
    emit updated();
}

void Telemetry::sampleCpu() {
    const auto lines = readFile(m_proc + "/stat").split('\n');
    QVector<quint64> busy, total;
    for (const auto &line : lines) {
        if (!line.startsWith("cpu"))
            continue;
        const auto fields = line.simplified().split(' ');
        if (fields.size() < 8)
            continue;
        quint64 sum = 0;
        for (int i = 1; i < fields.size(); ++i)
            sum += fields[i].toULongLong();
        const quint64 idle = fields[4].toULongLong() + fields[5].toULongLong();
        busy.append(sum - idle);
        total.append(sum);
    }
    if (busy.isEmpty())
        return;

    if (m_prevTotal.size() == total.size()) {
        auto load = [&](int i) {
            const double dt = double(total[i] - m_prevTotal[i]);
            return dt > 0 ? 100.0 * double(busy[i] - m_prevBusy[i]) / dt : 0.0;
        };
        m_cpuTotal = load(0);
        m_cores.clear();
        for (int i = 1; i < total.size(); ++i)
            m_cores.append(load(i));
    }
    m_prevBusy = busy;
    m_prevTotal = total;
}

void Telemetry::sampleProcess() {
    if (m_pid <= 0) {
        m_processCpu = 0;
        return;
    }
    const QByteArray stat = readFile(QString("%1/%2/stat").arg(m_proc).arg(m_pid));
    // The command name may contain spaces; the fields we want follow its ')'.
    const qsizetype close = stat.lastIndexOf(')');
    if (close < 0) {
        m_processCpu = 0;
        return;
    }
    const auto fields = stat.mid(close + 2).split(' ');
    if (fields.size() < 13)
        return;
    const quint64 ticks = fields[11].toULongLong() + fields[12].toULongLong();   // utime + stime
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_prevProcAt > 0 && now > m_prevProcAt) {
        const double seconds = (now - m_prevProcAt) / 1000.0;
        // As a share of the whole machine, so 100% means every core is busy.
        m_processCpu = 100.0 * double(ticks - m_prevProcTicks) / m_ticksPerSecond / seconds / std::max(1, m_coreCount);
    }
    m_prevProcTicks = ticks;
    m_prevProcAt = now;
}

void Telemetry::sampleClocks() {
    m_clocks.clear();
    const auto measured = m_cycles.sample();
    double ranMhz = 0, topMhz = 0;
    bool requestedLow = false;

    QDir dir(m_sys + "/devices/system/cpu/cpufreq");
    const auto policies = dir.entryList({"policy*"}, QDir::Dirs, QDir::Name);
    for (const auto &policy : policies) {
        const QString base = dir.filePath(policy);
        double cur = readFile(base + "/scaling_cur_freq").toDouble() / 1000.0;
        const double max = readFile(base + "/cpuinfo_max_freq").toDouble() / 1000.0;
        const QString cpus = QString::fromUtf8(readFile(base + "/related_cpus"));
        if (max <= 0)
            continue;
        // Prefer the clock the encoder actually got on these cores over the
        // one that was asked for.
        bool real = false;
        const QList<int> policyCpus = CycleCounter::parseCpuList(cpus);
        for (const auto &cluster : measured) {
            if (cluster.mhz <= 0 || policyCpus.isEmpty() || !cluster.cpus.contains(policyCpus.first()))
                continue;
            cur = std::min(cluster.mhz, max);
            real = true;
            ranMhz += cur * cluster.seconds;
            topMhz += max * cluster.seconds;
        }
        const bool low = m_cpuTotal > 80 && cur < max * 0.85;
        if (low && !real)
            requestedLow = true;
        m_clocks.append(QVariantMap{{"name", policy}, {"cpus", cpus}, {"mhz", cur}, {"maxMhz", max},
                                    {"measured", real}, {"low", low}});
    }

    m_clocksMeasured = topMhz > 0;
    m_clockShare = m_clocksMeasured ? ranMhz / topMhz : 0;
    // Only a busy machine running well below its top clock is throttling; an
    // idle one is just saving power. A little hysteresis keeps the warning
    // from flickering around the line.
    if (m_clocksMeasured) {
        const double limit = m_throttled ? 0.9 : 0.85;
        m_throttled = m_cpuTotal > 80 && m_clockShare < limit;
    } else {
        m_throttled = requestedLow;
    }
}

void Telemetry::sampleSensors() {
    m_temperatures.clear();
    m_power.clear();
    m_hottest = 0;
    m_systemWatts = 0;
    QDir dir(m_sys + "/class/hwmon");
    const QRegularExpression input("^(temp|power)(\\d+)_input$");
    for (const auto &hw : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString base = dir.filePath(hw);
        const QString chip = QString::fromUtf8(readFile(base + "/name"));
        for (const auto &file : QDir(base).entryList({"temp*_input", "power*_input"}, QDir::Files, QDir::Name)) {
            const auto m = input.match(file);
            if (!m.hasMatch())
                continue;
            const QByteArray raw = readFile(base + "/" + file);
            bool ok = false;
            const double value = raw.toDouble(&ok);
            if (!ok)
                continue;    // sensors that fault return an error, not a number
            QString label = QString::fromUtf8(readFile(QString("%1/%2%3_label").arg(base, m.captured(1), m.captured(2))));
            if (label.isEmpty() || label == "temp")
                label = chip;
            if (m.captured(1) == "temp") {
                const double c = value / 1000.0;
                if (c <= 0 || c > 150)
                    continue;
                m_temperatures.append(QVariantMap{{"label", label}, {"celsius", c}});
                m_hottest = std::max(m_hottest, c);
            } else {
                const double w = value / 1e6;
                if (w <= 0)
                    continue;
                m_power.append(QVariantMap{{"label", label}, {"watts", w}});
                if (label.contains("Total System", Qt::CaseInsensitive) || (m_systemWatts == 0 && label.contains("System")))
                    m_systemWatts = w;
            }
        }
    }
}
