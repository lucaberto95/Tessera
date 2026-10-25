#include "ui/MergeDialog.h"

#include "core/Project.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

MergeDialog::MergeDialog(Project *project, Merge::Source *source, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_source(source)
    , m_author(new QComboBox)
    , m_conflict(new QComboBox)
    , m_text(new QPlainTextEdit)
{
    setWindowTitle(tr("Unisci un altro progetto"));
    resize(560, 520);

    auto *intro = new QLabel(tr("Il lavoro del progetto «%1» viene aggiunto a questo progetto. Documenti, codici e "
                                "codifiche già presenti non si duplicano.")
                                 .arg(source->name()));
    intro->setTextFormat(Qt::PlainText); // the name comes from a file
    intro->setWordWrap(true);

    m_author->addItem(tr("Tutti gli autori"), QString());
    for (const QString &a : source->authors())
        m_author->addItem(a, a);
    m_conflict->addItem(tr("Importa come copia"), int(Merge::TextConflict::ImportCopy));
    m_conflict->addItem(tr("Salta il documento"), int(Merge::TextConflict::Skip));

    auto *form = new QFormLayout;
    form->addRow(tr("Importa le codifiche di:"), m_author);
    form->addRow(tr("Documento con testo diverso:"), m_conflict);

    m_text->setReadOnly(true);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    m_merge = buttons->addButton(tr("Unisci"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addWidget(m_text, 1);
    layout->addWidget(buttons);

    connect(m_author, &QComboBox::currentIndexChanged, this, &MergeDialog::refresh);
    connect(m_conflict, &QComboBox::currentIndexChanged, this, &MergeDialog::refresh);
    refresh();
}

Merge::Options MergeDialog::options() const
{
    Merge::Options o;
    o.onlyAuthor = m_author->currentData().toString();
    o.onTextConflict = Merge::TextConflict(m_conflict->currentData().toInt());
    return o;
}

void MergeDialog::setOptions(const Merge::Options &options)
{
    const int a = m_author->findData(options.onlyAuthor);
    m_author->setCurrentIndex(a < 0 ? 0 : a);
    m_conflict->setCurrentIndex(m_conflict->findData(int(options.onTextConflict)));
}

void MergeDialog::refresh()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_report = Merge::analyze(*m_project, *m_source, options());
    QApplication::restoreOverrideCursor();
    m_text->setPlainText(m_report.ok ? reportText(m_report, false) : m_report.error);
    m_merge->setEnabled(m_report.ok && m_report.changes() > 0);
}

QString MergeDialog::reportText(const Merge::Report &r, bool done)
{
    QStringList blocks;
    QStringList items;
    auto add = [&items](const QString &label, int count) {
        if (count > 0)
            items << QStringLiteral("  %1: %2").arg(label).arg(count);
    };
    // A heading is written only if something follows it.
    auto flush = [&](const QString &title) {
        if (!items.isEmpty())
            blocks << title + QLatin1Char('\n') + items.join(QLatin1Char('\n'));
        items.clear();
    };

    add(done ? tr("nuovi aggiunti") : tr("nuovi"), r.documentsNew);
    add(tr("già presenti"), r.documentsPresent);
    add(tr("con testo diverso"), r.documentsConflict);
    add(done ? tr("importati come copia") : tr("da importare come copia"), r.documentsCopied);
    add(tr("saltati"), r.documentsSkipped);
    add(tr("gruppi nuovi"), r.groupsNew);
    flush(tr("Documenti"));
    add(tr("nuovi"), r.codesNew);
    add(tr("già presenti"), r.codesPresent);
    flush(tr("Codici"));
    add(tr("nuove"), r.segmentsNew);
    add(tr("già presenti"), r.segmentsDuplicate);
    add(tr("in conflitto (commenti uniti)"), r.segmentsConflict);
    add(tr("di altri autori, ignorate"), r.segmentsFiltered);
    add(tr("non importate"), r.segmentsSkipped);
    flush(tr("Codifiche"));
    add(tr("nuovi"), r.memosNew);
    add(tr("già presenti"), r.memosPresent);
    add(tr("modificati (testi uniti)"), r.memosMerged);
    add(tr("non importati"), r.memosSkipped);
    flush(tr("Memo"));
    add(tr("collegamenti nuovi"), r.linksNew);
    add(tr("collegamenti già presenti"), r.linksDuplicate);
    add(tr("sintesi nuove"), r.summariesNew);
    add(tr("sintesi modificate (testi uniti)"), r.summariesMerged);
    add(tr("variabili nuove"), r.variablesNew);
    add(tr("valori di variabili importati"), r.variableValuesSet);
    add(tr("valori di variabili diversi (si tiene il proprio)"), r.variableValueConflicts);
    add(tr("set nuovi"), r.setsNew);
    add(tr("set ampliati"), r.setsExtended);
    add(tr("dizionari nuovi"), r.dictionariesNew);
    add(tr("tabelle riassuntive nuove"), r.summaryTablesNew);
    add(tr("mappe concettuali nuove"), r.conceptMapsNew);
    add(tr("domande di ricerca nuove"), r.questionsNew);
    add(tr("domande di ricerca con evidenze aggiunte"), r.questionsExtended);
    flush(tr("Altro"));

    if (!r.conflictDocuments.isEmpty())
        blocks << tr("Documenti con testo diverso: %1").arg(r.conflictDocuments.join(QStringLiteral(", ")));
    if (r.changes() == 0 && !done)
        blocks.prepend(tr("Non c'è nulla da aggiungere: il progetto contiene già tutto."));
    if (blocks.isEmpty())
        return tr("Non c'è nulla da aggiungere: il progetto contiene già tutto.");
    return blocks.join(QStringLiteral("\n\n"));
}