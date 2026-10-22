#include "ui/MainWindow.h"
#include "core/DemoProject.h"
#include "core/Exporters.h"
#include "core/Importers.h"
#include "core/Project.h"
#include "core/Qdpx.h"
#include "core/TextUtils.h"
#include "ui/CodeTree.h"
#include "ui/DocumentTree.h"
#include "ui/DocumentView.h"
#include "ui/CooccurrenceDialog.h"
#include "ui/StatsDialog.h"
#include "ui/MatrixDialog.h"
#include "ui/RetrievalDialog.h"
#include "ui/VariableFilterDialog.h"
#include "ui/MemoManager.h"
#include "ui/SearchDialog.h"
#include "ui/SegmentsPanel.h"
#include "ui/UiUtils.h"
#include "ui/VariablesDialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
const QString kProjectFilter = QObject::tr("Progetto Tessera (*.tsra)");
constexpr int MaxRecent = 8;
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_project(new Project(this))
    , m_documents(new DocumentTree(m_project))
    , m_codes(new CodeTree(m_project))
    , m_view(new DocumentView(m_project))
    , m_segments(new SegmentsPanel(m_project))
    , m_mainSplitter(new QSplitter(Qt::Horizontal))
    , m_leftSplitter(new QSplitter(Qt::Vertical))
    , m_rightSplitter(new QSplitter(Qt::Vertical))
    , m_stats(new QLabel)
{
    setWindowIcon(Ui::colorIcon(QColor(0x3d5a80), 32));
    const QString author = QSettings().value(QStringLiteral("user/name")).toString();
    if (!author.isEmpty())
        m_project->setAuthor(author);

    m_view->setCurrentCodeProvider([this] { return m_codes->currentCodeId(); });

    m_leftSplitter->addWidget(m_documents);
    m_leftSplitter->addWidget(m_codes);
    m_leftSplitter->setStretchFactor(0, 1);
    m_leftSplitter->setStretchFactor(1, 1);
    m_rightSplitter->addWidget(m_view);
    m_rightSplitter->addWidget(m_segments);
    m_rightSplitter->setStretchFactor(0, 3);
    m_rightSplitter->setStretchFactor(1, 2);
    m_leftSplitter->setMinimumWidth(260);
    m_mainSplitter->addWidget(m_leftSplitter);
    m_mainSplitter->addWidget(m_rightSplitter);
    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 3);
    m_mainSplitter->setSizes({320, 960});
    m_rightSplitter->setSizes({520, 300});
    setCentralWidget(m_mainSplitter);
    statusBar()->addPermanentWidget(m_stats);

    createActions();
    createMenus();
    createToolBar();
    connectProject();

    QSettings s;
    resize(1360, 860);
    restoreGeometry(s.value(QStringLiteral("ui/geometry")).toByteArray());
    m_mainSplitter->restoreState(s.value(QStringLiteral("ui/mainSplitter")).toByteArray());
    m_leftSplitter->restoreState(s.value(QStringLiteral("ui/leftSplitter")).toByteArray());
    m_rightSplitter->restoreState(s.value(QStringLiteral("ui/rightSplitter")).toByteArray());

    updateActions();
    updateTitle();
}

MainWindow::~MainWindow()
{
    // Child widgets emit signals while they are destroyed; our slots must not run on half-deleted actions.
    const QList<QObject *> children = findChildren<QObject *>();
    for (QObject *child : children)
        child->disconnect(this);
}

void MainWindow::createActions()
{
    auto make = [this](const QString &text, auto slot, const QKeySequence &shortcut = {}, bool needsProject = true,
                       QStyle::StandardPixmap icon = QStyle::StandardPixmap(-1)) {
        auto *a = new QAction(text, this);
        if (!shortcut.isEmpty())
            a->setShortcut(shortcut);
        if (icon != QStyle::StandardPixmap(-1))
            a->setIcon(style()->standardIcon(icon));
        connect(a, &QAction::triggered, this, slot);
        if (needsProject)
            m_projectActions << a;
        return a;
    };

    m_newProject = make(tr("&Nuovo progetto…"), &MainWindow::newProject, QKeySequence::New, false,
                        QStyle::SP_FileDialogNewFolder);
    m_openProject = make(tr("&Apri progetto…"), &MainWindow::openProjectDialog, QKeySequence::Open, false,
                         QStyle::SP_DialogOpenButton);
    m_saveCopy = make(tr("Salva una &copia come…"), &MainWindow::saveCopyAs, QKeySequence::SaveAs);
    m_closeProject = make(tr("&Chiudi progetto"), &MainWindow::closeProject);
    m_importQdpx = make(tr("Importa progetto &QDPX (REFI-QDA)…"), &MainWindow::importQdpx, {}, false);
    m_exportQdpx = make(tr("&Esporta come QDPX (REFI-QDA)…"), &MainWindow::exportQdpx);
    m_example = make(tr("Crea progetto di &esempio…"), &MainWindow::newExampleProject, {}, false);
    m_quit = make(tr("&Esci"), &QWidget::close, QKeySequence::Quit, false);

    m_undo = make(tr("&Annulla"), [this] {
        if (!m_project->undo())
            Ui::showError(this, m_project->lastError());
    }, QKeySequence::Undo, true, QStyle::SP_ArrowBack);
    m_redo = make(tr("&Ripeti"), [this] {
        if (!m_project->redo())
            Ui::showError(this, m_project->lastError());
    }, QKeySequence::Redo, true, QStyle::SP_ArrowForward);
    m_redo->setShortcuts({QKeySequence::Redo, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});

    m_importDocs = make(tr("&Importa documenti…"), [this] { importDocuments(m_documents->currentGroupId()); },
                        QKeySequence(Qt::CTRL | Qt::Key_I), true, QStyle::SP_FileIcon);
    m_newTextDoc = make(tr("Nuovo documento di &testo…"), [this] { newTextDocument(m_documents->currentGroupId()); });
    m_newGroup = make(tr("Nuovo &gruppo di documenti…"), [this] {
        const QString name = Ui::askText(this, tr("Nuovo gruppo"), tr("Nome del gruppo:"));
        if (!name.isEmpty())
            m_project->addGroup(name);
    });
    m_newDocumentSet = make(tr("Nuovo &set di documenti…"), [this] { m_documents->newDocumentSet(); });
    m_documentSetFromActive = make(tr("Crea set dai documenti &attivi…"),
                                   [this] { m_documents->newDocumentSetFromActive(); });
    m_variables = make(tr("&Variabili dei documenti…"), &MainWindow::showVariables);
    m_activateByVariables = make(tr("Attiva per &variabili…"), &MainWindow::showVariableFilter);

    m_newCode = make(tr("&Nuovo codice…"), [this] { m_codes->newCode(0); }, QKeySequence(Qt::CTRL | Qt::Key_K));
    m_newSubcode = make(tr("Nuovo &sottocodice…"), [this] {
        if (const qint64 id = m_codes->currentCodeId())
            m_codes->newCode(id);
        else
            Ui::showError(this, tr("Seleziona prima il codice superiore."));
    });
    m_codeCurrent = make(tr("&Codifica selezione con il codice selezionato"), &MainWindow::codeWithCurrent,
                         QKeySequence(Qt::CTRL | Qt::Key_E));
    m_codeNew = make(tr("Codifica selezione con un &nuovo codice…"), &MainWindow::codeWithNewCode,
                     QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    m_editText = make(tr("&Modifica testo del documento"), [this](bool on) { m_view->setEditing(on); });
    m_editText->setCheckable(true);
    m_exportCodebook = make(tr("Esporta &codebook…"), &MainWindow::exportCodebook);

    m_search = make(tr("&Ricerca lessicale…"), &MainWindow::showSearch,
                    QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), true,
                    QStyle::SP_FileDialogContentsView);
    m_matrix = make(tr("&Matrice codici × documenti…"), &MainWindow::showMatrix, QKeySequence(Qt::CTRL | Qt::Key_M));
    m_retrieval = make(tr("Recupero &complesso…"), &MainWindow::showRetrieval, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
    m_cooccurrence = make(tr("Co-&occorrenze dei codici…"), &MainWindow::showCooccurrence);
    m_statsAction = make(tr("&Statistiche e tabelle incrociate…"), &MainWindow::showStats);
    m_exportSegmentsHtml = make(tr("Esporta segmenti recuperati (&HTML)…"), [this] { m_segments->exportHtml(); });
    m_exportSegmentsCsv = make(tr("Esporta segmenti recuperati (&Excel/CSV)…"), [this] { m_segments->exportCsv(); });

    m_projectMemo = make(tr("Memo del &progetto…"), &MainWindow::editProjectMemo);
    m_memos = make(tr("&Tutti i memo…"), &MainWindow::showMemos, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    m_projectMemo->setIcon(Ui::memoIcon());

    m_userName = make(tr("&Nome utente…"), &MainWindow::setUserName, {}, false);
    m_about = make(tr("&Informazioni su Tessera"), &MainWindow::about, {}, false);
}

void MainWindow::createMenus()
{
    QMenu *project = menuBar()->addMenu(tr("&Progetto"));
    project->addAction(m_newProject);
    project->addAction(m_openProject);
    m_recentMenu = project->addMenu(tr("Progetti &recenti"));
    rebuildRecentMenu();
    project->addAction(m_saveCopy);
    project->addAction(m_closeProject);
    project->addSeparator();
    project->addAction(m_importQdpx);
    project->addAction(m_exportQdpx);
    project->addSeparator();
    project->addAction(m_example);
    project->addSeparator();
    project->addAction(m_quit);

    QMenu *edit = menuBar()->addMenu(tr("&Modifica"));
    edit->addAction(m_undo);
    edit->addAction(m_redo);

    QMenu *docs = menuBar()->addMenu(tr("&Documenti"));
    docs->addAction(m_importDocs);
    docs->addAction(m_newTextDoc);
    docs->addAction(m_newGroup);
    docs->addSeparator();
    docs->addAction(m_editText);
    docs->addSeparator();
    docs->addAction(m_variables);
    docs->addSeparator();
    docs->addAction(m_newDocumentSet);
    docs->addAction(m_documentSetFromActive);
    docs->addSeparator();
    docs->addAction(m_activateByVariables);
    docs->addAction(tr("Attiva tutti i documenti"), this, [this] { m_documents->setAllActive(true); });
    docs->addAction(tr("Disattiva tutti i documenti"), this, [this] { m_documents->setAllActive(false); });

    QMenu *codes = menuBar()->addMenu(tr("&Codici"));
    codes->addAction(m_newCode);
    codes->addAction(m_newSubcode);
    codes->addSeparator();
    codes->addAction(m_codeCurrent);
    codes->addAction(m_codeNew);
    codes->addSeparator();
    codes->addAction(tr("Attiva tutti i codici"), this, [this] { m_codes->setAllActive(true); });
    codes->addAction(tr("Disattiva tutti i codici"), this, [this] { m_codes->setAllActive(false); });
    codes->addSeparator();
    codes->addAction(m_exportCodebook);

    QMenu *analysis = menuBar()->addMenu(tr("&Analisi"));
    analysis->addAction(m_search);
    analysis->addAction(m_matrix);
    analysis->addAction(m_retrieval);
    analysis->addAction(m_cooccurrence);
    analysis->addAction(m_statsAction);
    analysis->addAction(m_activateByVariables);
    analysis->addSeparator();
    analysis->addAction(m_exportSegmentsHtml);
    analysis->addAction(m_exportSegmentsCsv);

    QMenu *memos = menuBar()->addMenu(tr("&Memo"));
    memos->addAction(m_projectMemo);
    memos->addAction(m_memos);

    QMenu *settings = menuBar()->addMenu(tr("&Impostazioni"));
    settings->addAction(m_userName);

    QMenu *help = menuBar()->addMenu(tr("&Aiuto"));
    help->addAction(m_about);
}

void MainWindow::createToolBar()
{
    QToolBar *tb = addToolBar(tr("Strumenti"));
    tb->setObjectName(QStringLiteral("mainToolBar"));
    tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    tb->setMovable(false);
    tb->addAction(m_newProject);
    tb->addAction(m_openProject);
    tb->addSeparator();
    QAction *undo = new QAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("Annulla"), this);
    undo->setToolTip(tr("Annulla (Ctrl+Z)"));
    connect(undo, &QAction::triggered, m_undo, &QAction::trigger);
    connect(m_undo, &QAction::changed, undo, [undo, this] {
        undo->setEnabled(m_undo->isEnabled());
        undo->setToolTip(m_undo->text().remove(QLatin1Char('&')) + QStringLiteral(" (Ctrl+Z)"));
    });
    QAction *redo = new QAction(style()->standardIcon(QStyle::SP_ArrowForward), tr("Ripeti"), this);
    connect(redo, &QAction::triggered, m_redo, &QAction::trigger);
    connect(m_redo, &QAction::changed, redo, [redo, this] {
        redo->setEnabled(m_redo->isEnabled());
        redo->setToolTip(m_redo->text().remove(QLatin1Char('&')) + QStringLiteral(" (Ctrl+Y)"));
    });
    tb->addAction(undo);
    tb->addAction(redo);
    tb->addSeparator();
    tb->addAction(m_importDocs);
    tb->addSeparator();
    QAction *newCode = new QAction(Ui::colorIcon(QColor(0x2ca02c)), tr("Nuovo codice"), this);
    connect(newCode, &QAction::triggered, m_newCode, &QAction::trigger);
    m_projectActions << newCode;
    tb->addAction(newCode);
    QAction *code = new QAction(tr("Codifica (Ctrl+E)"), this);
    connect(code, &QAction::triggered, m_codeCurrent, &QAction::trigger);
    m_projectActions << code;
    tb->addAction(code);
    tb->addSeparator();
    tb->addAction(m_search);
    QAction *matrix = new QAction(tr("Matrice"), this);
    connect(matrix, &QAction::triggered, m_matrix, &QAction::trigger);
    m_projectActions << matrix;
    tb->addAction(matrix);
    QAction *vars = new QAction(tr("Variabili"), this);
    connect(vars, &QAction::triggered, m_variables, &QAction::trigger);
    m_projectActions << vars;
    tb->addAction(vars);
    QAction *memos = new QAction(Ui::memoIcon(), tr("Memo"), this);
    connect(memos, &QAction::triggered, m_memos, &QAction::trigger);
    m_projectActions << memos;
    tb->addAction(memos);
}

void MainWindow::connectProject()
{
    connect(m_project, &Project::opened, this, [this] {
        refreshAll();
        updateActions();
        updateTitle();
    });
    connect(m_project, &Project::closed, this, [this] {
        m_view->closeDocument();
        refreshAll();
        updateActions();
        updateTitle();
    });
    connect(m_project, &Project::groupsChanged, this, [this] {
        m_documents->rebuild();
        updateStats();
    });
    // DocumentView subscribes to the project signals itself.
    connect(m_project, &Project::documentsChanged, this, [this] {
        m_documents->rebuild();
        updateRetrieval();
        updateStats();
    });
    connect(m_project, &Project::documentSetsChanged, m_documents, &DocumentTree::rebuild);
    connect(m_project, &Project::codesChanged, this, [this] {
        m_codes->rebuild();
        updateRetrieval();
        updateStats();
    });
    connect(m_project, &Project::segmentsChanged, this, [this] {
        m_documents->rebuild();
        m_codes->rebuild();
        m_segments->refresh();
        updateStats();
    });
    connect(m_project, &Project::undoStateChanged, this, &MainWindow::updateActions);
    connect(m_project, &Project::memosChanged, this, [this] {
        m_documents->rebuild();
        m_codes->rebuild();
    });

    connect(m_documents, &DocumentTree::documentOpened, m_view, &DocumentView::openDocument);
    connect(m_documents, &DocumentTree::activationChanged, this, &MainWindow::updateRetrieval);
    connect(m_documents, &DocumentTree::importRequested, this, &MainWindow::importDocuments);
    connect(m_documents, &DocumentTree::newTextDocumentRequested, this, &MainWindow::newTextDocument);
    connect(m_codes, &CodeTree::activationChanged, this, &MainWindow::updateRetrieval);
    connect(m_codes, &CodeTree::currentCodeChanged, this, &MainWindow::updateActions);
    connect(m_view, &DocumentView::selectionChanged, this, &MainWindow::updateActions);
    connect(m_view, &DocumentView::statusMessage, this, [this](const QString &m) { statusBar()->showMessage(m, 5000); });
    connect(m_view, &DocumentView::newCodeForSelectionRequested, this, &MainWindow::codeWithNewCode);
    connect(m_view, &DocumentView::editingChanged, this, [this](bool on) {
        const QSignalBlocker block(m_editText);
        m_editText->setChecked(on);
        updateActions();
    });
    connect(m_segments, &SegmentsPanel::segmentActivated, this, &MainWindow::openSegment);
}

void MainWindow::updateActions()
{
    const bool open = m_project->isOpen();
    for (QAction *a : std::as_const(m_projectActions))
        a->setEnabled(open);
    const bool canCode = open && m_view->hasSelection() && !m_view->isEditing();
    m_codeCurrent->setEnabled(canCode && m_codes->currentCodeId());
    m_codeNew->setEnabled(canCode);
    m_newSubcode->setEnabled(open && m_codes->currentCodeId());
    m_editText->setEnabled(open && m_view->documentId());

    // While editing a document's text, Ctrl+Z belongs to the text editor.
    const bool editing = m_view->isEditing();
    m_undo->setEnabled(open && !editing && m_project->canUndo());
    m_redo->setEnabled(open && !editing && m_project->canRedo());
    m_undo->setText(m_project->canUndo() ? tr("&Annulla: %1").arg(m_project->undoLabel()) : tr("&Annulla"));
    m_redo->setText(m_project->canRedo() ? tr("&Ripeti: %1").arg(m_project->redoLabel()) : tr("&Ripeti"));
}

void MainWindow::updateTitle()
{
    setWindowTitle(m_project->isOpen() ? tr("%1 — Tessera").arg(m_project->name()) : tr("Tessera"));
    setWindowFilePath(m_project->filePath());
}

void MainWindow::updateStats()
{
    if (!m_project->isOpen()) {
        m_stats->clear();
        return;
    }
    m_stats->setText(tr("%n documenti", nullptr, int(m_project->documents().size())) + QStringLiteral(" · ") +
                     tr("%n codici", nullptr, int(m_project->codes().size())) + QStringLiteral(" · ") +
                     tr("%n segmenti", nullptr, int(m_project->allSegments().size())));
}

void MainWindow::refreshAll()
{
    m_documents->rebuild();
    m_codes->rebuild();
    m_view->onCodesChanged();
    updateRetrieval();
    updateStats();
}

void MainWindow::updateRetrieval()
{
    m_segments->setActivation(m_documents->activeDocumentIds(), m_codes->activeCodeIds());
}

// ---------------------------------------------------------------- project lifecycle

QString MainWindow::askNewProjectPath(const QString &title, const QString &suggestedName)
{
    QString path = Ui::saveFileName(this, title, suggestedName, kProjectFilter);
    if (path.isEmpty())
        return {};
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".tsra");
    return path;
}

bool MainWindow::openProject(const QString &path)
{
    m_view->commitEdits();
    m_view->closeDocument();
    if (!m_project->open(path)) {
        Ui::showError(this, tr("Impossibile aprire il progetto:\n%1").arg(m_project->lastError()));
        return false;
    }
    addRecent(m_project->filePath());
    if (!m_project->migrationBackupPath().isEmpty()) {
        QMessageBox::information(this, tr("Progetto aggiornato"),
                                 tr("Il progetto è stato aggiornato al formato di questa versione di Tessera.\n"
                                    "Una copia del file originale è stata salvata in:\n%1")
                                     .arg(QDir::toNativeSeparators(m_project->migrationBackupPath())));
    }
    statusBar()->showMessage(tr("Progetto aperto: %1").arg(m_project->filePath()), 5000);
    return true;
}

void MainWindow::reopenLastProject()
{
    const QStringList recent = QSettings().value(QStringLiteral("ui/recent")).toStringList();
    if (!recent.isEmpty() && QFile::exists(recent.first()))
        openProject(recent.first());
}

void MainWindow::newProject()
{
    const QString path = askNewProjectPath(tr("Nuovo progetto"), tr("Nuovo progetto.tsra"));
    if (path.isEmpty())
        return;
    m_view->commitEdits();
    m_view->closeDocument();
    if (!m_project->create(path)) {
        Ui::showError(this, tr("Impossibile creare il progetto:\n%1").arg(m_project->lastError()));
        return;
    }
    addRecent(m_project->filePath());
    statusBar()->showMessage(tr("Nuovo progetto creato. Importa i documenti con Ctrl+I."), 8000);
}

void MainWindow::openProjectDialog()
{
    const QString path = Ui::openFileName(this, tr("Apri progetto"), kProjectFilter + QStringLiteral(";;") +
                                                                         tr("Tutti i file (*)"));
    if (!path.isEmpty())
        openProject(path);
}

void MainWindow::closeProject()
{
    m_view->commitEdits();
    m_project->close();
}

void MainWindow::saveCopyAs()
{
    m_view->commitEdits();
    const QString path = askNewProjectPath(tr("Salva una copia del progetto"), m_project->name() + tr(" - copia.tsra"));
    if (path.isEmpty())
        return;
    if (QFileInfo(path).absoluteFilePath() == m_project->filePath()) {
        Ui::showError(this, tr("Scegli un file diverso da quello del progetto aperto."));
        return;
    }
    if (m_project->saveCopyAs(path))
        statusBar()->showMessage(tr("Copia salvata in %1").arg(path), 5000);
    else
        Ui::showError(this, m_project->lastError());
}

bool MainWindow::createExampleProject(const QString &path)
{
    m_view->commitEdits();
    m_view->closeDocument();
    if (!m_project->create(path) || !DemoProject::populate(*m_project)) {
        Ui::showError(this, tr("Impossibile creare il progetto di esempio:\n%1").arg(m_project->lastError()));
        return false;
    }
    m_project->clearUndoHistory();
    addRecent(m_project->filePath());
    return true;
}

void MainWindow::newExampleProject()
{
    const QString path = askNewProjectPath(tr("Crea progetto di esempio"), tr("Esempio lavoro da remoto.tsra"));
    if (!path.isEmpty() && createExampleProject(path))
        showOverview();
}

void MainWindow::showOverview()
{
    if (!m_project->isOpen())
        return;
    const QList<Document> docs = m_project->documents();
    if (!docs.isEmpty()) {
        m_view->openDocument(docs.first().id);
        m_documents->setCurrentDocument(docs.first().id);
    }
    m_codes->setAllActive(true);
}

void MainWindow::importQdpx()
{
    const QString source = Ui::openFileName(this, tr("Importa progetto QDPX"),
                                            tr("Progetto REFI-QDA (*.qdpx);;Tutti i file (*)"));
    if (source.isEmpty())
        return;
    const QString target = askNewProjectPath(tr("Salva il progetto importato come"),
                                             QFileInfo(source).completeBaseName() + QStringLiteral(".tsra"));
    if (target.isEmpty())
        return;
    m_view->commitEdits();
    m_view->closeDocument();
    if (!m_project->create(target)) {
        Ui::showError(this, m_project->lastError());
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString report, error;
    const bool ok = Qdpx::importProject(*m_project, source, &report, &error);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        m_project->close();
        QFile::remove(target);
        Ui::showError(this, tr("Importazione non riuscita:\n%1").arg(error));
        return;
    }
    m_project->clearUndoHistory(); // the import is the starting point of the new project
    addRecent(m_project->filePath());
    QMessageBox::information(this, tr("Importa QDPX"), report);
}

void MainWindow::exportQdpx()
{
    m_view->commitEdits();
    const QString path = Ui::saveFileName(this, tr("Esporta come QDPX"), m_project->name() + QStringLiteral(".qdpx"),
                                          tr("Progetto REFI-QDA (*.qdpx)"));
    if (path.isEmpty())
        return;
    QString error;
    if (Qdpx::exportProject(*m_project, path, &error))
        statusBar()->showMessage(tr("Progetto esportato in %1").arg(path), 5000);
    else
        Ui::showError(this, error);
}

// ---------------------------------------------------------------- documents

void MainWindow::importDocuments(qint64 groupId)
{
    const QStringList files = Ui::openFileNames(this, tr("Importa documenti"), Importers::fileDialogFilter());
    if (files.isEmpty())
        return;
    if (!groupId)
        groupId = m_documents->currentGroupId();

    QProgressDialog progress(tr("Importazione dei documenti…"), tr("Annulla"), 0, int(files.size()), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(400);
    QStringList failures;
    qint64 lastId = 0;
    m_project->setUndoLabel(tr("Importa documenti"));
    m_project->beginBatch();
    for (int i = 0; i < files.size(); ++i) {
        progress.setValue(i);
        if (progress.wasCanceled())
            break;
        const Importers::Result r = Importers::importFile(files[i]);
        if (!r.ok) {
            failures << QFileInfo(files[i]).fileName() + QStringLiteral(": ") + r.error;
            continue;
        }
        lastId = m_project->addDocument(groupId, QFileInfo(files[i]).completeBaseName(), r.text, files[i]);
    }
    progress.setValue(int(files.size()));
    m_project->commitBatch();
    if (lastId)
        m_view->openDocument(lastId);
    if (!failures.isEmpty())
        QMessageBox::warning(this, tr("Importa documenti"),
                             tr("Alcuni file non sono stati importati:\n\n%1").arg(failures.join(QLatin1Char('\n'))));
}

void MainWindow::newTextDocument(qint64 groupId)
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Nuovo documento di testo"));
    dlg.resize(640, 480);
    auto *name = new QLineEdit(tr("Nuovo documento"));
    auto *text = new QPlainTextEdit;
    text->setPlaceholderText(tr("Incolla o scrivi qui il testo (es. una trascrizione)…"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *form = new QFormLayout;
    form->addRow(tr("Nome"), name);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(text, 1);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted || name->text().trimmed().isEmpty())
        return;
    const qint64 id = m_project->addDocument(groupId ? groupId : m_documents->currentGroupId(), name->text().trimmed(),
                                             TextUtils::normalizeText(text->toPlainText()));
    if (id)
        m_view->openDocument(id);
}

void MainWindow::showVariables()
{
    VariablesDialog dlg(m_project, this);
    dlg.exec();
}

// ---------------------------------------------------------------- coding

void MainWindow::codeWithCurrent()
{
    const qint64 code = m_codes->currentCodeId();
    if (!code) {
        Ui::showError(this, tr("Seleziona un codice nel pannello Codici."));
        return;
    }
    if (!m_view->hasSelection()) {
        statusBar()->showMessage(tr("Seleziona prima del testo nel documento."), 4000);
        return;
    }
    m_view->codeSelection(code);
}

void MainWindow::codeWithNewCode()
{
    if (!m_view->hasSelection()) {
        statusBar()->showMessage(tr("Seleziona prima del testo nel documento."), 4000);
        return;
    }
    const QString suggestion = m_view->selectedText().simplified().left(40);
    const QString name = Ui::askText(this, tr("Codifica con un nuovo codice"),
                                     tr("Nome del nuovo codice (creato al primo livello):"), suggestion);
    if (name.isEmpty())
        return;
    // Creating the code and coding the selection are undone together.
    m_project->setUndoLabel(tr("Codifica con nuovo codice"));
    m_project->beginBatch();
    qint64 code = m_project->findCode(0, name);
    if (!code)
        code = m_project->addCode(0, name, TextUtils::paletteColor(int(m_project->codes().size())));
    if (code)
        m_view->codeSelection(code);
    m_project->commitBatch();
    if (code)
        m_codes->setCurrentCode(code);
}

void MainWindow::exportCodebook()
{
    const QString path = Ui::saveFileName(this, tr("Esporta codebook"), m_project->name() + tr(" - codebook.html"),
                                          tr("Pagina HTML (*.html);;CSV per Excel (*.csv)"));
    if (path.isEmpty())
        return;
    QString error;
    const bool ok = path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive)
                        ? Exporters::writeCsv(path, Exporters::codebookTable(*m_project), &error)
                        : Exporters::exportCodebookHtml(*m_project, path, &error);
    if (!ok)
        Ui::showError(this, error);
}

// ---------------------------------------------------------------- analysis

void MainWindow::showSearch()
{
    auto *dlg = new SearchDialog(m_project, [this] { return m_documents->activeDocumentIds(); },
                                 [this] { return m_codes->currentCodeId(); }, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &SearchDialog::hitActivated, this,
            [this](qint64 doc, int start, int end) { openSegment(doc, start, end, 0); });
    connect(m_project, &Project::closed, dlg, &QDialog::close);
    dlg->show();
}

void MainWindow::showRetrieval()
{
    auto *dlg = new RetrievalDialog(m_project, m_documents->activeDocumentIds(), m_codes->activeCodeIds(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &RetrievalDialog::retrievalRequested, m_segments, &SegmentsPanel::setRetrieval);
    dlg->show();
}

void MainWindow::showCooccurrence()
{
    auto *dlg = new CooccurrenceDialog(m_project, m_documents->activeDocumentIds(), m_codes->activeCodeIds(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &CooccurrenceDialog::passagesRequested, m_segments, &SegmentsPanel::setPassages);
    connect(m_project, &Project::closed, dlg, &QDialog::close);
    dlg->show();
}

void MainWindow::showStats()
{
    auto *dlg = new StatsDialog(m_project, m_documents->activeDocumentIds(), m_codes->activeCodeIds(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_project, &Project::closed, dlg, &QDialog::close);
    dlg->show();
}

void MainWindow::showVariableFilter()
{
    auto *dlg = new VariableFilterDialog(m_project, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &VariableFilterDialog::activationRequested, m_documents, &DocumentTree::setActiveDocuments);
    connect(dlg, &VariableFilterDialog::setRequested, this,
            [this](const QList<qint64> &ids) { m_documents->newDocumentSet(ids); });
    connect(m_project, &Project::closed, dlg, &QDialog::close);
    dlg->show();
}

void MainWindow::showMatrix()
{
    auto *dlg = new MatrixDialog(m_project, m_documents->activeDocumentIds(), m_codes->activeCodeIds(), this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &MatrixDialog::cellActivated, this, [this](qint64 code, qint64 doc) {
        // Show that code's segments in that document.
        m_codes->setCurrentCode(code);
        const QList<Segment> segs = m_project->segments({doc}, {code});
        if (!segs.isEmpty())
            openSegment(doc, segs.first().start, segs.first().end, segs.first().id);
        else
            m_view->openDocument(doc);
    });
    connect(m_project, &Project::closed, dlg, &QDialog::close);
    dlg->show();
}

void MainWindow::openSegment(qint64 documentId, int start, int end, qint64 segmentId)
{
    m_view->openDocument(documentId);
    m_documents->setCurrentDocument(documentId);
    m_view->selectRange(start, end, segmentId);
}

// ---------------------------------------------------------------- memos

void MainWindow::editProjectMemo()
{
    Ui::editMemo(this, m_project, int(MemoTarget::Project), 0, tr("progetto \"%1\"").arg(m_project->name()));
}

void MainWindow::showMemos()
{
    auto *dlg = new MemoManager(m_project, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &MemoManager::showTarget, this, &MainWindow::showMemoTarget);
    connect(m_project, &Project::closed, dlg, &QDialog::close);
    dlg->show();
}

void MainWindow::showMemoTarget(int targetType, qint64 targetId)
{
    switch (static_cast<MemoTarget>(targetType)) {
    case MemoTarget::Document:
        openSegment(targetId, 0, 0, 0);
        break;
    case MemoTarget::Code:
        m_codes->setCurrentCode(targetId);
        break;
    case MemoTarget::Segment: {
        const Segment s = m_project->segment(targetId);
        if (s.id)
            openSegment(s.documentId, s.start, s.end, s.id);
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------- settings, recent files

void MainWindow::setUserName()
{
    const QString name = Ui::askText(this, tr("Nome utente"),
                                     tr("Nome registrato come autore di codifiche e memo:"), m_project->author());
    if (name.isEmpty())
        return;
    m_project->setAuthor(name);
    QSettings().setValue(QStringLiteral("user/name"), name);
}

void MainWindow::about()
{
    QMessageBox::about(this, tr("Informazioni su Tessera"),
                       tr("<h3>Tessera %1</h3>"
                          "<p>Software per l'analisi qualitativa dei dati: documenti, sistema di codici, codifica, "
                          "memo, variabili, ricerca e matrici.</p>"
                          "<p>I progetti sono file SQLite (.tsra) salvati automaticamente a ogni modifica. "
                          "Lo scambio con altri software QDA avviene tramite lo standard aperto REFI-QDA (.qdpx).</p>"
                          "<p>Realizzato con Qt %2.</p>")
                           .arg(QStringLiteral(TESSERA_VERSION_STRING), QString::fromLatin1(qVersion())));
}

void MainWindow::addRecent(const QString &path)
{
    QSettings s;
    QStringList recent = s.value(QStringLiteral("ui/recent")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > MaxRecent)
        recent.removeLast();
    s.setValue(QStringLiteral("ui/recent"), recent);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();
    const QStringList recent = QSettings().value(QStringLiteral("ui/recent")).toStringList();
    for (const QString &path : recent) {
        QAction *a = m_recentMenu->addAction(QFileInfo(path).completeBaseName(), this, [this, path] { openProject(path); });
        a->setToolTip(path);
        a->setEnabled(QFile::exists(path));
    }
    m_recentMenu->setEnabled(!recent.isEmpty());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    m_view->commitEdits();
    QSettings s;
    s.setValue(QStringLiteral("ui/geometry"), saveGeometry());
    s.setValue(QStringLiteral("ui/mainSplitter"), m_mainSplitter->saveState());
    s.setValue(QStringLiteral("ui/leftSplitter"), m_leftSplitter->saveState());
    s.setValue(QStringLiteral("ui/rightSplitter"), m_rightSplitter->saveState());
    m_project->close();
    event->accept();
}
