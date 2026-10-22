#include "core/Stats.h"
#include "core/Exporters.h"
#include "core/Project.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace Stats {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Stats", text);
}

bool isNumeric(VariableType type)
{
    return type == VariableType::Integer || type == VariableType::Decimal;
}

QString numberLabel(double x)
{
    return QString::number(x, 'g', 6);
}

} // namespace

VariableStats describe(VariableType type, const QStringList &values)
{
    VariableStats s;
    s.type = type;
    s.total = int(values.size());
    s.numeric = isNumeric(type);

    QList<double> numbers;
    std::map<QString, int> counts;
    for (const QString &raw : values) {
        if (raw.trimmed().isEmpty()) {
            ++s.empty;
            continue;
        }
        QString v;
        if (!Exporters::normalizeVariableValue(type, raw, &v) || v.isEmpty()) {
            ++s.invalid;
            continue;
        }
        ++s.valid;
        ++counts[v];
        if (s.numeric)
            numbers << v.toDouble();
    }

    for (const auto &[value, count] : counts)
        s.frequencies.append({value, count, s.valid ? 100.0 * count / s.valid : 0.0});
    std::stable_sort(s.frequencies.begin(), s.frequencies.end(),
                     [](const Frequency &a, const Frequency &b) { return a.count > b.count; });

    if (s.numeric && !numbers.isEmpty()) {
        std::sort(numbers.begin(), numbers.end());
        const int n = int(numbers.size());
        double sum = 0;
        for (double x : numbers)
            sum += x;
        s.values = numbers;
        s.mean = sum / n;
        s.min = numbers.first();
        s.max = numbers.last();
        s.median = n % 2 ? numbers[n / 2] : (numbers[n / 2 - 1] + numbers[n / 2]) / 2;
        if (n > 1) {
            double sq = 0;
            for (double x : numbers)
                sq += (x - s.mean) * (x - s.mean);
            s.stdDev = std::sqrt(sq / (n - 1));
        }
    }
    return s;
}

VariableStats describe(const Project &project, qint64 variableId, const QList<qint64> &documentIds)
{
    VariableType type = VariableType::Text;
    for (const Variable &v : project.variables())
        if (v.id == variableId)
            type = v.type;
    const auto values = project.allVariableValues();
    const QSet<qint64> only(documentIds.begin(), documentIds.end());
    QStringList list;
    for (const Document &d : project.documents())
        if (documentIds.isEmpty() || only.contains(d.id))
            list << values.value(d.id).value(variableId);
    return describe(type, list);
}

QList<Group> groupsByVariable(const Project &project, qint64 variableId, const QList<qint64> &documentIds, int bins)
{
    VariableType type = VariableType::Text;
    for (const Variable &v : project.variables())
        if (v.id == variableId)
            type = v.type;
    const auto values = project.allVariableValues();
    const QSet<qint64> only(documentIds.begin(), documentIds.end());

    struct Item {
        qint64 doc;
        QString value; // normalized
    };
    QList<Item> items;
    Group empty{tr("(vuoto)"), {}};
    Group invalid{tr("(non valido)"), {}};
    for (const Document &d : project.documents()) {
        if (!documentIds.isEmpty() && !only.contains(d.id))
            continue;
        const QString raw = values.value(d.id).value(variableId);
        QString v;
        if (raw.trimmed().isEmpty())
            empty.documents << d.id;
        else if (!Exporters::normalizeVariableValue(type, raw, &v) || v.isEmpty())
            invalid.documents << d.id;
        else
            items.append({d.id, v});
    }

    QList<Group> out;
    if (isNumeric(type) && bins > 0 && !items.isEmpty()) {
        double lo = items.first().value.toDouble();
        double hi = lo;
        for (const Item &it : items) {
            lo = std::min(lo, it.value.toDouble());
            hi = std::max(hi, it.value.toDouble());
        }
        const double width = (hi - lo) / bins;
        out.resize(bins);
        for (int i = 0; i < bins; ++i)
            out[i].label = QStringLiteral("[%1 – %2%3")
                               .arg(numberLabel(lo + i * width), numberLabel(lo + (i + 1) * width),
                                    i == bins - 1 ? QStringLiteral("]") : QStringLiteral(")"));
        for (const Item &it : items) {
            int bin = width > 0 ? int((it.value.toDouble() - lo) / width) : 0;
            bin = std::clamp(bin, 0, bins - 1); // the maximum belongs to the last class
            out[bin].documents << it.doc;
        }
        // Empty classes stay: their columns show the shape of the distribution.
    } else {
        QList<QString> order;
        QHash<QString, QList<qint64>> byValue;
        for (const Item &it : items) {
            if (!byValue.contains(it.value))
                order << it.value;
            byValue[it.value] << it.doc;
        }
        if (type != VariableType::Text) // numbers by magnitude, dates and booleans by stored form
            std::sort(order.begin(), order.end(), [type](const QString &a, const QString &b) {
                return isNumeric(type) ? a.toDouble() < b.toDouble() : a < b;
            });
        for (const QString &v : order) {
            QString label = v;
            if (type == VariableType::Boolean)
                label = v == QLatin1String("1") ? tr("Vero") : tr("Falso");
            out.append({label, byValue.value(v)});
        }
    }
    if (!empty.documents.isEmpty())
        out << empty;
    if (!invalid.documents.isEmpty())
        out << invalid;
    return out;
}

QList<Group> groupsByDocumentGroups(const Project &project, const QList<qint64> &documentIds)
{
    const QSet<qint64> only(documentIds.begin(), documentIds.end());
    QList<Group> out;
    for (const DocumentGroup &g : project.groups()) {
        Group group{g.name, {}};
        for (const Document &d : project.documentsInGroup(g.id))
            if (documentIds.isEmpty() || only.contains(d.id))
                group.documents << d.id;
        out << group;
    }
    return out;
}

QList<Group> groupsByDocumentSets(const Project &project, const QList<qint64> &documentIds)
{
    const QSet<qint64> only(documentIds.begin(), documentIds.end());
    QList<Group> out;
    for (const DocumentSet &s : project.documentSets()) {
        Group group{s.name, {}};
        for (qint64 id : project.documentSetMembers(s.id))
            if (documentIds.isEmpty() || only.contains(id))
                group.documents << id;
        out << group;
    }
    return out;
}

int Crosstab::rowTotal(int row) const
{
    int sum = 0;
    for (int n : counts.value(row))
        sum += n;
    return sum;
}

int Crosstab::columnTotal(int column) const
{
    int sum = 0;
    for (const QList<int> &r : counts)
        sum += r.value(column);
    return sum;
}

double Crosstab::percent(int row, int column, Percent basis) const
{
    int denominator = 0;
    if (basis == Percent::OfRow)
        denominator = rowTotal(row);
    else if (basis == Percent::OfColumn)
        denominator = measure == Measure::Documents ? totalDocuments.value(column) : columnTotal(column);
    else
        return count(row, column);
    return denominator > 0 ? 100.0 * count(row, column) / denominator : 0.0;
}

Crosstab crosstab(const QList<qint64> &codes, const QList<Group> &columns, const QList<Segment> &segments,
                  Measure measure)
{
    Crosstab t;
    t.measure = measure;
    t.codes = codes;
    t.columns = columns;
    t.counts.fill(QList<int>(columns.size(), 0), codes.size());

    QHash<qint64, int> rowOf;
    for (int i = 0; i < codes.size(); ++i)
        rowOf.insert(codes[i], i);

    // document -> columns it belongs to (a document can be in several sets)
    QHash<qint64, QList<int>> columnsOf;
    for (int c = 0; c < columns.size(); ++c) {
        t.totalDocuments << int(columns[c].documents.size());
        for (qint64 d : columns[c].documents)
            columnsOf[d] << c;
    }

    std::set<std::tuple<int, int, qint64>> seen; // (row, column, document) for Measure::Documents
    for (const Segment &s : segments) {
        const auto row = rowOf.constFind(s.codeId);
        const auto cols = columnsOf.constFind(s.documentId);
        if (row == rowOf.constEnd() || cols == columnsOf.constEnd())
            continue;
        for (int c : *cols) {
            if (measure == Measure::Segments)
                ++t.counts[*row][c];
            else if (seen.insert({*row, c, s.documentId}).second)
                ++t.counts[*row][c];
        }
    }
    return t;
}

Histogram histogram(const VariableStats &stats, int bins)
{
    Histogram h;
    if (stats.values.isEmpty())
        return h;
    if (bins <= 0)
        bins = int(std::ceil(std::log2(double(stats.values.size())) + 1));
    const double lo = stats.min;
    const double width = (stats.max - stats.min) / bins;
    for (int i = 0; i <= bins; ++i)
        h.edges << (i == bins ? stats.max : lo + i * width);
    h.counts.fill(0, bins);
    for (double x : stats.values)
        ++h.counts[width > 0 ? std::clamp(int((x - lo) / width), 0, bins - 1) : 0];
    return h;
}

namespace {

// Regularized lower incomplete gamma P(a, x): series for x < a + 1, continued fraction otherwise.
double gammaP(double a, double x)
{
    if (x <= 0)
        return 0;
    const double front = std::exp(-x + a * std::log(x) - std::lgamma(a));
    if (x < a + 1) {
        double term = 1 / a, sum = term;
        for (int n = 1; n < 1000 && std::abs(term) > std::abs(sum) * 1e-15; ++n) {
            term *= x / (a + n);
            sum += term;
        }
        return front * sum;
    }
    const double tiny = 1e-300;
    double b = x + 1 - a, c = 1 / tiny, d = 1 / b, h = d;
    for (int i = 1; i < 1000; ++i) {
        const double an = -i * (i - a);
        b += 2;
        d = an * d + b;
        if (std::abs(d) < tiny) d = tiny;
        c = b + an / c;
        if (std::abs(c) < tiny) c = tiny;
        d = 1 / d;
        const double delta = d * c;
        h *= delta;
        if (std::abs(delta - 1) < 1e-15)
            break;
    }
    return 1 - front * h;
}

} // namespace

double chiSquareSurvival(double x, int df)
{
    if (df <= 0)
        return 1;
    if (x <= 0)
        return 1;
    return std::clamp(1 - gammaP(df / 2.0, x / 2.0), 0.0, 1.0);
}

ChiSquare chiSquare(const Crosstab &table)
{
    ChiSquare r;
    QList<int> rows, cols;
    for (int i = 0; i < table.counts.size(); ++i)
        if (table.rowTotal(i) > 0)
            rows << i;
    for (int j = 0; j < table.columns.size(); ++j)
        if (table.columnTotal(j) > 0)
            cols << j;
    if (rows.size() < 2 || cols.size() < 2)
        return r;

    double total = 0;
    for (int i : rows)
        total += table.rowTotal(i);
    for (int i : rows)
        for (int j : cols) {
            const double expected = double(table.rowTotal(i)) * table.columnTotal(j) / total;
            const double diff = table.count(i, j) - expected;
            r.statistic += diff * diff / expected;
            if (expected < 5)
                ++r.smallExpected;
        }
    r.valid = true;
    r.df = int((rows.size() - 1) * (cols.size() - 1));
    r.pValue = chiSquareSurvival(r.statistic, r.df);
    return r;
}

QList<qint64> codesToVariables(Project &project, const QList<qint64> &codes, Measure measure)
{
    QList<qint64> created;
    if (codes.isEmpty())
        return created;
    const auto counts = project.segmentCountByCodeAndDocument(); // code -> document -> count
    const QList<Document> documents = project.documents();

    QSet<QString> names;
    for (const Variable &v : project.variables())
        names.insert(v.name.toLower());

    project.setUndoLabel(tr("Trasforma codici in variabili"));
    project.beginBatch();
    for (qint64 code : codes) {
        const QString base = tr("Segmenti: ") + project.codePath(code);
        QString name = base;
        for (int n = 2; names.contains(name.toLower()); ++n)
            name = QStringLiteral("%1 (%2)").arg(base).arg(n);
        names.insert(name.toLower());

        const qint64 id = project.addVariable(name, VariableType::Integer);
        if (!id)
            continue;
        created << id;
        const auto perDocument = counts.value(code);
        for (const Document &d : documents) {
            int n = perDocument.value(d.id);
            if (measure == Measure::Documents)
                n = n > 0 ? 1 : 0;
            project.setVariableValue(d.id, id, QString::number(n));
        }
    }
    project.commitBatch();
    return created;
}

} // namespace Stats
