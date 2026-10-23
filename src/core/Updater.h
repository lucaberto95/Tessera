#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// Automatic updates: looks for a newer GitHub release, downloads its win64 zip, checks it and hands the swap of
// the files to a small script that runs after the application has quit. Nothing happens without the user's
// settings: the check at startup can be turned off, and a download only starts after the user accepts it.
namespace Update {

struct Release {
    QString version;     // "0.29.0"
    QString name;
    QString notes;       // plain text
    QString pageUrl;     // release page, for a manual download
    QUrl assetUrl;       // Tessera-x.y.z-win64.zip
    QString assetName;
    qint64 size = 0;
    QString sha256;      // lower-case hex, empty when GitHub gives no digest
    bool hasAsset() const { return assetUrl.isValid(); }
};

// Numeric comparison of dotted versions ("0.9.1" < "0.28.0"); a leading "v" is ignored. <0, 0, >0.
int compareVersions(const QString &a, const QString &b);

// Reads the answer of GET /repos/<owner>/<repo>/releases/latest. The asset must be the win64 zip and must be
// served from github.com or from `trustedHost` (the host of the API, which differs only in tests).
bool parseRelease(const QByteArray &json, const QString &trustedHost, Release *out, QString *error = nullptr);

// Unpacks the zip into `outDir` (entries that would leave it are refused) and returns the folder that holds
// Tessera.exe, empty with `error` filled in when the archive is not a Tessera distribution.
QString extractDistribution(const QString &zipPath, const QString &outDir, QString *error = nullptr);

// True when files can be created in the folder of the running application (an update cannot replace them otherwise).
bool canWriteTo(const QString &dir);

// Writes the script that waits for `pid` to exit, copies `sourceDir` over `appDir` and restarts Tessera, then runs it
// detached. The caller quits right after. Returns false (error filled in) if the script could not be started.
bool launchReplacement(const QString &sourceDir, const QString &appDir, qint64 pid, const QString &workDir,
                       QString *error = nullptr);

class Checker : public QObject {
    Q_OBJECT
public:
    static QString defaultApiUrl();
    explicit Checker(const QString &apiUrl = defaultApiUrl(), QObject *parent = nullptr);

    void check();                                       // -> checked()
    void download(const Release &release, const QString &zipPath); // -> downloadProgress(), downloaded()
    void cancel();

signals:
    // `release` is valid only when ok; `newer` tells whether it is more recent than `currentVersion`.
    void checked(bool ok, const Update::Release &release, bool newer, const QString &error);
    void downloadProgress(qint64 received, qint64 total);
    void downloaded(bool ok, const QString &zipPath, const QString &error);

public:
    void setCurrentVersion(const QString &v) { m_current = v; } // tests

private:
    QNetworkReply *get(const QUrl &url, const QByteArray &accept);

    QString m_apiUrl;
    QString m_current;
    QNetworkAccessManager *m_nam;
    QPointer<QNetworkReply> m_reply;
};

} // namespace Update

Q_DECLARE_METATYPE(Update::Release)
