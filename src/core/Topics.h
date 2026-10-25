#pragma once

#include "core/WordFrequencies.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>

class Project;

// Exploratory topic modeling: Latent Dirichlet Allocation with collapsed Gibbs sampling, written for
// Tessera (no external library). The units (whole documents, paragraphs or segments) are bags of words
// taken with Dictionaries::tokenize; the result is a set of topics, each with its characteristic words,
// and the share of every topic in every unit. It is a starting point to build a code system, not a
// finding: with short texts and few units the topics are unstable, and the number of topics is a choice.
// Pure functions on texts already read from the project, so they can run in a worker thread. With the
// same options (including the seed) the result is always the same.
namespace Topics {

struct Options {
    int topics = 5;                // number of topics; 0 = choose it automatically (see chooseTopicCount)
    int iterations = 300;          // Gibbs sweeps over every word
    quint32 seed = 1;              // the random generator is Tessera's own, so the seed gives the same result everywhere
    int minLength = 3;             // words shorter than this (in characters) are ignored
    int minUnits = 2;              // words occurring in fewer units are ignored
    double maxUnitShare = 0.85;    // words occurring in a larger share of the units are ignored (needs 5+ units)
    bool caseSensitive = false;
    bool excludeNumbers = true;
    QSet<QString> stopWords;       // lower case
    double alpha = 0;              // topic prior of a unit; 0 = automatic
    double beta = 0.01;            // word prior of a topic
    int topWords = 10;             // words kept per topic
    int topUnits = 5;              // most representative units kept per topic
    int maxAutoTopics = 10;        // upper bound of the automatic choice
};

struct TopicWord {
    QString word;
    double weight = 0; // probability of the word in the topic
};

struct Topic {
    int index = 0;           // 0-based
    QList<TopicWord> words;  // most characteristic first
    double share = 0;        // share of the analysed words that belong to the topic, 0..1
    int units = 0;           // units in which it is the main topic
    QList<int> topUnits;     // indices into Result::units, the most representative first
};

struct Result {
    QList<Topic> topics;
    QList<Words::Source> units;
    QList<QList<double>> theta; // per unit, the share of each topic (sums to 1); uniform when the unit has no words
    QList<int> dominant;        // per unit, the main topic, -1 when the unit has no usable words
    QList<int> unitWords;       // per unit, words used by the model
    int vocabulary = 0;         // distinct words used
    int tokens = 0;             // words used in total
    int requestedTopics = 0;    // Options::topics as asked (0 = automatic)
    double coherence = 0;       // mean UMass coherence of the topics: closer to 0 is better
    bool complete = true;       // false when cancelled

    bool isEmpty() const { return topics.isEmpty(); }
    double themeShare(int unit, int topic) const { return theta.value(unit).value(topic); }
};

// Splits each document into paragraphs: one per line, with consecutive short lines joined until they
// reach `minChars` or a blank line; a short remainder at the end is attached to the previous paragraph. Ranges cover the original
// text, trimmed of leading and trailing white space.
QList<Words::Source> paragraphSources(const QList<Words::Source> &sources, const QHash<qint64, QString> &texts,
                                      int minChars = 60);

// Fits the model. `progress(done, total)` is called after each sweep; returning false cancels (the result
// is then marked incomplete).
Result model(const QList<Words::Source> &units, const QHash<qint64, QString> &texts, const Options &options,
             const std::function<bool(int, int)> &progress = {});

// The automatic number of topics: the model is fitted with 2..max topics (short runs) and the smallest
// number whose coherence is close to the best one wins. A heuristic: check the topics by reading them.
int chooseTopicCount(const QList<Words::Source> &units, const QHash<qint64, QString> &texts, const Options &options,
                     const std::function<bool(int, int)> &progress = {});

// "Tema 3: lavoro, orario, casa": the name given to the code of a topic.
QString topicLabel(const Topic &topic, int words = 3);

struct CodeOptions {
    QString parentName;          // top-level code that holds the topic codes; created if missing
    bool codeUnits = false;      // also code the units that belong to a topic
    double threshold = 0.5;      // minimum share of the topic in a unit to code it
    int topicWords = 3;          // words in the name of each code
};

struct CodeResult {
    qint64 parentCode = 0;
    QList<qint64> codes;         // one per topic, in topic order
    int segments = 0;            // codings added
};

// Creates a code per topic under a top-level code (reusing existing ones of the same name), one undo
// step. With `codeUnits`, each unit is coded with the topic whose share is the largest provided it is at
// least `threshold`; the result must be reviewed.
CodeResult createCodes(Project &project, const Result &result, const CodeOptions &options,
                       const QHash<qint64, QString> &texts);

// Tables for the export (first row = header).
QList<QStringList> topicRows(const Result &result);
QList<QStringList> assignmentRows(const Result &result, const QHash<qint64, QString> &documentNames,
                                  const QHash<qint64, QString> &texts);

} // namespace Topics
