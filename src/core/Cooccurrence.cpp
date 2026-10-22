#include "core/Cooccurrence.h"

#include <QCoreApplication>
#include <QMap>
#include <QRandomGenerator>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace Cooccurrence {

namespace {

using DocSegments = QMap<qint64, QList<Segment>>;

struct Analysis {
    int count = 0;
    int involvedA = 0; // units of code A taking part in a co-occurrence with B
    int involvedB = 0;
    QList<Retrieval::Result> passages;
};

bool overlaps(const Segment &a, const Segment &b) { return a.start < b.end && b.start < a.end; }

// Distance between two intervals: 0 when they touch or overlap, else the gap in characters or newlines.
int gap(const Segment &a, const Segment &b, const QString &text, bool paragraphs)
{
    const int lo = std::min(a.end, b.end);
    const int hi = std::max(a.start, b.start);
    if (lo >= hi)
        return 0;
    return paragraphs ? int(text.mid(lo, hi - lo).count(QLatin1Char('\n'))) : hi - lo;
}

DocSegments byDocument(const QList<Segment> &segments, qint64 code)
{
    DocSegments out;
    for (const Segment &s : segments)
        if (s.codeId == code)
            out[s.documentId].append(s);
    return out;
}

Analysis analyse(const Options &opt, qint64 codeA, qint64 codeB, const DocSegments &a, const DocSegments &b,
                 const std::function<QString(qint64)> &text)
{
    Analysis out;
    switch (opt.criterion) {
    case Criterion::Overlap: {
        QSet<qint64> ia, ib;
        QList<Segment> both;
        for (auto it = a.cbegin(); it != a.cend(); ++it) {
            const QList<Segment> &others = b.value(it.key());
            for (const Segment &x : it.value())
                for (const Segment &y : others)
                    if (overlaps(x, y)) {
                        ia.insert(x.id);
                        ib.insert(y.id);
                    }
        }
        for (const DocSegments *m : {&a, &b})
            for (const QList<Segment> &l : *m)
                both += l;
        Retrieval::Query q;
        q.mode = Retrieval::Mode::Intersection;
        q.codesA = {codeA, codeB};
        out.passages = Retrieval::run(q, both);
        out.count = int(out.passages.size());
        out.involvedA = int(ia.size());
        out.involvedB = int(ib.size());
        break;
    }
    case Criterion::Proximity: {
        QSet<qint64> ia, ib;
        for (auto it = a.cbegin(); it != a.cend(); ++it) {
            const QList<Segment> &others = b.value(it.key());
            if (others.isEmpty())
                continue;
            const QString t = opt.inParagraphs && text ? text(it.key()) : QString();
            for (const Segment &x : it.value())
                for (const Segment &y : others)
                    if (gap(x, y, t, opt.inParagraphs) <= opt.distance) {
                        ia.insert(x.id);
                        ib.insert(y.id);
                        out.passages.append({it.key(), std::min(x.start, y.start), std::max(x.end, y.end),
                                             {codeA, codeB}, 0});
                    }
        }
        std::stable_sort(out.passages.begin(), out.passages.end(),
                         [](const Retrieval::Result &p, const Retrieval::Result &q) {
                             return p.documentId != q.documentId ? p.documentId < q.documentId : p.start < q.start;
                         });
        out.count = int(out.passages.size());
        out.involvedA = int(ia.size());
        out.involvedB = int(ib.size());
        break;
    }
    case Criterion::SameDocument:
        for (auto it = a.cbegin(); it != a.cend(); ++it) {
            if (!b.contains(it.key()))
                continue;
            ++out.count;
            for (const QList<Segment> &l : {it.value(), b.value(it.key())})
                for (const Segment &s : l)
                    out.passages.append({s.documentId, s.start, s.end, {s.codeId}, s.id});
        }
        out.involvedA = out.involvedB = out.count;
        break;
    }
    return out;
}

} // namespace

QString criterionLabel(Criterion criterion)
{
    switch (criterion) {
    case Criterion::Overlap: return QCoreApplication::translate("Cooccurrence", "Sovrapposizione dei segmenti");
    case Criterion::Proximity: return QCoreApplication::translate("Cooccurrence", "Vicinanza");
    case Criterion::SameDocument: return QCoreApplication::translate("Cooccurrence", "Stesso documento");
    }
    return {};
}

Pair makePair(qint64 a, qint64 b)
{
    return a <= b ? Pair(a, b) : Pair(b, a);
}

double Matrix::jaccard(qint64 a, qint64 b) const
{
    if (a == b)
        return frequency(a) > 0 ? 1.0 : 0.0;
    const int m = std::min(involved.value({a, b}), involved.value({b, a}));
    const int denominator = frequency(a) + frequency(b) - m;
    return denominator > 0 ? double(m) / denominator : 0.0;
}

Matrix compute(const Options &options, const QList<Segment> &segments, const std::function<QString(qint64)> &textOf)
{
    Matrix matrix;
    matrix.codes = options.codes;

    // Paragraph distances ask for the same texts many times.
    QHash<qint64, QString> texts;
    const std::function<QString(qint64)> cached = [&](qint64 doc) {
        if (!texts.contains(doc))
            texts.insert(doc, textOf ? textOf(doc) : QString());
        return texts.value(doc);
    };

    QHash<qint64, DocSegments> index;
    for (qint64 c : options.codes)
        index.insert(c, byDocument(segments, c));

    for (int i = 0; i < options.codes.size(); ++i) {
        const qint64 a = options.codes[i];
        const DocSegments &sa = index[a];
        int freq = 0;
        if (options.criterion == Criterion::SameDocument)
            freq = int(sa.size());
        else
            for (const QList<Segment> &l : sa)
                freq += int(l.size());
        matrix.counts.insert(makePair(a, a), freq);

        for (int j = i + 1; j < options.codes.size(); ++j) {
            const qint64 b = options.codes[j];
            const Analysis r = analyse(options, a, b, sa, index[b], cached);
            matrix.counts.insert(makePair(a, b), r.count);
            matrix.involved.insert({a, b}, r.involvedA);
            matrix.involved.insert({b, a}, r.involvedB);
        }
    }
    return matrix;
}

QList<Retrieval::Result> passages(const Options &options, qint64 codeA, qint64 codeB, const QList<Segment> &segments,
                                  const std::function<QString(qint64)> &textOf)
{
    if (codeA == codeB) { // the diagonal: the code's own segments
        QList<Retrieval::Result> out;
        for (const Segment &s : segments)
            if (s.codeId == codeA)
                out.append({s.documentId, s.start, s.end, {s.codeId}, s.id});
        return out;
    }
    return analyse(options, codeA, codeB, byDocument(segments, codeA), byDocument(segments, codeB), textOf).passages;
}

QList<QPointF> forceLayout(const QList<QList<double>> &weights, quint32 seed, int iterations)
{
    const int n = int(weights.size());
    if (n == 0)
        return {};
    if (n == 1)
        return {QPointF(0.5, 0.5)};

    double maxWeight = 0;
    for (const QList<double> &row : weights)
        for (double w : row)
            maxWeight = std::max(maxWeight, w);

    QRandomGenerator rng(seed);
    QList<QPointF> pos(n);
    for (QPointF &p : pos)
        p = QPointF(rng.generateDouble(), rng.generateDouble());

    const double k = std::sqrt(1.0 / n);
    for (int it = 0; it < iterations; ++it) {
        const double temperature = 0.1 * (1.0 - double(it) / iterations) + 0.001;
        QList<QPointF> disp(n, QPointF(0, 0));
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                QPointF d = pos[i] - pos[j];
                double dist = std::hypot(d.x(), d.y());
                if (dist < 1e-6) { // coincident nodes: nudge apart deterministically
                    d = QPointF(1e-3 * (i + 1), 1e-3 * (j + 1));
                    dist = std::hypot(d.x(), d.y());
                }
                double force = k * k / dist; // repulsion
                const double w = maxWeight > 0 ? weights[i][j] / maxWeight : 0.0;
                force -= w * dist * dist / k; // attraction along weighted edges
                const QPointF push = d / dist * force;
                disp[i] += push;
                disp[j] -= push;
            }
        }
        for (int i = 0; i < n; ++i) {
            const double len = std::hypot(disp[i].x(), disp[i].y());
            if (len > 0)
                pos[i] += disp[i] / len * std::min(len, temperature);
        }
    }

    // Scale to fill the unit square.
    double minX = pos[0].x(), maxX = minX, minY = pos[0].y(), maxY = minY;
    for (const QPointF &p : pos) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    for (QPointF &p : pos)
        p = QPointF(maxX > minX ? (p.x() - minX) / (maxX - minX) : 0.5,
                    maxY > minY ? (p.y() - minY) / (maxY - minY) : 0.5);
    return pos;
}

} // namespace Cooccurrence
