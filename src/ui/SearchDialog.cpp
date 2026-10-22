#include "ui/SearchDialog.h"
#include "core/Project.h"
#include "core/TextUtils.h"
#include "ui/UiUtils.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>

SearchDialog::SearchDialog(Project *project, std::function<QList<qint64>()> activeDocuments,
                           std::function<qint64()> currentCode, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_activeDocuments(std::move(activeDocuments))
    , m_currentCode(std::move(currentCode))
    , m_query(new QLineEdit)
    , m_caseSensitive(new QCheckBox(tr("Maiuscole/minuscole")))
    , m_wholeWords(new QCheckBox(tr("Parole intere")))
    , m_regex(new QCheckBox(tr("Espressione regolare")))
    , m_scope(new QComboBox)
    , m_extent(new QComboBox)
    , m_summary(new QLabel)
    , m_table(new QTableWidget)
{
    setWindowTitle(tr("Ricerca lessicale"));
    resize(860, 600);

    m_query->setPlaceholderText(tr("Parola o frase da cercare (più termini alternativi separati da \" OR \")"));
    m_scope->addItem(tr("Tutti i documenti"));
    m_scope->addItem(tr("Solo documenti attivi"));
    auto *searchButton = new QPushButton(tr("Cerca"));
    searchButton->setDefault(true);
    connect(searchButton, &QPushButton::clicked, this, &SearchDialog::search);
    connect(m_query, &QLineEdit::returnPressed, this, &SearchDialog::search);

    auto *row1 = new QHBoxLayout;
    row1->addWidget(m_query, 1);
    row1->addWidget(searchButton);
    auto *row2 = new QHBoxLayout;
    row2->addWidget(m_caseSensitive);
    row2->addWidget(m_wholeWords);
    row2->addWidget(m_regex);
    row2->addStretch();
    row2->addWidget(m_scope);

    m_table->setColumnCount(3);
    m_table->setHorizontalHeaderLabels({tr("Documento"), tr("Posizione"), tr("Contesto")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setColumnWidth(0, 180);
    m_table->setColumnWidth(1, 80);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        if (row >= 0 && row < m_hits.size())
            emit hitActivated(m_hits[row].documentId, m_hits[row].start, m_hits[row].end);
    });

    auto *autoBox = new QGroupBox(tr("Codifica automatica dei risultati"));
    m_extent->addItem(tr("Solo la parola trovata"));
    m_extent->addItem(tr("La frase che la contiene"));
    m_extent->addItem(tr("Il paragrafo che la contiene"));
    auto *autoButton = new QPushButton(tr("Codifica tutti con il codice selezionato"));
    connect(autoButton, &QPushButton::clicked, this, &SearchDialog::autocode);
    auto *autoLayout = new QHBoxLayout(autoBox);
    autoLayout->addWidget(new QLabel(tr("Estensione:")));
    autoLayout->addWidget(m_extent);
    autoLayout->addStretch();
    autoLayout->addWidget(autoButton);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(row1);
    layout->addLayout(row2);
    layout->addWidget(m_summary);
    layout->addWidget(m_table, 1);
    layout->addWidget(autoBox);
}

void SearchDialog::search()
{
    m_hits.clear();
    const QString query = m_query->text().trimmed();
    if (query.isEmpty())
        return;

    QStringList terms = m_regex->isChecked() ? QStringList{query}
                                             : query.split(QStringLiteral(" OR "), Qt::SkipEmptyParts);
    QStringList patterns;
    for (QString t : terms) {
        t = t.trimmed();
        if (!m_regex->isChecked())
            t = QRegularExpression::escape(t);
        if (m_wholeWords->isChecked())
            t = QStringLiteral("\\b") + t + QStringLiteral("\\b");
        patterns << QStringLiteral("(?:") + t + QLatin1Char(')');
    }
    QRegularExpression re(patterns.join(QLatin1Char('|')),
                          m_caseSensitive->isChecked() ? QRegularExpression::UseUnicodePropertiesOption
                                                       : QRegularExpression::CaseInsensitiveOption |
                                                             QRegularExpression::UseUnicodePropertiesOption);
    if (!re.isValid()) {
        Ui::showError(this, tr("Espressione regolare non valida: %1").arg(re.errorString()));
        return;
    }

    QList<Document> docs = m_project->documents();
    if (m_scope->currentIndex() == 1) {
        const QList<qint64> active = m_activeDocuments();
        docs.removeIf([&](const Document &d) { return !active.contains(d.id); });
    }

    m_table->setRowCount(0);
    QSet<qint64> docsWithHits;
    QList<QStringList> rows;
    for (const Document &d : docs) {
        const QString text = m_project->documentText(d.id);
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            if (m.capturedLength() == 0)
                continue;
            m_hits.append({d.id, int(m.capturedStart()), int(m.capturedEnd()), m.captured()});
            docsWithHits.insert(d.id);
            rows << QStringList{d.name, QString::number(m.capturedStart()),
                                TextUtils::keywordInContext(text, int(m.capturedStart()), int(m.capturedEnd()))};
            if (m_hits.size() >= 20000)
                break;
        }
    }
    m_table->setRowCount(int(rows.size()));
    for (int r = 0; r < rows.size(); ++r)
        for (int c = 0; c < 3; ++c)
            m_table->setItem(r, c, new QTableWidgetItem(rows[r][c]));
    m_summary->setText(tr("%n risultati", nullptr, int(m_hits.size())) + QStringLiteral(" ") +
                       tr("in %n documenti", nullptr, int(docsWithHits.size())) +
                       QStringLiteral(" · ") + tr("doppio clic per aprire nel documento"));
}

void SearchDialog::autocode()
{
    if (m_hits.isEmpty()) {
        Ui::showError(this, tr("Esegui prima una ricerca."));
        return;
    }
    const qint64 codeId = m_currentCode();
    if (!codeId) {
        Ui::showError(this, tr("Seleziona prima un codice nel pannello Codici."));
        return;
    }
    if (!Ui::confirm(this, tr("Codifica automatica"),
                     tr("Codificare %n risultati con il codice \"%1\"?", nullptr, int(m_hits.size()))
                         .arg(m_project->codePath(codeId))))
        return;

    QHash<qint64, QString> texts;
    const qsizetype before = m_project->allSegments().size();
    int stale = 0;
    m_project->setUndoLabel(tr("Codifica automatica"));
    m_project->beginBatch();
    for (const Hit &h : m_hits) {
        if (!texts.contains(h.documentId))
            texts.insert(h.documentId, m_project->documentText(h.documentId));
        const QString &text = texts[h.documentId];
        if (text.mid(h.start, h.end - h.start) != h.match) {
            ++stale;
            continue;
        }
        auto range = std::pair<int, int>{h.start, h.end};
        if (m_extent->currentIndex() == 1)
            range = TextUtils::sentenceBounds(text, h.start, h.end);
        else if (m_extent->currentIndex() == 2)
            range = TextUtils::paragraphBounds(text, h.start, h.end);
        m_project->addSegment(h.documentId, codeId, range.first, range.second);
    }
    m_project->commitBatch();
    const int created = int(m_project->allSegments().size() - before);
    QString message = tr("%n nuovi segmenti codificati.", nullptr, created);
    if (stale)
        message += QLatin1Char('\n') + tr("%n risultati ignorati perché il testo è cambiato dopo la ricerca: "
                                          "ripeti la ricerca.", nullptr, stale);
    QMessageBox::information(this, tr("Codifica automatica"), message);
}
