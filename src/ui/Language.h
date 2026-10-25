#pragma once

#include <QString>
#include <QStringList>

class QTranslator;

// User interface language. The source language of every tr() string is Italian, so "it" needs no
// translator; other languages are .qm files embedded in the application (":/i18n/tessera_<code>.qm").
namespace Language {

// Codes of the available languages ("it" first).
QStringList available();

// Name of a language written in that language ("Italiano", "English").
QString nativeName(const QString &code);

// Stored choice: "" = follow the system language, otherwise a code from available().
QString storedChoice();
void setStoredChoice(const QString &code);

// Code actually used for a stored choice: the system UI languages are tried in order, then "it".
QString resolve(const QString &choice);

// Fills \a app and \a qt with the translators for \a code (nothing to load for the source language).
// Returns true when a translation for \a code was loaded. \a qtTranslationsDir may be empty.
bool load(const QString &code, QTranslator &app, QTranslator &qt, const QString &qtTranslationsDir);

} // namespace Language
