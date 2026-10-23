#pragma once

#include <QMainWindow>

class CodeTree;
class DocumentTree;
class DocumentView;
class Project;
class QLabel;
class QMenu;
class QSplitter;
class SegmentsPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool openProject(const QString &path);
    bool createExampleProject(const QString &path);
    // Opens the first document and activates all codes, so every panel shows content.
    void showOverview();
    void reopenLastProject();
    // First launch: asks the name recorded as author of codings and memos (the merge of projects tells
    // people's work apart by it). Does nothing once a name has been saved.
    void askUserNameOnFirstRun();
    // Looks for a newer release. At startup (manual = false) it is silent unless an update exists and does nothing
    // when the user turned the check off; from the Aiuto menu (manual = true) it always reports the outcome.
    void checkForUpdates(bool manual);
    void checkForUpdatesAtStartup();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createMenus();
    void createLanguageMenu(QMenu *settings);
    void createToolBar();
    void connectProject();
    void updateActions();
    void updateTitle();
    void updateStats();
    void refreshAll();
    void updateRetrieval();

    void newProject();
    void openProjectDialog();
    void closeProject();
    void saveCopyAs();
    void importQdpx();
    void mergeProject();
    void exportQdpx();
    void exportProjectXlsx();
    void newExampleProject();
    void importDocuments(qint64 groupId);
    void importSurvey();
    void newTextDocument(qint64 groupId);
    void codeWithCurrent();
    void codeWithNewCode();
    void showSearch();
    void showMatrix();
    void showRetrieval();
    void showCooccurrence();
    void showAgreement();
    void showStats();
    void showSummaryGrid();
    void showSummaryTables();
    void showDictionaries();
    void showSentiment();
    void showWordFrequencies();
    void showVariableFilter();
    void showVariables();
    void showMemos();
    void showLinks();
    void editProjectMemo();
    void exportCodebook();
    void createReport();
    void setUserName();
    void showAiSettings();
    // AI functions (issue #40): each opens AiAssistDialog, which sends nothing until the user presses "Genera".
    void aiSummarizeDocument(qint64 documentId);
    void aiSummarizeCode(qint64 codeId);
    void aiPassage(int task, qint64 segmentId);
    // Help with the code system (issue #41): each opens AiCodesDialog, which sends nothing until "Genera".
    void aiSuggestCodes(qint64 documentId, bool selectionOnly);
    void aiSuggestSubcodes(qint64 codeId);
    void aiGroupCodes(qint64 parentId);
    void about();
    void openSegment(qint64 documentId, int start, int end, qint64 segmentId);
    void showMemoTarget(int targetType, qint64 targetId);
    void addRecent(const QString &path);
    void rebuildRecentMenu();
    QString askNewProjectPath(const QString &title, const QString &suggestedName);

    Project *m_project;
    DocumentTree *m_documents;
    CodeTree *m_codes;
    DocumentView *m_view;
    SegmentsPanel *m_segments;
    QSplitter *m_mainSplitter;
    QSplitter *m_leftSplitter;
    QSplitter *m_rightSplitter;
    QLabel *m_stats;
    QMenu *m_recentMenu = nullptr;

    QList<QAction *> m_projectActions; // enabled only with an open project
    QAction *m_newProject = nullptr;
    QAction *m_openProject = nullptr;
    QAction *m_saveCopy = nullptr;
    QAction *m_closeProject = nullptr;
    QAction *m_importQdpx = nullptr;
    QAction *m_mergeProject = nullptr;
    QAction *m_exportQdpx = nullptr;
    QAction *m_exportProjectXlsx = nullptr;
    QAction *m_example = nullptr;
    QAction *m_quit = nullptr;
    QAction *m_undo = nullptr;
    QAction *m_redo = nullptr;
    QAction *m_importDocs = nullptr;
    QAction *m_importSurvey = nullptr;
    QAction *m_newTextDoc = nullptr;
    QAction *m_newGroup = nullptr;
    QAction *m_newDocumentSet = nullptr;
    QAction *m_documentSetFromActive = nullptr;
    QAction *m_variables = nullptr;
    QAction *m_activateByVariables = nullptr;
    QAction *m_newCode = nullptr;
    QAction *m_newSubcode = nullptr;
    QAction *m_codeCurrent = nullptr;
    QAction *m_codeNew = nullptr;
    QAction *m_editText = nullptr;
    QAction *m_startLink = nullptr;
    QAction *m_completeLink = nullptr;
    QAction *m_links = nullptr;
    QAction *m_exportCodebook = nullptr;
    QAction *m_createReport = nullptr;
    QAction *m_search = nullptr;
    QAction *m_matrix = nullptr;
    QAction *m_retrieval = nullptr;
    QAction *m_cooccurrence = nullptr;
    QAction *m_agreement = nullptr;
    QAction *m_statsAction = nullptr;
    QAction *m_summaryGrid = nullptr;
    QAction *m_summaryTables = nullptr;
    QAction *m_dictionaries = nullptr;
    QAction *m_sentiment = nullptr;
    QAction *m_wordFrequencies = nullptr;
    QAction *m_exportSegmentsHtml = nullptr;
    QAction *m_exportSegmentsXlsx = nullptr;
    QAction *m_exportSegmentsCsv = nullptr;
    QAction *m_projectMemo = nullptr;
    QAction *m_memos = nullptr;
    QAction *m_userName = nullptr;
    QAction *m_aiSettings = nullptr;
    QAction *m_aiSummarizeDoc = nullptr;
    QAction *m_aiSummarizeCode = nullptr;
    QAction *m_aiExplain = nullptr;
    QAction *m_aiTranslate = nullptr;
    QAction *m_aiSuggestCodes = nullptr;
    QAction *m_aiSuggestCodesSel = nullptr;
    QAction *m_aiSuggestSubcodes = nullptr;
    QAction *m_aiGroupCodes = nullptr;
    QAction *m_about = nullptr;
    QAction *m_checkUpdates = nullptr;
    QAction *m_autoUpdateCheck = nullptr;
    bool m_updating = false;
};
