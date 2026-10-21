#pragma once

#include <QSet>
#include <QTreeWidget>

class Project;

// "Document system": groups and documents, with check boxes to activate documents for retrieval.
class DocumentTree : public QTreeWidget {
    Q_OBJECT
public:
    enum Kind { GroupItem = 1, DocumentItem = 2 };
    static constexpr int KindRole = Qt::UserRole;
    static constexpr int IdRole = Qt::UserRole + 1;

    explicit DocumentTree(Project *project, QWidget *parent = nullptr);

    void rebuild();
    QList<qint64> activeDocumentIds() const;
    qint64 currentGroupId() const; // group of the selected item, or the first group
    void setCurrentDocument(qint64 id);
    void setAllActive(bool active);

signals:
    void documentOpened(qint64 id);
    void activationChanged();
    void importRequested(qint64 groupId);
    void newTextDocumentRequested(qint64 groupId);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void onItemChanged(QTreeWidgetItem *item, int column);
    void renameItem(QTreeWidgetItem *item);
    void deleteItem(QTreeWidgetItem *item);
    void editMemo(QTreeWidgetItem *item);

    Project *m_project;
    QSet<qint64> m_active;
    QSet<qint64> m_collapsedGroups;
    bool m_rebuilding = false;
};
