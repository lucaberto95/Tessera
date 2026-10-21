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

} // namespace Qdpx
