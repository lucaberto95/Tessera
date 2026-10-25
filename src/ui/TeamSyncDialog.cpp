#include "ui/TeamSyncDialog.h"

#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
QString settingsKey(const Project &project, const char *what)
{
    return QStringLiteral("teamSync/%1/%2").arg(project.meta(QStringLiteral("project_uuid")), QLatin1String(what));
}
} // namespace

TeamSyncDialog::TeamSyncDialog(Project *project, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_folder(new QLineEdit(savedFolder(*project)))
    , m_auto(new QCheckBox(tr("Sincronizza automaticamente quando apro il progetto")))
    , m_report(new QPlainTextEdit)
{
    setWindowTitle(tr("Sincronizzazione del gruppo"));
    resize(760, 520);

    auto *intro = new QLabel(tr("Ognuno lavora sul proprio file di progetto, che resta sul proprio computer. Tessera scrive il "
                                "tuo lavoro (codifiche, codici, memo, collegamenti, sintesi) in un file di modifiche nella "
                                "cartella condivisa e legge quelli degli altri membri, applicando le novità in un unico passo "
                                "annullabile."));
    intro->setWordWrap(true);
    auto *warning = new QLabel(tr("Attenzione: non mettere mai il file del progetto (.tsra) nella cartella condivisa "
                                  "(OneDrive, Dropbox, rete...): i servizi di sincronizzazione possono danneggiare un "
                                  "database aperto. Nella cartella condivisa vanno solo i file di modifiche (.tsrlog) che "
                                  "scrive Tessera. Non vengono sincronizzati documenti nuovi o modificati, né le eliminazioni."));
    warning->setWordWrap(true);
    auto *who = new QLabel(tr("Il tuo nome (autore delle codifiche): %1").arg(project->author()));
    who->setTextFormat(Qt::PlainText); // the author name is typed by the user

    m_folder->setPlaceholderText(tr("Cartella condivisa"));
    auto *browse = new QPushButton(tr("Sfoglia…"));
    auto *sync = new QPushButton(tr("Sincronizza ora"));
    auto *preview = new QPushButton(tr("Anteprima"));
    preview->setToolTip(tr("Mostra cosa cambierebbe senza modificare il progetto."));
    auto *publish = new QPushButton(tr("Solo pubblica il mio lavoro"));
    auto *reread = new QPushButton(tr("Rileggi tutto"));
    reread->setToolTip(tr("Dimentica quanto è già stato letto e rilegge tutti i file (utile dopo aver annullato una sincronizzazione)."));
    auto *close = new QPushButton(tr("Chiudi"));
    for (auto *b : {browse, sync, preview, publish, reread, close})
        b->setAutoDefault(false);
    m_auto->setChecked(autoSyncOnOpen(*project));
    m_report->setReadOnly(true);

    connect(browse, &QPushButton::clicked, this, &TeamSyncDialog::chooseFolder);
    connect(sync, &QPushButton::clicked, this, [this] {
        saveFolder(*m_project, folder());
        m_report->setPlainText(syncNow());
    });
    connect(preview, &QPushButton::clicked, this, [this] { m_report->setPlainText(syncNow(true)); });
    connect(publish, &QPushButton::clicked, this, [this] {
        saveFolder(*m_project, folder());
        m_report->setPlainText(publishOnly());
    });
    connect(reread, &QPushButton::clicked, this, [this] {
        saveFolder(*m_project, folder());
        m_report->setPlainText(syncNow(false, true));
    });
    connect(m_auto, &QCheckBox::toggled, this, [this](bool on) { setAutoSyncOnOpen(*m_project, on); });
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(m_folder, 1);
    folderRow->addWidget(browse);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(sync);
    buttons->addWidget(preview);
    buttons->addWidget(publish);
    buttons->addWidget(reread);
    buttons->addStretch(1);
    buttons->addWidget(close);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(warning);
    layout->addWidget(who);
    layout->addLayout(folderRow);
    layout->addWidget(m_auto);
    layout->addLayout(buttons);
    layout->addWidget(m_report, 1);
}

void TeamSyncDialog::setFolder(const QString &folder) { m_folder->setText(folder); }
QString TeamSyncDialog::folder() const { return m_folder->text().trimmed(); }

void TeamSyncDialog::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Cartella condivisa"), folder());
    if (!dir.isEmpty())
        setFolder(dir);
}

QString TeamSyncDialog::publishOnly()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const TeamSync::PublishResult pub = TeamSync::publish(*m_project, folder());
    QApplication::restoreOverrideCursor();
    if (!pub.ok)
        return tr("Pubblicazione non riuscita: %1").arg(pub.error);
    return tr("Pubblicato il tuo lavoro: %1 elementi (versione %2).").arg(pub.records).arg(pub.generation);
}

QString TeamSyncDialog::syncNow(bool dryRun, bool rereadAll)
{
    QString text;
    if (!dryRun) {
        const QString published = publishOnly();
        text = published + QStringLiteral("\n\n");
        if (published.startsWith(tr("Pubblicazione non riuscita")))
            return text.trimmed();
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const TeamSync::SyncReport report = TeamSync::synchronize(*m_project, folder(), dryRun, rereadAll);
    QApplication::restoreOverrideCursor();
    return text + (dryRun ? tr("ANTEPRIMA (non è stato modificato nulla)\n") : QString()) + describe(report);
}

QString TeamSyncDialog::describe(const TeamSync::SyncReport &report)
{
    if (!report.ok)
        return tr("Sincronizzazione non riuscita: %1").arg(report.error);
    QStringList lines;
    for (const TeamSync::MemberReport &m : report.members) {
        if (m.ignored)
            lines << tr("%1: ignorato (%2)").arg(m.file, m.note);
        else if (m.unchanged)
            lines << tr("%1: nessuna novità (versione %2)").arg(m.member).arg(m.generation);
        else
            lines << tr("%1 (versione %2): %3 codifiche, %4 collegamenti, %5 memo, %6 sintesi, %7 codici nuovi, %8 aggiornati, "
                        "%9 in conflitto, %10 non applicati")
                         .arg(m.member).arg(m.generation).arg(m.segmentsNew).arg(m.linksNew).arg(m.memosNew)
                         .arg(m.summariesNew).arg(m.codesNew).arg(m.updated).arg(m.conflicts).arg(m.skipped);
    }
    if (report.members.isEmpty())
        lines << tr("Nessun file di altri membri.");
    if (!report.conflicts.isEmpty()) {
        lines << QString() << tr("Conflitti (il testo è cambiato da entrambe le parti: sono stati tenuti i due testi, "
                                 "controllali):");
        for (const QString &c : report.conflicts)
            lines << QStringLiteral("- ") + c;
    }
    if (!report.warnings.isEmpty()) {
        lines << QString() << tr("Avvisi:");
        for (const QString &w : report.warnings)
            lines << QStringLiteral("- ") + w;
    }
    lines << QString() << tr("Totale: %1").arg(TeamSync::summarize(report));
    if (report.changes() > 0)
        lines << tr("La sincronizzazione si può annullare con Modifica ▸ Annulla.");
    return lines.join(QLatin1Char('\n'));
}

QString TeamSyncDialog::savedFolder(const Project &project)
{
    return QSettings().value(settingsKey(project, "folder")).toString();
}

void TeamSyncDialog::saveFolder(const Project &project, const QString &folder)
{
    QSettings().setValue(settingsKey(project, "folder"), folder);
}

bool TeamSyncDialog::autoSyncOnOpen(const Project &project)
{
    return QSettings().value(settingsKey(project, "auto"), false).toBool();
}

void TeamSyncDialog::setAutoSyncOnOpen(const Project &project, bool on)
{
    QSettings().setValue(settingsKey(project, "auto"), on);
}

QString TeamSyncDialog::runAutoSync(Project &project)
{
    if (!autoSyncOnOpen(project) || project.author().trimmed().isEmpty())
        return {};
    const QString folder = savedFolder(project);
    if (folder.isEmpty() || !QDir(folder).exists())
        return {};
    const TeamSync::PublishResult pub = TeamSync::publish(project, folder);
    if (!pub.ok)
        return QCoreApplication::translate("TeamSyncDialog", "Sincronizzazione del gruppo non riuscita: %1").arg(pub.error);
    const TeamSync::SyncReport report = TeamSync::synchronize(project, folder);
    if (!report.ok)
        return QCoreApplication::translate("TeamSyncDialog", "Sincronizzazione del gruppo non riuscita: %1").arg(report.error);
    QString message = QCoreApplication::translate("TeamSyncDialog", "Sincronizzazione del gruppo: %1").arg(TeamSync::summarize(report));
    if (!report.conflicts.isEmpty() || !report.warnings.isEmpty())
        message += QCoreApplication::translate("TeamSyncDialog", " - %1 conflitti, %2 avvisi: in Lavoro di gruppo, Sincronizzazione, «Rileggi tutto» li mostra").arg(report.conflicts.size()).arg(report.warnings.size());
    return message;
}
