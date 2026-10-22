#include "ui/DictionaryDialog.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace {

QTableWidgetItem *cell(const QString &text, bool right = false)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (right)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

QString percentText(double x)
{
    return QString::number(x, 'f', 2) + QStringLiteral(" %");
}

} // namespace

DictionaryDialog::DictionaryDialog(Project *project, std::function<QList<qint64>()> activeDocuments, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_activeDocuments(std::move(activeDocuments))
    , m_dictionary(new QComboBox)
    , m_tabs(new QTabWidget)
    , m_categories(new QTreeWidget)
    , m_entries(new QListWidget)
    , m_entryEdit(new QLineEdit)
    , m_entriesLabel(new QLabel)
    , m_scope(new QComboBox)
    , m_caseSensitive(new QCheckBox(tr("Maiuscole/minuscole")))
    , m_values(new QComboBox)
    , m_withVariables(new QCheckBox(tr("Aggiungi le variabili")))
    , m_summary(new QLabel)
    , m_table(new QTableWidget)
{
    setWindowTitle(tr("Dizionari e analisi del contenuto"));
    resize(920, 620);

    // Dictionary bar
    auto *newDict = new QPushButton(tr("Nuovo…"));
    auto *renameDict = new QPushButton(tr("Rinomina…"));
    auto *deleteDict = new QPushButton(tr("Elimina"));
    connect(newDict, &QPushButton::clicked, this, &DictionaryDialog::newDictionary);
    connect(renameDict, &QPushButton::clicked, this, &DictionaryDialog::renameDictionary);
    connect(deleteDict, &QPushButton::clicked, this, &DictionaryDialog::deleteDictionary);
    auto *dictBar = new QHBoxLayout;
    dictBar->addWidget(new QLabel(tr("Dizionario:")));
    dictBar->addWidget(m_dictionary, 1);
    dictBar->addWidget(newDict);
    dictBar->addWidget(renameDict);
    dictBar->addWidget(deleteDict);

    // "Categorie e voci" tab
    m_categories->setObjectName(QStringLiteral("dictionaryCategories"));
    m_categories->setColumnCount(2);
    m_categories->setHeaderLabels({tr("Categoria"), tr("Voci")});
    m_categories->header()->setStretchLastSection(false);
    m_categories->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_categories->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(m_categories, &QTreeWidget::currentItemChanged, this, &DictionaryDialog::reloadEntries);
    connect(m_categories, &QTreeWidget::itemDoubleClicked, this, &DictionaryDialog::renameCategory);

    auto *newCat = new QPushButton(tr("Nuova categoria…"));
    auto *newSub = new QPushButton(tr("Nuova sottocategoria…"));
    auto *renameCat = new QPushButton(tr("Rinomina…"));
    auto *deleteCat = new QPushButton(tr("Elimina"));
    connect(newCat, &QPushButton::clicked, this, [this] { newCategory(false); });
    connect(newSub, &QPushButton::clicked, this, [this] { newCategory(true); });
    connect(renameCat, &QPushButton::clicked, this, &DictionaryDialog::renameCategory);
    connect(deleteCat, &QPushButton::clicked, this, &DictionaryDialog::deleteCategory);
    auto *catButtons = new QHBoxLayout;
    catButtons->addWidget(newCat);
    catButtons->addWidget(newSub);
    catButtons->addWidget(renameCat);
    catButtons->addWidget(deleteCat);
    catButtons->addStretch();
    auto *catColumn = new QVBoxLayout;
    catColumn->addWidget(m_categories, 1);
    catColumn->addLayout(catButtons);

    m_entries->setObjectName(QStringLiteral("dictionaryEntries"));
    m_entries->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_entries, &QListWidget::itemDoubleClicked, this, &DictionaryDialog::editEntry);
    m_entryEdit->setPlaceholderText(tr("Parola o espressione (* come jolly: lavor*)"));
    m_entryEdit->setToolTip(tr("Più voci in una volta: separale con virgole"));
    auto *addEntry = new QPushButton(tr("Aggiungi"));
    connect(addEntry, &QPushButton::clicked, this, &DictionaryDialog::addEntries);
    connect(m_entryEdit, &QLineEdit::returnPressed, this, &DictionaryDialog::addEntries);
    auto *editEntryButton = new QPushButton(tr("Modifica…"));
    auto *removeEntry = new QPushButton(tr("Rimuovi"));
    connect(editEntryButton, &QPushButton::clicked, this, &DictionaryDialog::editEntry);
    connect(removeEntry, &QPushButton::clicked, this, &DictionaryDialog::removeEntries);
    auto *addRow = new QHBoxLayout;
    addRow->addWidget(m_entryEdit, 1);
    addRow->addWidget(addEntry);
    auto *entryButtons = new QHBoxLayout;
    entryButtons->addWidget(editEntryButton);
    entryButtons->addWidget(removeEntry);
    entryButtons->addStretch();
    auto *entryColumn = new QVBoxLayout;
    m_entriesLabel->setTextFormat(Qt::PlainText);
    entryColumn->addWidget(m_entriesLabel);
    entryColumn->addWidget(m_entries, 1);
    entryColumn->addLayout(addRow);
    entryColumn->addLayout(entryButtons);

    auto *editorColumns = new QHBoxLayout;
    editorColumns->addLayout(catColumn, 1);
    editorColumns->addLayout(entryColumn, 1);

    auto *importButton = new QPushButton(tr("Importa…"));
    importButton->setToolTip(tr("Importa categorie e voci da Excel (.xlsx) o CSV (colonne Categoria e Voce), "
                                "oppure un elenco di parole da un file di testo (una per riga)"));
    auto *exportButton = new QPushButton(tr("Esporta…"));
    exportButton->setToolTip(tr("Esporta il dizionario in Excel (.xlsx) o CSV"));
    connect(importButton, &QPushButton::clicked, this, &DictionaryDialog::importDictionary);
    connect(exportButton, &QPushButton::clicked, this, &DictionaryDialog::exportDictionary);
    auto *fileRow = new QHBoxLayout;
    auto *hint = new QLabel(tr("Una categoria conta anche le voci delle sue sottocategorie. "
                               "Le espressioni di più parole ignorano punteggiatura e a capo tra le parole."));
    hint->setWordWrap(true);
    fileRow->addWidget(hint, 1);
    fileRow->addWidget(importButton);
    fileRow->addWidget(exportButton);

    auto *editorPage = new QWidget;
    auto *editorLayout = new QVBoxLayout(editorPage);
    editorLayout->addLayout(editorColumns, 1);
    editorLayout->addLayout(fileRow);
    m_tabs->addTab(editorPage, tr("Categorie e voci"));

    // "Analisi" tab
    m_scope->addItem(tr("Tutti i documenti"));
    m_scope->addItem(tr("Solo documenti attivi"));
    m_values->addItem(tr("Conteggi"), int(Values::Counts));
    m_values->addItem(tr("% delle parole del documento"), int(Values::PercentOfWords));
    connect(m_scope, &QComboBox::currentIndexChanged, this, &DictionaryDialog::runAnalysis);
    connect(m_caseSensitive, &QCheckBox::toggled, this, &DictionaryDialog::runAnalysis);
    connect(m_values, &QComboBox::currentIndexChanged, this, &DictionaryDialog::fillAnalysisTable);
    connect(m_withVariables, &QCheckBox::toggled, this, &DictionaryDialog::fillAnalysisTable);
    auto *analyzeButton = new QPushButton(tr("Analizza"));
    connect(analyzeButton, &QPushButton::clicked, this, &DictionaryDialog::runAnalysis);
    auto *optionsRow = new QHBoxLayout;
    optionsRow->addWidget(m_scope);
    optionsRow->addWidget(m_values);
    optionsRow->addWidget(m_caseSensitive);
    optionsRow->addWidget(m_withVariables);
    optionsRow->addStretch();
    optionsRow->addWidget(analyzeButton);

    m_table->setObjectName(QStringLiteral("dictionaryAnalysisTable"));
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectColumns);
    m_table->verticalHeader()->setVisible(false);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        if (row >= 0 && row < m_analysis.documents.size())
            emit documentActivated(m_analysis.documents[row]);
    });
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);

    auto *exportAnalysisButton = new QPushButton(tr("Esporta…"));
    exportAnalysisButton->setToolTip(tr("Esporta la tabella in Excel (.xlsx), CSV o HTML"));
    auto *variablesButton = new QPushButton(tr("Salva come variabili"));
    variablesButton->setToolTip(tr("Crea una variabile intera per ogni categoria selezionata (o per tutte) con le "
                                   "occorrenze in ogni documento"));
    auto *autocodeButton = new QPushButton(tr("Codifica automatica…"));
    connect(exportAnalysisButton, &QPushButton::clicked, this, &DictionaryDialog::exportAnalysis);
    connect(variablesButton, &QPushButton::clicked, this, &DictionaryDialog::saveAsVariables);
    connect(autocodeButton, &QPushButton::clicked, this, &DictionaryDialog::showAutocode);
    auto *analysisButtons = new QHBoxLayout;
    analysisButtons->addWidget(m_summary, 1);
    analysisButtons->addWidget(exportAnalysisButton);
    analysisButtons->addWidget(variablesButton);
    analysisButtons->addWidget(autocodeButton);

    auto *analysisPage = new QWidget;
    auto *analysisLayout = new QVBoxLayout(analysisPage);
    analysisLayout->addLayout(optionsRow);
    analysisLayout->addWidget(m_table, 1);
    analysisLayout->addLayout(analysisButtons);
    m_tabs->addTab(analysisPage, tr("Analisi"));
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1 && m_analysisStale)
            runAnalysis();
    });

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(dictBar);
    layout->addWidget(m_tabs, 1);
    // Return in the entry field only adds the entry: no button may act as the dialog's default.
    for (QPushButton *button : findChildren<QPushButton *>())
        button->setAutoDefault(false);

    connect(m_dictionary, &QComboBox::currentIndexChanged, this, [this] {
        reloadCategories();
        m_analysisStale = true;
        if (m_tabs->currentIndex() == 1)
            runAnalysis();
    });
    // Changes made here, by undo or elsewhere: the analysis is recomputed when it is next shown.
    auto stale = [this] {
        m_analysisStale = true;
        if (m_tabs->currentIndex() == 1 && isVisible())
            runAnalysis();
    };
    connect(m_project, &Project::dictionariesChanged, this, [this, stale] {
        reloadDictionaries();
        stale();
    });
    connect(m_project, &Project::documentsChanged, this, stale);
    connect(m_project, &Project::variablesChanged, this, [this] {
        if (m_withVariables->isChecked())
            fillAnalysisTable();
    });

    reloadDictionaries();
}

// ---------------------------------------------------------------- selection

qint64 DictionaryDialog::currentDictionaryId() const
{
    return m_dictionary->currentData().toLongLong();
}

void DictionaryDialog::selectDictionary(qint64 id)
{
    const int i = m_dictionary->findData(id);
    if (i >= 0)
        m_dictionary->setCurrentIndex(i);
}

qint64 DictionaryDialog::currentCategoryId() const
{
    const QTreeWidgetItem *item = m_categories->currentItem();
    return item ? item->data(0, Qt::UserRole).toLongLong() : 0;
}

void DictionaryDialog::selectCategory(qint64 id)
{
    for (QTreeWidgetItemIterator it(m_categories); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toLongLong() == id) {
            m_categories->setCurrentItem(*it);
            return;
        }
    }
}

void DictionaryDialog::reloadDictionaries()
{
    const qint64 current = currentDictionaryId();
    {
        const QSignalBlocker block(m_dictionary);
        m_dictionary->clear();
        for (const Dictionary &d : m_project->dictionaries())
            m_dictionary->addItem(d.name, d.id);
        const int i = m_dictionary->findData(current);
        m_dictionary->setCurrentIndex(i >= 0 ? i : (m_dictionary->count() > 0 ? 0 : -1));
    }
    reloadCategories();
}

void DictionaryDialog::reloadCategories()
{
    const qint64 current = currentCategoryId();
    const qint64 dictionary = currentDictionaryId();
    QHash<qint64, int> entryCounts;
    for (const DictEntry &e : m_project->dictEntriesOfDictionary(dictionary))
        ++entryCounts[e.categoryId];
    {
        const QSignalBlocker block(m_categories);
        m_categories->clear();
        QHash<qint64, QTreeWidgetItem *> items;
        for (const DictCategory &c : m_project->dictCategories(dictionary)) {
            QTreeWidgetItem *parent = items.value(c.parentId);
            auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_categories);
            item->setText(0, c.name);
            item->setText(1, QString::number(entryCounts.value(c.id)));
            item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
            item->setData(0, Qt::UserRole, c.id);
            items.insert(c.id, item);
        }
        m_categories->expandAll();
        if (QTreeWidgetItem *item = items.value(current))
            m_categories->setCurrentItem(item);
        else if (m_categories->topLevelItemCount() > 0)
            m_categories->setCurrentItem(m_categories->topLevelItem(0));
    }
    reloadEntries();
}

void DictionaryDialog::reloadEntries()
{
    QSet<qint64> selected;
    for (const QListWidgetItem *item : m_entries->selectedItems())
        selected.insert(item->data(Qt::UserRole).toLongLong());
    m_entries->clear();
    const qint64 category = currentCategoryId();
    m_entryEdit->setEnabled(category > 0);
    if (!category) {
        m_entriesLabel->setText(currentDictionaryId() ? tr("Crea o seleziona una categoria.")
                                                      : tr("Crea prima un dizionario."));
        return;
    }
    m_entriesLabel->setText(tr("Voci di «%1»").arg(m_project->dictCategoryPath(category)));
    for (const DictEntry &e : m_project->dictEntries(category)) {
        auto *item = new QListWidgetItem(e.pattern, m_entries);
        item->setData(Qt::UserRole, e.id);
        item->setSelected(selected.contains(e.id));
    }
}

// ---------------------------------------------------------------- editing

void DictionaryDialog::newDictionary()
{
    const QString name = Ui::askText(this, tr("Nuovo dizionario"), tr("Nome del dizionario:"));
    if (name.isEmpty())
        return;
    if (const qint64 id = m_project->addDictionary(name)) {
        selectDictionary(id);
        m_tabs->setCurrentIndex(0);
    }
}

void DictionaryDialog::renameDictionary()
{
    const qint64 id = currentDictionaryId();
    if (!id)
        return;
    const QString name = Ui::askText(this, tr("Rinomina dizionario"), tr("Nome del dizionario:"),
                                     m_dictionary->currentText());
    if (!name.isEmpty())
        m_project->renameDictionary(id, name);
}

void DictionaryDialog::deleteDictionary()
{
    const qint64 id = currentDictionaryId();
    if (!id)
        return;
    if (Ui::confirm(this, tr("Elimina dizionario"),
                    tr("Eliminare il dizionario «%1» con tutte le sue categorie e voci?").arg(m_dictionary->currentText())))
        m_project->deleteDictionary(id);
}

void DictionaryDialog::newCategory(bool sub)
{
    qint64 dictionary = currentDictionaryId();
    const qint64 parent = sub ? currentCategoryId() : 0;
    if (sub && !parent) {
        Ui::showError(this, tr("Seleziona prima la categoria superiore."));
        return;
    }
    const QString name = Ui::askText(this, sub ? tr("Nuova sottocategoria") : tr("Nuova categoria"),
                                     tr("Nome della categoria:"));
    if (name.isEmpty())
        return;
    if (!dictionary) {
        // A first category creates the dictionary too.
        dictionary = m_project->addDictionary(tr("Dizionario"));
        selectDictionary(dictionary);
    }
    if (const qint64 id = m_project->addDictCategory(dictionary, parent, name)) {
        selectCategory(id);
        m_entryEdit->setFocus();
    }
}

void DictionaryDialog::renameCategory()
{
    const qint64 id = currentCategoryId();
    if (!id)
        return;
    const QString name = Ui::askText(this, tr("Rinomina categoria"), tr("Nome della categoria:"),
                                     m_project->dictCategory(id).name);
    if (!name.isEmpty())
        m_project->renameDictCategory(id, name);
}

void DictionaryDialog::deleteCategory()
{
    const qint64 id = currentCategoryId();
    if (!id)
        return;
    if (Ui::confirm(this, tr("Elimina categoria"),
                    tr("Eliminare la categoria «%1» con le sue sottocategorie e voci?")
                        .arg(m_project->dictCategoryPath(id))))
        m_project->deleteDictCategory(id);
}

void DictionaryDialog::addEntries()
{
    const qint64 category = currentCategoryId();
    if (!category) {
        Ui::showError(this, tr("Seleziona prima una categoria."));
        return;
    }
    QStringList patterns;
    for (const QString &p : m_entryEdit->text().split(QLatin1Char(','), Qt::SkipEmptyParts))
        if (!p.simplified().isEmpty())
            patterns << p.simplified();
    if (patterns.isEmpty())
        return;
    m_project->setUndoLabel(tr("Nuova voce del dizionario"));
    m_project->beginBatch();
    for (const QString &p : patterns)
        m_project->addDictEntry(category, p);
    m_project->commitBatch();
    m_entryEdit->clear();
}

void DictionaryDialog::editEntry()
{
    QListWidgetItem *item = m_entries->currentItem();
    if (!item)
        return;
    const QString pattern = Ui::askText(this, tr("Modifica voce"), tr("Parola o espressione:"), item->text());
    if (!pattern.isEmpty() && pattern != item->text())
        m_project->setDictEntryPattern(item->data(Qt::UserRole).toLongLong(), pattern);
}

void DictionaryDialog::removeEntries()
{
    const QList<QListWidgetItem *> items = m_entries->selectedItems();
    if (items.isEmpty())
        return;
    QList<qint64> ids;
    for (const QListWidgetItem *item : items)
        ids << item->data(Qt::UserRole).toLongLong();
    m_project->setUndoLabel(tr("Elimina voci del dizionario"));
    m_project->beginBatch();
    for (qint64 id : ids)
        m_project->deleteDictEntry(id);
    m_project->commitBatch();
}

void DictionaryDialog::importDictionary()
{
    const QString path = Ui::openFileName(this, tr("Importa dizionario"),
                                          tr("Dizionari (*.xlsx *.csv *.txt);;Cartella di lavoro Excel (*.xlsx);;"
                                             "CSV (*.csv);;Elenco di parole (*.txt)"));
    if (path.isEmpty())
        return;
    qint64 dictionary = currentDictionaryId();
    if (!dictionary) {
        dictionary = m_project->addDictionary(QFileInfo(path).completeBaseName());
        selectDictionary(dictionary);
    }
    QString error;
    const int added = Dictionaries::importFile(*m_project, dictionary, currentCategoryId(), path, &error);
    if (added < 0)
        Ui::showError(this, error);
    else
        Ui::showInfo(this, tr("Importa dizionario"), tr("%n voci importate.", nullptr, added));
}

void DictionaryDialog::exportDictionary()
{
    const qint64 dictionary = currentDictionaryId();
    if (!dictionary)
        return;
    const QString path = Ui::saveFileName(this, tr("Esporta dizionario"), m_dictionary->currentText() + QStringLiteral(".xlsx"),
                                          tr("Cartella di lavoro Excel (*.xlsx);;CSV per Excel (*.csv)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!Dictionaries::exportFile(*m_project, dictionary, path, &error))
        Ui::showError(this, error);
}

// ---------------------------------------------------------------- analysis

void DictionaryDialog::setActiveDocumentsOnly(bool on)
{
    m_scope->setCurrentIndex(on ? 1 : 0);
}

void DictionaryDialog::setValues(Values values)
{
    m_values->setCurrentIndex(m_values->findData(int(values)));
}

void DictionaryDialog::setWithVariables(bool on)
{
    m_withVariables->setChecked(on);
}

void DictionaryDialog::runAnalysis()
{
    QList<qint64> documents;
    if (m_scope->currentIndex() == 1) {
        documents = m_activeDocuments ? m_activeDocuments() : QList<qint64>();
        // No active document means every document, as in the rest of the program.
    }
    m_analysis = Dictionaries::analyze(*m_project, currentDictionaryId(), documents, m_caseSensitive->isChecked());
    m_analysisStale = false;
    fillAnalysisTable();
}

void DictionaryDialog::fillAnalysisTable()
{
    const Dictionaries::Analysis &a = m_analysis;
    const bool percent = Values(m_values->currentData().toInt()) == Values::PercentOfWords;
    QHash<qint64, QString> names;
    for (const Document &d : m_project->documents())
        names.insert(d.id, d.name);
    const QList<Variable> variables = m_withVariables->isChecked() ? m_project->variables() : QList<Variable>();
    const auto values = m_withVariables->isChecked() ? m_project->allVariableValues()
                                                     : QHash<qint64, QHash<qint64, QString>>();

    QStringList header{tr("Documento"), tr("Parole")};
    for (const DictCategory &c : a.categories)
        header << m_project->dictCategoryPath(c.id);
    for (const Variable &v : variables)
        header << v.name;
    const int firstCategory = 2;
    const int firstVariable = firstCategory + int(a.categories.size());

    m_table->clear();
    m_table->setColumnCount(int(header.size()));
    m_table->setHorizontalHeaderLabels(header);
    m_table->setRowCount(a.categories.isEmpty() ? 0 : int(a.documents.size()) + 1);
    m_export = {header};
    if (a.categories.isEmpty()) {
        m_summary->setText(currentDictionaryId() ? tr("Il dizionario non ha categorie.")
                                                 : tr("Crea un dizionario nella scheda «Categorie e voci»."));
        return;
    }

    auto value = [&](qint64 category, qint64 doc) {
        return percent ? percentText(a.percent(category, doc)) : QString::number(a.count(category, doc));
    };
    auto exportValue = [&](qint64 category, qint64 doc) {
        return percent ? QString::number(a.percent(category, doc), 'f', 2) : QString::number(a.count(category, doc));
    };
    for (int r = 0; r < a.documents.size(); ++r) {
        const qint64 doc = a.documents[r];
        QStringList line{names.value(doc), QString::number(a.words.value(doc))};
        m_table->setItem(r, 0, cell(names.value(doc)));
        m_table->setItem(r, 1, cell(line[1], true));
        for (int c = 0; c < a.categories.size(); ++c) {
            const qint64 category = a.categories[c].id;
            m_table->setItem(r, firstCategory + c, cell(value(category, doc), true));
            line << exportValue(category, doc);
        }
        for (int v = 0; v < variables.size(); ++v) {
            const QString text = values.value(doc).value(variables[v].id);
            m_table->setItem(r, firstVariable + v, cell(text));
            line << text;
        }
        m_export.append(line);
    }

    // Totals: occurrences in all the documents, or their share of all the words.
    const int last = int(a.documents.size());
    const int words = a.totalWords();
    QStringList totals{tr("Totale"), QString::number(words)};
    m_table->setItem(last, 0, cell(tr("Totale")));
    m_table->setItem(last, 1, cell(totals[1], true));
    for (int c = 0; c < a.categories.size(); ++c) {
        const int n = a.total(a.categories[c].id);
        const double share = words > 0 ? 100.0 * n / words : 0.0;
        m_table->setItem(last, firstCategory + c, cell(percent ? percentText(share) : QString::number(n), true));
        totals << (percent ? QString::number(share, 'f', 2) : QString::number(n));
    }
    for (int v = 0; v < variables.size(); ++v) {
        m_table->setItem(last, firstVariable + v, cell(QString()));
        totals << QString();
    }
    m_export.append(totals);
    QFont bold = m_table->font();
    bold.setBold(true);
    for (int c = 0; c < m_table->columnCount(); ++c)
        if (QTableWidgetItem *item = m_table->item(last, c))
            item->setFont(bold);
    m_table->resizeColumnsToContents();

    int hits = 0;
    for (const DictCategory &c : a.categories)
        if (c.parentId == 0)
            hits += a.total(c.id);
    m_summary->setText(tr("%n documenti analizzati", nullptr, int(a.documents.size())) + QStringLiteral(", ") +
                       tr("%n parole", nullptr, words) + QStringLiteral(", ") +
                       tr("%n occorrenze delle categorie principali", nullptr, hits) + QStringLiteral(" · ") +
                       tr("doppio clic su una riga per aprire il documento"));
}

void DictionaryDialog::exportAnalysis()
{
    if (m_analysisStale)
        runAnalysis();
    if (m_export.size() <= 1)
        return;
    const QString suggested = m_project->name() + QStringLiteral(" - ") + m_dictionary->currentText() + QStringLiteral(".xlsx");
    const QString path = Ui::saveFileName(this, tr("Esporta analisi"), suggested,
                                          tr("Cartella di lavoro Excel (*.xlsx);;CSV per Excel (*.csv);;"
                                             "Pagina HTML (*.html)"));
    if (path.isEmpty())
        return;
    if (Ui::isXlsxPath(path)) {
        Ui::saveXlsx(this, path, {Exporters::tableSheet(m_dictionary->currentText(), m_export)});
        return;
    }
    QString error;
    const bool ok = path.endsWith(QLatin1String(".html"), Qt::CaseInsensitive)
                        ? Exporters::writeHtmlTable(path, windowTitle(), m_export, &error)
                        : Exporters::writeCsv(path, m_export, &error);
    if (!ok)
        Ui::showError(this, error);
}

void DictionaryDialog::saveAsVariables()
{
    if (m_analysisStale)
        runAnalysis();
    if (m_analysis.categories.isEmpty())
        return;
    // The categories of the selected columns, or every category.
    QList<qint64> categories;
    QSet<int> columns;
    for (const QModelIndex &index : m_table->selectionModel()->selectedIndexes())
        columns.insert(index.column());
    for (int c = 0; c < m_analysis.categories.size(); ++c)
        if (columns.contains(c + 2))
            categories << m_analysis.categories[c].id;
    if (categories.isEmpty()) {
        for (const DictCategory &c : m_analysis.categories)
            categories << c.id;
    }
    if (!Ui::confirm(this, tr("Salva come variabili"),
                     tr("Creare %n variabili con le occorrenze per documento?", nullptr, int(categories.size()))))
        return;
    const int n = int(Dictionaries::countsToVariables(*m_project, m_analysis, categories).size());
    Ui::showInfo(this, tr("Salva come variabili"), tr("Create %n variabili.", nullptr, n));
}

int DictionaryDialog::autocode(const QList<qint64> &categories, const QHash<qint64, qint64> &codes,
                               Dictionaries::Extent extent)
{
    runAnalysis(); // the text may have changed since the table was computed
    return Dictionaries::autocode(*m_project, m_analysis, categories, codes, extent);
}

void DictionaryDialog::showAutocode()
{
    if (m_analysisStale)
        runAnalysis();
    if (m_analysis.categories.isEmpty())
        return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Codifica automatica con il dizionario"));
    dlg.resize(640, 460);
    auto *table = new QTableWidget(int(m_analysis.categories.size()), 3);
    table->setHorizontalHeaderLabels({tr("Categoria"), tr("Occorrenze"), tr("Codice")});
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    const QList<Code> codes = m_project->codesInTreeOrder();
    QSet<qint64> parents;
    for (const DictCategory &c : m_analysis.categories)
        parents.insert(c.parentId);
    QList<QComboBox *> combos;
    for (int r = 0; r < m_analysis.categories.size(); ++r) {
        const DictCategory &c = m_analysis.categories[r];
        auto *name = cell(QString(m_analysis.depth(c.id) * 4, QLatin1Char(' ')) + c.name);
        name->setFlags(name->flags() | Qt::ItemIsUserCheckable);
        // Leaves by default: a parent category would code its subcategories' occurrences again.
        const int total = m_analysis.total(c.id);
        name->setCheckState(!parents.contains(c.id) && total > 0 ? Qt::Checked : Qt::Unchecked);
        table->setItem(r, 0, name);
        table->setItem(r, 1, cell(QString::number(total), true));
        auto *combo = new QComboBox;
        combo->addItem(tr("Codice «%1» (creato se manca)").arg(c.name), 0);
        for (const Code &code : codes)
            combo->addItem(Ui::colorIcon(code.color), m_project->codePath(code.id), code.id);
        table->setCellWidget(r, 2, combo);
        combos << combo;
    }
    table->resizeColumnToContents(0);

    auto *extent = new QComboBox;
    extent->addItem(tr("Solo la parola o l'espressione trovata"), int(Dictionaries::Extent::Word));
    extent->addItem(tr("La frase che la contiene"), int(Dictionaries::Extent::Sentence));
    extent->addItem(tr("Il paragrafo che la contiene"), int(Dictionaries::Extent::Paragraph));
    auto *extentRow = new QHBoxLayout;
    extentRow->addWidget(new QLabel(tr("Estensione:")));
    extentRow->addWidget(extent);
    extentRow->addStretch();

    auto *info = new QLabel(tr("I nuovi codici vengono creati sotto il codice «%1», con la stessa struttura delle "
                               "categorie. Tutta la codifica è un solo passo di annulla.")
                                .arg(m_dictionary->currentText()));
    info->setTextFormat(Qt::PlainText);
    info->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Codifica"));
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(table, 1);
    layout->addLayout(extentRow);
    layout->addWidget(info);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;

    QList<qint64> categories;
    QHash<qint64, qint64> chosen;
    for (int r = 0; r < m_analysis.categories.size(); ++r) {
        const qint64 id = m_analysis.categories[r].id;
        if (const qint64 code = combos[r]->currentData().toLongLong())
            chosen.insert(id, code);
        if (table->item(r, 0)->checkState() == Qt::Checked)
            categories << id;
    }
    if (categories.isEmpty())
        return;
    const int created = autocode(categories, chosen, Dictionaries::Extent(extent->currentData().toInt()));
    Ui::showInfo(this, tr("Codifica automatica"), tr("%n nuovi segmenti codificati.", nullptr, created));
}
