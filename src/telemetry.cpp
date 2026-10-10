#include "telemetry.h"
#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QProcess>
#include <QStandardPaths>
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
    // NVIDIA's driver keeps its numbers to itself; nvidia-smi reads them.
    m_nvidiaSmi = sysRoot.isEmpty() ? QStandardPaths::findExecutable("nvidia-smi") : QString();
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &Telemetry::sample);
    m_timer.start();
    sample();
}

void Telemetry::setProcesses(const QList<qint64> &pids) {
    if (pids == m_pids)
        return;
    m_pids = pids;
    m_cycles.setProcesses(pids);
    m_prevProcTicks.clear();
    m_prevProcAt = 0;
    m_processCpu = 0;
    m_processCpuByPid.clear();
}

void Telemetry::sample() {
    sampleCpu();
    sampleProcess();
    sampleClocks();
    sampleSensors();
    sampleGpus();
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
    if (m_pids.isEmpty()) {
        m_processCpu = 0;
        m_processCpuByPid.clear();
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double seconds = m_prevProcAt > 0 && now > m_prevProcAt ? (now - m_prevProcAt) / 1000.0 : 0;
    double total = 0;
    for (const qint64 pid : std::as_const(m_pids)) {
        const QByteArray stat = readFile(QString("%1/%2/stat").arg(m_proc).arg(pid));
        // The command name may contain spaces; the fields we want follow its ')'.
        const qsizetype close = stat.lastIndexOf(')');
        if (close < 0)
            continue;
        const auto fields = stat.mid(close + 2).split(' ');
        if (fields.size() < 13)
            continue;
        const quint64 ticks = fields[11].toULongLong() + fields[12].toULongLong();   // utime + stime
        if (seconds > 0 && m_prevProcTicks.contains(pid)) {
            // As a share of the whole machine, so 100% means every core is busy.
            const double share = 100.0 * double(ticks - m_prevProcTicks.value(pid)) / m_ticksPerSecond / seconds
                                 / std::max(1, m_coreCount);
            m_processCpuByPid.insert(pid, share);
            total += share;
        }
        m_prevProcTicks.insert(pid, ticks);
    }
    m_processCpu = total;
    m_prevProcAt = now;
}

void Telemetry::sampleClocks() {
    m_clocks.clear();
    const auto measured = m_cycles.sample();
    double ranMhz = 0, topMhz = 0;
    bool requestedLow = false;

    QDir dir(m_sys + "/devices/system/cpu/cpufreq");
    // By number: policy2 before policy10, which a name sort gets wrong.
    auto policies = dir.entryList({"policy*"}, QDir::Dirs);
    std::sort(policies.begin(), policies.end(), [](const QString &a, const QString &b) {
        return a.mid(6).toInt() < b.mid(6).toInt();
    });
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

namespace {
double numberOr(const QByteArray &raw, double fallback = -1) {
    bool ok = false;
    const double v = raw.trimmed().toDouble(&ok);
    return ok ? v : fallback;
}
}

QVariantList Telemetry::parseNvidiaSmi(const QByteArray &output, bool withEncoder) {
    QVariantList gpus;
    for (const QByteArray &line : output.split('\n')) {
        const QList<QByteArray> f = line.split(',');
        const int need = withEncoder ? 7 : 6;
        if (f.size() < need)
            continue;
        int i = 0;
        QVariantMap g;
        g["name"] = QString::fromUtf8(f[i++].trimmed());
        g["busy"] = numberOr(f[i++]);
        g["encoder"] = withEncoder ? numberOr(f[i++]) : -1.0;
        g["mhz"] = numberOr(f[i++]);
        g["maxMhz"] = numberOr(f[i++]);
        g["celsius"] = numberOr(f[i++]);
        g["watts"] = numberOr(f[i++]);
        g["vendor"] = QStringLiteral("nvidia");
        gpus << g;
    }
    return gpus;
}

void Telemetry::queryNvidia() {
    if (m_nvidiaSmi.isEmpty() || m_nvidiaBusy)
        return;
    m_nvidiaBusy = true;
    const bool withEncoder = m_nvidiaEncoderField;
    const QString fields = QString("name,utilization.gpu,%1clocks.gr,clocks.max.gr,temperature.gpu,power.draw")
                               .arg(withEncoder ? "utilization.encoder," : "");
    auto *smi = new QProcess(this);
    connect(smi, &QProcess::finished, this, [this, smi, withEncoder](int code, QProcess::ExitStatus) {
        smi->deleteLater();
        m_nvidiaBusy = false;
        if (code != 0 && withEncoder) {
            m_nvidiaEncoderField = false;    // ask again without it next time
            return;
        }
        m_nvidiaGpus = code == 0 ? parseNvidiaSmi(smi->readAllStandardOutput(), withEncoder) : QVariantList();
    });
    connect(smi, &QProcess::errorOccurred, this, [this, smi](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            smi->deleteLater();
            m_nvidiaBusy = false;
            m_nvidiaSmi.clear();
        }
    });
    smi->start(m_nvidiaSmi, {"--query-gpu=" + fields, "--format=csv,noheader,nounits"});
}

// AMD and Intel publish their load and clocks in sysfs; Apple's GPU under
// Asahi and NVIDIA's (through DRM) don't, so those are skipped here.
void Telemetry::sampleGpus() {
    if (++m_ticks % 2 == 1)
        queryNvidia();

    m_drmGpus.clear();
    QDir drm(m_sys + "/class/drm");
    static const QRegularExpression card("^card\\d+$");
    for (const QString &name : drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!card.match(name).hasMatch())
            continue;
        const QString base = drm.filePath(name);
        const QString device = base + "/device";
        QString driver;
        for (const QByteArray &line : readFile(device + "/uevent").split('\n'))
            if (line.startsWith("DRIVER="))
                driver = QString::fromUtf8(line.mid(7));
        double busy = -1, mhz = -1, maxMhz = -1, celsius = -1, watts = -1;
        QString label, vendor = "intel";
        if (driver == "amdgpu") {
            label = "AMD GPU";
            vendor = "amd";
            busy = numberOr(readFile(device + "/gpu_busy_percent"));
            // "0: 500Mhz", "1: 2250Mhz *": the starred level is the current one.
            for (const QByteArray &line : readFile(device + "/pp_dpm_sclk").split('\n')) {
                const QByteArray level = line.mid(line.indexOf(':') + 1).trimmed();
                const double value = numberOr(level.left(level.indexOf("Mhz")));
                if (value <= 0)
                    continue;
                maxMhz = std::max(maxMhz, value);
                if (line.trimmed().endsWith('*'))
                    mhz = value;
            }
        } else if (driver == "i915") {
            label = "Intel GPU";
            mhz = numberOr(readFile(base + "/gt_act_freq_mhz"));
            if (mhz < 0)
                mhz = numberOr(readFile(base + "/gt_cur_freq_mhz"));
            maxMhz = numberOr(readFile(base + "/gt_max_freq_mhz"));
        } else if (driver == "xe") {
            label = "Intel GPU";
            mhz = numberOr(readFile(device + "/tile0/gt0/freq0/act_freq"));
            maxMhz = numberOr(readFile(device + "/tile0/gt0/freq0/max_freq"));
        } else {
            continue;
        }
        // The card's own temperature and power, from its hwmon directory.
        const QStringList mons = QDir(device + "/hwmon").entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        if (!mons.isEmpty()) {
            const QString mon = device + "/hwmon/" + mons.first();
            const double t = numberOr(readFile(mon + "/temp1_input"));
            if (t > 0)
                celsius = t / 1000.0;
            double p = numberOr(readFile(mon + "/power1_average"));
            if (p < 0)
                p = numberOr(readFile(mon + "/power1_input"));
            if (p > 0)
                watts = p / 1e6;
        }
        if (busy < 0 && mhz < 0)
            continue;
        // The PCI device ties this card to its render node (renderD128 and
        // card1 are the same device), so a VA-API job can find its GPU.
        m_drmGpus << QVariantMap{{"name", label}, {"busy", busy}, {"encoder", -1.0}, {"mhz", mhz},
                                 {"maxMhz", maxMhz}, {"celsius", celsius}, {"watts", watts},
                                 {"vendor", vendor}, {"pci", QFileInfo(device).canonicalFilePath()}};
    }
    m_gpus = m_nvidiaGpus + m_drmGpus;
}

QVariantMap Telemetry::gpuFor(const QString &engine, const QString &device) const {
    QString pci;
    if (engine == "vaapi" && !device.isEmpty())
        pci = QFileInfo(m_sys + "/class/drm/" + QFileInfo(device).fileName() + "/device").canonicalFilePath();
    for (const QVariant &v : m_gpus) {
        const QVariantMap g = v.toMap();
        const QString vendor = g.value("vendor").toString();
        if ((engine == "nvenc" && vendor == "nvidia") || (engine == "qsv" && vendor == "intel")
            || (engine == "vaapi" && !pci.isEmpty() && g.value("pci").toString() == pci))
            return g;
    }
    return {};
}
