#include "ui/MemoManager.h"
#include "core/Exporters.h"
#include "core/Project.h"
#include "core/TextUtils.h"
#include "ui/MemoDialog.h"
#include "ui/UiUtils.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLocale>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int MemoIdRole = Qt::UserRole;
}

MemoManager::MemoManager(Project *project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_table(new QTableWidget)
{
    setWindowTitle(tr("Memo del progetto"));
    setWindowIcon(Ui::memoIcon());
    resize(900, 520);

    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels({tr("Collegato a"), tr("Titolo"), tr("Anteprima"), tr("Autore"), tr("Modificato")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setColumnWidth(0, 220);
    m_table->setColumnWidth(1, 180);
    m_table->setSortingEnabled(true);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &MemoManager::openRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QPushButton *newMemo = buttons->addButton(tr("Nuovo memo libero…"), QDialogButtonBox::ActionRole);
    QPushButton *open = buttons->addButton(tr("Apri"), QDialogButtonBox::ActionRole);
    QPushButton *go = buttons->addButton(tr("Vai all'oggetto"), QDialogButtonBox::ActionRole);
    QPushButton *exportBtn = buttons->addButton(tr("Esporta…"), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(newMemo, &QPushButton::clicked, this, [this] {
        Memo m;
        m.targetType = MemoTarget::Free;
        Ui::openMemo(parentWidget() ? parentWidget() : this, m_project, m);
    });
    connect(open, &QPushButton::clicked, this, [this] {
        if (m_table->currentRow() >= 0)
            openRow(m_table->currentRow());
    });
    connect(go, &QPushButton::clicked, this, [this] {
        const int row = m_table->currentRow();
        if (row < 0)
            return;
        const QList<MemoLink> links = m_project->memoLinks(m_table->item(row, 0)->data(MemoIdRole).toLongLong());
        if (!links.isEmpty())
            emit showTarget(int(links.first().targetType), links.first().targetId);
    });
    connect(exportBtn, &QPushButton::clicked, this, [this] {
        const QString path = Ui::saveFileName(this, tr("Esporta memo"), m_project->name() + tr(" - memo.html"),
                                              tr("Pagina HTML (*.html);;CSV per Excel (*.csv)"));
        if (path.isEmpty())
            return;
        Exporters::Table rows{{tr("Collegato a"), tr("Titolo"), tr("Testo"), tr("Autore"), tr("Creato"), tr("Modificato")}};
        for (const Memo &m : m_project->memos())
            rows << QStringList{targetLabel(m), m.title, m.body, m.author,
                                QLocale().toString(m.createdAt.toLocalTime(), QLocale::ShortFormat),
                                QLocale().toString(m.modifiedAt.toLocalTime(), QLocale::ShortFormat)};
        QString error;
        const bool ok = path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)
                            ? Exporters::writeCsv(path, rows, &error)
                            : Exporters::writeHtmlTable(path, tr("Memo – %1").arg(m_project->name()), rows, &error);
        if (!ok)
            Ui::showError(this, error);
    });
    connect(m_project, &Project::memosChanged, this, &MemoManager::refresh);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_table);
    layout->addWidget(buttons);
    refresh();
}

// All the elements of the memo, or "Progetto" / "Memo libero" when it has none.
QString MemoManager::targetLabel(const Memo &memo) const
{
    QStringList labels;
    if (memo.targetType == MemoTarget::Project)
        labels << Ui::memoTargetLabel(m_project, int(MemoTarget::Project), 0);
    for (const MemoLink &l : m_project->memoLinks(memo.id))
        labels << Ui::memoTargetLabel(m_project, int(l.targetType), l.targetId);
    if (labels.isEmpty())
        labels << Ui::memoTargetLabel(m_project, int(MemoTarget::Free), 0);
    return labels.join(QStringLiteral("; "));
}

void MemoManager::refresh()
{
    m_table->setSortingEnabled(false);
    const QList<Memo> memos = m_project->memos();
    m_table->setRowCount(int(memos.size()));
    for (int row = 0; row < memos.size(); ++row) {
        const Memo &m = memos[row];
        auto *target = new QTableWidgetItem(Ui::memoIcon(), targetLabel(m));
        target->setToolTip(Ui::plainToolTip(target->text().replace(QStringLiteral("; "), QStringLiteral("\n"))));
        target->setData(MemoIdRole, m.id);
        m_table->setItem(row, 0, target);
        m_table->setItem(row, 1, new QTableWidgetItem(m.title));
        m_table->setItem(row, 2, new QTableWidgetItem(m.body.simplified().left(200)));
        m_table->setItem(row, 3, new QTableWidgetItem(m.author));
        auto *modified = new QTableWidgetItem(QLocale().toString(m.modifiedAt.toLocalTime(), QLocale::ShortFormat));
        modified->setData(Qt::UserRole + 1, m.modifiedAt);
        m_table->setItem(row, 4, modified);
    }
    m_table->setSortingEnabled(true);
}

void MemoManager::openRow(int row)
{
    const Memo m = m_project->memo(m_table->item(row, 0)->data(MemoIdRole).toLongLong());
    if (!m.id)
        return;
    Ui::openMemo(parentWidget() ? parentWidget() : this, m_project, m);
}
