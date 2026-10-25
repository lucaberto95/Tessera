#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

class QAudioOutput;
class QComboBox;
class QLabel;
class QMediaPlayer;
class QPushButton;
class QSpinBox;
class QToolButton;
class QVideoWidget;

// Timeline of a recording (issue #28): the playhead, the selected interval and the coded intervals as coloured
// bands under the bar. A click moves the playhead, a drag selects an interval, a right click on a band asks
// for its menu. It only reports what the user does; MediaPlayerWidget and DocumentView act on it.
class MediaTimeline : public QWidget {
    Q_OBJECT
public:
    struct Band {
        qint64 id = 0; // segment id
        qint64 startMs = 0;
        qint64 endMs = 0;
        QColor color;
        QString tip; // plain text
    };

    explicit MediaTimeline(QWidget *parent = nullptr);

    void setDuration(qint64 ms);
    qint64 duration() const { return m_duration; }
    void setPosition(qint64 ms);
    void setBands(const QList<Band> &bands);
    // Interval being selected (endMs <= startMs: none).
    void setSelection(qint64 startMs, qint64 endMs);
    qint64 selectionStart() const { return m_selStart; }
    qint64 selectionEnd() const { return m_selEnd; }
    bool hasSelection() const { return m_selEnd > m_selStart; }

    qint64 msAtX(int x) const;
    int xAtMs(qint64 ms) const;
    // Bands at the time, topmost first.
    QList<qint64> bandsAt(qint64 ms) const;
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

signals:
    void seekRequested(qint64 ms);
    void selectionChanged();
    void bandContextMenu(qint64 segmentId, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool event(QEvent *event) override;

private:
    QRect barRect() const;
    int laneOf(int index) const;

    qint64 m_duration = 0;
    qint64 m_position = 0;
    QList<Band> m_bands;
    QList<int> m_lanes; // row of each band, so that overlapping intervals do not hide each other
    int m_laneCount = 0;
    qint64 m_selStart = 0;
    qint64 m_selEnd = 0;
    bool m_pressed = false;
    bool m_dragging = false;
    int m_pressX = 0;
};

// Player of audio and video recordings (Qt Multimedia): play/pause, rewind and skip, speed, position, the
// timeline with the coded intervals and the "in/out" marks used to code an interval. In compact mode (above the
// text of a transcript) it has no video, no marks and a thinner timeline. The Qt player is created the first
// time a file is loaded, so the widget is cheap while there is nothing to play and works without a backend.
class MediaPlayerWidget : public QWidget {
    Q_OBJECT
public:
    explicit MediaPlayerWidget(QWidget *parent = nullptr);

    // Loads a recording. `path` is the file found on disk (empty = missing); `storedPath` is only used in the
    // message. Returns false when there is nothing to play.
    bool setMedia(const QString &path, const QString &storedPath = {});
    void clear();
    bool hasMedia() const { return m_loaded; }
    QString mediaPath() const { return m_path; }

    // Compact: transcript mode. showVideo: the picture of a video (full mode only).
    void setCompact(bool compact);
    void setVideoVisible(bool visible);

    qint64 position() const { return m_position; }
    qint64 duration() const { return m_timeline->duration(); }
    bool isPlaying() const;
    double speed() const;
    int rewindSeconds() const;
    void setRewindSeconds(int seconds);

    void seek(qint64 ms);
    void play();
    void pause();
    void togglePlay();
    void skip(int seconds); // negative = rewind; stays inside the recording
    void rewind() { skip(-rewindSeconds()); }
    void setSpeed(double rate);
    // Plays from startMs and pauses at endMs (endMs < 0: to the end of the recording).
    void playRange(qint64 startMs, qint64 endMs);
    // Where the current range playback stops (-1: none); exposed for tests.
    qint64 rangeEnd() const { return m_rangeEnd; }

    // Selected interval (set by dragging on the timeline or with the In / Out buttons).
    void setSelection(qint64 startMs, qint64 endMs);
    qint64 selectionStart() const { return m_timeline->selectionStart(); }
    qint64 selectionEnd() const { return m_timeline->selectionEnd(); }
    bool hasSelection() const { return m_timeline->hasSelection(); }
    void setBands(const QList<MediaTimeline::Band> &bands) { m_timeline->setBands(bands); }
    MediaTimeline *timeline() const { return m_timeline; }

    // Position reported by the backend; public so that tests can drive the logic without a real player.
    void handlePosition(qint64 ms);
    void handleDuration(qint64 ms);
    // Text shown when the recording cannot be played (empty: no problem).
    QString problem() const { return m_problem; }

signals:
    void positionChanged(qint64 ms);
    void playingChanged(bool playing);
    void selectionChanged();
    void relinkRequested();
    void bandContextMenu(qint64 segmentId, const QPoint &globalPos);
    void codeDropped(qint64 codeId);
    void codeSelectionRequested(); // "Codifica l'intervallo": DocumentView uses the current code

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void ensurePlayer();
    void showProblem(const QString &text);
    void updateTimeLabel();
    void updateSelectionLabel();
    void updateButtons();

    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audio = nullptr;
    QVideoWidget *m_video = nullptr;
    QWidget *m_filler;
    MediaTimeline *m_timeline;
    QToolButton *m_playButton;
    QToolButton *m_backButton;
    QToolButton *m_forwardButton;
    QSpinBox *m_rewindSpin;
    QComboBox *m_speedBox;
    QLabel *m_timeLabel;
    QWidget *m_marks;
    QLabel *m_selectionLabel;
    QPushButton *m_playSelection;
    QPushButton *m_codeSelection;
    QWidget *m_problemBar;
    QLabel *m_problemLabel;

    QString m_path;
    bool m_loaded = false;
    bool m_compact = false;
    bool m_videoVisible = false;
    qint64 m_position = 0;
    qint64 m_rangeEnd = -1;
    bool m_rangeEntered = false;
    QString m_problem;
};
