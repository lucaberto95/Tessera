#pragma once

#include "core/Report.h"

#include <QDialog>

class Project;
class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;

// "Crea report..." (issue #45): choose the contents, grouping and format, then write a Word, PDF or HTML file.
class ReportDialog : public QDialog {
    Q_OBJECT
public:
    enum class Format { Docx, Pdf, Html };

    explicit ReportDialog(Project *project, QWidget *parent = nullptr);

    Report::Options options() const;
    Format format() const;
    // Builds the report and writes it to `path` in the chosen format (also used by the OK button after asking
    // for the file name). Shows an error and returns false on failure.
    bool generate(const QString &path);

    // Test hooks
    void setFormat(Format format);
    void setOptions(const Report::Options &options);

private:
    void accept() override;

    Project *m_project;
    QLineEdit *m_title;
    QCheckBox *m_contents, *m_codebook, *m_segments, *m_comments, *m_memos, *m_variables, *m_summaryTables;
    QSpinBox *m_examples;
    QComboBox *m_grouping, *m_format;
};
