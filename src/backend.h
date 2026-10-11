#pragma once

#include <QDBusUnixFileDescriptor>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QUrl>
#include <QVariantList>

#include "job.h"
#include "preset.h"
#include "hardware.h"
#include "telemetry.h"

class FilePicker;

class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QList<QObject *> jobs READ jobs NOTIFY jobsChanged)
    Q_PROPERTY(int selectedIndex READ selectedIndex WRITE setSelectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(QObject *selectedJob READ selectedJob NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString defaultPresetId READ defaultPresetId NOTIFY presetsChanged)
    Q_PROPERTY(QString outputMode READ outputMode NOTIFY settingsChanged)
    Q_PROPERTY(QString outputFolder READ outputFolder NOTIFY settingsChanged)
    // How output files are named, for every conversion (see naming.h).
    Q_PROPERTY(QString namePattern READ namePattern WRITE setNamePattern NOTIFY settingsChanged)
    Q_PROPERTY(QObject *telemetry READ telemetry CONSTANT)
    Q_PROPERTY(bool running READ running NOTIFY queueChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY queueChanged)
    Q_PROPERTY(bool inhibiting READ inhibiting NOTIFY queueChanged)
    Q_PROPERTY(bool hasActiveWork READ hasActiveWork NOTIFY queueChanged)
    Q_PROPERTY(int waitingCount READ waitingCount NOTIFY queueChanged)
    Q_PROPERTY(int doneCount READ doneCount NOTIFY queueChanged)
    Q_PROPERTY(double queuePercent READ queuePercent NOTIFY queueChanged)
    Q_PROPERTY(double queueRemaining READ queueRemaining NOTIFY queueChanged)
    Q_PROPERTY(QDateTime queueFinishAt READ queueFinishAt NOTIFY queueChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY jobsChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY jobsChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QObject *hardware READ hardwareObject CONSTANT)
    Q_PROPERTY(bool gpuEnabled READ gpuEnabled WRITE setGpuEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int parallel READ parallel WRITE setParallel NOTIFY settingsChanged)
    Q_PROPERTY(int autoParallel READ autoParallel CONSTANT)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY queueChanged)

public:
    explicit Backend(FilePicker *picker, const QString &configDir = {}, Telemetry *telemetry = nullptr,
                     QObject *parent = nullptr);
    ~Backend() override;

    QList<QObject *> jobs() const;
    int selectedIndex() const { return m_selected; }
    void setSelectedIndex(int index);
    QObject *selectedJob() const;
    QVariantList presets() const;
    QString defaultPresetId() const { return m_defaultPreset; }
    QString outputMode() const { return m_outputMode; }
    QString outputFolder() const { return m_outputFolder; }
    QString namePattern() const { return m_namePattern; }
    void setNamePattern(const QString &pattern);
    QObject *telemetry() const { return m_telemetry; }
    bool running() const { return m_running; }
    bool paused() const;
    bool inhibiting() const { return m_inhibit.isValid(); }
    bool hasActiveWork() const;
    int runningCount() const;
    int waitingCount() const;

    // GPU encoding is experimental and off until switched on. `hardware` is
    // what the machine was found to have.
    void setHardware(Hardware *hardware);
    QObject *hardwareObject() const;
    bool gpuEnabled() const { return m_gpuEnabled; }
    void setGpuEnabled(bool on);
    // How many files convert at once: 0 means automatic, sized to the machine.
    int parallel() const { return m_parallel; }
    void setParallel(int n);
    static int autoParallel();
    int effectiveParallel() const { return m_parallel > 0 ? m_parallel : autoParallel(); }
    // The encoder a job would run on now, and its VA-API device.
    QPair<QString, QString> engineFor(const Preset &preset) const;
    int doneCount() const;
    double queuePercent() const;
    double queueRemaining() const;
    QDateTime queueFinishAt() const;
    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    QString notice() const { return m_notice; }

    Q_INVOKABLE void addFilesDialog();
    Q_INVOKABLE void chooseOutputFolder();
    // Asks for a file for the job at `index`: subtitles to burn in, a LUT
    // or a logo (the last two go into its preset).
    Q_INVOKABLE void chooseFile(int index, const QString &kind);
    Q_INVOKABLE void useSourceFolders();
    Q_INVOKABLE void addUrls(const QVariantList &urls);
    void addPaths(const QStringList &paths);

    Q_INVOKABLE void toggleQueue();
    Q_INVOKABLE void startQueue();
    Q_INVOKABLE void cancelJob(int index);
    Q_INVOKABLE void removeJob(int index);
    Q_INVOKABLE void retryJob(int index);
    Q_INVOKABLE void moveJob(int index, int delta);
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    Q_INVOKABLE void setJobPreset(int index, const QString &presetId);
    Q_INVOKABLE void editJobPreset(int index, const QString &key, const QVariant &value);
    Q_INVOKABLE void applyToWaiting(int index);
    Q_INVOKABLE QString savePreset(int index, const QString &name);
    Q_INVOKABLE void deletePreset(const QString &id);

    Q_INVOKABLE void openOutput(int index);
    Q_INVOKABLE void revealOutput(int index);
    Q_INVOKABLE void copyText(const QString &text);
    Q_INVOKABLE void dismissNotice();

    Job *jobAt(int index) const;
    Preset presetById(const QString &id) const;
    QString folderFor(const QString &input) const;

signals:
    void jobsChanged();
    void selectionChanged();
    void presetsChanged();
    void settingsChanged();
    void queueChanged();
    void noticeChanged();
    void outputWritten(const QString &path);

private:
    void addJob(const QString &path);
    void startNext();
    void onJobFinished(Job *job, bool ok);
    void onTick();
    void resolveEngines();
    void setNotice(const QString &text);
    void setInhibit(bool on);
    void notify(const QString &title, const QString &body);
    void loadConfig();
    void saveConfig() const;
    void savePresets() const;

    FilePicker *m_picker;
    QString m_configDir;
    Telemetry *m_telemetry;
    bool m_ownsTelemetry = false;

    QList<Job *> m_jobs;
    int m_selected = -1;
    int m_nextId = 1;
    QList<Preset> m_userPresets;
    QString m_defaultPreset = "review";
    QString m_outputMode = "beside";   // "beside" the source when writable, or "folder"
    QString m_outputFolder;
    QString m_namePattern;
    bool m_running = false;
    QDBusUnixFileDescriptor m_inhibit;
    QString m_notice;
    QList<qint64> m_watchedPids;
    Hardware *m_hardware = nullptr;
    int m_fileFor = -1;         // the job id a file is being chosen for
    bool m_gpuEnabled = false;
    int m_parallel = 0;

    // Removed jobs with the position they came from, for undo.
    struct Removed { Job *job; int index; };
    QList<Removed> m_undo;
    QList<Removed> m_redo;
};
