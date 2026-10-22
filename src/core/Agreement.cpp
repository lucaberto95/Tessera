#include "core/Agreement.h"

#include "core/Project.h"

#include <QCoreApplication>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Agreement {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

using Key = QPair<qint64, qint64>; // (document, code)

QHash<Key, QList<Segment>> group(const QList<Segment> &segments, const QSet<qint64> &docs, const QSet<qint64> &codes,
                                 bool textOnly)
{
    QHash<Key, QList<Segment>> out;
    for (const Segment &s : segments) {
        if (!docs.contains(s.documentId) || !codes.contains(s.codeId))
            continue;
        if (textOnly && s.kind != SegmentKind::Text)
            continue;
        out[{s.documentId, s.codeId}] << s;
    }
    for (auto it = out.begin(); it != out.end(); ++it)
        std::stable_sort(it->begin(), it->end(), [](const Segment &x, const Segment &y) {
            return x.start != y.start ? x.start < y.start : x.end < y.end;
        });
    return out;
}

// overlap / union of two text segments (0 when they do not overlap)
double overlapRatio(const Segment &a, const Segment &b, bool *overlaps)
{
    const int overlap = std::min(a.end, b.end) - std::max(a.start, b.start);
    *overlaps = overlap > 0;
    if (overlap <= 0)
        return 0;
    const int unionLength = a.length() + b.length() - overlap;
    return unionLength > 0 ? double(overlap) / unionLength : 0;
}

bool matches(const Segment &s, const QList<Segment> &others, double minOverlap)
{
    for (const Segment &o : others) {
        bool overlaps = false;
        const double ratio = overlapRatio(s, o, &overlaps);
        if (overlaps && ratio + 1e-9 >= minOverlap)
            return true;
    }
    return false;
}

void finish(CodeResult &r, bool hasTable)
{
    r.percent = r.units > 0 ? 100.0 * r.agreements / r.units : 0;
    const double observed = r.units > 0 ? double(r.agreements) / r.units : kNaN;
    if (hasTable) {
        int maxCategory = 1;
        for (auto it = r.table.cbegin(); it != r.table.cend(); ++it)
            maxCategory = std::max({maxCategory, it.key().first, it.key().second});
        r.categories = maxCategory + 1;
        r.kappa = cohenKappa(r.table);
    } else {
        r.categories = 2;
        r.kappa = kNaN;
    }
    r.kappaBP = brennanPredigerKappa(observed, r.categories);
}

struct Pending {
    int docIndex = 0, codeIndex = 0;
    Disagreement d;
};
} // namespace

QString levelLabel(Level level)
{
    switch (level) {
    case Level::Existence:
        return QCoreApplication::translate("Agreement", "Presenza del codice nel documento");
    case Level::Frequency:
        return QCoreApplication::translate("Agreement", "Frequenza del codice nel documento");
    case Level::Segments:
        return QCoreApplication::translate("Agreement", "Segmenti");
    }
    return {};
}

const CodeResult *Result::forCode(qint64 codeId) const
{
    for (const CodeResult &r : perCode)
        if (r.codeId == codeId)
            return &r;
    return nullptr;
}

double cohenKappa(const QMap<QPair<int, int>, int> &table)
{
    double n = 0, agree = 0;
    QMap<int, double> rows, cols;
    for (auto it = table.cbegin(); it != table.cend(); ++it) {
        n += it.value();
        if (it.key().first == it.key().second)
            agree += it.value();
        rows[it.key().first] += it.value();
        cols[it.key().second] += it.value();
    }
    if (n <= 0)
        return kNaN;
    double pe = 0;
    for (auto it = rows.cbegin(); it != rows.cend(); ++it)
        pe += (it.value() / n) * (cols.value(it.key()) / n);
    if (1.0 - pe < 1e-12)
        return kNaN;
    return (agree / n - pe) / (1.0 - pe);
}

double brennanPredigerKappa(double observed, int categories)
{
    if (std::isnan(observed) || categories < 2)
        return kNaN;
    const double pe = 1.0 / categories;
    return (observed - pe) / (1.0 - pe);
}

Result compute(const Options &options, const QList<Segment> &coderA, const QList<Segment> &coderB)
{
    Result result;
    result.options = options;
    const bool segmentLevel = options.level == Level::Segments;
    const QSet<qint64> docSet(options.documents.cbegin(), options.documents.cend());
    const QSet<qint64> codeSet(options.codes.cbegin(), options.codes.cend());
    const auto a = group(coderA, docSet, codeSet, segmentLevel);
    const auto b = group(coderB, docSet, codeSet, segmentLevel);

    QList<Pending> pending;
    CodeResult total;
    for (int ci = 0; ci < options.codes.size(); ++ci) {
        const qint64 code = options.codes.at(ci);
        CodeResult r;
        r.codeId = code;
        for (int di = 0; di < options.documents.size(); ++di) {
            const qint64 doc = options.documents.at(di);
            const QList<Segment> sa = a.value({doc, code});
            const QList<Segment> sb = b.value({doc, code});
            auto add = [&](DisagreementKind kind, int countA, int countB, const Segment &shown) {
                Pending p;
                p.docIndex = di;
                p.codeIndex = ci;
                p.d.documentId = doc;
                p.d.codeId = code;
                p.d.kind = kind;
                p.d.countA = countA;
                p.d.countB = countB;
                p.d.segment = shown;
                pending << p;
            };
            if (segmentLevel) {
                for (const Segment &s : sa) {
                    ++r.units;
                    if (matches(s, sb, options.minOverlap))
                        ++r.agreements;
                    else
                        add(DisagreementKind::OnlyA, 1, 0, s);
                }
                for (const Segment &s : sb) {
                    ++r.units;
                    if (matches(s, sa, options.minOverlap))
                        ++r.agreements;
                    else
                        add(DisagreementKind::OnlyB, 0, 1, s);
                }
                continue;
            }
            const int na = sa.size(), nb = sb.size();
            const bool existence = options.level == Level::Existence;
            const int ca = existence ? std::min(na, 1) : na;
            const int cb = existence ? std::min(nb, 1) : nb;
            ++r.units;
            ++r.table[{ca, cb}];
            if (ca == cb) {
                ++r.agreements;
            } else if (cb == 0) {
                add(DisagreementKind::OnlyA, na, nb, sa.first());
            } else if (ca == 0) {
                add(DisagreementKind::OnlyB, na, nb, sb.first());
            } else {
                add(DisagreementKind::Count, na, nb, na > nb ? sa.first() : sb.first());
            }
        }
        finish(r, !segmentLevel);
        total.units += r.units;
        total.agreements += r.agreements;
        for (auto it = r.table.cbegin(); it != r.table.cend(); ++it)
            total.table[it.key()] += it.value();
        result.perCode << r;
    }
    finish(total, !segmentLevel);
    result.total = total;

    std::stable_sort(pending.begin(), pending.end(), [](const Pending &x, const Pending &y) {
        if (x.docIndex != y.docIndex)
            return x.docIndex < y.docIndex;
        return x.codeIndex < y.codeIndex;
    });
    for (const Pending &p : pending)
        result.disagreements << p.d;
    return result;
}

QStringList authors(const Project &project)
{
    QSet<QString> seen;
    QStringList out;
    for (const Segment &s : project.allSegments(true))
        if (!s.author.isEmpty() && !seen.contains(s.author)) {
            seen.insert(s.author);
            out << s.author;
        }
    out.sort(Qt::CaseInsensitive);
    return out;
}

QList<Segment> byAuthor(const QList<Segment> &segments, const QString &author)
{
    if (author.isEmpty())
        return segments;
    QList<Segment> out;
    for (const Segment &s : segments)
        if (s.author == author)
            out << s;
    return out;
}

namespace {
template<typename Item, typename NameFn>
QHash<qint64, qint64> mapItems(const QList<Item> &from, const QList<Item> &to, NameFn nameOf)
{
    QHash<qint64, qint64> map;
    QHash<QString, qint64> byUuid;
    QHash<QString, int> nameCount;
    QHash<QString, qint64> byName;
    for (const Item &t : to) {
        if (!t.uuid.isEmpty())
            byUuid.insert(t.uuid, t.id);
        const QString n = nameOf(t);
        ++nameCount[n];
        byName.insert(n, t.id);
    }
    QHash<QString, int> fromNameCount;
    for (const Item &f : from)
        ++fromNameCount[nameOf(f)];
    for (const Item &f : from) {
        if (!f.uuid.isEmpty() && byUuid.contains(f.uuid)) {
            map.insert(f.id, byUuid.value(f.uuid));
            continue;
        }
        const QString n = nameOf(f);
        if (nameCount.value(n) == 1 && fromNameCount.value(n) == 1)
            map.insert(f.id, byName.value(n));
    }
    return map;
}
} // namespace

QHash<qint64, qint64> mapDocuments(const Project &from, const Project &to)
{
    return mapItems(from.documents(), to.documents(), [](const Document &d) { return d.name; });
}

QHash<qint64, qint64> mapCodes(const Project &from, const Project &to)
{
    // Codes are matched by path, so two "Altro" codes under different parents stay apart.
    const QList<Code> f = from.codesInTreeOrder(), t = to.codesInTreeOrder();
    QHash<qint64, QString> fromPath, toPath;
    for (const Code &c : f)
        fromPath.insert(c.id, from.codePath(c.id));
    for (const Code &c : t)
        toPath.insert(c.id, to.codePath(c.id));
    QHash<qint64, qint64> map;
    QHash<QString, qint64> byUuid, byPath;
    for (const Code &c : t) {
        if (!c.uuid.isEmpty())
            byUuid.insert(c.uuid, c.id);
        byPath.insert(toPath.value(c.id), c.id);
    }
    for (const Code &c : f) {
        if (!c.uuid.isEmpty() && byUuid.contains(c.uuid))
            map.insert(c.id, byUuid.value(c.uuid));
        else if (byPath.contains(fromPath.value(c.id)))
            map.insert(c.id, byPath.value(fromPath.value(c.id)));
    }
    return map;
}

QList<Segment> remap(const QList<Segment> &segments, const QHash<qint64, qint64> &documents,
                     const QHash<qint64, qint64> &codes)
{
    QList<Segment> out;
    for (Segment s : segments) {
        if (!documents.contains(s.documentId) || !codes.contains(s.codeId))
            continue;
        s.documentId = documents.value(s.documentId);
        s.codeId = codes.value(s.codeId);
        out << s;
    }
    return out;
}

} // namespace Agreement
