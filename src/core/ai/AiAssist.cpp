#include "AiAssist.h"

#include <QCoreApplication>
#include <QFile>
#include <QTimer>

namespace Ai::Assist {

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
    case Task::SummarizeDocument: return "summarize_document.txt";
    case Task::SummarizeSegments: return "summarize_segments.txt";
    case Task::Explain: return "explain.txt";
    case Task::Translate: return "translate.txt";
    }
    return "summarize_document.txt";
}

QString fill(QString prompt, Task task, const Options &o)
{
    const QString language = o.language.trimmed().isEmpty() ? QStringLiteral("Italian") : o.language.trimmed();
    const QString extra = o.instructions.trimmed();
    prompt.replace(QLatin1String("{length}"), lengthInstruction(o.length, task));
    prompt.replace(QLatin1String("{language}"), language);
    prompt.replace(QLatin1String("{instructions}"),
                   extra.isEmpty() ? QString() : QStringLiteral("\nAdditional instructions from the user: ") + extra);
    return prompt;
}

QString block(const Source &s)
{
    return QStringLiteral("[%1]\n%2").arg(s.label, s.text);
}

} // namespace

QString functionId(Task task)
{
    switch (task) {
    case Task::SummarizeDocument: return QStringLiteral("summarize_document");
    case Task::SummarizeSegments: return QStringLiteral("summarize_segments");
    case Task::Explain: return QStringLiteral("explain");
    case Task::Translate: return QStringLiteral("translate");
    }
    return {};
}

QString taskTitle(Task task)
{
    switch (task) {
    case Task::SummarizeDocument: return QCoreApplication::translate("Ai::Assist", "Riassunto del documento");
    case Task::SummarizeSegments: return QCoreApplication::translate("Ai::Assist", "Riassunto dei segmenti");
    case Task::Explain: return QCoreApplication::translate("Ai::Assist", "Spiegazione");
    case Task::Translate: return QCoreApplication::translate("Ai::Assist", "Traduzione");
    }
    return {};
}

bool usesLength(Task task)
{
    return task != Task::Translate;
}

QString lengthName(Length length)
{
    switch (length) {
    case Length::Short: return QCoreApplication::translate("Ai::Assist", "Breve");
    case Length::Medium: return QCoreApplication::translate("Ai::Assist", "Media");
    case Length::Long: return QCoreApplication::translate("Ai::Assist", "Lunga");
    }
    return {};
}

QString lengthInstruction(Length length, Task task)
{
    if (task == Task::Translate)
        return {};
    switch (length) {
    case Length::Short: return QStringLiteral("short, two or three sentences");
    case Length::Medium: return QStringLiteral("medium, one paragraph of about 100 to 150 words");
    case Length::Long: return QStringLiteral("long and detailed, several paragraphs of about 300 to 400 words");
    }
    return {};
}

QString systemPrompt(Task task, const Options &options)
{
    return fill(loadPrompt(promptFile(task)), task, options);
}

QString combinePrompt(Task task, const Options &options)
{
    return fill(loadPrompt("combine.txt"), task, options);
}

QString userText(Task task, const QList<Source> &sources)
{
    QStringList parts;
    for (const Source &s : sources)
        parts << (task == Task::SummarizeSegments && !s.label.isEmpty() ? block(s) : s.text);
    return parts.join(QStringLiteral("\n\n"));
}

int wordsOf(const QList<Source> &sources)
{
    int n = 0;
    for (const Source &s : sources)
        n += countWords(s.text);
    return n;
}

QStringList planChunks(Task task, const QList<Source> &sources, int maxTokens)
{
    if (task != Task::SummarizeSegments) {
        const QString text = userText(task, sources);
        return text.trimmed().isEmpty() ? QStringList() : splitIntoChunks(text, maxTokens);
    }
    // Segments: keep each citation with its text; group the blocks greedily.
    QStringList blocks;
    for (const Source &s : sources) {
        if (s.text.trimmed().isEmpty())
            continue;
        const QString whole = s.label.isEmpty() ? s.text : block(s);
        if (estimateTokens(whole) <= maxTokens) {
            blocks << whole;
            continue;
        }
        const int room = qMax(50, maxTokens - estimateTokens(s.label) - 4);
        for (const QString &piece : splitIntoChunks(s.text, room))
            blocks << (s.label.isEmpty() ? piece : block({s.label, piece}));
    }
    QStringList chunks;
    QString current;
    for (const QString &b : blocks) {
        if (!current.isEmpty() && estimateTokens(current) + estimateTokens(b) + 1 > maxTokens) {
            chunks << current;
            current.clear();
        }
        current += (current.isEmpty() ? QString() : QStringLiteral("\n\n")) + b;
    }
    if (!current.isEmpty())
        chunks << current;
    return chunks;
}

// ---------------------------------------------------------------- Run

Run::Run(Client *client, Task task, const Options &options, const QList<Source> &sources, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_task(task)
    , m_options(options)
    , m_sources(sources)
{
}

void Run::start()
{
    if (m_started)
        return;
    m_started = true;
    if (!m_client) {
        Response r;
        r.kind = ErrorKind::Config;
        r.error = QCoreApplication::translate("Ai::Assist", "Nessun provider AI utilizzabile.");
        QTimer::singleShot(0, this, [this, r] { fail(r); });
        return;
    }
    const QStringList chunks = planChunks(m_task, m_sources, m_maxTokens);
    if (chunks.isEmpty()) {
        Response r;
        r.kind = ErrorKind::Config;
        r.error = QCoreApplication::translate("Ai::Assist", "Non c'è testo da inviare.");
        QTimer::singleShot(0, this, [this, r] { fail(r); });
        return;
    }
    const QString system = systemPrompt(m_task, m_options);
    for (int i = 0; i < chunks.size(); ++i) {
        QString s = system;
        if (chunks.size() > 1)
            s += QStringLiteral("\nThe material is too long to be sent at once: this is part %1 of %2. Work only on this part.")
                     .arg(i + 1)
                     .arg(chunks.size());
        m_queue.append({s, chunks.at(i)});
    }
    const bool reduces = m_task == Task::SummarizeDocument || m_task == Task::SummarizeSegments;
    m_total = chunks.size() + (reduces && chunks.size() > 1 ? 1 : 0);
    emit progress(0, m_total);
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
    r.error = QCoreApplication::translate("Ai::Assist", "Operazione annullata.");
    fail(r);
}

void Run::sendNext()
{
    if (m_cancelled || m_finished)
        return;
    const Job &job = m_queue.at(m_index);
    Request req;
    req.messages = {{QStringLiteral("system"), job.system}, {QStringLiteral("user"), job.user}};
    req.temperature = m_task == Task::Translate ? 0.0 : 0.2;
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
    m_outputs << r.text.trimmed();
    emit progress(qMin(m_requests, m_total), qMax(m_total, m_requests + (m_index + 1 < m_queue.size() ? 1 : 0)));
    if (++m_index < m_queue.size()) {
        sendNext();
        return;
    }
    stageDone();
}

void Run::stageDone()
{
    const QStringList outputs = m_outputs;
    m_outputs.clear();
    m_queue.clear();
    m_index = 0;
    const bool reduces = m_task == Task::SummarizeDocument || m_task == Task::SummarizeSegments;
    if (!reduces) {
        finish(outputs.join(QStringLiteral("\n\n")));
        return;
    }
    if (outputs.size() == 1) {
        finish(outputs.first());
        return;
    }
    // Merge the partial results: all at once if they fit, otherwise in groups and then again.
    const QString sep = QStringLiteral("\n\n---\n\n");
    const QString system = combinePrompt(m_task, m_options);
    const QString all = outputs.join(sep);
    if (estimateTokens(all) <= m_maxTokens || m_levels >= 3) {
        m_queue.append({system, all});
    } else {
        ++m_levels;
        QString current;
        int count = 0;
        for (const QString &o : outputs) {
            if (count > 0 && estimateTokens(current) + estimateTokens(o) > m_maxTokens) {
                m_queue.append({system, current});
                current.clear();
                count = 0;
            }
            current += (count ? sep : QString()) + o;
            ++count;
        }
        if (count)
            m_queue.append({system, current});
        m_total += int(m_queue.size());
    }
    sendNext();
}

void Run::fail(const Response &r)
{
    if (m_finished)
        return;
    m_finished = true;
    m_response = r;
    m_response.ok = false;
    emit finished();
}

void Run::finish(const QString &text)
{
    if (m_finished)
        return;
    m_finished = true;
    m_response = Response();
    m_response.ok = true;
    m_response.text = text;
    m_response.attempts = m_requests;
    emit progress(m_total, m_total);
    emit finished();
}

} // namespace Ai::Assist
