#include "hardware.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>

namespace {
// Fastest first: a dedicated NVIDIA engine, then Intel's own API, then the
// generic one.
const QStringList kPreference = {"nvenc", "qsv", "vaapi"};

QString readFile(const QString &path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll().trimmed()) : QString();
}

QString vendorName(const QString &vendor) {
    if (vendor == "0x8086")
        return "Intel";
    if (vendor == "0x1002")
        return "AMD";
    if (vendor == "0x10de")
        return "NVIDIA";
    return "GPU";
}
}

QString Hardware::vendorOf(const QString &device, const QString &driSys) {
    if (device.isEmpty())
        return {};
    const QString root = driSys.isEmpty() ? QString("/sys/class/drm") : driSys;
    const QString name = vendorName(readFile(QString("%1/%2/device/vendor").arg(root, QFileInfo(device).fileName())));
    return name == "GPU" ? QString() : name.toLower();
}

Hardware::Hardware(const QString &driSys, const QString &devDir, QObject *parent)
    : QObject(parent), m_driSys(driSys.isEmpty() ? "/sys/class/drm" : driSys),
      m_devDir(devDir.isEmpty() ? "/dev/dri" : devDir) {}

Hardware::~Hardware() {
    for (QProcess *p : findChildren<QProcess *>()) {
        p->disconnect();
        p->kill();
        p->waitForFinished(1000);
    }
}

QString Hardware::encoderName(const QString &id, const QString &codec) {
    return codec + "_" + id;    // h264_nvenc, hevc_vaapi…
}

QString Hardware::explainFailure(const QString &id, const QByteArray &errors) {
    // ffmpeg built against a newer NVENC SDK than the installed driver:
    // "The minimum required Nvidia driver for nvenc is 610.00 or newer".
    static const QRegularExpression nvenc("minimum required Nvidia driver for nvenc is ([0-9.]+)");
    const auto m = nvenc.match(QString::fromUtf8(errors));
    if (id == "nvenc" && m.hasMatch())
        return QString("NVIDIA GPU found, but this ffmpeg's NVENC needs NVIDIA driver %1 or newer. Update the "
                       "driver, or use a distribution ffmpeg built for the one installed.")
            .arg(m.captured(1).section('.', 0, 0));
    return {};
}

QStringList Hardware::parseEncoderList(const QByteArray &output) {
    // Lines look like " V....D h264_nvenc   NVIDIA NVENC H.264 encoder".
    QStringList names;
    for (const QByteArray &line : output.split('\n')) {
        const QList<QByteArray> parts = line.simplified().split(' ');
        // The legend at the top (" V..... = Video") has the same shape.
        if (parts.size() >= 3 && parts[0].size() == 6 && parts[0].startsWith('V') && parts[1] != "=")
            names << QString::fromUtf8(parts[1]);
    }
    return names;
}

QStringList Hardware::testArguments(const QString &id, const QString &codec, const QString &device) {
    QStringList a{"-nostdin", "-hide_banner", "-loglevel", "error"};
    if (id == "vaapi")
        a << "-vaapi_device" << device;
    a << "-f" << "lavfi" << "-i" << "color=black:size=320x240:rate=25:duration=0.4";
    // Test what real encodes ask for: HEVC is always 10-bit Main 10, which
    // older GPUs (AMD before VCN, for one) can't do even though 8-bit works.
    const bool hevc = codec == "hevc";
    const QString format = hevc ? "p010le" : "nv12";
    if (id == "vaapi")
        a << "-vf" << "format=" + format + ",hwupload";
    else
        a << "-vf" << "format=" + format;
    a << "-c:v" << encoderName(id, codec) << "-profile:v" << (hevc ? "main10" : "high") << "-f" << "null" << "-";
    return a;
}

void Hardware::detect() {
    m_ready = false;
    m_encoders.clear();
    m_problems.clear();
    m_pending.clear();
    auto *list = new QProcess(this);
    connect(list, &QProcess::finished, this, [this, list](int, QProcess::ExitStatus) {
        list->deleteLater();
        const QStringList built = parseEncoderList(list->readAllStandardOutput());
        m_built = built;
        m_listed = true;

        // VA-API needs a render node; name it after the card's maker.
        QStringList nodes = QDir(m_devDir).entryList({"renderD*"}, QDir::System | QDir::Files, QDir::Name);
        for (const QString &codec : {QStringLiteral("h264"), QStringLiteral("hevc")}) {
            if (built.contains(encoderName("nvenc", codec)))
                m_pending << Candidate{"nvenc", codec, {}, "NVIDIA NVENC"};
            if (built.contains(encoderName("qsv", codec)))
                m_pending << Candidate{"qsv", codec, {}, "Intel Quick Sync"};
            if (built.contains(encoderName("vaapi", codec)))
                for (const QString &node : nodes) {
                    const QString vendor = readFile(QString("%1/%2/device/vendor").arg(m_driSys, node));
                    m_pending << Candidate{"vaapi", codec, m_devDir + "/" + node, vendorName(vendor) + " VA-API"};
                }
        }
        runNext();
    });
    connect(list, &QProcess::errorOccurred, this, [this, list](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        list->deleteLater();
        runNext();    // no ffmpeg: nothing to find
    });
    list->start("ffmpeg", {"-hide_banner", "-encoders"});
}

void Hardware::runNext() {
    // A family already proven on one VA-API node doesn't need the others.
    while (!m_pending.isEmpty()) {
        const Candidate c = m_pending.first();
        bool known = false;
        for (const Encoder &e : std::as_const(m_encoders))
            known = known || (e.id == c.id && e.codecs.contains(c.codec));
        if (!known)
            break;
        m_pending.removeFirst();
    }
    if (m_pending.isEmpty()) {
        std::sort(m_encoders.begin(), m_encoders.end(), [](const Encoder &a, const Encoder &b) {
            return kPreference.indexOf(a.id) < kPreference.indexOf(b.id);
        });
        m_ready = true;
        emit changed();
        return;
    }

    const Candidate c = m_pending.takeFirst();
    auto *test = new QProcess(this);
    // A broken driver can hang instead of failing; give up on it.
    QTimer::singleShot(10000, test, [test] { test->kill(); });
    connect(test, &QProcess::finished, this, [this, test, c](int code, QProcess::ExitStatus status) {
        test->deleteLater();
        if (status == QProcess::NormalExit && code == 0) {
            auto it = std::find_if(m_encoders.begin(), m_encoders.end(), [&](const Encoder &e) {
                return e.id == c.id && e.device == c.device;
            });
            if (it == m_encoders.end())
                m_encoders << Encoder{c.id, c.name, c.device, {c.codec}};
            else
                it->codecs << c.codec;
        } else {
            const QString why = explainFailure(c.id, test->readAllStandardError());
            if (!why.isEmpty() && !m_problems.contains(why))
                m_problems << why;
        }
        runNext();
    });
    test->start("ffmpeg", testArguments(c.id, c.codec, c.device));
}

QVariantList Hardware::encodersVariant() const {
    QVariantList list;
    for (const Encoder &e : m_encoders)
        list << QVariantMap{{"id", e.id}, {"name", e.name}, {"device", e.device}, {"codecs", e.codecs}};
    return list;
}

void Hardware::setBuiltEncoders(const QStringList &names) {
    m_built = names;
    m_listed = true;
    emit changed();
}

void Hardware::setEncoders(const QList<Encoder> &encoders) {
    m_encoders = encoders;
    m_ready = true;
    emit changed();
}

QString Hardware::best(const QString &codec) const {
    for (const Encoder &e : m_encoders)
        if (e.codecs.contains(codec))
            return e.id;
    return {};
}

QString Hardware::deviceFor(const QString &id) const {
    for (const Encoder &e : m_encoders)
        if (e.id == id)
            return e.device;
    return {};
}

QString Hardware::nameFor(const QString &id) const {
    for (const Encoder &e : m_encoders)
        if (e.id == id)
            return e.name;
    return {};
}
