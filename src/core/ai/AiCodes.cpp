#include "AiCodes.h"

#include "core/Project.h"
#include "core/TextUtils.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <QTimer>

namespace Ai::Codes {

namespace {

QString loadPrompt(const char *name)
{
    QFile f(QStringLiteral(":/ai/") + QLatin1String(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

const char *promptFile(Task task)
{
    switch (task) {
    case Task::SuggestCodes: return "suggest_codes.txt";
    case Task::SuggestSubcodes: return "suggest_subcodes.txt";
    case Task::GroupCodes: return "group_codes.txt";
    }
    return "suggest_codes.txt";
}

QString listKey(Task task)
{
    switch (task) {
    case Task::SuggestCodes: return QStringLiteral("codes");
    case Task::SuggestSubcodes: return QStringLiteral("subcodes");
    case Task::GroupCodes: return QStringLiteral("groups");
    }
    return {};
}

QString idsKey(Task task)
{
    return task == Task::SuggestSubcodes ? QStringLiteral("segment_ids") : QStringLiteral("code_ids");
}

QString oneLine(const QString &s)
{
    return s.simplified();
}

QString lower(const QString &s)
{
    return oneLine(s).toLower();
}

// A number as the model may write it: 3, 3.0 or "3".
bool readId(const QJsonValue &v, int *out)
{
    if (v.isDouble()) {
        const double d = v.toDouble();
        if (d != double(int(d)))
            return false;
        *out = int(d);
        return true;
    }
    if (v.isString()) {
        bool ok = false;
        const int n = v.toString().trimmed().toInt(&ok);
        if (ok)
            *out = n;
        return ok;
    }
    return false;
}

} // namespace

QString functionId(Task task)
{
    switch (task) {
    case Task::SuggestCodes: return QStringLiteral("suggest_codes");
    case Task::SuggestSubcodes: return QStringLiteral("suggest_subcodes");
    case Task::GroupCodes: return QStringLiteral("group_codes");
    }
    return {};
}

QString taskTitle(Task task)
{
    switch (task) {
    case Task::SuggestCodes: return QCoreApplication::translate("Ai::Codes", "Suggerimento di codici");
    case Task::SuggestSubcodes: return QCoreApplication::translate("Ai::Codes", "Suggerimento di sottocodici");
    case Task::GroupCodes: return QCoreApplication::translate("Ai::Codes", "Raggruppamento dei codici");
    }
    return {};
}

void Material::addItem(qint64 refId, const QString &t)
{
    QString text = t.trimmed();
    if (text.isEmpty())
        return;
    if (text.size() > kMaxItemChars)
        text = text.left(kMaxItemChars) + QStringLiteral("…");
    items.append({refId, text});
}

int Material::words() const
{
    int n = countWords(text) + countWords(subject) + countWords(subjectNote);
    for (const Item &i : items)
        n += countWords(i.text);
    return n;
}

bool Material::isEmpty() const
{
    return task == Task::SuggestCodes ? text.trimmed().isEmpty() : items.isEmpty();
}

QString systemPrompt(Task task, const Options &options, const QStringList &existing)
{
    QString prompt = loadPrompt(promptFile(task));
    const QString language = options.language.trimmed().isEmpty() ? QStringLiteral("Italian") : options.language.trimmed();
    const QString extra = options.instructions.trimmed();
    QString ex;
    if (!existing.isEmpty() && task != Task::GroupCodes) {
        QStringList names;
        for (const QString &n : existing)
            names << oneLine(n);
        ex = QStringLiteral("\nThese codes already exist, do not propose them again: ") + names.join(QStringLiteral("; ")) + QLatin1Char('.');
    }
    prompt.replace(QLatin1String("{max}"), QString::number(qMax(1, options.maxSuggestions)));
    prompt.replace(QLatin1String("{existing}"), ex);
    prompt.replace(QLatin1String("{language}"), language);
    prompt.replace(QLatin1String("{instructions}"),
                   extra.isEmpty() ? QString() : QStringLiteral("\nAdditional instructions from the user: ") + extra);
    return prompt;
}

QJsonObject schemaFor(Task task)
{
    const bool withIds = task != Task::SuggestCodes;
    QJsonObject props{{QStringLiteral("name"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                      {QStringLiteral("description"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}};
    QJsonArray required{QStringLiteral("name"), QStringLiteral("description")};
    if (withIds) {
        props.insert(idsKey(task), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")},
                                               {QStringLiteral("items"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}}});
        required.append(idsKey(task));
    }
    const QJsonObject item{{QStringLiteral("type"), QStringLiteral("object")},
                           {QStringLiteral("properties"), props},
                           {QStringLiteral("required"), required},
                           {QStringLiteral("additionalProperties"), false}};
    return QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{listKey(task), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), item}}}}},
        {QStringLiteral("required"), QJsonArray{listKey(task)}},
        {QStringLiteral("additionalProperties"), false}};
}

QList<Chunk> planChunks(const Material &m, int maxTokens, int maxItems)
{
    QList<Chunk> chunks;
    if (m.task == Task::SuggestCodes) {
        if (m.text.trimmed().isEmpty())
            return chunks;
        for (const QString &piece : splitIntoChunks(m.text, maxTokens))
            if (!piece.trimmed().isEmpty())
                chunks.append({piece, {}});
        return chunks;
    }
    QString header;
    if (m.task == Task::SuggestSubcodes) {
        header = QStringLiteral("Code: ") + oneLine(m.subject);
        if (!m.subjectNote.trimmed().isEmpty())
            header += QStringLiteral("\nDescription: ") + oneLine(m.subjectNote);
        header += QStringLiteral("\n\nPassages:");
    } else {
        header = QStringLiteral("Codes:");
    }
    const int headerTokens = estimateTokens(header);
    Chunk cur;
    int n = 0, tokens = headerTokens;
    auto flush = [&] {
        if (n)
            chunks.append(cur);
        cur = Chunk();
        n = 0;
        tokens = headerTokens;
    };
    for (const Item &item : m.items) {
        const QString body = m.task == Task::GroupCodes ? oneLine(item.text) : item.text;
        const int cost = estimateTokens(body) + 3;
        if (n > 0 && (n >= maxItems || tokens + cost > maxTokens))
            flush();
        ++n;
        tokens += cost;
        cur.ids.insert(n, item.refId);
        cur.user += (cur.user.isEmpty() ? header : QString()) + (m.task == Task::GroupCodes ? QStringLiteral("\n") : QStringLiteral("\n\n"))
                    + QStringLiteral("[%1] ").arg(n) + body;
    }
    flush();
    return chunks;
}

void mergeAnswer(Task task, const QJsonDocument &json, const QHash<int, qint64> &ids, const QStringList &existingNames,
                 Result &into)
{
    QSet<QString> existing;
    for (const QString &n : existingNames)
        existing.insert(lower(n));
    QSet<qint64> assigned;
    for (const Proposal &p : std::as_const(into.proposals))
        for (qint64 id : p.refIds)
            assigned.insert(id);

    const QJsonArray list = json.object().value(listKey(task)).toArray();
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        QString name = oneLine(o.value(QStringLiteral("name")).toString());
        if (name.size() > 120)
            name = name.left(120).trimmed();
        if (name.isEmpty() || existing.contains(name.toLower())) {
            ++into.skippedProposals;
            continue;
        }
        QList<qint64> refs;
        if (task != Task::SuggestCodes) {
            for (const QJsonValue &idv : o.value(idsKey(task)).toArray()) {
                int local = 0;
                if (!readId(idv, &local) || !ids.contains(local)) {
                    ++into.invalidIds; // invented (or out of range): never used
                    continue;
                }
                const qint64 ref = ids.value(local);
                if (assigned.contains(ref))
                    continue; // an element goes to one proposal only: the first wins
                assigned.insert(ref);
                refs.append(ref);
            }
        }
        const QString description = o.value(QStringLiteral("description")).toString().trimmed();
        Proposal *found = nullptr;
        for (Proposal &p : into.proposals)
            if (p.name.toLower() == name.toLower())
                found = &p;
        if (found) {
            found->refIds += refs;
            if (found->description.isEmpty())
                found->description = description;
            continue;
        }
        if (task != Task::SuggestCodes && refs.isEmpty()) {
            ++into.skippedProposals; // nothing to put in it
            continue;
        }
        into.proposals.append({name, description, refs, true});
    }
}

// ---------------------------------------------------------------- Run

Run::Run(Client *client, const Material &material, const Options &options, QObject *parent)
    : QObject(parent), m_client(client), m_material(material), m_options(options)
{
}

void Run::start()
{
    if (m_started)
        return;
    m_started = true;
    auto failLater = [this](ErrorKind kind, const QString &message) {
        Response r;
        r.kind = kind;
        r.error = message;
        QTimer::singleShot(0, this, [this, r] { fail(r); });
    };
    if (!m_client) {
        failLater(ErrorKind::Config, QCoreApplication::translate("Ai::Codes", "Nessun provider AI utilizzabile."));
        return;
    }
    m_chunks = planChunks(m_material, m_maxTokens, m_maxItems);
    if (m_chunks.isEmpty()) {
        failLater(ErrorKind::Config, QCoreApplication::translate("Ai::Codes", "Non c'è nulla da inviare."));
        return;
    }
    emit progress(0, int(m_chunks.size()));
    sendNext();
}

void Run::cancel()
{
    if (m_finished)
        return;
    m_cancelled = true;
    if (m_current) {
        m_current->cancel(); // finishes the reply, which ends the run
        return;
    }
    m_started = true;
    Response r;
    r.kind = ErrorKind::Cancelled;
    r.error = QCoreApplication::translate("Ai::Codes", "Operazione annullata.");
    fail(r);
}

void Run::sendNext()
{
    if (m_cancelled || m_finished)
        return;
    const Chunk &chunk = m_chunks.at(m_index);
    QString system = systemPrompt(m_material.task, m_options, m_material.existingNames);
    if (m_chunks.size() > 1) {
        system += QStringLiteral("\nThe material is too long to be sent at once: this is part %1 of %2. Work only on this part.")
                      .arg(m_index + 1)
                      .arg(m_chunks.size());
        QStringList earlier;
        for (const Proposal &p : std::as_const(m_result.proposals))
            earlier << p.name;
        if (!earlier.isEmpty())
            system += QStringLiteral("\nProposed from the earlier parts: ") + earlier.join(QStringLiteral("; "))
                      + QStringLiteral(". If one of them fits here too, use exactly the same name.");
    }
    Request req;
    req.messages = {{QStringLiteral("system"), system}, {QStringLiteral("user"), chunk.user}};
    req.schema = schemaFor(m_material.task);
    req.schemaName = functionId(m_material.task);
    req.temperature = 0.2;
    ++m_requests;
    Reply *reply = m_client->chat(req);
    m_current = reply;
    if (reply->isFinished())
        QTimer::singleShot(0, this, [this, reply] { onReply(reply); });
    else
        connect(reply, &Reply::finished, this, [this, reply] { onReply(reply); });
}

void Run::onReply(Reply *reply)
{
    if (m_finished) {
        reply->deleteLater();
        return;
    }
    const Response r = reply->response();
    reply->deleteLater();
    m_current = nullptr;
    if (!r.ok) {
        fail(r);
        return;
    }
    mergeAnswer(m_material.task, r.json, m_chunks.at(m_index).ids, m_material.existingNames, m_result);
    emit progress(m_index + 1, int(m_chunks.size()));
    if (++m_index < m_chunks.size()) {
        sendNext();
        return;
    }
    finish();
}

void Run::fail(const Response &r)
{
    if (m_finished)
        return;
    m_finished = true;
    m_response = r;
    m_response.ok = false;
    m_result = Result();
    emit finished();
}

void Run::finish()
{
    if (m_finished)
        return;
    m_finished = true;
    m_response = Response();
    m_response.ok = true;
    m_response.attempts = m_requests;
    emit finished();
}

// ---------------------------------------------------------------- apply

QString provenanceNote(const Config &config)
{
    return QCoreApplication::translate("Ai::Codes", "Proposto da %1").arg(authorName(config));
}

ApplyReport apply(Project &project, Task task, qint64 contextId, const QList<Proposal> &accepted, const QString &provenance)
{
    ApplyReport report;
    if (accepted.isEmpty())
        return report;
    project.setUndoLabel(taskTitle(task));
    project.beginBatch();
    QSet<QString> usedNames;
    for (const Proposal &p : accepted) {
        const QString name = oneLine(p.name);
        if (name.isEmpty() || usedNames.contains(name.toLower()) || project.findCode(contextId, name)) {
            ++report.skipped; // empty, repeated, or taken since the proposal was made
            continue;
        }
        usedNames.insert(name.toLower());
        const QColor color = contextId ? project.code(contextId).color
                                       : TextUtils::paletteColor(int(project.codes().size()));
        const qint64 id = project.addCode(contextId, name, color);
        if (!id) {
            ++report.skipped;
            continue;
        }
        QString description = p.description.trimmed();
        if (!provenance.trimmed().isEmpty())
            description += (description.isEmpty() ? QString() : QStringLiteral("\n\n")) + provenance.trimmed();
        if (!description.isEmpty())
            project.setCodeDescription(id, description);
        ++report.createdCodes;
        report.newCodeIds << id;
        for (qint64 ref : p.refIds) {
            if (task == Task::SuggestSubcodes) {
                const Segment s = project.segment(ref);
                if (s.id && s.codeId == contextId && project.setSegmentCode(ref, id))
                    ++report.movedSegments;
                else
                    ++report.skipped;
            } else if (task == Task::GroupCodes) {
                const Code c = project.code(ref);
                if (c.id && c.parentId == contextId && project.moveCode(ref, id))
                    ++report.movedCodes;
                else
                    ++report.skipped;
            }
        }
    }
    project.commitBatch();
    return report;
}

} // namespace Ai::Codes
