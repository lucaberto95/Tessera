#pragma once

#include <QDialog>

class Project;
class QLabel;
class QPushButton;
class QTableWidget;

// Team work by rounds on a shared folder (issue #24): history of the rounds, start of a new round (one copy per
// member), import of the returned copies. The workflow itself is in core/Team.h.
class TeamDialog : public QDialog {
    Q_OBJECT
public:
    explicit TeamDialog(Project *project, QWidget *parent = nullptr);

    // True when the user asked, from here, to compare the coders (the caller opens the agreement dialog).
    bool agreementRequested() const { return m_agreementRequested; }

    // Non-interactive halves of the buttons, also used by the tests: they do what the dialog does after the
    // user has chosen members/folder and files.
    bool startRound(const QStringList &members, const QString &folder);
    QString importFiles(const QStringList &paths, bool askConfirmation = true);

private:
    void refresh();
    void newRound();
    void importReturned();
    void removeEntry();

    Project *m_project;
    QLabel *m_info;
    QTableWidget *m_table;
    QPushButton *m_reopen;
    QPushButton *m_remove;
    bool m_agreementRequested = false;
};