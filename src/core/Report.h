#pragma once

#include "core/Exporters.h"

#include <QColor>
#include <QList>
#include <QString>

class Project;

// Reports (issue #45): the contents are built once as a list of blocks (title, heading, paragraph,
// quote, table...) and written as Word (.docx), PDF or HTML, so that the three formats stay consistent.
namespace Report {

struct Run {
    QString text; // '\n' = line break
    bool bold = false;
    bool italic = false;
    QColor color; // invalid = the style's colour
};

enum class BlockType {
    Title,     // cover title
    Subtitle,
    Contents,  // table of contents: a TOC field in .docx (Word fills it in), a list of links in HTML/PDF
    Heading,   // `level` 1..3
    Paragraph, // `level` = indentation steps
    Meta,      // small grey line (source of a quotation, details)
    Quote,     // quoted text; `accent` = colour of the bar at its left
    Table      // `rows`, the first row is the header
};

struct Block {
    BlockType type = BlockType::Paragraph;
    int level = 1;
    QList<Run> runs;
    QColor accent;
    Exporters::Table rows;
    bool pageBreakBefore = false;
};

struct Document {
    QString title;
    QList<Block> blocks;
};

enum class Grouping { ByCode, ByDocument };

struct Options {
    QString title; // empty = the project's name
    bool contents = true;
    bool codebook = true;
    int examplesPerCode = 2; // segments quoted under each code of the codebook
    bool segments = true;
    Grouping grouping = Grouping::ByCode;
    bool comments = true; // comments of the segments
    bool memos = true;
    bool variables = false;
    bool summaryTables = false;
};

Document build(const Project &project, const Options &options);

QString toHtml(const Document &document);
bool writeHtml(const QString &path, const Document &document, QString *error);
bool writePdf(const QString &path, const Document &document, QString *error);
// Real Word styles (Title, Heading 1-3, Quote...) and Word tables; the table of contents is a field
// that Word updates when the file is opened.
bool writeDocx(const QString &path, const Document &document, QString *error);

} // namespace Report
