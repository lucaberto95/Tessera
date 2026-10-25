#pragma once

#include "core/Model.h"

#include <QColor>
#include <QList>
#include <QString>

class Project;

// Logic behind the concept maps (issue #15), without widgets: what a node stands for, node styles, and the
// automatic starting points ("templates") that fill a map.
namespace ConceptMaps {

// What a node shows right now. A node whose target was deleted is reported with exists == false and the
// caption saved in the node.
struct Target {
    bool exists = true; // always true for a free text node
    QString caption;
    QColor color; // invalid = default
};
Target resolve(const Project &project, const MapNode &node);

// style_json: {"color": "#rrggbb", "width": n, "line": "solid|dash|dot"}. Missing fields give the defaults.
struct Style {
    QColor color;
    int width = 2;
    QString line = QStringLiteral("solid");
};
Style styleFromJson(const QString &json);
QString styleToJson(const Style &style);

// Templates: each adds nodes and arrows to an existing map (positions in scene units) in one undo step and
// returns the number of nodes added; nothing is added when there is nothing to show.
// "Code and subcodes": the code, its descendants, arrows parent -> child.
int addCodeWithSubcodes(Project &project, qint64 mapId, qint64 codeId);
// "Code and documents": the code and the documents it appears in, arrows code -> document labelled with the number
// of segments.
int addCodeWithDocuments(Project &project, qint64 mapId, qint64 codeId);
// "Co-occurrences": the given codes, an arrow between two codes that overlap in at least `minCount` passages,
// labelled with the count. Codes without any arrow are still added.
int addCooccurrences(Project &project, qint64 mapId, const QList<qint64> &codeIds, int minCount = 1);

} // namespace ConceptMaps