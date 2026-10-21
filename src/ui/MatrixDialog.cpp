#include "ui/MatrixDialog.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QBarCategoryAxis>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHorizontalStackedBarSeries>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QValueAxis>
#include <QVBoxLayout>

MatrixDialog::MatrixDialog(Project *project, const QList<qint64> &activeDocuments, const QList<qint64> &activeCodes,
                           QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_docIds(activeDocuments)
    , m_codeIds(activeCodes)
    , m_presenceOnly(new QCheckBox(tr("Conta solo presenza (1 = codice presente nel documento)")))
    , m_aggregate(new QCheckBox(tr("Includi i sottocodici nei codici superiori")))
    , m_matrix(new QTableWidget)
    , m_frequencies(new QTableWidget)
    , m_chart(new QChartView)
    , m_scopeLabel(new QLabel)
{
    setWindowTitle(tr("Matrice codici × documenti"));
    resize(1000, 640);

    if (m_docIds.isEmpty())
        for (const Document &d : m_project->documents())
            m_docIds << d.id;
    if (m_codeIds.isEmpty())
        for (const Code &c : m_project->codesInTreeOrder())
            m_codeIds << c.id;
    m_scopeLabel->setText(tr("%n codici", nullptr, int(m_codeIds.size())) + QStringLiteral(" × ") +
                          tr("%n documenti", nullptr, int(m_docIds.size())) + QStringLiteral(" — ") +
                          tr("attiva codici e documenti per restringere l'analisi (nessuno attivo = tutti)"));

    for (QTableWidget *t : {m_matrix, m_frequencies}) {
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setSelectionMode(QAbstractItemView::SingleSelection);
    }
    m_frequencies->verticalHeader()->setVisible(false);
    connect(m_matrix, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (row < m_codeIds.size() && col < m_docIds.size())
            emit cellActivated(m_codeIds[row], m_docIds[col]);
    });
    connect(m_presenceOnly, &QCheckBox::toggled, this, &MatrixDialog::rebuild);
    connect(m_aggregate, &QCheckBox::toggled, this, &MatrixDialog::rebuild);
    m_chart->setRenderHint(QPainter::Antialiasing);

    auto *tabs = new QTabWidget;
    auto *matrixPage = new QWidget;
    auto *matrixLayout = new QVBoxLayout(matrixPage);
    auto *options = new QHBoxLayout;
    options->addWidget(m_presenceOnly);
    options->addWidget(m_aggregate);
    options->addStretch();
    auto *exportMatrix = new QPushButton(tr("Esporta CSV…"));
    connect(exportMatrix, &QPushButton::clicked, this, [this] { exportTable(m_matrixTable, tr("matrice")); });
    options->addWidget(exportMatrix);
    matrixLayout->addLayout(options);
    matrixLayout->addWidget(m_matrix);
    tabs->addTab(matrixPage, tr("Matrice"));

    auto *freqPage = new QWidget;
    auto *freqLayout = new QVBoxLayout(freqPage);
    auto *exportFreq = new QPushButton(tr("Esporta CSV…"));
    connect(exportFreq, &QPushButton::clicked, this, [this] { exportTable(m_frequencyTable, tr("frequenze")); });
    auto *freqBar = new QHBoxLayout;
    freqBar->addStretch();
    freqBar->addWidget(exportFreq);
    freqLayout->addLayout(freqBar);
    freqLayout->addWidget(m_frequencies);
    tabs->addTab(freqPage, tr("Frequenze dei codici"));
    tabs->addTab(m_chart, tr("Grafico"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_scopeLabel);
    layout->addWidget(tabs);
    rebuild();
}

void MatrixDialog::rebuild()
{
    const bool presence = m_presenceOnly->isChecked();
    const bool aggregate = m_aggregate->isChecked();
    const auto raw = m_project->segmentCountByCodeAndDocument();
    QHash<qint64, Code> codes;
    for (const Code &c : m_project->codes())
        codes.insert(c.id, c);
    QHash<qint64, QString> docNames;
    for (const Document &d : m_project->documents())
        docNames.insert(d.id, d.name);

    auto cellCount = [&](qint64 code, qint64 doc) {
        int n = raw.value(code).value(doc);
        if (aggregate)
            for (qint64 sub : m_project->codeDescendants(code))
                n += raw.value(sub).value(doc);
        return presence ? (n > 0 ? 1 : 0) : n;
    };

    // ---- matrix
    const int rows = int(m_codeIds.size());
    const int cols = int(m_docIds.size());
    QList<QList<int>> values(rows, QList<int>(cols, 0));
    int maxValue = 1;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            values[r][c] = cellCount(m_codeIds[r], m_docIds[c]);
            maxValue = qMax(maxValue, values[r][c]);
        }

    m_matrix->clear();
    m_matrix->setRowCount(rows + 1);
    m_matrix->setColumnCount(cols + 1);
    QStringList header;
    for (qint64 d : m_docIds)
        header << docNames.value(d);
    header << tr("Totale");
    m_matrix->setHorizontalHeaderLabels(header);
    QStringList vheader;
    for (qint64 c : m_codeIds)
        vheader << m_project->codePath(c);
    vheader << tr("Totale");
    m_matrix->setVerticalHeaderLabels(vheader);
    for (int r = 0; r < rows; ++r)
        m_matrix->verticalHeaderItem(r)->setIcon(Ui::colorIcon(codes.value(m_codeIds[r]).color));

    m_matrixTable.clear();
    m_matrixTable << (QStringList{tr("Codice")} + header);
    QList<int> colTotals(cols + 1, 0);
    for (int r = 0; r < rows; ++r) {
        int rowTotal = 0;
        QStringList csvRow{vheader[r]};
        QColor base = codes.value(m_codeIds[r]).color;
        for (int c = 0; c < cols; ++c) {
            const int v = values[r][c];
            rowTotal += v;
            colTotals[c] += v;
            auto *item = new QTableWidgetItem(v ? QString::number(v) : QString());
            item->setTextAlignment(Qt::AlignCenter);
            if (v) {
                QColor bg = base;
                bg.setAlpha(40 + int(160.0 * v / maxValue));
                item->setBackground(bg);
            }
            m_matrix->setItem(r, c, item);
            csvRow << QString::number(v);
        }
        colTotals[cols] += rowTotal;
        auto *total = new QTableWidgetItem(QString::number(rowTotal));
        total->setTextAlignment(Qt::AlignCenter);
        QFont bold = total->font();
        bold.setBold(true);
        total->setFont(bold);
        m_matrix->setItem(r, cols, total);
        csvRow << QString::number(rowTotal);
        m_matrixTable << csvRow;
    }
    QStringList totalsRow{tr("Totale")};
    for (int c = 0; c <= cols; ++c) {
        auto *item = new QTableWidgetItem(QString::number(colTotals[c]));
        item->setTextAlignment(Qt::AlignCenter);
        QFont bold = item->font();
        bold.setBold(true);
        item->setFont(bold);
        m_matrix->setItem(rows, c, item);
        totalsRow << QString::number(colTotals[c]);
    }
    m_matrixTable << totalsRow;
    m_matrix->resizeColumnsToContents();

    // ---- frequencies (always counts, independent of the presence option)
    m_frequencies->clear();
    m_frequencies->setColumnCount(4);
    m_frequencies->setHorizontalHeaderLabels({tr("Codice"), tr("Segmenti"), tr("Documenti"), tr("% documenti")});
    m_frequencies->setRowCount(rows);
    m_frequencyTable.clear();
    m_frequencyTable << QStringList{tr("Codice"), tr("Segmenti"), tr("Documenti"), tr("% documenti")};
    QList<std::pair<qint64, int>> chartData;
    for (int r = 0; r < rows; ++r) {
        int segs = 0, docs = 0;
        for (int c = 0; c < cols; ++c) {
            int n = raw.value(m_codeIds[r]).value(m_docIds[c]);
            if (aggregate)
                for (qint64 sub : m_project->codeDescendants(m_codeIds[r]))
                    n += raw.value(sub).value(m_docIds[c]);
            segs += n;
            docs += n > 0 ? 1 : 0;
        }
        const double pct = cols ? 100.0 * docs / cols : 0.0;
        const QStringList row{vheader[r], QString::number(segs), QString::number(docs), QString::number(pct, 'f', 1)};
        for (int c = 0; c < 4; ++c) {
            auto *item = new QTableWidgetItem;
            if (c == 0) {
                item->setText(row[0]);
                item->setIcon(Ui::colorIcon(codes.value(m_codeIds[r]).color));
            } else {
                item->setData(Qt::DisplayRole, c == 3 ? QVariant(pct) : QVariant(row[c].toInt()));
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            }
            m_frequencies->setItem(r, c, item);
        }
        m_frequencyTable << row;
        chartData.append({m_codeIds[r], segs});
    }
    m_frequencies->setSortingEnabled(true);
    m_frequencies->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    // ---- chart: top 25 codes by number of segments
    std::stable_sort(chartData.begin(), chartData.end(), [](auto a, auto b) { return a.second > b.second; });
    if (chartData.size() > 25)
        chartData.resize(25);
    std::reverse(chartData.begin(), chartData.end()); // largest at the top
    auto *chart = new QChart;
    auto *series = new QHorizontalStackedBarSeries;
    QStringList categories;
    int maxSegs = 1;
    for (int i = 0; i < chartData.size(); ++i) {
        const Code code = codes.value(chartData[i].first);
        categories << code.name;
        auto *set = new QBarSet(code.name);
        set->setColor(code.color);
        set->setBorderColor(code.color.darker(120));
        for (int j = 0; j < chartData.size(); ++j)
            *set << (j == i ? chartData[i].second : 0);
        series->append(set);
        maxSegs = qMax(maxSegs, chartData[i].second);
    }
    series->setBarWidth(0.7);
    chart->addSeries(series);
    auto *axisY = new QBarCategoryAxis;
    axisY->append(categories);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);
    auto *axisX = new QValueAxis;
    axisX->setRange(0, maxSegs);
    axisX->setLabelFormat(QStringLiteral("%d"));
    axisX->applyNiceNumbers();
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);
    chart->legend()->hide();
    chart->setTitle(tr("Segmenti per codice"));
    QChart *old = m_chart->chart();
    m_chart->setChart(chart);
    delete old;
}

void MatrixDialog::exportTable(const Exporters::Table &table, const QString &suffix)
{
    const QString path = Ui::saveFileName(this, tr("Esporta"), m_project->name() + QStringLiteral(" - ") + suffix +
                                                                   QStringLiteral(".csv"),
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
