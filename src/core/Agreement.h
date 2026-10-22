#pragma once

#include "core/Model.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>
#include <QStringList>

class Project;

// Intercoder agreement (issue #23): how far two coders agree on the same documents. Pure logic on in-memory
// segments of one shared id space (see mapDocuments/mapCodes/remap to bring a second project into it).
//
// Three levels of comparison, each producing per-code and total figures:
//  - Existence: unit = (document, code); the coder either used the code in the document or not.
//    Categories 0/1; agreement counts documents where both or neither used the code.
//  - Frequency: unit = (document, code); category = number of segments with the code in the document.
//    Agreement when the two counts are equal.
//  - Segments: units are the segments themselves (no predefined units). A segment of one coder is "matched" when
//    the other coder has a segment of the same code in the same document whose overlap with it is at least
//    `minOverlap` of their union (overlap / union, so containment of a much longer segment does not count).
//    Units = segments of A + segments of B, agreements = matched ones. Only text segments are compared.
//
// Figures: percentage = agreements / units. Cohen's kappa = (po - pe) / (1 - pe), with pe from the marginal
// distributions of the two coders over the categories (existence, frequency); undefined (NaN) when pe = 1 and for
// the segment level, which has no "neither coded" cases to build marginals from. Brennan-Prediger kappa uses a
// uniform chance agreement pe = 1/k: (po - 1/k) / (1 - 1/k), with k = 2 categories (existence, segments) or
// max count + 1 (frequency, at least 2); it is the usual choice when units are not predefined.
// Totals pool the units (and the category tables) of all codes.
namespace Agreement {

enum class Level { Existence, Frequency, Segments };

QString levelLabel(Level level);

struct Options {
    Level level = Level::Existence;
    QList<qint64> documents; // documents compared, in the order the disagreements are listed
    QList<qint64> codes;     // codes compared
    double minOverlap = 0.9; // Segments: 0..1, overlap / union needed to call two segments the same
};

struct CodeResult {
    qint64 codeId = 0; // 0 in the total
    int units = 0;
    int agreements = 0;
    double percent = 0; // 0..100
    double kappa = 0;   // Cohen; NaN when undefined
    double kappaBP = 0; // Brennan-Prediger; NaN when undefined
    int categories = 2; // k of Brennan-Prediger
    // (category of A, category of B) -> units; empty for Segments
    QMap<QPair<int, int>, int> table;
};

enum class DisagreementKind {
    OnlyA, // A used the code (or the segment), B did not
    OnlyB,
    Count  // Frequency: both used the code but a different number of times
};

struct Disagreement {
    qint64 documentId = 0;
    qint64 codeId = 0;
    DisagreementKind kind = DisagreementKind::OnlyA;
    int countA = 0, countB = 0; // segments of the code in the document (Segments: 1/0 or 0/1)
    // A segment to show: the unmatched one (Segments), otherwise the first segment of the coder who
    // used the code (the one with more segments for Count). Text segments carry start/end.
    Segment segment;
};

struct Result {
    Options options;
    QList<CodeResult> perCode; // in the order of options.codes
    CodeResult total;
    QList<Disagreement> disagreements; // by document, then code, then position

    const CodeResult *forCode(qint64 codeId) const;
};

Result compute(const Options &options, const QList<Segment> &coderA, const QList<Segment> &coderB);

// Cohen's and Brennan-Prediger kappa of a category table, and the observed agreement; exposed for the tests.
double cohenKappa(const QMap<QPair<int, int>, int> &table);
double brennanPredigerKappa(double observed, int categories);

// ---- two coders in a project, or two projects

// Distinct authors of the (text and non-text) segments of the project.
QStringList authors(const Project &project);
// The segments written by `author` (all of them when empty).
QList<Segment> byAuthor(const QList<Segment> &segments, const QString &author);

// Ids of `from` mapped to the same entity in `to`: documents by uuid, then by name (only when the name is unique
// on both sides); codes by uuid, then by path. Entities without a counterpart are left out.
QHash<qint64, qint64> mapDocuments(const Project &from, const Project &to);
QHash<qint64, qint64> mapCodes(const Project &from, const Project &to);
// Rewrites document and code ids of the segments through the maps; segments without a counterpart are dropped.
QList<Segment> remap(const QList<Segment> &segments, const QHash<qint64, qint64> &documents,
                     const QHash<qint64, qint64> &codes);

} // namespace Agreement
