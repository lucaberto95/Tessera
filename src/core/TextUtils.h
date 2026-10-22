#pragma once

#include "core/Model.h"

#include <QColor>
#include <QList>
#include <QString>

namespace TextUtils {

// Converts CRLF / CR line endings to LF and strips a leading BOM.
QString normalizeText(QString text);

// QDPX positions count Unicode code points; Tessera stores UTF-16 indexes.
int utf16ToCodePoint(const QString &text, int utf16Index);
int codePointToUtf16(const QString &text, int codePointIndex);

// Updates segment boundaries after `removed` characters at `position` were replaced by `added`
// characters. Segments that end up empty are removed from the list.
void adjustSegmentsForEdit(QList<Segment> &segments, int position, int removed, int added);

// Same rule for one range [start, end): returns false when its text was entirely removed.
bool adjustRangeForEdit(int &start, int &end, int position, int removed, int added);

// Same rule for the ends of links that lie in `documentId` (a link can have both ends there).
// Links with an end whose text was entirely removed are removed from the list.
void adjustLinksForEdit(QList<DocumentLink> &links, qint64 documentId, int position, int removed, int added);

// Single-line excerpt of [start, end), shortened in the middle when longer than maxLength.
QString excerpt(const QString &text, int start, int end, int maxLength = 300);

// Keyword-in-context string: `width` characters around the match on each side.
QString keywordInContext(const QString &text, int start, int end, int width = 50);

// Boundaries of the sentence / paragraph that contains [start, end).
std::pair<int, int> sentenceBounds(const QString &text, int start, int end);
std::pair<int, int> paragraphBounds(const QString &text, int start, int end);

QColor paletteColor(int index);

} // namespace TextUtils
