#include "ui/SummaryTablesDialog.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
constexpr int IdRole = Qt::UserRole;

QListWidgetItem *checkItem(QListWidget *list, const QString &text, qint64 id, const QIcon &icon = {})
{
    auto *item = new QListWidgetItem(icon, text, list);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
    item->setData(IdRole, id);
    return item;
}
} // namespace

SummaryTablesDialog::SummaryTablesDialog(Project *project, const QList<qint64> &activeDocuments,
                                         const QList<qint64> &activeCodes, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_activeDocs(activeDocuments)
    , m_activeCodes(activeCodes)
    , m_tables(new QListWidget)
    , m_byDocuments(new QRadioButton(tr("Documenti scelti")))
    , m_bySet(new QRadioButton(tr("Set di documenti")))
    , m_sets(new QComboBox)
    , m_documents(new QListWidget)
    , m_codes(new QListWidget)
    , m_variables(new QListWidget)
    , m_preview(new QTableWidget)
{
    setWindowTitle(tr("Tabelle riassuntive"));
    resize(1200, 700);
    m_byDocuments->setChecked(true);
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->setWordWrap(true);
    m_preview->setSelectionMode(QAbstractItemView::SingleSelection);

    auto *newButton = new QPushButton(tr("Nuova…"));
    auto *renameButton = new QPushButton(tr("Rinomina…"));
    auto *deleteButton = new QPushButton(tr("Elimina"));
    auto *exportButton = new QPushButton(tr("Esporta…"));
    exportButton->setToolTip(tr("Esporta in Excel (.xlsx), CSV o HTML"));
    connect(newButton, &QPushButton::clicked, this, &SummaryTablesDialog::newTable);
    connect(renameButton, &QPushButton::clicked, this, &SummaryTablesDialog::renameTable);
    connect(deleteButton, &QPushButton::clicked, this, &SummaryTablesDialog::deleteTable);
    connect(exportButton, &QPushButton::clicked, this, &SummaryTablesDialog::exportTable);

    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(new QLabel(tr("Tabelle salvate")));
    leftLayout->addWidget(m_tables, 1);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(newButton);
    buttons->addWidget(renameButton);
    buttons->addWidget(deleteButton);
    leftLayout->addLayout(buttons);

    auto *rowsBox = new QGroupBox(tr("Righe"));
    auto *rowsLayout = new QVBoxLayout(rowsBox);
    rowsLayout->addWidget(m_byDocuments);
    rowsLayout->addWidget(m_documents, 1);
    auto *setRow = new QHBoxLayout;
    setRow->addWidget(m_bySet);
    setRow->addWidget(m_sets, 1);
    rowsLayout->addLayout(setRow);
    auto *codesBox = new QGroupBox(tr("Colonne: codici"));
    auto *codesLayout = new QVBoxLayout(codesBox);
    codesLayout->addWidget(m_codes);
    auto *varsBox = new QGroupBox(tr("Colonne: variabili"));
    auto *varsLayout = new QVBoxLayout(varsBox);
    varsLayout->addWidget(m_variables);

    auto *config = new QWidget;
    auto *configLayout = new QHBoxLayout(config);
    configLayout->setContentsMargins(0, 0, 0, 0);
    configLayout->addWidget(rowsBox, 1);
    configLayout->addWidget(codesBox, 1);
    configLayout->addWidget(varsBox, 1);

    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(config);
    auto *previewPane = new QWidget;
    auto *previewLayout = new QVBoxLayout(previewPane);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    auto *previewBar = new QHBoxLayout;
    previewBar->addWidget(new QLabel(tr("Anteprima (si aggiorna quando le sintesi cambiano)")));
    previewBar->addStretch();
    previewBar->addWidget(exportButton);
    previewLayout->addLayout(previewBar);
    previewLayout->addWidget(m_preview, 1);
    splitter->addWidget(previewPane);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);
    rightLayout->addWidget(splitter);

    auto *main = new QSplitter;
    main->addWidget(left);
    main->addWidget(right);
    main->setStretchFactor(0, 1);
    main->setStretchFactor(1, 4);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(main);

    connect(m_tables, &QListWidget::currentRowChanged, this, [this] { loadConfig(); });
    for (QListWidget *l : {m_documents, m_codes, m_variables})
        connect(l, &QListWidget::itemChanged, this, [this] { configChanged(); });
    connect(m_byDocuments, &QRadioButton::toggled, this, [this] { configChanged(); });
    connect(m_sets, &QComboBox::currentIndexChanged, this, [this] { configChanged(); });
    connect(m_project, &Project::summariesChanged, this, [this] {
        // Own saves of the configuration come here too: only the preview needs to follow.
        if (!m_loading)
            refreshPreview();
    });
    connect(m_project, &Project::codesChanged, this, [this] { loadConfig(); });
    connect(m_project, &Project::documentsChanged, this, [this] { loadConfig(); });
    connect(m_project, &Project::variablesChanged, this, [this] { loadConfig(); });
    connect(m_project, &Project::documentSetsChanged, this, [this] { loadConfig(); });

    reloadList();
}

qint64 SummaryTablesDialog::currentTableId() const
{
    const QListWidgetItem *item = m_tables->currentItem();
    return item ? item->data(IdRole).toLongLong() : 0;
}

void SummaryTablesDialog::selectTable(qint64 id)
{
    for (int i = 0; i < m_tables->count(); ++i)
        if (m_tables->item(i)->data(IdRole).toLongLong() == id)
            m_tables->setCurrentRow(i);
}

void SummaryTablesDialog::reloadList()
{
    const qint64 keep = currentTableId();
    m_loading = true;
    m_tables->clear();
    for (const SummaryTable &t : m_project->summaryTables()) {
        auto *item = new QListWidgetItem(t.name, m_tables);
        item->setData(IdRole, t.id);
    }
    m_loading = false;
    if (m_tables->count()) {
        m_tables->setCurrentRow(0);
        selectTable(keep);
    }
    loadConfig();
}

void SummaryTablesDialog::setChecks(QListWidget *list, const QList<qint64> &ids)
{
    for (int i = 0; i < list->count(); ++i)
        list->item(i)->setCheckState(ids.contains(list->item(i)->data(IdRole).toLongLong()) ? Qt::Checked
                                                                                            : Qt::Unchecked);
}

QList<qint64> SummaryTablesDialog::checkedIds(const QListWidget *list)
{
    QList<qint64> out;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            out << list->item(i)->data(IdRole).toLongLong();
    return out;
}

// Fills the choice lists from the project and ticks those of the selected table.
void SummaryTablesDialog::loadConfig()
{
    if (m_loading)
        return;
    m_loading = true;
    const SummaryTable table = m_project->summaryTable(currentTableId());
    const Summaries::TableConfig cfg = Summaries::configFromJson(table.configJson);

    m_documents->clear();
    for (const Document &d : m_project->documents())
        checkItem(m_documents, d.name, d.id);
    m_codes->clear();
    for (const Code &c : m_project->codesInTreeOrder())
        checkItem(m_codes, m_project->codePath(c.id), c.id, Ui::colorIcon(c.color));
    m_variables->clear();
    for (const Variable &v : m_project->variables())
        checkItem(m_variables, v.name, v.id);
    m_sets->clear();
    for (const DocumentSet &s : m_project->documentSets())
        m_sets->addItem(s.name, s.id);
    m_sets->setCurrentIndex(m_sets->findData(qint64(cfg.setId)));
    setChecks(m_documents, cfg.documentIds);
    setChecks(m_codes, cfg.codeIds);
    setChecks(m_variables, cfg.variableIds);
    const bool useSet = cfg.setId != 0 && m_sets->currentIndex() >= 0;
    m_bySet->setChecked(useSet);
    m_byDocuments->setChecked(!useSet);

    const bool has = table.id != 0;
    for (QWidget *w : std::initializer_list<QWidget *>{m_byDocuments, m_bySet, m_sets, m_documents, m_codes,
                                                       m_variables})
        w->setEnabled(has);
    m_loading = false;
    refreshPreview();
}

Summaries::TableConfig SummaryTablesDialog::config() const
{
    Summaries::TableConfig cfg;
    cfg.documentIds = checkedIds(m_documents);
    cfg.codeIds = checkedIds(m_codes);
    cfg.variableIds = checkedIds(m_variables);
    if (m_bySet->isChecked() && m_sets->currentIndex() >= 0)
        cfg.setId = m_sets->currentData().toLongLong();
    return cfg;
}

void SummaryTablesDialog::setConfig(const Summaries::TableConfig &cfg)
{
    m_loading = true;
    setChecks(m_documents, cfg.documentIds);
    setChecks(m_codes, cfg.codeIds);
    setChecks(m_variables, cfg.variableIds);
    const int setIndex = m_sets->findData(qint64(cfg.setId));
    m_sets->setCurrentIndex(setIndex);
    m_bySet->setChecked(cfg.setId != 0 && setIndex >= 0);
    m_byDocuments->setChecked(!m_bySet->isChecked());
    m_loading = false;
    configChanged();
}

// Every change of the choices is saved at once, like everything else in the project.
void SummaryTablesDialog::configChanged()
{
    if (m_loading)
        return;
    const SummaryTable table = m_project->summaryTable(currentTableId());
    if (!table.id)
        return;
    m_loading = true; // the save emits summariesChanged: the preview is refreshed below
    m_project->updateSummaryTable(table.id, table.name, Summaries::configToJson(config()));
    m_loading = false;
    refreshPreview();
}

void SummaryTablesDialog::refreshPreview()
{
    m_table.clear();
    if (currentTableId())
        m_table = Summaries::buildTable(*m_project, config());
    m_preview->clear();
    m_preview->setRowCount(0);
    m_preview->setColumnCount(0);
    if (m_table.isEmpty())
        return;
    const int cols = int(m_table.first().size());
    m_preview->setColumnCount(cols);
    m_preview->setHorizontalHeaderLabels(m_table.first());
    m_preview->setRowCount(int(m_table.size()) - 1);
    for (int r = 1; r < m_table.size(); ++r)
        for (int c = 0; c < cols; ++c) {
            auto *item = new QTableWidgetItem(m_table[r].value(c));
            item->setToolTip(Ui::plainToolTip(m_table[r].value(c)));
            m_preview->setItem(r - 1, c, item);
        }
    m_preview->resizeColumnsToContents();
    for (int c = 0; c < cols; ++c)
        m_preview->setColumnWidth(c, qMin(m_preview->columnWidth(c), 320));
    m_preview->resizeRowsToContents();
}

qint64 SummaryTablesDialog::createTable(const QString &name)
{
    Summaries::TableConfig cfg;
    cfg.documentIds = m_activeDocs;
    cfg.codeIds = m_activeCodes;
    if (cfg.documentIds.isEmpty())
        for (const Document &d : m_project->documents())
            cfg.documentIds << d.id;
    if (cfg.codeIds.isEmpty())
        for (const Code &c : m_project->codesInTreeOrder())
            cfg.codeIds << c.id;
    const qint64 id = m_project->addSummaryTable(name, Summaries::configToJson(cfg));
    if (id) {
        reloadList();
        selectTable(id);
    }
    return id;
}

void SummaryTablesDialog::newTable()
{
    const QString name = Ui::askText(this, tr("Nuova tabella riassuntiva"), tr("Nome della tabella:"),
                                     tr("Tabella %1").arg(m_tables->count() + 1));
    if (name.isEmpty())
        return;
    createTable(name);
}

void SummaryTablesDialog::renameTable()
{
    const SummaryTable table = m_project->summaryTable(currentTableId());
    if (!table.id)
        return;
    const QString name = Ui::askText(this, tr("Rinomina tabella"), tr("Nome della tabella:"), table.name);
    if (name.isEmpty() || name == table.name)
        return;
    m_project->updateSummaryTable(table.id, name, table.configJson);
    reloadList();
}

void SummaryTablesDialog::deleteTable()
{
    const SummaryTable table = m_project->summaryTable(currentTableId());
    if (!table.id)
        return;
    if (!Ui::confirm(this, tr("Elimina tabella"),
                     tr("Eliminare la tabella «%1»? Le sintesi restano nel progetto.").arg(table.name)))
        return;
    m_project->deleteSummaryTable(table.id);
    reloadList();
}

void SummaryTablesDialog::exportTable()
{
    const SummaryTable table = m_project->summaryTable(currentTableId());
    if (!table.id || m_table.isEmpty())
        return;
    const QString path = Ui::saveFileName(this, tr("Esporta tabella riassuntiva"),
                                          m_project->name() + QStringLiteral(" - ") + table.name +
                                              QStringLiteral(".xlsx"),
                                          tr("Cartella di lavoro Excel (*.xlsx);;CSV per Excel (*.csv);;"
                                             "Pagina HTML (*.html)"));
    if (path.isEmpty())
        return;
    if (Ui::isXlsxPath(path)) {
        Ui::saveXlsx(this, path, {Summaries::buildSheet(*m_project, config(), table.name)});
        return;
    }
    QString error;
    const bool ok = path.endsWith(QLatin1String(".html"), Qt::CaseInsensitive)
                        ? Exporters::writeHtmlTable(path, table.name, m_table, &error)
                        : Exporters::writeCsv(path, m_table, &error);
    if (!ok)
        Ui::showError(this, error);
}
