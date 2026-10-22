#include "core/RichText.h"
#include "core/TextUtils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QTextTable>

#include <algorithm>
#include <functional>

namespace RichText {

QString toJson(const Formatting &f)
{
    if (f.isEmpty())
        return {};
    QJsonArray spans, blocks;
    for (const Span &s : f.spans)
        spans.append(QJsonArray{s.start, s.end, s.style});
    for (const Block &b : f.blocks)
        blocks.append(QJsonArray{b.start, b.end, int(b.kind), b.level});
    QJsonObject o;
    o.insert(QStringLiteral("v"), 1);
    o.insert(QStringLiteral("s"), spans);
    o.insert(QStringLiteral("b"), blocks);
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

Formatting fromJson(const QString &json)
{
    Formatting f;
    if (json.isEmpty())
        return f;
    const QJsonObject o = QJsonDocument::fromJson(json.toUtf8()).object();
    for (const QJsonValue &v : o.value(QStringLiteral("s")).toArray()) {
        const QJsonArray a = v.toArray();
        if (a.size() >= 3)
            f.spans.append({a.at(0).toInt(), a.at(1).toInt(), a.at(2).toInt()});
    }
    for (const QJsonValue &v : o.value(QStringLiteral("b")).toArray()) {
        const QJsonArray a = v.toArray();
        const int kind = a.size() >= 4 ? a.at(2).toInt() : -1;
        if (kind > 0 && kind <= int(BlockKind::TableCell))
            f.blocks.append({a.at(0).toInt(), a.at(1).toInt(), BlockKind(kind), a.at(3).toInt()});
    }
    return f;
}

void clamp(Formatting &f, int textLength)
{
    QList<Span> spans;
    for (Span s : std::as_const(f.spans)) {
        s.start = qMax(0, s.start);
        s.end = qMin(textLength, s.end);
        if (s.end > s.start && s.style)
            spans << s;
    }
    f.spans = spans;
    QList<Block> blocks;
    for (Block b : std::as_const(f.blocks)) {
        b.start = qMax(0, b.start);
        b.end = qMin(textLength, b.end);
        if (b.start <= b.end && b.start <= textLength)
            blocks << b;
    }
    f.blocks = blocks;
}

void adjustForEdit(Formatting &f, int position, int removed, int added)
{
    QList<Span> spans;
    for (Span s : std::as_const(f.spans))
        if (TextUtils::adjustRangeForEdit(s.start, s.end, position, removed, added))
            spans << s;
    f.spans = spans;
    QList<Block> blocks;
    for (Block b : std::as_const(f.blocks)) {
        if (removed == 0 && position == b.start) { // typed at the start of a paragraph: part of it
            b.end += added;
            blocks << b;
            continue;
        }
        // The line break belongs to the paragraph while the range moves, so that empty paragraphs
        // and text typed at the end of a heading follow it.
        int end = b.end + 1;
        if (TextUtils::adjustRangeForEdit(b.start, end, position, removed, added)) {
            b.end = end - 1;
            blocks << b;
        }
    }
    f.blocks = blocks;
}

void Builder::add(const QString &s)
{
    if (s.isEmpty())
        return;
    const int start = int(text.size());
    text += s;
    if (!style)
        return;
    if (!m_f.spans.isEmpty() && m_f.spans.last().end == start && m_f.spans.last().style == style)
        m_f.spans.last().end = int(text.size());
    else
        m_f.spans.append({start, int(text.size()), style});
}

void Builder::endParagraph(BlockKind kind, int level)
{
    if (kind != BlockKind::Paragraph)
        m_f.blocks.append({m_lineStart, int(text.size()), kind, level});
    text += QLatin1Char('\n');
    m_lineStart = int(text.size());
}

namespace {

void forEachBlockOf(QTextDocument *doc, const Block &b, const std::function<void(const QTextBlock &)> &fn)
{
    QTextBlock block = doc->findBlock(b.start);
    const int end = qMax(b.end, b.start + 1);
    for (bool first = true; block.isValid() && (first || block.position() < end); block = block.next(), first = false)
        fn(block);
}

QTextCursor textOf(const QTextBlock &block)
{
    QTextCursor c(block);
    c.setPosition(block.position());
    c.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
    return c;
}

} // namespace

void apply(QTextDocument *doc, const Formatting &formatting)
{
    Formatting f = formatting;
    clamp(f, doc->characterCount() - 1);
    QTextCursor cursor(doc);
    cursor.beginEditBlock();
    for (const Span &s : std::as_const(f.spans)) {
        cursor.setPosition(s.start);
        cursor.setPosition(s.end, QTextCursor::KeepAnchor);
        QTextCharFormat cf;
        if (s.style & Bold)
            cf.setFontWeight(QFont::Bold);
        if (s.style & Italic)
            cf.setFontItalic(true);
        if (s.style & Underline)
            cf.setFontUnderline(true);
        if (s.style & Strike)
            cf.setFontStrikeOut(true);
        cursor.mergeCharFormat(cf);
    }

    std::sort(f.blocks.begin(), f.blocks.end(), [](const Block &a, const Block &b) { return a.start < b.start; });
    QTextList *list = nullptr;
    BlockKind listKind = BlockKind::Paragraph;
    int listLevel = 0, listEnd = -2;
    for (const Block &b : std::as_const(f.blocks)) {
        const bool isList = b.kind == BlockKind::Bullet || b.kind == BlockKind::Numbered;
        forEachBlockOf(doc, b, [&](const QTextBlock &block) {
            QTextCursor c(block);
            QTextBlockFormat bf;
            QTextCharFormat cf;
            switch (b.kind) {
            case BlockKind::Heading: {
                const int level = qBound(1, b.level, 6);
                bf.setHeadingLevel(level);
                bf.setTopMargin(level <= 2 ? 12 : 8);
                bf.setBottomMargin(4);
                cf.setFontWeight(QFont::Bold);
                cf.setProperty(QTextFormat::FontSizeAdjustment, qMax(0, 4 - level));
                break;
            }
            case BlockKind::TableCell:
                // Translucent grey bands: readable on light and dark themes.
                bf.setBackground(QColor(128, 128, 128, b.level >= 2 ? 90 : (b.level == 1 ? 45 : 18)));
                bf.setLeftMargin(10);
                bf.setRightMargin(10);
                bf.setBottomMargin(0);
                if (b.level >= 2)
                    cf.setFontWeight(QFont::Bold);
                break;
            default:
                break;
            }
            if (b.kind != BlockKind::Paragraph)
                c.mergeBlockFormat(bf);
            if (cf != QTextCharFormat())
                textOf(block).mergeCharFormat(cf);
            if (isList) {
                if (list && listKind == b.kind && listLevel == b.level && block.position() <= listEnd + 1) {
                    list->add(block);
                } else {
                    QTextListFormat lf;
                    lf.setStyle(b.kind == BlockKind::Bullet ? QTextListFormat::ListDisc : QTextListFormat::ListDecimal);
                    lf.setIndent(b.level + 1);
                    list = c.createList(lf);
                    listKind = b.kind;
                    listLevel = b.level;
                }
                listEnd = block.position() + block.length() - 1;
            }
        });
        if (!isList)
            list = nullptr;
    }
    cursor.endEditBlock();
}

Formatting fromDocument(const QTextDocument &doc, QString *text)
{
    Builder b;
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        BlockKind kind = BlockKind::Paragraph;
        int level = 0;
        if (block.blockFormat().headingLevel() > 0) {
            kind = BlockKind::Heading;
            level = block.blockFormat().headingLevel();
        } else if (QTextList *list = block.textList()) {
            const auto style = list->format().style();
            kind = (style == QTextListFormat::ListDecimal || style == QTextListFormat::ListLowerAlpha
                    || style == QTextListFormat::ListUpperAlpha || style == QTextListFormat::ListLowerRoman
                    || style == QTextListFormat::ListUpperRoman)
                ? BlockKind::Numbered
                : BlockKind::Bullet;
            level = qMax(0, list->format().indent() - 1);
        } else if (QTextTable *table = QTextCursor(block).currentTable()) {
            const int row = table->cellAt(QTextCursor(block)).row();
            kind = BlockKind::TableCell;
            level = row < table->format().headerRowCount() ? 2 : row % 2;
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            const QTextCharFormat cf = fragment.charFormat();
            int style = 0;
            // The size and weight of a heading come from its block kind, not from a bold span.
            if (cf.fontWeight() >= QFont::DemiBold && kind != BlockKind::Heading)
                style |= Bold;
            if (cf.fontItalic())
                style |= Italic;
            if (cf.fontUnderline() && !cf.isAnchor())
                style |= Underline;
            if (cf.fontStrikeOut())
                style |= Strike;
            b.style = style;
            QString t = fragment.text();
            t.replace(QChar::Nbsp, QLatin1Char(' ')); // as QTextDocument::toPlainText() does
            t.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
            b.add(t);
        }
        b.endParagraph(kind, level);
    }
    QString plain = b.text;
    plain.chop(1); // the last block has no line break
    Formatting f = b.formatting();
    if (plain != doc.toPlainText()) {
        text->clear();
        return {};
    }
    *text = plain;
    clamp(f, int(plain.size()));
    return f;
}

} // namespace RichText
