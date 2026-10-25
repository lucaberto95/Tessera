#pragma once

#include "core/Model.h"

#include <QDialog>
#include <QPointF>

class MapScene;
class Project;
class QGraphicsView;
class QListWidget;

// Concept maps (issue #15): a canvas of nodes (free text, codes, documents, memos, segments) joined by labelled
// arrows. Every change is saved at once; a node is saved when the mouse is released, not while dragging.
// Codes and documents can be dragged from the trees onto the canvas.
class ConceptMapDialog : public QDialog {
    Q_OBJECT
public:
    explicit ConceptMapDialog(Project *project, QWidget *parent = nullptr);

    qint64 currentMapId() const;
    void selectMap(qint64 id);
    qint64 createMap(const QString &name);
    // Adds a node for an element of the project (or a free text node for MapNodeKind::Text) to the current map.
    qint64 addElement(MapNodeKind kind, qint64 refId, const QString &label = {}, QPointF at = {});
    int nodeItemCount() const;
    int edgeItemCount() const;
    // Renders the current map; the format follows the extension (png, svg or pdf).
    bool exportTo(const QString &path);

signals:
    // Double click on a node that stands for an element of the project.
    void elementActivated(int kind, qint64 refId);

private:
    friend class MapScene;
    void reloadList();
    void scheduleReload();
    void loadMap();
    void saveMovedNodes();
    void createLink(qint64 fromNode, qint64 toNode);
    void editItem(int itemType, qint64 id);
    void newMap();
    void renameMap();
    void deleteMap();
    void addFreeText();
    void addPicked(MapNodeKind kind);
    void runTemplate(int which);
    void startLink(bool on);
    void deleteSelection();
    void editSelection();
    void applyColor();
    void applyWidth();
    void applyLine(const QString &line);
    void align(int how);
    void exportDialog();
    void zoom(double factor);
    QPointF viewCenter() const;
    void updateActions();

    Project *m_project;
    QListWidget *m_list;
    QGraphicsView *m_view;
    MapScene *m_scene;
    QAction *m_linkAction = nullptr;
    QList<QAction *> m_needsMap;
    QList<QAction *> m_needsSelection;
    bool m_reloadPending = false;
    bool m_busy = false;
};