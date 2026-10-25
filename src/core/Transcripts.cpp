#include "core/Transcripts.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace Transcripts {

namespace {

const QRegularExpression &cueTimingRe()
{
    static const QRegularExpression re(
        QStringLiteral("^\\s*((?:\\d+:)?\\d{1,2}:\\d{2}(?:[.,]\\d{1,3})?)\\s*-->\\s*((?:\\d+:)?\\d{1,2}:\\d{2}(?:[.,]\\d{1,3})?)"));
    return re;
}

QString stripTags(QString line)
{
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    line.remove(tags);
    return line;
}

// Shared by SRT and VTT: blocks separated by blank lines, the timing line identifies a cue.
Parsed parseCues(const QString &content, bool vtt)
{
    Parsed out;
    QString normalized = content;
    if (normalized.startsWith(QChar(0xFEFF)))
        normalized.remove(0, 1);
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = normalized.split(QLatin1Char('\n'));

    QStringList paragraphs;
    int i = 0;
    while (i < lines.size()) {
        const QRegularExpressionMatch m = cueTimingRe().match(lines.at(i));
        if (!m.hasMatch()) {
            ++i;
            continue;
        }
        const qint64 start = parseTime(m.captured(1));
        ++i;
        QStringList cue;
        while (i < lines.size() && !lines.at(i).trimmed().isEmpty()) {
            const QString clean = stripTags(lines.at(i)).trimmed();
            if (!clean.isEmpty())
                cue << clean;
            ++i;
        }
        if (start < 0 || cue.isEmpty())
            continue;
        Timestamp ts;
        ts.timeMs = start;
        // The position is filled below, once the paragraph is appended.
        out.stamps.append(ts);
        paragraphs << cue.join(QLatin1Char(' '));
    }
    Q_UNUSED(vtt)
    int pos = 0;
    for (int k = 0; k < paragraphs.size(); ++k) {
        out.stamps[k].textPos = pos;
        out.text += paragraphs.at(k);
        pos += int(paragraphs.at(k).size());
        if (k + 1 < paragraphs.size()) {
            out.text += QLatin1Char('\n');
            ++pos;
        }
    }
    out.ok = !out.stamps.isEmpty();
    return out;
}

QString two(qint64 v) { return QStringLiteral("%1").arg(v, 2, 10, QLatin1Char('0')); }

QString cueTime(qint64 ms, QChar millisSeparator)
{
    const qint64 h = ms / 3600000, m = (ms / 60000) % 60, s = (ms / 1000) % 60, f = ms % 1000;
    return two(h) + QLatin1Char(':') + two(m) + QLatin1Char(':') + two(s) + millisSeparator +
           QStringLiteral("%1").arg(f, 3, 10, QLatin1Char('0'));
}

} // namespace

QString formatTime(qint64 ms, bool withMillis)
{
    if (ms < 0)
        ms = 0;
    const qint64 h = ms / 3600000, m = (ms / 60000) % 60, s = (ms / 1000) % 60;
    QString out = two(h) + QLatin1Char(':') + two(m) + QLatin1Char(':') + two(s);
    if (withMillis)
        out += QStringLiteral(".%1").arg(ms % 1000, 3, 10, QLatin1Char('0'));
    return out;
}

qint64 parseTime(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^(?:(?:(\\d+):)?(\\d{1,2}):)?(\\d+)(?:[.,](\\d{1,3}))?$"));
    const QRegularExpressionMatch m = re.match(text.trimmed());
    if (!m.hasMatch())
        return -1;
    const bool hasMinutes = !m.captured(2).isEmpty();
    const qint64 h = m.captured(1).toLongLong();
    const qint64 min = m.captured(2).toLongLong();
    const qint64 s = m.captured(3).toLongLong();
    if (hasMinutes && s > 59)
        return -1;
    QString frac = m.captured(4);
    while (!frac.isEmpty() && frac.size() < 3)
        frac += QLatin1Char('0');
    return ((h * 60 + min) * 60 + s) * 1000 + frac.toLongLong();
}

Parsed parseSrt(const QString &content) { return parseCues(content, false); }
Parsed parseVtt(const QString &content) { return parseCues(content, true); }

Parsed parseBracketed(const QString &content)
{
    static const QRegularExpression marker(QStringLiteral("\\[((?:\\d+:)?\\d{1,2}:\\d{2}(?:[.,]\\d{1,3})?)\\] ?"));
    Parsed out;
    QString text = content;
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));

    int last = 0;
    QRegularExpressionMatchIterator it = marker.globalMatch(text);
    QList<Timestamp> stamps;
    QString result;
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const qint64 t = parseTime(m.captured(1));
        result += text.mid(last, int(m.capturedStart()) - last);
        last = int(m.capturedEnd());
        if (t < 0) {
            result += m.captured(0);
            continue;
        }
        Timestamp ts;
        ts.textPos = int(result.size());
        ts.timeMs = t;
        stamps << ts;
    }
    result += text.mid(last);
    if (stamps.size() < 2)
        return out;
    out.ok = true;
    out.text = result;
    out.stamps = stamps;
    return out;
}

Parsed parseByName(const QString &fileName, const QString &content)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix == QLatin1String("srt"))
        return parseSrt(content);
    if (suffix == QLatin1String("vtt"))
        return parseVtt(content);
    if (suffix == QLatin1String("txt"))
        return parseBracketed(content);
    return {};
}

QString exportTranscript(const QString &text, const QList<Timestamp> &stampsIn, Format format)
{
    QList<Timestamp> stamps = stampsIn;
    std::stable_sort(stamps.begin(), stamps.end(), [](const Timestamp &a, const Timestamp &b) {
        return a.textPos != b.textPos ? a.textPos < b.textPos : a.timeMs < b.timeMs;
    });

    if (format == Format::Text) {
        QString out;
        int last = 0;
        for (const Timestamp &ts : std::as_const(stamps)) {
            const int pos = qBound(last, ts.textPos, int(text.size()));
            out += text.mid(last, pos - last);
            out += QLatin1Char('[') + formatTime(ts.timeMs) + QStringLiteral("] ");
            last = pos;
        }
        return out + text.mid(last);
    }

    struct Cue {
        qint64 start;
        QString text;
    };
    QList<Cue> cues;
    const int firstPos = stamps.isEmpty() ? int(text.size()) : qBound(0, stamps.first().textPos, int(text.size()));
    if (!text.left(firstPos).trimmed().isEmpty())
        cues.append({0, text.left(firstPos).trimmed()});
    for (qsizetype k = 0; k < stamps.size(); ++k) {
        const int from = qBound(0, stamps.at(k).textPos, int(text.size()));
        const int to = k + 1 < stamps.size() ? qBound(from, stamps.at(k + 1).textPos, int(text.size())) : int(text.size());
        const QString body = text.mid(from, to - from).trimmed();
        if (!body.isEmpty())
            cues.append({stamps.at(k).timeMs, body});
    }

    const bool vtt = format == Format::Vtt;
    const QChar sep = vtt ? QLatin1Char('.') : QLatin1Char(',');
    QString out = vtt ? QStringLiteral("WEBVTT\n\n") : QString();
    for (qsizetype k = 0; k < cues.size(); ++k) {
        qint64 end = k + 1 < cues.size() ? cues.at(k + 1).start : cues.at(k).start + 3000;
        if (end <= cues.at(k).start)
            end = cues.at(k).start + 1000;
        if (!vtt)
            out += QString::number(k + 1) + QLatin1Char('\n');
        out += cueTime(cues.at(k).start, sep) + QStringLiteral(" --> ") + cueTime(end, sep) + QLatin1Char('\n');
        // A blank line would end the cue: collapse any in the text.
        QString body = cues.at(k).text;
        body.replace(QRegularExpression(QStringLiteral("\\n\\s*\\n+")), QStringLiteral("\n"));
        out += body + QStringLiteral("\n\n");
    }
    return out;
}

} // namespace Transcripts
