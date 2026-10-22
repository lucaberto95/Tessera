#pragma once

#include "core/Model.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

class Project;

// Dictionary-based content analysis: occurrences of the categories of a dictionary in the documents,
// automatic coding of the occurrences and import/export of dictionaries. Matching works on words, so
// punctuation, line breaks and repeated spaces between the words of an expression do not matter.
namespace Dictionaries {

// A word of a text, as UTF-16 positions [start, end).
struct Token {
    int start = 0;
    int end = 0;
};

// Words: runs of letters, digits and combining marks. Every other character separates words, so
// "dell'azienda" gives "dell" and "azienda", and "smart-working" gives "smart" and "working".
QList<Token> tokenize(const QString &text);

// An occurrence of a pattern: positions in the text and the index of the pattern that matched.
struct Match {
    int start = 0;
    int end = 0;
    int pattern = 0;
};

// Occurrences of the patterns, without overlaps: word by word, the longest pattern that starts at the
// word wins and the search continues after it. A pattern is one or more words; '*' inside a word matches
// any run of letters ("lavor*" finds "lavoro" and "lavoratori", "*mente" finds "veramente").
// Comparison ignores case unless `caseSensitive`.
QList<Match> findMatches(const QString &text, const QStringList &patterns, bool caseSensitive = false);

struct Hit {
    qint64 documentId = 0;
    int start = 0;
    int end = 0;
};

// Result of analyze(): a category counts its own entries and those of its subcategories (each word of
// the text at most once per category).
struct Analysis {
    qint64 dictionaryId = 0;
    QList<DictCategory> categories;               // tree order
    QList<qint64> documents;                      // document tree order
    QHash<qint64, int> words;                     // document -> number of words
    QHash<qint64, QHash<qint64, int>> counts;     // category -> document -> occurrences
    QHash<qint64, QList<Hit>> hits;               // category -> occurrences, by document and position

    int count(qint64 category, qint64 document) const { return counts.value(category).value(document); }
    int total(qint64 category) const;
    int totalWords() const;
    // Occurrences as a share of the document's words, in [0, 100]; 0 for a document without words.
    double percent(qint64 category, qint64 document) const;
    int depth(qint64 category) const; // 0 = top level
};

// `documentIds` empty = every document of the project.
Analysis analyze(const Project &project, qint64 dictionaryId, const QList<qint64> &documentIds = {},
                 bool caseSensitive = false);

enum class Extent { Word, Sentence, Paragraph };

// Codes the occurrences of `categories` (in one undo step). `codes` maps a category to an existing code;
// a category missing from it (or mapped to 0) gets a code with its name, created when missing: under the
// code of its parent category, or for a top-level category under a top-level code named after the
// dictionary. *usedCodes (optional) receives the code of every coded category. Returns the number of new
// segments; occurrences that no longer fit the document's text are skipped.
int autocode(Project &project, const Analysis &analysis, const QList<qint64> &categories,
             const QHash<qint64, qint64> &codes, Extent extent, QHash<qint64, qint64> *usedCodes = nullptr);

// One integer variable per category, named "<dictionary>: <category path>" (made unique with " (n)"),
// holding the occurrences in every analysed document. One undo step; returns the new variables.
QList<qint64> countsToVariables(Project &project, const Analysis &analysis, const QList<qint64> &categories);

// Dictionary as a table: header "Categoria", "Voce", then one row per entry (and one per category
// without entries), the category written as its path "Tema > Sottotema".
QList<QStringList> toTable(const Project &project, qint64 dictionaryId);
// Adds the rows of such a table to the dictionary, creating the missing categories; a header row is
// skipped and rows with only a category create the category. One undo step; returns the entries added.
int importTable(Project &project, qint64 dictionaryId, const QList<QStringList> &rows);
// One entry per non-empty line, into the category (lines starting with '#' are comments).
int importWords(Project &project, qint64 categoryId, const QString &text);

// Files: .xlsx and .csv hold the table above; .txt is a word list that goes into `categoryId`, or into a
// new category named after the file when it is 0. Returns the entries added, or -1 with *error set.
int importFile(Project &project, qint64 dictionaryId, qint64 categoryId, const QString &path, QString *error);
// .xlsx, or CSV for Excel for any other suffix.
bool exportFile(const Project &project, qint64 dictionaryId, const QString &path, QString *error);

} // namespace Dictionaries
