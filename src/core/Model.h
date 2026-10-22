#pragma once

#include <QColor>
#include <QDateTime>
#include <QList>
#include <QString>

// Plain value types for the entities stored in a Tessera project.
// Text positions are UTF-16 offsets into the document's plain text (QString indexes).

struct DocumentGroup {
    qint64 id = 0;
    QString name;
    int position = 0;
};

// A named selection of documents across groups; a document can belong to several sets.
struct DocumentSet {
    qint64 id = 0;
    QString name;
    int position = 0;
    QString uuid;
};

struct Document {
    qint64 id = 0;
    qint64 groupId = 0;
    QString name;
    QString sourcePath;
    QDateTime importedAt;
    int position = 0;
    QString uuid;
};

struct Code {
    qint64 id = 0;
    qint64 parentId = 0; // 0 = top level
    QString name;
    QColor color;
    QString description;
    int position = 0;
    QString uuid;
};

struct Segment {
    qint64 id = 0;
    qint64 documentId = 0;
    qint64 codeId = 0;
    int start = 0; // inclusive
    int end = 0;   // exclusive
    int weight = 0;
    QString comment;
    QString author;
    QDateTime createdAt;
    QString uuid;

    int length() const { return end - start; }
};

// Hyperlink between two passages, in two documents or in the same one. It can be followed from
// either end, so "from" and "to" only record the order in which the ends were chosen.
struct DocumentLink {
    qint64 id = 0;
    qint64 fromDocumentId = 0;
    int fromStart = 0; // inclusive
    int fromEnd = 0;   // exclusive
    qint64 toDocumentId = 0;
    int toStart = 0;
    int toEnd = 0;
    QString comment;
    QString author;
    QDateTime createdAt;
    QString uuid;
};

// Free: a memo with no primary target (it may still be linked to elements, see MemoLink).
enum class MemoTarget { Project, Group, Document, Code, Segment, DocumentSet, Free };

// Primary target of a memo (Group..DocumentSet, Project or Free); the memo can be linked to
// further elements through MemoLink.
struct Memo {
    qint64 id = 0;
    MemoTarget targetType = MemoTarget::Project;
    qint64 targetId = 0;
    QString title;
    QString body;
    QString author;
    QDateTime createdAt;
    QDateTime modifiedAt;
    QString uuid;
};

// An element a memo is attached to: its primary target or one of its additional links.
// Only groups, documents, codes, segments and document sets can be linked.
struct MemoLink {
    MemoTarget targetType = MemoTarget::Code;
    qint64 targetId = 0;
    bool operator==(const MemoLink &) const = default;
};

enum class VariableType { Text, Integer, Decimal, Boolean, Date };

struct Variable {
    qint64 id = 0;
    QString name;
    VariableType type = VariableType::Text;
    int position = 0;
    QString uuid;
};

QString memoTargetToString(MemoTarget t);
MemoTarget memoTargetFromString(const QString &s);
QString variableTypeToString(VariableType t);
VariableType variableTypeFromString(const QString &s);
QString variableTypeLabel(VariableType t);
