#include "FakeHttp.h"
#include "core/Agreement.h"
#include "core/ai/AiAssist.h"
#include "core/ai/AiCodes.h"
#include "core/ai/AiClient.h"
#include "core/ai/AiConfig.h"
#include "core/ConceptMaps.h"
#include "core/Cooccurrence.h"
#include "core/Dictionaries.h"
#include "core/Exporters.h"
#include "core/ForeignImport.h"
#include "core/Images.h"
#include "core/Importers.h"
#include "core/Merge.h"
#include "core/Pdfs.h"
#include "core/Project.h"
#include "core/Qdpx.h"
#include "core/Report.h"
#include "core/RichText.h"
#include "core/Retrieval.h"
#include "core/Stats.h"
#include "core/Summaries.h"
#include "core/Survey.h"
#include "core/TextUtils.h"
#include "core/VariableFilter.h"
#include "core/Updater.h"
#include "core/WordFrequencies.h"
#include "core/Xlsx.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <functional>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>
#include <QXmlStreamReader>
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

    // Image left half red, right half blue.
    static QImage twoColorImage(int w, int h)
    {
        QImage img(w, h, QImage::Format_RGB32);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                img.setPixelColor(x, y, x < w / 2 ? QColor(Qt::red) : QColor(Qt::blue));
        return img;
    }

    static QByteArray encodeImage(const QImage &img, const char *format)
    {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        img.save(&buffer, format);
        return bytes;
    }

    // A JPEG carrying an EXIF Orientation tag (1..8), inserted as APP1 right after the SOI marker.
    static QByteArray jpegWithOrientation(const QImage &img, int orientation)
    {
        QByteArray jpg = encodeImage(img, "JPEG");
        QByteArray app1("\xFF\xE1\x00\x22" "Exif\0\0" "II*\0\x08\0\0\0" "\x01\0" "\x12\x01\x03\0\x01\0\0\0", 28);
        app1.append(char(orientation));
        app1.append(QByteArray(7, '\0')); // rest of the value, then the offset of the next IFD
        return jpg.left(2) + app1 + jpg.mid(2);
    }

    // A4 PDF with real text: the lines of each page are drawn at x = 72 pt, from y = 100 pt, 40 pt apart.
    // The writer's resolution is 72 dpi, so painter units are PDF points.
    static QByteArray makePdf(const QList<QStringList> &pages)
    {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        QPdfWriter writer(&buffer);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        QPainter painter(&writer);
        painter.setFont(QFont(QStringLiteral("Arial"), 14));
        for (int p = 0; p < pages.size(); ++p) {
            if (p > 0)
                writer.newPage();
            for (int i = 0; i < pages[p].size(); ++i)
                painter.drawText(QPointF(72, 100 + 40 * i), pages[p][i]);
        }
        painter.end();
        return bytes;
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
    void reportDocxPdfHtml()
    {
        Project p;
        QVERIFY(p.create(path("report.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d1 = p.addDocument(g, "Intervista 1", QString("Mi piace lavorare da casa <e> & basta.\nSeconda riga \U0001F600 emoji."));
        const qint64 d2 = p.addDocument(g, "Intervista 2", "Il traffico \x01 è un problema.");
        const qint64 parent = p.addCode(0, "Lavoro", Qt::blue);
        const qint64 child = p.addCode(parent, "Casa", Qt::green);
        p.setCodeDescription(parent, "Tutto sul lavoro");
        const qint64 s1 = p.addSegment(d1, child, 0, 55);
        p.addSegment(d2, parent, 0, 10);
        QVERIFY(p.setSegmentComment(s1, "commento chiave"));
        Memo m;
        m.targetType = MemoTarget::Project;
        m.title = "Idee";
        m.body = "Prima riga\nSeconda riga";
        QVERIFY(p.saveMemo(m) > 0);
        const qint64 v = p.addVariable("Età", VariableType::Integer);
        QVERIFY(p.setVariableValue(d1, v, "42"));
        Summaries::TableConfig cfg;
        cfg.documentIds = {d1, d2};
        cfg.codeIds = {child};
        p.addSummaryTable("Sintesi", Summaries::configToJson(cfg));

        Report::Options opt;
        opt.title = "Titolo <prova>";
        opt.variables = true;
        opt.summaryTables = true;
        const Report::Document doc = Report::build(p, opt);
        QCOMPARE(doc.title, QString("Titolo <prova>"));

        // DOCX: a valid ZIP whose parts are well-formed XML, with real Word styles.
        const QString docx = path("report.docx");
        QString error;
        QVERIFY2(Report::writeDocx(docx, doc, &error), qPrintable(error));
        QZipReader zip(docx);
        QVERIFY(zip.isReadable());
        QStringList parts;
        for (const auto &fi : zip.fileInfoList())
            parts << fi.filePath;
        for (const char *name : {"[Content_Types].xml", "_rels/.rels", "word/document.xml", "word/styles.xml",
                                 "word/_rels/document.xml.rels", "docProps/core.xml"}) {
            QVERIFY2(parts.contains(QString::fromLatin1(name)), name);
            const QByteArray xml = zip.fileData(QString::fromLatin1(name));
            QXmlStreamReader reader(xml);
            while (!reader.atEnd())
                reader.readNext();
            QVERIFY2(!reader.hasError(), qPrintable(QString("%1: %2").arg(name, reader.errorString())));
        }
        const QString body = QString::fromUtf8(zip.fileData("word/document.xml"));
        const QString styles = QString::fromUtf8(zip.fileData("word/styles.xml"));
        for (const char *style : {"Title", "Heading1", "Heading2", "Quote", "Normal"})
            QVERIFY2(styles.contains(QString("w:styleId=\"%1\"").arg(style)), style);
        QVERIFY(styles.contains("<w:outlineLvl w:val=\"0\"/>")); // headings feed the automatic table of contents
        QVERIFY(body.contains("<w:pStyle w:val=\"Heading1\"/>"));
        QVERIFY(body.contains("<w:pStyle w:val=\"Heading2\"/>"));
        QVERIFY(body.contains("<w:pStyle w:val=\"Quote\"/>"));
        QVERIFY(body.contains("TOC \\o \"1-3\""));
        QVERIFY(body.contains("<w:tbl>")); // variables and summary tables
        QVERIFY(body.contains("Titolo &lt;prova&gt;"));
        QVERIFY(body.contains("&lt;e&gt; &amp; basta"));
        QVERIFY(body.contains("commento chiave"));
        QVERIFY(body.contains("Seconda riga"));
        QVERIFY(!body.contains(QChar(0x01))); // characters XML cannot hold are dropped
        QVERIFY(body.contains(QString::fromUcs4(U"\U0001F600")));

        // PDF and HTML
        const QString pdf = path("report.pdf");
        QVERIFY2(Report::writePdf(pdf, doc, &error), qPrintable(error));
        QFile pf(pdf);
        QVERIFY(pf.open(QIODevice::ReadOnly));
        QVERIFY(pf.readAll().startsWith("%PDF"));
        const QString html = path("report.html");
        QVERIFY2(Report::writeHtml(html, doc, &error), qPrintable(error));
        QFile hf(html);
        QVERIFY(hf.open(QIODevice::ReadOnly));
        const QString h = QString::fromUtf8(hf.readAll());
        QVERIFY(h.contains("<h1"));
        QVERIFY(h.contains("Codebook"));
        QVERIFY(h.contains("&lt;e&gt;"));

        // Options: no codebook / grouping by document / no comments / no examples.
        Report::Options small;
        small.contents = false;
        small.codebook = false;
        small.memos = false;
        small.comments = false;
        small.grouping = Report::Grouping::ByDocument;
        const QString smallHtml = Report::toHtml(Report::build(p, small));
        QVERIFY(!smallHtml.contains("Codebook"));
        QVERIFY(!smallHtml.contains("Idee"));
        QVERIFY(!smallHtml.contains("commento chiave"));
        QVERIFY(smallHtml.contains("Intervista 2"));
        const QString withComment = Report::toHtml(Report::build(p, Report::Options()));
        QVERIFY(withComment.contains("commento chiave"));
        QVERIFY(withComment.contains("Idee"));
        Report::Options noExamples;
        noExamples.segments = false;
        noExamples.examplesPerCode = 0;
        QVERIFY(!Report::toHtml(Report::build(p, noExamples)).contains("Mi piace lavorare"));
        Report::Options oneExample = noExamples;
        oneExample.examplesPerCode = 1;
        QVERIFY(Report::toHtml(Report::build(p, oneExample)).contains("Mi piace lavorare"));
    }
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
            Project::builtInMigrations().at(4), // segment anchors (v6), needed by the segment queries
            Project::builtInMigrations().at(6), // PDF area text (v8), read by the segment queries
        };
        QCOMPARE(migrations.last().toVersion, 8);
        Project p;
        p.setMigrations(migrations);
        QCOMPARE(p.schemaVersion(), 8);
        QVERIFY(p.open(path("m1.tsra")));
        QCOMPARE(p.meta("schema_version"), QString("8"));
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
                           "ALTER TABLE documents_new RENAME TO documents",
                           // segment anchors, needed by the segment queries
                           "ALTER TABLE segments ADD COLUMN anchor_kind TEXT NOT NULL DEFAULT 'text'",
                           "ALTER TABLE segments ADD COLUMN page INTEGER", "ALTER TABLE segments ADD COLUMN x1 REAL",
                           "ALTER TABLE segments ADD COLUMN y1 REAL", "ALTER TABLE segments ADD COLUMN x2 REAL",
                           "ALTER TABLE segments ADD COLUMN y2 REAL", "ALTER TABLE segments ADD COLUMN t_start INTEGER",
                           "ALTER TABLE segments ADD COLUMN t_end INTEGER",
                           "ALTER TABLE segments ADD COLUMN area_text TEXT NOT NULL DEFAULT ''"}}});
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

    // Issue #25: a v1 project upgrades to generalized segments without losing data.
    void generalizedSegmentsMigration()
    {
        qint64 code = 0;
        {
            Project old;
            old.setMigrations({});
            QVERIFY(old.create(path("gs1.tsra")));
            const qint64 d = old.addDocument(old.groups().first().id, "Intervista", "Testo da codificare");
            code = old.addCode(0, "Tema", Qt::red);
            QVERIFY(old.addSegment(d, code, 2, 9, 3, "nota") > 0);
        }
        Project p;
        QVERIFY(p.open(path("gs1.tsra")));
        QVERIFY(!p.migrationBackupPath().isEmpty());
        QCOMPARE(fileVersion(p.migrationBackupPath()), 1);
        QCOMPARE(p.documents().first().kind, DocumentKind::Text);
        const QList<Segment> segs = p.allSegments();
        QCOMPARE(segs.size(), 1);
        QCOMPARE(segs.first().kind, SegmentKind::Text);
        QCOMPARE(segs.first().start, 2);
        QCOMPARE(segs.first().end, 9);
        QCOMPARE(segs.first().weight, 3);
        QCOMPARE(segs.first().comment, QString("nota"));
    }

    void areaAndTimeSegments()
    {
        Project p;
        QVERIFY(p.create(path("anchors.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 code = p.addCode(0, "Tema", Qt::red);
        const qint64 text = p.addDocument(g, "Testo", "Un testo di prova");
        const qint64 pdf = p.addMediaDocument(g, "Rapporto", DocumentKind::Pdf);
        const qint64 img = p.addMediaDocument(g, "Foto", DocumentKind::Image);
        const qint64 aud = p.addMediaDocument(g, "Audio", DocumentKind::Audio);
        QVERIFY(pdf > 0 && img > 0 && aud > 0);
        QCOMPARE(p.document(pdf).kind, DocumentKind::Pdf);
        QCOMPARE(p.document(text).kind, DocumentKind::Text);
        QVERIFY(p.addMediaDocument(g, "x", DocumentKind::Text) == 0);

        // Area: PDF needs a page, images do not; corners are normalised.
        const qint64 a = p.addAreaSegment(pdf, code, 3, 200, 300, 100, 50, 2, "area");
        QVERIFY(a > 0);
        const Segment sa = p.segment(a);
        QCOMPARE(sa.kind, SegmentKind::Area);
        QCOMPARE(sa.page, 3);
        QCOMPARE(sa.x1, 100.0);
        QCOMPARE(sa.y1, 50.0);
        QCOMPARE(sa.x2, 200.0);
        QCOMPARE(sa.y2, 300.0);
        QCOMPARE(sa.weight, 2);
        QCOMPARE(p.addAreaSegment(pdf, code, 3, 100, 50, 200, 300), a); // duplicate
        QCOMPARE(p.addAreaSegment(pdf, code, 0, 1, 1, 5, 5), qint64(0)); // pdf needs a page
        QVERIFY(p.addAreaSegment(img, code, 1, 1, 1, 5, 5) == 0);        // image has no pages
        QVERIFY(p.addAreaSegment(img, code, 0, 1, 1, 5, 5) > 0);
        QVERIFY(p.addAreaSegment(img, code, 0, 1, 1, 1, 5) == 0);        // empty
        QVERIFY(p.addAreaSegment(text, code, 0, 1, 1, 5, 5) == 0);
        QVERIFY(p.addAreaSegment(aud, code, 0, 1, 1, 5, 5) == 0);

        // Time.
        const qint64 t = p.addTimeSegment(aud, code, 1500, 4200);
        QVERIFY(t > 0);
        const Segment st = p.segment(t);
        QCOMPARE(st.kind, SegmentKind::Time);
        QCOMPARE(st.tStartMs, qint64(1500));
        QCOMPARE(st.tEndMs, qint64(4200));
        QCOMPARE(p.addTimeSegment(aud, code, 1500, 4200), t);
        QVERIFY(p.addTimeSegment(aud, code, 10, 10) == 0);
        QVERIFY(p.addTimeSegment(pdf, code, 0, 10) == 0);
        QVERIFY(p.addSegment(pdf, code, 0, 1) == 0);

        // Text-based lists skip non-text segments unless asked.
        QVERIFY(p.addSegment(text, code, 0, 2) > 0);
        QCOMPARE(p.allSegments().size(), 1);
        QCOMPARE(p.allSegments(true).size(), 4);
        QCOMPARE(p.segmentsForDocument(pdf).size(), 0);
        QCOMPARE(p.segmentsForDocument(pdf, true).size(), 1);
        QCOMPARE(p.segments({}, {code}).size(), 1);
        QCOMPARE(p.segments({}, {code}, true).size(), 4);
        QCOMPARE(p.segmentCountByDocument().value(pdf), 1);

        // Undo/redo restore the anchor.
        QVERIFY(p.deleteSegment(t));
        QVERIFY(p.undo());
        QCOMPARE(p.segment(t).tEndMs, qint64(4200));
        QVERIFY(p.deleteDocument(aud));
        QVERIFY(p.undo());
        QCOMPARE(p.segment(t).kind, SegmentKind::Time);
        QCOMPARE(p.document(aud).kind, DocumentKind::Audio);

        // QDPX keeps working: the text document round-trips, non-text ones are left out for now.
        QString err, report;
        QVERIFY2(Qdpx::exportProject(p, path("anchors.qdpx"), &err), qPrintable(err));
        Project q;
        QVERIFY(q.create(path("anchors2.tsra")));
        QVERIFY2(Qdpx::importProject(q, path("anchors.qdpx"), &report, &err), qPrintable(err));
        QCOMPARE(q.documents().size(), 1);
        QCOMPARE(q.allSegments().size(), 1);
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

    // ---- formatted documents (issue #32)

    static QString docxNs() { return QStringLiteral("xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\""); }

    QString writeFormattedDocx()
    {
        const QByteArray doc =
            "<?xml version=\"1.0\"?><w:document " + docxNs().toUtf8() + "><w:body>"
            "<w:p><w:pPr><w:pStyle w:val=\"Heading1\"/></w:pPr><w:r><w:t>Titolo</w:t></w:r></w:p>"
            "<w:p><w:pPr><w:tabs><w:tab w:val=\"left\" w:pos=\"720\"/></w:tabs><w:rPr><w:b/></w:rPr></w:pPr>"
            "<w:r><w:t xml:space=\"preserve\">Testo </w:t></w:r><w:r><w:rPr><w:b/></w:rPr><w:t>forte</w:t></w:r>"
            "<w:r><w:rPr><w:i/><w:u w:val=\"single\"/></w:rPr><w:t xml:space=\"preserve\"> corsivo</w:t></w:r>"
            "<w:r><w:rPr><w:b w:val=\"0\"/></w:rPr><w:t> fine</w:t></w:r></w:p>"
            "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"1\"/></w:numPr></w:pPr><w:r><w:t>voce uno</w:t></w:r></w:p>"
            "<w:p><w:pPr><w:numPr><w:ilvl w:val=\"0\"/><w:numId w:val=\"2\"/></w:numPr></w:pPr><w:r><w:t>numero</w:t></w:r></w:p>"
            "<w:tbl><w:tr><w:trPr><w:tblHeader/></w:trPr><w:tc><w:p><w:r><w:t>A</w:t></w:r></w:p></w:tc>"
            "<w:tc><w:p><w:r><w:t>B</w:t></w:r></w:p></w:tc></w:tr>"
            "<w:tr><w:tc><w:p><w:r><w:t>1</w:t></w:r></w:p></w:tc><w:tc><w:p><w:r><w:t>2</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"
            "<w:p><w:r><w:t>chiusura</w:t></w:r></w:p>"
            "</w:body></w:document>";
        const QByteArray styles =
            "<?xml version=\"1.0\"?><w:styles " + docxNs().toUtf8() + ">"
            "<w:style w:type=\"paragraph\" w:styleId=\"Heading1\"><w:name w:val=\"heading 1\"/></w:style></w:styles>";
        const QByteArray numbering =
            "<?xml version=\"1.0\"?><w:numbering " + docxNs().toUtf8() + ">"
            "<w:abstractNum w:abstractNumId=\"0\"><w:lvl w:ilvl=\"0\"><w:numFmt w:val=\"bullet\"/></w:lvl></w:abstractNum>"
            "<w:abstractNum w:abstractNumId=\"1\"><w:lvl w:ilvl=\"0\"><w:numFmt w:val=\"decimal\"/></w:lvl></w:abstractNum>"
            "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>"
            "<w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num></w:numbering>";
        {
            QZipWriter zip(path("fmt.docx"));
            zip.addFile("word/document.xml", doc);
            zip.addFile("word/styles.xml", styles);
            zip.addFile("word/numbering.xml", numbering);
        }
        return path("fmt.docx");
    }

    static QString rawText(const QTextDocument &d)
    {
        QString t = d.toRawText();
        t.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
        return t;
    }

    static bool hasStyle(const RichText::Formatting &f, int from, int to, int style)
    {
        for (const RichText::Span &s : f.spans)
            if (s.start == from && s.end == to && s.style == style)
                return true;
        return false;
    }

    static bool hasBlock(const RichText::Formatting &f, int from, int to, RichText::BlockKind kind, int level)
    {
        for (const RichText::Block &b : f.blocks)
            if (b.start == from && b.end == to && b.kind == kind && b.level == level)
                return true;
        return false;
    }

    void importDocxKeepsFormatting()
    {
        const auto r = Importers::importFile(writeFormattedDocx());
        QVERIFY2(r.ok, qPrintable(r.error));
        // The tab stop in the paragraph properties is not text.
        QCOMPARE(r.text, QString("Titolo\nTesto forte corsivo fine\nvoce uno\nnumero\nA\nB\n1\n2\nchiusura"));
        QVERIFY(!r.richContent.isEmpty());
        const RichText::Formatting f = RichText::fromJson(r.richContent);
        using K = RichText::BlockKind;
        QVERIFY(hasBlock(f, 0, 6, K::Heading, 1));
        QVERIFY(hasBlock(f, 32, 40, K::Bullet, 0));
        QVERIFY(hasBlock(f, 41, 47, K::Numbered, 0));
        QVERIFY(hasBlock(f, 48, 49, K::TableCell, 2)); // header row
        QVERIFY(hasBlock(f, 50, 51, K::TableCell, 2));
        QVERIFY(hasBlock(f, 52, 53, K::TableCell, 1));
        QVERIFY(hasBlock(f, 54, 55, K::TableCell, 1));
        QCOMPARE(f.blocks.size(), 7);
        const int forte = int(r.text.indexOf("forte"));
        QVERIFY(hasStyle(f, forte, forte + 5, RichText::Bold));
        QVERIFY(hasStyle(f, forte + 5, forte + 13, RichText::Italic | RichText::Underline));
        QCOMPARE(f.spans.size(), 2);
    }

    void formattingKeepsPositions()
    {
        const auto r = Importers::importFile(writeFormattedDocx());
        QVERIFY(r.ok);
        QTextDocument shown;
        shown.setPlainText(r.text);
        RichText::apply(&shown, RichText::fromJson(r.richContent));
        // The core constraint: formatting never changes the text, so positions mean the same thing.
        QCOMPARE(rawText(shown), r.text);
        QCOMPARE(int(shown.characterCount()) - 1, int(r.text.size()));
        QCOMPARE(shown.blockCount(), 9);
        QCOMPARE(shown.findBlock(0).blockFormat().headingLevel(), 1);
        {
            QTextCursor h(&shown);
            h.setPosition(2);
            QVERIFY(h.charFormat().fontWeight() >= QFont::Bold);
        }
        QVERIFY(shown.findBlock(32).textList());
        QCOMPARE(shown.findBlock(32).textList()->format().style(), QTextListFormat::ListDisc);
        QCOMPARE(shown.findBlock(41).textList()->format().style(), QTextListFormat::ListDecimal);
        QCOMPARE(shown.findBlock(48).blockFormat().background().color().alpha(), 90); // table header
        const int forte = int(r.text.indexOf("forte"));
        QTextCursor c(&shown);
        c.setPosition(forte + 1);
        QVERIFY(c.charFormat().fontWeight() >= QFont::Bold);
        c.setPosition(forte + 8);
        QVERIFY(c.charFormat().fontItalic());

        // Coding and retrieval work on the plain text: same segments with or without formatting.
        Project p;
        QVERIFY(p.create(path("fmtpos.tsra")));
        const qint64 plainDoc = p.addDocument(p.groups().first().id, "plain", r.text);
        const qint64 richDoc = p.addDocument(p.groups().first().id, "rich", r.text);
        QVERIFY(p.setDocumentRichContent(richDoc, r.richContent));
        const qint64 code = p.addCode(0, "c", Qt::red);
        for (const qint64 d : {plainDoc, richDoc})
            QVERIFY(p.addSegment(d, code, forte, forte + 13, 0, "") > 0);
        QCOMPARE(p.documentText(richDoc), p.documentText(plainDoc));
        const auto sp = p.segmentsForDocument(plainDoc), sr = p.segmentsForDocument(richDoc);
        QCOMPARE(sr.size(), 1);
        QCOMPARE(sr.first().start, sp.first().start);
        QCOMPARE(sr.first().end, sp.first().end);
        QTextDocument again;
        again.setPlainText(p.documentText(richDoc));
        RichText::apply(&again, RichText::fromJson(p.documentRichContent(richDoc)));
        QTextCursor sel(&again);
        sel.setPosition(sr.first().start);
        sel.setPosition(sr.first().end, QTextCursor::KeepAnchor);
        QCOMPARE(sel.selectedText(), r.text.mid(forte, 13));
    }

    void formattingFollowsTextEdit()
    {
        Project p;
        QVERIFY(p.create(path("fmtedit.tsra")));
        const qint64 d = p.addDocument(p.groups().first().id, "d", "Titolo\nuno due tre");
        RichText::Formatting f;
        f.blocks.append({0, 6, RichText::BlockKind::Heading, 1});
        f.spans.append({11, 14, RichText::Bold}); // "due"
        QVERIFY(p.setDocumentRichContent(d, RichText::toJson(f)));
        QVERIFY(p.updateDocumentText(d, "Il Titolo\nuno due tre", {}, {}));
        const RichText::Formatting g = RichText::fromJson(p.documentRichContent(d));
        QVERIFY(hasBlock(g, 0, 9, RichText::BlockKind::Heading, 1));
        const QString text = p.documentText(d);
        QVERIFY2(hasBlock(g, 0, 9, RichText::BlockKind::Heading, 1), qPrintable(RichText::toJson(g)));
        const int due = int(text.indexOf("due"));
        QVERIFY(hasStyle(g, due, due + 3, RichText::Bold));
        // Undo restores text and formatting together.
        QVERIFY(p.undo());
        QCOMPARE(p.documentText(d), QString("Titolo\nuno due tre"));
        QVERIFY2(hasBlock(RichText::fromJson(p.documentRichContent(d)), 0, 6, RichText::BlockKind::Heading, 1),
                 qPrintable(p.documentRichContent(d)));
    }

    void importRtfAndHtmlKeepFormatting()
    {
        writeFile("f.rtf", "{\\rtf1\\ansi Normale \\b forte\\b0  \\i obliquo\\i0\\par "
                           "\\pard\\intbl A\\cell B\\cell\\row \\pard dopo\\par}");
        auto r = Importers::importFile(path("f.rtf"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.text, QString("Normale forte obliquo\nA\tB\t\ndopo"));
        RichText::Formatting f = RichText::fromJson(r.richContent);
        QVERIFY(hasStyle(f, 8, 13, RichText::Bold));
        QVERIFY(hasStyle(f, 14, 21, RichText::Italic));
        QVERIFY(hasBlock(f, 22, 26, RichText::BlockKind::TableCell, 0));

        writeFile("f.html", "<html><body><h2>Titolo</h2><p>Un <b>grassetto</b> e <i>corsivo</i>.</p>"
                            "<ul><li>uno</li><li>due</li></ul><ol><li>primo</li></ol>"
                            "<table border=1><tr><th>X</th><th>Y</th></tr><tr><td>1</td><td>2</td></tr></table></body></html>");
        r = Importers::importFile(path("f.html"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(!r.richContent.isEmpty());
        f = RichText::fromJson(r.richContent);
        const int g = int(r.text.indexOf("grassetto"));
        QVERIFY(hasStyle(f, g, g + 9, RichText::Bold));
        QVERIFY(hasBlock(f, 0, 6, RichText::BlockKind::Heading, 2));
        const int uno = int(r.text.indexOf("uno"));
        QVERIFY(hasBlock(f, uno, uno + 3, RichText::BlockKind::Bullet, 0));
        const int primo = int(r.text.indexOf("primo"));
        QVERIFY(hasBlock(f, primo, primo + 5, RichText::BlockKind::Numbered, 0));
        const int one = int(r.text.indexOf("\n1\n")) + 1;
        QVERIFY(hasBlock(f, one, one + 1, RichText::BlockKind::TableCell, 1));
        // Same text as before the formatting was kept.
        QTextDocument shown;
        shown.setPlainText(r.text);
        RichText::apply(&shown, f);
        QCOMPARE(rawText(shown), r.text);
    }

    void importOdtKeepsFormatting()
    {
        const QByteArray content =
            "<?xml version=\"1.0\"?><office:document-content xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
            "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
            "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
            "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\">"
            "<office:automatic-styles><style:style style:name=\"T1\" style:family=\"text\">"
            "<style:text-properties fo:font-weight=\"bold\"/></style:style>"
            "<text:list-style style:name=\"L1\"><text:list-level-style-number text:level=\"1\"/></text:list-style>"
            "</office:automatic-styles><office:body><office:text>"
            "<text:h text:outline-level=\"2\">Capo</text:h>"
            "<text:p>Un <text:span text:style-name=\"T1\">grassetto</text:span> qui</text:p>"
            "<text:list text:style-name=\"L1\"><text:list-item><text:p>primo</text:p></text:list-item></text:list>"
            "<table:table><table:table-row><table:table-cell><text:p>c1</text:p></table:table-cell></table:table-row></table:table>"
            "</office:text></office:body></office:document-content>";
        {
            QZipWriter zip(path("fmt.odt"));
            zip.addFile("content.xml", content);
        }
        const auto r = Importers::importFile(path("fmt.odt"));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.text, QString("Capo\nUn grassetto qui\nprimo\nc1"));
        const RichText::Formatting f = RichText::fromJson(r.richContent);
        QVERIFY(hasBlock(f, 0, 4, RichText::BlockKind::Heading, 2));
        QVERIFY(hasStyle(f, 8, 17, RichText::Bold));
        QVERIFY(hasBlock(f, 22, 27, RichText::BlockKind::Numbered, 0));
        QVERIFY(hasBlock(f, 28, 30, RichText::BlockKind::TableCell, 0));
    }

    void mergeCopiesFormatting()
    {
        Project a, b;
        QVERIFY(a.create(path("fa.tsra")));
        QVERIFY(b.create(path("fb.tsra")));
        const qint64 d = b.addDocument(b.groups().first().id, "d", "Titolo\ntesto");
        RichText::Formatting f;
        f.blocks.append({0, 6, RichText::BlockKind::Heading, 1});
        QVERIFY(b.setDocumentRichContent(d, RichText::toJson(f)));
        b.close();
        Merge::Source src;
        QString error;
        QVERIFY2(src.load(path("fb.tsra"), &error), qPrintable(error));
        QVERIFY(Merge::apply(a, src, {}).ok);
        QCOMPARE(RichText::fromJson(a.documentRichContent(a.documents().first().id)).blocks.size(), 1);
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

    void agreementExistence()
    {
        using namespace Agreement;
        // Code 1 over documents 1..10: both coders in 1-5, only A in 6, only B in 7-8, neither in 9-10.
        // Code 2: nobody used it.
        QList<Segment> a, b;
        for (int d = 1; d <= 6; ++d)
            a << seg(d, d, 1, 0, 5);
        for (int d : {1, 2, 3, 4, 5, 7, 8})
            b << seg(100 + d, d, 1, 2, 4);
        Options o;
        o.level = Level::Existence;
        for (int d = 1; d <= 10; ++d)
            o.documents << d;
        o.codes = {1, 2};
        const Result r = compute(o, a, b);

        const CodeResult *c1 = r.forCode(1);
        QVERIFY(c1);
        QCOMPARE(c1->units, 10);
        QCOMPARE(c1->agreements, 7);
        QVERIFY(qFuzzyCompare(c1->percent, 70.0));
        // po = .7, pe = .6*.7 + .4*.3 = .54 -> (.7 - .54) / .46
        QVERIFY(qAbs(c1->kappa - 0.16 / 0.46) < 1e-9);
        QVERIFY(qAbs(c1->kappaBP - 0.4) < 1e-9); // 2 * .7 - 1
        // Code 2: full agreement, but no variation -> Cohen undefined.
        const CodeResult *c2 = r.forCode(2);
        QCOMPARE(c2->agreements, 10);
        QVERIFY(std::isnan(c2->kappa));
        QVERIFY(qAbs(c2->kappaBP - 1.0) < 1e-9);
        // Total pools the units: po = 17/20, pe = .3*.35 + .7*.65 = .56
        QCOMPARE(r.total.units, 20);
        QCOMPARE(r.total.agreements, 17);
        QVERIFY(qAbs(r.total.kappa - (0.85 - 0.56) / 0.44) < 1e-9);

        QCOMPARE(r.disagreements.size(), 3);
        QCOMPARE(r.disagreements[0].documentId, qint64(6));
        QCOMPARE(int(r.disagreements[0].kind), int(DisagreementKind::OnlyA));
        QCOMPARE(r.disagreements[0].segment.start, 0);
        QCOMPARE(r.disagreements[1].documentId, qint64(7));
        QCOMPARE(int(r.disagreements[1].kind), int(DisagreementKind::OnlyB));
        QCOMPARE(r.disagreements[1].segment.start, 2);
    }

    void agreementFrequency()
    {
        using namespace Agreement;
        // Segments of code 1 per document: A (2,1,0,3), B (2,2,0,1).
        QList<Segment> a, b;
        int id = 1;
        const int na[] = {2, 1, 0, 3}, nb[] = {2, 2, 0, 1};
        for (int d = 0; d < 4; ++d) {
            for (int i = 0; i < na[d]; ++i)
                a << seg(id++, d + 1, 1, i * 10, i * 10 + 5);
            for (int i = 0; i < nb[d]; ++i)
                b << seg(id++, d + 1, 1, i * 10 + 1, i * 10 + 6);
        }
        Options o;
        o.level = Level::Frequency;
        o.documents = {1, 2, 3, 4};
        o.codes = {1};
        const Result r = compute(o, a, b);
        QCOMPARE(r.total.units, 4);
        QCOMPARE(r.total.agreements, 2);
        QVERIFY(qFuzzyCompare(r.total.percent, 50.0));
        // pe = 1/16 * (1*2 + 1*1 + 1*1 + 0) = .25 -> (.5 - .25) / .75
        QVERIFY(qAbs(r.total.kappa - 1.0 / 3) < 1e-9);
        QCOMPARE(r.total.categories, 4);
        QVERIFY(qAbs(r.total.kappaBP - 1.0 / 3) < 1e-9);
        QCOMPARE(r.disagreements.size(), 2);
        QCOMPARE(int(r.disagreements[0].kind), int(DisagreementKind::Count));
        QCOMPARE(r.disagreements[0].documentId, qint64(2));
        QCOMPARE(r.disagreements[0].countA, 1);
        QCOMPARE(r.disagreements[0].countB, 2);
        QCOMPARE(r.disagreements[1].documentId, qint64(4));
        QCOMPARE(r.disagreements[1].segment.start, 0); // the coder with more segments (A)
        QCOMPARE(r.disagreements[1].countA, 3);
    }

    void agreementSegments()
    {
        using namespace Agreement;
        // Document 1, code 1. A: 0-10, 20-30, 50-60. B: 0-10, 21-30, 100-110.
        // 20-30 vs 21-30: overlap 9, union 10 -> .9.
        QList<Segment> a{seg(1, 1, 1, 0, 10), seg(2, 1, 1, 20, 30), seg(3, 1, 1, 50, 60)};
        QList<Segment> b{seg(4, 1, 1, 0, 10), seg(5, 1, 1, 21, 30), seg(6, 1, 1, 100, 110)};
        Segment area = seg(7, 1, 1, 0, 1); // non-text segments are not compared at this level
        area.kind = SegmentKind::Area;
        a << area;
        Options o;
        o.level = Level::Segments;
        o.documents = {1};
        o.codes = {1};
        o.minOverlap = 0.9;
        Result r = compute(o, a, b);
        QCOMPARE(r.total.units, 6);
        QCOMPARE(r.total.agreements, 4);
        QVERIFY(qAbs(r.total.percent - 100.0 * 4 / 6) < 1e-9);
        QVERIFY(std::isnan(r.total.kappa));
        QVERIFY(qAbs(r.total.kappaBP - (4.0 / 6 - 0.5) / 0.5) < 1e-9);
        QCOMPARE(r.disagreements.size(), 2);
        QCOMPARE(int(r.disagreements[0].kind), int(DisagreementKind::OnlyA));
        QCOMPARE(r.disagreements[0].segment.start, 50);
        QCOMPARE(int(r.disagreements[1].kind), int(DisagreementKind::OnlyB));
        QCOMPARE(r.disagreements[1].segment.start, 100);

        o.minOverlap = 0.95; // 20-30 / 21-30 no longer agree
        r = compute(o, a, b);
        QCOMPARE(r.total.agreements, 2);
        QCOMPARE(r.disagreements.size(), 4);
        QVERIFY(qAbs(r.total.kappaBP - (2.0 / 6 - 0.5) / 0.5) < 1e-9);

        // A code only the other coder used elsewhere does not match; a different code never does.
        QList<Segment> other{seg(8, 1, 2, 0, 10)};
        o.codes = {1, 2};
        r = compute(o, {seg(1, 1, 1, 0, 10)}, other);
        QCOMPARE(r.forCode(1)->agreements, 0);
        QCOMPARE(r.forCode(2)->agreements, 0);
        QCOMPARE(r.total.units, 2);
    }

    void agreementTwoCodersAndProjects()
    {
        // Two copies of a project coded by different people; ids differ, uuids and names match.
        const QString fileA = m_dir.filePath("agree-a.tsra"), fileB = m_dir.filePath("agree-b.tsra");
        Project a;
        a.setAuthor("Anna");
        QVERIFY(a.create(fileA));
        const qint64 d1 = a.addDocument(a.groups().first().id, "Uno", "Primo testo da codificare.");
        const qint64 d2 = a.addDocument(a.groups().first().id, "Due", "Secondo testo da codificare.");
        const qint64 parent = a.addCode(0, "Tema", Qt::red);
        const qint64 sub = a.addCode(parent, "Sotto", Qt::blue);
        a.addSegment(d1, sub, 0, 5);
        a.addSegment(d2, parent, 0, 7);
        QVERIFY(a.saveCopyAs(fileB));
        a.addSegment(d1, parent, 6, 11); // Anna's second coding

        QCOMPARE(Agreement::authors(a), (QStringList{"Anna"}));
        Project b;
        b.setAuthor("Bruno");
        QVERIFY(b.open(fileB));
        b.addSegment(d1, b.findCode(b.findCode(0, "Tema"), "Sotto"), 0, 4);
        QCOMPARE(Agreement::authors(b), (QStringList{"Anna", "Bruno"})); // the copy kept Anna's codings
        QCOMPARE(Agreement::byAuthor(b.allSegments(true), "Bruno").size(), 1);

        const auto docs = Agreement::mapDocuments(b, a);
        const auto codes = Agreement::mapCodes(b, a);
        QCOMPARE(docs.size(), 2);
        QCOMPARE(docs.value(d2), d2);
        QCOMPARE(codes.value(b.findCode(b.findCode(0, "Tema"), "Sotto")), sub);
        const QList<Segment> mapped = Agreement::remap(Agreement::byAuthor(b.allSegments(true), "Bruno"), docs, codes);
        QCOMPARE(mapped.size(), 1);
        QCOMPARE(mapped.first().documentId, d1);
        QCOMPARE(mapped.first().codeId, sub);

        Agreement::Options o;
        o.level = Agreement::Level::Existence;
        o.documents = {d1, d2};
        o.codes = {parent, sub};
        const Agreement::Result r = Agreement::compute(o, Agreement::byAuthor(a.allSegments(true), "Anna"), mapped);
        QCOMPARE(r.forCode(sub)->agreements, 2);   // d1 both; d2 neither
        QCOMPARE(r.forCode(parent)->agreements, 0); // Anna coded it in both documents, Bruno never
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

    void foreignImportDetectsAndImports()
    {
        // A MAXQDA-like SQLite file (built here, no vendor material): recognised, refused with guidance, not modified.
        const QString mq = path("sample.mqda");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "mqda_fixture");
            db.setDatabaseName(mq);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("CREATE TABLE sys_Setup (Key TEXT, Value TEXT)"));
            QVERIFY(q.exec("INSERT INTO sys_Setup VALUES ('APP', 'MAXQDA test MX24')"));
            db.close();
        }
        QSqlDatabase::removeDatabase("mqda_fixture");
        const QByteArray before = QCryptographicHash::hash([&] { QFile f(mq); f.open(QIODevice::ReadOnly); return f.readAll(); }(),
                                                           QCryptographicHash::Sha256);
        const ForeignImport::Info info = ForeignImport::detect(mq);
        QCOMPARE(info.kind, ForeignImport::Kind::MaxqdaNative);
        QVERIFY(info.description.contains("MX24"));
        Project p;
        QVERIFY(p.create(path("fi1.tsra")));
        QString report, err;
        QVERIFY(!ForeignImport::importFile(p, mq, &report, &err));
        QVERIFY(err.contains("REFI-QDA"));
        const QByteArray after = QCryptographicHash::hash([&] { QFile f(mq); f.open(QIODevice::ReadOnly); return f.readAll(); }(),
                                                          QCryptographicHash::Sha256);
        QCOMPARE(after, before);

        // NVivo native files: explained, not converted.
        QFile nv(path("sample.nvp"));
        QVERIFY(nv.open(QIODevice::WriteOnly));
        nv.write("MSSQL-ish binary");
        nv.close();
        QCOMPARE(ForeignImport::detect(path("sample.nvp")).kind, ForeignImport::Kind::NvivoNative);
        QVERIFY(!ForeignImport::importFile(p, path("sample.nvp"), &report, &err));
        QVERIFY(err.contains("REFI-QDA"));

        // A bare REFI-QDA codebook (as exported by NVivo/MAXQDA) imports its code tree.
        QFile qdc(path("book.qdc"));
        QVERIFY(qdc.open(QIODevice::WriteOnly));
        qdc.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                  "<CodeBook xmlns=\"urn:QDA-XML:codebook:1.0\"><Codes>"
                  "<Code guid=\"11111111-1111-1111-1111-111111111111\" name=\"Lavoro\" isCodable=\"true\" color=\"#ff0000\">"
                  "<Code guid=\"22222222-2222-2222-2222-222222222222\" name=\"Stress\" isCodable=\"true\"/></Code>"
                  "</Codes></CodeBook>");
        qdc.close();
        QCOMPARE(ForeignImport::detect(path("book.qdc")).kind, ForeignImport::Kind::Qdc);
        QVERIFY2(ForeignImport::importFile(p, path("book.qdc"), &report, &err), qPrintable(err));
        QCOMPARE(p.codes().size(), 2);
        QCOMPARE(p.codes().first().name, QString("Lavoro"));

        // A .qdpx goes through the regular importer.
        Project src;
        QVERIFY(src.create(path("fi_src.tsra")));
        src.addCode(0, "Tema", QColor("#112233"));
        QVERIFY2(Qdpx::exportProject(src, path("fi.qdpx"), &err), qPrintable(err));
        QCOMPARE(ForeignImport::detect(path("fi.qdpx")).kind, ForeignImport::Kind::Qdpx);
        Project dst;
        QVERIFY(dst.create(path("fi_dst.tsra")));
        QVERIFY2(ForeignImport::importFile(dst, path("fi.qdpx"), &report, &err), qPrintable(err));
        QCOMPARE(dst.codes().size(), 1);

        QVERIFY(ForeignImport::detect(path("nonexistent.xyz")).kind == ForeignImport::Kind::Unknown);
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

    void qdpxPdfAndImageRoundTrip()
    {
        Project p;
        QVERIFY(p.create(path("media_src.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 emptyGroup = p.addGroup("Vuoto");
        p.addGroup("Davvero vuoto");
        const QByteArray pdfBytes = makePdf({{"Primo paragrafo sul lavoro"}, {"Pagina due"}});
        const QByteArray png = encodeImage(twoColorImage(100, 60), "PNG");
        const QByteArray jpg = encodeImage(twoColorImage(40, 30), "JPEG");
        const qint64 text = p.addDocument(g, "Testo", "un testo da codificare");
        const qint64 pdf = p.addPdfDocument(g, "Rapporto", pdfBytes, "Primo paragrafo sul lavoro\fPagina due");
        const qint64 img = p.addImageDocument(g, "Foto", png);
        const qint64 img2 = p.addImageDocument(emptyGroup, "Scatto", jpg);
        const qint64 a = p.addCode(0, "A", Qt::red);
        const qint64 b = p.addCode(0, "B", Qt::green);
        const qint64 pdfArea = p.addAreaSegment(pdf, a, 2, 72, 90, 200, 120, 3, "nota area", {}, "Pagina due");
        p.addAreaSegment(pdf, b, 1, 10, 20, 110, 220);
        const qint64 imgArea = p.addAreaSegment(img, a, 0, 10, 5, 60, 40, 0, "commento");
        p.addAreaSegment(img2, b, 0, 1, 2, 30, 20);
        p.setVariableValue(img, p.addVariable("Anno", VariableType::Integer), "2020");
        p.addLink(text, 0, 2, pdf, 0, 7, "legame");
        p.addDocumentSet("Visuali", {pdf, img});
        // Memos of groups and sets (several per element, and links from a memo to a group).
        Memo gm;
        gm.targetType = MemoTarget::Group;
        gm.targetId = emptyGroup;
        gm.title = "Gruppo";
        gm.body = "memo del gruppo";
        const qint64 gmId = p.saveMemo(gm);
        Memo gm2 = gm;
        gm2.title = "Altro";
        gm2.body = "secondo memo";
        gm2.targetType = MemoTarget::Code;
        gm2.targetId = a;
        const qint64 gm2Id = p.saveMemo(gm2);
        QVERIFY(p.addMemoLink(gm2Id, MemoTarget::Group, emptyGroup));
        QVERIFY(p.addMemoLink(gm2Id, MemoTarget::DocumentSet, p.documentSets().first().id));
        Q_UNUSED(gmId);
        Q_UNUSED(pdfArea);
        Q_UNUSED(imgArea);

        QString err, report;
        QVERIFY2(Qdpx::exportProject(p, path("media.qdpx"), &err), qPrintable(err));
        // The files travel inside the archive with the guid as name.
        QZipReader zip(path("media.qdpx"));
        QVERIFY(!zip.fileData("sources/" + p.document(pdf).uuid.toUtf8() + ".pdf").isEmpty());
        QVERIFY(!zip.fileData("sources/" + p.document(img2).uuid.toUtf8() + ".jpg").isEmpty());
        QVERIFY(zip.fileData("project.qde").contains("<PDFSelection"));
        QVERIFY(zip.fileData("project.qde").contains("<PictureSelection"));

        Project q;
        QVERIFY(q.create(path("media_dst.tsra")));
        QVERIFY2(Qdpx::importProject(q, path("media.qdpx"), &report, &err), qPrintable(err));
        QVERIFY2(!report.contains("ignorat"), qPrintable(report));
        QCOMPARE(q.documents().size(), 4);
        auto byName = [&](const QString &name) {
            for (const Document &d : q.documents())
                if (d.name == name)
                    return d;
            return Document();
        };
        const Document qpdf = byName("Rapporto"), qimg = byName("Foto"), qimg2 = byName("Scatto");
        QCOMPARE(qpdf.kind, DocumentKind::Pdf);
        QCOMPARE(qpdf.uuid, p.document(pdf).uuid);
        QCOMPARE(q.documentData(qpdf.id), pdfBytes);
        QCOMPARE(q.documentText(qpdf.id), p.documentText(pdf));
        QCOMPARE(qimg.kind, DocumentKind::Image);
        QCOMPARE(q.documentData(qimg.id), png);
        QCOMPARE(q.documentData(qimg2.id), jpg);
        QCOMPARE(q.documentImage(qimg2.id).size(), QSize(40, 30));
        QCOMPARE(q.groups().size(), 3); // empty groups survive too (the default one was replaced)
        QCOMPARE(q.groups().last().name, QString("Davvero vuoto"));
        QCOMPARE(q.document(qimg2.id).groupId, q.groups().at(1).id);

        // Areas: coordinates, page, comment, selected text and code.
        QList<Segment> pdfSegs;
        for (const Segment &s : q.segmentsForDocument(qpdf.id, true))
            pdfSegs << s;
        QCOMPARE(pdfSegs.size(), 2);
        int areas = 0;
        for (const Segment &s : pdfSegs) {
            if (s.kind == SegmentKind::Text) {
                QCOMPARE(s.start, 0);
                QCOMPARE(s.end, 7);
            } else if (s.page == 2) {
                ++areas;
                QCOMPARE(s.x1, 72.0);
                QCOMPARE(s.y2, 120.0);
                QCOMPARE(s.comment, QString("nota area"));
                QCOMPARE(s.areaText, QString("Pagina due"));
                QCOMPARE(q.code(s.codeId).name, QString("A"));
            }
        }
        QCOMPARE(areas, 1);
        const QList<Segment> imgSegs = q.segmentsForDocument(qimg.id, true);
        QCOMPARE(imgSegs.size(), 1);
        QCOMPARE(imgSegs.first().kind, SegmentKind::Area);
        QCOMPARE(imgSegs.first().page, 0);
        QCOMPARE(imgSegs.first().x1, 10.0);
        QCOMPARE(imgSegs.first().y1, 5.0);
        QCOMPARE(imgSegs.first().x2, 60.0);
        QCOMPARE(imgSegs.first().y2, 40.0);
        QCOMPARE(imgSegs.first().comment, QString("commento"));
        QCOMPARE(imgSegs.first().uuid, p.segment(imgArea).uuid);
        QCOMPARE(q.allVariableValues().value(qimg.id).size(), 1);

        // Link to the PDF text, sets, and memos of groups and sets.
        QCOMPARE(q.links().size(), 1);
        QCOMPARE(q.documentSets().size(), 1);
        QCOMPARE(q.documentSetMembers(q.documentSets().first().id).size(), 2);
        qint64 qEmpty = 0;
        for (const DocumentGroup &x : q.groups())
            if (x.name == "Vuoto")
                qEmpty = x.id;
        QVERIFY(qEmpty);
        QCOMPARE(q.memos().size(), 2);
        QCOMPARE(q.memoFor(MemoTarget::Group, qEmpty).body, QString("memo del gruppo"));
        Memo second;
        for (const Memo &m : q.memos())
            if (m.title == "Altro")
                second = m;
        QVERIFY(second.id);
        QVERIFY(q.memoLinks(second.id).contains(MemoLink{MemoTarget::Group, qEmpty}));
        QVERIFY(q.memoLinks(second.id).contains(MemoLink{MemoTarget::DocumentSet, q.documentSets().first().id}));
        QCOMPARE(q.memoLinks(second.id).size(), 3);

        // Exporting the imported project again keeps everything.
        QVERIFY2(Qdpx::exportProject(q, path("media2.qdpx"), &err), qPrintable(err));
        Project r;
        QVERIFY(r.create(path("media_dst2.tsra")));
        QVERIFY2(Qdpx::importProject(r, path("media2.qdpx"), &report, &err), qPrintable(err));
        QCOMPARE(r.documents().size(), 4);
        QCOMPARE(r.allSegments(true).size(), p.allSegments(true).size());
        QCOMPARE(r.memos().size(), 2);
    }

    void qdpxForeignMediaSources()
    {
        const QByteArray png = encodeImage(twoColorImage(50, 40), "PNG");
        const QString xml = QStringLiteral(
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<Project xmlns=\"urn:QDA-XML:project:1.0\" name=\"Altro\" origin=\"Altro software 1.0\">"
            "<CodeBook><Codes><Code guid=\"44444444-4444-4444-8444-444444444444\" name=\"K\" isCodable=\"true\"/>"
            "</Codes></CodeBook><Sources>"
            "<PictureSource guid=\"11111111-1111-4111-8111-111111111111\" name=\"Foto\" path=\"internal://pic.png\">"
            "<PictureSelection guid=\"55555555-5555-4555-8555-555555555555\" name=\"etichetta\""
            " firstX=\"30\" firstY=\"20\" secondX=\"5\" secondY=\"2\">"
            "<Coding guid=\"66666666-6666-4666-8666-666666666666\">"
            "<CodeRef targetGUID=\"44444444-4444-4444-8444-444444444444\"/></Coding></PictureSelection>"
            "<PictureSelection guid=\"55555555-5555-4555-8555-555555555556\""
            " firstX=\"1\" firstY=\"1\" secondX=\"1\" secondY=\"9\"/>"
            "</PictureSource>"
            "<PictureSource guid=\"11111111-1111-4111-8111-111111111112\" name=\"Mancante\" path=\"internal://no.png\"/>"
            "<PDFSource guid=\"11111111-1111-4111-8111-111111111113\" name=\"PdfSolotesto\" path=\"internal://no.pdf\">"
            "<Representation guid=\"77777777-7777-4777-8777-777777777777\">"
            "<PlainTextContent>solo il testo</PlainTextContent></Representation></PDFSource>"
            "<AudioSource guid=\"11111111-1111-4111-8111-111111111114\" name=\"Voce\" path=\"internal://v.mp3\"/>"
            "<VideoSource guid=\"11111111-1111-4111-8111-111111111115\" name=\"Film\" path=\"internal://f.mp4\"/>"
            "<VideoSource guid=\"11111111-1111-4111-8111-111111111116\" name=\"Film2\" path=\"internal://g.mp4\"/>"
            "</Sources><Cases><Case guid=\"88888888-8888-4888-8888-888888888888\" name=\"Caso\"/></Cases>"
            "<Graphs><Graph guid=\"99999999-9999-4999-8999-999999999999\" name=\"Mappa\"/></Graphs></Project>");
        {
            QZipWriter zip(path("foreign_media.qdpx"));
            zip.addFile("project.qde", xml.toUtf8());
            zip.addFile("sources/pic.png", png);
            zip.close();
        }
        Project p;
        QVERIFY(p.create(path("foreign_media.tsra")));
        QString err, report;
        QVERIFY2(Qdpx::importProject(p, path("foreign_media.qdpx"), &report, &err), qPrintable(err));
        QCOMPARE(p.documents().size(), 2); // picture + text-only PDF
        const Document foto = p.documents().first();
        QCOMPARE(foto.kind, DocumentKind::Image);
        QCOMPARE(p.documentData(foto.id), png);
        const QList<Segment> segs = p.segmentsForDocument(foto.id, true);
        QCOMPARE(segs.size(), 1);
        QCOMPARE(segs.first().x1, 5.0); // corners in any order
        QCOMPARE(segs.first().y2, 20.0);
        QCOMPARE(p.documents().last().kind, DocumentKind::Text);
        QCOMPARE(p.documentText(p.documents().last().id), QString("solo il testo"));
        // What was left out is reported.
        QVERIFY2(report.contains("1 audio"), qPrintable(report));
        QVERIFY2(report.contains("2 video"), qPrintable(report));
        QVERIFY2(report.contains("1 immagini"), qPrintable(report));
        QVERIFY2(report.contains("1 casi"), qPrintable(report));
        QVERIFY2(report.contains("Mappe concettuali: 1"), qPrintable(report)); // the empty graph became a map
        QVERIFY2(report.contains("1 selezioni"), qPrintable(report));
        QVERIFY2(report.contains("1 PDF senza file"), qPrintable(report));
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

    // ---------------------------------------------------------------- word frequencies (#18)

    void wordFrequenciesCountsAndFilters()
    {
        Project p;
        QVERIFY(p.create(path("words.tsra")));
        const qint64 g = p.groups().first().id;
        // Apostrophes and hyphens separate words; numbers are words; emoji are not.
        const qint64 d = p.addDocument(g, "D", QString::fromUtf8("L'azienda e dell'azienda: smart-working, 2024 e 2024! "
                                                                 "\xF0\x9F\x98\x80 Azienda"));
        const QList<Words::Source> src = Words::documentSources({d});
        const QHash<qint64, QString> texts = Words::loadTexts(p, src);

        Words::Result r = Words::frequencies(src, texts, {});
        QCOMPARE(r.totalWords, 11);
        QCOMPARE(r.countedWords, 11);
        QCOMPARE(r.documents, 1);
        QCOMPARE(r.entries.size(), 7);
        QCOMPARE(r.entries[0].word, QString("azienda")); // case folded: "Azienda" counts too
        QCOMPARE(r.entries[0].count, 3);
        QCOMPARE(r.entries[1].word, QString("2024")); // ties alphabetically
        QCOMPARE(r.entries[2].word, QString("e"));
        QVERIFY(qAbs(r.percent(r.entries[0]) - 100.0 * 3 / 11) < 1e-9);
        QCOMPARE(r.indexOf("working"), 6);
        QCOMPARE(r.entries[0].documents, 1);

        Words::Options o;
        o.caseSensitive = true;
        r = Words::frequencies(src, texts, o);
        QCOMPARE(r.entries[r.indexOf("azienda")].count, 2);
        QCOMPARE(r.entries[r.indexOf("Azienda")].count, 1);

        // Minimum length, numbers and stop words reduce the counted words, not the total.
        o = {};
        o.minLength = 3;
        o.excludeNumbers = true;
        o.stopWords = {Words::stopWord("Dell'")};
        r = Words::frequencies(src, texts, o);
        QCOMPARE(r.totalWords, 11);
        QCOMPARE(r.countedWords, 5); // azienda x3, smart, working
        QCOMPARE(r.entries.size(), 3);
        o.minLength = 6;
        QCOMPARE(Words::frequencies(src, texts, o).entries.size(), 2); // azienda, working

        // Cancelling from the progress callback stops the computation.
        r = Words::frequencies(src, texts, {}, {}, [](int, int) { return false; });
        QVERIFY(!r.complete);
    }

    void wordFrequenciesColumnsAndSources()
    {
        Project p;
        QVERIFY(p.create(path("words-cols.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 a = p.addDocument(g, "A", "casa casa lavoro");
        const qint64 b = p.addDocument(g, "B", "lavoro lavoro lavoro\ncasa");
        const QList<Words::Source> src = Words::documentSources({a, b});
        const QHash<qint64, QString> texts = Words::loadTexts(p, src);
        Words::Result r = Words::frequencies(src, texts, {}, {{"Primo", {a}}, {"Secondo", {b}}, {"Tutti", {a, b}}});
        QCOMPARE(r.columnLabels, QStringList({"Primo", "Secondo", "Tutti"}));
        QCOMPARE(r.columnWords, QList<int>({3, 4, 7}));
        const Words::Entry &lavoro = r.entries[r.indexOf("lavoro")];
        QCOMPARE(lavoro.count, 4);
        QCOMPARE(lavoro.documents, 2);
        QCOMPARE(lavoro.columns, QList<int>({1, 3, 4}));

        // Segments: overlapping or touching passages of a document merge, so nothing is counted twice.
        auto seg = [&](qint64 doc, int s, int e) {
            Segment x;
            x.documentId = doc;
            x.start = s;
            x.end = e;
            return x;
        };
        const QList<Words::Source> merged = Words::segmentSources({seg(b, 0, 5), seg(a, 20, 25), seg(a, 0, 4), seg(a, 3, 9), seg(a, 9, 12)});
        QCOMPARE(merged.size(), 3);
        QCOMPARE(merged[0].documentId, a);
        QCOMPARE(merged[0].start, 0);
        QCOMPARE(merged[0].end, 12); // [0,4) + [3,9) + [9,12) are one passage
        QCOMPARE(merged[1].start, 20);
        r = Words::frequencies(Words::segmentSources({seg(a, 0, 8), seg(b, 0, 20)}), texts, {});
        QCOMPARE(r.totalWords, 2 + 3); // "casa cas" (cut mid-word) and "lavoro lavoro lavoro"
        QCOMPARE(r.documents, 2);
    }

    void keywordInContextOccurrences()
    {
        Project p;
        QVERIFY(p.create(path("kwic.tsra")));
        const qint64 g = p.groups().first().id;
        const QString t1 = "Il lavoro da casa e il Lavoro in ufficio.\nUn lavoro da remoto.";
        const qint64 d1 = p.addDocument(g, "D1", t1);
        const qint64 d2 = p.addDocument(g, "D2", "niente");
        const QList<Words::Source> src = Words::documentSources({d1, d2});
        const QHash<qint64, QString> texts = Words::loadTexts(p, src);

        QList<Words::Occurrence> occ = Words::keywordInContext(src, texts, "lavoro", false, 8);
        QCOMPARE(occ.size(), 3);
        QCOMPARE(occ[0].keyword, QString("lavoro"));
        QCOMPARE(occ[0].left, QString("Il "));
        QCOMPARE(occ[0].right, QString(" da casa"));
        QCOMPARE(t1.mid(occ[0].start, occ[0].end - occ[0].start), QString("lavoro"));
        QCOMPARE(occ[1].keyword, QString("Lavoro"));
        QCOMPARE(occ[2].left, QString("cio. Un ")); // the line break becomes a space
        QCOMPARE(Words::keywordInContext(src, texts, "lavoro", true, 8).size(), 2);
        // More words in a row, across punctuation; apostrophes in the query separate words.
        occ = Words::keywordInContext(src, texts, "lavoro da", false, 5);
        QCOMPARE(occ.size(), 2);
        QCOMPARE(occ[1].keyword, QString("lavoro da"));
        QCOMPARE(Words::keywordInContext(src, texts, "lavoro", false, 8, 2).size(), 2); // limit
        QVERIFY(Words::keywordInContext(src, texts, "...", false).isEmpty());
        // A passage limits the search, but not the context around a hit.
        Words::Source onlyFirst{d1, 0, 14};
        occ = Words::keywordInContext({onlyFirst}, texts, "lavoro", false, 4);
        QCOMPARE(occ.size(), 1);
        QCOMPARE(occ[0].right, QString(" da "));
    }

    void wordStopLists()
    {
        const QStringList it = Words::builtInStopWords("it");
        QVERIFY(it.contains("dell") && it.contains("che") && it.contains("è") && !it.contains("lavoro"));
        QVERIFY(Words::builtInStopWords("en").contains("the"));
        QCOMPARE(Words::stopWord("  Dell' "), QString("dell"));
        QCOMPARE(Words::stopWord("due parole"), QString());
        QCOMPARE(Words::stopWord("..."), QString());

        {
            Project p;
            QVERIFY(p.create(path("stop.tsra")));
            QCOMPARE(Words::stopList(p), it); // never set: the Italian list
            Words::setStopList(p, {"Zeta", "alfa", "ALFA", "x y", ""});
            QCOMPARE(Words::stopList(p), QStringList({"alfa", "zeta"}));
            Words::setStopList(p, {});
            QVERIFY(Words::stopList(p).isEmpty()); // an empty list is not "never set"
            Words::setStopList(p, {"beta"});
        }
        Project q;
        QVERIFY(q.open(path("stop.tsra")));
        QCOMPARE(Words::stopList(q), QStringList({"beta"}));
    }

    void wordsAddedToDictionary()
    {
        Project p;
        QVERIFY(p.create(path("words-dict.tsra")));
        const DictionaryFixture f = makeDictionaryProject(p);
        const qint64 undoable = p.dictEntries(f.tempo).size();
        QCOMPARE(Words::addToDictionary(p, f.tempo, {"orologio", "tempo", "Orologio", "scadenza"}), 2);
        QCOMPARE(p.dictEntries(f.tempo).size(), undoable + 2);
        QVERIFY(p.undo()); // one step
        QCOMPARE(p.dictEntries(f.tempo).size(), undoable);
    }

    // ------------------------------------------------------------------ image documents (#27)

    void imageDocumentRoundTrip()
    {
        const QImage source = twoColorImage(100, 60);
        QVERIFY(source.save(path("foto.png")));
        QVERIFY(Images::isImageFile("x/FOTO.JPG"));
        QVERIFY(!Images::isImageFile("x/nota.txt"));
        QVERIFY(Importers::fileDialogFilter().contains("*.webp"));

        QString error;
        const QByteArray bytes = Images::readFile(path("foto.png"), &error);
        QVERIFY2(!bytes.isEmpty(), qPrintable(error));

        const QString file = path("img.tsra");
        qint64 doc = 0, code = 0, area = 0;
        {
            Project p;
            QVERIFY(p.create(file));
            code = p.addCode(0, "Tema", Qt::green);
            p.clearUndoHistory();
            doc = p.addImageDocument(p.groups().first().id, "Foto", bytes, path("foto.png"));
            QVERIFY(doc > 0);
            QCOMPARE(p.document(doc).kind, DocumentKind::Image);
            QCOMPARE(p.documentData(doc), bytes);
            QCOMPARE(p.documentImage(doc).size(), QSize(100, 60));
            QCOMPARE(p.documentText(doc), QString());

            area = p.addAreaSegment(doc, code, 0, 10, 5, 60, 40, 0, "commento");
            QVERIFY(area > 0);

            // Renaming does not touch the content; the whole import is one undo step.
            QVERIFY(p.renameDocument(doc, "Altra foto"));
            QCOMPARE(p.documentData(doc), bytes);
            QVERIFY(p.undo()); // rename
            QVERIFY(p.undo()); // area
            QVERIFY(p.undo()); // import
            QCOMPARE(p.documents().size(), 0);
            QVERIFY(p.documentData(doc).isEmpty());
            QVERIFY(p.redo());
            QCOMPARE(p.documentData(doc), bytes);
            QVERIFY(p.redo());
            QVERIFY(p.redo());

            // Deleting the document removes the blob; undo brings it back.
            QVERIFY(p.deleteDocument(doc));
            QVERIFY(p.documentData(doc).isEmpty());
            QVERIFY(p.undo());
            QCOMPARE(p.documentData(doc), bytes);
            QCOMPARE(p.segment(area).comment, QString("commento"));

            QVERIFY(p.saveCopyAs(path("img-copia.tsra")));
        }
        for (const QString &name : {QString("img.tsra"), QString("img-copia.tsra")}) {
            Project p;
            QVERIFY(p.open(path(name)));
            QCOMPARE(p.documentData(doc), bytes);
            const Segment s = p.segment(area);
            QCOMPARE(s.kind, SegmentKind::Area);
            QCOMPARE(s.x1, 10.0);
            QCOMPARE(s.y2, 40.0);
            // The crop is the area of the image, in the original pixels.
            const QImage crop = Images::crop(p.documentImage(doc), s);
            QCOMPARE(crop.size(), QSize(50, 35));
            QCOMPARE(crop.pixelColor(0, 0), QColor(Qt::red));
            QCOMPARE(crop.pixelColor(49, 34), QColor(Qt::blue));
        }
    }

    void imageFormatsAndErrors()
    {
        const QImage img = twoColorImage(40, 20);
        for (const char *format : {"PNG", "JPEG", "BMP"}) {
            const QString name = QString("f.%1").arg(QString(format).toLower());
            writeFile(name, encodeImage(img, format));
            QString error;
            QVERIFY2(!Images::readFile(path(name), &error).isEmpty(), qPrintable(name + ": " + error));
        }
        // TIFF and WebP depend on the plugins Qt ships; when the writer is there, the reader must be.
        for (const char *format : {"TIFF", "WEBP"}) {
            if (!QImageWriter::supportedImageFormats().contains(format))
                continue;
            const QString name = QString("f.%1").arg(QString(format).toLower());
            writeFile(name, encodeImage(img, format));
            QString error;
            QVERIFY2(!Images::readFile(path(name), &error).isEmpty(), qPrintable(name + ": " + error));
        }
        // Not an image, though named like one; a missing file.
        writeFile("falso.png", "questo non e' un PNG");
        QString error;
        QVERIFY(Images::readFile(path("falso.png"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(Images::readFile(path("assente.png"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void imageExifOrientationIsAppliedOnce()
    {
        // Stored 40x20, red left / blue right, with Orientation 6 (rotate 90 degrees clockwise to display).
        const QByteArray jpeg = jpegWithOrientation(twoColorImage(40, 20), 6);
        writeFile("exif.jpg", jpeg);
        QString error;
        QVERIFY2(!Images::readFile(path("exif.jpg"), &error).isEmpty(), qPrintable(error));

        Project p;
        QVERIFY(p.create(path("exif.tsra")));
        const qint64 doc = p.addImageDocument(p.groups().first().id, "Ruotata", jpeg);
        QCOMPARE(p.documentData(doc), jpeg); // stored as it came
        const QImage upright = p.documentImage(doc);
        QCOMPARE(upright.size(), QSize(20, 40));
        // Upright: red on top, blue below, so area coordinates and crops refer to what the user sees.
        QVERIFY(qRed(upright.pixel(10, 5)) > 200 && qBlue(upright.pixel(10, 5)) < 80);
        QVERIFY(qBlue(upright.pixel(10, 35)) > 200 && qRed(upright.pixel(10, 35)) < 80);
        // An area covering the lower half is in the upright frame: it is the blue part.
        const qint64 code = p.addCode(0, "Tema", Qt::red);
        const qint64 a = p.addAreaSegment(doc, code, 0, 0, 22, 20, 40);
        QVERIFY(a > 0);
        const QImage crop = Images::crop(upright, p.segment(a));
        QCOMPARE(crop.size(), QSize(20, 18));
        QVERIFY(qBlue(crop.pixel(5, 5)) > 200);
        // Areas outside the image give nothing.
        Segment far;
        far.x1 = 500;
        far.y1 = 500;
        far.x2 = 600;
        far.y2 = 600;
        QVERIFY(Images::crop(upright, far).isNull());
    }

    void imageAreasInExports()
    {
        Project p;
        QVERIFY(p.create(path("export-img.tsra")));
        const QImage img = twoColorImage(80, 40);
        const qint64 g = p.groups().first().id;
        const qint64 doc = p.addImageDocument(g, "Foto", encodeImage(img, "PNG"));
        const qint64 text = p.addDocument(g, "Testo", "Una frase da codificare");
        const qint64 code = p.addCode(0, "Tema", QColor("#336699"));
        const qint64 a = p.addAreaSegment(doc, code, 0, 0, 0, 40, 40, 3, "metà rossa");
        QVERIFY(a > 0);
        QVERIFY(p.addSegment(text, code, 0, 3) > 0);

        // Text-based lists still skip the area; the explicit ones include it.
        QCOMPARE(p.allSegments().size(), 1);
        const QList<Segment> all = p.allSegments(true);
        QCOMPARE(all.size(), 2);

        const Exporters::Table table = Exporters::segmentsTable(p, all);
        QCOMPARE(table.size(), 3);
        QStringList areaRow;
        for (const QStringList &row : table)
            if (row.value(1) == "Foto")
                areaRow = row;
        QVERIFY(!areaRow.isEmpty());
        QVERIFY(areaRow.last().contains("40 × 40"));
        QCOMPARE(areaRow.value(6), QString("metà rossa"));

        const QString html = path("segmenti-img.html");
        QString error;
        QVERIFY2(Exporters::exportSegmentsHtml(p, all, html, &error), qPrintable(error));
        QFile f(html);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(f.readAll());
        QVERIFY(content.contains("class=\"text\">Una</div>"));
        const QRegularExpression re("src=\"data:image/png;base64,([A-Za-z0-9+/=]+)\"");
        const QRegularExpressionMatch m = re.match(content);
        QVERIFY2(m.hasMatch(), "the cropped area must be embedded as an image");
        QImage embedded;
        QVERIFY(embedded.loadFromData(QByteArray::fromBase64(m.captured(1).toLatin1())));
        QCOMPARE(embedded.size(), QSize(40, 40));
        QCOMPARE(embedded.pixelColor(20, 20), QColor(Qt::red));

        // The workbook export has the area too, and the memo label of an area is readable.
        const QList<Xlsx::Sheet> sheets = Exporters::projectSheets(p);
        bool found = false;
        for (const Xlsx::Sheet &s : sheets)
            for (const QStringList &row : s.rows)
                found = found || row.contains("metà rossa");
        QVERIFY(found);
        QVERIFY(Exporters::memoTargetLabel(p, MemoTarget::Segment, a).contains("40 × 40"));
    }

    void imageDocumentsMigrateFromV6()
    {
        const QString file = path("v6-img.tsra");
        {
            Project old;
            old.setMigrations(Project::builtInMigrations().mid(0, 5)); // up to schema 6
            QVERIFY(old.create(file));
            QCOMPARE(old.addMediaDocument(old.groups().first().id, "Foto", DocumentKind::Image) > 0, true);
        }
        QCOMPARE(fileVersion(file), 6);
        Project p;
        QVERIFY(p.open(file));
        QCOMPARE(p.schemaVersion(), 12);
        QCOMPARE(fileVersion(file), 12);
        const qint64 doc = p.documents().first().id;
        QVERIFY(p.setDocumentData(doc, encodeImage(twoColorImage(10, 10), "PNG")));
        QCOMPARE(p.documentImage(doc).size(), QSize(10, 10));
    }

    void pdfTextAndGeometry()
    {
        const QByteArray bytes = makePdf({{"Primo paragrafo sul lavoro", "Secondo paragrafo sulla casa"}, {"Pagina due"}});
        writeFile("doc.pdf", bytes);
        QString error;
        QVERIFY2(!Pdfs::readFile(path("doc.pdf"), &error).isEmpty(), qPrintable(error));
        writeFile("rotto.pdf", "questo non e' un PDF");
        QVERIFY(Pdfs::readFile(path("rotto.pdf"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(Pdfs::isPdfFile("X.PDF") && !Pdfs::isPdfFile("x.png"));

        Pdfs::Doc doc;
        QVERIFY(doc.load(bytes));
        QCOMPARE(doc.pageCount(), 2);
        QVERIFY(qAbs(doc.pageSize(1).width() - 595) < 3 && qAbs(doc.pageSize(1).height() - 842) < 3);
        QVERIFY(doc.pageText(1).contains("Primo paragrafo sul lavoro"));
        QVERIFY(doc.pageText(1).contains("Secondo paragrafo sulla casa"));
        QCOMPARE(doc.pageText(2), QString("Pagina due"));
        QVERIFY(doc.pageText(3).isEmpty());

        // Dragging over the first line selects it; coordinates are points from the top left of the page.
        const Pdfs::Doc::Selection sel = doc.select(1, QPointF(60, 80), QPointF(500, 108));
        QVERIFY2(sel.text.contains("Primo paragrafo sul lavoro"), qPrintable(sel.text));
        QVERIFY(!sel.text.contains("Secondo"));
        QVERIFY2(sel.rect.top() > 90 && sel.rect.bottom() < 120 && sel.rect.left() > 70 && sel.rect.left() < 95,
                 qPrintable(QString("%1,%2 %3x%4").arg(sel.rect.left()).arg(sel.rect.top()).arg(sel.rect.width()).arg(sel.rect.height())));

        // The text inside a rectangle, with the lines in order and line breaks kept.
        QCOMPARE(doc.textInArea(1, QRectF(60, 80, 480, 28)), QString("Primo paragrafo sul lavoro"));
        const QString both = doc.textInArea(1, QRectF(60, 80, 480, 70));
        QVERIFY2(both.contains("lavoro\nSecondo") || both.contains("lavoro Secondo"), qPrintable(both));
        QVERIFY(doc.textInArea(1, QRectF(400, 400, 50, 50)).isEmpty());
        QVERIFY(doc.textInArea(2, QRectF(60, 80, 480, 28)).contains("Pagina due"));

        // Rendering and cropping an area.
        const QImage page = doc.render(1, QSize(595, 842));
        QCOMPARE(page.size(), QSize(595, 842));
        Segment area;
        area.page = 1;
        area.x1 = 60;
        area.y1 = 80;
        area.x2 = 300;
        area.y2 = 110;
        const QImage crop = doc.cropArea(area, 480, 100);
        QVERIFY(!crop.isNull() && crop.width() <= 480 && crop.height() <= 100);
        bool dark = false; // the crop holds the glyphs, not a blank page
        for (int y = 0; y < crop.height() && !dark; ++y)
            for (int x = 0; x < crop.width() && !dark; ++x)
                dark = qGray(crop.pixel(x, y)) < 100;
        QVERIFY(dark);
        area.page = 9;
        QVERIFY(doc.cropArea(area, 100, 100).isNull());

        // The content kept for search: pages separated by a form feed.
        const QString content = Pdfs::joinPages(Pdfs::pageTexts(doc));
        QCOMPARE(content.count(QLatin1Char('\f')), 1);
        QCOMPARE(Pdfs::pageOfOffset(content, 3), 1);
        QCOMPARE(Pdfs::pageOfOffset(content, int(content.indexOf("Pagina due"))), 2);
    }

    void pdfDocumentsAndAreasPersist()
    {
        const QByteArray bytes = makePdf({{"Primo paragrafo sul lavoro", "Secondo paragrafo sulla casa"}, {"Pagina due"}});
        const QString file = path("pdf.tsra");
        qint64 doc = 0, code = 0, textArea = 0, rectArea = 0;
        {
            Project p;
            QVERIFY(p.create(file));
            Pdfs::Doc d;
            QVERIFY(d.load(bytes));
            doc = p.addPdfDocument(p.groups().first().id, "Rapporto", bytes, Pdfs::joinPages(Pdfs::pageTexts(d)), "rapporto.pdf");
            QVERIFY(doc > 0);
            QCOMPARE(p.document(doc).kind, DocumentKind::Pdf);
            QCOMPARE(p.documentData(doc), bytes);
            QVERIFY(p.documentText(doc).contains("Pagina due")); // searchable
            code = p.addCode(0, "Lavoro", Qt::red);
            textArea = p.addAreaSegment(doc, code, 1, 72, 88, 330, 106, 0, {}, {}, "Primo paragrafo sul lavoro");
            rectArea = p.addAreaSegment(doc, code, 2, 60, 80, 300, 110);
            QVERIFY(textArea > 0 && rectArea > 0);
            QVERIFY(p.addSegment(doc, code, 0, 3) == 0); // no text segments on a PDF
            QCOMPARE(p.allSegments().size(), 0);
            QCOMPARE(p.allSegments(true).size(), 2);
            // A PDF area needs a page, an image area must not have one.
            QCOMPARE(p.addAreaSegment(doc, code, 0, 1, 1, 5, 5), qint64(0));
            // Coding the same rectangle again gives the same coding.
            QCOMPARE(p.addAreaSegment(doc, code, 1, 72, 88, 330, 106), textArea);

            // Undo takes the whole import away, with its blob and extracted text.
            QVERIFY(p.undo()); // rectArea
            QVERIFY(p.undo()); // textArea
            QCOMPARE(p.allSegments(true).size(), 0);
            QVERIFY(p.redo() && p.redo());
            QCOMPARE(p.allSegments(true).size(), 2);
        }
        Project p;
        QVERIFY(p.open(file));
        QCOMPARE(p.documentData(doc), bytes);
        const Segment t = p.segment(textArea);
        QCOMPARE(t.kind, SegmentKind::Area);
        QCOMPARE(t.page, 1);
        QCOMPARE(t.areaText, QString("Primo paragrafo sul lavoro"));
        QCOMPARE(QRectF(t.x1, t.y1, t.x2 - t.x1, t.y2 - t.y1), QRectF(72, 88, 258, 18));
        QCOMPARE(p.segment(rectArea).areaText, QString());

        // Lists, tables and the HTML export show the text, or the cropped page when there is none.
        const QList<Segment> all = p.allSegments(true);
        const Exporters::Table table = Exporters::segmentsTable(p, all);
        QStringList cells;
        for (const QStringList &row : table)
            cells << row.last();
        QVERIFY(cells.contains("Primo paragrafo sul lavoro"));
        QVERIFY(cells.join("|").contains("pagina 2"));
        QString error;
        const QString html = path("pdf.html");
        QVERIFY2(Exporters::exportSegmentsHtml(p, all, html, &error), qPrintable(error));
        QFile f(html);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(f.readAll());
        QVERIFY(content.contains("Primo paragrafo sul lavoro"));
        QCOMPARE(content.count("data:image/png;base64,"), 2);
        QVERIFY(Exporters::memoTargetLabel(p, MemoTarget::Segment, textArea).contains("Primo paragrafo"));

        // The extracted text takes part in the analyses that read documentText, but a PDF is not coded as text.
        QVERIFY(p.documentText(doc).contains("Secondo paragrafo"));
        const qint64 dict = p.addDictionary("Lessico");
        const qint64 cat = p.addDictCategory(dict, 0, "Lavoro");
        QVERIFY(p.addDictEntry(cat, "lavoro") > 0);
        const Dictionaries::Analysis analysis = Dictionaries::analyze(p, dict);
        QCOMPARE(analysis.count(cat, doc), 1);
        const qsizetype before = p.allSegments(true).size();
        QCOMPARE(Dictionaries::autocode(p, analysis, {cat}, {}, Dictionaries::Extent::Word), 0);
        QCOMPARE(p.allSegments(true).size(), before);
    }

    void pdfDocumentsMigrateFromV7()
    {
        const QString file = path("v7-pdf.tsra");
        {
            Project old;
            old.setMigrations(Project::builtInMigrations().mid(0, 6)); // up to schema 7: no area text yet
            QVERIFY(old.create(file));
            QVERIFY(old.addMediaDocument(old.groups().first().id, "Rapporto", DocumentKind::Pdf) > 0);
        }
        QCOMPARE(fileVersion(file), 7);
        Project p;
        QVERIFY(p.open(file));
        QCOMPARE(fileVersion(file), 12);
        const qint64 doc = p.documents().first().id;
        const qint64 a = p.addAreaSegment(doc, p.addCode(0, "T", Qt::red), 1, 1, 1, 50, 50, 0, {}, {}, "testo");
        QVERIFY(a > 0);
        QCOMPARE(p.segment(a).areaText, QString("testo"));
    }
    void summariesPersistCascadeAndUndo()
    {
        const QString file = path("summaries.tsra");
        qint64 d1 = 0, d2 = 0, a = 0, b = 0;
        {
            Project p;
            QVERIFY(p.create(file));
            const qint64 g = p.groups().first().id;
            d1 = p.addDocument(g, "D1", "uno due tre");
            d2 = p.addDocument(g, "D2", "quattro");
            a = p.addCode(0, "A", Qt::red);
            b = p.addCode(0, "B", Qt::blue);
            QSignalSpy spy(&p, &Project::summariesChanged);
            QVERIFY(p.setSummary(d1, a, "prima"));
            QCOMPARE(spy.count(), 1);
            QCOMPARE(p.summary(d1, a).body, QString("prima"));
            QVERIFY(p.setSummary(d1, a, "prima")); // unchanged: nothing written
            QCOMPARE(spy.count(), 1);
            QVERIFY(p.setSummary(d1, b, "x"));
            QVERIFY(p.setSummary(d2, a, "y"));
            QCOMPARE(p.summaries().size(), 3);
            QCOMPARE(p.summaryCountForCode(a), 2);
            QCOMPARE(p.summaryCountForDocument(d1), 2);
        }
        {
            // Closed and reopened: the summaries are there.
            Project p;
            QVERIFY(p.open(file));
            QCOMPARE(p.summary(d1, a).body, QString("prima"));
            QCOMPARE(p.summary(d1, a).author, p.author());
            QVERIFY(p.summary(d1, a).modifiedAt.isValid());
            QVERIFY(!p.summary(d2, b).id);

            // A blank body deletes the summary.
            QVERIFY(p.setSummary(d2, a, " \n "));
            QVERIFY(!p.summary(d2, a).id);
            QVERIFY(p.setSummary(d2, a, "y")); // not an unchanged one any more
            QCOMPARE(p.summaries().size(), 3);

            // Deleting a code or a document deletes its summaries; undo brings them back.
            QVERIFY(p.deleteCode(b));
            QCOMPARE(p.summaries().size(), 2);
            QVERIFY(p.undo());
            QCOMPARE(p.summary(d1, b).body, QString("x"));
            QVERIFY(p.deleteDocument(d2));
            QCOMPARE(p.summaries().size(), 2);
            QVERIFY(p.undo());
            QCOMPARE(p.summary(d2, a).body, QString("y"));
        }
        {
            // Writing is one undo step: consecutive edits of one summary merge.
            Project p;
            QVERIFY(p.create(path("summary-undo.tsra")));
            const qint64 doc = p.addDocument(p.groups().first().id, "D", "testo");
            const qint64 code = p.addCode(0, "A", Qt::red);
            const qint64 other = p.addCode(0, "B", Qt::red);
            p.clearUndoHistory();
            p.setSummary(doc, code, "a");
            p.setSummary(doc, code, "ab");
            p.setSummary(doc, code, "abc");
            QCOMPARE(p.undoLabel(), QString("Sintesi"));
            QVERIFY(p.undo());
            QVERIFY(!p.canUndo()); // one step for all three
            QVERIFY(!p.summary(doc, code).id);
            QVERIFY(p.redo());
            QCOMPARE(p.summary(doc, code).body, QString("abc"));

            // Another cell, or any other change in between, starts a new step.
            p.setSummary(doc, other, "k");
            p.setSummary(doc, code, "abcd");
            QVERIFY(p.undo());
            QCOMPARE(p.summary(doc, code).body, QString("abc"));
            QCOMPARE(p.summary(doc, other).body, QString("k"));
            QVERIFY(p.undo());
            QVERIFY(!p.summary(doc, other).id);
            QCOMPARE(p.summary(doc, code).body, QString("abc"));
            p.setSummary(doc, code, "abcde");
            p.addCode(0, "C", Qt::green);
            p.setSummary(doc, code, "abcdef");
            QVERIFY(p.undo()); // only the last edit
            QCOMPARE(p.summary(doc, code).body, QString("abcde"));
        }
    }
    void summaryTablesAndExport()
    {
        Project p;
        QVERIFY(p.create(path("summary-tables.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d1 = p.addDocument(g, "Intervista 1", "uno");
        const qint64 d2 = p.addDocument(g, "Intervista 2", "due");
        const qint64 d3 = p.addDocument(g, "Intervista 3", "tre");
        const qint64 theme = p.addCode(0, "Tema", Qt::red);
        const qint64 sub = p.addCode(theme, "Sotto", Qt::blue);
        const qint64 age = p.addVariable("Eta", VariableType::Integer);
        p.setVariableValue(d1, age, "30");
        p.setVariableValue(d2, age, "45");
        p.setSummary(d1, theme, "Sintesi; con \"virgolette\"\ne a capo");
        p.setSummary(d2, sub, "solo sotto");
        const qint64 set = p.addDocumentSet("Metà", {d3, d1});

        Summaries::TableConfig cfg;
        cfg.documentIds = {d2, d1};
        cfg.codeIds = {sub, theme};
        cfg.variableIds = {age};
        // JSON round trip
        const Summaries::TableConfig back = Summaries::configFromJson(Summaries::configToJson(cfg));
        QCOMPARE(back.documentIds, cfg.documentIds);
        QCOMPARE(back.codeIds, cfg.codeIds);
        QCOMPARE(back.variableIds, cfg.variableIds);
        QCOMPARE(back.setId, qint64(0));

        // Rows follow the document tree order, columns the order of the codes, variables come first.
        Exporters::Table t = Summaries::buildTable(p, cfg);
        QCOMPARE(t.size(), 3);
        QCOMPARE(t[0], (QStringList{"Documento", "Eta", "Tema > Sotto", "Tema"}));
        QCOMPARE(t[1], (QStringList{"Intervista 1", "30", "", "Sintesi; con \"virgolette\"\ne a capo"}));
        QCOMPARE(t[2], (QStringList{"Intervista 2", "45", "solo sotto", ""}));

        // A saved table, regenerated when a summary changes.
        const qint64 id = p.addSummaryTable("Confronto", Summaries::configToJson(cfg));
        QVERIFY(id > 0);
        QCOMPARE(p.summaryTables().size(), 1);
        QCOMPARE(Summaries::buildTable(p, Summaries::configFromJson(p.summaryTable(id).configJson)), t);
        p.setSummary(d2, theme, "nuova");
        t = Summaries::buildTable(p, Summaries::configFromJson(p.summaryTable(id).configJson));
        QCOMPARE(t[2].last(), QString("nuova"));

        // Rows from a set; elements deleted since are skipped.
        cfg.setId = set;
        cfg.variableIds.clear();
        t = Summaries::buildTable(p, cfg);
        QCOMPARE(t.size(), 3);
        QCOMPARE(t[1].first(), QString("Intervista 1"));
        QCOMPARE(t[2].first(), QString("Intervista 3"));
        QCOMPARE(t[0].size(), 3);
        p.deleteCode(sub);
        QCOMPARE(Summaries::buildTable(p, cfg)[0], (QStringList{"Documento", "Tema"}));

        // Rename and delete.
        QVERIFY(p.updateSummaryTable(id, "Nuovo nome", Summaries::configToJson(cfg)));
        QCOMPARE(p.summaryTable(id).name, QString("Nuovo nome"));
        QCOMPARE(Summaries::configFromJson(p.summaryTable(id).configJson).setId, set);

        // Exports: CSV, HTML and a worksheet (summaries as wrapped text).
        cfg.setId = 0;
        cfg.codeIds = {theme};
        cfg.variableIds = {age};
        const Exporters::Table table = Summaries::buildTable(p, cfg);
        QString error;
        QVERIFY(Exporters::writeCsv(path("sum.csv"), table, &error));
        QCOMPARE(Exporters::readCsv(path("sum.csv"), &error), table);
        QVERIFY(Exporters::writeHtmlTable(path("sum.html"), "Confronto", table, &error));
        QFile html(path("sum.html"));
        QVERIFY(html.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(html.readAll()).contains("virgolette"));
        const Xlsx::Sheet sheet = Summaries::buildSheet(p, cfg, "Confronto");
        QVERIFY(sheet.wrapColumns.contains(2));
        QVERIFY(sheet.textColumns.contains(2));
        QVERIFY(!sheet.textColumns.contains(1)); // the variable keeps its number
        QVERIFY(Xlsx::writeWorkbook(path("sum.xlsx"), {sheet}, &error));
        const Xlsx::Table read = Xlsx::readFirstSheet(path("sum.xlsx"), &error);
        QCOMPARE(read.size(), table.size());
        QCOMPARE(read[1][2], table[1][2]);

        QVERIFY(p.deleteSummaryTable(id));
        QVERIFY(p.summaryTables().isEmpty());
    }
    void summariesMigrateFromV8()
    {
        const QString file = path("v8-summaries.tsra");
        {
            Project old;
            old.setMigrations(Project::builtInMigrations().mid(0, 7)); // up to schema 8
            QVERIFY(old.create(file));
            old.addCode(0, "A", Qt::red);
        }
        QCOMPARE(fileVersion(file), 8);
        Project p;
        QVERIFY(p.open(file));
        QCOMPARE(fileVersion(file), 12);
        const qint64 doc = p.addDocument(p.groups().first().id, "D", "testo");
        QVERIFY(p.setSummary(doc, p.codes().first().id, "ok"));
        QVERIFY(p.addSummaryTable("T", "{}") > 0);
    }    void wordCloudLayoutDoesNotOverlap()
    {
        QList<QSizeF> sizes;
        for (int i = 0; i < 40; ++i)
            sizes << QSizeF(30 + (i * 37) % 120, 12 + (i * 11) % 40);
        const QList<QPointF> centres = Words::cloudLayout(sizes, 2);
        QCOMPARE(centres.size(), sizes.size());
        QCOMPARE(centres[0], QPointF(0, 0)); // the first word sits in the middle
        for (int i = 0; i < sizes.size(); ++i)
            for (int j = i + 1; j < sizes.size(); ++j) {
                const QRectF a(centres[i] - QPointF(sizes[i].width() / 2, sizes[i].height() / 2), sizes[i]);
                const QRectF b(centres[j] - QPointF(sizes[j].width() / 2, sizes[j].height() / 2), sizes[j]);
                QVERIFY2(!a.intersects(b), qPrintable(QString("%1 and %2 overlap").arg(i).arg(j)));
            }
    }

    // ---- merging projects (issue #22)

    // Two coders start from the same project and work apart.
private:
    struct Pair {
        QString a, b;
        qint64 docA = 0, docB = 0;
    };
    Pair makeCoders(const QString &tag)
    {
        Pair pr;
        pr.a = path(tag + "-anna.tsra");
        pr.b = path(tag + "-bruno.tsra");
        Project base;
        base.setAuthor("Anna");
        base.create(pr.a);
        const qint64 g = base.groups().first().id;
        const qint64 doc = base.addDocument(g, "Intervista", "Il lavoro da remoto cambia tutto. Seconda frase.");
        const qint64 tema = base.addCode(0, "Tema", Qt::red);
        const qint64 sub = base.addCode(tema, "Sotto", Qt::blue);
        base.addSegment(doc, tema, 0, 5);
        base.addSegment(doc, sub, 9, 15, 0, "originale");
        Memo m;
        m.targetType = MemoTarget::Code;
        m.targetId = tema;
        m.title = "Tema";
        m.body = "Memo di Anna";
        base.saveMemo(m);
        base.saveCopyAs(pr.b);
        return pr;
    }

    static int countOf(const Merge::Report &r) { return r.changes(); }

private slots:
    void mergeTwoCoders()
    {
        const Pair pr = makeCoders("two");
        // Bruno's copy: a duplicate coding, a new one, a different comment on a shared one, a new code, a memo.
        {
            Project b;
            b.setAuthor("Bruno");
            QVERIFY(b.open(pr.b));
            const qint64 doc = b.documents().first().id;
            const qint64 tema = b.findCode(0, "Tema");
            const qint64 sub = b.findCode(tema, "Sotto");
            b.addSegment(doc, tema, 0, 5);                      // same coding as Anna's
            b.addSegment(doc, tema, 20, 32, 2, "di Bruno");     // new
            b.setSegmentComment(b.segments({doc}, {sub}).first().id, "secondo parere"); // other comment, same place
            const qint64 nuovo = b.addCode(tema, "Nuovo", Qt::green);
            b.addSegment(doc, nuovo, 34, 40);
            Memo m = b.memoFor(MemoTarget::Code, tema);
            m.body = "Memo di Bruno";
            b.saveMemo(m);
            Memo extra;
            extra.targetType = MemoTarget::Document;
            extra.targetId = doc;
            extra.body = "Su tutta l'intervista";
            b.saveMemo(extra);
            b.addLink(doc, 0, 5, doc, 20, 32, "legame");
            const qint64 age = b.addVariable("Età", VariableType::Integer);
            b.setVariableValue(doc, age, "41");
            b.setSummary(doc, tema, "Sintesi di Bruno");
            b.addDocument(b.groups().first().id, "Seconda intervista", "Un altro testo da codificare.");
            b.addDocumentSet("Gruppo di lavoro", {doc});
        }

        Project a;
        a.setAuthor("Anna");
        QVERIFY(a.open(pr.a));
        const QByteArray fileBefore = fileHash(pr.a);
        Q_UNUSED(fileBefore);
        Merge::Source src;
        QString error;
        QVERIFY2(src.load(pr.b, &error), qPrintable(error));
        QCOMPARE(src.authors(), (QStringList{"Anna", "Bruno"}));

        const int segsBefore = a.allSegments(true).size();
        const Merge::Report dry = Merge::analyze(a, src, {});
        QVERIFY(dry.ok);
        QCOMPARE(a.allSegments(true).size(), segsBefore); // a dry run writes nothing
        QCOMPARE(dry.documentsPresent, 1);
        QCOMPARE(dry.documentsNew, 1);
        QCOMPARE(dry.codesPresent, 2);
        QCOMPARE(dry.codesNew, 1);
        QCOMPARE(dry.segmentsDuplicate, 1);
        QCOMPARE(dry.segmentsNew, 2);
        QCOMPARE(dry.segmentsConflict, 1);
        QCOMPARE(dry.memosMerged, 1);
        QCOMPARE(dry.memosNew, 1);
        QCOMPARE(dry.linksNew, 1);
        QCOMPARE(dry.summariesNew, 1);
        QCOMPARE(dry.variablesNew, 1);
        QCOMPARE(dry.variableValuesSet, 1);
        QCOMPARE(dry.setsNew, 1);

        const int docsBefore = a.documents().size();
        const int codesBefore = a.codes().size();
        const Merge::Report r = Merge::apply(a, src, {});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.segmentsNew, dry.segmentsNew);
        QCOMPARE(r.segmentsConflict, dry.segmentsConflict);
        QCOMPARE(r.memosMerged, dry.memosMerged);
        QCOMPARE(a.documents().size(), docsBefore + 1);
        QCOMPARE(a.codes().size(), codesBefore + 1);
        QCOMPARE(a.allSegments(true).size(), segsBefore + 2);

        // No duplicates, and Bruno's work keeps his name.
        const qint64 doc = a.documents().first().id;
        const qint64 tema = a.findCode(0, "Tema");
        const QList<Segment> onTema = a.segments({doc}, {tema});
        QCOMPARE(onTema.size(), 2);
        bool foundBruno = false;
        for (const Segment &s : onTema)
            if (s.start == 20) {
                foundBruno = true;
                QCOMPARE(s.author, QString("Bruno"));
                QCOMPARE(s.weight, 2);
                QCOMPARE(s.comment, QString("di Bruno"));
            }
        QVERIFY(foundBruno);
        QCOMPARE(a.findCode(tema, "Nuovo") > 0, true);
        const qint64 sub = a.findCode(tema, "Sotto");
        const Segment shared = a.segments({doc}, {sub}).first();
        QVERIFY(shared.comment.contains("originale") && shared.comment.contains("secondo parere"));
        // Two different memos on the same code are concatenated, with the author in the separator.
        const Memo cm = a.memoFor(MemoTarget::Code, tema);
        QVERIFY(cm.body.contains("Memo di Anna") && cm.body.contains("Memo di Bruno"));
        QVERIFY(cm.body.contains("Bruno"));
        QCOMPARE(a.memoFor(MemoTarget::Document, doc).body, QString("Su tutta l'intervista"));
        QCOMPARE(a.memoFor(MemoTarget::Document, doc).author, QString("Bruno"));
        QCOMPARE(a.links().size(), 1);
        QCOMPARE(a.links().first().author, QString("Bruno"));
        QCOMPARE(a.summary(doc, tema).body, QString("Sintesi di Bruno"));
        QCOMPARE(a.summary(doc, tema).author, QString("Bruno"));
        QCOMPARE(a.variableValue(doc, a.variables().first().id), QString("41"));
        QCOMPARE(a.documentSetMembers(a.documentSets().first().id), QList<qint64>{doc});

        // Merging again finds nothing to add.
        const Merge::Report again = Merge::apply(a, src, {});
        QVERIFY(again.ok);
        QCOMPARE(countOf(again), 0);

        // The whole merge was one undo step (the second one changed nothing).
        QVERIFY(a.canUndo());
        QVERIFY(a.undo());
        QCOMPARE(a.allSegments(true).size(), segsBefore);
        QCOMPARE(a.documents().size(), docsBefore);
        QCOMPARE(a.codes().size(), codesBefore);
        QCOMPARE(a.memoFor(MemoTarget::Code, tema).body, QString("Memo di Anna"));
        QVERIFY(a.links().isEmpty());
        QVERIFY(a.variables().isEmpty());
        QVERIFY(a.documentSets().isEmpty());
        QCOMPARE(a.summary(doc, tema).id, qint64(0));
        QCOMPARE(a.segments({doc}, {sub}).first().comment, QString("originale"));
        QVERIFY(a.redo());
        QCOMPARE(a.allSegments(true).size(), segsBefore + 2);
    }

    void mergeTextConflict()
    {
        const Pair pr = makeCoders("conflict");
        {
            Project b;
            b.setAuthor("Bruno");
            QVERIFY(b.open(pr.b));
            const qint64 doc = b.documents().first().id;
            const qint64 tema = b.findCode(0, "Tema");
            b.addSegment(doc, tema, 20, 32);
            QVERIFY(b.updateDocumentText(doc, b.documentText(doc) + " Aggiunta di Bruno.", b.segmentsForDocument(doc),
                                         b.links()));
        }
        Merge::Source src;
        QString error;
        QVERIFY2(src.load(pr.b, &error), qPrintable(error));

        {   // Skip: the document and its codings stay out.
            Project a;
            QVERIFY(a.open(pr.a));
            Merge::Options skip;
            skip.onTextConflict = Merge::TextConflict::Skip;
            const Merge::Report r = Merge::apply(a, src, skip);
            QVERIFY(r.ok);
            QCOMPARE(r.documentsConflict, 1);
            QCOMPARE(r.documentsSkipped, 1);
            QCOMPARE(r.conflictDocuments, QStringList{"Intervista"});
            QCOMPARE(a.documents().size(), 1);
            QCOMPARE(a.allSegments().size(), 2);
            QVERIFY(r.segmentsSkipped > 0);
        }
        // Import as a copy: the original is untouched, the copy carries Bruno's text and codings.
        Project a;
        QVERIFY(a.open(pr.a));
        const QString originalText = a.documentText(a.documents().first().id);
        const Merge::Report r = Merge::apply(a, src, {});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.documentsCopied, 1);
        QCOMPARE(a.documents().size(), 2);
        QCOMPARE(a.documentText(a.documents().first().id), originalText);
        const Document copy = a.documents().last();
        QVERIFY(copy.name.startsWith("Intervista (copia di"));
        QVERIFY(a.documentText(copy.id).endsWith("Aggiunta di Bruno."));
        QVERIFY(copy.uuid != a.documents().first().uuid);
        QCOMPARE(a.segmentsForDocument(copy.id).size(), 3);
        QCOMPARE(a.segmentsForDocument(a.documents().first().id).size(), 2);
        // A second merge recognises the copy.
        const Merge::Report again = Merge::apply(a, src, {});
        QCOMPARE(countOf(again), 0);
        QCOMPARE(a.documents().size(), 2);
    }

    void mergeOnlyAuthor()
    {
        const Pair pr = makeCoders("author");
        {
            Project b;
            b.setAuthor("Bruno");
            QVERIFY(b.open(pr.b));
            const qint64 doc = b.documents().first().id;
            const qint64 tema = b.findCode(0, "Tema");
            const qint64 nuovo = b.addCode(0, "Solo di Bruno", Qt::green);
            b.addSegment(doc, nuovo, 34, 40);
            b.setAuthor("Carla");
            b.addSegment(doc, tema, 20, 32);
            b.addCode(0, "Inutile", Qt::gray);
            b.addDictionary("Dizionario");
        }
        Project a;
        QVERIFY(a.open(pr.a));
        Merge::Source src;
        QString error;
        QVERIFY2(src.load(pr.b, &error), qPrintable(error));
        QCOMPARE(src.authors(), (QStringList{"Anna", "Bruno", "Carla"}));
        Merge::Options o;
        o.onlyAuthor = "Bruno";
        const Merge::Report r = Merge::apply(a, src, o);
        QVERIFY(r.ok);
        QCOMPARE(r.segmentsNew, 1);
        QCOMPARE(r.segmentsFiltered, 3); // Anna's two (also in Bruno's copy) and Carla's
        QVERIFY(a.findCode(0, "Solo di Bruno") > 0);
        QCOMPARE(a.findCode(0, "Inutile"), qint64(0)); // codes nobody used with that author stay out
        QVERIFY(a.dictionaries().isEmpty());
        QCOMPARE(a.allSegments().size(), 3);
    }

    void mergeMediaAndOlderSchemas()
    {
        const QByteArray pdfBytes = makePdf({{"Primo paragrafo"}, {"Pagina due"}});
        const QByteArray png = encodeImage(twoColorImage(100, 60), "PNG");
        const QString fileB = path("media-b.tsra");
        {
            Project b;
            b.setMigrations(Project::builtInMigrations().mid(0, 7)); // schema 8 file, like an older Tessera
            QVERIFY(b.create(fileB));
            b.setAuthor("Bruno");
            const qint64 g = b.groups().first().id;
            const qint64 pdf = b.addPdfDocument(g, "Rapporto", pdfBytes, "Primo paragrafo\fPagina due");
            const qint64 img = b.addImageDocument(b.addGroup("Foto"), "Scatto", png);
            const qint64 a = b.addCode(0, "A", Qt::red);
            b.addAreaSegment(pdf, a, 2, 72, 90, 200, 120, 3, "nota", {}, "Pagina due");
            b.addAreaSegment(img, a, 0, 10, 5, 60, 40);
        }
        const QByteArray before = fileHash(fileB);
        Project a;
        QVERIFY(a.create(path("media-a.tsra")));
        a.setAuthor("Anna");
        Merge::Source src;
        QString error;
        QVERIFY2(src.load(fileB, &error), qPrintable(error));
        QCOMPARE(fileHash(fileB), before);                 // the original is neither upgraded nor backed up
        QCOMPARE(fileVersion(fileB), 8);
        QVERIFY(!QFileInfo::exists(path("media-b (backup v8).tsra")));
        const Merge::Report r = Merge::apply(a, src, {});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.documentsNew, 2);
        QCOMPARE(r.segmentsNew, 2);
        QCOMPARE(a.documents().size(), 2);
        const Document pdf = a.documents().first();
        QCOMPARE(pdf.kind, DocumentKind::Pdf);
        QCOMPARE(a.documentData(pdf.id), pdfBytes);
        QCOMPARE(a.documentText(pdf.id), QString("Primo paragrafo\fPagina due"));
        const QList<Segment> areas = a.segmentsForDocument(pdf.id, true);
        QCOMPARE(areas.size(), 1);
        QCOMPARE(areas.first().areaText, QString("Pagina due"));
        QCOMPARE(areas.first().author, QString("Bruno"));
        QCOMPARE(a.groups().size(), 2); // the default group matched by name, "Foto" created
        QCOMPARE(countOf(Merge::apply(a, src, {})), 0);
        QVERIFY(a.undo());
        QVERIFY(a.documents().isEmpty());
        QVERIFY(a.allSegments(true).isEmpty());
        QVERIFY(a.documentData(pdf.id).isEmpty());
    }

    void mergeFromQdpxAndFailures()
    {
        const Pair pr = makeCoders("qdpx");
        {
            Project b;
            b.setAuthor("Bruno");
            QVERIFY(b.open(pr.b));
            const qint64 doc = b.documents().first().id;
            b.addSegment(doc, b.findCode(0, "Tema"), 20, 32, 0, "da qdpx");
        }
        {
            Project b;
            QVERIFY(b.open(pr.b));
            QString err;
            QVERIFY2(Qdpx::exportProject(b, path("coders.qdpx"), &err), qPrintable(err));
        }
        Project a;
        QVERIFY(a.open(pr.a));
        Merge::Source src;
        QString error;
        QVERIFY2(src.load(path("coders.qdpx"), &error), qPrintable(error));
        const Merge::Report r = Merge::apply(a, src, {});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.documentsPresent, 1);
        QCOMPARE(r.segmentsNew, 1);
        QCOMPARE(r.segmentsDuplicate, 2);
        QCOMPARE(a.documents().size(), 1);
        QCOMPARE(a.allSegments().size(), 3);

        Merge::Source bad;
        writeFile("not-a-project.tsra", "this is not sqlite");
        QVERIFY(!bad.load(path("not-a-project.tsra"), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!bad.load(path("missing.tsra"), &error));
    }

    // ---------------------------------------------------------------- AI infrastructure (issue #39)

    static QByteArray completion(const QString &content)
    {
        return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{
                   {"message", QJsonObject{{"role", "assistant"}, {"content", content}}}}}}})
            .toJson(QJsonDocument::Compact);
    }

    static Ai::Response awaitReply(Ai::Reply *r, int ms = 5000)
    {
        if (!r->isFinished()) {
            QSignalSpy spy(r, &Ai::Reply::finished);
            spy.wait(ms);
        }
        return r->response();
    }

    static Ai::Config localConfig(const FakeHttp &srv)
    {
        Ai::Config c;
        c.provider = Ai::Provider::Local;
        c.baseUrl = srv.url();
        c.model = "tessera-test";
        c.apiKey = "secret-key-123";
        c.timeoutSec = 5;
        return c;
    }

    static QJsonObject simpleSchema()
    {
        return QJsonObject{{"type", "object"},
                           {"properties", QJsonObject{{"codes", QJsonObject{{"type", "array"}}}}},
                           {"required", QJsonArray{"codes"}}};
    }

private slots:
    void aiChatRequestAndStructuredOutput()
    {
        FakeHttp srv;
        srv.handler = [](const FakeHttp::Req &) {
            return FakeHttp::Res{200, completion(R"({"codes":["lavoro","tempo"]})")};
        };
        Ai::Client client(localConfig(srv));
        Ai::Request req;
        req.messages = {{"system", "Sei un assistente."}, {"user", "Proponi codici."}};
        req.schema = simpleSchema();
        req.schemaName = "codes_result";
        const Ai::Response r = awaitReply(client.chat(req));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.attempts, 1);
        QCOMPARE(r.json.object().value("codes").toArray().size(), 2);

        QCOMPARE(srv.requests.size(), 1);
        const FakeHttp::Req &sent = srv.requests.first();
        QCOMPARE(sent.method, QString("POST"));
        QCOMPARE(sent.path, QString("/v1/chat/completions"));
        QCOMPARE(sent.headers.value("authorization"), QByteArray("Bearer secret-key-123"));
        const QJsonObject body = QJsonDocument::fromJson(sent.body).object();
        QCOMPARE(body.value("model").toString(), QString("tessera-test"));
        QCOMPARE(body.value("stream").toBool(true), false);
        QCOMPARE(body.value("messages").toArray().size(), 2);
        QCOMPARE(body.value("messages").toArray().at(1).toObject().value("content").toString(), QString("Proponi codici."));
        const QJsonObject rf = body.value("response_format").toObject();
        QCOMPARE(rf.value("type").toString(), QString("json_schema"));
        QCOMPARE(rf.value("json_schema").toObject().value("name").toString(), QString("codes_result"));
        QVERIFY(rf.value("json_schema").toObject().value("schema").toObject() == simpleSchema());

        // Free text: no response_format, no json.
        srv.requests.clear();
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{200, completion("Ciao")}; };
        Ai::Request plain;
        plain.messages = {{"user", "Saluta"}};
        const Ai::Response p = awaitReply(client.chat(plain));
        QVERIFY(p.ok);
        QCOMPARE(p.text, QString("Ciao"));
        QVERIFY(!QJsonDocument::fromJson(srv.requests.first().body).object().contains("response_format"));

        // The answer may come wrapped in a code fence or in chatter.
        srv.handler = [](const FakeHttp::Req &) {
            return FakeHttp::Res{200, completion("Ecco:\n```json\n{\"codes\": []}\n```")};
        };
        const Ai::Response f = awaitReply(client.chat(req));
        QVERIFY2(f.ok, qPrintable(f.error));
        QVERIFY(f.json.object().contains("codes"));
    }

    void aiRetriesOnInvalidJson()
    {
        FakeHttp srv;
        int n = 0;
        srv.handler = [&n](const FakeHttp::Req &) {
            return FakeHttp::Res{200, completion(++n == 1 ? QString("non e' json") : QString(R"({"codes":["a"]})"))};
        };
        Ai::Client client(localConfig(srv));
        Ai::Request req;
        req.messages = {{"user", "x"}};
        req.schema = simpleSchema();
        Ai::Response r = awaitReply(client.chat(req));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.attempts, 2);
        QCOMPARE(srv.requests.size(), 2);
        // The retry shows the model its wrong answer and asks again.
        const QJsonArray msgs = QJsonDocument::fromJson(srv.requests.at(1).body).object().value("messages").toArray();
        QCOMPARE(msgs.size(), 3);
        QCOMPARE(msgs.at(1).toObject().value("role").toString(), QString("assistant"));

        // Still invalid after the retries: a clear error.
        srv.requests.clear();
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{200, completion("{\"altro\":1}")}; };
        req.maxRetries = 2;
        r = awaitReply(client.chat(req));
        QVERIFY(!r.ok);
        QCOMPARE(r.kind, Ai::ErrorKind::InvalidJson);
        QCOMPARE(r.attempts, 3);
        QVERIFY(!r.error.isEmpty());

        // No retries asked: a single attempt.
        srv.requests.clear();
        req.maxRetries = 0;
        r = awaitReply(client.chat(req));
        QCOMPARE(r.attempts, 1);
    }

    void aiFallsBackWithoutResponseFormat()
    {
        FakeHttp srv;
        srv.handler = [](const FakeHttp::Req &q) {
            if (q.body.contains("response_format"))
                return FakeHttp::Res{400, R"({"error":{"message":"response_format is not supported"}})"};
            return FakeHttp::Res{200, completion(R"({"codes":["x"]})")};
        };
        Ai::Client client(localConfig(srv));
        Ai::Request req;
        req.messages = {{"user", "x"}};
        req.schema = simpleSchema();
        const Ai::Response r = awaitReply(client.chat(req));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(srv.requests.size(), 2);
        const QJsonObject second = QJsonDocument::fromJson(srv.requests.at(1).body).object();
        QVERIFY(!second.contains("response_format"));
        QVERIFY(second.value("messages").toArray().first().toObject().value("content").toString().contains("JSON Schema"));
    }

    void aiErrorsAreClear()
    {
        FakeHttp srv;
        Ai::Request req;
        req.messages = {{"user", "x"}};

        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{401, R"({"error":{"message":"bad key"}})"}; };
        {
            Ai::Client c(localConfig(srv));
            const Ai::Response r = awaitReply(c.chat(req));
            QVERIFY(!r.ok);
            QCOMPARE(r.kind, Ai::ErrorKind::Unauthorized);
            QVERIFY(r.error.contains("chiave API"));
            QVERIFY(!r.error.contains("secret-key-123")); // keys never appear in messages
        }
        srv.handler = [](const FakeHttp::Req &) {
            return FakeHttp::Res{404, R"({"error":{"message":"model 'tessera-test' not found"}})"};
        };
        {
            Ai::Client c(localConfig(srv));
            const Ai::Response r = awaitReply(c.chat(req));
            QCOMPARE(r.kind, Ai::ErrorKind::ModelNotFound);
            QVERIFY(r.error.contains("tessera-test"));
        }
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{404, "<html>nope</html>"}; };
        {
            Ai::Client c(localConfig(srv));
            const Ai::Response r = awaitReply(c.chat(req));
            QCOMPARE(r.kind, Ai::ErrorKind::Http);
            QVERIFY(r.error.contains("/v1"));
        }
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{200, "{\"hello\":1}"}; };
        {
            Ai::Client c(localConfig(srv));
            QCOMPARE(awaitReply(c.chat(req)).kind, Ai::ErrorKind::BadResponse);
        }
        // Timeout: the server never answers.
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{200, {}, true}; };
        {
            Ai::Client c(localConfig(srv));
            c.setTimeoutMs(300);
            const Ai::Response r = awaitReply(c.chat(req));
            QCOMPARE(r.kind, Ai::ErrorKind::Timeout);
        }
        // Wrong address: nothing listens there.
        {
            Ai::Config cfg = localConfig(srv);
            quint16 port;
            {
                QTcpServer tmp;
                QVERIFY(tmp.listen(QHostAddress::LocalHost, 0));
                port = tmp.serverPort();
            }
            cfg.baseUrl = QString("http://127.0.0.1:%1/v1").arg(port);
            Ai::Client c(cfg);
            const Ai::Response r = awaitReply(c.chat(req));
            QCOMPARE(r.kind, Ai::ErrorKind::Unreachable);
            QVERIFY(r.error.contains("non risponde"));
        }
        // Incomplete configuration: reported without any network traffic.
        {
            Ai::Config cfg = localConfig(srv);
            cfg.model.clear();
            srv.requests.clear();
            Ai::Client c(cfg);
            const Ai::Response r = awaitReply(c.chat(req));
            QCOMPARE(r.kind, Ai::ErrorKind::Config);
            QVERIFY(r.error.contains("modello"));
            cfg = localConfig(srv);
            cfg.baseUrl = "localhost 11434";
            Ai::Client c2(cfg);
            QCOMPARE(awaitReply(c2.chat(req)).kind, Ai::ErrorKind::Config);
            QCOMPARE(srv.requests.size(), 0);
        }
        // A redirect is never followed (it could carry the key elsewhere).
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{302, {}, false, "Location: http://127.0.0.1:1/x\r\n"}; };
        {
            Ai::Client c(localConfig(srv));
            srv.requests.clear();
            const Ai::Response r = awaitReply(c.chat(req));
            QVERIFY(!r.ok);
            QCOMPARE(srv.requests.size(), 1);
        }
    }

    void aiCancel()
    {
        FakeHttp srv;
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{200, {}, true}; };
        Ai::Client client(localConfig(srv));
        Ai::Request req;
        req.messages = {{"user", "x"}};
        Ai::Reply *reply = client.chat(req);
        QSignalSpy spy(reply, &Ai::Reply::finished);
        QTimer::singleShot(150, reply, [reply] { reply->cancel(); });
        QVERIFY(spy.wait(3000));
        QCOMPARE(reply->response().kind, Ai::ErrorKind::Cancelled);
        QVERIFY(!reply->response().ok);
        reply->cancel(); // idempotent
        QTest::qWait(50);
        QCOMPARE(spy.count(), 1);

        // Cancelling right after the call, before anything was sent.
        srv.requests.clear();
        Ai::Reply *early = client.chat(req);
        QSignalSpy spy2(early, &Ai::Reply::finished);
        early->cancel();
        QTest::qWait(100);
        QCOMPARE(spy2.count(), 1);
        QCOMPARE(early->response().kind, Ai::ErrorKind::Cancelled);
        QCOMPARE(srv.requests.size(), 0);
    }

    void aiModelsAndConnectionTest()
    {
        FakeHttp srv;
        srv.handler = [](const FakeHttp::Req &q) {
            if (q.method == "GET" && q.path == "/v1/models")
                return FakeHttp::Res{200, R"({"object":"list","data":[{"id":"zeta"},{"id":"tessera-test"},{"id":"models/gem"}]})"};
            return FakeHttp::Res{404, "{}"};
        };
        Ai::Client client(localConfig(srv));
        Ai::Response r = awaitReply(client.listModels());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.models, QStringList({"gem", "tessera-test", "zeta"}));
        QCOMPARE(srv.requests.first().headers.value("authorization"), QByteArray("Bearer secret-key-123"));

        r = awaitReply(client.testConnection());
        QVERIFY2(r.ok, qPrintable(r.error));

        Ai::Config other = localConfig(srv);
        other.model = "assente";
        Ai::Client c2(other);
        r = awaitReply(c2.testConnection());
        QVERIFY(!r.ok);
        QCOMPARE(r.kind, Ai::ErrorKind::ModelNotFound);
        QVERIFY(r.error.contains("assente"));

        // Listing the models needs no model name.
        Ai::Config noModel = localConfig(srv);
        noModel.model.clear();
        Ai::Client c3(noModel);
        QVERIFY(awaitReply(c3.listModels()).ok);

        // Not an OpenAI-compatible server.
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{200, "{\"version\":\"1\"}"}; };
        QCOMPARE(awaitReply(client.listModels()).kind, Ai::ErrorKind::BadResponse);

        // A server without /models: a one-token request decides.
        srv.requests.clear();
        srv.handler = [](const FakeHttp::Req &q) {
            if (q.path == "/v1/chat/completions")
                return FakeHttp::Res{200, completion("pong")};
            return FakeHttp::Res{404, "{}"};
        };
        r = awaitReply(client.testConnection());
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(srv.requests.size(), 2);
        QCOMPARE(QJsonDocument::fromJson(srv.requests.at(1).body).object().value("max_tokens").toInt(), 1);

        // Cancelling the test cancels the call in flight.
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{200, {}, true}; };
        Ai::Reply *t = client.testConnection();
        QSignalSpy spy(t, &Ai::Reply::finished);
        QTimer::singleShot(150, t, [t] { t->cancel(); });
        QVERIFY(spy.wait(3000));
        QCOMPARE(t->response().kind, Ai::ErrorKind::Cancelled);
    }

    void aiNoNetworkWithoutExplicitCall()
    {
        FakeHttp srv;
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{200, completion("x")}; };
        Ai::Client client(localConfig(srv));
        QTest::qWait(150);
        QCOMPARE(srv.requests.size(), 0); // creating a client sends nothing

        QTemporaryDir dir;
        Ai::Settings::useFilesForTesting(dir.filePath("s.ini"), dir.filePath("admin.ini"));
        Ai::Config cloud;
        cloud.provider = Ai::Provider::OpenAi;
        cloud.baseUrl = Ai::defaultBaseUrl(cloud.provider);
        cloud.model = "m";
        cloud.apiKey = "k";
        QVERIFY(cloud.isConfigured());
        QString err;
        QVERIFY(!Ai::Settings::mayCall(cloud));
        QVERIFY(!Ai::makeClient(cloud, nullptr, &err)); // cloud without confirmation
        QVERIFY(!err.isEmpty());
        Ai::Settings::setCloudConfirmed(Ai::Provider::OpenAi, true);
        QVERIFY(Ai::Settings::mayCall(cloud));
        Ai::Client *c = Ai::makeClient(cloud, nullptr, &err);
        QVERIFY(c);
        delete c;
        // Local server: no confirmation needed; no provider: no client.
        QVERIFY(Ai::Settings::mayCall(localConfig(srv)));
        QVERIFY(!Ai::makeClient(Ai::Config(), nullptr, &err));
        // Cloud providers must use https.
        cloud.baseUrl = "http://api.example.com/v1";
        QVERIFY(!cloud.isConfigured());
        Ai::Settings::useFilesForTesting({}, {});
        QCOMPARE(srv.requests.size(), 0);
    }

    // ---- automatic updates --------------------------------------------------------------------------------

    static QByteArray makeZip(const QList<QPair<QString, QByteArray>> &files)
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        QZipWriter zip(&buffer);
        for (const auto &f : files)
            zip.addFile(f.first, f.second);
        zip.close();
        return buffer.data();
    }

    static QByteArray releaseJson(const FakeHttp &srv, const QString &tag, const QByteArray &zip, bool withDigest = true)
    {
        QJsonObject asset{{"name", "Tessera-" + tag.mid(1) + "-win64.zip"},
                          {"size", zip.size()},
                          {"browser_download_url", srv.url() + "/asset.zip"}};
        if (withDigest)
            asset["digest"] = "sha256:" + QString::fromLatin1(QCryptographicHash::hash(zip, QCryptographicHash::Sha256).toHex());
        return QJsonDocument(QJsonObject{{"tag_name", tag}, {"name", "Release " + tag}, {"body", "<b>notes</b>"},
                                         {"html_url", "https://github.com/x/y/releases/tag/" + tag},
                                         {"assets", QJsonArray{asset}}}).toJson();
    }

    void updateVersionComparison()
    {
        QVERIFY(Update::compareVersions("0.9.1", "0.28.0") < 0);
        QVERIFY(Update::compareVersions("v1.0.0", "0.99.9") > 0);
        QCOMPARE(Update::compareVersions("1.2", "1.2.0"), 0);
        QVERIFY(Update::compareVersions("0.28.0", "0.28.1") < 0);
    }

    void updateParseReleaseTrustsOnlyGithub()
    {
        const QString base = "https://github.com/o/r/releases/download/v0.30.0/";
        const QByteArray json = QJsonDocument(QJsonObject{
            {"tag_name", "v0.30.0"}, {"name", "n"}, {"body", "b"},
            {"html_url", "https://github.com/o/r/releases/tag/v0.30.0"},
            {"assets", QJsonArray{
                QJsonObject{{"name", "evil.zip"}, {"size", 1}, {"browser_download_url", base + "evil.zip"}},
                QJsonObject{{"name", "Tessera-0.30.0-win64.zip"}, {"size", 10}, {"digest", "sha256:ABCD"},
                            {"browser_download_url", base + "Tessera-0.30.0-win64.zip"}}}}}).toJson();
        Update::Release r;
        QVERIFY(Update::parseRelease(json, {}, &r));
        QCOMPARE(r.version, QString("0.30.0"));
        QCOMPARE(r.assetName, QString("Tessera-0.30.0-win64.zip"));
        QCOMPARE(r.sha256, QString("abcd"));
        QVERIFY(r.hasAsset());
        // The same asset on another host is not accepted.
        QByteArray other = json;
        other.replace("https://github.com", "https://evil.example.com");
        QVERIFY(Update::parseRelease(other, {}, &r));
        QVERIFY(!r.hasAsset());
        QString err;
        QVERIFY(!Update::parseRelease("{\"tag_name\":\"latest\"}", {}, &r, &err));
        QVERIFY(!Update::parseRelease("not json", {}, &r, &err));
    }

    void updateExtractRefusesPathTraversal()
    {
        QTemporaryDir dir;
        QString err;
        const QString zip = dir.filePath("bad.zip");
        QFile f(zip);
        QVERIFY(f.open(QIODevice::WriteOnly));
        // QZipWriter drops "../" itself, so the name is patched in the bytes (same length, CRC unaffected).
        QByteArray bad = makeZip({{"Tessera/Tessera.exe", "x"}, {"xx/escape.txt", "x"}});
        bad.replace("xx/escape.txt", "../escape.txt");
        f.write(bad);
        f.close();
        // Whether the reader refuses the entry or flattens it (Qt strips "../"), nothing leaves the folder.
        Update::extractDistribution(zip, dir.filePath("out"), &err);
        QVERIFY(!QFileInfo::exists(dir.filePath("escape.txt")));

        QFile g(dir.filePath("ok.zip"));
        QVERIFY(g.open(QIODevice::WriteOnly));
        g.write(makeZip({{"Tessera/Tessera.exe", "exe"}, {"Tessera/platforms/q.dll", "dll"}}));
        g.close();
        const QString src = Update::extractDistribution(dir.filePath("ok.zip"), dir.filePath("good"), &err);
        QVERIFY2(!src.isEmpty(), qPrintable(err));
        QVERIFY(QFileInfo::exists(QDir(src).filePath("platforms/q.dll")));

        // Windows PowerShell's Compress-Archive stores names with backslashes; contents must still be extracted.
        QByteArray backslashed = makeZip({{"Tessera/Tessera.exe", QByteArray(5000, 'e')},
                                          {"Tessera/platforms/q.dll", QByteArray(5000, 'd')}});
        backslashed.replace("Tessera/Tessera.exe", "Tessera\\Tessera.exe");
        backslashed.replace("Tessera/platforms/q.dll", "Tessera\\platforms\\q.dll");
        QFile bs(dir.filePath("bs.zip"));
        QVERIFY(bs.open(QIODevice::WriteOnly));
        bs.write(backslashed);
        bs.close();
        const QString bsrc = Update::extractDistribution(dir.filePath("bs.zip"), dir.filePath("bsout"), &err);
        QVERIFY2(!bsrc.isEmpty(), qPrintable(err));
        QCOMPARE(QFileInfo(QDir(bsrc).filePath("Tessera.exe")).size(), qint64(5000));
        QCOMPARE(QFileInfo(QDir(bsrc).filePath("platforms/q.dll")).size(), qint64(5000));

        QFile h(dir.filePath("nope.zip"));
        QVERIFY(h.open(QIODevice::WriteOnly));
        h.write(makeZip({{"readme.txt", "x"}}));
        h.close();
        QVERIFY(Update::extractDistribution(dir.filePath("nope.zip"), dir.filePath("none"), &err).isEmpty());
    }

#ifdef Q_OS_WIN
    void updateReplacementScriptCopiesFiles()
    {
        QTemporaryDir dir;
        QDir().mkpath(dir.filePath("src/sub"));
        QDir().mkpath(dir.filePath("app"));
        for (const char *name : {"src/new.txt", "src/sub/deep.txt"}) {
            QFile f(dir.filePath(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("new");
        }
        QFile old(dir.filePath("app/new.txt"));
        QVERIFY(old.open(QIODevice::WriteOnly));
        old.write("old");
        old.close();
        QString err;
        // A pid that does not exist: the script starts copying at once.
        QVERIFY2(Update::launchReplacement(dir.filePath("src"), dir.filePath("app"), 999999, dir.filePath("work"), &err),
                 qPrintable(err));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("app/sub/deep.txt")), 10000);
        QFile now(dir.filePath("app/new.txt"));
        QVERIFY(now.open(QIODevice::ReadOnly));
        QCOMPARE(now.readAll(), QByteArray("new"));
        QVERIFY(!Update::launchReplacement("C:/a%b", dir.filePath("app"), 1, dir.filePath("work2"), &err));
    }
#endif

    void updateCheckAndDownload()
    {
        const QByteArray zip = makeZip({{"Tessera/Tessera.exe", "exe"}});
        FakeHttp srv;
        QByteArray tag = "v9.9.9";
        bool corrupt = false;
        srv.handler = [&](const FakeHttp::Req &r) {
            if (r.path.endsWith("/asset.zip")) {
                QByteArray body = zip;
                if (corrupt)
                    body[body.size() - 1] = char(body.at(body.size() - 1) ^ 0x55);
                return FakeHttp::Res{200, body};
            }
            return FakeHttp::Res{200, releaseJson(srv, QString::fromLatin1(tag), zip)};
        };
        Update::Checker checker(srv.url() + "/releases/latest");
        checker.setCurrentVersion("0.28.0");
        QSignalSpy checked(&checker, &Update::Checker::checked);
        checker.check();
        QVERIFY(checked.wait(3000));
        QVERIFY(checked.at(0).at(0).toBool());
        QVERIFY(checked.at(0).at(2).toBool()); // newer
        const auto release = qvariant_cast<Update::Release>(checked.at(0).at(1));
        QCOMPARE(release.version, QString("9.9.9"));
        QVERIFY(release.hasAsset());
        QVERIFY(srv.requests.at(0).headers.value("user-agent").startsWith("Tessera/"));

        QTemporaryDir dir;
        QSignalSpy done(&checker, &Update::Checker::downloaded);
        checker.download(release, dir.filePath("t.zip"));
        QVERIFY(done.wait(3000));
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(2).toString()));
        QCOMPARE(QFileInfo(dir.filePath("t.zip")).size(), qint64(zip.size()));

        // A corrupted download is rejected and leaves nothing behind.
        corrupt = true;
        QSignalSpy bad(&checker, &Update::Checker::downloaded);
        checker.download(release, dir.filePath("c.zip"));
        QVERIFY(bad.wait(3000));
        QVERIFY(!bad.at(0).at(0).toBool());
        QVERIFY(!QFileInfo::exists(dir.filePath("c.zip")));
        QVERIFY(!QFileInfo::exists(dir.filePath("c.zip.part")));

        // Same version: not newer.
        checker.setCurrentVersion("9.9.9");
        QSignalSpy same(&checker, &Update::Checker::checked);
        checker.check();
        QVERIFY(same.wait(3000));
        QVERIFY(!same.at(0).at(2).toBool());

        // Server error: reported, not crashed.
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{500, "{}"}; };
        QSignalSpy err(&checker, &Update::Checker::checked);
        checker.check();
        QVERIFY(err.wait(3000));
        QVERIFY(!err.at(0).at(0).toBool());
        QVERIFY(!err.at(0).at(3).toString().isEmpty());
    }

    void aiSettingsSecretsAndLock()
    {
        QTemporaryDir dir;
        const QString ini = dir.filePath("s.ini"), admin = dir.filePath("admin.ini");
        Ai::Settings::useFilesForTesting(ini, admin);

        QCOMPARE(Ai::Settings::load().provider, Ai::Provider::None); // default: no AI
        Ai::Config c;
        c.provider = Ai::Provider::OpenAi;
        c.baseUrl = Ai::defaultBaseUrl(c.provider);
        c.model = "gpt-test";
        c.apiKey = "sk-SECRET-VALUE";
        c.timeoutSec = 33;
        QVERIFY(Ai::Settings::save(c));
        const Ai::Config back = Ai::Settings::load();
        QCOMPARE(back.provider, Ai::Provider::OpenAi);
        QCOMPARE(back.model, QString("gpt-test"));
        QCOMPARE(back.timeoutSec, 33);
        QCOMPARE(back.apiKey, QString("sk-SECRET-VALUE")); // from the credential store
        // The key is not in the settings file.
        QFile f(ini);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray text = f.readAll();
        QVERIFY(!text.contains("SECRET"));
        QVERIFY(text.contains("gpt-test"));
        f.close();

        // Administrator lock: settings come from the file and cannot be changed.
        {
            QFile a(admin);
            QVERIFY(a.open(QIODevice::WriteOnly));
            a.write("[AI]\nLocked=true\nProvider=local\nUrl=http://lab-server:11434/v1\nModel=llama3\nTimeout=200\n");
        }
        QVERIFY(Ai::Settings::isLocked());
        const Ai::Config locked = Ai::Settings::load();
        QCOMPARE(locked.provider, Ai::Provider::Local);
        QCOMPARE(locked.baseUrl, QString("http://lab-server:11434/v1"));
        QCOMPARE(locked.model, QString("llama3"));
        QCOMPARE(locked.timeoutSec, 200);
        QString err;
        QVERIFY(!Ai::Settings::save(c, &err));
        QVERIFY(!err.isEmpty());
        QCOMPARE(Ai::Settings::load().model, QString("llama3"));
        // Disabled: no provider whatever the settings say.
        {
            QFile a(admin);
            QVERIFY(a.open(QIODevice::WriteOnly | QIODevice::Truncate));
            a.write("[AI]\nDisabled=true\n");
        }
        QVERIFY(Ai::Settings::isLocked());
        QCOMPARE(Ai::Settings::load().provider, Ai::Provider::None);

        // The author of AI codings.
        QCOMPARE(Ai::authorName(back), QString("AI (modello gpt-test)"));
        Ai::Settings::useFilesForTesting({}, {});
    }

    void aiChunkingAndDescriptions()
    {
        QString text;
        for (int i = 0; i < 200; ++i)
            text += QString("Paragrafo %1. Una frase lunga abbastanza per contare qualcosa. Altra frase.\n\n").arg(i);
        QCOMPARE(Ai::estimateTokens("abcdefgh"), 2);
        QCOMPARE(Ai::estimateTokens(""), 0);
        const QStringList chunks = Ai::splitIntoChunks(text, 500);
        QVERIFY(chunks.size() > 5);
        QCOMPARE(chunks.join(QString()), text); // nothing lost, nothing duplicated
        for (const QString &c : chunks) {
            QVERIFY(Ai::estimateTokens(c) <= 500);
            QVERIFY(c.endsWith("\n\n")); // cut at paragraph boundaries
        }
        QCOMPARE(Ai::splitIntoChunks("breve", 500), QStringList({"breve"}));
        QVERIFY(Ai::splitIntoChunks(QString(), 10).isEmpty());
        // No separator at all, with emoji: hard cut that keeps the text.
        const QString solid = QString("\U0001F600").repeated(100) + QString("x").repeated(50);
        const QStringList solidChunks = Ai::splitIntoChunks(solid, 10);
        QCOMPARE(solidChunks.join(QString()), solid);
        for (const QString &c : solidChunks)
            QVERIFY(!c.isEmpty() && !c.back().isHighSurrogate());

        QCOMPARE(Ai::countWords("Una frase, di sei parole."), 5);
        QCOMPARE(Ai::describeInput(3, 12000), QString("3 documenti, ~12.000 parole"));
        QCOMPARE(Ai::describeInput(1, 1), QString("1 documento, ~1 parola"));
    }

    void conceptMapsPersistAndUndo()
    {
        const QString file = path("maps.tsra");
        qint64 mapId = 0, code = 0, doc = 0, memo = 0;
        {
            Project p;
            QVERIFY(p.create(file));
            doc = p.addDocument(p.groups().first().id, "Intervista", "testo di prova");
            code = p.addCode(0, "Tema", Qt::red);
            Memo m;
            m.targetType = MemoTarget::Free;
            m.title = "Idea";
            memo = p.saveMemo(m);
            mapId = p.addConceptMap("Modello");
            QVERIFY(mapId > 0);
            MapNode a;
            a.mapId = mapId;
            a.kind = MapNodeKind::Code;
            a.refId = code;
            a.x = 10.5;
            a.y = -20;
            MapNode b;
            b.mapId = mapId;
            b.kind = MapNodeKind::Document;
            b.refId = doc;
            b.x = 200;
            MapNode c;
            c.mapId = mapId;
            c.label = "Ipotesi libera";
            c.y = 100;
            c.w = 120;
            c.h = 40;
            ConceptMaps::Style st;
            st.color = QColor("#336699");
            st.width = 4;
            st.line = "dash";
            c.styleJson = ConceptMaps::styleToJson(st);
            const qint64 na = p.addMapNode(a), nb = p.addMapNode(b), nc = p.addMapNode(c);
            QVERIFY(na > 0 && nb > 0 && nc > 0);
            MapEdge e1{0, mapId, na, nb, "compare in", {}, {}};
            MapEdge e2{0, mapId, nb, nc, "suggerisce", st.color.isValid() ? ConceptMaps::styleToJson(st) : QString(), {}};
            QVERIFY(p.addMapEdge(e1) > 0);
            QVERIFY(p.addMapEdge(e2) > 0);
        }
        Project p;
        QVERIFY(p.open(file));
        QCOMPARE(p.conceptMaps().size(), 1);
        QCOMPARE(p.conceptMaps().first().name, QString("Modello"));
        QList<MapNode> nodes = p.mapNodes(mapId);
        QCOMPARE(nodes.size(), 3);
        QCOMPARE(nodes[0].x, 10.5);
        QCOMPARE(nodes[0].y, -20.0);
        QCOMPARE(nodes[1].kind, MapNodeKind::Document);
        QCOMPARE(nodes[2].label, QString("Ipotesi libera"));
        QCOMPARE(nodes[2].w, 120.0);
        const ConceptMaps::Style st = ConceptMaps::styleFromJson(nodes[2].styleJson);
        QCOMPARE(st.color, QColor("#336699"));
        QCOMPARE(st.width, 4);
        QCOMPARE(st.line, QString("dash"));
        QCOMPARE(p.mapEdges(mapId).size(), 2);
        QCOMPARE(p.mapEdges(mapId)[1].label, QString("suggerisce"));

        // What a node shows follows its target; a deleted target leaves a marked node.
        QCOMPARE(ConceptMaps::resolve(p, nodes[0]).caption, QString("Tema"));
        QVERIFY(ConceptMaps::resolve(p, nodes[0]).color == QColor(Qt::red));
        QVERIFY(p.deleteCode(code));
        const ConceptMaps::Target gone = ConceptMaps::resolve(p, p.mapNodes(mapId)[0]);
        QVERIFY(!gone.exists);
        QVERIFY(gone.caption.contains("eliminato"));
        QCOMPARE(p.mapNodes(mapId).size(), 3);

        // Moving a node is one undo step; deleting a node removes its arrows, and undo brings them back.
        nodes = p.mapNodes(mapId);
        nodes[1].x = 999;
        QVERIFY(p.updateMapNode(nodes[1]));
        QCOMPARE(p.mapNode(nodes[1].id).x, 999.0);
        QVERIFY(p.undo());
        QCOMPARE(p.mapNode(nodes[1].id).x, 200.0);
        QVERIFY(p.deleteMapNode(nodes[1].id));
        QCOMPARE(p.mapNodes(mapId).size(), 2);
        QCOMPARE(p.mapEdges(mapId).size(), 0);
        QVERIFY(p.undo());
        QCOMPARE(p.mapNodes(mapId).size(), 3);
        QCOMPARE(p.mapEdges(mapId).size(), 2);

        // Deleting the map removes everything; undo restores it.
        QVERIFY(p.deleteConceptMap(mapId));
        QVERIFY(p.conceptMaps().isEmpty());
        QVERIFY(p.mapNodes(mapId).isEmpty());
        QVERIFY(p.undo());
        QCOMPARE(p.mapNodes(mapId).size(), 3);
        QCOMPARE(p.mapEdges(mapId).size(), 2);
    }

    void conceptMapTemplates()
    {
        Project p;
        QVERIFY(p.create(path("map-templates.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d1 = p.addDocument(g, "Uno", "aaaa bbbb cccc");
        const qint64 d2 = p.addDocument(g, "Due", "dddd eeee");
        const qint64 root = p.addCode(0, "Radice", Qt::red);
        const qint64 k1 = p.addCode(root, "Figlio 1", Qt::blue);
        const qint64 k2 = p.addCode(root, "Figlio 2", Qt::green);
        p.addCode(k1, "Nipote", Qt::yellow);
        p.addSegment(d1, root, 0, 4);
        p.addSegment(d1, root, 5, 9);
        p.addSegment(d2, root, 0, 4);
        p.addSegment(d1, k1, 0, 4);
        p.addSegment(d1, k2, 10, 14);
        const qint64 map = p.addConceptMap("T");

        QCOMPARE(ConceptMaps::addCodeWithSubcodes(p, map, root), 4);
        QCOMPARE(p.mapNodes(map).size(), 4);
        QCOMPARE(p.mapEdges(map).size(), 3);
        QVERIFY(p.undo()); // one step for the whole template
        QVERIFY(p.mapNodes(map).isEmpty());

        QCOMPARE(ConceptMaps::addCodeWithDocuments(p, map, root), 3);
        const QList<MapEdge> edges = p.mapEdges(map);
        QCOMPARE(edges.size(), 2);
        QCOMPARE(edges[0].label, QString("2")); // two segments in "Uno"
        QCOMPARE(edges[1].label, QString("1"));
        QVERIFY(p.undo());

        // Overlapping codings of root and k1 on [0,4): one co-occurrence; k2 stays apart.
        QCOMPARE(ConceptMaps::addCooccurrences(p, map, {root, k1, k2}), 3);
        QCOMPARE(p.mapEdges(map).size(), 1);
        QCOMPARE(p.mapEdges(map).first().label, QString("1"));
        QCOMPARE(ConceptMaps::addCodeWithSubcodes(p, 9999, root), 0);
    }

    void conceptMapsQdpxRoundTripAndMerge()
    {
        Project p;
        QVERIFY(p.create(path("map-src.tsra")));
        const qint64 doc = p.addDocument(p.groups().first().id, "Intervista", "testo di prova");
        const qint64 code = p.addCode(0, "Tema", Qt::red);
        const qint64 seg = p.addSegment(doc, code, 0, 5);
        Memo m;
        m.targetType = MemoTarget::Free;
        m.title = "Idea";
        const qint64 memo = p.saveMemo(m);
        const qint64 map = p.addConceptMap("Modello");
        auto node = [&](MapNodeKind k, qint64 ref, const QString &label, double x, double y) {
            MapNode n;
            n.mapId = map;
            n.kind = k;
            n.refId = ref;
            n.label = label;
            n.x = x;
            n.y = y;
            n.w = 100;
            n.h = 40;
            return p.addMapNode(n);
        };
        const qint64 n1 = node(MapNodeKind::Code, code, "", 10, 20);
        const qint64 n2 = node(MapNodeKind::Document, doc, "", 300, 20);
        const qint64 n3 = node(MapNodeKind::Memo, memo, "", 10, 200);
        const qint64 n4 = node(MapNodeKind::Segment, seg, "", 300, 200);
        const qint64 n5 = node(MapNodeKind::Text, 0, "Libero", 150, 100);
        MapEdge e;
        e.mapId = map;
        e.fromNode = n1;
        e.toNode = n2;
        e.label = "in";
        ConceptMaps::Style st;
        st.color = QColor("#aa0000");
        e.styleJson = ConceptMaps::styleToJson(st);
        QVERIFY(p.addMapEdge(e) > 0);
        e.fromNode = n3;
        e.toNode = n4;
        e.label = "su";
        e.styleJson.clear();
        QVERIFY(p.addMapEdge(e) > 0);
        e.fromNode = n5;
        e.toNode = n1;
        e.label = "causa";
        QVERIFY(p.addMapEdge(e) > 0);

        QString err, report;
        QVERIFY2(Qdpx::exportProject(p, path("map.qdpx"), &err), qPrintable(err));
        Project q;
        QVERIFY(q.create(path("map-dst.tsra")));
        QVERIFY2(Qdpx::importProject(q, path("map.qdpx"), &report, &err), qPrintable(err));
        QCOMPARE(q.conceptMaps().size(), 1);
        const qint64 qm = q.conceptMaps().first().id;
        QCOMPARE(q.conceptMaps().first().name, QString("Modello"));
        const QList<MapNode> nodes = q.mapNodes(qm);
        QCOMPARE(nodes.size(), 5);
        QCOMPARE(nodes[0].kind, MapNodeKind::Code);
        QCOMPARE(q.code(nodes[0].refId).name, QString("Tema"));
        QCOMPARE(nodes[0].x, 10.0);
        QCOMPARE(nodes[0].w, 100.0);
        QCOMPARE(nodes[1].kind, MapNodeKind::Document);
        QCOMPARE(q.document(nodes[1].refId).name, QString("Intervista"));
        QCOMPARE(nodes[2].kind, MapNodeKind::Memo);
        QCOMPARE(nodes[3].kind, MapNodeKind::Segment);
        QCOMPARE(nodes[4].kind, MapNodeKind::Text);
        QCOMPARE(nodes[4].label, QString("Libero"));
        const QList<MapEdge> edges = q.mapEdges(qm);
        QCOMPARE(edges.size(), 3);
        QCOMPARE(edges[0].label, QString("in"));
        QCOMPARE(edges[0].fromNode, nodes[0].id);
        QCOMPARE(edges[0].toNode, nodes[1].id);
        QCOMPARE(ConceptMaps::styleFromJson(edges[0].styleJson).color, QColor("#aa0000"));
        QCOMPARE(edges[2].fromNode, nodes[4].id);

        // Merge into a project that has the same code and document: the map comes along, linked to them.
        p.close();
        Project t;
        QVERIFY(t.create(path("map-target.tsra")));
        Merge::Source src;
        QVERIFY2(src.load(path("map-src.tsra"), &err), qPrintable(err));
        const Merge::Report rep = Merge::apply(t, src, {});
        QVERIFY2(rep.ok, qPrintable(rep.error));
        QCOMPARE(rep.conceptMapsNew, 1);
        QCOMPARE(t.conceptMaps().size(), 1);
        const QList<MapNode> mn = t.mapNodes(t.conceptMaps().first().id);
        QCOMPARE(mn.size(), 5);
        QCOMPARE(t.code(mn[0].refId).name, QString("Tema"));
        QCOMPARE(t.document(mn[1].refId).name, QString("Intervista"));
        QCOMPARE(mn[2].kind, MapNodeKind::Text); // memo nodes become text with their caption
        QCOMPARE(mn[2].label, QString("Idea"));
        QCOMPARE(t.mapEdges(t.conceptMaps().first().id).size(), 3);
        // A second merge adds nothing.
        QCOMPARE(Merge::apply(t, src, {}).conceptMapsNew, 0);
    }

    void aiLogInProject()
    {
        Project p;
        QVERIFY(p.create(path("ailog.tsra")));
        QCOMPARE(p.schemaVersion(), 12);
        QVERIFY(!p.aiLogEnabled());
        QCOMPARE(p.addAiLog("f", "local", "m", "in", "out"), qint64(0)); // off by default
        p.setAiLogEnabled(true);
        QSignalSpy spy(&p, &Project::aiLogChanged);
        const bool couldUndo = p.canUndo();
        const qint64 id = p.addAiLog("suggerimento codici", "local", "llama3", "3 documenti, ~12.000 parole", "{\"codes\":[]}");
        QVERIFY(id > 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(p.canUndo(), couldUndo); // the log is not an undo step
        const QList<AiLogEntry> log = p.aiLog();
        QCOMPARE(log.size(), 1);
        QCOMPARE(log.first().function, QString("suggerimento codici"));
        QCOMPARE(log.first().model, QString("llama3"));
        QCOMPARE(log.first().inputSummary, QString("3 documenti, ~12.000 parole"));
        QVERIFY(log.first().createdAt.isValid());
        p.close();
        QVERIFY(p.open(path("ailog.tsra")));
        QVERIFY(p.aiLogEnabled());
        QCOMPARE(p.aiLog().size(), 1);
        QVERIFY(p.clearAiLog());
        QVERIFY(p.aiLog().isEmpty());
    }

    // ---------------------------------------------------------------- AI summaries, explanations, translations (issue #40)

private:
    static Ai::Response runAssist(Ai::Assist::Run &run, int ms = 8000)
    {
        if (!run.isFinished()) {
            QSignalSpy spy(&run, &Ai::Assist::Run::finished);
            run.start();
            spy.wait(ms);
        }
        return run.response();
    }

    static QString sentSystem(const FakeHttp::Req &r)
    {
        const QJsonArray m = QJsonDocument::fromJson(r.body).object().value("messages").toArray();
        return m.at(0).toObject().value("content").toString();
    }
    static QString sentUser(const FakeHttp::Req &r)
    {
        const QJsonArray m = QJsonDocument::fromJson(r.body).object().value("messages").toArray();
        return m.at(1).toObject().value("content").toString();
    }

private slots:
    void aiAssistPrompts()
    {
        using namespace Ai::Assist;
        Options o;
        o.length = Length::Short;
        o.language = "francese";
        o.instructions = "concentrati sulle difficolta";
        for (Task t : {Task::SummarizeDocument, Task::SummarizeSegments, Task::Explain, Task::Translate}) {
            const QString s = systemPrompt(t, o);
            QVERIFY2(!s.isEmpty(), "prompt resource missing");
            QVERIFY(s.contains("francese"));                       // output language always explicit
            QVERIFY(s.contains("concentrati sulle difficolta"));   // additional instructions
            QVERIFY(!s.contains("{language}") && !s.contains("{length}") && !s.contains("{instructions}"));
        }
        QVERIFY(systemPrompt(Task::SummarizeDocument, o).contains("short"));
        QVERIFY(!usesLength(Task::Translate));
        QVERIFY(combinePrompt(Task::SummarizeDocument, o).contains("francese"));
        // Without instructions the sentence is not there; an empty language means Italian.
        Options plain;
        const QString s = systemPrompt(Task::Explain, plain);
        QVERIFY(!s.contains("Additional instructions"));
        QVERIFY(s.contains("Italian"));
    }

    void aiAssistSingleRequestAndNoSendBeforeStart()
    {
        using namespace Ai::Assist;
        FakeHttp srv;
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{200, completion("  Un riassunto breve.  ")}; };
        Ai::Client client(localConfig(srv));
        Options o;
        o.language = "italiano";
        Run run(&client, Task::SummarizeDocument, o, {{QString(), "Testo dell'intervista sul lavoro da remoto."}});
        QTest::qWait(100);
        QCOMPARE(srv.requests.size(), 0); // creating a Run sends nothing
        const Ai::Response r = runAssist(run);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.text, QString("Un riassunto breve."));
        QCOMPARE(srv.requests.size(), 1);
        QCOMPARE(run.requestsMade(), 1);
        QVERIFY(sentSystem(srv.requests.first()).contains("italiano"));
        QCOMPARE(sentUser(srv.requests.first()), QString("Testo dell'intervista sul lavoro da remoto."));
        QVERIFY(!srv.requests.first().headers.value("authorization").isEmpty());
    }

    void aiAssistMapReduceLongDocument()
    {
        using namespace Ai::Assist;
        FakeHttp srv;
        int n = 0;
        srv.handler = [&n](const FakeHttp::Req &) { return FakeHttp::Res{200, completion(QString("parziale %1").arg(++n))}; };
        Ai::Client client(localConfig(srv));
        QString text;
        for (int i = 0; i < 30; ++i)
            text += QString("Paragrafo numero %1 con alcune parole di riempimento per allungare il testo.\n\n").arg(i);
        Run run(&client, Task::SummarizeDocument, Options(), {{QString(), text}});
        run.setMaxChunkTokens(200);
        QVERIFY(planChunks(Task::SummarizeDocument, {{QString(), text}}, 200).size() > 2);
        QSignalSpy progress(&run, &Run::progress);
        const Ai::Response r = runAssist(run);
        QVERIFY2(r.ok, qPrintable(r.error));
        const qsizetype chunks = planChunks(Task::SummarizeDocument, {{QString(), text}}, 200).size();
        QVERIFY(srv.requests.size() > chunks); // one per chunk plus the merging requests
        QVERIFY(sentSystem(srv.requests.first()).contains("part 1 of"));
        QVERIFY(sentUser(srv.requests.last()).contains("parziale 1")); // the last request merges partial results
        QVERIFY(sentSystem(srv.requests.last()).contains("merge"));
        QCOMPARE(r.text, QString("parziale %1").arg(n));
        QVERIFY(progress.count() >= chunks);
    }

    // ---------------------------------------------------------------- AI code suggestions (issue #41)

    void aiCodesPromptsChunksAndInvalidIds()
    {
        using namespace Ai::Codes;
        Options o;
        o.language = "francese";
        o.maxSuggestions = 5;
        for (Task t : {Task::SuggestCodes, Task::SuggestSubcodes, Task::GroupCodes}) {
            const QString s = systemPrompt(t, o, {"Esistente"});
            QVERIFY2(!s.isEmpty(), "prompt resource missing");
            QVERIFY(s.contains("francese") && s.contains("5"));
            QVERIFY(!s.contains("{max}") && !s.contains("{language}") && !s.contains("{existing}"));
            QVERIFY(!schemaFor(t).isEmpty());
        }
        QVERIFY(systemPrompt(Task::SuggestCodes, o, {"Esistente"}).contains("Esistente"));

        Material m;
        m.task = Task::SuggestSubcodes;
        m.subject = "Vantaggi";
        for (int i = 0; i < 5; ++i)
            m.addItem(1000 + i, QString("Segmento numero %1").arg(i));
        const QList<Chunk> chunks = planChunks(m, 3000, 2);
        QCOMPARE(chunks.size(), 3); // at most two segments per request, numbers restart from 1
        QVERIFY(chunks.first().user.contains("[1] Segmento numero 0"));
        QVERIFY(!chunks.first().user.contains("1000")); // database ids are never sent
        QCOMPARE(chunks.at(1).ids.value(1), qint64(1002));

        // Invented numbers (7, 0, "x") are dropped; valid ones are mapped back; an element stays in the first proposal.
        Result r;
        const QJsonDocument answer = QJsonDocument::fromJson(
            R"({"subcodes":[{"name":"Flessibilita","description":"d","segment_ids":[1,7,0,"2"]},
                           {"name":"Costi","description":"","segment_ids":[2,99]},
                           {"name":"Vuoto","description":"","segment_ids":[42]},
                           {"name":"esistente","description":"","segment_ids":[1]}]})");
        mergeAnswer(Task::SuggestSubcodes, answer, chunks.first().ids, {"Esistente"}, r);
        QCOMPARE(r.invalidIds, 4);
        QCOMPARE(r.proposals.size(), 1);
        QCOMPARE(r.proposals.first().name, QString("Flessibilita"));
        QCOMPARE(r.proposals.first().refIds, (QList<qint64>{1000, 1001}));
        QVERIFY(r.skippedProposals >= 2);
    }

    void aiCodesRunSplitsAndMerges()
    {
        using namespace Ai::Codes;
        FakeHttp srv;
        int n = 0;
        srv.handler = [&n](const FakeHttp::Req &) {
            ++n;
            return FakeHttp::Res{200, completion(n == 1 ? QString(R"({"subcodes":[{"name":"Tempo","description":"t","segment_ids":[1,2]}]})")
                                                        : QString(R"({"subcodes":[{"name":"TEMPO","description":"x","segment_ids":[1,5]}]})"))};
        };
        Ai::Client client(localConfig(srv));
        Material m;
        m.task = Task::SuggestSubcodes;
        m.subject = "Lavoro";
        for (int i = 0; i < 4; ++i)
            m.addItem(10 + i, QString("Brano %1").arg(i));
        Run run(&client, m, Options());
        run.setMaxItemsPerRequest(2);
        QTest::qWait(100);
        QCOMPARE(srv.requests.size(), 0); // nothing before start()
        QSignalSpy spy(&run, &Run::finished);
        run.start();
        spy.wait(8000);
        QVERIFY2(run.response().ok, qPrintable(run.response().error));
        QCOMPARE(srv.requests.size(), 2);
        QVERIFY(sentSystem(srv.requests.last()).contains("part 2 of 2"));
        QVERIFY(sentSystem(srv.requests.last()).contains("Tempo")); // names found so far are offered again
        const Result r = run.result();
        QCOMPARE(r.proposals.size(), 1); // same name (any case) merged
        QCOMPARE(r.proposals.first().refIds, (QList<qint64>{10, 11, 12}));
        QCOMPARE(r.invalidIds, 1);
        QVERIFY(srv.requests.first().body.contains("json_schema"));
    }

    void aiCodesApplyIsOneUndoStep()
    {
        using namespace Ai::Codes;
        QTemporaryDir dir;
        Project p;
        QVERIFY(p.create(dir.filePath("aicodes.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d = p.addDocument(g, "D", "Uno due tre quattro cinque sei sette otto.");
        const qint64 parent = p.addCode(0, "Lavoro", QColor("#112233"));
        const qint64 s1 = p.addSegment(d, parent, 0, 3);
        const qint64 s2 = p.addSegment(d, parent, 4, 7);
        const qint64 s3 = p.addSegment(d, parent, 8, 12);
        p.clearUndoHistory();
        // Subcodes: s1 and s2 move; a deleted segment and a segment of another code are skipped.
        const qint64 other = p.addCode(0, "Altro", QColor("#445566"));
        const qint64 s4 = p.addSegment(d, other, 13, 18);
        QVERIFY(p.deleteSegment(s3));
        p.clearUndoHistory();
        Ai::Config cfg;
        cfg.model = "m1";
        const ApplyReport rep = apply(p, Task::SuggestSubcodes, parent,
                                      {{"Casa", "Da casa", {s1, s2, s3, s4}, true}, {"Ufficio", "", {}, true}, {"Casa", "", {}, true}},
                                      provenanceNote(cfg));
        QCOMPARE(rep.createdCodes, 2);
        QCOMPARE(rep.movedSegments, 2);
        QCOMPARE(rep.skipped, 3); // s3 gone, s4 elsewhere, duplicate name
        const qint64 casa = p.findCode(parent, "Casa");
        QVERIFY(casa > 0);
        QCOMPARE(p.segment(s1).codeId, casa);
        QCOMPARE(p.segment(s4).codeId, other);
        QVERIFY(p.code(casa).description.startsWith("Da casa"));
        QVERIFY(p.code(casa).description.contains("AI (modello m1)")); // provenance note
        QVERIFY(p.undo()); // ONE step undoes codes and moves
        QCOMPARE(p.findCode(parent, "Casa"), qint64(0));
        QCOMPARE(p.segment(s1).codeId, parent);
        QVERIFY(!p.canUndo());

        // Groups: codes move under the new category.
        const qint64 a = p.addCode(0, "A", QColor("#111111"));
        const qint64 b = p.addCode(0, "B", QColor("#222222"));
        p.clearUndoHistory();
        const ApplyReport g2 = apply(p, Task::GroupCodes, 0, {{"Gruppo", "", {a, b}, true}}, QString());
        QCOMPARE(g2.movedCodes, 2);
        const qint64 grp = p.findCode(0, "Gruppo");
        QCOMPARE(p.code(a).parentId, grp);
        QCOMPARE(p.code(grp).description, QString()); // no provenance when switched off
        QVERIFY(p.undo());
        QCOMPARE(p.code(a).parentId, qint64(0));
        QCOMPARE(p.findCode(0, "Gruppo"), qint64(0));
        QVERIFY(!p.canUndo());

        // New codes under a parent.
        const ApplyReport g3 = apply(p, Task::SuggestCodes, parent, {{"Nuovo", "d", {}, true}}, QString());
        QCOMPARE(g3.createdCodes, 1);
        QVERIFY(p.findCode(parent, "Nuovo") > 0);
        QVERIFY(p.undo());
        QVERIFY(!p.canUndo());
    }
    void aiAssistTranslateConcatenates()
    {
        using namespace Ai::Assist;
        FakeHttp srv;
        int n = 0;
        srv.handler = [&n](const FakeHttp::Req &) { return FakeHttp::Res{200, completion(QString("T%1").arg(++n))}; };
        Ai::Client client(localConfig(srv));
        QString text;
        for (int i = 0; i < 40; ++i)
            text += QString("Frase dell'intervistato numero %1 che parla di altro ancora.\n\n").arg(i);
        Options o;
        o.language = "inglese";
        Run run(&client, Task::Translate, o, {{QString(), text}});
        run.setMaxChunkTokens(200);
        const qsizetype chunks = planChunks(Task::Translate, {{QString(), text}}, 200).size();
        QVERIFY(chunks > 1);
        const Ai::Response r = runAssist(run);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(srv.requests.size(), int(chunks)); // no merging for translations
        QVERIFY(sentSystem(srv.requests.first()).contains("inglese"));
        QVERIFY(r.text.startsWith("T1\n\nT2"));
    }

    void aiAssistSegmentsKeepCitations()
    {
        using namespace Ai::Assist;
        FakeHttp srv;
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{200, completion("Sintesi [Intervista 1] [Intervista 2]")}; };
        Ai::Client client(localConfig(srv));
        const QList<Source> sources{{"Intervista 1", "Lavoro da casa, piu' libertà."}, {"Intervista 2", "Mi manca l'ufficio."}};
        const QString user = userText(Task::SummarizeSegments, sources);
        QVERIFY(user.contains("[Intervista 1]\nLavoro") && user.contains("[Intervista 2]\nMi manca"));
        Run run(&client, Task::SummarizeSegments, Options(), sources);
        const Ai::Response r = runAssist(run);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(srv.requests.size(), 1);
        QVERIFY(sentUser(srv.requests.first()).contains("[Intervista 2]"));
        QVERIFY(sentSystem(srv.requests.first()).contains("square brackets"));
        QVERIFY(r.text.contains("[Intervista 1]"));
        // Chunks never separate a passage from its label.
        QList<Source> many;
        for (int i = 0; i < 20; ++i)
            many.append({QString("Intervista %1").arg(i), QString("Dichiarazione %1 abbastanza lunga da occupare spazio nel blocco.").arg(i)});
        const QStringList chunks = planChunks(Task::SummarizeSegments, many, 120);
        QVERIFY(chunks.size() > 1);
        for (const QString &c : chunks)
            QVERIFY(c.startsWith("[Intervista "));
        QStringList all = chunks;
        QCOMPARE(all.join("\n\n").count("[Intervista "), 20);
    }

    void aiAssistErrorsAndCancel()
    {
        using namespace Ai::Assist;
        FakeHttp srv;
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{401, R"({"error":{"message":"bad key"}})"}; };
        {
            Ai::Client client(localConfig(srv));
            Run run(&client, Task::Explain, Options(), {{QString(), "bundle"}});
            const Ai::Response r = runAssist(run);
            QVERIFY(!r.ok);
            QCOMPARE(r.kind, Ai::ErrorKind::Unauthorized);
            QVERIFY(!r.error.isEmpty());
        }
        {
            Ai::Client client(localConfig(srv));
            Run run(&client, Task::Explain, Options(), {{QString(), "   "}}); // nothing to send
            const Ai::Response r = runAssist(run);
            QVERIFY(!r.ok);
            QCOMPARE(srv.requests.size(), 1); // still only the first request
        }
        srv.handler = [](const FakeHttp::Req &) { return FakeHttp::Res{200, {}, true}; };
        {
            Ai::Client client(localConfig(srv));
            Run run(&client, Task::Translate, Options(), {{QString(), "ciao"}});
            QSignalSpy spy(&run, &Run::finished);
            run.start();
            QTest::qWait(100);
            run.cancel();
            QTRY_COMPARE(spy.count(), 1);
            QCOMPARE(run.response().kind, Ai::ErrorKind::Cancelled);
        }
    }
};

QTEST_MAIN(TestCore)
#include "test_core.moc"
