#pragma once

#include "core/TeamSync.h"

#include <QDialog>

class Project;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

// Continuous team work through a shared folder (issue #24, option 2): the coder publishes their own change log
// there and reads the others'. The mechanism is in core/TeamSync.h.
class TeamSyncDialog : public QDialog {
    Q_OBJECT
public:
    explicit TeamSyncDialog(Project *project, QWidget *parent = nullptr);

    void setFolder(const QString &folder);
    QString folder() const;

    // Non-interactive halves of the buttons, also used by the tests. Both return the text shown in the report.
    // syncNow publishes the user's own work and then reads the others' (nothing is written with `dryRun`).
    QString syncNow(bool dryRun = false, bool rereadAll = false);
    QString publishOnly();

    static QString describe(const TeamSync::SyncReport &report);

    // Per project and per computer (the folder differs between members), kept in the application settings.
    static QString savedFolder(const Project &project);
    static void saveFolder(const Project &project, const QString &folder);
    static bool autoSyncOnOpen(const Project &project);
    static void setAutoSyncOnOpen(const Project &project, bool on);
    // Called when a project is opened: when the user asked for it, synchronises and returns a one-line message
    // (empty when nothing was done).
    static QString runAutoSync(Project &project);

private:
    void chooseFolder();

    Project *m_project;
    QLineEdit *m_folder;
    QCheckBox *m_auto;
    QPlainTextEdit *m_report;
};
