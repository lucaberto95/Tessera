#include "ui/ReportDialog.h"
#include "core/Project.h"
#include "ui/UiUtils.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

ReportDialog::ReportDialog(Project *project, QWidget *parent) : QDialog(parent), m_project(project)
{
    setWindowTitle(tr("Crea report"));

    m_title = new QLineEdit(project->name(), this);
    m_title->setPlaceholderText(project->name());
    m_contents = new QCheckBox(tr("Indice dei contenuti"), this);
    m_contents->setChecked(true);
    m_codebook = new QCheckBox(tr("Codebook (gerarchia, colori, descrizioni, frequenze)"), this);
    m_codebook->setChecked(true);
    m_examples = new QSpinBox(this);
    m_examples->setRange(0, 20);
    m_examples->setValue(2);
    m_examples->setToolTip(tr("Segmenti citati sotto ogni codice del codebook"));
    m_segments = new QCheckBox(tr("Segmenti codificati"), this);
    m_segments->setChecked(true);
    m_grouping = new QComboBox(this);
    m_grouping->addItem(tr("Per codice"), int(Report::Grouping::ByCode));
    m_grouping->addItem(tr("Per documento"), int(Report::Grouping::ByDocument));
    m_comments = new QCheckBox(tr("Includi i commenti dei segmenti"), this);
    m_comments->setChecked(true);
    m_memos = new QCheckBox(tr("Memo"), this);
    m_memos->setChecked(true);
    m_variables = new QCheckBox(tr("Variabili dei documenti (tabella)"), this);
    m_summaryTables = new QCheckBox(tr("Tabelle riassuntive"), this);
    m_format = new QComboBox(this);
    m_format->addItem(tr("Word (.docx)"), int(Format::Docx));
    m_format->addItem(tr("PDF (.pdf)"), int(Format::Pdf));
    m_format->addItem(tr("Pagina HTML (.html)"), int(Format::Html));

    auto *general = new QFormLayout;
    general->addRow(tr("Titolo:"), m_title);
    general->addRow(tr("Formato:"), m_format);

    auto *contents = new QGroupBox(tr("Contenuti"), this);
    auto *cl = new QFormLayout(contents);
    cl->addRow(m_contents);
    cl->addRow(m_codebook);
    cl->addRow(tr("   Esempi per codice:"), m_examples);
    cl->addRow(m_segments);
    cl->addRow(tr("   Raggruppamento:"), m_grouping);
    cl->addRow(m_comments);
    cl->addRow(m_memos);
    cl->addRow(m_variables);
    cl->addRow(m_summaryTables);
    connect(m_codebook, &QCheckBox::toggled, m_examples, &QWidget::setEnabled);
    connect(m_segments, &QCheckBox::toggled, m_grouping, &QWidget::setEnabled);
    connect(m_segments, &QCheckBox::toggled, m_comments, &QWidget::setEnabled);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Crea…"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Annulla"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(general);
    layout->addWidget(contents);
    layout->addWidget(buttons);
}

Report::Options ReportDialog::options() const
{
    Report::Options o;
    o.title = m_title->text().trimmed();
    o.contents = m_contents->isChecked();
    o.codebook = m_codebook->isChecked();
    o.examplesPerCode = m_examples->value();
    o.segments = m_segments->isChecked();
    o.grouping = Report::Grouping(m_grouping->currentData().toInt());
    o.comments = m_comments->isChecked();
    o.memos = m_memos->isChecked();
    o.variables = m_variables->isChecked();
    o.summaryTables = m_summaryTables->isChecked();
    return o;
}

void ReportDialog::setOptions(const Report::Options &o)
{
    m_title->setText(o.title);
    m_contents->setChecked(o.contents);
    m_codebook->setChecked(o.codebook);
    m_examples->setValue(o.examplesPerCode);
    m_segments->setChecked(o.segments);
    m_grouping->setCurrentIndex(m_grouping->findData(int(o.grouping)));
    m_comments->setChecked(o.comments);
    m_memos->setChecked(o.memos);
    m_variables->setChecked(o.variables);
    m_summaryTables->setChecked(o.summaryTables);
}

ReportDialog::Format ReportDialog::format() const
{
    return Format(m_format->currentData().toInt());
}

void ReportDialog::setFormat(Format format)
{
    m_format->setCurrentIndex(m_format->findData(int(format)));
}

bool ReportDialog::generate(const QString &path)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const Report::Document doc = Report::build(*m_project, options());
    QString error;
    bool ok = false;
    switch (format()) {
    case Format::Docx: ok = Report::writeDocx(path, doc, &error); break;
    case Format::Pdf: ok = Report::writePdf(path, doc, &error); break;
    case Format::Html: ok = Report::writeHtml(path, doc, &error); break;
    }
    QApplication::restoreOverrideCursor();
    if (!ok)
        Ui::showError(this, error);
    return ok;
}

void ReportDialog::accept()
{
    QString suffix, filter;
    switch (format()) {
    case Format::Docx:
        suffix = QStringLiteral("docx");
        filter = tr("Documento Word (*.docx)");
        break;
    case Format::Pdf:
        suffix = QStringLiteral("pdf");
        filter = tr("Documento PDF (*.pdf)");
        break;
    case Format::Html:
        suffix = QStringLiteral("html");
        filter = tr("Pagina HTML (*.html)");
        break;
    }
    QString path = Ui::saveFileName(this, tr("Salva il report"),
                                    m_project->name() + tr(" - report.") + suffix, filter);
    if (path.isEmpty())
        return;
    if (!path.endsWith(QLatin1Char('.') + suffix, Qt::CaseInsensitive))
        path += QLatin1Char('.') + suffix;
    if (generate(path))
        QDialog::accept();
}
