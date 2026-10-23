#include "core/Media.h"
#include "core/Transcripts.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace Media {

QStringList audioSuffixes()
{
    return {QStringLiteral("mp3"), QStringLiteral("wav"), QStringLiteral("m4a"), QStringLiteral("aac"),
            QStringLiteral("ogg"), QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("flac"),
            QStringLiteral("wma")};
}

QStringList videoSuffixes()
{
    return {QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov"), QStringLiteral("mkv"),
            QStringLiteral("avi"), QStringLiteral("webm"), QStringLiteral("wmv")};
}

DocumentKind kindForFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (audioSuffixes().contains(suffix))
        return DocumentKind::Audio;
    if (videoSuffixes().contains(suffix))
        return DocumentKind::Video;
    return DocumentKind::Text;
}

bool isMediaFile(const QString &path)
{
    return kindForFile(path) != DocumentKind::Text;
}

QString fileDialogFilter()
{
    QStringList patterns;
    for (const QString &s : audioSuffixes() + videoSuffixes())
        patterns << QStringLiteral("*.") + s;
    return QCoreApplication::translate("Media", "Audio e video (%1);;Tutti i file (*)")
        .arg(patterns.join(QLatin1Char(' ')));
}

QString storedPath(const QString &projectFile, const QString &mediaFile)
{
    if (mediaFile.isEmpty() || projectFile.isEmpty())
        return mediaFile;
    const QDir base = QFileInfo(projectFile).absoluteDir();
    const QString absolute = QFileInfo(mediaFile).absoluteFilePath();
    const QString relative = base.relativeFilePath(absolute);
    // Inside the project folder only: "../x" or another drive would be fragile, an absolute path is clearer.
    if (relative.startsWith(QLatin1String("..")) || QDir::isAbsolutePath(relative))
        return absolute;
    return relative;
}

QString resolvePath(const QString &projectFile, const QString &stored)
{
    if (stored.isEmpty())
        return {};
    const QFileInfo direct(stored);
    if (direct.isAbsolute()) {
        if (direct.isFile())
            return direct.absoluteFilePath();
    } else if (!projectFile.isEmpty()) {
        const QFileInfo rel(QFileInfo(projectFile).absoluteDir().filePath(stored));
        if (rel.isFile())
            return rel.absoluteFilePath();
    }
    // Moved recording: same file name next to the project.
    if (!projectFile.isEmpty()) {
        const QFileInfo near(QFileInfo(projectFile).absoluteDir().filePath(direct.fileName()));
        if (near.isFile())
            return near.absoluteFilePath();
    }
    return {};
}

QString formatRange(qint64 startMs, qint64 endMs)
{
    const QString start = Transcripts::formatTime(startMs);
    return start + QChar(0x2013) + (endMs >= 0 ? Transcripts::formatTime(endMs) : QString());
}

int stampIndexAtTime(const QList<Timestamp> &stamps, qint64 timeMs)
{
    int found = -1;
    for (int i = 0; i < stamps.size(); ++i)
        if (stamps[i].timeMs <= timeMs)
            found = i;
    return found;
}

int stampIndexAtPosition(const QList<Timestamp> &stamps, int pos)
{
    int found = -1;
    for (int i = 0; i < stamps.size(); ++i)
        if (stamps[i].textPos <= pos)
            found = i;
    return found;
}

std::pair<int, int> textSpanOfStamp(const QList<Timestamp> &stamps, int index, int textLength)
{
    if (index < 0 || index >= stamps.size())
        return {0, 0};
    const int start = stamps[index].textPos;
    int end = textLength;
    for (int i = index + 1; i < stamps.size(); ++i)
        if (stamps[i].textPos > start) {
            end = stamps[i].textPos;
            break;
        }
    return {start, qMax(start, end)};
}

std::pair<qint64, qint64> timeRangeOfText(const QList<Timestamp> &stamps, int start, int end)
{
    if (stamps.isEmpty())
        return {-1, -1};
    const int first = stampIndexAtPosition(stamps, start);
    const qint64 from = first < 0 ? 0 : stamps[first].timeMs;
    qint64 to = -1;
    for (const Timestamp &ts : stamps)
        if (ts.textPos >= qMax(end, start + 1) && ts.timeMs > from) {
            to = ts.timeMs;
            break;
        }
    return {from, to};
}

} // namespace Media
