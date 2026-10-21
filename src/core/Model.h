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

enum class MemoTarget { Project, Group, Document, Code, Segment, DocumentSet };

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
