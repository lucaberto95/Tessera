#pragma once

#include <QString>
#include <QStringList>

class Project;

// Continuous team work through a shared folder, with no server (issue #24, option 2: log of changes).
//
// Every coder keeps their OWN copy of the project (the .tsra never goes into the shared folder: SQLite files
// synchronised by a cloud service get corrupted) and "publishes" their work as a change log in the shared
// folder: "tessera-<project uuid>.sync/<member>.tsrlog". A member writes only their own file, so two people never write the
// same file. "Synchronise" reads the files of the other members and applies what is new to the local copy, in
// one undo step. See docs in TeamSync.cpp for the file format and the conflict rules.
//
// Why not Merge filtered by author: Merge needs the other person's whole project (documents with their content,
// as a .tsra), which is exactly what must not travel through the cloud folder, and it cannot tell "edited by the
// author since" from "edited here". The log carries only the records of its author, each with a uuid and a
// timestamp; applying them reuses the public API of Project and the same matching ideas as Merge.
namespace TeamSync {

constexpr int FormatVersion = 1;
constexpr const char *FileSuffix = "tsrlog";
constexpr const char *StateKey = "team_sync_state"; // meta: what has already been read from each member

// Folder that holds the logs of a project inside the shared folder: "tessera-<project uuid>.sync" (the members
// may have renamed their copies, the identifier is the same for all).
QString syncFolder(const QString &sharedFolder, const QString &projectUuid);
// File of a member inside it.
QString logFileName(const QString &member);

struct PublishResult {
    bool ok = true;
    QString error;
    QString path;
    int records = 0;
    int generation = 0; // grows at every publication
};

// Writes the log of the work of project.author() (segments, links, memos, summaries, with the codes and
// documents they refer to) into `sharedFolder`. Atomic: a half-written file is never visible. Refuses an empty
// author and a file that belongs to another member whose name gives the same file name.
PublishResult publish(Project &project, const QString &sharedFolder);

struct MemberReport {
    QString member;
    QString file;
    int generation = 0;
    bool unchanged = false; // same file as the last time: nothing to read
    bool ignored = false;   // partial, corrupt, of another project or of a newer format
    QString note;           // why it was ignored / what was found
    int segmentsNew = 0, linksNew = 0, memosNew = 0, summariesNew = 0, codesNew = 0;
    int updated = 0;   // records that the author changed after the previous synchronisation
    int conflicts = 0; // texts changed on both sides
    int skipped = 0;   // records that could not be placed (missing or different document...)
    int changes() const { return segmentsNew + linksNew + memosNew + summariesNew + codesNew + updated + conflicts; }
};

struct SyncReport {
    bool ok = true;
    QString error;
    QList<MemberReport> members;
    QStringList warnings;  // problems with files or documents
    QStringList conflicts; // one line per text changed on both sides (both versions are kept)
    int changes() const
    {
        int n = 0;
        for (const MemberReport &m : members)
            n += m.changes();
        return n;
    }
    int ignoredFiles() const
    {
        int n = 0;
        for (const MemberReport &m : members)
            n += m.ignored ? 1 : 0;
        return n;
    }
};

// Reads the logs of the other members and applies what is new, as one undo step. With `dryRun` nothing is
// kept (the same run is rolled back). `forceReread` ignores what was remembered about the files (they are
// read again; records already applied are recognised and not duplicated).
SyncReport synchronize(Project &project, const QString &sharedFolder, bool dryRun = false, bool forceReread = false);

// Forgets what has been read so far (for instance after undoing a synchronisation that should be redone).
void resetState(Project &project);

// Names of the members that have a log in the folder.
QStringList members(const Project &project, const QString &sharedFolder);

// One-line summary of a report.
QString summarize(const SyncReport &report);

} // namespace TeamSync
