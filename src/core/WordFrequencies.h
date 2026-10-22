#pragma once

#include "core/Model.h"
#include "core/Stats.h"

#include <QHash>
#include <QList>
#include <QPointF>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <functional>

class Project;

// Quantitative lexical analysis: word frequencies (with a stop list and comparison columns), keyword in
// context and the layout of a word cloud. Words come from Dictionaries::tokenize, so apostrophes and
// hyphens separate words ("dell'azienda" = "dell" + "azienda") and emoji are not words.
// Pure functions on texts already read from the project, so they can run in a worker thread.
namespace Words {

// A passage to analyse: [start, end) of a document's text; end < 0 = up to the end.
struct Source {
    qint64 documentId = 0;
    int start = 0;
    int end = -1;
};

QList<Source> documentSources(const QList<qint64> &documentIds);
// The passages of the segments, by document and position; overlapping or touching passages of the same
// document are merged, so a word coded twice is counted once.
QList<Source> segmentSources(const QList<Segment> &segments);
// The texts of the sources' documents (read in the calling thread: the project is not thread-safe).
QHash<qint64, QString> loadTexts(const Project &project, const QList<Source> &sources);

// The form in which a word is counted: composed accents, lower case unless `caseSensitive`.
QString wordKey(const QString &word, bool caseSensitive);

struct Options {
    int minLength = 1;           // in characters (code points)
    bool caseSensitive = false;
    bool excludeNumbers = false; // words made only of digits
    QSet<QString> stopWords;     // in lower case; always compared ignoring case
};

struct Entry {
    QString word;
    int count = 0;
    int documents = 0;  // documents in which it occurs
    QList<int> columns; // occurrences in each comparison column
};

struct Result {
    QList<Entry> entries;    // most frequent first, then alphabetical
    int totalWords = 0;      // every word of the sources
    int countedWords = 0;    // words left after the filters: the base of percent()
    int documents = 0;       // documents analysed
    QStringList columnLabels;
    QList<int> columnWords;  // counted words in each column
    bool complete = true;    // false when the computation was cancelled

    double percent(const Entry &e) const { return countedWords > 0 ? 100.0 * e.count / countedWords : 0.0; }
    int indexOf(const QString &word) const; // -1 when missing
};

// `columns` (optional): a column per group of documents (Stats::groupsByDocumentGroups and similar).
// `progress(done, total)` is called after each source; returning false cancels.
Result frequencies(const QList<Source> &sources, const QHash<qint64, QString> &texts, const Options &options,
                   const QList<Stats::Group> &columns = {},
                   const std::function<bool(int, int)> &progress = {});

// An occurrence of a word with its context on one line (line breaks become spaces).
struct Occurrence {
    qint64 documentId = 0;
    int start = 0;
    int end = 0;
    QString left;
    QString keyword; // as written in the text
    QString right;
};

// Occurrences of `word` (compared like wordKey) in the sources, in source order, with `width` characters
// of the document's text on each side; at most `limit`.
QList<Occurrence> keywordInContext(const QList<Source> &sources, const QHash<qint64, QString> &texts,
                                   const QString &word, bool caseSensitive, int width = 60, int limit = 20000);

// Stop lists written for Tessera ("it", "en"). Words are in lower case, without apostrophes.
QStringList stopWordLanguages();
QString stopWordLanguageName(const QString &language);
QStringList builtInStopWords(const QString &language);
// Normalised form of a line of a stop list: "Dell'" -> "dell"; empty when it holds no single word.
QString stopWord(const QString &line);
// The project's stop list (meta "word_stoplist"); the Italian list when it was never set.
QStringList stopList(const Project &project);
void setStopList(Project &project, const QStringList &words);

// Adds the words as entries of a dictionary category, one undo step; returns the entries really added
// (words already in the category are not added twice).
int addToDictionary(Project &project, qint64 categoryId, const QStringList &words);

// Word cloud: places boxes of the given sizes, in order, on a spiral around (0, 0) so that no two boxes
// (enlarged by `gap`) overlap. Returns the centre of each box.
QList<QPointF> cloudLayout(const QList<QSizeF> &sizes, qreal gap = 2);

} // namespace Words
