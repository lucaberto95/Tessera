#include "ui/SummaryGridDialog.h"
#include "core/Project.h"
#include "core/Summaries.h"
#include "ui/UiUtils.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QSet>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

SummaryGridDialog::SummaryGridDialog(Project *project, const QList<qint64> &activeDocuments,
                                     const QList<qint64> &activeCodes, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_docIds(activeDocuments)
    , m_codeIds(activeCodes)
    , m_grid(new QTableWidget)
    , m_segments(new QListWidget)
    , m_title(new QLabel)
    , m_status(new QLabel)
    , m_editor(new QPlainTextEdit)
    , m_saveTimer(new QTimer(this))
{
    setWindowTitle(tr("Griglia delle sintesi"));
    resize(1200, 640);
    if (m_docIds.isEmpty())
        for (const Document &d : m_project->documents())
            m_docIds << d.id;
    if (m_codeIds.isEmpty())
        for (const Code &c : m_project->codesInTreeOrder())
            m_codeIds << c.id;

    m_grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_grid->setSelectionMode(QAbstractItemView::SingleSelection);
    m_grid->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_grid->setToolTip(Ui::plainToolTip(
        tr("Righe: documenti. Colonne: codici. Il numero è quello dei segmenti; l'icona indica una sintesi scritta.")));

    m_segments->setWordWrap(true);
    m_segments->setSelectionMode(QAbstractItemView::NoSelection);
    m_segments->setAlternatingRowColors(true);

    m_title->setTextFormat(Qt::PlainText);
    m_title->setWordWrap(true);
    QFont bold = m_title->font();
    bold.setBold(true);
    m_title->setFont(bold);
    m_status->setTextFormat(Qt::PlainText);
    m_editor->setPlaceholderText(tr("Scrivi qui la sintesi di ciò che il documento dice su questo codice…"));
    m_editor->setEnabled(false);

    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(new QLabel(tr("Documenti × codici")));
    leftLayout->addWidget(m_grid);
    auto *middle = new QWidget;
    auto *middleLayout = new QVBoxLayout(middle);
    middleLayout->setContentsMargins(0, 0, 0, 0);
    middleLayout->addWidget(new QLabel(tr("Segmenti della cella")));
    middleLayout->addWidget(m_segments);
    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(m_title);
    rightLayout->addWidget(m_editor, 1);
    rightLayout->addWidget(m_status);

    auto *splitter = new QSplitter;
    splitter->addWidget(left);
    splitter->addWidget(middle);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 3);
    splitter->setStretchFactor(2, 3);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(splitter);

    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(1500);
    connect(m_saveTimer, &QTimer::timeout, this, &SummaryGridDialog::flush);
    connect(m_editor, &QPlainTextEdit::textChanged, this, [this] {
        if (m_loading || !m_doc)
            return;
        m_dirty = true;
        m_status->setText(tr("Modifica in corso…"));
        m_saveTimer->start();
    });
    connect(m_grid, &QTableWidget::currentCellChanged, this, [this] { onCurrentCellChanged(); });
    connect(m_project, &Project::summariesChanged, this, [this] { reloadFromProject(); });
    connect(m_project, &Project::segmentsChanged, this, [this] { reloadFromProject(); });
    connect(m_project, &Project::codesChanged, this, [this] { reloadFromProject(); });
    connect(m_project, &Project::documentsChanged, this, [this] { reloadFromProject(); });

    rebuildGrid();
}

void SummaryGridDialog::rebuildGrid()
{
    // Documents and codes deleted meanwhile leave the grid.
    QList<qint64> docs, codes;
    for (qint64 d : std::as_const(m_docIds))
        if (m_project->document(d).id)
            docs << d;
    for (qint64 c : std::as_const(m_codeIds))
        if (m_project->code(c).id)
            codes << c;
    m_docIds = docs;
    m_codeIds = codes;

    const bool wasBlocked = m_grid->blockSignals(true);
    m_grid->clear();
    m_grid->setRowCount(int(m_docIds.size()));
    m_grid->setColumnCount(int(m_codeIds.size()));
    for (int r = 0; r < m_docIds.size(); ++r)
        m_grid->setVerticalHeaderItem(r, new QTableWidgetItem(m_project->document(m_docIds[r]).name));
    for (int c = 0; c < m_codeIds.size(); ++c) {
        const Code code = m_project->code(m_codeIds[c]);
        auto *h = new QTableWidgetItem(m_project->codePath(code.id));
        h->setIcon(Ui::colorIcon(code.color));
        m_grid->setHorizontalHeaderItem(c, h);
    }
    const auto counts = m_project->segmentCountByCodeAndDocument(); // code -> document -> count
    QHash<qint64, QSet<qint64>> written;                            // document -> codes with a summary
    for (const Summary &s : m_project->summaries())
        if (!s.body.isEmpty())
            written[s.documentId].insert(s.codeId);
    for (int r = 0; r < m_docIds.size(); ++r)
        for (int c = 0; c < m_codeIds.size(); ++c) {
            const int n = counts.value(m_codeIds[c]).value(m_docIds[r]);
            const bool has = written.value(m_docIds[r]).contains(m_codeIds[c]);
            auto *item = new QTableWidgetItem(n ? QString::number(n) : QString());
            item->setTextAlignment(Qt::AlignCenter);
            if (n) {
                QColor bg = m_project->code(m_codeIds[c]).color;
                bg.setAlpha(70);
                item->setBackground(bg);
            }
            if (has)
                item->setIcon(Ui::memoIcon());
            item->setToolTip(Ui::plainToolTip(tr("%n segmenti", nullptr, n) + (has ? tr(" · sintesi scritta") : QString())));
            m_grid->setItem(r, c, item);
        }
    m_grid->resizeColumnsToContents();
    for (int c = 0; c < m_codeIds.size(); ++c)
        m_grid->setColumnWidth(c, qMin(m_grid->columnWidth(c), 160));
    const int row = int(m_docIds.indexOf(m_doc));
    const int col = int(m_codeIds.indexOf(m_code));
    if (row >= 0 && col >= 0)
        m_grid->setCurrentCell(row, col);
    m_grid->blockSignals(wasBlocked);
}

void SummaryGridDialog::selectCell(qint64 documentId, qint64 codeId)
{
    const int row = int(m_docIds.indexOf(documentId));
    const int col = int(m_codeIds.indexOf(codeId));
    if (row >= 0 && col >= 0)
        m_grid->setCurrentCell(row, col);
}

void SummaryGridDialog::onCurrentCellChanged()
{
    const int row = m_grid->currentRow();
    const int col = m_grid->currentColumn();
    flush(); // the summary of the cell being left is written first
    // The save rebuilds the grid, which put the current cell back on the old one.
    if (row != m_grid->currentRow() || col != m_grid->currentColumn()) {
        const bool wasBlocked = m_grid->blockSignals(true);
        m_grid->setCurrentCell(row, col);
        m_grid->blockSignals(wasBlocked);
    }
    if (row < 0 || col < 0) {
        m_doc = m_code = 0;
    } else {
        m_doc = m_docIds.value(row);
        m_code = m_codeIds.value(col);
    }
    m_dirty = false;
    loadCell();
}

void SummaryGridDialog::loadCell()
{
    m_segments->clear();
    m_loading = true;
    if (!m_doc || !m_code) {
        m_title->clear();
        m_editor->clear();
        m_editor->setEnabled(false);
        m_status->clear();
        m_loading = false;
        return;
    }
    m_title->setText(m_project->document(m_doc).name + QStringLiteral(" — ") + m_project->codePath(m_code));
    const QList<Segment> segs = m_project->segments({m_doc}, {m_code}, true);
    for (const Segment &s : segs) {
        QString text = Summaries::segmentText(*m_project, s);
        if (!s.comment.isEmpty())
            text += QStringLiteral("\n(") + s.comment + QLatin1Char(')');
        auto *item = new QListWidgetItem(text);
        m_segments->addItem(item);
    }
    if (segs.isEmpty())
        m_segments->addItem(tr("(nessun segmento: la sintesi può comunque essere scritta)"));
    const Summary s = m_project->summary(m_doc, m_code);
    m_editor->setEnabled(true);
    m_editor->setPlainText(s.body);
    m_status->setText(s.id ? tr("Salvata il %1").arg(s.modifiedAt.toLocalTime().toString(Qt::TextDate)) : QString());
    m_loading = false;
}

void SummaryGridDialog::reloadFromProject()
{
    if (m_project->isOpen() == false)
        return;
    rebuildGrid();
    if (m_doc && (!m_project->document(m_doc).id || !m_project->code(m_code).id)) {
        m_doc = m_code = 0;
        m_dirty = false;
        loadCell();
        return;
    }
    if (!m_doc)
        return;
    if (m_dirty) {
        // Do not overwrite what is being typed; refresh the segments only.
        const bool wasLoading = m_loading;
        const QString keep = m_editor->toPlainText();
        loadCell();
        m_loading = true;
        m_editor->setPlainText(keep);
        m_loading = wasLoading;
        return;
    }
    // Same text as stored: leave the cursor alone (this is the echo of our own save).
    const QString stored = m_project->summary(m_doc, m_code).body;
    if (stored != m_editor->toPlainText() && !(stored.isEmpty() && m_editor->toPlainText().trimmed().isEmpty()))
        loadCell();
}

QString SummaryGridDialog::editorText() const { return m_editor->toPlainText(); }

void SummaryGridDialog::setEditorText(const QString &text) { m_editor->setPlainText(text); }

int SummaryGridDialog::segmentRows() const { return m_segments->count(); }

void SummaryGridDialog::flush()
{
    m_saveTimer->stop();
    if (!m_dirty || !m_doc || !m_code || !m_project->isOpen())
        return;
    m_dirty = false;
    const QString text = m_editor->toPlainText();
    if (m_project->setSummary(m_doc, m_code, text))
        m_status->setText(tr("Salvata"));
    else
        m_status->setText(tr("Salvataggio non riuscito: %1").arg(m_project->lastError()));
}

void SummaryGridDialog::done(int result)
{
    flush();
    QDialog::done(result);
}

void SummaryGridDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && !isActiveWindow())
        flush();
    QDialog::changeEvent(event);
}
