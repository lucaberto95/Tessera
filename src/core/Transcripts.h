#pragma once

#include "core/Model.h"

#include <QList>
#include <QString>

// Transcripts with time stamps (issue #29): reading and writing the common formats. A transcript is
// the plain text of a document plus its time stamps (position in the text -> time in the recording).
namespace Transcripts {

struct Parsed {
    bool ok = false; // the content had at least one time stamp
    QString text;
    QList<Timestamp> stamps; // documentId is 0
};

enum class Format { Srt, Vtt, Text };

// "h:mm:ss" ("hh:mm:ss.mmm" with millis). Hours are not capped at 24.
QString formatTime(qint64 ms, bool withMillis = false);
// Accepts "ss", "mm:ss", "hh:mm:ss", optionally with ".mmm" or ",mmm"; returns -1 when it is not a time.
qint64 parseTime(const QString &text);

// SubRip: numbered cues "00:00:01,000 --> 00:00:04,000"; each cue becomes a paragraph with a stamp at its start.
Parsed parseSrt(const QString &content);
// WebVTT: same, with the header, NOTE/STYLE blocks, optional cue ids and tags (<v Name>, <c>…) ignored.
Parsed parseVtt(const QString &content);
// Plain text with markers like "[00:01:23]" (also "[1:23]", "[00:01:23.500]") anywhere in the text; the markers
// are removed (with one following space) and become stamps. Needs at least two markers to count as a transcript.
Parsed parseBracketed(const QString &content);
// Chooses the parser by suffix (srt, vtt) or by content ("[hh:mm:ss]" markers for other text).
Parsed parseByName(const QString &fileName, const QString &content);

// Writes the document text with its stamps. Srt/Vtt: one cue per stamp, from its position to the next
// (the last one lasts a few seconds); Text: markers "[hh:mm:ss]" inserted at the stamps.
QString exportTranscript(const QString &text, const QList<Timestamp> &stamps, Format format);

} // namespace Transcripts
