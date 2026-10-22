#pragma once

#include "core/Model.h"

#include <QList>
#include <QString>

#include <functional>

// Complex segment retrieval: relations between the segments of two code lists (A and B).
// Pure logic on in-memory segments; no database access.
namespace Retrieval {

enum class Mode {
    Intersection,    // text covered at the same time by every code of A
    IntersectionSet, // text covered by at least `count` different codes of A
    Overlap,         // segments of A overlapping (even partly) a segment of B, returned whole
    OnlyOneCode,     // segments of A overlapping no segment of another code of A
    OnlyThisCode,    // segments of A overlapping no segment of B
    IfInside,        // segments of A entirely inside a segment of B
    IfOutside,       // segments of A inside no segment of B
    Near,            // segments of A within `distance` of a segment of B
    FollowedBy       // A followed by B within `distance`; the result spans from A to B
};

struct Query {
    Mode mode = Mode::Intersection;
    QList<qint64> codesA;
    QList<qint64> codesB;
    int count = 2;            // IntersectionSet
    int distance = 1;         // Near, FollowedBy
    bool inParagraphs = true; // distance unit: paragraphs (newlines) or characters
};

// A retrieved passage. For modes that return stored segments `segmentId` is set; intersections and
// "followed by" spans are new intervals (`segmentId` == 0).
struct Result {
    qint64 documentId = 0;
    int start = 0;
    int end = 0; // exclusive
    QList<qint64> codeIds;
    qint64 segmentId = 0;
};

bool needsCodesB(Mode mode);
QString modeLabel(Mode mode);

// `segments` may contain any codes; only those listed in the query are considered.
// `textOf` returns a document's text and is called only for paragraph distances.
QList<Result> run(const Query &query, const QList<Segment> &segments,
                  const std::function<QString(qint64)> &textOf = {});

} // namespace Retrieval
