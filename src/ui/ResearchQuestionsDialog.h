#pragma once

#include "core/Model.h"

#include <QDialog>
#include <QTimer>

class Project;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;

// Research questions worksheet (issue #17): a list of questions, each with status, description, themes (one per
// line), evidence (segments, codes and memos, kept as references and dropped here from the code tree or added with
// the buttons), conclusions and working notes. Edits are saved shortly after typing stops; the whole worksheet can
// be exported to Word, PDF or HTML as a draft of the results chapter.
class ResearchQuestionsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ResearchQuestionsDialog(Project *project, QWidget *parent = nullptr);

    qint64 currentQuestionId() const;
    void selectQuestion(qint64 id);
    // Writes pending edits to the project now.
    void commitEdits();
    qint64 createQuestion(const QString &title);
    // Test hooks.
    QListWidget *questionList() const { return m_questions; }
    QListWidget *evidenceList() const { return m_evidence; }
    QLineEdit *titleEdit() const { return m_title; }
    QComboBox *statusBox() const { return m_status; }
    QPlainTextEdit *descriptionEdit() const { return m_description; }
    QPlainTextEdit *themesEdit() const { return m_themes; }
    QPlainTextEdit *conclusionsEdit() const { return m_conclusions; }
    QPlainTextEdit *notesEdit() const { return m_notes; }
    // Writes the worksheet; the format follows the suffix (.docx, .pdf, .html).
    bool exportTo(const QString &path);
    bool addDroppedMime(const class QMimeData *mime);

signals:
    // Kind is an EvidenceKind as int.
    void evidenceActivated(int kind, qint64 refId);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void reloadQuestions();
    void loadQuestion();
    void reloadEvidence();
    void scheduleSave();
    void newQuestion();
    void deleteQuestion();
    void moveQuestion(int delta);
    void addSegments();
    void addCodes();
    void addMemos();
    void removeEvidence();
    void exportWorksheet();
    void updateButtons();

    Project *m_project;
    QListWidget *m_questions;
    QLineEdit *m_title;
    QComboBox *m_status;
    QPlainTextEdit *m_description;
    QPlainTextEdit *m_themes;
    QListWidget *m_evidence;
    QPlainTextEdit *m_conclusions;
    QPlainTextEdit *m_notes;
    QPushButton *m_deleteButton, *m_upButton, *m_downButton, *m_removeButton;
    QWidget *m_editor;
    QTimer m_saveTimer;
    qint64 m_loadedId = 0; // question whose fields are in the editors
    bool m_loading = false;
    bool m_dirty = false;
};
