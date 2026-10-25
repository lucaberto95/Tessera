#pragma once

#include "core/Model.h"

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSqlDatabase>
#include <QStringList>
#include <QVariant>

// A Tessera project is a single SQLite file. Every change is written immediately,
// so there is no separate "save" step; "save copy as" uses VACUUM INTO.
class Project : public QObject {
    Q_OBJECT
public:
    static constexpr const char *FileSuffix = "tsra";

    // The base schema (version 1) never changes: every later change is a migration, applied in
    // order by both create() and open(), so new and upgraded files always have the same schema.
    struct Migration {
        int toVersion = 0;
        QStringList statements;
    };
    static constexpr int BaseSchemaVersion = 1;
    static QList<Migration> builtInMigrations();
    // Tests replace the migrations to simulate older and newer schema versions.
    void setMigrations(const QList<Migration> &migrations) { m_migrations = migrations; }
    int schemaVersion() const; // the version create() writes and open() upgrades to
    // Copy of the file taken before open() upgraded it; empty when no upgrade was needed.
    QString migrationBackupPath() const { return m_migrationBackup; }

    explicit Project(QObject *parent = nullptr);
    ~Project() override;

    bool create(const QString &path);
    bool open(const QString &path);
    void close();
    bool isOpen() const;
    QString filePath() const { return m_path; }
    QString name() const;
    QString lastError() const { return m_lastError; }
    bool saveCopyAs(const QString &path);

    // Author recorded on new segments and memos.
    QString author() const { return m_author; }
    void setAuthor(const QString &author) { m_author = author; }

    // Groups multiple writes into one transaction; signals are deferred until the outermost commit.
    // A batch is also a single undo step.
    void beginBatch();
    bool commitBatch();
    void rollbackBatch();

    // Undo/redo of every change to the project data, for the current session. Each top-level
    // operation (or batch) is one step. The label of a step is the first one set while it runs.
    bool canUndo() const { return !m_undoSteps.isEmpty(); }
    bool canRedo() const { return !m_redoSteps.isEmpty(); }
    QString undoLabel() const { return canUndo() ? m_undoSteps.last().label : QString(); }
    QString redoLabel() const { return canRedo() ? m_redoSteps.last().label : QString(); }
    bool undo();
    bool redo();
    // Labels of the steps that can be undone (most recent first) and redone (next one first).
    QStringList undoLabels() const;
    QStringList redoLabels() const;
    // Undo/redo several steps in a row; stops at the first failure. Returns false if any step failed.
    bool undoSteps(int count);
    bool redoSteps(int count);
    void clearUndoHistory();
    void setUndoLabel(const QString &label);
    static constexpr int MaxUndoSteps = 200;

    QString meta(const QString &key) const;
    void setMeta(const QString &key, const QString &value);

    // Document groups
    QList<DocumentGroup> groups() const;
    qint64 addGroup(const QString &name);
    bool renameGroup(qint64 id, const QString &name);
    bool deleteGroup(qint64 id);

    // Documents
    QList<Document> documents() const;
    QList<Document> documentsInGroup(qint64 groupId) const;
    Document document(qint64 id) const;
    QString documentText(qint64 id) const;
    // Formatting of a text document (JSON, see RichText.h); empty = plain. Ranges refer to documentText().
    QString documentRichContent(qint64 id) const;
    bool setDocumentRichContent(qint64 id, const QString &json);
    qint64 addDocument(qint64 groupId, const QString &name, const QString &text,
                       const QString &sourcePath = {}, const QString &uuid = {});
    // Document with no text (pdf, image, audio, video): its segments are areas or time ranges. The
    // binary content itself is attached by the importers of those kinds.
    qint64 addMediaDocument(qint64 groupId, const QString &name, DocumentKind kind,
                            const QString &sourcePath = {}, const QString &uuid = {});
    // Original bytes of a non-text document (v7), kept in a separate table.
    bool setDocumentData(qint64 id, const QByteArray &data);
    QByteArray documentData(qint64 id) const;
    // Image document with its file content, in one undo step. The bytes are stored as they are.
    qint64 addImageDocument(qint64 groupId, const QString &name, const QByteArray &data,
                            const QString &sourcePath = {}, const QString &uuid = {});
    // PDF document shown as pages (issue #26): the file bytes are kept as they are, `text` is the text
    // extracted at import (pages joined with '\f', see Pdfs::joinPages), stored as the document's content
    // so that lexical search, word frequencies and dictionaries see it. One undo step.
    qint64 addPdfDocument(qint64 groupId, const QString &name, const QByteArray &data, const QString &text,
                          const QString &sourcePath = {}, const QString &uuid = {});
    // Decoded image with the EXIF orientation applied; area coordinates are pixels of this image.
    // Null for other kinds or undecodable data.
    QImage documentImage(qint64 id) const;
    bool renameDocument(qint64 id, const QString &name);
    bool moveDocument(qint64 id, qint64 groupId);
    bool deleteDocument(qint64 id);
    // Replaces the text, the document's segments and the ends of its links atomically (used by the
    // text editing mode). Segments and links of the document missing from the lists are deleted.
    bool updateDocumentText(qint64 id, const QString &text, const QList<Segment> &segments,
                            const QList<DocumentLink> &links);

    // Document sets: references to documents of any group; a document can be in several sets.
    QList<DocumentSet> documentSets() const;
    QList<qint64> documentSetMembers(qint64 setId) const; // in document tree order
    qint64 addDocumentSet(const QString &name, const QList<qint64> &documentIds = {}, const QString &uuid = {});
    bool renameDocumentSet(qint64 id, const QString &name);
    bool deleteDocumentSet(qint64 id);
    bool addToDocumentSet(qint64 setId, const QList<qint64> &documentIds);
    bool removeFromDocumentSet(qint64 setId, const QList<qint64> &documentIds);

    // Codes
    QList<Code> codes() const;            // ordered by sibling position, not hierarchical
    QList<Code> codesInTreeOrder() const; // depth-first, as shown in the code tree
    Code code(qint64 id) const;
    qint64 addCode(qint64 parentId, const QString &name, const QColor &color, const QString &uuid = {});
    bool renameCode(qint64 id, const QString &name);
    bool setCodeColor(qint64 id, const QColor &color);
    bool setCodeDescription(qint64 id, const QString &description);
    // Moves a code under newParentId (0 = top level) before the sibling at `position` (-1 = append).
    bool moveCode(qint64 id, qint64 newParentId, int position = -1);
    bool deleteCode(qint64 id);
    QList<qint64> codeDescendants(qint64 id) const;
    QString codePath(qint64 id, const QString &separator = QStringLiteral(" > ")) const;
    qint64 findCode(qint64 parentId, const QString &name) const;

    // Segments (codings)
    // The lists hold text segments only, unless includeNonText is set: text-based code (document view,
    // retrieval, co-occurrences, exports) relies on start/end being real positions.
    QList<Segment> segmentsForDocument(qint64 documentId, bool includeNonText = false) const;
    QList<Segment> segments(const QList<qint64> &documentIds, const QList<qint64> &codeIds,
                            bool includeNonText = false) const;
    QList<Segment> allSegments(bool includeNonText = false) const;
    Segment segment(qint64 id) const;
    // Returns the id of the new segment, or of an identical existing one.
    qint64 addSegment(qint64 documentId, qint64 codeId, int start, int end, int weight = 0,
                      const QString &comment = {}, const QString &uuid = {});
    // Area of a pdf (page >= 1, PDF points) or image (page 0, pixels) document; x1,y1 is the top left corner.
    // `areaText` is the text of a pdf area (selected text or the text inside the rectangle).
    qint64 addAreaSegment(qint64 documentId, qint64 codeId, int page, double x1, double y1, double x2, double y2,
                          int weight = 0, const QString &comment = {}, const QString &uuid = {},
                          const QString &areaText = {});
    // Time range of an audio or video document, in milliseconds.
    qint64 addTimeSegment(qint64 documentId, qint64 codeId, qint64 startMs, qint64 endMs, int weight = 0,
                          const QString &comment = {}, const QString &uuid = {});
    bool deleteSegment(qint64 id);
    bool setSegmentComment(qint64 id, const QString &comment);
    bool setSegmentWeight(qint64 id, int weight);
    bool setSegmentCode(qint64 id, qint64 codeId);
    // Restores who wrote an item and when (the add methods stamp the current user and time); used when
    // merging projects. For summaries `when` is the modification time; for memos `modified` is the other one.
    enum class Provenance { Segment, Link, Memo, Summary };
    bool setProvenance(Provenance kind, qint64 id, const QString &author, const QDateTime &when,
                       const QDateTime &modified = {});
    QHash<qint64, int> segmentCountByCode() const;
    QHash<qint64, int> segmentCountByDocument() const;
    QHash<qint64, QHash<qint64, int>> segmentCountByCodeAndDocument() const; // code -> document -> count

    // Links between passages of documents
    QList<DocumentLink> links() const;                              // in document tree order of the "from" end
    QList<DocumentLink> linksForDocument(qint64 documentId) const;  // links with at least one end there
    DocumentLink link(qint64 id) const;
    qint64 addLink(qint64 fromDocumentId, int fromStart, int fromEnd, qint64 toDocumentId, int toStart, int toEnd,
                   const QString &comment = {}, const QString &uuid = {});
    bool setLinkComment(qint64 id, const QString &comment);
    bool deleteLink(qint64 id);

    // Memos. A memo has a primary target (an element, the project, or none for a free memo) and can
    // be linked to further elements; lookups by element consider both. A memo is deleted only when
    // none of its elements is left: if its primary target goes, its first remaining link takes over.
    QList<Memo> memos() const;
    Memo memo(qint64 id) const;
    // The memo of an element: the one whose primary target it is, else one linked to it; id == 0 if none.
    Memo memoFor(MemoTarget type, qint64 targetId) const;
    qint64 saveMemo(Memo memo);                          // inserts when memo.id == 0
    bool deleteMemo(qint64 id);
    QHash<qint64, qint64> memoIdsFor(MemoTarget type) const; // targetId -> memoId (primary or linked)
    // Elements of a memo: the primary target (if it is an element) first, then the links in order.
    QList<MemoLink> memoLinks(qint64 memoId) const;
    // Replaces the elements of a memo (one undo step). Invalid or missing elements are skipped. If the
    // primary target is not in the list, the first element takes its place, or the memo becomes free;
    // project and free memos keep their primary target.
    bool setMemoLinks(qint64 memoId, const QList<MemoLink> &links);
    bool addMemoLink(qint64 memoId, MemoTarget type, qint64 targetId); // no-op if already linked
    static bool isLinkableMemoTarget(MemoTarget type);

    // Document variables
    QList<Variable> variables() const;
    qint64 addVariable(const QString &name, VariableType type, const QString &uuid = {});
    bool renameVariable(qint64 id, const QString &name);
    bool deleteVariable(qint64 id);
    QString variableValue(qint64 documentId, qint64 variableId) const;
    bool setVariableValue(qint64 documentId, qint64 variableId, const QString &value);
    QHash<qint64, QHash<qint64, QString>> allVariableValues() const; // document -> variable -> value

    // Dictionaries for content analysis: categories form a tree inside a dictionary.
    QList<Dictionary> dictionaries() const;
    Dictionary dictionary(qint64 id) const;
    qint64 addDictionary(const QString &name, const QString &uuid = {});
    bool renameDictionary(qint64 id, const QString &name);
    bool deleteDictionary(qint64 id); // with its categories and entries
    QList<DictCategory> dictCategories(qint64 dictionaryId) const; // depth-first, as shown in the tree
    DictCategory dictCategory(qint64 id) const;
    qint64 addDictCategory(qint64 dictionaryId, qint64 parentId, const QString &name);
    bool renameDictCategory(qint64 id, const QString &name);
    bool deleteDictCategory(qint64 id); // with its subcategories and entries
    qint64 findDictCategory(qint64 dictionaryId, qint64 parentId, const QString &name) const;
    QString dictCategoryPath(qint64 id, const QString &separator = QStringLiteral(" > ")) const;
    QList<DictEntry> dictEntries(qint64 categoryId) const;                 // in order of insertion
    QList<DictEntry> dictEntriesOfDictionary(qint64 dictionaryId) const;   // every category
    // Returns the id of the new entry, or of the category's entry with the same pattern (ignoring case).
    qint64 addDictEntry(qint64 categoryId, const QString &pattern);
    bool setDictEntryPattern(qint64 id, const QString &pattern);
    bool deleteDictEntry(qint64 id);

    // Summaries (document x code) and saved summary tables. Deleting a document or a code deletes its
    // summaries. An empty (or blank) body deletes the summary. Consecutive edits of the same summary
    // with nothing else in between are merged into one undo step, so writing is one step to undo.
    QList<Summary> summaries() const;
    Summary summary(qint64 documentId, qint64 codeId) const; // id == 0 if there is none
    int summaryCountForCode(qint64 codeId) const;            // non-empty summaries
    int summaryCountForDocument(qint64 documentId) const;
    bool setSummary(qint64 documentId, qint64 codeId, const QString &body);
    QList<SummaryTable> summaryTables() const;
    SummaryTable summaryTable(qint64 id) const;
    qint64 addSummaryTable(const QString &name, const QString &configJson, const QString &uuid = {});
    bool updateSummaryTable(qint64 id, const QString &name, const QString &configJson);
    bool deleteSummaryTable(qint64 id);

    // Concept maps (issue #15). Deleting a map deletes its nodes and arrows; deleting a node deletes its arrows.
    // Nodes may point at elements that are later deleted: they are kept (see MapNode). Moving a node is one
    // updateMapNode call, so the editor calls it on mouse release only.
    QList<ConceptMap> conceptMaps() const;
    ConceptMap conceptMap(qint64 id) const;
    qint64 addConceptMap(const QString &name, const QString &uuid = {});
    bool renameConceptMap(qint64 id, const QString &name);
    bool deleteConceptMap(qint64 id);
    QList<MapNode> mapNodes(qint64 mapId) const;
    MapNode mapNode(qint64 id) const;
    qint64 addMapNode(const MapNode &node); // id/uuid ignored (uuid kept if given)
    bool updateMapNode(const MapNode &node);
    bool deleteMapNode(qint64 id);
    QList<MapEdge> mapEdges(qint64 mapId) const;
    qint64 addMapEdge(const MapEdge &edge);
    bool updateMapEdge(const MapEdge &edge);
    bool deleteMapEdge(qint64 id);
    // Research questions worksheet (issue #17). Evidence refers to segments, codes and memos and disappears
    // with the element it refers to (or with the question). An element is attached to a question only once.
    QList<ResearchQuestion> researchQuestions() const;
    ResearchQuestion researchQuestion(qint64 id) const; // id == 0 if missing
    qint64 addResearchQuestion(const QString &title, const QString &uuid = {});
    bool updateResearchQuestion(const ResearchQuestion &question); // everything but id, position and uuid
    bool deleteResearchQuestion(qint64 id);
    bool moveResearchQuestion(qint64 id, int delta);              // -1 up, +1 down
    QList<QuestionEvidence> questionEvidence(qint64 questionId) const;
    bool addQuestionEvidence(qint64 questionId, EvidenceKind kind, qint64 refId); // false if missing; true if already there
    bool removeQuestionEvidence(qint64 evidenceId);
    static QString questionStatusToString(QuestionStatus status);
    static QuestionStatus questionStatusFromString(const QString &s);
    // Transcript time stamps of a text document (issue #29, v12), ordered by position then time. They
    // follow edits of the text (updateDocumentText); deleting the document deletes them.
    QList<Timestamp> timestamps(qint64 documentId) const;
    qint64 addTimestamp(qint64 documentId, int textPos, qint64 timeMs);
    bool setTimestampTime(qint64 id, qint64 timeMs);
    bool deleteTimestamp(qint64 id);
    // Replaces every time stamp of the document (one undo step); invalid ones (position outside the text,
    // negative time) are skipped.
    bool setTimestamps(qint64 documentId, const QList<Timestamp> &stamps);
    // Moves every time stamp of the document by `deltaMs` (not below zero).
    bool shiftTimestamps(qint64 documentId, qint64 deltaMs);
    // Recording associated with a text document (a path: media can be large, so it is not embedded).
    QString documentMediaPath(qint64 documentId) const;
    bool setDocumentMediaPath(qint64 documentId, const QString &path);
    // Audio/video documents (issue #28): the recording is linked, not embedded. The stored path (media_path) is
    // relative to the folder of the project file when the recording is under it (see Media::storedPath).
    // `name` empty = the file name without suffix. Returns 0 (lastError set) if the file is not audio or video.
    qint64 addRecordingDocument(qint64 groupId, const QString &mediaFile, const QString &name = {});
    // Path to keep in the project for a recording file.
    QString storableMediaPath(const QString &mediaFile) const;
    // The recording of a document as an existing file; empty when it has none or the file is missing.
    QString resolvedMediaPath(qint64 documentId) const;

    // Log of the requests made to language models (issue #39). Optional, off by default (meta "ai_log"):
    // addAiLog does nothing and returns 0 while it is off. Not undoable, not merged, not exported. Never holds keys.
    bool aiLogEnabled() const;
    void setAiLogEnabled(bool enabled);
    qint64 addAiLog(const QString &function, const QString &provider, const QString &model,
                    const QString &inputSummary, const QString &output);
    QList<AiLogEntry> aiLog() const;
    bool clearAiLog();

    // Rounds of team work (issue #24); see Team.h for the workflow. Bookkeeping only: not undoable, not merged.
    QList<TeamCycle> teamCycles() const; // by round number, then creation
    int nextTeamCycleNumber() const;
    qint64 addTeamCycleEntry(int number, const QString &member, const QString &fileName);
    // Sent: back to "handed out"; Returned/Merged: stamps the time and keeps `report` (the merge summary).
    bool setTeamCycleStatus(qint64 id, TeamCycleStatus status, const QString &report = {});
    bool deleteTeamCycle(qint64 id);
    bool clearTeamCycles();

signals:
    void opened();
    void teamCyclesChanged();
    void summariesChanged();
    void mapsChanged();
    void researchQuestionsChanged();
    void aiLogChanged();
    void closed();
    void groupsChanged();
    void documentsChanged();
    void documentSetsChanged();
    void codesChanged();
    void segmentsChanged(qint64 documentId); // 0 = several documents
    void memosChanged();
    void variablesChanged();
    void linksChanged();
    void dictionariesChanged();
    void timestampsChanged();
    void undoStateChanged();

private:
    // Inverse SQL statements of one step, in execution order (replayed backwards).
    struct UndoStep {
        QString label;
        QStringList statements;
    };
    bool installUndoTriggers();
    void closeUndoStep();
    QStringList takeUndoLog();
    bool replay(const UndoStep &step, UndoStep *inverse);
    void emitAllChanged();

    bool initSchema();
    bool applyMigrations(int fromVersion);
    bool exec(const QString &sql, const QVariantList &binds = {}) const;
    qint64 insert(const QString &sql, const QVariantList &binds);
    QVariant scalar(const QString &sql, const QVariantList &binds = {}) const;
    bool fail(const QString &message) const;
    void notify(void (Project::*signal)());
    void notifySegments(qint64 documentId);
    int nextPosition(const QString &table, const QString &where = {}, const QVariantList &binds = {}) const;
    void removeOrphanMemos();
    bool memoTargetExists(MemoTarget type, qint64 id) const;

    QSqlDatabase m_db;
    QString m_connection;
    QString m_path;
    QString m_author;
    QList<Migration> m_migrations = builtInMigrations();
    QString m_migrationBackup;
    mutable QString m_lastError;
    int m_batchDepth = 0;
    QList<void (Project::*)()> m_pendingSignals;
    QList<qint64> m_pendingSegmentDocs;
    QList<UndoStep> m_undoSteps;
    QList<UndoStep> m_redoSteps;
    QString m_stepLabel;
    bool m_undoReady = false;
    bool m_replaying = false;
    // Undo-step merging of summary edits: the summary written by the latest step, if no step came since.
    quint64 m_stepSerial = 0;
    quint64 m_summaryStepSerial = 0;
    qint64 m_summaryDoc = 0;
    qint64 m_summaryCode = 0;
};

QString newUuid();
