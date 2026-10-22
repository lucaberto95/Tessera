#pragma once

#include "AiClient.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

// First AI functions (issue #40): summary of a document, summary of the segments of a code, explanation and
// translation of a passage. The logic is here, without widgets: prompts (resources/ai/*.txt, written from
// scratch, with the output language always explicit), splitting of long texts and map-reduce, one request at a
// time. Nothing is sent by itself: a Run sends only after start(), which a feature calls after an explicit action
// of the user, with a client obtained from Ai::makeClient (provider configured and, for the cloud, confirmed).
namespace Ai::Assist {

enum class Task { SummarizeDocument, SummarizeSegments, Explain, Translate };
enum class Length { Short, Medium, Long };

struct Options {
    Length length = Length::Medium; // not used by Translate
    QString language;               // output language, e.g. "italiano"; empty = Italian
    QString instructions;           // free additional instructions of the user
};

// A piece of material. For the segments of a code `label` is the citation shown to the model and in the
// result, e.g. "Intervista 3" (becomes "[Intervista 3]"); empty for a document or a selected passage.
struct Source {
    QString label;
    QString text;
};

QString functionId(Task task);   // id written in the AI log
QString taskTitle(Task task);    // Italian name
bool usesLength(Task task);
QString lengthName(Length length); // Italian
QString lengthInstruction(Length length, Task task); // sentence put in the prompt

// System prompt of the task with language, length and instructions filled in.
QString systemPrompt(Task task, const Options &options);
// Prompt that merges partial results (the "reduce" step of long inputs).
QString combinePrompt(Task task, const Options &options);

// The user message for a list of sources: the text as is, or one "[label]\ntext" block per source.
QString userText(Task task, const QList<Source> &sources);

// How the input is cut: chunks of at most `maxTokens` (estimateTokens), each source whole where it fits and
// cut at natural boundaries where not (the label is repeated), sources grouped greedily. Chunks of a document
// joined give back the text.
QStringList planChunks(Task task, const QList<Source> &sources, int maxTokens);

int wordsOf(const QList<Source> &sources);

// One execution: a single request for short inputs, otherwise one request per chunk and then, for summaries,
// requests that merge the partial results; translations and explanations are concatenated. Sequential.
class Run : public QObject {
    Q_OBJECT
public:
    Run(Client *client, Task task, const Options &options, const QList<Source> &sources, QObject *parent = nullptr);

    void setMaxChunkTokens(int tokens) { m_maxTokens = qMax(200, tokens); }
    void start();   // sends the first request
    void cancel();  // stops; finished() is emitted with ErrorKind::Cancelled
    bool isFinished() const { return m_finished; }
    Response response() const { return m_response; } // text = the result when ok
    int requestsMade() const { return m_requests; }

signals:
    void progress(int done, int total); // estimated total
    void finished();

private:
    struct Job {
        QString system;
        QString user;
    };
    void sendNext();
    void onReply(Reply *reply);
    void stageDone();
    void fail(const Response &r);
    void finish(const QString &text);

    QPointer<Client> m_client;
    Task m_task;
    Options m_options;
    QList<Source> m_sources;
    int m_maxTokens = 3000;
    QList<Job> m_queue;
    int m_index = 0;
    QStringList m_outputs;
    bool m_started = false;
    bool m_finished = false;
    bool m_cancelled = false;
    int m_requests = 0;
    int m_total = 1;
    int m_levels = 0;
    Response m_response;
    QPointer<Reply> m_current;
};

} // namespace Ai::Assist
