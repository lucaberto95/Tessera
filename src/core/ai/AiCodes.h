#pragma once

#include "AiClient.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

class Project;

// AI help with the code system (issue #41): codes for a text, subcodes for a code that is too broad, categories
// for a flat list of codes. The result is always a *proposal* (names, descriptions and what belongs where) that the
// researcher edits and applies; nothing is created by itself. The logic is here, without widgets: prompts
// (resources/ai/*.txt), structured output (JSON Schema), short local numbers in the prompt that are mapped back to
// database ids (and checked: a model can invent them), splitting of long input, and the apply step in one undo step.
namespace Ai::Codes {

enum class Task { SuggestCodes, SuggestSubcodes, GroupCodes };

QString functionId(Task task); // id written in the AI log
QString taskTitle(Task task);  // Italian name

struct Options {
    QString language;         // output language, e.g. "italiano"; empty = Italian
    QString instructions;     // free additional instructions of the user
    int maxSuggestions = 8;   // per request
};

// A numbered entry of the prompt: a segment (subcodes) or a code (groups). refId is the database id.
struct Item {
    qint64 refId = 0;
    QString text;
};

struct Material {
    Task task = Task::SuggestCodes;
    QString subject;          // subcodes: name of the code; groups: empty
    QString subjectNote;      // subcodes: description of the code
    QString text;             // SuggestCodes: the text to code
    QList<Item> items;        // SuggestSubcodes: segments; GroupCodes: "Name - description" of the codes
    QStringList existingNames; // names that must not be proposed again (also used to discard such proposals)

    // Adds a numbered entry; a long text is cut to `kMaxItemChars` so one passage cannot fill a request.
    void addItem(qint64 refId, const QString &text);
    int words() const;        // words that would be sent
    bool isEmpty() const;
};
inline constexpr int kMaxItemChars = 1500;

// One proposed code. refIds are database ids: segments to move into the new subcode, or codes to put under the new
// category; empty for SuggestCodes.
struct Proposal {
    QString name;
    QString description;
    QList<qint64> refIds;
    bool selected = true;
};

struct Result {
    QList<Proposal> proposals;
    int invalidIds = 0;        // numbers returned by the model that were not in the prompt (discarded)
    int skippedProposals = 0;  // empty name, already existing name, no valid member
};

// One request: the user message and the map from the numbers shown in it to database ids.
struct Chunk {
    QString user;
    QHash<int, qint64> ids;
};

QString systemPrompt(Task task, const Options &options, const QStringList &existing);
QJsonObject schemaFor(Task task);
// Splits the material: SuggestCodes by tokens (splitIntoChunks), the others by tokens and by at most
// `maxItems` numbered entries per request (numbers restart from 1 in each chunk).
QList<Chunk> planChunks(const Material &material, int maxTokens, int maxItems);
// Reads the structured answer of one request. `ids` maps the numbers of that request to database ids.
// Adds to `into` (proposals with the same name, case-insensitively, are merged; an id already assigned to another
// proposal stays where it is).
void mergeAnswer(Task task, const QJsonDocument &json, const QHash<int, qint64> &ids, const QStringList &existingNames,
                 Result &into);

// One execution: one request per chunk, sequential. Nothing is sent before start().
class Run : public QObject {
    Q_OBJECT
public:
    Run(Client *client, const Material &material, const Options &options, QObject *parent = nullptr);

    void setMaxChunkTokens(int tokens) { m_maxTokens = qMax(200, tokens); }
    void setMaxItemsPerRequest(int items) { m_maxItems = qMax(1, items); }
    void start();
    void cancel();
    bool isFinished() const { return m_finished; }
    Response response() const { return m_response; } // ok / error
    Result result() const { return m_result; }
    int requestsMade() const { return m_requests; }

signals:
    void progress(int done, int total);
    void finished();

private:
    void sendNext();
    void onReply(Reply *reply);
    void fail(const Response &r);
    void finish();

    QPointer<Client> m_client;
    Material m_material;
    Options m_options;
    int m_maxTokens = 3000;
    int m_maxItems = 40;
    QList<Chunk> m_chunks;
    int m_index = 0;
    bool m_started = false;
    bool m_finished = false;
    bool m_cancelled = false;
    int m_requests = 0;
    Response m_response;
    Result m_result;
    QPointer<Reply> m_current;
};

// Applies the proposals the user accepted, in ONE undo step.
//   SuggestCodes:    new codes under `contextId` (0 = top level).
//   SuggestSubcodes: new subcodes of `contextId` (the code) and the segments of that code listed in refIds move into them.
//   GroupCodes:      new categories under `contextId` (the parent of the listed codes, 0 = top level) and the codes
//                    listed in refIds (children of `contextId`) move under them.
// `provenance` (e.g. "Proposto dall'AI (modello X)") is appended to the description of every new code; empty = none.
// What changed since the proposal (deleted segments, moved codes, names now taken) is skipped and counted.
struct ApplyReport {
    int createdCodes = 0;
    int movedSegments = 0;
    int movedCodes = 0;
    int skipped = 0;
    QList<qint64> newCodeIds;
};
ApplyReport apply(Project &project, Task task, qint64 contextId, const QList<Proposal> &accepted,
                  const QString &provenance);

QString provenanceNote(const Config &config); // "Proposto dall'AI (modello X)"

} // namespace Ai::Codes
