#include "ui/ConceptMapDialog.h"

#include "core/ConceptMaps.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsSceneDragDropEvent>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPdfWriter>
#include <QPushButton>
#include <QSet>
#include <QSvgGenerator>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr int kNodeType = QGraphicsItem::UserType + 1;
constexpr int kEdgeType = QGraphicsItem::UserType + 2;
constexpr double kGrid = 20;
const QColor kSelectionColor(0x1a, 0x73, 0xe8);

QColor defaultFill(MapNodeKind kind, const QColor &codeColor)
{
    switch (kind) {
    case MapNodeKind::Text: return QColor(0xff, 0xf6, 0xc8);
    case MapNodeKind::Document: return QColor(0xe3, 0xee, 0xfc);
    case MapNodeKind::Memo: return QColor(0xff, 0xe9, 0xd2);
    case MapNodeKind::Segment: return QColor(0xe6, 0xf4, 0xe6);
    case MapNodeKind::Code:
        return codeColor.isValid() ? codeColor.lighter(185) : QColor(0xee, 0xee, 0xee);
    }
    return Qt::white;
}

} // namespace

class EdgeItem;

class NodeItem : public QGraphicsItem {
public:
    NodeItem(const MapNode &node, const ConceptMaps::Target &target) : m_node(node), m_target(target)
    {
        setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
        setPos(node.x, node.y);
        const ConceptMaps::Style style = ConceptMaps::styleFromJson(node.styleJson);
        m_fill = style.color.isValid() ? style.color : defaultFill(node.kind, target.color);
        const QFontMetrics fm(QApplication::font());
        const double iconWidth = node.kind == MapNodeKind::Text ? 0 : 24;
        double w = node.w;
        if (w <= 0)
            w = std::clamp<double>(fm.horizontalAdvance(target.caption) + 24 + iconWidth, 110, 240);
        double h = node.h;
        if (h <= 0)
            h = std::max<double>(
                36, fm.boundingRect(QRect(0, 0, int(w - 16 - iconWidth), 2000), Qt::TextWordWrap, target.caption).height() + 18);
        m_size = QSizeF(w, h);
    }

    int type() const override { return kNodeType; }
    qint64 id() const { return m_node.id; }
    const MapNode &node() const { return m_node; }
    void setStored(const MapNode &n) { m_node = n; }
    const ConceptMaps::Target &target() const { return m_target; }
    QSizeF size() const { return m_size; }
    QPointF center() const { return pos() + QPointF(m_size.width() / 2, m_size.height() / 2); }
    QRectF sceneRectOfNode() const { return QRectF(pos(), m_size); }
    void setSnap(bool on) { m_snap = on; }
    void addEdge(EdgeItem *e) { m_edges << e; }
    void setLinkSource(bool on)
    {
        m_linkSource = on;
        update();
    }

    QRectF boundingRect() const override { return QRectF(-3, -3, m_size.width() + 6, m_size.height() + 6); }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        p->setRenderHint(QPainter::Antialiasing);
        const bool hot = isSelected() || m_linkSource;
        const QColor border =
            isSelected() ? kSelectionColor : m_linkSource ? QColor(0xe8, 0x71, 0x0a) : QColor(0x60, 0x60, 0x60);
        p->setPen(QPen(border, hot ? 3 : 1.5, m_target.exists ? Qt::SolidLine : Qt::DashLine));
        p->setBrush(m_target.exists ? m_fill : QColor(0xe0, 0xe0, 0xe0));
        const QRectF r(QPointF(), m_size);
        p->drawRoundedRect(r, 8, 8);

        double textLeft = 8;
        if (m_node.kind != MapNodeKind::Text) {
            const QRectF icon(7, r.height() / 2 - 7, 14, 14);
            p->setPen(QPen(QColor(0x40, 0x40, 0x40), 1));
            switch (m_node.kind) {
            case MapNodeKind::Code:
                p->setBrush(m_target.color.isValid() ? m_target.color : QColor(0x99, 0x99, 0x99));
                p->drawEllipse(icon);
                break;
            case MapNodeKind::Document:
                p->setBrush(Qt::white);
                p->drawRect(icon.adjusted(2, 0, -2, 0));
                p->drawLine(icon.topLeft() + QPointF(4, 4), icon.topRight() + QPointF(-4, 4));
                p->drawLine(icon.topLeft() + QPointF(4, 8), icon.topRight() + QPointF(-4, 8));
                break;
            case MapNodeKind::Memo:
                Ui::memoIcon().paint(p, icon.toRect());
                break;
            default: {
                QFont f = p->font();
                f.setBold(true);
                p->setFont(f);
                p->drawText(icon, Qt::AlignCenter, QStringLiteral("\""));
                break;
            }
            }
            textLeft = 26;
        }
        p->setPen(QColor(0x22, 0x22, 0x22));
        QFont f = p->font();
        f.setBold(false);
        f.setItalic(!m_target.exists);
        p->setFont(f);
        QTextOption opt(m_node.kind == MapNodeKind::Text ? Qt::AlignCenter : Qt::AlignVCenter | Qt::AlignLeft);
        opt.setWrapMode(QTextOption::WordWrap);
        p->drawText(QRectF(textLeft, 4, m_size.width() - textLeft - 8, m_size.height() - 8), m_target.caption, opt);
    }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;

private:
    MapNode m_node;
    ConceptMaps::Target m_target;
    QColor m_fill;
    QSizeF m_size;
    QList<EdgeItem *> m_edges;
    bool m_snap = false;
    bool m_linkSource = false;
};

class EdgeItem : public QGraphicsItem {
public:
    EdgeItem(const MapEdge &edge, NodeItem *from, NodeItem *to) : m_edge(edge), m_from(from), m_to(to)
    {
        setFlag(ItemIsSelectable);
        setZValue(-1);
        m_style = ConceptMaps::styleFromJson(edge.styleJson);
        from->addEdge(this);
        to->addEdge(this);
        updateGeometry();
    }

    int type() const override { return kEdgeType; }
    qint64 id() const { return m_edge.id; }
    const MapEdge &edge() const { return m_edge; }

    void updateGeometry()
    {
        prepareGeometryChange();
        const QPointF c1 = m_from->center(), c2 = m_to->center();
        m_p1 = border(m_from, c2);
        m_p2 = border(m_to, c1);
        m_labelRect = QRectF();
        if (!m_edge.label.isEmpty()) {
            const QFontMetrics fm(QApplication::font());
            QRectF r = fm.boundingRect(m_edge.label);
            r.adjust(-5, -2, 5, 2);
            r.moveCenter((m_p1 + m_p2) / 2);
            m_labelRect = r;
        }
    }

    QRectF boundingRect() const override
    {
        return QRectF(m_p1, m_p2).normalized().united(m_labelRect).adjusted(-14, -14, 14, 14);
    }

    QPainterPath shape() const override
    {
        QPainterPath line(m_p1);
        line.lineTo(m_p2);
        QPainterPathStroker stroker;
        stroker.setWidth(std::max(12, m_style.width + 8));
        QPainterPath s = stroker.createStroke(line);
        s.addRect(m_labelRect);
        return s;
    }

    void paint(QPainter *p, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        if (m_from == m_to)
            return;
        p->setRenderHint(QPainter::Antialiasing);
        const QColor color =
            isSelected() ? kSelectionColor : m_style.color.isValid() ? m_style.color : QColor(0x44, 0x44, 0x44);
        const Qt::PenStyle ps = m_style.line == QLatin1String("dash")  ? Qt::DashLine
                                : m_style.line == QLatin1String("dot") ? Qt::DotLine
                                                                       : Qt::SolidLine;
        p->setPen(QPen(color, m_style.width, ps, Qt::FlatCap));
        QLineF line(m_p1, m_p2);
        const double head = 9 + m_style.width * 1.5;
        if (line.length() > head)
            line.setLength(line.length() - head * 0.7);
        p->drawLine(line);
        // Arrow head at the target.
        const double angle = std::atan2(m_p2.y() - m_p1.y(), m_p2.x() - m_p1.x());
        const QPointF a = m_p2 - QPointF(std::cos(angle - 0.4) * head, std::sin(angle - 0.4) * head);
        const QPointF b = m_p2 - QPointF(std::cos(angle + 0.4) * head, std::sin(angle + 0.4) * head);
        p->setPen(QPen(color, 1));
        p->setBrush(color);
        p->drawPolygon(QPolygonF({m_p2, a, b}));
        if (!m_labelRect.isNull()) {
            p->setPen(QPen(color, 1));
            p->setBrush(Qt::white);
            p->drawRoundedRect(m_labelRect, 4, 4);
            p->setPen(QColor(0x22, 0x22, 0x22));
            p->drawText(m_labelRect, Qt::AlignCenter, m_edge.label);
        }
    }

private:
    static QPointF border(const NodeItem *n, const QPointF &toward)
    {
        const QPointF c = n->center();
        const QPointF d = toward - c;
        if (qFuzzyIsNull(d.x()) && qFuzzyIsNull(d.y()))
            return c;
        const double hw = n->size().width() / 2, hh = n->size().height() / 2;
        const double tx = qFuzzyIsNull(d.x()) ? 1e9 : hw / std::abs(d.x());
        const double ty = qFuzzyIsNull(d.y()) ? 1e9 : hh / std::abs(d.y());
        return c + d * std::min(tx, ty);
    }

    MapEdge m_edge;
    NodeItem *m_from;
    NodeItem *m_to;
    ConceptMaps::Style m_style;
    QPointF m_p1, m_p2;
    QRectF m_labelRect;
};

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    if (change == ItemPositionChange && m_snap) {
        const QPointF p = value.toPointF();
        return QPointF(std::round(p.x() / kGrid) * kGrid, std::round(p.y() / kGrid) * kGrid);
    }
    if (change == ItemPositionHasChanged)
        for (EdgeItem *e : std::as_const(m_edges))
            e->updateGeometry();
    return QGraphicsItem::itemChange(change, value);
}

// The canvas: grid, link mode, drops from the trees, and the events the dialog saves from.
class MapScene : public QGraphicsScene {
    Q_OBJECT
public:
    using QGraphicsScene::QGraphicsScene;

    bool grid() const { return m_grid; }
    void setGrid(bool on)
    {
        m_grid = on;
        for (QGraphicsItem *i : items())
            if (i->type() == kNodeType)
                static_cast<NodeItem *>(i)->setSnap(on);
        invalidate(sceneRect(), BackgroundLayer);
    }
    void setExporting(bool on) { m_exporting = on; }
    void setLinking(bool on)
    {
        m_linking = on;
        cancelLink();
    }
    void cancelLink()
    {
        if (m_source)
            m_source->setLinkSource(false);
        m_source = nullptr;
    }
    void forgetItems() { m_source = nullptr; }

signals:
    void moved();
    void linkRequested(qint64 fromNode, qint64 toNode);
    void editRequested(int itemType, qint64 id);
    void elementActivated(int kind, qint64 refId);
    void elementDropped(int kind, qint64 refId, QPointF at);

protected:
    void drawBackground(QPainter *p, const QRectF &rect) override
    {
        p->fillRect(rect, Qt::white);
        if (!m_grid || m_exporting)
            return;
        p->setPen(QPen(QColor(0xc8, 0xc8, 0xc8), 1.5));
        const double x0 = std::floor(rect.left() / kGrid) * kGrid, y0 = std::floor(rect.top() / kGrid) * kGrid;
        for (double x = x0; x < rect.right(); x += kGrid)
            for (double y = y0; y < rect.bottom(); y += kGrid)
                p->drawPoint(QPointF(x, y));
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *e) override
    {
        if (m_linking && e->button() == Qt::LeftButton) {
            QGraphicsItem *item = itemAt(e->scenePos(), QTransform());
            if (item && item->type() == kNodeType) {
                auto *node = static_cast<NodeItem *>(item);
                if (!m_source) {
                    m_source = node;
                    node->setLinkSource(true);
                } else if (node != m_source) {
                    const qint64 from = m_source->id(), to = node->id();
                    cancelLink();
                    emit linkRequested(from, to);
                }
            } else {
                cancelLink();
            }
            e->accept();
            return;
        }
        QGraphicsScene::mousePressEvent(e);
    }

    void mouseReleaseEvent(QGraphicsSceneMouseEvent *e) override
    {
        QGraphicsScene::mouseReleaseEvent(e);
        if (!m_linking)
            emit moved();
    }

    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *e) override
    {
        QGraphicsItem *item = itemAt(e->scenePos(), QTransform());
        if (item && item->type() == kNodeType) {
            const auto *node = static_cast<NodeItem *>(item);
            if (node->node().kind == MapNodeKind::Text)
                emit editRequested(1, node->id());
            else if (node->target().exists)
                emit elementActivated(int(node->node().kind), node->node().refId);
            e->accept();
            return;
        }
        if (item && item->type() == kEdgeType) {
            emit editRequested(2, static_cast<EdgeItem *>(item)->id());
            e->accept();
            return;
        }
        QGraphicsScene::mouseDoubleClickEvent(e);
    }

    static bool accepts(const QMimeData *m)
    {
        return m && (m->hasFormat(QString::fromLatin1(Ui::CodeMimeType)) ||
                     m->hasFormat(QString::fromLatin1(Ui::DocumentMimeType)));
    }
    void dragEnterEvent(QGraphicsSceneDragDropEvent *e) override { e->setAccepted(accepts(e->mimeData())); }
    void dragMoveEvent(QGraphicsSceneDragDropEvent *e) override { e->setAccepted(accepts(e->mimeData())); }
    void dropEvent(QGraphicsSceneDragDropEvent *e) override
    {
        if (!accepts(e->mimeData()))
            return;
        if (e->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType)))
            emit elementDropped(int(MapNodeKind::Code),
                                e->mimeData()->data(QString::fromLatin1(Ui::CodeMimeType)).toLongLong(), e->scenePos());
        else
            emit elementDropped(int(MapNodeKind::Document),
                                e->mimeData()->data(QString::fromLatin1(Ui::DocumentMimeType)).toLongLong(),
                                e->scenePos());
        e->acceptProposedAction();
    }

private:
    bool m_grid = false;
    bool m_exporting = false;
    bool m_linking = false;
    NodeItem *m_source = nullptr;
};

namespace {
// Zoom with Ctrl + wheel.
class MapView : public QGraphicsView {
public:
    using QGraphicsView::QGraphicsView;
    std::function<void(double)> zoomBy;

protected:
    void wheelEvent(QWheelEvent *e) override
    {
        if (e->modifiers() & Qt::ControlModifier) {
            if (zoomBy)
                zoomBy(e->angleDelta().y() > 0 ? 1.15 : 1 / 1.15);
            e->accept();
            return;
        }
        QGraphicsView::wheelEvent(e);
    }
};

int pickIndex(QWidget *parent, const QString &title, const QString &label, const QStringList &items)
{
    if (items.isEmpty()) {
        Ui::showInfo(parent, title, QCoreApplication::translate("ConceptMapDialog", "Non c'è nulla da scegliere."));
        return -1;
    }
    bool ok = false;
    const QString chosen = QInputDialog::getItem(parent, title, label, items, 0, false, &ok);
    return ok ? int(items.indexOf(chosen)) : -1;
}
} // namespace

ConceptMapDialog::ConceptMapDialog(Project *project, QWidget *parent) : QDialog(parent), m_project(project)
{
    setWindowTitle(tr("Mappe concettuali"));
    setWindowFlag(Qt::WindowMaximizeButtonHint);
    resize(1100, 680);

    m_list = new QListWidget;
    m_list->setMaximumWidth(220);
    auto *newButton = new QPushButton(tr("Nuova"));
    auto *renameButton = new QPushButton(tr("Rinomina"));
    auto *deleteButton = new QPushButton(tr("Elimina"));
    for (QPushButton *b : {newButton, renameButton, deleteButton})
        b->setAutoDefault(false);
    connect(newButton, &QPushButton::clicked, this, &ConceptMapDialog::newMap);
    connect(renameButton, &QPushButton::clicked, this, &ConceptMapDialog::renameMap);
    connect(deleteButton, &QPushButton::clicked, this, &ConceptMapDialog::deleteMap);

    m_scene = new MapScene(this);
    m_scene->setSceneRect(-400, -300, 800, 600);
    auto *view = new MapView(m_scene);
    view->zoomBy = [this](double f) { zoom(f); };
    view->setRenderHint(QPainter::Antialiasing);
    view->setDragMode(QGraphicsView::RubberBandDrag);
    view->setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    view->setAcceptDrops(true);
    m_view = view;

    auto *bar = new QToolBar;
    auto act = [&](const QString &text, auto slot, bool needsSel = false) {
        QAction *a = bar->addAction(text, this, slot);
        m_needsMap << a;
        if (needsSel)
            m_needsSelection << a;
        return a;
    };
    auto menuButton = [&](const QString &text, QMenu *menu, bool needsSel = false) {
        QAction *a = bar->addAction(text);
        a->setMenu(menu);
        m_needsMap << a;
        if (needsSel)
            m_needsSelection << a;
        return a;
    };

    auto *addMenu = new QMenu(this);
    addMenu->addAction(tr("Testo libero"), this, &ConceptMapDialog::addFreeText);
    addMenu->addAction(tr("Codice…"), this, [this] { addPicked(MapNodeKind::Code); });
    addMenu->addAction(tr("Documento…"), this, [this] { addPicked(MapNodeKind::Document); });
    addMenu->addAction(tr("Memo…"), this, [this] { addPicked(MapNodeKind::Memo); });
    addMenu->addAction(tr("Segmento…"), this, [this] { addPicked(MapNodeKind::Segment); });
    menuButton(tr("Aggiungi"), addMenu);

    auto *tplMenu = new QMenu(this);
    tplMenu->addAction(tr("Codice e sottocodici…"), this, [this] { runTemplate(0); });
    tplMenu->addAction(tr("Codice e documenti in cui compare…"), this, [this] { runTemplate(1); });
    tplMenu->addAction(tr("Co-occorrenze dei codici…"), this, [this] { runTemplate(2); });
    menuButton(tr("Modelli"), tplMenu);

    bar->addSeparator();
    m_linkAction = act(tr("Collega"), [this] { startLink(m_linkAction->isChecked()); });
    m_linkAction->setCheckable(true);
    m_linkAction->setToolTip(tr("Clic sul nodo di partenza e poi su quello di arrivo per tracciare una freccia"));
    act(tr("Modifica"), &ConceptMapDialog::editSelection, true)
        ->setToolTip(tr("Modifica il testo del nodo o l'etichetta della freccia (doppio clic)"));
    act(tr("Colore…"), &ConceptMapDialog::applyColor, true);
    act(tr("Spessore…"), &ConceptMapDialog::applyWidth, true);
    auto *lineMenu = new QMenu(this);
    lineMenu->addAction(tr("Continuo"), this, [this] { applyLine(QStringLiteral("solid")); });
    lineMenu->addAction(tr("Tratteggiato"), this, [this] { applyLine(QStringLiteral("dash")); });
    lineMenu->addAction(tr("Punteggiato"), this, [this] { applyLine(QStringLiteral("dot")); });
    menuButton(tr("Tratto"), lineMenu, true);
    QAction *del = act(tr("Elimina selezione"), &ConceptMapDialog::deleteSelection, true);
    del->setShortcut(Qt::Key_Delete);

    bar->addSeparator();
    auto *alignMenu = new QMenu(this);
    const QStringList alignNames = {tr("A sinistra"), tr("A destra"), tr("In alto"), tr("In basso"),
                                    tr("Centra in orizzontale"), tr("Centra in verticale")};
    for (int i = 0; i < alignNames.size(); ++i)
        alignMenu->addAction(alignNames[i], this, [this, i] { align(i); });
    menuButton(tr("Allinea"), alignMenu, true);
    QAction *gridAction = bar->addAction(tr("Griglia"), this, [this](bool on) { m_scene->setGrid(on); });
    gridAction->setCheckable(true);
    gridAction->setToolTip(tr("Mostra la griglia e aggancia i nodi"));
    bar->addAction(tr("Zoom +"), this, [this] { zoom(1.25); });
    bar->addAction(tr("Zoom -"), this, [this] { zoom(0.8); });
    bar->addAction(tr("100%"), this, [this] { m_view->resetTransform(); });
    auto *exportMenu = new QMenu(this);
    exportMenu->addAction(tr("PNG, SVG o PDF…"), this, &ConceptMapDialog::exportDialog);
    menuButton(tr("Esporta"), exportMenu);

    // Menus in the toolbar open on a click.
    for (QAction *a : bar->actions())
        if (a->menu())
            if (auto *button = qobject_cast<QToolButton *>(bar->widgetForAction(a)))
                button->setPopupMode(QToolButton::InstantPopup);

    auto *left = new QVBoxLayout;
    left->addWidget(new QLabel(tr("Mappe")));
    left->addWidget(m_list, 1);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(newButton);
    buttons->addWidget(renameButton);
    buttons->addWidget(deleteButton);
    left->addLayout(buttons);
    auto *right = new QVBoxLayout;
    right->addWidget(bar);
    right->addWidget(m_view, 1);
    auto *layout = new QHBoxLayout(this);
    layout->addLayout(left);
    layout->addLayout(right, 1);

    connect(m_list, &QListWidget::currentRowChanged, this, [this] {
        m_linkAction->setChecked(false);
        startLink(false);
        loadMap();
    });
    connect(m_scene, &MapScene::moved, this, &ConceptMapDialog::saveMovedNodes);
    connect(m_scene, &MapScene::linkRequested, this, &ConceptMapDialog::createLink);
    connect(m_scene, &MapScene::editRequested, this, &ConceptMapDialog::editItem);
    connect(m_scene, &MapScene::elementActivated, this, &ConceptMapDialog::elementActivated);
    connect(m_scene, &MapScene::elementDropped, this,
            [this](int kind, qint64 ref, QPointF at) { addElement(MapNodeKind(kind), ref, {}, at); });
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &ConceptMapDialog::updateActions);
    connect(m_project, &Project::mapsChanged, this, &ConceptMapDialog::scheduleReload);
    connect(m_project, &Project::codesChanged, this, &ConceptMapDialog::scheduleReload);
    connect(m_project, &Project::documentsChanged, this, &ConceptMapDialog::scheduleReload);
    connect(m_project, &Project::memosChanged, this, &ConceptMapDialog::scheduleReload);
    connect(m_project, &Project::segmentsChanged, this, &ConceptMapDialog::scheduleReload);

    reloadList();
}

qint64 ConceptMapDialog::currentMapId() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toLongLong() : 0;
}

void ConceptMapDialog::selectMap(qint64 id)
{
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toLongLong() == id)
            m_list->setCurrentRow(i);
}

qint64 ConceptMapDialog::createMap(const QString &name)
{
    const qint64 id = m_project->addConceptMap(name);
    if (id) {
        reloadList();
        selectMap(id);
    }
    return id;
}

int ConceptMapDialog::nodeItemCount() const
{
    int n = 0;
    for (const QGraphicsItem *i : m_scene->items())
        n += i->type() == kNodeType;
    return n;
}

int ConceptMapDialog::edgeItemCount() const
{
    int n = 0;
    for (const QGraphicsItem *i : m_scene->items())
        n += i->type() == kEdgeType;
    return n;
}

void ConceptMapDialog::reloadList()
{
    const qint64 keep = currentMapId();
    m_list->blockSignals(true);
    m_list->clear();
    for (const ConceptMap &m : m_project->conceptMaps()) {
        auto *item = new QListWidgetItem(m.name);
        item->setData(Qt::UserRole, m.id);
        m_list->addItem(item);
    }
    int row = -1;
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toLongLong() == keep)
            row = i;
    if (row < 0 && m_list->count())
        row = 0;
    m_list->setCurrentRow(row);
    m_list->blockSignals(false);
    loadMap(); // always: the nodes may have changed even when the same map stays selected
}

void ConceptMapDialog::scheduleReload()
{
    if (m_busy || m_reloadPending)
        return;
    m_reloadPending = true;
    // Never rebuild the scene inside the event handler that changed the project.
    QTimer::singleShot(0, this, [this] {
        m_reloadPending = false;
        reloadList();
    });
}

void ConceptMapDialog::loadMap()
{
    m_scene->forgetItems();
    m_scene->clear();
    const qint64 mapId = currentMapId();
    if (mapId) {
        QHash<qint64, NodeItem *> byId;
        for (const MapNode &n : m_project->mapNodes(mapId)) {
            auto *item = new NodeItem(n, ConceptMaps::resolve(*m_project, n));
            item->setSnap(m_scene->grid());
            m_scene->addItem(item);
            byId.insert(n.id, item);
        }
        for (const MapEdge &e : m_project->mapEdges(mapId))
            if (byId.contains(e.fromNode) && byId.contains(e.toNode))
                m_scene->addItem(new EdgeItem(e, byId.value(e.fromNode), byId.value(e.toNode)));
        const QRectF content = m_scene->itemsBoundingRect().adjusted(-300, -200, 300, 200);
        m_scene->setSceneRect(content.united(QRectF(-400, -300, 800, 600)));
    }
    updateActions();
}

void ConceptMapDialog::updateActions()
{
    const bool hasMap = currentMapId() != 0;
    const bool hasSel = !m_scene->selectedItems().isEmpty();
    for (QAction *a : std::as_const(m_needsMap))
        a->setEnabled(hasMap && (!m_needsSelection.contains(a) || hasSel));
}

QPointF ConceptMapDialog::viewCenter() const
{
    return m_view->mapToScene(m_view->viewport()->rect().center());
}

void ConceptMapDialog::saveMovedNodes()
{
    QList<NodeItem *> moved;
    for (QGraphicsItem *i : m_scene->items())
        if (i->type() == kNodeType) {
            auto *n = static_cast<NodeItem *>(i);
            if (std::abs(n->pos().x() - n->node().x) > 0.01 || std::abs(n->pos().y() - n->node().y) > 0.01)
                moved << n;
        }
    if (moved.isEmpty())
        return;
    m_busy = true;
    m_project->beginBatch();
    for (NodeItem *n : moved) {
        MapNode stored = n->node();
        stored.x = n->pos().x();
        stored.y = n->pos().y();
        m_project->updateMapNode(stored);
        n->setStored(stored);
    }
    m_project->commitBatch();
    m_busy = false;
}

qint64 ConceptMapDialog::addElement(MapNodeKind kind, qint64 refId, const QString &label, QPointF at)
{
    const qint64 mapId = currentMapId();
    if (!mapId)
        return 0;
    if (kind != MapNodeKind::Text)
        for (const MapNode &n : m_project->mapNodes(mapId))
            if (n.kind == kind && n.refId == refId) {
                m_scene->clearSelection();
                for (QGraphicsItem *i : m_scene->items())
                    if (i->type() == kNodeType && static_cast<NodeItem *>(i)->id() == n.id)
                        i->setSelected(true);
                return n.id;
            }
    MapNode n;
    n.mapId = mapId;
    n.kind = kind;
    n.refId = refId;
    n.label = label;
    if (kind != MapNodeKind::Text)
        n.label = ConceptMaps::resolve(*m_project, n).caption;
    const QPointF p = at.isNull() ? viewCenter() + QPointF(20, 20) * (nodeItemCount() % 8) : at;
    n.x = std::round(p.x() / kGrid) * kGrid;
    n.y = std::round(p.y() / kGrid) * kGrid;
    return m_project->addMapNode(n);
}

void ConceptMapDialog::addFreeText()
{
    const QString text = Ui::askText(this, tr("Nuovo nodo"), tr("Testo del nodo:"));
    if (!text.isEmpty())
        addElement(MapNodeKind::Text, 0, text);
}

void ConceptMapDialog::addPicked(MapNodeKind kind)
{
    QStringList labels;
    QList<qint64> ids;
    QString title;
    if (kind == MapNodeKind::Code) {
        title = tr("Aggiungi un codice");
        QHash<qint64, Code> byId;
        const QList<Code> codes = m_project->codesInTreeOrder();
        for (const Code &c : codes)
            byId.insert(c.id, c);
        for (const Code &c : codes) {
            int depth = 0;
            for (qint64 p = c.parentId; p && byId.contains(p) && depth < 20; p = byId.value(p).parentId)
                ++depth;
            labels << QString(depth * 2, QLatin1Char(' ')) + c.name;
            ids << c.id;
        }
    } else if (kind == MapNodeKind::Document) {
        title = tr("Aggiungi un documento");
        for (const Document &d : m_project->documents()) {
            labels << d.name;
            ids << d.id;
        }
    } else if (kind == MapNodeKind::Memo) {
        title = tr("Aggiungi un memo");
        for (const Memo &m : m_project->memos()) {
            labels << (m.title.isEmpty() ? m.body.simplified().left(50) : m.title);
            ids << m.id;
        }
    } else {
        title = tr("Aggiungi un segmento");
        for (const Segment &s : m_project->allSegments()) {
            if (ids.size() >= 500)
                break;
            labels << QStringLiteral("%1: %2 [%3]")
                          .arg(m_project->document(s.documentId).name,
                               m_project->documentText(s.documentId).mid(s.start, s.end - s.start).simplified().left(40),
                               m_project->code(s.codeId).name);
            ids << s.id;
        }
    }
    const int i = pickIndex(this, title, tr("Elemento:"), labels);
    if (i >= 0)
        addElement(kind, ids.at(i));
}

void ConceptMapDialog::runTemplate(int which)
{
    const qint64 mapId = currentMapId();
    if (!mapId)
        return;
    qint64 code = 0;
    QList<qint64> codes;
    int minCount = 1;
    if (which < 2) {
        QStringList labels;
        for (const Code &c : m_project->codesInTreeOrder()) {
            labels << c.name;
            codes << c.id;
        }
        const int i = pickIndex(this, tr("Modello"), tr("Codice di partenza:"), labels);
        if (i < 0)
            return;
        code = codes.at(i);
    } else {
        QSet<qint64> used;
        for (const Segment &s : m_project->allSegments())
            used.insert(s.codeId);
        for (const Code &c : m_project->codesInTreeOrder())
            if (used.contains(c.id))
                codes << c.id;
        if (codes.isEmpty()) {
            Ui::showInfo(this, tr("Modello"), tr("Nessun codice è stato ancora applicato."));
            return;
        }
        bool ok = false;
        minCount = QInputDialog::getInt(this, tr("Co-occorrenze"), tr("Numero minimo di passi in comune:"), 1, 1, 100000,
                                        1, &ok);
        if (!ok)
            return;
    }

    QSet<qint64> before;
    double right = 0, top = 0;
    bool any = false;
    for (const MapNode &n : m_project->mapNodes(mapId)) {
        before.insert(n.id);
        right = any ? std::max(right, n.x + 240) : n.x + 240;
        top = any ? std::min(top, n.y) : n.y;
        any = true;
    }
    m_project->beginBatch();
    const int added = which == 0   ? ConceptMaps::addCodeWithSubcodes(*m_project, mapId, code)
                      : which == 1 ? ConceptMaps::addCodeWithDocuments(*m_project, mapId, code)
                                   : ConceptMaps::addCooccurrences(*m_project, mapId, codes, minCount);
    if (added && any) {
        // Put the new nodes to the right of what is already on the map.
        QList<MapNode> fresh;
        double minX = 0, minY = 0;
        for (const MapNode &n : m_project->mapNodes(mapId))
            if (!before.contains(n.id)) {
                minX = fresh.isEmpty() ? n.x : std::min(minX, n.x);
                minY = fresh.isEmpty() ? n.y : std::min(minY, n.y);
                fresh << n;
            }
        for (MapNode n : fresh) {
            n.x += right + 60 - minX;
            n.y += top - minY;
            m_project->updateMapNode(n);
        }
    }
    m_project->commitBatch();
    if (!added)
        Ui::showInfo(this, tr("Modello"), tr("Non c'è nulla da aggiungere."));
}

void ConceptMapDialog::startLink(bool on)
{
    m_scene->setLinking(on);
    m_view->setDragMode(on ? QGraphicsView::NoDrag : QGraphicsView::RubberBandDrag);
}

void ConceptMapDialog::createLink(qint64 fromNode, qint64 toNode)
{
    bool ok = false;
    const QString label = QInputDialog::getText(this, tr("Nuova freccia"), tr("Etichetta (può restare vuota):"),
                                                QLineEdit::Normal, QString(), &ok);
    if (!ok)
        return;
    MapEdge e;
    e.mapId = currentMapId();
    e.fromNode = fromNode;
    e.toNode = toNode;
    e.label = label.trimmed();
    m_project->addMapEdge(e);
}

void ConceptMapDialog::editItem(int itemType, qint64 id)
{
    if (itemType == 1) {
        MapNode n = m_project->mapNode(id);
        if (!n.id || n.kind != MapNodeKind::Text)
            return;
        const QString text = Ui::askText(this, tr("Modifica nodo"), tr("Testo del nodo:"), n.label);
        if (text.isEmpty() || text == n.label)
            return;
        n.label = text;
        m_project->updateMapNode(n);
    } else {
        const QList<MapEdge> edges = m_project->mapEdges(currentMapId());
        for (MapEdge e : edges)
            if (e.id == id) {
                bool ok = false;
                const QString label = QInputDialog::getText(this, tr("Modifica freccia"), tr("Etichetta:"),
                                                            QLineEdit::Normal, e.label, &ok);
                if (!ok)
                    return;
                e.label = label.trimmed();
                m_project->updateMapEdge(e);
                return;
            }
    }
}

void ConceptMapDialog::editSelection()
{
    for (QGraphicsItem *i : m_scene->selectedItems()) {
        if (i->type() == kNodeType && static_cast<NodeItem *>(i)->node().kind == MapNodeKind::Text) {
            editItem(1, static_cast<NodeItem *>(i)->id());
            return;
        }
        if (i->type() == kEdgeType) {
            editItem(2, static_cast<EdgeItem *>(i)->id());
            return;
        }
    }
}

void ConceptMapDialog::deleteSelection()
{
    QList<qint64> nodes, edges;
    for (QGraphicsItem *i : m_scene->selectedItems()) {
        if (i->type() == kNodeType)
            nodes << static_cast<NodeItem *>(i)->id();
        else if (i->type() == kEdgeType)
            edges << static_cast<EdgeItem *>(i)->id();
    }
    if (nodes.isEmpty() && edges.isEmpty())
        return;
    m_project->beginBatch();
    for (qint64 id : edges)
        m_project->deleteMapEdge(id);
    for (qint64 id : nodes)
        m_project->deleteMapNode(id);
    m_project->commitBatch();
}

void ConceptMapDialog::applyColor()
{
    const QColor color = QColorDialog::getColor(Qt::white, this, tr("Colore"));
    if (!color.isValid())
        return;
    m_project->beginBatch();
    for (QGraphicsItem *i : m_scene->selectedItems()) {
        if (i->type() == kNodeType) {
            MapNode n = static_cast<NodeItem *>(i)->node();
            ConceptMaps::Style s = ConceptMaps::styleFromJson(n.styleJson);
            s.color = color;
            n.styleJson = ConceptMaps::styleToJson(s);
            m_project->updateMapNode(n);
        } else if (i->type() == kEdgeType) {
            MapEdge e = static_cast<EdgeItem *>(i)->edge();
            ConceptMaps::Style s = ConceptMaps::styleFromJson(e.styleJson);
            s.color = color;
            e.styleJson = ConceptMaps::styleToJson(s);
            m_project->updateMapEdge(e);
        }
    }
    m_project->commitBatch();
}

void ConceptMapDialog::applyWidth()
{
    bool ok = false;
    const int width =
        QInputDialog::getInt(this, tr("Spessore"), tr("Spessore delle frecce selezionate:"), 2, 1, 12, 1, &ok);
    if (!ok)
        return;
    m_project->beginBatch();
    for (QGraphicsItem *i : m_scene->selectedItems())
        if (i->type() == kEdgeType) {
            MapEdge e = static_cast<EdgeItem *>(i)->edge();
            ConceptMaps::Style s = ConceptMaps::styleFromJson(e.styleJson);
            s.width = width;
            e.styleJson = ConceptMaps::styleToJson(s);
            m_project->updateMapEdge(e);
        }
    m_project->commitBatch();
}

void ConceptMapDialog::applyLine(const QString &line)
{
    m_project->beginBatch();
    for (QGraphicsItem *i : m_scene->selectedItems())
        if (i->type() == kEdgeType) {
            MapEdge e = static_cast<EdgeItem *>(i)->edge();
            ConceptMaps::Style s = ConceptMaps::styleFromJson(e.styleJson);
            s.line = line;
            e.styleJson = ConceptMaps::styleToJson(s);
            m_project->updateMapEdge(e);
        }
    m_project->commitBatch();
}

void ConceptMapDialog::align(int how)
{
    QList<NodeItem *> nodes;
    for (QGraphicsItem *i : m_scene->selectedItems())
        if (i->type() == kNodeType)
            nodes << static_cast<NodeItem *>(i);
    if (nodes.size() < 2)
        return;
    QRectF all = nodes.first()->sceneRectOfNode();
    for (NodeItem *n : nodes)
        all = all.united(n->sceneRectOfNode());
    for (NodeItem *n : nodes) {
        QPointF p = n->pos();
        const QSizeF s = n->size();
        switch (how) {
        case 0: p.setX(all.left()); break;
        case 1: p.setX(all.right() - s.width()); break;
        case 2: p.setY(all.top()); break;
        case 3: p.setY(all.bottom() - s.height()); break;
        case 4: p.setY(all.center().y() - s.height() / 2); break;
        default: p.setX(all.center().x() - s.width() / 2); break;
        }
        n->setPos(p);
    }
    saveMovedNodes();
}

void ConceptMapDialog::newMap()
{
    const QString name = Ui::askText(this, tr("Nuova mappa"), tr("Nome della mappa:"),
                                     tr("Mappa %1").arg(m_project->conceptMaps().size() + 1));
    if (!name.isEmpty())
        createMap(name);
}

void ConceptMapDialog::renameMap()
{
    const qint64 id = currentMapId();
    if (!id)
        return;
    const QString name =
        Ui::askText(this, tr("Rinomina la mappa"), tr("Nome della mappa:"), m_project->conceptMap(id).name);
    if (!name.isEmpty())
        m_project->renameConceptMap(id, name);
}

void ConceptMapDialog::deleteMap()
{
    const qint64 id = currentMapId();
    if (!id)
        return;
    if (Ui::confirm(this, tr("Elimina la mappa"),
                    tr("Eliminare la mappa \"%1\"? L'operazione si può annullare.").arg(m_project->conceptMap(id).name)))
        m_project->deleteConceptMap(id);
}

void ConceptMapDialog::zoom(double factor)
{
    m_view->scale(factor, factor);
}

void ConceptMapDialog::exportDialog()
{
    const QString path = Ui::saveFileName(this, tr("Esporta la mappa"), m_project->name() + tr(" - mappa.png"),
                                          tr("Immagine PNG (*.png);;Immagine SVG (*.svg);;Documento PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    if (!exportTo(path))
        Ui::showError(this, tr("Impossibile salvare la mappa."));
}

bool ConceptMapDialog::exportTo(const QString &path)
{
    QRectF area = m_scene->itemsBoundingRect();
    if (area.isEmpty())
        return false;
    area.adjust(-30, -30, 30, 30);
    const QString ext = QFileInfo(path).suffix().toLower();
    const QList<QGraphicsItem *> selected = m_scene->selectedItems();
    m_scene->clearSelection();
    m_scene->setExporting(true);
    const QRectF target(QPointF(), area.size());
    bool ok = false;
    if (ext == QLatin1String("svg")) {
        QSvgGenerator svg;
        svg.setFileName(path);
        svg.setSize(area.size().toSize());
        svg.setViewBox(target);
        svg.setTitle(windowTitle());
        QPainter painter;
        if (painter.begin(&svg)) {
            painter.setRenderHint(QPainter::Antialiasing);
            painter.fillRect(target, Qt::white);
            m_scene->render(&painter, target, area);
            painter.end();
            ok = true;
        }
    } else if (ext == QLatin1String("pdf")) {
        QPdfWriter pdf(path);
        pdf.setResolution(96);
        pdf.setPageSize(QPageSize(QSizeF(area.width() * 25.4 / 96, area.height() * 25.4 / 96), QPageSize::Millimeter));
        pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter painter;
        if (painter.begin(&pdf)) {
            painter.setRenderHint(QPainter::Antialiasing);
            m_scene->render(&painter, target, area);
            painter.end();
            ok = true;
        }
    } else {
        QImage image((area.size() * 2).toSize(), QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        m_scene->render(&painter, QRectF(QPointF(), QSizeF(image.size())), area);
        painter.end();
        ok = image.save(path);
    }
    m_scene->setExporting(false);
    for (QGraphicsItem *i : selected)
        i->setSelected(true);
    return ok;
}

#include "ConceptMapDialog.moc"
