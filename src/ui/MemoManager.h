#pragma once

#include <QDialog>

class Project;
struct Memo;
class QTableWidget;

// Lists all memos of the project; double-click opens one.
class MemoManager : public QDialog {
    Q_OBJECT
public:
    explicit MemoManager(Project *project, QWidget *parent = nullptr);

signals:
    void showTarget(int targetType, qint64 targetId);

private:
    void refresh();
    QString targetLabel(const Memo &memo) const;
    void openRow(int row);

    Project *m_project;
    QTableWidget *m_table;
};
