#pragma once

#include "core/Dictionaries.h"
#include "core/Exporters.h"

#include <QDialog>

#include <functional>

class Project;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTabWidget;
class QTableWidget;
class QTreeWidget;

// Dictionaries for content analysis: editor of categories and entries, the documents x categories table,
// automatic coding of the occurrences and counts saved as document variables.
class DictionaryDialog : public QDialog {
    Q_OBJECT
public:
    enum class Values { Counts, PercentOfWords };

    DictionaryDialog(Project *project, std::function<QList<qint64>()> activeDocuments, QWidget *parent = nullptr);

    qint64 currentDictionaryId() const;
    void selectDictionary(qint64 id);
    qint64 currentCategoryId() const;
    void selectCategory(qint64 id);

    // "Analisi" tab: options, then runAnalysis() fills the table.
    void setActiveDocumentsOnly(bool on);
    void setValues(Values values);
    void setWithVariables(bool on);
    void runAnalysis();
    const Dictionaries::Analysis &analysis() const { return m_analysis; }
    const Exporters::Table &analysisTable() const { return m_export; }

    // Recomputes the analysis on the current text and codes the occurrences (see Dictionaries::autocode).
    int autocode(const QList<qint64> &categories, const QHash<qint64, qint64> &codes, Dictionaries::Extent extent);

signals:
    void documentActivated(qint64 documentId);

private:
    void reloadDictionaries();
    void reloadCategories();
    void reloadEntries();
    void fillAnalysisTable();

    void newDictionary();
    void renameDictionary();
    void deleteDictionary();
    void newCategory(bool sub);
    void renameCategory();
    void deleteCategory();
    void addEntries();
    void editEntry();
    void removeEntries();
    void importDictionary();
    void exportDictionary();
    void exportAnalysis();
    void saveAsVariables();
    void showAutocode();

    Project *m_project;
    std::function<QList<qint64>()> m_activeDocuments;
    Dictionaries::Analysis m_analysis;
    Exporters::Table m_export;
    bool m_analysisStale = true;

    QComboBox *m_dictionary;
    QTabWidget *m_tabs;
    QTreeWidget *m_categories;
    QListWidget *m_entries;
    QLineEdit *m_entryEdit;
    QLabel *m_entriesLabel;

    QComboBox *m_scope;
    QCheckBox *m_caseSensitive;
    QComboBox *m_values;
    QCheckBox *m_withVariables;
    QLabel *m_summary;
    QTableWidget *m_table;
};
