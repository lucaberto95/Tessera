#pragma once

#include <QColor>
#include <QDialog>

class Project;
class QComboBox;
class QDialogButtonBox;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;

// Creation dialog for a code: name, position in the hierarchy, color and description (#67).
class NewCodeDialog : public QDialog {
    Q_OBJECT
public:
    NewCodeDialog(Project *project, qint64 parentId, QWidget *parent = nullptr);

    QString name() const;
    qint64 parentId() const;
    QColor color() const { return m_color; }
    QString description() const;

    QLineEdit *nameEdit() const { return m_name; }
    QComboBox *parentCombo() const { return m_parent; }

private:
    void applyDefaultColor();
    void setColor(const QColor &color);

    Project *m_project;
    QLineEdit *m_name = nullptr;
    QComboBox *m_parent = nullptr;
    QToolButton *m_colorButton = nullptr;
    QPlainTextEdit *m_description = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
    QColor m_color;
    bool m_colorPicked = false;
};
