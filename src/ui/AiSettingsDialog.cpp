#include "AiSettingsDialog.h"

#include "UiUtils.h"
#include "core/Project.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

AiSettingsDialog::AiSettingsDialog(Project *project, QWidget *parent)
    : QDialog(parent), m_project(project)
{
    setWindowTitle(tr("Impostazioni AI"));
    setMinimumWidth(520);
    m_confirm = [this](const QString &notice) { return Ui::confirm(this, tr("Invio dei dati a un provider cloud"), notice); };

    auto *intro = new QLabel(tr("Le funzioni AI usano un modello linguistico. Con un server locale i dati non "
                                "lasciano il computer; con un provider cloud i testi inviati escono dal computer. "
                                "Nulla viene inviato senza un'azione esplicita."));
    intro->setTextFormat(Qt::PlainText);
    intro->setWordWrap(true);

    m_lockLabel = new QLabel;
    m_lockLabel->setTextFormat(Qt::PlainText);
    m_lockLabel->setWordWrap(true);
    m_lockLabel->setVisible(false);

    m_provider = new QComboBox;
    for (Ai::Provider p : Ai::allProviders())
        m_provider->addItem(Ai::providerName(p), Ai::providerId(p));
    m_url = new QLineEdit;
    m_url->setPlaceholderText(QStringLiteral("http://localhost:11434/v1"));
    m_model = new QComboBox;
    m_model->setEditable(true);
    m_model->setInsertPolicy(QComboBox::NoInsert);
    m_refresh = new QPushButton(tr("Aggiorna elenco"));
    m_refresh->setAutoDefault(false);
    m_refresh->setToolTip(tr("Legge i modelli disponibili dal server (GET /models)"));
    auto *modelRow = new QHBoxLayout;
    modelRow->addWidget(m_model, 1);
    modelRow->addWidget(m_refresh);
    m_key = new QLineEdit;
    m_key->setEchoMode(QLineEdit::Password);
    m_key->setPlaceholderText(tr("Opzionale per un server locale"));
    m_key->setToolTip(tr("Salvata nell'archivio credenziali di Windows: mai nel progetto né nelle impostazioni."));
    m_timeout = new QSpinBox;
    m_timeout->setRange(1, 3600);
    m_timeout->setSuffix(tr(" s"));
    m_timeout->setValue(120);
    m_logRequests = new QCheckBox(tr("Registra le richieste AI nel progetto (per trasparenza e riproducibilità)"));
    m_logRequests->setToolTip(tr("Salva funzione, modello, riepilogo di ciò che è stato inviato e risposta. "
                                 "Le chiavi API non sono mai registrate."));
    m_logRequests->setEnabled(m_project);
    if (m_project)
        m_logRequests->setChecked(m_project->aiLogEnabled());

    m_test = new QPushButton(tr("Prova connessione"));
    m_test->setAutoDefault(false);
    m_status = new QLabel;
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    auto *testRow = new QHBoxLayout;
    testRow->addWidget(m_test);
    testRow->addWidget(m_status, 1);

    auto *form = new QFormLayout;
    form->addRow(tr("Provider:"), m_provider);
    form->addRow(tr("Indirizzo del server:"), m_url);
    form->addRow(tr("Modello:"), modelRow);
    form->addRow(tr("Chiave API:"), m_key);
    form->addRow(tr("Timeout:"), m_timeout);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(m_lockLabel);
    layout->addLayout(form);
    layout->addLayout(testRow);
    layout->addWidget(m_logRequests);
    layout->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &AiSettingsDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &AiSettingsDialog::reject);
    connect(m_provider, &QComboBox::currentIndexChanged, this, [this] { providerChanged(); });
    connect(m_test, &QPushButton::clicked, this, [this] {
        if (m_reply)
            m_reply->cancel();
        else
            testConnection();
    });
    connect(m_refresh, &QPushButton::clicked, this, [this] { refreshModels(); });

    m_locked = Ai::Settings::isLocked();
    setConfig(Ai::Settings::load());
    if (m_locked) {
        m_lockLabel->setText(tr("Le impostazioni AI sono bloccate dall'amministratore di questa installazione "
                                "(file tessera.ini): possono essere solo consultate e provate."));
        m_lockLabel->setVisible(true);
    }
    updateEnabled();
}

AiSettingsDialog::~AiSettingsDialog() = default;

Ai::Config AiSettingsDialog::config() const
{
    Ai::Config c;
    c.provider = Ai::providerFromId(m_provider->currentData().toString());
    c.baseUrl = m_url->text().trimmed();
    c.model = m_model->currentText().trimmed();
    c.apiKey = m_key->text().trimmed();
    c.timeoutSec = m_timeout->value();
    return c;
}

void AiSettingsDialog::setConfig(const Ai::Config &c)
{
    QSignalBlocker block(m_provider);
    m_provider->setCurrentIndex(qMax(0, m_provider->findData(Ai::providerId(c.provider))));
    m_shown = c.provider;
    m_url->setText(c.baseUrl);
    m_model->clear();
    m_model->setCurrentText(c.model);
    m_key->setText(c.apiKey);
    m_timeout->setValue(c.timeoutSec);
    updateEnabled();
}

void AiSettingsDialog::selectProvider(Ai::Provider provider)
{
    m_provider->setCurrentIndex(qMax(0, m_provider->findData(Ai::providerId(provider))));
}

// Switching provider proposes that provider's address and model, and loads its key from the credential store.
void AiSettingsDialog::providerChanged()
{
    const Ai::Provider p = Ai::providerFromId(m_provider->currentData().toString());
    if (m_url->text().trimmed().isEmpty() || m_url->text().trimmed() == Ai::defaultBaseUrl(m_shown))
        m_url->setText(Ai::defaultBaseUrl(p));
    if (m_model->currentText().trimmed().isEmpty() || m_model->currentText().trimmed() == Ai::defaultModel(m_shown))
        m_model->setCurrentText(Ai::defaultModel(p));
    m_model->clear();
    m_key->setText(p == Ai::Provider::None ? QString() : Ai::Secrets::read(Ai::Settings::apiKeySecretName(p)));
    m_shown = p;
    setStatus({}, false);
    updateEnabled();
}

void AiSettingsDialog::updateEnabled()
{
    const bool any = Ai::providerFromId(m_provider->currentData().toString()) != Ai::Provider::None;
    const bool edit = !m_locked;
    m_provider->setEnabled(edit);
    m_url->setEnabled(edit && any);
    m_model->setEnabled(edit && any);
    m_key->setEnabled(edit && any);
    m_timeout->setEnabled(edit && any);
    m_refresh->setEnabled(any && !m_reply);
    m_test->setEnabled(any);
    m_test->setText(m_reply ? tr("Annulla") : tr("Prova connessione"));
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(!m_locked || m_project);
}

void AiSettingsDialog::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_statusError = error;
    m_status->setStyleSheet(error ? QStringLiteral("color: #b00020;") : QString());
}

QString AiSettingsDialog::statusText() const
{
    return m_status->text();
}

QStringList AiSettingsDialog::modelList() const
{
    QStringList out;
    for (int i = 0; i < m_model->count(); ++i)
        out << m_model->itemText(i);
    return out;
}

bool AiSettingsDialog::ensureCloudConfirmed()
{
    const Ai::Provider p = config().provider;
    if (!Ai::isCloud(p) || Ai::Settings::cloudConfirmed(p))
        return true;
    if (!m_confirm(Ai::Settings::cloudNotice(p)))
        return false;
    if (!m_locked)
        Ai::Settings::setCloudConfirmed(p, true);
    return true;
}

void AiSettingsDialog::testConnection()
{
    startCall(false);
}

void AiSettingsDialog::refreshModels()
{
    startCall(true);
}

// The only places where this dialog talks to the network, both from a click of the user.
void AiSettingsDialog::startCall(bool listOnly)
{
    if (m_reply)
        return;
    const Ai::Config c = config();
    const QString problem = c.validate();
    // Listing models needs only the address; the connection test needs the whole configuration.
    if (c.provider == Ai::Provider::None || (!listOnly && !problem.isEmpty())) {
        setStatus(problem, true);
        return;
    }
    if (!ensureCloudConfirmed()) {
        setStatus(tr("Operazione annullata: l'invio dei dati al provider cloud non è stato confermato."), true);
        return;
    }
    delete m_client;
    m_client = new Ai::Client(c, this);
    m_reply = listOnly ? m_client->listModels() : m_client->testConnection();
    setStatus(tr("Connessione in corso…"), false);
    updateEnabled();
    Ai::Reply *reply = m_reply;
    connect(reply, &Ai::Reply::finished, this, [this, reply, listOnly] {
        const Ai::Response r = reply->response();
        m_reply = nullptr;
        reply->deleteLater();
        if (!r.ok) {
            setStatus(r.error, r.kind != Ai::ErrorKind::Cancelled ? true : false);
        } else if (listOnly) {
            const QString current = m_model->currentText();
            m_model->clear();
            m_model->addItems(r.models);
            m_model->setCurrentText(current);
            setStatus(tr("%n modelli disponibili.", nullptr, int(r.models.size())), false);
        } else {
            setStatus(tr("Connessione riuscita: il server risponde e il modello «%1» è disponibile.")
                          .arg(config().model),
                      false);
        }
        updateEnabled();
    });
}

bool AiSettingsDialog::apply()
{
    if (m_project)
        m_project->setAiLogEnabled(m_logRequests->isChecked());
    if (m_locked)
        return true;
    const Ai::Config c = config();
    if (c.provider != Ai::Provider::None) {
        const QString problem = c.validate();
        if (!problem.isEmpty()) {
            Ui::showError(this, problem);
            return false;
        }
        if (!ensureCloudConfirmed())
            return false;
    }
    QString error;
    if (!Ai::Settings::save(c, &error)) {
        Ui::showError(this, error);
        return false;
    }
    return true;
}

void AiSettingsDialog::accept()
{
    if (m_reply)
        m_reply->cancel();
    if (apply())
        QDialog::accept();
}

void AiSettingsDialog::reject()
{
    if (m_reply)
        m_reply->cancel();
    QDialog::reject();
}
