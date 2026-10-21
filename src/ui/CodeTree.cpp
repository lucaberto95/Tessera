#include "ui/CodeTree.h"
#include "core/Project.h"
#include "core/TextUtils.h"
#include "ui/UiUtils.h"

#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDropEvent>
#include <QHeaderView>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include <functional>

CodeTree::CodeTree(Project *project, QWidget *parent)
    : QTreeWidget(parent)
    , m_project(project)
{
    setColumnCount(2);
    setHeaderLabels({tr("Codici"), tr("Segm.")});
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setUniformRowHeights(true);

    connect(this, &QTreeWidget::itemChanged, this, &CodeTree::onItemChanged);
    connect(this, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *cur) {
        if (!m_rebuilding)
            emit currentCodeChanged(cur ? cur->data(0, IdRole).toLongLong() : 0);
    });
    connect(this, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) {
        if (!m_rebuilding)
            m_collapsed.insert(item->data(0, IdRole).toLongLong());
    });
    connect(this, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        if (!m_rebuilding)
            m_collapsed.remove(item->data(0, IdRole).toLongLong());
    });
}

void CodeTree::rebuild()
{
    m_rebuilding = true;
    const qint64 current = currentCodeId();
    clear();
    if (!m_project->isOpen()) {
        m_rebuilding = false;
        return;
    }
    const QList<Code> codes = m_project->codes();
    const QHash<qint64, int> counts = m_project->segmentCountByCode();
    const QHash<qint64, qint64> memos = m_project->memoIdsFor(MemoTarget::Code);
    QHash<qint64, QList<Code>> children;
    QSet<qint64> existing;
    for (const Code &c : codes) {
        children[c.parentId] << c;
        existing.insert(c.id);
    }
    m_active.intersect(existing);

    QTreeWidgetItem *currentItemPtr = nullptr;
    std::function<int(QTreeWidgetItem *, qint64)> build = [&](QTreeWidgetItem *parentItem, qint64 parentId) {
        int subtotal = 0;
        for (const Code &c : children.value(parentId)) {
            auto *item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(this);
            item->setText(0, c.name);
            item->setIcon(0, Ui::colorIcon(c.color));
            item->setData(0, IdRole, c.id);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            item->setCheckState(0, m_active.contains(c.id) ? Qt::Checked : Qt::Unchecked);
            const int own = counts.value(c.id);
            const int sub = build(item, c.id);
            item->setText(1, own ? QString::number(own) : QString());
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            if (memos.contains(c.id))
                item->setIcon(1, Ui::memoIcon());
            QString tip = c.name;
            if (!c.description.isEmpty())
                tip += QStringLiteral("\n\n") + c.description;
            tip += QStringLiteral("\n\n") + tr("Segmenti: %1 (con sottocodici: %2)").arg(own).arg(own + sub);
            item->setToolTip(0, tip);
            item->setExpanded(!m_collapsed.contains(c.id));
            if (c.id == current)
                currentItemPtr = item;
            subtotal += own + sub;
        }
        return subtotal;
    };
    build(nullptr, 0);
    if (currentItemPtr)
        setCurrentItem(currentItemPtr);
    m_rebuilding = false;
}

qint64 CodeTree::currentCodeId() const
{
    return currentItem() ? currentItem()->data(0, IdRole).toLongLong() : 0;
}

QTreeWidgetItem *CodeTree::findItem(qint64 id) const
{
    QTreeWidgetItemIterator it(const_cast<CodeTree *>(this));
    for (; *it; ++it) {
        if ((*it)->data(0, IdRole).toLongLong() == id)
            return *it;
    }
    return nullptr;
}

void CodeTree::setCurrentCode(qint64 id)
{
    if (QTreeWidgetItem *item = findItem(id))
        setCurrentItem(item);
}

QList<qint64> CodeTree::activeCodeIds() const
{
    QList<qint64> out;
    QTreeWidgetItemIterator it(const_cast<CodeTree *>(this));
    for (; *it; ++it) {
        const qint64 id = (*it)->data(0, IdRole).toLongLong();
        if (m_active.contains(id))
            out << id;
    }
    return out;
}

void CodeTree::setAllActive(bool active)
{
    m_active.clear();
    if (active) {
        for (const Code &c : m_project->codes())
            m_active.insert(c.id);
    }
    rebuild();
    emit activationChanged();
}

void CodeTree::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (m_rebuilding || column != 0)
        return;
    const qint64 id = item->data(0, IdRole).toLongLong();
    const bool now = item->checkState(0) == Qt::Checked;
    if (now == m_active.contains(id))
        return;
    if (now)
        m_active.insert(id);
    else
        m_active.remove(id);
    emit activationChanged();
}

qint64 CodeTree::newCode(qint64 parentId)
{
    const QString name = Ui::askText(this, parentId ? tr("Nuovo sottocodice") : tr("Nuovo codice"),
                                     parentId ? tr("Nome del sottocodice di \"%1\":").arg(m_project->code(parentId).name)
                                              : tr("Nome del codice:"));
    if (name.isEmpty())
        return 0;
    const QColor color = parentId ? m_project->code(parentId).color
                                  : TextUtils::paletteColor(int(m_project->codes().size()));
    const qint64 id = m_project->addCode(parentId, name, color);
    if (id) {
        if (QTreeWidgetItem *parent = findItem(parentId))
            parent->setExpanded(true);
        setCurrentCode(id);
    }
    return id;
}

void CodeTree::renameCode(qint64 id)
{
    const QString name = Ui::askText(this, tr("Rinomina codice"), tr("Nome:"), m_project->code(id).name);
    if (!name.isEmpty())
        m_project->renameCode(id, name);
}

void CodeTree::changeColor(qint64 id)
{
    const QColor color = QColorDialog::getColor(m_project->code(id).color, this, tr("Colore del codice"));
    if (color.isValid())
        m_project->setCodeColor(id, color);
}

void CodeTree::editDescription(qint64 id)
{
    const Code code = m_project->code(id);
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Descrizione di \"%1\"").arg(code.name));
    dlg.resize(480, 320);
    auto *edit = new QPlainTextEdit(code.description);
    edit->setPlaceholderText(tr("Definizione del codice, criteri di applicazione, esempi…"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(edit);
    layout->addWidget(buttons);
    if (dlg.exec() == QDialog::Accepted)
        m_project->setCodeDescription(id, edit->toPlainText());
}

void CodeTree::editMemo(qint64 id)
{
    Ui::editMemo(this, m_project, int(MemoTarget::Code), id, tr("codice \"%1\"").arg(m_project->codePath(id)));
}

void CodeTree::deleteCode(qint64 id)
{
    const int subcodes = int(m_project->codeDescendants(id).size());
    QString text = tr("Eliminare il codice \"%1\" e tutte le sue codifiche?").arg(m_project->code(id).name);
    if (subcodes)
        text += QLatin1Char('\n') + tr("Verranno eliminati anche %n sottocodici.", nullptr, subcodes);
    if (Ui::confirm(this, tr("Elimina codice"), text))
        m_project->deleteCode(id);
}

void CodeTree::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_project->isOpen())
        return;
    QTreeWidgetItem *item = itemAt(viewport()->mapFromGlobal(event->globalPos()));
    if (item)
        setCurrentItem(item);
    const qint64 id = item ? item->data(0, IdRole).toLongLong() : 0;

    QMenu menu(this);
    menu.addAction(tr("Nuovo codice…"), this, [this] { newCode(0); });
    if (id) {
        menu.addAction(tr("Nuovo sottocodice…"), this, [this, id] { newCode(id); });
        menu.addSeparator();
        menu.addAction(tr("Rinomina…"), this, [this, id] { renameCode(id); });
        menu.addAction(tr("Colore…"), this, [this, id] { changeColor(id); });
        menu.addAction(tr("Descrizione…"), this, [this, id] { editDescription(id); });
        menu.addAction(Ui::memoIcon(), tr("Memo…"), this, [this, id] { editMemo(id); });
        menu.addSeparator();
        menu.addAction(tr("Attiva solo questo codice"), this, [this, id] {
            m_active = {id};
            rebuild();
            emit activationChanged();
        });
        menu.addAction(tr("Attiva con i sottocodici"), this, [this, id] {
            m_active.insert(id);
            for (qint64 c : m_project->codeDescendants(id))
                m_active.insert(c);
            rebuild();
            emit activationChanged();
        });
        menu.addSeparator();
        menu.addAction(tr("Elimina…"), this, [this, id] { deleteCode(id); });
    }
    menu.addSeparator();
    menu.addAction(tr("Attiva tutti i codici"), this, [this] { setAllActive(true); });
    menu.addAction(tr("Disattiva tutti i codici"), this, [this] { setAllActive(false); });
    menu.addAction(tr("Espandi tutto"), this, [this] {
        m_collapsed.clear();
        expandAll();
    });
    menu.addAction(tr("Comprimi tutto"), this, [this] { collapseAll(); });
    menu.exec(event->globalPos());
}

QStringList CodeTree::mimeTypes() const { return {QString::fromLatin1(Ui::CodeMimeType)}; }

QMimeData *CodeTree::mimeData(const QList<QTreeWidgetItem *> &items) const
{
    if (items.isEmpty())
        return nullptr;
    auto *mime = new QMimeData;
    mime->setData(QString::fromLatin1(Ui::CodeMimeType), QByteArray::number(items.first()->data(0, IdRole).toLongLong()));
    mime->setText(items.first()->text(0));
    return mime;
}

void CodeTree::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->source() != this) {
        event->ignore();
        return;
    }
    QTreeWidget::dragMoveEvent(event);
}

void CodeTree::dropEvent(QDropEvent *event)
{
    if (event->source() != this || !event->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType))) {
        event->ignore();
        return;
    }
    const qint64 dragged = event->mimeData()->data(QString::fromLatin1(Ui::CodeMimeType)).toLongLong();
    QTreeWidgetItem *target = itemAt(event->position().toPoint());
    qint64 newParent = 0;
    int position = -1;
    switch (dropIndicatorPosition()) {
    case QAbstractItemView::OnItem:
        newParent = target ? target->data(0, IdRole).toLongLong() : 0;
        break;
    case QAbstractItemView::AboveItem:
    case QAbstractItemView::BelowItem: {
        QTreeWidgetItem *parent = target ? target->parent() : nullptr;
        newParent = parent ? parent->data(0, IdRole).toLongLong() : 0;
        int index = parent ? parent->indexOfChild(target) : indexOfTopLevelItem(target);
        // Index among siblings excluding the dragged code itself.
        QTreeWidgetItem *draggedItem = findItem(dragged);
        const int draggedIndex = draggedItem && draggedItem->parent() == parent
                                     ? (parent ? parent->indexOfChild(draggedItem) : indexOfTopLevelItem(draggedItem))
                                     : -1;
        if (draggedIndex >= 0 && draggedIndex < index)
            --index;
        position = dropIndicatorPosition() == QAbstractItemView::AboveItem ? index : index + 1;
        break;
    }
    case QAbstractItemView::OnViewport:
        newParent = 0;
        break;
    }
    // The model is rebuilt from the project: never let QTreeWidget move items itself.
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
    if (newParent == dragged)
        return;
    if (!m_project->moveCode(dragged, newParent, position))
        Ui::showError(this, m_project->lastError());
    else if (QTreeWidgetItem *p = findItem(newParent))
        p->setExpanded(true);
}
