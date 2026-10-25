#include "ui/ResearchQuestionsDialog.h"
#include "core/Project.h"
#include "core/ResearchWorksheet.h"
#include "ui/UiUtils.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QSplitter>
#include <QVBoxLayout>

namespace {

constexpr int KindRole = Qt::UserRole;
constexpr int RefRole = Qt::UserRole + 1;
constexpr int EvidenceRole = Qt::UserRole + 2;

struct PickItem {
    QString text;
    qint64 id = 0;
    QIcon icon;
};

QString oneLine(const QString &text, int max)
{
    const QString t = text.simplified();
    return t.size() <= max ? t : t.left(max).trimmed() + QStringLiteral("…");
}

// Multi-selection list with a filter box; returns the chosen ids (empty on cancel).
QList<qint64> pickItems(QWidget *parent, const QString &title, const QList<PickItem> &items)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.resize(620, 480);
    auto *filter = new QLineEdit;
    filter->setPlaceholderText(QCoreApplication::translate("ResearchQuestionsDialog", "Filtra…"));
    filter->setClearButtonEnabled(true);
    auto *list = new QListWidget;
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    for (const PickItem &p : items) {
        auto *item = new QListWidgetItem(p.icon, p.text, list);
        item->setData(Qt::UserRole, p.id);
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    QObject::connect(filter, &QLineEdit::textChanged, list, [list](const QString &text) {
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setHidden(!list->item(i)->text().contains(text, Qt::CaseInsensitive));
    });
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(filter);
    layout->addWidget(list, 1);
    layout->addWidget(buttons);
    QList<qint64> chosen;
    if (dlg.exec() != QDialog::Accepted)
        return chosen;
    for (const QListWidgetItem *item : list->selectedItems())
        chosen << item->data(Qt::UserRole).toLongLong();
    return chosen;
}

} // namespace

ResearchQuestionsDialog::ResearchQuestionsDialog(Project *project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_questions(new QListWidget)
    , m_title(new QLineEdit)
    , m_status(new QComboBox)
    , m_description(new QPlainTextEdit)
    , m_themes(new QPlainTextEdit)
    , m_evidence(new QListWidget)
    , m_conclusions(new QPlainTextEdit)
    , m_notes(new QPlainTextEdit)
{
    setWindowTitle(tr("Domande di ricerca"));
    resize(1100, 720);

    m_status->addItem(tr("Aperta"), int(QuestionStatus::Open));
    m_status->addItem(tr("In lavorazione"), int(QuestionStatus::InProgress));
    m_status->addItem(tr("Con risposta"), int(QuestionStatus::Answered));
    m_description->setPlaceholderText(tr("Descrizione e contesto della domanda"));
    m_themes->setPlaceholderText(tr("Un tema per riga"));
    m_conclusions->setPlaceholderText(tr("Conclusioni, collegamenti a teorie e letteratura"));
    m_notes->setPlaceholderText(tr("Note di lavoro (non incluse nell'esportazione, se non richiesto)"));
    m_evidence->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_evidence->setAcceptDrops(true);
    m_evidence->viewport()->setAcceptDrops(true);
    m_evidence->viewport()->installEventFilter(this);
    m_evidence->setToolTip(tr("Trascina qui i codici dall'albero dei codici"));

    auto *newButton = new QPushButton(tr("Nuova…"));
    m_deleteButton = new QPushButton(tr("Elimina"));
    m_upButton = new QPushButton(tr("Su"));
    m_downButton = new QPushButton(tr("Giù"));
    auto *exportButton = new QPushButton(tr("Esporta…"));
    exportButton->setToolTip(tr("Esporta il foglio in Word (.docx), PDF o HTML come bozza dei risultati"));
    auto *addSegments = new QPushButton(tr("Segmenti…"));
    auto *addCodes = new QPushButton(tr("Codici…"));
    auto *addMemos = new QPushButton(tr("Memo…"));
    m_removeButton = new QPushButton(tr("Rimuovi"));
    for (QPushButton *b : {newButton, m_deleteButton, m_upButton, m_downButton, exportButton, addSegments, addCodes,
                           addMemos, m_removeButton})
        b->setAutoDefault(false);
    connect(newButton, &QPushButton::clicked, this, &ResearchQuestionsDialog::newQuestion);
    connect(m_deleteButton, &QPushButton::clicked, this, &ResearchQuestionsDialog::deleteQuestion);
    connect(m_upButton, &QPushButton::clicked, this, [this] { moveQuestion(-1); });
    connect(m_downButton, &QPushButton::clicked, this, [this] { moveQuestion(1); });
    connect(exportButton, &QPushButton::clicked, this, &ResearchQuestionsDialog::exportWorksheet);
    connect(addSegments, &QPushButton::clicked, this, &ResearchQuestionsDialog::addSegments);
    connect(addCodes, &QPushButton::clicked, this, &ResearchQuestionsDialog::addCodes);
    connect(addMemos, &QPushButton::clicked, this, &ResearchQuestionsDialog::addMemos);
    connect(m_removeButton, &QPushButton::clicked, this, &ResearchQuestionsDialog::removeEvidence);

    auto *left = new QWidget;
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(new QLabel(tr("Domande")));
    leftLayout->addWidget(m_questions, 1);
    auto *row1 = new QHBoxLayout;
    row1->addWidget(newButton);
    row1->addWidget(m_deleteButton);
    leftLayout->addLayout(row1);
    auto *row2 = new QHBoxLayout;
    row2->addWidget(m_upButton);
    row2->addWidget(m_downButton);
    leftLayout->addLayout(row2);
    leftLayout->addWidget(exportButton);

    m_editor = new QWidget;
    auto *editorLayout = new QVBoxLayout(m_editor);
    editorLayout->setContentsMargins(0, 0, 0, 0);
    auto *head = new QHBoxLayout;
    head->addWidget(new QLabel(tr("Domanda:")));
    head->addWidget(m_title, 1);
    head->addWidget(new QLabel(tr("Stato:")));
    head->addWidget(m_status);
    editorLayout->addLayout(head);

    auto *top = new QWidget;
    auto *topLayout = new QHBoxLayout(top);
    topLayout->setContentsMargins(0, 0, 0, 0);
    auto *descBox = new QGroupBox(tr("Descrizione"));
    (new QVBoxLayout(descBox))->addWidget(m_description);
    auto *themesBox = new QGroupBox(tr("Temi"));
    (new QVBoxLayout(themesBox))->addWidget(m_themes);
    topLayout->addWidget(descBox, 1);
    topLayout->addWidget(themesBox, 1);

    auto *evidenceBox = new QGroupBox(tr("Evidenze (riferimenti, non copie)"));
    auto *evidenceLayout = new QVBoxLayout(evidenceBox);
    auto *evidenceButtons = new QHBoxLayout;
    evidenceButtons->addWidget(new QLabel(tr("Aggiungi:")));
    evidenceButtons->addWidget(addSegments);
    evidenceButtons->addWidget(addCodes);
    evidenceButtons->addWidget(addMemos);
    evidenceButtons->addStretch();
    evidenceButtons->addWidget(m_removeButton);
    evidenceLayout->addLayout(evidenceButtons);
    evidenceLayout->addWidget(m_evidence, 1);

    auto *bottom = new QWidget;
    auto *bottomLayout = new QHBoxLayout(bottom);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    auto *concBox = new QGroupBox(tr("Conclusioni"));
    (new QVBoxLayout(concBox))->addWidget(m_conclusions);
    auto *notesBox = new QGroupBox(tr("Note di lavoro"));
    (new QVBoxLayout(notesBox))->addWidget(m_notes);
    bottomLayout->addWidget(concBox, 2);
    bottomLayout->addWidget(notesBox, 1);

    auto *vertical = new QSplitter(Qt::Vertical);
    vertical->addWidget(top);
    vertical->addWidget(evidenceBox);
    vertical->addWidget(bottom);
    vertical->setStretchFactor(0, 2);
    vertical->setStretchFactor(1, 3);
    vertical->setStretchFactor(2, 2);
    editorLayout->addWidget(vertical, 1);

    auto *main = new QSplitter;
    main->addWidget(left);
    main->addWidget(m_editor);
    main->setStretchFactor(0, 1);
    main->setStretchFactor(1, 4);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(main);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(600);
    connect(&m_saveTimer, &QTimer::timeout, this, &ResearchQuestionsDialog::commitEdits);
    connect(m_title, &QLineEdit::textEdited, this, [this] { scheduleSave(); });
    connect(m_status, &QComboBox::activated, this, [this] { scheduleSave(); });
    for (QPlainTextEdit *e : {m_description, m_themes, m_conclusions, m_notes})
        connect(e, &QPlainTextEdit::textChanged, this, [this] { scheduleSave(); });
    connect(m_questions, &QListWidget::currentRowChanged, this, [this] {
        if (m_loading)
            return;
        commitEdits();
        loadQuestion();
    });
    connect(m_evidence, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        emit evidenceActivated(item->data(KindRole).toInt(), item->data(RefRole).toLongLong());
    });
    connect(m_evidence, &QListWidget::itemSelectionChanged, this, [this] { updateButtons(); });

    connect(m_project, &Project::researchQuestionsChanged, this, [this] {
        // Own saves of the texts come here too: the editors already show them.
        const qint64 keep = currentQuestionId();
        reloadQuestions();
        selectQuestion(keep);
        if (!currentQuestionId() && m_questions->count())
            m_questions->setCurrentRow(0); // the current question was deleted
        if (!m_dirty && m_loadedId != currentQuestionId())
            loadQuestion();
        else
            reloadEvidence();
    });
    // Evidence disappears with its element, and its label follows renames.
    connect(m_project, &Project::codesChanged, this, [this] { reloadEvidence(); });
    connect(m_project, &Project::memosChanged, this, [this] { reloadEvidence(); });
    connect(m_project, &Project::segmentsChanged, this, [this] { reloadEvidence(); });
    connect(m_project, &Project::documentsChanged, this, [this] { reloadEvidence(); });

    reloadQuestions();
    if (m_questions->count())
        m_questions->setCurrentRow(0);
    else
        loadQuestion();
}

qint64 ResearchQuestionsDialog::currentQuestionId() const
{
    const QListWidgetItem *item = m_questions->currentItem();
    return item ? item->data(Qt::UserRole).toLongLong() : 0;
}

void ResearchQuestionsDialog::selectQuestion(qint64 id)
{
    for (int i = 0; i < m_questions->count(); ++i)
        if (m_questions->item(i)->data(Qt::UserRole).toLongLong() == id) {
            m_questions->setCurrentRow(i);
            return;
        }
}

void ResearchQuestionsDialog::reloadQuestions()
{
    const qint64 keep = currentQuestionId();
    const bool wasLoading = m_loading;
    m_loading = true;
    m_questions->clear();
    for (const ResearchQuestion &q : m_project->researchQuestions()) {
        auto *item = new QListWidgetItem(q.title.trimmed().isEmpty() ? tr("(senza titolo)") : q.title, m_questions);
        item->setData(Qt::UserRole, q.id);
        item->setToolTip(Ui::plainToolTip(ResearchWorksheet::statusLabel(q.status)));
        if (q.id == keep)
            m_questions->setCurrentItem(item);
    }
    m_loading = wasLoading;
}

void ResearchQuestionsDialog::loadQuestion()
{
    const ResearchQuestion q = m_project->researchQuestion(currentQuestionId());
    const bool wasLoading = m_loading;
    m_loading = true;
    m_loadedId = q.id;
    m_dirty = false;
    m_saveTimer.stop();
    m_title->setText(q.title);
    m_status->setCurrentIndex(m_status->findData(int(q.status)));
    m_description->setPlainText(q.description);
    m_themes->setPlainText(q.themes.join(QLatin1Char('\n')));
    m_conclusions->setPlainText(q.conclusions);
    m_notes->setPlainText(q.notes);
    m_loading = wasLoading;
    reloadEvidence();
    updateButtons();
}

void ResearchQuestionsDialog::reloadEvidence()
{
    const qint64 id = currentQuestionId();
    m_evidence->clear();
    if (!id) {
        updateButtons();
        return;
    }
    QHash<qint64, QString> docTexts;
    QHash<qint64, QString> docNames;
    for (const Document &d : m_project->documents())
        docNames.insert(d.id, d.name);
    for (const QuestionEvidence &e : m_project->questionEvidence(id)) {
        QString text, tip;
        QIcon icon;
        switch (e.kind) {
        case EvidenceKind::Code: {
            const Code c = m_project->code(e.refId);
            text = tr("Codice: %1").arg(m_project->codePath(c.id));
            icon = Ui::colorIcon(c.color);
            break;
        }
        case EvidenceKind::Memo: {
            const Memo m = m_project->memo(e.refId);
            text = tr("Memo: %1").arg(m.title.trimmed().isEmpty() ? oneLine(m.body, 80) : m.title.trimmed());
            tip = m.body;
            icon = Ui::memoIcon();
            break;
        }
        case EvidenceKind::Segment: {
            const Segment s = m_project->segment(e.refId);
            QString body;
            if (s.kind == SegmentKind::Text) {
                if (!docTexts.contains(s.documentId))
                    docTexts.insert(s.documentId, m_project->documentText(s.documentId));
                body = docTexts.value(s.documentId).mid(s.start, s.length());
            } else {
                body = s.kind == SegmentKind::Area ? s.areaText : QString();
            }
            text = tr("Segmento: %1 · %2 · %3")
                       .arg(m_project->codePath(s.codeId), docNames.value(s.documentId), oneLine(body, 100));
            tip = body;
            icon = Ui::colorIcon(m_project->code(s.codeId).color);
            break;
        }
        }
        auto *item = new QListWidgetItem(icon, text, m_evidence);
        item->setData(KindRole, int(e.kind));
        item->setData(RefRole, e.refId);
        item->setData(EvidenceRole, e.id);
        if (!tip.isEmpty())
            item->setToolTip(Ui::plainToolTip(oneLine(tip, 600)));
    }
    updateButtons();
}

void ResearchQuestionsDialog::updateButtons()
{
    const bool has = currentQuestionId() != 0;
    m_editor->setEnabled(has);
    m_deleteButton->setEnabled(has);
    m_upButton->setEnabled(has && m_questions->currentRow() > 0);
    m_downButton->setEnabled(has && m_questions->currentRow() < m_questions->count() - 1);
    m_removeButton->setEnabled(has && !m_evidence->selectedItems().isEmpty());
}

void ResearchQuestionsDialog::scheduleSave()
{
    if (m_loading || !m_loadedId)
        return;
    m_dirty = true;
    m_saveTimer.start();
}

void ResearchQuestionsDialog::commitEdits()
{
    m_saveTimer.stop();
    if (!m_dirty || !m_loadedId)
        return;
    m_dirty = false;
    ResearchQuestion q = m_project->researchQuestion(m_loadedId);
    if (!q.id)
        return;
    q.title = m_title->text();
    q.status = QuestionStatus(m_status->currentData().toInt());
    q.description = m_description->toPlainText();
    q.themes = m_themes->toPlainText().split(QLatin1Char('\n'));
    q.conclusions = m_conclusions->toPlainText();
    q.notes = m_notes->toPlainText();
    m_project->updateResearchQuestion(q);
}

qint64 ResearchQuestionsDialog::createQuestion(const QString &title)
{
    commitEdits();
    const qint64 id = m_project->addResearchQuestion(title);
    if (id)
        selectQuestion(id);
    return id;
}

void ResearchQuestionsDialog::newQuestion()
{
    const QString title = Ui::askText(this, tr("Nuova domanda di ricerca"), tr("Domanda:"));
    if (!title.isEmpty())
        createQuestion(title);
}

void ResearchQuestionsDialog::deleteQuestion()
{
    const ResearchQuestion q = m_project->researchQuestion(currentQuestionId());
    if (!q.id)
        return;
    if (!Ui::confirm(this, tr("Elimina domanda"),
                     tr("Eliminare la domanda «%1» con i suoi temi, le evidenze e le conclusioni?").arg(q.title)))
        return;
    m_dirty = false;
    m_saveTimer.stop();
    m_project->deleteResearchQuestion(q.id);
}

void ResearchQuestionsDialog::moveQuestion(int delta)
{
    commitEdits();
    const qint64 id = currentQuestionId();
    if (id)
        m_project->moveResearchQuestion(id, delta);
}

void ResearchQuestionsDialog::addSegments()
{
    commitEdits();
    const qint64 id = currentQuestionId();
    if (!id)
        return;
    QSet<qint64> have;
    for (const QuestionEvidence &e : m_project->questionEvidence(id))
        if (e.kind == EvidenceKind::Segment)
            have.insert(e.refId);
    QHash<qint64, QString> names, texts;
    for (const Document &d : m_project->documents())
        names.insert(d.id, d.name);
    QHash<qint64, Code> codes;
    for (const Code &c : m_project->codes())
        codes.insert(c.id, c);
    QList<PickItem> items;
    for (const Segment &s : m_project->allSegments(true)) {
        if (have.contains(s.id))
            continue;
        QString body;
        if (s.kind == SegmentKind::Text) {
            if (!texts.contains(s.documentId))
                texts.insert(s.documentId, m_project->documentText(s.documentId));
            body = texts.value(s.documentId).mid(s.start, s.length());
        } else if (s.kind == SegmentKind::Area) {
            body = s.areaText;
        }
        items.append({tr("%1 · %2 · %3").arg(m_project->codePath(s.codeId), names.value(s.documentId), oneLine(body, 100)),
                      s.id, Ui::colorIcon(codes.value(s.codeId).color)});
    }
    if (items.isEmpty()) {
        Ui::showInfo(this, windowTitle(), tr("Non ci sono segmenti da aggiungere."));
        return;
    }
    const QList<qint64> chosen = pickItems(this, tr("Aggiungi segmenti"), items);
    m_project->beginBatch();
    for (qint64 s : chosen)
        m_project->addQuestionEvidence(id, EvidenceKind::Segment, s);
    m_project->commitBatch();
}

void ResearchQuestionsDialog::addCodes()
{
    commitEdits();
    const qint64 id = currentQuestionId();
    if (!id)
        return;
    QSet<qint64> have;
    for (const QuestionEvidence &e : m_project->questionEvidence(id))
        if (e.kind == EvidenceKind::Code)
            have.insert(e.refId);
    QList<PickItem> items;
    for (const Code &c : m_project->codesInTreeOrder())
        if (!have.contains(c.id))
            items.append({m_project->codePath(c.id), c.id, Ui::colorIcon(c.color)});
    if (items.isEmpty()) {
        Ui::showInfo(this, windowTitle(), tr("Non ci sono codici da aggiungere."));
        return;
    }
    const QList<qint64> chosen = pickItems(this, tr("Aggiungi codici"), items);
    m_project->beginBatch();
    for (qint64 c : chosen)
        m_project->addQuestionEvidence(id, EvidenceKind::Code, c);
    m_project->commitBatch();
}

void ResearchQuestionsDialog::addMemos()
{
    commitEdits();
    const qint64 id = currentQuestionId();
    if (!id)
        return;
    QSet<qint64> have;
    for (const QuestionEvidence &e : m_project->questionEvidence(id))
        if (e.kind == EvidenceKind::Memo)
            have.insert(e.refId);
    QList<PickItem> items;
    for (const Memo &m : m_project->memos())
        if (!have.contains(m.id))
            items.append({m.title.trimmed().isEmpty() ? oneLine(m.body, 100) : m.title.trimmed(), m.id, Ui::memoIcon()});
    if (items.isEmpty()) {
        Ui::showInfo(this, windowTitle(), tr("Non ci sono memo da aggiungere."));
        return;
    }
    const QList<qint64> chosen = pickItems(this, tr("Aggiungi memo"), items);
    m_project->beginBatch();
    for (qint64 m : chosen)
        m_project->addQuestionEvidence(id, EvidenceKind::Memo, m);
    m_project->commitBatch();
}

void ResearchQuestionsDialog::removeEvidence()
{
    commitEdits();
    const auto selected = m_evidence->selectedItems();
    if (selected.isEmpty())
        return;
    m_project->beginBatch();
    for (const QListWidgetItem *item : selected)
        m_project->removeQuestionEvidence(item->data(EvidenceRole).toLongLong());
    m_project->commitBatch();
}

bool ResearchQuestionsDialog::addDroppedMime(const QMimeData *mime)
{
    const QString codeMime = QString::fromLatin1(Ui::CodeMimeType);
    const qint64 id = currentQuestionId();
    if (!id || !mime || !mime->hasFormat(codeMime))
        return false;
    commitEdits();
    return m_project->addQuestionEvidence(id, EvidenceKind::Code, mime->data(codeMime).toLongLong());
}

bool ResearchQuestionsDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_evidence->viewport()) {
        const QString codeMime = QString::fromLatin1(Ui::CodeMimeType);
        switch (event->type()) {
        case QEvent::DragEnter:
        case QEvent::DragMove: {
            auto *e = static_cast<QDragMoveEvent *>(event);
            if (currentQuestionId() && e->mimeData()->hasFormat(codeMime)) {
                e->setDropAction(Qt::CopyAction);
                e->accept();
                return true;
            }
            break;
        }
        case QEvent::Drop: {
            auto *e = static_cast<QDropEvent *>(event);
            if (addDroppedMime(e->mimeData())) {
                e->setDropAction(Qt::CopyAction);
                e->accept();
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return QDialog::eventFilter(watched, event);
}

bool ResearchQuestionsDialog::exportTo(const QString &path)
{
    commitEdits();
    const Report::Document doc = ResearchWorksheet::build(*m_project, {});
    const QString suffix = QFileInfo(path).suffix().toLower();
    QString error;
    bool ok = false;
    if (suffix == QLatin1String("docx"))
        ok = Report::writeDocx(path, doc, &error);
    else if (suffix == QLatin1String("pdf"))
        ok = Report::writePdf(path, doc, &error);
    else
        ok = Report::writeHtml(path, doc, &error);
    if (!ok)
        Ui::showError(this, error);
    return ok;
}

void ResearchQuestionsDialog::exportWorksheet()
{
    QString path = Ui::saveFileName(this, tr("Esporta il foglio di lavoro"), m_project->name() + tr(" - domande di ricerca.docx"),
                                    tr("Documento Word (*.docx);;Documento PDF (*.pdf);;Pagina HTML (*.html)"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".docx");
    exportTo(path);
}

void ResearchQuestionsDialog::closeEvent(QCloseEvent *event)
{
    commitEdits();
    QDialog::closeEvent(event);
}
