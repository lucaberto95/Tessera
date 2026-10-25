#pragma once

#include "core/Merge.h"

#include <QString>
#include <QStringList>

class Project;

// Team work with project files (issue #24, option 1: rounds on a shared folder, no server). The project manager
// ("responsabile") starts a round: Tessera writes one copy of the project per member, named after the round and
// the member, and records the round in the manager's project (Project::teamCycles). Each member opens their
// copy, which already knows who they are (meta "team_member": the author name recorded on every coding, memo,
// link and summary they write), codes, and sends the file back. The manager imports the returned copies: each
// is merged with Merge restricted to the member's own work, and the round is marked as merged. Once everything
// is in, the coders can be compared with the existing intercoder agreement (Agreement::byAuthor).
namespace Team {

constexpr const char *MemberKey = "team_member"; // meta of a copy handed to a member
constexpr const char *CycleKey = "team_cycle";   // meta: round number of that copy

// File name of the copy of round `number` for `member`: "<project>_ciclo<N>_<member>.tsra", with characters not
// allowed in file names replaced.
QString copyFileName(const QString &projectName, int number, const QString &member);

struct StartResult {
    bool ok = true;
    QString error;
    int number = 0;       // round number
    QStringList files;    // paths written, in the order of the members
};

// Writes one copy per member into `folder` (created if needed) and records the round. Members are trimmed,
// blank and repeated names are refused. Nothing is recorded when a copy cannot be written. The copies carry
// no round history.
StartResult startCycle(Project &manager, const QStringList &members, const QString &folder);

struct ReturnResult {
    bool ok = true;
    QString error;
    qint64 cycleId = 0;     // the round entry the copy belongs to
    QString member;
    int number = 0;
    bool noWork = false;    // the copy has no work by the member (merge did nothing)
    Merge::Report report;
};

// Which open round entry a returned file answers to, from the markers inside it. Returns 0 and sets `error`
// when the file does not come from a round of this project (unknown member/round, or already merged).
qint64 findCycle(const Project &manager, const Merge::Source &source, QString *error);

// Merges a returned copy: only the member's own work, as one undo step; the round entry goes to "returned"
// and, on success, to "merged" with a summary. With `dryRun` nothing is written (counts only).
ReturnResult importReturned(Project &manager, const QString &path, Merge::TextConflict onConflict, bool dryRun = false);

// One-line summary of a merge report for the round history.
QString summarize(const Merge::Report &report);

} // namespace Team