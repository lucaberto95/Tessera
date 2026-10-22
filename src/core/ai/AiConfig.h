#pragma once

#include <QString>
#include <QStringList>

// Configuration of the language-model provider (issue #39). Local first: the default provider is "none"; a local
// OpenAI-compatible server (Ollama, LM Studio, llama.cpp, vLLM) keeps the data on the machine; cloud providers
// need an explicit choice and a confirmation of what is sent and to whom.
//
// Secrets: the API key is never written to the .tsra file, to QSettings or to logs. It lives in the Windows
// Credential Manager (Ai::Secrets); on other platforms it is kept only for the running session.
namespace Ai {

enum class Provider { None, Local, OpenAi, Anthropic, Mistral, Google };

QString providerId(Provider p);              // stable id stored in the settings
Provider providerFromId(const QString &id);
QString providerName(Provider p);            // Italian display name
QList<Provider> allProviders();
bool isCloud(Provider p);
QString defaultBaseUrl(Provider p);          // OpenAI-compatible endpoint (up to and including /v1)
QString defaultModel(Provider p);

struct Config {
    Provider provider = Provider::None;
    QString baseUrl;     // e.g. http://localhost:11434/v1
    QString model;
    QString apiKey;      // optional for local servers; never persisted outside Ai::Secrets
    int timeoutSec = 120;

    // A provider with a usable URL and a model: calls can be made (cloud providers also need the confirmation).
    bool isConfigured() const;
    QString validate() const; // empty when fine, otherwise the problem in Italian
};

// Where the key is stored. `name` identifies the provider (e.g. "openai").
namespace Secrets {
bool write(const QString &name, const QString &secret);
QString read(const QString &name);
bool remove(const QString &name);
} // namespace Secrets

// Persistent settings (QSettings + Secrets) and the administrator lock.
namespace Settings {

// Administrator file next to the executable (tessera.ini):
//   [AI]
//   Locked=true        ; users cannot change the AI settings
//   Disabled=true      ; no AI at all (provider none)
//   Provider=local     ; with Locked: the provider, Url, Model and Timeout are taken from here
//   Url=http://server:11434/v1
//   Model=llama3
//   Timeout=180
// The API key is never read from the file.
QString adminFilePath();
bool isLocked();   // Locked=true or Disabled=true in the administrator file

Config load();                        // saved settings (or the locked ones), with the key from Secrets
bool save(const Config &config, QString *error = nullptr); // refuses when locked
QString apiKeySecretName(Provider p); // name of the key in Secrets, one per cloud provider/local

// A cloud provider can be used only after the user confirmed, once per provider, that data leave the machine.
bool cloudConfirmed(Provider p);
void setCloudConfirmed(Provider p, bool confirmed);
bool mayCall(const Config &config); // configured and (local or confirmed): the gate every AI feature must pass
QString cloudNotice(Provider p);    // what is sent and to whom, shown at first use

// Tests: use these files instead of the real QSettings and administrator file, and keep keys in memory only.
void useFilesForTesting(const QString &settingsIni, const QString &adminIni);

} // namespace Settings

// "Autore" of the coding made by the AI: "AI (modello X)".
QString authorName(const Config &config);

} // namespace Ai
