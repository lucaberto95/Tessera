#pragma once

#include "core/Model.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <utility>

// Audio and video recordings (issues #28, #29). Recordings are never embedded in the project (they can be
// huge): the document keeps a path, relative to the folder of the .tsra file when the recording lives under it
// (so a moved project folder keeps working), absolute otherwise. This header holds the logic that does not need
// a player: file kinds, path handling and the mapping between time stamps, text positions and times.
namespace Media {

QStringList audioSuffixes(); // mp3, wav, m4a, aac, ogg, oga, opus, flac, wma
QStringList videoSuffixes(); // mp4, m4v, mov, mkv, avi, webm, wmv
bool isMediaFile(const QString &path);
// DocumentKind::Audio / Video by suffix; DocumentKind::Text when the file is not a recording.
DocumentKind kindForFile(const QString &path);
QString fileDialogFilter(); // "Audio e video (*.mp3 ...);;Tutti i file (*)"

// Path to keep in the project for `mediaFile`: relative to the folder of `projectFile` when it is inside it.
QString storedPath(const QString &projectFile, const QString &mediaFile);
// Existing file for a stored path: the absolute path, or the relative one next to the project, or (a moved
// recording) the same file name in the project folder. Empty when nothing is found.
QString resolvePath(const QString &projectFile, const QString &stored);

// "00:12:30" + en dash + "00:13:05"; an open end is shown as "00:12:30" + en dash.
QString formatRange(qint64 startMs, qint64 endMs);

// ---- time stamps <-> text. `stamps` are ordered by text position, as Project::timestamps returns them.

// Index of the stamp that governs `timeMs` (the last one, in text order, whose time is <= timeMs); -1 if the
// time is before the first stamp or there are none.
int stampIndexAtTime(const QList<Timestamp> &stamps, qint64 timeMs);
// Index of the stamp that governs the text position (the last one with textPos <= pos); -1 if there is none.
int stampIndexAtPosition(const QList<Timestamp> &stamps, int pos);
// Text covered by stamp `index`: from its position to the next stamp's (or to `textLength`).
std::pair<int, int> textSpanOfStamp(const QList<Timestamp> &stamps, int index, int textLength);
// Time range of a text passage: from the stamp that governs `start` (0 when the passage precedes every stamp)
// to the first stamp at or after `end`; endMs is -1 when no stamp follows (play to the end of the recording).
// {-1, -1} when there are no stamps at all.
std::pair<qint64, qint64> timeRangeOfText(const QList<Timestamp> &stamps, int start, int end);

} // namespace Media
