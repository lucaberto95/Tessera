#include "ui/DocumentView.h"
#include "core/Project.h"
#include "core/TextUtils.h"
#include "ui/CodingStripe.h"
#include "ui/UiUtils.h"

#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTextBlock>
#include <QTextEdit>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <QRegularExpression>

#include <functional>

namespace {
// QTextEdit::toPlainText() turns non-breaking spaces into spaces; keep the stored text exact.
QString rawText(const QTextEdit *edit)
{
    QString t = edit->document()->toRawText();
    t.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    return t;
}
} // namespace

// QTextEdit that accepts codes dropped from the code tree and delegates menus/tooltips.
class DocTextEdit : public QTextEdit {
public:
    using QTextEdit::QTextEdit;
    std::function<void(qint64 codeId)> onCodeDropped;
    std::function<void(const QPoint &globalPos, int position)> onContextMenu;
    std::function<QString(int position)> tooltipAt;

protected:
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType))) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
        } else {
            QTextEdit::dragEnterEvent(e);
        }
    }
    void dragMoveEvent(QDragMoveEvent *e) override
    {
        if (e->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType))) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
        } else {
            QTextEdit::dragMoveEvent(e);
        }
    }
    void dropEvent(QDropEvent *e) override
    {
        if (e->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType))) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
            if (onCodeDropped)
                onCodeDropped(e->mimeData()->data(QString::fromLatin1(Ui::CodeMimeType)).toLongLong());
        } else {
            QTextEdit::dropEvent(e);
        }
    }
    void contextMenuEvent(QContextMenuEvent *e) override
    {
        if (onContextMenu)
            onContextMenu(e->globalPos(), cursorForPosition(e->pos()).position());
    }
    bool viewportEvent(QEvent *e) override
    {
        if (e->type() == QEvent::ToolTip && tooltipAt) {
            auto *help = static_cast<QHelpEvent *>(e);
            const QString tip = tooltipAt(cursorForPosition(help->pos()).position());
            if (tip.isEmpty())
                QToolTip::hideText();
            else
                QToolTip::showText(help->globalPos(), tip, viewport());
            return true;
        }
        return QTextEdit::viewportEvent(e);
    }
};

DocumentView::DocumentView(Project *project, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_stack(new QStackedWidget)
    , m_title(new QLabel)
    , m_info(new QLabel)
    , m_editButton(new QToolButton)
    , m_edit(new DocTextEdit)
    , m_stripe(new CodingStripe(m_edit))
{
    auto *placeholder = new QLabel(tr("Fai doppio clic su un documento nel pannello Documenti per aprirlo.\n\n"
                                      "Seleziona del testo e trascina un codice sulla selezione,\n"
                                      "oppure usa Ctrl+E per codificarla con il codice selezionato."));
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11pt"));

    QFont titleFont = m_title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.15);
    titleFont.setBold(true);
    m_title->setFont(titleFont);
    m_info->setStyleSheet(QStringLiteral("color: palette(mid)"));
    m_editButton->setText(tr("Modifica testo"));
    m_editButton->setCheckable(true);
    m_editButton->setToolTip(tr("Attiva la modifica del testo del documento. Le codifiche esistenti si adattano "
                                "alle modifiche; il testo viene salvato quando disattivi la modifica."));
    connect(m_editButton, &QToolButton::toggled, this, &DocumentView::setEditing);

    QFont textFont(QStringLiteral("Segoe UI"), 11);
    m_edit->setFont(textFont);
    m_edit->setReadOnly(true);
    m_edit->setAcceptRichText(false);
    m_edit->setAcceptDrops(true);
    m_edit->setFrameShape(QFrame::NoFrame);
    m_edit->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_edit->document()->setDocumentMargin(14);

    m_edit->onCodeDropped = [this](qint64 codeId) { codeSelection(codeId); };
    m_edit->onContextMenu = [this](const QPoint &pos, int position) { showTextMenu(pos, position); };
    m_edit->tooltipAt = [this](int position) {
        QStringList lines;
        for (const Segment &s : segmentsAt(position))
            lines << QStringLiteral("<span style='color:%1'>&#9632;</span> %2")
                         .arg(m_codes.value(s.codeId).color.name(), segmentLabel(s).toHtmlEscaped());
        return lines.join(QStringLiteral("<br>"));
    };
    connect(m_edit, &QTextEdit::selectionChanged, this, &DocumentView::selectionChanged);
    connect(m_stripe, &CodingStripe::segmentClicked, this, [this](qint64 id) {
        for (const Segment &s : m_segments)
            if (s.id == id)
                selectRange(s.start, s.end, s.id);
    });
    connect(m_stripe, &CodingStripe::segmentContextMenu, this, &DocumentView::showSegmentMenu);

    auto *header = new QHBoxLayout;
    header->setContentsMargins(8, 6, 8, 6);
    header->addWidget(m_title);
    header->addSpacing(12);
    header->addWidget(m_info, 1);
    header->addWidget(m_editButton);

    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(m_stripe);
    body->addWidget(m_edit, 1);

    auto *docPage = new QWidget;
    auto *docLayout = new QVBoxLayout(docPage);
    docLayout->setContentsMargins(0, 0, 0, 0);
    docLayout->setSpacing(0);
    docLayout->addLayout(header);
    auto *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    docLayout->addWidget(line);
    docLayout->addLayout(body, 1);

    // The view keeps itself in sync with the project (stale segments would be lost when saving edits).
    connect(m_project, &Project::segmentsChanged, this, &DocumentView::onSegmentsChanged);
    connect(m_project, &Project::codesChanged, this, &DocumentView::onCodesChanged);
    connect(m_project, &Project::documentsChanged, this, &DocumentView::onDocumentsChanged);

    m_stack->addWidget(placeholder);
    m_stack->addWidget(docPage);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_stack);
}

void DocumentView::openDocument(qint64 id)
{
    if (id == m_docId)
        return;
    commitEdits();
    const Document doc = m_project->document(id);
    if (!doc.id) {
        closeDocument();
        return;
    }
    m_docId = id;
    m_highlighted = 0;
    setDisplayedText(m_project->documentText(id));
    m_edit->moveCursor(QTextCursor::Start);
    m_stack->setCurrentIndex(1);
    onDocumentsChanged();
    onCodesChanged();
    emit selectionChanged();
}

void DocumentView::setDisplayedText(const QString &text)
{
    m_edit->setPlainText(text);
    // Comfortable reading line height; not part of the undo history.
    QTextCursor all(m_edit->document());
    all.select(QTextCursor::Document);
    QTextBlockFormat fmt;
    fmt.setLineHeight(135, QTextBlockFormat::ProportionalHeight);
    fmt.setBottomMargin(4);
    all.mergeBlockFormat(fmt);
    m_edit->document()->clearUndoRedoStacks();
}

void DocumentView::closeDocument()
{
    commitEdits();
    m_docId = 0;
    m_segments.clear();
    m_edit->clear();
    m_stripe->setSegments({}, {});
    m_stack->setCurrentIndex(0);
    emit selectionChanged();
}

bool DocumentView::hasSelection() const { return m_docId && m_edit->textCursor().hasSelection(); }

std::pair<int, int> DocumentView::selection() const
{
    const QTextCursor c = m_edit->textCursor();
    return {c.selectionStart(), c.selectionEnd()};
}

QString DocumentView::selectedText() const
{
    const auto [s, e] = selection();
    return rawText(m_edit).mid(s, e - s);
}

qint64 DocumentView::codeSelection(qint64 codeId)
{
    if (!m_docId || !codeId)
        return 0;
    if (m_editing) {
        Ui::showError(this, tr("Termina la modifica del testo prima di codificare."));
        return 0;
    }
    auto [start, end] = selection();
    // Trim surrounding whitespace so segments start and end on text.
    const QString text = rawText(m_edit);
    while (start < end && text.at(start).isSpace())
        ++start;
    while (end > start && text.at(end - 1).isSpace())
        --end;
    if (end <= start) {
        emit statusMessage(tr("Seleziona prima del testo da codificare."));
        return 0;
    }
    const qint64 id = m_project->addSegment(m_docId, codeId, start, end);
    if (id)
        emit statusMessage(tr("Codificato con \"%1\"").arg(m_project->codePath(codeId)));
    else
        Ui::showError(this, m_project->lastError());
    return id;
}

void DocumentView::selectRange(int start, int end, qint64 segmentId)
{
    const int len = m_edit->document()->characterCount() - 1;
    QTextCursor c(m_edit->document());
    c.setPosition(qBound(0, start, len));
    c.setPosition(qBound(0, end, len), QTextCursor::KeepAnchor);
    m_edit->setTextCursor(c);
    m_edit->ensureCursorVisible();
    // Bring the start of the selection into view as well.
    QTextCursor startCursor(m_edit->document());
    startCursor.setPosition(qBound(0, start, len));
    const QRect r = m_edit->cursorRect(startCursor);
    if (r.top() < 0 || r.top() > m_edit->viewport()->height())
        m_edit->verticalScrollBar()->setValue(m_edit->verticalScrollBar()->value() + r.top() - 40);
    m_highlighted = segmentId;
    m_stripe->setHighlightedSegment(segmentId);
    refreshHighlights();
    m_edit->setFocus();
}

void DocumentView::reloadSegments()
{
    m_segments = m_docId ? m_project->segmentsForDocument(m_docId) : QList<Segment>{};
    m_stripe->setSegments(m_segments, m_codes);
    refreshHighlights();
}

void DocumentView::refreshHighlights()
{
    QList<QTextEdit::ExtraSelection> extras;
    const int len = m_edit->document()->characterCount() - 1;
    // Longer segments first so nested (shorter) ones are painted on top.
    QList<Segment> ordered = m_segments;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Segment &a, const Segment &b) { return a.length() > b.length(); });
    for (const Segment &s : ordered) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = QTextCursor(m_edit->document());
        sel.cursor.setPosition(qBound(0, s.start, len));
        sel.cursor.setPosition(qBound(0, s.end, len), QTextCursor::KeepAnchor);
        QColor color = m_codes.value(s.codeId).color;
        if (!color.isValid())
            color = Qt::gray;
        color.setAlpha(s.id == m_highlighted ? 120 : 50);
        sel.format.setBackground(color);
        if (s.id == m_highlighted)
            sel.format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
        extras << sel;
    }
    m_edit->setExtraSelections(extras);
    m_stripe->update();
}

QList<Segment> DocumentView::segmentsAt(int position) const
{
    QList<Segment> out;
    for (const Segment &s : m_segments)
        if (position >= s.start && position < s.end)
            out << s;
    return out;
}

QString DocumentView::segmentLabel(const Segment &s) const
{
    QString label = m_project->codePath(s.codeId);
    if (s.weight)
        label += tr(" · peso %1").arg(s.weight);
    if (!s.comment.isEmpty())
        label += QStringLiteral(" · «") + s.comment.left(80) + QStringLiteral("»");
    return label;
}

void DocumentView::showTextMenu(const QPoint &globalPos, int position)
{
    if (!m_docId)
        return;
    QMenu menu(this);
    const bool selected = hasSelection();
    const qint64 current = m_currentCode ? m_currentCode() : 0;

    if (!m_editing) {
        QAction *codeCurrent = menu.addAction(
            current ? tr("Codifica con \"%1\"").arg(m_project->code(current).name) : tr("Codifica con il codice selezionato"),
            this, [this, current] { codeSelection(current); });
        codeCurrent->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
        codeCurrent->setEnabled(selected && current);
        QAction *codeNew = menu.addAction(tr("Codifica con un nuovo codice…"), this,
                                          [this] { emit newCodeForSelectionRequested(); });
        codeNew->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
        codeNew->setEnabled(selected);

        QMenu *codeWith = menu.addMenu(tr("Codifica con"));
        codeWith->setEnabled(selected);
        const QList<Code> codes = m_project->codes();
        if (codes.isEmpty())
            codeWith->addAction(tr("(nessun codice)"))->setEnabled(false);
        // Flat list ordered as the tree, indented by depth.
        QHash<qint64, QList<Code>> children;
        for (const Code &c : codes)
            children[c.parentId] << c;
        std::function<void(qint64, int)> add = [&](qint64 parent, int depth) {
            for (const Code &c : children.value(parent)) {
                codeWith->addAction(Ui::colorIcon(c.color), QString(depth * 4, QLatin1Char(' ')) + c.name, this,
                                    [this, id = c.id] { codeSelection(id); });
                add(c.id, depth + 1);
            }
        };
        add(0, 0);
    }

    const QList<Segment> here = segmentsAt(position);
    if (!here.isEmpty() && !m_editing) {
        menu.addSeparator();
        for (const Segment &s : here) {
            QMenu *sub = menu.addMenu(Ui::colorIcon(m_codes.value(s.codeId).color), segmentLabel(s));
            sub->addAction(tr("Seleziona segmento"), this, [this, s] { selectRange(s.start, s.end, s.id); });
            sub->addAction(tr("Commento…"), this, [this, s] {
                bool ok = false;
                const QString c = QInputDialog::getMultiLineText(this, tr("Commento al segmento"), tr("Commento:"),
                                                                 s.comment, &ok);
                if (ok)
                    m_project->setSegmentComment(s.id, c.trimmed());
            });
            sub->addAction(tr("Peso…"), this, [this, s] {
                bool ok = false;
                const int w = QInputDialog::getInt(this, tr("Peso del segmento"), tr("Peso (0-100):"), s.weight, 0,
                                                   100, 1, &ok);
                if (ok)
                    m_project->setSegmentWeight(s.id, w);
            });
            sub->addAction(Ui::memoIcon(), tr("Memo…"), this, [this, s] {
                Ui::editMemo(this, m_project, int(MemoTarget::Segment), s.id,
                             tr("segmento \"%1\"").arg(TextUtils::excerpt(rawText(m_edit), s.start, s.end, 80)));
            });
            sub->addSeparator();
            sub->addAction(tr("Rimuovi codifica"), this, [this, s] { m_project->deleteSegment(s.id); });
        }
    }

    menu.addSeparator();
    QAction *copy = menu.addAction(tr("Copia"), m_edit, &QTextEdit::copy);
    copy->setEnabled(selected);
    menu.addAction(tr("Seleziona tutto"), m_edit, &QTextEdit::selectAll);
    menu.exec(globalPos);
}

void DocumentView::showSegmentMenu(qint64 segmentId, const QPoint &globalPos)
{
    for (const Segment &s : m_segments) {
        if (s.id == segmentId) {
            selectRange(s.start, s.end, s.id);
            showTextMenu(globalPos, s.start);
            return;
        }
    }
}

void DocumentView::onSegmentsChanged(qint64 documentId)
{
    if (m_editing || !m_docId)
        return;
    if (documentId == 0 || documentId == m_docId)
        reloadSegments();
}

void DocumentView::onCodesChanged()
{
    m_codes.clear();
    if (m_project->isOpen())
        for (const Code &c : m_project->codes())
            m_codes.insert(c.id, c);
    if (!m_editing)
        reloadSegments();
    else {
        m_stripe->setSegments(m_segments, m_codes);
        refreshHighlights();
    }
}

void DocumentView::onDocumentsChanged()
{
    if (!m_docId)
        return;
    const Document doc = m_project->document(m_docId);
    if (!doc.id) {
        // Deleted: discard pending edits.
        m_editing = false;
        m_docId = 0;
        closeDocument();
        return;
    }
    m_title->setText(doc.name);
    const QString text = m_editing ? rawText(m_edit) : m_project->documentText(m_docId);
    if (!m_editing && text != rawText(m_edit)) {
        // Changed outside the view (undo/redo of a text edit): show the stored text, keep the scroll.
        const int scroll = m_edit->verticalScrollBar()->value();
        setDisplayedText(text);
        m_edit->verticalScrollBar()->setValue(scroll);
        reloadSegments();
    }
    const int words = int(text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size());
    m_info->setText(tr("%1 parole · %2 caratteri").arg(words).arg(text.size()));
}

void DocumentView::setEditing(bool editing)
{
    if (editing == m_editing)
        return;
    if (editing) {
        if (!m_docId) {
            m_editButton->setChecked(false);
            return;
        }
        reloadSegments();
        m_editing = true;
        m_editSnapshot = rawText(m_edit);
        m_edit->setReadOnly(false);
        m_edit->setTextInteractionFlags(Qt::TextEditorInteraction);
        m_editConnection = connect(m_edit, &QTextEdit::textChanged, this, &DocumentView::onTextChangedWhileEditing);
        m_editButton->setText(tr("Salva testo"));
        emit statusMessage(tr("Modifica del testo attiva: premi \"Salva testo\" per salvare."));
    } else {
        commitEdits();
    }
    if (m_editButton->isChecked() != m_editing) {
        const QSignalBlocker block(m_editButton);
        m_editButton->setChecked(m_editing);
    }
    emit editingChanged(m_editing);
}

void DocumentView::onTextChangedWhileEditing()
{
    // Diff against the previous text: common prefix and suffix give the edited range.
    const QString now = rawText(m_edit);
    const QString &before = m_editSnapshot;
    int prefix = 0;
    const int maxPrefix = int(qMin(before.size(), now.size()));
    while (prefix < maxPrefix && before.at(prefix) == now.at(prefix))
        ++prefix;
    int suffix = 0;
    const int maxSuffix = maxPrefix - prefix;
    while (suffix < maxSuffix && before.at(before.size() - 1 - suffix) == now.at(now.size() - 1 - suffix))
        ++suffix;
    const int removed = int(before.size()) - prefix - suffix;
    const int added = int(now.size()) - prefix - suffix;
    if (removed || added) {
        TextUtils::adjustSegmentsForEdit(m_segments, prefix, removed, added);
        m_stripe->setSegments(m_segments, m_codes);
        refreshHighlights();
    }
    m_editSnapshot = now;
}

bool DocumentView::commitEdits()
{
    if (!m_editing)
        return true;
    disconnect(m_editConnection);
    m_editing = false;
    m_edit->setReadOnly(true);
    m_edit->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    m_editButton->setText(tr("Modifica testo"));
    {
        const QSignalBlocker block(m_editButton);
        m_editButton->setChecked(false);
    }
    const bool ok = m_project->updateDocumentText(m_docId, rawText(m_edit), m_segments);
    if (!ok)
        Ui::showError(this, tr("Impossibile salvare il testo: %1").arg(m_project->lastError()));
    else
        emit statusMessage(tr("Testo del documento salvato."));
    reloadSegments();
    emit editingChanged(false);
    return ok;
}
