#include "core/ResearchWorksheet.h"
#include "core/Project.h"
#include "core/Summaries.h"

#include <QCoreApplication>
#include <QDate>
#include <QLocale>

namespace ResearchWorksheet {

namespace {

Report::Run run(const QString &text, bool bold = false, bool italic = false, const QColor &color = QColor())
{
    return Report::Run{text, bold, italic, color};
}

Report::Block block(Report::BlockType type, const QList<Report::Run> &runs, int level = 1)
{
    Report::Block b;
    b.type = type;
    b.runs = runs;
    b.level = level;
    return b;
}

void addLines(const QString &text, Report::Document *out, bool italic = false)
{
    for (const QString &line : text.split(QLatin1Char('\n')))
        if (!line.trimmed().isEmpty())
            out->blocks << block(Report::BlockType::Paragraph, {run(line.trimmed(), false, italic)});
}

} // namespace

QString statusLabel(QuestionStatus status)
{
    switch (status) {
    case QuestionStatus::Open: return QCoreApplication::translate("ResearchWorksheet", "Aperta");
    case QuestionStatus::InProgress: return QCoreApplication::translate("ResearchWorksheet", "In lavorazione");
    case QuestionStatus::Answered: return QCoreApplication::translate("ResearchWorksheet", "Con risposta");
    }
    return {};
}

Report::Document build(const Project &project, const Options &options)
{
    using Report::BlockType;
    Report::Document doc;
    doc.title = options.title.trimmed().isEmpty() ? QCoreApplication::translate("ResearchWorksheet", "Domande di ricerca") : options.title.trimmed();
    doc.blocks << block(BlockType::Title, {run(doc.title)});
    doc.blocks << block(BlockType::Subtitle,
                        {run(QCoreApplication::translate("ResearchWorksheet", "Progetto %1 – %2")
                                 .arg(project.name(), QLocale().toString(QDate::currentDate(), QLocale::LongFormat)))});
    if (options.contents)
        doc.blocks << block(BlockType::Contents, {run(QCoreApplication::translate("ResearchWorksheet", "Indice"))});

    QList<ResearchQuestion> questions;
    for (const ResearchQuestion &q : project.researchQuestions())
        if (options.questions.isEmpty() || options.questions.contains(q.id))
            questions << q;
    if (questions.isEmpty())
        doc.blocks << block(BlockType::Paragraph, {run(QCoreApplication::translate("ResearchWorksheet", "Il progetto non ha domande di ricerca."), false, true)});

    for (const ResearchQuestion &q : questions) {
        Report::Block h = block(BlockType::Heading, {run(q.title.trimmed().isEmpty() ? QCoreApplication::translate("ResearchWorksheet", "Domanda senza titolo") : q.title.trimmed())}, 1);
        h.pageBreakBefore = &q != &questions.first();
        doc.blocks << h;
        doc.blocks << block(BlockType::Meta, {run(QCoreApplication::translate("ResearchWorksheet", "Stato: %1").arg(statusLabel(q.status)))});
        addLines(q.description, &doc);

        if (!q.themes.isEmpty()) {
            doc.blocks << block(BlockType::Heading, {run(QCoreApplication::translate("ResearchWorksheet", "Temi"))}, 2);
            for (const QString &theme : q.themes)
                doc.blocks << block(BlockType::Paragraph, {run(QStringLiteral("• ") + theme)}, 1);
        }

        const QList<QuestionEvidence> evidence = project.questionEvidence(q.id);
        if (!evidence.isEmpty()) {
            doc.blocks << block(BlockType::Heading, {run(QCoreApplication::translate("ResearchWorksheet", "Evidenze"))}, 2);
            const QHash<qint64, int> counts = project.segmentCountByCode();
            for (const QuestionEvidence &e : evidence) {
                switch (e.kind) {
                case EvidenceKind::Code: {
                    const Code c = project.code(e.refId);
                    doc.blocks << block(BlockType::Paragraph,
                                        {run(QStringLiteral("■ "), false, false, c.color),
                                         run(project.codePath(c.id), true),
                                         run(QStringLiteral("  (%1)").arg(QCoreApplication::translate("ResearchWorksheet", "%n segmenti", nullptr, counts.value(c.id))),
                                             false, false, QColor(Qt::darkGray))});
                    if (!c.description.trimmed().isEmpty())
                        doc.blocks << block(BlockType::Paragraph, {run(c.description.trimmed())}, 2);
                    break;
                }
                case EvidenceKind::Memo: {
                    const Memo m = project.memo(e.refId);
                    doc.blocks << block(BlockType::Paragraph,
                                        {run(QCoreApplication::translate("ResearchWorksheet", "Memo: "), true, true),
                                         run(m.title.trimmed().isEmpty() ? QCoreApplication::translate("ResearchWorksheet", "Memo senza titolo") : m.title.trimmed(), true)});
                    for (const QString &line : m.body.split(QLatin1Char('\n')))
                        if (!line.trimmed().isEmpty())
                            doc.blocks << block(BlockType::Paragraph, {run(line.trimmed(), false, true)}, 2);
                    break;
                }
                case EvidenceKind::Segment: {
                    const Segment s = project.segment(e.refId);
                    Report::Block quote = block(BlockType::Quote, {run(Summaries::segmentText(project, s).trimmed())});
                    quote.accent = project.code(s.codeId).color;
                    doc.blocks << quote;
                    QStringList parts{project.codePath(s.codeId), project.document(s.documentId).name};
                    if (s.kind == SegmentKind::Text)
                        parts << QStringLiteral("%1–%2").arg(s.start).arg(s.end);
                    doc.blocks << block(BlockType::Meta, {run(parts.join(QStringLiteral(" · ")))});
                    if (!s.comment.trimmed().isEmpty())
                        doc.blocks << block(BlockType::Paragraph, {run(QCoreApplication::translate("ResearchWorksheet", "Commento: "), true), run(s.comment.trimmed())});
                    break;
                }
                }
            }
        }

        if (!q.conclusions.trimmed().isEmpty()) {
            doc.blocks << block(BlockType::Heading, {run(QCoreApplication::translate("ResearchWorksheet", "Conclusioni"))}, 2);
            addLines(q.conclusions, &doc);
        }
        if (options.notes && !q.notes.trimmed().isEmpty()) {
            doc.blocks << block(BlockType::Heading, {run(QCoreApplication::translate("ResearchWorksheet", "Note di lavoro"))}, 2);
            addLines(q.notes, &doc, true);
        }
    }
    return doc;
}

} // namespace ResearchWorksheet
