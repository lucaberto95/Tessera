#pragma once

#include "core/ai/AiCodes.h"

#include <QDialog>
#include <QHash>
#include <QSet>

#include <functional>

class Project;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QSpinBox;
class QTableWidget;

// Panel of the AI help with the code system (issue #41): codes for a text, subcodes for a code, categories for a
// flat list of codes. It shows what will be sent and to whom, sends only when the user presses "Genera" (the request
// can be cancelled) and shows the answer as an editable *proposal*: one row per code with a check box, an editable
// name and description, and, for subcodes and categories, the list of what would go where (each item can be left
// out). "Applica" creates the checked codes (and moves segments or codes) in one undo step; the new codes carry a
// provenance note in the description, which the user can switch off.
class AiCodesDialog : public QDialog {
    Q_OBJECT
public:
    // contextId: SuggestCodes = initial parent of the new codes (0 = top level, can be changed in the panel);
    // SuggestSubcodes = the code; GroupCodes = the parent of the listed codes (0 = top level).
    AiCodesDialog(Project *project, const Ai::Codes::Material &material, const QString &subject, qint64 contextId,
                  QWidget *parent = nullptr);
    ~AiCodesDialog() override;

    Ai::Codes::Options options() const;
    void setOptions(const Ai::Codes::Options &options);
    void setMaxChunkTokens(int tokens) { m_maxChunkTokens = tokens; }
    void setMaxItemsPerRequest(int items) { m_maxItems = items; }
    void setConfirmHandler(std::function<bool(const QString &)> handler) { m_confirm = std::move(handler); }

    void generate();            // "Genera" / "Rigenera": the only place where a request starts
    void cancelRequest();
    bool isBusy() const { return m_run != nullptr; }
    QString statusText() const;
    bool statusIsError() const { return m_statusError; }
    QString inputDescription() const; // "12 segmenti, ~320 parole"
    QString recipientText() const;
    qint64 parentId() const;          // where new codes are created (SuggestCodes: the chosen one)

    // The proposals as edited: only the checked rows, with the edited name/description and without the items the
    // user left out.
    QList<Ai::Codes::Proposal> acceptedProposals() const;
    int proposalCount() const;
    void setProvenanceEnabled(bool on);
    bool provenanceEnabled() const;

    Ai::Codes::ApplyReport applyProposals(); // "Applica": creates the codes, one undo step

    void reject() override;

private:
    void setStatus(const QString &text, bool error);
    void updateEnabled();
    void fillTable(const Ai::Codes::Result &result);
    void showDetail();
    void onDetailChanged(QListWidgetItem *item);
    QString itemText(qint64 refId) const;

    Project *m_project;
    Ai::Codes::Material m_material;
    qint64 m_contextId;
    int m_maxChunkTokens = 3000;
    int m_maxItems = 40;
    Ai::Config m_usedConfig;

    QComboBox *m_language;
    QLineEdit *m_instructions;
    QSpinBox *m_max;
    QComboBox *m_parent = nullptr;
    QLabel *m_recipient;
    QLabel *m_input;
    QLabel *m_status;
    QTableWidget *m_table;
    QListWidget *m_detail;
    QCheckBox *m_provenance;
    QPushButton *m_generate;
    QPushButton *m_cancel;
    QPushButton *m_apply;
    Ai::Client *m_client = nullptr;
    Ai::Codes::Run *m_run = nullptr;
    QList<QList<qint64>> m_rowRefs;   // refIds proposed for each table row
    QList<QSet<qint64>> m_rowLeftOut; // items the user unchecked, per row
    bool m_statusError = false;
    bool m_filling = false;
    std::function<bool(const QString &)> m_confirm;
};