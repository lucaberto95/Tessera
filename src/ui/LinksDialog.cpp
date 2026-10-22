#include "ui/LinksDialog.h"
#include "core/Project.h"
#include "core/TextUtils.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int LinkIdRole = Qt::UserRole;
constexpr int TargetColumn = 2; // columns from here to the comment belong to the target end
constexpr int CommentColumn = 4;
} // namespace

LinksDialog::LinksDialog(Project *project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_table(new QTableWidget)
{
    setWindowTitle(tr("Collegamenti"));
    resize(1000, 480);

    auto *hint = new QLabel(tr("Doppio clic sulla partenza o sull'arrivo per aprire quel brano nel documento."));
    hint->setStyleSheet(QStringLiteral("color: palette(mid)"));

    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({tr("Da"), tr("Testo di partenza"), tr("A"), tr("Testo di arrivo"),
                                        tr("Commento"), tr("Autore"), tr("Creato")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->setColumnWidth(0, 150);
    m_table->setColumnWidth(2, 150);
    m_table->setColumnWidth(CommentColumn, 200);
    m_table->setSortingEnabled(true);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int column) {
        if (column == CommentColumn)
            editComment();
        else
            openCurrentEnd(column >= TargetColumn);
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QPushButton *origin = buttons->addButton(tr("Vai alla partenza"), QDialogButtonBox::ActionRole);
    QPushButton *target = buttons->addButton(tr("Vai all'arrivo"), QDialogButtonBox::ActionRole);
    QPushButton *comment = buttons->addButton(tr("Commento…"), QDialogButtonBox::ActionRole);
    QPushButton *remove = buttons->addButton(tr("Elimina"), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(origin, &QPushButton::clicked, this, [this] { openCurrentEnd(false); });
    connect(target, &QPushButton::clicked, this, [this] { openCurrentEnd(true); });
    connect(comment, &QPushButton::clicked, this, &LinksDialog::editComment);
    connect(remove, &QPushButton::clicked, this, &LinksDialog::deleteCurrent);
    auto updateButtons = [=, this] {
        const bool any = currentLinkId() != 0;
        for (QPushButton *b : {origin, target, comment, remove})
            b->setEnabled(any);
    };
    connect(m_table, &QTableWidget::itemSelectionChanged, this, updateButtons);

    // Documents may be renamed or their text edited while the window is open.
    connect(m_project, &Project::linksChanged, this, &LinksDialog::refresh);
    connect(m_project, &Project::documentsChanged, this, &LinksDialog::refresh);
    connect(m_project, &Project::linksChanged, this, updateButtons);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_table);
    layout->addWidget(buttons);
    refresh();
    updateButtons();
}

qint64 LinksDialog::currentLinkId() const
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0) || !m_table->item(row, 0)->isSelected())
        return 0;
    return m_table->item(row, 0)->data(LinkIdRole).toLongLong();
}

void LinksDialog::selectLink(qint64 linkId)
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (m_table->item(row, 0)->data(LinkIdRole).toLongLong() == linkId) {
            m_table->selectRow(row);
            return;
        }
    }
}

void LinksDialog::refresh()
{
    if (!m_project->isOpen()) {
        m_table->setRowCount(0);
        return;
    }
    const qint64 current = currentLinkId();
    m_table->setSortingEnabled(false);
    const QList<DocumentLink> links = m_project->links();
    QHash<qint64, QString> names, texts;
    for (const Document &d : m_project->documents())
        names.insert(d.id, d.name);
    auto textOf = [&](qint64 doc) {
        if (!texts.contains(doc))
            texts.insert(doc, m_project->documentText(doc));
        return texts.value(doc);
    };
    m_table->setRowCount(int(links.size()));
    for (int row = 0; row < links.size(); ++row) {
        const DocumentLink &l = links[row];
        auto *from = new QTableWidgetItem(names.value(l.fromDocumentId));
        from->setData(LinkIdRole, l.id);
        m_table->setItem(row, 0, from);
        m_table->setItem(row, 1, new QTableWidgetItem(TextUtils::excerpt(textOf(l.fromDocumentId), l.fromStart, l.fromEnd, 160)));
        m_table->setItem(row, 2, new QTableWidgetItem(names.value(l.toDocumentId)));
        m_table->setItem(row, 3, new QTableWidgetItem(TextUtils::excerpt(textOf(l.toDocumentId), l.toStart, l.toEnd, 160)));
        m_table->setItem(row, CommentColumn, new QTableWidgetItem(l.comment.simplified()));
        m_table->setItem(row, 5, new QTableWidgetItem(l.author));
        auto *created = new QTableWidgetItem(QLocale().toString(l.createdAt.toLocalTime(), QLocale::ShortFormat));
        created->setData(Qt::UserRole + 1, l.createdAt);
        m_table->setItem(row, 6, created);
    }
    m_table->setSortingEnabled(true);
    if (current)
        selectLink(current);
}

void LinksDialog::openCurrentEnd(bool target)
{
    const DocumentLink l = m_project->link(currentLinkId());
    if (!l.id)
        return;
    if (target)
        emit endActivated(l.toDocumentId, l.toStart, l.toEnd);
    else
        emit endActivated(l.fromDocumentId, l.fromStart, l.fromEnd);
}

void LinksDialog::editComment()
{
    const DocumentLink l = m_project->link(currentLinkId());
    if (!l.id)
        return;
    bool ok = false;
    const QString c =
        QInputDialog::getMultiLineText(this, tr("Commento al collegamento"), tr("Commento:"), l.comment, &ok);
    if (ok)
        m_project->setLinkComment(l.id, c.trimmed());
}

void LinksDialog::deleteCurrent()
{
    if (const qint64 id = currentLinkId())
        m_project->deleteLink(id); // can be undone with Ctrl+Z
}
