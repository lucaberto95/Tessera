#pragma once

#include <QDialog>

class Project;
class QTableWidget;

// Lists every link of the project with its two ends and its comment. Double click (or the
// buttons) opens one end in the document view; the comment can be edited and links deleted.
class LinksDialog : public QDialog {
    Q_OBJECT
public:
    explicit LinksDialog(Project *project, QWidget *parent = nullptr);

    qint64 currentLinkId() const;
    void selectLink(qint64 linkId);
    // Emits endActivated for the origin (false) or the target (true) of the current link.
    void openCurrentEnd(bool target);

signals:
    void endActivated(qint64 documentId, int start, int end);

private:
    void refresh();
    void editComment();
    void deleteCurrent();

    Project *m_project;
    QTableWidget *m_table;
};
