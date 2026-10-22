#pragma once

#include "core/Model.h"

#include <QHash>
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
    qint64 addDocument(qint64 groupId, const QString &name, const QString &text,
                       const QString &sourcePath = {}, const QString &uuid = {});
    bool renameDocument(qint64 id, const QString &name);
    bool moveDocument(qint64 id, qint64 groupId);
    bool deleteDocument(qint64 id);
    // Replaces the text and the document's segments atomically (used by the text editing mode).
    bool updateDocumentText(qint64 id, const QString &text, const QList<Segment> &segments);

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
    QList<Segment> segmentsForDocument(qint64 documentId) const;
    QList<Segment> segments(const QList<qint64> &documentIds, const QList<qint64> &codeIds) const;
    QList<Segment> allSegments() const;
    Segment segment(qint64 id) const;
    // Returns the id of the new segment, or of an identical existing one.
    qint64 addSegment(qint64 documentId, qint64 codeId, int start, int end, int weight = 0,
                      const QString &comment = {}, const QString &uuid = {});
    bool deleteSegment(qint64 id);
    bool setSegmentComment(qint64 id, const QString &comment);
    bool setSegmentWeight(qint64 id, int weight);
    bool setSegmentCode(qint64 id, qint64 codeId);
    QHash<qint64, int> segmentCountByCode() const;
    QHash<qint64, int> segmentCountByDocument() const;
    QHash<qint64, QHash<qint64, int>> segmentCountByCodeAndDocument() const; // code -> document -> count

    // Memos
    QList<Memo> memos() const;
    Memo memo(qint64 id) const;
    Memo memoFor(MemoTarget type, qint64 targetId) const; // id == 0 if none
    qint64 saveMemo(Memo memo);                          // inserts when memo.id == 0
    bool deleteMemo(qint64 id);
    QHash<qint64, qint64> memoIdsFor(MemoTarget type) const; // targetId -> memoId

    // Document variables
    QList<Variable> variables() const;
    qint64 addVariable(const QString &name, VariableType type, const QString &uuid = {});
    bool renameVariable(qint64 id, const QString &name);
    bool deleteVariable(qint64 id);
    QString variableValue(qint64 documentId, qint64 variableId) const;
    bool setVariableValue(qint64 documentId, qint64 variableId, const QString &value);
    QHash<qint64, QHash<qint64, QString>> allVariableValues() const; // document -> variable -> value

signals:
    void opened();
    void closed();
    void groupsChanged();
    void documentsChanged();
    void documentSetsChanged();
    void codesChanged();
    void segmentsChanged(qint64 documentId); // 0 = several documents
    void memosChanged();
    void variablesChanged();
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
};

QString newUuid();
