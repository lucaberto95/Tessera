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

// What a document holds. Only text documents have content in documents.content; the other kinds
// are anchored by areas (pdf, image) or time ranges (audio, video).
enum class DocumentKind { Text, Pdf, Image, Audio, Video };

struct Document {
    qint64 id = 0;
    DocumentKind kind = DocumentKind::Text;
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

// Where a segment sits in its document. Text: start/end; Area: rectangle (and page, for pdf);
// Time: millisecond range.
enum class SegmentKind { Text, Area, Time };

struct Segment {
    qint64 id = 0;
    qint64 documentId = 0;
    qint64 codeId = 0;
    SegmentKind kind = SegmentKind::Text;
    int start = 0; // inclusive; text segments only (non-text segments store 0..1 internally, ignore it)
    int end = 0;   // exclusive
    // Area: PDF points (origin top left) or pixels of the original image; page is 1-based, 0 for images.
    int page = 0;
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    // Area of a pdf: the text it was coded with (selected text or the text inside the rectangle), if any.
    QString areaText;
    // Time: milliseconds from the start of the media.
    qint64 tStartMs = 0, tEndMs = 0;
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

// Dictionary for dictionary-based content analysis: a tree of categories, each with word patterns.
struct Dictionary {
    qint64 id = 0;
    QString name;
    int position = 0;
    QString uuid;
};

struct DictCategory {
    qint64 id = 0;
    qint64 dictionaryId = 0;
    qint64 parentId = 0; // 0 = top level
    QString name;
    int position = 0;
};

// A word or expression of several words; '*' matches any run of letters inside a word ("lavor*").
struct DictEntry {
    qint64 id = 0;
    qint64 categoryId = 0;
    QString pattern;
};

// The researcher's own synthesis of what a document says about a code (issue #14). At most one per
// document x code; plain text.
struct Summary {
    qint64 id = 0;
    qint64 documentId = 0;
    qint64 codeId = 0;
    QString body;
    QString author;
    QDateTime modifiedAt;
    QString uuid;
};

// A saved summary table: documents as rows, codes as columns. `configJson` is described in Summaries.h.
struct SummaryTable {
    qint64 id = 0;
    QString name;
    QString configJson;
    int position = 0;
    QString uuid;
};

// Concept maps (issue #15): a canvas of nodes joined by labelled arrows. A node is free text or points at a
// code, a document, a memo or a segment (`refId`); a node whose target was deleted stays, shown as deleted.
// Geometry is in scene units; `styleJson` holds optional {"color": "#rrggbb", "width": n, "line": "solid|dash|dot"}.
enum class MapNodeKind { Text, Code, Document, Memo, Segment };

struct ConceptMap {
    qint64 id = 0;
    QString name;
    int position = 0;
    QString uuid;
};

struct MapNode {
    qint64 id = 0;
    qint64 mapId = 0;
    MapNodeKind kind = MapNodeKind::Text;
    qint64 refId = 0;
    QString label; // text of a free node; for the others, the caption last seen (used if the target is gone)
    double x = 0, y = 0, w = 0, h = 0; // w/h == 0: size from the content
    QString styleJson;
    QString uuid;
};

struct MapEdge {
    qint64 id = 0;
    qint64 mapId = 0;
    qint64 fromNode = 0;
    qint64 toNode = 0;
    QString label;
    QString styleJson;
    QString uuid;
};

// Research questions worksheet (issue #17): a question with its status, notes, themes (one per line),
// conclusions and evidence. Evidence only refers to segments, codes and memos (never copies them).
enum class QuestionStatus { Open, InProgress, Answered };

struct ResearchQuestion {
    qint64 id = 0;
    QString title;
    QString description;
    QuestionStatus status = QuestionStatus::Open;
    QStringList themes;
    QString conclusions;
    QString notes;
    int position = 0;
    QString uuid;
};

enum class EvidenceKind { Segment, Code, Memo };

struct QuestionEvidence {
    qint64 id = 0;
    qint64 questionId = 0;
    EvidenceKind kind = EvidenceKind::Segment;
    qint64 refId = 0;
    int position = 0;
};
// A time marker of a transcript (issue #29): the text from `textPos` (UTF-16 offset) on was said at
// `timeMs` of the document's associated recording. It follows edits of the text like a segment does.
struct Timestamp {
    qint64 id = 0;
    qint64 documentId = 0;
    int textPos = 0;
    qint64 timeMs = 0;
};

// One request made to a language model (issue #39), kept for transparency and reproducibility. Not part of undo.
struct AiLogEntry {
    qint64 id = 0;
    QString function; // what the request was for (e.g. "suggerimento codici")
    QString provider;
    QString model;
    QDateTime createdAt;
    QString inputSummary; // what was sent, in words ("3 documenti, ~12.000 parole"), not the text itself
    QString output;
};

// A round of team work (issue #24): the project manager hands a copy to each member, the members code it and
// send it back, the manager merges the returned copies. Bookkeeping of the manager's project only: not part of
// undo, not merged, not exported (the copies given to the members carry no cycle history).
enum class TeamCycleStatus { Sent, Returned, Merged };
struct TeamCycle {
    qint64 id = 0;
    int number = 0;     // 1, 2, ... in order of creation of the round
    QString member;     // coder the copy was given to (the author name recorded on their codings)
    QString fileName;   // file name of the copy handed out
    TeamCycleStatus status = TeamCycleStatus::Sent;
    QDateTime sentAt;
    QDateTime returnedAt; // null until the copy came back
    QDateTime mergedAt;   // null until it was merged
    QString report;       // summary of the merge, for the history
};

QString teamCycleStatusToString(TeamCycleStatus s);
TeamCycleStatus teamCycleStatusFromString(const QString &s);

QString documentKindToString(DocumentKind k);
DocumentKind documentKindFromString(const QString &s);
QString segmentKindToString(SegmentKind k);
SegmentKind segmentKindFromString(const QString &s);
QString memoTargetToString(MemoTarget t);
MemoTarget memoTargetFromString(const QString &s);
QString variableTypeToString(VariableType t);
VariableType variableTypeFromString(const QString &s);
QString variableTypeLabel(VariableType t);
