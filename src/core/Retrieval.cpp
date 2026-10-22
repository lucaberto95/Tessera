#include "core/Retrieval.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <iterator>
#include <set>

namespace Retrieval {

namespace {

// Adjacent segments (end == start) do not overlap.
bool overlaps(const Segment &a, const Segment &b) { return a.start < b.end && b.start < a.end; }
bool inside(const Segment &a, const Segment &b) { return b.start <= a.start && a.end <= b.end; }

Result whole(const Segment &s)
{
    return {s.documentId, s.start, s.end, {s.codeId}, s.id};
}

// Intervals where the number of distinct covering codes satisfies `enough`; contiguous ones merge.
QList<Result> coverage(qint64 documentId, const QList<Segment> &segs, const std::function<bool(int)> &enough)
{
    std::set<int> bounds;
    for (const Segment &s : segs) {
        bounds.insert(s.start);
        bounds.insert(s.end);
    }
    QList<Result> out;
    for (auto it = bounds.cbegin(); std::next(it) != bounds.cend(); ++it) {
        const int from = *it;
        const int to = *std::next(it);
        QSet<qint64> codes;
        for (const Segment &s : segs) {
            if (s.start <= from && to <= s.end)
                codes.insert(s.codeId);
        }
        if (!enough(int(codes.size())))
            continue;
        QList<qint64> ids(codes.cbegin(), codes.cend());
        std::sort(ids.begin(), ids.end());
        if (!out.isEmpty() && out.last().end == from) {
            Result &last = out.last();
            last.end = to;
            for (qint64 c : std::as_const(ids))
                if (!last.codeIds.contains(c))
                    last.codeIds << c;
            std::sort(last.codeIds.begin(), last.codeIds.end());
        } else {
            out.append({documentId, from, to, ids, 0});
        }
    }
    return out;
}

// Distance between two intervals: 0 when they touch or overlap, else the gap in characters or newlines.
int gap(const Segment &a, const Segment &b, const QString &text, bool paragraphs)
{
    const int lo = std::min(a.end, b.end);
    const int hi = std::max(a.start, b.start);
    if (lo >= hi)
        return 0;
    return paragraphs ? int(text.mid(lo, hi - lo).count(QLatin1Char('\n'))) : hi - lo;
}

} // namespace

bool needsCodesB(Mode mode)
{
    switch (mode) {
    case Mode::Intersection:
    case Mode::IntersectionSet:
    case Mode::OnlyOneCode:
        return false;
    default:
        return true;
    }
}

QString modeLabel(Mode mode)
{
    switch (mode) {
    case Mode::Intersection: return QCoreApplication::translate("Retrieval", "Intersezione");
    case Mode::IntersectionSet: return QCoreApplication::translate("Retrieval", "Intersezione (insieme)");
    case Mode::Overlap: return QCoreApplication::translate("Retrieval", "Sovrapposizione");
    case Mode::OnlyOneCode: return QCoreApplication::translate("Retrieval", "Solo un codice");
    case Mode::OnlyThisCode: return QCoreApplication::translate("Retrieval", "Solo questo codice");
    case Mode::IfInside: return QCoreApplication::translate("Retrieval", "Se dentro");
    case Mode::IfOutside: return QCoreApplication::translate("Retrieval", "Se fuori");
    case Mode::Near: return QCoreApplication::translate("Retrieval", "Vicinanza");
    case Mode::FollowedBy: return QCoreApplication::translate("Retrieval", "Seguito da");
    }
    return {};
}

QList<Result> run(const Query &query, const QList<Segment> &segments, const std::function<QString(qint64)> &textOf)
{
    const QSet<qint64> setA(query.codesA.cbegin(), query.codesA.cend());
    const QSet<qint64> setB(query.codesB.cbegin(), query.codesB.cend());
    if (setA.isEmpty() || (needsCodesB(query.mode) && setB.isEmpty()))
        return {};

    QList<qint64> docOrder;
    QHash<qint64, QList<Segment>> a, b;
    for (const Segment &s : segments) {
        const bool inA = setA.contains(s.codeId);
        const bool inB = setB.contains(s.codeId);
        if (!inA && !inB)
            continue;
        if (!docOrder.contains(s.documentId))
            docOrder << s.documentId;
        if (inA)
            a[s.documentId].append(s);
        if (inB)
            b[s.documentId].append(s);
    }

    QList<Result> out;
    for (qint64 doc : std::as_const(docOrder)) {
        const QList<Segment> &as = a[doc];
        const QList<Segment> &bs = b[doc];
        QList<Result> found;
        const auto anyB = [&](const Segment &x, const auto &rel) {
            return std::any_of(bs.cbegin(), bs.cend(), [&](const Segment &y) { return y.id != x.id && rel(x, y); });
        };

        switch (query.mode) {
        case Mode::Intersection:
            found = coverage(doc, as, [n = int(setA.size())](int c) { return c == n; });
            break;
        case Mode::IntersectionSet:
            found = coverage(doc, as, [n = std::max(1, query.count)](int c) { return c >= n; });
            break;
        case Mode::Overlap:
            for (const Segment &x : as)
                if (anyB(x, overlaps))
                    found << whole(x);
            break;
        case Mode::OnlyOneCode:
            for (const Segment &x : as) {
                const bool clash = std::any_of(as.cbegin(), as.cend(), [&](const Segment &y) {
                    return y.codeId != x.codeId && overlaps(x, y);
                });
                if (!clash)
                    found << whole(x);
            }
            break;
        case Mode::OnlyThisCode:
            for (const Segment &x : as)
                if (!anyB(x, overlaps))
                    found << whole(x);
            break;
        case Mode::IfInside:
            for (const Segment &x : as)
                if (anyB(x, inside))
                    found << whole(x);
            break;
        case Mode::IfOutside:
            for (const Segment &x : as)
                if (!anyB(x, inside))
                    found << whole(x);
            break;
        case Mode::Near: {
            const QString text = query.inParagraphs && textOf ? textOf(doc) : QString();
            for (const Segment &x : as) {
                const auto isNear = [&](const Segment &p, const Segment &q) {
                    return gap(p, q, text, query.inParagraphs) <= query.distance;
                };
                if (anyB(x, isNear))
                    found << whole(x);
            }
            break;
        }
        case Mode::FollowedBy: {
            const QString text = query.inParagraphs && textOf ? textOf(doc) : QString();
            for (const Segment &x : as) {
                const Segment *best = nullptr;
                for (const Segment &y : bs) {
                    if (y.id == x.id || y.start < x.end)
                        continue;
                    if (gap(x, y, text, query.inParagraphs) <= query.distance && (!best || y.start < best->start))
                        best = &y;
                }
                if (best)
                    found.append({doc, x.start, best->end, {x.codeId, best->codeId}, 0});
            }
            break;
        }
        }
        std::stable_sort(found.begin(), found.end(), [](const Result &p, const Result &q) {
            return p.start != q.start ? p.start < q.start : p.end < q.end;
        });
        out += found;
    }
    return out;
}

} // namespace Retrieval
