#include "ui/MemoDialog.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

MemoDialog::MemoDialog(Project *project, const Memo &memo, const QString &targetLabel, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_memo(memo)
    , m_title(new QLineEdit(memo.title))
    , m_body(new QPlainTextEdit(memo.body))
{
    setWindowTitle(memo.id ? tr("Memo") : tr("Nuovo memo"));
    setWindowIcon(Ui::memoIcon());
    resize(560, 460);

    auto *target = new QLabel(targetLabel.toHtmlEscaped());
    target->setWordWrap(true);
    target->setTextFormat(Qt::RichText);
    target->setText(QStringLiteral("<b>%1</b> %2").arg(tr("Riferito a:"), targetLabel.toHtmlEscaped()));

    if (m_title->text().isEmpty())
        m_title->setPlaceholderText(tr("Titolo del memo"));
    m_body->setPlaceholderText(tr("Scrivi qui le tue riflessioni, definizioni, idee analitiche…"));
    m_body->setTabChangesFocus(false);

    auto *info = new QLabel;
    info->setTextFormat(Qt::PlainText); // the author's name may come from an imported file
    if (memo.id) {
        info->setText(tr("Autore: %1 · creato %2 · modificato %3")
                          .arg(memo.author, QLocale().toString(memo.createdAt.toLocalTime(), QLocale::ShortFormat),
                               QLocale().toString(memo.modifiedAt.toLocalTime(), QLocale::ShortFormat)));
        info->setStyleSheet(QStringLiteral("color: palette(mid)"));
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    if (memo.id) {
        QPushButton *del = buttons->addButton(tr("Elimina memo"), QDialogButtonBox::DestructiveRole);
        connect(del, &QPushButton::clicked, this, &MemoDialog::remove);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &MemoDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *form = new QFormLayout;
    form->addRow(tr("Titolo"), m_title);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(target);
    layout->addLayout(form);
    layout->addWidget(m_body, 1);
    layout->addWidget(info);
    layout->addWidget(buttons);
    m_body->setFocus();
}

void MemoDialog::save()
{
    m_memo.title = m_title->text().trimmed();
    m_memo.body = m_body->toPlainText();
    if (m_memo.id == 0 && m_memo.title.isEmpty() && m_memo.body.trimmed().isEmpty()) {
        reject();
        return;
    }
    if (!m_project->saveMemo(m_memo)) {
        Ui::showError(this, m_project->lastError());
        return;
    }
    accept();
}

void MemoDialog::remove()
{
    if (!Ui::confirm(this, tr("Elimina memo"), tr("Eliminare definitivamente questo memo?")))
        return;
    m_project->deleteMemo(m_memo.id);
    accept();
}
