#include "core/Topics.h"
#include "core/Dictionaries.h"
#include "core/Project.h"
#include "core/TextUtils.h"

#include <QCoreApplication>
#include <QHash>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Topics {

namespace {

// SplitMix64: small, fast and identical on every platform (std distributions are not).
class Random {
public:
    explicit Random(quint32 seed) : m_state(0x9E3779B97F4A7C15ULL ^ (quint64(seed) * 0xBF58476D1CE4E5B9ULL)) {}
    quint64 next()
    {
        quint64 z = (m_state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    double uniform() { return double(next() >> 11) * (1.0 / 9007199254740992.0); } // [0, 1)
    int below(int n) { return int(next() % quint64(n)); }

private:
    quint64 m_state;
};

struct Prepared {
    QList<Words::Source> units;
    QStringList vocabulary;
    std::vector<std::vector<int>> words;        // per unit, the word ids in order
    std::vector<std::vector<int>> wordUnits;     // per word, the units (ascending) where it occurs
    int tokens = 0;
};

struct Fit {
    int topics = 0;
    std::vector<int> nwk; // vocabulary x topics
    std::vector<int> nk;
    std::vector<int> ndk; // units x topics
    bool complete = true;
};

std::pair<int, int> unitRange(const Words::Source &s, const QString &text)
{
    const int start = std::clamp(s.start, 0, int(text.size()));
    const int end = s.end < 0 ? int(text.size()) : std::clamp(s.end, start, int(text.size()));
    return {start, end};
}

Prepared prepare(const QList<Words::Source> &units, const QHash<qint64, QString> &texts, const Options &o)
{
    Prepared p;
    p.units = units;
    const int unitCount = int(units.size());

    QList<QStringList> keys;
    keys.reserve(unitCount);
    QHash<QString, int> unitFrequency;
    for (const Words::Source &u : units) {
        QStringList list;
        const QString text = texts.value(u.documentId);
        const auto [start, end] = unitRange(u, text);
        const QString part = text.mid(start, end - start);
        QSet<QString> seen;
        for (const Dictionaries::Token &t : Dictionaries::tokenize(part)) {
            const QString raw = part.mid(t.start, t.end - t.start);
            if (raw.toUcs4().size() < o.minLength)
                continue;
            const QString key = Words::wordKey(raw, o.caseSensitive);
            if (o.excludeNumbers) {
                bool digits = true;
                for (const QChar c : key)
                    if (!c.isDigit()) {
                        digits = false;
                        break;
                    }
                if (digits)
                    continue;
            }
            if (o.stopWords.contains(key.toLower()))
                continue;
            list << key;
            if (!seen.contains(key)) {
                seen.insert(key);
                unitFrequency[key] += 1;
            }
        }
        keys << list;
    }

    const int maxUnits = unitCount >= 5 ? int(std::floor(o.maxUnitShare * unitCount)) : unitCount;
    QStringList vocabulary;
    for (auto it = unitFrequency.constBegin(); it != unitFrequency.constEnd(); ++it)
        if (it.value() >= o.minUnits && it.value() <= maxUnits)
            vocabulary << it.key();
    vocabulary.sort(); // the order of a QHash is not stable
    QHash<QString, int> ids;
    for (int i = 0; i < vocabulary.size(); ++i)
        ids.insert(vocabulary[i], i);
    p.vocabulary = vocabulary;

    p.words.resize(unitCount);
    p.wordUnits.resize(vocabulary.size());
    for (int d = 0; d < unitCount; ++d) {
        QSet<int> seen;
        for (const QString &k : keys[d]) {
            const auto found = ids.constFind(k);
            if (found == ids.constEnd())
                continue;
            p.words[d].push_back(found.value());
            if (!seen.contains(found.value())) {
                seen.insert(found.value());
                p.wordUnits[found.value()].push_back(d);
            }
        }
        p.tokens += int(p.words[d].size());
    }
    return p;
}

double automaticAlpha(const Options &o, int topics)
{
    return o.alpha > 0 ? o.alpha : std::min(1.0, 5.0 / std::max(1, topics));
}

Fit fitModel(const Prepared &p, int topics, int iterations, const Options &o, int progressBase, int progressTotal,
             const std::function<bool(int, int)> &progress)
{
    Fit f;
    f.topics = topics;
    const int V = int(p.vocabulary.size());
    const int D = int(p.units.size());
    const double alpha = automaticAlpha(o, topics);
    const double beta = o.beta;
    f.nwk.assign(size_t(V) * topics, 0);
    f.nk.assign(topics, 0);
    f.ndk.assign(size_t(D) * topics, 0);
    if (V == 0 || p.tokens == 0)
        return f;

    Random rng(o.seed);
    std::vector<std::vector<int>> z(D);
    for (int d = 0; d < D; ++d) {
        z[d].resize(p.words[d].size());
        for (size_t i = 0; i < p.words[d].size(); ++i) {
            const int k = rng.below(topics);
            z[d][i] = k;
            ++f.nwk[size_t(p.words[d][i]) * topics + k];
            ++f.nk[k];
            ++f.ndk[size_t(d) * topics + k];
        }
    }

    std::vector<double> weights(topics);
    const double vBeta = V * beta;
    for (int it = 0; it < iterations; ++it) {
        for (int d = 0; d < D; ++d) {
            for (size_t i = 0; i < p.words[d].size(); ++i) {
                const int w = p.words[d][i];
                int k = z[d][i];
                --f.nwk[size_t(w) * topics + k];
                --f.nk[k];
                --f.ndk[size_t(d) * topics + k];
                double sum = 0;
                for (int t = 0; t < topics; ++t) {
                    sum += (f.ndk[size_t(d) * topics + t] + alpha) * (f.nwk[size_t(w) * topics + t] + beta) /
                           (f.nk[t] + vBeta);
                    weights[t] = sum;
                }
                const double r = rng.uniform() * sum;
                k = 0;
                while (k < topics - 1 && weights[k] <= r)
                    ++k;
                z[d][i] = k;
                ++f.nwk[size_t(w) * topics + k];
                ++f.nk[k];
                ++f.ndk[size_t(d) * topics + k];
            }
        }
        if (progress && !progress(progressBase + it + 1, progressTotal)) {
            f.complete = false;
            return f;
        }
    }
    return f;
}

// Words of a topic, most characteristic first: a mix of the probability of the word in the topic and how
// much more likely it is there than in the whole text (so that words common to every topic come later).
QList<TopicWord> characteristicWords(const Prepared &p, const Fit &f, int topic, double beta, int count)
{
    const int V = int(p.vocabulary.size());
    constexpr double lambda = 0.6;
    struct Scored {
        int word;
        double score;
        double phi;
    };
    std::vector<Scored> scored;
    scored.reserve(V);
    std::vector<int> total(V, 0);
    for (int w = 0; w < V; ++w)
        for (int t = 0; t < f.topics; ++t)
            total[w] += f.nwk[size_t(w) * f.topics + t];
    for (int w = 0; w < V; ++w) {
        const int n = f.nwk[size_t(w) * f.topics + topic];
        if (n == 0)
            continue;
        const double phi = (n + beta) / (f.nk[topic] + V * beta);
        const double overall = double(total[w]) / std::max(1, p.tokens);
        scored.push_back({w, lambda * std::log(phi) + (1 - lambda) * std::log(phi / std::max(overall, 1e-12)), phi});
    }
    std::sort(scored.begin(), scored.end(), [&](const Scored &a, const Scored &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return p.vocabulary[a.word] < p.vocabulary[b.word];
    });
    QList<TopicWord> out;
    for (size_t i = 0; i < scored.size() && int(i) < count; ++i)
        out.append({p.vocabulary[scored[i].word], scored[i].phi});
    return out;
}

// UMass coherence of one topic: how often its best words appear together in the same units.
double umass(const Prepared &p, const QList<TopicWord> &words)
{
    QHash<QString, int> index;
    for (int i = 0; i < p.vocabulary.size(); ++i)
        index.insert(p.vocabulary[i], i);
    auto together = [&](int a, int b) {
        const auto &x = p.wordUnits[a];
        const auto &y = p.wordUnits[b];
        int n = 0;
        size_t i = 0, j = 0;
        while (i < x.size() && j < y.size()) {
            if (x[i] == y[j]) {
                ++n;
                ++i;
                ++j;
            } else if (x[i] < y[j]) {
                ++i;
            } else {
                ++j;
            }
        }
        return n;
    };
    double sum = 0;
    int pairs = 0;
    for (int m = 1; m < words.size(); ++m)
        for (int l = 0; l < m; ++l) {
            const int a = index.value(words[m].word);
            const int b = index.value(words[l].word);
            sum += std::log((together(a, b) + 1.0) / std::max<size_t>(1, p.wordUnits[b].size()));
            ++pairs;
        }
    return pairs > 0 ? sum / pairs : 0.0;
}

Result buildResult(const Prepared &p, const Fit &f, const Options &o)
{
    Result r;
    r.units = p.units;
    r.vocabulary = int(p.vocabulary.size());
    r.tokens = p.tokens;
    r.requestedTopics = o.topics;
    r.complete = f.complete;
    const int K = f.topics;
    const int D = int(p.units.size());
    if (K == 0 || p.tokens == 0)
        return r;

    // Topics from the largest to the smallest.
    std::vector<int> order(K);
    for (int k = 0; k < K; ++k)
        order[k] = k;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return f.nk[a] > f.nk[b]; });

    const double alpha = automaticAlpha(o, K);
    r.theta.reserve(D);
    r.dominant.reserve(D);
    for (int d = 0; d < D; ++d) {
        const int n = int(p.words[d].size());
        QList<double> row;
        int best = -1;
        for (int k = 0; k < K; ++k) {
            const double v = n > 0 ? (f.ndk[size_t(d) * K + order[k]] + alpha) / (n + K * alpha) : 1.0 / K;
            row << v;
            if (n > 0 && (best < 0 || v > row[best]))
                best = k;
        }
        r.theta << row;
        r.dominant << best;
        r.unitWords << n;
    }

    double coherence = 0;
    for (int k = 0; k < K; ++k) {
        Topic t;
        t.index = k;
        t.words = characteristicWords(p, f, order[k], o.beta, o.topWords);
        t.share = double(f.nk[order[k]]) / p.tokens;
        QList<int> members;
        for (int d = 0; d < D; ++d)
            if (r.dominant[d] == k)
                members << d;
        t.units = int(members.size());
        std::stable_sort(members.begin(), members.end(), [&](int a, int b) {
            if (r.theta[a][k] != r.theta[b][k])
                return r.theta[a][k] > r.theta[b][k];
            return r.unitWords[a] > r.unitWords[b];
        });
        t.topUnits = members.mid(0, o.topUnits);
        coherence += umass(p, t.words);
        r.topics << t;
    }
    r.coherence = coherence / K;
    return r;
}

int pickTopicCount(const Prepared &p, const Options &o, int *iterationsPerRun, const std::function<bool(int, int)> &progress,
                   int total, bool *cancelled)
{
    const int D = int(p.units.size());
    const int V = int(p.vocabulary.size());
    const int maxK = std::max(2, std::min({o.maxAutoTopics, D / 2, V / 2}));
    QList<double> scores;
    QList<int> counts;
    int base = 0;
    for (int k = 2; k <= maxK; ++k) {
        const Fit f = fitModel(p, k, *iterationsPerRun, o, base, total, progress);
        base += *iterationsPerRun;
        if (!f.complete) {
            *cancelled = true;
            return k;
        }
        Options probe = o;
        probe.topics = k;
        scores << buildResult(p, f, probe).coherence;
        counts << k;
    }
    if (counts.isEmpty())
        return 2;
    double best = scores.first();
    for (double s : scores)
        best = std::max(best, s);
    // Coherence keeps rising a little when a topic is split in two: take the smallest number that is
    // almost as coherent as the best one.
    const double tolerance = 0.15 * std::abs(best) + 1e-9;
    for (int i = 0; i < counts.size(); ++i)
        if (scores[i] >= best - tolerance)
            return counts[i];
    return counts.last();
}

int autoRuns(const Prepared &p, const Options &o)
{
    const int maxK = std::max(2, std::min({o.maxAutoTopics, int(p.units.size()) / 2, int(p.vocabulary.size()) / 2}));
    return maxK - 1;
}

} // namespace

// ---------------------------------------------------------------- paragraphs

QList<Words::Source> paragraphSources(const QList<Words::Source> &sources, const QHash<qint64, QString> &texts,
                                      int minChars)
{
    QList<Words::Source> out;
    for (const Words::Source &s : sources) {
        const QString text = texts.value(s.documentId);
        const auto [start, end] = unitRange(s, text);
        QList<Words::Source> pieces;
        int pos = start;
        int chunkStart = -1;
        int chunkEnd = -1;
        auto flush = [&] {
            if (chunkStart >= 0)
                pieces.append({s.documentId, chunkStart, chunkEnd});
            chunkStart = chunkEnd = -1;
        };
        while (pos <= end) {
            int lineEnd = text.indexOf(QLatin1Char('\n'), pos);
            if (lineEnd < 0 || lineEnd > end)
                lineEnd = end;
            // Trim the line.
            int a = pos, b = lineEnd;
            while (a < b && text[a].isSpace())
                ++a;
            while (b > a && text[b - 1].isSpace())
                --b;
            if (b > a) {
                if (chunkStart < 0)
                    chunkStart = a;
                chunkEnd = b;
                if (chunkEnd - chunkStart >= minChars)
                    flush();
            } else {
                flush(); // a blank line always ends a paragraph
            }
            if (lineEnd >= end)
                break;
            pos = lineEnd + 1;
        }
        if (chunkStart >= 0) {
            // A short remainder is attached to the previous paragraph.
            if (!pieces.isEmpty() && chunkEnd - chunkStart < minChars)
                pieces.last().end = chunkEnd;
            else
                flush();
        }
        out += pieces;
    }
    return out;
}

// ---------------------------------------------------------------- model

Result model(const QList<Words::Source> &units, const QHash<qint64, QString> &texts, const Options &options,
             const std::function<bool(int, int)> &progress)
{
    const Prepared p = prepare(units, texts, options);
    int topics = options.topics;
    bool cancelled = false;
    const int autoIterations = std::max(10, std::min(options.iterations, 100));
    const int autoTotal = options.topics <= 0 ? autoRuns(p, options) * autoIterations : 0;
    const int total = autoTotal + options.iterations;
    if (topics <= 0) {
        if (p.tokens == 0 || p.vocabulary.isEmpty()) {
            Result r;
            r.units = units;
            r.requestedTopics = 0;
            return r;
        }
        int it = autoIterations;
        topics = pickTopicCount(p, options, &it, progress, total, &cancelled);
        if (cancelled) {
            Result r;
            r.units = units;
            r.complete = false;
            return r;
        }
    }
    topics = std::max(1, topics);
    const Fit f = fitModel(p, topics, options.iterations, options, autoTotal, total, progress);
    return buildResult(p, f, options);
}

int chooseTopicCount(const QList<Words::Source> &units, const QHash<qint64, QString> &texts, const Options &options,
                     const std::function<bool(int, int)> &progress)
{
    const Prepared p = prepare(units, texts, options);
    if (p.tokens == 0 || p.vocabulary.isEmpty())
        return 2;
    int it = std::max(10, std::min(options.iterations, 100));
    bool cancelled = false;
    return pickTopicCount(p, options, &it, progress, autoRuns(p, options) * it, &cancelled);
}

// ---------------------------------------------------------------- codes and tables

QString topicLabel(const Topic &topic, int words)
{
    QStringList list;
    for (int i = 0; i < topic.words.size() && i < words; ++i)
        list << topic.words[i].word;
    return QCoreApplication::translate("Topics", "Tema %1: %2").arg(QString::number(topic.index + 1), list.join(QStringLiteral(", ")));
}

CodeResult createCodes(Project &project, const Result &result, const CodeOptions &options,
                       const QHash<qint64, QString> &texts)
{
    CodeResult out;
    if (result.topics.isEmpty())
        return out;
    project.setUndoLabel(QCoreApplication::translate("Topics", "Codici dai temi"));
    project.beginBatch();
    int colorIndex = int(project.codes().size());
    auto findOrAdd = [&](qint64 parent, const QString &name) {
        qint64 id = project.findCode(parent, name);
        if (!id)
            id = project.addCode(parent, name, TextUtils::paletteColor(colorIndex++));
        return id;
    };
    const QString parentName = options.parentName.trimmed().isEmpty()
                                   ? QCoreApplication::translate("Topics", "Temi (topic modeling)")
                                   : options.parentName.trimmed();
    out.parentCode = findOrAdd(0, parentName);
    for (const Topic &t : result.topics)
        out.codes << findOrAdd(out.parentCode, topicLabel(t, options.topicWords));

    if (options.codeUnits) {
        for (int u = 0; u < result.units.size(); ++u) {
            const int topic = result.dominant.value(u, -1);
            if (topic < 0 || result.themeShare(u, topic) < options.threshold)
                continue;
            const Words::Source &s = result.units[u];
            const QString text = texts.value(s.documentId);
            auto [start, end] = unitRange(s, text);
            while (start < end && text[start].isSpace())
                ++start;
            while (end > start && text[end - 1].isSpace())
                --end;
            if (end <= start)
                continue;
            if (project.addSegment(s.documentId, out.codes[topic], start, end) > 0)
                ++out.segments;
        }
    }
    project.commitBatch();
    return out;
}

QList<QStringList> topicRows(const Result &result)
{
    QList<QStringList> rows;
    rows << QStringList{QCoreApplication::translate("Topics", "Tema"), QCoreApplication::translate("Topics", "Parole caratteristiche"),
                        QCoreApplication::translate("Topics", "Quota delle parole (%)"),
                        QCoreApplication::translate("Topics", "Unità in cui è prevalente")};
    for (const Topic &t : result.topics) {
        QStringList words;
        for (const TopicWord &w : t.words)
            words << w.word;
        rows << QStringList{QString::number(t.index + 1), words.join(QStringLiteral(", ")),
                            QString::number(t.share * 100, 'f', 1), QString::number(t.units)};
    }
    return rows;
}

QList<QStringList> assignmentRows(const Result &result, const QHash<qint64, QString> &documentNames,
                                  const QHash<qint64, QString> &texts)
{
    QList<QStringList> rows;
    QStringList header{QCoreApplication::translate("Topics", "Documento"), QCoreApplication::translate("Topics", "Da"),
                       QCoreApplication::translate("Topics", "A"), QCoreApplication::translate("Topics", "Tema prevalente"),
                       QCoreApplication::translate("Topics", "Quota del tema (%)")};
    for (const Topic &t : result.topics)
        header << QCoreApplication::translate("Topics", "Tema %1 (%)").arg(t.index + 1);
    header << QCoreApplication::translate("Topics", "Estratto");
    rows << header;
    for (int u = 0; u < result.units.size(); ++u) {
        const Words::Source &s = result.units[u];
        const QString text = texts.value(s.documentId);
        const auto [start, end] = unitRange(s, text);
        const int topic = result.dominant.value(u, -1);
        QStringList line{documentNames.value(s.documentId), QString::number(start), QString::number(end),
                         topic >= 0 ? QString::number(topic + 1) : QString(),
                         topic >= 0 ? QString::number(result.themeShare(u, topic) * 100, 'f', 1) : QString()};
        for (int k = 0; k < result.topics.size(); ++k)
            line << (topic >= 0 ? QString::number(result.themeShare(u, k) * 100, 'f', 1) : QString());
        line << text.mid(start, std::min(end - start, 240)).simplified();
        rows << line;
    }
    return rows;
}

} // namespace Topics
