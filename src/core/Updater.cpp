#include "Updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QtCore/private/qzipreader_p.h>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace Update {

namespace {

QList<int> versionParts(const QString &v)
{
    QString s = v.trimmed();
    if (s.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        s.remove(0, 1);
    QList<int> parts;
    for (const QString &p : s.split(QLatin1Char('.')))
        parts << p.toInt(); // "1-beta" -> 0: only plain releases are published
    return parts;
}

bool isTrusted(const QUrl &url, const QString &trustedHost)
{
    const QString host = url.host().toLower();
    if (url.scheme() == QLatin1String("https") && host == QLatin1String("github.com"))
        return true;
    return !trustedHost.isEmpty() && host == trustedHost.toLower();
}

} // namespace

int compareVersions(const QString &a, const QString &b)
{
    const QList<int> pa = versionParts(a), pb = versionParts(b);
    const int n = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; ++i) {
        const int x = pa.value(i, 0), y = pb.value(i, 0);
        if (x != y)
            return x < y ? -1 : 1;
    }
    return 0;
}

bool parseRelease(const QByteArray &json, const QString &trustedHost, Release *out, QString *error)
{
    auto fail = [error](const QString &m) {
        if (error)
            *error = m;
        return false;
    };
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (!doc.isObject())
        return fail(QCoreApplication::translate("Update", "Risposta del server non valida."));
    const QJsonObject o = doc.object();

    Release r;
    QString tag = o.value(QLatin1String("tag_name")).toString().trimmed();
    if (tag.startsWith(QLatin1Char('v')))
        tag.remove(0, 1);
    static const QRegularExpression versionRe(QStringLiteral("^\\d+(\\.\\d+){0,3}$"));
    if (!versionRe.match(tag).hasMatch())
        return fail(QCoreApplication::translate("Update", "Numero di versione non riconosciuto."));
    r.version = tag;
    r.name = o.value(QLatin1String("name")).toString();
    r.notes = o.value(QLatin1String("body")).toString();
    r.pageUrl = o.value(QLatin1String("html_url")).toString();

    static const QRegularExpression assetRe(QStringLiteral("^Tessera-[0-9.]+-win64\\.zip$"));
    for (const QJsonValue &v : o.value(QLatin1String("assets")).toArray()) {
        const QJsonObject a = v.toObject();
        const QString name = a.value(QLatin1String("name")).toString();
        const QUrl url(a.value(QLatin1String("browser_download_url")).toString());
        if (!assetRe.match(name).hasMatch() || !isTrusted(url, trustedHost))
            continue;
        r.assetName = name;
        r.assetUrl = url;
        r.size = static_cast<qint64>(a.value(QLatin1String("size")).toDouble());
        const QString digest = a.value(QLatin1String("digest")).toString();
        if (digest.startsWith(QLatin1String("sha256:"), Qt::CaseInsensitive))
            r.sha256 = digest.mid(7).toLower();
        break;
    }
    *out = r;
    return true;
}

bool canWriteTo(const QString &dir)
{
    QFile probe(QDir(dir).filePath(QStringLiteral(".tessera-write-test")));
    if (!probe.open(QIODevice::WriteOnly))
        return false;
    probe.close();
    probe.remove();
    return true;
}

QString extractDistribution(const QString &zipPath, const QString &outDir, QString *error)
{
    auto fail = [error](const QString &m) {
        if (error)
            *error = m;
        return QString();
    };
    QZipReader zip(zipPath);
    if (!zip.isReadable() || zip.status() != QZipReader::NoError)
        return fail(QCoreApplication::translate("Update", "L'archivio scaricato non è leggibile."));
    QDir().mkpath(outDir);
    const QDir out(outDir);
    const QString root = QDir::cleanPath(out.absolutePath()) + QLatin1Char('/');

    for (const QZipReader::FileInfo &fi : zip.fileInfoList()) {
        const QString rel = QDir::fromNativeSeparators(fi.filePath);
        const QString target = QDir::cleanPath(out.filePath(rel));
        // Zip-slip: nothing may land outside the extraction folder.
        if (rel.contains(QStringLiteral("..")) || QDir::isAbsolutePath(rel) || rel.contains(QLatin1Char(':'))
            || !target.startsWith(root, Qt::CaseInsensitive))
            return fail(QCoreApplication::translate("Update", "L'archivio contiene percorsi non validi."));
        if (fi.isDir) {
            QDir().mkpath(target);
            continue;
        }
        if (!fi.isFile)
            continue;
        QDir().mkpath(QFileInfo(target).absolutePath());
        // Archives made by Windows PowerShell's Compress-Archive store backslashes; the reader reports
        // slashes but looks entries up by the stored name, so try both spellings.
        QByteArray data = zip.fileData(fi.filePath);
        if (data.isEmpty() && fi.size > 0)
            data = zip.fileData(QString(fi.filePath).replace(QLatin1Char('/'), QLatin1Char('\\')));
        QFile f(target);
        if (data.size() != fi.size || !f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
            return fail(QCoreApplication::translate("Update", "Impossibile estrarre l'aggiornamento."));
    }
    for (const QString &candidate : {out.filePath(QStringLiteral("Tessera")), out.absolutePath()}) {
        if (QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("Tessera.exe"))))
            return QDir::toNativeSeparators(candidate);
    }
    return fail(QCoreApplication::translate("Update", "L'archivio non contiene Tessera."));
}

bool launchReplacement(const QString &sourceDir, const QString &appDir, qint64 pid, const QString &workDir,
                       QString *error)
{
    auto fail = [error](const QString &m) {
        if (error)
            *error = m;
        return false;
    };
    const QString src = QDir::toNativeSeparators(sourceDir), app = QDir::toNativeSeparators(appDir);
    // cmd.exe expands these inside quotes too; real installation paths never contain them.
    for (const QString &p : {src, app}) {
        if (p.contains(QLatin1Char('%')) || p.contains(QLatin1Char('!')) || p.contains(QLatin1Char('"')))
            return fail(QCoreApplication::translate("Update", "Il percorso dell'applicazione non è supportato."));
    }
    QDir().mkpath(workDir);
    const QString scriptPath = QDir(workDir).filePath(QStringLiteral("update.cmd"));
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(QCoreApplication::translate("Update", "Impossibile preparare l'aggiornamento."));
    const QString text = QStringLiteral(
        "@echo off\r\n"
        "set /a tries=0\r\n"
        ":wait\r\n"
        "tasklist /FI \"PID eq %1\" 2>nul | find \"%1\" >nul\r\n"
        "if errorlevel 1 goto copy\r\n"
        "set /a tries+=1\r\n"
        "if %tries% GEQ 60 exit /b 1\r\n"
        "ping -n 2 127.0.0.1 >nul\r\n"
        "goto wait\r\n"
        ":copy\r\n"
        "set /a tries=0\r\n"
        ":again\r\n"
        "xcopy \"%2\\*\" \"%3\\\" /E /Y /I /Q >nul\r\n"
        "if not errorlevel 1 goto done\r\n"
        "set /a tries+=1\r\n"
        "if %tries% GEQ 10 goto done\r\n"
        "ping -n 2 127.0.0.1 >nul\r\n"
        "goto again\r\n"
        ":done\r\n"
        "start \"\" \"%3\\Tessera.exe\"\r\n")
        .arg(QString::number(pid), src, app);
    script.write(text.toLocal8Bit());
    script.close();

    QProcess p;
    p.setProgram(QStringLiteral("cmd.exe"));
    p.setArguments({QStringLiteral("/c"), QDir::toNativeSeparators(scriptPath)});
    p.setWorkingDirectory(workDir);
#ifdef Q_OS_WIN
    p.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
#endif
    if (!p.startDetached())
        return fail(QCoreApplication::translate("Update", "Impossibile avviare l'aggiornamento."));
    return true;
}

// ---- Checker ----------------------------------------------------------------------------------------------

QString Checker::defaultApiUrl()
{
    return QStringLiteral("https://api.github.com/repos/lucaberto95/Tessera/releases/latest");
}

Checker::Checker(const QString &apiUrl, QObject *parent)
    : QObject(parent), m_apiUrl(apiUrl), m_current(QStringLiteral(TESSERA_VERSION_STRING)),
      m_nam(new QNetworkAccessManager(this))
{
    qRegisterMetaType<Update::Release>();
}

QNetworkReply *Checker::get(const QUrl &url, const QByteArray &accept)
{
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "Tessera/" + m_current.toLatin1());
    req.setRawHeader("Accept", accept);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    return m_nam->get(req);
}

void Checker::check()
{
    cancel();
    QNetworkReply *reply = get(QUrl(m_apiUrl), "application/vnd.github+json");
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::OperationCanceledError && reply->property("cancelled").toBool())
            return;
        if (reply->error() != QNetworkReply::NoError) {
            emit checked(false, {}, false,
                         tr("Impossibile controllare gli aggiornamenti: %1").arg(reply->errorString()));
            return;
        }
        Release r;
        QString err;
        if (!parseRelease(reply->readAll(), QUrl(m_apiUrl).host(), &r, &err)) {
            emit checked(false, {}, false, err);
            return;
        }
        emit checked(true, r, compareVersions(r.version, m_current) > 0, {});
    });
}

void Checker::download(const Release &release, const QString &zipPath)
{
    cancel();
    auto *file = new QFile(zipPath + QStringLiteral(".part"));
    if (!release.hasAsset() || !file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        delete file;
        emit downloaded(false, {}, tr("Impossibile scrivere il file scaricato."));
        return;
    }
    QNetworkReply *reply = get(release.assetUrl, "application/octet-stream");
    m_reply = reply;
    file->setParent(reply);
    auto *hash = new QCryptographicHash(QCryptographicHash::Sha256);
    connect(reply, &QObject::destroyed, [hash] { delete hash; });
    connect(reply, &QNetworkReply::readyRead, reply, [reply, file, hash] {
        const QByteArray chunk = reply->readAll();
        hash->addData(chunk);
        file->write(chunk);
    });
    connect(reply, &QNetworkReply::downloadProgress, this, &Checker::downloadProgress);
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, hash, release, zipPath] {
        reply->deleteLater();
        if (reply->property("cancelled").toBool())
            return;
        const QByteArray rest = reply->readAll();
        hash->addData(rest);
        file->write(rest);
        file->close();
        auto fail = [&](const QString &m) {
            QFile::remove(file->fileName());
            emit downloaded(false, {}, m);
        };
        if (reply->error() != QNetworkReply::NoError)
            return fail(tr("Download non riuscito: %1").arg(reply->errorString()));
        if (release.size > 0 && QFileInfo(file->fileName()).size() != release.size)
            return fail(tr("Il file scaricato è incompleto."));
        if (!release.sha256.isEmpty() && hash->result().toHex() != release.sha256.toLatin1())
            return fail(tr("Il file scaricato è danneggiato (controllo SHA-256 non superato)."));
        QFile::remove(zipPath);
        if (!QFile::rename(file->fileName(), zipPath))
            return fail(tr("Impossibile salvare il file scaricato."));
        emit downloaded(true, zipPath, {});
    });
}

void Checker::cancel()
{
    if (m_reply) {
        m_reply->setProperty("cancelled", true);
        m_reply->abort();
    }
}

} // namespace Update
