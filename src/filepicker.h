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

signals:
    void mediaSelected(const QList<QUrl> &urls);
    void folderSelected(const QUrl &url);
    void failed(const QString &message);
};
