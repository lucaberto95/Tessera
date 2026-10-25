#pragma once

#include <QDialog>

#include <functional>

class Project;
class QLabel;
class QTableWidget;

// Time stamps of a transcript (issue #29): list, insert at the cursor, edit the time, shift all of them,
// associate the recording, export to SRT / WebVTT / text with [hh:mm:ss] markers.
class TranscriptDialog : public QDialog {
    Q_OBJECT
public:
    // `cursorPosition` returns the text position where "Inserisci" puts a stamp (-1: no text cursor).
    TranscriptDialog(Project *project, qint64 documentId, std::function<int()> cursorPosition, QWidget *parent = nullptr);

    qint64 documentId() const { return m_docId; }
    int rowCount() const;

    // Used by the buttons; public so that tests can drive them without dialogs.
    bool insertAt(int textPos, qint64 timeMs);
    bool exportTo(const QString &path);

signals:
    void positionActivated(qint64 documentId, int textPos);

private:
    qint64 currentStampId() const;
    void refresh();
    void insertAtCursor();
    void editTime();
    void shiftAll();
    void associateMedia();
    void exportDialog();
    void activateCurrent();

    Project *m_project;
    qint64 m_docId;
    std::function<int()> m_cursor;
    QLabel *m_media;
    QTableWidget *m_table;
};
