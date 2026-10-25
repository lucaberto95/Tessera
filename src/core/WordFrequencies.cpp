#include "core/WordFrequencies.h"
#include "core/Dictionaries.h"
#include "core/Project.h"

#include <QCoreApplication>
#include <QRectF>

#include <algorithm>
#include <cmath>

namespace Words {

namespace {

const QString kStopListKey = QStringLiteral("word_stoplist");
// Prefix of the stored list: tells an empty list apart from one that was never set.
const QString kStoredMarker = QStringLiteral("#");

// Function words written for Tessera (articles, prepositions, pronouns, auxiliaries, common adverbs).
// Apostrophes split words, so elided forms appear as the stem ("dell", "l", "un").
const char *const kItalianStopWords =
    "a ad al allo alla ai agli alle all anche ancora avere aveva avevano ha hai hanno ho abbiamo avete "
    "c che chi ci cio ciò come con col coi contro cosa cui d da dal dallo dalla dai dagli dalle dall dei degli del "
    "dell della delle dello dentro di dopo dove due e ed è era erano ero eri essere essi esso fa fra fu "
    "gli già i il in infatti io l la le lei lo loro lui ma me mentre mi mia mie miei mio molto "
    "n ne né nei nel nell nella nelle nello neanche nessuno noi non nostra nostre nostri nostro o ogni oppure "
    "ormai per perché perche però pero piu più po poi può puo qua quale quali quando quanto quasi quel quell "
    "quella quelle quelli quello questa queste questi questo qui s sarà sarebbe se sei sembra sempre senza si "
    "sia siamo siete solo sono sta stata state stati stato stesso su sua sue sugli sui sul sull sulla sulle "
    "suo suoi t ti tra tre tu tua tue tuo tuoi tutta tutte tutti tutto un una uno v vi voi vostro "
    "essere sono stare fare faccio fanno fatto";

const char *const kEnglishStopWords =
    "a about after again all also am an and any are as at be because been before being both but by can could "
    "did do does doing down during each few for from further had has have having he her here hers him his how "
    "i if in into is it its just me more most my no nor not of off on once only or other our ours out over own "
    "same she should so some such than that the their theirs them then there these they this those through to "
    "too under until up very was we were what when where which while who whom why will with would you your yours "
    "d ll m re s t ve";

} // namespace

QList<Source> documentSources(const QList<qint64> &documentIds)
{
    QList<Source> out;
    for (qint64 id : documentIds)
        out.append({id, 0, -1});
    return out;
}

QList<Source> segmentSources(const QList<Segment> &segments)
{
    QList<Source> sorted;
    for (const Segment &s : segments)
        sorted.append({s.documentId, s.start, s.end});
    std::sort(sorted.begin(), sorted.end(), [](const Source &a, const Source &b) {
        return a.documentId != b.documentId ? a.documentId < b.documentId : a.start < b.start;
    });
    QList<Source> out;
    for (const Source &s : sorted) {
        if (!out.isEmpty() && out.last().documentId == s.documentId && s.start <= out.last().end)
            out.last().end = std::max(out.last().end, s.end);
        else
            out.append(s);
    }
    return out;
}

QHash<qint64, QString> loadTexts(const Project &project, const QList<Source> &sources)
{
    QHash<qint64, QString> texts;
    for (const Source &s : sources)
        if (!texts.contains(s.documentId))
            texts.insert(s.documentId, project.documentText(s.documentId));
    return texts;
}

QString wordKey(const QString &word, bool caseSensitive)
{
    const QString nfc = word.normalized(QString::NormalizationForm_C);
    return caseSensitive ? nfc : nfc.toCaseFolded();
}

int Result::indexOf(const QString &word) const
{
    for (int i = 0; i < entries.size(); ++i)
        if (entries[i].word == word)
            return i;
    return -1;
}

namespace {

// The part of the text a source covers, clamped to the text.
std::pair<int, int> sourceRange(const Source &s, const QString &text)
{
    const int start = std::clamp(s.start, 0, int(text.size()));
    const int end = s.end < 0 ? int(text.size()) : std::clamp(s.end, start, int(text.size()));
    return {start, end};
}

} // namespace

Result frequencies(const QList<Source> &sources, const QHash<qint64, QString> &texts, const Options &options,
                   const QList<Stats::Group> &columns, const std::function<bool(int, int)> &progress)
{
    Result r;
    QHash<qint64, QList<int>> documentColumns;
    for (int c = 0; c < columns.size(); ++c) {
        r.columnLabels << columns[c].label;
        for (qint64 doc : columns[c].documents)
            documentColumns[doc] << c;
    }
    r.columnWords = QList<int>(columns.size(), 0);

    struct Accumulator {
        int count = 0;
        QSet<qint64> documents;
        QList<int> columns;
    };
    QHash<QString, Accumulator> words;
    QSet<qint64> analysed;

    for (int i = 0; i < sources.size(); ++i) {
        if (progress && !progress(i, int(sources.size()))) {
            r.complete = false;
            break;
        }
        const Source &s = sources[i];
        if (!texts.contains(s.documentId))
            continue;
        analysed.insert(s.documentId);
        const QString &full = texts[s.documentId];
        const auto [start, end] = sourceRange(s, full);
        const QString part = full.mid(start, end - start);
        const QList<int> cols = documentColumns.value(s.documentId);
        for (const Dictionaries::Token &t : Dictionaries::tokenize(part)) {
            ++r.totalWords;
            const QString word = part.mid(t.start, t.end - t.start);
            const QString key = wordKey(word, options.caseSensitive);
            if (key.toUcs4().size() < options.minLength)
                continue;
            if (options.excludeNumbers &&
                std::all_of(key.cbegin(), key.cend(), [](QChar c) { return c.isDigit(); }))
                continue;
            if (!options.stopWords.isEmpty() &&
                options.stopWords.contains(options.caseSensitive ? wordKey(word, false) : key))
                continue;
            ++r.countedWords;
            Accumulator &a = words[key];
            ++a.count;
            a.documents.insert(s.documentId);
            if (a.columns.isEmpty())
                a.columns = QList<int>(columns.size(), 0);
            for (int c : cols) {
                ++a.columns[c];
                ++r.columnWords[c];
            }
        }
    }
    r.documents = int(analysed.size());

    r.entries.reserve(words.size());
    for (auto it = words.cbegin(); it != words.cend(); ++it)
        r.entries.append({it.key(), it->count, int(it->documents.size()), it->columns});
    std::sort(r.entries.begin(), r.entries.end(), [](const Entry &a, const Entry &b) {
        return a.count != b.count ? a.count > b.count : a.word < b.word;
    });
    return r;
}

QList<Occurrence> keywordInContext(const QList<Source> &sources, const QHash<qint64, QString> &texts,
                                   const QString &word, bool caseSensitive, int width, int limit)
{
    QList<Occurrence> out;
    QStringList keys;
    for (const Dictionaries::Token &t : Dictionaries::tokenize(word))
        keys << wordKey(word.mid(t.start, t.end - t.start), caseSensitive);
    if (keys.isEmpty())
        return out;

    auto oneLine = [](QString s) {
        s.replace(QLatin1Char('\n'), QLatin1Char(' '));
        return s;
    };
    for (const Source &s : sources) {
        if (!texts.contains(s.documentId))
            continue;
        const QString &full = texts[s.documentId];
        const auto [start, end] = sourceRange(s, full);
        const QString part = full.mid(start, end - start);
        const QList<Dictionaries::Token> tokens = Dictionaries::tokenize(part);
        for (int i = 0; i + keys.size() <= tokens.size(); ++i) {
            bool ok = true;
            for (int k = 0; ok && k < keys.size(); ++k)
                ok = wordKey(part.mid(tokens[i + k].start, tokens[i + k].end - tokens[i + k].start),
                             caseSensitive) == keys[k];
            if (!ok)
                continue;
            const int from = start + tokens[i].start;
            const int to = start + tokens[i + keys.size() - 1].end;
            int leftFrom = std::max(0, from - width);
            if (leftFrom > 0 && full.at(leftFrom).isLowSurrogate())
                ++leftFrom;
            int rightTo = std::min(int(full.size()), to + width);
            if (rightTo < full.size() && full.at(rightTo).isLowSurrogate())
                --rightTo;
            out.append({s.documentId, from, to, oneLine(full.mid(leftFrom, from - leftFrom)),
                        oneLine(full.mid(from, to - from)), oneLine(full.mid(to, rightTo - to))});
            if (out.size() >= limit)
                return out;
        }
    }
    return out;
}

// ---------------------------------------------------------------- stop lists

QStringList stopWordLanguages()
{
    return {QStringLiteral("it"), QStringLiteral("en")};
}

QString stopWordLanguageName(const QString &language)
{
    return language == QLatin1String("en") ? QCoreApplication::translate("Words", "Inglese") : QCoreApplication::translate("Words", "Italiano");
}

QString stopWord(const QString &line)
{
    const QString trimmed = line.trimmed();
    const QList<Dictionaries::Token> tokens = Dictionaries::tokenize(trimmed);
    if (tokens.size() != 1)
        return {};
    return wordKey(trimmed.mid(tokens[0].start, tokens[0].end - tokens[0].start), false);
}

QStringList builtInStopWords(const QString &language)
{
    const QString raw = QString::fromUtf8(language == QLatin1String("en") ? kEnglishStopWords : kItalianStopWords);
    QStringList out;
    QSet<QString> seen;
    for (const QString &w : raw.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QString key = stopWord(w);
        if (!key.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            out << key;
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

QStringList stopList(const Project &project)
{
    const QString stored = project.meta(kStopListKey);
    if (!stored.startsWith(kStoredMarker))
        return builtInStopWords(QStringLiteral("it"));
    QStringList out;
    for (const QString &line : stored.mid(1).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString key = stopWord(line);
        if (!key.isEmpty() && !out.contains(key))
            out << key;
    }
    return out;
}

void setStopList(Project &project, const QStringList &words)
{
    QStringList clean;
    QSet<QString> seen;
    for (const QString &w : words) {
        const QString key = stopWord(w);
        if (!key.isEmpty() && !seen.contains(key)) {
            seen.insert(key);
            clean << key;
        }
    }
    std::sort(clean.begin(), clean.end());
    project.setMeta(kStopListKey, kStoredMarker + clean.join(QLatin1Char('\n')));
}

int addToDictionary(Project &project, qint64 categoryId, const QStringList &words)
{
    int added = 0;
    project.setUndoLabel(QCoreApplication::translate("Words", "Aggiungi parole al dizionario"));
    project.beginBatch();
    for (const QString &w : words) {
        const qsizetype before = project.dictEntries(categoryId).size();
        if (project.addDictEntry(categoryId, w) && project.dictEntries(categoryId).size() > before)
            ++added;
    }
    project.commitBatch();
    return added;
}

// ---------------------------------------------------------------- word cloud

QList<QPointF> cloudLayout(const QList<QSizeF> &sizes, qreal gap)
{
    QList<QPointF> centres;
    QList<QRectF> placed;
    for (const QSizeF &size : sizes) {
        const QSizeF box = size + QSizeF(2 * gap, 2 * gap);
        QPointF centre;
        // Archimedean spiral, stretched in x because words are wide.
        for (qreal t = 0;; t += 0.1) {
            const qreal radius = 1.5 * t;
            centre = QPointF(1.6 * radius * std::cos(t), radius * std::sin(t));
            const QRectF rect(centre - QPointF(box.width() / 2, box.height() / 2), box);
            bool free = true;
            for (const QRectF &other : std::as_const(placed))
                if (rect.intersects(other)) {
                    free = false;
                    break;
                }
            if (free) {
                placed.append(rect);
                break;
            }
        }
        centres.append(centre);
    }
    return centres;
}

} // namespace Words
