#include "ui/AgreementDialog.h"

#include "core/Merge.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace {
QString number(double v, int decimals)
{
    return std::isnan(v) ? QStringLiteral("n.d.") : QString::number(v, 'f', decimals);
}
} // namespace

AgreementDialog::AgreementDialog(Project *project, const QList<qint64> &activeDocuments,
                                 const QList<qint64> &activeCodes, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_activeDocs(activeDocuments)
    , m_activeCodes(activeCodes)
    , m_source(new QComboBox)
    , m_coderA(new QComboBox)
    , m_coderB(new QComboBox)
    , m_fileLabel(new QLabel)
    , m_fileButton(new QPushButton(tr("Scegli il file…")))
    , m_level(new QComboBox)
    , m_overlap(new QSpinBox)
    , m_overlapLabel(new QLabel(tr("Sovrapposizione minima:")))
    , m_info(new QLabel)
    , m_resultTable(new QTableWidget)
    , m_disagreementTable(new QTableWidget)
{
    setWindowTitle(tr("Accordo tra codificatori"));
    resize(1000, 700);

    m_source->addItem(tr("Due autori di questo progetto"), 0);
    m_source->addItem(tr("Questo progetto e un altro file (.tsra)"), 1);
    m_fileLabel->setTextFormat(Qt::PlainText);
    m_fileButton->setVisible(false);
    m_fileLabel->setVisible(false);
    for (Agreement::Level l : {Agreement::Level::Existence, Agreement::Level::Frequency, Agreement::Level::Segments})
        m_level->addItem(Agreement::levelLabel(l), int(l));
    m_overlap->setRange(1, 100);
    m_overlap->setValue(90);
    m_overlap->setSuffix(QStringLiteral(" %"));
    m_overlap->setToolTip(tr("Due segmenti dello stesso codice concordano se la parte in comune è almeno questa "
                             "percentuale della loro unione"));
    m_info->setWordWrap(true);
    m_info->setTextFormat(Qt::PlainText);

    auto *sourceRow = new QHBoxLayout;
    sourceRow->addWidget(m_source);
    sourceRow->addWidget(m_fileButton);
    sourceRow->addWidget(m_fileLabel, 1);
    auto *coderRow = new QHBoxLayout;
    coderRow->addWidget(new QLabel(tr("Codificatore A:")));
    coderRow->addWidget(m_coderA, 1);
    coderRow->addWidget(new QLabel(tr("Codificatore B:")));
    coderRow->addWidget(m_coderB, 1);
    auto *levelRow = new QHBoxLayout;
    levelRow->addWidget(new QLabel(tr("Livello di confronto:")));
    levelRow->addWidget(m_level);
    levelRow->addSpacing(12);
    levelRow->addWidget(m_overlapLabel);
    levelRow->addWidget(m_overlap);
    levelRow->addStretch();
    auto *exportButton = new QPushButton(tr("Esporta…"));
    exportButton->setToolTip(tr("Esporta in Excel (.xlsx), CSV o HTML"));
    levelRow->addWidget(exportButton);

    m_resultTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultTable->horizontalHeader()->setStretchLastSection(false);
    m_resultTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_disagreementTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_disagreementTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_disagreementTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_disagreementTable->setToolTip(tr("Doppio clic per aprire il documento sul punto"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(sourceRow);
    layout->addLayout(coderRow);
    layout->addLayout(levelRow);
    layout->addWidget(m_info);
    layout->addWidget(m_resultTable, 2);
    auto *disagreementsLabel = new QLabel(tr("Disaccordi"));
    QFont bold = disagreementsLabel->font();
    bold.setBold(true);
    disagreementsLabel->setFont(bold);
    layout->addWidget(disagreementsLabel);
    layout->addWidget(m_disagreementTable, 3);

    connect(m_source, &QComboBox::currentIndexChanged, this, [this] {
        const bool file = m_source->currentData().toInt() == 1;
        m_fileButton->setVisible(file);
        m_fileLabel->setVisible(file);
        if (file && !m_other)
            chooseOtherFile();
        fillAuthors();
        rebuild();
    });
    connect(m_fileButton, &QPushButton::clicked, this, [this] {
        chooseOtherFile();
        fillAuthors();
        rebuild();
    });
    connect(m_coderA, &QComboBox::currentIndexChanged, this, &AgreementDialog::rebuild);
    connect(m_coderB, &QComboBox::currentIndexChanged, this, &AgreementDialog::rebuild);
    connect(m_level, &QComboBox::currentIndexChanged, this, &AgreementDialog::rebuild);
    connect(m_overlap, &QSpinBox::valueChanged, this, &AgreementDialog::rebuild);
    connect(exportButton, &QPushButton::clicked, this, &AgreementDialog::exportResults);
    connect(m_disagreementTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row < 0 || row >= m_shown.size())
            return;
        const Agreement::Disagreement &d = m_shown.at(row);
        const bool fromB = m_other && (d.kind == Agreement::DisagreementKind::OnlyB ||
                                       (d.kind == Agreement::DisagreementKind::Count && d.countB > d.countA));
        const bool text = d.segment.kind == SegmentKind::Text;
        emit disagreementActivated(d.documentId, text ? d.segment.start : -1, text ? d.segment.end : -1,
                                   fromB ? 0 : d.segment.id);
    });

    fillAuthors();
    rebuild();
}

AgreementDialog::~AgreementDialog() = default;

void AgreementDialog::setCoders(const QString &authorA, const QString &authorB)
{
    const QSignalBlocker a(m_coderA), b(m_coderB);
    m_coderA->setCurrentIndex(std::max(0, m_coderA->findData(authorA)));
    m_coderB->setCurrentIndex(std::max(0, m_coderB->findData(authorB)));
    rebuild();
}

bool AgreementDialog::setOtherProject(const QString &path, QString *error)
{
    auto source = std::make_unique<Merge::Source>();
    if (!source->load(path, error))
        return false;
    m_other = std::move(source);
    m_docMap = Agreement::mapDocuments(m_other->project(), *m_project);
    m_codeMap = Agreement::mapCodes(m_other->project(), *m_project);
    m_fileLabel->setText(m_other->name());
    const QSignalBlocker block(m_source);
    m_source->setCurrentIndex(1);
    m_fileButton->setVisible(true);
    m_fileLabel->setVisible(true);
    fillAuthors();
    rebuild();
    return true;
}

void AgreementDialog::setLevel(Agreement::Level level)
{
    m_level->setCurrentIndex(m_level->findData(int(level)));
}

void AgreementDialog::setMinOverlap(int percent)
{
    m_overlap->setValue(percent);
}

void AgreementDialog::chooseOtherFile()
{
    const QString path = Ui::openFileName(this, tr("Progetto dell'altro codificatore"),
                                          tr("Progetti Tessera e REFI-QDA (*.tsra *.qdpx);;Tutti i file (*)"));
    if (path.isEmpty()) {
        if (!m_other) {
            const QSignalBlocker block(m_source);
            m_source->setCurrentIndex(0);
            m_fileButton->setVisible(false);
            m_fileLabel->setVisible(false);
        }
        return;
    }
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto source = std::make_unique<Merge::Source>();
    const bool ok = source->load(path, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        Ui::showError(this, tr("Impossibile leggere il progetto:\n%1").arg(error));
        return;
    }
    m_other = std::move(source);
    m_docMap = Agreement::mapDocuments(m_other->project(), *m_project);
    m_codeMap = Agreement::mapCodes(m_other->project(), *m_project);
    m_fileLabel->setText(m_other->name());
}

void AgreementDialog::fillAuthors()
{
    const QSignalBlocker a(m_coderA), b(m_coderB);
    m_coderA->clear();
    m_coderB->clear();
    const bool file = m_source->currentData().toInt() == 1;
    if (file) {
        m_coderA->addItem(tr("Tutti gli autori"), QString());
        for (const QString &s : Agreement::authors(*m_project))
            m_coderA->addItem(s, s);
        m_coderB->addItem(tr("Tutti gli autori"), QString());
        if (m_other)
            for (const QString &s : Agreement::authors(m_other->project()))
                m_coderB->addItem(s, s);
    } else {
        const QStringList names = Agreement::authors(*m_project);
        for (const QString &s : names) {
            m_coderA->addItem(s, s);
            m_coderB->addItem(s, s);
        }
        if (names.size() > 1)
            m_coderB->setCurrentIndex(1);
    }
}

QString AgreementDialog::excerpt(const Segment &segment, qint64 documentId, bool fromOther) const
{
    QString text;
    if (segment.kind == SegmentKind::Text) {
        if (fromOther) {
            qint64 otherId = 0;
            for (auto it = m_docMap.cbegin(); it != m_docMap.cend(); ++it)
                if (it.value() == documentId)
                    otherId = it.key();
            text = m_other->project().documentText(otherId).mid(segment.start, segment.length());
        } else {
            text = m_project->documentText(documentId).mid(segment.start, segment.length());
        }
    } else if (!segment.areaText.isEmpty()) {
        text = segment.areaText;
    } else {
        return tr("(area o intervallo di tempo)");
    }
    text.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\f'), QLatin1Char(' '));
    text = text.simplified();
    if (text.size() > 200)
        text = text.left(200) + QStringLiteral("…");
    return text;
}

void AgreementDialog::rebuild()
{
    const bool file = m_source->currentData().toInt() == 1 && m_other;
    const auto level = Agreement::Level(m_level->currentData().toInt());
    m_overlap->setEnabled(level == Agreement::Level::Segments);
    m_overlapLabel->setEnabled(level == Agreement::Level::Segments);

    // Documents and codes: the active ones, or all of them.
    m_docs.clear();
    m_codes.clear();
    QList<qint64> docs = m_activeDocs;
    if (docs.isEmpty())
        for (const Document &d : m_project->documents())
            docs << d.id;
    if (file) {
        const QList<qint64> common = m_docMap.values();
        for (qint64 d : docs)
            if (common.contains(d))
                m_docs << d;
    } else {
        m_docs = docs;
    }
    m_codes = m_activeCodes;
    if (m_codes.isEmpty())
        for (const Code &c : m_project->codesInTreeOrder())
            m_codes << c.id;

    QList<Segment> a = Agreement::byAuthor(m_project->allSegments(true), m_coderA->currentData().toString());
    QList<Segment> b;
    if (file) {
        b = Agreement::remap(m_other->project().allSegments(true), m_docMap, m_codeMap);
        b = Agreement::byAuthor(b, m_coderB->currentData().toString());
        for (Segment &s : b)
            s.id = 0; // ids of the other project mean nothing here
    } else {
        b = Agreement::byAuthor(m_project->allSegments(true), m_coderB->currentData().toString());
    }

    Agreement::Options o;
    o.level = level;
    o.documents = m_docs;
    o.codes = m_codes;
    o.minOverlap = m_overlap->value() / 100.0;
    m_result = Agreement::compute(o, a, b);

    if (m_source->currentData().toInt() == 1 && !m_other)
        m_info->setText(tr("Scegli il file del progetto con cui confrontare."));
    else
        m_info->setText(tr("%n documenti, %1 codici. Attiva documenti e codici per restringere il confronto "
                           "(nessuno attivo = tutti).",
                           nullptr, int(m_docs.size()))
                            .arg(m_codes.size()));

    // Results
    const bool segments = level == Agreement::Level::Segments;
    m_resultExport.clear();
    m_resultExport << QStringList{tr("Codice"), segments ? tr("Segmenti") : tr("Unità"), tr("Concordi"),
                                  tr("Accordo %"), tr("Kappa di Cohen"), tr("Kappa di Brennan-Prediger")};
    for (const Agreement::CodeResult &r : m_result.perCode)
        m_resultExport << QStringList{m_project->codePath(r.codeId), QString::number(r.units),
                                      QString::number(r.agreements), number(r.percent, 1), number(r.kappa, 3),
                                      number(r.kappaBP, 3)};
    const Agreement::CodeResult &t = m_result.total;
    m_resultExport << QStringList{tr("Totale"), QString::number(t.units), QString::number(t.agreements),
                                  number(t.percent, 1), number(t.kappa, 3), number(t.kappaBP, 3)};
    m_resultTable->clear();
    m_resultTable->setColumnCount(6);
    m_resultTable->setRowCount(m_resultExport.size() - 1);
    m_resultTable->setHorizontalHeaderLabels(m_resultExport.first());
    for (int r = 1; r < m_resultExport.size(); ++r)
        for (int c = 0; c < 6; ++c) {
            auto *item = new QTableWidgetItem(m_resultExport.at(r).at(c));
            if (c > 0)
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (r == m_resultExport.size() - 1) {
                QFont f = item->font();
                f.setBold(true);
                item->setFont(f);
            }
            m_resultTable->setItem(r - 1, c, item);
        }
    m_resultTable->resizeColumnsToContents();
    m_resultTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    // Disagreements
    m_shown = m_result.disagreements;
    m_disagreementExport.clear();
    m_disagreementExport << QStringList{tr("Documento"), tr("Codice"), tr("Disaccordo"), tr("Segmenti A"),
                                        tr("Segmenti B"), tr("Testo")};
    for (const Agreement::Disagreement &d : m_shown) {
        QString kind;
        switch (d.kind) {
        case Agreement::DisagreementKind::OnlyA:
            kind = tr("Solo A");
            break;
        case Agreement::DisagreementKind::OnlyB:
            kind = tr("Solo B");
            break;
        case Agreement::DisagreementKind::Count:
            kind = tr("Frequenza diversa");
            break;
        }
        const bool fromB = file && (d.kind == Agreement::DisagreementKind::OnlyB ||
                                    (d.kind == Agreement::DisagreementKind::Count && d.countB > d.countA));
        m_disagreementExport << QStringList{m_project->document(d.documentId).name, m_project->codePath(d.codeId),
                                            kind, QString::number(d.countA), QString::number(d.countB),
                                            excerpt(d.segment, d.documentId, fromB)};
    }
    m_disagreementTable->clear();
    m_disagreementTable->setColumnCount(6);
    m_disagreementTable->setRowCount(m_shown.size());
    m_disagreementTable->setHorizontalHeaderLabels(m_disagreementExport.first());
    for (int r = 0; r < m_shown.size(); ++r)
        for (int c = 0; c < 6; ++c)
            m_disagreementTable->setItem(r, c, new QTableWidgetItem(m_disagreementExport.at(r + 1).at(c)));
    m_disagreementTable->resizeColumnsToContents();
    m_disagreementTable->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
}

void AgreementDialog::exportResults()
{
    const QString path = Ui::saveFileName(this, tr("Esporta"),
                                          m_project->name() + QStringLiteral(" - accordo.xlsx"),
                                          tr("Cartella di lavoro Excel (*.xlsx);;CSV per Excel (*.csv);;"
                                             "Pagina HTML (*.html)"));
    if (path.isEmpty())
        return;
    if (Ui::isXlsxPath(path)) {
        Xlsx::Sheet disagreements = Exporters::tableSheet(tr("Disaccordi"), m_disagreementExport);
        disagreements.textColumns = {0, 1, 2, 5};
        Ui::saveXlsx(this, path, {Exporters::tableSheet(tr("Accordo"), m_resultExport), disagreements});
        return;
    }
    // One table: results, an empty row, then the disagreements.
    Exporters::Table all = m_resultExport;
    all << QStringList() << m_disagreementExport;
    QString error;
    const bool ok = path.endsWith(QLatin1String(".html"), Qt::CaseInsensitive)
                        ? Exporters::writeHtmlTable(path, windowTitle(), all, &error)
                        : Exporters::writeCsv(path, all, &error);
    if (!ok)
        Ui::showError(this, error);
}
