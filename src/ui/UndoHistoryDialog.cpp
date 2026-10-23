#include "ui/UndoHistoryDialog.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
constexpr int KindRole = Qt::UserRole;      // 0 = current state, 1 = undo step, 2 = redo step
constexpr int CountRole = Qt::UserRole + 1; // number of steps to undo/redo to reach this row
} // namespace

UndoHistoryDialog::UndoHistoryDialog(Project *project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_list(new QListWidget)
    , m_apply(new QPushButton)
{
    setWindowTitle(tr("Cronologia di annulla"));
    resize(420, 480);

    auto *hint = new QLabel(tr("In alto le azioni ripetibili, sotto lo stato attuale, poi le azioni annullabili "
                               "(le più recenti per prime). Scegli un punto e torna a quello stato in un solo passo."));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: palette(mid)"));

    m_list->setSelectionMode(QAbstractItemView::SingleSelection);

    m_apply->setDefault(false);
    m_apply->setAutoDefault(false);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->addButton(m_apply, QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_apply, &QPushButton::clicked, this, &UndoHistoryDialog::applySelected);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &UndoHistoryDialog::updateButton);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { applySelected(); });

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_list, 1);
    layout->addWidget(buttons);

    connect(m_project, &Project::undoStateChanged, this, &UndoHistoryDialog::refresh);
    refresh();
}

void UndoHistoryDialog::refresh()
{
    m_list->clear();
    const QStringList redo = m_project->redoLabels(); // next one first
    const QStringList undo = m_project->undoLabels(); // most recent first

    for (qsizetype i = redo.size() - 1; i >= 0; --i) {
        auto *item = new QListWidgetItem(tr("Ripeti: %1").arg(redo.at(i)));
        item->setData(KindRole, 2);
        item->setData(CountRole, int(i) + 1);
        m_list->addItem(item);
    }
    auto *current = new QListWidgetItem(tr("— Stato attuale —"));
    QFont bold = current->font();
    bold.setBold(true);
    current->setFont(bold);
    current->setTextAlignment(Qt::AlignCenter);
    current->setData(KindRole, 0);
    current->setFlags(Qt::ItemIsEnabled);
    m_list->addItem(current);
    for (qsizetype i = 0; i < undo.size(); ++i) {
        auto *item = new QListWidgetItem(tr("Annulla: %1").arg(undo.at(i)));
        item->setData(KindRole, 1);
        item->setData(CountRole, int(i) + 1);
        m_list->addItem(item);
    }
    m_list->scrollToItem(current);
    updateButton();
}

void UndoHistoryDialog::updateButton()
{
    const QListWidgetItem *item = m_list->currentItem();
    const int kind = item && item->isSelected() ? item->data(KindRole).toInt() : 0;
    const int count = item ? item->data(CountRole).toInt() : 0;
    m_apply->setEnabled(kind != 0);
    if (kind == 1)
        m_apply->setText(tr("Annulla %n azione/i", nullptr, count));
    else if (kind == 2)
        m_apply->setText(tr("Ripeti %n azione/i", nullptr, count));
    else
        m_apply->setText(tr("Torna a questo punto"));
}

int UndoHistoryDialog::currentStateRow() const
{
    return int(m_project->redoLabels().size());
}

int UndoHistoryDialog::rowCount() const
{
    return m_list->count();
}

void UndoHistoryDialog::selectRow(int row)
{
    m_list->setCurrentRow(row);
}

bool UndoHistoryDialog::applySelected()
{
    const QListWidgetItem *item = m_list->currentItem();
    if (!item)
        return false;
    const int kind = item->data(KindRole).toInt();
    const int count = item->data(CountRole).toInt();
    if (kind == 0)
        return false;
    const bool ok = kind == 1 ? m_project->undoSteps(count) : m_project->redoSteps(count);
    if (!ok)
        Ui::showError(this, m_project->lastError());
    return ok;
}
