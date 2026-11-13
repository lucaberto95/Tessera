#include "ui/NewCodeDialog.h"
#include "core/Project.h"
#include "core/TextUtils.h"
#include "ui/UiUtils.h"

#include <QColorDialog>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
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
    m_codes = project->codesInTreeOrder();
    QHash<qint64, qint64> parentOf;
    for (const Code &c : m_codes)
        parentOf.insert(c.id, c.parentId);
    for (const Code &c : m_codes) {
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

    // Suggest existing code names while typing (#66).
    QStringList names;
    for (const Code &c : m_codes)
        names << c.name;
    names.removeDuplicates();
    auto *completer = new QCompleter(names, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    m_name->setCompleter(completer);
    connect(completer, QOverload<const QString &>::of(&QCompleter::activated), this, [this](const QString &text) {
        for (const Code &c : m_codes) {
            if (c.name.compare(text, Qt::CaseInsensitive) == 0) {
                const int i = m_parent->findData(c.parentId);
                if (i >= 0)
                    m_parent->setCurrentIndex(i);
                break;
            }
        }
    });

    m_hint = new QLabel;
    m_hint->setTextFormat(Qt::PlainText);
    m_hint->setWordWrap(true);
    m_hint->hide();
    connect(m_name, &QLineEdit::textChanged, this, [this] { updateExisting(); });
    connect(m_parent, &QComboBox::currentIndexChanged, this, [this] { updateExisting(); });

    auto *form = new QFormLayout(this);
    form->addRow(tr("Nome:"), m_name);
    form->addRow(m_hint);
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

qint64 NewCodeDialog::existingCodeId() const
{
    const QString n = name();
    qint64 found = 0;
    for (const Code &c : m_codes) {
        if (c.name.compare(n, Qt::CaseInsensitive) != 0)
            continue;
        if (c.parentId == parentId())
            return c.id;
        if (!found)
            found = c.id;
    }
    return found;
}

void NewCodeDialog::setUseExisting(bool on)
{
    m_useExisting = on;
    updateExisting();
}

void NewCodeDialog::updateExisting()
{
    const bool exists = existingCodeId() != 0;
    m_hint->setText(m_useExisting ? tr("Esiste già: la selezione sarà codificata con il codice esistente.")
                                  : tr("Esiste già un codice con questo nome."));
    m_hint->setVisible(exists);
    m_colorButton->setEnabled(!(m_useExisting && exists));
    m_description->setEnabled(!(m_useExisting && exists));
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
