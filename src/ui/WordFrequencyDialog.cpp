#include "ui/WordFrequencyDialog.h"
#include "core/Exporters.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSvgGenerator>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>

namespace {

constexpr int kMaxTableRows = 20000;
constexpr int kIndexRole = Qt::UserRole;

QTableWidgetItem *numberItem(double value)
{
    auto *item = new QTableWidgetItem;
    item->setData(Qt::DisplayRole, value);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QTableWidgetItem *numberItem(int value)
{
    auto *item = new QTableWidgetItem;
    item->setData(Qt::DisplayRole, value);
    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

QTableWidgetItem *textItem(const QString &text, Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter)
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(alignment);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

// Light to dark blue with the frequency (0 = rarest, 1 = most frequent).
QColor cloudColor(double t)
{
    const QColor low(0x7a, 0xa6, 0xd6);
    const QColor high(0x12, 0x2c, 0x5c);
    return QColor::fromRgbF(low.redF() + t * (high.redF() - low.redF()), low.greenF() + t * (high.greenF() - low.greenF()),
                            low.blueF() + t * (high.blueF() - low.blueF()));
}

} // namespace

WordFrequencyDialog::WordFrequencyDialog(Project *project, std::function<QList<qint64>()> activeDocuments,
                                         std::function<QList<Segment>()> retrievedSegments, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_activeDocuments(std::move(activeDocuments))
    , m_retrieved(std::move(retrievedSegments))
    , m_tabs(new QTabWidget)
    , m_scope(new QComboBox)
    , m_compare(new QComboBox)
    , m_minLength(new QSpinBox)
    , m_caseSensitive(new QCheckBox(tr("Maiuscole/minuscole")))
    , m_excludeNumbers(new QCheckBox(tr("Escludi i numeri")))
    , m_useStopList(new QCheckBox(tr("Escludi le parole della lista")))
    , m_progress(new QProgressBar)
    , m_summary(new QLabel)
    , m_filter(new QLineEdit)
    , m_table(new QTableWidget)
    , m_topWords(new QSpinBox)
    , m_scene(new QGraphicsScene(this))
    , m_cloudView(new QGraphicsView)
    , m_keywordEdit(new QLineEdit)
    , m_width(new QSpinBox)
    , m_keywordSummary(new QLabel)
    , m_keywordTable(new QTableWidget)
{
    setWindowTitle(tr("Frequenze di parole, nuvola e parole in contesto"));
    resize(1000, 700);

    m_scope->addItem(tr("Tutti i documenti"), int(Scope::AllDocuments));
    m_scope->addItem(tr("Solo documenti attivi"), int(Scope::ActiveDocuments));
    m_scope->addItem(tr("Segmenti recuperati"), int(Scope::RetrievedSegments));
    m_compare->addItem(tr("Nessun confronto"), CompareNone);
    m_compare->addItem(tr("Confronta per gruppo di documenti"), CompareGroups);
    m_compare->addItem(tr("Confronta per set di documenti"), CompareSets);
    for (const Variable &v : m_project->variables())
        m_compare->addItem(tr("Confronta per variabile: %1").arg(v.name), v.id);
    m_minLength->setRange(1, 50);
    m_minLength->setValue(3);
    m_minLength->setSuffix(tr(" lettere"));
    m_minLength->setPrefix(tr("Lunghezza minima: "));
    m_useStopList->setChecked(true);
    m_progress->setTextVisible(false);
    m_progress->setMaximumHeight(6);
    m_progress->hide();

    auto *stopListButton = new QPushButton(tr("Lista…"));
    stopListButton->setToolTip(tr("Modifica la lista delle parole da escludere (salvata nel progetto)"));
    connect(stopListButton, &QPushButton::clicked, this, &WordFrequencyDialog::editStopList);
    auto *refresh = new QPushButton(tr("Aggiorna"));
    connect(refresh, &QPushButton::clicked, this, &WordFrequencyDialog::run);

    auto *scopeRow = new QHBoxLayout;
    scopeRow->addWidget(new QLabel(tr("Testo:")));
    scopeRow->addWidget(m_scope);
    scopeRow->addWidget(m_compare, 1);
    auto *optionRow = new QHBoxLayout;
    optionRow->addWidget(m_minLength);
    optionRow->addWidget(m_caseSensitive);
    optionRow->addWidget(m_excludeNumbers);
    optionRow->addWidget(m_useStopList);
    optionRow->addWidget(stopListButton);
    optionRow->addStretch();
    optionRow->addWidget(refresh);

    // Options that change the result recompute it after a short pause.
    auto *rerun = new QTimer(this);
    rerun->setSingleShot(true);
    rerun->setInterval(250);
    connect(rerun, &QTimer::timeout, this, &WordFrequencyDialog::run);
    auto schedule = [rerun] { rerun->start(); };
    connect(m_scope, &QComboBox::currentIndexChanged, this, schedule);
    connect(m_compare, &QComboBox::currentIndexChanged, this, schedule);
    connect(m_minLength, &QSpinBox::valueChanged, this, schedule);
    connect(m_caseSensitive, &QCheckBox::toggled, this, schedule);
    connect(m_excludeNumbers, &QCheckBox::toggled, this, schedule);
    connect(m_useStopList, &QCheckBox::toggled, this, schedule);

    // Frequencies tab
    m_filter->setPlaceholderText(tr("Filtra le parole (contiene…)"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setObjectName(QStringLiteral("wordFilter"));
    connect(m_filter, &QLineEdit::textChanged, this, &WordFrequencyDialog::applyFilter);
    m_table->setObjectName(QStringLiteral("wordFrequencyTable"));
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->verticalHeader()->setVisible(false);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &WordFrequencyDialog::keywordSelected);

    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);
    auto *contextButton = new QPushButton(tr("Parole in contesto"));
    auto *searchButton = new QPushButton(tr("Cerca e codifica…"));
    auto *dictionaryButton = new QPushButton(tr("Aggiungi al dizionario…"));
    auto *excludeButton = new QPushButton(tr("Escludi dalla lista"));
    auto *exportButton = new QPushButton(tr("Esporta…"));
    exportButton->setToolTip(tr("Esporta la tabella in Excel (.xlsx), CSV o HTML"));
    connect(contextButton, &QPushButton::clicked, this, &WordFrequencyDialog::keywordSelected);
    connect(searchButton, &QPushButton::clicked, this, &WordFrequencyDialog::searchSelected);
    connect(dictionaryButton, &QPushButton::clicked, this, &WordFrequencyDialog::addSelectedToDictionaryInteractive);
    connect(excludeButton, &QPushButton::clicked, this, &WordFrequencyDialog::excludeSelectedWords);
    connect(exportButton, &QPushButton::clicked, this, [this] { exportRows(tr("Frequenze"), m_exportTable); });
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(contextButton);
    buttons->addWidget(searchButton);
    buttons->addWidget(dictionaryButton);
    buttons->addWidget(excludeButton);
    buttons->addStretch();
    buttons->addWidget(exportButton);

    auto *freqPage = new QWidget;
    auto *freqLayout = new QVBoxLayout(freqPage);
    freqLayout->addWidget(m_filter);
    freqLayout->addWidget(m_table, 1);
    freqLayout->addWidget(m_summary);
    freqLayout->addLayout(buttons);
    m_tabs->addTab(freqPage, tr("Frequenze"));

    // Cloud tab
    m_topWords->setRange(5, 300);
    m_topWords->setValue(60);
    m_topWords->setPrefix(tr("Prime "));
    m_topWords->setSuffix(tr(" parole"));
    connect(m_topWords, &QSpinBox::valueChanged, this, [this] { rebuildCloud(); });
    auto *png = new QPushButton(tr("Esporta PNG…"));
    auto *svg = new QPushButton(tr("Esporta SVG…"));
    connect(png, &QPushButton::clicked, this, [this] { exportCloud(false); });
    connect(svg, &QPushButton::clicked, this, [this] { exportCloud(true); });
    m_cloudView->setScene(m_scene);
    m_cloudView->setRenderHint(QPainter::Antialiasing);
    m_scene->setBackgroundBrush(Qt::white);
    auto *cloudBar = new QHBoxLayout;
    cloudBar->addWidget(m_topWords);
    cloudBar->addWidget(new QLabel(tr("Dimensione e colore dipendono dalla frequenza")));
    cloudBar->addStretch();
    cloudBar->addWidget(png);
    cloudBar->addWidget(svg);
    auto *cloudPage = new QWidget;
    auto *cloudLayout = new QVBoxLayout(cloudPage);
    cloudLayout->addLayout(cloudBar);
    cloudLayout->addWidget(m_cloudView, 1);
    m_tabs->addTab(cloudPage, tr("Nuvola di parole"));

    // Keyword in context tab
    m_keywordEdit->setPlaceholderText(tr("Parola (o più parole di seguito)"));
    m_width->setRange(10, 300);
    m_width->setValue(60);
    m_width->setPrefix(tr("Contesto: "));
    m_width->setSuffix(tr(" caratteri"));
    auto *find = new QPushButton(tr("Cerca"));
    auto *exportKwic = new QPushButton(tr("Esporta…"));
    connect(find, &QPushButton::clicked, this, &WordFrequencyDialog::runKeyword);
    connect(m_keywordEdit, &QLineEdit::returnPressed, this, &WordFrequencyDialog::runKeyword);
    connect(exportKwic, &QPushButton::clicked, this, [this] {
        QList<QStringList> rows{{tr("Documento"), tr("Contesto sinistro"), tr("Parola"), tr("Contesto destro")}};
        for (int r = 0; r < m_keywordTable->rowCount(); ++r) {
            QStringList line;
            for (int c = 0; c < 4; ++c)
                line << m_keywordTable->item(r, c)->text();
            rows << line;
        }
        exportRows(tr("Parole in contesto"), rows);
    });
    m_keywordTable->setObjectName(QStringLiteral("keywordTable"));
    m_keywordTable->setColumnCount(4);
    m_keywordTable->setHorizontalHeaderLabels({tr("Documento"), tr("Contesto sinistro"), tr("Parola"), tr("Contesto destro")});
    m_keywordTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_keywordTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_keywordTable->verticalHeader()->setVisible(false);
    m_keywordTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_keywordTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_keywordTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_keywordTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_keywordTable->setColumnWidth(0, 150);
    connect(m_keywordTable, &QTableWidget::cellDoubleClicked, this, &WordFrequencyDialog::activateOccurrence);
    m_keywordSummary->setTextFormat(Qt::PlainText);
    auto *kwicBar = new QHBoxLayout;
    kwicBar->addWidget(m_keywordEdit, 1);
    kwicBar->addWidget(m_width);
    kwicBar->addWidget(find);
    kwicBar->addWidget(exportKwic);
    auto *kwicPage = new QWidget;
    auto *kwicLayout = new QVBoxLayout(kwicPage);
    kwicLayout->addLayout(kwicBar);
    kwicLayout->addWidget(m_keywordTable, 1);
    kwicLayout->addWidget(m_keywordSummary);
    m_tabs->addTab(kwicPage, tr("Parole in contesto"));
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int) { fitCloud(); });

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(scopeRow);
    layout->addLayout(optionRow);
    layout->addWidget(m_progress);
    layout->addWidget(m_tabs, 1);
    // Return in a text field must not press a button.
    for (QPushButton *button : findChildren<QPushButton *>())
        button->setAutoDefault(false);

    connect(&m_watcher, &QFutureWatcher<Words::Result>::finished, this, &WordFrequencyDialog::finished);
}

WordFrequencyDialog::~WordFrequencyDialog()
{
    if (m_cancel)
        m_cancel->store(true);
    for (QFuture<Words::Result> &f : m_running)
        f.waitForFinished();
}

void WordFrequencyDialog::setScope(Scope scope)
{
    const QSignalBlocker block(m_scope);
    m_scope->setCurrentIndex(m_scope->findData(int(scope)));
}

void WordFrequencyDialog::setComparison(qint64 comparison)
{
    const QSignalBlocker block(m_compare);
    m_compare->setCurrentIndex(std::max(0, int(m_compare->findData(comparison))));
}

void WordFrequencyDialog::setMinLength(int length)
{
    const QSignalBlocker block(m_minLength);
    m_minLength->setValue(length);
}

void WordFrequencyDialog::setCaseSensitive(bool on)
{
    const QSignalBlocker block(m_caseSensitive);
    m_caseSensitive->setChecked(on);
}

void WordFrequencyDialog::setUseStopList(bool on)
{
    const QSignalBlocker block(m_useStopList);
    m_useStopList->setChecked(on);
}

void WordFrequencyDialog::setExcludeNumbers(bool on)
{
    const QSignalBlocker block(m_excludeNumbers);
    m_excludeNumbers->setChecked(on);
}

void WordFrequencyDialog::setTopWords(int count)
{
    m_topWords->setValue(count);
}

bool WordFrequencyDialog::isBusy() const
{
    return m_busy;
}

// ---------------------------------------------------------------- computation

void WordFrequencyDialog::run()
{
    if (m_cancel)
        m_cancel->store(true);
    ++m_generation;

    const Scope scope = Scope(m_scope->currentData().toInt());
    QList<Words::Source> sources;
    if (scope == Scope::RetrievedSegments) {
        sources = Words::segmentSources(m_retrieved ? m_retrieved() : QList<Segment>());
    } else {
        const QList<qint64> active = scope == Scope::ActiveDocuments && m_activeDocuments ? m_activeDocuments()
                                                                                           : QList<qint64>();
        QList<qint64> ids;
        for (const Document &d : m_project->documents())
            if (active.isEmpty() || active.contains(d.id)) // no active document = every document
                ids << d.id;
        sources = Words::documentSources(ids);
    }
    QList<qint64> documentIds;
    QSet<qint64> seen;
    for (const Words::Source &s : sources)
        if (!seen.contains(s.documentId)) {
            seen.insert(s.documentId);
            documentIds << s.documentId;
        }

    QList<Stats::Group> columns;
    const qint64 compare = m_compare->currentData().toLongLong();
    if (compare == CompareGroups)
        columns = Stats::groupsByDocumentGroups(*m_project, documentIds);
    else if (compare == CompareSets)
        columns = Stats::groupsByDocumentSets(*m_project, documentIds);
    else if (compare > 0)
        columns = Stats::groupsByVariable(*m_project, compare, documentIds);

    Words::Options options;
    options.minLength = m_minLength->value();
    options.caseSensitive = m_caseSensitive->isChecked();
    options.excludeNumbers = m_excludeNumbers->isChecked();
    if (m_useStopList->isChecked()) {
        const QStringList list = Words::stopList(*m_project);
        options.stopWords = QSet<QString>(list.begin(), list.end());
    }

    m_sources = sources;
    m_texts = Words::loadTexts(*m_project, sources); // read here: the project is not thread-safe
    m_runCase = options.caseSensitive;

    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_cancel = cancel;
    const int generation = m_generation;
    QObject *self = this;
    m_progress->setRange(0, int(sources.size()));
    m_progress->setValue(0);
    m_progress->show();
    m_busy = true;

    const QList<Words::Source> jobSources = m_sources;
    const QHash<qint64, QString> jobTexts = m_texts;
    QFuture<Words::Result> future = QtConcurrent::run([=]() {
        return Words::frequencies(jobSources, jobTexts, options, columns, [=](int done, int) {
            if (cancel->load())
                return false;
            if (done % 4 == 0)
                QMetaObject::invokeMethod(self, [this_ = static_cast<WordFrequencyDialog *>(self), done, generation] {
                    if (generation == this_->m_generation)
                        this_->m_progress->setValue(done);
                }, Qt::QueuedConnection);
            return true;
        });
    });
    m_running.removeIf([](const QFuture<Words::Result> &f) { return f.isFinished(); });
    m_running.append(future);
    m_watcher.setFuture(future); // replaces the previous one: its result is never delivered
}

void WordFrequencyDialog::finished()
{
    m_result = m_watcher.result();
    m_resultCaseSensitive = m_runCase;
    m_busy = false;
    m_progress->hide();
    fillTable();
    rebuildCloud();
    if (!m_pendingKeyword.isEmpty()) {
        m_pendingKeyword.clear();
        runKeyword();
    } else if (!m_keywordEdit->text().trimmed().isEmpty()) {
        runKeyword();
    }
}

void WordFrequencyDialog::fillTable()
{
    const Words::Result &r = m_result;
    QStringList header{tr("Parola"), tr("Frequenza"), tr("%"), tr("Documenti")};
    header += r.columnLabels;

    m_table->setSortingEnabled(false);
    m_table->clear();
    m_table->setColumnCount(int(header.size()));
    m_table->setHorizontalHeaderLabels(header);
    const int rows = int(std::min<qsizetype>(r.entries.size(), kMaxTableRows));
    m_table->setRowCount(rows);
    for (int i = 0; i < rows; ++i) {
        const Words::Entry &e = r.entries[i];
        m_table->setItem(i, 0, textItem(e.word));
        m_table->setItem(i, 1, numberItem(e.count));
        m_table->setItem(i, 2, numberItem(std::round(r.percent(e) * 100) / 100));
        m_table->setItem(i, 3, numberItem(e.documents));
        for (int c = 0; c < r.columnLabels.size(); ++c)
            m_table->setItem(i, 4 + c, numberItem(e.columns.value(c)));
    }
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(1, Qt::DescendingOrder);
    m_table->resizeColumnsToContents();

    m_exportTable = {header};
    for (const Words::Entry &e : r.entries) {
        QStringList line{e.word, QString::number(e.count), QString::number(r.percent(e), 'f', 2),
                         QString::number(e.documents)};
        for (int c = 0; c < r.columnLabels.size(); ++c)
            line << QString::number(e.columns.value(c));
        m_exportTable << line;
    }

    QString text = tr("%n parole nel testo", nullptr, r.totalWords) + QStringLiteral(", ") +
                   tr("%n conteggiate dopo i filtri", nullptr, r.countedWords) + QStringLiteral(", ") +
                   tr("%n parole diverse", nullptr, int(r.entries.size())) + QStringLiteral(", ") +
                   tr("in %n documenti", nullptr, r.documents);
    if (r.entries.size() > kMaxTableRows)
        text += QStringLiteral(" · ") + tr("la tabella mostra le prime %1 (l'esportazione le contiene tutte)")
                                            .arg(kMaxTableRows);
    m_summary->setText(text);
    applyFilter();
}

void WordFrequencyDialog::applyFilter()
{
    const QString needle = m_filter->text().trimmed();
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const QTableWidgetItem *item = m_table->item(r, 0);
        m_table->setRowHidden(r, !needle.isEmpty() && item && !item->text().contains(needle, Qt::CaseInsensitive));
    }
}

int WordFrequencyDialog::visibleRows() const
{
    int n = 0;
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (!m_table->isRowHidden(r))
            ++n;
    return n;
}

// ---------------------------------------------------------------- selection actions

QStringList WordFrequencyDialog::selectedWords() const
{
    QStringList out;
    const QModelIndexList rows = m_table->selectionModel()->selectedRows(0);
    QList<int> numbers;
    for (const QModelIndex &i : rows)
        numbers << i.row();
    std::sort(numbers.begin(), numbers.end());
    for (int r : numbers)
        if (const QTableWidgetItem *item = m_table->item(r, 0))
            out << item->text();
    return out;
}

void WordFrequencyDialog::selectWords(const QStringList &words)
{
    m_table->clearSelection();
    for (int r = 0; r < m_table->rowCount(); ++r)
        if (const QTableWidgetItem *item = m_table->item(r, 0); item && words.contains(item->text()))
            m_table->selectionModel()->select(m_table->model()->index(r, 0),
                                              QItemSelectionModel::Select | QItemSelectionModel::Rows);
}

void WordFrequencyDialog::excludeSelectedWords()
{
    const QStringList words = selectedWords();
    if (words.isEmpty()) {
        Ui::showError(this, tr("Seleziona prima una o più parole nella tabella."));
        return;
    }
    Words::setStopList(*m_project, Words::stopList(*m_project) + words);
    setUseStopList(true);
    run();
}

int WordFrequencyDialog::addSelectedToDictionary(qint64 categoryId)
{
    return Words::addToDictionary(*m_project, categoryId, selectedWords());
}

void WordFrequencyDialog::addSelectedToDictionaryInteractive()
{
    const QStringList words = selectedWords();
    if (words.isEmpty()) {
        Ui::showError(this, tr("Seleziona prima una o più parole nella tabella."));
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Aggiungi al dizionario"));
    auto *category = new QComboBox;
    for (const Dictionary &d : m_project->dictionaries())
        for (const DictCategory &c : m_project->dictCategories(d.id))
            category->addItem(d.name + QStringLiteral(": ") + m_project->dictCategoryPath(c.id), c.id);
    if (category->count() == 0) {
        Ui::showError(this, tr("Non c'è nessuna categoria: crea prima un dizionario con almeno una categoria "
                               "(Analisi ▸ Dizionari e analisi del contenuto)."));
        return;
    }
    auto *info = new QLabel(tr("%n parole selezionate diventano voci della categoria.", nullptr, int(words.size())));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(info);
    layout->addWidget(category);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const int added = addSelectedToDictionary(category->currentData().toLongLong());
    Ui::showInfo(this, tr("Aggiungi al dizionario"), tr("%n voci aggiunte.", nullptr, added));
}

void WordFrequencyDialog::searchSelected()
{
    const QStringList words = selectedWords();
    if (words.isEmpty()) {
        Ui::showError(this, tr("Seleziona prima una parola nella tabella."));
        return;
    }
    emit searchRequested(words.first());
}

void WordFrequencyDialog::keywordSelected()
{
    const QStringList words = selectedWords();
    if (words.isEmpty()) {
        Ui::showError(this, tr("Seleziona prima una parola nella tabella."));
        return;
    }
    showKeyword(words.first());
}

void WordFrequencyDialog::editStopList()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Lista delle parole da escludere"));
    dlg.resize(420, 520);
    auto *edit = new QPlainTextEdit(Words::stopList(*m_project).join(QLatin1Char('\n')));
    auto *info = new QLabel(tr("Una parola per riga. La lista è salvata nel progetto."));
    info->setWordWrap(true);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(info);
    layout->addWidget(edit, 1);
    auto *loadRow = new QHBoxLayout;
    loadRow->addWidget(new QLabel(tr("Sostituisci con la lista predefinita:")));
    for (const QString &language : Words::stopWordLanguages()) {
        auto *button = new QPushButton(Words::stopWordLanguageName(language));
        button->setAutoDefault(false);
        connect(button, &QPushButton::clicked, &dlg,
                [edit, language] { edit->setPlainText(Words::builtInStopWords(language).join(QLatin1Char('\n'))); });
        loadRow->addWidget(button);
    }
    loadRow->addStretch();
    layout->addLayout(loadRow);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;
    Words::setStopList(*m_project, edit->toPlainText().split(QLatin1Char('\n')));
    run();
}

// ---------------------------------------------------------------- cloud

void WordFrequencyDialog::rebuildCloud()
{
    m_scene->clear();
    m_cloudWords = 0;
    const int n = int(std::min<qsizetype>(m_result.entries.size(), m_topWords->value()));
    if (n == 0) {
        m_scene->setSceneRect(0, 0, 10, 10);
        return;
    }
    const int maxCount = m_result.entries.first().count;
    const int minCount = m_result.entries[n - 1].count;
    QList<QGraphicsSimpleTextItem *> items;
    QList<QSizeF> sizes;
    for (int i = 0; i < n; ++i) {
        const Words::Entry &e = m_result.entries[i];
        const double t = maxCount > minCount ? double(e.count - minCount) / (maxCount - minCount) : 0.5;
        QFont font;
        font.setBold(true);
        font.setPixelSize(int(11 + 45 * std::sqrt(t)));
        auto *item = m_scene->addSimpleText(e.word, font);
        item->setBrush(cloudColor(t));
        item->setToolTip(Ui::plainToolTip(tr("%1: %2").arg(e.word, QString::number(e.count))));
        items << item;
        sizes << item->boundingRect().size();
    }
    const QList<QPointF> centres = Words::cloudLayout(sizes, 3);
    for (int i = 0; i < n; ++i)
        items[i]->setPos(centres[i] - QPointF(sizes[i].width() / 2, sizes[i].height() / 2));
    m_cloudWords = n;
    m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-16, -16, 16, 16));
    fitCloud();
}

void WordFrequencyDialog::fitCloud()
{
    if (m_scene->items().isEmpty())
        return;
    m_cloudView->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}

void WordFrequencyDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    fitCloud();
}

void WordFrequencyDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (m_generation == 0)
        run();
    fitCloud();
}

void WordFrequencyDialog::exportCloud(bool svg)
{
    if (m_cloudWords == 0)
        return;
    const QString path = Ui::saveFileName(this, tr("Esporta la nuvola"),
                                          m_project->name() + (svg ? tr(" - nuvola.svg") : tr(" - nuvola.png")),
                                          svg ? tr("Immagine SVG (*.svg)") : tr("Immagine PNG (*.png)"));
    if (path.isEmpty())
        return;
    const QRectF rect = m_scene->sceneRect();
    if (svg) {
        QSvgGenerator generator;
        generator.setFileName(path);
        generator.setSize(rect.size().toSize());
        generator.setViewBox(QRectF(QPointF(), rect.size()));
        generator.setTitle(windowTitle());
        QPainter painter;
        if (!painter.begin(&generator)) {
            Ui::showError(this, tr("Impossibile salvare l'immagine."));
            return;
        }
        painter.fillRect(QRectF(QPointF(), rect.size()), Qt::white);
        m_scene->render(&painter, QRectF(QPointF(), rect.size()), rect);
        painter.end();
        return;
    }
    const qreal scale = std::min(2.0, 4000.0 / std::max(rect.width(), rect.height()));
    QImage image((rect.size() * scale).toSize(), QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    m_scene->render(&painter, QRectF(QPointF(), QSizeF(image.size())), rect);
    painter.end();
    if (!image.save(path))
        Ui::showError(this, tr("Impossibile salvare l'immagine."));
}

// ---------------------------------------------------------------- keyword in context

void WordFrequencyDialog::showKeyword(const QString &word)
{
    m_keywordEdit->setText(word);
    m_tabs->setCurrentIndex(2);
    if (m_busy)
        m_pendingKeyword = word;
    else
        runKeyword();
}

void WordFrequencyDialog::runKeyword()
{
    const QString word = m_keywordEdit->text().trimmed();
    m_occurrences = word.isEmpty() ? QList<Words::Occurrence>()
                                   : Words::keywordInContext(m_sources, m_texts, word, m_resultCaseSensitive,
                                                             m_width->value());
    QHash<qint64, QString> names;
    for (const Document &d : m_project->documents())
        names.insert(d.id, d.name);

    m_keywordTable->setSortingEnabled(false);
    m_keywordTable->setRowCount(int(m_occurrences.size()));
    QSet<qint64> docs;
    for (int r = 0; r < m_occurrences.size(); ++r) {
        const Words::Occurrence &o = m_occurrences[r];
        docs.insert(o.documentId);
        auto *doc = textItem(names.value(o.documentId));
        doc->setData(kIndexRole, r);
        m_keywordTable->setItem(r, 0, doc);
        m_keywordTable->setItem(r, 1, textItem(o.left, Qt::AlignRight | Qt::AlignVCenter));
        auto *key = textItem(o.keyword, Qt::AlignCenter);
        QFont bold = key->font();
        bold.setBold(true);
        key->setFont(bold);
        m_keywordTable->setItem(r, 2, key);
        m_keywordTable->setItem(r, 3, textItem(o.right));
    }
    m_keywordTable->setSortingEnabled(true);
    m_keywordSummary->setText(word.isEmpty() ? QString()
                                             : tr("%n occorrenze", nullptr, int(m_occurrences.size())) +
                                                   QStringLiteral(" ") + tr("in %n documenti", nullptr, int(docs.size())) +
                                                   QStringLiteral(" · ") + tr("doppio clic per aprire nel documento"));
}

void WordFrequencyDialog::activateOccurrence(int row)
{
    const QTableWidgetItem *item = m_keywordTable->item(row, 0);
    if (!item)
        return;
    const int index = item->data(kIndexRole).toInt();
    if (index >= 0 && index < m_occurrences.size())
        emit occurrenceActivated(m_occurrences[index].documentId, m_occurrences[index].start, m_occurrences[index].end);
}

// ---------------------------------------------------------------- export

void WordFrequencyDialog::exportRows(const QString &name, const QList<QStringList> &rows)
{
    if (rows.size() <= 1)
        return;
    const QString path = Ui::saveFileName(this, tr("Esporta"), m_project->name() + QStringLiteral(" - ") + name + QStringLiteral(".xlsx"),
                                          tr("Cartella di lavoro Excel (*.xlsx);;CSV per Excel (*.csv);;Pagina HTML (*.html)"));
    if (path.isEmpty())
        return;
    if (Ui::isXlsxPath(path)) {
        Xlsx::Sheet sheet = Exporters::tableSheet(name, rows);
        if (name == tr("Parole in contesto"))
            sheet.textColumns = {0, 1, 2, 3};
        Ui::saveXlsx(this, path, {sheet});
        return;
    }
    QString error;
    const bool ok = path.endsWith(QLatin1String(".html"), Qt::CaseInsensitive)
                        ? Exporters::writeHtmlTable(path, name, rows, &error)
                        : Exporters::writeCsv(path, rows, &error);
    if (!ok)
        Ui::showError(this, error);
}
