#include "core/Dictionaries.h"
#include "core/Exporters.h"
#include "core/Importers.h"
#include "core/Project.h"
#include "core/TextUtils.h"
#include "core/Xlsx.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <functional>

namespace Dictionaries {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Dictionaries", text);
}

const QString PathSeparator = QStringLiteral(" > ");

bool isWordChar(char32_t c, bool wildcard)
{
    if (wildcard && c == U'*')
        return true;
    if (QChar::isLetterOrNumber(c))
        return true;
    switch (QChar::category(c)) {
    case QChar::Mark_NonSpacing:
    case QChar::Mark_SpacingCombining:
    case QChar::Mark_Enclosing:
        return true;
    default:
        return false;
    }
}

// Words of `text`; with `wildcard`, '*' is part of a word (used for patterns).
QList<Token> words(const QString &text, bool wildcard)
{
    QList<Token> out;
    int start = -1;
    const int n = int(text.size());
    for (int i = 0; i < n;) {
        char32_t c = text.at(i).unicode();
        int width = 1;
        if (text.at(i).isHighSurrogate() && i + 1 < n && text.at(i + 1).isLowSurrogate()) {
            c = QChar::surrogateToUcs4(text.at(i), text.at(i + 1));
            width = 2;
        }
        if (isWordChar(c, wildcard)) {
            if (start < 0)
                start = i;
        } else if (start >= 0) {
            out.append({start, i});
            start = -1;
        }
        i += width;
    }
    if (start >= 0)
        out.append({start, n});
    return out;
}

// The form in which words are compared: composed accents, and case-folded unless case matters.
QString comparable(const QString &word, bool caseSensitive)
{
    const QString nfc = word.normalized(QString::NormalizationForm_C);
    return caseSensitive ? nfc : nfc.toCaseFolded();
}

// One word of a pattern: an exact word or a wildcard expression.
struct WordMatcher {
    QString exact;
    QRegularExpression re; // valid only for words with '*'
    bool wildcard = false;
};

struct CompiledPattern {
    QList<WordMatcher> words;
};

// Finds, for every word of a text, the patterns that start there and how many words they span.
class Matcher {
public:
    Matcher(const QStringList &patterns, bool caseSensitive)
        : m_caseSensitive(caseSensitive)
    {
        for (int p = 0; p < patterns.size(); ++p) {
            CompiledPattern compiled;
            const QString &pattern = patterns[p];
            for (const Token &t : words(pattern, true)) {
                WordMatcher w;
                const QString word = comparable(pattern.mid(t.start, t.end - t.start), caseSensitive);
                if (word.contains(QLatin1Char('*'))) {
                    QStringList parts = word.split(QLatin1Char('*'));
                    for (QString &part : parts)
                        part = QRegularExpression::escape(part);
                    w.wildcard = true;
                    w.re = QRegularExpression(QStringLiteral("\\A(?:") + parts.join(QStringLiteral(".*")) +
                                                  QStringLiteral(")\\z"),
                                              QRegularExpression::UseUnicodePropertiesOption);
                } else {
                    w.exact = word;
                }
                compiled.words << w;
            }
            m_patterns << compiled;
            if (compiled.words.isEmpty())
                continue; // only punctuation: never matches
            if (compiled.words.first().wildcard)
                m_wildcardFirst << p;
            else
                m_exactFirst[compiled.words.first().exact] << p;
        }
    }

    // For each word: (pattern, number of words) of the patterns that match starting there.
    QList<QList<std::pair<int, int>>> candidates(const QString &text, const QList<Token> &tokens)
    {
        QStringList forms;
        forms.reserve(tokens.size());
        for (const Token &t : tokens)
            forms << comparable(text.mid(t.start, t.end - t.start), m_caseSensitive);

        QList<QList<std::pair<int, int>>> out(tokens.size());
        for (int i = 0; i < forms.size(); ++i) {
            QList<int> starting = m_exactFirst.value(forms[i]);
            starting += wildcardStarts(forms[i]);
            for (int p : starting) {
                const QList<WordMatcher> &ws = m_patterns[p].words;
                if (i + ws.size() > forms.size())
                    continue;
                bool ok = true;
                for (int k = 1; ok && k < ws.size(); ++k)
                    ok = matches(ws[k], forms[i + k]);
                if (ok)
                    out[i].append({p, int(ws.size())});
            }
        }
        return out;
    }

private:
    bool matches(const WordMatcher &w, const QString &form) const
    {
        return w.wildcard ? w.re.match(form).hasMatch() : w.exact == form;
    }

    // Patterns whose first word is a wildcard matching `form`; cached, since words repeat a lot.
    QList<int> wildcardStarts(const QString &form)
    {
        if (m_wildcardFirst.isEmpty())
            return {};
        auto it = m_wildcardCache.constFind(form);
        if (it != m_wildcardCache.constEnd())
            return *it;
        QList<int> out;
        for (int p : m_wildcardFirst)
            if (matches(m_patterns[p].words.first(), form))
                out << p;
        m_wildcardCache.insert(form, out);
        return out;
    }

    bool m_caseSensitive;
    QList<CompiledPattern> m_patterns;
    QHash<QString, QList<int>> m_exactFirst;
    QList<int> m_wildcardFirst;
    QHash<QString, QList<int>> m_wildcardCache;
};

// Non-overlapping, longest-first selection among the candidates of the patterns accepted by `accept`.
template <typename Accept>
QList<Match> select(const QList<Token> &tokens, const QList<QList<std::pair<int, int>>> &candidates, Accept accept)
{
    QList<Match> out;
    for (int i = 0; i < candidates.size();) {
        int bestPattern = -1;
        int bestLength = 0;
        for (const auto &[pattern, length] : candidates[i]) {
            if (length > bestLength && accept(pattern)) {
                bestPattern = pattern;
                bestLength = length;
            }
        }
        if (bestPattern < 0) {
            ++i;
            continue;
        }
        out.append({tokens[i].start, tokens[i + bestLength - 1].end, bestPattern});
        i += bestLength;
    }
    return out;
}

} // namespace

QList<Token> tokenize(const QString &text)
{
    return words(text, false);
}

QList<Match> findMatches(const QString &text, const QStringList &patterns, bool caseSensitive)
{
    Matcher matcher(patterns, caseSensitive);
    const QList<Token> tokens = tokenize(text);
    return select(tokens, matcher.candidates(text, tokens), [](int) { return true; });
}

// ---------------------------------------------------------------- analysis

int Analysis::total(qint64 category) const
{
    int sum = 0;
    for (int n : counts.value(category))
        sum += n;
    return sum;
}

int Analysis::totalWords() const
{
    int sum = 0;
    for (qint64 d : documents)
        sum += words.value(d);
    return sum;
}

double Analysis::percent(qint64 category, qint64 document) const
{
    const int n = words.value(document);
    return n > 0 ? 100.0 * count(category, document) / n : 0.0;
}

int Analysis::depth(qint64 category) const
{
    QHash<qint64, qint64> parent;
    for (const DictCategory &c : categories)
        parent.insert(c.id, c.parentId);
    int depth = 0;
    for (qint64 p = parent.value(category); p > 0 && depth < categories.size(); p = parent.value(p))
        ++depth;
    return depth;
}

Analysis analyze(const Project &project, qint64 dictionaryId, const QList<qint64> &documentIds, bool caseSensitive)
{
    Analysis a;
    a.dictionaryId = dictionaryId;
    a.categories = project.dictCategories(dictionaryId);
    for (const Document &d : project.documents())
        if (documentIds.isEmpty() || documentIds.contains(d.id))
            a.documents << d.id;

    // Entries of every category, then the categories whose subtree contains each one.
    QStringList patterns;
    QList<qint64> patternCategory;
    for (const DictEntry &e : project.dictEntriesOfDictionary(dictionaryId)) {
        patterns << e.pattern;
        patternCategory << e.categoryId;
    }
    QHash<qint64, qint64> parent;
    for (const DictCategory &c : a.categories)
        parent.insert(c.id, c.parentId);
    // category -> patterns of the category and of its subcategories
    QHash<qint64, QSet<int>> subtreePatterns;
    for (int p = 0; p < patterns.size(); ++p) {
        int guard = 0;
        for (qint64 c = patternCategory[p]; c > 0 && guard++ <= a.categories.size(); c = parent.value(c))
            subtreePatterns[c].insert(p);
    }

    Matcher matcher(patterns, caseSensitive);
    for (qint64 doc : a.documents) {
        const QString text = project.documentText(doc);
        const QList<Token> tokens = tokenize(text);
        a.words.insert(doc, int(tokens.size()));
        const auto candidates = matcher.candidates(text, tokens);
        for (const DictCategory &c : a.categories) {
            const QSet<int> accepted = subtreePatterns.value(c.id);
            if (accepted.isEmpty())
                continue;
            const QList<Match> matches = select(tokens, candidates, [&](int p) { return accepted.contains(p); });
            if (matches.isEmpty())
                continue;
            a.counts[c.id].insert(doc, int(matches.size()));
            QList<Hit> &hits = a.hits[c.id];
            for (const Match &m : matches)
                hits.append({doc, m.start, m.end});
        }
    }
    return a;
}

// ---------------------------------------------------------------- coding and variables

int autocode(Project &project, const Analysis &analysis, const QList<qint64> &categories,
             const QHash<qint64, qint64> &codes, Extent extent, QHash<qint64, qint64> *usedCodes)
{
    QHash<qint64, DictCategory> byId;
    for (const DictCategory &c : analysis.categories)
        byId.insert(c.id, c);
    const QString dictionaryName = project.dictionary(analysis.dictionaryId).name;

    QHash<qint64, qint64> resolved; // category -> code
    qint64 rootCode = 0;
    int colorIndex = int(project.codes().size());
    auto findOrAdd = [&](qint64 parentCode, const QString &name) {
        qint64 id = project.findCode(parentCode, name);
        if (!id)
            id = project.addCode(parentCode, name, TextUtils::paletteColor(colorIndex++));
        return id;
    };
    // Depth-first through the parents; the guard stops at a (corrupt) cycle.
    std::function<qint64(qint64, int)> codeFor = [&](qint64 category, int guard) -> qint64 {
        if (resolved.contains(category))
            return resolved.value(category);
        qint64 code = codes.value(category);
        if (!code && byId.contains(category) && guard < analysis.categories.size() + 1) {
            const DictCategory c = byId.value(category);
            qint64 parentCode = 0;
            if (c.parentId > 0) {
                parentCode = codeFor(c.parentId, guard + 1);
            } else {
                if (!rootCode)
                    rootCode = findOrAdd(0, dictionaryName.isEmpty() ? tr("Dizionario") : dictionaryName);
                parentCode = rootCode;
            }
            code = parentCode ? findOrAdd(parentCode, c.name) : 0;
        }
        resolved.insert(category, code);
        return code;
    };

    QHash<qint64, QString> texts;
    const qsizetype before = project.allSegments().size(); // addSegment merges duplicates
    project.setUndoLabel(tr("Codifica automatica con il dizionario"));
    project.beginBatch();
    for (qint64 category : categories) {
        const QList<Hit> hits = analysis.hits.value(category);
        if (hits.isEmpty())
            continue;
        const qint64 code = codeFor(category, 0);
        if (!code)
            continue;
        if (usedCodes)
            usedCodes->insert(category, code);
        for (const Hit &h : hits) {
            if (!texts.contains(h.documentId))
                texts.insert(h.documentId, project.documentText(h.documentId));
            const QString &text = texts[h.documentId];
            if (h.end > text.size())
                continue;
            auto range = std::pair<int, int>{h.start, h.end};
            if (extent == Extent::Sentence)
                range = TextUtils::sentenceBounds(text, h.start, h.end);
            else if (extent == Extent::Paragraph)
                range = TextUtils::paragraphBounds(text, h.start, h.end);
            project.addSegment(h.documentId, code, range.first, range.second);
        }
    }
    project.commitBatch();
    return int(project.allSegments().size() - before);
}

QList<qint64> countsToVariables(Project &project, const Analysis &analysis, const QList<qint64> &categories)
{
    QList<qint64> created;
    if (categories.isEmpty())
        return created;
    QSet<QString> names;
    for (const Variable &v : project.variables())
        names.insert(v.name.toLower());
    const QString dictionaryName = project.dictionary(analysis.dictionaryId).name;

    project.setUndoLabel(tr("Salva i conteggi come variabili"));
    project.beginBatch();
    for (qint64 category : categories) {
        // One arg() with both values: a '%1' typed in a name must stay literal.
        const QString base = QStringLiteral("%1: %2").arg(dictionaryName, project.dictCategoryPath(category));
        QString name = base;
        for (int n = 2; names.contains(name.toLower()); ++n)
            name = QStringLiteral("%1 (%2)").arg(base, QString::number(n));
        names.insert(name.toLower());

        const qint64 id = project.addVariable(name, VariableType::Integer);
        if (!id)
            continue;
        created << id;
        for (qint64 doc : analysis.documents)
            project.setVariableValue(doc, id, QString::number(analysis.count(category, doc)));
    }
    project.commitBatch();
    return created;
}

// ---------------------------------------------------------------- import / export

QList<QStringList> toTable(const Project &project, qint64 dictionaryId)
{
    QList<QStringList> rows{{tr("Categoria"), tr("Voce")}};
    QHash<qint64, QList<DictEntry>> entries;
    for (const DictEntry &e : project.dictEntriesOfDictionary(dictionaryId))
        entries[e.categoryId] << e;
    QHash<qint64, QString> paths;
    QSet<qint64> parents;
    const QList<DictCategory> categories = project.dictCategories(dictionaryId);
    for (const DictCategory &c : categories) {
        paths.insert(c.id, c.parentId > 0 ? paths.value(c.parentId) + PathSeparator + c.name : c.name);
        parents.insert(c.parentId);
    }
    for (const DictCategory &c : categories) {
        const QList<DictEntry> list = entries.value(c.id);
        // A category without entries is kept only if nothing below it carries its path.
        if (list.isEmpty() && !parents.contains(c.id))
            rows.append({paths.value(c.id), QString()});
        for (const DictEntry &e : list)
            rows.append({paths.value(c.id), e.pattern});
    }
    return rows;
}

int importTable(Project &project, qint64 dictionaryId, const QList<QStringList> &rows)
{
    int added = 0;
    QHash<QString, qint64> known; // path -> category
    project.setUndoLabel(tr("Importa dizionario"));
    project.beginBatch();
    for (int r = 0; r < rows.size(); ++r) {
        const QString path = rows[r].value(0).trimmed();
        const QString entry = rows[r].value(1).simplified();
        if (r == 0) {
            const QString first = path.toLower();
            if (first == QLatin1String("categoria") || first == QLatin1String("category"))
                continue;
        }
        if (path.isEmpty())
            continue;
        qint64 category = known.value(path);
        if (!category) {
            qint64 parent = 0;
            for (QString name : path.split(QLatin1Char('>'))) {
                name = name.trimmed();
                if (name.isEmpty())
                    continue;
                qint64 id = project.findDictCategory(dictionaryId, parent, name);
                if (!id)
                    id = project.addDictCategory(dictionaryId, parent, name);
                parent = id;
            }
            category = parent;
            known.insert(path, category);
        }
        if (!category || entry.isEmpty())
            continue;
        const int before = int(project.dictEntries(category).size());
        if (project.addDictEntry(category, entry) && project.dictEntries(category).size() > before)
            ++added;
    }
    project.commitBatch();
    return added;
}

int importWords(Project &project, qint64 categoryId, const QString &text)
{
    int added = 0;
    project.setUndoLabel(tr("Importa voci"));
    project.beginBatch();
    QStringList existing;
    for (const DictEntry &e : project.dictEntries(categoryId))
        existing << e.pattern.toLower();
    for (const QString &line : TextUtils::normalizeText(text).split(QLatin1Char('\n'))) {
        const QString entry = line.simplified();
        if (entry.isEmpty() || entry.startsWith(QLatin1Char('#')) || existing.contains(entry.toLower()))
            continue;
        if (project.addDictEntry(categoryId, entry)) {
            existing << entry.toLower();
            ++added;
        }
    }
    project.commitBatch();
    return added;
}

int importFile(Project &project, qint64 dictionaryId, qint64 categoryId, const QString &path, QString *error)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("xlsx") || suffix == QLatin1String("csv")) {
        QString err;
        const QList<QStringList> rows = suffix == QLatin1String("xlsx") ? Xlsx::readFirstSheet(path, &err)
                                                                        : Exporters::readCsv(path, &err);
        if (!err.isEmpty()) {
            *error = err;
            return -1;
        }
        return importTable(project, dictionaryId, rows);
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = f.errorString();
        return -1;
    }
    const QString text = Importers::decodeText(f.readAll());
    project.setUndoLabel(tr("Importa voci"));
    project.beginBatch();
    if (!categoryId) {
        const QString name = QFileInfo(path).completeBaseName();
        categoryId = project.findDictCategory(dictionaryId, 0, name);
        if (!categoryId)
            categoryId = project.addDictCategory(dictionaryId, 0, name);
    }
    const int added = categoryId ? importWords(project, categoryId, text) : 0;
    project.commitBatch();
    return added;
}

bool exportFile(const Project &project, qint64 dictionaryId, const QString &path, QString *error)
{
    const QList<QStringList> rows = toTable(project, dictionaryId);
    if (QFileInfo(path).suffix().compare(QLatin1String("xlsx"), Qt::CaseInsensitive) == 0) {
        Xlsx::Sheet sheet;
        sheet.name = project.dictionary(dictionaryId).name;
        if (sheet.name.trimmed().isEmpty())
            sheet.name = tr("Dizionario");
        sheet.rows = rows;
        sheet.textColumns = {0, 1};
        return Xlsx::writeWorkbook(path, {sheet}, error);
    }
    return Exporters::writeCsv(path, rows, error);
}

} // namespace Dictionaries
