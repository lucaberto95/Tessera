#pragma once

#include "core/Model.h"

#include <QDialog>

class Project;
class QLineEdit;
class QListWidget;
class QMimeData;
class QPlainTextEdit;
class QTreeWidget;

// Editor for a single memo, with the elements it is linked to ("Collegato a"). It is not modal:
// codes and documents dragged onto it from the trees are added to its links. Links, title and
// text are written together on "Salva".
class MemoDialog : public QDialog {
    Q_OBJECT
public:
    MemoDialog(Project *project, const Memo &memo, QWidget *parent = nullptr);

    qint64 memoId() const { return m_memo.id; }
    QList<MemoLink> links() const { return m_links; } // as currently shown, not yet saved
    void addLinks(const QList<MemoLink> &links);
    void removeLinks(const QList<int> &rows);
    bool save(); // false if nothing was written (error, or an empty new memo)

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static bool canLink(const QMimeData *mime);
    bool linkDropped(const QMimeData *mime);
    void refreshLinks();
    void pickLinks();
    void remove();

    Project *m_project;
    Memo m_memo;
    QList<MemoLink> m_links;
    QList<MemoLink> m_savedLinks;
    QLineEdit *m_title;
    QPlainTextEdit *m_body;
    QListWidget *m_linkList;
};

// Chooses codes, documents (and their groups and sets) and segments to link a memo to.
// Elements already linked are shown checked, in italics.
class MemoLinkPicker : public QDialog {
    Q_OBJECT
public:
    MemoLinkPicker(Project *project, const QList<MemoLink> &existing, QWidget *parent = nullptr);
    QList<MemoLink> selectedLinks() const; // checked elements that were not linked yet
    QTreeWidget *tree() const { return m_tree; }

private:
    QTreeWidget *m_tree;
    QList<MemoLink> m_existing;
};
