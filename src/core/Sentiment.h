#pragma once

#include "core/Model.h"
#include "core/WordFrequencies.h"

#include <QHash>
#include <QList>
#include <QString>

class Project;

// Exploratory sentiment analysis with a small lexicon written for Tessera (Italian and English): every
// word of the lexicon has a polarity from -2 to +2, a negation ("non", "not", "mai", ...) in the few
// words before it flips and weakens it, an intensifier ("molto", "very") strengthens it and a diminisher
// ("abbastanza", "slightly") weakens it. The score is a pure function of the text, so it is reproducible.
// It is a rough aid for exploring the material, never a definitive result.
namespace Sentiment {

enum class Polarity { Negative, Neutral, Positive };
enum class Language { Auto, Italian, English }; // Auto = both lexicons together
enum class Unit { Segments, Sentences, Documents };

struct Score {
    double value = 0;  // in [-1, 1]
    int words = 0;     // words of the text
    int hits = 0;      // words of the lexicon found
    Polarity polarity = Polarity::Neutral;
};

// Scores with an absolute value below this are neutral.
constexpr double kNeutralThreshold = 0.1;

Polarity polarityOf(double value);
QString polarityName(Polarity polarity); // "Positivo", "Negativo", "Neutro" (translated)

// The lexicon polarity of a lower-case word (0 when unknown); used by the tests and the lexicon preview.
int wordPolarity(const QString &word, Language language = Language::Auto);
int lexiconSize(Language language = Language::Auto);

Score score(const QString &text, Language language = Language::Auto);

// A scored passage of a document.
struct Item {
    qint64 documentId = 0;
    int start = 0;
    int end = 0;
    QString text;
    Score score;
};

// Passages as individual items (no merging; the same passage is listed once). Segments with the same
// position count once whatever their code.
QList<Words::Source> segmentPassages(const QList<Segment> &segments);
// The sentences of the sources: ends at . ! ? … and at line breaks; empty ones are dropped.
QList<Words::Source> sentencePassages(const QList<Words::Source> &sources, const QHash<qint64, QString> &texts);

// Scores every passage (text documents only).
QList<Item> analyze(const QList<Words::Source> &passages, const QHash<qint64, QString> &texts, Language language);
// Convenience: reads the texts from the project.
QList<Item> analyze(const Project &project, Unit unit, const QList<Segment> &segments, const QList<qint64> &documentIds,
                    Language language);

// Codes the items with the sub-codes "Positivo", "Negativo" and "Neutro" of the top-level code
// `parentName` (created when missing). One undo step. Returns the number of new segments.
int codeByPolarity(Project &project, const QList<Item> &items, const QString &parentName);

} // namespace Sentiment
