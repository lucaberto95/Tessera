#include "Theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWindow>

namespace {

const QColor kAccent(0x0E, 0x6B, 0x6B);
const QColor kBar(0xEC, 0xE9, 0xE3);
const QColor kLine(0xDD, 0xD9, 0xD1);
const QColor kInk(0x1F, 0x23, 0x28);

#ifdef Q_OS_MACOS
constexpr bool kMac = true;
#else
constexpr bool kMac = false;
#endif

constexpr int kBarHeight = kMac ? 44 : 40;
constexpr int kEdge = 5; // resize strip around a framed window

// Close / minimize / maximize button: a flat rectangle on Windows and Linux, a coloured dot on macOS.
class CaptionButton : public QAbstractButton
{
public:
    enum Kind { Minimize, Maximize, Close };

    CaptionButton(Kind kind, QWidget *host, QWidget *parent)
        : QAbstractButton(parent), m_kind(kind), m_host(host)
    {
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::ArrowCursor);
        if (kMac)
            setFixedSize(14, 14);
        else
            setFixedSize(46, kBarHeight);
        if (kind == Maximize)
            host->installEventFilter(this);
        connect(this, &QAbstractButton::clicked, host, [this, kind] {
            if (kind == Close)
                m_host->close();
            else if (kind == Minimize)
                m_host->showMinimized();
            else
                m_host->isMaximized() ? m_host->showNormal() : m_host->showMaximized();
        });
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_host && e->type() == QEvent::WindowStateChange)
            update();
        return QAbstractButton::eventFilter(o, e);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const bool hot = underMouse();
        if (kMac) {
            const QColor c = m_kind == Close ? QColor(0xFF, 0x5F, 0x57)
                           : m_kind == Minimize ? QColor(0xFE, 0xBC, 0x2E) : QColor(0x28, 0xC8, 0x40);
            p.setPen(c.darker(115));
            p.setBrush(hot ? c.lighter(110) : c);
            p.drawEllipse(rect().adjusted(0, 0, -1, -1));
            return;
        }
        QColor ink = kInk;
        if (m_kind == Close && hot) {
            p.fillRect(rect(), QColor(0xC4, 0x2B, 0x1C));
            ink = Qt::white;
        } else if (hot) {
            p.fillRect(rect(), kLine);
        }
        p.setPen(QPen(ink, 1.2));
        const QPointF c = QRectF(rect()).center();
        if (m_kind == Minimize) {
            p.drawLine(QPointF(c.x() - 6, c.y()), QPointF(c.x() + 6, c.y()));
        } else if (m_kind == Maximize) {
            p.setBrush(Qt::NoBrush);
            if (m_host->isMaximized()) {
                p.drawRect(QRectF(c.x() - 5, c.y() - 3, 8, 8));
                p.drawLine(QPointF(c.x() - 3, c.y() - 5), QPointF(c.x() + 5, c.y() - 5));
                p.drawLine(QPointF(c.x() + 5, c.y() - 5), QPointF(c.x() + 5, c.y() + 3));
            } else {
                p.drawRect(QRectF(c.x() - 5, c.y() - 5, 10, 10));
            }
        } else {
            p.drawLine(QPointF(c.x() - 5, c.y() - 5), QPointF(c.x() + 5, c.y() + 5));
            p.drawLine(QPointF(c.x() + 5, c.y() - 5), QPointF(c.x() - 5, c.y() + 5));
        }
    }

private:
    Kind m_kind;
    QWidget *m_host;
};

// The three buttons (or just Close for dialogs) laid out the way the platform expects.
QWidget *createControls(QWidget *host, bool minMax, QWidget *parent)
{
    auto *box = new QWidget(parent);
    box->setObjectName(QStringLiteral("CaptionControls"));
    auto *lay = new QHBoxLayout(box);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(kMac ? 8 : 0);
    auto add = [&](CaptionButton::Kind kind, const char *tip) {
        auto *b = new CaptionButton(kind, host, box);
        b->setToolTip(QCoreApplication::translate("TitleBar", tip));
        b->setAccessibleName(b->toolTip());
        lay->addWidget(b);
    };
    if (kMac) {
        add(CaptionButton::Close, "Chiudi");
        if (minMax) {
            add(CaptionButton::Minimize, "Riduci a icona");
            add(CaptionButton::Maximize, "Ingrandisci");
        }
    } else {
        if (minMax) {
            add(CaptionButton::Minimize, "Riduci a icona");
            add(CaptionButton::Maximize, "Ingrandisci");
        }
        add(CaptionButton::Close, "Chiudi");
    }
    return box;
}

QLabel *createLogo(QWidget *host, QWidget *parent)
{
    Q_UNUSED(host);
    auto *logo = new QLabel(parent);
    const qreal dpr = logo->devicePixelRatioF();
    QPixmap pm(QSize(20, 20) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const QColor teal(0x0E, 0x6B, 0x6B), orange(0xD9, 0x8A, 0x4E);
    p.setBrush(teal);
    p.drawRoundedRect(QRectF(1, 1, 8, 8), 2, 2);
    p.drawRoundedRect(QRectF(11, 11, 8, 8), 2, 2);
    p.setBrush(orange);
    p.drawRoundedRect(QRectF(11, 1, 8, 8), 2, 2);
    p.drawRoundedRect(QRectF(1, 11, 8, 8), 2, 2);
    p.end();
    logo->setPixmap(pm);
    logo->setFixedSize(20, 20);
    return logo;
}

void startMove(QWidget *w)
{
    if (QWindow *h = w->windowHandle())
        h->startSystemMove();
}

void toggleMaximize(QWidget *w)
{
    w->isMaximized() ? w->showNormal() : w->showMaximized();
}

// The bar drawn at the top of a framed window: logo, title, window buttons. Drags the window.
class TitleBar : public QWidget
{
public:
    TitleBar(QWidget *host, bool minMax) : QWidget(host), m_host(host), m_minMax(minMax)
    {
        setObjectName(QStringLiteral("TitleBar"));
        setAttribute(Qt::WA_StyledBackground);
        setFixedHeight(kBarHeight);
        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(kMac ? 16 : 12, 0, kMac ? 16 : 0, 0);
        lay->setSpacing(10);
        m_title = new QLabel(host->windowTitle(), this);
        m_title->setObjectName(QStringLiteral("TitleBarText"));
        m_title->setTextFormat(Qt::PlainText);
        QWidget *controls = createControls(host, minMax, this);
        if (kMac) {
            lay->addWidget(controls);
            lay->addStretch(1);
            lay->addWidget(m_title);
            lay->addStretch(1);
            lay->addSpacing(controls->sizeHint().width());
        } else {
            lay->addWidget(createLogo(host, this));
            lay->addWidget(m_title, 1);
            lay->addWidget(controls);
        }
    }

    void setTitle(const QString &t) { m_title->setText(t); }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton)
            startMove(m_host);
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && m_minMax)
            toggleMaximize(m_host);
    }

private:
    QWidget *m_host;
    bool m_minMax;
    QLabel *m_title;
};

// Per-window helper: keeps the bar in place and gives the window edges that resize it.
class Frame : public QObject
{
public:
    Frame(QWidget *w, bool isMain) : QObject(w), m_w(w), m_main(isMain)
    {
        w->setWindowFlag(Qt::FramelessWindowHint, true);
        w->setMouseTracking(true);
        w->installEventFilter(this);

        QMenuBar *mb = isMain && !kMac ? qobject_cast<QMainWindow *>(w)->menuBar() : nullptr;
        if (mb) {
            // Windows/Linux main window: the menu bar is the title bar.
            mb->setFixedHeight(kBarHeight);
            auto *left = new QWidget(mb);
            auto *ll = new QHBoxLayout(left);
            ll->setContentsMargins(14, 0, 8, 0);
            ll->setSpacing(10);
            ll->addWidget(createLogo(w, left));
            auto *name = new QLabel(QStringLiteral("Tessera"), left);
            name->setObjectName(QStringLiteral("TitleBarName"));
            ll->addWidget(name);
            mb->setCornerWidget(left, Qt::TopLeftCorner);
            mb->setCornerWidget(createControls(w, true, mb), Qt::TopRightCorner);
            // Project name, centred on the bar and kept clear of the menus.
            m_menuTitle = new QLabel(w->windowTitle(), mb);
            m_menuTitle->setObjectName(QStringLiteral("TitleBarText"));
            m_menuTitle->setTextFormat(Qt::PlainText);
            m_menuTitle->setAlignment(Qt::AlignCenter);
            m_menuTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
            mb->installEventFilter(this);
            m_menuBar = mb;
        } else {
            m_bar = new TitleBar(w, !qobject_cast<QDialog *>(w));
        }
        applyMargins();
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == m_menuBar) {
            if (e->type() == QEvent::Resize || e->type() == QEvent::LayoutRequest || e->type() == QEvent::Show)
                placeMenuTitle();
            if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonDblClick) {
                auto *me = static_cast<QMouseEvent *>(e);
                if (me->button() == Qt::LeftButton && !m_menuBar->actionAt(me->position().toPoint())
                    && m_menuBar->childAt(me->position().toPoint()) == nullptr) {
                    e->type() == QEvent::MouseButtonPress ? startMove(m_w) : toggleMaximize(m_w);
                    return true;
                }
            }
            return false;
        }
        switch (e->type()) {
        case QEvent::Resize:
            if (m_bar)
                m_bar->setGeometry(0, 0, m_w->width(), kBarHeight);
            break;
        case QEvent::WindowTitleChange:
            if (m_bar)
                m_bar->setTitle(m_w->windowTitle());
            if (m_menuTitle) {
                m_menuTitle->setText(m_w->windowTitle());
                placeMenuTitle();
            }
            break;
        case QEvent::WindowStateChange:
            applyMargins();
            break;
        case QEvent::MouseMove:
            m_w->setCursor(cursorFor(edgesAt(static_cast<QMouseEvent *>(e)->position().toPoint())));
            break;
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(e);
            const Qt::Edges edges = edgesAt(me->position().toPoint());
            if (me->button() == Qt::LeftButton && edges && m_w->windowHandle()) {
                m_w->windowHandle()->startSystemResize(edges);
                return true;
            }
            break;
        }
        default:
            break;
        }
        return QObject::eventFilter(o, e);
    }

private:
    void placeMenuTitle()
    {
        // Between the last menu and the window buttons, centred when there is room.
        const QList<QAction *> acts = m_menuBar->actions();
        int left = 0;
        if (!acts.isEmpty())
            left = m_menuBar->actionGeometry(acts.last()).right() + 12;
        const int right = m_menuBar->width() - 3 * 46 - 12;
        const int w = qMax(0, qMin(right - left, m_menuTitle->fontMetrics().horizontalAdvance(m_menuTitle->text()) + 24));
        const int x = qBound(left, (m_menuBar->width() - w) / 2, qMax(left, right - w));
        m_menuTitle->setGeometry(x, 0, w, kBarHeight);
    }

    bool resizable() const { return !m_w->isMaximized() && !m_w->isFullScreen(); }

    void applyMargins()
    {
        const int side = resizable() ? kEdge : 0;
        const int top = m_menuBar ? 0 : kBarHeight;
        m_w->setContentsMargins(side, top, side, side);
    }

    Qt::Edges edgesAt(const QPoint &p) const
    {
        Qt::Edges e;
        if (!resizable() || m_w->minimumSize() == m_w->maximumSize())
            return e;
        if (p.x() < kEdge)
            e |= Qt::LeftEdge;
        if (p.x() >= m_w->width() - kEdge)
            e |= Qt::RightEdge;
        if (p.y() >= m_w->height() - kEdge)
            e |= Qt::BottomEdge;
        return e;
    }

    static Qt::CursorShape cursorFor(Qt::Edges e)
    {
        if (e == (Qt::LeftEdge | Qt::BottomEdge))
            return Qt::SizeBDiagCursor;
        if (e == (Qt::RightEdge | Qt::BottomEdge))
            return Qt::SizeFDiagCursor;
        if (e & Qt::BottomEdge)
            return Qt::SizeVerCursor;
        if (e)
            return Qt::SizeHorCursor;
        return Qt::ArrowCursor;
    }

    QWidget *m_w;
    bool m_main;
    TitleBar *m_bar = nullptr;
    QMenuBar *m_menuBar = nullptr;
    QLabel *m_menuTitle = nullptr;
};

// Dialogs the application does not draw itself (system file/colour pickers, message boxes).
bool isSystemDialog(QWidget *w)
{
    for (const char *name : {"QMessageBox", "QFileDialog", "QInputDialog", "QColorDialog", "QFontDialog",
                             "QProgressDialog", "QWizard", "QErrorMessage"})
        if (w->inherits(name))
            return true;
    return false;
}

class AutoFrame : public QObject
{
protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::Polish && o->isWidgetType()) {
            auto *w = static_cast<QWidget *>(o);
            if (w->isWindow() && qobject_cast<QDialog *>(w) && !isSystemDialog(w))
                Ui::frameWindow(w);
        }
        return false;
    }
};

const char *kStyleSheet = R"(
QMainWindow { background: #ECE9E3; }
QDialog { background: #F4F2EE; }
#TitleBar { background: #ECE9E3; border-bottom: 1px solid #DDD9D1; }
#TitleBarText { color: #1F2328; font-size: 13px; font-weight: 600; }
QMenuBar #TitleBarText { color: #59626B; font-weight: 400; }
#TitleBarName { color: #1F2328; font-size: 14px; font-weight: 700; }
QMenuBar { background: #ECE9E3; border-bottom: 1px solid #DDD9D1; spacing: 2px; }
QMenuBar::item { padding: 5px 10px; margin: 6px 1px; background: transparent; border-radius: 6px; }
QMenuBar::item:selected { background: #DDD9D1; }
QMenu { background: #FFFFFF; border: 1px solid #DDD9D1; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 20px; border-radius: 6px; }
QMenu::item:selected { background: #DCEFEE; color: #0A4F4F; }
QMenu::separator { height: 1px; background: #EEEBE5; margin: 4px 8px; }
QToolBar { background: #FBFAF8; border: 0; border-bottom: 1px solid #DDD9D1; padding: 8px 14px; spacing: 8px; }
QToolBar::separator { background: #DDD9D1; width: 1px; margin: 4px 6px; }
QToolBar QToolButton { padding: 8px 12px; background: #FFFFFF; border: 1px solid #DDD9D1; border-radius: 8px; }
QToolBar QToolButton:hover { border-color: #0E6B6B; }
QToolBar QToolButton:pressed { background: #EEEBE5; }
QToolBar QToolButton#PrimaryAction { background: #0E6B6B; color: #FFFFFF; font-weight: 600; }
QToolBar QToolButton#PrimaryAction:hover { background: #0B5A5A; border-color: #0B5A5A; }
QPushButton { background: #FFFFFF; border: 1px solid #DDD9D1; border-radius: 8px; padding: 7px 16px; min-height: 18px; }
QPushButton:hover { border-color: #0E6B6B; }
QPushButton:disabled { color: #8A9099; }
QPushButton:default { background: #0E6B6B; border-color: #0E6B6B; color: #FFFFFF; font-weight: 600; }
QPushButton:default:hover { background: #0B5A5A; }
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: #FFFFFF; border: 1px solid #DDD9D1; border-radius: 8px; padding: 6px 10px;
    selection-background-color: #0E6B6B; selection-color: #FFFFFF; }
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border: 1px solid #0E6B6B; }
QComboBox QAbstractItemView { background: #FFFFFF; border: 1px solid #DDD9D1; selection-background-color: #DCEFEE;
    selection-color: #0A4F4F; }
QTreeView, QListView, QTableView, QTableWidget { background: #FFFFFF; alternate-background-color: #FAF9F6;
    border: 1px solid #DDD9D1; border-radius: 8px; selection-background-color: #DCEFEE; selection-color: #0A4F4F; }
QHeaderView::section { background: #F4F2EE; color: #59626B; border: 0; border-bottom: 1px solid #DDD9D1;
    padding: 6px 8px; font-weight: 600; }
QTabWidget::pane { border: 1px solid #DDD9D1; border-radius: 8px; background: #FFFFFF; top: -1px; }
QTabBar::tab { padding: 8px 16px; border: 0; background: transparent; color: #59626B; }
QTabBar::tab:selected { color: #0A4F4F; border-bottom: 2px solid #0E6B6B; font-weight: 600; }
QGroupBox { border: 1px solid #DDD9D1; border-radius: 10px; margin-top: 14px; padding-top: 10px; background: #FFFFFF; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; color: #59626B; }
QStatusBar { background: #ECE9E3; border-top: 1px solid #DDD9D1; color: #59626B; }
QSplitter::handle { background: #DDD9D1; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }
QScrollBar:vertical { background: transparent; width: 12px; margin: 0; }
QScrollBar::handle:vertical { background: #C9C4BA; border-radius: 4px; min-height: 28px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 12px; margin: 0; }
QScrollBar::handle:horizontal { background: #C9C4BA; border-radius: 4px; min-width: 28px; margin: 2px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QToolTip { background: #1F2328; color: #FFFFFF; border: 0; padding: 4px 8px; }
)";

} // namespace

namespace Ui {

void applyTheme(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);
    QPalette p;
    p.setColor(QPalette::Window, QColor(0xF4, 0xF2, 0xEE));
    p.setColor(QPalette::WindowText, kInk);
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, QColor(0xFA, 0xF9, 0xF6));
    p.setColor(QPalette::Text, kInk);
    p.setColor(QPalette::Button, Qt::white);
    p.setColor(QPalette::ButtonText, kInk);
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(0x6B, 0x73, 0x7B));
    p.setColor(QPalette::Mid, QColor(0x6B, 0x73, 0x7B)); // "palette(mid)" is the muted text colour
    p.setColor(QPalette::ToolTipBase, kInk);
    p.setColor(QPalette::ToolTipText, Qt::white);
    app.setPalette(p);
    QFont f = app.font();
    f.setPointSizeF(9.5);
    app.setFont(f);
    app.setStyleSheet(QString::fromUtf8(kStyleSheet));
}

void frameWindow(QWidget *w)
{
    if (!w || w->property("tesseraFrame").toBool())
        return;
    w->setProperty("tesseraFrame", true);
    new Frame(w, qobject_cast<QMainWindow *>(w) != nullptr);
}

void installAutoFrame(QApplication &app)
{
    app.installEventFilter(new AutoFrame);
}

} // namespace Ui
