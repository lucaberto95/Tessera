#include "core/TeamSync.h"

#include "core/Project.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

// File format ("tessera-<project uuid>.sync/<member>.tsrlog"), version 1. UTF-8 text, JSON Lines (one JSON object per line):
//   line 1     {"t":"tsrlog","v":1,"project":<project uuid>,"projectName":..,"member":..,"gen":n,"written":iso}
//   records    {"t":"doc"|"code"|"set"|"seg"|"link"|"memo"|"sum","id":<uuid>,...}
//   last line  {"t":"end","n":<number of records>}
// The file is rewritten as a whole, atomically, by its only author (a snapshot of their own work, not an
// append-only journal: a reader that missed any number of publications still gets everything). A file without
// the last line is a partial one (the cloud service has not finished copying it) and is ignored as a whole.
// Records the author wrote carry their uuid and the creation/modification time; everything that references
// something else does so by uuid (documents, codes, segments, document sets) or by name (groups).
//
// What the reader does is idempotent: it remembers, per author, the records already applied (uuid -> hash of
// the text they had) and per file the digest read last, so a second synchronisation changes nothing and a
// record the reader deleted is not brought back. Rules when the same text was changed on both sides:
//   - not changed here since it was applied (or never applied and empty here): the author's new version replaces it;
//   - changed here too: both texts are kept (the author's is appended, headed by their name and date) and the
//     case is listed in the report;
//   - codings, links: matched by uuid, then by position; documents must have the same content on both sides
//     (otherwise positions would not fit), codes by uuid, then by name under the same parent.

namespace TeamSync {

namespace {


QString cleanName(QString s)
{
    s.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\s]+")), QStringLiteral("_"));
    return s.trimmed();
}

QString sha1(const QByteArray &data) { return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex()); }
QString textHash(const QString &s) { return sha1(s.toUtf8()); }

QString docHash(const Project &p, const Document &d)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(documentKindToString(d.kind).toUtf8());
    h.addData(QByteArray(1, '\0'));
    h.addData(p.documentText(d.id).toUtf8());
    h.addData(QByteArray(1, '\0'));
    h.addData(p.documentData(d.id));
    return QString::fromLatin1(h.result().toHex());
}

QString iso(const QDateTime &dt) { return dt.isValid() ? dt.toUTC().toString(Qt::ISODateWithMs) : QString(); }
QDateTime fromIso(const QString &s) { return s.isEmpty() ? QDateTime() : QDateTime::fromString(s, Qt::ISODateWithMs); }

bool isInside(const QString &file, const QString &folder)
{
    const QString f = QDir::cleanPath(QFileInfo(file).absolutePath()).toCaseFolded();
    QString d = QDir::cleanPath(QFileInfo(folder).absoluteFilePath()).toCaseFolded();
    if (!d.endsWith(QLatin1Char('/')))
        d += QLatin1Char('/');
    return (f + QLatin1Char('/')).startsWith(d);
}

// ---------------------------------------------------------------- state

struct FileState {
    QString sha;
    int gen = 0;
};
struct MemberState {
    QHash<QString, QString> applied; // record key -> hash of its text when applied
    QHash<QString, QString> alias;   // uuid in the log -> uuid of the local record it was matched with
};
struct State {
    int selfGen = 0;
    QHash<QString, FileState> files;
    QHash<QString, MemberState> members;
};

QJsonObject toJson(const QHash<QString, QString> &h)
{
    QJsonObject o;
    for (auto it = h.cbegin(); it != h.cend(); ++it)
        o.insert(it.key(), it.value());
    return o;
}
QHash<QString, QString> hashFromJson(const QJsonObject &o)
{
    QHash<QString, QString> h;
    for (auto it = o.begin(); it != o.end(); ++it)
        h.insert(it.key(), it.value().toString());
    return h;
}

State loadState(const Project &p)
{
    State s;
    const QJsonObject root = QJsonDocument::fromJson(p.meta(QLatin1String(StateKey)).toUtf8()).object();
    s.selfGen = root.value(QStringLiteral("selfGen")).toInt();
    const QJsonObject files = root.value(QStringLiteral("files")).toObject();
    for (auto it = files.begin(); it != files.end(); ++it)
        s.files.insert(it.key(), {it.value().toObject().value(QStringLiteral("sha")).toString(),
                                  it.value().toObject().value(QStringLiteral("gen")).toInt()});
    const QJsonObject members = root.value(QStringLiteral("members")).toObject();
    for (auto it = members.begin(); it != members.end(); ++it) {
        MemberState m;
        m.applied = hashFromJson(it.value().toObject().value(QStringLiteral("applied")).toObject());
        m.alias = hashFromJson(it.value().toObject().value(QStringLiteral("alias")).toObject());
        s.members.insert(it.key(), m);
    }
    return s;
}

void saveState(Project &p, const State &s)
{
    QJsonObject files, members;
    for (auto it = s.files.cbegin(); it != s.files.cend(); ++it)
        files.insert(it.key(), QJsonObject{{QStringLiteral("sha"), it.value().sha}, {QStringLiteral("gen"), it.value().gen}});
    for (auto it = s.members.cbegin(); it != s.members.cend(); ++it)
        members.insert(it.key(), QJsonObject{{QStringLiteral("applied"), toJson(it.value().applied)},
                                             {QStringLiteral("alias"), toJson(it.value().alias)}});
    const QJsonObject root{{QStringLiteral("selfGen"), s.selfGen}, {QStringLiteral("files"), files}, {QStringLiteral("members"), members}};
    p.setMeta(QLatin1String(StateKey), QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
}

// ---------------------------------------------------------------- reading a log

struct LogFile {
    bool valid = false;
    QString problem;       // why the file is not usable
    QJsonObject header;
    QList<QJsonObject> docs, codes, sets, segs, links, memos, sums;
    int declared = 0, parsed = 0;
    QString sha;
    QString member() const { return header.value(QStringLiteral("member")).toString(); }
    int gen() const { return header.value(QStringLiteral("gen")).toInt(); }
};

LogFile readLog(const QString &path, const QString &projectUuid)
{
    LogFile lf;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        lf.problem = QCoreApplication::translate("TeamSync", "impossibile aprire il file");
        return lf;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    lf.sha = sha1(bytes);
    QList<QByteArray> lines = bytes.split('\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty())
        lines.removeLast();
    if (lines.isEmpty()) {
        lf.problem = QCoreApplication::translate("TeamSync", "file vuoto (forse ancora in copia dal servizio cloud)");
        return lf;
    }
    QJsonParseError err;
    const QJsonObject header = QJsonDocument::fromJson(lines.first(), &err).object();
    if (err.error != QJsonParseError::NoError || header.value(QStringLiteral("t")).toString() != QLatin1String("tsrlog")) {
        lf.problem = QCoreApplication::translate("TeamSync", "non è un file di sincronizzazione di Tessera");
        return lf;
    }
    lf.header = header;
    if (header.value(QStringLiteral("v")).toInt() > FormatVersion) {
        lf.problem = QCoreApplication::translate("TeamSync", "formato più recente di questa versione di Tessera: aggiorna il programma");
        return lf;
    }
    if (header.value(QStringLiteral("member")).toString().trimmed().isEmpty()) {
        lf.problem = QCoreApplication::translate("TeamSync", "manca il nome del membro");
        return lf;
    }
    if (header.value(QStringLiteral("project")).toString() != projectUuid) {
        lf.problem = QCoreApplication::translate("TeamSync", "appartiene a un altro progetto");
        return lf;
    }
    const QJsonObject footer = lines.size() > 1 ? QJsonDocument::fromJson(lines.last()).object() : QJsonObject();
    if (footer.value(QStringLiteral("t")).toString() != QLatin1String("end")) {
        lf.problem = QCoreApplication::translate("TeamSync", "file incompleto (forse ancora in copia dal servizio cloud): riprova tra poco");
        return lf;
    }
    lf.declared = footer.value(QStringLiteral("n")).toInt();
    for (int i = 1; i < lines.size() - 1; ++i) {
        const QJsonObject o = QJsonDocument::fromJson(lines.at(i), &err).object();
        if (err.error != QJsonParseError::NoError || o.isEmpty())
            continue; // a damaged line: the rest of the file is still good
        const QString t = o.value(QStringLiteral("t")).toString();
        if (t == QLatin1String("doc")) lf.docs << o;
        else if (t == QLatin1String("code")) lf.codes << o;
        else if (t == QLatin1String("set")) lf.sets << o;
        else if (t == QLatin1String("seg")) lf.segs << o;
        else if (t == QLatin1String("link")) lf.links << o;
        else if (t == QLatin1String("memo")) lf.memos << o;
        else if (t == QLatin1String("sum")) lf.sums << o;
        else continue; // a record type of a later version
        ++lf.parsed;
    }
    lf.valid = true;
    return lf;
}

QString jstr(const QJsonObject &o, const char *k) { return o.value(QLatin1String(k)).toString(); }
int jint(const QJsonObject &o, const char *k) { return o.value(QLatin1String(k)).toInt(); }
double jdbl(const QJsonObject &o, const char *k) { return o.value(QLatin1String(k)).toDouble(); }
qint64 jll(const QJsonObject &o, const char *k) { return qint64(o.value(QLatin1String(k)).toDouble()); }

// ---------------------------------------------------------------- applying

QString segmentKey(const Segment &s, qint64 doc, qint64 code)
{
    switch (s.kind) {
    case SegmentKind::Text:
        return QStringLiteral("t|%1|%2|%3|%4").arg(doc).arg(code).arg(s.start).arg(s.end);
    case SegmentKind::Area:
        return QStringLiteral("a|%1|%2|%3|%4|%5|%6|%7")
            .arg(doc).arg(code).arg(s.page)
            .arg(s.x1, 0, 'f', 3).arg(s.y1, 0, 'f', 3).arg(s.x2, 0, 'f', 3).arg(s.y2, 0, 'f', 3);
    case SegmentKind::Time:
        return QStringLiteral("m|%1|%2|%3|%4").arg(doc).arg(code).arg(s.tStartMs).arg(s.tEndMs);
    }
    return {};
}

QString linkKey(qint64 d1, int s1, int e1, qint64 d2, int s2, int e2)
{
    return QStringLiteral("%1|%2|%3|%4|%5|%6").arg(d1).arg(s1).arg(e1).arg(d2).arg(s2).arg(e2);
}

QString joinText(const QString &existing, const QString &incoming, const QString &author, const QDateTime &when)
{
    if (existing.trimmed().isEmpty())
        return incoming;
    QString who = author;
    if (when.isValid())
        who += (who.isEmpty() ? QString() : QStringLiteral(", ")) + when.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"));
    const QChar dash(0x2014);
    return existing + QStringLiteral("\n\n") + dash + QLatin1Char(' ') + who + QLatin1Char(' ') + dash + QLatin1Char('\n') + incoming;
}

enum class TextAction { Same, Replace, Join };

// What to do with `incoming` when the local text is `current`; `appliedHash` is the hash of the text the last time
// this record was applied (empty if never).
TextAction reconcile(const QString &current, const QString &appliedHash, const QString &incoming)
{
    if (current == incoming)
        return TextAction::Same;
    if (!appliedHash.isEmpty() && textHash(current) == appliedHash)
        return TextAction::Replace; // untouched here since the author's previous version
    if (incoming.trimmed().isEmpty() || current.contains(incoming.trimmed()))
        return TextAction::Same;
    if (current.trimmed().isEmpty())
        return TextAction::Replace;
    return TextAction::Join;
}

class Applier {
public:
    Applier(Project &project, State &state, SyncReport &report) : p(project), st(state), r(report) { buildIndexes(); }

    bool failed() const { return m_failed; }
    void applyFile(const LogFile &lf, MemberReport &mr);

private:
    void buildIndexes();
    void fail()
    {
        if (!m_failed) {
            m_failed = true;
            r.ok = false;
            r.error = p.lastError();
        }
    }
    void resolveDocuments(const LogFile &lf, MemberReport &mr);
    qint64 ensureCode(const QString &uuid, MemberReport &mr);
    qint64 ensureSet(const QString &uuid);
    void applySegments(const LogFile &lf, MemberReport &mr);
    void applyLinks(const LogFile &lf, MemberReport &mr);
    void applyMemos(const LogFile &lf, MemberReport &mr);
    void applySummaries(const LogFile &lf, MemberReport &mr);
    void conflict(MemberReport &mr, const QString &line)
    {
        ++mr.conflicts;
        r.conflicts << line;
    }
    QString localUuid(const QString &logUuid) const { return ms->alias.value(logUuid, logUuid); }
    QString docName(qint64 id) const { return p.document(id).name; }

    Project &p;
    State &st;
    SyncReport &r;
    bool m_failed = false;
    MemberState *ms = nullptr;
    QString m_author;

    // Local indexes, kept up to date while applying.
    QHash<QString, Document> docByUuid;
    QHash<QString, QList<Document>> docsByName;
    QHash<qint64, QString> docHashes;
    QHash<QString, qint64> codeByUuid;
    QHash<QString, Segment> segByUuid, segByKey;
    QHash<QString, DocumentLink> linkByUuid;
    QSet<QString> linkKeys;
    QHash<QString, Memo> memoByUuid;
    QHash<QString, qint64> setByUuid, setByName;
    QHash<QString, qint64> groupByName;

    // Per file.
    QHash<QString, qint64> docMap, codeMap, segMap, setMap;
    QHash<QString, QJsonObject> codeRecs, setRecs;
    QSet<QString> codeBusy;
};

void Applier::buildIndexes()
{
    for (const Document &d : p.documents()) {
        docByUuid.insert(d.uuid, d);
        docsByName[d.name].append(d);
    }
    for (const Code &c : p.codes())
        codeByUuid.insert(c.uuid, c.id);
    for (const Segment &s : p.allSegments(true)) {
        segByUuid.insert(s.uuid, s);
        segByKey.insert(segmentKey(s, s.documentId, s.codeId), s);
    }
    for (const DocumentLink &l : p.links()) {
        linkByUuid.insert(l.uuid, l);
        linkKeys.insert(linkKey(l.fromDocumentId, l.fromStart, l.fromEnd, l.toDocumentId, l.toStart, l.toEnd));
    }
    for (const Memo &m : p.memos())
        memoByUuid.insert(m.uuid, m);
    for (const DocumentSet &s : p.documentSets()) {
        setByUuid.insert(s.uuid, s.id);
        setByName.insert(s.name, s.id);
    }
    for (const DocumentGroup &g : p.groups())
        groupByName.insert(g.name, g.id);
}

void Applier::resolveDocuments(const LogFile &lf, MemberReport &mr)
{
    const QString who = lf.member();
    for (const QJsonObject &rec : lf.docs) {
        const QString uuid = jstr(rec, "id"), name = jstr(rec, "name"), hash = jstr(rec, "hash");
        qint64 id = 0;
        bool different = false;
        auto hashOf = [&](const Document &d) {
            auto it = docHashes.constFind(d.id);
            if (it == docHashes.constEnd())
                it = docHashes.insert(d.id, docHash(p, d));
            return it.value();
        };
        const Document byUuid = docByUuid.value(uuid);
        if (byUuid.id) {
            if (hashOf(byUuid) == hash)
                id = byUuid.id;
            else
                different = true;
        } else {
            for (const Document &d : docsByName.value(name))
                if (hashOf(d) == hash) {
                    id = d.id;
                    break;
                }
        }
        if (!id) {
            r.warnings << (different ? QCoreApplication::translate("TeamSync", "%1: il documento «%2» ha un contenuto diverso nelle due copie: le sue codifiche non vengono applicate "
                                          "(le posizioni non coinciderebbero).").arg(who, name)
                                     : QCoreApplication::translate("TeamSync", "%1: il documento «%2» non c'è in questo progetto: le sue codifiche non vengono applicate.").arg(who, name));
        }
        docMap.insert(uuid, id);
    }
    Q_UNUSED(mr)
}

qint64 Applier::ensureCode(const QString &uuid, MemberReport &mr)
{
    auto cached = codeMap.constFind(uuid);
    if (cached != codeMap.constEnd())
        return cached.value();
    const QJsonObject rec = codeRecs.value(uuid);
    if (rec.isEmpty() || codeBusy.contains(uuid) || m_failed)
        return 0;
    codeBusy.insert(uuid);
    const QString parentUuid = jstr(rec, "parent");
    const qint64 parent = parentUuid.isEmpty() ? 0 : ensureCode(parentUuid, mr);
    qint64 id = 0;
    if (parentUuid.isEmpty() || parent) {
        const QString name = jstr(rec, "name");
        id = codeByUuid.value(uuid, 0);
        if (id) {
            if (p.code(id).name != name) {
                ++mr.conflicts;
                r.conflicts << QCoreApplication::translate("TeamSync", "Codice «%1» di %2: qui si chiama «%3». Resta il nome locale.").arg(name, jstr(rec, "by"), p.code(id).name);
            }
        } else {
            id = p.findCode(parent, name);
        }
        if (!id) {
            id = p.addCode(parent, name, QColor(jstr(rec, "color")), uuid);
            if (!id) {
                fail();
            } else {
                ++mr.codesNew;
                codeByUuid.insert(uuid, id);
            }
        }
        if (id && !jstr(rec, "desc").isEmpty() && p.code(id).description.isEmpty())
            p.setCodeDescription(id, jstr(rec, "desc"));
    }
    codeBusy.remove(uuid);
    codeMap.insert(uuid, id);
    return id;
}

qint64 Applier::ensureSet(const QString &uuid)
{
    auto cached = setMap.constFind(uuid);
    if (cached != setMap.constEnd())
        return cached.value();
    qint64 id = setByUuid.value(uuid, 0);
    if (!id)
        id = setByName.value(jstr(setRecs.value(uuid), "name"), 0);
    setMap.insert(uuid, id);
    return id;
}

void Applier::applySegments(const LogFile &lf, MemberReport &mr)
{
    for (const QJsonObject &rec : lf.segs) {
        if (m_failed)
            return;
        const QString uuid = jstr(rec, "id");
        const QString key = QStringLiteral("s:") + uuid;
        const QString comment = jstr(rec, "c");
        const QString author = jstr(rec, "a");
        const QDateTime created = fromIso(jstr(rec, "cr"));

        auto updateComment = [&](Segment local, const QString &applied) {
            switch (reconcile(local.comment, applied, comment)) {
            case TextAction::Same:
                break;
            case TextAction::Replace:
                ++mr.updated;
                if (!p.setSegmentComment(local.id, comment))
                    fail();
                break;
            case TextAction::Join:
                conflict(mr, QCoreApplication::translate("TeamSync", "Commento di una codifica di %1 in «%2» (codice «%3»): cambiato anche qui, tenuti entrambi i testi.")
                                 .arg(lf.member(), docName(local.documentId), p.code(local.codeId).name));
                if (!p.setSegmentComment(local.id, joinText(local.comment, comment, author, fromIso(jstr(rec, "mod")))))
                    fail();
                break;
            }
        };

        if (ms->applied.contains(key)) {
            const QString applied = ms->applied.value(key);
            if (applied == textHash(comment))
                continue;
            const Segment local = p.segment(segByUuid.value(localUuid(uuid)).id);
            if (local.id)
                updateComment(local, applied);
            ms->applied.insert(key, textHash(comment));
            continue;
        }
        const qint64 td = docMap.value(jstr(rec, "doc"), 0);
        const qint64 tc = td ? ensureCode(jstr(rec, "code"), mr) : 0;
        if (m_failed)
            return;
        if (!td || !tc) {
            ++mr.skipped;
            continue;
        }
        Segment s;
        s.kind = segmentKindFromString(jstr(rec, "k"));
        s.start = jint(rec, "s");
        s.end = jint(rec, "e");
        s.page = jint(rec, "pg");
        s.x1 = jdbl(rec, "x1"); s.y1 = jdbl(rec, "y1"); s.x2 = jdbl(rec, "x2"); s.y2 = jdbl(rec, "y2");
        s.tStartMs = jll(rec, "ms"); s.tEndMs = jll(rec, "me");
        const QString positional = segmentKey(s, td, tc);
        Segment existing = segByUuid.value(uuid);
        if (!existing.id)
            existing = segByKey.value(positional);
        if (existing.id) {
            if (existing.uuid != uuid)
                ms->alias.insert(uuid, existing.uuid);
            segMap.insert(uuid, existing.id);
            updateComment(existing, QString());
            ms->applied.insert(key, textHash(comment));
            continue;
        }
        qint64 id = 0;
        const int weight = jint(rec, "w");
        switch (s.kind) {
        case SegmentKind::Text: id = p.addSegment(td, tc, s.start, s.end, weight, comment, uuid); break;
        case SegmentKind::Area:
            id = p.addAreaSegment(td, tc, s.page, s.x1, s.y1, s.x2, s.y2, weight, comment, uuid, jstr(rec, "at"));
            break;
        case SegmentKind::Time: id = p.addTimeSegment(td, tc, s.tStartMs, s.tEndMs, weight, comment, uuid); break;
        }
        if (!id) {
            ++mr.skipped; // does not fit the document
            continue;
        }
        if (!p.setProvenance(Project::Provenance::Segment, id, author, created)) {
            fail();
            return;
        }
        const Segment made = p.segment(id);
        ++mr.segmentsNew;
        segMap.insert(uuid, id);
        segByUuid.insert(made.uuid, made);
        segByKey.insert(positional, made);
        ms->applied.insert(key, textHash(comment));
    }
}

void Applier::applyLinks(const LogFile &lf, MemberReport &mr)
{
    for (const QJsonObject &rec : lf.links) {
        if (m_failed)
            return;
        const QString uuid = jstr(rec, "id");
        const QString key = QStringLiteral("l:") + uuid;
        const QString comment = jstr(rec, "c");
        const QString author = jstr(rec, "a");

        auto updateComment = [&](const DocumentLink &local, const QString &applied) {
            switch (reconcile(local.comment, applied, comment)) {
            case TextAction::Same:
                break;
            case TextAction::Replace:
                ++mr.updated;
                if (!p.setLinkComment(local.id, comment))
                    fail();
                break;
            case TextAction::Join:
                conflict(mr, QCoreApplication::translate("TeamSync", "Commento di un collegamento di %1 tra «%2» e «%3»: cambiato anche qui, tenuti entrambi i testi.")
                                 .arg(lf.member(), docName(local.fromDocumentId), docName(local.toDocumentId)));
                if (!p.setLinkComment(local.id, joinText(local.comment, comment, author, fromIso(jstr(rec, "cr")))))
                    fail();
                break;
            }
        };

        if (ms->applied.contains(key)) {
            const QString applied = ms->applied.value(key);
            if (applied == textHash(comment))
                continue;
            const DocumentLink local = p.link(linkByUuid.value(localUuid(uuid)).id);
            if (local.id)
                updateComment(local, applied);
            ms->applied.insert(key, textHash(comment));
            continue;
        }
        const qint64 from = docMap.value(jstr(rec, "fd"), 0), to = docMap.value(jstr(rec, "td"), 0);
        if (!from || !to) {
            ++mr.skipped;
            continue;
        }
        const int fs = jint(rec, "fs"), fe = jint(rec, "fe"), ts = jint(rec, "ts"), te = jint(rec, "te");
        const QString k1 = linkKey(from, fs, fe, to, ts, te), k2 = linkKey(to, ts, te, from, fs, fe);
        DocumentLink existing = linkByUuid.value(uuid);
        if (!existing.id && (linkKeys.contains(k1) || linkKeys.contains(k2))) {
            for (const DocumentLink &l : p.linksForDocument(from))
                if (linkKey(l.fromDocumentId, l.fromStart, l.fromEnd, l.toDocumentId, l.toStart, l.toEnd) == k1 ||
                    linkKey(l.fromDocumentId, l.fromStart, l.fromEnd, l.toDocumentId, l.toStart, l.toEnd) == k2) {
                    existing = l;
                    break;
                }
        }
        if (existing.id) {
            if (existing.uuid != uuid)
                ms->alias.insert(uuid, existing.uuid);
            updateComment(existing, QString());
            ms->applied.insert(key, textHash(comment));
            continue;
        }
        const qint64 id = p.addLink(from, fs, fe, to, ts, te, comment, uuid);
        if (!id) {
            ++mr.skipped;
            continue;
        }
        if (!p.setProvenance(Project::Provenance::Link, id, author, fromIso(jstr(rec, "cr")))) {
            fail();
            return;
        }
        ++mr.linksNew;
        linkKeys.insert(k1);
        linkByUuid.insert(uuid, p.link(id));
        ms->applied.insert(key, textHash(comment));
    }
}

void Applier::applyMemos(const LogFile &lf, MemberReport &mr)
{
    for (const QJsonObject &rec : lf.memos) {
        if (m_failed)
            return;
        const QString uuid = jstr(rec, "id");
        const QString key = QStringLiteral("m:") + uuid;
        const QString title = jstr(rec, "title"), body = jstr(rec, "body"), author = jstr(rec, "a");
        const QDateTime created = fromIso(jstr(rec, "cr")), modified = fromIso(jstr(rec, "mod"));
        const QString hash = textHash(body);

        // Replaces or joins the body of a local memo.
        auto update = [&](Memo local, const QString &applied) {
            switch (reconcile(local.body, applied, body)) {
            case TextAction::Same:
                break;
            case TextAction::Replace:
                ++mr.updated;
                local.body = body;
                local.title = title;
                if (!p.saveMemo(local) ||
                    !p.setProvenance(Project::Provenance::Memo, local.id, local.author, local.createdAt, modified))
                    fail();
                break;
            case TextAction::Join:
                conflict(mr, QCoreApplication::translate("TeamSync", "Memo «%1» di %2: cambiato anche qui, tenuti entrambi i testi.")
                                 .arg(local.title.isEmpty() ? title : local.title, lf.member()));
                local.body = joinText(local.body, body, author, modified);
                if (local.title.isEmpty())
                    local.title = title;
                if (!p.saveMemo(local))
                    fail();
                break;
            }
            if (!m_failed)
                memoByUuid.insert(local.uuid, p.memo(local.id));
        };

        if (ms->applied.contains(key)) {
            const QString applied = ms->applied.value(key);
            if (applied == hash)
                continue;
            const Memo local = memoByUuid.value(localUuid(uuid));
            if (local.id)
                update(local, applied);
            ms->applied.insert(key, hash);
            continue;
        }

        const MemoTarget primary = memoTargetFromString(jstr(rec, "pt"));
        const bool elementMemo = Project::isLinkableMemoTarget(primary);
        QList<MemoLink> mapped;
        for (const QJsonValue &v : rec.value(QStringLiteral("els")).toArray()) {
            const QJsonObject el = v.toObject();
            const MemoTarget type = memoTargetFromString(jstr(el, "t"));
            const QString ref = jstr(el, "r");
            qint64 id = 0;
            switch (type) {
            case MemoTarget::Group: id = groupByName.value(ref, 0); break;
            case MemoTarget::Document: id = docMap.value(ref, 0); break;
            case MemoTarget::Code: id = ensureCode(ref, mr); break;
            case MemoTarget::Segment: id = segMap.value(ref, 0); break;
            case MemoTarget::DocumentSet: id = ensureSet(ref); break;
            default: break;
            }
            if (id)
                mapped << MemoLink{type, id};
        }
        if (m_failed)
            return;
        if (elementMemo && mapped.isEmpty()) {
            ++mr.skipped;
            continue;
        }

        Memo existing = memoByUuid.value(uuid);
        if (!existing.id) {
            if (primary == MemoTarget::Project)
                existing = p.memoFor(MemoTarget::Project, 0);
            else if (elementMemo)
                existing = p.memoFor(mapped.first().targetType, mapped.first().targetId);
            else
                for (const Memo &c : p.memos())
                    if (c.targetType == MemoTarget::Free && c.title == title && c.body == body) {
                        existing = c;
                        break;
                    }
        }
        if (existing.id) {
            if (existing.uuid != uuid)
                ms->alias.insert(uuid, existing.uuid);
            update(existing, QString());
            ms->applied.insert(key, hash);
            continue;
        }
        Memo nm;
        nm.targetType = elementMemo ? mapped.first().targetType : primary;
        nm.targetId = elementMemo ? mapped.first().targetId : 0;
        nm.title = title;
        nm.body = body;
        nm.author = author;
        nm.createdAt = created;
        nm.uuid = uuid;
        const qint64 id = p.saveMemo(nm);
        if (!id || !p.setProvenance(Project::Provenance::Memo, id, author, created, modified) ||
            (mapped.size() > 1 && !p.setMemoLinks(id, mapped))) {
            fail();
            return;
        }
        ++mr.memosNew;
        memoByUuid.insert(uuid, p.memo(id));
        ms->applied.insert(key, hash);
    }
}

void Applier::applySummaries(const LogFile &lf, MemberReport &mr)
{
    for (const QJsonObject &rec : lf.sums) {
        if (m_failed)
            return;
        const QString body = jstr(rec, "body");
        if (body.trimmed().isEmpty())
            continue;
        const QString key = QStringLiteral("u:") + jstr(rec, "doc") + QLatin1Char('|') + jstr(rec, "code");
        const QString hash = textHash(body);
        const QString applied = ms->applied.value(key);
        if (!applied.isEmpty() && applied == hash)
            continue;
        const qint64 td = docMap.value(jstr(rec, "doc"), 0);
        const qint64 tc = td ? ensureCode(jstr(rec, "code"), mr) : 0;
        if (m_failed)
            return;
        if (!td || !tc) {
            ++mr.skipped;
            continue;
        }
        const QString author = jstr(rec, "a");
        const QDateTime modified = fromIso(jstr(rec, "mod"));
        const Summary local = p.summary(td, tc);
        if (!local.id) {
            if (!applied.isEmpty())
                continue; // deleted here after it was applied: not brought back
            if (!p.setSummary(td, tc, body) || !p.setProvenance(Project::Provenance::Summary, p.summary(td, tc).id, author, modified)) {
                fail();
                return;
            }
            ++mr.summariesNew;
        } else {
            switch (reconcile(local.body, applied, body)) {
            case TextAction::Same:
                break;
            case TextAction::Replace:
                ++mr.updated;
                if (!p.setSummary(td, tc, body) || !p.setProvenance(Project::Provenance::Summary, p.summary(td, tc).id, author, modified))
                    fail();
                break;
            case TextAction::Join:
                conflict(mr, QCoreApplication::translate("TeamSync", "Sintesi di «%1» per il codice «%2» (di %3): cambiata anche qui, tenuti entrambi i testi.")
                                 .arg(docName(td), p.code(tc).name, lf.member()));
                if (!p.setSummary(td, tc, joinText(local.body, body, author, modified)))
                    fail();
                break;
            }
        }
        ms->applied.insert(key, hash);
    }
}

void Applier::applyFile(const LogFile &lf, MemberReport &mr)
{
    ms = &st.members[lf.member()];
    docMap.clear();
    codeMap.clear();
    segMap.clear();
    setMap.clear();
    codeRecs.clear();
    setRecs.clear();
    codeBusy.clear();
    for (const QJsonObject &c : lf.codes)
        codeRecs.insert(jstr(c, "id"), c);
    for (const QJsonObject &s : lf.sets)
        setRecs.insert(jstr(s, "id"), s);
    // Codings applied before (or already present) are known to the memos that refer to them.
    for (const QJsonObject &s : lf.segs) {
        const QString uuid = jstr(s, "id");
        const Segment local = segByUuid.value(localUuid(uuid));
        if (local.id)
            segMap.insert(uuid, local.id);
    }
    resolveDocuments(lf, mr);
    // Codes first, so that their creation does not depend on which record mentions them.
    for (const QJsonObject &c : lf.codes) {
        if (m_failed)
            return;
        ensureCode(jstr(c, "id"), mr);
    }
    applySegments(lf, mr);
    applyLinks(lf, mr);
    applyMemos(lf, mr);
    applySummaries(lf, mr);
}

} // namespace

QString syncFolder(const QString &sharedFolder, const QString &projectUuid)
{
    // Named after the project identifier, not its file name: every member may call their copy differently.
    return QDir(sharedFolder).filePath(QStringLiteral("tessera-") + cleanName(projectUuid) + QStringLiteral(".sync"));
}

QString logFileName(const QString &member)
{
    return cleanName(member) + QLatin1Char('.') + QLatin1String(FileSuffix);
}

PublishResult publish(Project &p, const QString &sharedFolder)
{
    PublishResult res;
    auto fail = [&res](const QString &message) {
        res.ok = false;
        res.error = message;
        return res;
    };
    if (!p.isOpen())
        return fail(QCoreApplication::translate("TeamSync", "Nessun progetto aperto"));
    const QString member = p.author().trimmed();
    if (member.isEmpty())
        return fail(QCoreApplication::translate("TeamSync", "Imposta prima il tuo nome (autore delle codifiche): senza nome non si può sincronizzare."));
    const QString projectUuid = p.meta(QStringLiteral("project_uuid"));
    if (projectUuid.isEmpty())
        return fail(QCoreApplication::translate("TeamSync", "Il progetto non ha un identificativo: salvalo di nuovo con questa versione di Tessera."));
    if (sharedFolder.trimmed().isEmpty())
        return fail(QCoreApplication::translate("TeamSync", "Scegli la cartella condivisa."));
    if (isInside(p.filePath(), sharedFolder))
        return fail(QCoreApplication::translate("TeamSync", "Il file del progetto è dentro la cartella condivisa. Un file .tsra non va tenuto in una cartella sincronizzata "
                       "(OneDrive, Dropbox...): si può danneggiare. Spostalo in una cartella locale."));
    const QString dir = syncFolder(sharedFolder, projectUuid);
    if (!QDir().mkpath(dir))
        return fail(QCoreApplication::translate("TeamSync", "Impossibile creare la cartella %1").arg(dir));
    const QString path = QDir(dir).filePath(logFileName(member));

    int previousGen = 0;
    if (QFileInfo::exists(path)) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject h = QJsonDocument::fromJson(f.readLine()).object();
            if (h.value(QStringLiteral("t")).toString() == QLatin1String("tsrlog")) {
                if (h.value(QStringLiteral("member")).toString() != member)
                    return fail(QCoreApplication::translate("TeamSync", "Il file %1 appartiene a un altro membro («%2»): i nomi danno lo stesso nome di file.")
                                    .arg(QFileInfo(path).fileName(), h.value(QStringLiteral("member")).toString()));
                previousGen = h.value(QStringLiteral("gen")).toInt();
            }
        }
    }
    State state = loadState(p);
    const int gen = qMax(previousGen, state.selfGen) + 1;

    // Collect the work of this author and everything it refers to.
    QHash<qint64, QString> docUuid, codeUuid, segUuid, setUuid;
    for (const Document &d : p.documents())
        docUuid.insert(d.id, d.uuid);
    for (const Code &c : p.codes())
        codeUuid.insert(c.id, c.uuid);
    for (const DocumentSet &s : p.documentSets())
        setUuid.insert(s.id, s.uuid);
    QHash<qint64, QString> groupName;
    for (const DocumentGroup &g : p.groups())
        groupName.insert(g.id, g.name);
    QSet<qint64> docsUsed, codesUsed, setsUsed;
    auto useDoc = [&](qint64 id) { docsUsed.insert(id); return docUuid.value(id); };
    auto useCode = [&](qint64 id) {
        for (qint64 c = id; c && !codesUsed.contains(c); c = p.code(c).parentId)
            codesUsed.insert(c);
        return codeUuid.value(id);
    };

    QList<QJsonObject> records;
    for (const Segment &s : p.allSegments(true)) {
        segUuid.insert(s.id, s.uuid);
        if (s.author != member)
            continue;
        QJsonObject o{{QStringLiteral("t"), QStringLiteral("seg")}, {QStringLiteral("id"), s.uuid},
                      {QStringLiteral("doc"), useDoc(s.documentId)}, {QStringLiteral("code"), useCode(s.codeId)},
                      {QStringLiteral("k"), segmentKindToString(s.kind)}, {QStringLiteral("w"), s.weight},
                      {QStringLiteral("c"), s.comment}, {QStringLiteral("a"), s.author}, {QStringLiteral("cr"), iso(s.createdAt)}};
        switch (s.kind) {
        case SegmentKind::Text:
            o.insert(QStringLiteral("s"), s.start);
            o.insert(QStringLiteral("e"), s.end);
            break;
        case SegmentKind::Area:
            o.insert(QStringLiteral("pg"), s.page);
            o.insert(QStringLiteral("x1"), s.x1);
            o.insert(QStringLiteral("y1"), s.y1);
            o.insert(QStringLiteral("x2"), s.x2);
            o.insert(QStringLiteral("y2"), s.y2);
            if (!s.areaText.isEmpty())
                o.insert(QStringLiteral("at"), s.areaText);
            break;
        case SegmentKind::Time:
            o.insert(QStringLiteral("ms"), double(s.tStartMs));
            o.insert(QStringLiteral("me"), double(s.tEndMs));
            break;
        }
        records << o;
    }
    for (const DocumentLink &l : p.links()) {
        if (l.author != member)
            continue;
        records << QJsonObject{{QStringLiteral("t"), QStringLiteral("link")}, {QStringLiteral("id"), l.uuid},
                               {QStringLiteral("fd"), useDoc(l.fromDocumentId)}, {QStringLiteral("fs"), l.fromStart},
                               {QStringLiteral("fe"), l.fromEnd}, {QStringLiteral("td"), useDoc(l.toDocumentId)},
                               {QStringLiteral("ts"), l.toStart}, {QStringLiteral("te"), l.toEnd},
                               {QStringLiteral("c"), l.comment}, {QStringLiteral("a"), l.author},
                               {QStringLiteral("cr"), iso(l.createdAt)}};
    }
    for (const Memo &m : p.memos()) {
        if (m.author != member)
            continue;
        QJsonArray els;
        for (const MemoLink &e : p.memoLinks(m.id)) {
            QString ref;
            switch (e.targetType) {
            case MemoTarget::Group: ref = groupName.value(e.targetId); break;
            case MemoTarget::Document: ref = useDoc(e.targetId); break;
            case MemoTarget::Code: ref = useCode(e.targetId); break;
            case MemoTarget::Segment: ref = segUuid.value(e.targetId); break;
            case MemoTarget::DocumentSet:
                ref = setUuid.value(e.targetId);
                setsUsed.insert(e.targetId);
                break;
            default: break;
            }
            if (!ref.isEmpty())
                els.append(QJsonObject{{QStringLiteral("t"), memoTargetToString(e.targetType)}, {QStringLiteral("r"), ref}});
        }
        records << QJsonObject{{QStringLiteral("t"), QStringLiteral("memo")}, {QStringLiteral("id"), m.uuid},
                               {QStringLiteral("pt"), memoTargetToString(m.targetType)}, {QStringLiteral("els"), els},
                               {QStringLiteral("title"), m.title}, {QStringLiteral("body"), m.body},
                               {QStringLiteral("a"), m.author}, {QStringLiteral("cr"), iso(m.createdAt)},
                               {QStringLiteral("mod"), iso(m.modifiedAt)}};
    }
    for (const Summary &s : p.summaries()) {
        if (s.author != member || s.body.trimmed().isEmpty())
            continue;
        records << QJsonObject{{QStringLiteral("t"), QStringLiteral("sum")}, {QStringLiteral("id"), s.uuid},
                               {QStringLiteral("doc"), useDoc(s.documentId)}, {QStringLiteral("code"), useCode(s.codeId)},
                               {QStringLiteral("body"), s.body}, {QStringLiteral("a"), s.author},
                               {QStringLiteral("mod"), iso(s.modifiedAt)}};
    }

    QList<QJsonObject> context;
    for (const Document &d : p.documents())
        if (docsUsed.contains(d.id))
            context << QJsonObject{{QStringLiteral("t"), QStringLiteral("doc")}, {QStringLiteral("id"), d.uuid},
                                   {QStringLiteral("name"), d.name}, {QStringLiteral("kind"), documentKindToString(d.kind)},
                                   {QStringLiteral("hash"), docHash(p, d)}};
    for (const Code &c : p.codesInTreeOrder())
        if (codesUsed.contains(c.id))
            context << QJsonObject{{QStringLiteral("t"), QStringLiteral("code")}, {QStringLiteral("id"), c.uuid},
                                   {QStringLiteral("parent"), c.parentId ? codeUuid.value(c.parentId) : QString()},
                                   {QStringLiteral("name"), c.name}, {QStringLiteral("color"), c.color.name()},
                                   {QStringLiteral("desc"), c.description}, {QStringLiteral("by"), member}};
    for (const DocumentSet &s : p.documentSets())
        if (setsUsed.contains(s.id))
            context << QJsonObject{{QStringLiteral("t"), QStringLiteral("set")}, {QStringLiteral("id"), s.uuid}, {QStringLiteral("name"), s.name}};
    context << records;

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return fail(QCoreApplication::translate("TeamSync", "Impossibile scrivere %1").arg(path));
    auto line = [&out](const QJsonObject &o) {
        out.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        out.write("\n");
    };
    line(QJsonObject{{QStringLiteral("t"), QStringLiteral("tsrlog")}, {QStringLiteral("v"), FormatVersion},
                     {QStringLiteral("project"), projectUuid}, {QStringLiteral("projectName"), p.name()},
                     {QStringLiteral("member"), member}, {QStringLiteral("gen"), gen},
                     {QStringLiteral("written"), iso(QDateTime::currentDateTimeUtc())}});
    for (const QJsonObject &o : std::as_const(context))
        line(o);
    line(QJsonObject{{QStringLiteral("t"), QStringLiteral("end")}, {QStringLiteral("n"), int(context.size())}});
    if (!out.commit())
        return fail(QCoreApplication::translate("TeamSync", "Impossibile scrivere %1").arg(path));

    state.selfGen = gen;
    saveState(p, state);
    res.path = path;
    res.records = int(records.size());
    res.generation = gen;
    return res;
}

SyncReport synchronize(Project &p, const QString &sharedFolder, bool dryRun, bool forceReread)
{
    SyncReport r;
    auto fail = [&r](const QString &message) {
        r.ok = false;
        r.error = message;
        return r;
    };
    if (!p.isOpen())
        return fail(QCoreApplication::translate("TeamSync", "Nessun progetto aperto"));
    const QString me = p.author().trimmed();
    if (me.isEmpty())
        return fail(QCoreApplication::translate("TeamSync", "Imposta prima il tuo nome (autore delle codifiche): senza nome non si può sincronizzare."));
    const QString projectUuid = p.meta(QStringLiteral("project_uuid"));
    if (projectUuid.isEmpty())
        return fail(QCoreApplication::translate("TeamSync", "Il progetto non ha un identificativo: salvalo di nuovo con questa versione di Tessera."));
    if (sharedFolder.trimmed().isEmpty() || !QDir(sharedFolder).exists())
        return fail(QCoreApplication::translate("TeamSync", "La cartella condivisa non esiste."));
    if (isInside(p.filePath(), sharedFolder))
        return fail(QCoreApplication::translate("TeamSync", "Il file del progetto è dentro la cartella condivisa. Un file .tsra non va tenuto in una cartella sincronizzata "
                       "(OneDrive, Dropbox...): si può danneggiare. Spostalo in una cartella locale."));
    const QDir dir(syncFolder(sharedFolder, projectUuid));
    if (!dir.exists()) {
        r.warnings << QCoreApplication::translate("TeamSync", "Nella cartella condivisa non c'è ancora nessun file di sincronizzazione di questo progetto.");
        return r;
    }

    State state = loadState(p);
    p.setUndoLabel(QCoreApplication::translate("TeamSync", "Sincronizzazione del gruppo"));
    p.beginBatch();
    Applier applier(p, state, r);
    const QFileInfoList files = dir.entryInfoList({QStringLiteral("*.") + QLatin1String(FileSuffix)}, QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files) {
        if (applier.failed())
            break;
        MemberReport mr;
        mr.file = fi.fileName();
        const LogFile lf = readLog(fi.absoluteFilePath(), projectUuid);
        mr.member = lf.header.isEmpty() ? fi.completeBaseName() : lf.member();
        mr.generation = lf.gen();
        if (!lf.valid) {
            mr.ignored = true;
            mr.note = lf.problem;
            r.warnings << QCoreApplication::translate("TeamSync", "%1: file ignorato (%2).").arg(fi.fileName(), lf.problem);
            r.members << mr;
            continue;
        }
        if (lf.member() == me)
            continue; // my own log
        FileState &fs = state.files[fi.fileName()];
        if (!forceReread && fs.sha == lf.sha) {
            mr.unchanged = true;
            r.members << mr;
            continue;
        }
        if (lf.gen() < fs.gen)
            r.warnings << QCoreApplication::translate("TeamSync", "%1: il file è più vecchio di quello già letto (generazione %2, letta %3). Riletto comunque.")
                              .arg(fi.fileName()).arg(lf.gen()).arg(fs.gen);
        if (fi.fileName() != logFileName(lf.member()))
            r.warnings << QCoreApplication::translate("TeamSync", "%1: il nome del file non corrisponde al membro «%2» (copia creata dal servizio cloud?). Letto comunque.")
                              .arg(fi.fileName(), lf.member());
        if (lf.parsed < lf.declared) {
            const QString text = QCoreApplication::translate("TeamSync", "%1: %2 righe illeggibili ignorate.").arg(fi.fileName()).arg(lf.declared - lf.parsed);
            mr.note = text;
            r.warnings << text;
        }
        applier.applyFile(lf, mr);
        fs.sha = lf.sha;
        fs.gen = lf.gen();
        r.members << mr;
    }
    if (applier.failed()) {
        p.rollbackBatch();
        return r;
    }
    saveState(p, state);
    if (dryRun) {
        p.rollbackBatch();
        return r;
    }
    if (!p.commitBatch()) {
        r.ok = false;
        r.error = p.lastError();
    }
    return r;
}

void resetState(Project &p)
{
    State s = loadState(p);
    s.files.clear();
    s.members.clear();
    saveState(p, s);
}

QStringList members(const Project &p, const QString &sharedFolder)
{
    QStringList out;
    const QString projectUuid = p.meta(QStringLiteral("project_uuid"));
    const QDir dir(syncFolder(sharedFolder, projectUuid));
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.") + QLatin1String(FileSuffix)}, QDir::Files, QDir::Name)) {
        const LogFile lf = readLog(fi.absoluteFilePath(), projectUuid);
        if (lf.valid && !out.contains(lf.member()))
            out << lf.member();
    }
    return out;
}

QString summarize(const SyncReport &r)
{
    if (!r.ok)
        return r.error;
    int seg = 0, link = 0, memo = 0, sum = 0, code = 0, upd = 0;
    for (const MemberReport &m : r.members) {
        seg += m.segmentsNew;
        link += m.linksNew;
        memo += m.memosNew;
        sum += m.summariesNew;
        code += m.codesNew;
        upd += m.updated;
    }
    return QCoreApplication::translate("TeamSync", "%1 codifiche, %2 collegamenti, %3 memo, %4 sintesi, %5 codici nuovi; %6 aggiornamenti, %7 conflitti")
        .arg(seg).arg(link).arg(memo).arg(sum).arg(code).arg(upd).arg(r.conflicts.size());
}

} // namespace TeamSync
