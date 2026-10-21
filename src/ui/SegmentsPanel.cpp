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
constexpr int SegmentIdRole = Qt::UserRole;
} // namespace

SegmentsPanel::SegmentsPanel(Project *project, QWidget *parent)
    : QWidget(parent)
    , m_project(project)
    , m_summary(new QLabel)
    , m_filter(new QLineEdit)
    , m_table(new QTableWidget)
{
    m_filter->setPlaceholderText(tr("Filtra nel testo dei segmenti…"));
    m_filter->setClearButtonEnabled(true);
    m_filter->setMaximumWidth(280);
    connect(m_filter, &QLineEdit::textChanged, this, &SegmentsPanel::applyFilter);

    auto *html = new QToolButton;
    html->setText(tr("Esporta HTML"));
    connect(html, &QToolButton::clicked, this, &SegmentsPanel::exportHtml);
    auto *csv = new QToolButton;
    csv->setText(tr("Esporta Excel (CSV)"));
    connect(csv, &QToolButton::clicked, this, &SegmentsPanel::exportCsv);

    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(6, 4, 6, 4);
    bar->addWidget(m_summary, 1);
    bar->addWidget(m_filter);
    bar->addWidget(html);
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
        const qint64 id = segmentIdAtRow(row);
        for (const Segment &s : m_segments)
            if (s.id == id)
                emit segmentActivated(s.documentId, s.start, s.end, s.id);
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
    m_segments = m_project->isOpen() ? m_project->segments(m_documentIds, m_codeIds) : QList<Segment>{};

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
        doc->setData(SegmentIdRole, s.id);
        doc->setToolTip(tr("Posizione %1–%2").arg(s.start).arg(s.end));
        auto *code = new QTableWidgetItem(Ui::colorIcon(codes.value(s.codeId).color), m_project->codePath(s.codeId));
        auto *text = new QTableWidgetItem(TextUtils::excerpt(texts.value(s.documentId), s.start, s.end, 600));
        text->setToolTip(texts.value(s.documentId).mid(s.start, qMin(s.length(), 3000)));
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

    if (m_codeIds.isEmpty())
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

qint64 SegmentsPanel::segmentIdAtRow(int row) const
{
    QTableWidgetItem *it = m_table->item(row, DocumentColumn);
    return it ? it->data(SegmentIdRole).toLongLong() : 0;
}

void SegmentsPanel::contextMenuEvent(QContextMenuEvent *event)
{
    const QPoint pos = m_table->viewport()->mapFromGlobal(event->globalPos());
    const int row = m_table->rowAt(pos.y());
    if (row < 0 || !m_table->viewport()->rect().contains(pos))
        return;
    const qint64 id = segmentIdAtRow(row);
    Segment seg;
    for (const Segment &s : m_segments)
        if (s.id == id)
            seg = s;
    if (!seg.id)
        return;

    QMenu menu(this);
    menu.addAction(tr("Mostra nel documento"), this,
                   [this, seg] { emit segmentActivated(seg.documentId, seg.start, seg.end, seg.id); });
    menu.addAction(tr("Copia testo"), this, [this, seg] {
        QApplication::clipboard()->setText(m_project->documentText(seg.documentId).mid(seg.start, seg.length()));
    });
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
    menu.addAction(Ui::memoIcon(), tr("Memo…"), this, [this, seg] {
        Ui::editMemo(this, m_project, int(MemoTarget::Segment), seg.id,
                     tr("segmento \"%1\"").arg(TextUtils::excerpt(m_project->documentText(seg.documentId), seg.start,
                                                                  seg.end, 80)));
    });
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
