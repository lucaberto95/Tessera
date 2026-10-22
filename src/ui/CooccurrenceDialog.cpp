#include "ui/CooccurrenceDialog.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QGraphicsEllipseItem>
#include <QGraphicsLineItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QSvgGenerator>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace {
constexpr QSizeF kMapSize(760, 520);
constexpr qreal kMapMargin = 50;
} // namespace

CooccurrenceDialog::CooccurrenceDialog(Project *project, const QList<qint64> &activeDocuments,
                                       const QList<qint64> &activeCodes, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_docIds(activeDocuments)
    , m_codeIds(activeCodes)
    , m_criterion(new QComboBox)
    , m_distance(new QSpinBox)
    , m_unit(new QComboBox)
    , m_value(new QComboBox)
    , m_threshold(new QDoubleSpinBox)
    , m_scopeLabel(new QLabel)
    , m_table(new QTableWidget)
    , m_scene(new QGraphicsScene(this))
    , m_view(new QGraphicsView)
{
    setWindowTitle(tr("Co-occorrenze dei codici"));
    resize(1000, 680);

    if (m_codeIds.isEmpty())
        for (const Code &c : m_project->codesInTreeOrder())
            m_codeIds << c.id;
    m_segments = m_project->segments(m_docIds, m_codeIds);
    m_scopeLabel->setText(tr("%n codici", nullptr, int(m_codeIds.size())) + QStringLiteral(" — ") +
                          (m_docIds.isEmpty() ? tr("tutti i documenti") : tr("%n documenti attivi", nullptr, int(m_docIds.size()))) +
                          QStringLiteral(" — ") +
                          tr("attiva codici e documenti per restringere l'analisi (nessuno attivo = tutti)"));

    for (Cooccurrence::Criterion c : {Cooccurrence::Criterion::Overlap, Cooccurrence::Criterion::Proximity,
                                      Cooccurrence::Criterion::SameDocument})
        m_criterion->addItem(Cooccurrence::criterionLabel(c), int(c));
    m_distance->setRange(0, 1000);
    m_distance->setValue(1);
    m_unit->addItem(tr("paragrafi"), true);
    m_unit->addItem(tr("caratteri"), false);
    m_value->addItem(tr("Numero di co-occorrenze"), false);
    m_value->addItem(tr("Indice di Jaccard"), true);
    m_threshold->setDecimals(2);
    m_threshold->setRange(0, 100000);
    m_threshold->setSingleStep(1);
    m_threshold->setValue(1);

    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setToolTip(tr("Doppio clic su una cella per vedere i passaggi nel pannello dei segmenti"));
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &CooccurrenceDialog::activateCell);

    m_view->setScene(m_scene);
    m_view->setRenderHint(QPainter::Antialiasing);

    // A sensible starting threshold for each kind of value, set before the rebuild that follows.
    connect(m_value, &QComboBox::currentIndexChanged, this, [this] {
        const QSignalBlocker block(m_threshold);
        const bool jaccard = m_value->currentData().toBool();
        m_threshold->setMaximum(jaccard ? 1 : 100000);
        m_threshold->setValue(jaccard ? 0.1 : 1);
    });
    connect(m_criterion, &QComboBox::currentIndexChanged, this, &CooccurrenceDialog::rebuild);
    connect(m_distance, &QSpinBox::valueChanged, this, &CooccurrenceDialog::rebuild);
    connect(m_unit, &QComboBox::currentIndexChanged, this, &CooccurrenceDialog::rebuild);
    connect(m_value, &QComboBox::currentIndexChanged, this, &CooccurrenceDialog::rebuild);
    connect(m_threshold, &QDoubleSpinBox::valueChanged, this, [this] { rebuildMap(); });

    auto *options = new QHBoxLayout;
    options->addWidget(new QLabel(tr("Criterio:")));
    options->addWidget(m_criterion);
    options->addWidget(new QLabel(tr("entro")));
    options->addWidget(m_distance);
    options->addWidget(m_unit);
    options->addSpacing(16);
    options->addWidget(new QLabel(tr("Valore:")));
    options->addWidget(m_value);
    options->addStretch();

    auto *tabs = new QTabWidget;
    auto *matrixPage = new QWidget;
    auto *matrixLayout = new QVBoxLayout(matrixPage);
    auto *matrixBar = new QHBoxLayout;
    matrixBar->addStretch();
    auto *exportCsv = new QPushButton(tr("Esporta CSV…"));
    connect(exportCsv, &QPushButton::clicked, this, &CooccurrenceDialog::exportMatrix);
    matrixBar->addWidget(exportCsv);
    matrixLayout->addLayout(matrixBar);
    matrixLayout->addWidget(m_table);
    tabs->addTab(matrixPage, tr("Matrice"));

    auto *mapPage = new QWidget;
    auto *mapLayout = new QVBoxLayout(mapPage);
    auto *mapBar = new QHBoxLayout;
    mapBar->addWidget(new QLabel(tr("Mostra gli archi con valore almeno")));
    mapBar->addWidget(m_threshold);
    mapBar->addStretch();
    auto *png = new QPushButton(tr("Esporta PNG…"));
    connect(png, &QPushButton::clicked, this, &CooccurrenceDialog::exportMapPng);
    auto *svg = new QPushButton(tr("Esporta SVG…"));
    connect(svg, &QPushButton::clicked, this, &CooccurrenceDialog::exportMapSvg);
    mapBar->addWidget(png);
    mapBar->addWidget(svg);
    mapLayout->addLayout(mapBar);
    mapLayout->addWidget(m_view);
    tabs->addTab(mapPage, tr("Mappa"));

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_scopeLabel);
    layout->addLayout(options);
    layout->addWidget(tabs);
    rebuild();
}

void CooccurrenceDialog::setCriterion(Cooccurrence::Criterion criterion)
{
    m_criterion->setCurrentIndex(m_criterion->findData(int(criterion)));
}

void CooccurrenceDialog::setNormalized(bool jaccard)
{
    m_value->setCurrentIndex(m_value->findData(jaccard));
}

void CooccurrenceDialog::setMapThreshold(double threshold)
{
    m_threshold->setValue(threshold);
}

Cooccurrence::Options CooccurrenceDialog::options() const
{
    Cooccurrence::Options o;
    o.criterion = Cooccurrence::Criterion(m_criterion->currentData().toInt());
    o.codes = m_codeIds;
    o.distance = m_distance->value();
    o.inParagraphs = m_unit->currentData().toBool();
    return o;
}

double CooccurrenceDialog::cellValue(qint64 a, qint64 b) const
{
    if (a == b || !m_value->currentData().toBool())
        return m_matrix.count(a, b);
    return m_matrix.jaccard(a, b);
}

QString CooccurrenceDialog::cellText(double value, bool frequency) const
{
    return m_value->currentData().toBool() && !frequency ? QString::number(value, 'f', 2) : QString::number(int(value));
}

void CooccurrenceDialog::rebuild()
{
    const bool proximity = options().criterion == Cooccurrence::Criterion::Proximity;
    m_distance->setEnabled(proximity);
    m_unit->setEnabled(proximity);
    const bool jaccard = m_value->currentData().toBool();
    m_threshold->setDecimals(jaccard ? 2 : 0);
    m_threshold->setSingleStep(jaccard ? 0.05 : 1);
    m_threshold->setMaximum(jaccard ? 1 : 100000);

    m_matrix = Cooccurrence::compute(options(), m_segments,
                                     [this](qint64 id) { return m_project->documentText(id); });
    rebuildMatrix();
    rebuildMap();
}

void CooccurrenceDialog::rebuildMatrix()
{
    QHash<qint64, Code> codes;
    for (const Code &c : m_project->codes())
        codes.insert(c.id, c);
    const int n = int(m_codeIds.size());

    double maxValue = 0;
    for (qint64 a : std::as_const(m_codeIds))
        for (qint64 b : std::as_const(m_codeIds))
            if (a != b)
                maxValue = std::max(maxValue, cellValue(a, b));

    QStringList header;
    for (qint64 c : std::as_const(m_codeIds))
        header << m_project->codePath(c);
    m_table->clear();
    m_table->setRowCount(n);
    m_table->setColumnCount(n);
    m_table->setHorizontalHeaderLabels(header);
    m_table->setVerticalHeaderLabels(header);
    for (int i = 0; i < n; ++i) {
        const QIcon icon = Ui::colorIcon(codes.value(m_codeIds[i]).color);
        m_table->verticalHeaderItem(i)->setIcon(icon);
        m_table->horizontalHeaderItem(i)->setIcon(icon);
    }

    m_exportTable.clear();
    m_exportTable << (QStringList{tr("Codice")} + header);
    for (int r = 0; r < n; ++r) {
        QStringList csvRow{header[r]};
        for (int c = 0; c < n; ++c) {
            const double v = cellValue(m_codeIds[r], m_codeIds[c]);
            auto *item = new QTableWidgetItem(r == c || v > 0 ? cellText(v, r == c) : QString());
            item->setTextAlignment(Qt::AlignCenter);
            if (r == c) { // the diagonal is the code's frequency
                QFont bold = item->font();
                bold.setBold(true);
                item->setFont(bold);
                item->setBackground(palette().alternateBase());
            } else if (v > 0 && maxValue > 0) {
                QColor bg = codes.value(m_codeIds[r]).color;
                bg.setAlpha(40 + int(160.0 * v / maxValue));
                item->setBackground(bg);
            }
            m_table->setItem(r, c, item);
            csvRow << cellText(v, r == c);
        }
        m_exportTable << csvRow;
    }
    m_table->resizeColumnsToContents();
}

void CooccurrenceDialog::rebuildMap()
{
    m_scene->clear();
    m_mapNodes = 0;
    m_mapEdges = 0;
    const int n = int(m_codeIds.size());
    m_scene->setSceneRect(0, 0, kMapSize.width(), kMapSize.height());
    m_scene->setBackgroundBrush(palette().base());
    if (n == 0)
        return;

    QList<QList<double>> weights(n, QList<double>(n, 0.0));
    double maxWeight = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (i != j) {
                weights[i][j] = cellValue(m_codeIds[i], m_codeIds[j]);
                maxWeight = std::max(maxWeight, weights[i][j]);
            }
    int maxFrequency = 1;
    for (qint64 c : std::as_const(m_codeIds))
        maxFrequency = std::max(maxFrequency, m_matrix.frequency(c));

    // The layout always uses the full weights so that moving the threshold does not move the nodes.
    const QList<QPointF> unit = Cooccurrence::forceLayout(weights);
    QList<QPointF> pos;
    for (const QPointF &p : unit)
        pos << QPointF(kMapMargin + p.x() * (kMapSize.width() - 2 * kMapMargin),
                       kMapMargin + p.y() * (kMapSize.height() - 2 * kMapMargin));

    const double threshold = m_threshold->value();
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            const double w = weights[i][j];
            if (w <= 0 || w < threshold)
                continue;
            QColor line = palette().text().color();
            line.setAlpha(110);
            auto *edge = m_scene->addLine(QLineF(pos[i], pos[j]), QPen(line, 1 + 6 * w / maxWeight));
            edge->setToolTip(Ui::plainToolTip(tr("%1 × %2: %3").arg(m_project->codePath(m_codeIds[i]),
                                                                    m_project->codePath(m_codeIds[j]), cellText(w))));
            edge->setZValue(0);
            ++m_mapEdges;
        }

    QHash<qint64, Code> codes;
    for (const Code &c : m_project->codes())
        codes.insert(c.id, c);
    for (int i = 0; i < n; ++i) {
        const Code code = codes.value(m_codeIds[i]);
        const double freq = m_matrix.frequency(m_codeIds[i]);
        const double radius = 8 + 20 * std::sqrt(freq / maxFrequency);
        auto *node = m_scene->addEllipse(QRectF(pos[i].x() - radius, pos[i].y() - radius, 2 * radius, 2 * radius),
                                         QPen(code.color.darker(130), 1.5), QBrush(code.color));
        node->setToolTip(Ui::plainToolTip(
            tr("%1: frequenza %2").arg(m_project->codePath(m_codeIds[i]), QString::number(int(freq)))));
        node->setZValue(1);
        auto *label = m_scene->addSimpleText(code.name);
        label->setBrush(palette().text());
        label->setPos(pos[i].x() - label->boundingRect().width() / 2, pos[i].y() + radius + 2);
        label->setZValue(2);
        ++m_mapNodes;
    }
}

void CooccurrenceDialog::activateCell(int row, int col)
{
    if (row < 0 || col < 0 || row >= m_codeIds.size() || col >= m_codeIds.size())
        return;
    const QList<Retrieval::Result> found =
        Cooccurrence::passages(options(), m_codeIds[row], m_codeIds[col], m_segments,
                               [this](qint64 id) { return m_project->documentText(id); });
    const QString names = row == col ? m_project->codePath(m_codeIds[row])
                                     : m_project->codePath(m_codeIds[row]) + QStringLiteral(" × ") +
                                           m_project->codePath(m_codeIds[col]);
    emit passagesRequested(found, tr("Co-occorrenza: %1").arg(names));
}

void CooccurrenceDialog::exportMatrix()
{
    const QString path = Ui::saveFileName(this, tr("Esporta"), m_project->name() + tr(" - co-occorrenze.csv"),
                                          tr("CSV per Excel (*.csv);;Pagina HTML (*.html)"));
    if (path.isEmpty())
        return;
    QString error;
    const bool ok = path.endsWith(QLatin1String(".html"), Qt::CaseInsensitive)
                        ? Exporters::writeHtmlTable(path, windowTitle(), m_exportTable, &error)
                        : Exporters::writeCsv(path, m_exportTable, &error);
    if (!ok)
        Ui::showError(this, error);
}

void CooccurrenceDialog::exportMapPng()
{
    const QString path = Ui::saveFileName(this, tr("Esporta la mappa"), m_project->name() + tr(" - mappa.png"),
                                          tr("Immagine PNG (*.png)"));
    if (path.isEmpty())
        return;
    QImage image(kMapSize.toSize() * 2, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    m_scene->render(&painter, QRectF(QPointF(), QSizeF(image.size())), m_scene->sceneRect());
    painter.end();
    if (!image.save(path))
        Ui::showError(this, tr("Impossibile salvare l'immagine."));
}

void CooccurrenceDialog::exportMapSvg()
{
    const QString path = Ui::saveFileName(this, tr("Esporta la mappa"), m_project->name() + tr(" - mappa.svg"),
                                          tr("Immagine SVG (*.svg)"));
    if (path.isEmpty())
        return;
    QSvgGenerator svg;
    svg.setFileName(path);
    svg.setSize(kMapSize.toSize());
    svg.setViewBox(QRectF(QPointF(), kMapSize));
    svg.setTitle(windowTitle());
    QPainter painter;
    if (!painter.begin(&svg)) {
        Ui::showError(this, tr("Impossibile salvare l'immagine."));
        return;
    }
    painter.fillRect(QRectF(QPointF(), kMapSize), Qt::white);
    m_scene->render(&painter, QRectF(QPointF(), kMapSize), m_scene->sceneRect());
    painter.end();
}
