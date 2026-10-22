#include "core/Images.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>

#include <cmath>

namespace Images {

namespace {
constexpr qint64 MaxFileSize = 200LL * 1024 * 1024;
} // namespace

QStringList suffixes()
{
    return {QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"), QStringLiteral("gif"),
            QStringLiteral("bmp"),  QStringLiteral("tif"),  QStringLiteral("tiff"), QStringLiteral("webp")};
}

bool isImageFile(const QString &path)
{
    return suffixes().contains(QFileInfo(path).suffix().toLower());
}

QByteArray readFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QObject::tr("Impossibile aprire %1: %2").arg(QFileInfo(path).fileName(), f.errorString());
        return {};
    }
    if (f.size() > MaxFileSize) {
        *error = QObject::tr("%1 è troppo grande (massimo 200 MB)").arg(QFileInfo(path).fileName());
        return {};
    }
    const QByteArray data = f.readAll();
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool needsPlugin = suffix == QLatin1String("tif") || suffix == QLatin1String("tiff") || suffix == QLatin1String("webp");
    if (image.isNull() && needsPlugin && reader.error() == QImageReader::UnsupportedFormatError) {
        // TIFF and WebP need the imageformats plugins of Qt's qtimageformats module, not always installed
        *error = QObject::tr("%1: formato non supportato da questa installazione (manca il plugin di Qt per leggerlo)")
                     .arg(QFileInfo(path).fileName());
        return {};
    }
    if (image.isNull()) {
        *error = QObject::tr("%1 non è un'immagine leggibile: %2").arg(QFileInfo(path).fileName(), reader.errorString());
        return {};
    }
    return data;
}

QImage crop(const QImage &image, const Segment &area)
{
    if (image.isNull())
        return {};
    const int left = int(std::floor(area.x1)), top = int(std::floor(area.y1));
    const QRect r(left, top, int(std::ceil(area.x2)) - left, int(std::ceil(area.y2)) - top);
    const QRect clipped = r.intersected(image.rect());
    return clipped.isEmpty() ? QImage() : image.copy(clipped);
}

QImage scaledToFit(const QImage &image, int maxWidth, int maxHeight)
{
    if (image.isNull() || (image.width() <= maxWidth && image.height() <= maxHeight))
        return image;
    return image.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QByteArray toPng(const QImage &image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

QString areaLabel(const Segment &area)
{
    const int w = int(std::lround(area.x2 - area.x1)), h = int(std::lround(area.y2 - area.y1));
    const QString where = QObject::tr("(%1, %2)").arg(int(std::lround(area.x1))).arg(int(std::lround(area.y1)));
    if (area.page > 0) // PDF: points on a page
        return QObject::tr("Area %1 × %2 pt a %3, pagina %4").arg(w).arg(h).arg(where).arg(area.page);
    return QObject::tr("Area %1 × %2 px a %3").arg(w).arg(h).arg(where);
}

} // namespace Images
