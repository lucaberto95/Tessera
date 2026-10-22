#include "AiCodesDialog.h"

#include "AiUi.h"
#include "UiUtils.h"
#include "core/Project.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

using namespace Ai::Codes;

namespace {
constexpr int RowRole = Qt::UserRole + 1; // refId in the detail list
}

AiCodesDialog::AiCodesDialog(Project *project, const Material &material, const QString &subject, qint64 contextId,
                             QWidget *parent)
    : QDialog(parent), m_project(project), m_material(material), m_contextId(contextId)
{
    setWindowTitle(tr("AI: %1").arg(taskTitle(material.task)));
    setMinimumSize(640, 620);
    m_confirm = [this](const QString &notice) { return Ui::confirm(this, tr("Invio dei dati a un provider cloud"), notice); };

    auto *subjectLabel = new QLabel(subject);
    subjectLabel->setTextFormat(Qt::PlainText);
    subjectLabel->setWordWrap(true);

    m_language = new QComboBox;
    m_language->setEditable(true);
    m_language->addItems({tr("italiano"), tr("inglese"), tr("francese"), tr("spagnolo"), tr("tedesco"), tr("portoghese")});
    m_language->setCurrentText(tr("italiano"));
    m_instructions = new QLineEdit;
    m_instructions->setPlaceholderText(tr("Opzionali, ad esempio «codici descrittivi, non interpretativi»"));
    m_max = new QSpinBox;
    m_max->setRange(1, 30);
    m_max->setValue(8);

    auto *form = new QFormLayout;
    form->addRow(tr("Lingua dei nomi:"), m_language);
    form->addRow(tr("Numero massimo di proposte:"), m_max);
    form->addRow(tr("Istruzioni aggiuntive:"), m_instructions);
    if (material.task == Task::SuggestCodes) {
        m_parent = new QComboBox;
        m_parent->addItem(tr("(primo livello)"), qint64(0));
        for (const Code &c : m_project->codesInTreeOrder())
            m_parent->addItem(m_project->codePath(c.id), c.id);
        m_parent->setCurrentIndex(qMax(0, m_parent->findData(contextId)));
        form->addRow(tr("Crea i codici sotto:"), m_parent);
    }

    m_recipient = new QLabel;
    m_recipient->setTextFormat(Qt::PlainText);
    m_recipient->setWordWrap(true);
    m_input = new QLabel;
    m_input->setTextFormat(Qt::PlainText);
    m_input->setWordWrap(true);
    m_status = new QLabel;
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);

    m_table = new QTableWidget(0, 3);
    m_table->setObjectName(QStringLiteral("aiProposals"));
    m_table->setHorizontalHeaderLabels({tr("Proposta"), tr("Descrizione"),
                                        material.task == Task::SuggestSubcodes ? tr("Segmenti")
                                        : material.task == Task::GroupCodes    ? tr("Codici")
                                                                               : tr("Elementi")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->setColumnWidth(0, 200);
    m_table->setColumnHidden(2, material.task == Task::SuggestCodes);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setWordWrap(true);
    m_table->setToolTip(tr("Spunta le proposte da creare; nome e descrizione si possono modificare"));

    m_detail = new QListWidget;
    m_detail->setObjectName(QStringLiteral("aiDetail"));
    m_detail->setWordWrap(true);
    m_detail->setVisible(material.task != Task::SuggestCodes);
    m_detail->setToolTip(tr("Cosa verrebbe spostato nella proposta selezionata: togli la spunta per lasciare un elemento dov'è"));

    m_provenance = new QCheckBox(tr("Aggiungi alla descrizione dei nuovi codici la nota di provenienza (proposto dall'AI)"));
    m_provenance->setChecked(true);

    auto button = [](const QString &text) {
        auto *b = new QPushButton(text);
        b->setAutoDefault(false);
        return b;
    };
    m_generate = button(tr("Genera"));
    m_generate->setToolTip(tr("Invia il testo mostrato sopra al modello e attende la risposta"));
    m_cancel = button(tr("Annulla richiesta"));
    m_apply = button(tr("Applica"));
    m_apply->setToolTip(tr("Crea i codici spuntati; si può annullare con Modifica > Annulla"));
    auto *selectAll = button(tr("Seleziona tutti"));
    auto *selectNone = button(tr("Deseleziona tutti"));
    auto *close = button(tr("Chiudi"));

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_generate);
    buttons->addWidget(m_cancel);
    buttons->addStretch(1);
    buttons->addWidget(selectAll);
    buttons->addWidget(selectNone);
    buttons->addWidget(m_apply);
    buttons->addWidget(close);

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(m_table);
    split->addWidget(m_detail);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(subjectLabel);
    layout->addLayout(form);
    layout->addWidget(m_recipient);
    layout->addWidget(m_input);
    layout->addWidget(split, 1);
    layout->addWidget(m_provenance);
    layout->addWidget(m_status);
    layout->addLayout(buttons);

    connect(m_generate, &QPushButton::clicked, this, [this] { generate(); });
    connect(m_cancel, &QPushButton::clicked, this, [this] { cancelRequest(); });
    connect(m_apply, &QPushButton::clicked, this, [this] { applyProposals(); });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(selectAll, &QPushButton::clicked, this, [this] {
        for (int r = 0; r < m_table->rowCount(); ++r)
            m_table->item(r, 0)->setCheckState(Qt::Checked);
    });
    connect(selectNone, &QPushButton::clicked, this, [this] {
        for (int r = 0; r < m_table->rowCount(); ++r)
            m_table->item(r, 0)->setCheckState(Qt::Unchecked);
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] { showDetail(); });
    connect(m_table, &QTableWidget::itemChanged, this, [this] { updateEnabled(); });
    connect(m_detail, &QListWidget::itemChanged, this, &AiCodesDialog::onDetailChanged);

    m_input->setText(tr("Verrà inviato: %1").arg(inputDescription()));
    m_recipient->setText(recipientText());
    updateEnabled();
}

AiCodesDialog::~AiCodesDialog() = default;

Options AiCodesDialog::options() const
{
    Options o;
    o.language = m_language->currentText().trimmed();
    o.instructions = m_instructions->text().trimmed();
    o.maxSuggestions = m_max->value();
    return o;
}

void AiCodesDialog::setOptions(const Options &o)
{
    m_language->setCurrentText(o.language);
    m_instructions->setText(o.instructions);
    m_max->setValue(o.maxSuggestions);
}

QString AiCodesDialog::inputDescription() const
{
    const qint64 words = m_material.words();
    const QString wordText = QLocale(QLocale::Italian).toString(words) + QLatin1Char(' ') + (words == 1 ? tr("parola") : tr("parole"));
    switch (m_material.task) {
    case Task::SuggestCodes: {
        QString s = Ai::describeInput(1, words);
        if (!m_material.existingNames.isEmpty())
            s += tr(", più i nomi di %n codici esistenti", nullptr, int(m_material.existingNames.size()));
        return s;
    }
    case Task::SuggestSubcodes:
        return tr("%n segmenti", nullptr, int(m_material.items.size())) + QStringLiteral(", ~") + wordText
               + tr(" e il nome del codice «%1»").arg(m_material.subject);
    case Task::GroupCodes:
        return tr("%n codici (nome e descrizione)", nullptr, int(m_material.items.size())) + QStringLiteral(", ~") + wordText;
    }
    return {};
}

QString AiCodesDialog::recipientText() const
{
    return Ui::aiRecipientText();
}

qint64 AiCodesDialog::parentId() const
{
    return m_parent ? m_parent->currentData().toLongLong() : m_contextId;
}

QString AiCodesDialog::statusText() const
{
    return m_status->text();
}

void AiCodesDialog::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_statusError = error;
    m_status->setStyleSheet(error ? QStringLiteral("color: #b00020;") : QString());
}

int AiCodesDialog::proposalCount() const
{
    return m_table->rowCount();
}

void AiCodesDialog::setProvenanceEnabled(bool on)
{
    m_provenance->setChecked(on);
}

bool AiCodesDialog::provenanceEnabled() const
{
    return m_provenance->isChecked();
}

void AiCodesDialog::updateEnabled()
{
    const bool busy = isBusy();
    bool any = false;
    for (int r = 0; r < m_table->rowCount(); ++r)
        any = any || (m_table->item(r, 0)->checkState() == Qt::Checked && !m_table->item(r, 0)->text().trimmed().isEmpty());
    m_generate->setEnabled(!busy);
    m_generate->setText(m_table->rowCount() ? tr("Rigenera") : tr("Genera"));
    m_cancel->setVisible(busy);
    m_apply->setEnabled(!busy && any);
    m_language->setEnabled(!busy);
    m_instructions->setEnabled(!busy);
    m_max->setEnabled(!busy);
    if (m_parent)
        m_parent->setEnabled(!busy);
}

QString AiCodesDialog::itemText(qint64 refId) const
{
    for (const Item &i : m_material.items)
        if (i.refId == refId)
            return i.text;
    return {};
}

void AiCodesDialog::fillTable(const Result &result)
{
    m_filling = true;
    m_table->setRowCount(0);
    m_rowRefs.clear();
    m_rowLeftOut.clear();
    for (const Proposal &p : result.proposals) {
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        auto *name = new QTableWidgetItem(p.name);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        name->setCheckState(p.selected ? Qt::Checked : Qt::Unchecked);
        auto *desc = new QTableWidgetItem(p.description);
        desc->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
        auto *count = new QTableWidgetItem(QString::number(p.refIds.size()));
        count->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        count->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, desc);
        m_table->setItem(row, 2, count);
        m_rowRefs.append(p.refIds);
        m_rowLeftOut.append(QSet<qint64>());
    }
    m_table->resizeRowsToContents();
    m_filling = false;
    if (m_table->rowCount())
        m_table->selectRow(0);
    showDetail();
}

void AiCodesDialog::showDetail()
{
    if (m_filling)
        return;
    m_filling = true;
    m_detail->clear();
    const int row = m_table->currentRow();
    if (row >= 0 && row < m_rowRefs.size()) {
        for (qint64 ref : m_rowRefs.at(row)) {
            QString text = itemText(ref);
            if (text.size() > 300)
                text = text.left(300) + QStringLiteral("…");
            auto *it = new QListWidgetItem(text.simplified());
            it->setData(RowRole, ref);
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            it->setCheckState(m_rowLeftOut.at(row).contains(ref) ? Qt::Unchecked : Qt::Checked);
            m_detail->addItem(it);
        }
    }
    m_filling = false;
}

void AiCodesDialog::onDetailChanged(QListWidgetItem *item)
{
    const int row = m_table->currentRow();
    if (m_filling || row < 0 || row >= m_rowLeftOut.size())
        return;
    const qint64 ref = item->data(RowRole).toLongLong();
    if (item->checkState() == Qt::Checked)
        m_rowLeftOut[row].remove(ref);
    else
        m_rowLeftOut[row].insert(ref);
    m_filling = true;
    m_table->item(row, 2)->setText(QString::number(m_rowRefs.at(row).size() - m_rowLeftOut.at(row).size()));
    m_filling = false;
}

QList<Proposal> AiCodesDialog::acceptedProposals() const
{
    QList<Proposal> out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        if (m_table->item(r, 0)->checkState() != Qt::Checked)
            continue;
        Proposal p;
        p.name = m_table->item(r, 0)->text().simplified();
        p.description = m_table->item(r, 1)->text().trimmed();
        for (qint64 ref : m_rowRefs.value(r))
            if (!m_rowLeftOut.value(r).contains(ref))
                p.refIds.append(ref);
        if (!p.name.isEmpty())
            out.append(p);
    }
    return out;
}

// The only place where this dialog sends anything, from a click of the user on "Genera"/"Rigenera".
void AiCodesDialog::generate()
{
    if (m_run)
        return;
    m_recipient->setText(recipientText());
    QString error;
    delete m_client;
    m_client = Ui::makeConfirmedAiClient(this, m_confirm, &m_usedConfig, &error);
    if (!m_client) {
        setStatus(error, true);
        return;
    }
    m_run = new Run(m_client, m_material, options(), this);
    m_run->setMaxChunkTokens(m_maxChunkTokens);
    m_run->setMaxItemsPerRequest(m_maxItems);
    connect(m_run, &Run::progress, this, [this](int done, int total) {
        if (total > 1)
            setStatus(tr("Richiesta %1 di %2…").arg(qMin(done + 1, total)).arg(total), false);
    });
    Run *run = m_run;
    connect(run, &Run::finished, this, [this, run] {
        const Ai::Response r = run->response();
        const Result result = run->result();
        m_run = nullptr;
        run->deleteLater();
        if (m_project) {
            QStringList names;
            for (const Proposal &p : result.proposals)
                names << p.name;
            m_project->addAiLog(functionId(m_material.task), Ai::providerId(m_usedConfig.provider), m_usedConfig.model,
                                inputDescription(), r.ok ? names.join(QStringLiteral("; ")) : r.error);
        }
        if (r.ok) {
            fillTable(result);
            QString text;
            if (result.proposals.isEmpty())
                text = tr("Il modello non ha proposto nulla di utilizzabile.");
            else
                text = tr("%n proposte di %1. Non è stato creato nulla: controllale e premi «Applica».", nullptr,
                          int(result.proposals.size()))
                           .arg(Ai::authorName(m_usedConfig));
            if (result.invalidIds)
                text += QLatin1Char(' ') + tr("Scartati %n riferimenti non validi.", nullptr, result.invalidIds);
            if (result.skippedProposals)
                text += QLatin1Char(' ') + tr("Scartate %n proposte vuote o già esistenti.", nullptr, result.skippedProposals);
            setStatus(text, false);
        } else {
            setStatus(r.error, r.kind != Ai::ErrorKind::Cancelled);
        }
        updateEnabled();
    });
    setStatus(tr("Richiesta in corso…"), false);
    updateEnabled();
    run->start();
}

void AiCodesDialog::cancelRequest()
{
    if (m_run)
        m_run->cancel();
}

void AiCodesDialog::reject()
{
    cancelRequest();
    QDialog::reject();
}

ApplyReport AiCodesDialog::applyProposals()
{
    ApplyReport report;
    if (isBusy() || !m_project)
        return report;
    const QList<Proposal> accepted = acceptedProposals();
    if (accepted.isEmpty())
        return report;
    const Ai::Config cfg = m_usedConfig.model.isEmpty() ? Ai::Settings::load() : m_usedConfig;
    const QString note = m_provenance->isChecked() ? provenanceNote(cfg) : QString();
    report = Ai::Codes::apply(*m_project, m_material.task, parentId(), accepted, note);
    QString text = tr("Creati %n codici", nullptr, report.createdCodes);
    if (report.movedSegments)
        text += tr(", spostati %n segmenti", nullptr, report.movedSegments);
    if (report.movedCodes)
        text += tr(", spostati %n codici", nullptr, report.movedCodes);
    text += QStringLiteral(". ") + tr("Si può annullare con Modifica > Annulla.");
    if (report.skipped)
        text += QLatin1Char(' ') + tr("%n elementi saltati (nome già usato o dati cambiati).", nullptr, report.skipped);
    setStatus(text, false);
    // The proposals are used: clear them so they cannot be applied twice.
    fillTable(Result());
    updateEnabled();
    return report;
}