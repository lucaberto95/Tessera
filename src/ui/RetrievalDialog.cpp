#include "ui/RetrievalDialog.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

constexpr int CodeIdRole = Qt::UserRole;

QString modeDescription(Retrieval::Mode mode)
{
    switch (mode) {
    case Retrieval::Mode::Intersection:
        return QObject::tr("Il testo coperto contemporaneamente da tutti i codici di A.");
    case Retrieval::Mode::IntersectionSet:
        return QObject::tr("Il testo dove almeno n codici di A si sovrappongono.");
    case Retrieval::Mode::Overlap:
        return QObject::tr("I segmenti di A che si sovrappongono, anche in parte, a un segmento di B.");
    case Retrieval::Mode::OnlyOneCode:
        return QObject::tr("I segmenti di A che non si sovrappongono a segmenti di altri codici di A.");
    case Retrieval::Mode::OnlyThisCode:
        return QObject::tr("I segmenti di A che non si sovrappongono a nessun segmento di B.");
    case Retrieval::Mode::IfInside:
        return QObject::tr("I segmenti di A interamente contenuti in un segmento di B.");
    case Retrieval::Mode::IfOutside:
        return QObject::tr("I segmenti di A che non sono contenuti in nessun segmento di B.");
    case Retrieval::Mode::Near:
        return QObject::tr("I segmenti di A a non più di k paragrafi (o caratteri) da un segmento di B.");
    case Retrieval::Mode::FollowedBy:
        return QObject::tr("Un segmento di A seguito da uno di B entro k paragrafi (o caratteri): "
                           "il risultato va dall'inizio di A alla fine di B.");
    }
    return {};
}

QListWidget *makeCodeList(Project *project, const QList<qint64> &checked)
{
    auto *list = new QListWidget;
    for (const Code &c : project->codes()) {
        auto *item = new QListWidgetItem(Ui::colorIcon(c.color), project->codePath(c.id));
        item->setData(CodeIdRole, c.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(checked.contains(c.id) ? Qt::Checked : Qt::Unchecked);
        list->addItem(item);
    }
    return list;
}

} // namespace

RetrievalDialog::RetrievalDialog(Project *project, const QList<qint64> &activeDocuments,
                                 const QList<qint64> &activeCodes, QWidget *parent)
    : QDialog(parent)
    , m_project(project)
    , m_docIds(activeDocuments)
    , m_mode(new QComboBox)
    , m_listA(makeCodeList(project, activeCodes))
    , m_listB(makeCodeList(project, {}))
    , m_count(new QSpinBox)
    , m_distance(new QSpinBox)
    , m_unit(new QComboBox)
    , m_preview(new QLabel)
    , m_description(new QLabel)
{
    setWindowTitle(tr("Recupero complesso"));
    resize(760, 560);

    for (Retrieval::Mode m : {Retrieval::Mode::Intersection, Retrieval::Mode::IntersectionSet,
                              Retrieval::Mode::Overlap, Retrieval::Mode::OnlyOneCode, Retrieval::Mode::OnlyThisCode,
                              Retrieval::Mode::IfInside, Retrieval::Mode::IfOutside, Retrieval::Mode::Near,
                              Retrieval::Mode::FollowedBy})
        m_mode->addItem(Retrieval::modeLabel(m), int(m));
    m_description->setWordWrap(true);

    m_count->setRange(1, 99);
    m_count->setValue(2);
    m_distance->setRange(0, 100000);
    m_distance->setValue(1);
    m_unit->addItem(tr("paragrafi"), true);
    m_unit->addItem(tr("caratteri"), false);

    auto *params = new QFormLayout;
    params->addRow(tr("Modalità:"), m_mode);
    params->addRow(QString(), m_description);
    params->addRow(tr("Codici sovrapposti (n):"), m_count);
    auto *distRow = new QHBoxLayout;
    distRow->addWidget(m_distance);
    distRow->addWidget(m_unit);
    distRow->addStretch();
    params->addRow(tr("Distanza (k):"), distRow);

    auto *boxA = new QGroupBox(tr("Codici A"));
    auto *la = new QVBoxLayout(boxA);
    la->addWidget(m_listA);
    auto *boxB = new QGroupBox(tr("Codici B"));
    auto *lb = new QVBoxLayout(boxB);
    lb->addWidget(m_listB);
    auto *lists = new QHBoxLayout;
    lists->addWidget(boxA);
    lists->addWidget(boxB);

    auto *buttons = new QDialogButtonBox;
    m_apply = buttons->addButton(tr("Mostra nel pannello"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Chiudi"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { emit retrievalRequested(query()); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(params);
    layout->addLayout(lists, 1);
    layout->addWidget(m_preview);
    layout->addWidget(buttons);

    connect(m_mode, &QComboBox::currentIndexChanged, this, &RetrievalDialog::updateState);
    connect(m_count, &QSpinBox::valueChanged, this, &RetrievalDialog::updateState);
    connect(m_distance, &QSpinBox::valueChanged, this, &RetrievalDialog::updateState);
    connect(m_unit, &QComboBox::currentIndexChanged, this, &RetrievalDialog::updateState);
    connect(m_listA, &QListWidget::itemChanged, this, &RetrievalDialog::updateState);
    connect(m_listB, &QListWidget::itemChanged, this, &RetrievalDialog::updateState);
    updateState();
}

QList<qint64> RetrievalDialog::checkedCodes(QListWidget *list) const
{
    QList<qint64> ids;
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->checkState() == Qt::Checked)
            ids << list->item(i)->data(CodeIdRole).toLongLong();
    return ids;
}

Retrieval::Query RetrievalDialog::query() const
{
    Retrieval::Query q;
    q.mode = static_cast<Retrieval::Mode>(m_mode->currentData().toInt());
    q.codesA = checkedCodes(m_listA);
    q.codesB = checkedCodes(m_listB);
    q.count = m_count->value();
    q.distance = m_distance->value();
    q.inParagraphs = m_unit->currentData().toBool();
    return q;
}

int RetrievalDialog::previewCount() const
{
    const Retrieval::Query q = query();
    QList<qint64> codes = q.codesA;
    for (qint64 c : q.codesB)
        if (!codes.contains(c))
            codes << c;
    return int(Retrieval::run(q, m_project->segments(m_docIds, codes),
                              [this](qint64 id) { return m_project->documentText(id); })
                   .size());
}

void RetrievalDialog::updateState()
{
    const Retrieval::Mode mode = static_cast<Retrieval::Mode>(m_mode->currentData().toInt());
    m_description->setText(modeDescription(mode));
    m_listB->setEnabled(Retrieval::needsCodesB(mode));
    m_count->setEnabled(mode == Retrieval::Mode::IntersectionSet);
    const bool usesDistance = mode == Retrieval::Mode::Near || mode == Retrieval::Mode::FollowedBy;
    m_distance->setEnabled(usesDistance);
    m_unit->setEnabled(usesDistance);

    const Retrieval::Query q = query();
    const bool ready = !q.codesA.isEmpty() && (!Retrieval::needsCodesB(mode) || !q.codesB.isEmpty());
    m_apply->setEnabled(ready);
    m_preview->setText(ready ? tr("Risultati: %1 (documenti attivi: %2)")
                                   .arg(previewCount())
                                   .arg(m_docIds.isEmpty() ? tr("tutti") : QString::number(m_docIds.size()))
                             : tr("Scegli almeno un codice in A%1.")
                                   .arg(Retrieval::needsCodesB(mode) ? tr(" e uno in B") : QString()));
}
