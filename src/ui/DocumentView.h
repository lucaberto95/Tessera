#pragma once

#include "core/Model.h"

#include <QHash>
#include <QSet>
#include <QWidget>

#include <functional>

class CodingStripe;
class DocTextEdit;
class ImageView;
class PdfView;
class Project;
class QMenu;
class QSpinBox;
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
    // Image documents show an ImageView instead of the text; the selection is then a rectangle
    // (hasSelection, codeSelection and selectRange work on it), and links and text editing do not apply.
    bool isImage() const { return m_isImage; }
    ImageView *imageView() const { return m_imageView; }
    // PDF documents show their pages in a PdfView. The selection is then selected text or a rectangle (Alt +
    // drag) on a page; both are coded as an area with its text. Links, text editing and Ctrl+F do not apply.
    bool isPdf() const { return m_isPdf; }
    PdfView *pdfView() const { return m_pdfView; }
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

    // Links: the selection becomes the first end ("Inizia collegamento"), then a selection in any
    // document completes the link. Ctrl+click on a linked passage opens the other end.
    void startLink();
    qint64 completeLink(); // returns the new link id or 0
    void cancelPendingLink();
    bool hasPendingLink() const { return m_pendingLink.documentId != 0; }
    QList<DocumentLink> links() const { return m_links; } // links with an end in the open document
    QList<DocumentLink> linksAt(int position) const;
    // Opens the end of the link opposite to the one at `position` (-1: the end in another document,
    // or the second one). Returns false if the link does not exist.
    bool followLink(qint64 linkId, int position = -1);
    bool followLinkAt(int position); // the first link at `position`

    void onSegmentsChanged(qint64 documentId);
    void onCodesChanged();
    void onDocumentsChanged();
    void onLinksChanged();

signals:
    void selectionChanged();
    void editingChanged(bool editing);
    void newCodeForSelectionRequested();
    void statusMessage(const QString &message);
    void pendingLinkChanged();
    void linkFollowed(qint64 documentId); // the view now shows that document

private:
    struct LinkEnd {
        qint64 documentId = 0;
        int start = 0;
        int end = 0;
    };
    LinkEnd otherEnd(const DocumentLink &link, int position) const;
    QString linkLabel(const DocumentLink &link, int position) const;
    std::pair<int, int> trimmedSelection() const; // selection without surrounding whitespace
    void reloadLinks();
    void reloadSegments();
    void setDisplayedText(const QString &text);
    void refreshHighlights();
    void showTextMenu(const QPoint &globalPos, int position);
    void onTextChangedWhileEditing();
    QList<Segment> segmentsAt(int position) const;
    QString segmentLabel(const Segment &s) const;
    // "Codifica con" submenu (enabled when there is a selection) and the per-segment actions after "select".
    void addCodeWithMenu(QMenu *menu, bool enabled);
    void addSegmentActions(QMenu *menu, const Segment &s);
    void setImageMode(bool image);
    void setPdfMode(bool pdf);
    void reloadPdfAreas();
    void updatePdfInfo();
    void onPdfAreaClicked(const QList<qint64> &ids);
    void showPdfMenu(const QList<qint64> &ids, const QPoint &globalPos);
    void onPdfCodeDropped(qint64 codeId, int page, const QPointF &pagePos);
    qint64 codePdfArea(qint64 codeId, int page, const QRectF &rect, const QString &text);
    void updateZoomLabel();
    void reloadImageAreas();
    void updateImageInfo();
    void onImageAreaClicked(const QList<qint64> &ids);
    void showImageMenu(const QList<qint64> &ids, const QPoint &globalPos);
    void onImageCodeDropped(qint64 codeId, const QPointF &imagePos);
    qint64 codeImageArea(qint64 codeId, const QRectF &rect);
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
    QWidget *m_textBody = nullptr; // coding stripe + text, hidden for images
    ImageView *m_imageView;
    PdfView *m_pdfView;
    QSpinBox *m_pageSpin = nullptr;
    QToolButton *m_fitButton;
    QToolButton *m_actualButton;
    QLabel *m_zoomLabel;
    bool m_isImage = false;
    bool m_isPdf = false;

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
    QSet<qint64> m_editKnownLinkIds;
    QMetaObject::Connection m_editConnection;

    QList<DocumentLink> m_links;
    LinkEnd m_pendingLink; // first end of a link being created (documentId 0 = none)

    QWidget *m_findBar = nullptr;
    QLineEdit *m_findField = nullptr;
    QLabel *m_findCounter = nullptr;
    QCheckBox *m_findCase = nullptr;
    QCheckBox *m_findWhole = nullptr;
    QList<std::pair<int, int>> m_findMatches; // [start, end) UTF-16 offsets in the raw text
    int m_findIndex = -1;
};
