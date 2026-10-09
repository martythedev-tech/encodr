#include "cyclecounter.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>

#include <cstring>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace {
QString readFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll().trimmed());
}

// The generic "CPU cycles" event on one particular PMU: the PMU's type goes in
// the top half of the config, which is how the kernel tells apart the
// performance and efficiency cores' counters on hybrid chips.
int openCycles(int tid, quint32 pmuType) {
    perf_event_attr attr;
    std::memset(&attr, 0, sizeof attr);
    attr.size = sizeof attr;
    attr.type = PERF_TYPE_HARDWARE;
    attr.config = (quint64(pmuType) << 32) | PERF_COUNT_HW_CPU_CYCLES;
    attr.exclude_kernel = 1;
    attr.exclude_hv = 1;
    attr.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    return int(syscall(SYS_perf_event_open, &attr, tid, -1, -1, PERF_FLAG_FD_CLOEXEC));
}
}

QList<int> CycleCounter::parseCpuList(const QString &list) {
    QList<int> cpus;
    for (const QString &part : list.split(QRegularExpression("[,\\s]+"), Qt::SkipEmptyParts)) {
        const auto ends = part.split('-');
        bool ok1 = false, ok2 = true;
        const int from = ends[0].toInt(&ok1);
        const int to = ends.size() > 1 ? ends[1].toInt(&ok2) : from;
        if (ok1 && ok2)
            for (int c = from; c <= to; ++c)
                cpus.append(c);
    }
    return cpus;
}

CycleCounter::CycleCounter(const QString &sysRoot, const QString &procRoot)
    : m_proc(procRoot.isEmpty() ? "/proc" : procRoot) {
    // Each core type has its own PMU that lists the cores it covers.
    QDir dir((sysRoot.isEmpty() ? "/sys" : sysRoot) + "/bus/event_source/devices");
    for (const QString &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString base = dir.filePath(name);
        if (!QFile::exists(base + "/events/cycles") && !QFile::exists(base + "/events/cpu-cycles"))
            continue;
        const QList<int> cpus = parseCpuList(readFile(base + "/cpus"));
        bool ok = false;
        const quint32 type = readFile(base + "/type").toUInt(&ok);
        if (ok && !cpus.isEmpty())
            m_pmus.append({type, cpus});
    }
}

CycleCounter::~CycleCounter() {
    closeAll();
}

void CycleCounter::closeAll() {
    for (const auto &counters : std::as_const(m_threads))
        for (const Counter &c : counters)
            if (c.fd >= 0)
                ::close(c.fd);
    m_threads.clear();
}

void CycleCounter::setProcess(qint64 pid) {
    closeAll();
    m_pid = pid;
}

QVector<CycleCounter::Cluster> CycleCounter::sample() {
    QVector<Cluster> clusters(m_pmus.size());
    for (int i = 0; i < m_pmus.size(); ++i)
        clusters[i].cpus = m_pmus[i].cpus;
    if (m_pid <= 0 || m_pmus.isEmpty())
        return clusters;

    // ffmpeg starts and ends worker threads as it goes, so follow them.
    const QStringList tids = QDir(QString("%1/%2/task").arg(m_proc).arg(m_pid)).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    QSet<int> alive;
    for (const QString &t : tids) {
        const int tid = t.toInt();
        if (tid <= 0)
            continue;
        alive.insert(tid);
        if (m_threads.contains(tid))
            continue;
        QVector<Counter> counters(m_pmus.size());
        for (int i = 0; i < m_pmus.size(); ++i)
            counters[i].fd = openCycles(tid, m_pmus[i].type);
        m_threads.insert(tid, counters);
    }

    QVector<quint64> cycles(m_pmus.size(), 0), running(m_pmus.size(), 0);
    for (auto it = m_threads.begin(); it != m_threads.end();) {
        for (int i = 0; i < m_pmus.size(); ++i) {
            Counter &c = it.value()[i];
            quint64 values[3] = {0, 0, 0};    // count, time enabled, time running
            if (c.fd < 0 || ::read(c.fd, values, sizeof values) != sizeof values)
                continue;
            cycles[i] += values[0] - c.cycles;
            running[i] += values[2] - c.running;
            c.cycles = values[0];
            c.running = values[2];
        }
        // A finished thread's last counts are in; let its counters go.
        if (!alive.contains(it.key())) {
            for (const Counter &c : it.value())
                if (c.fd >= 0)
                    ::close(c.fd);
            it = m_threads.erase(it);
        } else {
            ++it;
        }
    }

    for (int i = 0; i < m_pmus.size(); ++i) {
        clusters[i].seconds = running[i] / 1e9;
        // A sliver of time on a cluster gives a meaningless average.
        if (running[i] > 50'000'000)
            clusters[i].mhz = double(cycles[i]) / (running[i] / 1e3);
    }
    return clusters;
}
