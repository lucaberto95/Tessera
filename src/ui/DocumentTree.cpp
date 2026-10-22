#include "ui/DocumentTree.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QContextMenuEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QMenu>
#include <QStyle>
#include <QTreeWidgetItemIterator>

DocumentTree::DocumentTree(Project *project, QWidget *parent)
    : QTreeWidget(parent)
    , m_project(project)
{
    setColumnCount(2);
    setHeaderLabels({tr("Documenti"), tr("Segm.")});
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setUniformRowHeights(true);
    // Documents can be dragged onto a set (to add them) or onto a group (to move them).
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::CopyAction);

    connect(this, &QTreeWidget::itemChanged, this, &DocumentTree::onItemChanged);
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (isDocument(item))
            emit documentOpened(item->data(0, IdRole).toLongLong());
    });
    connect(this, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        if (!m_rebuilding)
            m_collapsed.insert(expansionKey(item));
    });
    connect(this, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        if (!m_rebuilding)
            m_collapsed.remove(expansionKey(item));
    });
}

bool DocumentTree::isDocument(const QTreeWidgetItem *item)
{
    const int kind = item ? item->data(0, KindRole).toInt() : 0;
    return kind == DocumentItem || kind == SetMemberItem;
}

// Groups and sets have separate id spaces, so the key includes the kind.
QString DocumentTree::expansionKey(const QTreeWidgetItem *item)
{
    return QString::number(item->data(0, KindRole).toInt()) + QLatin1Char(':') +
           QString::number(item->data(0, IdRole).toLongLong());
}

void DocumentTree::rebuild()
{
    // A document can appear in several sets: identify the selection by the parent too.
    auto keyOf = [](const QTreeWidgetItem *item) {
        return item->parent() ? expansionKey(item) + QLatin1Char('/') + expansionKey(item->parent())
                              : expansionKey(item);
    };
    m_rebuilding = true;
    const QString selected = currentItem() ? keyOf(currentItem()) : QString();
    clear();
    if (!m_project->isOpen()) {
        m_rebuilding = false;
        return;
    }

    const QHash<qint64, int> counts = m_project->segmentCountByDocument();
    const QHash<qint64, qint64> docMemos = m_project->memoIdsFor(MemoTarget::Document);
    const QHash<qint64, qint64> groupMemos = m_project->memoIdsFor(MemoTarget::Group);
    const QHash<qint64, qint64> setMemos = m_project->memoIdsFor(MemoTarget::DocumentSet);
    const QIcon groupIcon = style()->standardIcon(QStyle::SP_DirIcon);
    const QIcon docIcon = style()->standardIcon(QStyle::SP_FileIcon);
    const QIcon setIcon = style()->standardIcon(QStyle::SP_DirLinkIcon);
    const QIcon memberIcon = style()->standardIcon(QStyle::SP_FileLinkIcon);

    auto restoreSelection = [&](QTreeWidgetItem *item) {
        if (keyOf(item) == selected)
            setCurrentItem(item);
    };
    // Returns the document's segment count, summed up by the group or set.
    auto addDocument = [&](QTreeWidgetItem *parent, const Document &d, Kind kind) {
        auto *di = new QTreeWidgetItem(parent);
        di->setText(0, d.name);
        di->setIcon(0, kind == DocumentItem ? docIcon : memberIcon);
        di->setData(0, KindRole, kind);
        di->setData(0, IdRole, d.id);
        di->setFlags(di->flags() | Qt::ItemIsUserCheckable);
        di->setCheckState(0, m_active.contains(d.id) ? Qt::Checked : Qt::Unchecked);
        const int n = counts.value(d.id);
        if (n)
            di->setText(1, QString::number(n));
        di->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        if (docMemos.contains(d.id))
            di->setIcon(1, Ui::memoIcon());
        QString tip = d.name;
        if (!d.sourcePath.isEmpty())
            tip += QLatin1Char('\n') + d.sourcePath;
        di->setToolTip(0, Ui::plainToolTip(tip));
        restoreSelection(di);
        return n;
    };
    auto initContainer = [](QTreeWidgetItem *item, const QString &name, const QIcon &icon, Kind kind, qint64 id) {
        item->setText(0, name);
        item->setIcon(0, icon);
        item->setData(0, KindRole, kind);
        item->setData(0, IdRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
    };
    auto finishContainer = [&](QTreeWidgetItem *item, int segmentCount, bool hasMemo) {
        if (item->childCount() == 0)
            item->setCheckState(0, Qt::Unchecked);
        if (segmentCount)
            item->setText(1, QString::number(segmentCount));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        if (hasMemo)
            item->setIcon(1, Ui::memoIcon());
        item->setExpanded(!m_collapsed.contains(expansionKey(item)));
        restoreSelection(item);
    };

    QSet<qint64> existing;
    QHash<qint64, Document> documents;
    for (const DocumentGroup &g : m_project->groups()) {
        auto *gi = new QTreeWidgetItem(this);
        initContainer(gi, g.name, groupIcon, GroupItem, g.id);
        int groupCount = 0;
        for (const Document &d : m_project->documentsInGroup(g.id)) {
            existing.insert(d.id);
            documents.insert(d.id, d);
            groupCount += addDocument(gi, d, DocumentItem);
        }
        finishContainer(gi, groupCount, groupMemos.contains(g.id));
    }

    auto *root = new QTreeWidgetItem(this);
    root->setText(0, tr("Set di documenti"));
    root->setIcon(0, setIcon);
    root->setData(0, KindRole, SetsRootItem);
    root->setData(0, IdRole, 0);
    root->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    root->setToolTip(0, tr("Trascina qui sopra i documenti, o usa il menu contestuale per creare un set."));
    for (const DocumentSet &s : m_project->documentSets()) {
        auto *si = new QTreeWidgetItem(root);
        initContainer(si, s.name, setIcon, SetItem, s.id);
        int setCount = 0;
        for (qint64 id : m_project->documentSetMembers(s.id))
            setCount += addDocument(si, documents.value(id), SetMemberItem);
        finishContainer(si, setCount, setMemos.contains(s.id));
    }
    root->setExpanded(!m_collapsed.contains(expansionKey(root)));
    restoreSelection(root);

    m_active.intersect(existing);
    m_rebuilding = false;
}

QList<qint64> DocumentTree::activeDocumentIds() const
{
    // Keep tree order; every document appears exactly once among the groups.
    QList<qint64> out;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem *g = topLevelItem(i);
        if (g->data(0, KindRole).toInt() != GroupItem)
            continue;
        for (int j = 0; j < g->childCount(); ++j) {
            const qint64 id = g->child(j)->data(0, IdRole).toLongLong();
            if (m_active.contains(id))
                out << id;
        }
    }
    return out;
}

qint64 DocumentTree::currentGroupId() const
{
    QTreeWidgetItem *item = currentItem();
    if (item && item->data(0, KindRole).toInt() == DocumentItem)
        item = item->parent();
    if (item && item->data(0, KindRole).toInt() == GroupItem)
        return item->data(0, IdRole).toLongLong();
    QTreeWidgetItem *first = topLevelItemCount() ? topLevelItem(0) : nullptr;
    return first && first->data(0, KindRole).toInt() == GroupItem ? first->data(0, IdRole).toLongLong() : 0;
}

void DocumentTree::setCurrentDocument(qint64 id)
{
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem *g = topLevelItem(i);
        if (g->data(0, KindRole).toInt() != GroupItem)
            continue;
        for (int j = 0; j < g->childCount(); ++j) {
            if (g->child(j)->data(0, IdRole).toLongLong() == id) {
                setCurrentItem(g->child(j));
                return;
            }
        }
    }
}

void DocumentTree::setAllActive(bool active)
{
    QList<qint64> ids;
    if (active) {
        for (const Document &d : m_project->documents())
            ids << d.id;
    }
    setActiveDocuments(ids);
}

void DocumentTree::setActiveDocuments(const QList<qint64> &ids)
{
    m_active = QSet<qint64>(ids.cbegin(), ids.cend());
    rebuild();
    emit activationChanged();
}

qint64 DocumentTree::newDocumentSet(const QList<qint64> &documentIds)
{
    const QString name = Ui::askText(this, tr("Nuovo set di documenti"), tr("Nome del set:"));
    if (name.isEmpty())
        return 0;
    return m_project->addDocumentSet(name, documentIds);
}

qint64 DocumentTree::newDocumentSetFromActive()
{
    const QList<qint64> ids = activeDocumentIds();
    if (ids.isEmpty()) {
        Ui::showError(this, tr("Attiva prima i documenti da mettere nel set."));
        return 0;
    }
    return newDocumentSet(ids);
}

void DocumentTree::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_rebuilding || column != 0 || !isDocument(item))
        return;
    const qint64 id = item->data(0, IdRole).toLongLong();
    const bool before = m_active.contains(id);
    const bool now = item->checkState(0) == Qt::Checked;
    if (before == now)
        return;
    if (now)
        m_active.insert(id);
    else
        m_active.remove(id);
    syncDocumentCheckBoxes(id, item);
    emit activationChanged();
}

// The other entries of the same document (in its group and in other sets) follow its activation.
void DocumentTree::syncDocumentCheckBoxes(qint64 documentId, const QTreeWidgetItem *except)
{
    const bool wasRebuilding = std::exchange(m_rebuilding, true);
    const Qt::CheckState state = m_active.contains(documentId) ? Qt::Checked : Qt::Unchecked;
    for (QTreeWidgetItemIterator it(this); *it; ++it) {
        QTreeWidgetItem *item = *it;
        if (item != except && isDocument(item) && item->data(0, IdRole).toLongLong() == documentId &&
            item->checkState(0) != state)
            item->setCheckState(0, state);
    }
    m_rebuilding = wasRebuilding;
}

void DocumentTree::renameItem(QTreeWidgetItem *item)
{
    const qint64 id = item->data(0, IdRole).toLongLong();
    const int kind = item->data(0, KindRole).toInt();
    const QString title = kind == GroupItem ? tr("Rinomina gruppo")
                          : kind == SetItem ? tr("Rinomina set")
                                            : tr("Rinomina documento");
    const QString name = Ui::askText(this, title, tr("Nome:"), item->text(0));
    if (name.isEmpty())
        return;
    if (kind == GroupItem)
        m_project->renameGroup(id, name);
    else if (kind == SetItem)
        m_project->renameDocumentSet(id, name);
    else
        m_project->renameDocument(id, name);
}

void DocumentTree::deleteItem(QTreeWidgetItem *item)
{
    const qint64 id = item->data(0, IdRole).toLongLong();
    switch (item->data(0, KindRole).toInt()) {
    case GroupItem:
        if (m_project->groups().size() <= 1) {
            Ui::showError(this, tr("Il progetto deve contenere almeno un gruppo di documenti."));
            return;
        }
        if (!Ui::confirm(this, tr("Elimina gruppo"),
                         tr("Eliminare il gruppo \"%1\" con tutti i suoi %2 documenti e le relative codifiche?")
                             .arg(item->text(0), QString::number(item->childCount()))))
            return;
        m_project->deleteGroup(id);
        break;
    case SetItem:
        if (!Ui::confirm(this, tr("Elimina set"),
                         tr("Eliminare il set \"%1\"? I documenti restano nei loro gruppi.").arg(item->text(0))))
            return;
        m_project->deleteDocumentSet(id);
        break;
    case DocumentItem:
        if (!Ui::confirm(this, tr("Elimina documento"),
                         tr("Eliminare il documento \"%1\" e tutte le sue codifiche?").arg(item->text(0))))
            return;
        m_project->deleteDocument(id);
        break;
    }
}

void DocumentTree::editMemo(QTreeWidgetItem *item)
{
    MemoTarget target = MemoTarget::Document;
    QString label = tr("documento ");
    switch (item->data(0, KindRole).toInt()) {
    case GroupItem:
        target = MemoTarget::Group;
        label = tr("gruppo ");
        break;
    case SetItem:
        target = MemoTarget::DocumentSet;
        label = tr("set ");
        break;
    }
    Ui::editMemo(this, m_project, int(target), item->data(0, IdRole).toLongLong(),
                 label + QLatin1Char('"') + item->text(0) + QLatin1Char('"'));
}

void DocumentTree::addSetMenu(QMenu *menu, qint64 documentId)
{
    QMenu *sets = menu->addMenu(tr("Aggiungi al set"));
    const QList<DocumentSet> all = m_project->documentSets();
    for (const DocumentSet &s : all) {
        QAction *a = sets->addAction(s.name, this,
                                     [this, sid = s.id, documentId] { m_project->addToDocumentSet(sid, {documentId}); });
        a->setEnabled(!m_project->documentSetMembers(s.id).contains(documentId));
    }
    if (!all.isEmpty())
        sets->addSeparator();
    sets->addAction(tr("Nuovo set…"), this, [this, documentId] { newDocumentSet({documentId}); });
}

void DocumentTree::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_project->isOpen())
        return;
    QTreeWidgetItem *item = itemAt(viewport()->mapFromGlobal(event->globalPos()));
    if (item)
        setCurrentItem(item);
    const int kind = item ? item->data(0, KindRole).toInt() : 0;
    QMenu menu(this);

    if (isDocument(item)) {
        const qint64 id = item->data(0, IdRole).toLongLong();
        menu.addAction(tr("Apri documento"), this, [this, id] { emit documentOpened(id); });
        menu.addSeparator();
        if (kind == DocumentItem) {
            QMenu *move = menu.addMenu(tr("Sposta nel gruppo"));
            for (const DocumentGroup &g : m_project->groups()) {
                QAction *a = move->addAction(g.name, this, [this, id, gid = g.id] { m_project->moveDocument(id, gid); });
                a->setEnabled(g.id != item->parent()->data(0, IdRole).toLongLong());
            }
        }
        addSetMenu(&menu, id);
        if (kind == SetMemberItem) {
            const qint64 setId = item->parent()->data(0, IdRole).toLongLong();
            menu.addAction(tr("Rimuovi dal set"), this,
                           [this, setId, id] { m_project->removeFromDocumentSet(setId, {id}); });
        }
        menu.addSeparator();
    }

    if (kind == SetsRootItem || kind == SetItem || kind == SetMemberItem) {
        menu.addAction(tr("Nuovo set di documenti…"), this, [this] { newDocumentSet(); });
        if (kind == SetItem) {
            const qint64 id = item->data(0, IdRole).toLongLong();
            menu.addAction(tr("Attiva solo i documenti del set"), this,
                           [this, id] { setActiveDocuments(m_project->documentSetMembers(id)); });
        }
    } else {
        const qint64 groupId = currentGroupId();
        menu.addAction(tr("Importa documenti…"), this, [this, groupId] { emit importRequested(groupId); });
        menu.addAction(tr("Nuovo documento di testo…"), this, [this, groupId] { emit newTextDocumentRequested(groupId); });
        menu.addAction(tr("Nuovo gruppo…"), this, [this] {
            const QString name = Ui::askText(this, tr("Nuovo gruppo"), tr("Nome del gruppo:"));
            if (!name.isEmpty())
                m_project->addGroup(name);
        });
    }

    if (kind == GroupItem || kind == DocumentItem || kind == SetItem || kind == SetMemberItem) {
        menu.addSeparator();
        menu.addAction(Ui::memoIcon(), tr("Memo…"), this, [this, item] { editMemo(item); });
        if (kind != SetMemberItem) {
            menu.addAction(tr("Rinomina…"), this, [this, item] { renameItem(item); });
            menu.addAction(tr("Elimina…"), this, [this, item] { deleteItem(item); });
        }
    }
    menu.addSeparator();
    menu.addAction(tr("Attiva tutti i documenti"), this, [this] { setAllActive(true); });
    menu.addAction(tr("Disattiva tutti i documenti"), this, [this] { setAllActive(false); });
    QAction *fromActive = menu.addAction(tr("Crea set dai documenti attivi…"), this, [this] { newDocumentSetFromActive(); });
    fromActive->setEnabled(!m_active.isEmpty());
    menu.exec(event->globalPos());
}

// What dropping the dragged document here would do: {SetItem, setId} adds it to that set,
// {GroupItem, groupId} moves it to that group, {0, 0} nothing.
std::pair<int, qint64> DocumentTree::dropTarget(QDropEvent *event, qint64 *documentId) const
{
    const QTreeWidgetItem *dragged = event->source() == this ? currentItem() : nullptr;
    const QTreeWidgetItem *target = itemAt(event->position().toPoint());
    if (!isDocument(dragged) || !target)
        return {0, 0};
    const qint64 doc = dragged->data(0, IdRole).toLongLong();
    int kind = target->data(0, KindRole).toInt();
    qint64 id = target->data(0, IdRole).toLongLong();
    if (kind == DocumentItem || kind == SetMemberItem) {
        kind = kind == DocumentItem ? GroupItem : SetItem;
        id = target->parent()->data(0, IdRole).toLongLong();
    }
    if (kind == SetItem ? m_project->documentSetMembers(id).contains(doc)
                        : kind != GroupItem || m_project->document(doc).groupId == id)
        return {0, 0};
    *documentId = doc;
    return {kind, id};
}

void DocumentTree::dragMoveEvent(QDragMoveEvent *event)
{
    QTreeWidget::dragMoveEvent(event); // auto-scroll and drop indicator
    qint64 doc = 0;
    const int kind = dropTarget(event, &doc).first;
    if (kind) {
        event->setDropAction(kind == SetItem ? Qt::CopyAction : Qt::MoveAction);
        event->accept();
    } else {
        event->ignore();
    }
}

void DocumentTree::dropEvent(QDropEvent *event)
{
    qint64 doc = 0;
    const auto [kind, id] = dropTarget(event, &doc);
    // The tree never moves items itself: the project changes and rebuild() redraws it.
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    if (kind == SetItem)
        m_project->addToDocumentSet(id, {doc});
    else if (kind == GroupItem)
        m_project->moveDocument(doc, id);
}
