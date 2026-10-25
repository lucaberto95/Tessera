#include "ui/Language.h"

#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace {
const QString kSettingsKey = QStringLiteral("ui/language");
}

QStringList Language::available()
{
    return {QStringLiteral("it"), QStringLiteral("en")};
}

QString Language::nativeName(const QString &code)
{
    if (code == QLatin1String("en"))
        return QStringLiteral("English");
    return QStringLiteral("Italiano");
}

QString Language::storedChoice()
{
    const QString v = QSettings().value(kSettingsKey).toString();
    return available().contains(v) ? v : QString();
}

void Language::setStoredChoice(const QString &code)
{
    if (code.isEmpty())
        QSettings().remove(kSettingsKey);
    else
        QSettings().setValue(kSettingsKey, code);
}

QString Language::resolve(const QString &choice)
{
    if (available().contains(choice))
        return choice;
    for (const QString &name : QLocale::system().uiLanguages()) {
        const QString base = name.left(2).toLower();
        if (available().contains(base))
            return base;
    }
    return QStringLiteral("it");
}

bool Language::load(const QString &code, QTranslator &app, QTranslator &qt, const QString &qtTranslationsDir)
{
    if (code == QLatin1String("it")) {
        // Source language: only the standard Qt dialogs need a translation.
        if (!qtTranslationsDir.isEmpty())
            (void)qt.load(QLocale(QLocale::Italian), QStringLiteral("qtbase"), QStringLiteral("_"), qtTranslationsDir);
        return true;
    }
    const bool ok = app.load(QStringLiteral(":/i18n/tessera_%1.qm").arg(code));
    if (!qtTranslationsDir.isEmpty())
        (void)qt.load(QLocale(code), QStringLiteral("qtbase"), QStringLiteral("_"), qtTranslationsDir);
    return ok;
}
