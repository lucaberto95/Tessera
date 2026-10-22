#pragma once

#include <QDialog>

#include <functional>

class Project;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableWidget;

// Lexical search across documents, with optional automatic coding of the hits.
class SearchDialog : public QDialog {
    Q_OBJECT
public:
    SearchDialog(Project *project, std::function<QList<qint64>()> activeDocuments,
                 std::function<qint64()> currentCode, QWidget *parent = nullptr);

signals:
    void hitActivated(qint64 documentId, int start, int end);

private:
    struct Hit {
        qint64 documentId;
        int start;
        int end;
        QString match; // to skip hits whose document changed after the search
    };
    void search();
    void autocode();

    Project *m_project;
    std::function<QList<qint64>()> m_activeDocuments;
    std::function<qint64()> m_currentCode;
    QLineEdit *m_query;
    QCheckBox *m_caseSensitive;
    QCheckBox *m_wholeWords;
    QCheckBox *m_regex;
    QComboBox *m_scope;
    QComboBox *m_extent;
    QLabel *m_summary;
    QTableWidget *m_table;
    QList<Hit> m_hits;
};
