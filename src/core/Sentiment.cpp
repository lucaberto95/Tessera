#include "core/Sentiment.h"
#include "core/Dictionaries.h"
#include "core/Project.h"

#include <QColor>
#include <QCoreApplication>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>

namespace Sentiment {

namespace {

// Lexicon written for Tessera. Words are lower case without accents; a trailing '*' is a stem
// ("buon*" = buono, buona, buoni, ...), at least three letters long.
struct Lexicon {
    QHash<QString, int> exact;
    QHash<QString, int> stems;
    QSet<QString> negators;
    QHash<QString, double> boosters; // intensifiers (> 1) and diminishers (< 1)
};

void add(Lexicon &lex, const char *words, int value)
{
    for (const QString &w : QString::fromLatin1(words).split(' ', Qt::SkipEmptyParts)) {
        if (w.endsWith('*'))
            lex.stems.insert(w.chopped(1), value);
        else
            lex.exact.insert(w, value);
    }
}

void addSet(QSet<QString> &set, const char *words)
{
    for (const QString &w : QString::fromLatin1(words).split(' ', Qt::SkipEmptyParts))
        set.insert(w);
}

void addBoost(Lexicon &lex, const char *words, double factor)
{
    for (const QString &w : QString::fromLatin1(words).split(' ', Qt::SkipEmptyParts))
        lex.boosters.insert(w, factor);
}

Lexicon buildItalian()
{
    Lexicon l;
    add(l, "ottim* eccellent* fantastic* meravigli* perfett* splendid* straordinari* magnific* adoro amore "
           "entusias* bellissim* felicissim* stupend*", 2);
    add(l, "buon* bene benessere piace piacciono piaciuto piaciuta piacevol* gradevol* felic* content* soddisf* "
           "gentil* cortes* simpatic* affettuos* facile facili semplice semplici positiv* fiducia fiducios* sicuro "
           "sicura sicuri sicure sicurezza amo apprezz* grazie ringrazi* speran* spero miglior* successo successi "
           "vantagg* aiut* sostegno orgogli* seren* tranquill* comod* efficac* efficien* interessant* stimolant* "
           "motivat* divert* gioia gioios* sorris* fortun* opportunit* caloros* accogli* bello bella belli belle "
           "bellezza bravo brava bravi brave utile utili importante importanti", 1);
    add(l, "male cattiv* brutt* peggio peggior* trist* infelic* problem* difficil* difficolt* stress* ansi* paura "
           "paure pauros* preoccup* rabbia arrabbi* fastidi* noios* inutil* impossibil* fatic* frustr* delus* "
           "insoddisf* disagi* insicur* incertezz* confus* caos caotic* sbagli* error* colpa litig* conflitt* "
           "solitudin* vergogn* criticit* carenz* mancanz* manca insufficien* scars* lament* rifiut* ostacol* "
           "rischi* pericol* dolor* soffr* sofferen* perdita falliment* fallit* ingiust* discrimin* violen*", -1);
    add(l, "pessim* orribil* terribil* disastr* schifo odio disgust* tragic* devastant* insopportabil*", -2);
    addSet(l.negators, "non mai senza nessun nessuna nessuno niente nulla neanche nemmeno no");
    addBoost(l, "molto tanto troppo davvero veramente estremamente assolutamente particolarmente incredibilmente "
                "totalmente super", 1.5);
    addBoost(l, "abbastanza piuttosto leggermente relativamente", 0.5);
    return l;
}

Lexicon buildEnglish()
{
    Lexicon l;
    add(l, "excellent amazing wonderful fantastic awesome perfect love loved loves lovely brilliant superb "
           "outstanding delighted thrilled", 2);
    add(l, "good great nice enjoy* happy happier happiest glad pleased pleasant satisf* helpful useful easy easier "
           "simple clear positive trust safe comfortable beautiful best better improve* improvement success* "
           "benefit* advantage* supportive thank* grateful appreciate* hope* excited exciting proud calm effective "
           "efficient interesting engaging motivated friendly kind reliable fun joy* welcom* valuable fair "
           "fortunate", 1);
    add(l, "bad worse poor sad unhappy angry anger problem* difficult* stress* stressful anxious anxiety fear* "
           "afraid worr* frustrat* disappoint* useless impossible slow tired exhausting pain* boring confus* "
           "chaos mistake* wrong fail* lack* unfair unsafe risk* danger* conflict* lonely loneliness dissatisf* "
           "complain* blame* reject* obstacle* barrier* burden ugly rude hostile toxic shame guilt* suffer* loss "
           "lose loses", -1);
    add(l, "worst terrible awful horrible hate hated hates hatred disaster* disgusting tragic devastating "
           "unbearable", -2);
    addSet(l.negators, "not no never without neither nor nobody nothing cannot cant don doesn didn isn wasn aren "
                       "weren won wouldn couldn shouldn haven hasn hadn ain");
    addBoost(l, "very really extremely absolutely incredibly highly truly particularly totally", 1.5);
    addBoost(l, "slightly somewhat fairly rather", 0.5);
    return l;
}

const Lexicon &lexicon(Language lang)
{
    static const Lexicon it = buildItalian();
    static const Lexicon en = buildEnglish();
    return lang == Language::English ? en : it;
}

QList<const Lexicon *> lexicons(Language lang)
{
    if (lang == Language::Italian)
        return {&lexicon(Language::Italian)};
    if (lang == Language::English)
        return {&lexicon(Language::English)};
    return {&lexicon(Language::Italian), &lexicon(Language::English)};
}

// Lower case, no diacritics, so the lexicon can be plain ASCII.
QString fold(const QString &word)
{
    QString out;
    for (const QChar c : word.normalized(QString::NormalizationForm_D).toCaseFolded())
        if (c.category() != QChar::Mark_NonSpacing)
            out.append(c);
    return out;
}

int lookup(const QList<const Lexicon *> &lexs, const QString &w)
{
    for (const Lexicon *l : lexs) {
        const auto it = l->exact.constFind(w);
        if (it != l->exact.constEnd())
            return it.value();
    }
    for (const Lexicon *l : lexs)
        for (int len = std::min<int>(w.size(), 14); len >= 3; --len) {
            const auto it = l->stems.constFind(w.left(len));
            if (it != l->stems.constEnd())
                return it.value();
        }
    return 0;
}

bool isBreak(QChar c)
{
    return c == '.' || c == '!' || c == '?' || c == ';' || c == ':' || c == ',' || c == '\n' || c == '\r' ||
           c == QChar(0x2026);
}

} // namespace

Polarity polarityOf(double value)
{
    if (value >= kNeutralThreshold)
        return Polarity::Positive;
    if (value <= -kNeutralThreshold)
        return Polarity::Negative;
    return Polarity::Neutral;
}

QString polarityName(Polarity polarity)
{
    switch (polarity) {
    case Polarity::Positive:
        return QCoreApplication::translate("Sentiment", "Positivo");
    case Polarity::Negative:
        return QCoreApplication::translate("Sentiment", "Negativo");
    default:
        return QCoreApplication::translate("Sentiment", "Neutro");
    }
}

int wordPolarity(const QString &word, Language language)
{
    return lookup(lexicons(language), fold(word));
}

int lexiconSize(Language language)
{
    int n = 0;
    for (const Lexicon *l : lexicons(language))
        n += int(l->exact.size() + l->stems.size());
    return n;
}

Score score(const QString &text, Language language)
{
    const QList<const Lexicon *> lexs = lexicons(language);
    const QList<Dictionaries::Token> tokens = Dictionaries::tokenize(text);
    QStringList words;
    QList<int> polarity;
    for (const Dictionaries::Token &t : tokens) {
        words << fold(text.mid(t.start, t.end - t.start));
        polarity << lookup(lexs, words.last());
    }

    Score out;
    out.words = int(tokens.size());
    double sum = 0;
    for (int i = 0; i < words.size(); ++i) {
        if (polarity[i] == 0)
            continue;
        ++out.hits;
        double v = polarity[i];
        double factor = 1;
        bool negated = false;
        // Up to three words before, within the same clause and not past another word of the lexicon.
        for (int j = i - 1; j >= 0 && j >= i - 3; --j) {
            bool broken = false;
            for (int k = tokens[j].end; k < tokens[j + 1].start && !broken; ++k)
                broken = isBreak(text.at(k));
            if (broken || polarity[j] != 0)
                break;
            bool isNeg = false, isBoost = false;
            double boost = 1;
            for (const Lexicon *l : lexs) {
                isNeg = isNeg || l->negators.contains(words[j]);
                const auto b = l->boosters.constFind(words[j]);
                if (b != l->boosters.constEnd()) {
                    isBoost = true;
                    boost = b.value();
                }
            }
            if (isNeg)
                negated = true;
            else if (isBoost)
                factor *= boost;
        }
        v *= factor;
        if (negated)
            v *= -0.75;
        sum += v;
    }
    out.value = sum / std::sqrt(sum * sum + 15.0);
    out.polarity = out.hits > 0 ? polarityOf(out.value) : Polarity::Neutral;
    return out;
}

QList<Words::Source> segmentPassages(const QList<Segment> &segments)
{
    QList<Words::Source> out;
    QSet<QString> seen;
    for (const Segment &s : segments) {
        if (s.kind != SegmentKind::Text || s.end <= s.start)
            continue;
        const QString key = QStringLiteral("%1:%2:%3").arg(s.documentId).arg(s.start).arg(s.end);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        out.append({s.documentId, s.start, s.end});
    }
    return out;
}

QList<Words::Source> sentencePassages(const QList<Words::Source> &sources, const QHash<qint64, QString> &texts)
{
    QList<Words::Source> out;
    for (const Words::Source &src : sources) {
        const QString text = texts.value(src.documentId);
        const int end = src.end < 0 ? int(text.size()) : std::min<int>(src.end, int(text.size()));
        int start = std::max(0, src.start);
        auto addSentence = [&](int from, int to) {
            while (from < to && text.at(from).isSpace())
                ++from;
            while (to > from && text.at(to - 1).isSpace())
                --to;
            for (int i = from; i < to; ++i)
                if (text.at(i).isLetterOrNumber()) {
                    out.append({src.documentId, from, to});
                    return;
                }
        };
        int from = start;
        for (int i = start; i < end; ++i) {
            const QChar c = text.at(i);
            bool cut = false;
            if (c == '\n' || c == '\r') {
                cut = true;
            } else if (c == '.' || c == '!' || c == '?' || c == QChar(0x2026)) {
                int j = i + 1;
                while (j < end && (text.at(j) == '.' || text.at(j) == '!' || text.at(j) == '?'))
                    ++j;
                while (j < end && (text.at(j) == '"' || text.at(j) == ')' || text.at(j) == QChar(0x201D) ||
                                   text.at(j) == QChar(0xBB)))
                    ++j;
                if (j >= end || text.at(j).isSpace()) {
                    i = j - 1;
                    cut = true;
                }
            }
            if (cut) {
                addSentence(from, i + 1);
                from = i + 1;
            }
        }
        addSentence(from, end);
    }
    return out;
}

QList<Item> analyze(const QList<Words::Source> &passages, const QHash<qint64, QString> &texts, Language language)
{
    QList<Item> items;
    for (const Words::Source &p : passages) {
        if (!texts.contains(p.documentId))
            continue;
        const QString &all = texts[p.documentId];
        const int end = p.end < 0 ? int(all.size()) : std::min<int>(p.end, int(all.size()));
        const int start = std::max(0, p.start);
        if (end <= start)
            continue;
        Item item;
        item.documentId = p.documentId;
        item.start = start;
        item.end = end;
        item.text = all.mid(start, end - start);
        item.score = score(item.text, language);
        items.append(item);
    }
    return items;
}

QList<Item> analyze(const Project &project, Unit unit, const QList<Segment> &segments, const QList<qint64> &documentIds,
                    Language language)
{
    QList<Words::Source> passages;
    if (unit == Unit::Segments) {
        passages = segmentPassages(segments);
    } else {
        QList<qint64> ids = documentIds;
        if (ids.isEmpty())
            for (const Document &d : project.documents())
                ids << d.id;
        passages = Words::documentSources(ids);
    }
    // Text documents only: images, pdf and media have no text positions to code.
    QHash<qint64, bool> isText;
    QList<Words::Source> kept;
    for (const Words::Source &s : passages) {
        if (!isText.contains(s.documentId))
            isText.insert(s.documentId, project.document(s.documentId).kind == DocumentKind::Text);
        if (isText[s.documentId])
            kept.append(s);
    }
    const QHash<qint64, QString> texts = Words::loadTexts(project, kept);
    if (unit == Unit::Sentences)
        kept = sentencePassages(kept, texts);
    return analyze(kept, texts, language);
}

int codeByPolarity(Project &project, const QList<Item> &items, const QString &parentName)
{
    if (items.isEmpty() || parentName.trimmed().isEmpty())
        return 0;
    const qsizetype before = project.allSegments().size();
    project.setUndoLabel(QCoreApplication::translate("Sentiment", "Codifica per polarità del sentiment"));
    project.beginBatch();
    qint64 parent = project.findCode(0, parentName.trimmed());
    if (!parent)
        parent = project.addCode(0, parentName.trimmed(), QColor(0x5b, 0x7d, 0xb1));
    QHash<int, qint64> codes;
    auto codeFor = [&](Polarity p) {
        if (!codes.contains(int(p))) {
            const QString name = polarityName(p);
            qint64 id = project.findCode(parent, name);
            if (!id) {
                const QColor color = p == Polarity::Positive   ? QColor(0x2e, 0x9e, 0x5b)
                                     : p == Polarity::Negative ? QColor(0xcc, 0x3b, 0x3b)
                                                               : QColor(0x8a, 0x8a, 0x8a);
                id = project.addCode(parent, name, color);
            }
            codes.insert(int(p), id);
        }
        return codes[int(p)];
    };
    for (const Item &it : items) {
        const qint64 code = codeFor(it.score.polarity);
        if (code)
            project.addSegment(it.documentId, code, it.start, it.end);
    }
    project.commitBatch();
    return int(project.allSegments().size() - before);
}

} // namespace Sentiment
