#pragma once

#include <QDialog>
#include <QList>

class Project;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QTableWidget;
class QTimer;

// Summary grid (issue #14): documents x codes on the left, the segments of the selected cell in the
// middle, the editor of its summary on the right. The summary is saved after a pause in typing, when
// another cell is selected and when the window is closed or loses focus.
class SummaryGridDialog : public QDialog {
    Q_OBJECT
public:
    SummaryGridDialog(Project *project, const QList<qint64> &activeDocuments, const QList<qint64> &activeCodes,
                      QWidget *parent = nullptr);

    void selectCell(qint64 documentId, qint64 codeId);
    qint64 currentDocument() const { return m_doc; }
    qint64 currentCode() const { return m_code; }
    QString editorText() const;
    void setEditorText(const QString &text); // as if typed
    int segmentRows() const;
    QTableWidget *grid() const { return m_grid; }
    // Writes the summary being edited, if it changed.
    void flush();

    void done(int result) override;

protected:
    void changeEvent(QEvent *event) override;

private:
    void rebuildGrid();
    void onCurrentCellChanged();
    void loadCell();
    void reloadFromProject();

    Project *m_project;
    QList<qint64> m_docIds;
    QList<qint64> m_codeIds;
    QTableWidget *m_grid;
    QListWidget *m_segments;
    QLabel *m_title;
    QLabel *m_status;
    QPlainTextEdit *m_editor;
    QTimer *m_saveTimer;
    qint64 m_doc = 0;
    qint64 m_code = 0;
    bool m_dirty = false;
    bool m_loading = false;
};
