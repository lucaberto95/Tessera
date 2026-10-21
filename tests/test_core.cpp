#include "core/Exporters.h"
#include "core/Importers.h"
#include "core/Project.h"
#include "core/Qdpx.h"
#include "core/TextUtils.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QtCore/private/qzipwriter_p.h>

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
};

QTEST_MAIN(TestCore)
#include "test_core.moc"
