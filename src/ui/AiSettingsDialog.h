#pragma once

#include "core/ai/AiClient.h"

#include <QDialog>

#include <functional>

class Project;
class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

// "Impostazioni > AI..." (issue #39): provider (none / local OpenAI-compatible server / cloud), address, model,
// API key, timeout, connection test and the optional log of AI requests in the project. Nothing is sent anywhere
// until the user presses "Prova connessione" or "Aggiorna elenco" (and, for a cloud provider, confirms the notice).
class AiSettingsDialog : public QDialog {
    Q_OBJECT
public:
    // `project` may be null (no project open): then the log option is disabled.
    explicit AiSettingsDialog(Project *project, QWidget *parent = nullptr);
    ~AiSettingsDialog() override;

    Ai::Config config() const; // what the fields currently say
    void setConfig(const Ai::Config &config);
    void selectProvider(Ai::Provider provider);

    // Saves the settings (asking the cloud confirmation first). False if the user declined or saving failed.
    bool apply();
    void testConnection();    // "Prova connessione"
    void refreshModels();     // "Aggiorna elenco"
    bool isBusy() const { return m_reply != nullptr; }
    QString statusText() const;
    bool statusIsError() const { return m_statusError; }
    QStringList modelList() const;

    // Tests replace the cloud-notice question.
    void setConfirmHandler(std::function<bool(const QString &)> handler) { m_confirm = std::move(handler); }

    void accept() override;
    void reject() override;

private:
    void providerChanged();
    void updateEnabled();
    void setStatus(const QString &text, bool error);
    bool ensureCloudConfirmed();
    void startCall(bool listOnly);

    Project *m_project;
    QComboBox *m_provider;
    QLineEdit *m_url;
    QComboBox *m_model;
    QLineEdit *m_key;
    QSpinBox *m_timeout;
    QCheckBox *m_logRequests;
    QLabel *m_lockLabel;
    QLabel *m_status;
    QPushButton *m_test;
    QPushButton *m_refresh;
    QDialogButtonBox *m_buttons;
    Ai::Reply *m_reply = nullptr;
    Ai::Client *m_client = nullptr;
    bool m_locked = false;
    bool m_statusError = false;
    Ai::Provider m_shown = Ai::Provider::None;
    std::function<bool(const QString &)> m_confirm;
};
