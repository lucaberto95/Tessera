#include "core/Qdpx.h"
#include "core/Project.h"
#include "core/TextUtils.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>

#include <algorithm>
#include <functional>
#include <tuple>

namespace Qdpx {

namespace {

const QString kNamespace = QStringLiteral("urn:QDA-XML:project:1.0");
constexpr int kMaxCodeDepth = 200;

QString xmlDate(const QDateTime &dt)
{
    return (dt.isValid() ? dt : QDateTime::currentDateTimeUtc()).toUTC().toString(Qt::ISODate);
}

QString qdpxVariableType(VariableType t)
{
    switch (t) {
    case VariableType::Text: return QStringLiteral("Text");
    case VariableType::Integer: return QStringLiteral("Integer");
    case VariableType::Decimal: return QStringLiteral("Float");
    case VariableType::Boolean: return QStringLiteral("Boolean");
    case VariableType::Date: return QStringLiteral("Date");
    }
    return QStringLiteral("Text");
}

VariableType tesseraVariableType(const QString &t)
{
    if (t == QLatin1String("Integer")) return VariableType::Integer;
    if (t == QLatin1String("Float")) return VariableType::Decimal;
    if (t == QLatin1String("Boolean")) return VariableType::Boolean;
    if (t == QLatin1String("Date") || t == QLatin1String("DateTime")) return VariableType::Date;
    return VariableType::Text;
}

QString valueElement(VariableType t)
{
    switch (t) {
    case VariableType::Integer: return QStringLiteral("IntegerValue");
    case VariableType::Decimal: return QStringLiteral("FloatValue");
    case VariableType::Boolean: return QStringLiteral("BooleanValue");
    case VariableType::Date: return QStringLiteral("DateValue");
    case VariableType::Text: break;
    }
    return QStringLiteral("TextValue");
}

// Tessera stores booleans as "1"/"0"; QDPX uses xsd:boolean.
QString toQdpxValue(VariableType t, const QString &v)
{
    if (t == VariableType::Boolean)
        return (v == QLatin1String("1") || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
                   ? QStringLiteral("true") : QStringLiteral("false");
    if (t == VariableType::Decimal)
        return QString(v).replace(QLatin1Char(','), QLatin1Char('.'));
    return v;
}

QString fromQdpxValue(VariableType t, const QString &v)
{
    if (t == VariableType::Boolean)
        return (v == QLatin1String("true") || v == QLatin1String("1")) ? QStringLiteral("1") : QStringLiteral("0");
    if (t == VariableType::Date)
        return v.left(10);
    return v;
}

// REFI-QDA has sets but no groups, so groups are exported as sets too. Their GUIDs derive from
// this namespace and the group's index: other programs see ordinary GUIDs, Tessera recognizes them
// on import and turns those sets back into groups.
QString groupSetGuid(qsizetype index)
{
    static const QUuid ns(QStringLiteral("{6f1d4c2e-8a3b-4e5f-9c7d-2b1a0e9f8d34}"));
    return QUuid::createUuidV5(ns, QString::number(index)).toString(QUuid::WithoutBraces);
}

// REFI-QDA links join objects with a GUID, so each end of a Tessera link is exported as a
// PlainTextSelection without codings; its GUID derives from the link's, to stay stable.
QString linkEndGuid(const QString &linkUuid, bool from)
{
    static const QUuid ns(QStringLiteral("{b3e7a1d2-5c4f-4a8e-9d61-7f2c0e8b4a19}"));
    return QUuid::createUuidV5(ns, linkUuid + (from ? QStringLiteral(":from") : QStringLiteral(":to")))
        .toString(QUuid::WithoutBraces);
}

} // namespace

// ============================================================================ export

bool exportProject(const Project &project, const QString &path, QString *error)
{
    const QList<Code> codes = project.codes();
    const QList<Document> docs = project.documents();
    const QList<DocumentGroup> groups = project.groups();
    const QList<Variable> vars = project.variables();
    const auto values = project.allVariableValues();
    const QList<Memo> memos = project.memos();
    const QList<Segment> segments = project.allSegments();

    // Users: one per distinct author name.
    QHash<QString, QString> userGuids;
    auto userGuid = [&](const QString &name) {
        const QString key = name.isEmpty() ? project.author() : name;
        if (!userGuids.contains(key))
            userGuids.insert(key, newUuid());
        return userGuids.value(key);
    };
    userGuid(project.author());
    for (const Segment &s : segments)
        userGuid(s.author);
    for (const Memo &m : memos)
        userGuid(m.author);

    QHash<qint64, QString> codeGuid;
    for (const Code &c : codes)
        codeGuid.insert(c.id, c.uuid.isEmpty() ? newUuid() : c.uuid);
    QHash<qint64, QString> varGuid;
    for (const Variable &v : vars)
        varGuid.insert(v.id, v.uuid.isEmpty() ? newUuid() : v.uuid);

    // memo lookups: (type, target) -> list of memos
    QHash<QString, QList<Memo>> memosByTarget;
    for (const Memo &m : memos)
        memosByTarget[memoTargetToString(m.targetType) + QLatin1Char(':') + QString::number(m.targetId)] << m;
    auto memosFor = [&](MemoTarget t, qint64 id) {
        return memosByTarget.value(memoTargetToString(t) + QLatin1Char(':') + QString::number(id));
    };

    QHash<qint64, QList<Code>> children;
    for (const Code &c : codes)
        children[c.parentId] << c;
    QHash<qint64, QList<Segment>> segmentsByDoc;
    for (const Segment &s : segments)
        segmentsByDoc[s.documentId] << s;
    const QList<DocumentLink> links = project.links();
    struct LinkEnd {
        QString guid;
        int start, end;
        const DocumentLink *link;
    };
    QHash<qint64, QList<LinkEnd>> linkEndsByDoc;
    for (const DocumentLink &l : links) {
        userGuid(l.author);
        linkEndsByDoc[l.fromDocumentId].append({linkEndGuid(l.uuid, true), l.fromStart, l.fromEnd, &l});
        linkEndsByDoc[l.toDocumentId].append({linkEndGuid(l.uuid, false), l.toStart, l.toEnd, &l});
    }

    QByteArray xml;
    QXmlStreamWriter w(&xml);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeDefaultNamespace(kNamespace);
    w.writeStartElement(kNamespace, QStringLiteral("Project"));
    w.writeAttribute(QStringLiteral("name"), project.name());
    w.writeAttribute(QStringLiteral("origin"), QStringLiteral("Tessera ") + QStringLiteral(TESSERA_VERSION_STRING));
    w.writeAttribute(QStringLiteral("creatingUserGUID"), userGuid(project.author()));
    w.writeAttribute(QStringLiteral("creationDateTime"),
                     xmlDate(QDateTime::fromString(project.meta(QStringLiteral("created_at")), Qt::ISODateWithMs)));
    w.writeAttribute(QStringLiteral("modifiedDateTime"), xmlDate(QDateTime::currentDateTimeUtc()));

    w.writeStartElement(QStringLiteral("Users"));
    for (auto it = userGuids.cbegin(); it != userGuids.cend(); ++it) {
        w.writeEmptyElement(QStringLiteral("User"));
        w.writeAttribute(QStringLiteral("guid"), it.value());
        w.writeAttribute(QStringLiteral("name"), it.key());
    }
    w.writeEndElement();

    auto writeNoteRefs = [&](MemoTarget t, qint64 id) {
        for (const Memo &m : memosFor(t, id)) {
            w.writeEmptyElement(QStringLiteral("NoteRef"));
            w.writeAttribute(QStringLiteral("targetGUID"), m.uuid);
        }
    };

    w.writeStartElement(QStringLiteral("CodeBook"));
    w.writeStartElement(QStringLiteral("Codes"));
    std::function<void(qint64)> writeCodes = [&](qint64 parent) {
        for (const Code &c : children.value(parent)) {
            w.writeStartElement(QStringLiteral("Code"));
            w.writeAttribute(QStringLiteral("guid"), codeGuid.value(c.id));
            w.writeAttribute(QStringLiteral("name"), c.name);
            w.writeAttribute(QStringLiteral("isCodable"), QStringLiteral("true"));
            w.writeAttribute(QStringLiteral("color"), c.color.name().toUpper());
            if (!c.description.isEmpty())
                w.writeTextElement(QStringLiteral("Description"), c.description);
            writeNoteRefs(MemoTarget::Code, c.id);
            writeCodes(c.id);
            w.writeEndElement();
        }
    };
    writeCodes(0);
    w.writeEndElement(); // Codes
    w.writeEndElement(); // CodeBook

    if (!vars.isEmpty()) {
        w.writeStartElement(QStringLiteral("Variables"));
        for (const Variable &v : vars) {
            w.writeEmptyElement(QStringLiteral("Variable"));
            w.writeAttribute(QStringLiteral("guid"), varGuid.value(v.id));
            w.writeAttribute(QStringLiteral("name"), v.name);
            w.writeAttribute(QStringLiteral("typeOfVariable"), qdpxVariableType(v.type));
        }
        w.writeEndElement();
    }

    QHash<QString, QByteArray> sourceFiles;
    QHash<qint64, QString> docGuid;
    w.writeStartElement(QStringLiteral("Sources"));
    for (const Document &d : docs) {
        const QString guid = d.uuid.isEmpty() ? newUuid() : d.uuid;
        docGuid.insert(d.id, guid);
        const QString text = project.documentText(d.id);
        const QString fileName = guid + QStringLiteral(".txt");
        sourceFiles.insert(fileName, text.toUtf8());

        w.writeStartElement(QStringLiteral("TextSource"));
        w.writeAttribute(QStringLiteral("guid"), guid);
        w.writeAttribute(QStringLiteral("name"), d.name);
        w.writeAttribute(QStringLiteral("plainTextPath"), QStringLiteral("internal://") + fileName);
        w.writeAttribute(QStringLiteral("creatingUser"), userGuid(project.author()));
        w.writeAttribute(QStringLiteral("creationDateTime"), xmlDate(d.importedAt));
        for (const Segment &s : segmentsByDoc.value(d.id)) {
            w.writeStartElement(QStringLiteral("PlainTextSelection"));
            w.writeAttribute(QStringLiteral("guid"), s.uuid.isEmpty() ? newUuid() : s.uuid);
            w.writeAttribute(QStringLiteral("name"), TextUtils::excerpt(text, s.start, s.end, 60));
            w.writeAttribute(QStringLiteral("startPosition"), QString::number(TextUtils::utf16ToCodePoint(text, s.start)));
            w.writeAttribute(QStringLiteral("endPosition"), QString::number(TextUtils::utf16ToCodePoint(text, s.end)));
            w.writeAttribute(QStringLiteral("creatingUser"), userGuid(s.author));
            w.writeAttribute(QStringLiteral("creationDateTime"), xmlDate(s.createdAt));
            if (!s.comment.isEmpty())
                w.writeTextElement(QStringLiteral("Description"), s.comment);
            w.writeStartElement(QStringLiteral("Coding"));
            w.writeAttribute(QStringLiteral("guid"), newUuid());
            w.writeAttribute(QStringLiteral("creatingUser"), userGuid(s.author));
            w.writeAttribute(QStringLiteral("creationDateTime"), xmlDate(s.createdAt));
            w.writeEmptyElement(QStringLiteral("CodeRef"));
            w.writeAttribute(QStringLiteral("targetGUID"), codeGuid.value(s.codeId));
            w.writeEndElement(); // Coding
            writeNoteRefs(MemoTarget::Segment, s.id);
            w.writeEndElement(); // PlainTextSelection
        }
        for (const LinkEnd &e : linkEndsByDoc.value(d.id)) {
            w.writeEmptyElement(QStringLiteral("PlainTextSelection"));
            w.writeAttribute(QStringLiteral("guid"), e.guid);
            w.writeAttribute(QStringLiteral("name"), TextUtils::excerpt(text, e.start, e.end, 60));
            w.writeAttribute(QStringLiteral("startPosition"), QString::number(TextUtils::utf16ToCodePoint(text, e.start)));
            w.writeAttribute(QStringLiteral("endPosition"), QString::number(TextUtils::utf16ToCodePoint(text, e.end)));
            w.writeAttribute(QStringLiteral("creatingUser"), userGuid(e.link->author));
            w.writeAttribute(QStringLiteral("creationDateTime"), xmlDate(e.link->createdAt));
        }
        for (const Variable &v : vars) {
            const QString value = values.value(d.id).value(v.id);
            if (value.isEmpty())
                continue;
            w.writeStartElement(QStringLiteral("VariableValue"));
            w.writeEmptyElement(QStringLiteral("VariableRef"));
            w.writeAttribute(QStringLiteral("targetGUID"), varGuid.value(v.id));
            w.writeTextElement(valueElement(v.type), toQdpxValue(v.type, value));
            w.writeEndElement();
        }
        writeNoteRefs(MemoTarget::Document, d.id);
        w.writeEndElement(); // TextSource
    }
    w.writeEndElement(); // Sources

    // Memos of groups and sets travel as the set's Description: as notes nothing would refer to
    // them, and an importer would add them again as project memos.
    QList<Memo> notes;
    for (const Memo &m : memos)
        if (m.targetType != MemoTarget::Group && m.targetType != MemoTarget::DocumentSet)
            notes << m;
    if (!notes.isEmpty()) {
        w.writeStartElement(QStringLiteral("Notes"));
        for (const Memo &m : notes) {
            w.writeStartElement(QStringLiteral("Note"));
            w.writeAttribute(QStringLiteral("guid"), m.uuid);
            w.writeAttribute(QStringLiteral("name"), m.title.isEmpty() ? QObject::tr("Memo") : m.title);
            w.writeAttribute(QStringLiteral("creatingUser"), userGuid(m.author));
            w.writeAttribute(QStringLiteral("creationDateTime"), xmlDate(m.createdAt));
            w.writeAttribute(QStringLiteral("modifiedDateTime"), xmlDate(m.modifiedAt));
            w.writeTextElement(QStringLiteral("PlainTextContent"), m.body);
            w.writeEndElement();
        }
        w.writeEndElement();
    }

    if (!links.isEmpty()) {
        w.writeStartElement(QStringLiteral("Links"));
        for (const DocumentLink &l : links) {
            w.writeEmptyElement(QStringLiteral("Link"));
            w.writeAttribute(QStringLiteral("guid"), l.uuid);
            if (!l.comment.isEmpty())
                w.writeAttribute(QStringLiteral("name"), l.comment);
            w.writeAttribute(QStringLiteral("direction"), QStringLiteral("Bidirectional"));
            w.writeAttribute(QStringLiteral("originGUID"), linkEndGuid(l.uuid, true));
            w.writeAttribute(QStringLiteral("targetGUID"), linkEndGuid(l.uuid, false));
        }
        w.writeEndElement();
    }

    w.writeStartElement(QStringLiteral("Sets"));
    for (int i = 0; i < groups.size(); ++i) {
        const DocumentGroup &g = groups.at(i);
        w.writeStartElement(QStringLiteral("Set"));
        w.writeAttribute(QStringLiteral("guid"), groupSetGuid(i));
        w.writeAttribute(QStringLiteral("name"), g.name);
        const QList<Memo> groupMemos = memosFor(MemoTarget::Group, g.id);
        if (!groupMemos.isEmpty())
            w.writeTextElement(QStringLiteral("Description"), groupMemos.first().body);
        for (const Document &d : docs) {
            if (d.groupId != g.id)
                continue;
            w.writeEmptyElement(QStringLiteral("MemberSource"));
            w.writeAttribute(QStringLiteral("targetGUID"), docGuid.value(d.id));
        }
        w.writeEndElement();
    }
    for (const DocumentSet &s : project.documentSets()) {
        w.writeStartElement(QStringLiteral("Set"));
        w.writeAttribute(QStringLiteral("guid"), s.uuid);
        w.writeAttribute(QStringLiteral("name"), s.name);
        const QList<Memo> setMemos = memosFor(MemoTarget::DocumentSet, s.id);
        if (!setMemos.isEmpty())
            w.writeTextElement(QStringLiteral("Description"), setMemos.first().body);
        for (qint64 id : project.documentSetMembers(s.id)) {
            w.writeEmptyElement(QStringLiteral("MemberSource"));
            w.writeAttribute(QStringLiteral("targetGUID"), docGuid.value(id));
        }
        w.writeEndElement();
    }
    w.writeEndElement(); // Sets

    writeNoteRefs(MemoTarget::Project, 0);
    w.writeEndElement(); // Project
    w.writeEndDocument();

    // The archive is built in memory and saved with QSaveFile, so an existing file is replaced only
    // once the new one is complete (QZipWriter::close() closes its device, which QSaveFile forbids).
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    QZipWriter zip(&buffer);
    zip.setCompressionPolicy(QZipWriter::AutoCompress);
    zip.addFile(QStringLiteral("project.qde"), xml);
    zip.addDirectory(QStringLiteral("sources"));
    for (auto it = sourceFiles.cbegin(); it != sourceFiles.cend(); ++it)
        zip.addFile(QStringLiteral("sources/") + it.key(), it.value());
    zip.close();
    QSaveFile file(path);
    if (zip.status() != QZipWriter::NoError || !file.open(QIODevice::WriteOnly)
        || file.write(buffer.data()) != buffer.data().size() || !file.commit()) {
        *error = QObject::tr("Impossibile scrivere %1").arg(path);
        return false;
    }
    return true;
}

// ============================================================================ import

namespace {

struct PendingSelection {
    QString guid;
    int start = 0;
    int end = 0;
    QString description;
    QString user;
    QDateTime created;
    QStringList codeGuids;
    QStringList noteGuids;
};

struct PendingSource {
    QString guid;
    QString name;
    QString plainTextPath;
    QString inlineText;
    QDateTime created;
    QList<PendingSelection> selections;
    QList<std::pair<QString, QString>> variableValues; // variable guid -> value
    QStringList noteGuids;
};

struct PendingNote {
    QString guid;
    QString name;
    QString body;
    QString user;
    QDateTime created;
};

struct PendingSet {
    QString guid;
    QString name;
    QString description;
    QStringList members; // source guids
};

struct PendingLink {
    QString guid;
    QString name;
    QString origin; // object guids
    QString target;
};

struct TextRange {
    qint64 documentId = 0;
    int start = 0;
    int end = 0;
};

struct Importer {
    Project &project;
    QZipReader &zip;
    QString qdpxDir;
    QXmlStreamReader r;

    QHash<QString, QString> users;     // guid -> name
    QHash<QString, qint64> codes;      // guid -> id
    QHash<QString, std::pair<qint64, VariableType>> variables;
    QList<PendingSource> sources;
    QList<PendingNote> notes;
    QString origin; // Project/@origin, e.g. "Tessera 0.1.0"
    QList<PendingSet> sets;
    QList<PendingLink> links;
    QList<std::tuple<MemoTarget, QString, QString>> noteRefs; // target type, target guid, note guid
    QStringList projectNoteRefs;
    QStringList warnings;
    int skippedSources = 0;

    Importer(Project &p, QZipReader &z, const QString &dir, const QByteArray &xml)
        : project(p), zip(z), qdpxDir(dir), r(xml) {}

    QDateTime date(const QString &attr) const
    {
        return QDateTime::fromString(r.attributes().value(attr).toString(), Qt::ISODate);
    }
    QString attr(const QString &name) const { return r.attributes().value(name).toString(); }

    void parseCode(qint64 parentId, int index, int depth)
    {
        const QString guid = attr(QStringLiteral("guid"));
        const QString name = attr(QStringLiteral("name"));
        QColor color(attr(QStringLiteral("color")));
        if (!color.isValid())
            color = TextUtils::paletteColor(index);
        // A repeated GUID gets a new one, so a later export stays valid.
        const qint64 id = project.addCode(parentId, name.isEmpty() ? QObject::tr("(senza nome)") : name, color,
                                          codes.contains(guid) ? QString() : guid);
        if (!codes.contains(guid))
            codes.insert(guid, id);
        int childIndex = 0;
        while (r.readNextStartElement()) {
            if (r.name() == QLatin1String("Code")) {
                // Deeper nesting is skipped: a crafted file could otherwise exhaust the stack.
                if (depth + 1 < kMaxCodeDepth)
                    parseCode(id, childIndex++, depth + 1);
                else
                    r.skipCurrentElement();
            } else if (r.name() == QLatin1String("Description")) {
                project.setCodeDescription(id, r.readElementText());
            } else if (r.name() == QLatin1String("NoteRef")) {
                noteRefs.append({MemoTarget::Code, guid, attr(QStringLiteral("targetGUID"))});
                r.skipCurrentElement();
            } else {
                r.skipCurrentElement();
            }
        }
    }

    void parseSelection(PendingSource &src)
    {
        PendingSelection sel;
        sel.guid = attr(QStringLiteral("guid"));
        sel.start = attr(QStringLiteral("startPosition")).toInt();
        sel.end = attr(QStringLiteral("endPosition")).toInt();
        sel.user = attr(QStringLiteral("creatingUser"));
        sel.created = date(QStringLiteral("creationDateTime"));
        while (r.readNextStartElement()) {
            if (r.name() == QLatin1String("Description")) {
                sel.description = r.readElementText();
            } else if (r.name() == QLatin1String("Coding")) {
                while (r.readNextStartElement()) {
                    if (r.name() == QLatin1String("CodeRef"))
                        sel.codeGuids << attr(QStringLiteral("targetGUID"));
                    r.skipCurrentElement();
                }
            } else if (r.name() == QLatin1String("NoteRef")) {
                sel.noteGuids << attr(QStringLiteral("targetGUID"));
                r.skipCurrentElement();
            } else {
                r.skipCurrentElement();
            }
        }
        src.selections << sel;
    }

    void parseVariableValue(PendingSource &src)
    {
        QString varGuid, value;
        while (r.readNextStartElement()) {
            if (r.name() == QLatin1String("VariableRef")) {
                varGuid = attr(QStringLiteral("targetGUID"));
                r.skipCurrentElement();
            } else if (r.name().endsWith(QLatin1String("Value"))) {
                value = r.readElementText();
            } else {
                r.skipCurrentElement();
            }
        }
        if (!varGuid.isEmpty())
            src.variableValues.append({varGuid, value});
    }

    // Parses the children of a text-bearing element (TextSource or PDF Representation).
    void parseTextSourceBody(PendingSource &src)
    {
        while (r.readNextStartElement()) {
            const auto n = r.name();
            if (n == QLatin1String("PlainTextContent"))
                src.inlineText = r.readElementText();
            else if (n == QLatin1String("PlainTextSelection"))
                parseSelection(src);
            else if (n == QLatin1String("VariableValue"))
                parseVariableValue(src);
            else if (n == QLatin1String("NoteRef")) {
                src.noteGuids << attr(QStringLiteral("targetGUID"));
                r.skipCurrentElement();
            } else
                r.skipCurrentElement();
        }
    }

    void parseSources()
    {
        while (r.readNextStartElement()) {
            const auto n = r.name();
            if (n == QLatin1String("TextSource")) {
                PendingSource src;
                src.guid = attr(QStringLiteral("guid"));
                src.name = attr(QStringLiteral("name"));
                src.plainTextPath = attr(QStringLiteral("plainTextPath"));
                src.created = date(QStringLiteral("creationDateTime"));
                parseTextSourceBody(src);
                sources << src;
            } else if (n == QLatin1String("PDFSource")) {
                PendingSource src;
                src.guid = attr(QStringLiteral("guid"));
                src.name = attr(QStringLiteral("name"));
                src.created = date(QStringLiteral("creationDateTime"));
                bool hasRepresentation = false;
                while (r.readNextStartElement()) {
                    if (r.name() == QLatin1String("Representation")) {
                        hasRepresentation = true;
                        src.plainTextPath = attr(QStringLiteral("plainTextPath"));
                        parseTextSourceBody(src);
                    } else if (r.name() == QLatin1String("VariableValue")) {
                        parseVariableValue(src);
                    } else {
                        r.skipCurrentElement();
                    }
                }
                if (hasRepresentation)
                    sources << src;
                else
                    ++skippedSources;
            } else {
                ++skippedSources; // pictures, audio, video
                r.skipCurrentElement();
            }
        }
    }

    void parseNotes()
    {
        while (r.readNextStartElement()) {
            if (r.name() != QLatin1String("Note")) {
                r.skipCurrentElement();
                continue;
            }
            PendingNote note;
            note.guid = attr(QStringLiteral("guid"));
            note.name = attr(QStringLiteral("name"));
            note.user = attr(QStringLiteral("creatingUser"));
            note.created = date(QStringLiteral("creationDateTime"));
            const QString path = attr(QStringLiteral("plainTextPath"));
            while (r.readNextStartElement()) {
                if (r.name() == QLatin1String("PlainTextContent"))
                    note.body = r.readElementText();
                else
                    r.skipCurrentElement();
            }
            if (note.body.isEmpty() && !path.isEmpty())
                note.body = readSourceText(path);
            notes << note;
        }
    }

    void parseSets()
    {
        while (r.readNextStartElement()) {
            if (r.name() != QLatin1String("Set")) {
                r.skipCurrentElement();
                continue;
            }
            PendingSet set;
            set.guid = attr(QStringLiteral("guid"));
            set.name = attr(QStringLiteral("name"));
            while (r.readNextStartElement()) {
                if (r.name() == QLatin1String("MemberSource")) {
                    set.members << attr(QStringLiteral("targetGUID"));
                    r.skipCurrentElement();
                } else if (r.name() == QLatin1String("Description")) {
                    set.description = r.readElementText();
                } else {
                    r.skipCurrentElement();
                }
            }
            sets << set;
        }
    }

    void parseLinks()
    {
        while (r.readNextStartElement()) {
            if (r.name() == QLatin1String("Link"))
                links.append({attr(QStringLiteral("guid")), attr(QStringLiteral("name")),
                              attr(QStringLiteral("originGUID")), attr(QStringLiteral("targetGUID"))});
            r.skipCurrentElement();
        }
    }

    QString readSourceText(const QString &path)
    {
        QByteArray data;
        if (path.startsWith(QLatin1String("internal://"))) {
            const QString name = path.mid(11);
            data = zip.fileData(QStringLiteral("sources/") + name);
            if (data.isEmpty())
                data = zip.fileData(name);
        } else if (path.startsWith(QLatin1String("relative://")) || path.startsWith(QLatin1String("absolute://"))) {
            const QString file = path.startsWith(QLatin1String("relative://"))
                ? QDir(qdpxDir).filePath(path.mid(11))
                : path.mid(11);
            QFile f(file);
            if (isReadableSourcePath(file, qdpxDir) && f.open(QIODevice::ReadOnly))
                data = f.readAll();
        }
        if (data.startsWith("\xEF\xBB\xBF"))
            data.remove(0, 3);
        return QString::fromUtf8(data);
    }

    bool run(QString *error)
    {
        if (!r.readNextStartElement() || r.name() != QLatin1String("Project")) {
            *error = QObject::tr("project.qde non contiene un elemento Project");
            return false;
        }
        origin = attr(QStringLiteral("origin"));
        int topIndex = 0;
        while (r.readNextStartElement()) {
            const auto n = r.name();
            if (n == QLatin1String("Users")) {
                while (r.readNextStartElement()) {
                    users.insert(attr(QStringLiteral("guid")), attr(QStringLiteral("name")));
                    r.skipCurrentElement();
                }
            } else if (n == QLatin1String("CodeBook")) {
                while (r.readNextStartElement()) {
                    if (r.name() == QLatin1String("Codes")) {
                        while (r.readNextStartElement()) {
                            if (r.name() == QLatin1String("Code"))
                                parseCode(0, topIndex++, 0);
                            else
                                r.skipCurrentElement();
                        }
                    } else {
                        r.skipCurrentElement();
                    }
                }
            } else if (n == QLatin1String("Variables")) {
                while (r.readNextStartElement()) {
                    if (r.name() == QLatin1String("Variable")) {
                        const VariableType type = tesseraVariableType(attr(QStringLiteral("typeOfVariable")));
                        QString name = attr(QStringLiteral("name"));
                        const QString guid = attr(QStringLiteral("guid"));
                        qint64 id = project.addVariable(name, type, guid);
                        if (!id) // duplicate name
                            id = project.addVariable(name + QStringLiteral(" (2)"), type, guid);
                        variables.insert(guid, {id, type});
                    }
                    r.skipCurrentElement();
                }
            } else if (n == QLatin1String("Sources")) {
                parseSources();
            } else if (n == QLatin1String("Notes")) {
                parseNotes();
            } else if (n == QLatin1String("Sets")) {
                parseSets();
            } else if (n == QLatin1String("Links")) {
                parseLinks();
            } else if (n == QLatin1String("NoteRef")) {
                projectNoteRefs << attr(QStringLiteral("targetGUID"));
                r.skipCurrentElement();
            } else {
                r.skipCurrentElement();
            }
        }
        if (r.hasError()) {
            *error = QObject::tr("Errore XML in project.qde (riga %1): %2").arg(r.lineNumber()).arg(r.errorString());
            return false;
        }
        materialize();
        return true;
    }

    void materialize()
    {
        const qint64 defaultGroup = project.groups().isEmpty() ? project.addGroup(QObject::tr("Documenti"))
                                                               : project.groups().first().id;
        QHash<QString, qint64> docIds;
        QHash<QString, qint64> segmentIds;
        QHash<QString, TextRange> selectionRanges; // every valid selection, coded or not (link ends)
        int unresolvedCodes = 0;
        int invalidSelections = 0;
        for (const PendingSource &src : sources) {
            QString raw = src.inlineText;
            if (raw.isEmpty() && !src.plainTextPath.isEmpty())
                raw = readSourceText(src.plainTextPath);
            // Selection positions refer to the raw text; line endings are normalized to LF and
            // positions remapped through `map` (raw UTF-16 index -> normalized index).
            QString text;
            QList<int> map;
            text.reserve(raw.size());
            map.reserve(raw.size() + 1);
            for (qsizetype i = 0; i < raw.size(); ++i) {
                map << int(text.size());
                const QChar c = raw.at(i);
                if (c == QLatin1Char('\r')) {
                    if (i + 1 < raw.size() && raw.at(i + 1) == QLatin1Char('\n'))
                        continue; // the following LF produces the newline
                    text += QLatin1Char('\n');
                } else {
                    text += c;
                }
            }
            map << int(text.size());
            const bool repeatedGuid = docIds.contains(src.guid);
            const qint64 docId =
                project.addDocument(defaultGroup, src.name, text, {}, repeatedGuid ? QString() : src.guid);
            if (!repeatedGuid)
                docIds.insert(src.guid, docId);
            for (const PendingSelection &sel : src.selections) {
                if (sel.start < 0) {
                    ++invalidSelections;
                    continue;
                }
                const int rawStart = TextUtils::codePointToUtf16(raw, sel.start);
                const int rawEnd = TextUtils::codePointToUtf16(raw, sel.end);
                const int start = map.value(rawStart, -1);
                const int end = map.value(rawEnd, -1);
                if (start < 0 || end <= start || end > text.size()) {
                    ++invalidSelections;
                    continue;
                }
                if (!sel.guid.isEmpty() && !selectionRanges.contains(sel.guid))
                    selectionRanges.insert(sel.guid, {docId, start, end});
                for (int i = 0; i < sel.codeGuids.size(); ++i) {
                    const qint64 codeId = codes.value(sel.codeGuids[i]);
                    if (!codeId) {
                        ++unresolvedCodes;
                        continue;
                    }
                    const qint64 segId = project.addSegment(docId, codeId, start, end, 0, sel.description,
                                                            i == 0 ? sel.guid : QString());
                    if (i == 0)
                        segmentIds.insert(sel.guid, segId);
                }
                for (const QString &note : sel.noteGuids)
                    noteRefs.append({MemoTarget::Segment, sel.guid, note});
            }
            for (const auto &[varGuid, value] : src.variableValues) {
                const auto var = variables.value(varGuid);
                if (var.first)
                    project.setVariableValue(docId, var.first, fromQdpxValue(var.second, value));
            }
            for (const QString &note : src.noteGuids)
                noteRefs.append({MemoTarget::Document, src.guid, note});
        }

        // Sets exported from Tessera groups become groups again (a document goes to the first one
        // that lists it). Every other set becomes a document set, except in files of Tessera 0.1.0:
        // it exported only groups, with random GUIDs, while newer files always have at least one
        // recognizable group.
        QSet<QUuid> groupGuids;
        for (qsizetype i = 0; i < sets.size(); ++i)
            groupGuids.insert(QUuid(groupSetGuid(i)));
        const bool onlyGroups =
            origin.startsWith(QLatin1String("Tessera ")) &&
            std::none_of(sets.cbegin(), sets.cend(), [&](const PendingSet &s) { return groupGuids.contains(QUuid(s.guid)); });
        auto addSetMemo = [&](MemoTarget type, qint64 target, const PendingSet &set) {
            if (set.description.isEmpty())
                return;
            Memo memo;
            memo.targetType = type;
            memo.targetId = target;
            memo.title = set.name;
            memo.body = set.description;
            project.saveMemo(memo);
        };
        QSet<QString> grouped;
        bool createdGroups = false;
        for (const PendingSet &set : sets) {
            if (onlyGroups || groupGuids.contains(QUuid(set.guid))) {
                qint64 groupId = 0;
                for (const QString &m : set.members) {
                    if (!docIds.contains(m) || grouped.contains(m))
                        continue;
                    if (!groupId) {
                        groupId = project.addGroup(set.name);
                        createdGroups = true;
                        addSetMemo(MemoTarget::Group, groupId, set);
                    }
                    project.moveDocument(docIds.value(m), groupId);
                    grouped.insert(m);
                }
            } else {
                QList<qint64> members;
                for (const QString &m : set.members)
                    if (docIds.contains(m))
                        members << docIds.value(m);
                const qint64 setId = project.addDocumentSet(set.name, members, set.guid);
                addSetMemo(MemoTarget::DocumentSet, setId, set);
            }
        }
        if (createdGroups && project.documentsInGroup(defaultGroup).isEmpty())
            project.deleteGroup(defaultGroup);

        // Notes -> memos, attached to the objects that reference them.
        QHash<QString, PendingNote> notesByGuid;
        for (const PendingNote &n : notes)
            notesByGuid.insert(n.guid, n);
        QSet<QString> usedNotes;
        auto makeMemo = [&](MemoTarget type, qint64 target, const PendingNote &n) {
            Memo memo;
            memo.targetType = type;
            memo.targetId = target;
            memo.title = n.name;
            memo.body = n.body;
            memo.author = users.value(n.user);
            memo.createdAt = n.created;
            memo.uuid = usedNotes.contains(n.guid) ? QString() : n.guid;
            project.saveMemo(memo);
            usedNotes.insert(n.guid);
        };
        for (const auto &[type, targetGuid, noteGuid] : noteRefs) {
            if (!notesByGuid.contains(noteGuid))
                continue;
            qint64 target = 0;
            if (type == MemoTarget::Code)
                target = codes.value(targetGuid);
            else if (type == MemoTarget::Document)
                target = docIds.value(targetGuid);
            else if (type == MemoTarget::Segment)
                target = segmentIds.value(targetGuid);
            if (target)
                makeMemo(type, target, notesByGuid.value(noteGuid));
        }
        for (const PendingNote &n : notes) {
            if (!usedNotes.contains(n.guid))
                makeMemo(MemoTarget::Project, 0, n);
        }

        // Links between two text selections become document links; REFI-QDA also links other
        // objects (codes, notes, whole sources), which Tessera cannot represent.
        int importedLinks = 0, skippedLinks = 0;
        QSet<QString> linkGuids;
        for (const PendingLink &l : links) {
            if (!selectionRanges.contains(l.origin) || !selectionRanges.contains(l.target)) {
                ++skippedLinks;
                continue;
            }
            const TextRange from = selectionRanges.value(l.origin), to = selectionRanges.value(l.target);
            const bool repeatedGuid = l.guid.isEmpty() || linkGuids.contains(l.guid);
            if (project.addLink(from.documentId, from.start, from.end, to.documentId, to.start, to.end, l.name,
                                repeatedGuid ? QString() : l.guid)) {
                ++importedLinks;
                linkGuids.insert(l.guid);
            } else {
                ++skippedLinks;
            }
        }

        report = QObject::tr("Importati: %1 documenti, %2 codici, %3 segmenti, %4 memo, %5 variabili.")
                     .arg(sources.size())
                     .arg(codes.size())
                     .arg(project.allSegments().size())
                     .arg(project.memos().size())
                     .arg(variables.size());
        if (importedLinks)
            report += QLatin1Char(' ') + QObject::tr("Collegamenti tra documenti: %1.").arg(importedLinks);
        if (skippedLinks)
            warnings << QObject::tr("%1 collegamenti tra oggetti diversi da brani di testo ignorati.").arg(skippedLinks);
        if (skippedSources)
            warnings << QObject::tr("%1 fonti non testuali (immagini, audio, video, PDF senza testo) ignorate.")
                            .arg(skippedSources);
        if (unresolvedCodes)
            warnings << QObject::tr("%1 codifiche con codice inesistente ignorate.").arg(unresolvedCodes);
        if (invalidSelections)
            warnings << QObject::tr("%1 selezioni fuori dal testo ignorate.").arg(invalidSelections);
    }

    QString report;
};

} // namespace

bool isReadableSourcePath(const QString &file, const QString &qdpxDir)
{
    // Decided on the string alone: any file system call on a network path would already reach the host.
    const QString raw = QDir::fromNativeSeparators(file);
    if (raw.startsWith(QLatin1String("//"))) {
        const QString dir = QDir::cleanPath(QDir::fromNativeSeparators(qdpxDir));
        if (!dir.startsWith(QLatin1String("//"))
            || !QDir::cleanPath(raw).startsWith(dir + QLatin1Char('/'), Qt::CaseInsensitive))
            return false;
    }
    return QFileInfo(raw).isFile(); // not a directory or a device such as CON
}

bool importProject(Project &project, const QString &path, QString *report, QString *error)
{
    QZipReader zip(path);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError) {
        *error = QObject::tr("%1 non è un file QDPX valido").arg(QFileInfo(path).fileName());
        return false;
    }
    QByteArray xml = zip.fileData(QStringLiteral("project.qde"));
    if (xml.isEmpty()) {
        // Some exporters place project.qde in a folder.
        for (const QZipReader::FileInfo &fi : zip.fileInfoList()) {
            if (fi.filePath.endsWith(QLatin1String(".qde"), Qt::CaseInsensitive)) {
                xml = zip.fileData(fi.filePath);
                break;
            }
        }
    }
    if (xml.isEmpty()) {
        *error = QObject::tr("Il file QDPX non contiene project.qde");
        return false;
    }

    Importer importer(project, zip, QFileInfo(path).absolutePath(), xml);
    project.beginBatch();
    if (!importer.run(error)) {
        project.rollbackBatch();
        return false;
    }
    project.commitBatch();
    *report = importer.report;
    if (!importer.warnings.isEmpty())
        *report += QLatin1Char('\n') + importer.warnings.join(QLatin1Char('\n'));
    return true;
}

} // namespace Qdpx
