#pragma once

#include <QSet>
#include <QTreeWidget>

class Project;

// "Code system": hierarchical codes with colors and frequencies. Codes can be reorganized by
// drag and drop and dragged onto a text selection to code it.
class CodeTree : public QTreeWidget {
    Q_OBJECT
public:
    static constexpr int IdRole = Qt::UserRole;

    explicit CodeTree(Project *project, QWidget *parent = nullptr);

    void rebuild();
    qint64 currentCodeId() const;
    void setCurrentCode(qint64 id);
    QList<qint64> activeCodeIds() const;
    void setAllActive(bool active);

    // Interactive actions, also used by the main window menus.
    qint64 newCode(qint64 parentId);
    void renameCode(qint64 id);
    void changeColor(qint64 id);
    void editDescription(qint64 id);
    void editMemo(qint64 id);
    void deleteCode(qint64 id);

signals:
    void activationChanged();
    void currentCodeChanged(qint64 id);
    void aiSubcodesRequested(qint64 codeId); // "Suggerisci sottocodici con l'AI" (issue #41)
    void aiGroupRequested(qint64 parentId);  // "Raggruppa i sottocodici/codici con l'AI" (0 = first level)
    void aiSummaryRequested(qint64 codeId); // "Riassumi i segmenti con l'AI…" (issue #40)

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
    QStringList mimeTypes() const override;
    void dropEvent(QDropEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;

private:
    void onItemChanged(QTreeWidgetItem *item, int column);
    QTreeWidgetItem *findItem(qint64 id) const;

    Project *m_project;
    QSet<qint64> m_active;
    QSet<qint64> m_collapsed;
    bool m_rebuilding = false;
};
