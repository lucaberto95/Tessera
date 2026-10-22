#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QWidget;
class Project;

namespace Ui {

inline constexpr const char *CodeMimeType = "application/x-tessera-code-id";

// Names, document text and other values may come from imported files. Qt renders tooltips,
// labels and message boxes as rich text when the text looks like HTML, and rich text loads the
// files named by <img src>: from \\host\share that sends the user's credentials to the host. So
// such text is shown only through these helpers (or a QLabel set to Qt::PlainText).
QString plainToolTip(const QString &text); // rich text that shows `text` literally
void showInfo(QWidget *parent, const QString &title, const QString &text);

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
