#include "core/Pdfs.h"
#include "core/TextUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QPdfDocumentRenderOptions>
#include <QPdfSelection>

#include <algorithm>
#include <cmath>

namespace Pdfs {

namespace {
constexpr qint64 MaxFileSize = 500LL * 1024 * 1024;

// pdfium separates lines with CR LF and may add spaces before them; keep line breaks, drop the clutter.
QString cleanText(QString text)
{
    text = TextUtils::normalizeText(std::move(text));
    text.replace(QChar::Nbsp, QLatin1Char(' '));
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString &line : lines)
        line = line.trimmed();
    return lines.join(QLatin1Char('\n')).trimmed();
}
} // namespace

bool isPdfFile(const QString &path)
{
    return QFileInfo(path).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) == 0;
}

Doc::Doc() = default;

bool Doc::load(const QByteArray &data)
{
    m_valid = false;
    m_pdf.close();
    m_data = data;
    m_buffer.close();
    m_buffer.setData(m_data);
    if (!m_buffer.open(QIODevice::ReadOnly))
        return false;
    m_pdf.load(&m_buffer); // synchronous for a random-access device
    m_valid = m_pdf.status() == QPdfDocument::Status::Ready && m_pdf.pageCount() > 0;
    return m_valid;
}

int Doc::pageCount() const
{
    return m_valid ? m_pdf.pageCount() : 0;
}

QSizeF Doc::pageSize(int page) const
{
    if (!m_valid || page < 1 || page > m_pdf.pageCount())
        return {};
    return m_pdf.pagePointSize(page - 1);
}

QString Doc::rawPageText(int page) const
{
    if (!m_valid || page < 1 || page > m_pdf.pageCount())
        return {};
    return m_pdf.getAllText(page - 1).text();
}

QString Doc::pageText(int page) const
{
    return cleanText(rawPageText(page));
}

const Doc::PageChars &Doc::chars(int page) const
{
    auto it = m_chars.find(page);
    if (it != m_chars.end())
        return it.value();
    PageChars pc;
    if (m_valid && page >= 1 && page <= m_pdf.pageCount()) {
        pc.text = m_pdf.getAllText(page - 1).text();
        pc.boxes.reserve(pc.text.size());
        for (int i = 0; i < pc.text.size(); ++i) {
            const QPdfSelection sel = m_pdf.getSelectionAtIndex(page - 1, i, 1);
            pc.boxes << (sel.isValid() ? sel.boundingRectangle() : QRectF());
        }
    }
    return m_chars.insert(page, pc).value();
}

int Doc::nearestChar(const PageChars &pc, const QPointF &pt) const
{
    // The line closest to the point first, then the glyph of that line closest to it horizontally.
    auto dyOf = [&](const QRectF &b) { return std::max({b.top() - pt.y(), 0.0, pt.y() - b.bottom()}); };
    double minDy = -1;
    for (const QRectF &b : pc.boxes)
        if (!b.isEmpty() && (minDy < 0 || dyOf(b) < minDy))
            minDy = dyOf(b);
    if (minDy < 0)
        return -1;
    int best = -1;
    double bestDx = 0;
    for (int i = 0; i < pc.boxes.size(); ++i) {
        const QRectF &b = pc.boxes[i];
        if (b.isEmpty() || dyOf(b) > minDy + 1.0)
            continue;
        const double dx = std::max({b.left() - pt.x(), 0.0, pt.x() - b.right()});
        if (best < 0 || dx < bestDx) {
            best = i;
            bestDx = dx;
        }
    }
    return best;
}
QString Doc::textInArea(int page, const QRectF &area) const
{
    const PageChars &pc = chars(page);
    QString out, gap;
    for (int i = 0; i < pc.text.size(); ++i) {
        const QRectF &box = pc.boxes[i];
        if (box.isEmpty()) { // line breaks and other glyphs without a box: kept only between kept glyphs
            gap += pc.text.at(i);
            continue;
        }
        if (area.contains(box.center())) {
            if (!out.isEmpty())
                out += gap;
            out += pc.text.at(i);
        }
        gap.clear();
    }
    return cleanText(out);
}

Doc::Selection Doc::select(int page, const QPointF &from, const QPointF &to) const
{
    Selection result;
    const PageChars &pc = chars(page);
    int a = nearestChar(pc, from), b = nearestChar(pc, to);
    if (a < 0 || b < 0)
        return result;
    if (a > b)
        std::swap(a, b);
    const QPdfSelection sel = m_pdf.getSelectionAtIndex(page - 1, a, b - a + 1);
    if (!sel.isValid())
        return result;
    result.text = cleanText(sel.text());
    result.bounds = sel.bounds();
    result.rect = sel.boundingRectangle();
    return result;
}
QImage Doc::render(int page, const QSize &pixelSize) const
{
    if (!m_valid || page < 1 || page > m_pdf.pageCount() || pixelSize.isEmpty())
        return {};
    QImage image = m_pdf.render(page - 1, pixelSize);
    return image;
}

QImage Doc::cropArea(const Segment &area, int maxWidth, int maxHeight, double maxScale) const
{
    const QSizeF size = pageSize(area.page);
    if (size.isEmpty())
        return {};
    const QRectF page(QPointF(0, 0), size);
    const QRectF r = QRectF(QPointF(area.x1, area.y1), QPointF(area.x2, area.y2)).normalized().intersected(page);
    if (r.width() < 0.5 || r.height() < 0.5)
        return {};
    const double scale = std::min({double(maxWidth) / r.width(), double(maxHeight) / r.height(), maxScale});
    const QImage full = render(area.page, QSize(std::max(1, int(std::ceil(size.width() * scale))),
                                               std::max(1, int(std::ceil(size.height() * scale)))));
    if (full.isNull())
        return {};
    const QRect px(int(std::floor(r.left() * scale)), int(std::floor(r.top() * scale)), int(std::ceil(r.width() * scale)),
                   int(std::ceil(r.height() * scale)));
    return full.copy(px.intersected(full.rect()));
}

QByteArray readFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("Impossibile aprire %1: %2").arg(QFileInfo(path).fileName(), f.errorString());
        return {};
    }
    if (f.size() > MaxFileSize) {
        *error = QObject::tr("%1 è troppo grande (massimo 500 MB)").arg(QFileInfo(path).fileName());
        return {};
    }
    const QByteArray data = f.readAll();
    Doc doc;
    if (!doc.load(data)) {
        *error = QObject::tr("Impossibile leggere il PDF %1").arg(QFileInfo(path).fileName());
        return {};
    }
    return data;
}

QStringList pageTexts(const Doc &doc)
{
    QStringList pages;
    for (int i = 1; i <= doc.pageCount(); ++i)
        pages << doc.pageText(i).remove(QLatin1Char('\f'));
    return pages;
}

QString joinPages(const QStringList &pages)
{
    return pages.join(QLatin1Char('\f'));
}

int pageOfOffset(const QString &content, int offset)
{
    int page = 1;
    const int end = std::min<int>(offset, int(content.size()));
    for (int i = 0; i < end; ++i)
        if (content.at(i) == QLatin1Char('\f'))
            ++page;
    return page;
}

} // namespace Pdfs
