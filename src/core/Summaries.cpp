#include "core/Summaries.h"
#include "core/Images.h"
#include "core/Project.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace Summaries {

namespace {

QJsonArray idsToJson(const QList<qint64> &ids)
{
    QJsonArray a;
    for (qint64 id : ids)
        a.append(double(id));
    return a;
}

QList<qint64> idsFromJson(const QJsonValue &v)
{
    QList<qint64> out;
    for (const QJsonValue &x : v.toArray())
        if (x.toDouble() > 0 && !out.contains(qint64(x.toDouble())))
            out << qint64(x.toDouble());
    return out;
}

QString clock(qint64 ms)
{
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

QString configToJson(const TableConfig &config)
{
    QJsonObject o;
    o.insert(QStringLiteral("documents"), idsToJson(config.documentIds));
    o.insert(QStringLiteral("set"), double(config.setId));
    o.insert(QStringLiteral("codes"), idsToJson(config.codeIds));
    o.insert(QStringLiteral("variables"), idsToJson(config.variableIds));
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

TableConfig configFromJson(const QString &json)
{
    TableConfig c;
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    c.documentIds = idsFromJson(o.value(QStringLiteral("documents")));
    c.setId = qint64(o.value(QStringLiteral("set")).toDouble());
    c.codeIds = idsFromJson(o.value(QStringLiteral("codes")));
    c.variableIds = idsFromJson(o.value(QStringLiteral("variables")));
    return c;
}

QList<qint64> tableDocuments(const Project &project, const TableConfig &config)
{
    QSet<qint64> wanted;
    if (config.setId) {
        const QList<qint64> members = project.documentSetMembers(config.setId);
        wanted = QSet<qint64>(members.begin(), members.end());
    } else {
        wanted = QSet<qint64>(config.documentIds.begin(), config.documentIds.end());
    }
    QList<qint64> out;
    for (const Document &d : project.documents()) // document tree order
        if (wanted.contains(d.id))
            out << d.id;
    return out;
}

Exporters::Table buildTable(const Project &project, const TableConfig &config)
{
    QHash<qint64, Variable> variables;
    for (const Variable &v : project.variables())
        variables.insert(v.id, v);
    QList<qint64> varIds;
    for (qint64 id : config.variableIds)
        if (variables.contains(id))
            varIds << id;
    QList<qint64> codeIds;
    for (qint64 id : config.codeIds)
        if (project.code(id).id)
            codeIds << id;

    QHash<qint64, QHash<qint64, QString>> bodies; // document -> code -> summary
    for (const Summary &s : project.summaries())
        bodies[s.documentId].insert(s.codeId, s.body);
    const auto values = project.allVariableValues();
    QHash<qint64, QString> names;
    for (const Document &d : project.documents())
        names.insert(d.id, d.name);

    Exporters::Table table;
    QStringList header{QObject::tr("Documento")};
    for (qint64 v : varIds)
        header << variables.value(v).name;
    for (qint64 c : codeIds)
        header << project.codePath(c);
    table << header;
    for (qint64 doc : tableDocuments(project, config)) {
        QStringList row{names.value(doc)};
        for (qint64 v : varIds)
            row << values.value(doc).value(v);
        for (qint64 c : codeIds)
            row << bodies.value(doc).value(c);
        table << row;
    }
    return table;
}

Xlsx::Sheet buildSheet(const Project &project, const TableConfig &config, const QString &sheetName)
{
    const Exporters::Table table = buildTable(project, config);
    Xlsx::Sheet sheet = Exporters::tableSheet(sheetName, table);
    // Summaries are free text; variables keep their typed values.
    int varCount = 0;
    for (qint64 id : config.variableIds)
        for (const Variable &v : project.variables())
            if (v.id == id)
                ++varCount;
    for (int c = 1 + varCount; c < (table.isEmpty() ? 0 : int(table.first().size())); ++c) {
        sheet.textColumns.insert(c);
        sheet.wrapColumns.insert(c);
    }
    return sheet;
}

QString segmentText(const Project &project, const Segment &s)
{
    switch (s.kind) {
    case SegmentKind::Area:
        return s.areaText.isEmpty() ? Images::areaLabel(s) : s.areaText;
    case SegmentKind::Time:
        return QStringLiteral("[%1 - %2]").arg(clock(s.tStartMs), clock(s.tEndMs));
    default:
        return project.documentText(s.documentId).mid(s.start, s.length());
    }
}

} // namespace Summaries
