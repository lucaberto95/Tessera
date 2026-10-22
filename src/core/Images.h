#pragma once

#include "core/Model.h"

#include <QByteArray>
#include <QImage>
#include <QString>
#include <QStringList>

// Helpers for image documents (issue #27). Area coordinates are pixels of the upright image, i.e.
// the one returned by Project::documentImage (EXIF orientation applied).
namespace Images {

QStringList suffixes(); // png, jpg, jpeg, gif, bmp, tif, tiff, webp
bool isImageFile(const QString &path);

// Reads an image file to be stored in a project: the bytes are kept as they are, after checking that
// Qt can decode them (the needed imageformats plugin is there) and that the file is not absurdly big.
QByteArray readFile(const QString &path, QString *error);

// The part of `image` covered by an area segment (clamped to the image); null if nothing is left.
QImage crop(const QImage &image, const Segment &area);
QImage scaledToFit(const QImage &image, int maxWidth, int maxHeight);
QByteArray toPng(const QImage &image);
// "Area 120 × 80 px a (10, 20)": the textual form of an area, for tables and tooltips.
QString areaLabel(const Segment &area);

} // namespace Images
