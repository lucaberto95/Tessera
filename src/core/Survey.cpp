#include "core/Survey.h"
#include "core/Exporters.h"
#include "core/Project.h"
#include "core/TextUtils.h"
#include "core/Xlsx.h"

#include <QFileInfo>
#include <QHash>
#include <QObject>
#include <QSet>

#include <algorithm>

namespace Survey {

namespace {

bool isEmptyRow(const QStringList &row)
{
    return std::all_of(row.cbegin(), row.cend(), [](const QString &s) { return s.trimmed().isEmpty(); });
}

QString cell(const QStringList &row, int column)
{
    return column < row.size() ? row.at(column) : QString();
}

bool isIdHeader(const QString &name)
{
    static const QStringList names{
        QStringLiteral("id"), QStringLiteral("id rispondente"), QStringLiteral("rispondente"),
        QStringLiteral("codice rispondente"), QStringLiteral("respondent"), QStringLiteral("respondent id"),
        QStringLiteral("respondentid"), QStringLiteral("response id"), QStringLiteral("responseid"),
        QStringLiteral("participant"), QStringLiteral("partecipante"), QStringLiteral("id partecipante")};
    return names.contains(name.trimmed().toLower());
}

// Free text worth coding: long or wordy answers that are not a handful of repeated categories.
bool looksLikeOpenAnswer(const QStringList &values)
{
    qsizetype totalLength = 0;
    qsizetype maxWords = 0;
    QSet<QString> distinct;
    for (const QString &v : values) {
        totalLength += v.size();
        maxWords = qMax(maxWords, v.split(QLatin1Char(' '), Qt::SkipEmptyParts).size());
        distinct.insert(v.toLower());
    }
    if (values.size() >= 4 && distinct.size() * 2 <= values.size())
        return false;
    return totalLength >= 20 * values.size() || maxWords >= 5;
}

} // namespace

Table readTable(const QString &path, QString *error)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    Table table;
    error->clear();
    if (suffix == QLatin1String("xlsx") || suffix == QLatin1String("xlsm")) {
        table = Xlsx::readFirstSheet(path, error);
    } else if (suffix == QLatin1String("xls")) {
        *error = QObject::tr("Il formato .xls (Excel 97-2003) non è supportato: salvalo come .xlsx o .csv");
        return {};
    } else {
        table = Exporters::readCsv(path, error);
    }
    if (!error->isEmpty())
        return {};
    while (!table.isEmpty() && isEmptyRow(table.first()))
        table.removeFirst();
    while (!table.isEmpty() && isEmptyRow(table.last()))
        table.removeLast();
    if (table.isEmpty())
        *error = QObject::tr("Il file %1 non contiene dati").arg(QFileInfo(path).fileName());
    return table;
}

QList<Column> proposeColumns(const Project &project, const Table &table)
{
    QList<Column> columns;
    if (table.isEmpty())
        return columns;
    qsizetype count = 0;
    for (const QStringList &row : table)
        count = qMax(count, row.size());

    QHash<QString, VariableType> existing;
    for (const Variable &v : project.variables())
        existing.insert(v.name, v.type);

    bool hasId = false;
    for (int c = 0; c < count; ++c) {
        Column col;
        col.name = cell(table.first(), c).simplified();
        if (col.name.isEmpty())
            col.name = QObject::tr("Colonna %1").arg(c + 1);
        QStringList values;
        for (qsizetype r = 1; r < table.size(); ++r) {
            const QString v = cell(table[r], c).trimmed();
            if (!v.isEmpty())
                values << v;
        }
        col.samples = values.mid(0, 3);
        col.existingVariable = existing.contains(col.name);

        if (values.isEmpty()) {
            col.role = Role::Ignore;
        } else if (!hasId && isIdHeader(col.name)) {
            col.role = Role::Id;
            hasId = true;
        } else if (col.existingVariable) {
            col.role = Role::Variable;
        } else {
            col.type = Exporters::inferVariableType(values);
            col.role = (col.type == VariableType::Text && looksLikeOpenAnswer(values)) ? Role::OpenAnswer
                                                                                       : Role::Variable;
        }
        if (col.existingVariable)
            col.type = existing.value(col.name);
        columns << col;
    }
    return columns;
}

QString respondentText(const QList<Column> &columns, const QStringList &row, QList<AnswerRange> *answerRanges)
{
    QString text;
    answerRanges->clear();
    for (int c = 0; c < columns.size(); ++c) {
        if (columns[c].role != Role::OpenAnswer)
            continue;
        const QString answer = TextUtils::normalizeText(cell(row, c)).trimmed();
        if (answer.isEmpty())
            continue;
        if (!text.isEmpty())
            text += QStringLiteral("\n\n");
        text += columns[c].name + QLatin1Char('\n');
        AnswerRange range;
        range.column = c;
        range.start = int(text.size());
        text += answer;
        range.end = int(text.size());
        *answerRanges << range;
    }
    return text;
}

bool importSurvey(Project &project, const Table &table, const Options &options, Report *report, QString *error)
{
    *report = {};
    const QList<Column> &columns = options.columns;
    int idColumn = -1;
    bool hasAnswers = false;
    for (int c = 0; c < columns.size(); ++c) {
        if (columns[c].role == Role::Id && idColumn < 0)
            idColumn = c;
        hasAnswers = hasAnswers || columns[c].role == Role::OpenAnswer;
    }
    if (!hasAnswers) {
        *error = QObject::tr("Indica almeno una colonna come risposta aperta.");
        return false;
    }

    project.setUndoLabel(QObject::tr("Importa dati di sondaggio"));
    project.beginBatch();
    auto fail = [&] {
        *error = project.lastError();
        project.rollbackBatch();
        return false;
    };

    const QString groupName = options.groupName.trimmed().isEmpty() ? QObject::tr("Sondaggio")
                                                                     : options.groupName.trimmed();
    report->groupId = project.addGroup(groupName);
    if (!report->groupId)
        return fail();

    QHash<QString, Variable> varsByName;
    for (const Variable &v : project.variables())
        varsByName.insert(v.name, v);
    QHash<int, Variable> columnVariable;
    for (int c = 0; c < columns.size(); ++c) {
        if (columns[c].role != Role::Variable)
            continue;
        Variable var = varsByName.value(columns[c].name);
        if (!var.id) {
            var.name = columns[c].name;
            var.type = columns[c].type;
            var.id = project.addVariable(var.name, var.type);
            if (!var.id)
                return fail();
            varsByName.insert(var.name, var);
            ++report->newVariables;
        }
        columnVariable.insert(c, var);
    }

    QHash<int, qint64> columnCode; // created on the first answer, so empty questions add no code
    for (qsizetype r = 1; r < table.size(); ++r) {
        const QStringList &row = table[r];
        if (isEmptyRow(row))
            continue;
        QList<AnswerRange> ranges;
        const QString text = respondentText(columns, row, &ranges);
        if (ranges.isEmpty()) {
            ++report->skippedRows;
            continue;
        }
        QString name = idColumn >= 0 ? cell(row, idColumn).simplified() : QString();
        if (name.isEmpty())
            name = QObject::tr("Rispondente %1").arg(r);
        const qint64 docId = project.addDocument(report->groupId, name, text, options.sourcePath);
        if (!docId)
            return fail();
        if (!report->firstDocumentId)
            report->firstDocumentId = docId;
        ++report->documents;

        if (options.codeAnswers) {
            for (const AnswerRange &range : std::as_const(ranges)) {
                qint64 code = columnCode.value(range.column);
                if (!code) {
                    const QString &question = columns[range.column].name;
                    code = project.findCode(0, question);
                    if (!code) {
                        code = project.addCode(0, question, TextUtils::paletteColor(int(project.codes().size())));
                        if (!code)
                            return fail();
                        ++report->newCodes;
                    }
                    columnCode.insert(range.column, code);
                }
                if (!project.addSegment(docId, code, range.start, range.end))
                    return fail();
                ++report->codedAnswers;
            }
        }

        for (auto it = columnVariable.cbegin(); it != columnVariable.cend(); ++it) {
            const QString raw = cell(row, it.key());
            QString value;
            if (!Exporters::normalizeVariableValue(it->type, raw, &value)) {
                report->invalidValues << QObject::tr("%1 / %2: \"%3\"").arg(name, it->name, raw.trimmed());
                continue;
            }
            if (!value.isEmpty() && !project.setVariableValue(docId, it->id, value))
                return fail();
        }
    }
    if (!project.commitBatch()) {
        *error = project.lastError();
        return false;
    }
    return true;
}

QString Report::text() const
{
    QString out = QObject::tr("%1 documenti creati, %2 risposte codificate, %3 codici nuovi, %4 variabili nuove.")
                      .arg(QString::number(documents), QString::number(codedAnswers), QString::number(newCodes),
                           QString::number(newVariables));
    if (skippedRows > 0)
        out += QLatin1Char('\n') + QObject::tr("Rispondenti senza risposte aperte (saltati): %1").arg(skippedRows);
    if (!invalidValues.isEmpty())
        out += QLatin1Char('\n') + QObject::tr("Valori non compatibili con il tipo (ignorati): %1")
                                       .arg(invalidValues.join(QStringLiteral("; ")));
    return out;
}

} // namespace Survey
