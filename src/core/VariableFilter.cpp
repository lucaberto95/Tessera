#include "core/VariableFilter.h"
#include "core/Exporters.h"
#include "core/Project.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace VariableFilter {

namespace {

const QString kMetaKey = QStringLiteral("variable_filter");

QString opKey(Op op)
{
    switch (op) {
    case Op::Equals: return QStringLiteral("eq");
    case Op::NotEquals: return QStringLiteral("ne");
    case Op::Contains: return QStringLiteral("contains");
    case Op::IsEmpty: return QStringLiteral("empty");
    case Op::Less: return QStringLiteral("lt");
    case Op::LessEq: return QStringLiteral("le");
    case Op::Greater: return QStringLiteral("gt");
    case Op::GreaterEq: return QStringLiteral("ge");
    case Op::IsTrue: return QStringLiteral("true");
    case Op::IsFalse: return QStringLiteral("false");
    case Op::Before: return QStringLiteral("before");
    case Op::After: return QStringLiteral("after");
    case Op::Between: return QStringLiteral("between");
    }
    return {};
}

bool opFromKey(const QString &key, Op *op)
{
    for (int i = int(Op::Equals); i <= int(Op::Between); ++i) {
        if (opKey(Op(i)) == key) {
            *op = Op(i);
            return true;
        }
    }
    return false;
}

// Three-way comparison of two values of a numeric or date type, both in stored form.
int compare(VariableType type, const QString &a, const QString &b)
{
    if (type == VariableType::Date) // ISO dates sort as text
        return a.compare(b);
    const double x = a.toDouble();
    const double y = b.toDouble();
    return x < y ? -1 : (x > y ? 1 : 0);
}

} // namespace

QList<Op> operatorsFor(VariableType type)
{
    switch (type) {
    case VariableType::Text:
        return {Op::Equals, Op::NotEquals, Op::Contains, Op::IsEmpty};
    case VariableType::Integer:
    case VariableType::Decimal:
        return {Op::Equals, Op::NotEquals, Op::Less, Op::LessEq, Op::Greater, Op::GreaterEq, Op::IsEmpty};
    case VariableType::Boolean:
        return {Op::IsTrue, Op::IsFalse, Op::IsEmpty};
    case VariableType::Date:
        return {Op::Equals, Op::Before, Op::After, Op::Between, Op::IsEmpty};
    }
    return {};
}

QString opLabel(Op op)
{
    switch (op) {
    case Op::Equals: return QCoreApplication::translate("VariableFilter", "=");
    case Op::NotEquals: return QCoreApplication::translate("VariableFilter", "≠");
    case Op::Contains: return QCoreApplication::translate("VariableFilter", "contiene");
    case Op::IsEmpty: return QCoreApplication::translate("VariableFilter", "è vuoto");
    case Op::Less: return QCoreApplication::translate("VariableFilter", "<");
    case Op::LessEq: return QCoreApplication::translate("VariableFilter", "≤");
    case Op::Greater: return QCoreApplication::translate("VariableFilter", ">");
    case Op::GreaterEq: return QCoreApplication::translate("VariableFilter", "≥");
    case Op::IsTrue: return QCoreApplication::translate("VariableFilter", "è vero");
    case Op::IsFalse: return QCoreApplication::translate("VariableFilter", "è falso");
    case Op::Before: return QCoreApplication::translate("VariableFilter", "prima del");
    case Op::After: return QCoreApplication::translate("VariableFilter", "dopo il");
    case Op::Between: return QCoreApplication::translate("VariableFilter", "tra");
    }
    return {};
}

bool opNeedsValue(Op op)
{
    return op != Op::IsEmpty && op != Op::IsTrue && op != Op::IsFalse;
}

bool opNeedsSecondValue(Op op)
{
    return op == Op::Between;
}

bool matches(VariableType type, Op op, const QString &stored, const QString &value, const QString &value2)
{
    if (stored.trimmed().isEmpty())
        return op == Op::IsEmpty;
    if (op == Op::IsEmpty)
        return false;

    QString s;
    if (!Exporters::normalizeVariableValue(type, stored, &s))
        return false;

    if (op == Op::IsTrue || op == Op::IsFalse) {
        if (type != VariableType::Boolean)
            return false;
        return (s == QLatin1String("1")) == (op == Op::IsTrue);
    }

    QString v;
    if (!Exporters::normalizeVariableValue(type, value, &v) || v.isEmpty())
        return false;

    if (type == VariableType::Text) {
        switch (op) {
        case Op::Equals: return s.compare(v, Qt::CaseInsensitive) == 0;
        case Op::NotEquals: return s.compare(v, Qt::CaseInsensitive) != 0;
        case Op::Contains: return s.contains(v, Qt::CaseInsensitive);
        default: return false;
        }
    }
    if (type == VariableType::Boolean) {
        switch (op) {
        case Op::Equals: return s == v;
        case Op::NotEquals: return s != v;
        default: return false;
        }
    }

    // Integer, Decimal, Date.
    const int c = compare(type, s, v);
    switch (op) {
    case Op::Equals: return c == 0;
    case Op::NotEquals: return c != 0;
    case Op::Less: return type != VariableType::Date && c < 0;
    case Op::LessEq: return type != VariableType::Date && c <= 0;
    case Op::Greater: return type != VariableType::Date && c > 0;
    case Op::GreaterEq: return type != VariableType::Date && c >= 0;
    case Op::Before: return type == VariableType::Date && c < 0;
    case Op::After: return type == VariableType::Date && c > 0;
    case Op::Between: {
        QString w;
        if (!Exporters::normalizeVariableValue(type, value2, &w) || w.isEmpty())
            return false;
        const QString lo = compare(type, v, w) <= 0 ? v : w;
        const QString hi = lo == v ? w : v;
        return compare(type, s, lo) >= 0 && compare(type, s, hi) <= 0;
    }
    default:
        return false;
    }
}

QList<qint64> matchingDocuments(const Project &project, const QList<Condition> &conditions, bool any)
{
    QHash<qint64, VariableType> types;
    for (const Variable &v : project.variables())
        types.insert(v.id, v.type);
    const auto values = project.allVariableValues();

    QList<qint64> out;
    for (const Document &d : project.documents()) {
        const auto docValues = values.value(d.id);
        bool result = !any; // AND starts true, OR starts false
        for (const Condition &c : conditions) {
            const bool ok = types.contains(c.variableId)
                && matches(types.value(c.variableId), c.op, docValues.value(c.variableId), c.value, c.value2);
            if (any && ok) {
                result = true;
                break;
            }
            if (!any && !ok) {
                result = false;
                break;
            }
        }
        if (conditions.isEmpty())
            result = true;
        if (result)
            out << d.id;
    }
    return out;
}

Saved loadSaved(const Project &project)
{
    Saved saved;
    const QJsonObject root = QJsonDocument::fromJson(project.meta(kMetaKey).toUtf8()).object();
    saved.any = root.value(QStringLiteral("any")).toBool();
    for (const QJsonValue &item : root.value(QStringLiteral("conditions")).toArray()) {
        const QJsonObject o = item.toObject();
        Condition c;
        if (!opFromKey(o.value(QStringLiteral("op")).toString(), &c.op))
            continue;
        c.variableId = o.value(QStringLiteral("variable")).toInteger();
        c.value = o.value(QStringLiteral("value")).toString();
        c.value2 = o.value(QStringLiteral("value2")).toString();
        saved.conditions << c;
    }
    return saved;
}

void save(Project &project, const QList<Condition> &conditions, bool any)
{
    QJsonArray array;
    for (const Condition &c : conditions) {
        array.append(QJsonObject{{QStringLiteral("variable"), c.variableId},
                                 {QStringLiteral("op"), opKey(c.op)},
                                 {QStringLiteral("value"), c.value},
                                 {QStringLiteral("value2"), c.value2}});
    }
    const QJsonObject root{{QStringLiteral("any"), any}, {QStringLiteral("conditions"), array}};
    project.setMeta(kMetaKey, QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

} // namespace VariableFilter
