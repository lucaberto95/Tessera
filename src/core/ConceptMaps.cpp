#include "core/ConceptMaps.h"

#include "core/Cooccurrence.h"
#include "core/Project.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <functional>

namespace ConceptMaps {

namespace {
QString tr(const char *s) { return QCoreApplication::translate("ConceptMaps", s); }

QString oneLine(QString s, int max = 60)
{
    s = s.simplified();
    if (s.size() > max)
        s = s.left(max - 1) + QChar(0x2026);
    return s;
}
} // namespace

Target resolve(const Project &project, const MapNode &node)
{
    Target t;
    t.caption = node.label;
    switch (node.kind) {
    case MapNodeKind::Text:
        break;
    case MapNodeKind::Code: {
        const Code c = project.code(node.refId);
        t.exists = c.id != 0;
        if (t.exists) {
            t.caption = c.name;
            t.color = c.color;
        }
        break;
    }
    case MapNodeKind::Document: {
        const Document d = project.document(node.refId);
        t.exists = d.id != 0;
        if (t.exists)
            t.caption = d.name;
        break;
    }
    case MapNodeKind::Memo: {
        const Memo m = project.memo(node.refId);
        t.exists = m.id != 0;
        if (t.exists)
            t.caption = m.title.isEmpty() ? oneLine(m.body) : m.title;
        break;
    }
    case MapNodeKind::Segment: {
        const Segment s = project.segment(node.refId);
        t.exists = s.id != 0;
        if (t.exists) {
            const Code c = project.code(s.codeId);
            t.color = c.color;
            QString text = s.kind == SegmentKind::Text
                               ? project.documentText(s.documentId).mid(s.start, s.end - s.start)
                               : s.areaText;
            t.caption = oneLine(text.isEmpty() ? c.name : text);
        }
        break;
    }
    }
    if (!t.exists)
        t.caption = tr("(elemento eliminato)") + (node.label.isEmpty() ? QString() : QStringLiteral(" ") + node.label);
    return t;
}

Style styleFromJson(const QString &json)
{
    Style s;
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    if (o.contains(QStringLiteral("color")))
        s.color = QColor(o.value(QStringLiteral("color")).toString());
    if (o.contains(QStringLiteral("width")))
        s.width = std::clamp(o.value(QStringLiteral("width")).toInt(2), 1, 12);
    const QString line = o.value(QStringLiteral("line")).toString();
    if (line == QLatin1String("dash") || line == QLatin1String("dot"))
        s.line = line;
    return s;
}

QString styleToJson(const Style &style)
{
    QJsonObject o;
    if (style.color.isValid())
        o.insert(QStringLiteral("color"), style.color.name());
    if (style.width != 2)
        o.insert(QStringLiteral("width"), style.width);
    if (style.line != QLatin1String("solid"))
        o.insert(QStringLiteral("line"), style.line);
    return o.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

namespace {
constexpr double kStepX = 200, kStepY = 90;

MapNode makeNode(qint64 mapId, MapNodeKind kind, qint64 ref, const QString &label, double x, double y)
{
    MapNode n;
    n.mapId = mapId;
    n.kind = kind;
    n.refId = ref;
    n.label = label;
    n.x = x;
    n.y = y;
    return n;
}

void addEdge(Project &p, qint64 mapId, qint64 from, qint64 to, const QString &label = {})
{
    MapEdge e;
    e.mapId = mapId;
    e.fromNode = from;
    e.toNode = to;
    e.label = label;
    p.addMapEdge(e);
}
} // namespace

int addCodeWithSubcodes(Project &p, qint64 mapId, qint64 codeId)
{
    const Code root = p.code(codeId);
    if (!root.id || !p.conceptMap(mapId).id)
        return 0;
    const QList<Code> all = p.codes();
    QHash<qint64, QList<Code>> children;
    for (const Code &c : all)
        children[c.parentId] << c;

    int added = 0;
    int nextRow = 0; // leaves get consecutive rows; a parent sits at the mean of its children
    p.beginBatch();
    std::function<std::pair<qint64, double>(const Code &, int)> place = [&](const Code &c, int depth) {
        QList<std::pair<qint64, double>> kids;
        for (const Code &child : children.value(c.id))
            kids << place(child, depth + 1);
        double y = 0;
        if (kids.isEmpty()) {
            y = nextRow++ * kStepY;
        } else {
            y = (kids.first().second + kids.last().second) / 2;
        }
        const qint64 id = p.addMapNode(makeNode(mapId, MapNodeKind::Code, c.id, c.name, depth * kStepX, y));
        ++added;
        for (const auto &k : kids)
            addEdge(p, mapId, id, k.first);
        return std::pair<qint64, double>(id, y);
    };
    place(root, 0);
    p.commitBatch();
    return added;
}

int addCodeWithDocuments(Project &p, qint64 mapId, qint64 codeId)
{
    const Code code = p.code(codeId);
    if (!code.id || !p.conceptMap(mapId).id)
        return 0;
    QHash<qint64, int> perDocument;
    for (const Segment &s : p.segments({}, {codeId}, true))
        ++perDocument[s.documentId];
    QList<Document> docs;
    for (const Document &d : p.documents())
        if (perDocument.contains(d.id))
            docs << d;
    if (docs.isEmpty())
        return 0;

    p.beginBatch();
    const double mid = (docs.size() - 1) * kStepY / 2;
    const qint64 root = p.addMapNode(makeNode(mapId, MapNodeKind::Code, code.id, code.name, 0, mid));
    int row = 0;
    for (const Document &d : docs) {
        const qint64 id = p.addMapNode(makeNode(mapId, MapNodeKind::Document, d.id, d.name, kStepX * 1.5, row++ * kStepY));
        addEdge(p, mapId, root, id, QString::number(perDocument.value(d.id)));
    }
    p.commitBatch();
    return int(docs.size()) + 1;
}

int addCooccurrences(Project &p, qint64 mapId, const QList<qint64> &codeIds, int minCount)
{
    if (codeIds.isEmpty() || !p.conceptMap(mapId).id)
        return 0;
    Cooccurrence::Options opt;
    opt.criterion = Cooccurrence::Criterion::Overlap;
    opt.codes = codeIds;
    const Cooccurrence::Matrix m = Cooccurrence::compute(opt, p.allSegments());

    const int n = int(codeIds.size());
    QList<QList<double>> weights(n, QList<double>(n, 0.0));
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (m.count(codeIds[i], codeIds[j]) >= minCount)
                weights[i][j] = weights[j][i] = m.count(codeIds[i], codeIds[j]);
    const QList<QPointF> pos = Cooccurrence::forceLayout(weights);
    const double scale = std::max(300.0, 120.0 * std::sqrt(double(n)));

    p.beginBatch();
    QList<qint64> nodes;
    int added = 0;
    for (int i = 0; i < n; ++i) {
        const Code c = p.code(codeIds[i]);
        if (!c.id) {
            nodes << 0;
            continue;
        }
        nodes << p.addMapNode(makeNode(mapId, MapNodeKind::Code, c.id, c.name, pos.value(i).x() * scale,
                                       pos.value(i).y() * scale));
        ++added;
    }
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (weights[i][j] > 0 && nodes[i] && nodes[j]) {
                addEdge(p, mapId, nodes[i], nodes[j], QString::number(int(weights[i][j])));
            }
    p.commitBatch();
    return added;
}

} // namespace ConceptMaps