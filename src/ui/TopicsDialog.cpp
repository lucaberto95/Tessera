#include "ui/TopicsDialog.h"
#include "core/Exporters.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kIndexRole = Qt::UserRole;

QTableWidgetItem *textItem(const QString &text, Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter)
{
    auto *item = new QTableWidgetItem(text);
    item->setTextAlignment(alignment);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

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

QString excerpt(const QString &text, int start, int end)
{
    const int s = std::clamp(start, 0, int(text.size()));
    const int e = end < 0 ? int(text.size()) : std::clamp(end, s, int(text.size()));
    QString out = text.mid(s, std::min(e - s, 260)).simplified();
    if (e - s > 260)
        out += QStringLiteral("…");
    return out;
}

} // namespace

TopicsDialog::TopicsDialog(Project *project, std::function<QList<qint64>()> activeDocuments,
                           std::function<QList<Segment>()> retrievedSegments, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_activeDocuments(std::move(activeDocuments))
    , m_retrieved(std::move(retrievedSegments))
    , m_scope(new QComboBox)
    , m_unit(new QComboBox)
    , m_topics(new QSpinBox)
    , m_seed(new QSpinBox)
    , m_minLength(new QSpinBox)
    , m_useStopList(new QCheckBox(tr("Escludi le parole della lista")))
    , m_progress(new QProgressBar)
    , m_summary(new QLabel)
    , m_tabs(new QTabWidget)
    , m_topicTable(new QTableWidget)
    , m_passageTable(new QTableWidget)
    , m_unitTable(new QTableWidget)
{
    setWindowTitle(tr("Topic modeling"));
    resize(1000, 720);

    m_scope->addItem(tr("Tutti i documenti"), int(Scope::AllDocuments));
    m_scope->addItem(tr("Solo documenti attivi"), int(Scope::ActiveDocuments));
    m_scope->addItem(tr("Segmenti recuperati"), int(Scope::RetrievedSegments));
    m_unit->addItem(tr("Un'unità per documento"), 0);
    m_unit->addItem(tr("Un'unità per paragrafo"), 1);
    m_topics->setRange(0, 50);
    m_topics->setValue(5);
    m_topics->setSpecialValueText(tr("Automatico"));
    m_topics->setPrefix(tr("Temi: "));
    m_topics->setObjectName(QStringLiteral("topicCount"));
    m_seed->setRange(0, 999999);
    m_seed->setValue(1);
    m_seed->setPrefix(tr("Seme: "));
    m_seed->setToolTip(tr("Con lo stesso seme e le stesse opzioni il risultato è sempre lo stesso"));
    m_minLength->setRange(1, 30);
    m_minLength->setValue(3);
    m_minLength->setPrefix(tr("Lunghezza minima: "));
    m_minLength->setSuffix(tr(" lettere"));
    m_useStopList->setChecked(true);
    m_progress->setTextVisible(false);
    m_progress->setMaximumHeight(6);
    m_progress->hide();

    auto *warning = new QLabel(tr("Analisi esplorativa e automatica: i temi dipendono dal numero scelto e dal seme, e sono "
                                  "instabili su testi brevi o pochi documenti. Leggi le parole e i passaggi prima di darne "
                                  "un'interpretazione."));
    warning->setWordWrap(true);
    auto *runButton = new QPushButton(tr("Calcola"));
    runButton->setDefault(true);
    connect(runButton, &QPushButton::clicked, this, &TopicsDialog::run);

    auto *row1 = new QHBoxLayout;
    row1->addWidget(new QLabel(tr("Testo:")));
    row1->addWidget(m_scope);
    row1->addWidget(m_unit);
    row1->addStretch();
    auto *row2 = new QHBoxLayout;
    row2->addWidget(m_topics);
    row2->addWidget(m_seed);
    row2->addWidget(m_minLength);
    row2->addWidget(m_useStopList);
    row2->addStretch();
    row2->addWidget(runButton);

    // Topics tab: the topics and, below, the most representative passages of the selected one.
    m_topicTable->setObjectName(QStringLiteral("topicTable"));
    m_topicTable->setColumnCount(4);
    m_topicTable->setHorizontalHeaderLabels({tr("Tema"), tr("Parole caratteristiche"), tr("Quota delle parole (%)"),
                                             tr("Unità in cui prevale")});
    m_topicTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_topicTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_topicTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_topicTable->verticalHeader()->setVisible(false);
    m_topicTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    connect(m_topicTable, &QTableWidget::itemSelectionChanged, this, &TopicsDialog::fillPassages);

    m_passageTable->setObjectName(QStringLiteral("passageTable"));
    m_passageTable->setColumnCount(3);
    m_passageTable->setHorizontalHeaderLabels({tr("Documento"), tr("Quota del tema (%)"), tr("Estratto")});
    m_passageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_passageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_passageTable->verticalHeader()->setVisible(false);
    m_passageTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    connect(m_passageTable, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        const QTableWidgetItem *item = m_passageTable->item(row, 0);
        const int unit = item ? item->data(kIndexRole).toInt() : -1;
        if (unit >= 0 && unit < m_result.units.size())
            emit passageActivated(m_result.units[unit].documentId, m_result.units[unit].start, m_result.units[unit].end);
    });

    auto *codesButton = new QPushButton(tr("Crea codici dai temi…"));
    auto *exportButton = new QPushButton(tr("Esporta…"));
    exportButton->setToolTip(tr("Esporta i temi e l'assegnazione delle unità in Excel (.xlsx), CSV o HTML"));
    connect(codesButton, &QPushButton::clicked, this, &TopicsDialog::createCodesInteractive);
    connect(exportButton, &QPushButton::clicked, this, &TopicsDialog::exportTables);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(new QLabel(tr("Doppio clic su un passaggio per aprirlo nel documento")));
    buttons->addStretch();
    buttons->addWidget(codesButton);
    buttons->addWidget(exportButton);

    auto *topicPage = new QWidget;
    auto *topicLayout = new QVBoxLayout(topicPage);
    topicLayout->addWidget(m_topicTable, 2);
    topicLayout->addWidget(new QLabel(tr("Passaggi più rappresentativi del tema selezionato:")));
    topicLayout->addWidget(m_passageTable, 2);
    m_tabs->addTab(topicPage, tr("Temi"));

    // Assignment tab: every unit with its main topic.
    m_unitTable->setObjectName(QStringLiteral("unitTable"));
    m_unitTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_unitTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_unitTable->verticalHeader()->setVisible(false);
    connect(m_unitTable, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        const QTableWidgetItem *item = m_unitTable->item(row, 0);
        const int unit = item ? item->data(kIndexRole).toInt() : -1;
        if (unit >= 0 && unit < m_result.units.size())
            emit passageActivated(m_result.units[unit].documentId, m_result.units[unit].start, m_result.units[unit].end);
    });
    m_tabs->addTab(m_unitTable, tr("Assegnazione delle unità"));

    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(warning);
    layout->addLayout(row1);
    layout->addLayout(row2);
    layout->addWidget(m_progress);
    layout->addWidget(m_tabs, 1);
    layout->addWidget(m_summary);
    layout->addLayout(buttons);
    for (QPushButton *button : findChildren<QPushButton *>())
        button->setAutoDefault(false);
    runButton->setAutoDefault(true);

    connect(&m_watcher, &QFutureWatcher<Topics::Result>::finished, this, &TopicsDialog::finished);
}

TopicsDialog::~TopicsDialog()
{
    if (m_cancel)
        m_cancel->store(true);
    for (QFuture<Topics::Result> &f : m_running)
        f.waitForFinished();
}

void TopicsDialog::setScope(Scope scope)
{
    m_scope->setCurrentIndex(m_scope->findData(int(scope)));
}

void TopicsDialog::setParagraphs(bool on)
{
    m_unit->setCurrentIndex(on ? 1 : 0);
}

void TopicsDialog::setTopicCount(int count)
{
    m_topics->setValue(count);
}

void TopicsDialog::setSeed(int seed)
{
    m_seed->setValue(seed);
}

int TopicsDialog::topicRowCount() const
{
    return m_topicTable->rowCount();
}

int TopicsDialog::unitRowCount() const
{
    return m_unitTable->rowCount();
}

int TopicsDialog::passageRowCount() const
{
    return m_passageTable->rowCount();
}

void TopicsDialog::selectTopic(int index)
{
    m_topicTable->selectRow(index);
}

void TopicsDialog::run()
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
    m_texts = Words::loadTexts(*m_project, sources); // read here: the project is not thread-safe
    m_names.clear();
    for (const Document &d : m_project->documents())
        m_names.insert(d.id, d.name);
    if (m_unit->currentData().toInt() == 1)
        sources = Topics::paragraphSources(sources, m_texts);

    Topics::Options options;
    options.topics = m_topics->value();
    options.seed = quint32(m_seed->value());
    options.minLength = m_minLength->value();
    if (m_useStopList->isChecked()) {
        const QStringList list = Words::stopList(*m_project);
        options.stopWords = QSet<QString>(list.begin(), list.end());
    }

    auto cancel = std::make_shared<std::atomic_bool>(false);
    m_cancel = cancel;
    m_progress->setRange(0, 0); // busy until the first sweep is reported
    m_progress->show();
    m_busy = true;
    m_summary->setText(tr("Calcolo in corso…"));

    const QHash<qint64, QString> texts = m_texts;
    const int generation = m_generation;
    QObject *self = this;
    QFuture<Topics::Result> future = QtConcurrent::run([=]() {
        return Topics::model(sources, texts, options, [=](int done, int total) {
            if (cancel->load())
                return false;
            if (done % 10 == 0)
                QMetaObject::invokeMethod(self, [this_ = static_cast<TopicsDialog *>(self), done, total, generation] {
                    if (generation == this_->m_generation) {
                        this_->m_progress->setRange(0, total);
                        this_->m_progress->setValue(done);
                    }
                }, Qt::QueuedConnection);
            return true;
        });
    });
    m_running.removeIf([](const QFuture<Topics::Result> &f) { return f.isFinished(); });
    m_running.append(future);
    m_watcher.setFuture(future); // replaces the previous one: its result is never delivered
}

void TopicsDialog::finished()
{
    m_result = m_watcher.result();
    m_busy = false;
    m_progress->hide();
    fillTables();
}

void TopicsDialog::fillTables()
{
    const Topics::Result &r = m_result;
    m_topicTable->setSortingEnabled(false);
    m_topicTable->clearContents();
    m_topicTable->setRowCount(int(r.topics.size()));
    for (int i = 0; i < r.topics.size(); ++i) {
        const Topics::Topic &t = r.topics[i];
        QStringList words;
        for (const Topics::TopicWord &w : t.words)
            words << w.word;
        m_topicTable->setItem(i, 0, numberItem(t.index + 1));
        m_topicTable->setItem(i, 1, textItem(words.join(QStringLiteral(", "))));
        m_topicTable->setItem(i, 2, numberItem(std::round(t.share * 1000) / 10));
        m_topicTable->setItem(i, 3, numberItem(t.units));
    }
    m_topicTable->setSortingEnabled(true);
    m_topicTable->sortByColumn(0, Qt::AscendingOrder);
    m_topicTable->resizeColumnToContents(0);

    QStringList header{tr("Documento"), tr("Da"), tr("A"), tr("Tema prevalente"), tr("Quota (%)"), tr("Estratto")};
    m_unitTable->setSortingEnabled(false);
    m_unitTable->clear();
    m_unitTable->setColumnCount(int(header.size()));
    m_unitTable->setHorizontalHeaderLabels(header);
    m_unitTable->setRowCount(int(r.units.size()));
    for (int u = 0; u < r.units.size(); ++u) {
        const Words::Source &s = r.units[u];
        const QString text = m_texts.value(s.documentId);
        const int topic = r.dominant.value(u, -1);
        auto *doc = textItem(m_names.value(s.documentId));
        doc->setData(kIndexRole, u);
        m_unitTable->setItem(u, 0, doc);
        m_unitTable->setItem(u, 1, numberItem(s.start));
        m_unitTable->setItem(u, 2, numberItem(s.end < 0 ? int(text.size()) : s.end));
        m_unitTable->setItem(u, 3, topic >= 0 ? numberItem(topic + 1) : textItem(QString()));
        m_unitTable->setItem(u, 4, topic >= 0 ? numberItem(std::round(r.themeShare(u, topic) * 1000) / 10) : textItem(QString()));
        m_unitTable->setItem(u, 5, textItem(excerpt(text, s.start, s.end)));
    }
    m_unitTable->setSortingEnabled(true);
    m_unitTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_unitTable->resizeColumnToContents(0);

    if (!r.complete)
        m_summary->setText(tr("Calcolo interrotto."));
    else if (r.topics.isEmpty())
        m_summary->setText(tr("Non ci sono abbastanza parole per calcolare dei temi: servono più testo, o meno filtri."));
    else
        m_summary->setText(tr("%n temi", nullptr, int(r.topics.size())) + QStringLiteral(", ") +
                           tr("%n unità", nullptr, int(r.units.size())) + QStringLiteral(", ") +
                           tr("%n parole diverse", nullptr, r.vocabulary) + QStringLiteral(" · ") +
                           tr("coerenza %1 (più vicina a 0 è meglio)").arg(QString::number(r.coherence, 'f', 2)));
    if (!r.topics.isEmpty())
        m_topicTable->selectRow(0);
    else
        fillPassages();
}

void TopicsDialog::fillPassages()
{
    m_passageTable->setRowCount(0);
    const QModelIndexList rows = m_topicTable->selectionModel()->selectedRows(0);
    if (rows.isEmpty())
        return;
    const QTableWidgetItem *number = m_topicTable->item(rows.first().row(), 0);
    const int topic = number ? number->data(Qt::DisplayRole).toInt() - 1 : -1;
    if (topic < 0 || topic >= m_result.topics.size())
        return;
    const QList<int> units = m_result.topics[topic].topUnits;
    m_passageTable->setRowCount(int(units.size()));
    for (int i = 0; i < units.size(); ++i) {
        const int u = units[i];
        const Words::Source &s = m_result.units[u];
        auto *doc = textItem(m_names.value(s.documentId));
        doc->setData(kIndexRole, u);
        m_passageTable->setItem(i, 0, doc);
        m_passageTable->setItem(i, 1, numberItem(std::round(m_result.themeShare(u, topic) * 1000) / 10));
        m_passageTable->setItem(i, 2, textItem(excerpt(m_texts.value(s.documentId), s.start, s.end)));
    }
    m_passageTable->resizeColumnToContents(0);
}

Topics::CodeResult TopicsDialog::createCodes(const Topics::CodeOptions &options)
{
    return Topics::createCodes(*m_project, m_result, options, m_texts);
}

void TopicsDialog::createCodesInteractive()
{
    if (m_result.topics.isEmpty()) {
        Ui::showError(this, tr("Calcola prima i temi."));
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Crea codici dai temi"));
    auto *parentName = new QLineEdit(tr("Temi (topic modeling)"));
    auto *codeUnits = new QCheckBox(tr("Codifica anche le unità associate a un tema"));
    auto *threshold = new QDoubleSpinBox;
    threshold->setRange(0.1, 1.0);
    threshold->setSingleStep(0.05);
    threshold->setValue(0.5);
    threshold->setSuffix(tr(" (quota minima del tema)"));
    threshold->setEnabled(false);
    connect(codeUnits, &QCheckBox::toggled, threshold, &QWidget::setEnabled);
    auto *info = new QLabel(tr("Ogni tema diventa un codice sotto il codice indicato. La codifica automatica delle unità è "
                               "approssimativa: va rivista. Si annulla in un solo passo."));
    info->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *form = new QFormLayout;
    form->addRow(tr("Codice che raccoglie i temi:"), parentName);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(info);
    layout->addLayout(form);
    layout->addWidget(codeUnits);
    layout->addWidget(threshold);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;
    Topics::CodeOptions options;
    options.parentName = parentName->text();
    options.codeUnits = codeUnits->isChecked();
    options.threshold = threshold->value();
    const Topics::CodeResult r = createCodes(options);
    Ui::showInfo(this, tr("Crea codici dai temi"),
                 tr("%n codici dei temi pronti.", nullptr, int(r.codes.size())) +
                     (options.codeUnits ? QStringLiteral(" ") + tr("%n codifiche aggiunte: vanno riviste.", nullptr, r.segments)
                                        : QString()));
}

void TopicsDialog::exportTables()
{
    if (m_result.topics.isEmpty()) {
        Ui::showError(this, tr("Calcola prima i temi."));
        return;
    }
    const QString path = Ui::saveFileName(this, tr("Esporta"), m_project->name() + tr(" - temi") + QStringLiteral(".xlsx"),
                                          tr("Cartella di lavoro Excel (*.xlsx);;CSV per Excel (*.csv);;Pagina HTML (*.html)"));
    if (path.isEmpty())
        return;
    const QList<QStringList> topics = Topics::topicRows(m_result);
    if (Ui::isXlsxPath(path)) {
        Xlsx::Sheet assignments = Exporters::tableSheet(tr("Assegnazione"), Topics::assignmentRows(m_result, m_names, m_texts));
        assignments.textColumns = {0, int(assignments.rows.isEmpty() ? 0 : assignments.rows.first().size()) - 1};
        Ui::saveXlsx(this, path, {Exporters::tableSheet(tr("Temi"), topics), assignments});
        return;
    }
    QString error;
    const bool ok = path.endsWith(QLatin1String(".html"), Qt::CaseInsensitive)
                        ? Exporters::writeHtmlTable(path, tr("Temi"), topics, &error)
                        : Exporters::writeCsv(path, topics, &error);
    if (!ok)
        Ui::showError(this, error);
}
