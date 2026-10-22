#pragma once

#include "core/Model.h"

#include <QBuffer>
#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QPdfDocument>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>

// Helpers for PDF documents shown as pages (issue #26). Pages are 1-based; coordinates are PDF points
// (1/72 inch) with the origin at the top left of the page, so they do not depend on the zoom.
namespace Pdfs {

bool isPdfFile(const QString &path);

// A PDF loaded from memory. Cheap to keep around, not copyable (it owns the QPdfDocument).
class Doc {
public:
    Doc();
    Doc(const Doc &) = delete;
    Doc &operator=(const Doc &) = delete;

    bool load(const QByteArray &data);
    bool isValid() const { return m_valid; }
    int pageCount() const;
    QSizeF pageSize(int page) const; // points; null for a bad page

    // Text layer of a page as pdfium reads it (CR LF line ends included): see pageText() for the clean form.
    QString rawPageText(int page) const;
    QString pageText(int page) const;
    // Text whose glyph centres lie inside `area` (points), in reading order with line breaks kept.
    QString textInArea(int page, const QRectF &area) const;
    // Selection between two points of a page, as when dragging with the mouse: the text and the
    // polygons covering it (points). `rect` is their bounding box. Each point selects the glyph nearest
    // to it, so a drag may start and end in the blank space around the text.
    struct Selection {
        QString text;
        QList<QPolygonF> bounds;
        QRectF rect;
        bool isEmpty() const { return text.isEmpty() || rect.isEmpty(); }
    };
    Selection select(int page, const QPointF &from, const QPointF &to) const;

    // The page rendered at the given pixel size (white background).
    QImage render(int page, const QSize &pixelSize) const;
    // The part of the page covered by an area segment, scaled to fit in maxWidth x maxHeight (never
    // magnified more than maxScale pixels per point); null if the area is outside the page.
    QImage cropArea(const Segment &area, int maxWidth, int maxHeight, double maxScale = 3.0) const;

private:
    // Glyph boxes of a page, indexed like rawPageText(); a glyph without a box (line break) has an empty one.
    struct PageChars {
        QString text;
        QList<QRectF> boxes;
    };
    const PageChars &chars(int page) const;
    int nearestChar(const PageChars &pc, const QPointF &pt) const; // -1 if the page has no glyph boxes

    QByteArray m_data;
    mutable QHash<int, PageChars> m_chars;
    mutable QBuffer m_buffer;
    mutable QPdfDocument m_pdf;
    bool m_valid = false;
};

// Reads a PDF file to be stored in a project: the bytes are kept as they are, after checking that
// they load and have at least one page.
QByteArray readFile(const QString &path, QString *error);

// Text of every page, normalised; and the document content built from them. Pages are separated by a
// form feed, so the page of a position in the content is pageOfOffset().
QStringList pageTexts(const Doc &doc);
QString joinPages(const QStringList &pages);
int pageOfOffset(const QString &content, int offset); // 1-based

} // namespace Pdfs
