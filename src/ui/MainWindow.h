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

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    void createActions();
    void createMenus();
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
    void exportQdpx();
    void newExampleProject();
    void importDocuments(qint64 groupId);
    void newTextDocument(qint64 groupId);
    void codeWithCurrent();
    void codeWithNewCode();
    void showSearch();
    void showMatrix();
    void showVariables();
    void showMemos();
    void editProjectMemo();
    void exportCodebook();
    void setUserName();
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
    QAction *m_exportQdpx = nullptr;
    QAction *m_example = nullptr;
    QAction *m_quit = nullptr;
    QAction *m_undo = nullptr;
    QAction *m_redo = nullptr;
    QAction *m_importDocs = nullptr;
    QAction *m_newTextDoc = nullptr;
    QAction *m_newGroup = nullptr;
    QAction *m_newDocumentSet = nullptr;
    QAction *m_documentSetFromActive = nullptr;
    QAction *m_variables = nullptr;
    QAction *m_newCode = nullptr;
    QAction *m_newSubcode = nullptr;
    QAction *m_codeCurrent = nullptr;
    QAction *m_codeNew = nullptr;
    QAction *m_editText = nullptr;
    QAction *m_exportCodebook = nullptr;
    QAction *m_search = nullptr;
    QAction *m_matrix = nullptr;
    QAction *m_exportSegmentsHtml = nullptr;
    QAction *m_exportSegmentsCsv = nullptr;
    QAction *m_projectMemo = nullptr;
    QAction *m_memos = nullptr;
    QAction *m_userName = nullptr;
    QAction *m_about = nullptr;
};
