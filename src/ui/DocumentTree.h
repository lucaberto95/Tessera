#pragma once

#include <QSet>
#include <QTreeWidget>

class Project;
class QMenu;

// "Document system": groups and documents, then the document sets, with check boxes to activate
// documents for retrieval. A set lists references to documents of any group, so the same document
// can appear several times; all its check boxes show the same activation.
class DocumentTree : public QTreeWidget {
    Q_OBJECT
public:
    enum Kind { GroupItem = 1, DocumentItem = 2, SetsRootItem = 3, SetItem = 4, SetMemberItem = 5 };
    static constexpr int KindRole = Qt::UserRole;
    static constexpr int IdRole = Qt::UserRole + 1;

    explicit DocumentTree(Project *project, QWidget *parent = nullptr);

    void rebuild();
    QList<qint64> activeDocumentIds() const;
    qint64 currentGroupId() const; // group of the selected item, or the first group
    void setCurrentDocument(qint64 id);
    void setAllActive(bool active);
    void setActiveDocuments(const QList<qint64> &ids);

    qint64 newDocumentSet(const QList<qint64> &documentIds = {}); // asks for the name
    qint64 newDocumentSetFromActive();

signals:
    void documentOpened(qint64 id);
    void activationChanged();
    void importRequested(qint64 groupId);
    void newTextDocumentRequested(qint64 groupId);
    void aiSuggestCodesRequested(qint64 documentId); // "Suggerisci codici con l'AI" (issue #41)
    void aiSummaryRequested(qint64 documentId); // "Riassumi con l'AI…" (issue #40)

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;

private:
    static bool isDocument(const QTreeWidgetItem *item);
    static QString expansionKey(const QTreeWidgetItem *item);
    void onItemChanged(QTreeWidgetItem *item, int column);
    void syncDocumentCheckBoxes(qint64 documentId, const QTreeWidgetItem *except);
    std::pair<int, qint64> dropTarget(QDropEvent *event, qint64 *documentId) const;
    void renameItem(QTreeWidgetItem *item);
    void deleteItem(QTreeWidgetItem *item);
    void editMemo(QTreeWidgetItem *item);
    void addSetMenu(QMenu *menu, qint64 documentId);

    Project *m_project;
    QSet<qint64> m_active;
    QSet<QString> m_collapsed; // expansionKey() of collapsed groups and sets
    bool m_rebuilding = false;
};
