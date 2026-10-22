#include "core/Merge.h"

#include "core/Project.h"
#include "core/Qdpx.h"
#include "core/Summaries.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>

#include <algorithm>

namespace Merge {

namespace {

QString trm(const char *text) { return QCoreApplication::translate("Merge", text); }

QByteArray fingerprint(const Project &p, const Document &d)
{
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(documentKindToString(d.kind).toUtf8());
    h.addData(QByteArray(1, '\0'));
    h.addData(p.documentText(d.id).toUtf8());
    h.addData(QByteArray(1, '\0'));
    h.addData(p.documentData(d.id));
    return h.result();
}

// Appends another person's text to an existing one, with a separator that names the author.
QString joinText(const QString &existing, const QString &incoming, const QString &author, const QDateTime &when)
{
    if (existing.trimmed().isEmpty())
        return incoming;
    QString who = author;
    if (when.isValid())
        who += (who.isEmpty() ? QString() : QStringLiteral(", ")) + when.toLocalTime().toString(QStringLiteral("yyyy-MM-dd"));
    if (who.isEmpty())
        who = trm("altra copia");
    return existing + QStringLiteral("\n\n— ") + who + QStringLiteral(" —\n") + incoming;
}

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

// Runs the merge, or only counts what it would do (dry). Ids of things that do not exist yet in the target
// are negative placeholders during a dry run; 0 means "left out".
class Merger {
public:
    Merger(Project &target, const Project &source, const QString &sourceName, const Options &options, bool dry)
        : t(target), s(source), m_sourceName(sourceName), opts(options), m_dry(dry)
    {
    }

    Report run();

private:
    bool authorOk(const QString &a) const { return opts.onlyAuthor.isEmpty() || a == opts.onlyAuthor; }
    bool filtering() const { return !opts.onlyAuthor.isEmpty(); }
    qint64 fakeId() { return m_nextFake--; }
    void fail()
    {
        if (!m_failed) {
            m_failed = true;
            report.ok = false;
            report.error = t.lastError();
        }
    }
    // A write returned 0/false: stop the merge.
    qint64 check(qint64 id)
    {
        if (!id)
            fail();
        return id;
    }

    void buildIndexes();
    qint64 ensureGroup(qint64 srcGroup);
    qint64 ensureDoc(qint64 srcDoc);
    qint64 createDocument(const Document &od, const QString &uuid, const QString &name, bool copy);
    qint64 ensureCode(qint64 srcCode);
    qint64 ensureVariable(qint64 srcVar);
    qint64 ensureSet(qint64 srcSet);
    bool mapElement(MemoTarget type, qint64 srcId, MemoLink *out);
    QByteArray targetFingerprint(qint64 docId);

    void mergeVariableValues();
    void mergeSegments();
    void mergeLinks();
    void mergeMemos();
    void mergeSummaries();
    void mergeDictionaries();
    void mergeSummaryTables();

    Project &t;
    const Project &s;
    QString m_sourceName;
    Options opts;
    bool m_dry;
    bool m_failed = false;
    qint64 m_nextFake = -1;
    Report report;

    // Source side
    QHash<qint64, QString> srcGroupName;
    QHash<qint64, Code> srcCodes;
    QHash<qint64, Variable> srcVars;
    QHash<qint64, DocumentSet> srcSets;
    // Source id -> target id
    QHash<qint64, qint64> groupMap, docMap, codeMap, varMap, setMap, segMap;
    // Target side
    QHash<QString, Document> tDocByUuid;
    QHash<QString, QList<Document>> tDocsByName;
    QHash<qint64, QByteArray> tFingerprints;
    QHash<QString, qint64> tCodeByUuid;
    QHash<QString, Segment> tSegByKey;
    QSet<QString> tSegUuids;
    QSet<QString> tLinkKeys;
    QSet<QString> tLinkUuids;
    QHash<QString, Memo> tMemoByUuid;
};

void Merger::buildIndexes()
{
    for (const DocumentGroup &g : s.groups())
        srcGroupName.insert(g.id, g.name);
    for (const Code &c : s.codes())
        srcCodes.insert(c.id, c);
    for (const Variable &v : s.variables())
        srcVars.insert(v.id, v);
    for (const DocumentSet &d : s.documentSets())
        srcSets.insert(d.id, d);

    for (const Document &d : t.documents()) {
        tDocByUuid.insert(d.uuid, d);
        tDocsByName[d.name].append(d);
    }
    for (const Code &c : t.codes())
        tCodeByUuid.insert(c.uuid, c.id);
    for (const Segment &sg : t.allSegments(true)) {
        tSegByKey.insert(segmentKey(sg, sg.documentId, sg.codeId), sg);
        tSegUuids.insert(sg.uuid);
    }
    for (const DocumentLink &l : t.links()) {
        tLinkKeys.insert(linkKey(l.fromDocumentId, l.fromStart, l.fromEnd, l.toDocumentId, l.toStart, l.toEnd));
        tLinkUuids.insert(l.uuid);
    }
    for (const Memo &m : t.memos())
        tMemoByUuid.insert(m.uuid, m);
}

QByteArray Merger::targetFingerprint(qint64 docId)
{
    auto it = tFingerprints.constFind(docId);
    if (it != tFingerprints.constEnd())
        return it.value();
    return tFingerprints.insert(docId, fingerprint(t, t.document(docId))).value();
}

qint64 Merger::ensureGroup(qint64 srcGroup)
{
    auto it = groupMap.constFind(srcGroup);
    if (it != groupMap.constEnd())
        return it.value();
    const QString name = srcGroupName.value(srcGroup);
    qint64 id = 0;
    for (const DocumentGroup &g : t.groups()) {
        if (g.name == name) {
            id = g.id;
            break;
        }
    }
    if (!id) {
        ++report.groupsNew;
        id = m_dry ? fakeId() : check(t.addGroup(name));
    }
    groupMap.insert(srcGroup, id);
    return id;
}

qint64 Merger::createDocument(const Document &od, const QString &uuid, const QString &name, bool copy)
{
    if (copy)
        ++report.documentsCopied;
    else
        ++report.documentsNew;
    if (m_dry)
        return fakeId();
    const qint64 group = ensureGroup(od.groupId);
    if (m_failed)
        return 0;
    const QString text = s.documentText(od.id);
    const QByteArray data = s.documentData(od.id);
    qint64 id = 0;
    switch (od.kind) {
    case DocumentKind::Text:
        id = t.addDocument(group, name, text, od.sourcePath, uuid);
        break;
    case DocumentKind::Image:
        id = t.addImageDocument(group, name, data, od.sourcePath, uuid);
        break;
    case DocumentKind::Pdf:
        id = t.addPdfDocument(group, name, data, text, od.sourcePath, uuid);
        break;
    case DocumentKind::Audio:
    case DocumentKind::Video:
        id = t.addMediaDocument(group, name, od.kind, od.sourcePath, uuid);
        if (id && !data.isEmpty() && !t.setDocumentData(id, data))
            id = 0;
        break;
    }
    return check(id);
}

qint64 Merger::ensureDoc(qint64 srcDoc)
{
    auto it = docMap.constFind(srcDoc);
    if (it != docMap.constEnd())
        return it.value();
    const Document od = s.document(srcDoc);
    qint64 result = 0;
    if (od.id) {
        const QByteArray fp = fingerprint(s, od);
        auto byUuid = tDocByUuid.constFind(od.uuid);
        if (!od.uuid.isEmpty() && byUuid != tDocByUuid.constEnd()) {
            if (targetFingerprint(byUuid->id) == fp) {
                ++report.documentsPresent;
                result = byUuid->id;
            } else {
                // Same document, different content: positions of codings would not fit.
                ++report.documentsConflict;
                report.conflictDocuments << od.name;
                if (opts.onTextConflict == TextConflict::Skip) {
                    ++report.documentsSkipped;
                } else {
                    const QString copyName = trm("%1 (copia di %2)").arg(od.name, m_sourceName);
                    for (const Document &d : tDocsByName.value(copyName)) {
                        if (targetFingerprint(d.id) == fp) {
                            result = d.id; // copied by an earlier merge
                            break;
                        }
                    }
                    if (result)
                        ++report.documentsPresent;
                    else
                        result = createDocument(od, QString(), copyName, true);
                }
            }
        } else {
            for (const Document &d : tDocsByName.value(od.name)) {
                if (targetFingerprint(d.id) == fp) {
                    result = d.id;
                    break;
                }
            }
            if (result)
                ++report.documentsPresent;
            else
                result = createDocument(od, od.uuid, od.name, false);
        }
    }
    docMap.insert(srcDoc, result);
    return result;
}

qint64 Merger::ensureCode(qint64 srcCode)
{
    auto it = codeMap.constFind(srcCode);
    if (it != codeMap.constEnd())
        return it.value();
    if (!srcCodes.contains(srcCode))
        return 0;
    const Code oc = srcCodes.value(srcCode);
    const qint64 parent = oc.parentId ? ensureCode(oc.parentId) : 0;
    if (m_failed)
        return 0;
    qint64 id = 0;
    auto byUuid = tCodeByUuid.constFind(oc.uuid);
    if (!oc.uuid.isEmpty() && byUuid != tCodeByUuid.constEnd())
        id = byUuid.value();
    else if (parent >= 0)
        id = t.findCode(parent, oc.name);
    if (id > 0) {
        ++report.codesPresent;
        if (!m_dry && !oc.description.isEmpty() && t.code(id).description.isEmpty())
            t.setCodeDescription(id, oc.description);
    } else {
        ++report.codesNew;
        if (m_dry) {
            id = fakeId();
        } else {
            id = check(t.addCode(parent, oc.name, oc.color, oc.uuid));
            if (id && !oc.description.isEmpty())
                t.setCodeDescription(id, oc.description);
        }
    }
    codeMap.insert(srcCode, id);
    return id;
}

qint64 Merger::ensureVariable(qint64 srcVar)
{
    auto it = varMap.constFind(srcVar);
    if (it != varMap.constEnd())
        return it.value();
    if (!srcVars.contains(srcVar))
        return 0;
    const Variable ov = srcVars.value(srcVar);
    qint64 id = 0;
    for (const Variable &v : t.variables()) {
        if (v.name == ov.name) {
            id = v.id;
            break;
        }
    }
    if (!id) {
        ++report.variablesNew;
        id = m_dry ? fakeId() : check(t.addVariable(ov.name, ov.type, ov.uuid));
    }
    varMap.insert(srcVar, id);
    return id;
}

qint64 Merger::ensureSet(qint64 srcSet)
{
    auto it = setMap.constFind(srcSet);
    if (it != setMap.constEnd())
        return it.value();
    if (!srcSets.contains(srcSet))
        return 0;
    const DocumentSet os = srcSets.value(srcSet);
    // Members: a filtered merge brings only documents it has already imported.
    QList<qint64> members;
    for (qint64 doc : s.documentSetMembers(srcSet)) {
        const qint64 mapped = filtering() ? docMap.value(doc, 0) : ensureDoc(doc);
        if (mapped)
            members << mapped;
    }
    if (m_failed)
        return 0;
    qint64 id = 0;
    const QList<DocumentSet> targetSets = t.documentSets();
    for (const DocumentSet &d : targetSets)
        if (!os.uuid.isEmpty() && d.uuid == os.uuid)
            id = d.id;
    if (!id)
        for (const DocumentSet &d : targetSets)
            if (d.name == os.name) {
                id = d.id;
                break;
            }
    if (id) {
        const QList<qint64> current = t.documentSetMembers(id);
        QList<qint64> missing;
        for (qint64 doc : std::as_const(members))
            if (doc < 0 || !current.contains(doc))
                missing << doc;
        if (!missing.isEmpty()) {
            ++report.setsExtended;
            if (!m_dry && !t.addToDocumentSet(id, missing))
                fail();
        }
    } else {
        ++report.setsNew;
        id = m_dry ? fakeId() : check(t.addDocumentSet(os.name, members, os.uuid));
    }
    setMap.insert(srcSet, id);
    return id;
}

void Merger::mergeVariableValues()
{
    const auto values = s.allVariableValues();
    for (const Document &od : s.documents()) {
        const qint64 td = docMap.value(od.id, 0);
        if (!td || m_failed)
            continue;
        const auto docValues = values.value(od.id);
        QList<qint64> varIds = docValues.keys();
        std::sort(varIds.begin(), varIds.end());
        for (qint64 srcVar : std::as_const(varIds)) {
            const QString value = docValues.value(srcVar);
            if (value.isEmpty())
                continue;
            const qint64 tv = ensureVariable(srcVar);
            if (!tv || m_failed)
                continue;
            const QString current = (td > 0 && tv > 0) ? t.variableValue(td, tv) : QString();
            if (current.isEmpty()) {
                ++report.variableValuesSet;
                if (!m_dry && !t.setVariableValue(td, tv, value))
                    fail();
            } else if (current != value) {
                ++report.variableValueConflicts; // the target keeps its own value
            }
        }
    }
}

void Merger::mergeSegments()
{
    for (const Segment &sg : s.allSegments(true)) {
        if (m_failed)
            return;
        if (!authorOk(sg.author)) {
            ++report.segmentsFiltered;
            continue;
        }
        const qint64 td = ensureDoc(sg.documentId);
        const qint64 tc = td ? ensureCode(sg.codeId) : 0;
        if (m_failed)
            return;
        if (!td || !tc) {
            ++report.segmentsSkipped;
            continue;
        }
        const QString key = segmentKey(sg, td, tc);
        auto existing = tSegByKey.find(key);
        if (existing != tSegByKey.end()) {
            segMap.insert(sg.id, existing->id);
            if (!sg.comment.isEmpty() && !existing->comment.contains(sg.comment)) {
                // Same coding, a different comment: keep both.
                ++report.segmentsConflict;
                existing->comment = joinText(existing->comment, sg.comment, sg.author, sg.createdAt);
                if (!m_dry && existing->id > 0 && !t.setSegmentComment(existing->id, existing->comment))
                    fail();
            } else {
                ++report.segmentsDuplicate;
            }
            continue;
        }
        Segment placed = sg;
        placed.documentId = td;
        placed.codeId = tc;
        if (m_dry) {
            ++report.segmentsNew;
            placed.id = fakeId();
            segMap.insert(sg.id, placed.id);
            tSegByKey.insert(key, placed);
            continue;
        }
        const QString uuid = tSegUuids.contains(sg.uuid) ? QString() : sg.uuid;
        qint64 id = 0;
        switch (sg.kind) {
        case SegmentKind::Text:
            id = t.addSegment(td, tc, sg.start, sg.end, sg.weight, sg.comment, uuid);
            break;
        case SegmentKind::Area:
            id = t.addAreaSegment(td, tc, sg.page, sg.x1, sg.y1, sg.x2, sg.y2, sg.weight, sg.comment, uuid,
                                  sg.areaText);
            break;
        case SegmentKind::Time:
            id = t.addTimeSegment(td, tc, sg.tStartMs, sg.tEndMs, sg.weight, sg.comment, uuid);
            break;
        }
        if (!id) {
            ++report.segmentsSkipped; // does not fit the document (or its kind)
            continue;
        }
        ++report.segmentsNew;
        if (!t.setProvenance(Project::Provenance::Segment, id, sg.author, sg.createdAt)) {
            fail();
            return;
        }
        placed.id = id;
        segMap.insert(sg.id, id);
        tSegByKey.insert(key, placed);
        tSegUuids.insert(t.segment(id).uuid);
    }
}

void Merger::mergeLinks()
{
    for (const DocumentLink &l : s.links()) {
        if (m_failed)
            return;
        if (!authorOk(l.author))
            continue;
        const qint64 from = ensureDoc(l.fromDocumentId);
        const qint64 to = ensureDoc(l.toDocumentId);
        if (m_failed)
            return;
        if (!from || !to)
            continue;
        const QString key = linkKey(from, l.fromStart, l.fromEnd, to, l.toStart, l.toEnd);
        const QString reverse = linkKey(to, l.toStart, l.toEnd, from, l.fromStart, l.fromEnd);
        if (tLinkKeys.contains(key) || tLinkKeys.contains(reverse)) {
            ++report.linksDuplicate;
            continue;
        }
        tLinkKeys.insert(key);
        if (m_dry) {
            ++report.linksNew;
            continue;
        }
        const QString uuid = tLinkUuids.contains(l.uuid) ? QString() : l.uuid;
        const qint64 id = t.addLink(from, l.fromStart, l.fromEnd, to, l.toStart, l.toEnd, l.comment, uuid);
        if (!id)
            continue;
        ++report.linksNew;
        if (!t.setProvenance(Project::Provenance::Link, id, l.author, l.createdAt)) {
            fail();
            return;
        }
    }
}

bool Merger::mapElement(MemoTarget type, qint64 srcId, MemoLink *out)
{
    qint64 id = 0;
    switch (type) {
    case MemoTarget::Group: id = ensureGroup(srcId); break;
    case MemoTarget::Document: id = ensureDoc(srcId); break;
    case MemoTarget::Code: id = ensureCode(srcId); break;
    case MemoTarget::Segment: id = segMap.value(srcId, 0); break;
    case MemoTarget::DocumentSet: id = ensureSet(srcId); break;
    default: break;
    }
    if (!id)
        return false;
    *out = {type, id};
    return true;
}

void Merger::mergeMemos()
{
    for (const Memo &m : s.memos()) {
        if (m_failed)
            return;
        if (!authorOk(m.author))
            continue;
        const QList<MemoLink> elements = s.memoLinks(m.id);
        QList<MemoLink> mapped;
        for (const MemoLink &e : elements) {
            MemoLink x;
            if (mapElement(e.targetType, e.targetId, &x))
                mapped << x;
        }
        if (m_failed)
            return;
        const bool elementMemo = Project::isLinkableMemoTarget(m.targetType);
        if (elementMemo && mapped.isEmpty()) {
            ++report.memosSkipped; // everything it was attached to was left out
            continue;
        }

        Memo existing;
        auto byUuid = tMemoByUuid.constFind(m.uuid);
        if (!m.uuid.isEmpty() && byUuid != tMemoByUuid.constEnd()) {
            existing = byUuid.value();
        } else if (m.targetType == MemoTarget::Project) {
            existing = t.memoFor(MemoTarget::Project, 0);
        } else if (elementMemo) {
            if (mapped.first().targetId > 0)
                existing = t.memoFor(mapped.first().targetType, mapped.first().targetId);
        } else {
            for (const Memo &c : t.memos()) {
                if (c.targetType == MemoTarget::Free && c.title == m.title && c.body == m.body) {
                    existing = c;
                    break;
                }
            }
        }

        if (existing.id) {
            const QString incoming = m.body.trimmed();
            if (incoming.isEmpty() || existing.body.contains(incoming)) {
                ++report.memosPresent;
                continue;
            }
            // Two different memos on the same element: keep both texts, the author tells them apart.
            ++report.memosMerged;
            if (!m_dry) {
                existing.body = joinText(existing.body, m.body, m.author, m.modifiedAt);
                if (existing.title.isEmpty())
                    existing.title = m.title;
                if (!t.saveMemo(existing)) {
                    fail();
                    return;
                }
            }
            continue;
        }

        ++report.memosNew;
        if (m_dry)
            continue;
        Memo nm = m;
        nm.id = 0;
        if (elementMemo) {
            nm.targetType = mapped.first().targetType;
            nm.targetId = mapped.first().targetId;
        }
        if (!m.uuid.isEmpty() && tMemoByUuid.contains(m.uuid))
            nm.uuid.clear();
        const qint64 id = t.saveMemo(nm);
        if (!id || !t.setProvenance(Project::Provenance::Memo, id, m.author, m.createdAt, m.modifiedAt) ||
            (mapped.size() > 1 && !t.setMemoLinks(id, mapped))) {
            fail();
            return;
        }
        tMemoByUuid.insert(t.memo(id).uuid, t.memo(id));
    }
}

void Merger::mergeSummaries()
{
    for (const Summary &sm : s.summaries()) {
        if (m_failed)
            return;
        if (!authorOk(sm.author) || sm.body.trimmed().isEmpty())
            continue;
        const qint64 td = ensureDoc(sm.documentId);
        const qint64 tc = td ? ensureCode(sm.codeId) : 0;
        if (m_failed)
            return;
        if (!td || !tc)
            continue;
        const Summary existing = (td > 0 && tc > 0) ? t.summary(td, tc) : Summary();
        if (!existing.id) {
            ++report.summariesNew;
            if (m_dry)
                continue;
            if (!t.setSummary(td, tc, sm.body) ||
                !t.setProvenance(Project::Provenance::Summary, t.summary(td, tc).id, sm.author, sm.modifiedAt)) {
                fail();
                return;
            }
        } else if (existing.body.contains(sm.body.trimmed())) {
            ++report.summariesPresent;
        } else {
            ++report.summariesMerged;
            if (!m_dry && !t.setSummary(td, tc, joinText(existing.body, sm.body, sm.author, sm.modifiedAt))) {
                fail();
                return;
            }
        }
    }
}

void Merger::mergeDictionaries()
{
    const QList<Dictionary> current = t.dictionaries();
    for (const Dictionary &od : s.dictionaries()) {
        if (m_failed)
            return;
        bool present = false;
        for (const Dictionary &d : current)
            if ((!od.uuid.isEmpty() && d.uuid == od.uuid) || d.name == od.name)
                present = true;
        if (present)
            continue;
        ++report.dictionariesNew;
        if (m_dry)
            continue;
        const qint64 nd = check(t.addDictionary(od.name, od.uuid));
        if (!nd)
            return;
        QHash<qint64, qint64> cats;
        for (const DictCategory &c : s.dictCategories(od.id)) {
            const qint64 nc = check(t.addDictCategory(nd, c.parentId ? cats.value(c.parentId, 0) : 0, c.name));
            if (!nc)
                return;
            cats.insert(c.id, nc);
            for (const DictEntry &e : s.dictEntries(c.id))
                if (!t.addDictEntry(nc, e.pattern)) {
                    fail();
                    return;
                }
        }
    }
}

void Merger::mergeSummaryTables()
{
    const QList<SummaryTable> current = t.summaryTables();
    for (const SummaryTable &st : s.summaryTables()) {
        if (m_failed)
            return;
        bool present = false;
        for (const SummaryTable &c : current)
            if ((!st.uuid.isEmpty() && c.uuid == st.uuid) || c.name == st.name)
                present = true;
        if (present)
            continue;
        ++report.summaryTablesNew;
        if (m_dry)
            continue;
        // The configuration names documents, a set, codes and variables of the other project.
        const Summaries::TableConfig in = Summaries::configFromJson(st.configJson);
        Summaries::TableConfig out;
        for (qint64 d : in.documentIds)
            if (const qint64 m = ensureDoc(d))
                out.documentIds << m;
        if (in.setId)
            out.setId = ensureSet(in.setId);
        for (qint64 c : in.codeIds)
            if (const qint64 m = ensureCode(c))
                out.codeIds << m;
        for (qint64 v : in.variableIds)
            if (const qint64 m = ensureVariable(v))
                out.variableIds << m;
        if (m_failed || !t.addSummaryTable(st.name, Summaries::configToJson(out), st.uuid)) {
            fail();
            return;
        }
    }
}

Report Merger::run()
{
    buildIndexes();
    if (!filtering()) {
        // Everything of the other project comes across, including what nothing refers to.
        for (const auto &g : s.groups())
            ensureGroup(g.id);
        for (const Document &d : s.documents()) {
            if (m_failed)
                break;
            ensureDoc(d.id);
        }
        for (const Code &c : s.codesInTreeOrder()) {
            if (m_failed)
                break;
            ensureCode(c.id);
        }
        for (const Variable &v : s.variables())
            ensureVariable(v.id);
        for (const DocumentSet &d : s.documentSets()) {
            if (m_failed)
                break;
            ensureSet(d.id);
        }
    }
    if (!m_failed)
        mergeSegments();
    if (!m_failed)
        mergeLinks();
    if (!m_failed)
        mergeMemos();
    if (!m_failed)
        mergeSummaries();
    if (!m_failed)
        mergeVariableValues();
    if (!m_failed && !filtering()) {
        mergeDictionaries();
        mergeSummaryTables();
    }
    return report;
}

} // namespace

Source::Source() = default;
Source::~Source() = default;

bool Source::load(const QString &path, QString *error)
{
    m_project.reset();
    m_dir = std::make_unique<QTemporaryDir>();
    auto fail = [&](const QString &message) {
        if (error)
            *error = message;
        m_project.reset();
        return false;
    };
    if (!m_dir->isValid())
        return fail(trm("Impossibile creare una cartella temporanea"));
    m_name = QFileInfo(path).completeBaseName();
    m_project = std::make_unique<Project>();
    const QString local = m_dir->filePath(QStringLiteral("source.tsra"));
    if (QFileInfo(path).suffix().compare(QLatin1String("qdpx"), Qt::CaseInsensitive) == 0) {
        QString report, err;
        if (!m_project->create(local) || !Qdpx::importProject(*m_project, path, &report, &err))
            return fail(err.isEmpty() ? m_project->lastError() : err);
    } else {
        // Work on a copy: opening may upgrade the schema (and leave a backup next to the file).
        if (!QFile::copy(path, local))
            return fail(trm("Impossibile leggere %1").arg(path));
        QFile::setPermissions(local, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        if (!m_project->open(local))
            return fail(m_project->lastError());
    }
    return true;
}

QStringList Source::authors() const
{
    QSet<QString> found;
    if (!m_project)
        return {};
    for (const Segment &s : m_project->allSegments(true))
        found.insert(s.author);
    for (const Memo &m : m_project->memos())
        found.insert(m.author);
    for (const DocumentLink &l : m_project->links())
        found.insert(l.author);
    for (const Summary &s : m_project->summaries())
        found.insert(s.author);
    found.remove(QString());
    QStringList out(found.begin(), found.end());
    out.sort(Qt::CaseInsensitive);
    return out;
}

Report analyze(Project &target, const Source &source, const Options &options)
{
    Merger merger(target, source.project(), source.name(), options, true);
    return merger.run();
}

Report apply(Project &target, const Source &source, const Options &options)
{
    target.setUndoLabel(trm("Unione di progetti"));
    target.beginBatch();
    Merger merger(target, source.project(), source.name(), options, false);
    Report report = merger.run();
    if (!report.ok) {
        target.rollbackBatch();
        return report;
    }
    if (!target.commitBatch()) {
        report.ok = false;
        report.error = target.lastError();
    }
    return report;
}

} // namespace Merge
