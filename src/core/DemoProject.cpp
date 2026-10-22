#include "core/DemoProject.h"
#include "core/Project.h"

namespace DemoProject {

namespace {

const char *const kInterviewA =
    "Intervistatore: Da quanto tempo lavori da remoto?\n"
    "Giulia: Ormai da quasi tre anni. All'inizio era una soluzione d'emergenza, poi l'azienda ha deciso di renderla "
    "stabile per due o tre giorni alla settimana.\n"
    "Intervistatore: Che cosa è cambiato nella tua giornata?\n"
    "Giulia: La cosa più evidente è il tempo. Prima passavo quasi due ore al giorno in macchina, adesso quel tempo lo "
    "uso per fare sport o per stare con mia figlia. Mi sento molto meno stanca la sera.\n"
    "Intervistatore: Ci sono anche aspetti negativi?\n"
    "Giulia: Sì, a volte mi sento isolata. Mi mancano le chiacchiere alla macchinetta del caffè, quelle conversazioni "
    "informali in cui nascevano le idee migliori. In videochiamata si parla solo di lavoro.\n"
    "Intervistatore: Come organizzi gli spazi in casa?\n"
    "Giulia: Ho ricavato un angolo nello studio, ma il problema è staccare: il computer è sempre lì e finisco per "
    "rispondere alle email anche alle dieci di sera.";

const char *const kInterviewB =
    "Intervistatore: Come descriveresti la tua esperienza con il lavoro agile?\n"
    "Marco: Per me è stata una liberazione. Posso organizzare le mie ore come voglio: se ho bisogno di andare dal "
    "medico la mattina recupero nel pomeriggio, senza chiedere permessi a nessuno.\n"
    "Intervistatore: E gli strumenti che usate?\n"
    "Marco: Usiamo Teams per le riunioni e un gestionale condiviso per i progetti. All'inizio c'era un po' di caos, "
    "troppe notifiche, troppi canali. Poi abbiamo stabilito delle regole e adesso funziona.\n"
    "Intervistatore: Ti senti parte del gruppo come prima?\n"
    "Marco: Onestamente no. I colleghi nuovi non li conosco quasi, li ho visti solo in video. Credo che per chi entra "
    "adesso in azienda sia molto più difficile sentirsi parte di qualcosa.";

const char *const kInterviewC =
    "Intervistatore: Il lavoro da remoto è stato utile nella tua situazione?\n"
    "Anna: Sì, soprattutto perché vivo lontano dalla città. Risparmio sulla benzina e non devo più svegliarmi alle "
    "sei per prendere il treno.\n"
    "Intervistatore: Ci sono state difficoltà tecniche?\n"
    "Anna: La connessione internet in paese non è affidabile, nei giorni di temporale cade spesso e devo spostarmi in "
    "biblioteca. Il computer me l'ha dato l'azienda, ma la linea me la pago io.\n"
    "Intervistatore: Come gestisci il confine tra casa e lavoro?\n"
    "Anna: Ho preso l'abitudine di uscire a fare una passeggiata alle sei: è il mio modo di dire che la giornata "
    "lavorativa è finita. Senza un rituale del genere lavorerei fino a notte.";

struct Coding {
    int doc;
    const char *code;
    const char *text;
};

} // namespace

bool populate(Project &project)
{
    if (!project.isOpen())
        return false;
    project.beginBatch();
    const qint64 group = project.groups().first().id;
    project.renameGroup(group, QObject::tr("Interviste"));
    const qint64 docs[] = {
        project.addDocument(group, QStringLiteral("Intervista Giulia"), QString::fromUtf8(kInterviewA)),
        project.addDocument(group, QStringLiteral("Intervista Marco"), QString::fromUtf8(kInterviewB)),
        project.addDocument(group, QStringLiteral("Intervista Anna"), QString::fromUtf8(kInterviewC)),
    };

    const qint64 benefits = project.addCode(0, QObject::tr("Benefici"), QColor(0x2ca02c));
    const qint64 difficulties = project.addCode(0, QObject::tr("Difficoltà"), QColor(0xd62728));
    const qint64 tools = project.addCode(0, QObject::tr("Strumenti digitali"), QColor(0x1f77b4));
    QHash<QString, qint64> codes{
        {QStringLiteral("Benefici"), benefits},
        {QStringLiteral("Difficoltà"), difficulties},
        {QStringLiteral("Strumenti digitali"), tools},
        {QStringLiteral("Tempo risparmiato"), project.addCode(benefits, QObject::tr("Tempo risparmiato"), QColor(0x66bb6a))},
        {QStringLiteral("Flessibilità"), project.addCode(benefits, QObject::tr("Flessibilità"), QColor(0x43a047))},
        {QStringLiteral("Isolamento"), project.addCode(difficulties, QObject::tr("Isolamento"), QColor(0xe57373))},
        {QStringLiteral("Confini casa-lavoro"),
         project.addCode(difficulties, QObject::tr("Confini casa-lavoro"), QColor(0xff7f0e))},
        {QStringLiteral("Problemi tecnici"), project.addCode(difficulties, QObject::tr("Problemi tecnici"), QColor(0x8c564b))},
    };
    project.setCodeDescription(codes.value(QStringLiteral("Isolamento")),
                               QObject::tr("Mancanza di relazioni informali con i colleghi, senso di esclusione."));

    const Coding codings[] = {
        {0, "Tempo risparmiato", "Prima passavo quasi due ore al giorno in macchina, adesso quel tempo lo uso per fare "
                                 "sport o per stare con mia figlia."},
        {0, "Isolamento", "a volte mi sento isolata. Mi mancano le chiacchiere alla macchinetta del caffè"},
        {0, "Strumenti digitali", "In videochiamata si parla solo di lavoro."},
        {0, "Confini casa-lavoro", "il problema è staccare: il computer è sempre lì e finisco per rispondere alle email "
                                   "anche alle dieci di sera."},
        {1, "Flessibilità", "Posso organizzare le mie ore come voglio"},
        {1, "Strumenti digitali", "Usiamo Teams per le riunioni e un gestionale condiviso per i progetti."},
        {1, "Difficoltà", "troppe notifiche, troppi canali."},
        {1, "Isolamento", "I colleghi nuovi non li conosco quasi, li ho visti solo in video."},
        {2, "Tempo risparmiato", "non devo più svegliarmi alle sei per prendere il treno."},
        {2, "Problemi tecnici", "La connessione internet in paese non è affidabile"},
        {2, "Confini casa-lavoro", "Ho preso l'abitudine di uscire a fare una passeggiata alle sei"},
    };
    for (const Coding &c : codings) {
        const QString text = project.documentText(docs[c.doc]);
        const QString needle = QString::fromUtf8(c.text);
        const int pos = int(text.indexOf(needle));
        if (pos >= 0)
            project.addSegment(docs[c.doc], codes.value(QString::fromUtf8(c.code)), pos, pos + int(needle.size()));
    }

    const qint64 age = project.addVariable(QObject::tr("Età"), VariableType::Integer);
    const qint64 sector = project.addVariable(QObject::tr("Settore"), VariableType::Text);
    const qint64 children = project.addVariable(QObject::tr("Figli"), VariableType::Boolean);
    const char *ages[] = {"38", "45", "29"};
    const char *sectors[] = {"Assicurazioni", "Informatica", "Pubblica amministrazione"};
    const char *kids[] = {"1", "0", "0"};
    for (int i = 0; i < 3; ++i) {
        project.setVariableValue(docs[i], age, QString::fromLatin1(ages[i]));
        project.setVariableValue(docs[i], sector, QString::fromLatin1(sectors[i]));
        project.setVariableValue(docs[i], children, QString::fromLatin1(kids[i]));
    }

    project.addDocumentSet(QObject::tr("Senza figli"), {docs[1], docs[2]});

    // A link between two passages that tell the same story from different sides.
    {
        const QString a = QStringLiteral("Mi mancano le chiacchiere alla macchinetta del caffè");
        const QString b = QStringLiteral("I colleghi nuovi non li conosco quasi");
        const int pa = int(project.documentText(docs[0]).indexOf(a));
        const int pb = int(project.documentText(docs[1]).indexOf(b));
        if (pa >= 0 && pb >= 0)
            project.addLink(docs[0], pa, pa + int(a.size()), docs[1], pb, pb + int(b.size()),
                            QObject::tr("Isolamento: lo stesso vissuto visto da chi c'era già e da chi è appena "
                                        "arrivato"));
    }

    Memo memo;
    memo.targetType = MemoTarget::Project;
    memo.title = QObject::tr("Domanda di ricerca");
    memo.body = QObject::tr("Come vivono i lavoratori il passaggio al lavoro da remoto? Quali benefici e quali "
                            "difficoltà emergono, e come cambiano i confini tra vita privata e lavoro?");
    project.saveMemo(memo);
    Memo codeMemo;
    codeMemo.targetType = MemoTarget::Code;
    codeMemo.targetId = codes.value(QStringLiteral("Isolamento"));
    codeMemo.title = QObject::tr("Isolamento e nuovi assunti");
    codeMemo.body = QObject::tr("Marco sottolinea che l'isolamento pesa soprattutto su chi entra in azienda adesso: "
                                "verificare nelle prossime interviste.");
    // The memo is also linked to the interview it talks about.
    project.addMemoLink(project.saveMemo(codeMemo), MemoTarget::Document, docs[1]);
    return project.commitBatch();
}

} // namespace DemoProject
