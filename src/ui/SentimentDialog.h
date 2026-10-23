#pragma once

#include "core/Retrieval.h"
#include "core/Sentiment.h"

#include <QDialog>

#include <functional>

class Project;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;

// Exploratory sentiment analysis (lexicon based) of the retrieved segments, of the sentences or of the
// whole documents: scores in a sortable table, distribution bar, filter by polarity and coding with the
// sub-codes Positivo / Negativo / Neutro.
class SentimentDialog : public QDialog {
    Q_OBJECT
public:
    SentimentDialog(Project *project, std::function<QList<qint64>()> activeDocuments,
                    std::function<QList<Segment>()> retrievedSegments, QWidget *parent = nullptr);

    void setUnit(Sentiment::Unit unit);
    void setLanguage(Sentiment::Language language);
    void analyze();
    const QList<Sentiment::Item> &items() const { return m_items; }
    // Shows only the items of that polarity (-1 = all). Returns the number of visible rows.
    int setPolarityFilter(int polarity);
    int visibleRows() const;
    // Codes every analysed item under the top-level code `parentName`; returns the new segments.
    int codeItems(const QString &parentName);

signals:
    void passagesRequested(const QList<Retrieval::Result> &passages, const QString &label);
    void passageActivated(qint64 documentId, int start, int end);

private:
    void fillTable();
    void codeInteractive();
    void showInPanel();

    Project *m_project;
    std::function<QList<qint64>()> m_activeDocuments;
    std::function<QList<Segment>()> m_retrieved;
    QList<Sentiment::Item> m_items;
    QComboBox *m_unit;
    QComboBox *m_language;
    QComboBox *m_filter;
    QLabel *m_summary;
    QWidget *m_bar;
    QTableWidget *m_table;
    QPushButton *m_codeButton;
    QPushButton *m_showButton;
};
