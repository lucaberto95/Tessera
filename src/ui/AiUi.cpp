#include "AiUi.h"

#include <QCoreApplication>

namespace Ui {

namespace {
QString tr(const char *text)
{
    return QCoreApplication::translate("AiUi", text);
}
} // namespace

QString aiRecipientText()
{
    const Ai::Config c = Ai::Settings::load();
    if (c.provider == Ai::Provider::None)
        return tr("Nessun provider AI configurato: apri Impostazioni > AI.");
    if (Ai::isCloud(c.provider))
        return tr("Destinatario: %1, modello %2. Il testo esce dal tuo computer.").arg(Ai::providerName(c.provider), c.model);
    return tr("Destinatario: %1, modello %2 (il testo non lascia il computer).").arg(Ai::providerName(c.provider), c.model);
}

Ai::Client *makeConfirmedAiClient(QObject *owner, const std::function<bool(const QString &)> &confirm, Ai::Config *used,
                                  QString *error)
{
    const Ai::Config config = Ai::Settings::load();
    if (config.provider == Ai::Provider::None) {
        if (error)
            *error = tr("Nessun provider AI configurato: apri Impostazioni > AI.");
        return nullptr;
    }
    if (Ai::isCloud(config.provider) && !Ai::Settings::cloudConfirmed(config.provider)) {
        if (!confirm(Ai::Settings::cloudNotice(config.provider))) {
            if (error)
                *error = tr("Operazione annullata: l'invio dei dati al provider cloud non è stato confermato.");
            return nullptr;
        }
        Ai::Settings::setCloudConfirmed(config.provider, true);
    }
    QString message;
    Ai::Client *client = Ai::makeClient(config, owner, &message);
    if (!client) {
        if (error)
            *error = message;
        return nullptr;
    }
    if (used)
        *used = config;
    return client;
}

} // namespace Ui
