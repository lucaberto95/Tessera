#include "core/Team.h"

#include "core/Project.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace Team {

namespace {
QString cleanName(QString s)
{
    s.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\s]+")), QStringLiteral("_"));
    return s.trimmed();
}
} // namespace

QString copyFileName(const QString &projectName, int number, const QString &member)
{
    return QStringLiteral("%1_ciclo%2_%3.%4")
        .arg(cleanName(projectName))
        .arg(number)
        .arg(cleanName(member))
        .arg(QLatin1String(Project::FileSuffix));
}

StartResult startCycle(Project &manager, const QStringList &rawMembers, const QString &folder)
{
    StartResult r;
    auto fail = [&r](const QString &message) {
        r.ok = false;
        r.error = message;
        return r;
    };
    if (!manager.isOpen())
        return fail(QCoreApplication::translate("Team", "Nessun progetto aperto"));
    QStringList members;
    QSet<QString> seen, names;
    const int number = manager.nextTeamCycleNumber();
    for (const QString &raw : rawMembers) {
        const QString m = raw.trimmed();
        if (m.isEmpty())
            return fail(QCoreApplication::translate("Team", "Il nome di un membro è vuoto"));
        if (seen.contains(m.toCaseFolded()))
            return fail(QCoreApplication::translate("Team", "Il membro «%1» è ripetuto").arg(m));
        seen.insert(m.toCaseFolded());
        // Different names can give the same file name once cleaned: they would overwrite each other.
        if (names.contains(copyFileName(manager.name(), number, m).toCaseFolded()))
            return fail(QCoreApplication::translate("Team", "I nomi dei membri danno lo stesso nome di file"));
        names.insert(copyFileName(manager.name(), number, m).toCaseFolded());
        members << m;
    }
    if (members.isEmpty())
        return fail(QCoreApplication::translate("Team", "Indica almeno un membro"));
    if (!QDir().mkpath(folder))
        return fail(QCoreApplication::translate("Team", "Impossibile creare la cartella %1").arg(folder));

    QStringList written;
    for (const QString &m : std::as_const(members)) {
        const QString path = QDir(folder).filePath(copyFileName(manager.name(), number, m));
        bool ok = manager.saveCopyAs(path);
        if (ok) {
            Project copy;
            ok = copy.open(path);
            if (ok) {
                copy.setMeta(QLatin1String(MemberKey), m);
                copy.setMeta(QLatin1String(CycleKey), QString::number(number));
                ok = copy.clearTeamCycles(); // the history belongs to the manager
                copy.close();
            }
            if (!ok)
                QFile::remove(path);
        }
        if (!ok) {
            for (const QString &w : std::as_const(written))
                QFile::remove(w);
            return fail(QCoreApplication::translate("Team", "Impossibile scrivere la copia per %1: %2").arg(m, manager.lastError()));
        }
        written << path;
    }
    for (int i = 0; i < members.size(); ++i)
        manager.addTeamCycleEntry(number, members.at(i), QFileInfo(written.at(i)).fileName());
    r.number = number;
    r.files = written;
    return r;
}

qint64 findCycle(const Project &manager, const Merge::Source &source, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return qint64(0);
    };
    const QString member = source.project().meta(QLatin1String(MemberKey));
    const int number = source.project().meta(QLatin1String(CycleKey)).toInt();
    if (member.isEmpty() || number <= 0)
        return fail(QCoreApplication::translate("Team", "Il file non è una copia di lavoro di gruppo (mancano membro e ciclo)."));
    for (const TeamCycle &c : manager.teamCycles()) {
        if (c.number != number || c.member != member)
            continue;
        if (c.status == TeamCycleStatus::Merged)
            return fail(QCoreApplication::translate("Team", "La copia di %1 del ciclo %2 è già stata unita.").arg(member).arg(number));
        return c.id;
    }
    return fail(QCoreApplication::translate("Team", "Questo progetto non ha il ciclo %1 per %2: la copia viene da un altro progetto.")
                    .arg(number)
                    .arg(member));
}

QString summarize(const Merge::Report &r)
{
    return QCoreApplication::translate("Team", "%1 codifiche, %2 memo, %3 collegamenti, %4 codici, %5 documenti nuovi")
        .arg(r.segmentsNew + r.segmentsConflict)
        .arg(r.memosNew + r.memosMerged)
        .arg(r.linksNew)
        .arg(r.codesNew)
        .arg(r.documentsNew + r.documentsCopied);
}

ReturnResult importReturned(Project &manager, const QString &path, Merge::TextConflict onConflict, bool dryRun)
{
    ReturnResult r;
    Merge::Source source;
    QString error;
    if (!source.load(path, &error)) {
        r.ok = false;
        r.error = error;
        return r;
    }
    r.cycleId = findCycle(manager, source, &error);
    if (!r.cycleId) {
        r.ok = false;
        r.error = error;
        return r;
    }
    r.member = source.project().meta(QLatin1String(MemberKey));
    r.number = source.project().meta(QLatin1String(CycleKey)).toInt();

    Merge::Options options;
    options.onTextConflict = onConflict;
    options.onlyAuthor = r.member; // a copy can only bring the work of the member it was given to
    if (dryRun) {
        r.report = Merge::analyze(manager, source, options);
        r.ok = r.report.ok;
        r.error = r.report.error;
        r.noWork = r.ok && r.report.changes() == 0;
        return r;
    }
    manager.setTeamCycleStatus(r.cycleId, TeamCycleStatus::Returned);
    r.report = Merge::apply(manager, source, options);
    r.ok = r.report.ok;
    r.error = r.report.error;
    if (!r.ok)
        return r;
    r.noWork = r.report.changes() == 0;
    manager.setTeamCycleStatus(r.cycleId, TeamCycleStatus::Merged, summarize(r.report));
    return r;
}

} // namespace Team