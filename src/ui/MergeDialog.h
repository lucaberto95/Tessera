#pragma once

#include "core/Merge.h"

#include <QDialog>

class Project;
class QComboBox;
class QPlainTextEdit;
class QPushButton;

// Summary of what merging another project would do, with the options of the merge (issue #22). The report is
// recomputed (dry run) whenever an option changes.
class MergeDialog : public QDialog {
    Q_OBJECT
public:
    MergeDialog(Project *project, Merge::Source *source, QWidget *parent = nullptr);

    Merge::Options options() const;
    void setOptions(const Merge::Options &options);
    Merge::Report report() const { return m_report; }

    // Readable summary of a report (plain text), also shown once the merge is done.
    static QString reportText(const Merge::Report &report, bool done);

private:
    void refresh();

    Project *m_project;
    Merge::Source *m_source;
    QComboBox *m_author;
    QComboBox *m_conflict;
    QPlainTextEdit *m_text;
    QPushButton *m_merge;
    Merge::Report m_report;
};
