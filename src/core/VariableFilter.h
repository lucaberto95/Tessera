#pragma once

#include "core/Model.h"

#include <QList>
#include <QString>

class Project;

// Conditions on document variables ("Genere = F AND Età >= 40") used to pick a subgroup of documents.
// Pure logic: values are stored as text, so every comparison converts them according to the type.
namespace VariableFilter {

enum class Op {
    Equals,
    NotEquals,
    Contains,
    IsEmpty,
    Less,
    LessEq,
    Greater,
    GreaterEq,
    IsTrue,
    IsFalse,
    Before,
    After,
    Between
};

struct Condition {
    qint64 variableId = 0;
    Op op = Op::Equals;
    QString value;
    QString value2; // upper bound of Between

    bool operator==(const Condition &o) const
    {
        return variableId == o.variableId && op == o.op && value == o.value && value2 == o.value2;
    }
};

QList<Op> operatorsFor(VariableType type);
QString opLabel(Op op);
bool opNeedsValue(Op op);   // false for IsEmpty, IsTrue, IsFalse
bool opNeedsSecondValue(Op op); // Between

// One stored value against one condition. An empty stored value satisfies only IsEmpty; a stored or
// condition value that is not valid for the type ("abc" in an integer variable) satisfies nothing.
bool matches(VariableType type, Op op, const QString &stored, const QString &value, const QString &value2 = {});

// Documents (in document tree order) that satisfy all the conditions, or any of them when `any`.
// No conditions means no restriction: every document. Conditions on deleted variables never match.
QList<qint64> matchingDocuments(const Project &project, const QList<Condition> &conditions, bool any);

// Last filter used, kept in the project file (not undoable: it is not project content).
struct Saved {
    QList<Condition> conditions;
    bool any = false;
};
Saved loadSaved(const Project &project);
void save(Project &project, const QList<Condition> &conditions, bool any);

} // namespace VariableFilter
