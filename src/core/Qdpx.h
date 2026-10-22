#pragma once

#include <QString>

class Project;

// REFI-QDA project exchange (.qdpx = zip with project.qde + sources/), see
// https://www.qdasoftware.org. Text sources, codes, codings, notes, variables and sets.
namespace Qdpx {

bool exportProject(const Project &project, const QString &path, QString *error);

// Imports into an open (normally freshly created) project. `report` summarizes what was
// imported and what was skipped (non-text sources, unresolved references...).
bool importProject(Project &project, const QString &path, QString *report, QString *error);

// Whether an external source named by a .qdpx (absolute:// or relative://, already resolved to
// `file`) may be read. Network paths are refused unless they lie in `qdpxDir`: on Windows merely
// opening \\host\share sends the user's credentials to that host.
bool isReadableSourcePath(const QString &file, const QString &qdpxDir);

} // namespace Qdpx
