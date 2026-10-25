#pragma once

#include "core/Model.h"
#include "core/Report.h"

class Project;

// Research questions worksheet (issue #17): the questions of the project with their themes, evidence
// (segments quoted with their text, codes, memos) and conclusions, as a draft of a results chapter. The result is
// a Report::Document, so it is written as Word (.docx), PDF or HTML with the writers of Report.
namespace ResearchWorksheet {

struct Options {
    QString title;           // empty = "Domande di ricerca"
    bool contents = true;
    bool notes = false;      // working notes of each question
    QList<qint64> questions; // empty = all, in worksheet order
};

QString statusLabel(QuestionStatus status); // "Aperta", "In lavorazione", "Con risposta"

Report::Document build(const Project &project, const Options &options);

} // namespace ResearchWorksheet
