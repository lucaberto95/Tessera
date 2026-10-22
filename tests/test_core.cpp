#include "core/Cooccurrence.h"
#include "core/Exporters.h"
#include "core/Importers.h"
#include "core/Project.h"
#include "core/Qdpx.h"
#include "core/Retrieval.h"
#include "core/Stats.h"
#include "core/TextUtils.h"
#include "core/VariableFilter.h"

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
        QVERIFY(p.updateDocumentText(d, "XX abc def ghi", segs));
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
};

QTEST_MAIN(TestCore)
#include "test_core.moc"
