#include "core/PointLegend.h"

#include "core/Geometry.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace cartesio {

namespace {

std::string trim(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

bool startsWith(std::string_view s, std::size_t pos, std::string_view what)
{
    return s.substr(pos, what.size()) == what;
}

// Virgolette di ogni tipo (anche tipografiche, in UTF-8) e parentesi intorno alla lettera.
std::size_t quoteAt(std::string_view s, std::size_t pos)
{
    for (std::string_view q : {"\"", "'", "(", ")", "\xC2\xAB", "\xC2\xBB", "\xE2\x80\x9C", "\xE2\x80\x9D",
                               "\xE2\x80\x98", "\xE2\x80\x99"})
        if (startsWith(s, pos, q))
            return q.size();
    return 0;
}

std::size_t separatorAt(std::string_view s, std::size_t pos)
{
    for (std::string_view q : {":", "=", "-", "\xE2\x80\x93", "\xE2\x80\x94"})
        if (startsWith(s, pos, q))
            return q.size();
    return 0;
}

void skipSpaces(std::string_view s, std::size_t& pos)
{
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t'))
        ++pos;
}

// Minuscole ASCII senza accenti (UTF-8: à è é ì ò ù e maiuscole); il resto diventa spazio.
std::string fold(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += std::isalnum(c) ? static_cast<char>(std::tolower(c)) : ' ';
            continue;
        }
        if (c == 0xC3 && i + 1 < s.size()) {
            const auto b = static_cast<unsigned char>(s[i + 1]) | 0x20;   // maiuscole -> minuscole
            ++i;
            if (b >= 0xA0 && b <= 0xA5)
                out += 'a';
            else if (b >= 0xA8 && b <= 0xAB)
                out += 'e';
            else if (b >= 0xAC && b <= 0xAF)
                out += 'i';
            else if (b >= 0xB2 && b <= 0xB6)
                out += 'o';
            else if (b >= 0xB9 && b <= 0xBC)
                out += 'u';
            else
                out += ' ';
            continue;
        }
        out += ' ';
    }
    return out;
}

bool isStopWord(const std::string& w)
{
    static const std::set<std::string> stop = {
        "alimentazione", "alimentazioni", "per", "con", "da", "dal", "dalla", "dallo", "di", "del", "della",
        "dello", "dei", "degli", "delle", "in", "al", "alla", "allo", "ai", "agli", "alle", "ed", "il", "la",
        "lo", "le", "gli", "un", "una", "uno", "loco", "sezionatore", "sezionatori", "tipo", "utenza", "utenze",
        "monofase", "trifase", "lato", "sx", "dx", "nr", "punto", "punti", "linea", "circuito", "the",
        // luoghi, non carichi: per scegliere fra circuiti uguali conta la posizione
        "nord", "sud", "est", "ovest", "sinistra", "sinistro", "destra", "destro"};
    return w.size() < 2 || stop.count(w) > 0;
}

std::string stem(std::string w)
{
    if (w.size() > 5) {
        const std::string_view end(w.data() + w.size() - 3, 3);
        if (end == "che" || end == "chi" || end == "ghe" || end == "ghi")
            w.resize(w.size() - 2);
    }
    while (w.size() > 3 && std::string_view("aeiou").find(w.back()) != std::string_view::npos)
        w.pop_back();
    return w;
}

// Sinonimi e sigle comuni negli impianti (sulle radici). Il primo nome è quello usato nel confronto.
const std::vector<std::string>* synonyms(const std::string& word)
{
    static const std::map<std::string, std::vector<std::string>> table = {
        {"ue", {"unit", "estern"}},
        {"ui", {"unit", "intern"}},
        {"motocondensant", {"unit", "estern"}},
        {"pdc", {"pomp", "calor"}},
        {"lam", {"barrier"}},
        {"cortin", {"barrier"}},
        {"bollitor", {"boiler"}},
        {"scaldacqu", {"boiler"}},
        {"scaldabagn", {"boiler"}},
        {"climatizzator", {"condizionator"}},
        {"split", {"condizionator"}},
        {"aspirator", {"estrattor"}},
        {"elevator", {"ascensor"}},
        {"montacarich", {"ascensor"}},
        {"fotocellul", {"elettrovalvol"}},
        {"rubinett", {"sanitar"}},
        {"lavab", {"sanitar"}},
        {"tagliafuoc", {"serrand"}},
    };
    const auto it = table.find(word);
    return it == table.end() ? nullptr : &it->second;
}

std::vector<std::string> words(std::string_view text)
{
    std::vector<std::string> out;
    const std::string f = fold(text);
    std::size_t i = 0;
    while (i < f.size()) {
        while (i < f.size() && f[i] == ' ')
            ++i;
        std::size_t j = i;
        while (j < f.size() && f[j] != ' ')
            ++j;
        if (j > i)
            out.push_back(f.substr(i, j - i));
        i = j;
    }
    return out;
}

void addTokens(const std::string& word, std::set<std::string>& out)
{
    if (isStopWord(word) || std::all_of(word.begin(), word.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return;
    const std::vector<std::string>* syn = synonyms(word);
    const std::string s = stem(word);
    if (!syn)
        syn = synonyms(s);
    if (syn)
        out.insert(syn->begin(), syn->end());
    else
        out.insert(s);
}

// Le parole originali di `description` (nell'ordine) le cui radici rispettano `keep`.
template <typename Keep>
std::string pickWords(const std::string& description, Keep keep)
{
    std::string out;
    std::size_t i = 0;
    while (i < description.size()) {
        while (i < description.size() && description[i] == ' ')
            ++i;
        std::size_t j = description.find(' ', i);
        if (j == std::string::npos)
            j = description.size();
        const std::string word = description.substr(i, j - i);
        const std::set<std::string> tokens = legendTokens(word);
        if (!word.empty() && !tokens.empty() && std::all_of(tokens.begin(), tokens.end(), keep)) {
            if (!out.empty())
                out += ' ';
            out += word;
        }
        i = j;
    }
    return out;
}

std::vector<std::string> entityLines(const Entity& e)
{
    if (e.type() == EntityType::Text)
        return {static_cast<const TextEntity&>(e).displayText()};
    if (e.type() == EntityType::MText)
        return static_cast<const MTextEntity&>(e).lines();
    return {};
}

double entityHeight(const Entity& e)
{
    if (e.type() == EntityType::Text)
        return static_cast<const TextEntity&>(e).height;
    if (e.type() == EntityType::MText)
        return static_cast<const MTextEntity&>(e).height;
    return 0.0;
}

bool unusedCircuit(const Circuit& c)
{
    const std::set<std::string> t = legendTokens(c.description);
    return c.number <= 0 || t.count("riserv") > 0 || c.description.empty();
}

} // namespace

std::optional<LegendEntry> parseLegendLine(std::string_view line, bool strict)
{
    const std::string s = trim(line);
    if (s.empty() || normalizeCircuitCode(s))
        return std::nullopt;
    std::string lower = s;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::size_t pos = 0;
    bool prefixed = false;
    for (std::string_view prefix : {"tipologia", "tipo", "lettera", "rif."})
        if (startsWith(lower, 0, prefix) && lower.size() > prefix.size()
            && !std::isalpha(static_cast<unsigned char>(lower[prefix.size()]))) {
            pos = prefix.size();
            prefixed = true;
            break;
        }
    skipSpaces(s, pos);
    const std::size_t open = quoteAt(s, pos);
    pos += open;
    skipSpaces(s, pos);
    const std::size_t letterStart = pos;
    if (pos >= s.size() || s[pos] < 'A' || s[pos] > 'Z')
        return std::nullopt;
    ++pos;
    while (pos < s.size() && pos - letterStart < 3
           && ((s[pos] >= 'A' && s[pos] <= 'Z') || std::isdigit(static_cast<unsigned char>(s[pos]))))
        ++pos;
    if (pos < s.size() && std::isalnum(static_cast<unsigned char>(s[pos])))
        return std::nullopt;   // parola più lunga, non una lettera
    const std::string letter = s.substr(letterStart, pos - letterStart);
    const std::size_t afterLetter = pos;
    skipSpaces(s, pos);
    if (open) {
        const std::size_t close = quoteAt(s, pos);
        if (!close)
            return std::nullopt;
        pos += close;
    }
    skipSpaces(s, pos);
    const std::size_t sep = separatorAt(s, pos);
    const bool spacedSeparator = sep && (pos > afterLetter || s[pos] == ':' || s[pos] == '=');
    pos += sep;
    const bool quoted = open > 0;
    if (!prefixed && !quoted && !spacedSeparator)
        return std::nullopt;
    if (strict && !prefixed && !quoted)
        return std::nullopt;
    std::string description = trim(std::string_view(s).substr(pos));
    while (!description.empty() && std::string_view(";.,:").find(description.back()) != std::string_view::npos)
        description.pop_back();
    description = trim(description);
    const std::vector<std::string> w = words(description);
    if (description.size() < 4 || std::none_of(w.begin(), w.end(), [](const std::string& x) { return x.size() >= 3; }))
        return std::nullopt;
    return LegendEntry{letter, description, {}};
}

std::vector<LegendEntry> readPointLegend(const Document& doc, const std::set<std::string>& letters,
                                         const std::vector<Vec2>& pointPositions, double letterRadius)
{
    std::vector<LegendEntry> found;
    std::set<std::string> seen;
    const auto accept = [&](LegendEntry e) {
        while (!e.description.empty() && std::string_view(";.,: ").find(e.description.back()) != std::string_view::npos)
            e.description.pop_back();
        if (seen.insert(e.letter).second)
            found.push_back(std::move(e));
    };

    struct Group {
        std::string where;
        std::vector<const Entity*> texts;
        bool model = false;
    };
    std::vector<Group> groups;
    std::map<int, std::size_t> bySpace;
    for (const auto& e : doc.entities()) {
        if (e->type() != EntityType::Text && e->type() != EntityType::MText)
            continue;
        const int space = doc.resolveSpace(e->space());
        auto it = bySpace.find(space);
        if (it == bySpace.end()) {
            Group g;
            g.model = space == 0;
            if (space == 0)
                g.where = "Modello";
            else if (const Layout* l = doc.findLayout(space))
                g.where = l->name;
            else
                g.where = "Layout";
            it = bySpace.emplace(space, groups.size()).first;
            groups.push_back(std::move(g));
        }
        groups[it->second].texts.push_back(e.get());
    }
    // Il modello per primo, poi i layout, poi i blocchi.
    std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.model && !b.model; });
    for (const auto& block : doc.blocks()) {
        if (!block || block->name.rfind("*Model_Space", 0) == 0 || block->name.rfind("*Paper_Space", 0) == 0)
            continue;
        Group g;
        g.where = "blocco " + block->name;
        for (const auto& e : block->entities)
            if (e->type() == EntityType::Text || e->type() == EntityType::MText)
                g.texts.push_back(e.get());
        if (!g.texts.empty())
            groups.push_back(std::move(g));
    }

    const auto parse = [&letters](const std::string& line) -> std::optional<LegendEntry> {
        if (auto e = parseLegendLine(line, true))
            return e;
        if (auto e = parseLegendLine(line, false); e && letters.count(e->letter))
            return e;
        return std::nullopt;
    };

    // 1) Righe "Tipo "D": ..." (anche più righe in un testo multilinea, con le righe di continuazione).
    for (const Group& g : groups)
        for (const Entity* e : g.texts) {
            std::optional<LegendEntry> current;
            for (const std::string& line : entityLines(*e)) {
                if (auto entry = parse(line)) {
                    if (current)
                        accept(*current);
                    current = std::move(entry);
                    current->where = g.where;
                } else if (current && !trim(line).empty()) {
                    current->description += ' ' + trim(line);
                }
            }
            if (current)
                accept(*current);
        }

    // 2) Lettera e descrizione in due testi affiancati, per le lettere della pianta non ancora trovate.
    for (const Group& g : groups)
        for (const Entity* e : g.texts) {
            const std::vector<std::string> lines = entityLines(*e);
            if (lines.size() != 1)
                continue;
            const std::string letter = trim(lines.front());
            if (!letters.count(letter) || seen.count(letter))
                continue;
            const BBox lb = e->bounds();
            if (!lb.isValid())
                continue;
            if (g.model && std::any_of(pointPositions.begin(), pointPositions.end(), [&](Vec2 p) {
                    return distance(p, lb.center()) <= letterRadius;
                }))
                continue;   // è la lettera di un punto in pianta
            const double h = std::max(entityHeight(*e), lb.height());
            const Entity* best = nullptr;
            double bestDx = std::numeric_limits<double>::max();
            for (const Entity* other : g.texts) {
                if (other == e)
                    continue;
                const BBox ob = other->bounds();
                if (!ob.isValid())
                    continue;
                const double hh = std::max(h, entityHeight(*other));
                const double dx = ob.min.x - lb.max.x;
                if (dx <= -0.1 * hh || dx > 30.0 * hh || std::abs(ob.center().y - lb.center().y) > 0.8 * hh)
                    continue;
                if (dx < bestDx) {
                    bestDx = dx;
                    best = other;
                }
            }
            if (!best)
                continue;
            std::string description;
            for (const std::string& line : entityLines(*best))
                description += (description.empty() ? "" : " ") + trim(line);
            const std::vector<std::string> w = words(description);
            if (normalizeCircuitCode(description) || std::count_if(w.begin(), w.end(), [](const std::string& x) {
                                                         return x.size() >= 3;
                                                     }) < 2)
                continue;
            accept(LegendEntry{letter, description, g.where});
        }
    return found;
}

std::set<std::string> legendTokens(std::string_view text)
{
    std::set<std::string> out;
    for (const std::string& w : words(text))
        addTokens(w, out);
    return out;
}

bool descriptionMatches(std::string_view description, std::string_view keywords)
{
    const std::set<std::string> k = legendTokens(keywords);
    if (k.empty())
        return false;
    const std::set<std::string> d = legendTokens(description);
    return std::includes(d.begin(), d.end(), k.begin(), k.end());
}

const char* legendSourceName(LegendSource source)
{
    switch (source) {
    case LegendSource::Text:
        return "legenda";
    case LegendSource::Plan:
        return "sigle in pianta";
    case LegendSource::User:
        return "manuale";
    case LegendSource::None:
        break;
    }
    return "";
}

std::vector<LegendMatch> interpretLegend(const Document& doc, const std::vector<Circuit>& circuits,
                                         const CircuitScanOptions& options,
                                         const std::map<std::string, std::string>& userKeywords)
{
    const std::vector<CircuitPoint> points = findCircuitPoints(doc, options);
    std::set<std::string> planLetters;
    std::vector<Vec2> positions;
    for (const CircuitPoint& p : points) {
        positions.push_back(p.position);
        if (!p.letter.empty())
            planLetters.insert(p.letter);
    }
    const std::vector<LegendEntry> legend = readPointLegend(doc, planLetters, positions, options.letterRadius);

    std::map<std::string, const Circuit*> byCode;
    for (const Circuit& c : circuits)
        byCode.emplace(c.code, &c);
    const std::vector<CircuitTag> tags = findCircuitTags(doc, options);
    std::set<std::string> planBoards;
    for (const CircuitTag& t : tags)
        if (byCode.count(t.code))
            planBoards.insert(byCode[t.code]->board);
    std::vector<const Circuit*> pool;
    for (const Circuit& c : circuits)
        if (!unusedCircuit(c) && (planBoards.empty() || planBoards.count(c.board)))
            pool.push_back(&c);

    // Ogni sigla va al punto più vicino; per lettera, le sigle dei punti che ne hanno una.
    std::vector<std::vector<std::string>> pointCodes(points.size());
    for (const CircuitTag& t : tags) {
        std::size_t best = points.size();
        double bestD = options.tagRadius;
        for (std::size_t i = 0; i < points.size(); ++i)
            if (const double d = distance(points[i].position, t.position); d <= bestD) {
                bestD = d;
                best = i;
            }
        if (best < points.size() && byCode.count(t.code))
            pointCodes[best].push_back(t.code);
    }

    std::vector<std::string> order(planLetters.begin(), planLetters.end());
    std::vector<std::string> others;
    for (const LegendEntry& e : legend)
        if (!planLetters.count(e.letter))
            others.push_back(e.letter);
    std::sort(others.begin(), others.end());
    order.insert(order.end(), others.begin(), others.end());

    std::vector<LegendMatch> out;
    std::map<std::string, std::set<std::string>> legendWordsByLetter;
    for (const LegendEntry& e : legend)
        legendWordsByLetter[e.letter] = legendTokens(e.description);
    for (const std::string& letter : order) {
        LegendMatch m;
        m.letter = letter;
        for (const LegendEntry& e : legend)
            if (e.letter == letter)
                m.legend = e.description;
        std::vector<std::string> taggedCodes;
        for (std::size_t i = 0; i < points.size(); ++i)
            if (points[i].letter == letter) {
                ++m.points;
                if (!pointCodes[i].empty()) {
                    ++m.tagged;
                    taggedCodes.insert(taggedCodes.end(), pointCodes[i].begin(), pointCodes[i].end());
                }
            }
        const std::set<std::string> legendWords = legendTokens(m.legend);

        if (const auto it = userKeywords.find(letter); it != userKeywords.end() && !trim(it->second).empty()) {
            m.keywords = trim(it->second);
            m.source = LegendSource::User;
        }
        // Dalle sigle già in pianta: le parole comuni ad almeno il 60% delle descrizioni.
        if (m.source == LegendSource::None && !taggedCodes.empty()) {
            std::map<std::string, int> count, descriptions;
            for (const std::string& code : taggedCodes) {
                const std::string& d = byCode[code]->description;
                ++descriptions[d];
                for (const std::string& t : legendTokens(d))
                    ++count[t];
            }
            const double needed = 0.6 * static_cast<double>(taggedCodes.size());
            const std::string& usual = std::max_element(descriptions.begin(), descriptions.end(), [](const auto& a, const auto& b) {
                                           return a.second < b.second;
                                       })->first;
            const std::string keywords = pickWords(usual, [&](const std::string& t) {
                return count.count(t) && count.at(t) >= needed;
            });
            if (!keywords.empty()) {
                m.keywords = keywords;
                m.source = LegendSource::Plan;
                const std::set<std::string> k = legendTokens(keywords);
                if (!legendWords.empty()
                    && std::none_of(k.begin(), k.end(), [&](const std::string& t) { return legendWords.count(t) > 0; }))
                    m.warning = "la legenda dice \"" + m.legend + "\", ma le sigle vicine ai punti sono di \"" + usual
                                + "\": verificare la lettera";
            }
        }
        // Dal testo della legenda: i circuiti che ne contengono più parole e, a pari merito, con meno parole
        // in più ("scala mobile": l'alimentazione, non il riscaldamento della scala mobile).
        const auto textScore = [&legendWords](const std::string& description) {
            const std::set<std::string> t = legendTokens(description);
            std::size_t common = 0;
            for (const std::string& w : legendWords)
                common += t.count(w);
            const double cover = static_cast<double>(common) / static_cast<double>(std::max<std::size_t>(1, legendWords.size()));
            const double jaccard = common ? static_cast<double>(common) / static_cast<double>(legendWords.size() + t.size() - common) : 0.0;
            return std::pair<double, double>{cover, jaccard};
        };
        std::pair<double, double> bestScore{0.0, 0.0};
        if (m.source == LegendSource::None && !legendWords.empty()) {
            const Circuit* best = nullptr;
            for (const Circuit* c : pool) {
                const auto s = textScore(c->description);
                if (s.first > bestScore.first + 1e-9 || (std::abs(s.first - bestScore.first) <= 1e-9 && s.second > bestScore.second + 1e-9)) {
                    best = c;
                    bestScore = s;
                }
            }
            if (best && bestScore.first >= 0.5) {
                m.keywords = pickWords(best->description, [&](const std::string& t) { return legendWords.count(t) > 0; });
                m.source = LegendSource::Text;
            }
        }

        if (!m.keywords.empty()) {
            // Dalle sigle in pianta: stesse parole delle descrizioni già usate (a parte luoghi e numeri).
            // Dal testo: il punteggio migliore. Dall'utente: le descrizioni che contengono le parole chiave.
            const std::set<std::string> k = legendTokens(m.keywords);
            const auto accepts = [&](const Circuit& c) {
                switch (m.source) {
                case LegendSource::Plan:
                    return legendTokens(c.description) == k;
                case LegendSource::Text: {
                    const auto s = textScore(c.description);
                    if (std::abs(s.first - bestScore.first) > 1e-9 || std::abs(s.second - bestScore.second) > 1e-9)
                        return false;
                    // Non se un'altra lettera lo descrive meglio (D "lama d'aria" e L "unità esterna lama
                    // d'aria": le unità esterne vanno alla L).
                    const std::set<std::string> t = legendTokens(c.description);
                    for (const auto& [other, words] : legendWordsByLetter) {
                        if (other == m.letter || words.empty())
                            continue;
                        std::size_t common = 0;
                        for (const std::string& w : words)
                            common += t.count(w);
                        const double cover = static_cast<double>(common) / static_cast<double>(words.size());
                        const double jaccard = static_cast<double>(common) / static_cast<double>(words.size() + t.size() - common);
                        if (cover > s.first + 1e-9 || (std::abs(cover - s.first) <= 1e-9 && jaccard > s.second + 1e-9))
                            return false;
                    }
                    return true;
                }
                default:
                    return descriptionMatches(c.description, m.keywords);
                }
            };
            for (const Circuit* c : pool)
                if (accepts(*c))
                    m.circuits.push_back(c->code);
            std::set<std::string> odd;
            for (const std::string& code : taggedCodes)
                if (!accepts(*byCode[code]))
                    odd.insert(code);
            m.outliers.assign(odd.begin(), odd.end());
        }
        if (m.warning.empty()) {
            if (m.legend.empty() && m.points > 0)
                m.warning = "lettera non trovata nella legenda";
            else if (!circuits.empty() && m.keywords.empty() && m.points > 0)
                m.warning = "nessun circuito dello schema corrisponde: indicare le parole chiave";
            else if (!circuits.empty() && !m.keywords.empty() && m.circuits.empty() && m.points > 0)
                m.warning = "nessun circuito contiene \"" + m.keywords + "\"";
        }
        out.push_back(std::move(m));
    }
    return out;
}

} // namespace cartesio
