#pragma once

#include "core/Model.h"
#include "core/ai/AiAssist.h"

#include <QDialog>

#include <functional>

class Project;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// Result panel of the AI functions (issue #40): summary of a document or of the segments of a code, explanation and
// translation of a passage. It shows what will be sent and to whom, sends only when the user presses "Genera"
// (the request can be cancelled), shows the result in an editable box and saves it only when the user presses
// "Salva come memo" (author "AI (modello X)"; the save is one undo step) or "Salva come sintesi".
class AiAssistDialog : public QDialog {
    Q_OBJECT
public:
    // Where "Salva come memo" writes. When the element already has a memo the text is appended to it.
    struct MemoDestination {
        MemoTarget type = MemoTarget::Document;
        qint64 id = 0;
        QString quote; // put before the text in the memo (the explained/translated passage)
    };

    AiAssistDialog(Project *project, Ai::Assist::Task task, const QList<Ai::Assist::Source> &sources,
                   const QString &subject, const MemoDestination &destination, QWidget *parent = nullptr);
    ~AiAssistDialog() override;

    // Summary of the segments of one code in one document: also saveable in the summary grid (issue #14).
    void setSummaryTarget(qint64 documentId, qint64 codeId);

    Ai::Assist::Options options() const;
    void setOptions(const Ai::Assist::Options &options);
    void setMaxChunkTokens(int tokens) { m_maxChunkTokens = tokens; }
    // Tests replace the question asked before the first use of a cloud provider.
    void setConfirmHandler(std::function<bool(const QString &)> handler) { m_confirm = std::move(handler); }

    void generate();            // "Genera" / "Rigenera": the only place where a request starts
    void cancelRequest();
    bool isBusy() const { return m_run != nullptr; }
    QString resultText() const;
    void setResultText(const QString &text);
    QString statusText() const;
    bool statusIsError() const { return m_statusError; }
    QString inputDescription() const; // "1 documento, ~320 parole"
    QString recipientText() const;    // who receives the text: provider, model, local or cloud

    qint64 saveAsMemo();   // 0 on failure or when there is no result
    bool saveAsSummary();
    void copyResult();

    void reject() override;

private:
    void setStatus(const QString &text, bool error);
    void updateEnabled();
    void updateRecipient();

    Project *m_project;
    Ai::Assist::Task m_task;
    QList<Ai::Assist::Source> m_sources;
    MemoDestination m_dest;
    qint64 m_summaryDoc = 0;
    qint64 m_summaryCode = 0;
    int m_maxChunkTokens = 3000;
    Ai::Config m_usedConfig; // what produced the shown result

    QComboBox *m_length = nullptr;
    QComboBox *m_language = nullptr;
    QLineEdit *m_instructions = nullptr;
    QLabel *m_recipient;
    QLabel *m_input;
    QLabel *m_status;
    QPlainTextEdit *m_result;
    QPushButton *m_generate;
    QPushButton *m_cancel;
    QPushButton *m_saveMemo;
    QPushButton *m_saveSummary;
    QPushButton *m_copy;
    Ai::Client *m_client = nullptr;
    Ai::Assist::Run *m_run = nullptr;
    bool m_statusError = false;
    bool m_hasResult = false;
    std::function<bool(const QString &)> m_confirm;
};
