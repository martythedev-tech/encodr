// Portal file picker from Omacut (MIT, David Heinemeier Hansson), adapted to
// pick several media files at once and to choose an output folder.
#pragma once

#include <QVariantMap>

#include "filepicker.h"

class PortalFilePicker : public FilePicker {
    Q_OBJECT

public:
    explicit PortalFilePicker(QObject *parent = nullptr);

    void openMedia() override;
    void chooseFolder(const QString &currentFolder) override;

private slots:
    void handleResponse(uint response, const QVariantMap &results);

private:
    enum class Action {
        None,
        Open,
        Folder
    };

    void requestFile(const QString &method, const QString &title,
                     QVariantMap options, Action action);
    bool connectToRequestPath(const QString &path);
    void clearPending();

    QString m_pendingPath;
    Action m_pendingAction = Action::None;
};
