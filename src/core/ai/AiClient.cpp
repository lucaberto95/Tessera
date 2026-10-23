#include "AiClient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>

namespace Ai {

namespace {

// Message of an API error body: {"error": {"message": "..."}} or {"error": "..."}.
QString apiErrorMessage(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject())
        return {};
    const QJsonValue e = doc.object().value(QStringLiteral("error"));
    if (e.isString())
        return e.toString();
    if (e.isObject())
        return e.toObject().value(QStringLiteral("message")).toString();
    return doc.object().value(QStringLiteral("message")).toString();
}

bool mentionsModel(const QByteArray &body)
{
    const QString s = QString::fromUtf8(body).toLower();
    return s.contains(QLatin1String("model"))
           && (s.contains(QLatin1String("not found")) || s.contains(QLatin1String("not_found"))
               || s.contains(QLatin1String("does not exist")) || s.contains(QLatin1String("not exist"))
               || s.contains(QLatin1String("unknown")));
}

// Turns a finished (or failed) network reply into a Response. `cancelled` = the user cancelled.
Response classify(QNetworkReply *net, const QByteArray &body, bool cancelled, const QString &model)
{
    Response r;
    r.httpStatus = net->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (cancelled) {
        r.kind = ErrorKind::Cancelled;
        r.error = QCoreApplication::translate("Ai", "Richiesta annullata.");
        return r;
    }
    const QNetworkReply::NetworkError err = net->error();
    if (r.httpStatus >= 300 || (err != QNetworkReply::NoError && r.httpStatus > 0)) {
        const QString detail = apiErrorMessage(body);
        const QString suffix = detail.isEmpty() ? QString() : QStringLiteral(" (%1)").arg(detail);
        if (r.httpStatus == 401 || r.httpStatus == 403) {
            r.kind = ErrorKind::Unauthorized;
            r.error = QCoreApplication::translate("Ai", "Il server rifiuta l'accesso: la chiave API manca o non è corretta.") + suffix;
        } else if ((r.httpStatus == 404 || r.httpStatus == 400) && mentionsModel(body)) {
            r.kind = ErrorKind::ModelNotFound;
            r.error = QCoreApplication::translate("Ai", "Il modello «%1» non è disponibile sul server.").arg(model) + suffix;
        } else if (r.httpStatus == 404) {
            r.kind = ErrorKind::Http;
            r.error = QCoreApplication::translate("Ai", "Indirizzo non trovato sul server (404): controlla l'URL, di solito termina con /v1.") + suffix;
        } else if (r.httpStatus == 429) {
            r.kind = ErrorKind::Http;
            r.error = QCoreApplication::translate("Ai", "Troppe richieste o quota esaurita (429): riprova più tardi.") + suffix;
        } else if (r.httpStatus >= 300 && r.httpStatus < 400) {
            r.kind = ErrorKind::Http;
            r.error = QCoreApplication::translate("Ai", "Il server ha risposto con un reindirizzamento (%1): controlla l'URL.").arg(r.httpStatus);
        } else {
            r.kind = ErrorKind::Http;
            r.error = QCoreApplication::translate("Ai", "Errore del server (HTTP %1).").arg(r.httpStatus) + suffix;
        }
        return r;
    }
    switch (err) {
    case QNetworkReply::NoError:
        r.ok = true;
        return r;
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError:
        r.kind = ErrorKind::Timeout;
        r.error = QCoreApplication::translate("Ai", "Il server non ha risposto entro il tempo previsto. Un modello grande può richiedere più tempo: "
                     "aumenta il timeout nelle impostazioni AI.");
        return r;
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::UnknownNetworkError:
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::TemporaryNetworkFailureError:
        r.kind = ErrorKind::Unreachable;
        r.error = QCoreApplication::translate("Ai", "Il server non risponde: è avviato (per esempio Ollama o LM Studio)? Controlla l'indirizzo e la "
                     "connessione.");
        return r;
    case QNetworkReply::SslHandshakeFailedError:
        r.kind = ErrorKind::Unreachable;
        r.error = QCoreApplication::translate("Ai", "Impossibile stabilire una connessione sicura con il server (certificato non valido).");
        return r;
    default:
        r.kind = ErrorKind::Unreachable;
        r.error = QCoreApplication::translate("Ai", "Errore di rete: %1").arg(net->errorString());
        return r;
    }
}

// JSON of a model answer: plain, inside a ``` fence, or the first {...}/[...] found in chatter around it.
QJsonDocument extractJson(const QString &content)
{
    QString s = content.trimmed();
    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(s.toUtf8(), &pe);
    if (pe.error == QJsonParseError::NoError)
        return doc;
    static const QRegularExpression fence(QStringLiteral("```(?:json)?\\s*(.*?)```"),
                                          QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch m = fence.match(s);
    if (m.hasMatch()) {
        doc = QJsonDocument::fromJson(m.captured(1).trimmed().toUtf8(), &pe);
        if (pe.error == QJsonParseError::NoError)
            return doc;
    }
    const int a = s.indexOf(QLatin1Char('{')), b = s.lastIndexOf(QLatin1Char('}'));
    if (a >= 0 && b > a) {
        doc = QJsonDocument::fromJson(s.mid(a, b - a + 1).toUtf8(), &pe);
        if (pe.error == QJsonParseError::NoError)
            return doc;
    }
    return {};
}

// Minimal check against the schema: right top-level type and required properties present.
bool matchesSchema(const QJsonDocument &doc, const QJsonObject &schema)
{
    if (doc.isNull())
        return false;
    const QString type = schema.value(QStringLiteral("type")).toString();
    if (type == QLatin1String("object")) {
        if (!doc.isObject())
            return false;
        const QJsonArray req = schema.value(QStringLiteral("required")).toArray();
        for (const QJsonValue &k : req)
            if (!doc.object().contains(k.toString()))
                return false;
    } else if (type == QLatin1String("array") && !doc.isArray()) {
        return false;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------- Reply

void Reply::finish(const Response &r)
{
    if (m_finished)
        return;
    m_response = r;
    m_finished = true;
    emit finished();
}

// ChatJob and friends are children of their Reply; cancel() reaches the network reply they are waiting for.
void Reply::cancel()
{
    if (m_finished)
        return;
    m_cancelled = true;
    if (m_sub) { // testConnection: a chain of calls, cancel the one in flight
        m_sub->cancel();
        return;
    }
    if (m_net) {
        m_net->abort(); // the job's finished handler reports the cancellation
        return;
    }
    Response r;
    r.kind = ErrorKind::Cancelled;
    r.error = QCoreApplication::translate("Ai", "Richiesta annullata.");
    finish(r);
}

// ---------------------------------------------------------------- Client

Client::Client(const Config &config, QObject *parent)
    : QObject(parent), m_config(config), m_nam(new QNetworkAccessManager(this))
{
}

Client::~Client() = default;

int Client::timeoutMs() const
{
    return m_timeoutMs > 0 ? m_timeoutMs : qMax(1, m_config.timeoutSec) * 1000;
}

QNetworkReply *Client::send(const QString &path, const QByteArray &body)
{
    QString base = m_config.baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QNetworkRequest req{QUrl(base + path)};
    req.setTransferTimeout(timeoutMs());
    // A redirect could carry the API key to another host: never follow one.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setRawHeader("Accept", "application/json");
    if (!m_config.apiKey.trimmed().isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_config.apiKey.trimmed().toUtf8());
    if (body.isEmpty())
        return m_nam->get(req);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    return m_nam->post(req, body);
}

QByteArray Client::buildChatBody(const Request &request, bool withResponseFormat) const
{
    QJsonArray messages;
    const bool structured = !request.schema.isEmpty();
    if (structured && !withResponseFormat) {
        // Servers without response_format: ask for the JSON in the prompt.
        messages.append(QJsonObject{
            {QStringLiteral("role"), QStringLiteral("system")},
            {QStringLiteral("content"),
             QStringLiteral("Reply only with a single JSON value that is valid for this JSON Schema, with no text "
                            "before or after it and no code fence. JSON Schema: ")
                 + QString::fromUtf8(QJsonDocument(request.schema).toJson(QJsonDocument::Compact))}});
    }
    for (const Message &m : request.messages)
        messages.append(QJsonObject{{QStringLiteral("role"), m.role}, {QStringLiteral("content"), m.content}});

    QJsonObject body{{QStringLiteral("model"), m_config.model.trimmed()},
                     {QStringLiteral("messages"), messages},
                     {QStringLiteral("stream"), false},
                     {QStringLiteral("temperature"), request.temperature}};
    if (request.maxTokens > 0)
        body.insert(QStringLiteral("max_tokens"), request.maxTokens);
    if (structured && withResponseFormat) {
        body.insert(QStringLiteral("response_format"),
                    QJsonObject{{QStringLiteral("type"), QStringLiteral("json_schema")},
                                {QStringLiteral("json_schema"),
                                 QJsonObject{{QStringLiteral("name"), request.schemaName},
                                             {QStringLiteral("strict"), true},
                                             {QStringLiteral("schema"), request.schema}}}});
    }
    return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

// ---------------------------------------------------------------- chat

class ChatJob : public QObject {
public:
    ChatJob(Client *client, Reply *reply, const Request &request)
        : QObject(reply), m_c(client), m_r(reply), m_req(request), m_retriesLeft(qMax(0, request.maxRetries))
    {
    }

    void start()
    {
        if (m_r->m_finished)
            return; // cancelled before the queued start
        ++m_attempts;
        QNetworkReply *net = m_c->send(QStringLiteral("/chat/completions"),
                                       m_c->buildChatBody(m_req, m_useFormat));
        m_r->m_net = net;
        connect(net, &QNetworkReply::finished, this, [this, net] { done(net); });
    }

private:
    void fail(const Response &base)
    {
        Response r = base;
        r.attempts = m_attempts;
        m_r->finish(r);
    }

    void done(QNetworkReply *net)
    {
        net->deleteLater();
        const QByteArray body = net->readAll();
        Response r = classify(net, body, m_r->m_cancelled, m_c->config().model);
        if (!r.ok) {
            // Server without response_format support: repeat once asking for the JSON in the prompt.
            const QString low = QString::fromUtf8(body).toLower();
            if (m_useFormat && !m_req.schema.isEmpty() && (r.httpStatus == 400 || r.httpStatus == 422)
                && r.kind != ErrorKind::ModelNotFound
                && (low.contains(QLatin1String("response_format")) || low.contains(QLatin1String("json_schema"))
                    || low.contains(QLatin1String("schema")))) {
                m_useFormat = false;
                start();
                return;
            }
            fail(r);
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        const QJsonArray choices = doc.object().value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            r.ok = false;
            r.kind = ErrorKind::BadResponse;
            r.error = QCoreApplication::translate("Ai", "La risposta del server non è nel formato atteso (manca «choices»): "
                         "il server è compatibile con le API OpenAI?");
            fail(r);
            return;
        }
        const QString content = choices.first().toObject().value(QStringLiteral("message")).toObject()
                                    .value(QStringLiteral("content")).toString();
        r.text = content;
        if (m_req.schema.isEmpty()) {
            r.attempts = m_attempts;
            m_r->finish(r);
            return;
        }
        const QJsonDocument parsed = extractJson(content);
        if (matchesSchema(parsed, m_req.schema)) {
            r.json = parsed;
            r.attempts = m_attempts;
            m_r->finish(r);
            return;
        }
        if (m_retriesLeft > 0) {
            --m_retriesLeft;
            m_req.messages.append({QStringLiteral("assistant"), content});
            m_req.messages.append({QStringLiteral("user"),
                                   QStringLiteral("That answer is not valid JSON for the requested schema. Reply "
                                                  "again with only the JSON value, no other text.")});
            start();
            return;
        }
        r.ok = false;
        r.kind = ErrorKind::InvalidJson;
        r.error = QCoreApplication::translate("Ai", "Il modello non ha restituito una risposta strutturata valida, anche dopo un nuovo tentativo. "
                     "Un modello più grande o un testo più breve possono aiutare.");
        fail(r);
    }

    Client *m_c;
    Reply *m_r;
    Request m_req;
    bool m_useFormat = true;
    int m_retriesLeft;
    int m_attempts = 0;
};

Reply *Client::chat(const Request &request)
{
    auto *reply = new Reply(this);
    const QString problem = m_config.validate();
    if (!problem.isEmpty()) {
        Response r;
        r.kind = ErrorKind::Config;
        r.error = problem;
        QMetaObject::invokeMethod(reply, [reply, r] { reply->finish(r); }, Qt::QueuedConnection);
        return reply;
    }
    auto *job = new ChatJob(this, reply, request);
    QMetaObject::invokeMethod(job, [job] { job->start(); }, Qt::QueuedConnection);
    return reply;
}

// ---------------------------------------------------------------- models

class ModelsJob : public QObject {
public:
    ModelsJob(Client *client, Reply *reply) : QObject(reply), m_c(client), m_r(reply) {}

    void start()
    {
        if (m_r->m_finished)
            return;
        QNetworkReply *net = m_c->send(QStringLiteral("/models"), {});
        m_r->m_net = net;
        connect(net, &QNetworkReply::finished, this, [this, net] {
            net->deleteLater();
            const QByteArray body = net->readAll();
            Response r = classify(net, body, m_r->m_cancelled, m_c->config().model);
            if (r.ok) {
                const QJsonDocument doc = QJsonDocument::fromJson(body);
                QJsonArray data = doc.object().value(QStringLiteral("data")).toArray();
                if (data.isEmpty() && doc.isObject())
                    data = doc.object().value(QStringLiteral("models")).toArray();
                if (!doc.isObject() || (!doc.object().contains(QStringLiteral("data"))
                                        && !doc.object().contains(QStringLiteral("models")))) {
                    r.ok = false;
                    r.kind = ErrorKind::BadResponse;
                    r.error = QCoreApplication::translate("Ai", "Il server ha risposto, ma non con un elenco di modelli: l'indirizzo è corretto "
                                 "(di solito termina con /v1)?");
                } else {
                    for (const QJsonValue &v : data) {
                        QString id = v.toObject().value(QStringLiteral("id")).toString();
                        if (id.isEmpty())
                            id = v.toObject().value(QStringLiteral("name")).toString();
                        if (id.startsWith(QLatin1String("models/")))
                            id = id.mid(7); // Google
                        if (!id.isEmpty())
                            r.models << id;
                    }
                    r.models.removeDuplicates();
                    std::sort(r.models.begin(), r.models.end());
                }
            }
            m_r->finish(r);
        });
    }

private:
    Client *m_c;
    Reply *m_r;
};

Reply *Client::listModels()
{
    auto *reply = new Reply(this);
    // Only the address (and key, for cloud) is needed to list models, not the model name.
    Config probe = m_config;
    if (probe.model.trimmed().isEmpty())
        probe.model = QStringLiteral("-");
    const QString problem = probe.validate();
    if (!problem.isEmpty()) {
        Response r;
        r.kind = ErrorKind::Config;
        r.error = problem;
        QMetaObject::invokeMethod(reply, [reply, r] { reply->finish(r); }, Qt::QueuedConnection);
        return reply;
    }
    auto *job = new ModelsJob(this, reply);
    QMetaObject::invokeMethod(job, [job] { job->start(); }, Qt::QueuedConnection);
    return reply;
}

// ---------------------------------------------------------------- test connection

class TestJob : public QObject {
public:
    TestJob(Client *client, Reply *reply) : QObject(reply), m_c(client), m_r(reply) {}

    void start()
    {
        if (m_r->m_finished)
            return;
        Reply *models = m_c->listModels();
        m_r->m_sub = models;
        connect(models, &Reply::finished, this, [this, models] {
            models->deleteLater();
            Response r = models->response();
            if (m_r->m_cancelled) {
                m_r->finish(r);
                return;
            }
            if (r.ok) {
                const QString want = m_c->config().model.trimmed();
                if (!r.models.isEmpty() && !r.models.contains(want)
                    && !r.models.contains(want + QStringLiteral(":latest"))) {
                    r.ok = false;
                    r.kind = ErrorKind::ModelNotFound;
                    r.error = QCoreApplication::translate("Ai", "Il server risponde, ma il modello «%1» non è tra quelli disponibili.").arg(want);
                }
                m_r->finish(r);
                return;
            }
            // Servers without /models: a one-token request tells whether the model answers.
            if (r.httpStatus == 404 && r.kind == ErrorKind::Http) {
                Request req;
                req.messages = {{QStringLiteral("user"), QStringLiteral("ping")}};
                req.maxTokens = 1;
                Reply *chat = m_c->chat(req);
                m_r->m_sub = chat;
                connect(chat, &Reply::finished, this, [this, chat] {
                    chat->deleteLater();
                    m_r->finish(chat->response());
                });
                return;
            }
            m_r->finish(r);
        });
    }

private:
    Client *m_c;
    Reply *m_r;
};

Reply *Client::testConnection()
{
    auto *reply = new Reply(this);
    Config probe = m_config;
    const QString problem = probe.validate();
    if (!problem.isEmpty()) {
        Response r;
        r.kind = ErrorKind::Config;
        r.error = problem;
        QMetaObject::invokeMethod(reply, [reply, r] { reply->finish(r); }, Qt::QueuedConnection);
        return reply;
    }
    auto *job = new TestJob(this, reply);
    QMetaObject::invokeMethod(job, [job] { job->start(); }, Qt::QueuedConnection);
    return reply;
}

Client *makeClient(const Config &config, QObject *parent, QString *error)
{
    if (!config.isConfigured()) {
        if (error)
            *error = config.validate();
        return nullptr;
    }
    if (isCloud(config.provider) && !Settings::cloudConfirmed(config.provider)) {
        if (error)
            *error = QCoreApplication::translate("Ai", "Prima di usare %1 bisogna confermare l'invio dei dati nelle impostazioni AI.")
                         .arg(providerName(config.provider));
        return nullptr;
    }
    return new Client(config, parent);
}

// ---------------------------------------------------------------- text helpers

int estimateTokens(const QString &text)
{
    return int((text.size() + 3) / 4);
}

namespace {

// Splits at `sep` keeping the separator at the end of each piece, so joining gives back the text.
QStringList splitKeeping(const QString &text, const QString &sep)
{
    QStringList out;
    qsizetype from = 0;
    for (;;) {
        const qsizetype i = text.indexOf(sep, from);
        if (i < 0) {
            if (from < text.size())
                out << text.mid(from);
            break;
        }
        out << text.mid(from, i + sep.size() - from);
        from = i + sep.size();
    }
    return out;
}

void splitRec(const QString &text, qsizetype maxChars, int level, QStringList &pieces)
{
    static const QStringList seps = {QStringLiteral("\n\n"), QStringLiteral("\n"), QStringLiteral(". "),
                                     QStringLiteral(" ")};
    if (text.size() <= maxChars) {
        pieces << text;
        return;
    }
    if (level >= seps.size()) {
        // No separator left: hard cut, never in the middle of a surrogate pair.
        qsizetype i = 0;
        while (i < text.size()) {
            qsizetype n = qMin(maxChars, text.size() - i);
            if (n < text.size() - i && text.at(i + n - 1).isHighSurrogate() && n > 1)
                --n;
            pieces << text.mid(i, n);
            i += n;
        }
        return;
    }
    const QStringList parts = splitKeeping(text, seps.at(level));
    if (parts.size() <= 1) {
        splitRec(text, maxChars, level + 1, pieces);
        return;
    }
    for (const QString &p : parts)
        splitRec(p, maxChars, level + 1, pieces);
}

} // namespace

QStringList splitIntoChunks(const QString &text, int maxTokens)
{
    const qsizetype maxChars = qMax(1, maxTokens) * 4;
    if (text.isEmpty())
        return {};
    QStringList pieces;
    splitRec(text, maxChars, 0, pieces);
    // Greedy merge of consecutive pieces up to the limit.
    QStringList chunks;
    QString cur;
    for (const QString &p : pieces) {
        if (!cur.isEmpty() && cur.size() + p.size() > maxChars) {
            chunks << cur;
            cur.clear();
        }
        cur += p;
    }
    if (!cur.isEmpty())
        chunks << cur;
    return chunks;
}

int countWords(const QString &text)
{
    int n = 0;
    bool in = false;
    for (QChar c : text) {
        const bool w = c.isLetterOrNumber();
        if (w && !in)
            ++n;
        in = w || (in && (c == QLatin1Char('\'') || c == QLatin1Char('-')));
    }
    return n;
}

QString describeInput(int documents, qint64 words)
{
    const QLocale it(QLocale::Italian);
    const QString docs = documents == 1 ? QCoreApplication::translate("Ai", "1 documento") : QCoreApplication::translate("Ai", "%1 documenti").arg(documents);
    return QStringLiteral("%1, ~%2 %3")
        .arg(docs, it.toString(words), words == 1 ? QCoreApplication::translate("Ai", "parola") : QCoreApplication::translate("Ai", "parole"));
}

} // namespace Ai
