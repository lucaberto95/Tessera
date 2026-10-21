#pragma once

class Project;

// Fills an empty project with a small example study (three interviews on remote work),
// useful to try the program and for screenshots/tests.
namespace DemoProject {
bool populate(Project &project);
}
