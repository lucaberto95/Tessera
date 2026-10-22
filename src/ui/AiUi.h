#pragma once

#include "core/ai/AiClient.h"

#include <QString>

#include <functional>

namespace Ui {

// Who receives the text of an AI request: provider, model, local or cloud (shown before every request).
QString aiRecipientText();

// The gate every AI dialog passes when the user presses the button that sends: loads the settings, asks (once per
// provider) the confirmation for a cloud provider with `confirm`, and builds the client. Returns null and fills
// `error` (Italian, for the user) when no provider is configured or the user did not confirm. `used` receives the
// configuration the client was built with (it names the author of what the AI produces).
Ai::Client *makeConfirmedAiClient(QObject *owner, const std::function<bool(const QString &)> &confirm,
                                  Ai::Config *used, QString *error);

} // namespace Ui
