#pragma once

#include "core/Model.h"

#include <QList>
#include <QString>
#include <QStringList>

class Project;

// Descriptive statistics of document variables and code x group cross tabulations.
// Pure logic: variable values are stored as text and converted according to the variable type.
namespace Stats {

struct Frequency {
    QString value; // stored form ("1"/"0" for booleans)
    int count = 0;
    double percent = 0; // of the valid values, in [0, 100]
};

struct VariableStats {
    VariableType type = VariableType::Text;
    int total = 0;   // values examined (one per document)
    int valid = 0;   // non-empty and valid for the type
    int empty = 0;
    int invalid = 0; // non-empty but not valid for the type ("abc" in an integer variable)
    bool numeric = false; // Integer and Decimal: the fields below are set when valid > 0
    double mean = 0;
    double median = 0;
    double stdDev = 0; // sample standard deviation (n - 1); 0 with a single value
    double min = 0;
    double max = 0;
    QList<double> values; // numeric: the valid values, sorted
    // Every type: distinct valid values, most frequent first (ties by value).
    QList<Frequency> frequencies;
};

// One value per document, in stored form; empty strings are missing values.
VariableStats describe(VariableType type, const QStringList &values);
// The same for a variable of the project over `documentIds` (empty = all documents).
VariableStats describe(const Project &project, qint64 variableId, const QList<qint64> &documentIds = {});

struct Histogram {
    QList<double> edges; // bins + 1 boundaries; every class is [edge, next) except the last, which is closed
    QList<int> counts;
};
// Equal-width classes over the numeric values; bins <= 0 picks Sturges' rule. Empty when there are no values.
Histogram histogram(const VariableStats &stats, int bins = 0);

// A column of a cross tabulation: documents that share a value, a group or a set.
struct Group {
    QString label;
    QList<qint64> documents;
};

// Documents split by the value of a variable, in order of first appearance for text and by value for
// the other types. Missing values go to a trailing "(vuoto)" group and invalid ones to "(non valido)".
// With `bins` > 0 and a numeric variable, values are put in that many equal-width classes instead.
QList<Group> groupsByVariable(const Project &project, qint64 variableId, const QList<qint64> &documentIds = {},
                              int bins = 0);
QList<Group> groupsByDocumentGroups(const Project &project, const QList<qint64> &documentIds = {});
QList<Group> groupsByDocumentSets(const Project &project, const QList<qint64> &documentIds = {});

enum class Measure {
    Segments,  // number of segments of the code in the group's documents
    Documents  // number of the group's documents that have the code
};
enum class Percent { None, OfColumn, OfRow };

struct Crosstab {
    Measure measure = Measure::Segments;
    QList<qint64> codes;           // rows
    QList<Group> columns;
    QList<QList<int>> counts;      // [row][column]
    QList<int> totalDocuments;     // per column: documents in the group

    int count(int row, int column) const { return counts.value(row).value(column); }
    int rowTotal(int row) const;
    int columnTotal(int column) const; // sum of the column's counts
    // Share of the cell in its column (denominator: the group's documents for Measure::Documents, the
    // column total for Measure::Segments) or in its row, in [0, 100]; 0 when the denominator is 0.
    double percent(int row, int column, Percent basis) const;
};

// `segments` may include other codes and documents: only the rows' codes and the columns' documents count.
Crosstab crosstab(const QList<qint64> &codes, const QList<Group> &columns, const QList<Segment> &segments,
                  Measure measure);

// Upper tail of the chi-square distribution: P(X >= x) with `df` degrees of freedom.
double chiSquareSurvival(double x, int df);

struct ChiSquare {
    bool valid = false; // false with fewer than two non-empty rows or columns
    double statistic = 0;
    int df = 0;
    double pValue = 1;
    int smallExpected = 0; // cells with expected count < 5: the approximation is then unreliable
};
// Independence test on the counts of the table; rows and columns that sum to 0 are ignored. It assumes
// each unit counts once, so it is only indicative with overlapping groups (sets) or Measure::Segments.
ChiSquare chiSquare(const Crosstab &table);

// "Transform codes into variables": one integer variable per code holding, for every document, the
// number of segments (Measure::Segments) or 0/1 (Measure::Documents) of that code. Variables are named
// after the code path and made unique with a " (n)" suffix; all in one undo step. Returns the new ids.
QList<qint64> codesToVariables(Project &project, const QList<qint64> &codes, Measure measure);

} // namespace Stats
