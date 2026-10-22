#include "AiConfig.h"

#include <QCoreApplication>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QUrl>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

namespace Ai {

namespace {
QString g_testSettings; // non-empty: tests
QString g_testAdmin;
bool g_testMode = false;

QMutex g_memMutex;
QHash<QString, QString> g_memSecrets; // test mode and non-Windows platforms (session only)

std::unique_ptr<QSettings> storage()
{
    if (g_testMode)
        return std::make_unique<QSettings>(g_testSettings, QSettings::IniFormat);
    return std::make_unique<QSettings>();
}
} // namespace

QString providerId(Provider p)
{
    switch (p) {
    case Provider::None: return QStringLiteral("none");
    case Provider::Local: return QStringLiteral("local");
    case Provider::OpenAi: return QStringLiteral("openai");
    case Provider::Anthropic: return QStringLiteral("anthropic");
    case Provider::Mistral: return QStringLiteral("mistral");
    case Provider::Google: return QStringLiteral("google");
    }
    return QStringLiteral("none");
}

Provider providerFromId(const QString &id)
{
    for (Provider p : allProviders())
        if (providerId(p) == id)
            return p;
    return Provider::None;
}

QString providerName(Provider p)
{
    switch (p) {
    case Provider::None: return QCoreApplication::translate("Ai", "Nessuno");
    case Provider::Local: return QCoreApplication::translate("Ai", "Server locale compatibile OpenAI");
    case Provider::OpenAi: return QStringLiteral("OpenAI");
    case Provider::Anthropic: return QStringLiteral("Anthropic");
    case Provider::Mistral: return QStringLiteral("Mistral");
    case Provider::Google: return QStringLiteral("Google (Gemini)");
    }
    return {};
}

QList<Provider> allProviders()
{
    return {Provider::None, Provider::Local, Provider::OpenAi, Provider::Anthropic, Provider::Mistral,
            Provider::Google};
}

bool isCloud(Provider p)
{
    return p != Provider::None && p != Provider::Local;
}

// All cloud providers are reached through their OpenAI-compatible endpoint.
QString defaultBaseUrl(Provider p)
{
    switch (p) {
    case Provider::Local: return QStringLiteral("http://localhost:11434/v1");
    case Provider::OpenAi: return QStringLiteral("https://api.openai.com/v1");
    case Provider::Anthropic: return QStringLiteral("https://api.anthropic.com/v1");
    case Provider::Mistral: return QStringLiteral("https://api.mistral.ai/v1");
    case Provider::Google: return QStringLiteral("https://generativelanguage.googleapis.com/v1beta/openai");
    case Provider::None: break;
    }
    return {};
}

QString defaultModel(Provider p)
{
    switch (p) {
    case Provider::OpenAi: return QStringLiteral("gpt-4o-mini");
    case Provider::Anthropic: return QStringLiteral("claude-sonnet-4-5");
    case Provider::Mistral: return QStringLiteral("mistral-small-latest");
    case Provider::Google: return QStringLiteral("gemini-2.0-flash");
    default: break;
    }
    return {};
}

QString Config::validate() const
{
    if (provider == Provider::None)
        return QCoreApplication::translate("Ai", "Nessun provider AI configurato.");
    const QUrl url(baseUrl.trimmed(), QUrl::StrictMode);
    if (baseUrl.trimmed().isEmpty() || !url.isValid() || url.host().isEmpty()
        || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
        return QCoreApplication::translate("Ai", "L'indirizzo del server non è valido (esempio: http://localhost:11434/v1).");
    if (isCloud(provider) && url.scheme() != QLatin1String("https"))
        return QCoreApplication::translate("Ai", "Un provider cloud richiede una connessione https.");
    if (model.trimmed().isEmpty())
        return QCoreApplication::translate("Ai", "Manca il modello: sceglierlo dall'elenco o scriverne il nome.");
    if (isCloud(provider) && apiKey.trimmed().isEmpty())
        return QCoreApplication::translate("Ai", "Manca la chiave API del provider.");
    if (timeoutSec < 1)
        return QCoreApplication::translate("Ai", "Il timeout deve essere di almeno un secondo.");
    return {};
}

bool Config::isConfigured() const
{
    return validate().isEmpty();
}

// ---------------------------------------------------------------- secrets

namespace Secrets {

namespace {
QString target(const QString &name) { return QStringLiteral("Tessera/AI/") + name; }
} // namespace

bool write(const QString &name, const QString &secret)
{
#ifdef Q_OS_WIN
    if (!g_testMode) {
        const QString t = target(name);
        const QByteArray blob(reinterpret_cast<const char *>(secret.utf16()), int(secret.size() * sizeof(char16_t)));
        CREDENTIALW cred = {};
        cred.Type = CRED_TYPE_GENERIC;
        cred.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(t.utf16()));
        cred.CredentialBlobSize = DWORD(blob.size());
        cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(blob.constData()));
        cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
        cred.UserName = const_cast<LPWSTR>(L"Tessera");
        return CredWriteW(&cred, 0) != FALSE;
    }
#endif
    QMutexLocker lock(&g_memMutex);
    g_memSecrets.insert(name, secret);
    return true;
}

QString read(const QString &name)
{
#ifdef Q_OS_WIN
    if (!g_testMode) {
        const QString t = target(name);
        PCREDENTIALW cred = nullptr;
        if (!CredReadW(reinterpret_cast<LPCWSTR>(t.utf16()), CRED_TYPE_GENERIC, 0, &cred))
            return {};
        const QString out = QString::fromUtf16(reinterpret_cast<const char16_t *>(cred->CredentialBlob),
                                               qsizetype(cred->CredentialBlobSize / sizeof(char16_t)));
        CredFree(cred);
        return out;
    }
#endif
    QMutexLocker lock(&g_memMutex);
    return g_memSecrets.value(name);
}

bool remove(const QString &name)
{
#ifdef Q_OS_WIN
    if (!g_testMode) {
        const QString t = target(name);
        return CredDeleteW(reinterpret_cast<LPCWSTR>(t.utf16()), CRED_TYPE_GENERIC, 0) != FALSE;
    }
#endif
    QMutexLocker lock(&g_memMutex);
    return g_memSecrets.remove(name) != 0;
}

} // namespace Secrets

// ---------------------------------------------------------------- settings

namespace Settings {

QString adminFilePath()
{
    if (g_testMode)
        return g_testAdmin;
    return QCoreApplication::applicationDirPath() + QStringLiteral("/tessera.ini");
}

namespace {
std::unique_ptr<QSettings> adminFile()
{
    return std::make_unique<QSettings>(adminFilePath(), QSettings::IniFormat);
}
bool adminFlag(const QString &key)
{
    if (adminFilePath().isEmpty())
        return false;
    const QVariant v = adminFile()->value(QStringLiteral("AI/") + key);
    return v.toString().toLower() == QLatin1String("true") || v.toString() == QLatin1String("1");
}
} // namespace

bool isLocked()
{
    return adminFlag(QStringLiteral("Locked")) || adminFlag(QStringLiteral("Disabled"));
}

QString apiKeySecretName(Provider p)
{
    return QStringLiteral("key-") + providerId(p);
}

Config load()
{
    Config c;
    if (adminFlag(QStringLiteral("Disabled")))
        return c; // provider none
    const bool locked = adminFlag(QStringLiteral("Locked"));
    // Locked: the values come from the administrator file (keys capitalized), otherwise from the user settings.
    std::unique_ptr<QSettings> s = locked ? adminFile() : storage();
    auto get = [&](const char *name, const QVariant &def = {}) {
        QString key = QString::fromLatin1(name);
        if (locked)
            return s->value(QStringLiteral("AI/") + key, def);
        key[0] = key[0].toLower();
        return s->value(QStringLiteral("ai/") + key, def);
    };
    c.provider = providerFromId(get("Provider").toString());
    c.baseUrl = get("Url").toString();
    c.model = get("Model").toString();
    c.timeoutSec = get("Timeout", 120).toInt();
    if (c.timeoutSec < 1)
        c.timeoutSec = 120;
    if (c.provider != Provider::None)
        c.apiKey = Secrets::read(apiKeySecretName(c.provider));
    return c;
}

bool save(const Config &config, QString *error)
{
    if (isLocked()) {
        if (error)
            *error = QCoreApplication::translate("Ai", "Le impostazioni AI sono bloccate dall'amministratore.");
        return false;
    }
    const auto s = storage();
    s->setValue(QStringLiteral("ai/provider"), providerId(config.provider));
    s->setValue(QStringLiteral("ai/url"), config.baseUrl.trimmed());
    s->setValue(QStringLiteral("ai/model"), config.model.trimmed());
    s->setValue(QStringLiteral("ai/timeout"), config.timeoutSec);
    // The key goes to the credential store only, never to the settings file.
    if (config.provider != Provider::None) {
        const QString name = apiKeySecretName(config.provider);
        if (config.apiKey.isEmpty())
            Secrets::remove(name);
        else if (!Secrets::write(name, config.apiKey)) {
            if (error)
                *error = QCoreApplication::translate("Ai", "Impossibile salvare la chiave API nell'archivio credenziali.");
            return false;
        }
    }
    s->sync();
    return true;
}

bool cloudConfirmed(Provider p)
{
    return isCloud(p) && storage()->value(QStringLiteral("ai/confirmed/") + providerId(p), false).toBool();
}

void setCloudConfirmed(Provider p, bool confirmed)
{
    const auto s = storage();
    s->setValue(QStringLiteral("ai/confirmed/") + providerId(p), confirmed);
    s->sync();
}

bool mayCall(const Config &config)
{
    if (!config.isConfigured())
        return false;
    return !isCloud(config.provider) || cloudConfirmed(config.provider);
}

QString cloudNotice(Provider p)
{
    return QCoreApplication::translate("Ai",
               "Con %1 i testi che scegli di analizzare (documenti, segmenti, nomi dei codici e quanto altro "
               "appare nella richiesta) vengono inviati via internet ai server di %1, fuori dal tuo computer. "
               "Verifica che i partecipanti alla ricerca abbiano acconsentito e che il comitato etico lo permetta.\n\n"
               "Ogni invio parte solo da un'azione esplicita e prima dell'invio è indicato che cosa viene inviato. "
               "Per tenere i dati sul computer usa un server locale (Ollama, LM Studio, llama.cpp).")
        .arg(providerName(p));
}

void useFilesForTesting(const QString &settingsIni, const QString &adminIni)
{
    g_testMode = !settingsIni.isEmpty() || !adminIni.isEmpty();
    g_testSettings = settingsIni;
    g_testAdmin = adminIni;
    QMutexLocker lock(&g_memMutex);
    g_memSecrets.clear();
}

} // namespace Settings

QString authorName(const Config &config)
{
    return QStringLiteral("AI (modello %1)").arg(config.model.trimmed().isEmpty() ? QStringLiteral("?") : config.model.trimmed());
}

} // namespace Ai
