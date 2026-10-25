#pragma once

#include "core/Model.h"
#include "core/Topics.h"

#include <QDialog>
#include <QFutureWatcher>

#include <atomic>
#include <functional>
#include <memory>

class Project;
class QCheckBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QSpinBox;
class QTabWidget;
class QTableWidget;

// Exploratory topic modeling (LDA) of the documents, their paragraphs or the retrieved segments: the
// topics with their words, the most representative passages, the assignment of every unit, codes created
// from the topics and an export. The computation runs in a worker thread.
class TopicsDialog : public QDialog {
    Q_OBJECT
public:
    enum class Scope { AllDocuments, ActiveDocuments, RetrievedSegments };

    TopicsDialog(Project *project, std::function<QList<qint64>()> activeDocuments,
                 std::function<QList<Segment>()> retrievedSegments, QWidget *parent = nullptr);
    ~TopicsDialog() override;

    void setScope(Scope scope);
    void setParagraphs(bool on);
    void setTopicCount(int count); // 0 = automatic
    void setSeed(int seed);
    // Starts the computation (the previous one is cancelled).
    void run();
    bool isBusy() const { return m_busy; }
    const Topics::Result &result() const { return m_result; }
    // Creates the codes from the last result (see Topics::createCodes); no confirmation.
    Topics::CodeResult createCodes(const Topics::CodeOptions &options);
    int topicRowCount() const;
    int unitRowCount() const;
    void selectTopic(int index);
    int passageRowCount() const;

signals:
    void passageActivated(qint64 documentId, int start, int end);

private:
    void finished();
    void fillTables();
    void fillPassages();
    void createCodesInteractive();
    void exportTables();

    Project *m_project;
    std::function<QList<qint64>()> m_activeDocuments;
    std::function<QList<Segment>()> m_retrieved;

    QComboBox *m_scope;
    QComboBox *m_unit;
    QSpinBox *m_topics;
    QSpinBox *m_seed;
    QSpinBox *m_minLength;
    QCheckBox *m_useStopList;
    QProgressBar *m_progress;
    QLabel *m_summary;
    QTabWidget *m_tabs;
    QTableWidget *m_topicTable;
    QTableWidget *m_passageTable;
    QTableWidget *m_unitTable;

    QFutureWatcher<Topics::Result> m_watcher;
    QList<QFuture<Topics::Result>> m_running;
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_busy = false;
    int m_generation = 0;
    Topics::Result m_result;
    QHash<qint64, QString> m_texts;
    QHash<qint64, QString> m_names;
};
