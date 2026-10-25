#include "ui/SentimentDialog.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {

constexpr int kMaxRows = 20000;
constexpr int kIndexRole = Qt::UserRole;

QColor polarityColor(Sentiment::Polarity p)
{
    switch (p) {
    case Sentiment::Polarity::Positive:
        return QColor(0x2e, 0x9e, 0x5b);
    case Sentiment::Polarity::Negative:
        return QColor(0xcc, 0x3b, 0x3b);
    default:
        return QColor(0x8a, 0x8a, 0x8a);
    }
}

// Stacked bar with the share of negative, neutral and positive items.
class DistributionBar : public QWidget {
public:
    DistributionBar() { setMinimumHeight(22); setMaximumHeight(22); }
    void setCounts(int negative, int neutral, int positive)
    {
        m_counts[0] = negative;
        m_counts[1] = neutral;
        m_counts[2] = positive;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const int total = m_counts[0] + m_counts[1] + m_counts[2];
        if (total == 0) {
            p.setPen(palette().mid().color());
            p.drawRect(rect().adjusted(0, 0, -1, -1));
            return;
        }
        const Sentiment::Polarity order[3] = {Sentiment::Polarity::Negative, Sentiment::Polarity::Neutral,
                                              Sentiment::Polarity::Positive};
        qreal x = 0;
        for (int i = 0; i < 3; ++i) {
            const qreal w = width() * qreal(m_counts[i]) / total;
            p.fillRect(QRectF(x, 0, w, height()), polarityColor(order[i]));
            x += w;
        }
    }

private:
    int m_counts[3] = {0, 0, 0};
};

QTableWidgetItem *readOnly(QTableWidgetItem *item)
{
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

} // namespace

SentimentDialog::SentimentDialog(Project *project, std::function<QList<qint64>()> activeDocuments,
                                 std::function<QList<Segment>()> retrievedSegments, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_activeDocuments(std::move(activeDocuments))
    , m_retrieved(std::move(retrievedSegments))
    , m_unit(new QComboBox)
    , m_language(new QComboBox)
    , m_filter(new QComboBox)
    , m_summary(new QLabel)
    , m_bar(new DistributionBar)
    , m_table(new QTableWidget)
    , m_codeButton(new QPushButton(tr("Codifica per polarità…")))
    , m_showButton(new QPushButton(tr("Mostra nel pannello segmenti")))
{
    setWindowTitle(tr("Analisi del sentiment"));
    resize(900, 640);

    m_unit->addItem(tr("Segmenti recuperati"), int(Sentiment::Unit::Segments));
    m_unit->addItem(tr("Frasi dei documenti attivi"), int(Sentiment::Unit::Sentences));
    m_unit->addItem(tr("Documenti attivi interi"), int(Sentiment::Unit::Documents));
    m_language->addItem(tr("Italiano e inglese"), int(Sentiment::Language::Auto));
    m_language->addItem(tr("Italiano"), int(Sentiment::Language::Italian));
    m_language->addItem(tr("Inglese"), int(Sentiment::Language::English));
    m_filter->addItem(tr("Tutte le polarità"), -1);
    m_filter->addItem(Sentiment::polarityName(Sentiment::Polarity::Positive), int(Sentiment::Polarity::Positive));
    m_filter->addItem(Sentiment::polarityName(Sentiment::Polarity::Negative), int(Sentiment::Polarity::Negative));
    m_filter->addItem(Sentiment::polarityName(Sentiment::Polarity::Neutral), int(Sentiment::Polarity::Neutral));

    auto *warning = new QLabel(tr("Stima automatica basata su un piccolo lessico (italiano e inglese): è un aiuto "
                                  "esplorativo, non un risultato definitivo. Ironia, sarcasmo e contesto sfuggono; "
                                  "verifica sempre i passaggi."));
    warning->setWordWrap(true);
    warning->setTextFormat(Qt::PlainText);

    auto *run = new QPushButton(tr("Analizza"));
    run->setDefault(true);
    connect(run, &QPushButton::clicked, this, &SentimentDialog::analyze);

    auto *options = new QHBoxLayout;
    options->addWidget(new QLabel(tr("Unità:")));
    options->addWidget(m_unit);
    options->addWidget(new QLabel(tr("Lingua:")));
    options->addWidget(m_language);
    options->addWidget(run);
    options->addStretch();

    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({tr("Documento"), tr("Testo"), tr("Punteggio"), tr("Polarità")});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSortingEnabled(true);
    m_table->setToolTip(tr("Clic sull'intestazione per ordinare; doppio clic per aprire il passaggio"));
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QTableWidgetItem *it = m_table->item(row, 0);
        if (!it)
            return;
        const Sentiment::Item &item = m_items.at(it->data(kIndexRole).toInt());
        emit passageActivated(item.documentId, item.start, item.end);
    });

    connect(m_filter, &QComboBox::currentIndexChanged, this,
            [this] { setPolarityFilter(m_filter->currentData().toInt()); });
    connect(m_codeButton, &QPushButton::clicked, this, &SentimentDialog::codeInteractive);
    connect(m_showButton, &QPushButton::clicked, this, &SentimentDialog::showInPanel);
    m_codeButton->setAutoDefault(false);
    m_showButton->setAutoDefault(false);
    m_codeButton->setEnabled(false);
    m_showButton->setEnabled(false);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(new QLabel(tr("Mostra:")));
    bottom->addWidget(m_filter);
    bottom->addStretch();
    bottom->addWidget(m_showButton);
    bottom->addWidget(m_codeButton);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(warning);
    layout->addLayout(options);
    layout->addWidget(m_summary);
    layout->addWidget(m_bar);
    layout->addWidget(m_table, 1);
    layout->addLayout(bottom);
}

void SentimentDialog::setUnit(Sentiment::Unit unit)
{
    m_unit->setCurrentIndex(m_unit->findData(int(unit)));
}

void SentimentDialog::setLanguage(Sentiment::Language language)
{
    m_language->setCurrentIndex(m_language->findData(int(language)));
}

void SentimentDialog::analyze()
{
    const auto unit = Sentiment::Unit(m_unit->currentData().toInt());
    const auto language = Sentiment::Language(m_language->currentData().toInt());
    m_items = Sentiment::analyze(*m_project, unit, m_retrieved ? m_retrieved() : QList<Segment>(),
                                 m_activeDocuments ? m_activeDocuments() : QList<qint64>(), language);
    fillTable();
}

void SentimentDialog::fillTable()
{
    int counts[3] = {0, 0, 0};
    double sum = 0;
    for (const Sentiment::Item &it : m_items) {
        ++counts[int(it.score.polarity)];
        sum += it.score.value;
    }
    static_cast<DistributionBar *>(m_bar)->setCounts(counts[int(Sentiment::Polarity::Negative)],
                                                     counts[int(Sentiment::Polarity::Neutral)],
                                                     counts[int(Sentiment::Polarity::Positive)]);
    if (m_items.isEmpty())
        m_summary->setText(tr("Nessun testo da analizzare."));
    else
        m_summary->setText(tr("%1 passaggi: %2 positivi, %3 negativi, %4 neutri. Punteggio medio %5")
                               .arg(m_items.size())
                               .arg(counts[int(Sentiment::Polarity::Positive)])
                               .arg(counts[int(Sentiment::Polarity::Negative)])
                               .arg(counts[int(Sentiment::Polarity::Neutral)])
                               .arg(sum / m_items.size(), 0, 'f', 2));

    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    const int rows = int(std::min<qsizetype>(m_items.size(), kMaxRows));
    m_table->setRowCount(rows);
    QHash<qint64, QString> names;
    for (int i = 0; i < rows; ++i) {
        const Sentiment::Item &it = m_items.at(i);
        if (!names.contains(it.documentId))
            names.insert(it.documentId, m_project->document(it.documentId).name);
        auto *doc = readOnly(new QTableWidgetItem(names[it.documentId]));
        doc->setData(kIndexRole, i);
        QString text = it.text.simplified();
        if (text.size() > 300)
            text = text.left(300) + QStringLiteral("…");
        auto *textItem = readOnly(new QTableWidgetItem(text));
        textItem->setToolTip(Ui::plainToolTip(it.text.left(1000)));
        auto *scoreItem = readOnly(new QTableWidgetItem);
        scoreItem->setData(Qt::DisplayRole, qRound(it.score.value * 100) / 100.0);
        scoreItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *polarity = readOnly(new QTableWidgetItem(Sentiment::polarityName(it.score.polarity)));
        polarity->setData(Qt::DecorationRole, Ui::colorIcon(polarityColor(it.score.polarity)));
        polarity->setData(Qt::UserRole, int(it.score.polarity));
        m_table->setItem(i, 0, doc);
        m_table->setItem(i, 1, textItem);
        m_table->setItem(i, 2, scoreItem);
        m_table->setItem(i, 3, polarity);
    }
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(2, Qt::DescendingOrder);
    m_table->resizeColumnToContents(0);
    m_table->resizeColumnToContents(3);
    m_codeButton->setEnabled(!m_items.isEmpty());
    m_showButton->setEnabled(!m_items.isEmpty());
    setPolarityFilter(m_filter->currentData().toInt());
}

int SentimentDialog::setPolarityFilter(int polarity)
{
    const int idx = m_filter->findData(polarity);
    if (idx >= 0 && idx != m_filter->currentIndex()) {
        const QSignalBlocker block(m_filter);
        m_filter->setCurrentIndex(idx);
    }
    int visible = 0;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        const bool show = polarity < 0 || m_table->item(r, 3)->data(Qt::UserRole).toInt() == polarity;
        m_table->setRowHidden(r, !show);
        visible += show;
    }
    return visible;
}

int SentimentDialog::visibleRows() const
{
    int n = 0;
    for (int r = 0; r < m_table->rowCount(); ++r)
        n += !m_table->isRowHidden(r);
    return n;
}

int SentimentDialog::codeItems(const QString &parentName)
{
    return Sentiment::codeByPolarity(*m_project, m_items, parentName);
}

void SentimentDialog::codeInteractive()
{
    const QString name = Ui::askText(this, tr("Codifica per polarità"),
                                     tr("Nome del codice sotto cui creare Positivo, Negativo e Neutro.\n"
                                        "I %1 passaggi analizzati vengono codificati (un solo passo di annullamento).")
                                         .arg(m_items.size()),
                                     tr("Sentiment"));
    if (name.isNull() || name.trimmed().isEmpty())
        return;
    const int added = codeItems(name);
    Ui::showInfo(this, tr("Codifica per polarità"),
                 tr("Aggiunti %n segmenti. La codifica è automatica: verificala prima di usarla.", nullptr, added));
}

void SentimentDialog::showInPanel()
{
    QList<Retrieval::Result> passages;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (m_table->isRowHidden(r))
            continue;
        const Sentiment::Item &it = m_items.at(m_table->item(r, 0)->data(kIndexRole).toInt());
        Retrieval::Result res;
        res.documentId = it.documentId;
        res.start = it.start;
        res.end = it.end;
        passages.append(res);
    }
    emit passagesRequested(passages, tr("Sentiment"));
}
