#include "ui/VariableFilterDialog.h"
#include "core/Exporters.h"
#include "core/Project.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

using namespace VariableFilter;

VariableFilterDialog::VariableFilterDialog(Project *project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_combine(new QComboBox)
    , m_rowsLayout(new QVBoxLayout)
    , m_preview(new QLabel)
{
    setWindowTitle(tr("Attiva documenti per variabili"));
    resize(720, 420);

    m_combine->addItem(tr("Tutte le condizioni (E)"), false);
    m_combine->addItem(tr("Almeno una condizione (O)"), true);

    auto *add = new QPushButton(tr("Aggiungi condizione"));
    m_rowsLayout->setContentsMargins(0, 0, 0, 0);

    auto *buttons = new QDialogButtonBox;
    m_activate = buttons->addButton(tr("Attiva"), QDialogButtonBox::AcceptRole);
    m_saveSet = buttons->addButton(tr("Salva come set…"), QDialogButtonBox::ActionRole);
    buttons->addButton(tr("Chiudi"), QDialogButtonBox::RejectRole);
    connect(m_activate, &QPushButton::clicked, this, [this] {
        remember();
        emit activationRequested(matchingDocuments());
    });
    connect(m_saveSet, &QPushButton::clicked, this, [this] {
        remember();
        emit setRequested(matchingDocuments());
    });
    connect(buttons, &QDialogButtonBox::rejected, this, [this] {
        remember();
        close();
    });

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Documenti che soddisfano:")));
    top->addWidget(m_combine);
    top->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addLayout(m_rowsLayout);
    layout->addWidget(add, 0, Qt::AlignLeft);
    layout->addStretch(1);
    layout->addWidget(m_preview);
    layout->addWidget(buttons);

    connect(add, &QPushButton::clicked, this, [this] {
        Condition c;
        const auto vars = m_project->variables();
        if (!vars.isEmpty())
            c.variableId = vars.first().id;
        addRow(c);
        updateState();
    });
    connect(m_combine, &QComboBox::currentIndexChanged, this, &VariableFilterDialog::updateState);
    connect(m_project, &Project::variablesChanged, this, &VariableFilterDialog::updateState);
    connect(m_project, &Project::documentsChanged, this, &VariableFilterDialog::updateState);

    const Saved saved = loadSaved(*m_project);
    setConditions(saved.conditions, saved.any);
    if (m_rows.isEmpty())
        add->click();
}

VariableType VariableFilterDialog::typeOf(qint64 variableId) const
{
    for (const Variable &v : m_project->variables())
        if (v.id == variableId)
            return v.type;
    return VariableType::Text;
}

void VariableFilterDialog::addRow(const Condition &condition)
{
    Row row;
    row.widget = new QWidget;
    row.variable = new QComboBox;
    row.op = new QComboBox;
    row.value = new QLineEdit;
    row.value2 = new QLineEdit;
    row.value->setPlaceholderText(tr("valore"));
    row.value2->setPlaceholderText(tr("fino a"));

    int index = -1;
    for (const Variable &v : m_project->variables()) {
        row.variable->addItem(QStringLiteral("%1 (%2)").arg(v.name, variableTypeLabel(v.type)), v.id);
        if (v.id == condition.variableId)
            index = row.variable->count() - 1;
    }
    if (index >= 0)
        row.variable->setCurrentIndex(index);
    fillOperators(row, condition.op);
    row.value->setText(condition.value);
    row.value2->setText(condition.value2);

    auto *remove = new QPushButton(tr("Rimuovi"));
    auto *h = new QHBoxLayout(row.widget);
    h->setContentsMargins(0, 0, 0, 0);
    h->addWidget(row.variable, 2);
    h->addWidget(row.op, 1);
    h->addWidget(row.value, 2);
    h->addWidget(row.value2, 2);
    h->addWidget(remove);
    m_rowsLayout->addWidget(row.widget);
    m_rows << row;

    QWidget *widget = row.widget;
    connect(remove, &QPushButton::clicked, this, [this, widget] {
        removeRow(widget);
        updateState();
    });
    connect(row.variable, &QComboBox::currentIndexChanged, this, [this, widget] {
        for (Row &r : m_rows)
            if (r.widget == widget)
                fillOperators(r, r.op->currentData().isValid() ? Op(r.op->currentData().toInt()) : Op::Equals);
        updateState();
    });
    connect(row.op, &QComboBox::currentIndexChanged, this, &VariableFilterDialog::updateState);
    connect(row.value, &QLineEdit::textChanged, this, &VariableFilterDialog::updateState);
    connect(row.value2, &QLineEdit::textChanged, this, &VariableFilterDialog::updateState);
}

void VariableFilterDialog::removeRow(QWidget *widget)
{
    for (qsizetype i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].widget == widget) {
            m_rows.removeAt(i);
            break;
        }
    }
    m_rowsLayout->removeWidget(widget);
    widget->deleteLater();
}

// Fills the operator list for the row's variable, keeping `selected` when the type still offers it.
void VariableFilterDialog::fillOperators(Row &row, Op selected)
{
    const QSignalBlocker block(row.op);
    row.op->clear();
    for (Op op : operatorsFor(typeOf(row.variable->currentData().toLongLong())))
        row.op->addItem(opLabel(op), int(op));
    const int i = row.op->findData(int(selected));
    row.op->setCurrentIndex(i >= 0 ? i : 0);
}

void VariableFilterDialog::updateRowState(const Row &row)
{
    const Op op = Op(row.op->currentData().toInt());
    const VariableType type = typeOf(row.variable->currentData().toLongLong());
    row.value->setVisible(opNeedsValue(op));
    row.value2->setVisible(opNeedsSecondValue(op));
    const auto check = [type](QLineEdit *edit) {
        QString normalized;
        const bool bad = !edit->text().trimmed().isEmpty()
            && !Exporters::normalizeVariableValue(type, edit->text(), &normalized);
        edit->setStyleSheet(bad ? QStringLiteral("QLineEdit { border: 1px solid #c0392b; }") : QString());
        edit->setToolTip(bad ? tr("Valore non valido per una variabile di tipo %1").arg(variableTypeLabel(type))
                             : QString());
    };
    check(row.value);
    check(row.value2);
}

QList<Condition> VariableFilterDialog::conditions() const
{
    QList<Condition> out;
    for (const Row &r : m_rows) {
        if (r.variable->currentIndex() < 0 || r.op->currentIndex() < 0)
            continue;
        Condition c;
        c.variableId = r.variable->currentData().toLongLong();
        c.op = Op(r.op->currentData().toInt());
        c.value = opNeedsValue(c.op) ? r.value->text().trimmed() : QString();
        c.value2 = opNeedsSecondValue(c.op) ? r.value2->text().trimmed() : QString();
        out << c;
    }
    return out;
}

bool VariableFilterDialog::matchAny() const
{
    return m_combine->currentData().toBool();
}

void VariableFilterDialog::setConditions(const QList<Condition> &conditions, bool any)
{
    const auto widgets = m_rows;
    for (const Row &r : widgets)
        removeRow(r.widget);
    for (const Condition &c : conditions)
        addRow(c);
    m_combine->setCurrentIndex(m_combine->findData(any));
    updateState();
}

QList<qint64> VariableFilterDialog::matchingDocuments() const
{
    return VariableFilter::matchingDocuments(*m_project, conditions(), matchAny());
}

void VariableFilterDialog::remember()
{
    save(*m_project, conditions(), matchAny());
}

void VariableFilterDialog::updateState()
{
    for (const Row &r : std::as_const(m_rows))
        updateRowState(r);
    const bool hasVariables = !m_project->variables().isEmpty();
    const bool ready = hasVariables && !m_rows.isEmpty();
    const int n = ready ? int(matchingDocuments().size()) : 0;
    // Activating zero documents would mean "all documents" in the panels, so it is not offered.
    m_activate->setEnabled(n > 0);
    m_saveSet->setEnabled(n > 0);
    if (!hasVariables)
        m_preview->setText(tr("Il progetto non ha variabili: creale da Documenti ▸ Variabili dei documenti."));
    else
        m_preview->setText(n == 1 ? tr("1 documento soddisfa le condizioni")
                                  : tr("%1 documenti soddisfano le condizioni").arg(n));
}
