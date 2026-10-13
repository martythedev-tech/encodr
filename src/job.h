#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <deque>

#include "analysis.h"
#include "mediainfo.h"
#include "preset.h"
#include "progress.h"

// One file in the queue: what it is, how it will be converted, and — while it
// runs — every number worth showing about the conversion.
class Job : public QObject {
    Q_OBJECT
    Q_PROPERTY(int id READ id CONSTANT)
    Q_PROPERTY(QString inputPath READ inputPath CONSTANT)
    Q_PROPERTY(QString fileName READ fileName CONSTANT)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap info READ infoVariant NOTIFY infoChanged)
    Q_PROPERTY(QVariantList issues READ issues NOTIFY infoChanged)
    // A better preset for this file, when there is one: {title, detail,
    // presetId, action}; empty otherwise.
    Q_PROPERTY(QVariantMap suggestion READ suggestion NOTIFY presetChanged)
    Q_PROPERTY(bool levelsScanned READ levelsScanned NOTIFY infoChanged)
    Q_PROPERTY(QString mediainfoReport READ mediainfoReport NOTIFY infoChanged)
    Q_PROPERTY(QVariantMap preset READ presetVariant NOTIFY presetChanged)
    Q_PROPERTY(QString presetProblem READ presetProblemText NOTIFY presetChanged)
    Q_PROPERTY(QString outputFolder READ outputFolder WRITE setOutputFolder NOTIFY presetChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY presetChanged)
    Q_PROPERTY(QString commandLine READ commandLine NOTIFY presetChanged)
    // Fit to size: the picture's bitrate in kbps for this file (0 = no size set).
    Q_PROPERTY(int fitKbps READ fitKbps NOTIFY presetChanged)
    // …held at the source's own rate, the source being under the size already.
    Q_PROPERTY(bool fitAtSourceRate READ fitAtSourceRate NOTIFY presetChanged)
    Q_PROPERTY(QString engine READ engine NOTIFY presetChanged)
    Q_PROPERTY(QString engineDevice READ engineDevice NOTIFY presetChanged)
    Q_PROPERTY(QStringList notes READ notes NOTIFY presetChanged)

    Q_PROPERTY(double percent READ percent NOTIFY statsChanged)
    Q_PROPERTY(double elapsed READ elapsed NOTIFY statsChanged)
    Q_PROPERTY(double remaining READ remaining NOTIFY statsChanged)
    Q_PROPERTY(QDateTime finishAt READ finishAt NOTIFY statsChanged)
    Q_PROPERTY(QDateTime startedAt READ startedAt NOTIFY statsChanged)
    Q_PROPERTY(QDateTime finishedAt READ finishedAt NOTIFY statsChanged)
    Q_PROPERTY(double speed READ speed NOTIFY statsChanged)
    Q_PROPERTY(double averageSpeed READ averageSpeed NOTIFY statsChanged)
    Q_PROPERTY(double fps READ fps NOTIFY statsChanged)
    Q_PROPERTY(double averageFps READ averageFps NOTIFY statsChanged)
    Q_PROPERTY(qint64 frame READ frame NOTIFY statsChanged)
    Q_PROPERTY(qint64 totalFrames READ totalFrames NOTIFY infoChanged)
    Q_PROPERTY(double bitrateKbps READ bitrateKbps NOTIFY statsChanged)
    Q_PROPERTY(double averageBitrateKbps READ averageBitrateKbps NOTIFY statsChanged)
    Q_PROPERTY(qint64 size READ size NOTIFY statsChanged)
    Q_PROPERTY(qint64 projectedSize READ projectedSize NOTIFY statsChanged)
    Q_PROPERTY(double outTime READ outTime NOTIFY statsChanged)
    Q_PROPERTY(double quantizer READ quantizer NOTIFY statsChanged)
    Q_PROPERTY(qint64 droppedFrames READ droppedFrames NOTIFY statsChanged)
    Q_PROPERTY(qint64 duplicatedFrames READ duplicatedFrames NOTIFY statsChanged)
    Q_PROPERTY(double energyWh READ energyWh NOTIFY statsChanged)
    Q_PROPERTY(double peakCpu READ peakCpu NOTIFY statsChanged)
    Q_PROPERTY(double cpu READ cpu NOTIFY statsChanged)
    Q_PROPERTY(double clockShare READ clockShare NOTIFY statsChanged)
    Q_PROPERTY(double lowestClockShare READ lowestClockShare NOTIFY statsChanged)
    Q_PROPERTY(int heatSeconds READ heatSeconds NOTIFY statsChanged)
    Q_PROPERTY(qint64 freeSpace READ freeSpace NOTIFY statsChanged)
    Q_PROPERTY(QVariantMap history READ history NOTIFY statsChanged)
    Q_PROPERTY(QString previewOut READ previewOut CONSTANT)
    Q_PROPERTY(QString previewSource READ previewSource CONSTANT)
    Q_PROPERTY(int previewStamp READ previewStamp NOTIFY previewChanged)
    Q_PROPERTY(bool hasLivePreview READ hasLivePreview NOTIFY previewChanged)
    Q_PROPERTY(double posterTime READ posterTime NOTIFY previewChanged)
    // One frame through the conversion's picture processing (the player's
    // Output view before converting); the latest request wins.
    Q_PROPERTY(bool canShowOutput READ canShowOutput NOTIFY presetChanged)
    Q_PROPERTY(QString outputStill READ outputStill CONSTANT)
    Q_PROPERTY(int outputStillStamp READ outputStillStamp NOTIFY outputStillChanged)
    Q_PROPERTY(double outputStillTime READ outputStillTime NOTIFY outputStillChanged)
    Q_PROPERTY(bool outputStillBusy READ outputStillBusy NOTIFY outputStillChanged)
    Q_PROPERTY(QString outputStillError READ outputStillError NOTIFY outputStillChanged)
    // The check after converting (see quality.h): "", "checking", "done" or
    // "failed"; what it's doing, how far, and what it found:
    // {measured, metric, tool, mean, min, low, verdict, subsample, worst:
    // [{time, score}], curve: [score…], videoNote, audio: [{track, state,
    // text}], report}.
    Q_PROPERTY(QString qualityState READ qualityState NOTIFY qualityChanged)
    Q_PROPERTY(QString qualityStep READ qualityStep NOTIFY qualityChanged)
    Q_PROPERTY(double qualityPercent READ qualityPercent NOTIFY qualityChanged)
    Q_PROPERTY(QVariantMap quality READ quality NOTIFY qualityChanged)
    // A frame of the finished file, for comparing with the Output still.
    Q_PROPERTY(QString finishedStill READ finishedStill CONSTANT)
    Q_PROPERTY(int finishedStillStamp READ finishedStillStamp NOTIFY finishedStillChanged)
    Q_PROPERTY(double finishedStillTime READ finishedStillTime NOTIFY finishedStillChanged)
    // Thumbnails across the whole source, for the timeline.
    Q_PROPERTY(QString strip READ strip CONSTANT)
    Q_PROPERTY(int stripCount READ stripCount CONSTANT)
    Q_PROPERTY(bool hasStrip READ hasStrip NOTIFY previewChanged)
    // The first audio track's loudness across the file: 0-1 per slice of the
    // timeline, empty until it's been read (or when there's no audio).
    Q_PROPERTY(QVariantList waveform READ waveform NOTIFY waveformChanged)
    // The whole-file check: black, freezes, silence, cuts.
    Q_PROPERTY(bool analysing READ analysing NOTIFY analysisChanged)
    Q_PROPERTY(bool analysed READ analysed NOTIFY analysisChanged)
    Q_PROPERTY(double analysisPercent READ analysisPercent NOTIFY analysisChanged)
    Q_PROPERTY(QVariantList moments READ moments NOTIFY analysisChanged)
    Q_PROPERTY(int cutCount READ cutCount NOTIFY analysisChanged)
    // Crop: off, bars (detected black bars), 1:1, 9:16, 4:5, 16:9 (centred)
    // or custom; the margins are pixels off each edge of the source.
    Q_PROPERTY(QString cropMode READ cropMode NOTIFY cropChanged)
    Q_PROPERTY(int cropLeft READ cropLeft NOTIFY cropChanged)
    Q_PROPERTY(int cropTop READ cropTop NOTIFY cropChanged)
    Q_PROPERTY(int cropRight READ cropRight NOTIFY cropChanged)
    Q_PROPERTY(int cropBottom READ cropBottom NOTIFY cropChanged)
    Q_PROPERTY(bool detectingBars READ detectingBars NOTIFY cropChanged)
    Q_PROPERTY(bool barsChecked READ barsChecked NOTIFY cropChanged)
    // Bars added to reach an aspect after crop and scaling: "off" or the
    // aspect ("9:16"); filled "black" or with the picture blurred ("blur").
    Q_PROPERTY(QString padMode READ padMode NOTIFY padChanged)
    Q_PROPERTY(QString padFill READ padFill NOTIFY padChanged)
    // Subtitles burned into the picture: "off", "track:N" (a text subtitle
    // track in the source) or a subtitle file's path.
    Q_PROPERTY(QString subtitleBurn READ subtitleBurn NOTIFY subtitlesChanged)
    Q_PROPERTY(QVariantList subtitleChoices READ subtitleChoices NOTIFY subtitlesChanged)
    // Clips joined on after this one (the queue's Join): how many in all,
    // [{name, path, duration, summary}] for each, and how they differ
    // (empty when they match and are read back to back as one file).
    Q_PROPERTY(int clipCount READ clipCount NOTIFY infoChanged)
    Q_PROPERTY(QVariantList clips READ clipsVariant NOTIFY infoChanged)
    Q_PROPERTY(QString joinDifference READ joinDifference NOTIFY infoChanged)
    Q_PROPERTY(double totalDuration READ length NOTIFY infoChanged)
    Q_PROPERTY(qint64 totalSize READ totalSize NOTIFY infoChanged)
    // The part of the source to convert: in and out points in seconds.
    Q_PROPERTY(double trimIn READ trimIn NOTIFY trimChanged)
    Q_PROPERTY(double trimOut READ trimOut NOTIFY trimChanged)
    Q_PROPERTY(bool trimmed READ trimmed NOTIFY trimChanged)

public:
    Job(int id, const QString &inputPath, QObject *parent = nullptr);
    ~Job() override;

    int id() const { return m_id; }
    QString inputPath() const { return m_input; }
    QString fileName() const;
    QString state() const { return m_state; }
    QString message() const { return m_message; }
    const MediaInfo &info() const { return m_info; }
    QVariantMap infoVariant() const { return m_info.toVariant(); }
    QVariantList issues() const;
    QVariantMap suggestion() const;
    bool levelsScanned() const { return m_levelsScanned; }
    QString mediainfoReport() const { return m_mediainfo; }
    const Preset &presetValue() const { return m_preset; }
    QVariantMap presetVariant() const { return m_preset.toVariant(); }
    QString presetProblemText() const;
    QString outputFolder() const { return m_outputFolder; }
    void setOutputFolder(const QString &folder);
    QString outputPath() const;
    // The naming pattern (see naming.h); set by the backend for every job.
    void setNamePattern(const QString &pattern);
    QString commandLine() const;
    int fitKbps() const { return m_info.valid ? fitVideoKbps(runPreset(), runInfo(), length()) : 0; }
    bool fitAtSourceRate() const { return fitKbps() > 0 && fitKbps() == sourceVideoKbps(runInfo()); }
    QStringList notes() const;

    double percent() const { return m_estimate.fraction * 100.0; }
    double elapsed() const;
    double remaining() const { return m_estimate.remaining; }
    QDateTime finishAt() const;
    QDateTime startedAt() const { return m_startedAt; }
    QDateTime finishedAt() const { return m_finishedAt; }
    double speed() const { return m_recentSpeed; }
    double averageSpeed() const { return m_estimate.averageSpeed; }
    double fps() const { return m_recentFps; }
    double averageFps() const;
    qint64 frame() const { return m_last.frame; }
    qint64 totalFrames() const { return m_info.hasVideo && m_info.fps > 0 ? qint64(std::llround(length() * m_info.fps)) : 0; }
    double bitrateKbps() const { return m_recentBitrate; }
    double averageBitrateKbps() const { return m_last.bitrateKbps; }
    qint64 size() const { return m_last.totalSize; }
    qint64 projectedSize() const;
    double outTime() const { return m_last.outTime; }
    double quantizer() const { return m_last.quantizer; }
    qint64 droppedFrames() const { return m_last.dropFrames; }
    qint64 duplicatedFrames() const { return m_last.dupFrames; }
    double energyWh() const { return m_energyWh; }
    double peakCpu() const { return m_peakCpu; }
    double cpu() const { return m_state == "running" ? m_pendingCpu : 0; }   // this job's share of the machine now
    double clockShare() const { return m_clockShare; }
    double lowestClockShare() const { return m_lowestClockShare; }
    int heatSeconds() const { return m_heatSeconds; }
    qint64 freeSpace() const { return m_freeSpace; }
    QVariantMap history() const;
    QString previewOut() const { return m_previewOut; }
    QString previewSource() const { return m_previewSource; }
    int previewStamp() const { return m_previewStamp; }
    bool hasLivePreview() const { return m_hasLivePreview; }
    double posterTime() const { return m_posterTime; }
    bool canShowOutput() const;
    QString outputStill() const { return m_outputStill; }
    int outputStillStamp() const { return m_stillStamp; }
    double outputStillTime() const { return m_stillTime; }
    bool outputStillBusy() const { return m_stillBusy || m_stillTimer.isActive(); }
    QString outputStillError() const { return m_stillError; }
    QString qualityState() const { return m_qualityState; }
    QString qualityStep() const { return m_qualityStep; }
    double qualityPercent() const { return m_qualityPercent; }
    QVariantMap quality() const { return m_quality; }
    Q_INVOKABLE void checkQuality();
    Q_INVOKABLE void stopQualityCheck();
    // Writes the check as a text file beside the output; returns its path.
    Q_INVOKABLE QString saveQualityReport();
    QString finishedStill() const { return m_finishedStill; }
    int finishedStillStamp() const { return m_finishedStamp; }
    double finishedStillTime() const { return m_finishedTime; }
    Q_INVOKABLE void showFinishedAt(double seconds);
    Q_INVOKABLE void showOutputAt(double seconds);
    QString strip() const { return m_strip; }
    int stripCount() const { return 16; }
    bool hasStrip() const { return m_hasStrip; }
    QVariantList waveform() const { return m_waveform; }
    bool analysing() const { return m_analysis; }
    bool analysed() const { return m_analysed; }
    double analysisPercent() const { return m_analysisPercent; }
    QVariantList moments() const { return momentsToVariant(m_moments); }
    int cutCount() const;
    Q_INVOKABLE void analyse();
    Q_INVOKABLE void stopAnalysis();
    // Writes the cuts as an EDL beside the output; returns its path, or
    // empty if it couldn't.
    Q_INVOKABLE QString saveEdl();

    QString cropMode() const { return m_cropMode; }
    int cropLeft() const { return m_crop.left(); }
    int cropTop() const { return m_crop.top(); }
    int cropRight() const { return m_crop.right(); }
    int cropBottom() const { return m_crop.bottom(); }
    bool detectingBars() const { return m_bars; }
    bool barsChecked() const { return m_barsChecked; }
    Q_INVOKABLE void setCropMode(const QString &mode);
    Q_INVOKABLE void setCropMargins(int left, int top, int right, int bottom);
    QString padMode() const { return m_padMode; }
    QString padFill() const { return m_padFill; }
    Q_INVOKABLE void setPadMode(const QString &mode);
    Q_INVOKABLE void setPadFill(const QString &fill);
    // The centred crop for an aspect ratio ("9:16"), on even pixels.
    static QMargins centredCrop(int width, int height, int across, int down);

    QString subtitleBurn() const { return m_subtitleBurn; }
    // None, each text track, subtitle files beside the source with its name
    // (film.srt, film.en.srt) and one picked by hand: [{value, text}].
    QVariantList subtitleChoices() const;
    Q_INVOKABLE void setSubtitleBurn(const QString &value);

    double trimIn() const { return m_trimIn; }
    double trimOut() const { return m_trimOut > 0 ? m_trimOut : m_info.duration; }
    bool trimmed() const { return m_trimIn > 0 || (m_trimOut > 0 && m_trimOut < m_info.duration); }
    // How much gets converted: the trimmed part, or every joined clip.
    double length() const;

    int clipCount() const { return 1 + int(m_clips.size()); }
    // The source's size, every joined clip's included.
    qint64 totalSize() const;
    bool joined() const { return !m_clips.isEmpty(); }
    QVariantList clipsVariant() const;
    QString joinDifference() const { return m_joinDifference; }
    // Joins these jobs' files on after this one, in order; the jobs become
    // this one's children until split. Trim and subtitle burn-in are for
    // single files, so they're cleared.
    void join(const QList<Job *> &clips);
    // Gives the joined jobs back, in order, parentless.
    QList<Job *> split();
    // The source as the conversion sees it: this file, or every joined clip
    // as one (their lengths added up; clips that differ keep one audio
    // track and no subtitles, as the join gives them).
    MediaInfo runInfo() const;
    Q_INVOKABLE void setTrim(double in, double out);
    Q_INVOKABLE void clearTrim() { setTrim(0, 0); }
    // Shows the source frame at this time in the preview (for setting the
    // in and out points); the latest request wins.
    Q_INVOKABLE void showFrameAt(double seconds);

    qint64 processId() const { return m_process ? m_process->processId() : 0; }
    bool isActive() const { return m_state == "running" || m_state == "paused"; }
    bool isWaiting() const { return m_state == "ready"; }
    bool isFinished() const { return m_state == "done" || m_state == "failed" || m_state == "cancelled"; }

    void setPreset(const Preset &preset);
    // Which encoder runs this job's H.264/HEVC ("cpu", "nvenc", "qsv",
    // "vaapi"), chosen by the backend from what the machine has.
    void setEngine(const QString &engine, const QString &device = {});
    QString engine() const { return m_engine; }
    QString engineDevice() const { return m_engineDevice; }
    // The preset as it will actually run, with the encoder filled in.
    Preset runPreset() const;
    void probe();
    bool start();
    void pause();
    void resume();
    void cancel();
    void reset();
    // Called by the backend once a second with the machine's readings.
    // Called once a second while converting. `clockShare` is how fast the
    // encoder's cores really ran against their top speed (0 = unknown).
    void recordSystem(double processCpu, double watts, double clockShare = 0, bool throttled = false);

    // The path ffmpeg writes to; renamed over the real name only on success,
    // so a failed or cancelled run never leaves a half file behind.
    static QString partialPathFor(const QString &output);
    static QString uniquePath(const QString &path);

signals:
    void stateChanged();
    void infoChanged();
    void presetChanged();
    void statsChanged();
    void finished(bool ok);
    void previewChanged();
    void trimChanged();
    void waveformChanged();
    void analysisChanged();
    void cropChanged();
    void padChanged();
    void subtitlesChanged();
    void outputStillChanged();
    void qualityChanged();
    void finishedStillChanged();

private:
    bool launch();    // starts ffmpeg; start() checks first
    void measureLoudness(int stream);    // the first pass, one audio stream at a time
    void encode();                       // the conversion itself
    void setState(const QString &state, const QString &message = {});
    void scanLevels(int track);
    void makePoster(double at);
    void makeStrip();
    void scanWaveform();
    void makeOutputStill();
    // `pass`: which pass of a fit-to-size run (0 = the run's single pass).
    Command command(const QString &output, bool previews, int pass = 0) const;
    void removePassLog() const;
    void checkPreview();
    void readProgress();
    void onFinished(int code, QProcess::ExitStatus status);
    void pushHistory(QVariantList &list, double value);
    double activeSeconds() const;

    int m_id;
    QString m_input;
    QString m_state = "probing";
    QString m_message;
    MediaInfo m_info;
    bool m_levelsScanned = false;
    QString m_mediainfo;
    Preset m_preset;
    QString m_outputFolder;
    QString m_namePattern;
    QString m_finalPath;      // fixed when the run starts
    QStringList m_runNotes;
    QString m_engine = "cpu";
    QString m_engineDevice;
    QString m_fallbackNote;     // why a GPU run was redone on the CPU
    QList<Loudness> m_loudness;     // measured for this run, one per audio stream
    bool m_loudnessReady = false;
    // Fit to size: the pass running (0 = a single pass), and when the second
    // started, so its time estimate isn't skewed by the quicker first.
    int m_pass = 0;
    double m_passStartedAt = 0;
    double m_trimIn = 0;
    double m_trimOut = 0;       // 0 = the end
    double m_posterTime = 0;
    QString m_strip;
    QPointer<QProcess> m_analysis;
    bool m_analysed = false;
    double m_analysisPercent = 0;
    QList<Moment> m_moments;
    QString m_subtitleBurn = "off";
    QString m_pickedSubtitles;      // a file chosen by hand, kept in the list
    QString m_subtitleLink;         // the plain-named link ffmpeg reads
    void linkSubtitles() const;
    QString m_cropMode = "off";
    QMargins m_crop;
    QList<Job *> m_clips;
    QString m_joinDifference;
    QString m_joinList;             // the concat list for clips that match
    QString m_padMode = "off";
    QString m_padFill = "black";
    QMargins m_detectedBars;
    QPointer<QProcess> m_bars;
    bool m_barsChecked = false;
    void detectBars();
    bool m_hasStrip = false;
    QVariantList m_waveform;
    double m_posterWanted = -1; // a frame asked for while another was being made
    bool m_posterBusy = false;
    QString m_outputStill;
    QTimer m_stillTimer;          // gathers quick requests (frame steps, slider drags) into one
    double m_stillWanted = -1;    // the frame to make next
    double m_stillTime = -1;      // the frame shown, -1 = none yet
    double m_stillAsked = -1;     // the frame last asked for
    int m_stillStamp = 0;
    bool m_stillBusy = false;
    QString m_stillError;
    QString m_qualityState;
    QString m_qualityStep;
    double m_qualityPercent = 0;
    QVariantMap m_quality;
    MediaInfo m_outputInfo;
    QList<QPointer<QProcess>> m_qualityProcesses;
    void measurePicture();
    void checkAudio();
    void finishQuality(const QString &failure = {});
    QProcess *qualityProcess();
    QString m_finishedStill;
    double m_finishedWanted = -1;
    double m_finishedTime = -1;
    int m_finishedStamp = 0;
    bool m_finishedBusy = false;
    void makeFinishedStill();

    QPointer<QProcess> m_process;
    QPointer<QProcess> m_helper;
    ProgressParser m_parser;
    ProgressSample m_last;
    Estimate m_estimate;
    QByteArray m_errors;
    bool m_cancelling = false;

    QElapsedTimer m_clock;
    qint64 m_activeMsBeforePause = 0;
    QDateTime m_startedAt;
    QDateTime m_finishedAt;

    struct Point { double t; double outTime; qint64 frame; qint64 size; };
    std::deque<Point> m_window;
    double m_recentSpeed = 0;
    double m_recentFps = 0;
    double m_recentBitrate = 0;
    double m_energyWh = 0;
    double m_peakCpu = 0;
    double m_clockShare = 0;
    double m_lowestClockShare = 0;
    int m_heatSeconds = 0;
    qint64 m_freeSpace = -1;
    double m_lastHistoryAt = -1;

    QString m_previewOut;
    QString m_passLog;
    QString m_previewSource;
    int m_previewStamp = 0;
    bool m_hasLivePreview = false;
    QDateTime m_previewTime;

    QVariantList m_speedHistory, m_fpsHistory, m_bitrateHistory, m_cpuHistory, m_wattHistory;
    double m_pendingCpu = 0;
    double m_pendingWatts = 0;
};
