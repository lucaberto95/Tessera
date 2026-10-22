#pragma once

#include "core/Agreement.h"
#include "core/Exporters.h"

#include <QDialog>
#include <memory>

class Project;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
namespace Merge {
class Source;
}

// Intercoder agreement (issue #23): two coders of this project (authors of the codings) or this project against
// another .tsra with the same documents; existence / frequency / segment level, per-code table with percentage
// and kappa, clickable list of disagreements, export.
class AgreementDialog : public QDialog {
    Q_OBJECT
public:
    AgreementDialog(Project *project, const QList<qint64> &activeDocuments, const QList<qint64> &activeCodes,
                    QWidget *parent = nullptr);
    ~AgreementDialog() override;

    // Test hooks. Coders by author name (empty = all authors); `setOtherProject` compares this project (A) with a
    // file (B) instead of two authors of this project.
    void setCoders(const QString &authorA, const QString &authorB);
    bool setOtherProject(const QString &path, QString *error);
    void setLevel(Agreement::Level level);
    void setMinOverlap(int percent);
    const Agreement::Result &result() const { return m_result; }
    int comparedDocuments() const { return m_docs.size(); }

signals:
    // Opens a disagreement: start/end are -1 for non-text segments (just open the document); segmentId is 0 when
    // the segment belongs to another project.
    void disagreementActivated(qint64 documentId, int start, int end, qint64 segmentId);

private:
    void rebuild();
    void chooseOtherFile();
    void fillAuthors();
    void exportResults();
    QString excerpt(const Segment &segment, qint64 documentId, bool fromOther) const;

    Project *m_project;
    QList<qint64> m_activeDocs;
    QList<qint64> m_activeCodes;
    std::unique_ptr<Merge::Source> m_other;
    QHash<qint64, qint64> m_docMap, m_codeMap; // other project -> this project
    QList<qint64> m_docs, m_codes;

    QComboBox *m_source;
    QComboBox *m_coderA;
    QComboBox *m_coderB;
    QLabel *m_fileLabel;
    QPushButton *m_fileButton;
    QComboBox *m_level;
    QSpinBox *m_overlap;
    QLabel *m_overlapLabel;
    QLabel *m_info;
    QTableWidget *m_resultTable;
    QTableWidget *m_disagreementTable;
    Agreement::Result m_result;
    QList<Agreement::Disagreement> m_shown;
    Exporters::Table m_resultExport, m_disagreementExport;
};
