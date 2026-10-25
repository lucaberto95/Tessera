#pragma once

#include <QString>

class Project;

// Import of projects made with other QDA software. Only open, documented interchange formats
// (REFI-QDA .qdpx projects and .qdc codebooks) are read. The native files of MAXQDA and NVivo are
// recognised and explained, but not converted: see docs/foreign-formats.md.
namespace ForeignImport {

enum class Kind {
    Unknown,
    Qdpx,        // REFI-QDA project (zip with project.qde)
    Qdc,         // REFI-QDA codebook (XML)
    MaxqdaNative, // .mqda / .mx24 ... (SQLite with a sys_Setup table)
    NvivoNative  // .nvp / .nvpx
};

struct Info {
    Kind kind = Kind::Unknown;
    QString description; // human readable, Italian; for native files includes the detected version
};

// Looks at the content (not only at the extension) and never modifies the file.
Info detect(const QString &path);

// Dialog filter for the "import project from another program" action.
QString fileDialogFilter();

// Imports a REFI-QDA project or codebook into an open (freshly created) project. Native MAXQDA and
// NVivo files return false with an `error` that says how to export them in REFI-QDA format.
bool importFile(Project &project, const QString &path, QString *report, QString *error);

} // namespace ForeignImport
