#include "ui/UiUtils.h"
#include "core/Exporters.h"
#include "core/Project.h"
#include "core/Xlsx.h"
#include "ui/MemoDialog.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QTextDocument>

namespace Ui {

namespace {

QString lastDir()
{
    return QSettings().value(QStringLiteral("ui/lastDir"), QDir::homePath()).toString();
}

void rememberDir(const QString &file)
{
    if (!file.isEmpty())
        QSettings().setValue(QStringLiteral("ui/lastDir"), QFileInfo(file).absolutePath());
}

// Message boxes with Qt::PlainText: their text often contains names from the project.
QMessageBox::StandardButton plainMessage(QWidget *parent, QMessageBox::Icon icon, const QString &title,
                                         const QString &text, QMessageBox::StandardButtons buttons,
                                         QMessageBox::StandardButton defaultButton)
{
    QMessageBox box(icon, title, text, buttons, parent);
    box.setTextFormat(Qt::PlainText);
    box.setDefaultButton(defaultButton);
    return QMessageBox::StandardButton(box.exec());
}

} // namespace

QString plainToolTip(const QString &text)
{
    return text.isEmpty() ? QString() : Qt::convertFromPlainText(text, Qt::WhiteSpaceNormal);
}

void showInfo(QWidget *parent, const QString &title, const QString &text)
{
    plainMessage(parent, QMessageBox::Information, title, text, QMessageBox::Ok, QMessageBox::Ok);
}

QIcon colorIcon(const QColor &color, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(color);
    p.setPen(color.darker(140));
    p.drawRoundedRect(QRectF(1.5, 1.5, size - 3, size - 3), 3, 3);
    return QIcon(pm);
}

QIcon memoIcon()
{
    static QIcon icon = [] {
        QPixmap pm(14, 14);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(QColor(0xffe082));
        p.setPen(QColor(0xc49000));
        p.drawRect(QRectF(2.5, 1.5, 9, 11));
        p.setPen(QColor(0x9a7000));
        for (int y = 4; y <= 10; y += 3)
            p.drawLine(QPointF(4.5, y + 0.5), QPointF(9.5, y + 0.5));
        return QIcon(pm);
    }();
    return icon;
}

bool confirm(QWidget *parent, const QString &title, const QString &text)
{
    return plainMessage(parent, QMessageBox::Question, title, text, QMessageBox::Yes | QMessageBox::No,
                        QMessageBox::No) == QMessageBox::Yes;
}

void showError(QWidget *parent, const QString &text)
{
    plainMessage(parent, QMessageBox::Warning, QObject::tr("Tessera"), text, QMessageBox::Ok, QMessageBox::Ok);
}

QString askText(QWidget *parent, const QString &title, const QString &label, const QString &value)
{
    bool ok = false;
    const QString text = QInputDialog::getText(parent, title, label, QLineEdit::Normal, value, &ok).trimmed();
    if (!ok || text.isEmpty())
        return {};
    return text;
}

QString saveFileName(QWidget *parent, const QString &title, const QString &suggestedName, const QString &filter)
{
    const QString file = QFileDialog::getSaveFileName(parent, title, QDir(lastDir()).filePath(suggestedName), filter);
    rememberDir(file);
    return file;
}

QString openFileName(QWidget *parent, const QString &title, const QString &filter)
{
    const QString file = QFileDialog::getOpenFileName(parent, title, lastDir(), filter);
    rememberDir(file);
    return file;
}

QStringList openFileNames(QWidget *parent, const QString &title, const QString &filter)
{
    const QStringList files = QFileDialog::getOpenFileNames(parent, title, lastDir(), filter);
    if (!files.isEmpty())
        rememberDir(files.first());
    return files;
}

void editMemo(QWidget *parent, Project *project, int targetType, qint64 targetId)
{
    openMemo(parent, project, project->memoFor(static_cast<MemoTarget>(targetType), targetId));
}

void openMemo(QWidget *parent, Project *project, const Memo &memo)
{
    QWidget *window = parent ? parent->window() : nullptr;
    if (memo.id) {
        for (QWidget *w : QApplication::topLevelWidgets()) {
            auto *open = qobject_cast<MemoDialog *>(w);
            if (open && open->memoId() == memo.id && open->isVisible()) {
                open->raise();
                open->activateWindow();
                return;
            }
        }
    }
    auto *dlg = new MemoDialog(project, memo, window);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

QString memoTargetLabel(const Project *project, int targetType, qint64 id)
{
    return Exporters::memoTargetLabel(*project, static_cast<MemoTarget>(targetType), id);
}

bool saveXlsx(QWidget *parent, const QString &path, const QList<Xlsx::Sheet> &sheets)
{
    QString error;
    int truncated = 0;
    if (!Xlsx::writeWorkbook(path, sheets, &error, &truncated)) {
        showError(parent, error);
        return false;
    }
    if (truncated > 0)
        showInfo(parent, QObject::tr("Esporta in Excel"),
                 QObject::tr("%n celle superavano il limite di Excel di 32.767 caratteri e sono state troncate.",
                             nullptr, truncated));
    return true;
}

bool isXlsxPath(const QString &path)
{
    return path.endsWith(QLatin1String(".xlsx"), Qt::CaseInsensitive);
}

} // namespace Ui
