#include "core/TextUtils.h"

namespace TextUtils {

QString normalizeText(QString text)
{
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    text.replace(QChar::LineSeparator, QLatin1Char('\n'));
    return text;
}

int utf16ToCodePoint(const QString &text, int utf16Index)
{
    utf16Index = qBound(0, utf16Index, int(text.size()));
    int cp = 0;
    for (int i = 0; i < utf16Index; ++i) {
        if (text.at(i).isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate())
            ++i;
        ++cp;
    }
    return cp;
}

int codePointToUtf16(const QString &text, int codePointIndex)
{
    int i = 0;
    for (int cp = 0; cp < codePointIndex && i < text.size(); ++cp) {
        if (text.at(i).isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate())
            i += 2;
        else
            ++i;
    }
    return i;
}

void adjustSegmentsForEdit(QList<Segment> &segments, int position, int removed, int added)
{
    const int removedEnd = position + removed;
    const int delta = added - removed;
    for (Segment &s : segments) {
        auto mapPos = [&](int p, bool isEnd) {
            if (p < position)
                return p;
            if (p >= removedEnd) {
                // Text typed exactly at a segment's end stays outside it; at its start it shifts it.
                return p + delta;
            }
            // Inside the replaced range: collapse to the edges of the inserted text.
            return isEnd ? position + added : position;
        };
        if (removed == 0 && s.end == position) {
            continue; // insertion right after the segment: unchanged
        }
        const int newStart = mapPos(s.start, false);
        const int newEnd = mapPos(s.end, true);
        s.start = newStart;
        s.end = newEnd;
    }
    segments.removeIf([](const Segment &s) { return s.end <= s.start; });
}

QString excerpt(const QString &text, int start, int end, int maxLength)
{
    start = qBound(0, start, int(text.size()));
    end = qBound(start, end, int(text.size()));
    QString s = text.mid(start, end - start).simplified();
    if (s.size() > maxLength && maxLength > 10) {
        const int half = maxLength / 2 - 2;
        s = s.left(half) + QStringLiteral(" … ") + s.right(half);
    }
    return s;
}

QString keywordInContext(const QString &text, int start, int end, int width)
{
    const int from = qMax(0, start - width);
    const int to = qMin(int(text.size()), end + width);
    QString before = text.mid(from, start - from).simplified();
    QString hit = text.mid(start, end - start).simplified();
    QString after = text.mid(end, to - end).simplified();
    return (from > 0 ? QStringLiteral("…") : QString()) + before + QStringLiteral(" [") + hit +
           QStringLiteral("] ") + after + (to < text.size() ? QStringLiteral("…") : QString());
}

std::pair<int, int> paragraphBounds(const QString &text, int start, int end)
{
    int s = start;
    while (s > 0 && text.at(s - 1) != QLatin1Char('\n'))
        --s;
    int e = qMax(end, start);
    while (e < text.size() && text.at(e) != QLatin1Char('\n'))
        ++e;
    return {s, e};
}

std::pair<int, int> sentenceBounds(const QString &text, int start, int end)
{
    auto isTerminator = [](QChar c) {
        return c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?') || c == QChar(0x2026) ||
               c == QLatin1Char('\n');
    };
    int s = start;
    while (s > 0 && !isTerminator(text.at(s - 1)))
        --s;
    while (s < start && text.at(s).isSpace())
        ++s;
    int e = qMax(end, start);
    while (e < text.size() && !isTerminator(text.at(e)))
        ++e;
    if (e < text.size() && text.at(e) != QLatin1Char('\n'))
        ++e; // include the terminating punctuation
    return {s, e};
}

QColor paletteColor(int index)
{
    static const QList<QColor> palette = {
        QColor(0x1f77b4), QColor(0xd62728), QColor(0x2ca02c), QColor(0xff7f0e), QColor(0x9467bd),
        QColor(0x17becf), QColor(0xe377c2), QColor(0x8c564b), QColor(0xbcbd22), QColor(0x7f7f7f),
        QColor(0x3d5a80), QColor(0xc44536),
    };
    return palette.at(qAbs(index) % palette.size());
}

} // namespace TextUtils
