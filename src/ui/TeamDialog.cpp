#include "ui/TeamDialog.h"

#include "core/Agreement.h"
#include "core/Project.h"
#include "core/Team.h"
#include "ui/UiUtils.h"

#include <QApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString dateText(const QDateTime &dt) { return dt.isValid() ? dt.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QString(); }
} // namespace

TeamDialog::TeamDialog(Project *project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_info(new QLabel)
    , m_table(new QTableWidget(0, 8))
{
    setWindowTitle(tr("Lavoro di gruppo"));
    resize(820, 480);

    auto *intro = new QLabel(tr("Il responsabile avvia un ciclo: Tessera scrive una copia del progetto per ogni "
                                "membro in una cartella condivisa. I membri codificano la propria copia e la "
                                "rimandano; il responsabile importa le copie rientrate, che aggiungono al "
                                "progetto solo il lavoro del rispettivo membro."));
    intro->setWordWrap(true);
    m_info->setTextFormat(Qt::PlainText); // the author name is typed by the user
    m_info->setWordWrap(true);

    m_table->setHorizontalHeaderLabels({tr("Ciclo"), tr("Membro"), tr("File"), tr("Stato"), tr("Inviata"),
                                        tr("Rientrata"), tr("Unita"), tr("Esito")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    auto *newButton = new QPushButton(tr("Nuovo ciclo…"));
    auto *importButton = new QPushButton(tr("Importa copie rientrate…"));
    auto *agreementButton = new QPushButton(tr("Confronta i codificatori…"));
    m_reopen = new QPushButton(tr("Segna come inviata"));
    m_reopen->setToolTip(tr("Se hai annullato un'unione: la copia potrà essere importata di nuovo."));
    m_remove = new QPushButton(tr("Rimuovi voce"));
    auto *close = new QPushButton(tr("Chiudi"));
    for (auto *b : {newButton, importButton, agreementButton, m_reopen, m_remove, close})
        b->setAutoDefault(false);
    connect(newButton, &QPushButton::clicked, this, &TeamDialog::newRound);
    connect(importButton, &QPushButton::clicked, this, &TeamDialog::importReturned);
    connect(agreementButton, &QPushButton::clicked, this, [this] {
        m_agreementRequested = true;
        accept();
    });
    connect(m_reopen, &QPushButton::clicked, this, [this] {
        const auto items = m_table->selectedItems();
        if (!items.isEmpty())
            m_project->setTeamCycleStatus(items.first()->data(Qt::UserRole).toLongLong(), TeamCycleStatus::Sent);
    });
    connect(m_remove, &QPushButton::clicked, this, &TeamDialog::removeEntry);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
            [this] {
                const bool any = !m_table->selectedItems().isEmpty();
                m_remove->setEnabled(any);
                m_reopen->setEnabled(any);
            });
    connect(m_project, &Project::teamCyclesChanged, this, &TeamDialog::refresh);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(newButton);
    buttons->addWidget(importButton);
    buttons->addWidget(agreementButton);
    buttons->addWidget(m_reopen);
    buttons->addWidget(m_remove);
    buttons->addStretch(1);
    buttons->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(m_info);
    layout->addWidget(m_table, 1);
    layout->addLayout(buttons);
    refresh();
}

void TeamDialog::refresh()
{
    m_info->setText(tr("Il tuo nome (autore delle codifiche): %1").arg(m_project->author()));
    const QList<TeamCycle> cycles = m_project->teamCycles();
    m_table->setRowCount(cycles.size());
    for (int row = 0; row < cycles.size(); ++row) {
        const TeamCycle &c = cycles.at(row);
        QString status;
        switch (c.status) {
        case TeamCycleStatus::Sent: status = tr("Inviata"); break;
        case TeamCycleStatus::Returned: status = tr("Rientrata"); break;
        case TeamCycleStatus::Merged: status = tr("Unita"); break;
        }
        const QStringList cells = {QString::number(c.number), c.member, c.fileName, status, dateText(c.sentAt),
                                   dateText(c.returnedAt), dateText(c.mergedAt), c.report};
        for (int col = 0; col < cells.size(); ++col) {
            auto *item = new QTableWidgetItem(cells.at(col));
            item->setData(Qt::UserRole, c.id);
            m_table->setItem(row, col, item);
        }
    }
    m_remove->setEnabled(false);
    m_reopen->setEnabled(false);
}

bool TeamDialog::startRound(const QStringList &members, const QString &folder)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const Team::StartResult r = Team::startCycle(*m_project, members, folder);
    QApplication::restoreOverrideCursor();
    if (!r.ok) {
        Ui::showError(this, r.error);
        return false;
    }
    return true;
}

void TeamDialog::newRound()
{
    // Offer the coders already known in the project (except the manager), one per line.
    QStringList known = Agreement::authors(*m_project);
    known.removeAll(m_project->author());

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Nuovo ciclo"));
    auto *members = new QPlainTextEdit(known.join(QLatin1Char('\n')));
    auto *folder = new QLineEdit(QFileInfo(m_project->filePath()).absolutePath());
    auto *browse = new QPushButton(tr("Sfoglia…"));
    browse->setAutoDefault(false);
    connect(browse, &QPushButton::clicked, &dlg, [&] {
        const QString dir = QFileDialog::getExistingDirectory(&dlg, tr("Cartella delle copie"), folder->text());
        if (!dir.isEmpty())
            folder->setText(dir);
    });
    auto *row = new QHBoxLayout;
    row->addWidget(folder, 1);
    row->addWidget(browse);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(tr("Membri del gruppo (un nome per riga, come compare come autore):")));
    layout->addWidget(members, 1);
    layout->addWidget(new QLabel(tr("Cartella in cui scrivere le copie (per esempio quella condivisa):")));
    layout->addLayout(row);
    layout->addWidget(buttons);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QStringList names = members->toPlainText().split(QLatin1Char('\n'));
    names.removeAll(QString());
    for (QString &n : names)
        n = n.trimmed();
    if (!startRound(names, folder->text().trimmed()))
        return;
    const int number = m_project->teamCycles().isEmpty() ? 1 : m_project->teamCycles().last().number;
    Ui::showInfo(this, tr("Nuovo ciclo"),
                 tr("Ciclo %1 avviato: %2 copie scritte in %3.").arg(number).arg(names.size()).arg(folder->text()));
}

QString TeamDialog::importFiles(const QStringList &paths, bool askConfirmation)
{
    QStringList lines;
    for (const QString &path : paths) {
        const QString name = QFileInfo(path).fileName();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const Team::ReturnResult dry = Team::importReturned(*m_project, path, Merge::TextConflict::ImportCopy, true);
        QApplication::restoreOverrideCursor();
        if (!dry.ok) {
            lines << tr("%1: non importata (%2)").arg(name, dry.error);
            continue;
        }
        if (askConfirmation) {
            QString text = tr("Copia di %1 (ciclo %2): %3.").arg(dry.member).arg(dry.number).arg(Team::summarize(dry.report));
            if (dry.noWork)
                text += tr("\n\nLa copia non contiene lavoro di questo membro: controlla che abbia usato il proprio "
                           "nome come autore.");
            text += tr("\n\nUnire?");
            if (!Ui::confirm(this, tr("Importa copia rientrata"), text)) {
                lines << tr("%1: saltata").arg(name);
                continue;
            }
        }
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const Team::ReturnResult done = Team::importReturned(*m_project, path, Merge::TextConflict::ImportCopy, false);
        QApplication::restoreOverrideCursor();
        lines << (done.ok ? tr("%1: unita (%2)").arg(name, Team::summarize(done.report))
                          : tr("%1: unione non riuscita (%2)").arg(name, done.error));
    }
    return lines.join(QLatin1Char('\n'));
}

void TeamDialog::importReturned()
{
    const QStringList paths = Ui::openFileNames(this, tr("Copie rientrate"), tr("Progetti Tessera (*.tsra);;Tutti i file (*)"));
    if (paths.isEmpty())
        return;
    const QString result = importFiles(paths);
    Ui::showInfo(this, tr("Importa copie rientrate"),
                 result + tr("\n\nOgni unione si può annullare con Modifica ▸ Annulla."));
}

void TeamDialog::removeEntry()
{
    const auto items = m_table->selectedItems();
    if (items.isEmpty())
        return;
    if (Ui::confirm(this, tr("Rimuovi voce"), tr("Rimuovere la voce dalla cronologia? I file non vengono toccati.")))
        m_project->deleteTeamCycle(items.first()->data(Qt::UserRole).toLongLong());
}