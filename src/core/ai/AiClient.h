#pragma once

#include "AiConfig.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// Client for OpenAI-compatible servers (/chat/completions and /models): Ollama, LM Studio, llama.cpp, vLLM and
// the cloud providers that expose that API (issue #39). Asynchronous, cancellable, with a timeout, with errors
// turned into messages the user can act on. Nothing here runs by itself: a request exists only when a feature
// calls chat()/listModels()/testConnection() after an explicit action of the user, and only if
// Ai::Settings::mayCall(config) is true (Ai::makeClient enforces both the configuration and the cloud consent).
namespace Ai {

enum class ErrorKind {
    None,
    Config,        // incomplete configuration
    Unreachable,   // connection refused / host not found / closed
    Timeout,
    Unauthorized,  // 401/403: wrong or missing API key
    ModelNotFound,
    Http,          // any other HTTP error
    BadResponse,   // reply that is not what the API promises
    InvalidJson,   // structured output still invalid after the retries
    Cancelled
};

struct Message {
    QString role; // system | user | assistant
    QString content;
};

struct Request {
    QList<Message> messages;
    // JSON Schema of the expected answer. When not empty the reply is structured output: asked with
    // response_format (json_schema) where the server supports it, otherwise with an instruction in the prompt,
    // then parsed and checked; invalid JSON is retried up to maxRetries times.
    QJsonObject schema;
    QString schemaName = QStringLiteral("result");
    double temperature = 0.0;
    int maxTokens = 0;   // 0 = server default
    int maxRetries = 1;  // new attempts after invalid JSON
};

struct Response {
    bool ok = false;
    ErrorKind kind = ErrorKind::None;
    QString error;           // message for the user (Italian), empty when ok
    int httpStatus = 0;
    QString text;            // content of the first choice
    QJsonDocument json;      // parsed content, when a schema was given
    QStringList models;      // listModels()
    int attempts = 0;        // HTTP requests made for a chat (retries included)
};

// One call in flight. finished() is emitted exactly once, also after cancel().
class Reply : public QObject {
    Q_OBJECT
public:
    bool isFinished() const { return m_finished; }
    Response response() const { return m_response; }
    void cancel();

signals:
    void finished();

private:
    friend class Client;
    friend class ChatJob;
    friend class ModelsJob;
    friend class TestJob;
    explicit Reply(QObject *parent) : QObject(parent) {}
    void finish(const Response &r);

    Response m_response;
    bool m_finished = false;
    bool m_cancelled = false;
    QPointer<QNetworkReply> m_net;
    QPointer<Reply> m_sub;
};

class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(const Config &config, QObject *parent = nullptr);
    ~Client() override;

    const Config &config() const { return m_config; }
    void setTimeoutMs(int ms) { m_timeoutMs = ms; } // overrides config.timeoutSec (tests)

    Reply *chat(const Request &request);
    Reply *listModels();
    // Lists the models and checks that the configured one is there (falls back to a one-token request on
    // servers without /models). Response.ok = reachable, authorized and model present.
    Reply *testConnection();

    // For tests/diagnostics: the JSON body chat() would send for this attempt.
    QByteArray buildChatBody(const Request &request, bool withResponseFormat) const;

private:
    friend class ChatJob;
    friend class ModelsJob;
    friend class TestJob;
    QNetworkReply *send(const QString &path, const QByteArray &body); // POST when body is not empty, else GET
    int timeoutMs() const;

    Config m_config;
    int m_timeoutMs = 0;
    QNetworkAccessManager *m_nam;
};

// The only way features should obtain a client: null (with `error` filled in) when no provider is configured or
// a cloud provider has not been confirmed by the user.
Client *makeClient(const Config &config, QObject *parent, QString *error = nullptr);

// Rough token estimate (characters / 4) and splitting of a long text into chunks that fit `maxTokens`, cut at
// paragraph, line, sentence or word boundaries (in this order); the chunks joined give back the text exactly.
int estimateTokens(const QString &text);
QStringList splitIntoChunks(const QString &text, int maxTokens);

// "3 documenti, ~12.000 parole": what a request is going to send, shown before every call.
int countWords(const QString &text);
QString describeInput(int documents, qint64 words);

} // namespace Ai
