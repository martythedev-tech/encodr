// The file picker interface follows Omacut (MIT, David Heinemeier Hansson).
#pragma once

#include <QList>
#include <QObject>
#include <QUrl>

class FilePicker : public QObject {
    Q_OBJECT

public:
    explicit FilePicker(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~FilePicker() = default;

    virtual void openMedia() = 0;
    virtual void chooseFolder(const QString &currentFolder) = 0;
    // One file of a `kind` (subtitles, lut, image), starting in `folder`.
    virtual void openFile(const QString &kind, const QString &folder) { Q_UNUSED(kind); Q_UNUSED(folder); }

signals:
    void mediaSelected(const QList<QUrl> &urls);
    void folderSelected(const QUrl &url);
    void fileSelected(const QString &kind, const QUrl &url);
    void failed(const QString &message);
};
