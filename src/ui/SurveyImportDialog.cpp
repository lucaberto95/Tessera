#include "ui/SurveyImportDialog.h"
#include "core/Project.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

using Survey::Role;

namespace {

constexpr int PreviewRows = 5;

} // namespace

SurveyImportDialog::SurveyImportDialog(Project *project, const Survey::Table &table, const QString &fileName,
                                       QWidget *parent)
    : QDialog(parent)
    , m_table(table)
    , m_columns(Survey::proposeColumns(*project, table))
    , m_columnsTable(new QTableWidget(int(m_columns.size()), 4))
    , m_groupName(new QLineEdit(QFileInfo(fileName).completeBaseName()))
    , m_codeAnswers(new QCheckBox(tr("Codifica ogni risposta con un codice che ha il nome della domanda")))
    , m_summary(new QLabel)
{
    setWindowTitle(tr("Importa dati di sondaggio"));
    resize(860, 620);

    auto *hint = new QLabel(tr("Ogni riga è un rispondente e diventa un documento: le risposte aperte ne formano il "
                               "testo, una per paragrafo dopo la domanda; le altre colonne possono diventare "
                               "variabili del documento. Scegli il ruolo di ogni colonna."));
    hint->setWordWrap(true);

    m_columnsTable->setObjectName(QStringLiteral("surveyColumns"));
    m_columnsTable->setHorizontalHeaderLabels({tr("Colonna"), tr("Ruolo"), tr("Tipo di variabile"), tr("Esempi")});
    m_columnsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_columnsTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_columnsTable->verticalHeader()->hide();
    m_columnsTable->horizontalHeader()->setStretchLastSection(true);
    for (int c = 0; c < m_columns.size(); ++c) {
        const Survey::Column &col = m_columns[c];
        m_columnsTable->setItem(c, 0, new QTableWidgetItem(col.name));

        auto *role = new QComboBox;
        role->addItem(tr("ID rispondente"), int(Role::Id));
        role->addItem(tr("Variabile"), int(Role::Variable));
        role->addItem(tr("Risposta aperta"), int(Role::OpenAnswer));
        role->addItem(tr("Ignora"), int(Role::Ignore));
        role->setCurrentIndex(role->findData(int(col.role)));
        m_columnsTable->setCellWidget(c, 1, role);
        m_roles << role;

        auto *type = new QComboBox;
        for (VariableType t : {VariableType::Text, VariableType::Integer, VariableType::Decimal, VariableType::Boolean,
                               VariableType::Date})
            type->addItem(variableTypeLabel(t), int(t));
        type->setCurrentIndex(type->findData(int(col.type)));
        if (col.existingVariable)
            type->setToolTip(tr("Variabile già presente nel progetto: il tipo resta quello esistente."));
        m_columnsTable->setCellWidget(c, 2, type);
        m_types << type;

        m_columnsTable->setItem(c, 3, new QTableWidgetItem(col.samples.join(QStringLiteral(" | "))));

        connect(role, &QComboBox::currentIndexChanged, this, [this, c] { onRoleChanged(c); });
        connect(type, &QComboBox::currentIndexChanged, this, [this, c] {
            m_columns[c].type = VariableType(m_types[c]->currentData().toInt());
        });
    }
    m_columnsTable->resizeColumnsToContents();
    m_columnsTable->setColumnWidth(0, qMin(m_columnsTable->columnWidth(0), 260));

    // First rows as they are in the file.
    const int previewCount = int(qMin<qsizetype>(PreviewRows, qMax<qsizetype>(0, table.size() - 1)));
    auto *preview = new QTableWidget(previewCount, int(m_columns.size()));
    QStringList headers;
    for (const Survey::Column &col : std::as_const(m_columns))
        headers << col.name;
    preview->setHorizontalHeaderLabels(headers);
    for (int r = 0; r < previewCount; ++r)
        for (int c = 0; c < table[r + 1].size() && c < m_columns.size(); ++c)
            preview->setItem(r, c, new QTableWidgetItem(table[r + 1][c]));
    preview->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_codeAnswers->setChecked(true);
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);

    auto *form = new QFormLayout;
    form->addRow(tr("Nuovo gruppo di documenti"), m_groupName);
    form->addRow(QString(), m_codeAnswers);

    auto *buttons = new QDialogButtonBox;
    m_import = buttons->addButton(tr("Importa"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Annulla"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_columnsTable, 3);
    layout->addWidget(new QLabel(tr("Prime righe del file:")));
    layout->addWidget(preview, 2);
    layout->addLayout(form);
    layout->addWidget(m_summary);
    layout->addWidget(buttons);

    for (int c = 0; c < m_columns.size(); ++c)
        onRoleChanged(c);
}

void SurveyImportDialog::onRoleChanged(int column)
{
    const Role role = Role(m_roles[column]->currentData().toInt());
    m_columns[column].role = role;
    // A single respondent ID: choosing a new one releases the previous column.
    if (role == Role::Id) {
        for (int c = 0; c < m_columns.size(); ++c) {
            if (c != column && m_columns[c].role == Role::Id) {
                const QSignalBlocker block(m_roles[c]);
                m_roles[c]->setCurrentIndex(m_roles[c]->findData(int(Role::Ignore)));
                m_columns[c].role = Role::Ignore;
                m_types[c]->setEnabled(false);
            }
        }
    }
    m_types[column]->setEnabled(role == Role::Variable && !m_columns[column].existingVariable);
    updateState();
}

void SurveyImportDialog::updateState()
{
    int answers = 0, variables = 0;
    for (const Survey::Column &col : std::as_const(m_columns)) {
        answers += col.role == Role::OpenAnswer;
        variables += col.role == Role::Variable;
    }
    m_import->setEnabled(answers > 0);
    if (answers == 0)
        m_summary->setText(tr("Indica almeno una colonna come risposta aperta."));
    else
        m_summary->setText(tr("Rispondenti: %1. Colonne di risposte aperte: %2; variabili: %3. "
                              "Le risposte vuote vengono saltate.")
                               .arg(QString::number(respondentCount()), QString::number(answers),
                                    QString::number(variables)));
}

int SurveyImportDialog::respondentCount() const
{
    int count = 0;
    for (qsizetype r = 1; r < m_table.size(); ++r) {
        const QStringList &row = m_table[r];
        count += std::any_of(row.cbegin(), row.cend(), [](const QString &s) { return !s.trimmed().isEmpty(); });
    }
    return count;
}

Survey::Options SurveyImportDialog::options() const
{
    Survey::Options o;
    o.columns = m_columns;
    o.groupName = m_groupName->text().trimmed();
    o.codeAnswers = m_codeAnswers->isChecked();
    return o;
}

void SurveyImportDialog::setColumnRole(int column, Survey::Role role)
{
    m_roles[column]->setCurrentIndex(m_roles[column]->findData(int(role)));
}

void SurveyImportDialog::setColumnType(int column, VariableType type)
{
    m_types[column]->setCurrentIndex(m_types[column]->findData(int(type)));
}
