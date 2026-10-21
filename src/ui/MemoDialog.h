#pragma once

#include "core/Model.h"

#include <QDialog>

class Project;
class QLineEdit;
class QPlainTextEdit;

// Editor for a single memo. Accepted when the memo was saved or deleted.
class MemoDialog : public QDialog {
    Q_OBJECT
public:
    MemoDialog(Project *project, const Memo &memo, const QString &targetLabel, QWidget *parent = nullptr);

private:
    void save();
    void remove();

    Project *m_project;
    Memo m_memo;
    QLineEdit *m_title;
    QPlainTextEdit *m_body;
};
