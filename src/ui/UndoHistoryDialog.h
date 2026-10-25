#pragma once

#include <QDialog>

class Project;
class QListWidget;
class QPushButton;

// Non-modal list of the steps that can be undone and redone, kept up to date by the project's
// undoStateChanged signal. Selecting a step and pressing the button (or double click) undoes or
// redoes every step up to and including it, in one go.
class UndoHistoryDialog : public QDialog {
    Q_OBJECT
public:
    explicit UndoHistoryDialog(Project *project, QWidget *parent = nullptr);

    // Row of the "current state" marker: redo steps are above it, undo steps below.
    int currentStateRow() const;
    int rowCount() const;
    void selectRow(int row);
    // Undoes or redoes up to the selected step. Returns false if nothing is selected or a step failed.
    bool applySelected();

private:
    void refresh();
    void updateButton();

    Project *m_project;
    QListWidget *m_list;
    QPushButton *m_apply;
};
