#include "core/Cooccurrence.h"
#include "core/Dictionaries.h"
#include "core/Exporters.h"
#include "core/Importers.h"
#include "core/Project.h"
#include "core/Qdpx.h"
#include "core/Retrieval.h"
#include "core/Stats.h"
#include "core/Survey.h"
#include "core/TextUtils.h"
#include "core/VariableFilter.h"
#include "core/Xlsx.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

namespace VF = VariableFilter;

class TestCore : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    QString path(const QString &name) const { return m_dir.filePath(name); }

    void writeFile(const QString &name, const QByteArray &data)
    {
        QFile f(path(name));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

    static Segment seg(qint64 id, qint64 doc, qint64 code, int start, int end)
    {
        Segment s;
        s.id = id;
        s.documentId = doc;
        s.codeId = code;
        s.start = start;
        s.end = end;
        return s;
    }

    static QList<QPair<int, int>> spans(const QList<Retrieval::Result> &r)
    {
        QList<QPair<int, int>> out;
        for (const auto &x : r)
            out.append({x.start, x.end});
        return out;
    }

    static QList<qint64> segmentIds(const QList<Retrieval::Result> &r)
    {
        QList<qint64> ids;
        for (const auto &x : r)
            ids << x.segmentId;
        return ids;
    }

    // First value of a query run directly on a project file, bypassing Project.
    static QVariant queryFile(const QString &file, const QString &sql)
    {
        QVariant result;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("raw"));
            db.setDatabaseName(file);
            if (db.open()) {
                QSqlQuery q(db);
                if (q.exec(sql) && q.next())
                    result = q.value(0);
            }
        }
        QSqlDatabase::removeDatabase(QStringLiteral("raw"));
        return result;
    }

    static QByteArray fileHash(const QString &file)
    {
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
    }

    static int fileVersion(const QString &file)
    {
        return queryFile(file, "SELECT value FROM meta WHERE key = 'schema_version'").toInt();
    }

    // Minimal workbook with one sheet: strings inline, numbers as they are, QDate cells as serials
    // styled with the built-in date format 14.
    void writeXlsx(const QString &name, const QList<QVariantList> &rows)
    {
        QByteArray sheet = "<?xml version=\"1.0\"?><worksheet xmlns=\"http://schemas.openxmlformats.org/"
                           "spreadsheetml/2006/main\"><sheetData>";
        for (int r = 0; r < rows.size(); ++r) {
            sheet += "<row r=\"" + QByteArray::number(r + 1) + "\">";
            for (int c = 0; c < rows[r].size(); ++c) {
                const QVariant &v = rows[r][c];
                const QByteArray ref = QByteArray(1, char('A' + c)) + QByteArray::number(r + 1);
                if (!v.isValid())
                    continue;
                if (v.typeId() == QMetaType::QDate)
                    sheet += "<c r=\"" + ref + "\" s=\"1\"><v>"
                        + QByteArray::number(QDate(1899, 12, 30).daysTo(v.toDate())) + "</v></c>";
                else if (v.typeId() == QMetaType::QString)
                    sheet += "<c r=\"" + ref + "\" t=\"inlineStr\"><is><t>" + v.toString().toHtmlEscaped().toUtf8()
                        + "</t></is></c>";
                else
                    sheet += "<c r=\"" + ref + "\"><v>" + QByteArray::number(v.toDouble(), 'g', 15) + "</v></c>";
            }
            sheet += "</row>";
        }
        sheet += "</sheetData></worksheet>";
        QZipWriter zip(path(name));
        zip.addFile("xl/workbook.xml",
                    "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                    "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                    "<sheets><sheet name=\"Risposte\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>");
        zip.addFile("xl/_rels/workbook.xml.rels",
                    "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                    "<Relationship Id=\"rId1\" Type=\"worksheet\" Target=\"worksheets/sheet1.xml\"/></Relationships>");
        zip.addFile("xl/styles.xml", "<styleSheet><cellXfs count=\"2\"><xf numFmtId=\"0\"/><xf numFmtId=\"14\"/>"
                                     "</cellXfs></styleSheet>");
        zip.addFile("xl/worksheets/sheet1.xml", sheet);
    }

private slots:
    void projectLifecycle()
    {
        Project p;
        QVERIFY(p.create(path("a.tsra")));
        QCOMPARE(p.groups().size(), 1);
        const qint64 g = p.groups().first().id;
        const qint64 d = p.addDocument(g, "Intervista 1", "Ciao mondo. Seconda frase.\nNuovo paragrafo.");
        QVERIFY(d > 0);
        const qint64 parent = p.addCode(0, "Emozioni", Qt::red);
        const qint64 child = p.addCode(parent, "Gioia", Qt::green);
        QCOMPARE(p.codePath(child), QString("Emozioni > Gioia"));
        QCOMPARE(p.codeDescendants(parent), QList<qint64>{child});

        const qint64 s1 = p.addSegment(d, child, 0, 11);
        QVERIFY(s1 > 0);
        QCOMPARE(p.addSegment(d, child, 0, 11), s1); // duplicates are merged
        QVERIFY(p.addSegment(d, parent, 5, 4) == 0); // empty / inverted range rejected
        QCOMPARE(p.segmentCountByCode().value(child), 1);

        Memo m;
        m.targetType = MemoTarget::Segment;
        m.targetId = s1;
        m.title = "Nota";
        m.body = "testo";
        QVERIFY(p.saveMemo(m) > 0);
        QCOMPARE(p.memoFor(MemoTarget::Segment, s1).body, QString("testo"));

        const qint64 v = p.addVariable("Età", VariableType::Integer);
        QVERIFY(p.setVariableValue(d, v, "42"));
        QCOMPARE(p.variableValue(d, v), QString("42"));

        // Deleting the parent code cascades to the subcode, its segments and their memos.
        QVERIFY(p.deleteCode(parent));
        QVERIFY(p.codes().isEmpty());
        QVERIFY(p.allSegments().isEmpty());
        QVERIFY(p.memos().isEmpty());

        // Reopen from disk.
        p.close();
        Project q;
        QVERIFY(q.open(path("a.tsra")));
        QCOMPARE(q.documents().size(), 1);
        QCOMPARE(q.variables().first().name, QString("Età"));
        QVERIFY(q.saveCopyAs(path("copy.tsra")));
        QVERIFY(QFile::exists(path("copy.tsra")));
    }

    void moveCodeRejectsCycles()
    {
        Project p;
        QVERIFY(p.create(path("b.tsra")));
        const qint64 a = p.addCode(0, "A", Qt::red);
        const qint64 b = p.addCode(a, "B", Qt::red);
        const qint64 c = p.addCode(0, "C", Qt::red);
        QVERIFY(!p.moveCode(a, b));
        QVERIFY(p.moveCode(c, a, 0));
        QCOMPARE(p.code(c).parentId, a);
        QCOMPARE(p.code(c).position, 0);
        QCOMPARE(p.code(b).position, 1);
        QVERIFY(p.moveCode(c, 0));
        QCOMPARE(p.code(c).parentId, qint64(0));
    }

    void migrationUpgradesOldProject()
    {
        {
            Project old;
            old.setMigrations({}); // base schema only, as Tessera 0.1.0 wrote it
            QVERIFY(old.create(path("m1.tsra")));
            const qint64 d = old.addDocument(old.groups().first().id, "Intervista", "Testo da codificare");
            QVERIFY(old.addSegment(d, old.addCode(0, "Tema", Qt::red), 0, 5) > 0);
        }
        QCOMPARE(fileVersion(path("m1.tsra")), 1);

        const QList<Project::Migration> migrations = {
            {2, {"CREATE TABLE extra (id INTEGER PRIMARY KEY, name TEXT NOT NULL DEFAULT '')"}},
            {3, {"ALTER TABLE codes ADD COLUMN alias TEXT NOT NULL DEFAULT ''"}},
        };
        Project p;
        p.setMigrations(migrations);
        QCOMPARE(p.schemaVersion(), 3);
        QVERIFY(p.open(path("m1.tsra")));
        QCOMPARE(p.meta("schema_version"), QString("3"));
        QCOMPARE(p.allSegments().size(), 1);
        QVERIFY(!p.canUndo());

        // The backup keeps the file as it was before the upgrade.
        const QString backup = p.migrationBackupPath();
        QCOMPARE(QFileInfo(backup).fileName(), QString("m1 (backup v1).tsra"));
        QCOMPARE(fileVersion(backup), 1);
        QCOMPARE(queryFile(backup, "SELECT count(*) FROM segments").toInt(), 1);

        // Undo triggers are built after the upgrade, so they cover the new column too.
        const qint64 code = p.codes().first().id;
        QVERIFY(p.renameCode(code, "Nuovo nome"));
        QVERIFY(p.undo());
        QCOMPARE(p.code(code).name, QString("Tema"));

        // Already up to date: no backup.
        p.close();
        QVERIFY(p.open(path("m1.tsra")));
        QVERIFY(p.migrationBackupPath().isEmpty());
        p.close();

        // A later upgrade never overwrites an earlier backup.
        {
            Project old;
            old.setMigrations({});
            QVERIFY(old.create(path("m1.tsra")));
        }
        QVERIFY(p.open(path("m1.tsra")));
        QCOMPARE(QFileInfo(p.migrationBackupPath()).fileName(), QString("m1 (backup v1 2).tsra"));
    }

    void migrationFailureLeavesFileUntouched()
    {
        {
            Project old;
            old.setMigrations({});
            QVERIFY(old.create(path("m2.tsra")));
        }
        const QByteArray before = fileHash(path("m2.tsra"));

        Project p;
        p.setMigrations({{2, {"CREATE TABLE extra (id INTEGER PRIMARY KEY)"}}, {3, {"NOT VALID SQL"}}});
        QVERIFY(!p.open(path("m2.tsra")));
        QVERIFY(!p.isOpen());
        QVERIFY(p.lastError().contains("versione 3"));
        QCOMPARE(fileHash(path("m2.tsra")), before);
        QVERIFY(!QFile::exists(path("m2 (backup v1).tsra")));
        QCOMPARE(fileVersion(path("m2.tsra")), 1);
    }

    void migrationRejectsDanglingReferences()
    {
        {
            Project old;
            old.setMigrations({});
            QVERIFY(old.create(path("m3.tsra")));
        }
        Project p;
        p.setMigrations({{2, {"INSERT INTO segments(document_id, code_id, start_pos, end_pos, uuid)"
                              " VALUES (99, 99, 0, 1, 'x')"}}});
        QVERIFY(!p.open(path("m3.tsra")));
        QVERIFY(p.lastError().contains("segments"));
        QCOMPARE(queryFile(path("m3.tsra"), "SELECT count(*) FROM segments").toInt(), 0);
        QCOMPARE(fileVersion(path("m3.tsra")), 1);
    }

    void migrationCanRebuildTables()
    {
        // SQLite cannot change a column in place: copy into a new table, drop the old one, rename.
        // Foreign keys are off while migrating, so the drop must not cascade to the segments.
        {
            Project old;
            old.setMigrations({});
            QVERIFY(old.create(path("m4.tsra")));
            const qint64 d = old.addDocument(old.groups().first().id, "Doc", "Testo");
            QVERIFY(old.addSegment(d, old.addCode(0, "C", Qt::red), 0, 5) > 0);
        }
        Project p;
        p.setMigrations({{2,
                          {"CREATE TABLE documents_new (id INTEGER PRIMARY KEY,"
                           " group_id INTEGER NOT NULL REFERENCES document_groups(id) ON DELETE CASCADE,"
                           " name TEXT NOT NULL, content TEXT NOT NULL DEFAULT '', source_path TEXT,"
                           " imported_at TEXT, position INTEGER NOT NULL DEFAULT 0, uuid TEXT NOT NULL,"
                           " kind TEXT NOT NULL DEFAULT 'text')",
                           "INSERT INTO documents_new (id, group_id, name, content, source_path, imported_at,"
                           " position, uuid) SELECT id, group_id, name, content, source_path, imported_at,"
                           " position, uuid FROM documents",
                           "DROP TABLE documents",
                           "ALTER TABLE documents_new RENAME TO documents"}}});
        QVERIFY(p.open(path("m4.tsra")));
        QCOMPARE(p.documents().size(), 1);
        QCOMPARE(p.allSegments().size(), 1);

        // Foreign keys are enforced again once the project is open.
        QVERIFY(p.deleteDocument(p.documents().first().id));
        QVERIFY(p.allSegments().isEmpty());
    }

    void futureVersionIsRejected()
    {
        Project newer;
        QList<Project::Migration> migrations = Project::builtInMigrations();
        migrations.append({99, {"CREATE TABLE extra (id INTEGER PRIMARY KEY)"}});
        newer.setMigrations(migrations);
        QVERIFY(newer.create(path("m5.tsra")));
        QCOMPARE(newer.meta("schema_version"), QString("99"));
        newer.close();
        const QByteArray before = fileHash(path("m5.tsra"));

        Project p;
        QVERIFY(!p.open(path("m5.tsra")));
        QVERIFY(p.lastError().contains("più recente"));
        QCOMPARE(fileHash(path("m5.tsra")), before);
    }

    void builtInUpgradeFromVersion1()
    {
        {
            Project old;
            old.setMigrations({});
            QVERIFY(old.create(path("v1.tsra")));
            old.addDocument(old.groups().first().id, "Doc", "Testo");
        }
        Project p;
        QVERIFY(p.open(path("v1.tsra")));
        QCOMPARE(p.meta("schema_version"), QString::number(p.schemaVersion()));
        QVERIFY(!p.migrationBackupPath().isEmpty());
        // The new tables work, undo included.
        QVERIFY(p.addDocumentSet("Set", {p.documents().first().id}) > 0);
        QVERIFY(p.undo());
        QVERIFY(p.documentSets().isEmpty());
        const qint64 doc = p.documents().first().id;
        QVERIFY(p.addLink(doc, 0, 2, doc, 3, 5) > 0);
        QVERIFY(p.undo());
        QVERIFY(p.links().isEmpty());
    }

    void documentSets()
    {
        Project p;
        QVERIFY(p.create(path("sets.tsra")));
        QCOMPARE(p.meta("schema_version"), QString::number(p.schemaVersion()));
        const qint64 g1 = p.groups().first().id;
        const qint64 g2 = p.addGroup("Secondo gruppo");
        const qint64 a = p.addDocument(g1, "A", "a");
        const qint64 b = p.addDocument(g2, "B", "b");
        const qint64 c = p.addDocument(g1, "C", "c");

        QSignalSpy spy(&p, &Project::documentSetsChanged);
        const qint64 s = p.addDocumentSet("Trasversale", {b, a});
        QVERIFY(s > 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(p.undoLabel(), QString("Nuovo set di documenti"));
        // Members follow the document tree order, whatever order they were added in.
        QCOMPARE(p.documentSetMembers(s), (QList<qint64>{a, b}));

        // A document can be in several sets; adding it twice to the same set is harmless.
        const qint64 s2 = p.addDocumentSet("Altro", {a});
        QVERIFY(p.addToDocumentSet(s, {a, c}));
        QCOMPARE(p.documentSetMembers(s), (QList<qint64>{a, c, b}));
        QVERIFY(!p.addToDocumentSet(s, {9999})); // unknown document
        QCOMPARE(p.documentSetMembers(s).size(), 3);
        QVERIFY(p.removeFromDocumentSet(s, {c}));
        QCOMPARE(p.documentSetMembers(s), (QList<qint64>{a, b}));
        QVERIFY(p.renameDocumentSet(s, "Rinominato"));
        QCOMPARE(p.documentSets().first().name, QString("Rinominato"));

        // Deleting a document removes it from every set; undo puts it back in all of them.
        QVERIFY(p.deleteDocument(a));
        QCOMPARE(p.documentSetMembers(s), QList<qint64>{b});
        QVERIFY(p.documentSetMembers(s2).isEmpty());
        QVERIFY(p.undo());
        QCOMPARE(p.documentSetMembers(s), (QList<qint64>{a, b}));
        QCOMPARE(p.documentSetMembers(s2), QList<qint64>{a});

        // Deleting a set keeps its documents and drops its memo.
        Memo m;
        m.targetType = MemoTarget::DocumentSet;
        m.targetId = s;
        m.body = "perché questo set";
        QVERIFY(p.saveMemo(m) > 0);
        QVERIFY(p.deleteDocumentSet(s));
        QCOMPARE(p.documentSets().size(), 1);
        QCOMPARE(p.documents().size(), 3);
        QVERIFY(p.memos().isEmpty());
        QVERIFY(p.undo());
        QCOMPARE(p.documentSets().size(), 2);
        QCOMPARE(p.documentSetMembers(s), (QList<qint64>{a, b}));
        QCOMPARE(p.memoFor(MemoTarget::DocumentSet, s).body, QString("perché questo set"));
    }

    void batchDefersSignals()
    {
        Project p;
        QVERIFY(p.create(path("c.tsra")));
        QSignalSpy spy(&p, &Project::codesChanged);
        p.beginBatch();
        p.addCode(0, "X", Qt::red);
        p.addCode(0, "Y", Qt::red);
        QCOMPARE(spy.count(), 0);
        QVERIFY(p.commitBatch());
        QCOMPARE(spy.count(), 1);
    }

    void undoRedoBasics()
    {
        Project p;
        QVERIFY(p.create(path("u1.tsra")));
        QVERIFY(!p.canUndo()); // creating the project is not an undoable step
        const qint64 g = p.groups().first().id;
        const qint64 d = p.addDocument(g, "Doc", "uno due tre");
        const qint64 c = p.addCode(0, "Tema", Qt::red);
        QSignalSpy spy(&p, &Project::undoStateChanged);
        const qint64 s = p.addSegment(d, c, 0, 3);
        QVERIFY(s > 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(p.undoLabel(), QString("Codifica"));

        QVERIFY(p.undo());
        QVERIFY(p.allSegments().isEmpty());
        QCOMPARE(p.undoLabel(), QString("Nuovo codice"));
        QCOMPARE(p.redoLabel(), QString("Codifica"));
        QVERIFY(p.redo());
        QCOMPARE(p.segment(s).end, 3); // same id restored

        // A new operation discards the redo history.
        QVERIFY(p.undo());
        QVERIFY(p.canRedo());
        p.renameCode(c, "Tema 2");
        QVERIFY(!p.canRedo());
        QVERIFY(p.undo());
        QCOMPARE(p.code(c).name, QString("Tema"));

        // Undo everything back to the empty project, then redo everything.
        while (p.canUndo())
            QVERIFY(p.undo());
        QVERIFY(p.documents().isEmpty());
        QVERIFY(p.codes().isEmpty());
        while (p.canRedo())
            QVERIFY(p.redo());
        QCOMPARE(p.documents().size(), 1);
        QCOMPARE(p.code(c).name, QString("Tema 2")); // the undone rename is redone too
    }

    void undoCascadeDelete()
    {
        Project p;
        QVERIFY(p.create(path("u2.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d = p.addDocument(g, "Doc", "testo di prova per la cascata");
        const qint64 parent = p.addCode(0, "Padre", Qt::red);
        const qint64 child = p.addCode(parent, "Figlio", Qt::blue);
        const qint64 s1 = p.addSegment(d, parent, 0, 5);
        const qint64 s2 = p.addSegment(d, child, 6, 8);
        Memo m;
        m.targetType = MemoTarget::Segment;
        m.targetId = s2;
        m.body = "nota";
        const qint64 memo = p.saveMemo(m);
        const qint64 v = p.addVariable("Ruolo", VariableType::Text);
        p.setVariableValue(d, v, "docente");

        // Deleting the parent code removes subcode, segments and the segment memo: one step.
        QVERIFY(p.deleteCode(parent));
        QVERIFY(p.codes().isEmpty());
        QVERIFY(p.memos().isEmpty());
        QCOMPARE(p.undoLabel(), QString("Elimina codice"));
        QVERIFY(p.undo());
        QCOMPARE(p.code(child).parentId, parent);
        QCOMPARE(p.segment(s1).codeId, parent);
        QCOMPARE(p.segment(s2).codeId, child);
        QCOMPARE(p.memo(memo).body, QString("nota"));

        // Deleting the document removes its segments, memos and variable values.
        QVERIFY(p.deleteDocument(d));
        QVERIFY(p.allSegments().isEmpty());
        QVERIFY(p.undo());
        QCOMPARE(p.documentText(d), QString("testo di prova per la cascata"));
        QCOMPARE(p.allSegments().size(), 2);
        QCOMPARE(p.variableValue(d, v), QString("docente"));
        QCOMPARE(p.memo(memo).targetId, s2);

        // Variable upsert: undo the update, then the insert.
        p.setVariableValue(d, v, "studente");
        QVERIFY(p.undo());
        QCOMPARE(p.variableValue(d, v), QString("docente"));
    }

    void undoBatchAndTextEdit()
    {
        Project p;
        QVERIFY(p.create(path("u3.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d = p.addDocument(g, "Doc", "abc def ghi");
        const qint64 c = p.addCode(0, "X", Qt::red);
        const qint64 s = p.addSegment(d, c, 4, 7);

        // Text edit with shifted segment: one step, undone together.
        QList<Segment> segs = p.segmentsForDocument(d);
        segs.first().start += 3;
        segs.first().end += 3;
        QVERIFY(p.updateDocumentText(d, "XX abc def ghi", segs, {}));
        QCOMPARE(p.undoLabel(), QString("Modifica testo"));
        QVERIFY(p.undo());
        QCOMPARE(p.documentText(d), QString("abc def ghi"));
        QCOMPARE(p.segment(s).start, 4);
        QVERIFY(p.redo());
        QCOMPARE(p.documentText(d), QString("XX abc def ghi"));
        QCOMPARE(p.segment(s).start, 7);

        // A labelled batch of several operations is a single step.
        p.setUndoLabel("Codifica automatica");
        p.beginBatch();
        p.addSegment(d, c, 0, 2);
        p.addSegment(d, c, 11, 14);
        QVERIFY(p.commitBatch());
        QCOMPARE(p.undoLabel(), QString("Codifica automatica"));
        QCOMPARE(p.allSegments().size(), 3);
        QVERIFY(p.undo());
        QCOMPARE(p.allSegments().size(), 1);

        // Moving a code and undoing restores parent and order.
        const qint64 c2 = p.addCode(0, "Y", Qt::red);
        QVERIFY(p.moveCode(c2, c));
        QVERIFY(p.undo());
        QCOMPARE(p.code(c2).parentId, qint64(0));
        QCOMPARE(p.code(c2).position, 1);

        // History is per session.
        p.close();
        QVERIFY(p.open(path("u3.tsra")));
        QVERIFY(!p.canUndo());
        QVERIFY(!p.canRedo());
        p.addGroup("Nuovo");
        QVERIFY(p.canUndo());
    }

    void adjustSegments()
    {
        QList<Segment> segs;
        Segment s;
        s.id = 1;
        s.start = 10;
        s.end = 20;
        segs << s;

        auto run = [&](int pos, int removed, int added) {
            QList<Segment> copy = segs;
            TextUtils::adjustSegmentsForEdit(copy, pos, removed, added);
            return copy;
        };
        QCOMPARE(run(0, 0, 5).first().start, 15);        // insert before
        QCOMPARE(run(0, 0, 5).first().end, 25);
        QCOMPARE(run(15, 0, 3).first().end, 23);         // insert inside grows
        QCOMPARE(run(20, 0, 3).first().end, 20);         // insert right after: unchanged
        QCOMPARE(run(10, 0, 3).first().start, 13);       // insert at start: outside
        QCOMPARE(run(12, 4, 0).first().end, 16);         // delete inside shrinks
        QVERIFY(run(5, 20, 0).isEmpty());                // delete everything around it
        QCOMPARE(run(5, 10, 0).first().start, 5);        // delete overlapping start
        QCOMPARE(run(5, 10, 0).first().end, 10);
    }

    void adjustLinks()
    {
        // Link 1: both ends in document 1; link 2: one end there, the other in document 2.
        DocumentLink same;
        same.id = 1;
        same.fromDocumentId = same.toDocumentId = 1;
        same.fromStart = 0;
        same.fromEnd = 5;
        same.toStart = 20;
        same.toEnd = 30;
        DocumentLink across;
        across.id = 2;
        across.fromDocumentId = 2;
        across.fromStart = 3;
        across.fromEnd = 8;
        across.toDocumentId = 1;
        across.toStart = 10;
        across.toEnd = 15;

        auto run = [&](int pos, int removed, int added) {
            QList<DocumentLink> copy{same, across};
            TextUtils::adjustLinksForEdit(copy, 1, pos, removed, added);
            return copy;
        };
        // Insertion at the very beginning shifts every end in document 1, not the one in document 2.
        QList<DocumentLink> r = run(0, 0, 4);
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[0].fromStart, 4); // text typed at an end's start stays outside it, as for segments
        QCOMPARE(r[0].fromEnd, 9);
        QCOMPARE(r[0].toStart, 24);
        QCOMPARE(r[0].toEnd, 34);
        QCOMPARE(r[1].toStart, 14);
        QCOMPARE(r[1].fromStart, 3); // other document: untouched
        QCOMPARE(r[1].fromEnd, 8);

        // Insertion between the ends moves only the later ones.
        r = run(17, 0, 2);
        QCOMPARE(r[0].fromEnd, 5);
        QCOMPARE(r[0].toStart, 22);
        QCOMPARE(r[1].toEnd, 15);

        // Deleting the text of one end removes the whole link.
        r = run(9, 7, 0); // [9, 16) contains [10, 15)
        QCOMPARE(r.size(), 1);
        QCOMPARE(r[0].id, qint64(1));
        QCOMPARE(r[0].toStart, 13);

        // Single range: same rule as segments.
        int s = 10, e = 20;
        QVERIFY(TextUtils::adjustRangeForEdit(s, e, 20, 0, 3));
        QCOMPARE(e, 20);
        QVERIFY(!TextUtils::adjustRangeForEdit(s, e, 5, 20, 0));
    }

    void documentLinks()
    {
        Project p;
        QVERIFY(p.create(path("links.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 a = p.addDocument(g, "A", "uno due tre quattro");
        const qint64 b = p.addDocument(g, "B", "alfa beta gamma");

        QSignalSpy spy(&p, &Project::linksChanged);
        const qint64 l = p.addLink(a, 4, 7, b, 5, 9, "contraddice"); // "due" -> "beta"
        QVERIFY(l > 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(p.undoLabel(), QString("Nuovo collegamento"));
        const DocumentLink link = p.link(l);
        QCOMPARE(link.fromDocumentId, a);
        QCOMPARE(link.toDocumentId, b);
        QCOMPARE(p.documentText(b).mid(link.toStart, link.toEnd - link.toStart), QString("beta"));
        QCOMPARE(link.comment, QString("contraddice"));
        QVERIFY(!link.uuid.isEmpty());
        QCOMPARE(link.author, p.author());
        // Found from both ends.
        QCOMPARE(p.linksForDocument(a).size(), 1);
        QCOMPARE(p.linksForDocument(b).size(), 1);
        QCOMPARE(p.links().size(), 1);

        // Invalid ends are rejected.
        QCOMPARE(p.addLink(a, 4, 4, b, 0, 4), qint64(0));      // empty
        QCOMPARE(p.addLink(a, 0, 3, b, 10, 99), qint64(0));    // outside the text
        QCOMPARE(p.addLink(a, 0, 3, 9999, 0, 3), qint64(0));   // unknown document
        QCOMPARE(p.addLink(a, 0, 3, a, 0, 3), qint64(0));      // a passage linked to itself
        QVERIFY(p.addLink(a, 0, 3, a, 8, 11) > 0);              // same document, other passage
        QCOMPARE(p.linksForDocument(a).size(), 2);
        QVERIFY(p.undo());

        QVERIFY(p.setLinkComment(l, "conferma"));
        QCOMPARE(p.link(l).comment, QString("conferma"));
        QVERIFY(p.undo());
        QCOMPARE(p.link(l).comment, QString("contraddice"));

        QVERIFY(p.deleteLink(l));
        QVERIFY(p.links().isEmpty());
        QVERIFY(p.undo());
        QCOMPARE(p.link(l).toStart, 5);

        // Deleting either document deletes the link; undo brings it back.
        QVERIFY(p.deleteDocument(b));
        QVERIFY(p.links().isEmpty());
        QVERIFY(p.undo());
        QCOMPARE(p.links().size(), 1);

        // Editing the text: the ends move with it and still cover the same words.
        const QString edited = "ZERO uno due tre quattro";
        QList<DocumentLink> links = p.linksForDocument(a);
        TextUtils::adjustLinksForEdit(links, a, 0, 0, 5);
        QVERIFY(p.updateDocumentText(a, edited, p.segmentsForDocument(a), links));
        const DocumentLink moved = p.link(l);
        QCOMPARE(edited.mid(moved.fromStart, moved.fromEnd - moved.fromStart), QString("due"));
        QCOMPARE(moved.toStart, 5); // the end in the other document does not move
        QVERIFY(p.undo());
        QCOMPARE(p.link(l).fromStart, 4);

        // A link missing from the list (its text was deleted) is deleted with the edit.
        QVERIFY(p.updateDocumentText(a, "uno tre quattro", {}, {}));
        QVERIFY(p.links().isEmpty());
        QVERIFY(p.undo());
        QCOMPARE(p.links().size(), 1);
    }

    void codePoints()
    {
        const QString t = QString::fromUtf8("a😀b");
        QCOMPARE(t.size(), 4);
        QCOMPARE(TextUtils::utf16ToCodePoint(t, 3), 2);
        QCOMPARE(TextUtils::codePointToUtf16(t, 2), 3);
        QCOMPARE(TextUtils::codePointToUtf16(t, 3), 4);
    }

    void bounds()
    {
        const QString t = "Prima frase. Seconda frase qui! Terza.\nAltro paragrafo.";
        const int pos = t.indexOf("frase qui");
        const auto [ss, se] = TextUtils::sentenceBounds(t, pos, pos + 5);
        QCOMPARE(t.mid(ss, se - ss), QString("Seconda frase qui!"));
        const auto [ps, pe] = TextUtils::paragraphBounds(t, pos, pos + 5);
        QCOMPARE(t.mid(ps, pe - ps), QString("Prima frase. Seconda frase qui! Terza."));
    }

    void importText()
    {
        writeFile("u.txt", "\xEF\xBB\xBF" "caff\xC3\xA8\r\nriga");
        auto r = Importers::importFile(path("u.txt"));
        QVERIFY(r.ok);
        QCOMPARE(r.text, QString::fromUtf8("caffè\nriga"));

        writeFile("l.txt", "caff\xE8"); // Windows-1252
        r = Importers::importFile(path("l.txt"));
        QVERIFY(r.ok);
        QCOMPARE(r.text, QString::fromUtf8("caffè"));
    }

    void importDocx()
    {
        const QByteArray xml =
            "<?xml version=\"1.0\"?><w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
            "<w:body><w:p><w:r><w:t>Prima</w:t></w:r><w:r><w:tab/><w:t xml:space=\"preserve\"> riga</w:t></w:r></w:p>"
            "<w:p><w:r><w:t>Seconda</w:t><w:br/><w:t>a capo</w:t></w:r><w:del><w:r><w:delText>tolto</w:delText></w:r></w:del></w:p>"
            "</w:body></w:document>";
        {
            QZipWriter zip(path("d.docx"));
            zip.addFile("word/document.xml", xml);
        }
        const auto r = Importers::importFile(path("d.docx"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.text, QString("Prima\t riga\nSeconda\na capo"));
    }

    void importRtf()
    {
        writeFile("r.rtf", "{\\rtf1\\ansi{\\fonttbl{\\f0 Arial;}}\\f0 Caff\\'e8 \\b forte\\b0\\par "
                           "Unicode \\u8364?uro\\par}");
        const auto r = Importers::importFile(path("r.rtf"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.text, QString::fromUtf8("Caffè forte\nUnicode €uro"));
    }

    void csvRoundTrip()
    {
        Exporters::Table t{{"a", "b;c"}, {"x \"y\"", "riga1\nriga2"}};
        QString err;
        QVERIFY(Exporters::writeCsv(path("t.csv"), t, &err));
        QCOMPARE(Exporters::readCsv(path("t.csv"), &err), t);
    }

    void xlsxReader()
    {
        // The first sheet is found through the workbook relationships, not by its usual name.
        const QByteArray workbook =
            "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
            "<workbookPr/><sheets><sheet name=\"Dati\" sheetId=\"7\" r:id=\"rId3\"/>"
            "<sheet name=\"Altro\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>";
        const QByteArray rels =
            "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
            "<Relationship Id=\"rId1\" Target=\"worksheets/sheet1.xml\"/>"
            "<Relationship Id=\"rId3\" Target=\"/xl/worksheets/dati.xml\"/></Relationships>";
        // Shared strings: plain, rich text runs with a phonetic guide, an empty one.
        const QByteArray shared = QByteArray(
            "<sst xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
            "<si><t>Nome</t></si><si><t>Data</t></si>"
            "<si><r><rPr><b/></rPr><t>Anna</t></r><r><t xml:space=\"preserve\"> Maria</t></r>"
            "<rPh sb=\"0\" eb=\"4\"><t>ANNA</t></rPh></si><si><t>Note</t></si><si><t/></si>"
            "<si><t>caffè ☕</t></si></sst>");
        // Styles: the xf of cellStyleXfs do not count; 1 = built-in date, 2 = custom date and time,
        // 3 = "0.00", 4 = a number format with a quoted word containing date letters.
        const QByteArray styles =
            "<styleSheet><numFmts><numFmt numFmtId=\"164\" formatCode=\"dd/mm/yyyy\\ hh:mm\"/>"
            "<numFmt numFmtId=\"165\" formatCode=\"[Red]0&quot; giorni&quot;\"/>"
            "<numFmt numFmtId=\"166\" formatCode=\"[h]:mm:ss\"/></numFmts>"
            "<cellStyleXfs><xf numFmtId=\"14\"/></cellStyleXfs>"
            "<cellXfs><xf numFmtId=\"0\"/><xf numFmtId=\"14\"/><xf numFmtId=\"164\"/><xf numFmtId=\"2\"/>"
            "<xf numFmtId=\"165\"/><xf numFmtId=\"166\"/></cellXfs></styleSheet>";
        const QByteArray sheet =
            "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"><sheetData>"
            "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"B1\" t=\"s\"><v>1</v></c>"
            "<c r=\"C1\" t=\"inlineStr\"><is><t>Valore</t></is></c><c r=\"E1\" t=\"s\"><v>3</v></c></row>"
            "<row r=\"2\"><c r=\"A2\" t=\"s\"><v>2</v></c><c r=\"B2\" s=\"1\"><v>45292</v></c>"
            "<c r=\"C2\" s=\"3\"><v>3.5</v></c><c r=\"D2\" t=\"b\"><v>1</v></c><c r=\"E2\" t=\"e\"><v>#N/A</v></c>"
            "<c r=\"F2\" t=\"s\"><v>4</v></c><c r=\"G2\" s=\"1\"/></row>"
            "<row r=\"4\"><c r=\"A4\" t=\"str\"><f>UPPER(\"x\")</f><v>calcolato</v></c>"
            "<c r=\"B4\" s=\"2\"><v>45292.5</v></c><c r=\"C4\" s=\"4\"><v>7</v></c>"
            "<c r=\"D4\" t=\"s\"><v>5</v></c><c r=\"E4\" s=\"5\"><v>0.75</v></c><c r=\"F4\"><v>1E-3</v></c></row>"
            "<row r=\"5\"><c r=\"A5\" t=\"s\"><v>99</v></c><c r=\"C5\" t=\"b\"><v>0</v></c></row>"
            "<row r=\"9\"><c r=\"A9\" s=\"1\"/></row>"
            "</sheetData></worksheet>";
        {
            QZipWriter zip(path("dati.xlsx"));
            zip.addFile("xl/workbook.xml", workbook);
            zip.addFile("xl/_rels/workbook.xml.rels", rels);
            zip.addFile("xl/sharedStrings.xml", shared);
            zip.addFile("xl/styles.xml", styles);
            zip.addFile("xl/worksheets/sheet1.xml", "<worksheet><sheetData><row><c t=\"inlineStr\"><is><t>sbagliato</t>"
                                                   "</is></c></row></sheetData></worksheet>");
            zip.addFile("xl/worksheets/dati.xml", sheet);
        }
        QString error;
        const Xlsx::Table t = Xlsx::readFirstSheet(path("dati.xlsx"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const Xlsx::Table expected{
            {"Nome", "Data", "Valore", "", "Note"},
            {"Anna Maria", "2024-01-01", "3.5", "VERO"}, // the error cell and the empty ones are empty
            {},                                          // row 3 is not in the file
            {"calcolato", "2024-01-01 12:00:00", "7", QString::fromUtf8("caffè ☕"), "18:00:00", "0.001"},
            {"", "", "FALSO"},                           // shared string index out of range
        };
        QCOMPARE(t, expected);

        // Serial dates, with Excel's 1900-02-29 and the 1904 date system.
        QCOMPARE(Xlsx::serialToDateTime(1).date(), QDate(1900, 1, 1));
        QCOMPARE(Xlsx::serialToDateTime(59).date(), QDate(1900, 2, 28));
        QCOMPARE(Xlsx::serialToDateTime(61).date(), QDate(1900, 3, 1));
        QCOMPARE(Xlsx::serialToDateTime(45292.25), QDateTime(QDate(2024, 1, 1), QTime(6, 0)));
        QCOMPARE(Xlsx::serialToDateTime(0, true).date(), QDate(1904, 1, 1));
        QVERIFY(!Xlsx::serialToDateTime(-1).isValid());

        // Not a workbook.
        writeFile("finto.xlsx", "non sono uno zip");
        QVERIFY(Xlsx::readFirstSheet(path("finto.xlsx"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void xlsxWriterRoundTrip()
    {
        const QString longText(Xlsx::MaxCellLength + 10, QLatin1Char('a'));
        Xlsx::Sheet data;
        data.name = "Dati: prova/1"; // characters Excel refuses in sheet names
        data.rows = {
            {"Nome", "Numero", "Data", "Data e ora", "Codice", "Note"},
            {"Anna", "3.5", "2024-01-01", "2024-01-01 12:30:00", "007", QString::fromUtf8("riga1\nriga2 ☕ \x01")},
            {"Bruno", "-12", "2024-02-30", "1899-12-31", "1e5", " spazi "},
            {"2024", "42", "", "", "_x0041_", longText},
        };
        data.textColumns = {0};
        data.wrapColumns = {5};
        data.fills.insert({1, 0}, QColor("#202060"));       // dark: white text
        data.fills.insert({2, 0}, QColor(255, 0, 0, 128));  // translucent: laid on white
        Xlsx::Sheet second;
        second.name = "dati: prova_1"; // the same name once cleaned, ignoring case
        second.rows = {{"A"}, {"x"}};
        Xlsx::Sheet empty;

        QString error;
        int truncated = -1;
        QVERIFY2(Xlsx::writeWorkbook(path("out.xlsx"), {data, second, empty}, &error, &truncated), qPrintable(error));
        QCOMPARE(truncated, 1);
        QCOMPARE(Xlsx::sheetNames(path("out.xlsx")),
                 QStringList({"Dati_ prova_1", "dati_ prova_1 (2)", "Foglio 3"}));

        // Numbers and dates come back as values (the reader formats them), invalid dates, dates before
        // 1900-03-01, "007" and "1e5" stay text; the control character is dropped, spaces are kept.
        const Xlsx::Table expected{
            {"Nome", "Numero", "Data", "Data e ora", "Codice", "Note"},
            {"Anna", "3.5", "2024-01-01", "2024-01-01 12:30:00", "007", QString::fromUtf8("riga1\nriga2 ☕ ")},
            {"Bruno", "-12", "2024-02-30", "1899-12-31", "1e5", " spazi "},
            {"2024", "42", "", "", "_x0041_", longText.left(Xlsx::MaxCellLength)},
        };
        QCOMPARE(Xlsx::readFirstSheet(path("out.xlsx"), &error), expected);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(Xlsx::readSheet(path("out.xlsx"), 1, &error), Xlsx::Table({{"A"}, {"x"}}));
        QVERIFY(Xlsx::readSheet(path("out.xlsx"), 2, &error).isEmpty());
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // The parts Excel needs, typed cells, frozen and filtered header, fills and wrapping.
        QZipReader zip(path("out.xlsx"));
        for (const char *part : {"[Content_Types].xml", "_rels/.rels", "xl/workbook.xml", "xl/_rels/workbook.xml.rels",
                                 "xl/styles.xml", "xl/worksheets/sheet1.xml", "xl/worksheets/sheet3.xml"})
            QVERIFY2(!zip.fileData(QString::fromLatin1(part)).isEmpty(), part);
        const QByteArray sheet = zip.fileData("xl/worksheets/sheet1.xml");
        QVERIFY(sheet.contains("<c r=\"B2\"><v>3.5</v></c>"));
        QVERIFY(sheet.contains("<c r=\"B3\"><v>-12</v></c>"));
        QVERIFY(sheet.contains("<c r=\"A4\" t=\"inlineStr\"><is><t>2024</t></is></c>")); // a text column
        QVERIFY(sheet.contains("state=\"frozen\""));
        QVERIFY(sheet.contains("<autoFilter ref=\"A1:F4\"/>"));
        QVERIFY(sheet.contains("_x005F_x0041_"));
        QVERIFY(!sheet.contains("\x01"));
        const QByteArray styles = zip.fileData("xl/styles.xml");
        QVERIFY(styles.contains("<b/>"));
        QVERIFY(styles.contains("<fgColor rgb=\"FF202060\"/>"));
        QVERIFY(styles.contains("<fgColor rgb=\"FFFF7F7F\"/>"));
        QVERIFY(styles.contains("<color rgb=\"FFFFFFFF\"/>"));
        QVERIFY(styles.contains("wrapText=\"1\""));
        QVERIFY(styles.contains("numFmtId=\"14\""));
        QVERIFY(styles.contains("numFmtId=\"22\""));
        const QByteArray workbook = zip.fileData("xl/workbook.xml");
        QVERIFY(workbook.contains("_xlnm._FilterDatabase"));
        QVERIFY(workbook.contains("'Dati_ prova_1'!$A$1:$F$4"));

        // Nothing to write.
        QVERIFY(!Xlsx::writeWorkbook(path("vuoto.xlsx"), {}, &error));
        QVERIFY(!error.isEmpty());
    }

    void xlsxProjectExport()
    {
        Project p;
        QVERIFY(p.create(path("excel.tsra")));
        const qint64 group = p.groups().first().id;
        const qint64 doc = p.addDocument(group, "Intervista 1", "Il lavoro da casa mi piace molto.");
        const qint64 doc2 = p.addDocument(group, "2024", "Secondo documento.");
        p.addDocumentSet("Campione", {doc});
        const qint64 code = p.addCode(0, "Benessere", QColor("#3366cc"));
        p.setCodeDescription(code, "Stare bene");
        p.addSegment(doc, code, 0, 9, 2, "commento");
        const qint64 age = p.addVariable("Età", VariableType::Integer);
        const qint64 zip = p.addVariable("CAP", VariableType::Text);
        const qint64 since = p.addVariable("Dal", VariableType::Date);
        p.setVariableValue(doc, age, "34");
        p.setVariableValue(doc, zip, "00184");
        p.setVariableValue(doc2, since, "2023-09-01");
        Memo m;
        m.targetType = MemoTarget::Code;
        m.targetId = code;
        m.title = "Nota";
        m.body = "Testo del memo";
        p.saveMemo(m);

        QString error;
        QVERIFY2(Xlsx::writeWorkbook(path("progetto.xlsx"), Exporters::projectSheets(p), &error), qPrintable(error));
        QCOMPARE(Xlsx::sheetNames(path("progetto.xlsx")),
                 QStringList({"Documenti", "Codici", "Segmenti", "Variabili", "Memo"}));

        const Xlsx::Table docs = Xlsx::readSheet(path("progetto.xlsx"), 0, &error);
        QCOMPARE(docs.size(), 3);
        QCOMPARE(docs[1].mid(0, 5), QStringList({"Intervista 1", "Documenti", "Campione", "1", "33"}));
        QCOMPARE(docs[2].value(0), QString("2024")); // a name, not a number
        QVERIFY(QDate::fromString(docs[1].value(5).left(10), Qt::ISODate).isValid()); // a real date

        const Xlsx::Table codes = Xlsx::readSheet(path("progetto.xlsx"), 1, &error);
        QCOMPARE(codes.value(1), QStringList({"Benessere", "1", "#3366cc", "Stare bene", "1", "Testo del memo"}));

        const Xlsx::Table segs = Xlsx::readSheet(path("progetto.xlsx"), 2, &error);
        QCOMPARE(segs.size(), 2);
        QCOMPARE(segs[1].mid(1, 6), QStringList({"Intervista 1", "Benessere", "0", "9", "2", "commento"}));
        QVERIFY(QDateTime::fromString(segs[1].value(8), "yyyy-MM-dd HH:mm:ss").isValid()); // a real date and time
        QCOMPARE(segs[1].value(9), QString("Il lavoro"));

        const Xlsx::Table vars = Xlsx::readSheet(path("progetto.xlsx"), 3, &error);
        QCOMPARE(vars.value(0), QStringList({"Documento", "Gruppo", "Età", "CAP", "Dal"}));
        QCOMPARE(vars.value(1).mid(2, 2), QStringList({"34", "00184"})); // the text variable keeps its zeros
        QCOMPARE(vars.value(2).value(4), QString("2023-09-01"));

        const Xlsx::Table memos = Xlsx::readSheet(path("progetto.xlsx"), 4, &error);
        QCOMPARE(memos.value(1).mid(0, 3), QStringList({"Codice: Benessere", "Nota", "Testo del memo"}));
        QVERIFY2(error.isEmpty(), qPrintable(error));

        // The code's colour behind the code's cell, typed numbers and dates in the sheet.
        QZipReader archive(path("progetto.xlsx"));
        QVERIFY(archive.fileData("xl/styles.xml").contains("<fgColor rgb=\"FF3366CC\"/>"));
        const QByteArray varsXml = archive.fileData("xl/worksheets/sheet4.xml");
        QVERIFY(varsXml.contains("<c r=\"C2\"><v>34</v></c>"));
        QVERIFY(varsXml.contains("<t>00184</t>"));
        QVERIFY(varsXml.contains("<c r=\"E3\" s=\""));
    }

    void surveyImport()
    {
        const QStringList questions{"Come organizzi la giornata di lavoro?", "Cosa ti manca dell'ufficio?",
                                    "Cosa cambieresti?\n(facoltativo)"};
        QList<QVariantList> rows;
        rows << QVariantList{"ID", "Età", "Genere", "Punteggio", "Data compilazione", questions[0], questions[1],
                             questions[2]};
        for (int i = 1; i <= 20; ++i) {
            QVariantList row{QStringLiteral("R%1").arg(i, 2, 10, QLatin1Char('0')), 20 + i,
                             i % 2 ? "F" : "M", 5 + i / 4.0, QDate(2024, 3, i)};
            for (int q = 1; q <= 3; ++q)
                row << QStringLiteral("Risposta %1 del rispondente %2, con qualche parola in più").arg(q).arg(i);
            if (i == 5)
                row[6] = QString();           // empty answer: skipped
            if (i == 7)
                row[7] = QStringLiteral("  "); // blank answer: skipped
            rows << row;
        }
        writeXlsx("sondaggio.xlsx", rows);

        QString error;
        const Survey::Table table = Survey::readTable(path("sondaggio.xlsx"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(table.size(), 21);

        Project p;
        QVERIFY(p.create(path("survey.tsra")));
        const int groupsBefore = int(p.groups().size());
        QList<Survey::Column> cols = Survey::proposeColumns(p, table);
        QCOMPARE(cols.size(), 8);
        using R = Survey::Role;
        const QList<R> roles{R::Id, R::Variable, R::Variable, R::Variable, R::Variable, R::OpenAnswer,
                             R::OpenAnswer, R::OpenAnswer};
        for (int c = 0; c < cols.size(); ++c)
            QVERIFY2(cols[c].role == roles[c], qPrintable(cols[c].name));
        QCOMPARE(cols[1].type, VariableType::Integer);
        QCOMPARE(cols[2].type, VariableType::Text);
        QCOMPARE(cols[3].type, VariableType::Decimal);
        QCOMPARE(cols[4].type, VariableType::Date);
        QCOMPARE(cols[7].name, QString("Cosa cambieresti? (facoltativo)")); // header on one line

        Survey::Options options;
        options.columns = cols;
        options.groupName = "Sondaggio 2024";
        Survey::Report report;
        QVERIFY2(Survey::importSurvey(p, table, options, &report, &error), qPrintable(error));
        QCOMPARE(report.documents, 20);
        QCOMPARE(report.codedAnswers, 58);
        QCOMPARE(report.newCodes, 3);
        QCOMPARE(report.newVariables, 4);

        // 20 documents in the new group, named after the respondent ID.
        QCOMPARE(p.groups().size(), groupsBefore + 1);
        QCOMPARE(p.groups().last().name, QString("Sondaggio 2024"));
        const QList<Document> docs = p.documentsInGroup(report.groupId);
        QCOMPARE(docs.size(), 20);
        QCOMPARE(docs.first().name, QString("R01"));

        // Each answer is a paragraph after its question, coded with the question's code.
        const QString answer1 = "Risposta 1 del rispondente 1, con qualche parola in più";
        QCOMPARE(p.documentText(docs[0].id),
                 questions[0] + "\n" + answer1 + "\n\n" + questions[1] + "\nRisposta 2 del rispondente 1, con "
                 "qualche parola in più\n\nCosa cambieresti? (facoltativo)\nRisposta 3 del rispondente 1, con "
                 "qualche parola in più");
        QCOMPARE(p.codes().size(), 3);
        const qint64 q1 = p.findCode(0, questions[0]);
        QVERIFY(q1 > 0);
        const QList<Segment> segs = p.segmentsForDocument(docs[0].id);
        QCOMPARE(segs.size(), 3);
        QCOMPARE(segs[0].codeId, q1);
        QCOMPARE(p.documentText(docs[0].id).mid(segs[0].start, segs[0].length()), answer1);
        QCOMPARE(p.segmentsForDocument(docs[4].id).size(), 2);
        QVERIFY(!p.documentText(docs[4].id).contains(questions[1]));
        QCOMPARE(p.segmentsForDocument(docs[6].id).size(), 2);
        QCOMPARE(p.segmentCountByCode().value(q1), 20);

        // Typed variables.
        const QList<Variable> vars = p.variables();
        QCOMPARE(vars.size(), 4);
        QCOMPARE(vars[0].name, QString("Età"));
        QCOMPARE(vars[0].type, VariableType::Integer);
        QCOMPARE(vars[3].type, VariableType::Date);
        QCOMPARE(p.variableValue(docs[2].id, vars[0].id), QString("23"));
        QCOMPARE(p.variableValue(docs[2].id, vars[1].id), QString("F"));
        QCOMPARE(p.variableValue(docs[1].id, vars[2].id), QString("5.5"));
        QCOMPARE(p.variableValue(docs[2].id, vars[3].id), QString("2024-03-03"));

        // The whole import is one undo step.
        QCOMPARE(p.undoLabel(), QString("Importa dati di sondaggio"));
        QVERIFY(p.undo());
        QCOMPARE(p.groups().size(), groupsBefore);
        QVERIFY(p.documents().isEmpty());
        QVERIFY(p.codes().isEmpty());
        QVERIFY(p.variables().isEmpty());
        QVERIFY(!p.canUndo());
        QVERIFY(p.redo());
        QCOMPARE(p.documents().size(), 20);

        // CSV, no ID column, existing variable and code reused, a respondent without answers skipped.
        writeFile("sondaggio.csv", "Età;Opinione\n30;Mi trovo bene a lavorare da casa\n41;\n"
                                   "abc;Preferisco l'ufficio, almeno qualche giorno\n");
        const Survey::Table csv = Survey::readTable(path("sondaggio.csv"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        cols = Survey::proposeColumns(p, csv);
        QCOMPARE(cols.size(), 2);
        QVERIFY(cols[0].existingVariable);
        QCOMPARE(cols[0].role, R::Variable);
        QCOMPARE(cols[0].type, VariableType::Integer);
        QCOMPARE(cols[1].role, R::OpenAnswer);
        cols[1].name = questions[0]; // reuses the code of the first survey
        options.columns = cols;
        options.groupName.clear();
        QVERIFY2(Survey::importSurvey(p, csv, options, &report, &error), qPrintable(error));
        QCOMPARE(report.documents, 2);
        QCOMPARE(report.skippedRows, 1);
        QCOMPARE(report.newCodes, 0);
        QCOMPARE(report.newVariables, 0);
        QCOMPARE(report.invalidValues.size(), 1); // "abc" is not an integer
        const QList<Document> csvDocs = p.documentsInGroup(report.groupId);
        QCOMPARE(csvDocs.size(), 2);
        QCOMPARE(csvDocs[0].name, QString("Rispondente 1"));
        QCOMPARE(csvDocs[1].name, QString("Rispondente 3"));
        QCOMPARE(p.groups().last().name, QString("Sondaggio"));
        QCOMPARE(p.segmentCountByCode().value(q1), 22);

        // Nothing marked as an open answer: nothing is imported.
        for (Survey::Column &c : options.columns)
            c.role = R::Ignore;
        const int docCount = int(p.documents().size());
        QVERIFY(!Survey::importSurvey(p, csv, options, &report, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(p.documents().size(), docCount);
    }

    void retrievalModes()
    {
        using Retrieval::Mode;
        // Codes: 1 = A, 2 = B, 3 = C. Document 10 has nested/adjacent cases, document 11 identical ones.
        const QList<Segment> segs{seg(1, 10, 1, 0, 10),  seg(2, 10, 1, 20, 30), seg(3, 10, 2, 5, 15),
                                  seg(4, 10, 2, 30, 40), // starts where A#2 ends: not overlapping
                                  seg(5, 10, 3, 8, 12),  seg(6, 10, 1, 8, 12), // A#6 nested in B#3
                                  seg(7, 11, 1, 0, 5),   seg(8, 11, 2, 0, 5)};
        Retrieval::Query q;
        q.codesA = {1};
        q.codesB = {2};

        q.mode = Mode::Overlap;
        auto r = Retrieval::run(q, segs);
        QCOMPARE(spans(r), (QList<QPair<int, int>>{{0, 10}, {8, 12}, {0, 5}}));
        QCOMPARE(segmentIds(r), (QList<qint64>{1, 6, 7}));

        q.mode = Mode::OnlyThisCode;
        QCOMPARE(segmentIds(Retrieval::run(q, segs)), (QList<qint64>{2}));

        q.mode = Mode::IfInside;
        QCOMPARE(segmentIds(Retrieval::run(q, segs)), (QList<qint64>{6, 7}));

        q.mode = Mode::IfOutside;
        QCOMPARE(segmentIds(Retrieval::run(q, segs)), (QList<qint64>{1, 2}));

        // A segment never counts as overlapping itself when a code is in both lists.
        Retrieval::Query self;
        self.mode = Mode::Overlap;
        self.codesA = {1};
        self.codesB = {1};
        QVERIFY(Retrieval::run(self, {seg(1, 10, 1, 0, 10), seg(2, 10, 1, 20, 30)}).isEmpty());

        // Adjacent segments (end == start) do not overlap.
        q.mode = Mode::Overlap;
        QVERIFY(Retrieval::run(q, {seg(1, 10, 1, 0, 10), seg(2, 10, 2, 10, 20)}).isEmpty());
        // An empty list gives no results.
        q.codesB.clear();
        QVERIFY(Retrieval::run(q, segs).isEmpty());
    }

    void retrievalIntersections()
    {
        using Retrieval::Mode;
        Retrieval::Query q;
        q.mode = Mode::Intersection;
        q.codesA = {1, 2};
        // Code 1: 0-10; code 2: 5-15 and 8-20; code 3 (not in the query) covers everything.
        const QList<Segment> segs{seg(1, 10, 1, 0, 10), seg(2, 10, 2, 5, 15), seg(3, 10, 2, 8, 20),
                                  seg(4, 10, 3, 0, 100)};
        auto r = Retrieval::run(q, segs);
        QCOMPARE(spans(r), (QList<QPair<int, int>>{{5, 10}}));
        QCOMPARE(r[0].codeIds, (QList<qint64>{1, 2}));
        QCOMPARE(r[0].segmentId, qint64(0)); // a new interval, not a stored segment

        QVERIFY(Retrieval::run(q, {seg(1, 10, 1, 0, 10), seg(2, 10, 2, 10, 20)}).isEmpty()); // adjacent
        QCOMPARE(spans(Retrieval::run(q, {seg(1, 10, 1, 3, 9), seg(2, 10, 2, 3, 9)})),
                 (QList<QPair<int, int>>{{3, 9}})); // identical
        QCOMPARE(spans(Retrieval::run(q, {seg(1, 10, 1, 0, 50), seg(2, 10, 2, 10, 20)})),
                 (QList<QPair<int, int>>{{10, 20}})); // nested
        QCOMPARE(spans(Retrieval::run(q, {seg(1, 10, 1, 0, 10), seg(2, 10, 1, 5, 20), seg(3, 10, 2, 0, 20)})),
                 (QList<QPair<int, int>>{{0, 20}})); // same-code overlap counts once

        q.mode = Mode::IntersectionSet;
        q.codesA = {1, 2, 3};
        q.count = 2;
        QCOMPARE(spans(Retrieval::run(q, segs)), (QList<QPair<int, int>>{{0, 20}}));
        q.count = 3;
        QCOMPARE(spans(Retrieval::run(q, segs)), (QList<QPair<int, int>>{{5, 10}}));
    }

    void retrievalOnlyOneAndProximity()
    {
        using Retrieval::Mode;
        Retrieval::Query q;
        q.mode = Mode::OnlyOneCode;
        q.codesA = {1, 2};
        const QList<Segment> segs{seg(1, 10, 1, 0, 10), seg(2, 10, 2, 5, 15), seg(3, 10, 1, 30, 40),
                                  seg(4, 10, 1, 35, 45)}; // overlap within the same code is allowed
        QCOMPARE(segmentIds(Retrieval::run(q, segs)), (QList<qint64>{3, 4}));

        // Paragraph distance counts the newlines between the two passages.
        const QString text = QStringLiteral("aaaa\nbbbb\ncccc\ndddd");
        const auto textOf = [&text](qint64) { return text; };
        const QList<Segment> pair{seg(1, 10, 1, 0, 4), seg(2, 10, 2, 10, 14)}; // 2 newlines, 6 characters apart
        Retrieval::Query prox;
        prox.mode = Mode::Near;
        prox.codesA = {1};
        prox.codesB = {2};
        prox.distance = 1;
        QCOMPARE(Retrieval::run(prox, pair, textOf).size(), 0);
        prox.distance = 2;
        QCOMPARE(Retrieval::run(prox, pair, textOf).size(), 1);
        prox.inParagraphs = false;
        prox.distance = 5;
        QCOMPARE(Retrieval::run(prox, pair, textOf).size(), 0);
        prox.distance = 6;
        QCOMPARE(Retrieval::run(prox, pair, textOf).size(), 1);
        // Overlapping and touching passages are at distance 0.
        prox.distance = 0;
        QCOMPARE(Retrieval::run(prox, {seg(1, 10, 1, 0, 10), seg(2, 10, 2, 9, 20)}, textOf).size(), 1);
        QCOMPARE(Retrieval::run(prox, {seg(1, 10, 1, 0, 10), seg(2, 10, 2, 10, 20)}, textOf).size(), 1);

        Retrieval::Query fol = prox;
        fol.mode = Mode::FollowedBy;
        fol.inParagraphs = true;
        fol.distance = 2;
        const auto r = Retrieval::run(fol, pair, textOf);
        QCOMPARE(r.size(), 1);
        QCOMPARE(r[0].start, 0);
        QCOMPARE(r[0].end, 14); // from the start of A to the end of B
        QCOMPARE(r[0].codeIds, (QList<qint64>{1, 2}));
        QCOMPARE(Retrieval::run(fol, {seg(1, 10, 1, 10, 14), seg(2, 10, 2, 0, 4)}, textOf).size(), 0); // B before A
    }

    void cooccurrenceMatrix()
    {
        using namespace Cooccurrence;
        // Doc 10: A 0-10, A 20-30, B 5-15, B 25-27, C 100-110. Doc 11: A 0-5, B 50-60.
        const QList<Segment> segs{seg(1, 10, 1, 0, 10),   seg(2, 10, 1, 20, 30), seg(3, 10, 2, 5, 15),
                                  seg(4, 10, 2, 25, 27),  seg(5, 10, 3, 100, 110), seg(6, 11, 1, 0, 5),
                                  seg(7, 11, 2, 50, 60)};
        Options o;
        o.codes = {1, 2, 3};

        // Overlap: A∩B gives the intervals 5-10 and 25-27; the diagonal is the segment count.
        Matrix m = compute(o, segs);
        QCOMPARE(m.count(1, 2), 2);
        QCOMPARE(m.count(2, 1), 2); // symmetric
        QCOMPARE(m.count(1, 3), 0);
        QCOMPARE(m.frequency(1), 3);
        QCOMPARE(m.frequency(2), 3);
        QCOMPARE(m.frequency(3), 1);
        for (qint64 a : o.codes)
            for (qint64 b : o.codes)
                QCOMPARE(m.count(a, b), m.count(b, a));
        // 2 of A's 3 segments and 2 of B's 3 take part: 2 / (3 + 3 - 2)
        QCOMPARE(m.jaccard(1, 2), 0.5);
        QCOMPARE(m.jaccard(1, 1), 1.0);
        QCOMPARE(m.jaccard(1, 3), 0.0);
        const auto cell = passages(o, 1, 2, segs);
        QCOMPARE(spans(cell), (QList<QPair<int, int>>{{5, 10}, {25, 27}}));
        QCOMPARE(passages(o, 1, 1, segs).size(), 3); // the diagonal lists the code's segments

        // Proximity in characters: A 20-30 and B 5-15 are 5 apart, A 0-10 and B 25-27 are 15 apart,
        // A 0-5 and B 50-60 (another document) are 45 apart.
        o.criterion = Criterion::Proximity;
        o.inParagraphs = false;
        o.distance = 0;
        QCOMPARE(compute(o, segs).count(1, 2), 2); // 0-10/5-15 and 20-30/25-27 overlap
        o.distance = 5;
        m = compute(o, segs);
        QCOMPARE(m.count(1, 2), 3); // adds 20-30 with 5-15
        QCOMPARE(m.count(1, 3), 0);
        o.distance = 100;
        QCOMPARE(compute(o, segs).count(1, 3), 2); // doc 10 only: C is in no other document
        QCOMPARE(spans(passages(o, 1, 3, segs)), (QList<QPair<int, int>>{{0, 110}, {20, 110}}));

        // Proximity in paragraphs uses the document text.
        const QString text = QStringLiteral("aaaa\nbbbb\ncccc\ndddd");
        const QList<Segment> two{seg(1, 10, 1, 0, 4), seg(2, 10, 2, 10, 14)};
        const auto textOf = [&text](qint64) { return text; };
        o.codes = {1, 2};
        o.inParagraphs = true;
        o.distance = 1;
        QCOMPARE(compute(o, two, textOf).count(1, 2), 0);
        o.distance = 2;
        QCOMPARE(compute(o, two, textOf).count(1, 2), 1);

        // Same document: one per shared document; the diagonal counts documents.
        o.criterion = Criterion::SameDocument;
        o.codes = {1, 2, 3};
        m = compute(o, segs);
        QCOMPARE(m.count(1, 2), 2);
        QCOMPARE(m.count(1, 3), 1);
        QCOMPARE(m.count(2, 3), 1);
        QCOMPARE(m.frequency(1), 2);
        QCOMPARE(m.frequency(3), 1);
        QCOMPARE(m.jaccard(1, 2), 1.0);
        QCOMPARE(m.jaccard(1, 3), 0.5);
        QCOMPARE(passages(o, 1, 3, segs).size(), 3); // A 0-10, A 20-30 and C of doc 10

        // Segments of codes outside the list are ignored.
        o.codes = {1};
        QCOMPARE(compute(o, segs).counts.size(), 1);
    }

    void cooccurrenceLayout()
    {
        using Weights = QList<QList<double>>;
        QVERIFY(Cooccurrence::forceLayout(Weights()).isEmpty());
        QCOMPARE(Cooccurrence::forceLayout(Weights{QList<double>{0}}), (QList<QPointF>{QPointF(0.5, 0.5)}));

        // 0-1 strongly linked, 2 isolated: same seed gives the same layout, linked nodes end up closer.
        const Weights w{{0, 5, 0}, {5, 0, 0}, {0, 0, 0}};
        const auto a = Cooccurrence::forceLayout(w, 7);
        QCOMPARE(a, Cooccurrence::forceLayout(w, 7));
        QCOMPARE(a.size(), 3);
        for (const QPointF &p : a) {
            QVERIFY(p.x() >= 0 && p.x() <= 1);
            QVERIFY(p.y() >= 0 && p.y() <= 1);
        }
        const auto dist = [&a](int i, int j) { return QLineF(a[i], a[j]).length(); };
        QVERIFY(dist(0, 1) < dist(0, 2));
        QVERIFY(dist(0, 1) < dist(1, 2));
    }

    void statsDescribe()
    {
        using VT = VariableType;
        // Empty and invalid values are counted apart and left out of the figures.
        const auto n = Stats::describe(VT::Integer, {"10", "20", "", "abc", "40", "20"});
        QCOMPARE(n.total, 6);
        QCOMPARE(n.valid, 4);
        QCOMPARE(n.empty, 1);
        QCOMPARE(n.invalid, 1);
        QVERIFY(n.numeric);
        QCOMPARE(n.mean, 22.5);
        QCOMPARE(n.median, 20.0);
        QCOMPARE(n.min, 10.0);
        QCOMPARE(n.max, 40.0);
        QVERIFY(qAbs(n.stdDev - 12.5831) < 1e-3); // sample standard deviation
        QCOMPARE(n.frequencies.first().value, QString("20"));
        QCOMPARE(n.frequencies.first().count, 2);
        QCOMPARE(n.frequencies.first().percent, 50.0);

        QCOMPARE(Stats::describe(VT::Integer, {"1", "2", "3", "4"}).median, 2.5);
        QCOMPARE(Stats::describe(VT::Integer, {"7"}).stdDev, 0.0);
        QCOMPARE(Stats::describe(VT::Integer, {"", "x"}).valid, 0);

        const auto t = Stats::describe(VT::Text, {"F", "M", "F", "", "F"});
        QVERIFY(!t.numeric);
        QCOMPARE(t.valid, 4);
        QCOMPARE(t.frequencies.size(), 2);
        QCOMPARE(t.frequencies[0].value, QString("F"));
        QCOMPARE(t.frequencies[0].percent, 75.0);

        const auto b = Stats::describe(VT::Boolean, {"1", "0", "1"});
        QCOMPARE(b.frequencies.first().value, QString("1"));
        QCOMPARE(b.frequencies.first().count, 2);
    }

    void statsChiSquare()
    {
        // Critical values at 5%: chi2(1) = 3.841459, chi2(2) = 5.991465, chi2(4) = 9.487729.
        QVERIFY(qAbs(Stats::chiSquareSurvival(3.841459, 1) - 0.05) < 1e-5);
        QVERIFY(qAbs(Stats::chiSquareSurvival(5.991465, 2) - 0.05) < 1e-5);
        QVERIFY(qAbs(Stats::chiSquareSurvival(9.487729, 4) - 0.05) < 1e-5);
        QVERIFY(qAbs(Stats::chiSquareSurvival(20, 3) - 0.000167) < 1e-5); // continued-fraction branch
        QCOMPARE(Stats::chiSquareSurvival(0, 2), 1.0);

        // [[10, 20], [30, 40]]: n (ad - bc)^2 / (r1 r2 c1 c2) = 100 * 200^2 / (30 * 70 * 40 * 60)
        Stats::Crosstab t;
        t.columns = {{"a", {}}, {"b", {}}};
        t.counts = {{10, 20}, {30, 40}};
        auto r = Stats::chiSquare(t);
        QVERIFY(r.valid);
        QCOMPARE(r.df, 1);
        QVERIFY(qAbs(r.statistic - 0.793651) < 1e-5);
        QVERIFY(qAbs(r.pValue - 0.3730) < 1e-3);
        QCOMPARE(r.smallExpected, 0);

        // Empty rows and columns are ignored; a single remaining row cannot be tested.
        t.counts = {{10, 20}, {0, 0}};
        QVERIFY(!Stats::chiSquare(t).valid);
        t.columns = {{"a", {}}, {"b", {}}, {"c", {}}};
        t.counts = {{1, 0, 3}, {2, 0, 1}};
        r = Stats::chiSquare(t);
        QVERIFY(r.valid);
        QCOMPARE(r.df, 1);
        QCOMPARE(r.smallExpected, 4);
    }

    void statsHistogram()
    {
        const auto s = Stats::describe(VariableType::Integer, {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"});
        const auto h = Stats::histogram(s, 3);
        QCOMPARE(h.edges.size(), 4);
        QCOMPARE(h.edges.first(), 1.0);
        QCOMPARE(h.edges.last(), 10.0);
        QCOMPARE(h.counts, (QList<int>{3, 3, 4})); // [1,4) [4,7) [7,10]
        QCOMPARE(Stats::histogram(s).counts.size(), 5); // Sturges: ceil(log2(10) + 1)
        QVERIFY(Stats::histogram(Stats::describe(VariableType::Integer, {"", "x"})).counts.isEmpty());
        QCOMPARE(Stats::histogram(Stats::describe(VariableType::Integer, {"5", "5"}), 2).counts, (QList<int>{2, 0}));
    }

    void statsCrosstab()
    {
        QTemporaryDir dir;
        Project p;
        QVERIFY(p.create(dir.filePath("stats.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d1 = p.addDocument(g, "D1", QString(50, 'x'));
        const qint64 d2 = p.addDocument(g, "D2", QString(50, 'x'));
        const qint64 d3 = p.addDocument(g, "D3", QString(50, 'x'));
        const qint64 d4 = p.addDocument(g, "D4", QString(50, 'x'));
        const qint64 sex = p.addVariable("Sesso", VariableType::Text);
        const qint64 age = p.addVariable("Eta", VariableType::Integer);
        p.setVariableValue(d1, sex, "F");
        p.setVariableValue(d2, sex, "M");
        p.setVariableValue(d3, sex, "F");
        // d4 has no value
        p.setVariableValue(d1, age, "20");
        p.setVariableValue(d2, age, "40");
        p.setVariableValue(d3, age, "60");
        p.setVariableValue(d4, age, "oops");

        const qint64 stress = p.addCode(0, "Stress", Qt::red);
        const qint64 other = p.addCode(0, "Altro", Qt::blue);
        p.addSegment(d1, stress, 0, 5);
        p.addSegment(d1, stress, 10, 15);
        p.addSegment(d2, stress, 0, 5);
        p.addSegment(d3, other, 0, 5);
        p.addSegment(d4, stress, 0, 5);

        auto groups = Stats::groupsByVariable(p, sex);
        QCOMPARE(groups.size(), 3); // F, M, (vuoto)
        QCOMPARE(groups[0].label, QString("F"));
        QCOMPARE(groups[0].documents, (QList<qint64>{d1, d3}));
        QCOMPARE(groups[1].documents, (QList<qint64>{d2}));
        QCOMPARE(groups[2].documents, (QList<qint64>{d4}));

        const QList<qint64> codes{stress, other};
        const auto segs = p.allSegments();
        auto t = Stats::crosstab(codes, groups, segs, Stats::Measure::Segments);
        QCOMPARE(t.count(0, 0), 2); // Stress in F: two segments in d1
        QCOMPARE(t.count(0, 1), 1);
        QCOMPARE(t.count(0, 2), 1);
        QCOMPARE(t.count(1, 0), 1);
        QCOMPARE(t.totalDocuments, (QList<int>{2, 1, 1}));
        QCOMPARE(t.rowTotal(0), 4);
        QCOMPARE(t.percent(0, 0, Stats::Percent::OfColumn), 100.0 * 2 / 3); // column total = 3 segments
        QCOMPARE(t.percent(0, 0, Stats::Percent::OfRow), 50.0);

        t = Stats::crosstab(codes, groups, segs, Stats::Measure::Documents);
        QCOMPARE(t.count(0, 0), 1); // only d1 has Stress among F's documents
        QCOMPARE(t.percent(0, 0, Stats::Percent::OfColumn), 50.0); // 1 of 2 documents
        QCOMPARE(t.count(1, 0), 1);

        // Numeric classes: 20, 40, 60 in two classes of width 20 ([20,40) and [40,60]); invalid has its own group.
        groups = Stats::groupsByVariable(p, age, {}, 2);
        QCOMPARE(groups.size(), 3);
        QCOMPARE(groups[0].documents, (QList<qint64>{d1})); // [20, 40)
        QCOMPARE(groups[1].documents, (QList<qint64>{d2, d3})); // [40, 60]: the maximum is in the last class
        QCOMPARE(groups[2].label, QString("(non valido)"));
        QCOMPARE(groups[2].documents, (QList<qint64>{d4}));

        // Restricted to some documents, and the group-based columns.
        QCOMPARE(Stats::groupsByVariable(p, sex, {d1, d2}).size(), 2);
        QCOMPARE(Stats::describe(p, age, {d1, d2}).mean, 30.0);
        QCOMPARE(Stats::groupsByDocumentGroups(p).first().documents.size(), 4);
        const qint64 set = p.addDocumentSet("S", {d1, d2});
        Q_UNUSED(set);
        QCOMPARE(Stats::groupsByDocumentSets(p).first().documents, (QList<qint64>{d1, d2}));

        // Codes become integer variables, in one undo step; a repeated name gets a suffix.
        const int before = int(p.variables().size());
        const auto ids = Stats::codesToVariables(p, {stress}, Stats::Measure::Segments);
        QCOMPARE(ids.size(), 1);
        QCOMPARE(p.variables().size(), before + 1);
        QCOMPARE(p.variableValue(d1, ids[0]), QString("2"));
        QCOMPARE(p.variableValue(d3, ids[0]), QString("0"));
        const auto again = Stats::codesToVariables(p, {stress, other}, Stats::Measure::Documents);
        QCOMPARE(again.size(), 2);
        QCOMPARE(p.variableValue(d1, again[0]), QString("1"));
        QVERIFY(p.variables().last().name != p.variables().at(p.variables().size() - 2).name);
        QVERIFY(p.undo());
        QCOMPARE(p.variables().size(), before + 1);
    }

    void variableTypeInference()
    {
        using VT = VariableType;
        QCOMPARE(Exporters::inferVariableType({"1", "-20", "", "300"}), VT::Integer);
        QCOMPARE(Exporters::inferVariableType({"1,5", "2", "3,25"}), VT::Decimal);          // Italian
        QCOMPARE(Exporters::inferVariableType({"1.5", "2", "3.25"}), VT::Decimal);          // English
        QCOMPARE(Exporters::inferVariableType({"1.234,5", "2"}), VT::Decimal);              // thousands + comma
        QCOMPARE(Exporters::inferVariableType({"1,234.5", "2"}), VT::Decimal);
        QCOMPARE(Exporters::inferVariableType({"1.234.567", "2"}), VT::Decimal);
        QCOMPARE(Exporters::inferVariableType({"1,2,3"}), VT::Text);
        QCOMPARE(Exporters::inferVariableType({"sì", "no", "sì"}), VT::Boolean);
        QCOMPARE(Exporters::inferVariableType({"true", "false"}), VT::Boolean);
        QCOMPARE(Exporters::inferVariableType({"vero", "vero"}), VT::Boolean);
        QCOMPARE(Exporters::inferVariableType({"0", "1", "1"}), VT::Boolean);
        QCOMPARE(Exporters::inferVariableType({"1", "1"}), VT::Integer);                    // one bare value
        QCOMPARE(Exporters::inferVariableType({"si", "no", "forse"}), VT::Text);
        QCOMPARE(Exporters::inferVariableType({"2024-03-05", "2023-12-31"}), VT::Date);
        QCOMPARE(Exporters::inferVariableType({"05/03/2024", "31/12/2023"}), VT::Date);
        QCOMPARE(Exporters::inferVariableType({"31/02/2024"}), VT::Text);                   // invalid day
        QCOMPARE(Exporters::inferVariableType({"12", "abc"}), VT::Text);
        QCOMPARE(Exporters::inferVariableType({"", " "}), VT::Text);

        QString out;
        QVERIFY(Exporters::normalizeVariableValue(VT::Decimal, "1.234,5", &out));
        QCOMPARE(out, QString("1234.5"));
        QVERIFY(Exporters::normalizeVariableValue(VT::Date, "05/03/2024", &out));
        QCOMPARE(out, QString("2024-03-05"));
        QVERIFY(Exporters::normalizeVariableValue(VT::Boolean, "Sì", &out));
        QCOMPARE(out, QString("1"));
        QVERIFY(Exporters::normalizeVariableValue(VT::Boolean, "falso", &out));
        QCOMPARE(out, QString("0"));
        QVERIFY(!Exporters::normalizeVariableValue(VT::Integer, "1,5", &out));
        QVERIFY(Exporters::normalizeVariableValue(VT::Integer, "", &out));
        QVERIFY(out.isEmpty());
    }

    void variablesCsvImportTypes()
    {
        Project p;
        QVERIFY(p.create(path("vars.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d1 = p.addDocument(g, "Doc uno", "a");
        const qint64 d2 = p.addDocument(g, "Doc due", "b");
        const qint64 existing = p.addVariable("Livello", VariableType::Integer);

        writeFile("vars.csv", QString::fromUtf8("Documento;Età;Peso;Consenso;Nascita;Nota;Livello\n"
                                                "Doc uno;34;70,5;sì;05/03/1990;ciao;3\n"
                                                "Doc due;41;82;no;1983-11-02;;abc\n"
                                                "Ignoto;1;1;sì;01/01/2000;x;1\n")
                                    .toUtf8());
        Exporters::CsvVariablesPreview prev;
        QString err;
        QVERIFY2(Exporters::previewVariablesCsv(p, path("vars.csv"), &prev, &err), qPrintable(err));
        QCOMPARE(prev.matchedRows, 2);
        QCOMPARE(prev.unmatchedDocuments, QStringList{"Ignoto"});
        QCOMPARE(prev.columns.size(), 6);
        QCOMPARE(prev.columns[0].type, VariableType::Integer);
        QCOMPARE(prev.columns[1].type, VariableType::Decimal);
        QCOMPARE(prev.columns[2].type, VariableType::Boolean);
        QCOMPARE(prev.columns[3].type, VariableType::Date);
        QCOMPARE(prev.columns[4].type, VariableType::Text);
        QVERIFY(prev.columns[5].existing);
        QCOMPARE(prev.columns[5].invalidCount, 1);

        QString report;
        // The user may override a proposed type.
        QVERIFY(Exporters::importVariablesCsv(p, path("vars.csv"), &report, &err,
                                              {{"Nota", VariableType::Text}, {"Età", VariableType::Text}}));
        QVERIFY(report.contains("abc"));
        QHash<QString, Variable> byName;
        for (const Variable &v : p.variables())
            byName.insert(v.name, v);
        QCOMPARE(byName["Età"].type, VariableType::Text);
        QCOMPARE(byName["Peso"].type, VariableType::Decimal);
        QCOMPARE(p.variableValue(d1, byName["Peso"].id), QString("70.5"));
        QCOMPARE(p.variableValue(d1, byName["Consenso"].id), QString("1"));
        QCOMPARE(p.variableValue(d2, byName["Consenso"].id), QString("0"));
        QCOMPARE(p.variableValue(d1, byName["Nascita"].id), QString("1990-03-05"));
        QCOMPARE(p.variableValue(d1, existing), QString("3"));
        QCOMPARE(p.variableValue(d2, existing), QString()); // "abc" skipped

        // One undo step removes the whole import.
        QVERIFY(p.undo());
        QCOMPARE(p.variables().size(), 1);
    }

    void variablesCsvRoundTripKeepsTypes()
    {
        Project a;
        QVERIFY(a.create(path("rt-a.tsra")));
        const qint64 ga = a.groups().first().id;
        const qint64 da1 = a.addDocument(ga, "Uno", "x");
        const qint64 da2 = a.addDocument(ga, "Due", "y");
        const qint64 vi = a.addVariable("Età", VariableType::Integer);
        const qint64 vd = a.addVariable("Peso", VariableType::Decimal);
        const qint64 vb = a.addVariable("Consenso", VariableType::Boolean);
        const qint64 vt = a.addVariable("Nascita", VariableType::Date);
        a.setVariableValue(da1, vi, "34");
        a.setVariableValue(da2, vi, "41");
        a.setVariableValue(da1, vd, "70.5");
        a.setVariableValue(da2, vd, "82");
        a.setVariableValue(da1, vb, "1");
        a.setVariableValue(da2, vb, "0");
        a.setVariableValue(da1, vt, "1990-03-05");
        a.setVariableValue(da2, vt, "1983-11-02");
        QString err;
        QVERIFY(Exporters::writeCsv(path("rt.csv"), Exporters::variablesTable(a), &err));

        Project b;
        QVERIFY(b.create(path("rt-b.tsra")));
        const qint64 gb = b.groups().first().id;
        const qint64 db1 = b.addDocument(gb, "Uno", "x");
        b.addDocument(gb, "Due", "y");
        QString report;
        QVERIFY2(Exporters::importVariablesCsv(b, path("rt.csv"), &report, &err), qPrintable(err));
        QHash<QString, Variable> byName;
        for (const Variable &v : b.variables())
            byName.insert(v.name, v);
        QCOMPARE(byName["Età"].type, VariableType::Integer);
        QCOMPARE(byName["Peso"].type, VariableType::Decimal);
        QCOMPARE(byName["Consenso"].type, VariableType::Boolean);
        QCOMPARE(byName["Nascita"].type, VariableType::Date);
        QCOMPARE(b.variableValue(db1, byName["Peso"].id), QString("70.5"));
        QCOMPARE(b.variableValue(db1, byName["Nascita"].id), QString("1990-03-05"));
    }

    void variableFilterMatches()
    {
        using VF::Op;
        using VT = VariableType;
        // Text: case-insensitive, empty satisfies only "is empty".
        QVERIFY(VF::matches(VT::Text, Op::Equals, "Femmina", "femmina"));
        QVERIFY(!VF::matches(VT::Text, Op::NotEquals, "Femmina", "femmina"));
        QVERIFY(VF::matches(VT::Text, Op::NotEquals, "Maschio", "femmina"));
        QVERIFY(VF::matches(VT::Text, Op::Contains, "Roma centro", "CENTRO"));
        QVERIFY(VF::matches(VT::Text, Op::IsEmpty, "", ""));
        QVERIFY(!VF::matches(VT::Text, Op::IsEmpty, "x", ""));
        QVERIFY(!VF::matches(VT::Text, Op::Equals, "", ""));
        QVERIFY(!VF::matches(VT::Text, Op::NotEquals, "", "x"));
        // Integer: numeric, not alphabetical (9 < 10); invalid values satisfy nothing.
        QVERIFY(VF::matches(VT::Integer, Op::Less, "9", "10"));
        QVERIFY(VF::matches(VT::Integer, Op::LessEq, "10", "10"));
        QVERIFY(VF::matches(VT::Integer, Op::Greater, "40", "39"));
        QVERIFY(VF::matches(VT::Integer, Op::GreaterEq, "40", "40"));
        QVERIFY(VF::matches(VT::Integer, Op::Equals, "-3", "-3"));
        QVERIFY(VF::matches(VT::Integer, Op::NotEquals, "-3", "3"));
        QVERIFY(!VF::matches(VT::Integer, Op::Greater, "abc", "1"));
        QVERIFY(!VF::matches(VT::Integer, Op::NotEquals, "abc", "1"));
        QVERIFY(!VF::matches(VT::Integer, Op::Greater, "5", "abc"));
        QVERIFY(!VF::matches(VT::Integer, Op::Greater, "", "1"));
        QVERIFY(!VF::matches(VT::Integer, Op::Greater, "5", ""));
        // Decimal accepts comma or point in the condition value.
        QVERIFY(VF::matches(VT::Decimal, Op::Greater, "70.5", "70,25"));
        QVERIFY(VF::matches(VT::Decimal, Op::Equals, "70.5", "70,5"));
        QVERIFY(!VF::matches(VT::Decimal, Op::Less, "70.5", "70.5"));
        // Boolean.
        QVERIFY(VF::matches(VT::Boolean, Op::IsTrue, "1", ""));
        QVERIFY(!VF::matches(VT::Boolean, Op::IsTrue, "0", ""));
        QVERIFY(VF::matches(VT::Boolean, Op::IsFalse, "0", ""));
        QVERIFY(!VF::matches(VT::Boolean, Op::IsFalse, "", ""));
        QVERIFY(VF::matches(VT::Boolean, Op::IsEmpty, "", ""));
        // Date: ISO storage, any accepted format in the condition; Between is inclusive and order-free.
        QVERIFY(VF::matches(VT::Date, Op::Before, "1990-03-05", "01/01/2000"));
        QVERIFY(!VF::matches(VT::Date, Op::After, "1990-03-05", "2000-01-01"));
        QVERIFY(VF::matches(VT::Date, Op::After, "2001-02-03", "2000-01-01"));
        QVERIFY(VF::matches(VT::Date, Op::Equals, "2000-01-01", "01/01/2000"));
        QVERIFY(VF::matches(VT::Date, Op::Between, "2000-01-01", "2000-01-01", "2000-12-31"));
        QVERIFY(VF::matches(VT::Date, Op::Between, "2000-12-31", "2000-12-31", "2000-01-01"));
        QVERIFY(!VF::matches(VT::Date, Op::Between, "2001-01-01", "2000-01-01", "2000-12-31"));
        QVERIFY(!VF::matches(VT::Date, Op::Between, "2000-06-01", "2000-01-01", "non-una-data"));
        QVERIFY(!VF::matches(VT::Date, Op::Before, "non-una-data", "2000-01-01"));
        // Operators offered per type.
        QVERIFY(!VF::operatorsFor(VT::Text).contains(Op::Less));
        QVERIFY(VF::operatorsFor(VT::Date).contains(Op::Between));
        QVERIFY(VF::operatorsFor(VT::Boolean).contains(Op::IsTrue));
    }

    void variableFilterDocuments()
    {
        using VF::Op;
        Project p;
        QVERIFY(p.create(path("vf.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d1 = p.addDocument(g, "D1", "a");
        const qint64 d2 = p.addDocument(g, "D2", "b");
        const qint64 d3 = p.addDocument(g, "D3", "c");
        const qint64 d4 = p.addDocument(g, "D4", "d");
        const qint64 sex = p.addVariable("Genere", VariableType::Text);
        const qint64 age = p.addVariable("Età", VariableType::Integer);
        p.setVariableValue(d1, sex, "F");
        p.setVariableValue(d1, age, "45");
        p.setVariableValue(d2, sex, "F");
        p.setVariableValue(d2, age, "30");
        p.setVariableValue(d3, sex, "M");
        p.setVariableValue(d3, age, "50");
        // d4 has no values at all.

        const VF::Condition female{sex, Op::Equals, "F", {}};
        const VF::Condition over40{age, Op::GreaterEq, "40", {}};
        QCOMPARE(VF::matchingDocuments(p, {female}, false), (QList<qint64>{d1, d2}));
        QCOMPARE(VF::matchingDocuments(p, {female, over40}, false), (QList<qint64>{d1}));
        QCOMPARE(VF::matchingDocuments(p, {female, over40}, true), (QList<qint64>{d1, d2, d3}));
        QCOMPARE(VF::matchingDocuments(p, {{sex, Op::IsEmpty, {}, {}}}, false), (QList<qint64>{d4}));
        // No conditions: no restriction.
        QCOMPARE(VF::matchingDocuments(p, {}, false), (QList<qint64>{d1, d2, d3, d4}));
        // A condition on a variable that does not exist never matches.
        QCOMPARE(VF::matchingDocuments(p, {{9999, Op::IsEmpty, {}, {}}}, false), QList<qint64>());
        QCOMPARE(VF::matchingDocuments(p, {{9999, Op::IsEmpty, {}, {}}, female}, true), (QList<qint64>{d1, d2}));
        // An invalid value in the condition matches nothing.
        QCOMPARE(VF::matchingDocuments(p, {{age, Op::Greater, "abc", {}}}, false), QList<qint64>());

        // The last filter is kept in the project file and is not an undo step.
        const QString lastStep = p.undoLabel();
        VF::save(p, {female, {age, Op::Between, "1", "2"}}, true);
        QCOMPARE(p.undoLabel(), lastStep);
        p.close();
        QVERIFY(p.open(path("vf.tsra")));
        const VF::Saved saved = VF::loadSaved(p);
        QVERIFY(saved.any);
        QCOMPARE(saved.conditions.size(), 2);
        QVERIFY(saved.conditions[0] == female);
        QCOMPARE(saved.conditions[1].op, Op::Between);
        QCOMPARE(saved.conditions[1].value2, QString("2"));
    }

    void qdpxRoundTrip()
    {
        Project p;
        QVERIFY(p.create(path("src.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 g2 = p.addGroup("Focus group");
        const QString text = QString::fromUtf8("Emoji 😀 qui. Fine del testo.");
        const qint64 d1 = p.addDocument(g, "Doc uno", text);
        const qint64 d2 = p.addDocument(g2, "Doc due", "Altro testo");
        const qint64 c1 = p.addCode(0, "Tema", QColor("#112233"));
        const qint64 c2 = p.addCode(c1, "Sotto", QColor("#445566"));
        p.setCodeDescription(c1, "descrizione");
        const int start = text.indexOf("qui");
        const qint64 s = p.addSegment(d1, c2, start, start + 3, 0, "commento");
        QVERIFY(s > 0);
        QVERIFY(p.addSegment(d2, c1, 0, 5) > 0);
        Memo m;
        m.targetType = MemoTarget::Segment;
        m.targetId = s;
        m.title = "Memo segmento";
        m.body = "corpo";
        p.saveMemo(m);
        Memo pm;
        pm.title = "Progetto";
        pm.body = "note generali";
        p.saveMemo(pm);
        const qint64 v = p.addVariable("Sesso", VariableType::Text);
        p.setVariableValue(d1, v, "F");
        const qint64 b = p.addVariable("Consenso", VariableType::Boolean);
        p.setVariableValue(d2, b, "1");

        QString err, report;
        QVERIFY2(Qdpx::exportProject(p, path("x.qdpx"), &err), qPrintable(err));

        Project q;
        QVERIFY(q.create(path("dst.tsra")));
        QVERIFY2(Qdpx::importProject(q, path("x.qdpx"), &report, &err), qPrintable(err));
        QCOMPARE(q.documents().size(), 2);
        QCOMPARE(q.groups().size(), 2);
        QCOMPARE(q.codes().size(), 2);
        QCOMPARE(q.allSegments().size(), 2);
        QCOMPARE(q.memos().size(), 2);
        const qint64 qc1 = q.findCode(0, "Tema");
        QVERIFY(qc1);
        QCOMPARE(q.code(qc1).description, QString("descrizione"));
        QCOMPARE(q.code(qc1).color.name(), QString("#112233"));
        const qint64 qc2 = q.findCode(qc1, "Sotto");
        QVERIFY(qc2);

        qint64 qd1 = 0;
        for (const Document &d : q.documents())
            if (d.name == "Doc uno")
                qd1 = d.id;
        QCOMPARE(q.documentText(qd1), text);
        const QList<Segment> segs = q.segmentsForDocument(qd1);
        QCOMPARE(segs.size(), 1);
        QCOMPARE(segs.first().start, start); // surrogate pair handled
        QCOMPARE(segs.first().end, start + 3);
        QCOMPARE(segs.first().codeId, qc2);
        QCOMPARE(segs.first().comment, QString("commento"));
        QCOMPARE(q.memoFor(MemoTarget::Segment, segs.first().id).body, QString("corpo"));
        QCOMPARE(q.memoFor(MemoTarget::Project, 0).body, QString("note generali"));

        QCOMPARE(q.variables().size(), 2);
        const auto values = q.allVariableValues();
        bool foundBool = false;
        for (const Variable &var : q.variables()) {
            if (var.name == "Consenso") {
                QCOMPARE(var.type, VariableType::Boolean);
                for (const Document &d : q.documents())
                    if (d.name == "Doc due") {
                        QCOMPARE(values.value(d.id).value(var.id), QString("1"));
                        foundBool = true;
                    }
            }
        }
        QVERIFY(foundBool);
    }

    void qdpxDocumentSets()
    {
        Project p;
        QVERIFY(p.create(path("sets_src.tsra")));
        const qint64 g1 = p.groups().first().id;
        const qint64 g2 = p.addGroup("Focus group");
        const qint64 a = p.addDocument(g1, "A", "testo a");
        const qint64 b = p.addDocument(g2, "B", "testo b");
        const qint64 s = p.addDocumentSet("Trasversale", {a, b});
        QVERIFY(p.addDocumentSet("Vuoto") > 0);
        Memo gm;
        gm.targetType = MemoTarget::Group;
        gm.targetId = g2;
        gm.body = "memo del gruppo";
        p.saveMemo(gm);
        Memo sm;
        sm.targetType = MemoTarget::DocumentSet;
        sm.targetId = s;
        sm.body = "memo del set";
        p.saveMemo(sm);

        QString err, report;
        QVERIFY2(Qdpx::exportProject(p, path("sets.qdpx"), &err), qPrintable(err));
        Project q;
        QVERIFY(q.create(path("sets_dst.tsra")));
        QVERIFY2(Qdpx::importProject(q, path("sets.qdpx"), &report, &err), qPrintable(err));

        // Groups come back as groups, sets as sets, and each memo exactly once.
        QCOMPARE(q.groups().size(), 2);
        QCOMPARE(q.documentSets().size(), 2);
        const DocumentSet qs = q.documentSets().first();
        QCOMPARE(qs.name, QString("Trasversale"));
        QCOMPARE(qs.uuid, p.documentSets().first().uuid);
        QCOMPARE(q.documentSetMembers(qs.id).size(), 2);
        QVERIFY(q.documentSetMembers(q.documentSets().last().id).isEmpty());
        QCOMPARE(q.memoFor(MemoTarget::DocumentSet, qs.id).body, QString("memo del set"));
        QCOMPARE(q.memoFor(MemoTarget::Group, q.groups().last().id).body, QString("memo del gruppo"));
        QCOMPARE(q.memos().size(), 2);
    }

    void memoLinkedToSeveralElements()
    {
        Project p;
        QVERIFY(p.create(path("memolinks.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d = p.addDocument(g, "Doc", "testo del documento");
        const qint64 a = p.addCode(0, "A", Qt::red);
        const qint64 b = p.addCode(0, "B", Qt::green);
        const qint64 c = p.addCode(0, "C", Qt::blue);
        Memo m;
        m.targetType = MemoTarget::Code;
        m.targetId = a;
        m.title = "Teoria";
        const qint64 memo = p.saveMemo(m);
        QVERIFY(memo > 0);
        QVERIFY(p.addMemoLink(memo, MemoTarget::Code, b));
        QVERIFY(p.addMemoLink(memo, MemoTarget::Code, c));
        QVERIFY(p.addMemoLink(memo, MemoTarget::Code, c)); // already linked: no duplicate
        QVERIFY(!p.addMemoLink(memo, MemoTarget::Code, 9999)); // missing element
        QVERIFY(!p.addMemoLink(memo, MemoTarget::Project, 0));
        const QList<MemoLink> abc = {{MemoTarget::Code, a}, {MemoTarget::Code, b}, {MemoTarget::Code, c}};
        QCOMPARE(p.memoLinks(memo), abc);

        // The memo shows up on all three codes.
        const QHash<qint64, qint64> byCode = p.memoIdsFor(MemoTarget::Code);
        QCOMPARE(byCode.size(), 3);
        for (qint64 code : {a, b, c}) {
            QCOMPARE(byCode.value(code), memo);
            QCOMPARE(p.memoFor(MemoTarget::Code, code).id, memo);
        }
        QVERIFY(p.memoIdsFor(MemoTarget::Document).isEmpty());

        // Deleting one of the codes, even the primary target, keeps the memo on the others.
        QVERIFY(p.deleteCode(a));
        QCOMPARE(p.memos().size(), 1);
        QCOMPARE(p.memo(memo).targetType, MemoTarget::Code);
        QCOMPARE(p.memo(memo).targetId, b);
        QCOMPARE(p.memoLinks(memo), (QList<MemoLink>{{MemoTarget::Code, b}, {MemoTarget::Code, c}}));
        QCOMPARE(p.memoIdsFor(MemoTarget::Code).size(), 2);
        QVERIFY(p.undo());
        QCOMPARE(p.memoLinks(memo), abc);
        QCOMPARE(p.memo(memo).targetId, a);

        // A memo goes away only with its last element.
        QVERIFY(p.deleteCode(b));
        QVERIFY(p.deleteCode(c));
        QCOMPARE(p.memoLinks(memo), (QList<MemoLink>{{MemoTarget::Code, a}}));
        QVERIFY(p.deleteCode(a));
        QVERIFY(p.memos().isEmpty());
        QVERIFY(p.undo());
        QVERIFY(p.undo());
        QVERIFY(p.undo());
        QCOMPARE(p.memoLinks(memo), abc);

        // Replacing the links is one undo step; without the primary target the first link takes over.
        QVERIFY(p.setMemoLinks(memo, {{MemoTarget::Document, d}, {MemoTarget::Code, c}, {MemoTarget::Code, 9999}}));
        QCOMPARE(p.undoLabel(), QString("Collega memo"));
        QCOMPARE(p.memo(memo).targetType, MemoTarget::Document);
        QCOMPARE(p.memoLinks(memo), (QList<MemoLink>{{MemoTarget::Document, d}, {MemoTarget::Code, c}}));
        QVERIFY(p.undo());
        QCOMPARE(p.memoLinks(memo), abc);
        // With no elements left the memo becomes free, and survives.
        QVERIFY(p.setMemoLinks(memo, {}));
        QCOMPARE(p.memo(memo).targetType, MemoTarget::Free);
        QVERIFY(p.deleteDocument(d));
        QCOMPARE(p.memos().size(), 1);

        // A free memo keeps no primary target: its links are only links.
        Memo freeMemo;
        freeMemo.targetType = MemoTarget::Free;
        freeMemo.body = "idea";
        const qint64 freeId = p.saveMemo(freeMemo);
        QVERIFY(p.addMemoLink(freeId, MemoTarget::Code, b));
        QCOMPARE(p.memo(freeId).targetType, MemoTarget::Free);
        QCOMPARE(p.memoLinks(freeId), (QList<MemoLink>{{MemoTarget::Code, b}}));
        QCOMPARE(p.memoFor(MemoTarget::Code, b).id, freeId);
        QVERIFY(p.deleteCode(b));
        QVERIFY(p.memoLinks(freeId).isEmpty());
        QCOMPARE(p.memo(freeId).body, QString("idea"));

        // Deleting a memo drops its links.
        QVERIFY(p.addMemoLink(freeId, MemoTarget::Code, c));
        QVERIFY(p.deleteMemo(freeId));
        QVERIFY(!p.memoIdsFor(MemoTarget::Code).contains(c));
    }

    void qdpxMemoWithSeveralNoteRefs()
    {
        Project p;
        QVERIFY(p.create(path("noterefs_src.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d = p.addDocument(g, "Doc", "un testo da codificare");
        const qint64 a = p.addCode(0, "A", Qt::red);
        const qint64 b = p.addCode(0, "B", Qt::green);
        const qint64 c = p.addCode(0, "C", Qt::blue);
        const qint64 s = p.addSegment(d, a, 0, 8);
        Memo m;
        m.targetType = MemoTarget::Code;
        m.targetId = a;
        m.title = "Teorico";
        m.body = "collega tutto";
        const qint64 memo = p.saveMemo(m);
        QVERIFY(p.setMemoLinks(memo, {{MemoTarget::Code, a}, {MemoTarget::Code, b}, {MemoTarget::Segment, s},
                                      {MemoTarget::Document, d}}));
        Memo freeMemo;
        freeMemo.targetType = MemoTarget::Free;
        freeMemo.title = "Libero";
        p.saveMemo(freeMemo);
        Memo pm;
        pm.title = "Progetto";
        pm.body = "domanda";
        p.saveMemo(pm);
        Q_UNUSED(c);

        QString err, report;
        QVERIFY2(Qdpx::exportProject(p, path("noterefs.qdpx"), &err), qPrintable(err));
        Project q;
        QVERIFY(q.create(path("noterefs_dst.tsra")));
        QVERIFY2(Qdpx::importProject(q, path("noterefs.qdpx"), &report, &err), qPrintable(err));

        // One memo per note, linked to every object that refers to it.
        QCOMPARE(q.memos().size(), 3);
        const qint64 qa = q.findCode(0, "A"), qb = q.findCode(0, "B"), qc = q.findCode(0, "C");
        const qint64 qd = q.documents().first().id;
        const qint64 qs = q.allSegments().first().id;
        Memo theory;
        for (const Memo &x : q.memos())
            if (x.title == "Teorico")
                theory = x;
        QVERIFY(theory.id);
        QCOMPARE(theory.uuid, p.memo(memo).uuid);
        const QList<MemoLink> links = q.memoLinks(theory.id);
        QCOMPARE(links.size(), 4);
        for (const MemoLink &l : QList<MemoLink>{{MemoTarget::Code, qa}, {MemoTarget::Code, qb},
                                                 {MemoTarget::Segment, qs}, {MemoTarget::Document, qd}})
            QVERIFY(links.contains(l));
        QVERIFY(!q.memoIdsFor(MemoTarget::Code).contains(qc));
        QCOMPARE(q.memoFor(MemoTarget::Project, 0).body, QString("domanda"));
        for (const Memo &x : q.memos())
            if (x.title == "Libero")
                QCOMPARE(x.targetType, MemoTarget::Free);

        // Exported again, the memo is still a single note with four references.
        QVERIFY2(Qdpx::exportProject(q, path("noterefs2.qdpx"), &err), qPrintable(err));
        Project r;
        QVERIFY(r.create(path("noterefs_dst2.tsra")));
        QVERIFY2(Qdpx::importProject(r, path("noterefs2.qdpx"), &report, &err), qPrintable(err));
        QCOMPARE(r.memos().size(), 3);
        QCOMPARE(r.memoLinks(r.memoFor(MemoTarget::Code, r.findCode(0, "B")).id).size(), 4);
    }

    void qdpxLinks()
    {
        Project p;
        QVERIFY(p.create(path("links_src.tsra")));
        const qint64 g = p.groups().first().id;
        const QString text = QString::fromUtf8("😀 prima frase. Seconda frase.");
        const qint64 a = p.addDocument(g, "A", text);
        const qint64 b = p.addDocument(g, "B", "altro documento");
        const int s1 = int(text.indexOf("prima")), s2 = int(text.indexOf("Seconda"));
        const qint64 across = p.addLink(a, s1, s1 + 5, b, 0, 5, "contraddice");
        const qint64 inside = p.addLink(a, s2, s2 + 7, a, s1, s1 + 5);
        QVERIFY(across && inside);
        // A coded passage can also be a link end.
        QVERIFY(p.addSegment(a, p.addCode(0, "C", Qt::red), s1, s1 + 5) > 0);

        QString err, report;
        QVERIFY2(Qdpx::exportProject(p, path("links.qdpx"), &err), qPrintable(err));
        Project q;
        QVERIFY(q.create(path("links_dst.tsra")));
        QVERIFY2(Qdpx::importProject(q, path("links.qdpx"), &report, &err), qPrintable(err));
        QVERIFY2(report.contains("Collegamenti tra documenti: 2"), qPrintable(report));
        QCOMPARE(q.allSegments().size(), 1); // link ends are not codings

        QCOMPARE(q.links().size(), 2);
        qint64 qa = 0, qb = 0;
        for (const Document &d : q.documents())
            (d.name == "A" ? qa : qb) = d.id;
        for (const DocumentLink &l : q.links()) {
            const DocumentLink orig = p.link(l.uuid == p.link(across).uuid ? across : inside);
            QCOMPARE(l.uuid, orig.uuid);
            QCOMPARE(l.comment, orig.comment);
            QCOMPARE(l.fromDocumentId, orig.fromDocumentId == a ? qa : qb);
            QCOMPARE(l.toDocumentId, orig.toDocumentId == a ? qa : qb);
            QCOMPARE(l.fromStart, orig.fromStart); // after the emoji: code points converted back
            QCOMPARE(l.fromEnd, orig.fromEnd);
            QCOMPARE(l.toStart, orig.toStart);
            QCOMPARE(l.toEnd, orig.toEnd);
        }

        // Links between other kinds of objects (here a code) are reported and skipped.
        const QString xml = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<Project xmlns=\"urn:QDA-XML:project:1.0\" name=\"Altro\">"
            "<CodeBook><Codes><Code guid=\"44444444-4444-4444-8444-444444444444\" name=\"K\" isCodable=\"true\"/>"
            "</Codes></CodeBook><Sources>"
            "<TextSource guid=\"11111111-1111-4111-8111-111111111111\" name=\"Uno\">"
            "<PlainTextContent>uno due</PlainTextContent>"
            "<PlainTextSelection guid=\"55555555-5555-4555-8555-555555555555\" startPosition=\"0\" endPosition=\"3\"/>"
            "<PlainTextSelection guid=\"66666666-6666-4666-8666-666666666666\" startPosition=\"4\" endPosition=\"7\"/>"
            "</TextSource></Sources><Links>"
            "<Link guid=\"77777777-7777-4777-8777-777777777777\" name=\"ok\" direction=\"OneWay\""
            " originGUID=\"55555555-5555-4555-8555-555555555555\" targetGUID=\"66666666-6666-4666-8666-666666666666\"/>"
            "<Link guid=\"88888888-8888-4888-8888-888888888888\" direction=\"Associative\""
            " originGUID=\"44444444-4444-4444-8444-444444444444\" targetGUID=\"66666666-6666-4666-8666-666666666666\"/>"
            "</Links></Project>");
        {
            QZipWriter zip(path("foreign_links.qdpx"));
            zip.addFile("project.qde", xml.toUtf8());
            zip.close();
        }
        Project f;
        QVERIFY(f.create(path("foreign_links.tsra")));
        QVERIFY2(Qdpx::importProject(f, path("foreign_links.qdpx"), &report, &err), qPrintable(err));
        QCOMPARE(f.links().size(), 1);
        QCOMPARE(f.links().first().comment, QString("ok"));
        QCOMPARE(f.links().first().toStart, 4);
        QVERIFY2(report.contains("1 collegamenti"), qPrintable(report));
    }

    void qdpxForeignSets()
    {
        auto writeQdpx = [this](const QString &name, const QString &origin) {
            const QString xml = QStringLiteral(
                "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                "<Project xmlns=\"urn:QDA-XML:project:1.0\" name=\"Altro\" origin=\"%1\">"
                "<Sources>"
                "<TextSource guid=\"11111111-1111-4111-8111-111111111111\" name=\"Uno\">"
                "<PlainTextContent>uno</PlainTextContent></TextSource>"
                "<TextSource guid=\"22222222-2222-4222-8222-222222222222\" name=\"Due\">"
                "<PlainTextContent>due</PlainTextContent></TextSource>"
                "</Sources><Sets>"
                "<Set guid=\"33333333-3333-4333-8333-333333333333\" name=\"Donne\">"
                "<Description>solo donne</Description>"
                "<MemberSource targetGUID=\"22222222-2222-4222-8222-222222222222\"/></Set>"
                "</Sets></Project>").arg(origin);
            QZipWriter zip(path(name));
            zip.addFile("project.qde", xml.toUtf8());
            zip.close();
        };
        QString err, report;

        // Sets from other programs become document sets; the documents stay in one group.
        writeQdpx("foreign.qdpx", "Altro software 1.0");
        Project p;
        QVERIFY(p.create(path("foreign.tsra")));
        QVERIFY2(Qdpx::importProject(p, path("foreign.qdpx"), &report, &err), qPrintable(err));
        QCOMPARE(p.groups().size(), 1);
        QCOMPARE(p.documentsInGroup(p.groups().first().id).size(), 2);
        QCOMPARE(p.documentSets().size(), 1);
        const DocumentSet s = p.documentSets().first();
        QCOMPARE(s.name, QString("Donne"));
        QCOMPARE(p.documentSetMembers(s.id).size(), 1);
        QCOMPARE(p.document(p.documentSetMembers(s.id).first()).name, QString("Due"));
        QCOMPARE(p.memoFor(MemoTarget::DocumentSet, s.id).body, QString("solo donne"));

        // Tessera 0.1.0 exported only groups, so its sets are still read as groups.
        writeQdpx("legacy.qdpx", "Tessera 0.1.0");
        Project q;
        QVERIFY(q.create(path("legacy.tsra")));
        QVERIFY2(Qdpx::importProject(q, path("legacy.qdpx"), &report, &err), qPrintable(err));
        QVERIFY(q.documentSets().isEmpty());
        QCOMPARE(q.groups().size(), 2);
        QCOMPARE(q.groups().last().name, QString("Donne"));
    }

    void qdpxSourcePaths()
    {
        // Network paths are refused without touching the network; local files are read.
        QVERIFY(!Qdpx::isReadableSourcePath("//host/share/a.txt", m_dir.path()));
        QVERIFY(!Qdpx::isReadableSourcePath("\\\\host\\share\\a.txt", m_dir.path()));
        QVERIFY(!Qdpx::isReadableSourcePath("//?/UNC/host/share/a.txt", m_dir.path()));
        QVERIFY(!Qdpx::isReadableSourcePath(m_dir.path(), m_dir.path())); // a directory
        writeFile("external.txt", "testo esterno");
        QVERIFY(Qdpx::isReadableSourcePath(path("external.txt"), m_dir.path()));

        const QString xml = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<Project xmlns=\"urn:QDA-XML:project:1.0\" name=\"P\"><Sources>"
            "<TextSource guid=\"11111111-1111-4111-8111-111111111111\" name=\"Locale\" plainTextPath=\"absolute://%1\"/>"
            "<TextSource guid=\"22222222-2222-4222-8222-222222222222\" name=\"Relativo\" plainTextPath=\"relative://external.txt\"/>"
            "</Sources></Project>").arg(path("external.txt"));
        {
            QZipWriter zip(path("paths.qdpx"));
            zip.addFile("project.qde", xml.toUtf8());
        }
        Project p;
        QVERIFY(p.create(path("paths.tsra")));
        QString report, err;
        QVERIFY2(Qdpx::importProject(p, path("paths.qdpx"), &report, &err), qPrintable(err));
        for (const Document &d : p.documents())
            QCOMPARE(p.documentText(d.id), QString("testo esterno"));
    }

    void qdpxMalformedInput()
    {
        // Repeated GUIDs, a negative position and codes nested deeper than the import limit.
        QString nested;
        for (int i = 0; i < 300; ++i)
            nested += QStringLiteral("<Code guid=\"%1\" name=\"N%2\">").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)).arg(i);
        for (int i = 0; i < 300; ++i)
            nested += QStringLiteral("</Code>");
        const QString xml = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<Project xmlns=\"urn:QDA-XML:project:1.0\" name=\"P\"><CodeBook><Codes>"
            "<Code guid=\"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\" name=\"A\"/>"
            "<Code guid=\"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\" name=\"B\"/>%1"
            "</Codes></CodeBook><Sources>"
            "<TextSource guid=\"11111111-1111-4111-8111-111111111111\" name=\"Uno\"><PlainTextContent>uno due</PlainTextContent>"
            "<PlainTextSelection guid=\"33333333-3333-4333-8333-333333333333\" startPosition=\"-2\" endPosition=\"3\">"
            "<Coding guid=\"44444444-4444-4444-8444-444444444444\"><CodeRef targetGUID=\"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa\"/></Coding>"
            "</PlainTextSelection></TextSource>"
            "<TextSource guid=\"11111111-1111-4111-8111-111111111111\" name=\"Due\"><PlainTextContent>tre</PlainTextContent></TextSource>"
            "</Sources></Project>").arg(nested);
        {
            QZipWriter zip(path("malformed.qdpx"));
            zip.addFile("project.qde", xml.toUtf8());
        }
        Project p;
        QVERIFY(p.create(path("malformed.tsra")));
        QString report, err;
        QVERIFY2(Qdpx::importProject(p, path("malformed.qdpx"), &report, &err), qPrintable(err));
        QCOMPARE(p.codes().size(), 2 + 200);
        QSet<QString> codeGuids, docGuids;
        for (const Code &c : p.codes())
            codeGuids.insert(c.uuid);
        for (const Document &d : p.documents())
            docGuids.insert(d.uuid);
        QCOMPARE(codeGuids.size(), p.codes().size());
        QCOMPARE(p.documents().size(), 2);
        QCOMPARE(docGuids.size(), 2);
        QVERIFY(p.allSegments().isEmpty());
        QVERIFY(report.contains("1 selezioni fuori dal testo"));
    }

    void qdpxExportReplacesFile()
    {
        Project p;
        QVERIFY(p.create(path("keep.tsra")));
        writeFile("keep.qdpx", "vecchio");
        QString err;
        // A directory with the same name cannot be written: the export fails with an error.
        QVERIFY(QDir(m_dir.path()).mkpath("dir.qdpx"));
        QVERIFY(!Qdpx::exportProject(p, path("dir.qdpx"), &err));
        QVERIFY(Qdpx::exportProject(p, path("keep.qdpx"), &err));
        QVERIFY(QFileInfo(path("keep.qdpx")).size() > 7);
    }

    void odtSpaceCountIsBounded()
    {
        const QByteArray xml =
            "<?xml version=\"1.0\"?><office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
            " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\"><office:body><office:text>"
            "<text:p>a<text:s text:c=\"3\"/>b<text:s text:c=\"2000000000\"/>c</text:p>"
            "</office:text></office:body></office:document-content>";
        {
            QZipWriter zip(path("spaces.odt"));
            zip.addFile("content.xml", xml);
        }
        const auto r = Importers::importFile(path("spaces.odt"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.text.startsWith("a   b "));
        QCOMPARE(r.text.size(), 3 + 3 + 1000);
    }

    void undoWithUnusualColumnNames()
    {
        // A file whose tables have column names with quotes must not break (or inject into) the undo triggers.
        {
            Project p;
            QVERIFY(p.create(path("columns.tsra")));
        }
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("alter"));
            db.setDatabaseName(path("columns.tsra"));
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("ALTER TABLE codes ADD COLUMN \"x\"\"y'z\" TEXT DEFAULT 'v'"));
        }
        QSqlDatabase::removeDatabase(QStringLiteral("alter"));
        Project p;
        QVERIFY2(p.open(path("columns.tsra")), qPrintable(p.lastError()));
        const qint64 c = p.addCode(0, "Codice", Qt::red);
        QVERIFY(p.renameCode(c, "Nuovo"));
        QVERIFY(p.deleteCode(c));
        QVERIFY(p.undo());
        QCOMPARE(p.code(c).name, QString("Nuovo"));
        QVERIFY(p.undo());
        QCOMPARE(p.code(c).name, QString("Codice"));
    }

    void segmentsStayInsideText()
    {
        Project p;
        QVERIFY(p.create(path("bounds.tsra")));
        const qint64 d = p.addDocument(p.groups().first().id, "D", "abc");
        const qint64 c = p.addCode(0, "C", Qt::red);
        QVERIFY(p.addSegment(d, c, 0, 3) > 0);
        QCOMPARE(p.addSegment(d, c, 1, 4), 0);
        QCOMPARE(p.addSegment(d, c, -1, 2), 0);
    }

    void csvFormulaCells()
    {
        const Exporters::Table t{{"=HYPERLINK(\"x\")", "-3", "+4,5", "@SUM(A1)", "-", "testo"}};
        QString err;
        QVERIFY(Exporters::writeCsv(path("formula.csv"), t, &err));
        QFile f(path("formula.csv"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString written = QString::fromUtf8(f.readAll());
        QVERIFY(written.contains("\"'=HYPERLINK(\"\"x\"\")\""));
        QVERIFY(written.contains(";-3;"));
        QVERIFY(written.contains("'@SUM(A1)"));
        QVERIFY(written.contains(";'-;"));
        QCOMPARE(Exporters::readCsv(path("formula.csv"), &err), t);
    }

    void placeholdersInNames()
    {
        Project p;
        QVERIFY(p.create(path("percent.tsra")));
        const qint64 d = p.addDocument(p.groups().first().id, "Intervista %5", "testo del documento");
        const qint64 c = p.addCode(0, "Tema %2", Qt::red);
        QVERIFY(p.addSegment(d, c, 0, 5) > 0);
        QString err;
        QVERIFY(Exporters::exportSegmentsHtml(p, p.allSegments(), path("percent.html"), &err));
        QFile f(path("percent.html"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString html = QString::fromUtf8(f.readAll());
        QVERIFY(html.contains("Intervista %5"));
        QVERIFY(html.contains("Tema %2"));

        p.addVariable("Segmenti: Tema %2", VariableType::Integer); // forces the " (2)" suffix
        const QList<qint64> vars = Stats::codesToVariables(p, {c}, Stats::Measure::Segments);
        QCOMPARE(vars.size(), 1);
        QCOMPARE(p.variables().last().name, QString("Segmenti: Tema %2 (2)"));
    }

    // ---------------------------------------------------------------- dictionaries (#19)

    void dictionaryTokenizeAndWildcards()
    {
        const QString text = "L'azienda, smart-working!";
        QStringList words;
        for (const Dictionaries::Token &t : Dictionaries::tokenize(text))
            words << text.mid(t.start, t.end - t.start);
        QCOMPARE(words, QStringList({"L", "azienda", "smart", "working"}));
        QCOMPARE(Dictionaries::tokenize(QString::fromUtf8("ciao \xF0\x9F\x98\x80 mondo")).size(), 2); // emoji: no word
        QCOMPARE(Dictionaries::tokenize(QString::fromUtf8("\xF0\x9D\x90\x80" "bc")).size(), 1); // letter outside the BMP

        auto found = [](const QString &text, const QStringList &patterns, bool caseSensitive = false) {
            QStringList out;
            for (const Dictionaries::Match &m : Dictionaries::findMatches(text, patterns, caseSensitive))
                out << text.mid(m.start, m.end - m.start);
            return out;
        };
        QCOMPARE(found("Il lavoro e i lavoratori collaborano. LAVORARE stanca; lavor.", {"lavor*"}),
                 QStringList({"lavoro", "lavoratori", "LAVORARE", "lavor"}));
        QCOMPARE(found("Veramente e solamente mente, non mentre.", {"*mente"}),
                 QStringList({"Veramente", "solamente", "mente"}));
        QCOMPARE(found("lavoro, lo, lavori", {"l*o"}), QStringList({"lavoro", "lo"}));
        QCOMPARE(found("Roma e roma", {"Roma"}, true), QStringList({"Roma"}));
        QCOMPARE(found("Roma e roma", {"Roma"}), QStringList({"Roma", "roma"}));
        // Accents compare in composed form: "perché" written with a combining accent still matches.
        QCOMPARE(found(QString::fromUtf8("perche\xCC\x81 no"), {QString::fromUtf8("perch\xC3\xA9")}).size(), 1);
        QVERIFY(found("testo", {"", "...", "*"}).size() == 1); // empty patterns never match, '*' matches any word
    }

    void dictionaryMultiWordExpressions()
    {
        const QString text = "Lavoro da casa, ma il lavoro\nda  casa stanca. Lavoro da remoto.";
        const QList<Dictionaries::Match> m = Dictionaries::findMatches(text, {"lavoro", "lavoro da casa"});
        QCOMPARE(m.size(), 3);
        // The longest expression wins over the single word, across punctuation and line breaks.
        QCOMPARE(text.mid(m[0].start, m[0].end - m[0].start), QString("Lavoro da casa"));
        QCOMPARE(m[0].pattern, 1);
        QCOMPARE(text.mid(m[1].start, m[1].end - m[1].start), QString("lavoro\nda  casa"));
        QCOMPARE(m[1].pattern, 1);
        QCOMPARE(text.mid(m[2].start, m[2].end - m[2].start), QString("Lavoro"));
        QCOMPARE(m[2].pattern, 0);

        QCOMPARE(Dictionaries::findMatches("Faccio smart-working.", {"smart working"}).size(), 1);
        QCOMPARE(Dictionaries::findMatches("dell'azienda e dell azienda", {"dell'azienda"}).size(), 2);
        const QString t2 = "lavoro da casa, lavoratori da remoto, lavoro in ufficio";
        QCOMPARE(Dictionaries::findMatches(t2, {"lavor* da *"}).size(), 2);
        QVERIFY(Dictionaries::findMatches("lavoro da", {"lavoro da casa"}).isEmpty()); // text ends first
    }

private:
    // Dictionary "Lavoro": Luoghi (casa, ufficio) > Remoto (da remoto, smart working); Tempo (ora*, tempo).
    struct DictionaryFixture {
        qint64 d1 = 0, d2 = 0, dict = 0, luoghi = 0, remoto = 0, tempo = 0;
    };
    DictionaryFixture makeDictionaryProject(Project &p)
    {
        DictionaryFixture f;
        const qint64 g = p.groups().first().id;
        f.d1 = p.addDocument(g, "D1", "Lavoro da casa. In ufficio solo il lunedì. Lavoro da remoto da anni.");
        f.d2 = p.addDocument(g, "D2", "Gli orari sono flessibili: smart working tutto il tempo.");
        f.dict = p.addDictionary("Lavoro");
        f.luoghi = p.addDictCategory(f.dict, 0, "Luoghi");
        f.remoto = p.addDictCategory(f.dict, f.luoghi, "Remoto");
        f.tempo = p.addDictCategory(f.dict, 0, "Tempo");
        p.addDictEntry(f.luoghi, "casa");
        p.addDictEntry(f.luoghi, "ufficio");
        p.addDictEntry(f.remoto, "da remoto");
        p.addDictEntry(f.remoto, "smart working");
        p.addDictEntry(f.tempo, "ora*");
        p.addDictEntry(f.tempo, "tempo");
        return f;
    }

private slots:
    void dictionaryProjectCrudAndUndo()
    {
        Project p;
        QVERIFY(p.create(path("dict.tsra")));
        QSignalSpy spy(&p, &Project::dictionariesChanged);
        const DictionaryFixture f = makeDictionaryProject(p);
        QVERIFY(spy.count() > 0);
        QCOMPARE(p.dictionaries().size(), 1);
        QList<qint64> order;
        for (const DictCategory &c : p.dictCategories(f.dict))
            order << c.id;
        QCOMPARE(order, QList<qint64>({f.luoghi, f.remoto, f.tempo})); // tree order
        QCOMPARE(p.dictCategoryPath(f.remoto), QString("Luoghi > Remoto"));
        QCOMPARE(p.findDictCategory(f.dict, f.luoghi, "Remoto"), f.remoto);
        QCOMPARE(p.dictEntriesOfDictionary(f.dict).size(), 6);

        // Same pattern ignoring case and spacing: the existing entry.
        const qint64 casa = p.dictEntries(f.luoghi).first().id;
        QCOMPARE(p.addDictEntry(f.luoghi, "  CASA "), casa);
        QCOMPARE(p.addDictEntry(f.luoghi, "   "), qint64(0));
        QVERIFY(p.setDictEntryPattern(casa, "casa*"));
        QCOMPARE(p.dictEntries(f.luoghi).first().pattern, QString("casa*"));
        QVERIFY(p.undo());
        QCOMPARE(p.dictEntries(f.luoghi).first().pattern, QString("casa"));

        // A subcategory cannot hang from a category of another dictionary.
        const qint64 other = p.addDictionary("Altro");
        QCOMPARE(p.addDictCategory(other, f.luoghi, "X"), qint64(0));

        // Deleting a dictionary takes its categories and entries; one undo brings everything back.
        QVERIFY(p.deleteDictionary(f.dict));
        QVERIFY(p.dictCategories(f.dict).isEmpty());
        QVERIFY(p.dictEntriesOfDictionary(f.dict).isEmpty());
        QVERIFY(p.undo());
        QCOMPARE(p.dictCategories(f.dict).size(), 3);
        QCOMPARE(p.dictEntriesOfDictionary(f.dict).size(), 6);
        QVERIFY(p.deleteDictCategory(f.luoghi)); // with its subcategory
        QCOMPARE(p.dictCategories(f.dict).size(), 1);
        QCOMPARE(p.dictEntriesOfDictionary(f.dict).size(), 2);
        QVERIFY(p.undo());

        p.close();
        Project q;
        QVERIFY(q.open(path("dict.tsra")));
        QCOMPARE(q.dictionary(f.dict).name, QString("Lavoro"));
        QCOMPARE(q.dictCategory(f.remoto).parentId, f.luoghi);
        QCOMPARE(q.dictEntries(f.tempo).size(), 2);
    }

    void dictionaryTablesAddedByMigration()
    {
        {
            Project old;
            old.setMigrations(Project::builtInMigrations().mid(0, 3)); // schema version 4
            QVERIFY(old.create(path("dict-old.tsra")));
        }
        QCOMPARE(fileVersion(path("dict-old.tsra")), 4);
        Project p;
        QVERIFY(p.open(path("dict-old.tsra")));
        const qint64 d = p.addDictionary("Nuovo");
        QVERIFY(d > 0);
        QVERIFY(p.addDictEntry(p.addDictCategory(d, 0, "C"), "parola") > 0);
        QVERIFY(p.undo());
        QVERIFY(p.dictEntriesOfDictionary(d).isEmpty());
    }

    void dictionaryCountsPerDocument()
    {
        Project p;
        QVERIFY(p.create(path("dict-counts.tsra")));
        const DictionaryFixture f = makeDictionaryProject(p);
        const Dictionaries::Analysis a = Dictionaries::analyze(p, f.dict);
        QCOMPARE(a.documents, QList<qint64>({f.d1, f.d2}));
        QCOMPARE(a.words.value(f.d1), 13);
        QCOMPARE(a.words.value(f.d2), 9);
        QCOMPARE(a.totalWords(), 22);

        // A category counts its subcategories too.
        QCOMPARE(a.count(f.remoto, f.d1), 1);
        QCOMPARE(a.count(f.luoghi, f.d1), 3);
        QCOMPARE(a.count(f.tempo, f.d1), 0);
        QCOMPARE(a.count(f.remoto, f.d2), 1);
        QCOMPARE(a.count(f.luoghi, f.d2), 1);
        QCOMPARE(a.count(f.tempo, f.d2), 2); // "orari", "tempo"
        QCOMPARE(a.total(f.luoghi), 4);
        QVERIFY(qAbs(a.percent(f.tempo, f.d2) - 200.0 / 9) < 1e-9);
        QCOMPARE(a.depth(f.remoto), 1);
        QCOMPARE(a.depth(f.tempo), 0);

        const QString text1 = p.documentText(f.d1);
        const QList<Dictionaries::Hit> hits = a.hits.value(f.remoto);
        QCOMPARE(hits.size(), 2);
        QCOMPARE(hits[0].documentId, f.d1);
        QCOMPARE(text1.mid(hits[0].start, hits[0].end - hits[0].start), QString("da remoto"));

        const Dictionaries::Analysis only2 = Dictionaries::analyze(p, f.dict, {f.d2});
        QCOMPARE(only2.documents, QList<qint64>({f.d2}));
        QCOMPARE(only2.total(f.luoghi), 1);
    }

    void dictionaryAutocodeIsOneUndoStep()
    {
        Project p;
        QVERIFY(p.create(path("dict-autocode.tsra")));
        const DictionaryFixture f = makeDictionaryProject(p);
        const Dictionaries::Analysis a = Dictionaries::analyze(p, f.dict);
        const QString labelBefore = p.undoLabel();

        QHash<qint64, qint64> used;
        QCOMPARE(Dictionaries::autocode(p, a, {f.remoto, f.tempo}, {}, Dictionaries::Extent::Word, &used), 4);
        // New codes mirror the categories under a code named after the dictionary.
        const qint64 root = p.findCode(0, "Lavoro");
        QVERIFY(root > 0);
        const qint64 luoghiCode = p.findCode(root, "Luoghi");
        QCOMPARE(used.value(f.remoto), p.findCode(luoghiCode, "Remoto"));
        QCOMPARE(used.value(f.tempo), p.findCode(root, "Tempo"));
        QCOMPARE(p.codes().size(), 4);
        const QList<Segment> remote = p.segments({}, {used.value(f.remoto)});
        QCOMPARE(remote.size(), 2);
        QCOMPARE(p.documentText(f.d2).mid(remote[1].start, remote[1].length()), QString("smart working"));

        // One undo removes codes and segments together; redo restores them.
        QCOMPARE(p.undoLabel(), QString("Codifica automatica con il dizionario"));
        QVERIFY(p.undo());
        QVERIFY(p.codes().isEmpty());
        QVERIFY(p.allSegments().isEmpty());
        QCOMPARE(p.undoLabel(), labelBefore);
        QVERIFY(p.redo());
        QCOMPARE(p.codes().size(), 4);
        QCOMPARE(p.allSegments().size(), 4);

        // Running it again adds nothing; an existing code and the sentence extent can be chosen.
        QCOMPARE(Dictionaries::autocode(p, a, {f.remoto, f.tempo}, {}, Dictionaries::Extent::Word), 0);
        const qint64 existing = p.addCode(0, "Tempi", Qt::blue);
        QCOMPARE(Dictionaries::autocode(p, a, {f.tempo}, {{f.tempo, existing}}, Dictionaries::Extent::Sentence), 1);
        const QList<Segment> sentence = p.segments({}, {existing});
        QCOMPARE(sentence.size(), 1); // both occurrences are in the same sentence
        QCOMPARE(p.documentText(f.d2).mid(sentence[0].start, sentence[0].length()),
                 QString("Gli orari sono flessibili: smart working tutto il tempo."));
    }

    void dictionaryCountsToVariables()
    {
        Project p;
        QVERIFY(p.create(path("dict-vars.tsra")));
        const DictionaryFixture f = makeDictionaryProject(p);
        const Dictionaries::Analysis a = Dictionaries::analyze(p, f.dict);
        p.addVariable("Lavoro: Tempo", VariableType::Text); // forces the " (2)" suffix
        const QList<qint64> vars = Dictionaries::countsToVariables(p, a, {f.remoto, f.tempo});
        QCOMPARE(vars.size(), 2);
        QCOMPARE(p.variables()[1].name, QString("Lavoro: Luoghi > Remoto"));
        QCOMPARE(p.variables()[1].type, VariableType::Integer);
        QCOMPARE(p.variables()[2].name, QString("Lavoro: Tempo (2)"));
        QCOMPARE(p.variableValue(f.d1, vars[0]), QString("1"));
        QCOMPARE(p.variableValue(f.d1, vars[1]), QString("0"));
        QCOMPARE(p.variableValue(f.d2, vars[1]), QString("2"));
        QVERIFY(p.undo());
        QCOMPARE(p.variables().size(), 1);
    }

    void dictionaryImportExport()
    {
        Project p;
        QVERIFY(p.create(path("dict-io.tsra")));
        const DictionaryFixture f = makeDictionaryProject(p);
        QVERIFY(p.addDictCategory(f.dict, 0, "Vuota") > 0);
        const QList<QStringList> table = Dictionaries::toTable(p, f.dict);
        QCOMPARE(table.first(), QStringList({"Categoria", "Voce"}));
        QVERIFY(table.contains(QStringList({"Luoghi > Remoto", "da remoto"})));
        QVERIFY(table.contains(QStringList({"Vuota", ""})));
        QCOMPARE(table.size(), 8); // header + 6 entries + the empty category

        QString err;
        for (const QString name : {QString("dict.csv"), QString("dict.xlsx")}) {
            QVERIFY2(Dictionaries::exportFile(p, f.dict, path(name), &err), qPrintable(err));
            const qint64 copy = p.addDictionary("Copia " + name);
            QCOMPARE(Dictionaries::importFile(p, copy, 0, path(name), &err), 6);
            QCOMPARE(Dictionaries::toTable(p, copy), table);
            // Importing again adds nothing, in one undo step.
            QCOMPARE(Dictionaries::importFile(p, copy, 0, path(name), &err), 0);
        }

        // Word lists: one entry per line, comments and duplicates skipped; a new category is named after the file.
        writeFile("Emozioni.txt", "# elenco\ngioia\n\nrabbia\nGIOIA\npaur*\n");
        QCOMPARE(Dictionaries::importFile(p, f.dict, 0, path("Emozioni.txt"), &err), 3);
        const qint64 emotions = p.findDictCategory(f.dict, 0, "Emozioni");
        QVERIFY(emotions > 0);
        QCOMPARE(p.dictEntries(emotions).size(), 3);
        QVERIFY(p.undo()); // category and entries together
        QCOMPARE(p.findDictCategory(f.dict, 0, "Emozioni"), qint64(0));
        QCOMPARE(Dictionaries::importFile(p, f.dict, f.tempo, path("Emozioni.txt"), &err), 3);
        QCOMPARE(p.dictEntries(f.tempo).size(), 5);
        QCOMPARE(Dictionaries::importFile(p, f.dict, 0, path("missing.txt"), &err), -1);
        QVERIFY(!err.isEmpty());
    }
};

QTEST_MAIN(TestCore)
#include "test_core.moc"
