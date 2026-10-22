#include "ui/MemoDialog.h"
#include "core/Project.h"
#include "core/TextUtils.h"
#include "ui/UiUtils.h"

#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStyle>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace {

constexpr int TypeRole = Qt::UserRole;   // MemoTarget as int; absent on headings
constexpr int IdRole = Qt::UserRole + 1;

QIcon linkIcon(const Project *project, const QStyle *style, const MemoLink &l)
{
    switch (l.targetType) {
    case MemoTarget::Code:
        return Ui::colorIcon(project->code(l.targetId).color);
    case MemoTarget::Segment:
        return Ui::colorIcon(project->code(project->segment(l.targetId).codeId).color);
    case MemoTarget::Document:
        return style->standardIcon(QStyle::SP_FileIcon);
    case MemoTarget::Group:
        return style->standardIcon(QStyle::SP_DirIcon);
    case MemoTarget::DocumentSet:
        return style->standardIcon(QStyle::SP_DirLinkIcon);
    default:
        return {};
    }
}

} // namespace

MemoDialog::MemoDialog(Project *project, const Memo &memo, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_memo(memo)
    , m_title(new QLineEdit(memo.title))
    , m_body(new QPlainTextEdit(memo.body))
    , m_linkList(new QListWidget)
{
    setWindowTitle(memo.id ? tr("Memo") : tr("Nuovo memo"));
    setWindowIcon(Ui::memoIcon());
    resize(580, 560);
    setAcceptDrops(true);

    if (memo.id)
        m_links = project->memoLinks(memo.id);
    else if (Project::isLinkableMemoTarget(memo.targetType))
        m_links = {{memo.targetType, memo.targetId}};
    m_savedLinks = m_links;

    if (m_title->text().isEmpty())
        m_title->setPlaceholderText(tr("Titolo del memo"));
    m_body->setPlaceholderText(tr("Scrivi qui le tue riflessioni, definizioni, idee analitiche…"));
    m_body->setTabChangesFocus(false);
    // The text fields would take a dropped code or document as text: it becomes a link instead.
    m_title->installEventFilter(this);
    m_body->viewport()->installEventFilter(this);

    // Linked elements.
    auto *linksBox = new QGroupBox(tr("Collegato a"));
    auto *linksLayout = new QVBoxLayout(linksBox);
    if (memo.targetType == MemoTarget::Project) {
        auto *projectLabel = new QLabel(tr("Memo del progetto"));
        projectLabel->setTextFormat(Qt::PlainText);
        linksLayout->addWidget(projectLabel);
    }
    m_linkList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_linkList->setMaximumHeight(120);
    auto *add = new QPushButton(tr("Aggiungi…"));
    auto *removeLink = new QPushButton(tr("Rimuovi"));
    removeLink->setEnabled(false);
    connect(add, &QPushButton::clicked, this, &MemoDialog::pickLinks);
    connect(removeLink, &QPushButton::clicked, this, [this] {
        QList<int> rows;
        for (QListWidgetItem *item : m_linkList->selectedItems())
            rows << m_linkList->row(item);
        removeLinks(rows);
    });
    connect(m_linkList, &QListWidget::itemSelectionChanged, this,
            [this, removeLink] { removeLink->setEnabled(!m_linkList->selectedItems().isEmpty()); });
    auto *linkButtons = new QVBoxLayout;
    linkButtons->addWidget(add);
    linkButtons->addWidget(removeLink);
    linkButtons->addStretch();
    auto *linkRow = new QHBoxLayout;
    linkRow->addWidget(m_linkList, 1);
    linkRow->addLayout(linkButtons);
    linksLayout->addLayout(linkRow);
    auto *hint = new QLabel(tr("Trascina qui codici e documenti per collegarli a questo memo."));
    hint->setStyleSheet(QStringLiteral("color: palette(mid)"));
    hint->setWordWrap(true);
    linksLayout->addWidget(hint);

    auto *info = new QLabel;
    info->setTextFormat(Qt::PlainText); // the author's name may come from an imported file
    if (memo.id) {
        info->setText(tr("Autore: %1 · creato %2 · modificato %3")
                          .arg(memo.author, QLocale().toString(memo.createdAt.toLocalTime(), QLocale::ShortFormat),
                               QLocale().toString(memo.modifiedAt.toLocalTime(), QLocale::ShortFormat)));
        info->setStyleSheet(QStringLiteral("color: palette(mid)"));
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    if (memo.id) {
        QPushButton *del = buttons->addButton(tr("Elimina memo"), QDialogButtonBox::DestructiveRole);
        connect(del, &QPushButton::clicked, this, &MemoDialog::remove);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        if (m_memo.id == 0 && m_title->text().trimmed().isEmpty() && m_body->toPlainText().trimmed().isEmpty() &&
            m_links == m_savedLinks) {
            reject(); // nothing to save in an empty new memo
            return;
        }
        if (save())
            accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout;
    form->addRow(tr("Titolo"), m_title);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(linksBox);
    layout->addLayout(form);
    layout->addWidget(m_body, 1);
    layout->addWidget(info);
    layout->addWidget(buttons);

    // Names shown in the list may change while the window is open.
    connect(project, &Project::codesChanged, this, &MemoDialog::refreshLinks);
    connect(project, &Project::documentsChanged, this, &MemoDialog::refreshLinks);
    connect(project, &Project::groupsChanged, this, &MemoDialog::refreshLinks);
    connect(project, &Project::documentSetsChanged, this, &MemoDialog::refreshLinks);
    connect(project, &Project::closed, this, &QDialog::reject);
    refreshLinks();
    m_body->setFocus();
}

void MemoDialog::refreshLinks()
{
    m_linkList->clear();
    for (const MemoLink &l : std::as_const(m_links)) {
        auto *item = new QListWidgetItem(linkIcon(m_project, style(), l),
                                         Ui::memoTargetLabel(m_project, int(l.targetType), l.targetId));
        item->setToolTip(Ui::plainToolTip(item->text()));
        m_linkList->addItem(item);
    }
}

void MemoDialog::addLinks(const QList<MemoLink> &links)
{
    bool changed = false;
    for (const MemoLink &l : links) {
        if (Project::isLinkableMemoTarget(l.targetType) && l.targetId && !m_links.contains(l)) {
            m_links << l;
            changed = true;
        }
    }
    if (changed)
        refreshLinks();
}

void MemoDialog::removeLinks(const QList<int> &rows)
{
    QList<int> sorted = rows;
    std::sort(sorted.begin(), sorted.end(), std::greater<int>());
    for (int row : std::as_const(sorted))
        if (row >= 0 && row < m_links.size())
            m_links.removeAt(row);
    refreshLinks();
}

void MemoDialog::pickLinks()
{
    MemoLinkPicker picker(m_project, m_links, this);
    if (picker.exec() == QDialog::Accepted)
        addLinks(picker.selectedLinks());
}

bool MemoDialog::save()
{
    m_memo.title = m_title->text().trimmed();
    m_memo.body = m_body->toPlainText();
    const bool linksChanged = m_links != m_savedLinks;
    if (m_memo.id == 0 && m_memo.title.isEmpty() && m_memo.body.trimmed().isEmpty() && !linksChanged)
        return false;
    if (m_memo.id && !m_project->memo(m_memo.id).id) {
        Ui::showError(this, tr("Il memo è stato eliminato."));
        return false;
    }
    // Text and links are one undo step.
    m_project->setUndoLabel(tr("Memo"));
    m_project->beginBatch();
    const qint64 id = m_project->saveMemo(m_memo);
    bool ok = id != 0;
    if (ok && linksChanged)
        ok = m_project->setMemoLinks(id, m_links);
    if (!ok) {
        const QString error = m_project->lastError();
        m_project->rollbackBatch();
        Ui::showError(this, error);
        return false;
    }
    if (!m_project->commitBatch()) {
        Ui::showError(this, m_project->lastError());
        return false;
    }
    m_memo = m_project->memo(id);
    m_links = m_savedLinks = m_project->memoLinks(id);
    refreshLinks();
    return true;
}

void MemoDialog::remove()
{
    if (!Ui::confirm(this, tr("Elimina memo"), tr("Eliminare definitivamente questo memo?")))
        return;
    m_project->deleteMemo(m_memo.id);
    accept();
}

bool MemoDialog::canLink(const QMimeData *mime)
{
    return mime && (mime->hasFormat(QString::fromLatin1(Ui::CodeMimeType)) ||
                    mime->hasFormat(QString::fromLatin1(Ui::DocumentMimeType)));
}

bool MemoDialog::linkDropped(const QMimeData *mime)
{
    if (!canLink(mime))
        return false;
    if (mime->hasFormat(QString::fromLatin1(Ui::CodeMimeType)))
        addLinks({{MemoTarget::Code, mime->data(QString::fromLatin1(Ui::CodeMimeType)).toLongLong()}});
    if (mime->hasFormat(QString::fromLatin1(Ui::DocumentMimeType)))
        addLinks({{MemoTarget::Document, mime->data(QString::fromLatin1(Ui::DocumentMimeType)).toLongLong()}});
    return true;
}

void MemoDialog::dragEnterEvent(QDragEnterEvent *event)
{
    if (canLink(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
}

void MemoDialog::dragMoveEvent(QDragMoveEvent *event)
{
    if (canLink(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
}

void MemoDialog::dropEvent(QDropEvent *event)
{
    if (linkDropped(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
}

bool MemoDialog::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto *e = static_cast<QDragMoveEvent *>(event);
        if (canLink(e->mimeData())) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
            return true;
        }
        break;
    }
    case QEvent::Drop: {
        auto *e = static_cast<QDropEvent *>(event);
        if (linkDropped(e->mimeData())) {
            e->setDropAction(Qt::CopyAction);
            e->accept();
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QDialog::eventFilter(watched, event);
}

// ---------------------------------------------------------------- MemoLinkPicker

MemoLinkPicker::MemoLinkPicker(Project *project, const QList<MemoLink> &existing, QWidget *parent)
    : QDialog(parent)
    , m_tree(new QTreeWidget)
    , m_existing(existing)
{
    setWindowTitle(tr("Collega il memo"));
    resize(520, 560);
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);

    auto heading = [this](const QString &text) {
        auto *item = new QTreeWidgetItem(m_tree, {text});
        item->setFlags(Qt::ItemIsEnabled);
        QFont f = item->font(0);
        f.setBold(true);
        item->setFont(0, f);
        return item;
    };
    auto checkable = [&](QTreeWidgetItem *parent, const QString &text, MemoTarget type, qint64 id) {
        const MemoLink link{type, id};
        auto *item = new QTreeWidgetItem(parent, {text});
        item->setIcon(0, linkIcon(project, style(), link));
        item->setData(0, TypeRole, int(type));
        item->setData(0, IdRole, id);
        item->setToolTip(0, Ui::plainToolTip(text));
        // Not disabled: a disabled item would disable its children (subcodes, documents of a group).
        const bool linked = existing.contains(link);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(0, linked ? Qt::Checked : Qt::Unchecked);
        if (linked) {
            QFont f = item->font(0);
            f.setItalic(true);
            item->setFont(0, f);
            item->setToolTip(0, Ui::plainToolTip(tr("%1 (già collegato)").arg(text)));
        }
        return item;
    };

    QTreeWidgetItem *codesRoot = heading(tr("Codici"));
    QHash<qint64, QTreeWidgetItem *> codeItems;
    for (const Code &c : project->codesInTreeOrder())
        codeItems.insert(c.id, checkable(codeItems.value(c.parentId, codesRoot), c.name, MemoTarget::Code, c.id));
    codesRoot->setExpanded(true);

    QTreeWidgetItem *docsRoot = heading(tr("Documenti"));
    for (const DocumentGroup &g : project->groups()) {
        QTreeWidgetItem *gi = checkable(docsRoot, g.name, MemoTarget::Group, g.id);
        for (const Document &d : project->documentsInGroup(g.id))
            checkable(gi, d.name, MemoTarget::Document, d.id);
        gi->setExpanded(true);
    }
    docsRoot->setExpanded(true);

    const QList<DocumentSet> sets = project->documentSets();
    if (!sets.isEmpty()) {
        QTreeWidgetItem *setsRoot = heading(tr("Set di documenti"));
        for (const DocumentSet &s : sets)
            checkable(setsRoot, s.name, MemoTarget::DocumentSet, s.id);
    }

    QTreeWidgetItem *segmentsRoot = heading(tr("Segmenti"));
    QHash<qint64, Code> codes;
    for (const Code &c : project->codes())
        codes.insert(c.id, c);
    for (const Document &d : project->documents()) {
        const QList<Segment> segments = project->segmentsForDocument(d.id);
        if (segments.isEmpty())
            continue;
        auto *di = new QTreeWidgetItem(segmentsRoot, {d.name});
        di->setFlags(Qt::ItemIsEnabled);
        di->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        const QString text = project->documentText(d.id);
        for (const Segment &s : segments)
            checkable(di, tr("%1: %2").arg(codes.value(s.codeId).name, TextUtils::excerpt(text, s.start, s.end, 80)),
                      MemoTarget::Segment, s.id);
    }

    auto *filter = new QLineEdit;
    filter->setPlaceholderText(tr("Filtra per nome o testo…"));
    filter->setClearButtonEnabled(true);
    connect(filter, &QLineEdit::textChanged, this, [this](const QString &text) {
        // An item stays visible when it or one of its descendants matches.
        std::function<bool(QTreeWidgetItem *)> apply = [&](QTreeWidgetItem *item) {
            bool visible = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive);
            for (int i = 0; i < item->childCount(); ++i)
                visible = apply(item->child(i)) || visible;
            item->setHidden(!visible);
            if (visible && !text.isEmpty())
                item->setExpanded(true);
            return visible;
        };
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
            QTreeWidgetItem *top = m_tree->topLevelItem(i);
            for (int j = 0; j < top->childCount(); ++j)
                apply(top->child(j));
        }
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Collega"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    auto *label = new QLabel(tr("Seleziona gli elementi da collegare al memo:"));
    layout->addWidget(label);
    layout->addWidget(filter);
    layout->addWidget(m_tree, 1);
    layout->addWidget(buttons);
    filter->setFocus();
}

QList<MemoLink> MemoLinkPicker::selectedLinks() const
{
    QList<MemoLink> out;
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        const QTreeWidgetItem *item = *it;
        if (!item->data(0, TypeRole).isValid() || item->checkState(0) != Qt::Checked)
            continue;
        const MemoLink link{static_cast<MemoTarget>(item->data(0, TypeRole).toInt()), item->data(0, IdRole).toLongLong()};
        if (!m_existing.contains(link))
            out << link;
    }
    return out;
}
