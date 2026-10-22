#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace Importers {

struct Result {
    bool ok = false;
    QString text;
    QString error;
    // Formatting (JSON, see RichText.h) of `text`; empty when the file had none worth keeping.
    QString richContent;
};

QStringList supportedSuffixes();
QString fileDialogFilter();

// Extracts plain text from a file, choosing the parser by suffix.
Result importFile(const QString &path);

// Decodes bytes as UTF-8/UTF-16 (BOM) or UTF-8, falling back to Windows-1252.
QString decodeText(const QByteArray &data);

Result importDocx(const QString &path);
Result importOdt(const QString &path);
Result importPdf(const QString &path);
Result importHtml(const QString &path);
Result importRtf(const QString &path);

} // namespace Importers
