#include "ui/MediaPlayerWidget.h"
#include "core/Media.h"
#include "core/Transcripts.h"
#include "ui/UiUtils.h"

#include <QAudioOutput>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QMediaPlayer>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

#include <algorithm>

namespace {
constexpr int BarHeight = 16;
constexpr int LaneHeight = 7;
constexpr int Margin = 2;
constexpr int DragThreshold = 4;
const char *kRewindKey = "media/rewindSeconds";
} // namespace

// ---------------------------------------------------------------- MediaTimeline

MediaTimeline::MediaTimeline(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

QSize MediaTimeline::minimumSizeHint() const
{
    return {120, BarHeight + 2 * Margin + m_laneCount * (LaneHeight + 1) + (m_laneCount ? 2 : 0)};
}

QSize MediaTimeline::sizeHint() const
{
    return {400, minimumSizeHint().height()};
}

QRect MediaTimeline::barRect() const
{
    return QRect(Margin, Margin, qMax(1, width() - 2 * Margin), BarHeight);
}

void MediaTimeline::setDuration(qint64 ms)
{
    m_duration = qMax<qint64>(0, ms);
    update();
}

void MediaTimeline::setPosition(qint64 ms)
{
    m_position = qBound<qint64>(0, ms, m_duration > 0 ? m_duration : ms);
    update();
}

void MediaTimeline::setBands(const QList<Band> &bands)
{
    m_bands = bands;
    // Greedy rows: a band goes to the first row where it does not overlap the previous one.
    QList<int> order(m_bands.size());
    for (int i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return m_bands[a].startMs < m_bands[b].startMs; });
    m_lanes = QList<int>(m_bands.size(), 0);
    QList<qint64> laneEnd;
    for (int idx : std::as_const(order)) {
        int lane = 0;
        while (lane < laneEnd.size() && laneEnd[lane] > m_bands[idx].startMs)
            ++lane;
        if (lane == laneEnd.size())
            laneEnd << 0;
        laneEnd[lane] = m_bands[idx].endMs;
        m_lanes[idx] = lane;
    }
    const int before = m_laneCount;
    m_laneCount = int(laneEnd.size());
    if (m_laneCount != before) {
        updateGeometry();
        setFixedHeight(minimumSizeHint().height());
    }
    update();
}

void MediaTimeline::setSelection(qint64 startMs, qint64 endMs)
{
    if (startMs == m_selStart && endMs == m_selEnd)
        return;
    m_selStart = qMax<qint64>(0, startMs);
    m_selEnd = endMs;
    update();
    emit selectionChanged();
}

qint64 MediaTimeline::msAtX(int x) const
{
    const QRect bar = barRect();
    if (m_duration <= 0 || bar.width() <= 0)
        return 0;
    const double f = double(x - bar.left()) / bar.width();
    return qBound<qint64>(0, qint64(f * double(m_duration)), m_duration);
}

int MediaTimeline::xAtMs(qint64 ms) const
{
    const QRect bar = barRect();
    if (m_duration <= 0)
        return bar.left();
    return bar.left() + int(double(bar.width()) * double(ms) / double(m_duration));
}

QList<qint64> MediaTimeline::bandsAt(qint64 ms) const
{
    QList<qint64> out;
    for (int i = int(m_bands.size()) - 1; i >= 0; --i)
        if (ms >= m_bands[i].startMs && ms <= m_bands[i].endMs)
            out << m_bands[i].id;
    return out;
}

void MediaTimeline::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect bar = barRect();
    const QPalette &pal = palette();
    p.setPen(Qt::NoPen);
    p.setBrush(pal.color(QPalette::Mid).lighter(135));
    p.drawRoundedRect(bar, 3, 3);
    if (m_duration > 0) {
        p.setBrush(pal.color(QPalette::Highlight).lighter(150));
        p.drawRoundedRect(QRect(bar.left(), bar.top(), xAtMs(m_position) - bar.left(), bar.height()), 3, 3);
    }
    if (hasSelection() && m_duration > 0) {
        QColor c = pal.color(QPalette::Highlight);
        c.setAlpha(110);
        const int x1 = xAtMs(m_selStart), x2 = xAtMs(m_selEnd);
        p.setBrush(c);
        p.setPen(pal.color(QPalette::Highlight));
        p.drawRect(QRect(x1, bar.top(), qMax(1, x2 - x1), bar.height()));
        p.setPen(Qt::NoPen);
    }
    for (int i = 0; i < m_bands.size() && m_duration > 0; ++i) {
        const Band &b = m_bands[i];
        QColor c = b.color.isValid() ? b.color : QColor(Qt::gray);
        const int x1 = xAtMs(b.startMs), x2 = xAtMs(b.endMs);
        const int y = bar.bottom() + 3 + m_lanes.value(i) * (LaneHeight + 1);
        p.setBrush(c);
        p.drawRoundedRect(QRect(x1, y, qMax(3, x2 - x1), LaneHeight), 2, 2);
    }
    if (m_duration > 0) {
        p.setPen(QPen(pal.color(QPalette::WindowText), 2));
        const int x = xAtMs(m_position);
        p.drawLine(x, bar.top() - 1, x, bar.bottom() + 1);
    }
}

void MediaTimeline::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_duration <= 0)
        return;
    m_pressed = true;
    m_dragging = false;
    m_pressX = int(event->position().x());
}

void MediaTimeline::mouseMoveEvent(QMouseEvent *event)
{
    const int x = int(event->position().x());
    if (!m_pressed)
        return;
    if (!m_dragging && qAbs(x - m_pressX) < DragThreshold)
        return;
    m_dragging = true;
    const qint64 a = msAtX(m_pressX), b = msAtX(x);
    setSelection(qMin(a, b), qMax(a, b));
}

void MediaTimeline::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_pressed)
        return;
    m_pressed = false;
    if (!m_dragging)
        emit seekRequested(msAtX(int(event->position().x())));
    m_dragging = false;
}

void MediaTimeline::contextMenuEvent(QContextMenuEvent *event)
{
    const QList<qint64> ids = bandsAt(msAtX(event->pos().x()));
    if (!ids.isEmpty())
        emit bandContextMenu(ids.first(), event->globalPos());
}

bool MediaTimeline::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(e);
        const QList<qint64> ids = bandsAt(msAtX(help->pos().x()));
        QStringList lines;
        for (qint64 id : ids)
            for (const Band &b : std::as_const(m_bands))
                if (b.id == id && !b.tip.isEmpty())
                    lines << b.tip;
        if (lines.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(help->globalPos(), Ui::plainToolTip(lines.join(QLatin1Char('\n'))), this);
        return true;
    }
    return QWidget::event(e);
}

// ---------------------------------------------------------------- MediaPlayerWidget

MediaPlayerWidget::MediaPlayerWidget(QWidget *parent)
    : QWidget(parent)
    , m_filler(new QWidget)
    , m_timeline(new MediaTimeline)
    , m_playButton(new QToolButton)
    , m_backButton(new QToolButton)
    , m_forwardButton(new QToolButton)
    , m_rewindSpin(new QSpinBox)
    , m_speedBox(new QComboBox)
    , m_timeLabel(new QLabel)
    , m_marks(new QWidget)
    , m_selectionLabel(new QLabel)
    , m_playSelection(new QPushButton(tr("Riproduci intervallo")))
    , m_codeSelection(new QPushButton(tr("Codifica intervallo")))
    , m_problemBar(new QWidget)
    , m_problemLabel(new QLabel)
{
    setAcceptDrops(true);
    m_filler->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_problemLabel->setTextFormat(Qt::PlainText);
    m_problemLabel->setWordWrap(true);
    auto *relink = new QPushButton(tr("Ricollega il file…"));
    relink->setAutoDefault(false);
    connect(relink, &QPushButton::clicked, this, &MediaPlayerWidget::relinkRequested);
    auto *problemLayout = new QHBoxLayout(m_problemBar);
    problemLayout->setContentsMargins(0, 0, 0, 0);
    problemLayout->addWidget(m_problemLabel, 1);
    problemLayout->addWidget(relink);
    m_problemBar->setStyleSheet(QStringLiteral("QLabel { color: #b71c1c }"));
    m_problemBar->hide();

    QStyle *st = style();
    m_playButton->setIcon(st->standardIcon(QStyle::SP_MediaPlay));
    m_playButton->setToolTip(tr("Riproduci / pausa (F9)"));
    m_backButton->setIcon(st->standardIcon(QStyle::SP_MediaSeekBackward));
    m_forwardButton->setIcon(st->standardIcon(QStyle::SP_MediaSeekForward));
    m_rewindSpin->setRange(1, 120);
    m_rewindSpin->setSuffix(tr(" s"));
    m_rewindSpin->setToolTip(tr("Secondi di salto indietro e avanti"));
    m_rewindSpin->setValue(QSettings().value(QLatin1String(kRewindKey), 5).toInt());
    auto updateJumpTips = [this] {
        m_backButton->setToolTip(tr("Indietro di %1 secondi (F7)").arg(m_rewindSpin->value()));
        m_forwardButton->setToolTip(tr("Avanti di %1 secondi").arg(m_rewindSpin->value()));
    };
    updateJumpTips();
    connect(m_rewindSpin, &QSpinBox::valueChanged, this, [this, updateJumpTips](int v) {
        QSettings().setValue(QLatin1String(kRewindKey), v);
        updateJumpTips();
    });
    for (double rate : {0.5, 0.75, 1.0, 1.25, 1.5, 2.0})
        m_speedBox->addItem(QLocale().toString(rate, 'g', 3) + QStringLiteral("×"), rate);
    m_speedBox->setCurrentIndex(2);
    m_speedBox->setToolTip(tr("Velocità di riproduzione"));
    m_timeLabel->setMinimumWidth(110);
    m_timeLabel->setStyleSheet(QStringLiteral("font-family: Consolas, monospace"));

    connect(m_playButton, &QToolButton::clicked, this, &MediaPlayerWidget::togglePlay);
    connect(m_backButton, &QToolButton::clicked, this, &MediaPlayerWidget::rewind);
    connect(m_forwardButton, &QToolButton::clicked, this, [this] { skip(m_rewindSpin->value()); });
    connect(m_speedBox, &QComboBox::currentIndexChanged, this, [this] { setSpeed(speed()); });
    connect(m_timeline, &MediaTimeline::seekRequested, this, &MediaPlayerWidget::seek);
    connect(m_timeline, &MediaTimeline::selectionChanged, this, [this] {
        updateSelectionLabel();
        emit selectionChanged();
    });
    connect(m_timeline, &MediaTimeline::bandContextMenu, this, &MediaPlayerWidget::bandContextMenu);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(4);
    controls->addWidget(m_backButton);
    controls->addWidget(m_playButton);
    controls->addWidget(m_forwardButton);
    controls->addWidget(m_rewindSpin);
    controls->addWidget(m_speedBox);
    controls->addSpacing(8);
    controls->addWidget(m_timeLabel);
    controls->addStretch(1);

    auto *in = new QPushButton(tr("Inizio qui"));
    in->setToolTip(tr("Imposta l'inizio dell'intervallo alla posizione corrente"));
    auto *out = new QPushButton(tr("Fine qui"));
    out->setToolTip(tr("Imposta la fine dell'intervallo alla posizione corrente"));
    for (QPushButton *b : {in, out, m_playSelection, m_codeSelection})
        b->setAutoDefault(false);
    m_codeSelection->setToolTip(tr("Codifica l'intervallo con il codice selezionato (Ctrl+E)"));
    connect(in, &QPushButton::clicked, this, [this] {
        const qint64 start = m_position;
        setSelection(start, qMax(start, m_timeline->selectionEnd()));
    });
    connect(out, &QPushButton::clicked, this, [this] {
        if (m_position <= 0)
            return;
        const qint64 start = m_timeline->selectionStart() < m_position ? m_timeline->selectionStart() : 0;
        setSelection(start, m_position);
    });
    connect(m_playSelection, &QPushButton::clicked, this,
            [this] { playRange(m_timeline->selectionStart(), m_timeline->selectionEnd()); });
    connect(m_codeSelection, &QPushButton::clicked, this, &MediaPlayerWidget::codeSelectionRequested);
    auto *marks = new QHBoxLayout(m_marks);
    marks->setContentsMargins(0, 0, 0, 0);
    marks->addWidget(in);
    marks->addWidget(out);
    marks->addWidget(m_selectionLabel, 1);
    marks->addWidget(m_playSelection);
    marks->addWidget(m_codeSelection);
    m_selectionLabel->setStyleSheet(QStringLiteral("color: palette(mid)"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);
    layout->addWidget(m_problemBar);
    layout->addWidget(m_filler, 1);
    layout->addLayout(controls);
    layout->addWidget(m_timeline);
    layout->addWidget(m_marks);

    updateTimeLabel();
    updateSelectionLabel();
    updateButtons();
}

void MediaPlayerWidget::ensurePlayer()
{
    if (m_player)
        return;
    m_player = new QMediaPlayer(this);
    m_audio = new QAudioOutput(this);
    m_player->setAudioOutput(m_audio);
    connect(m_player, &QMediaPlayer::positionChanged, this, &MediaPlayerWidget::handlePosition);
    connect(m_player, &QMediaPlayer::durationChanged, this, &MediaPlayerWidget::handleDuration);
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this] {
        updateButtons();
        emit playingChanged(isPlaying());
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        showProblem(tr("Impossibile riprodurre la registrazione: %1").arg(message));
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::InvalidMedia && m_problem.isEmpty())
            showProblem(tr("Impossibile riprodurre la registrazione: formato non supportato."));
    });
}

bool MediaPlayerWidget::setMedia(const QString &path, const QString &storedPath)
{
    if (m_loaded && path == m_path)
        return true;
    clear();
    if (path.isEmpty()) {
        showProblem(storedPath.isEmpty() ? tr("Nessuna registrazione associata.")
                                         : tr("Registrazione non trovata: %1").arg(storedPath));
        return false;
    }
    ensurePlayer();
    if (m_video)
        m_player->setVideoOutput(m_video);
    m_path = path;
    m_loaded = true;
    m_player->setSource(QUrl::fromLocalFile(path));
    showProblem({});
    updateButtons();
    return true;
}

void MediaPlayerWidget::clear()
{
    if (m_player) {
        m_player->stop();
        m_player->setSource(QUrl());
    }
    m_path.clear();
    m_loaded = false;
    m_position = 0;
    m_rangeEnd = -1;
    m_timeline->setDuration(0);
    m_timeline->setPosition(0);
    m_timeline->setSelection(0, 0);
    m_timeline->setBands({});
    showProblem({});
    updateTimeLabel();
    updateButtons();
}

void MediaPlayerWidget::showProblem(const QString &text)
{
    m_problem = text;
    m_problemLabel->setText(text);
    m_problemBar->setVisible(!text.isEmpty());
}

void MediaPlayerWidget::setCompact(bool compact)
{
    m_compact = compact;
    m_marks->setVisible(!compact);
    m_filler->setVisible(!compact && !m_videoVisible);
    if (m_video)
        m_video->setVisible(!compact && m_videoVisible);
}

void MediaPlayerWidget::setVideoVisible(bool visible)
{
    m_videoVisible = visible;
    if (visible && !m_video) {
        m_video = new QVideoWidget;
        m_video->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        m_video->setMinimumHeight(160);
        static_cast<QVBoxLayout *>(layout())->insertWidget(1, m_video, 1);
        if (m_player)
            m_player->setVideoOutput(m_video);
    }
    if (m_video)
        m_video->setVisible(visible && !m_compact);
    m_filler->setVisible(!m_compact && !visible);
}

bool MediaPlayerWidget::isPlaying() const
{
    return m_player && m_player->playbackState() == QMediaPlayer::PlayingState;
}

double MediaPlayerWidget::speed() const
{
    return m_speedBox->currentData().toDouble();
}

int MediaPlayerWidget::rewindSeconds() const
{
    return m_rewindSpin->value();
}

void MediaPlayerWidget::setRewindSeconds(int seconds)
{
    m_rewindSpin->setValue(seconds);
}

void MediaPlayerWidget::seek(qint64 ms)
{
    if (!m_loaded)
        return;
    m_rangeEnd = -1;
    ms = duration() > 0 ? qBound<qint64>(0, ms, duration()) : qMax<qint64>(0, ms); // the length may not be known yet
    if (m_player)
        m_player->setPosition(ms);
    handlePosition(ms);
}

void MediaPlayerWidget::play()
{
    if (m_loaded && m_player)
        m_player->play();
}

void MediaPlayerWidget::pause()
{
    if (m_player)
        m_player->pause();
}

void MediaPlayerWidget::togglePlay()
{
    if (isPlaying())
        pause();
    else
        play();
}

void MediaPlayerWidget::skip(int seconds)
{
    seek(m_position + qint64(seconds) * 1000);
}

void MediaPlayerWidget::setSpeed(double rate)
{
    const int idx = m_speedBox->findData(rate);
    if (idx >= 0 && idx != m_speedBox->currentIndex()) {
        const QSignalBlocker block(m_speedBox);
        m_speedBox->setCurrentIndex(idx);
    }
    if (m_player)
        m_player->setPlaybackRate(rate);
}

void MediaPlayerWidget::playRange(qint64 startMs, qint64 endMs)
{
    if (!m_loaded)
        return;
    seek(startMs);
    m_rangeEnd = endMs > startMs ? endMs : -1;
    m_rangeEntered = false;
    play();
}

void MediaPlayerWidget::setSelection(qint64 startMs, qint64 endMs)
{
    m_timeline->setSelection(startMs, endMs);
}

void MediaPlayerWidget::handlePosition(qint64 ms)
{
    m_position = ms;
    m_timeline->setPosition(ms);
    updateTimeLabel();
    if (m_rangeEnd >= 0) {
        // Positions reported before the seek took effect may still be the old ones: stop only after the
        // playback has been seen inside the range.
        if (ms < m_rangeEnd)
            m_rangeEntered = true;
        else if (m_rangeEntered) {
            m_rangeEnd = -1;
            pause();
        }
    }
    emit positionChanged(ms);
}

void MediaPlayerWidget::handleDuration(qint64 ms)
{
    m_timeline->setDuration(ms);
    updateTimeLabel();
}

void MediaPlayerWidget::updateTimeLabel()
{
    m_timeLabel->setText(Transcripts::formatTime(m_position) + QStringLiteral(" / ")
                         + Transcripts::formatTime(duration()));
}

void MediaPlayerWidget::updateSelectionLabel()
{
    const bool has = m_timeline->hasSelection();
    m_selectionLabel->setText(has ? tr("Intervallo: %1").arg(Media::formatRange(m_timeline->selectionStart(),
                                                                                m_timeline->selectionEnd()))
                                  : tr("Trascina sulla linea del tempo o usa \"Inizio qui\" / \"Fine qui\" per scegliere un intervallo."));
    m_playSelection->setEnabled(has && m_loaded);
    m_codeSelection->setEnabled(has);
}

void MediaPlayerWidget::updateButtons()
{
    const bool playing = isPlaying();
    m_playButton->setIcon(style()->standardIcon(playing ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay));
    for (QWidget *w : {static_cast<QWidget *>(m_playButton), static_cast<QWidget *>(m_backButton),
                       static_cast<QWidget *>(m_forwardButton), static_cast<QWidget *>(m_speedBox),
                       static_cast<QWidget *>(m_timeline)})
        w->setEnabled(m_loaded);
    updateSelectionLabel();
}

void MediaPlayerWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType))) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
}

void MediaPlayerWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType))) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
    }
}

void MediaPlayerWidget::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasFormat(QString::fromLatin1(Ui::CodeMimeType)))
        return;
    event->setDropAction(Qt::CopyAction);
    event->accept();
    emit codeDropped(event->mimeData()->data(QString::fromLatin1(Ui::CodeMimeType)).toLongLong());
}
