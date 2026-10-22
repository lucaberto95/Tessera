#include "ui/StatsDialog.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QPainter>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QValueAxis>
#include <QVBoxLayout>

namespace {

QString number(double x)
{
    return QString::number(x, 'f', 2);
}

QString percentText(double x)
{
    return QString::number(x, 'f', 1) + QStringLiteral(" %");
}

QTableWidgetItem *cell(const QString &text, bool right = false)
{
    auto *item = new QTableWidgetItem(text);
    if (right)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

} // namespace

StatsDialog::StatsDialog(Project *project, const QList<qint64> &activeDocuments, const QList<qint64> &activeCodes,
                         QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_docIds(activeDocuments)
    , m_codeIds(activeCodes)
    , m_variable(new QComboBox)
    , m_statsTable(new QTableWidget)
    , m_summary(new QLabel)
    , m_chart(new QChartView)
    , m_chiSquare(new QLabel)
    , m_columns(new QComboBox)
    , m_columnVariable(new QComboBox)
    , m_bins(new QSpinBox)
    , m_measure(new QComboBox)
    , m_percent(new QComboBox)
    , m_crosstabTable(new QTableWidget)
{
    setWindowTitle(tr("Statistiche e tabelle incrociate"));
    resize(900, 600);

    if (m_codeIds.isEmpty())
        for (const Code &c : m_project->codesInTreeOrder())
            m_codeIds << c.id;

    fillVariables();
    m_columns->addItem(tr("Valori di una variabile"), int(Columns::Variable));
    m_columns->addItem(tr("Gruppi di documenti"), int(Columns::DocumentGroups));
    m_columns->addItem(tr("Set di documenti"), int(Columns::DocumentSets));
    m_bins->setRange(0, 20);
    m_bins->setSpecialValueText(tr("valori distinti"));
    m_bins->setToolTip(tr("Variabili numeriche: numero di classi di uguale ampiezza (0 = un gruppo per valore)"));
    m_measure->addItem(tr("Segmenti"), int(Stats::Measure::Segments));
    m_measure->addItem(tr("Documenti con il codice"), int(Stats::Measure::Documents));
    m_percent->addItem(tr("Valori assoluti"), int(Stats::Percent::None));
    m_percent->addItem(tr("% di colonna"), int(Stats::Percent::OfColumn));
    m_percent->addItem(tr("% di riga"), int(Stats::Percent::OfRow));

    m_statsTable->setObjectName(QStringLiteral("statsTable"));
    m_crosstabTable->setObjectName(QStringLiteral("crosstabTable"));
    m_statsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_crosstabTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(m_variable, &QComboBox::currentIndexChanged, this, &StatsDialog::rebuildStats);
    for (QComboBox *box : {m_columns, m_columnVariable, m_measure, m_percent})
        connect(box, &QComboBox::currentIndexChanged, this, &StatsDialog::rebuildCrosstab);
    connect(m_bins, &QSpinBox::valueChanged, this, &StatsDialog::rebuildCrosstab);

    auto *tabs = new QTabWidget;

    auto *statsPage = new QWidget;
    auto *statsLayout = new QVBoxLayout(statsPage);
    auto *statsBar = new QHBoxLayout;
    statsBar->addWidget(new QLabel(tr("Variabile:")));
    statsBar->addWidget(m_variable);
    statsBar->addStretch();
    auto *statsCsv = new QPushButton(tr("Esporta CSV…"));
    connect(statsCsv, &QPushButton::clicked, this, [this] { exportTable(m_statsExport, tr("statistiche")); });
    statsBar->addWidget(statsCsv);
    statsLayout->addLayout(statsBar);
    statsLayout->addWidget(m_summary);
    m_chart->setMinimumHeight(220);
    m_chart->setRenderHint(QPainter::Antialiasing);
    statsLayout->addWidget(m_chart, 1);
    statsLayout->addWidget(m_statsTable, 1);
    tabs->addTab(statsPage, tr("Variabili"));

    auto *crossPage = new QWidget;
    auto *crossLayout = new QVBoxLayout(crossPage);
    auto *crossBar = new QHBoxLayout;
    crossBar->addWidget(new QLabel(tr("Colonne:")));
    crossBar->addWidget(m_columns);
    crossBar->addWidget(m_columnVariable);
    crossBar->addWidget(m_bins);
    crossBar->addSpacing(12);
    crossBar->addWidget(new QLabel(tr("Valori:")));
    crossBar->addWidget(m_measure);
    crossBar->addWidget(m_percent);
    crossBar->addStretch();
    auto *crossCsv = new QPushButton(tr("Esporta CSV…"));
    connect(crossCsv, &QPushButton::clicked, this, [this] { exportTable(m_crosstabExport, tr("tabella incrociata")); });
    crossBar->addWidget(crossCsv);
    auto *toVariables = new QPushButton(tr("Codici in variabili"));
    toVariables->setToolTip(tr("Crea una variabile intera per ogni codice con il numero di segmenti (o 0/1) per documento"));
    connect(toVariables, &QPushButton::clicked, this, [this] {
        const auto measure = Stats::Measure(m_measure->currentData().toInt());
        const int n = int(Stats::codesToVariables(*m_project, m_codeIds, measure).size());
        QMessageBox::information(this, tr("Tessera"), tr("Create %n variabili.", nullptr, n));
    });
    crossBar->addWidget(toVariables);
    crossLayout->addLayout(crossBar);
    crossLayout->addWidget(m_crosstabTable);
    m_chiSquare->setWordWrap(true);
    crossLayout->addWidget(m_chiSquare);
    tabs->addTab(crossPage, tr("Tabella incrociata"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(
        m_docIds.isEmpty() ? tr("Tutti i documenti (attiva documenti e codici per restringere l'analisi)")
                           : tr("%n documenti attivi", nullptr, int(m_docIds.size()))));
    layout->addWidget(tabs);

    // Variables created elsewhere (or by "Codici in variabili") appear without reopening the dialog.
    connect(m_project, &Project::variablesChanged, this, [this] {
        fillVariables();
        rebuildStats();
        rebuildCrosstab();
    });

    rebuildStats();
    rebuildCrosstab();
}

void StatsDialog::fillVariables()
{
    for (QComboBox *box : {m_variable, m_columnVariable}) {
        const QSignalBlocker block(box);
        const qint64 current = box->currentData().toLongLong();
        box->clear();
        for (const Variable &v : m_project->variables())
            box->addItem(v.name, v.id);
        const int i = box->findData(current);
        if (i >= 0)
            box->setCurrentIndex(i);
    }
}

void StatsDialog::selectVariable(qint64 variableId)
{
    const int i = m_variable->findData(variableId);
    if (i >= 0)
        m_variable->setCurrentIndex(i);
}

void StatsDialog::setColumns(Columns columns, qint64 variableId, int bins)
{
    const QSignalBlocker b1(m_columns), b2(m_columnVariable), b3(m_bins);
    m_columns->setCurrentIndex(m_columns->findData(int(columns)));
    if (variableId)
        m_columnVariable->setCurrentIndex(m_columnVariable->findData(variableId));
    m_bins->setValue(bins);
    rebuildCrosstab();
}

void StatsDialog::setMeasure(Stats::Measure measure)
{
    m_measure->setCurrentIndex(m_measure->findData(int(measure)));
}

void StatsDialog::setPercent(Stats::Percent percent)
{
    m_percent->setCurrentIndex(m_percent->findData(int(percent)));
}

void StatsDialog::rebuildStats()
{
    m_statsTable->clear();
    m_statsExport.clear();
    const qint64 id = m_variable->currentData().toLongLong();
    if (!id) {
        m_summary->setText(tr("Il progetto non ha variabili."));
        m_statsTable->setRowCount(0);
        m_statsTable->setColumnCount(0);
        return;
    }
    m_stats = Stats::describe(*m_project, id, m_docIds);
    const Stats::VariableStats &s = m_stats;

    m_summary->setText(tr("%1 documenti: %2 con valore, %3 vuoti, %4 non validi")
                           .arg(s.total).arg(s.valid).arg(s.empty).arg(s.invalid));

    // Descriptive figures first (numeric variables), then the frequency table.
    QList<QPair<QString, QString>> figures;
    if (s.numeric && s.valid > 0)
        figures = {{tr("n"), QString::number(s.valid)},    {tr("Media"), number(s.mean)},
                   {tr("Mediana"), number(s.median)},      {tr("Deviazione standard"), number(s.stdDev)},
                   {tr("Minimo"), number(s.min)},          {tr("Massimo"), number(s.max)}};

    m_statsTable->setColumnCount(3);
    m_statsTable->setHorizontalHeaderLabels({tr("Valore"), tr("Frequenza"), tr("%")});
    m_statsExport.append({tr("Valore"), tr("Frequenza"), tr("%")});
    m_statsTable->setRowCount(int(figures.size() + s.frequencies.size()));
    int row = 0;
    for (const auto &[label, value] : figures) {
        m_statsTable->setItem(row, 0, cell(label));
        m_statsTable->setItem(row, 1, cell(value, true));
        m_statsTable->setItem(row, 2, cell(QString()));
        m_statsExport.append({label, value, QString()});
        ++row;
    }
    for (const Stats::Frequency &f : s.frequencies) {
        QString label = f.value;
        if (s.type == VariableType::Boolean)
            label = f.value == QLatin1String("1") ? tr("Vero") : tr("Falso");
        m_statsTable->setItem(row, 0, cell(label));
        m_statsTable->setItem(row, 1, cell(QString::number(f.count), true));
        m_statsTable->setItem(row, 2, cell(percentText(f.percent), true));
        m_statsExport.append({label, QString::number(f.count), number(f.percent)});
        ++row;
    }
    m_statsTable->horizontalHeader()->setStretchLastSection(true);
    m_statsTable->resizeColumnsToContents();
    rebuildChart();
}

void StatsDialog::rebuildChart()
{
    // Numeric variables: histogram; the others: one bar per value (the 25 most frequent).
    QStringList categories;
    QList<int> values;
    const Stats::VariableStats &s = m_stats;
    if (s.numeric) {
        const Stats::Histogram h = Stats::histogram(s);
        for (int i = 0; i < h.counts.size(); ++i) {
            categories << QStringLiteral("%1–%2").arg(QString::number(h.edges[i], 'g', 4),
                                                       QString::number(h.edges[i + 1], 'g', 4));
            values << h.counts[i];
        }
    } else {
        for (int i = 0; i < s.frequencies.size() && i < 25; ++i) {
            const Stats::Frequency &f = s.frequencies[i];
            // Qt Charts draws axis labels as rich text.
            categories << (s.type == VariableType::Boolean ? (f.value == QLatin1String("1") ? tr("Vero") : tr("Falso"))
                                                           : f.value.toHtmlEscaped());
            values << f.count;
        }
    }
    m_chartBars = int(values.size());

    auto *chart = new QChart;
    if (!values.isEmpty()) {
        auto *set = new QBarSet(tr("Documenti"));
        int max = 1;
        for (int v : values) {
            *set << v;
            max = qMax(max, v);
        }
        auto *series = new QBarSeries;
        series->append(set);
        series->setLabelsVisible(true);
        series->setBarWidth(s.numeric ? 1.0 : 0.7);
        chart->addSeries(series);
        auto *axisX = new QBarCategoryAxis;
        axisX->append(categories);
        chart->addAxis(axisX, Qt::AlignBottom);
        series->attachAxis(axisX);
        auto *axisY = new QValueAxis;
        axisY->setRange(0, max);
        axisY->setLabelFormat(QStringLiteral("%d"));
        axisY->applyNiceNumbers();
        chart->addAxis(axisY, Qt::AlignLeft);
        series->attachAxis(axisY);
    }
    chart->legend()->hide();
    chart->setTitle(m_variable->currentText().toHtmlEscaped());
    QChart *old = m_chart->chart();
    m_chart->setChart(chart);
    delete old;
}

void StatsDialog::rebuildCrosstab()
{
    const auto kind = Columns(m_columns->currentData().toInt());
    m_columnVariable->setVisible(kind == Columns::Variable);
    m_bins->setVisible(kind == Columns::Variable);

    QList<Stats::Group> groups;
    switch (kind) {
    case Columns::Variable:
        if (m_columnVariable->currentData().toLongLong())
            groups = Stats::groupsByVariable(*m_project, m_columnVariable->currentData().toLongLong(), m_docIds,
                                             m_bins->value());
        break;
    case Columns::DocumentGroups: groups = Stats::groupsByDocumentGroups(*m_project, m_docIds); break;
    case Columns::DocumentSets: groups = Stats::groupsByDocumentSets(*m_project, m_docIds); break;
    }

    const auto measure = Stats::Measure(m_measure->currentData().toInt());
    const auto percent = Stats::Percent(m_percent->currentData().toInt());
    m_crosstab = Stats::crosstab(m_codeIds, groups, m_project->segments(m_docIds, m_codeIds), measure);

    const int cols = int(groups.size());
    QStringList header{tr("Codice")};
    for (const Stats::Group &g : groups)
        header << g.label;
    header << tr("Totale");

    m_crosstabTable->clear();
    m_crosstabTable->setColumnCount(int(header.size()));
    m_crosstabTable->setHorizontalHeaderLabels(header);
    m_crosstabTable->setRowCount(int(m_codeIds.size()) + 1);
    m_crosstabExport = {header};

    auto text = [&](double value, bool isPercent) {
        return isPercent ? percentText(value) : QString::number(int(value));
    };
    const bool isPercent = percent != Stats::Percent::None;
    for (int r = 0; r < m_codeIds.size(); ++r) {
        const QString name = m_project->codePath(m_codeIds[r]);
        QStringList line{name};
        m_crosstabTable->setItem(r, 0, cell(name));
        for (int c = 0; c < cols; ++c) {
            const QString t = text(m_crosstab.percent(r, c, percent), isPercent);
            m_crosstabTable->setItem(r, c + 1, cell(t, true));
            line << (isPercent ? number(m_crosstab.percent(r, c, percent)) : t);
        }
        // Row total: the sum of the row (documents can repeat across sets, so it is not a document count).
        const QString total = isPercent ? percentText(percent == Stats::Percent::OfRow ? 100.0 : 0.0)
                                        : QString::number(m_crosstab.rowTotal(r));
        m_crosstabTable->setItem(r, cols + 1, cell(percent == Stats::Percent::OfColumn ? QString() : total, true));
        line << (percent == Stats::Percent::OfColumn ? QString() : total);
        m_crosstabExport.append(line);
    }

    const int last = int(m_codeIds.size());
    QStringList totals{tr("Totale documenti")};
    m_crosstabTable->setItem(last, 0, cell(tr("Totale documenti")));
    int sum = 0;
    for (int c = 0; c < cols; ++c) {
        const QString t = QString::number(m_crosstab.totalDocuments[c]);
        m_crosstabTable->setItem(last, c + 1, cell(t, true));
        totals << t;
        sum += m_crosstab.totalDocuments[c];
    }
    m_crosstabTable->setItem(last, cols + 1, cell(QString::number(sum), true));
    totals << QString::number(sum);
    m_crosstabExport.append(totals);

    QFont bold = m_crosstabTable->font();
    bold.setBold(true);
    for (int c = 0; c <= cols + 1; ++c)
        if (auto *item = m_crosstabTable->item(last, c))
            item->setFont(bold);
    m_crosstabTable->resizeColumnsToContents();

    m_chi = Stats::chiSquare(m_crosstab);
    if (!m_chi.valid) {
        m_chiSquare->setText(tr("Chi-quadrato non calcolabile: servono almeno due righe e due colonne non vuote."));
    } else {
        QString text = tr("χ² = %1, gdl = %2, p = %3")
                           .arg(number(m_chi.statistic)).arg(m_chi.df)
                           .arg(m_chi.pValue < 0.0001 ? QStringLiteral("< 0,0001")
                                                      : QString::number(m_chi.pValue, 'f', 4).replace('.', ','));
        if (m_chi.smallExpected > 0)
            text += tr(" — attenzione: %n celle con frequenza attesa inferiore a 5, il test è poco affidabile", nullptr,
                       m_chi.smallExpected);
        if (kind == Columns::DocumentSets || measure == Stats::Measure::Segments)
            text += tr(". Indicativo: i segmenti o i set sovrapposti non sono osservazioni indipendenti.");
        m_chiSquare->setText(text);
    }
}

void StatsDialog::exportTable(const Exporters::Table &table, const QString &suffix)
{
    const QString path = Ui::saveFileName(this, tr("Esporta"), m_project->name() + QStringLiteral(" - ") + suffix + QStringLiteral(".csv"),
                                          tr("CSV per Excel (*.csv);;Pagina HTML (*.html)"));
    if (path.isEmpty())
        return;
    QString error;
    const bool ok = path.endsWith(QLatin1String(".html"), Qt::CaseInsensitive)
                        ? Exporters::writeHtmlTable(path, windowTitle(), table, &error)
                        : Exporters::writeCsv(path, table, &error);
    if (!ok)
        Ui::showError(this, error);
}
