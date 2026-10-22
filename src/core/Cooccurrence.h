#pragma once

#include "core/Model.h"
#include "core/Retrieval.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QPointF>
#include <QString>

#include <functional>

// Code co-occurrence: which codes appear together on the same passages or in the same documents.
// Pure logic on in-memory segments; no database access.
namespace Cooccurrence {

enum class Criterion {
    Overlap,     // the segments of the two codes overlap; one co-occurrence per shared interval
    Proximity,   // a segment of one code within `distance` of a segment of the other; one per pair
    SameDocument // both codes occur in the document; one per document
};

struct Options {
    Criterion criterion = Criterion::Overlap;
    QList<qint64> codes;      // codes in the matrix; segments of other codes are ignored
    int distance = 1;         // Proximity
    bool inParagraphs = true; // Proximity unit: paragraphs (newlines) or characters
};

QString criterionLabel(Criterion criterion);

using Pair = QPair<qint64, qint64>; // always (smaller id, larger id)
Pair makePair(qint64 a, qint64 b);

struct Matrix {
    QList<qint64> codes;
    QHash<Pair, int> counts; // co-occurrences; the diagonal (c, c) is the code's frequency
    // Units of a code taking part in at least one co-occurrence with another code, keyed (code, other).
    QHash<QPair<qint64, qint64>, int> involved;

    int count(qint64 a, qint64 b) const { return counts.value(makePair(a, b)); }
    // Frequency of a code: its segments, or its documents with the SameDocument criterion.
    int frequency(qint64 code) const { return counts.value(makePair(code, code)); }
    // Share of the two codes that occurs together, in [0, 1]; 1 on the diagonal. With
    // m = min(units of a with b, units of b with a) it is m / (freq(a) + freq(b) - m), the Jaccard
    // index of the two codes when matched one to one (exact for SameDocument).
    double jaccard(qint64 a, qint64 b) const;
};

// `textOf` returns a document's text and is called only for paragraph distances.
Matrix compute(const Options &options, const QList<Segment> &segments,
               const std::function<QString(qint64)> &textOf = {});

// The passages behind a matrix cell. Overlap and Proximity give new intervals (segmentId == 0, both
// codes listed); SameDocument gives every segment of the two codes in the shared documents.
QList<Retrieval::Result> passages(const Options &options, qint64 codeA, qint64 codeB,
                                  const QList<Segment> &segments,
                                  const std::function<QString(qint64)> &textOf = {});

// Fruchterman–Reingold layout of `weights` (n × n, symmetric, 0 = no edge) in the unit square.
// The seed makes it reproducible.
QList<QPointF> forceLayout(const QList<QList<double>> &weights, quint32 seed = 1, int iterations = 300);

} // namespace Cooccurrence
