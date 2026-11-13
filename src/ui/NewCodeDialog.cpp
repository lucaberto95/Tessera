#include "ui/NewCodeDialog.h"
#include "core/Project.h"
#include "core/TextUtils.h"
#include "ui/UiUtils.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>

NewCodeDialog::NewCodeDialog(Project *project, qint64 parentId, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
{
    setWindowTitle(parentId ? tr("Nuovo sottocodice") : tr("Nuovo codice"));
    resize(420, 340);

    m_name = new QLineEdit;
    m_name->setFocus();

    m_parent = new QComboBox;
    m_parent->addItem(tr("(livello superiore)"), qint64(0));
    // Tree order, indented by depth.
    const QList<Code> codes = project->codesInTreeOrder();
    QHash<qint64, qint64> parentOf;
    for (const Code &c : codes)
        parentOf.insert(c.id, c.parentId);
    for (const Code &c : codes) {
        int depth = 0;
        for (qint64 p = c.parentId; p && depth < 64; p = parentOf.value(p))
            ++depth;
        m_parent->addItem(Ui::colorIcon(c.color, 16), QString(depth * 4, QLatin1Char(' ')) + c.name, c.id);
    }
    const int index = m_parent->findData(parentId);
    m_parent->setCurrentIndex(index < 0 ? 0 : index);

    m_colorButton = new QToolButton;
    m_colorButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_colorButton->setText(tr("Scegli…"));
    connect(m_colorButton, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_color, this, tr("Colore del codice"));
        if (c.isValid()) {
            m_colorPicked = true;
            setColor(c);
        }
    });
    // Until the user picks a color, it follows the chosen parent.
    connect(m_parent, &QComboBox::currentIndexChanged, this, [this] {
        if (!m_colorPicked)
            applyDefaultColor();
    });
    applyDefaultColor();

    m_description = new QPlainTextEdit;
    m_description->setPlaceholderText(tr("Definizione del codice, criteri di applicazione, esempi…"));

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(m_name, &QLineEdit::textChanged, this, [this](const QString &) {
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!name().isEmpty());
    });

    auto *form = new QFormLayout(this);
    form->addRow(tr("Nome:"), m_name);
    form->addRow(tr("Posizione:"), m_parent);
    form->addRow(tr("Colore:"), m_colorButton);
    form->addRow(tr("Descrizione:"), m_description);
    form->addRow(m_buttons);
}

QString NewCodeDialog::name() const
{
    return m_name->text().trimmed();
}

qint64 NewCodeDialog::parentId() const
{
    return m_parent->currentData().toLongLong();
}

QString NewCodeDialog::description() const
{
    return m_description->toPlainText();
}

void NewCodeDialog::applyDefaultColor()
{
    const qint64 parent = parentId();
    setColor(parent ? m_project->code(parent).color : TextUtils::paletteColor(int(m_project->codes().size())));
}

void NewCodeDialog::setColor(const QColor &color)
{
    m_color = color;
    m_colorButton->setIcon(Ui::colorIcon(color, 16));
}
