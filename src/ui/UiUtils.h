#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QWidget;
class Project;

namespace Ui {

inline constexpr const char *CodeMimeType = "application/x-tessera-code-id";

QIcon colorIcon(const QColor &color, int size = 14);
QIcon memoIcon();

bool confirm(QWidget *parent, const QString &title, const QString &text);
void showError(QWidget *parent, const QString &text);

// Asks for a non-empty line of text; returns a null QString on cancel.
QString askText(QWidget *parent, const QString &title, const QString &label, const QString &value = {});

// File dialog helpers that remember the last directory.
QString saveFileName(QWidget *parent, const QString &title, const QString &suggestedName, const QString &filter);
QString openFileName(QWidget *parent, const QString &title, const QString &filter);
QStringList openFileNames(QWidget *parent, const QString &title, const QString &filter);

// Opens the memo editor for an object. Returns true if the memo was saved or deleted.
bool editMemo(QWidget *parent, Project *project, int targetType, qint64 targetId, const QString &targetLabel);

} // namespace Ui
