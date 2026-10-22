#pragma once

#include "core/Model.h"

#include <QHash>
#include <QSet>
#include <QWidget>

#include <functional>

class CodingStripe;
class DocTextEdit;
class Project;
class QCheckBox;
class QLabel;
class QLineEdit;
class QStackedWidget;
class QToolButton;

// Shows one document with its coded segments highlighted and a coding stripe in the margin.
// Text is read-only except in editing mode, where segment boundaries follow the edits.
class DocumentView : public QWidget {
    Q_OBJECT
public:
    explicit DocumentView(Project *project, QWidget *parent = nullptr);

    void setCurrentCodeProvider(std::function<qint64()> provider) { m_currentCode = std::move(provider); }

    qint64 documentId() const { return m_docId; }
    void openDocument(qint64 id);
    void closeDocument();

    bool hasSelection() const;
    std::pair<int, int> selection() const;
    QString selectedText() const;
    // Codes the current selection; returns the segment id or 0.
    qint64 codeSelection(qint64 codeId);
    void selectRange(int start, int end, qint64 segmentId = 0);

    bool isEditing() const { return m_editing; }
    void setEditing(bool editing);
    // Saves pending text edits (no-op when not editing). Returns false on failure.
    bool commitEdits();

    // Find bar for the open document (Ctrl+F); it never touches the stored text or the codings.
    void showFindBar();
    void hideFindBar();
    bool isFindBarVisible() const;
    void findNext();
    void findPrevious();
    int findMatchCount() const { return int(m_findMatches.size()); }

    void showSegmentMenu(qint64 segmentId, const QPoint &globalPos);

    void onSegmentsChanged(qint64 documentId);
    void onCodesChanged();
    void onDocumentsChanged();

signals:
    void selectionChanged();
    void editingChanged(bool editing);
    void newCodeForSelectionRequested();
    void statusMessage(const QString &message);

private:
    void reloadSegments();
    void setDisplayedText(const QString &text);
    void refreshHighlights();
    void showTextMenu(const QPoint &globalPos, int position);
    void onTextChangedWhileEditing();
    QList<Segment> segmentsAt(int position) const;
    QString segmentLabel(const Segment &s) const;
    void buildFindBar();
    // Recomputes the matches of the find field; the current one is the first at/after the cursor.
    void updateFindMatches();
    void goToFindMatch(int index);
    void updateFindCounter();

    Project *m_project;
    std::function<qint64()> m_currentCode;
    QStackedWidget *m_stack;
    QLabel *m_title;
    QLabel *m_info;
    QToolButton *m_editButton;
    DocTextEdit *m_edit;
    CodingStripe *m_stripe;

    qint64 m_docId = 0;
    QList<Segment> m_segments;
    QHash<qint64, Code> m_codes;
    qint64 m_highlighted = 0;
    bool m_editing = false;
    QString m_editSnapshot;
    // While editing: the stored text and the segments known to the view, to take in codings
    // added from elsewhere (they refer to the stored text) instead of deleting them on save.
    QString m_editBaseText;
    QSet<qint64> m_editKnownIds;
    QMetaObject::Connection m_editConnection;

    QWidget *m_findBar = nullptr;
    QLineEdit *m_findField = nullptr;
    QLabel *m_findCounter = nullptr;
    QCheckBox *m_findCase = nullptr;
    QCheckBox *m_findWhole = nullptr;
    QList<std::pair<int, int>> m_findMatches; // [start, end) UTF-16 offsets in the raw text
    int m_findIndex = -1;
};
