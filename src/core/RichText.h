#pragma once

#include <QList>
#include <QString>

class QTextDocument;

// Formatting of a text document (issue #32), kept apart from the plain text: character styles and
// paragraph kinds are ranges of UTF-16 offsets into that text. The plain text stays the only base
// of segment and link positions; applying the formatting to a QTextDocument never changes its text,
// so the positions of the document and of the plain text coincide (checked by the tests).
namespace RichText {

enum Style { Bold = 1, Italic = 2, Underline = 4, Strike = 8 };

struct Span {
    int start = 0; // inclusive
    int end = 0;   // exclusive
    int style = 0; // bit set of Style
    bool operator==(const Span &) const = default;
};

// Plain paragraphs are not recorded. `level`: heading 1-6; list nesting from 0; table cell: 0/1 =
// even/odd row (banding), 2 = header row. A table is shown as shaded lines: its cells stay the
// separate lines of the plain text, so the text of the document does not change.
enum class BlockKind { Paragraph = 0, Heading = 1, Bullet = 2, Numbered = 3, TableCell = 4 };

struct Block {
    int start = 0; // first character of the paragraph
    int end = 0;   // end of its text, excluding the line break (start == end: empty paragraph)
    BlockKind kind = BlockKind::Paragraph;
    int level = 0;
    bool operator==(const Block &) const = default;
};

struct Formatting {
    QList<Span> spans;
    QList<Block> blocks;
    bool isEmpty() const { return spans.isEmpty() && blocks.isEmpty(); }
};

// Compact JSON stored in documents.rich_content; empty string = no formatting.
QString toJson(const Formatting &f);
Formatting fromJson(const QString &json);

// Cuts the ranges to [0, textLength] and drops the ones left empty.
void clamp(Formatting &f, int textLength);

// Follows an edit of the text (`removed` characters at `position` replaced by `added`).
void adjustForEdit(Formatting &f, int position, int removed, int added);

// Collects text and formatting while an importer walks a file. The text is the importer's plain
// text; paragraphs end with '\n'.
class Builder {
public:
    QString text;
    int style = 0; // style of the text added from now on

    void add(const QString &s);
    // Ends the paragraph that started where the previous one ended.
    void endParagraph(BlockKind kind = BlockKind::Paragraph, int level = 0);
    Formatting formatting() const { return m_f; }

private:
    Formatting m_f;
    int m_lineStart = 0;
};

// Applies the formatting to a document whose text is `text` as plain text (one block per line).
// Nothing is inserted or removed. Keeps the base look of the document (fonts, line height).
void apply(QTextDocument *doc, const Formatting &f);

// Reads the formatting of a document built from rich text (HTML import). `text` receives the
// plain text, equal to doc->toPlainText(); returns empty formatting when they cannot be matched.
Formatting fromDocument(const QTextDocument &doc, QString *text);

} // namespace RichText
