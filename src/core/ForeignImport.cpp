#include "core/ForeignImport.h"
#include "core/Project.h"
#include "core/Qdpx.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>
#include <QtCore/private/qzipwriter_p.h>

namespace ForeignImport {

namespace {

QString tr(const char *s) { return QCoreApplication::translate("ForeignImport", s); }

QByteArray head(const QString &path, int n)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.read(n);
}

// Reads sys_Setup of a MAXQDA file through a separate read-only, immutable connection (the file is
// never written and MAXQDA may keep it open). Returns false if the file has no such table.
bool readMaxqdaSetup(const QString &path, QString *version)
{
    const QString name = QStringLiteral("tessera_foreign_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool found = false;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_URI"));
        db.setDatabaseName(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()).toString(QUrl::FullyEncoded)
                           + QStringLiteral("?mode=ro&immutable=1"));
        if (db.open()) {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT * FROM sys_Setup"))) {
                found = true;
                // Key/value table: keep the application string if there is one.
                const int cols = q.record().count();
                while (q.next()) {
                    if (cols >= 2 && q.value(0).toString().compare(QLatin1String("APP"), Qt::CaseInsensitive) == 0)
                        *version = q.value(1).toString();
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return found;
}

} // namespace

Info detect(const QString &path)
{
    Info info;
    const QString ext = QFileInfo(path).suffix().toLower();
    const QByteArray magic = head(path, 64);
    if (magic.isEmpty()) {
        info.description = tr("File non leggibile");
        return info;
    }
    if (magic.startsWith("PK\x03\x04")) {
        info.kind = Kind::Qdpx;
        info.description = tr("Progetto REFI-QDA (QDPX)");
        return info;
    }
    if (magic.startsWith("SQLite format 3")) {
        QString version;
        if (readMaxqdaSetup(path, &version)) {
            info.kind = Kind::MaxqdaNative;
            info.description = version.isEmpty() ? tr("Progetto MAXQDA")
                                                 : tr("Progetto MAXQDA (%1)").arg(version);
        } else if (ext == QLatin1String("nvpx")) {
            info.kind = Kind::NvivoNative;
            info.description = tr("Progetto NVivo");
        } else {
            info.description = tr("Database SQLite sconosciuto");
        }
        return info;
    }
    if (ext == QLatin1String("nvp") || ext == QLatin1String("nvpx")) {
        info.kind = Kind::NvivoNative;
        info.description = tr("Progetto NVivo");
        return info;
    }
    if (ext == QLatin1String("mqda") || ext.startsWith(QLatin1String("mx"))) {
        info.kind = Kind::MaxqdaNative;
        info.description = tr("Progetto MAXQDA");
        return info;
    }
    // XML: a REFI-QDA codebook has <CodeBook> as root element.
    if (QString::fromUtf8(head(path, 4096)).contains(QStringLiteral("<CodeBook"))) {
        info.kind = Kind::Qdc;
        info.description = tr("Libro dei codici REFI-QDA (QDC)");
    }
    return info;
}

QString fileDialogFilter()
{
    return tr("Progetti e libri dei codici REFI-QDA (*.qdpx *.qdc);;"
              "File nativi MAXQDA e NVivo (*.mqda *.mx24 *.mx22 *.mx20 *.nvp *.nvpx);;Tutti i file (*)");
}

bool importFile(Project &project, const QString &path, QString *report, QString *error)
{
    const Info info = detect(path);
    switch (info.kind) {
    case Kind::Qdpx:
        return Qdpx::importProject(project, path, report, error);
    case Kind::Qdc: {
        // A codebook is the <CodeBook> part of a project: wrap it into a minimal .qdpx and reuse the importer.
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            *error = tr("File non leggibile");
            return false;
        }
        QString xml = QString::fromUtf8(f.readAll());
        xml.remove(QRegularExpression(QStringLiteral("^\\s*<\\?xml[^>]*\\?>")));
        QTemporaryDir dir;
        if (!dir.isValid()) {
            *error = tr("Impossibile creare una cartella temporanea");
            return false;
        }
        const QString tmp = dir.filePath(QStringLiteral("codebook.qdpx"));
        {
            QFile out(tmp);
            if (!out.open(QIODevice::WriteOnly)) {
                *error = tr("Impossibile creare un file temporaneo");
                return false;
            }
            QZipWriter zip(&out);
            const QByteArray body = (QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?><Project name=\"")
                                     + QFileInfo(path).completeBaseName().toHtmlEscaped()
                                     + QStringLiteral("\">") + xml + QStringLiteral("</Project>")).toUtf8();
            zip.addFile(QStringLiteral("project.qde"), body);
            zip.close();
        }
        return Qdpx::importProject(project, tmp, report, error);
    }
    case Kind::MaxqdaNative:
        *error = tr("%1\n\nTessera non legge direttamente i file nativi di MAXQDA: il formato interno non è documentato "
                    "e non può essere verificato senza copiare il software.\n\n"
                    "In MAXQDA scegli Progetto > Esporta > Progetto REFI-QDA (.qdpx), poi importa quel file qui.")
                     .arg(info.description);
        return false;
    case Kind::NvivoNative:
        *error = tr("%1\n\nTessera non legge direttamente i file nativi di NVivo: il formato è proprietario e non documentato.\n\n"
                    "In NVivo scegli File > Esporta > Esporta progetto (formato REFI-QDA, .qdpx) oppure Esporta libro dei codici "
                    "(.qdc), poi importa quel file qui.")
                     .arg(info.description);
        return false;
    case Kind::Unknown:
        break;
    }
    *error = tr("Formato non riconosciuto. Sono supportati i progetti REFI-QDA (.qdpx) e i libri dei codici (.qdc).");
    return false;
}

} // namespace ForeignImport
