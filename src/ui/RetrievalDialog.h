#pragma once

#include "core/Retrieval.h"

#include <QDialog>

class Project;
class QComboBox;
class QLabel;
class QPushButton;
class QListWidget;
class QSpinBox;

// Builds a complex retrieval query: mode, code lists A and B and the numeric parameters.
class RetrievalDialog : public QDialog {
    Q_OBJECT
public:
    RetrievalDialog(Project *project, const QList<qint64> &activeDocuments, const QList<qint64> &activeCodes,
                    QWidget *parent = nullptr);

    Retrieval::Query query() const;
    int previewCount() const;

signals:
    void retrievalRequested(const Retrieval::Query &query);

private:
    void updateState();
    QList<qint64> checkedCodes(QListWidget *list) const;

    Project *m_project;
    QList<qint64> m_docIds;
    QComboBox *m_mode;
    QListWidget *m_listA;
    QListWidget *m_listB;
    QSpinBox *m_count;
    QSpinBox *m_distance;
    QComboBox *m_unit;
    QLabel *m_preview;
    QLabel *m_description;
    QPushButton *m_apply;
};
