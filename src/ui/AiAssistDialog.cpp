#include "AiAssistDialog.h"

#include "AiUi.h"
#include "UiUtils.h"
#include "core/Project.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Ai::Assist;

AiAssistDialog::AiAssistDialog(Project *project, Task task, const QList<Source> &sources, const QString &subject,
                               const MemoDestination &destination, QWidget *parent)
    : QDialog(parent), m_project(project), m_task(task), m_sources(sources), m_dest(destination)
{
    setWindowTitle(tr("AI: %1").arg(taskTitle(task)));
    setMinimumSize(560, 520);
    m_confirm = [this](const QString &notice) { return Ui::confirm(this, tr("Invio dei dati a un provider cloud"), notice); };

    auto *subjectLabel = new QLabel(subject);
    subjectLabel->setTextFormat(Qt::PlainText);
    subjectLabel->setWordWrap(true);

    m_length = new QComboBox;
    for (Length l : {Length::Short, Length::Medium, Length::Long})
        m_length->addItem(lengthName(l), int(l));
    m_length->setCurrentIndex(1);
    m_language = new QComboBox;
    m_language->setEditable(true);
    m_language->addItems({tr("italiano"), tr("inglese"), tr("francese"), tr("spagnolo"), tr("tedesco"), tr("portoghese")});
    m_language->setCurrentText(task == Task::Translate ? tr("inglese") : tr("italiano"));
    m_instructions = new QLineEdit;
    m_instructions->setPlaceholderText(tr("Opzionali, ad esempio «concentrati sulle difficoltà»"));

    auto *form = new QFormLayout;
    if (usesLength(task))
        form->addRow(tr("Lunghezza:"), m_length);
    form->addRow(task == Task::Translate ? tr("Traduci in:") : tr("Lingua del risultato:"), m_language);
    form->addRow(tr("Istruzioni aggiuntive:"), m_instructions);

    m_recipient = new QLabel;
    m_recipient->setTextFormat(Qt::PlainText);
    m_recipient->setWordWrap(true);
    m_input = new QLabel;
    m_input->setTextFormat(Qt::PlainText);
    m_input->setWordWrap(true);
    m_status = new QLabel;
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);

    m_result = new QPlainTextEdit;
    m_result->setPlaceholderText(tr("Il risultato comparirà qui dopo «Genera». Puoi modificarlo prima di salvarlo."));

    auto button = [](const QString &text) {
        auto *b = new QPushButton(text);
        b->setAutoDefault(false);
        return b;
    };
    m_generate = button(tr("Genera"));
    m_generate->setToolTip(tr("Invia il testo mostrato sopra al modello e attende la risposta"));
    m_cancel = button(tr("Annulla richiesta"));
    m_saveMemo = button(tr("Salva come memo"));
    m_saveSummary = button(tr("Salva come sintesi"));
    m_saveSummary->setVisible(false);
    m_copy = button(tr("Copia"));
    auto *close = button(tr("Chiudi"));

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_generate);
    buttons->addWidget(m_cancel);
    buttons->addStretch(1);
    buttons->addWidget(m_saveMemo);
    buttons->addWidget(m_saveSummary);
    buttons->addWidget(m_copy);
    buttons->addWidget(close);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(subjectLabel);
    layout->addLayout(form);
    layout->addWidget(m_recipient);
    layout->addWidget(m_input);
    layout->addWidget(m_result, 1);
    layout->addWidget(m_status);
    layout->addLayout(buttons);

    connect(m_generate, &QPushButton::clicked, this, [this] { generate(); });
    connect(m_cancel, &QPushButton::clicked, this, [this] { cancelRequest(); });
    connect(m_saveMemo, &QPushButton::clicked, this, [this] { saveAsMemo(); });
    connect(m_saveSummary, &QPushButton::clicked, this, [this] { saveAsSummary(); });
    connect(m_copy, &QPushButton::clicked, this, [this] { copyResult(); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_result, &QPlainTextEdit::textChanged, this, [this] { updateEnabled(); });

    m_input->setText(tr("Verrà inviato: %1").arg(inputDescription()));
    updateRecipient();
    updateEnabled();
}

AiAssistDialog::~AiAssistDialog() = default;

void AiAssistDialog::setSummaryTarget(qint64 documentId, qint64 codeId)
{
    m_summaryDoc = documentId;
    m_summaryCode = codeId;
    m_saveSummary->setVisible(documentId && codeId);
    updateEnabled();
}

Options AiAssistDialog::options() const
{
    Options o;
    o.length = Length(m_length->currentData().toInt());
    o.language = m_language->currentText().trimmed();
    o.instructions = m_instructions->text().trimmed();
    return o;
}

void AiAssistDialog::setOptions(const Options &o)
{
    m_length->setCurrentIndex(m_length->findData(int(o.length)));
    m_language->setCurrentText(o.language);
    m_instructions->setText(o.instructions);
}

QString AiAssistDialog::inputDescription() const
{
    qint64 words = wordsOf(m_sources);
    // One document or passage ("1 documento") or one entry per source for the segments of a code.
    if (m_task == Task::SummarizeSegments) {
        const int n = int(m_sources.size());
        return tr("%n segmenti", nullptr, n) + QStringLiteral(", ~%1 ").arg(QLocale(QLocale::Italian).toString(words))
               + (words == 1 ? tr("parola") : tr("parole"));
    }
    if (m_task == Task::SummarizeDocument)
        return Ai::describeInput(1, words);
    return tr("1 brano, ~%1 %2").arg(QLocale(QLocale::Italian).toString(words), words == 1 ? tr("parola") : tr("parole"));
}

QString AiAssistDialog::recipientText() const
{
    return Ui::aiRecipientText();
}

void AiAssistDialog::updateRecipient()
{
    m_recipient->setText(recipientText());
}

QString AiAssistDialog::resultText() const
{
    return m_result->toPlainText();
}

void AiAssistDialog::setResultText(const QString &text)
{
    m_result->setPlainText(text);
    m_hasResult = !text.trimmed().isEmpty();
    updateEnabled();
}

QString AiAssistDialog::statusText() const
{
    return m_status->text();
}

void AiAssistDialog::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_statusError = error;
    m_status->setStyleSheet(error ? QStringLiteral("color: #b00020;") : QString());
}

void AiAssistDialog::updateEnabled()
{
    const bool busy = isBusy();
    const bool text = !m_result->toPlainText().trimmed().isEmpty();
    m_generate->setEnabled(!busy);
    m_generate->setText(m_hasResult ? tr("Rigenera") : tr("Genera"));
    m_cancel->setVisible(busy);
    m_saveMemo->setEnabled(!busy && text);
    m_saveSummary->setEnabled(!busy && text && m_summaryDoc && m_summaryCode);
    m_copy->setEnabled(text);
    m_length->setEnabled(!busy);
    m_language->setEnabled(!busy);
    m_instructions->setEnabled(!busy);
}

// The only place where this dialog sends anything, from a click of the user on "Genera"/"Rigenera".
void AiAssistDialog::generate()
{
    if (m_run)
        return;
    updateRecipient();
    QString error;
    delete m_client;
    m_client = Ui::makeConfirmedAiClient(this, m_confirm, &m_usedConfig, &error);
    if (!m_client) {
        setStatus(error, true);
        return;
    }
    m_run = new Run(m_client, m_task, options(), m_sources, this);
    m_run->setMaxChunkTokens(m_maxChunkTokens);
    connect(m_run, &Run::progress, this, [this](int done, int total) {
        if (total > 1)
            setStatus(tr("Richiesta %1 di %2…").arg(qMin(done + 1, total)).arg(total), false);
    });
    Run *run = m_run;
    connect(run, &Run::finished, this, [this, run] {
        const Ai::Response r = run->response();
        m_run = nullptr;
        run->deleteLater();
        if (m_project)
            m_project->addAiLog(functionId(m_task), Ai::providerId(m_usedConfig.provider), m_usedConfig.model,
                                inputDescription(), r.ok ? r.text : r.error);
        if (r.ok) {
            m_result->setPlainText(r.text);
            m_hasResult = true;
            setStatus(tr("Risultato generato da %1. Non è stato salvato: controllalo e usa «Salva come memo».")
                          .arg(Ai::authorName(m_usedConfig)),
                      false);
        } else {
            setStatus(r.error, r.kind != Ai::ErrorKind::Cancelled);
        }
        updateEnabled();
    });
    setStatus(tr("Richiesta in corso…"), false);
    updateEnabled();
    run->start();
}

void AiAssistDialog::cancelRequest()
{
    if (m_run)
        m_run->cancel();
}

void AiAssistDialog::reject()
{
    cancelRequest();
    QDialog::reject();
}

qint64 AiAssistDialog::saveAsMemo()
{
    const QString text = m_result->toPlainText().trimmed();
    if (text.isEmpty() || !m_project || !m_dest.id)
        return 0;
    const QString author = Ai::authorName(m_usedConfig.model.isEmpty() ? Ai::Settings::load() : m_usedConfig);
    const QString quote = m_dest.quote.trimmed();
    QString body = quote.isEmpty() ? text : tr("Brano: «%1»").arg(quote) + QStringLiteral("\n\n") + text;
    Memo memo = m_project->memoFor(m_dest.type, m_dest.id);
    if (memo.id) {
        // The existing memo stays as it is; the generated text is added after it, signed.
        const QString stamp = QLocale(QLocale::Italian).toString(QDateTime::currentDateTime(), QLocale::ShortFormat);
        body = memo.body + (memo.body.isEmpty() ? QString() : QStringLiteral("\n\n")) + QStringLiteral("— %1, %2 —\n").arg(author, stamp) + body;
    } else {
        memo.targetType = m_dest.type;
        memo.targetId = m_dest.id;
        memo.title = taskTitle(m_task);
        memo.author = author;
    }
    memo.body = body;
    const qint64 id = m_project->saveMemo(memo);
    setStatus(id ? tr("Salvato nel memo (autore: %1). Si può annullare con Modifica > Annulla.").arg(author)
                 : tr("Impossibile salvare il memo."),
              !id);
    return id;
}

bool AiAssistDialog::saveAsSummary()
{
    const QString text = m_result->toPlainText().trimmed();
    if (text.isEmpty() || !m_project || !m_summaryDoc || !m_summaryCode)
        return false;
    const QString author = Ai::authorName(m_usedConfig.model.isEmpty() ? Ai::Settings::load() : m_usedConfig);
    QString body = text;
    const Summary existing = m_project->summary(m_summaryDoc, m_summaryCode);
    if (existing.id && !existing.body.trimmed().isEmpty())
        body = existing.body + QStringLiteral("\n\n— %1 —\n").arg(author) + text;
    // The summary is signed with the author of the project: sign it as the AI for this write only.
    const QString previousAuthor = m_project->author();
    m_project->setAuthor(author);
    const bool ok = m_project->setSummary(m_summaryDoc, m_summaryCode, body);
    m_project->setAuthor(previousAuthor);
    setStatus(ok ? tr("Salvato nella sintesi. Si può annullare con Modifica > Annulla.") : tr("Impossibile salvare la sintesi."), !ok);
    return ok;
}

void AiAssistDialog::copyResult()
{
    QApplication::clipboard()->setText(m_result->toPlainText());
    setStatus(tr("Copiato negli appunti."), false);
}
