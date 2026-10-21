#include "ui/DocumentTree.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QContextMenuEvent>
#include <QHeaderView>
#include <QMenu>
#include <QStyle>

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

    connect(this, &QTreeWidget::itemChanged, this, &DocumentTree::onItemChanged);
    connect(this, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (item->data(0, KindRole).toInt() == DocumentItem)
            emit documentOpened(item->data(0, IdRole).toLongLong());
    });
    connect(this, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        if (!m_rebuilding)
            m_collapsedGroups.insert(item->data(0, IdRole).toLongLong());
    });
    connect(this, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        if (!m_rebuilding)
            m_collapsedGroups.remove(item->data(0, IdRole).toLongLong());
    });
}

void DocumentTree::rebuild()
{
    m_rebuilding = true;
    const qint64 selectedId = currentItem() ? currentItem()->data(0, IdRole).toLongLong() : 0;
    const int selectedKind = currentItem() ? currentItem()->data(0, KindRole).toInt() : 0;
    clear();
    if (!m_project->isOpen()) {
        m_rebuilding = false;
        return;
    }

    const QHash<qint64, int> counts = m_project->segmentCountByDocument();
    const QHash<qint64, qint64> docMemos = m_project->memoIdsFor(MemoTarget::Document);
    const QHash<qint64, qint64> groupMemos = m_project->memoIdsFor(MemoTarget::Group);
    const QIcon groupIcon = style()->standardIcon(QStyle::SP_DirIcon);
    const QIcon docIcon = style()->standardIcon(QStyle::SP_FileIcon);

    QSet<qint64> existing;
    for (const DocumentGroup &g : m_project->groups()) {
        auto *gi = new QTreeWidgetItem(this);
        gi->setText(0, g.name);
        gi->setIcon(0, groupIcon);
        gi->setData(0, KindRole, GroupItem);
        gi->setData(0, IdRole, g.id);
        gi->setFlags(gi->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        if (groupMemos.contains(g.id))
            gi->setIcon(1, Ui::memoIcon());
        int groupCount = 0;
        for (const Document &d : m_project->documentsInGroup(g.id)) {
            existing.insert(d.id);
            auto *di = new QTreeWidgetItem(gi);
            di->setText(0, d.name);
            di->setIcon(0, docIcon);
            di->setData(0, KindRole, DocumentItem);
            di->setData(0, IdRole, d.id);
            di->setFlags(di->flags() | Qt::ItemIsUserCheckable);
            di->setCheckState(0, m_active.contains(d.id) ? Qt::Checked : Qt::Unchecked);
            const int n = counts.value(d.id);
            groupCount += n;
            if (n)
                di->setText(1, QString::number(n));
            di->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            if (docMemos.contains(d.id))
                di->setIcon(1, Ui::memoIcon());
            QString tip = d.name;
            if (!d.sourcePath.isEmpty())
                tip += QLatin1Char('\n') + d.sourcePath;
            di->setToolTip(0, tip);
            if (selectedKind == DocumentItem && selectedId == d.id)
                setCurrentItem(di);
        }
        if (gi->childCount() == 0)
            gi->setCheckState(0, Qt::Unchecked);
        if (groupCount)
            gi->setText(1, QString::number(groupCount));
        gi->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        gi->setExpanded(!m_collapsedGroups.contains(g.id));
        if (selectedKind == GroupItem && selectedId == g.id)
            setCurrentItem(gi);
    }
    m_active.intersect(existing);
    m_rebuilding = false;
}

QList<qint64> DocumentTree::activeDocumentIds() const
{
    // Keep tree order.
    QList<qint64> out;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem *g = topLevelItem(i);
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
    if (item)
        return item->data(0, IdRole).toLongLong();
    return topLevelItemCount() ? topLevelItem(0)->data(0, IdRole).toLongLong() : 0;
}

void DocumentTree::setCurrentDocument(qint64 id)
{
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem *g = topLevelItem(i);
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
    m_active.clear();
    if (active) {
        for (const Document &d : m_project->documents())
            m_active.insert(d.id);
    }
    rebuild();
    emit activationChanged();
}

void DocumentTree::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_rebuilding || column != 0 || item->data(0, KindRole).toInt() != DocumentItem)
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
    emit activationChanged();
}

void DocumentTree::renameItem(QTreeWidgetItem *item)
{
    const qint64 id = item->data(0, IdRole).toLongLong();
    const bool isGroup = item->data(0, KindRole).toInt() == GroupItem;
    const QString name = Ui::askText(this, isGroup ? tr("Rinomina gruppo") : tr("Rinomina documento"), tr("Nome:"),
                                     item->text(0));
    if (name.isEmpty())
        return;
    if (isGroup)
        m_project->renameGroup(id, name);
    else
        m_project->renameDocument(id, name);
}

void DocumentTree::deleteItem(QTreeWidgetItem *item)
{
    const qint64 id = item->data(0, IdRole).toLongLong();
    if (item->data(0, KindRole).toInt() == GroupItem) {
        if (topLevelItemCount() <= 1) {
            Ui::showError(this, tr("Il progetto deve contenere almeno un gruppo di documenti."));
            return;
        }
        if (!Ui::confirm(this, tr("Elimina gruppo"),
                         tr("Eliminare il gruppo \"%1\" con tutti i suoi %2 documenti e le relative codifiche?")
                             .arg(item->text(0))
                             .arg(item->childCount())))
            return;
        m_project->deleteGroup(id);
    } else {
        if (!Ui::confirm(this, tr("Elimina documento"),
                         tr("Eliminare il documento \"%1\" e tutte le sue codifiche?").arg(item->text(0))))
            return;
        m_project->deleteDocument(id);
    }
}

void DocumentTree::editMemo(QTreeWidgetItem *item)
{
    const bool isGroup = item->data(0, KindRole).toInt() == GroupItem;
    Ui::editMemo(this, m_project, int(isGroup ? MemoTarget::Group : MemoTarget::Document),
                 item->data(0, IdRole).toLongLong(),
                 (isGroup ? tr("gruppo ") : tr("documento ")) + QLatin1Char('"') + item->text(0) + QLatin1Char('"'));
}

void DocumentTree::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_project->isOpen())
        return;
    QTreeWidgetItem *item = itemAt(viewport()->mapFromGlobal(event->globalPos()));
    if (item)
        setCurrentItem(item);
    QMenu menu(this);
    const qint64 groupId = currentGroupId();

    if (item && item->data(0, KindRole).toInt() == DocumentItem) {
        const qint64 id = item->data(0, IdRole).toLongLong();
        menu.addAction(tr("Apri documento"), this, [this, id] { emit documentOpened(id); });
        menu.addSeparator();
        QMenu *move = menu.addMenu(tr("Sposta nel gruppo"));
        for (const DocumentGroup &g : m_project->groups()) {
            QAction *a = move->addAction(g.name, this, [this, id, gid = g.id] { m_project->moveDocument(id, gid); });
            a->setEnabled(g.id != item->parent()->data(0, IdRole).toLongLong());
        }
    }
    menu.addAction(tr("Importa documenti…"), this, [this, groupId] { emit importRequested(groupId); });
    menu.addAction(tr("Nuovo documento di testo…"), this, [this, groupId] { emit newTextDocumentRequested(groupId); });
    menu.addAction(tr("Nuovo gruppo…"), this, [this] {
        const QString name = Ui::askText(this, tr("Nuovo gruppo"), tr("Nome del gruppo:"));
        if (!name.isEmpty())
            m_project->addGroup(name);
    });
    if (item) {
        menu.addSeparator();
        menu.addAction(Ui::memoIcon(), tr("Memo…"), this, [this, item] { editMemo(item); });
        menu.addAction(tr("Rinomina…"), this, [this, item] { renameItem(item); });
        menu.addAction(tr("Elimina…"), this, [this, item] { deleteItem(item); });
    }
    menu.addSeparator();
    menu.addAction(tr("Attiva tutti i documenti"), this, [this] { setAllActive(true); });
    menu.addAction(tr("Disattiva tutti i documenti"), this, [this] { setAllActive(false); });
    menu.exec(event->globalPos());
}
