#include "ui/TranscriptDialog.h"
#include "core/Project.h"
#include "core/TextUtils.h"
#include "core/Transcripts.h"
#include "ui/UiUtils.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int StampIdRole = Qt::UserRole;
}

TranscriptDialog::TranscriptDialog(Project *project, qint64 documentId, std::function<int()> cursorPosition,
                                   QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_docId(documentId)
    , m_cursor(std::move(cursorPosition))
    , m_media(new QLabel)
    , m_table(new QTableWidget)
{
    setWindowTitle(tr("Time stamp della trascrizione"));
    resize(720, 520);

    auto *title = new QLabel;
    title->setTextFormat(Qt::PlainText);
    title->setText(m_project->document(m_docId).name);
    QFont f = title->font();
    f.setBold(true);
    title->setFont(f);
    m_media->setTextFormat(Qt::PlainText);
    m_media->setStyleSheet(QStringLiteral("color: palette(mid)"));

    auto *associate = new QPushButton(tr("Associa registrazione…"));
    auto *openMedia = new QPushButton(tr("Apri registrazione"));
    auto *mediaRow = new QHBoxLayout;
    mediaRow->addWidget(m_media, 1);
    mediaRow->addWidget(associate);
    mediaRow->addWidget(openMedia);

    m_table->setColumnCount(2);
    m_table->setHorizontalHeaderLabels({tr("Tempo"), tr("Testo")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setColumnWidth(0, 110);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this] { activateCurrent(); });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    QPushButton *insert = buttons->addButton(tr("Inserisci al cursore…"), QDialogButtonBox::ActionRole);
    QPushButton *edit = buttons->addButton(tr("Modifica tempo…"), QDialogButtonBox::ActionRole);
    QPushButton *remove = buttons->addButton(tr("Elimina"), QDialogButtonBox::ActionRole);
    QPushButton *shift = buttons->addButton(tr("Sposta tutti…"), QDialogButtonBox::ActionRole);
    QPushButton *go = buttons->addButton(tr("Vai al testo"), QDialogButtonBox::ActionRole);
    QPushButton *play = buttons->addButton(tr("Riproduci da qui"), QDialogButtonBox::ActionRole);
    QPushButton *exp = buttons->addButton(tr("Esporta…"), QDialogButtonBox::ActionRole);
    for (QPushButton *b : {insert, edit, remove, shift, go, play, exp, associate, openMedia})
        b->setAutoDefault(false);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(insert, &QPushButton::clicked, this, &TranscriptDialog::insertAtCursor);
    connect(edit, &QPushButton::clicked, this, &TranscriptDialog::editTime);
    connect(remove, &QPushButton::clicked, this, [this] {
        if (const qint64 id = currentStampId())
            m_project->deleteTimestamp(id);
    });
    connect(shift, &QPushButton::clicked, this, &TranscriptDialog::shiftAll);
    connect(go, &QPushButton::clicked, this, &TranscriptDialog::activateCurrent);
    connect(play, &QPushButton::clicked, this, &TranscriptDialog::playCurrent);
    connect(exp, &QPushButton::clicked, this, &TranscriptDialog::exportDialog);
    connect(associate, &QPushButton::clicked, this, &TranscriptDialog::associateMedia);
    connect(openMedia, &QPushButton::clicked, this, [this] {
        const QString path = m_project->resolvedMediaPath(m_docId);
        if (path.isEmpty())
            Ui::showError(this, tr("Registrazione non trovata. Ricollega il file con \"Associa registrazione…\"."));
        else
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
    auto updateButtons = [=, this] {
        const bool any = currentStampId() != 0;
        play->setEnabled(any && !m_project->documentMediaPath(m_docId).isEmpty());
        for (QPushButton *b : {edit, remove, go})
            b->setEnabled(any);
        shift->setEnabled(m_table->rowCount() > 0);
        exp->setEnabled(m_table->rowCount() > 0);
        openMedia->setEnabled(!m_project->documentMediaPath(m_docId).isEmpty());
    };
    connect(m_table, &QTableWidget::itemSelectionChanged, this, updateButtons);
    connect(m_project, &Project::timestampsChanged, this, [=, this] { refresh(); updateButtons(); });
    connect(m_project, &Project::documentsChanged, this, [=, this] { refresh(); updateButtons(); });
    connect(m_project, &Project::segmentsChanged, this, [=, this] { refresh(); updateButtons(); });

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addLayout(mediaRow);
    layout->addWidget(m_table, 1);
    layout->addWidget(buttons);
    refresh();
    updateButtons();
}

int TranscriptDialog::rowCount() const { return m_table->rowCount(); }

qint64 TranscriptDialog::currentStampId() const
{
    const int row = m_table->currentRow();
    if (row < 0 || !m_table->item(row, 0) || !m_table->item(row, 0)->isSelected())
        return 0;
    return m_table->item(row, 0)->data(StampIdRole).toLongLong();
}

void TranscriptDialog::refresh()
{
    if (!m_project->isOpen() || !m_project->document(m_docId).id) {
        m_table->setRowCount(0);
        return;
    }
    const qint64 current = currentStampId();
    const QString text = m_project->documentText(m_docId);
    const QList<Timestamp> stamps = m_project->timestamps(m_docId);
    m_table->setRowCount(int(stamps.size()));
    for (int row = 0; row < stamps.size(); ++row) {
        const Timestamp &ts = stamps[row];
        auto *time = new QTableWidgetItem(Transcripts::formatTime(ts.timeMs, true));
        time->setData(StampIdRole, ts.id);
        m_table->setItem(row, 0, time);
        const int end = int(qMin<qsizetype>(text.size(), qsizetype(ts.textPos) + 200));
        m_table->setItem(row, 1, new QTableWidgetItem(TextUtils::excerpt(text, ts.textPos, end, 160)));
        if (ts.id == current)
            m_table->selectRow(row);
    }
    const QString media = m_project->documentMediaPath(m_docId);
    m_media->setText(media.isEmpty() ? tr("Nessuna registrazione associata") : tr("Registrazione: %1").arg(media));
}

bool TranscriptDialog::insertAt(int textPos, qint64 timeMs)
{
    return m_project->addTimestamp(m_docId, textPos, timeMs) != 0;
}

void TranscriptDialog::insertAtCursor()
{
    const int pos = m_cursor ? m_cursor() : -1;
    if (pos < 0) {
        Ui::showError(this, tr("Apri il documento e posiziona il cursore nel punto del testo da marcare."));
        return;
    }
    const QList<Timestamp> stamps = m_project->timestamps(m_docId);
    qint64 suggested = 0;
    for (const Timestamp &ts : stamps)
        if (ts.textPos <= pos)
            suggested = ts.timeMs;
    const QString input = QInputDialog::getText(this, tr("Nuovo time stamp"),
                                                tr("Tempo (hh:mm:ss o mm:ss, anche con i millisecondi):"),
                                                QLineEdit::Normal, Transcripts::formatTime(suggested, true));
    if (input.isNull())
        return;
    const qint64 ms = Transcripts::parseTime(input);
    if (ms < 0) {
        Ui::showError(this, tr("Tempo non valido: \"%1\".").arg(input));
        return;
    }
    insertAt(pos, ms);
}

void TranscriptDialog::editTime()
{
    const qint64 id = currentStampId();
    if (!id)
        return;
    const int row = m_table->currentRow();
    const QString input = QInputDialog::getText(this, tr("Modifica time stamp"),
                                                tr("Tempo (hh:mm:ss o mm:ss, anche con i millisecondi):"),
                                                QLineEdit::Normal, m_table->item(row, 0)->text());
    if (input.isNull())
        return;
    const qint64 ms = Transcripts::parseTime(input);
    if (ms < 0) {
        Ui::showError(this, tr("Tempo non valido: \"%1\".").arg(input));
        return;
    }
    m_project->setTimestampTime(id, ms);
}

void TranscriptDialog::shiftAll()
{
    bool ok = false;
    const double seconds = QInputDialog::getDouble(this, tr("Sposta tutti i time stamp"),
                                                   tr("Secondi da aggiungere (negativi per anticipare):"), 0, -86400,
                                                   86400, 3, &ok);
    if (ok && seconds != 0)
        m_project->shiftTimestamps(m_docId, qRound64(seconds * 1000));
}

void TranscriptDialog::associateMedia()
{
    const QString path = Ui::openFileName(this, tr("Registrazione associata"),
                                          tr("Audio e video (*.mp3 *.wav *.m4a *.aac *.ogg *.flac *.mp4 *.mov *.mkv "
                                             "*.avi *.webm);;Tutti i file (*)"));
    if (!path.isEmpty())
        m_project->setDocumentMediaPath(m_docId, m_project->storableMediaPath(path));
}

bool TranscriptDialog::exportTo(const QString &path)
{
    Transcripts::Format format = Transcripts::Format::Text;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("srt"))
        format = Transcripts::Format::Srt;
    else if (suffix == QLatin1String("vtt"))
        format = Transcripts::Format::Vtt;
    const QString out = Transcripts::exportTranscript(m_project->documentText(m_docId),
                                                     m_project->timestamps(m_docId), format);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(out.toUtf8()) < 0 || !file.commit()) {
        Ui::showError(this, tr("Impossibile scrivere %1").arg(path));
        return false;
    }
    return true;
}

void TranscriptDialog::exportDialog()
{
    const QString name = m_project->document(m_docId).name + QStringLiteral(".srt");
    const QString path = Ui::saveFileName(this, tr("Esporta la trascrizione"), name,
                                          tr("SubRip (*.srt);;WebVTT (*.vtt);;Testo con [hh:mm:ss] (*.txt)"));
    if (!path.isEmpty())
        exportTo(path);
}

qint64 TranscriptDialog::currentTimeMs() const
{
    const qint64 id = currentStampId();
    for (const Timestamp &ts : m_project->timestamps(m_docId))
        if (ts.id == id)
            return ts.timeMs;
    return -1;
}

void TranscriptDialog::playCurrent()
{
    const qint64 ms = currentTimeMs();
    if (ms >= 0)
        emit playRequested(m_docId, ms);
}

void TranscriptDialog::activateCurrent()
{
    const qint64 id = currentStampId();
    if (!id)
        return;
    for (const Timestamp &ts : m_project->timestamps(m_docId))
        if (ts.id == id) {
            emit positionActivated(m_docId, ts.textPos);
            return;
        }
}
