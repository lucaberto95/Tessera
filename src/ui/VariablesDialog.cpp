#include "ui/VariablesDialog.h"
#include "core/Exporters.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

constexpr int FixedColumns = 2; // document, group
constexpr int DocIdRole = Qt::UserRole;
constexpr int VarIdRole = Qt::UserRole + 1;

// Validates and normalizes a value typed by the user. Returns false if it does not fit the type.
bool normalizeValue(VariableType type, const QString &input, QString *out)
{
    const QString v = input.trimmed();
    if (v.isEmpty()) {
        out->clear();
        return true;
    }
    bool ok = true;
    switch (type) {
    case VariableType::Text:
        *out = v;
        break;
    case VariableType::Integer:
        *out = QString::number(QLocale().toLongLong(v, &ok));
        if (!ok)
            *out = QString::number(v.toLongLong(&ok));
        break;
    case VariableType::Decimal: {
        double d = QLocale().toDouble(v, &ok);
        if (!ok)
            d = v.toDouble(&ok);
        *out = QString::number(d, 'g', 15);
        break;
    }
    case VariableType::Boolean: {
        const QString l = v.toLower();
        if (l == QLatin1String("1") || l == QLatin1String("sì") || l == QLatin1String("si") ||
            l == QLatin1String("yes") || l == QLatin1String("true") || l == QLatin1String("vero") || l == QLatin1String("x"))
            *out = QStringLiteral("1");
        else if (l == QLatin1String("0") || l == QLatin1String("no") || l == QLatin1String("false") ||
                 l == QLatin1String("falso"))
            *out = QStringLiteral("0");
        else
            ok = false;
        break;
    }
    case VariableType::Date: {
        QDate d = QDate::fromString(v, Qt::ISODate);
        for (const char *fmt : {"dd/MM/yyyy", "d/M/yyyy", "dd.MM.yyyy", "dd-MM-yyyy", "d/M/yy"}) {
            if (d.isValid())
                break;
            d = QDate::fromString(v, QString::fromLatin1(fmt));
        }
        ok = d.isValid();
        if (ok)
            *out = d.toString(Qt::ISODate);
        break;
    }
    }
    return ok;
}

QString displayValue(VariableType type, const QString &stored)
{
    if (type == VariableType::Boolean && !stored.isEmpty())
        return stored == QLatin1String("1") ? QObject::tr("Sì") : QObject::tr("No");
    if (type == VariableType::Date && !stored.isEmpty()) {
        const QDate d = QDate::fromString(stored, Qt::ISODate);
        return d.isValid() ? d.toString(QStringLiteral("dd/MM/yyyy")) : stored;
    }
    if (type == VariableType::Decimal && !stored.isEmpty())
        return QLocale().toString(stored.toDouble(), 'g', 15);
    return stored;
}

} // namespace

VariablesDialog::VariablesDialog(Project *project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_table(new QTableWidget)
{
    setWindowTitle(tr("Variabili dei documenti"));
    resize(900, 560);

    auto *hint = new QLabel(tr("Ogni riga è un documento, ogni colonna una variabile (es. età, genere, ruolo). "
                               "Fai doppio clic su una cella per modificarla."));
    hint->setWordWrap(true);

    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(true);
    connect(m_table, &QTableWidget::itemChanged, this, &VariablesDialog::onItemChanged);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto addButton = [&](const QString &text, void (VariablesDialog::*slot)()) {
        QPushButton *b = buttons->addButton(text, QDialogButtonBox::ActionRole);
        connect(b, &QPushButton::clicked, this, slot);
    };
    addButton(tr("Nuova variabile…"), &VariablesDialog::addVariable);
    addButton(tr("Rinomina…"), &VariablesDialog::renameVariable);
    addButton(tr("Elimina variabile"), &VariablesDialog::deleteVariable);
    addButton(tr("Importa CSV…"), &VariablesDialog::importCsv);
    addButton(tr("Esporta CSV…"), &VariablesDialog::exportCsv);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(hint);
    layout->addWidget(m_table, 1);
    layout->addWidget(buttons);
    refresh();
}

void VariablesDialog::refresh()
{
    m_refreshing = true;
    const QList<Variable> vars = m_project->variables();
    const QList<Document> docs = m_project->documents();
    const auto values = m_project->allVariableValues();
    QHash<qint64, QString> groups;
    for (const DocumentGroup &g : m_project->groups())
        groups.insert(g.id, g.name);

    m_table->clear();
    m_table->setColumnCount(FixedColumns + int(vars.size()));
    m_table->setRowCount(int(docs.size()));
    QStringList headers{tr("Documento"), tr("Gruppo")};
    for (int c = 0; c < vars.size(); ++c) {
        headers << vars[c].name;
        auto *h = new QTableWidgetItem(vars[c].name);
        h->setData(VarIdRole, vars[c].id);
        h->setToolTip(tr("Tipo: %1").arg(variableTypeLabel(vars[c].type)));
        m_table->setHorizontalHeaderItem(FixedColumns + c, h);
    }
    m_table->setHorizontalHeaderItem(0, new QTableWidgetItem(headers[0]));
    m_table->setHorizontalHeaderItem(1, new QTableWidgetItem(headers[1]));

    for (int r = 0; r < docs.size(); ++r) {
        const Document &d = docs[r];
        auto *name = new QTableWidgetItem(d.name);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        name->setData(DocIdRole, d.id);
        auto *group = new QTableWidgetItem(groups.value(d.groupId));
        group->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        m_table->setItem(r, 0, name);
        m_table->setItem(r, 1, group);
        for (int c = 0; c < vars.size(); ++c) {
            auto *cell = new QTableWidgetItem(displayValue(vars[c].type, values.value(d.id).value(vars[c].id)));
            cell->setData(DocIdRole, d.id);
            cell->setData(VarIdRole, vars[c].id);
            if (vars[c].type != VariableType::Text)
                cell->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(r, FixedColumns + c, cell);
        }
    }
    m_table->resizeColumnToContents(0);
    m_refreshing = false;
}

qint64 VariablesDialog::currentVariableId() const
{
    const int col = m_table->currentColumn();
    if (col < FixedColumns) {
        QMessageBox::information(const_cast<VariablesDialog *>(this), windowTitle(),
                                 tr("Seleziona una cella nella colonna della variabile."));
        return 0;
    }
    return m_table->horizontalHeaderItem(col)->data(VarIdRole).toLongLong();
}

void VariablesDialog::addVariable()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Nuova variabile"));
    auto *name = new QLineEdit;
    auto *type = new QComboBox;
    for (VariableType t : {VariableType::Text, VariableType::Integer, VariableType::Decimal, VariableType::Boolean,
                           VariableType::Date})
        type->addItem(variableTypeLabel(t), int(t));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *form = new QFormLayout(&dlg);
    form->addRow(tr("Nome"), name);
    form->addRow(tr("Tipo"), type);
    form->addRow(buttons);
    if (dlg.exec() != QDialog::Accepted || name->text().trimmed().isEmpty())
        return;
    if (!m_project->addVariable(name->text().trimmed(), static_cast<VariableType>(type->currentData().toInt())))
        Ui::showError(this, tr("Esiste già una variabile con questo nome."));
    refresh();
}

void VariablesDialog::renameVariable()
{
    const qint64 id = currentVariableId();
    if (!id)
        return;
    const QString name = Ui::askText(this, tr("Rinomina variabile"), tr("Nome:"),
                                     m_table->horizontalHeaderItem(m_table->currentColumn())->text());
    if (name.isEmpty())
        return;
    if (!m_project->renameVariable(id, name))
        Ui::showError(this, tr("Esiste già una variabile con questo nome."));
    refresh();
}

void VariablesDialog::deleteVariable()
{
    const qint64 id = currentVariableId();
    if (!id)
        return;
    const QString name = m_table->horizontalHeaderItem(m_table->currentColumn())->text();
    if (!Ui::confirm(this, tr("Elimina variabile"),
                     tr("Eliminare la variabile \"%1\" e tutti i suoi valori?").arg(name)))
        return;
    m_project->deleteVariable(id);
    refresh();
}

void VariablesDialog::onItemChanged(QTableWidgetItem *item)
{
    if (m_refreshing || item->column() < FixedColumns)
        return;
    const qint64 docId = item->data(DocIdRole).toLongLong();
    const qint64 varId = item->data(VarIdRole).toLongLong();
    VariableType type = VariableType::Text;
    for (const Variable &v : m_project->variables())
        if (v.id == varId)
            type = v.type;
    QString value;
    if (!normalizeValue(type, item->text(), &value)) {
        Ui::showError(this, tr("Il valore \"%1\" non è valido per una variabile di tipo %2.")
                                .arg(item->text(), variableTypeLabel(type)));
        value = m_project->variableValue(docId, varId);
    } else {
        m_project->setVariableValue(docId, varId, value);
    }
    m_refreshing = true;
    item->setText(displayValue(type, value));
    m_refreshing = false;
}

void VariablesDialog::importCsv()
{
    const QString path = Ui::openFileName(this, tr("Importa variabili"), tr("CSV (*.csv *.txt)"));
    if (path.isEmpty())
        return;
    QString report, error;
    if (!Exporters::importVariablesCsv(*m_project, path, &report, &error))
        Ui::showError(this, error);
    else
        QMessageBox::information(this, tr("Importa variabili"), report);
    refresh();
}

void VariablesDialog::exportCsv()
{
    const QString path = Ui::saveFileName(this, tr("Esporta variabili"), m_project->name() + tr(" - variabili.csv"),
                                          tr("CSV per Excel (*.csv)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!Exporters::writeCsv(path, Exporters::variablesTable(*m_project), &error))
        Ui::showError(this, error);
}
