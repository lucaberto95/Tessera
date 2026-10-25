#pragma once

#include <QString>
#include <QStringList>

#include <memory>

class Project;
class QTemporaryDir;

// Merging another project into the open one (issue #22): codings, memos, summaries, links, codes and new
// documents of a colleague's copy. Entities are matched by uuid, then by name/path (documents: name plus
// content). The whole merge is one undo step.
namespace Merge {

// What to do with a document that has the same uuid (or name) but a different content.
enum class TextConflict { ImportCopy, Skip };

struct Options {
    TextConflict onTextConflict = TextConflict::ImportCopy;
    // Non-empty: only the codings, memos, summaries and links written by this author are merged, with the
    // documents, codes and sets they need (no dictionaries, summary tables or unrelated documents).
    QString onlyAuthor;
};

struct Report {
    bool ok = true;
    QString error;
    // Documents: new in the target, already there, same document with a different content, and what became of
    // those (imported as a copy, skipped).
    int documentsNew = 0, documentsPresent = 0, documentsConflict = 0, documentsCopied = 0, documentsSkipped = 0;
    int groupsNew = 0;
    int codesNew = 0, codesPresent = 0;
    // Segments: new, identical ones, same place and code but a different comment (comments are joined), left
    // out because of the author filter, left out because their document was skipped or could not be added.
    int segmentsNew = 0, segmentsDuplicate = 0, segmentsConflict = 0, segmentsFiltered = 0, segmentsSkipped = 0;
    int memosNew = 0, memosPresent = 0, memosMerged = 0, memosSkipped = 0;
    int linksNew = 0, linksDuplicate = 0;
    int summariesNew = 0, summariesPresent = 0, summariesMerged = 0;
    int variablesNew = 0, variableValuesSet = 0, variableValueConflicts = 0;
    int setsNew = 0, setsExtended = 0;
    int dictionariesNew = 0, summaryTablesNew = 0;
    int conceptMapsNew = 0;
    int questionsNew = 0, questionsExtended = 0; // research questions: new, and present ones that gained evidence
    QStringList conflictDocuments; // names of the documents whose content differs

    int changes() const
    {
        return documentsNew + documentsCopied + groupsNew + codesNew + segmentsNew + segmentsConflict + memosNew +
               memosMerged + linksNew + summariesNew + summariesMerged + variablesNew + variableValuesSet +
               setsNew + setsExtended + dictionariesNew + summaryTablesNew + conceptMapsNew + questionsNew + questionsExtended;
    }
};

// The project to merge, read from a private copy: opening a .tsra may upgrade its schema, which must not touch
// the original file; a .qdpx is imported into a temporary project.
class Source {
public:
    Source();
    ~Source();
    bool load(const QString &path, QString *error);
    Project &project() { return *m_project; }
    const Project &project() const { return *m_project; }
    QString name() const { return m_name; }
    QStringList authors() const; // everyone who wrote a coding, memo, link or summary

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    std::unique_ptr<Project> m_project;
    QString m_name;
};

// Dry run: counts what apply() would do, without writing anything.
Report analyze(Project &target, const Source &source, const Options &options);
// Merges inside one batch (one undo step). On failure nothing is changed and `ok` is false.
Report apply(Project &target, const Source &source, const Options &options);

} // namespace Merge
