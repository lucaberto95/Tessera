#include "ui/SegmentsPanel.h"
#include "core/Exporters.h"
#include "core/Project.h"
#include "core/TextUtils.h"
#include "ui/UiUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
enum Column { DocumentColumn, CodeColumn, TextColumn, WeightColumn, CommentColumn, ColumnCount };
} // namespace

SegmentsPanel::SegmentsPanel(Project *project, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_summary(new QLabel)
    , m_filter(new QLineEdit)
    , m_table(new QTableWidget)
    , m_back(new QToolButton)
    , m_codeResults(new QToolButton)
{
    m_back->setText(tr("Torna al recupero semplice"));
    m_back->setVisible(false);
    connect(m_back, &QToolButton::clicked, this, &SegmentsPanel::clearRetrieval);
    m_codeResults->setText(tr("Codifica i risultati con…"));
    m_codeResults->setVisible(false);
    connect(m_codeResults, &QToolButton::clicked, this, &SegmentsPanel::codeResultsInteractive);

    m_filter->setPlaceholderText(tr("Filtra nel testo dei segmenti…"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setMaximumWidth(280);
    connect(m_filter, &QLineEdit::textChanged, this, &SegmentsPanel::applyFilter);

    auto *html = new QToolButton;
    html->setText(tr("Esporta HTML"));
    connect(html, &QToolButton::clicked, this, &SegmentsPanel::exportHtml);
    auto *xlsx = new QToolButton;
    xlsx->setText(tr("Esporta Excel (.xlsx)"));
    connect(xlsx, &QToolButton::clicked, this, &SegmentsPanel::exportXlsx);
    auto *csv = new QToolButton;
    csv->setText(tr("Esporta Excel (CSV)"));
    connect(csv, &QToolButton::clicked, this, &SegmentsPanel::exportCsv);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    bar->addWidget(m_summary, 1);
    bar->addWidget(m_back);
    bar->addWidget(m_codeResults);
    bar->addWidget(m_filter);
    bar->addWidget(html);
    bar->addWidget(xlsx);
    bar->addWidget(csv);

    m_table->setColumnCount(ColumnCount);
    m_table->setHorizontalHeaderLabels({tr("Documento"), tr("Codice"), tr("Segmento"), tr("Peso"), tr("Commento")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setWordWrap(true);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(DocumentColumn, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(CodeColumn, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(TextColumn, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(WeightColumn, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(CommentColumn, QHeaderView::Interactive);
    m_table->setColumnWidth(DocumentColumn, 160);
    m_table->setColumnWidth(CodeColumn, 180);
    m_table->setColumnWidth(CommentColumn, 180);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        if (row >= 0 && row < m_segments.size()) {
            const Segment &s = m_segments[row];
            emit segmentActivated(s.documentId, s.start, s.end, s.id);
        }
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addLayout(bar);
    layout->addWidget(m_table);
    refresh();
}

void SegmentsPanel::setActivation(const QList<qint64> &documentIds, const QList<qint64> &codeIds)
{
    m_documentIds = documentIds;
    m_codeIds = codeIds;
    refresh();
}

void SegmentsPanel::refresh()
{
    if (m_hasQuery)
        rebuildSegmentsFromQuery();
    else
        m_segments = m_project->isOpen() ? m_project->segments(m_documentIds, m_codeIds) : QList<Segment>{};
    m_back->setVisible(m_hasQuery);
    m_codeResults->setVisible(m_hasQuery);

    QHash<qint64, QString> docNames;
    QHash<qint64, QString> texts;
    QHash<qint64, Code> codes;
    if (m_project->isOpen()) {
        for (const Document &d : m_project->documents())
            docNames.insert(d.id, d.name);
        for (const Code &c : m_project->codes())
            codes.insert(c.id, c);
    }

    m_table->setUpdatesEnabled(false);
    m_table->clearContents();
    m_table->setRowCount(int(m_segments.size()));
    for (int row = 0; row < m_segments.size(); ++row) {
        const Segment &s = m_segments[row];
        if (!texts.contains(s.documentId))
            texts.insert(s.documentId, m_project->documentText(s.documentId));
        auto *doc = new QTableWidgetItem(docNames.value(s.documentId));
        doc->setToolTip(tr("Posizione %1–%2").arg(s.start).arg(s.end));
        QString codeLabel = m_project->codePath(s.codeId);
        if (m_hasQuery && row < m_results.size() && m_results[row].codeIds.size() > 1) {
            QStringList paths;
            for (qint64 c : m_results[row].codeIds)
                paths << m_project->codePath(c);
            codeLabel = paths.join(QStringLiteral(" + "));
        }
        auto *code = new QTableWidgetItem(Ui::colorIcon(codes.value(s.codeId).color), codeLabel);
        auto *text = new QTableWidgetItem(TextUtils::excerpt(texts.value(s.documentId), s.start, s.end, 600));
        text->setToolTip(Ui::plainToolTip(texts.value(s.documentId).mid(s.start, qMin(s.length(), 3000))));
        auto *weight = new QTableWidgetItem(s.weight ? QString::number(s.weight) : QString());
        weight->setTextAlignment(Qt::AlignCenter);
        auto *comment = new QTableWidgetItem(s.comment);
        m_table->setItem(row, DocumentColumn, doc);
        m_table->setItem(row, CodeColumn, code);
        m_table->setItem(row, TextColumn, text);
        m_table->setItem(row, WeightColumn, weight);
        m_table->setItem(row, CommentColumn, comment);
    }
    m_table->setUpdatesEnabled(true);

    if (m_hasQuery)
        m_summary->setText(tr("<b>%n risultati</b>", nullptr, int(m_segments.size())) + QStringLiteral(" · ") +
                           (m_fixed ? m_fixedLabel.toHtmlEscaped() // contains code names; the label is rich text
                                    : tr("Recupero complesso: %1").arg(Retrieval::modeLabel(m_query.mode))) +
                           QStringLiteral(" · ") +
                           (m_documentIds.isEmpty() ? tr("tutti i documenti")
                                                    : tr("%n documenti attivi", nullptr, int(m_documentIds.size()))));
    else if (m_codeIds.isEmpty())
        m_summary->setText(tr("Segmenti recuperati: attiva uno o più codici (caselle di spunta) per vederne i segmenti."));
    else
        m_summary->setText(tr("<b>%n segmenti</b>", nullptr, int(m_segments.size())) + QStringLiteral(" · ") +
                           tr("%n codici attivi", nullptr, int(m_codeIds.size())) + QStringLiteral(" · ") +
                           (m_documentIds.isEmpty() ? tr("tutti i documenti")
                                                    : tr("%n documenti attivi", nullptr, int(m_documentIds.size()))));
    applyFilter();
}

void SegmentsPanel::applyFilter()
{
    const QString f = m_filter->text().trimmed();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        bool visible = true;
        if (!f.isEmpty()) {
            visible = false;
            for (int col = 0; col < ColumnCount && !visible; ++col)
                if (QTableWidgetItem *it = m_table->item(row, col))
                    visible = it->text().contains(f, Qt::CaseInsensitive);
        }
        m_table->setRowHidden(row, !visible);
    }
}

void SegmentsPanel::setRetrieval(const Retrieval::Query &query)
{
    m_query = query;
    m_hasQuery = true;
    m_fixed = false;
    refresh();
}

void SegmentsPanel::setPassages(const QList<Retrieval::Result> &passages, const QString &label)
{
    m_fixedResults = passages;
    m_fixedLabel = label;
    m_fixed = true;
    m_hasQuery = true;
    refresh();
}

void SegmentsPanel::clearRetrieval()
{
    m_hasQuery = false;
    m_fixed = false;
    m_results.clear();
    refresh();
}

void SegmentsPanel::rebuildSegmentsFromQuery()
{
    m_segments.clear();
    m_results.clear();
    if (!m_project->isOpen())
        return;
    QHash<qint64, Segment> byId;
    if (m_fixed) {
        for (const Segment &s : m_project->allSegments())
            byId.insert(s.id, s);
        m_results = m_fixedResults;
    } else {
        QList<qint64> codes = m_query.codesA;
        for (qint64 c : std::as_const(m_query.codesB))
            if (!codes.contains(c))
                codes << c;
        const QList<Segment> source = m_project->segments(m_documentIds, codes);
        for (const Segment &s : source)
            byId.insert(s.id, s);
        m_results = Retrieval::run(m_query, source, [this](qint64 id) { return m_project->documentText(id); });
    }
    for (const Retrieval::Result &r : std::as_const(m_results)) {
        // Whole stored segments keep their weight/comment; new intervals have none.
        Segment s = byId.value(r.segmentId);
        s.documentId = r.documentId;
        s.start = r.start;
        s.end = r.end;
        s.codeId = r.codeIds.value(0);
        m_segments << s;
    }
}

qint64 SegmentsPanel::codeResults(const QString &codeName)
{
    if (!m_hasQuery || m_results.isEmpty() || codeName.trimmed().isEmpty())
        return 0;
    m_project->setUndoLabel(tr("Codifica i risultati"));
    m_project->beginBatch();
    const qint64 code = m_project->addCode(0, codeName.trimmed(), TextUtils::paletteColor(int(m_project->codes().size())));
    if (code) {
        for (const Retrieval::Result &r : std::as_const(m_results))
            m_project->addSegment(r.documentId, code, r.start, r.end);
    }
    m_project->commitBatch();
    return code;
}

void SegmentsPanel::codeResultsInteractive()
{
    const QString name = Ui::askText(this, tr("Codifica i risultati"), tr("Nome del nuovo codice:"),
                                     m_fixed ? m_fixedLabel : Retrieval::modeLabel(m_query.mode));
    if (!name.trimmed().isEmpty() && !codeResults(name))
        Ui::showError(this, tr("Non è stato possibile creare il codice."));
}

void SegmentsPanel::contextMenuEvent(QContextMenuEvent *event)
{
    const QPoint pos = m_table->viewport()->mapFromGlobal(event->globalPos());
    const int row = m_table->rowAt(pos.y());
    if (row < 0 || !m_table->viewport()->rect().contains(pos))
        return;
    if (row >= m_segments.size())
        return;
    const Segment seg = m_segments[row];

    QMenu menu(this);
    menu.addAction(tr("Mostra nel documento"), this,
                   [this, seg] { emit segmentActivated(seg.documentId, seg.start, seg.end, seg.id); });
    menu.addAction(tr("Copia testo"), this, [this, seg] {
        QApplication::clipboard()->setText(m_project->documentText(seg.documentId).mid(seg.start, seg.length()));
    });
    if (!seg.id) { // a new interval from a complex retrieval: nothing stored to edit
        menu.exec(event->globalPos());
        return;
    }
    menu.addSeparator();
    menu.addAction(tr("Commento…"), this, [this, seg] {
        bool ok = false;
        const QString c =
            QInputDialog::getMultiLineText(this, tr("Commento al segmento"), tr("Commento:"), seg.comment, &ok);
        if (ok)
            m_project->setSegmentComment(seg.id, c.trimmed());
    });
    menu.addAction(tr("Peso…"), this, [this, seg] {
        bool ok = false;
        const int w = QInputDialog::getInt(this, tr("Peso del segmento"), tr("Peso (0-100):"), seg.weight, 0, 100, 1, &ok);
        if (ok)
            m_project->setSegmentWeight(seg.id, w);
    });
    menu.addAction(Ui::memoIcon(), tr("Memo…"), this,
                   [this, seg] { Ui::editMemo(this, m_project, int(MemoTarget::Segment), seg.id); });
    menu.addSeparator();
    menu.addAction(tr("Rimuovi codifica"), this, [this, seg] {
        if (Ui::confirm(this, tr("Rimuovi codifica"), tr("Rimuovere questa codifica?")))
            m_project->deleteSegment(seg.id);
    });
    menu.exec(event->globalPos());
}

void SegmentsPanel::exportHtml()
{
    if (m_segments.isEmpty()) {
        Ui::showError(this, tr("Non ci sono segmenti recuperati da esportare."));
        return;
    }
    const QString path = Ui::saveFileName(this, tr("Esporta segmenti"), m_project->name() + tr(" - segmenti.html"),
                                          tr("Pagina HTML (*.html)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!Exporters::exportSegmentsHtml(*m_project, m_segments, path, &error))
        Ui::showError(this, error);
}

void SegmentsPanel::exportCsv()
{
    if (m_segments.isEmpty()) {
        Ui::showError(this, tr("Non ci sono segmenti recuperati da esportare."));
        return;
    }
    const QString path = Ui::saveFileName(this, tr("Esporta segmenti"), m_project->name() + tr(" - segmenti.csv"),
                                          tr("CSV per Excel (*.csv)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!Exporters::writeCsv(path, Exporters::segmentsTable(*m_project, m_segments), &error))
        Ui::showError(this, error);
}

void SegmentsPanel::exportXlsx()
{
    if (m_segments.isEmpty()) {
        Ui::showError(this, tr("Non ci sono segmenti recuperati da esportare."));
        return;
    }
    const QString path = Ui::saveFileName(this, tr("Esporta segmenti"), m_project->name() + tr(" - segmenti.xlsx"),
                                          tr("Cartella di lavoro Excel (*.xlsx)"));
    if (!path.isEmpty())
        Ui::saveXlsx(this, path, {Exporters::segmentsSheet(*m_project, m_segments)});
}
