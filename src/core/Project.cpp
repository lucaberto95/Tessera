#include "core/Project.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QtDebug>

#include <algorithm>
namespace {

QString isoNow() { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); }

QDateTime fromIso(const QVariant &v)
{
    QDateTime dt = QDateTime::fromString(v.toString(), Qt::ISODateWithMs);
    if (!dt.isValid())
        dt = QDateTime::fromString(v.toString(), Qt::ISODate);
    return dt;
}

QVariant idOrNull(qint64 id) { return id > 0 ? QVariant(id) : QVariant(QMetaType(QMetaType::LongLong)); }

// A null QString would be stored as SQL NULL; text columns use '' instead.
void bindAll(QSqlQuery &q, const QVariantList &binds)
{
    for (const QVariant &b : binds) {
        if (b.typeId() == QMetaType::QString && b.toString().isNull())
            q.addBindValue(QString(QLatin1String("")));
        else
            q.addBindValue(b);
    }
}

QString placeholders(qsizetype n)
{
    QStringList p;
    p.reserve(n);
    for (qsizetype i = 0; i < n; ++i)
        p << QStringLiteral("?");
    return p.join(QLatin1Char(','));
}

const char *const kSchema[] = {
    "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT)",
    "CREATE TABLE document_groups ("
    " id INTEGER PRIMARY KEY, name TEXT NOT NULL, position INTEGER NOT NULL DEFAULT 0)",
    "CREATE TABLE documents ("
    " id INTEGER PRIMARY KEY,"
    " group_id INTEGER NOT NULL REFERENCES document_groups(id) ON DELETE CASCADE,"
    " name TEXT NOT NULL, content TEXT NOT NULL DEFAULT '', source_path TEXT,"
    " imported_at TEXT, position INTEGER NOT NULL DEFAULT 0, uuid TEXT NOT NULL)",
    "CREATE TABLE codes ("
    " id INTEGER PRIMARY KEY,"
    " parent_id INTEGER REFERENCES codes(id) ON DELETE CASCADE,"
    " name TEXT NOT NULL, color TEXT NOT NULL DEFAULT '#808080', description TEXT NOT NULL DEFAULT '',"
    " position INTEGER NOT NULL DEFAULT 0, uuid TEXT NOT NULL)",
    "CREATE TABLE segments ("
    " id INTEGER PRIMARY KEY,"
    " document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
    " code_id INTEGER NOT NULL REFERENCES codes(id) ON DELETE CASCADE,"
    " start_pos INTEGER NOT NULL, end_pos INTEGER NOT NULL, weight INTEGER NOT NULL DEFAULT 0,"
    " comment TEXT NOT NULL DEFAULT '', author TEXT, created_at TEXT, uuid TEXT NOT NULL,"
    " CHECK (end_pos > start_pos))",
    "CREATE INDEX idx_segments_document ON segments(document_id, start_pos)",
    "CREATE INDEX idx_segments_code ON segments(code_id)",
    "CREATE TABLE memos ("
    " id INTEGER PRIMARY KEY, target_type TEXT NOT NULL, target_id INTEGER NOT NULL DEFAULT 0,"
    " title TEXT NOT NULL DEFAULT '', body TEXT NOT NULL DEFAULT '', author TEXT,"
    " created_at TEXT, modified_at TEXT, uuid TEXT NOT NULL)",
    "CREATE INDEX idx_memos_target ON memos(target_type, target_id)",
    "CREATE TABLE variables ("
    " id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, type TEXT NOT NULL,"
    " position INTEGER NOT NULL DEFAULT 0, uuid TEXT NOT NULL)",
    "CREATE TABLE variable_values ("
    " document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
    " variable_id INTEGER NOT NULL REFERENCES variables(id) ON DELETE CASCADE,"
    " value TEXT NOT NULL DEFAULT '', PRIMARY KEY (document_id, variable_id))",
};

// "<name> (backup v1).tsra", or "<name> (backup v1 2).tsra" if that already exists.
QString migrationBackupPathFor(const QString &path, int version)
{
    const QFileInfo info(path);
    for (int n = 1;; ++n) {
        const QString suffix = n == 1 ? QStringLiteral(" (backup v%1)").arg(version)
                                      : QStringLiteral(" (backup v%1 %2)").arg(version).arg(n);
        const QString candidate =
            info.dir().filePath(info.completeBaseName() + suffix + QLatin1Char('.') + info.suffix());
        if (!QFile::exists(candidate))
            return candidate;
    }
}

Document readDocument(const QSqlQuery &q)
{
    Document d;
    d.id = q.value(0).toLongLong();
    d.groupId = q.value(1).toLongLong();
    d.name = q.value(2).toString();
    d.sourcePath = q.value(3).toString();
    d.importedAt = fromIso(q.value(4));
    d.position = q.value(5).toInt();
    d.uuid = q.value(6).toString();
    d.kind = documentKindFromString(q.value(7).toString());
    return d;
}

Code readCode(const QSqlQuery &q)
{
    Code c;
    c.id = q.value(0).toLongLong();
    c.parentId = q.value(1).isNull() ? 0 : q.value(1).toLongLong();
    c.name = q.value(2).toString();
    c.color = QColor(q.value(3).toString());
    c.description = q.value(4).toString();
    c.position = q.value(5).toInt();
    c.uuid = q.value(6).toString();
    return c;
}

Segment readSegment(const QSqlQuery &q)
{
    Segment s;
    s.id = q.value(0).toLongLong();
    s.documentId = q.value(1).toLongLong();
    s.codeId = q.value(2).toLongLong();
    s.start = q.value(3).toInt();
    s.end = q.value(4).toInt();
    s.weight = q.value(5).toInt();
    s.comment = q.value(6).toString();
    s.author = q.value(7).toString();
    s.createdAt = fromIso(q.value(8));
    s.uuid = q.value(9).toString();
    s.kind = segmentKindFromString(q.value(10).toString());
    s.page = q.value(11).toInt();
    s.x1 = q.value(12).toDouble();
    s.y1 = q.value(13).toDouble();
    s.x2 = q.value(14).toDouble();
    s.y2 = q.value(15).toDouble();
    s.tStartMs = q.value(16).toLongLong();
    s.tEndMs = q.value(17).toLongLong();
    return s;
}

Memo readMemo(const QSqlQuery &q)
{
    Memo m;
    m.id = q.value(0).toLongLong();
    m.targetType = memoTargetFromString(q.value(1).toString());
    m.targetId = q.value(2).toLongLong();
    m.title = q.value(3).toString();
    m.body = q.value(4).toString();
    m.author = q.value(5).toString();
    m.createdAt = fromIso(q.value(6));
    m.modifiedAt = fromIso(q.value(7));
    m.uuid = q.value(8).toString();
    return m;
}

DocumentLink readLink(const QSqlQuery &q)
{
    DocumentLink l;
    l.id = q.value(0).toLongLong();
    l.fromDocumentId = q.value(1).toLongLong();
    l.fromStart = q.value(2).toInt();
    l.fromEnd = q.value(3).toInt();
    l.toDocumentId = q.value(4).toLongLong();
    l.toStart = q.value(5).toInt();
    l.toEnd = q.value(6).toInt();
    l.comment = q.value(7).toString();
    l.author = q.value(8).toString();
    l.createdAt = fromIso(q.value(9));
    l.uuid = q.value(10).toString();
    return l;
}

const QString kLinkColumns = QStringLiteral(
    "l.id, l.from_document_id, l.from_start, l.from_end, l.to_document_id, l.to_start, l.to_end, l.comment,"
    " l.author, l.created_at, l.uuid");
const QString kDocumentColumns = QStringLiteral(
    "d.id, d.group_id, d.name, d.source_path, d.imported_at, d.position, d.uuid, d.kind");
const QString kCodeColumns = QStringLiteral("id, parent_id, name, color, description, position, uuid");
const QString kSegmentColumns = QStringLiteral(
    "s.id, s.document_id, s.code_id, s.start_pos, s.end_pos, s.weight, s.comment, s.author, s.created_at, s.uuid,"
    " s.anchor_kind, s.page, s.x1, s.y1, s.x2, s.y2, s.t_start, s.t_end");
const QString kMemoColumns = QStringLiteral(
    "id, target_type, target_id, title, body, author, created_at, modified_at, uuid");

} // namespace

QString newUuid() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString documentKindToString(DocumentKind k)
{
    switch (k) {
    case DocumentKind::Pdf: return QStringLiteral("pdf");
    case DocumentKind::Image: return QStringLiteral("image");
    case DocumentKind::Audio: return QStringLiteral("audio");
    case DocumentKind::Video: return QStringLiteral("video");
    default: return QStringLiteral("text");
    }
}

DocumentKind documentKindFromString(const QString &s)
{
    for (DocumentKind k : {DocumentKind::Pdf, DocumentKind::Image, DocumentKind::Audio, DocumentKind::Video})
        if (s == documentKindToString(k))
            return k;
    return DocumentKind::Text;
}

QString segmentKindToString(SegmentKind k)
{
    switch (k) {
    case SegmentKind::Area: return QStringLiteral("area");
    case SegmentKind::Time: return QStringLiteral("time");
    default: return QStringLiteral("text");
    }
}

SegmentKind segmentKindFromString(const QString &s)
{
    if (s == QLatin1String("area"))
        return SegmentKind::Area;
    if (s == QLatin1String("time"))
        return SegmentKind::Time;
    return SegmentKind::Text;
}

QString memoTargetToString(MemoTarget t)
{
    switch (t) {
    case MemoTarget::Project: return QStringLiteral("project");
    case MemoTarget::Group: return QStringLiteral("group");
    case MemoTarget::Document: return QStringLiteral("document");
    case MemoTarget::Code: return QStringLiteral("code");
    case MemoTarget::Segment: return QStringLiteral("segment");
    case MemoTarget::DocumentSet: return QStringLiteral("document_set");
    case MemoTarget::Free: return QStringLiteral("free");
    }
    return QStringLiteral("project");
}

MemoTarget memoTargetFromString(const QString &s)
{
    if (s == QLatin1String("group")) return MemoTarget::Group;
    if (s == QLatin1String("document")) return MemoTarget::Document;
    if (s == QLatin1String("code")) return MemoTarget::Code;
    if (s == QLatin1String("segment")) return MemoTarget::Segment;
    if (s == QLatin1String("document_set")) return MemoTarget::DocumentSet;
    if (s == QLatin1String("free")) return MemoTarget::Free;
    return MemoTarget::Project;
}

QString variableTypeToString(VariableType t)
{
    switch (t) {
    case VariableType::Text: return QStringLiteral("text");
    case VariableType::Integer: return QStringLiteral("integer");
    case VariableType::Decimal: return QStringLiteral("decimal");
    case VariableType::Boolean: return QStringLiteral("boolean");
    case VariableType::Date: return QStringLiteral("date");
    }
    return QStringLiteral("text");
}

VariableType variableTypeFromString(const QString &s)
{
    if (s == QLatin1String("integer")) return VariableType::Integer;
    if (s == QLatin1String("decimal")) return VariableType::Decimal;
    if (s == QLatin1String("boolean")) return VariableType::Boolean;
    if (s == QLatin1String("date")) return VariableType::Date;
    return VariableType::Text;
}

QString variableTypeLabel(VariableType t)
{
    switch (t) {
    case VariableType::Text: return QObject::tr("Testo");
    case VariableType::Integer: return QObject::tr("Intero");
    case VariableType::Decimal: return QObject::tr("Decimale");
    case VariableType::Boolean: return QObject::tr("Sì/No");
    case VariableType::Date: return QObject::tr("Data");
    }
    return {};
}

Project::Project(QObject *parent)
    : QObject(parent)
    , m_connection(QStringLiteral("tessera_%1").arg(quintptr(this)))
{
    m_author = qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER", QStringLiteral("utente")));
}

Project::~Project() { close(); }

bool Project::fail(const QString &message) const
{
    m_lastError = message;
    qWarning().noquote() << "Project:" << message;
    return false;
}

bool Project::exec(const QString &sql, const QVariantList &binds) const
{
    QSqlQuery q(m_db);
    if (!q.prepare(sql))
        return fail(q.lastError().text() + QStringLiteral(" [") + sql + QLatin1Char(']'));
    bindAll(q, binds);
    if (!q.exec())
        return fail(q.lastError().text() + QStringLiteral(" [") + sql + QLatin1Char(']'));
    return true;
}

qint64 Project::insert(const QString &sql, const QVariantList &binds)
{
    QSqlQuery q(m_db);
    if (!q.prepare(sql)) {
        fail(q.lastError().text());
        return 0;
    }
    bindAll(q, binds);
    if (!q.exec()) {
        fail(q.lastError().text());
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

QVariant Project::scalar(const QString &sql, const QVariantList &binds) const
{
    QSqlQuery q(m_db);
    q.prepare(sql);
    bindAll(q, binds);
    if (q.exec() && q.next())
        return q.value(0);
    return {};
}

int Project::nextPosition(const QString &table, const QString &where, const QVariantList &binds) const
{
    QString sql = QStringLiteral("SELECT COALESCE(MAX(position) + 1, 0) FROM ") + table;
    if (!where.isEmpty())
        sql += QStringLiteral(" WHERE ") + where;
    return scalar(sql, binds).toInt();
}

// Every mutating method notifies after its writes; outside a batch that also closes the undo step.
void Project::notify(void (Project::*signal)())
{
    if (m_batchDepth > 0) {
        if (!m_pendingSignals.contains(signal))
            m_pendingSignals << signal;
        return;
    }
    closeUndoStep();
    (this->*signal)();
}

void Project::notifySegments(qint64 documentId)
{
    if (m_batchDepth > 0) {
        if (!m_pendingSegmentDocs.contains(documentId))
            m_pendingSegmentDocs << documentId;
        return;
    }
    closeUndoStep();
    emit segmentsChanged(documentId);
}

void Project::beginBatch()
{
    if (m_batchDepth++ == 0)
        m_db.transaction();
}

bool Project::commitBatch()
{
    if (m_batchDepth == 0)
        return false;
    if (--m_batchDepth > 0)
        return true;
    const bool ok = m_db.commit();
    closeUndoStep();
    const auto pending = std::exchange(m_pendingSignals, {});
    const auto docs = std::exchange(m_pendingSegmentDocs, {});
    for (auto signal : pending)
        (this->*signal)();
    if (docs.size() == 1)
        emit segmentsChanged(docs.first());
    else if (!docs.isEmpty())
        emit segmentsChanged(0);
    return ok;
}

void Project::rollbackBatch()
{
    if (m_batchDepth == 0)
        return;
    m_batchDepth = 0;
    m_db.rollback();
    m_pendingSignals.clear();
    m_pendingSegmentDocs.clear();
    takeUndoLog(); // already rolled back with the transaction; make sure nothing is left
    m_stepLabel.clear();
    // State may have changed partially in memory views: refresh everything.
    emitAllChanged();
}

void Project::emitAllChanged()
{
    emit groupsChanged();
    emit documentsChanged();
    emit documentSetsChanged();
    emit codesChanged();
    emit segmentsChanged(0);
    emit memosChanged();
    emit variablesChanged();
    emit linksChanged();
    emit dictionariesChanged();
}

// ---------------------------------------------------------------- undo / redo
//
// Temporary triggers (per connection, never stored in the project file) append to temp.undo_log
// the SQL statement that reverts each row change: DELETE for an insert, UPDATE with the old values
// for an update, INSERT with the old row for a delete (including rows removed by cascades).
// After each top-level operation the log is moved into an UndoStep. Undo runs the statements
// backwards; the triggers record their inverse, which becomes the redo step (and vice versa).

bool Project::installUndoTriggers()
{
    m_undoReady = false;
    if (!exec(QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS undo_log (seq INTEGER PRIMARY KEY, stmt TEXT NOT NULL)")))
        return false;
    static const char *const tables[] = {"document_groups", "documents", "codes", "segments",
                                         "memos", "variables", "variable_values",
                                         "document_sets", "document_set_members", "links", "memo_links",
                                         "dictionaries", "dict_categories", "dict_entries"};
    for (const char *table : tables) {
        const QString t = QString::fromLatin1(table);
        QStringList columns;
        QSqlQuery q(m_db);
        q.exec(QStringLiteral("PRAGMA main.table_info(%1)").arg(t));
        while (q.next())
            columns << q.value(1).toString();
        // Tables added by later migrations are absent when tests simulate an older schema.
        if (columns.isEmpty())
            continue;

        QStringList sets, names, values;
        for (QString c : columns) {
            // Column names come from the file: quote them as identifiers, and again inside the
            // string literal that builds the inverse statement, so no name can inject SQL.
            c.replace(QLatin1Char('"'), QStringLiteral("\"\""));
            const QString inLiteral = QString(c).replace(QLatin1Char('\''), QStringLiteral("''"));
            sets << QStringLiteral("\"%1\"='||quote(OLD.\"%2\")||'").arg(inLiteral, c);
            names << QStringLiteral("\"%1\"").arg(inLiteral);
            values << QStringLiteral("'||quote(OLD.\"%1\")||'").arg(c);
        }
        // Trigger bodies may not qualify table names; undo_log only exists in the temp schema.
        const QString log = QStringLiteral("INSERT INTO undo_log(stmt) VALUES(");
        const QStringList triggers = {
            QStringLiteral("CREATE TEMP TRIGGER IF NOT EXISTS undo_%1_ins AFTER INSERT ON main.%1 BEGIN ").arg(t) + log +
                QStringLiteral("'DELETE FROM main.%1 WHERE rowid='||NEW.rowid); END").arg(t),
            QStringLiteral("CREATE TEMP TRIGGER IF NOT EXISTS undo_%1_upd AFTER UPDATE ON main.%1 BEGIN ").arg(t) + log +
                QStringLiteral("'UPDATE main.%1 SET ").arg(t) + sets.join(QLatin1Char(',')) +
                QStringLiteral(" WHERE rowid='||OLD.rowid); END"),
            QStringLiteral("CREATE TEMP TRIGGER IF NOT EXISTS undo_%1_del AFTER DELETE ON main.%1 BEGIN ").arg(t) + log +
                QStringLiteral("'INSERT INTO main.%1(rowid,").arg(t) + names.join(QLatin1Char(',')) +
                QStringLiteral(") VALUES('||OLD.rowid||',") + values.join(QLatin1Char(',')) + QStringLiteral(")'); END"),
        };
        for (const QString &sql : triggers)
            if (!exec(sql))
                return false;
    }
    m_undoReady = true;
    return true;
}

QStringList Project::takeUndoLog()
{
    QStringList out;
    if (!m_db.isOpen())
        return out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT stmt FROM temp.undo_log ORDER BY seq")))
        return out;
    while (q.next())
        out << q.value(0).toString();
    if (!out.isEmpty())
        exec(QStringLiteral("DELETE FROM temp.undo_log"));
    return out;
}

void Project::setUndoLabel(const QString &label)
{
    // Inside a batch the first label wins (the outer operation names the whole step).
    if (m_batchDepth == 0 || m_stepLabel.isEmpty())
        m_stepLabel = label;
}

void Project::closeUndoStep()
{
    if (!m_undoReady || m_replaying || m_batchDepth > 0)
        return;
    const QString label = std::exchange(m_stepLabel, {});
    const QStringList statements = takeUndoLog();
    if (statements.isEmpty())
        return;
    m_undoSteps.append({label.isEmpty() ? tr("Modifica") : label, statements});
    while (m_undoSteps.size() > MaxUndoSteps)
        m_undoSteps.removeFirst();
    m_redoSteps.clear();
    emit undoStateChanged();
}

bool Project::replay(const UndoStep &step, UndoStep *inverse)
{
    m_replaying = true;
    takeUndoLog();
    bool ok = m_db.transaction();
    // Rows may be restored in any order (parents after children): check references at commit.
    ok = ok && exec(QStringLiteral("PRAGMA defer_foreign_keys = ON"));
    for (qsizetype i = step.statements.size() - 1; ok && i >= 0; --i)
        ok = exec(step.statements.at(i));
    if (ok) {
        inverse->label = step.label;
        inverse->statements = takeUndoLog();
        ok = m_db.commit();
    }
    if (!ok) {
        m_db.rollback();
        takeUndoLog();
    }
    m_replaying = false;
    return ok;
}

bool Project::undo()
{
    if (!canUndo() || m_batchDepth > 0)
        return false;
    const UndoStep step = m_undoSteps.takeLast();
    UndoStep redoStep;
    if (!replay(step, &redoStep)) {
        m_undoSteps.append(step);
        return fail(tr("Impossibile annullare: %1").arg(m_lastError));
    }
    m_redoSteps.append(redoStep);
    emitAllChanged();
    emit undoStateChanged();
    return true;
}

bool Project::redo()
{
    if (!canRedo() || m_batchDepth > 0)
        return false;
    const UndoStep step = m_redoSteps.takeLast();
    UndoStep undoStep;
    if (!replay(step, &undoStep)) {
        m_redoSteps.append(step);
        return fail(tr("Impossibile ripetere: %1").arg(m_lastError));
    }
    m_undoSteps.append(undoStep);
    emitAllChanged();
    emit undoStateChanged();
    return true;
}

void Project::clearUndoHistory()
{
    takeUndoLog();
    m_stepLabel.clear();
    const bool had = canUndo() || canRedo();
    m_undoSteps.clear();
    m_redoSteps.clear();
    if (had)
        emit undoStateChanged();
}

bool Project::initSchema()
{
    m_db.transaction();
    for (const char *stmt : kSchema) {
        if (!exec(QString::fromLatin1(stmt))) {
            m_db.rollback();
            return false;
        }
    }
    exec(QStringLiteral("INSERT INTO meta(key, value) VALUES ('schema_version', ?)"), {BaseSchemaVersion});
    exec(QStringLiteral("INSERT INTO meta(key, value) VALUES ('created_at', ?)"), {isoNow()});
    exec(QStringLiteral("INSERT INTO meta(key, value) VALUES ('created_by', ?)"), {m_author});
    exec(QStringLiteral("INSERT INTO meta(key, value) VALUES ('project_uuid', ?)"), {newUuid()});
    exec(QStringLiteral("INSERT INTO document_groups(name, position) VALUES (?, 0)"), {tr("Documenti")});
    return m_db.commit();
}

// ---------------------------------------------------------------- schema migrations
//
// To change the schema, append a Migration with the next version number (one SQL statement per
// entry); never edit kSchema or a migration that has been released. A new table must also be added to installUndoTriggers();
// new columns of existing tables are picked up automatically.

QList<Project::Migration> Project::builtInMigrations()
{
    return {
        {2,
         {// Document sets (issue #2).
          QStringLiteral("CREATE TABLE document_sets ("
                         " id INTEGER PRIMARY KEY, name TEXT NOT NULL, position INTEGER NOT NULL DEFAULT 0,"
                         " uuid TEXT NOT NULL)"),
          QStringLiteral("CREATE TABLE document_set_members ("
                         " set_id INTEGER NOT NULL REFERENCES document_sets(id) ON DELETE CASCADE,"
                         " document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                         " PRIMARY KEY (set_id, document_id))"),
          QStringLiteral("CREATE INDEX idx_document_set_members_document ON document_set_members(document_id)")}},
        {3,
         {// Links between passages of documents (issue #8).
          QStringLiteral("CREATE TABLE links ("
                         " id INTEGER PRIMARY KEY,"
                         " from_document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                         " from_start INTEGER NOT NULL, from_end INTEGER NOT NULL,"
                         " to_document_id INTEGER NOT NULL REFERENCES documents(id) ON DELETE CASCADE,"
                         " to_start INTEGER NOT NULL, to_end INTEGER NOT NULL,"
                         " comment TEXT NOT NULL DEFAULT '', author TEXT, created_at TEXT, uuid TEXT NOT NULL,"
                         " CHECK (from_end > from_start AND to_end > to_start))"),
          QStringLiteral("CREATE INDEX idx_links_from ON links(from_document_id)"),
          QStringLiteral("CREATE INDEX idx_links_to ON links(to_document_id)")}},
        {4,
         {// Memos linked to further elements besides their primary target (issue #9).
          QStringLiteral("CREATE TABLE memo_links ("
                         " id INTEGER PRIMARY KEY,"
                         " memo_id INTEGER NOT NULL REFERENCES memos(id) ON DELETE CASCADE,"
                         " target_type TEXT NOT NULL, target_id INTEGER NOT NULL,"
                         " UNIQUE (memo_id, target_type, target_id))"),
          QStringLiteral("CREATE INDEX idx_memo_links_target ON memo_links(target_type, target_id)")}},
        {5,
         {// Dictionaries for content analysis (issue #19).
          QStringLiteral("CREATE TABLE dictionaries ("
                         " id INTEGER PRIMARY KEY, name TEXT NOT NULL, position INTEGER NOT NULL DEFAULT 0,"
                         " uuid TEXT NOT NULL)"),
          QStringLiteral("CREATE TABLE dict_categories ("
                         " id INTEGER PRIMARY KEY,"
                         " dictionary_id INTEGER NOT NULL REFERENCES dictionaries(id) ON DELETE CASCADE,"
                         " parent_id INTEGER REFERENCES dict_categories(id) ON DELETE CASCADE,"
                         " name TEXT NOT NULL, position INTEGER NOT NULL DEFAULT 0)"),
          QStringLiteral("CREATE INDEX idx_dict_categories_dictionary ON dict_categories(dictionary_id)"),
          QStringLiteral("CREATE TABLE dict_entries ("
                         " id INTEGER PRIMARY KEY,"
                         " category_id INTEGER NOT NULL REFERENCES dict_categories(id) ON DELETE CASCADE,"
                         " pattern TEXT NOT NULL)"),
          QStringLiteral("CREATE INDEX idx_dict_entries_category ON dict_entries(category_id)")}},
        {6,
         {// Generalized segments (issue #25): document kind and non-text anchors. Added as nullable/defaulted
          // columns, so no table is rebuilt and existing rows are untouched. Non-text segments store
          // start_pos = 0, end_pos = 1 to satisfy the original CHECK (end_pos > start_pos).
          QStringLiteral("ALTER TABLE documents ADD COLUMN kind TEXT NOT NULL DEFAULT 'text'"),
          QStringLiteral("ALTER TABLE segments ADD COLUMN anchor_kind TEXT NOT NULL DEFAULT 'text'"),
          QStringLiteral("ALTER TABLE segments ADD COLUMN page INTEGER"),
          QStringLiteral("ALTER TABLE segments ADD COLUMN x1 REAL"),
          QStringLiteral("ALTER TABLE segments ADD COLUMN y1 REAL"),
          QStringLiteral("ALTER TABLE segments ADD COLUMN x2 REAL"),
          QStringLiteral("ALTER TABLE segments ADD COLUMN y2 REAL"),
          QStringLiteral("ALTER TABLE segments ADD COLUMN t_start INTEGER"),
          QStringLiteral("ALTER TABLE segments ADD COLUMN t_end INTEGER")}},
    };
}

int Project::schemaVersion() const
{
    int version = BaseSchemaVersion;
    for (const Migration &m : m_migrations)
        version = qMax(version, m.toVersion);
    return version;
}

// Runs before foreign keys are enabled, so a migration can rebuild a table (create, copy, drop,
// rename) without the drop cascading to the rows that reference it. All pending migrations form
// one transaction: if any fails, the file stays at its previous version.
bool Project::applyMigrations(int fromVersion)
{
    QList<Migration> pending;
    for (const Migration &m : m_migrations)
        if (m.toVersion > fromVersion)
            pending << m;
    if (pending.isEmpty())
        return true;
    std::sort(pending.begin(), pending.end(),
              [](const Migration &a, const Migration &b) { return a.toVersion < b.toVersion; });

    if (!m_db.transaction())
        return fail(m_db.lastError().text());
    bool ok = true;
    for (const Migration &m : pending) {
        for (const QString &stmt : m.statements)
            ok = ok && exec(stmt);
        ok = ok && exec(QStringLiteral("UPDATE meta SET value = ? WHERE key = 'schema_version'"), {m.toVersion});
    }
    if (ok) {
        QSqlQuery check(m_db);
        if (!check.exec(QStringLiteral("PRAGMA foreign_key_check")))
            ok = fail(check.lastError().text());
        else if (check.next())
            ok = fail(tr("la tabella %1 contiene riferimenti non validi").arg(check.value(0).toString()));
    }
    if (ok && !m_db.commit())
        ok = fail(m_db.lastError().text());
    if (!ok)
        m_db.rollback();
    return ok;
}

bool Project::create(const QString &path)
{
    close();
    if (QFile::exists(path) && !QFile::remove(path))
        return fail(tr("Impossibile sovrascrivere il file %1").arg(path));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(path);
    if (!m_db.open())
        return fail(m_db.lastError().text());
    if (!initSchema() || !applyMigrations(BaseSchemaVersion)
        || !exec(QStringLiteral("PRAGMA foreign_keys = ON")) || !installUndoTriggers()) {
        const QString err = m_lastError;
        close();
        return fail(err);
    }
    m_path = QFileInfo(path).absoluteFilePath();
    emit opened();
    return true;
}

bool Project::open(const QString &path)
{
    close();
    if (!QFile::exists(path))
        return fail(tr("Il file %1 non esiste").arg(path));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(path);
    if (!m_db.open())
        return fail(m_db.lastError().text());
    const QVariant version = scalar(QStringLiteral("SELECT value FROM meta WHERE key = 'schema_version'"));
    if (!version.isValid()) {
        close();
        return fail(tr("%1 non è un progetto Tessera").arg(QFileInfo(path).fileName()));
    }
    const int fileVersion = version.toInt();
    if (fileVersion > schemaVersion()) {
        close();
        return fail(tr("Il progetto è stato creato con una versione più recente di Tessera"));
    }
    if (fileVersion < schemaVersion()) {
        const QString backup = migrationBackupPathFor(path, fileVersion);
        if (!exec(QStringLiteral("VACUUM INTO ?"), {backup})) {
            const QString err = m_lastError;
            close();
            return fail(tr("Impossibile creare la copia di sicurezza prima di aggiornare il progetto: %1").arg(err));
        }
        if (!applyMigrations(fileVersion)) {
            const QString err = m_lastError;
            close();
            QFile::remove(backup); // the original is untouched, so the copy is not needed
            return fail(tr("Impossibile aggiornare il progetto alla versione %1: %2\n"
                           "Il file non è stato modificato.")
                            .arg(schemaVersion())
                            .arg(err));
        }
        m_migrationBackup = backup;
    }
    if (!exec(QStringLiteral("PRAGMA foreign_keys = ON")) || !installUndoTriggers()) {
        const QString err = m_lastError;
        close();
        return fail(err);
    }
    m_path = QFileInfo(path).absoluteFilePath();
    emit opened();
    return true;
}

void Project::close()
{
    if (!m_db.isValid())
        return;
    const bool wasOpen = m_db.isOpen();
    m_undoReady = false;
    m_undoSteps.clear();
    m_redoSteps.clear();
    m_stepLabel.clear();
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
    m_path.clear();
    m_migrationBackup.clear();
    m_batchDepth = 0;
    m_pendingSignals.clear();
    m_pendingSegmentDocs.clear();
    if (wasOpen) {
        emit undoStateChanged();
        emit closed();
    }
}

bool Project::isOpen() const { return m_db.isValid() && m_db.isOpen(); }

QString Project::name() const { return QFileInfo(m_path).completeBaseName(); }

bool Project::saveCopyAs(const QString &path)
{
    if (QFile::exists(path) && !QFile::remove(path))
        return fail(tr("Impossibile sovrascrivere il file %1").arg(path));
    return exec(QStringLiteral("VACUUM INTO ?"), {path});
}

QString Project::meta(const QString &key) const
{
    return scalar(QStringLiteral("SELECT value FROM meta WHERE key = ?"), {key}).toString();
}

void Project::setMeta(const QString &key, const QString &value)
{
    exec(QStringLiteral("INSERT INTO meta(key, value) VALUES (?, ?) "
                        "ON CONFLICT(key) DO UPDATE SET value = excluded.value"),
         {key, value});
}

// A memo is orphaned only when none of its elements is left. Links to deleted elements go first;
// a memo whose primary target was deleted then moves to its first remaining link.
void Project::removeOrphanMemos()
{
    // Unqualified columns: the condition applies to both memos and memo_links.
    const QString missing = QStringLiteral(
        "((target_type = 'group' AND target_id NOT IN (SELECT id FROM document_groups)) OR "
        "(target_type = 'document' AND target_id NOT IN (SELECT id FROM documents)) OR "
        "(target_type = 'code' AND target_id NOT IN (SELECT id FROM codes)) OR "
        "(target_type = 'segment' AND target_id NOT IN (SELECT id FROM segments)) OR "
        "(target_type = 'document_set' AND target_id NOT IN (SELECT id FROM document_sets)))");
    exec(QStringLiteral("DELETE FROM memo_links WHERE ") + missing);
    exec(QStringLiteral(
             "UPDATE memos SET"
             " target_type = (SELECT l.target_type FROM memo_links l WHERE l.memo_id = memos.id ORDER BY l.id LIMIT 1),"
             " target_id = (SELECT l.target_id FROM memo_links l WHERE l.memo_id = memos.id ORDER BY l.id LIMIT 1)"
             " WHERE id IN (SELECT memo_id FROM memo_links) AND ") +
         missing);
    // The promoted link is now the primary target.
    exec(QStringLiteral("DELETE FROM memo_links WHERE id IN (SELECT l.id FROM memo_links l JOIN memos m"
                        " ON m.id = l.memo_id AND m.target_type = l.target_type AND m.target_id = l.target_id)"));
    exec(QStringLiteral("DELETE FROM memos WHERE ") + missing);
}

bool Project::memoTargetExists(MemoTarget type, qint64 id) const
{
    QString table;
    switch (type) {
    case MemoTarget::Group: table = QStringLiteral("document_groups"); break;
    case MemoTarget::Document: table = QStringLiteral("documents"); break;
    case MemoTarget::Code: table = QStringLiteral("codes"); break;
    case MemoTarget::Segment: table = QStringLiteral("segments"); break;
    case MemoTarget::DocumentSet: table = QStringLiteral("document_sets"); break;
    case MemoTarget::Project:
    case MemoTarget::Free: return false;
    }
    return scalar(QStringLiteral("SELECT COUNT(*) FROM %1 WHERE id = ?").arg(table), {id}).toInt() > 0;
}

// ---------------------------------------------------------------- groups

QList<DocumentGroup> Project::groups() const
{
    QList<DocumentGroup> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id, name, position FROM document_groups ORDER BY position, id"));
    while (q.next())
        out.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toInt()});
    return out;
}

qint64 Project::addGroup(const QString &name)
{
    setUndoLabel(tr("Nuovo gruppo"));
    const qint64 id = insert(QStringLiteral("INSERT INTO document_groups(name, position) VALUES (?, ?)"),
                             {name, nextPosition(QStringLiteral("document_groups"))});
    if (id)
        notify(&Project::groupsChanged);
    return id;
}

bool Project::renameGroup(qint64 id, const QString &name)
{
    setUndoLabel(tr("Rinomina gruppo"));
    if (!exec(QStringLiteral("UPDATE document_groups SET name = ? WHERE id = ?"), {name, id}))
        return false;
    notify(&Project::groupsChanged);
    return true;
}

bool Project::deleteGroup(qint64 id)
{
    setUndoLabel(tr("Elimina gruppo"));
    beginBatch();
    const bool ok = exec(QStringLiteral("DELETE FROM document_groups WHERE id = ?"), {id});
    if (ok)
        removeOrphanMemos();
    if (!ok) {
        rollbackBatch();
        return false;
    }
    notify(&Project::groupsChanged);
    notify(&Project::documentsChanged);
    notify(&Project::documentSetsChanged);
    notify(&Project::memosChanged);
    notify(&Project::linksChanged);
    notifySegments(0);
    return commitBatch();
}

// ---------------------------------------------------------------- documents

QList<Document> Project::documents() const
{
    QList<Document> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT ") + kDocumentColumns +
           QStringLiteral(" FROM documents d JOIN document_groups g ON g.id = d.group_id "
                          "ORDER BY g.position, g.id, d.position, d.id"));
    while (q.next())
        out << readDocument(q);
    return out;
}

QList<Document> Project::documentsInGroup(qint64 groupId) const
{
    QList<Document> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + kDocumentColumns +
              QStringLiteral(" FROM documents d WHERE d.group_id = ? ORDER BY d.position, d.id"));
    q.addBindValue(groupId);
    q.exec();
    while (q.next())
        out << readDocument(q);
    return out;
}

Document Project::document(qint64 id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + kDocumentColumns + QStringLiteral(" FROM documents d WHERE d.id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return readDocument(q);
    return {};
}

QString Project::documentText(qint64 id) const
{
    return scalar(QStringLiteral("SELECT content FROM documents WHERE id = ?"), {id}).toString();
}

qint64 Project::addDocument(qint64 groupId, const QString &name, const QString &text,
                            const QString &sourcePath, const QString &uuid)
{
    setUndoLabel(tr("Nuovo documento"));
    const qint64 id = insert(
        QStringLiteral("INSERT INTO documents(group_id, name, content, source_path, imported_at, position, uuid) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?)"),
        {groupId, name, text, sourcePath, isoNow(),
         nextPosition(QStringLiteral("documents"), QStringLiteral("group_id = ?"), {groupId}),
         uuid.isEmpty() ? newUuid() : uuid});
    if (id)
        notify(&Project::documentsChanged);
    return id;
}

qint64 Project::addMediaDocument(qint64 groupId, const QString &name, DocumentKind kind,
                                const QString &sourcePath, const QString &uuid)
{
    setUndoLabel(tr("Nuovo documento"));
    if (kind == DocumentKind::Text) {
        fail(tr("Un documento di testo si crea con il suo testo"));
        return 0;
    }
    const qint64 id = insert(
        QStringLiteral("INSERT INTO documents(group_id, name, content, source_path, imported_at, position, uuid, kind) "
                       "VALUES (?, ?, '', ?, ?, ?, ?, ?)"),
        {groupId, name, sourcePath, isoNow(),
         nextPosition(QStringLiteral("documents"), QStringLiteral("group_id = ?"), {groupId}),
         uuid.isEmpty() ? newUuid() : uuid, documentKindToString(kind)});
    if (id)
        notify(&Project::documentsChanged);
    return id;
}

bool Project::renameDocument(qint64 id, const QString &name)
{
    setUndoLabel(tr("Rinomina documento"));
    if (!exec(QStringLiteral("UPDATE documents SET name = ? WHERE id = ?"), {name, id}))
        return false;
    notify(&Project::documentsChanged);
    return true;
}

bool Project::moveDocument(qint64 id, qint64 groupId)
{
    setUndoLabel(tr("Sposta documento"));
    if (!exec(QStringLiteral("UPDATE documents SET group_id = ?, position = ? WHERE id = ?"),
              {groupId, nextPosition(QStringLiteral("documents"), QStringLiteral("group_id = ?"), {groupId}), id}))
        return false;
    notify(&Project::documentsChanged);
    return true;
}

bool Project::deleteDocument(qint64 id)
{
    setUndoLabel(tr("Elimina documento"));
    beginBatch();
    if (!exec(QStringLiteral("DELETE FROM documents WHERE id = ?"), {id})) {
        rollbackBatch();
        return false;
    }
    removeOrphanMemos();
    notify(&Project::documentsChanged);
    notify(&Project::documentSetsChanged);
    notify(&Project::memosChanged);
    notify(&Project::variablesChanged);
    notify(&Project::linksChanged);
    notifySegments(id);
    return commitBatch();
}

bool Project::updateDocumentText(qint64 id, const QString &text, const QList<Segment> &segments,
                                 const QList<DocumentLink> &links)
{
    setUndoLabel(tr("Modifica testo"));
    beginBatch();
    bool ok = exec(QStringLiteral("UPDATE documents SET content = ? WHERE id = ?"), {text, id});
    QSet<qint64> kept;
    for (const Segment &s : segments) {
        if (!ok)
            break;
        if (s.end <= s.start || s.start < 0 || s.end > text.size())
            continue;
        kept.insert(s.id);
        ok = exec(QStringLiteral("UPDATE segments SET start_pos = ?, end_pos = ? WHERE id = ? AND document_id = ?"),
                  {s.start, s.end, s.id, id});
    }
    if (ok) {
        // Segments whose text was entirely deleted disappear.
        const QList<Segment> existing = segmentsForDocument(id);
        for (const Segment &s : existing) {
            if (!kept.contains(s.id) && ok)
                ok = exec(QStringLiteral("DELETE FROM segments WHERE id = ?"), {s.id});
        }
    }
    // Link ends in this document move like segments; a link whose end lost all its text goes away.
    auto validEnd = [&](int start, int end) { return end > start && start >= 0 && end <= text.size(); };
    QSet<qint64> keptLinks;
    for (const DocumentLink &l : links) {
        if (!ok)
            break;
        const bool from = l.fromDocumentId == id, to = l.toDocumentId == id;
        if ((!from && !to) || (from && !validEnd(l.fromStart, l.fromEnd)) || (to && !validEnd(l.toStart, l.toEnd)))
            continue;
        keptLinks.insert(l.id);
        if (from)
            ok = exec(QStringLiteral("UPDATE links SET from_start = ?, from_end = ? WHERE id = ? AND from_document_id = ?"),
                      {l.fromStart, l.fromEnd, l.id, id});
        if (ok && to)
            ok = exec(QStringLiteral("UPDATE links SET to_start = ?, to_end = ? WHERE id = ? AND to_document_id = ?"),
                      {l.toStart, l.toEnd, l.id, id});
    }
    if (ok) {
        for (const DocumentLink &l : linksForDocument(id)) {
            if (!keptLinks.contains(l.id) && ok)
                ok = exec(QStringLiteral("DELETE FROM links WHERE id = ?"), {l.id});
        }
    }
    if (!ok) {
        rollbackBatch();
        return false;
    }
    removeOrphanMemos();
    notify(&Project::documentsChanged);
    notify(&Project::memosChanged);
    notify(&Project::linksChanged);
    notifySegments(id);
    return commitBatch();
}

// ---------------------------------------------------------------- document sets

QList<DocumentSet> Project::documentSets() const
{
    QList<DocumentSet> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id, name, position, uuid FROM document_sets ORDER BY position, id"));
    while (q.next())
        out.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toInt(), q.value(3).toString()});
    return out;
}

QList<qint64> Project::documentSetMembers(qint64 setId) const
{
    QList<qint64> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT d.id FROM document_set_members m"
                             " JOIN documents d ON d.id = m.document_id"
                             " JOIN document_groups g ON g.id = d.group_id"
                             " WHERE m.set_id = ? ORDER BY g.position, g.id, d.position, d.id"));
    q.addBindValue(setId);
    q.exec();
    while (q.next())
        out << q.value(0).toLongLong();
    return out;
}

qint64 Project::addDocumentSet(const QString &name, const QList<qint64> &documentIds, const QString &uuid)
{
    setUndoLabel(tr("Nuovo set di documenti"));
    beginBatch();
    const qint64 id = insert(QStringLiteral("INSERT INTO document_sets(name, position, uuid) VALUES (?, ?, ?)"),
                             {name, nextPosition(QStringLiteral("document_sets")), uuid.isEmpty() ? newUuid() : uuid});
    if (!id || !addToDocumentSet(id, documentIds)) {
        rollbackBatch();
        return 0;
    }
    notify(&Project::documentSetsChanged);
    return commitBatch() ? id : 0;
}

bool Project::renameDocumentSet(qint64 id, const QString &name)
{
    setUndoLabel(tr("Rinomina set"));
    if (!exec(QStringLiteral("UPDATE document_sets SET name = ? WHERE id = ?"), {name, id}))
        return false;
    notify(&Project::documentSetsChanged);
    return true;
}

// Only the set and its memo go away: the documents stay in their groups.
bool Project::deleteDocumentSet(qint64 id)
{
    setUndoLabel(tr("Elimina set"));
    beginBatch();
    if (!exec(QStringLiteral("DELETE FROM document_sets WHERE id = ?"), {id})) {
        rollbackBatch();
        return false;
    }
    removeOrphanMemos();
    notify(&Project::documentSetsChanged);
    notify(&Project::memosChanged);
    return commitBatch();
}

// Documents already in the set are skipped.
bool Project::addToDocumentSet(qint64 setId, const QList<qint64> &documentIds)
{
    setUndoLabel(tr("Aggiungi al set"));
    beginBatch();
    for (qint64 documentId : documentIds) {
        if (!exec(QStringLiteral("INSERT OR IGNORE INTO document_set_members(set_id, document_id) VALUES (?, ?)"),
                  {setId, documentId})) {
            rollbackBatch();
            return false;
        }
    }
    notify(&Project::documentSetsChanged);
    return commitBatch();
}

bool Project::removeFromDocumentSet(qint64 setId, const QList<qint64> &documentIds)
{
    setUndoLabel(tr("Rimuovi dal set"));
    beginBatch();
    for (qint64 documentId : documentIds) {
        if (!exec(QStringLiteral("DELETE FROM document_set_members WHERE set_id = ? AND document_id = ?"),
                  {setId, documentId})) {
            rollbackBatch();
            return false;
        }
    }
    notify(&Project::documentSetsChanged);
    return commitBatch();
}

// ---------------------------------------------------------------- codes

QList<Code> Project::codes() const
{
    QList<Code> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT ") + kCodeColumns + QStringLiteral(" FROM codes ORDER BY position, id"));
    while (q.next())
        out << readCode(q);
    return out;
}

QList<Code> Project::codesInTreeOrder() const
{
    const QList<Code> all = codes();
    QHash<qint64, QList<Code>> children;
    for (const Code &c : all)
        children[c.parentId] << c;
    QList<Code> out;
    out.reserve(all.size());
    // Iterative DFS: push children in reverse so they pop in order.
    QList<Code> pending;
    for (auto it = children.value(0).crbegin(); it != children.value(0).crend(); ++it)
        pending << *it;
    while (!pending.isEmpty()) {
        const Code c = pending.takeLast();
        out << c;
        const QList<Code> kids = children.value(c.id);
        for (auto it = kids.crbegin(); it != kids.crend(); ++it)
            pending << *it;
    }
    return out;
}

Code Project::code(qint64 id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + kCodeColumns + QStringLiteral(" FROM codes WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return readCode(q);
    return {};
}

qint64 Project::addCode(qint64 parentId, const QString &name, const QColor &color, const QString &uuid)
{
    setUndoLabel(tr("Nuovo codice"));
    const int pos = parentId > 0
        ? nextPosition(QStringLiteral("codes"), QStringLiteral("parent_id = ?"), {parentId})
        : nextPosition(QStringLiteral("codes"), QStringLiteral("parent_id IS NULL"));
    const qint64 id = insert(
        QStringLiteral("INSERT INTO codes(parent_id, name, color, position, uuid) VALUES (?, ?, ?, ?, ?)"),
        {idOrNull(parentId), name, color.isValid() ? color.name() : QStringLiteral("#808080"), pos,
         uuid.isEmpty() ? newUuid() : uuid});
    if (id)
        notify(&Project::codesChanged);
    return id;
}

bool Project::renameCode(qint64 id, const QString &name)
{
    setUndoLabel(tr("Rinomina codice"));
    if (!exec(QStringLiteral("UPDATE codes SET name = ? WHERE id = ?"), {name, id}))
        return false;
    notify(&Project::codesChanged);
    return true;
}

bool Project::setCodeColor(qint64 id, const QColor &color)
{
    setUndoLabel(tr("Colore del codice"));
    if (!exec(QStringLiteral("UPDATE codes SET color = ? WHERE id = ?"), {color.name(), id}))
        return false;
    notify(&Project::codesChanged);
    notifySegments(0);
    return true;
}

bool Project::setCodeDescription(qint64 id, const QString &description)
{
    setUndoLabel(tr("Descrizione del codice"));
    if (!exec(QStringLiteral("UPDATE codes SET description = ? WHERE id = ?"), {description, id}))
        return false;
    notify(&Project::codesChanged);
    return true;
}

QList<qint64> Project::codeDescendants(qint64 id) const
{
    QList<qint64> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "WITH RECURSIVE sub(id) AS (SELECT id FROM codes WHERE parent_id = ? "
        "UNION ALL SELECT c.id FROM codes c JOIN sub ON c.parent_id = sub.id) SELECT id FROM sub"));
    q.addBindValue(id);
    q.exec();
    while (q.next())
        out << q.value(0).toLongLong();
    return out;
}

bool Project::moveCode(qint64 id, qint64 newParentId, int position)
{
    setUndoLabel(tr("Sposta codice"));
    if (id == newParentId || (newParentId > 0 && codeDescendants(id).contains(newParentId)))
        return fail(tr("Un codice non può diventare sottocodice di se stesso"));

    QList<qint64> siblings;
    {
        QSqlQuery q(m_db);
        q.prepare(newParentId > 0
                      ? QStringLiteral("SELECT id FROM codes WHERE parent_id = ? AND id <> ? ORDER BY position, id")
                      : QStringLiteral("SELECT id FROM codes WHERE parent_id IS NULL AND id <> ? ORDER BY position, id"));
        if (newParentId > 0)
            q.addBindValue(newParentId);
        q.addBindValue(id);
        q.exec();
        while (q.next())
            siblings << q.value(0).toLongLong();
    }
    if (position < 0 || position > siblings.size())
        position = int(siblings.size());
    siblings.insert(position, id);

    beginBatch();
    bool ok = exec(QStringLiteral("UPDATE codes SET parent_id = ? WHERE id = ?"), {idOrNull(newParentId), id});
    for (int i = 0; ok && i < siblings.size(); ++i)
        ok = exec(QStringLiteral("UPDATE codes SET position = ? WHERE id = ?"), {i, siblings[i]});
    if (!ok) {
        rollbackBatch();
        return false;
    }
    notify(&Project::codesChanged);
    return commitBatch();
}

bool Project::deleteCode(qint64 id)
{
    setUndoLabel(tr("Elimina codice"));
    beginBatch();
    if (!exec(QStringLiteral("DELETE FROM codes WHERE id = ?"), {id})) {
        rollbackBatch();
        return false;
    }
    removeOrphanMemos();
    notify(&Project::codesChanged);
    notify(&Project::memosChanged);
    notifySegments(0);
    return commitBatch();
}

QString Project::codePath(qint64 id, const QString &separator) const
{
    QStringList parts;
    QSet<qint64> seen;
    while (id > 0 && !seen.contains(id)) {
        seen.insert(id);
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT name, parent_id FROM codes WHERE id = ?"));
        q.addBindValue(id);
        if (!q.exec() || !q.next())
            break;
        parts.prepend(q.value(0).toString());
        id = q.value(1).isNull() ? 0 : q.value(1).toLongLong();
    }
    return parts.join(separator);
}

qint64 Project::findCode(qint64 parentId, const QString &name) const
{
    return parentId > 0
        ? scalar(QStringLiteral("SELECT id FROM codes WHERE parent_id = ? AND name = ?"), {parentId, name}).toLongLong()
        : scalar(QStringLiteral("SELECT id FROM codes WHERE parent_id IS NULL AND name = ?"), {name}).toLongLong();
}

// ---------------------------------------------------------------- segments

QList<Segment> Project::segmentsForDocument(qint64 documentId, bool includeNonText) const
{
    QList<Segment> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + kSegmentColumns +
              QStringLiteral(" FROM segments s WHERE s.document_id = ?") +
              (includeNonText ? QString() : QStringLiteral(" AND s.anchor_kind = 'text'")) +
              QStringLiteral(" ORDER BY s.start_pos, s.end_pos DESC, s.id"));
    q.addBindValue(documentId);
    q.exec();
    while (q.next())
        out << readSegment(q);
    return out;
}

QList<Segment> Project::segments(const QList<qint64> &documentIds, const QList<qint64> &codeIds,
                                  bool includeNonText) const
{
    QList<Segment> out;
    if (codeIds.isEmpty())
        return out;
    QString sql = QStringLiteral("SELECT ") + kSegmentColumns +
                  QStringLiteral(" FROM segments s JOIN documents d ON d.id = s.document_id "
                                 "JOIN document_groups g ON g.id = d.group_id WHERE s.code_id IN (") +
                  placeholders(codeIds.size()) + QLatin1Char(')');
    if (!documentIds.isEmpty())
        sql += QStringLiteral(" AND s.document_id IN (") + placeholders(documentIds.size()) + QLatin1Char(')');
    if (!includeNonText)
        sql += QStringLiteral(" AND s.anchor_kind = 'text'");
    sql += QStringLiteral(" ORDER BY g.position, g.id, d.position, d.id, s.start_pos, s.id");
    QSqlQuery q(m_db);
    q.prepare(sql);
    for (qint64 id : codeIds)
        q.addBindValue(id);
    for (qint64 id : documentIds)
        q.addBindValue(id);
    q.exec();
    while (q.next())
        out << readSegment(q);
    return out;
}

QList<Segment> Project::allSegments(bool includeNonText) const
{
    QList<Segment> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT ") + kSegmentColumns +
           QStringLiteral(" FROM segments s") + (includeNonText ? QString() : QStringLiteral(" WHERE s.anchor_kind = 'text'")) +
               QStringLiteral(" ORDER BY s.document_id, s.start_pos, s.id"));
    while (q.next())
        out << readSegment(q);
    return out;
}

Segment Project::segment(qint64 id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + kSegmentColumns + QStringLiteral(" FROM segments s WHERE s.id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return readSegment(q);
    return {};
}

qint64 Project::addSegment(qint64 documentId, qint64 codeId, int start, int end, int weight,
                           const QString &comment, const QString &uuid)
{
    setUndoLabel(tr("Codifica"));
    if (end <= start) {
        fail(tr("Segmento vuoto"));
        return 0;
    }
    // Positions computed on an older version of the text could fall outside it.
    if (start < 0 || end > documentText(documentId).size()) {
        fail(tr("Il segmento è fuori dal testo del documento"));
        return 0;
    }
    if (document(documentId).kind != DocumentKind::Text) {
        fail(tr("Un segmento di testo richiede un documento di testo"));
        return 0;
    }
    const qint64 existing = scalar(QStringLiteral("SELECT id FROM segments WHERE document_id = ? AND code_id = ? "
                                                  "AND anchor_kind = 'text' AND start_pos = ? AND end_pos = ?"),
                                   {documentId, codeId, start, end})
                                .toLongLong();
    if (existing)
        return existing;
    const qint64 id = insert(
        QStringLiteral("INSERT INTO segments(document_id, code_id, start_pos, end_pos, weight, comment, author, "
                       "created_at, uuid) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"),
        {documentId, codeId, start, end, weight, comment, m_author, isoNow(), uuid.isEmpty() ? newUuid() : uuid});
    if (id)
        notifySegments(documentId);
    return id;
}

qint64 Project::addAreaSegment(qint64 documentId, qint64 codeId, int page, double x1, double y1, double x2,
                              double y2, int weight, const QString &comment, const QString &uuid)
{
    setUndoLabel(tr("Codifica"));
    const DocumentKind kind = document(documentId).kind;
    if (kind != DocumentKind::Pdf && kind != DocumentKind::Image) {
        fail(tr("Un'area si codifica solo in un PDF o in un'immagine"));
        return 0;
    }
    if (page < 0 || (kind == DocumentKind::Pdf) != (page >= 1)) {
        fail(tr("Pagina non valida"));
        return 0;
    }
    // Normalise so that (x1, y1) is the top left corner.
    if (x2 < x1)
        std::swap(x1, x2);
    if (y2 < y1)
        std::swap(y1, y2);
    if (x1 < 0 || y1 < 0 || x2 <= x1 || y2 <= y1) {
        fail(tr("Area vuota o non valida"));
        return 0;
    }
    const qint64 existing = scalar(QStringLiteral("SELECT id FROM segments WHERE document_id = ? AND code_id = ? AND "
                                                  "anchor_kind = 'area' AND page = ? AND x1 = ? AND y1 = ? AND "
                                                  "x2 = ? AND y2 = ?"),
                                   {documentId, codeId, page, x1, y1, x2, y2})
                                .toLongLong();
    if (existing)
        return existing;
    const qint64 id = insert(
        QStringLiteral("INSERT INTO segments(document_id, code_id, start_pos, end_pos, weight, comment, author, "
                       "created_at, uuid, anchor_kind, page, x1, y1, x2, y2) "
                       "VALUES (?, ?, 0, 1, ?, ?, ?, ?, ?, 'area', ?, ?, ?, ?, ?)"),
        {documentId, codeId, weight, comment, m_author, isoNow(), uuid.isEmpty() ? newUuid() : uuid, page, x1, y1,
         x2, y2});
    if (id)
        notifySegments(documentId);
    return id;
}

qint64 Project::addTimeSegment(qint64 documentId, qint64 codeId, qint64 startMs, qint64 endMs, int weight,
                               const QString &comment, const QString &uuid)
{
    setUndoLabel(tr("Codifica"));
    const DocumentKind kind = document(documentId).kind;
    if (kind != DocumentKind::Audio && kind != DocumentKind::Video) {
        fail(tr("Un intervallo di tempo si codifica solo in un audio o in un video"));
        return 0;
    }
    if (startMs < 0 || endMs <= startMs) {
        fail(tr("Intervallo di tempo vuoto o non valido"));
        return 0;
    }
    const qint64 existing = scalar(QStringLiteral("SELECT id FROM segments WHERE document_id = ? AND code_id = ? AND "
                                                  "anchor_kind = 'time' AND t_start = ? AND t_end = ?"),
                                   {documentId, codeId, startMs, endMs})
                                .toLongLong();
    if (existing)
        return existing;
    const qint64 id = insert(
        QStringLiteral("INSERT INTO segments(document_id, code_id, start_pos, end_pos, weight, comment, author, "
                       "created_at, uuid, anchor_kind, t_start, t_end) "
                       "VALUES (?, ?, 0, 1, ?, ?, ?, ?, ?, 'time', ?, ?)"),
        {documentId, codeId, weight, comment, m_author, isoNow(), uuid.isEmpty() ? newUuid() : uuid, startMs, endMs});
    if (id)
        notifySegments(documentId);
    return id;
}

bool Project::deleteSegment(qint64 id)
{
    setUndoLabel(tr("Rimuovi codifica"));
    const qint64 doc = scalar(QStringLiteral("SELECT document_id FROM segments WHERE id = ?"), {id}).toLongLong();
    beginBatch();
    if (!exec(QStringLiteral("DELETE FROM segments WHERE id = ?"), {id})) {
        rollbackBatch();
        return false;
    }
    removeOrphanMemos();
    notify(&Project::memosChanged);
    notifySegments(doc);
    return commitBatch();
}

bool Project::setSegmentComment(qint64 id, const QString &comment)
{
    setUndoLabel(tr("Commento al segmento"));
    if (!exec(QStringLiteral("UPDATE segments SET comment = ? WHERE id = ?"), {comment, id}))
        return false;
    notifySegments(segment(id).documentId);
    return true;
}

bool Project::setSegmentWeight(qint64 id, int weight)
{
    setUndoLabel(tr("Peso del segmento"));
    if (!exec(QStringLiteral("UPDATE segments SET weight = ? WHERE id = ?"), {weight, id}))
        return false;
    notifySegments(segment(id).documentId);
    return true;
}

bool Project::setSegmentCode(qint64 id, qint64 codeId)
{
    setUndoLabel(tr("Cambia codice del segmento"));
    if (!exec(QStringLiteral("UPDATE segments SET code_id = ? WHERE id = ?"), {codeId, id}))
        return false;
    notifySegments(segment(id).documentId);
    return true;
}

QHash<qint64, int> Project::segmentCountByCode() const
{
    QHash<qint64, int> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT code_id, COUNT(*) FROM segments GROUP BY code_id"));
    while (q.next())
        out.insert(q.value(0).toLongLong(), q.value(1).toInt());
    return out;
}

QHash<qint64, int> Project::segmentCountByDocument() const
{
    QHash<qint64, int> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT document_id, COUNT(*) FROM segments GROUP BY document_id"));
    while (q.next())
        out.insert(q.value(0).toLongLong(), q.value(1).toInt());
    return out;
}

QHash<qint64, QHash<qint64, int>> Project::segmentCountByCodeAndDocument() const
{
    QHash<qint64, QHash<qint64, int>> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT code_id, document_id, COUNT(*) FROM segments GROUP BY code_id, document_id"));
    while (q.next())
        out[q.value(0).toLongLong()].insert(q.value(1).toLongLong(), q.value(2).toInt());
    return out;
}

// ---------------------------------------------------------------- links

QList<DocumentLink> Project::links() const
{
    QList<DocumentLink> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT ") + kLinkColumns +
           QStringLiteral(" FROM links l JOIN documents d ON d.id = l.from_document_id"
                          " JOIN document_groups g ON g.id = d.group_id"
                          " ORDER BY g.position, g.id, d.position, d.id, l.from_start, l.id"));
    while (q.next())
        out << readLink(q);
    return out;
}

QList<DocumentLink> Project::linksForDocument(qint64 documentId) const
{
    QList<DocumentLink> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + kLinkColumns +
              QStringLiteral(" FROM links l WHERE l.from_document_id = ? OR l.to_document_id = ? ORDER BY l.id"));
    q.addBindValue(documentId);
    q.addBindValue(documentId);
    q.exec();
    while (q.next())
        out << readLink(q);
    return out;
}

DocumentLink Project::link(qint64 id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + kLinkColumns + QStringLiteral(" FROM links l WHERE l.id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return readLink(q);
    return {};
}

qint64 Project::addLink(qint64 fromDocumentId, int fromStart, int fromEnd, qint64 toDocumentId, int toStart,
                        int toEnd, const QString &comment, const QString &uuid)
{
    setUndoLabel(tr("Nuovo collegamento"));
    // Both ends must be non-empty passages of existing documents (positions may be stale).
    auto validEnd = [this](qint64 doc, int start, int end) {
        return end > start && start >= 0 && document(doc).id && end <= documentText(doc).size();
    };
    if (!validEnd(fromDocumentId, fromStart, fromEnd) || !validEnd(toDocumentId, toStart, toEnd)) {
        fail(tr("Le estremità del collegamento devono essere brani di testo dei documenti"));
        return 0;
    }
    if (fromDocumentId == toDocumentId && fromStart == toStart && fromEnd == toEnd) {
        fail(tr("Un brano non può essere collegato a se stesso"));
        return 0;
    }
    const qint64 id = insert(
        QStringLiteral("INSERT INTO links(from_document_id, from_start, from_end, to_document_id, to_start, to_end,"
                       " comment, author, created_at, uuid) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"),
        {fromDocumentId, fromStart, fromEnd, toDocumentId, toStart, toEnd, comment, m_author, isoNow(),
         uuid.isEmpty() ? newUuid() : uuid});
    if (id)
        notify(&Project::linksChanged);
    return id;
}

bool Project::setLinkComment(qint64 id, const QString &comment)
{
    setUndoLabel(tr("Commento al collegamento"));
    if (!exec(QStringLiteral("UPDATE links SET comment = ? WHERE id = ?"), {comment, id}))
        return false;
    notify(&Project::linksChanged);
    return true;
}

bool Project::deleteLink(qint64 id)
{
    setUndoLabel(tr("Elimina collegamento"));
    if (!exec(QStringLiteral("DELETE FROM links WHERE id = ?"), {id}))
        return false;
    notify(&Project::linksChanged);
    return true;
}

// ---------------------------------------------------------------- memos

QList<Memo> Project::memos() const
{
    QList<Memo> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT ") + kMemoColumns + QStringLiteral(" FROM memos ORDER BY modified_at DESC, id"));
    while (q.next())
        out << readMemo(q);
    return out;
}

Memo Project::memo(qint64 id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT ") + kMemoColumns + QStringLiteral(" FROM memos WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return readMemo(q);
    return {};
}

Memo Project::memoFor(MemoTarget type, qint64 targetId) const
{
    QSqlQuery q(m_db);
    const QString target = memoTargetToString(type);
    q.prepare(QStringLiteral("SELECT ") + kMemoColumns +
              QStringLiteral(" FROM memos WHERE (target_type = ? AND target_id = ?)"
                             " OR id IN (SELECT memo_id FROM memo_links WHERE target_type = ? AND target_id = ?)"
                             " ORDER BY (target_type = ? AND target_id = ?) DESC, id LIMIT 1"));
    for (int i = 0; i < 3; ++i) {
        q.addBindValue(target);
        q.addBindValue(targetId);
    }
    if (q.exec() && q.next())
        return readMemo(q);
    Memo m;
    m.targetType = type;
    m.targetId = targetId;
    return m;
}

qint64 Project::saveMemo(Memo memo)
{
    setUndoLabel(tr("Memo"));
    const QString now = isoNow();
    if (memo.id == 0) {
        memo.id = insert(
            QStringLiteral("INSERT INTO memos(target_type, target_id, title, body, author, created_at, modified_at, "
                           "uuid) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"),
            {memoTargetToString(memo.targetType), memo.targetId, memo.title, memo.body,
             memo.author.isEmpty() ? m_author : memo.author,
             memo.createdAt.isValid() ? memo.createdAt.toString(Qt::ISODateWithMs) : now, now,
             memo.uuid.isEmpty() ? newUuid() : memo.uuid});
    } else if (!exec(QStringLiteral("UPDATE memos SET title = ?, body = ?, modified_at = ? WHERE id = ?"),
                     {memo.title, memo.body, now, memo.id})) {
        return 0;
    }
    if (memo.id)
        notify(&Project::memosChanged);
    return memo.id;
}

bool Project::deleteMemo(qint64 id)
{
    setUndoLabel(tr("Elimina memo"));
    if (!exec(QStringLiteral("DELETE FROM memos WHERE id = ?"), {id}))
        return false;
    notify(&Project::memosChanged);
    return true;
}

QHash<qint64, qint64> Project::memoIdsFor(MemoTarget type) const
{
    QHash<qint64, qint64> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT target_id, MIN(memo_id) FROM ("
                             " SELECT target_id, id AS memo_id FROM memos WHERE target_type = ?"
                             " UNION ALL SELECT target_id, memo_id FROM memo_links WHERE target_type = ?)"
                             " GROUP BY target_id"));
    q.addBindValue(memoTargetToString(type));
    q.addBindValue(memoTargetToString(type));
    q.exec();
    while (q.next())
        out.insert(q.value(0).toLongLong(), q.value(1).toLongLong());
    return out;
}

bool Project::isLinkableMemoTarget(MemoTarget type)
{
    return type != MemoTarget::Project && type != MemoTarget::Free;
}

QList<MemoLink> Project::memoLinks(qint64 memoId) const
{
    QList<MemoLink> out;
    const Memo m = memo(memoId);
    if (!m.id)
        return out;
    if (isLinkableMemoTarget(m.targetType))
        out.append({m.targetType, m.targetId});
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT target_type, target_id FROM memo_links WHERE memo_id = ? ORDER BY id"));
    q.addBindValue(memoId);
    q.exec();
    while (q.next()) {
        const MemoLink l{memoTargetFromString(q.value(0).toString()), q.value(1).toLongLong()};
        if (isLinkableMemoTarget(l.targetType) && !out.contains(l))
            out << l;
    }
    return out;
}

bool Project::setMemoLinks(qint64 memoId, const QList<MemoLink> &links)
{
    setUndoLabel(tr("Collega memo"));
    const Memo m = memo(memoId);
    if (!m.id)
        return fail(tr("Il memo non esiste più"));
    QList<MemoLink> wanted;
    for (const MemoLink &l : links)
        if (isLinkableMemoTarget(l.targetType) && !wanted.contains(l) && memoTargetExists(l.targetType, l.targetId))
            wanted << l;

    beginBatch();
    bool ok = true;
    MemoLink primary{m.targetType, m.targetId};
    if (isLinkableMemoTarget(m.targetType) && !wanted.contains(primary)) {
        primary = wanted.isEmpty() ? MemoLink{MemoTarget::Free, 0} : wanted.first();
        ok = exec(QStringLiteral("UPDATE memos SET target_type = ?, target_id = ? WHERE id = ?"),
                  {memoTargetToString(primary.targetType), primary.targetId, memoId});
    }
    wanted.removeAll(primary);
    ok = ok && exec(QStringLiteral("DELETE FROM memo_links WHERE memo_id = ?"), {memoId});
    for (const MemoLink &l : std::as_const(wanted))
        ok = ok && exec(QStringLiteral("INSERT INTO memo_links(memo_id, target_type, target_id) VALUES (?, ?, ?)"),
                        {memoId, memoTargetToString(l.targetType), l.targetId});
    if (!ok) {
        rollbackBatch();
        return false;
    }
    notify(&Project::memosChanged);
    return commitBatch();
}

bool Project::addMemoLink(qint64 memoId, MemoTarget type, qint64 targetId)
{
    if (!isLinkableMemoTarget(type) || !memoTargetExists(type, targetId))
        return false;
    QList<MemoLink> links = memoLinks(memoId);
    if (links.contains(MemoLink{type, targetId}))
        return true;
    links.append({type, targetId});
    return setMemoLinks(memoId, links);
}

// ---------------------------------------------------------------- variables

QList<Variable> Project::variables() const
{
    QList<Variable> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id, name, type, position, uuid FROM variables ORDER BY position, id"));
    while (q.next()) {
        Variable v;
        v.id = q.value(0).toLongLong();
        v.name = q.value(1).toString();
        v.type = variableTypeFromString(q.value(2).toString());
        v.position = q.value(3).toInt();
        v.uuid = q.value(4).toString();
        out << v;
    }
    return out;
}

qint64 Project::addVariable(const QString &name, VariableType type, const QString &uuid)
{
    setUndoLabel(tr("Nuova variabile"));
    const qint64 id = insert(QStringLiteral("INSERT INTO variables(name, type, position, uuid) VALUES (?, ?, ?, ?)"),
                             {name, variableTypeToString(type), nextPosition(QStringLiteral("variables")),
                              uuid.isEmpty() ? newUuid() : uuid});
    if (id)
        notify(&Project::variablesChanged);
    return id;
}

bool Project::renameVariable(qint64 id, const QString &name)
{
    setUndoLabel(tr("Rinomina variabile"));
    if (!exec(QStringLiteral("UPDATE variables SET name = ? WHERE id = ?"), {name, id}))
        return false;
    notify(&Project::variablesChanged);
    return true;
}

bool Project::deleteVariable(qint64 id)
{
    setUndoLabel(tr("Elimina variabile"));
    if (!exec(QStringLiteral("DELETE FROM variables WHERE id = ?"), {id}))
        return false;
    notify(&Project::variablesChanged);
    return true;
}

QString Project::variableValue(qint64 documentId, qint64 variableId) const
{
    return scalar(QStringLiteral("SELECT value FROM variable_values WHERE document_id = ? AND variable_id = ?"),
                  {documentId, variableId})
        .toString();
}

bool Project::setVariableValue(qint64 documentId, qint64 variableId, const QString &value)
{
    setUndoLabel(tr("Valore di variabile"));
    const bool ok = value.isEmpty()
        ? exec(QStringLiteral("DELETE FROM variable_values WHERE document_id = ? AND variable_id = ?"),
               {documentId, variableId})
        : exec(QStringLiteral("INSERT INTO variable_values(document_id, variable_id, value) VALUES (?, ?, ?) "
                              "ON CONFLICT(document_id, variable_id) DO UPDATE SET value = excluded.value"),
               {documentId, variableId, value});
    if (ok)
        notify(&Project::variablesChanged);
    return ok;
}

QHash<qint64, QHash<qint64, QString>> Project::allVariableValues() const
{
    QHash<qint64, QHash<qint64, QString>> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT document_id, variable_id, value FROM variable_values"));
    while (q.next())
        out[q.value(0).toLongLong()].insert(q.value(1).toLongLong(), q.value(2).toString());
    return out;
}

// ---------------------------------------------------------------- dictionaries

QList<Dictionary> Project::dictionaries() const
{
    QList<Dictionary> out;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id, name, position, uuid FROM dictionaries ORDER BY position, id"));
    while (q.next())
        out.append({q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toInt(), q.value(3).toString()});
    return out;
}

Dictionary Project::dictionary(qint64 id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, name, position, uuid FROM dictionaries WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return {q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toInt(), q.value(3).toString()};
    return {};
}

qint64 Project::addDictionary(const QString &name, const QString &uuid)
{
    setUndoLabel(tr("Nuovo dizionario"));
    const qint64 id = insert(QStringLiteral("INSERT INTO dictionaries(name, position, uuid) VALUES (?, ?, ?)"),
                             {name, nextPosition(QStringLiteral("dictionaries")), uuid.isEmpty() ? newUuid() : uuid});
    if (id)
        notify(&Project::dictionariesChanged);
    return id;
}

bool Project::renameDictionary(qint64 id, const QString &name)
{
    setUndoLabel(tr("Rinomina dizionario"));
    if (!exec(QStringLiteral("UPDATE dictionaries SET name = ? WHERE id = ?"), {name, id}))
        return false;
    notify(&Project::dictionariesChanged);
    return true;
}

bool Project::deleteDictionary(qint64 id)
{
    setUndoLabel(tr("Elimina dizionario"));
    if (!exec(QStringLiteral("DELETE FROM dictionaries WHERE id = ?"), {id}))
        return false;
    notify(&Project::dictionariesChanged);
    return true;
}

namespace {

DictCategory readDictCategory(const QSqlQuery &q)
{
    DictCategory c;
    c.id = q.value(0).toLongLong();
    c.dictionaryId = q.value(1).toLongLong();
    c.parentId = q.value(2).isNull() ? 0 : q.value(2).toLongLong();
    c.name = q.value(3).toString();
    c.position = q.value(4).toInt();
    return c;
}

} // namespace

QList<DictCategory> Project::dictCategories(qint64 dictionaryId) const
{
    QList<DictCategory> all;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, dictionary_id, parent_id, name, position FROM dict_categories"
                             " WHERE dictionary_id = ? ORDER BY position, id"));
    q.addBindValue(dictionaryId);
    q.exec();
    while (q.next())
        all << readDictCategory(q);

    QHash<qint64, QList<DictCategory>> children;
    for (const DictCategory &c : all)
        children[c.parentId] << c;
    QList<DictCategory> out;
    out.reserve(all.size());
    QList<DictCategory> pending; // iterative DFS: children pushed in reverse pop in order
    for (auto it = children.value(0).crbegin(); it != children.value(0).crend(); ++it)
        pending << *it;
    while (!pending.isEmpty()) {
        const DictCategory c = pending.takeLast();
        out << c;
        const QList<DictCategory> kids = children.value(c.id);
        for (auto it = kids.crbegin(); it != kids.crend(); ++it)
            pending << *it;
    }
    return out;
}

DictCategory Project::dictCategory(qint64 id) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, dictionary_id, parent_id, name, position FROM dict_categories WHERE id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next())
        return readDictCategory(q);
    return {};
}

qint64 Project::addDictCategory(qint64 dictionaryId, qint64 parentId, const QString &name)
{
    setUndoLabel(tr("Nuova categoria"));
    // A subcategory must belong to the same dictionary as its parent.
    if (parentId > 0 && dictCategory(parentId).dictionaryId != dictionaryId) {
        fail(tr("La categoria superiore appartiene a un altro dizionario"));
        return 0;
    }
    const int pos = parentId > 0
        ? nextPosition(QStringLiteral("dict_categories"), QStringLiteral("parent_id = ?"), {parentId})
        : nextPosition(QStringLiteral("dict_categories"), QStringLiteral("dictionary_id = ? AND parent_id IS NULL"),
                       {dictionaryId});
    const qint64 id = insert(
        QStringLiteral("INSERT INTO dict_categories(dictionary_id, parent_id, name, position) VALUES (?, ?, ?, ?)"),
        {dictionaryId, idOrNull(parentId), name, pos});
    if (id)
        notify(&Project::dictionariesChanged);
    return id;
}

bool Project::renameDictCategory(qint64 id, const QString &name)
{
    setUndoLabel(tr("Rinomina categoria"));
    if (!exec(QStringLiteral("UPDATE dict_categories SET name = ? WHERE id = ?"), {name, id}))
        return false;
    notify(&Project::dictionariesChanged);
    return true;
}

bool Project::deleteDictCategory(qint64 id)
{
    setUndoLabel(tr("Elimina categoria"));
    if (!exec(QStringLiteral("DELETE FROM dict_categories WHERE id = ?"), {id}))
        return false;
    notify(&Project::dictionariesChanged);
    return true;
}

qint64 Project::findDictCategory(qint64 dictionaryId, qint64 parentId, const QString &name) const
{
    return parentId > 0
        ? scalar(QStringLiteral("SELECT id FROM dict_categories WHERE dictionary_id = ? AND parent_id = ? AND name = ?"),
                 {dictionaryId, parentId, name}).toLongLong()
        : scalar(QStringLiteral("SELECT id FROM dict_categories WHERE dictionary_id = ? AND parent_id IS NULL AND name = ?"),
                 {dictionaryId, name}).toLongLong();
}

QString Project::dictCategoryPath(qint64 id, const QString &separator) const
{
    QStringList parts;
    QSet<qint64> seen;
    while (id > 0 && !seen.contains(id)) {
        seen.insert(id);
        const DictCategory c = dictCategory(id);
        if (!c.id)
            break;
        parts.prepend(c.name);
        id = c.parentId;
    }
    return parts.join(separator);
}

QList<DictEntry> Project::dictEntries(qint64 categoryId) const
{
    QList<DictEntry> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id, category_id, pattern FROM dict_entries WHERE category_id = ? ORDER BY id"));
    q.addBindValue(categoryId);
    q.exec();
    while (q.next())
        out.append({q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toString()});
    return out;
}

QList<DictEntry> Project::dictEntriesOfDictionary(qint64 dictionaryId) const
{
    QList<DictEntry> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT e.id, e.category_id, e.pattern FROM dict_entries e"
                             " JOIN dict_categories c ON c.id = e.category_id WHERE c.dictionary_id = ? ORDER BY e.id"));
    q.addBindValue(dictionaryId);
    q.exec();
    while (q.next())
        out.append({q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toString()});
    return out;
}

qint64 Project::addDictEntry(qint64 categoryId, const QString &pattern)
{
    setUndoLabel(tr("Nuova voce del dizionario"));
    const QString p = pattern.simplified();
    if (p.isEmpty()) {
        fail(tr("Voce vuota"));
        return 0;
    }
    for (const DictEntry &e : dictEntries(categoryId))
        if (e.pattern.compare(p, Qt::CaseInsensitive) == 0)
            return e.id;
    const qint64 id = insert(QStringLiteral("INSERT INTO dict_entries(category_id, pattern) VALUES (?, ?)"),
                             {categoryId, p});
    if (id)
        notify(&Project::dictionariesChanged);
    return id;
}

bool Project::setDictEntryPattern(qint64 id, const QString &pattern)
{
    setUndoLabel(tr("Modifica voce del dizionario"));
    const QString p = pattern.simplified();
    if (p.isEmpty())
        return fail(tr("Voce vuota"));
    if (!exec(QStringLiteral("UPDATE dict_entries SET pattern = ? WHERE id = ?"), {p, id}))
        return false;
    notify(&Project::dictionariesChanged);
    return true;
}

bool Project::deleteDictEntry(qint64 id)
{
    setUndoLabel(tr("Elimina voce del dizionario"));
    if (!exec(QStringLiteral("DELETE FROM dict_entries WHERE id = ?"), {id}))
        return false;
    notify(&Project::dictionariesChanged);
    return true;
}