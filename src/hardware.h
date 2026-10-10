#pragma once

#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantList>

// Finds the GPU video encoders this machine can really use. ffmpeg lists the
// encoders it was built with, but whether one works depends on the card and
// its driver, so each candidate encodes a few frames before it counts.
//
// The families: NVIDIA's NVENC, Intel's Quick Sync (QSV), and VA-API, which
// covers Intel and AMD through the open drivers.
class Hardware : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList encoders READ encodersVariant NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)

public:
    struct Encoder {
        QString id;          // nvenc, qsv, vaapi
        QString name;        // "NVIDIA NVENC", "Intel VA-API"…
        QString device;      // the render node, for VA-API
        QStringList codecs;  // h264, hevc: the ones that passed the test
    };

    // `driSys` is where the render nodes are described (normally /sys/class/drm),
    // `devDir` where they live (normally /dev/dri); swappable for tests.
    explicit Hardware(const QString &driSys = {}, const QString &devDir = {}, QObject *parent = nullptr);
    ~Hardware() override;

    // Runs in the background; `changed` fires when it's done.
    void detect();
    bool ready() const { return m_ready; }
    QList<Encoder> encoders() const { return m_encoders; }
    QVariantList encodersVariant() const;

    // The fastest family that passed for this codec, or "" for none.
    QString best(const QString &codec) const;
    // What detect() found; tests set it directly to stand in for a GPU.
    void setEncoders(const QList<Encoder> &encoders);
    QString deviceFor(const QString &id) const;
    QString nameFor(const QString &id) const;

    // Used directly by tests: names from `ffmpeg -encoders`, and the command
    // that proves one encoder works.
    static QStringList parseEncoderList(const QByteArray &output);
    static QStringList testArguments(const QString &id, const QString &codec, const QString &device);
    static QString encoderName(const QString &id, const QString &codec);

signals:
    void changed();

private:
    struct Candidate {
        QString id;
        QString codec;
        QString device;
        QString name;
    };
    void runNext();

    QString m_driSys;
    QString m_devDir;
    QList<Candidate> m_pending;
    QList<Encoder> m_encoders;
    bool m_ready = false;
};
