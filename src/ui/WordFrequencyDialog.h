#pragma once

#include "core/Model.h"
#include "core/WordFrequencies.h"

#include <QDialog>
#include <QFutureWatcher>

#include <atomic>
#include <functional>
#include <memory>

class Project;
class QCheckBox;
class QComboBox;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QProgressBar;
class QSpinBox;
class QTabWidget;
class QTableWidget;

// Lexical analysis: word frequencies (stop list, comparison columns), word cloud and keyword in context.
// The computation runs in a worker thread (QtConcurrent) with a progress bar.
class WordFrequencyDialog : public QDialog {
    Q_OBJECT
public:
    enum class Scope { AllDocuments, ActiveDocuments, RetrievedSegments };
    // Comparison values: None, DocumentGroups, DocumentSets or the id of a variable (> 0).
    static constexpr qint64 CompareNone = 0;
    static constexpr qint64 CompareGroups = -1;
    static constexpr qint64 CompareSets = -2;

    WordFrequencyDialog(Project *project, std::function<QList<qint64>()> activeDocuments,
                        std::function<QList<Segment>()> retrievedSegments, QWidget *parent = nullptr);
    ~WordFrequencyDialog() override;

    void setScope(Scope scope);
    void setComparison(qint64 comparison);
    void setMinLength(int length);
    void setCaseSensitive(bool on);
    void setUseStopList(bool on);
    void setExcludeNumbers(bool on);
    void setTopWords(int count);
    // Starts the computation (the previous one is cancelled). The table, the cloud and the summary are
    // updated when it ends.
    void run();
    bool isBusy() const;
    const Words::Result &result() const { return m_result; }

    // Words of the selected rows of the table; selectWords() selects the rows of these words.
    QStringList selectedWords() const;
    void selectWords(const QStringList &words);
    int visibleRows() const;
    // Adds the selected words to the stop list / to a dictionary category (see Words::addToDictionary).
    void excludeSelectedWords();
    int addSelectedToDictionary(qint64 categoryId);
    int cloudWordCount() const { return m_cloudWords; }

    // "Parole in contesto" tab for a word (runs after the computation if it is still going).
    void showKeyword(const QString &word);
    const QList<Words::Occurrence> &occurrences() const { return m_occurrences; }
    void activateOccurrence(int row);

signals:
    void occurrenceActivated(qint64 documentId, int start, int end);
    void searchRequested(const QString &word);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void finished();
    void fillTable();
    void applyFilter();
    void rebuildCloud();
    void fitCloud();
    void runKeyword();
    void editStopList();
    void addSelectedToDictionaryInteractive();
    void searchSelected();
    void keywordSelected();
    void exportRows(const QString &name, const QList<QStringList> &rows);
    void exportCloud(bool svg);

    Project *m_project;
    std::function<QList<qint64>()> m_activeDocuments;
    std::function<QList<Segment>()> m_retrieved;

    QTabWidget *m_tabs;
    QComboBox *m_scope;
    QComboBox *m_compare;
    QSpinBox *m_minLength;
    QCheckBox *m_caseSensitive;
    QCheckBox *m_excludeNumbers;
    QCheckBox *m_useStopList;
    QProgressBar *m_progress;
    QLabel *m_summary;
    QLineEdit *m_filter;
    QTableWidget *m_table;
    QSpinBox *m_topWords;
    QGraphicsScene *m_scene;
    QGraphicsView *m_cloudView;
    QLineEdit *m_keywordEdit;
    QSpinBox *m_width;
    QLabel *m_keywordSummary;
    QTableWidget *m_keywordTable;

    QFutureWatcher<Words::Result> m_watcher;
    QList<QFuture<Words::Result>> m_running; // waited for in the destructor
    std::shared_ptr<std::atomic_bool> m_cancel;
    bool m_busy = false;
    bool m_runCase = false;
    int m_generation = 0;
    Words::Result m_result;
    QList<Words::Source> m_sources;
    QHash<qint64, QString> m_texts;
    bool m_resultCaseSensitive = false;
    QString m_pendingKeyword;
    QList<Words::Occurrence> m_occurrences;
    QList<QStringList> m_exportTable;
    int m_cloudWords = 0;
};
