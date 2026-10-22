#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

class QWidget;
class Project;
struct Memo;
namespace Xlsx {
struct Sheet;
}

namespace Ui {

inline constexpr const char *CodeMimeType = "application/x-tessera-code-id";
inline constexpr const char *DocumentMimeType = "application/x-tessera-document-id";

// Names, document text and other values may come from imported files. Qt renders tooltips,
// labels and message boxes as rich text when the text looks like HTML, and rich text loads the
// files named by <img src>: from \\host\share that sends the user's credentials to the host. So
// such text is shown only through these helpers (or a QLabel set to Qt::PlainText).
QString plainToolTip(const QString &text); // rich text that shows `text` literally
void showInfo(QWidget *parent, const QString &title, const QString &text);

QIcon colorIcon(const QColor &color, int size = 14);
QIcon memoIcon();
QIcon imageIcon(); // image documents in the document tree
QIcon pdfIcon();   // PDF documents in the document tree

bool confirm(QWidget *parent, const QString &title, const QString &text);
void showError(QWidget *parent, const QString &text);

// Asks for a non-empty line of text; returns a null QString on cancel.
QString askText(QWidget *parent, const QString &title, const QString &label, const QString &value = {});

// File dialog helpers that remember the last directory.
QString saveFileName(QWidget *parent, const QString &title, const QString &suggestedName, const QString &filter);
QString openFileName(QWidget *parent, const QString &title, const QString &filter);
QStringList openFileNames(QWidget *parent, const QString &title, const QString &filter);

// Opens the (non-modal) memo editor for an object: its memo, or a new one. The window stays open
// while the user works elsewhere, so codes and documents can be dragged onto it.
void editMemo(QWidget *parent, Project *project, int targetType, qint64 targetId);
// Opens the editor for a memo; an editor already open for the same memo is raised instead.
void openMemo(QWidget *parent, Project *project, const Memo &memo);
// "Codice: Tema > Sotto", "Documento: Intervista"… for a memo target (MemoTarget as int).
QString memoTargetLabel(const Project *project, int targetType, qint64 targetId);

// Writes an .xlsx workbook; shows the error, or a notice when cells were cut to Excel's limit.
bool saveXlsx(QWidget *parent, const QString &path, const QList<Xlsx::Sheet> &sheets);
bool isXlsxPath(const QString &path);

} // namespace Ui
