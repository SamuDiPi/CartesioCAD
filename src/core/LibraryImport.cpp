#include "core/LibraryImport.h"

#include "core/Entity.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <regex>
#include <set>
#include <unordered_map>

namespace cartesio {

namespace {

constexpr KnownLibraryBlock kKnown[] = {
#include "core/LibraryKnown.inc"
};

std::string lowerAscii(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trimmed(const std::string& s)
{
    const auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return {};
    const auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// Parole tecniche generiche che possono restare nei blocchi (minuscole).
const std::set<std::string>& allowedWords()
{
    static const std::set<std::string> words{
        "on", "off", "i", "o", "trip", "test", "reset", "ok", "mode", "ready", "push", "to", "stop", "start", "auto",
        "manual", "man", "manuale", "alarm", "allarme", "a", "v", "w", "kw", "kva", "var", "hz", "ka", "ma", "ms", "s",
        "h", "min", "max", "n", "l1", "l2", "l3", "pe", "ue", "ui", "uimp", "ir", "isd", "ii", "in", "icu", "ics", "icc",
        "icw", "tr", "tsd", "tsi", "ig", "tg", "xin", "xir", "iec", "en", "cei", "nema", "ab1", "fisso", "fissa",
        "rimovibile", "estraibile", "esecuzione", "ese", "esec", "estr", "estraib", "rim", "manovra", "manov", "diretta",
        "dir", "rotativa", "rot", "telecomando", "tel", "telecom", "comando", "aperto", "chiuso", "open", "closed",
        "charged", "discharged", "carica", "scarica", "inserito", "sezionato", "test", "x", "e", "p", "u", "l", "m",
        "t", "z", "g", "r", "c", "d", "b", "f", "k", "q", "y", "ac", "dc", "cc", "ca", "led", "esc", "ent", "enter",
        "menu", "set", "info", "lock", "unlock", "pos", "position", "connected", "disconnected", "power", "fault",
        "ir(a)", "sel", "mem"};
    return words;
}

bool numberLike(const std::string& t)
{
    static const std::regex re(R"(^[<>~+\-]?[0-9][0-9.,x/\-]*[a-z%°]{0,3}$)", std::regex::icase);
    return std::regex_match(t, re);
}

std::vector<std::string> tokens(const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (std::isspace(c) || ch == '(' || ch == ')' || ch == ',' || ch == ';' || ch == ':' || ch == '/' || ch == '['
            || ch == ']' || ch == '+' || ch == '=' || ch == '.' || ch == '*' || ch == '"' || ch == '\'') {
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

std::string mtextPlain(const MTextEntity& m)
{
    std::string s;
    for (const std::string& line : m.lines())
        s += (s.empty() ? "" : " ") + line;
    return s;
}

std::vector<std::string> splitRow(const std::string& line)
{
    const char sep = line.find(';') != std::string::npos ? ';' : ',';
    std::vector<std::string> f;
    std::string cell;
    bool quoted = false;
    for (const char c : line) {
        if (c == '"')
            quoted = !quoted;
        else if (c == sep && !quoted) {
            f.push_back(trimmed(cell));
            cell.clear();
        } else
            cell += c;
    }
    f.push_back(trimmed(cell));
    return f;
}

std::vector<std::string> tokensBy(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::size_t a = 0;
    while (a <= s.size()) {
        const std::size_t b = s.find(sep, a);
        const std::string t = trimmed(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (!t.empty())
            out.push_back(t);
        if (b == std::string::npos)
            break;
        a = b + 1;
    }
    return out;
}

std::string number(double v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%g", v);
    std::string s = buf;
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

std::string safeCode(const std::string& s)
{
    std::string out;
    for (const char c : s)
        out += std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' ? c : '_';
    return out;
}

} // namespace

std::vector<LibraryMenuEntry> parseLibraryMenu(const std::string& text)
{
    std::vector<LibraryMenuEntry> out;
    std::vector<std::string> stack;
    static const std::regex item(R"(^\s*\[(.*?)\](.*)$)");
    static const std::regex insert(R"(INSERT\s+(\S+))", std::regex::icase);
    static const std::regex root(R"(^[A-Za-z]:[/\\][^/\\]+[/\\])");
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        start = end + 1;
        if (line.rfind("***", 0) == 0) {
            stack.clear();
            continue;
        }
        std::smatch m;
        if (!std::regex_match(line, m, item))
            continue;
        std::string label = m[1].str();
        const std::string action = m[2].str();
        int pops = 0;
        while (label.rfind("<-", 0) == 0) {
            label = label.substr(2);
            ++pops;
        }
        if (label.rfind("->", 0) == 0) {
            stack.push_back(trimmed(label.substr(2)));
            continue;
        }
        std::smatch f;
        if (std::regex_search(action, f, insert)) {
            std::string file = f[1].str();
            std::replace(file.begin(), file.end(), '\\', '/');
            file = std::regex_replace(file, root, "");
            out.push_back({stack, trimmed(label), file});
        }
        for (int i = 0; i < pops && !stack.empty(); ++i)
            stack.pop_back();
        if (end >= text.size())
            break;
    }
    return out;
}

bool libraryTextAllowed(const std::string& text)
{
    const std::string t = trimmed(text);
    if (t.empty())
        return true;
    for (const std::string& tok : tokens(t)) {
        const std::string low = lowerAscii(tok);
        if (allowedWords().count(low) || numberLike(low))
            continue;
        // simboli e lettere greche (Δ, ±...) senza lettere latine
        if (std::none_of(low.begin(), low.end(), [](char c) { return c >= 'a' && c <= 'z'; }))
            continue;
        return false;
    }
    return true;
}

void cleanLibraryDrawing(Document& doc, const std::string& uid, double scale, LibraryCleanReport& report)
{
    const int defpoints = doc.findLayer("Defpoints");
    std::set<std::string> logos;
    for (const auto& b : doc.blocks()) {
        const std::string low = lowerAscii(b->name);
        if (!b->name.empty() && b->name[0] != '*' && (low.find("logo") != std::string::npos
                                                       || low.find("marchio") != std::string::npos))
            logos.insert(b->name);
    }
    const auto noteWords = [&report](const std::string& text) {
        for (const std::string& tok : tokens(text)) {
            const std::string low = lowerAscii(tok);
            if (low.size() >= 3 && !allowedWords().count(low) && !numberLike(low)
                && std::any_of(low.begin(), low.end(), [](char c) { return c >= 'a' && c <= 'z'; }))
                ++report.words[low];
        }
    };
    // Vero se l'oggetto va tolto; pulisce gli attributi degli inserimenti.
    const auto drop = [&](Entity& e) {
        if (defpoints >= 0 && e.layer() == defpoints)
            return true;
        switch (e.type()) {
        case EntityType::Point:
        case EntityType::Dimension:
            return true;
        case EntityType::AttDef: {
            const auto& a = static_cast<const AttDefEntity&>(e);
            if (!trimmed(a.text.text).empty()) {
                report.log.push_back({"attributo", a.text.text});
                noteWords(a.text.text);
            }
            return true;
        }
        case EntityType::Text: {
            const auto& t = static_cast<const TextEntity&>(e);
            if (libraryTextAllowed(t.text))
                return false;
            report.log.push_back({"testo", t.text});
            noteWords(t.text);
            return true;
        }
        case EntityType::MText: {
            const std::string s = mtextPlain(static_cast<const MTextEntity&>(e));
            if (libraryTextAllowed(s))
                return false;
            report.log.push_back({"testo", s});
            noteWords(s);
            return true;
        }
        case EntityType::Insert: {
            auto& in = static_cast<InsertEntity&>(e);
            if (logos.count(in.blockName)) {
                report.log.push_back({"logo", in.blockName});
                return true;
            }
            for (Attribute& att : in.attributes)
                if (!libraryTextAllowed(att.text.text)) {
                    report.log.push_back({"attributo", att.text.text});
                    noteWords(att.text.text);
                    att.text.text.clear();
                    ++report.removed;
                }
            return false;
        }
        default:
            return false;
        }
    };

    // Nomi univoci per i blocchi con nome (non anonimi) che restano.
    std::map<std::string, std::string> rename;
    int n = 0;
    for (const auto& b : doc.blocks())
        if (!b->name.empty() && b->name[0] != '*' && !logos.count(b->name))
            rename[b->name] = "LIB_" + uid + "_" + std::to_string(++n);
    const auto renamed = [&rename](Entity& e) {
        if (e.type() == EntityType::Insert) {
            auto& in = static_cast<InsertEntity&>(e);
            if (const auto it = rename.find(in.blockName); it != rename.end())
                in.blockName = it->second;
        }
    };

    // Blocchi: oggetti tolti sul posto, poi i nomi.
    for (const auto& b : doc.blocks()) {
        if (logos.count(b->name))
            continue;
        auto& list = b->entities;
        const auto before = list.size();
        list.erase(std::remove_if(list.begin(), list.end(), [&](const std::unique_ptr<Entity>& e) { return drop(*e); }),
                   list.end());
        report.removed += static_cast<int>(before - list.size());
        for (auto& e : list)
            renamed(*e);
        b->invalidateBounds();
    }
    for (const std::string& name : logos)
        doc.eraseBlock(name);
    for (const auto& b : doc.blocks())
        if (const auto it = rename.find(b->name); it != rename.end())
            b->name = it->second;

    // Spazio modello: copie pulite, rinominate e scalate al posto degli originali.
    std::vector<std::unique_ptr<Entity>> kept;
    std::vector<EntityId> all;
    for (const auto& e : doc.entities()) {
        all.push_back(e->id());
        auto copy = e->clone();
        if (drop(*copy)) {
            ++report.removed;
            continue;
        }
        renamed(*copy);
        if (scale != 1.0)
            copy->transform(Affine::scaling({0.0, 0.0}, scale));
        kept.push_back(std::move(copy));
    }
    doc.commit(std::move(kept), all);
    doc.linkBlocks();
    doc.setInsUnits(4);
}

const KnownLibraryBlock* knownLibraryBlock(const std::string& sha256)
{
    static const std::unordered_map<std::string, const KnownLibraryBlock*> index = [] {
        std::unordered_map<std::string, const KnownLibraryBlock*> m;
        for (const KnownLibraryBlock& k : kKnown)
            m.emplace(k.sha256, &k);   // la prima voce di un file
        return m;
    }();
    const auto it = index.find(lowerAscii(sha256));
    return it == index.end() ? nullptr : it->second;
}

double knownLibraryScale()
{
    return 10.0;
}

std::vector<std::string> libraryKeywords(const std::map<std::string, int>& words, std::size_t count)
{
    // Solo sigle (lettere e cifre insieme, non misure come 250A) ripetute spesso: le parole comuni dei testi tolti
    // (moduli, quadro...) riconoscerebbero la famiglia in qualsiasi schema.
    std::vector<std::pair<int, std::string>> sorted;
    for (const auto& [w, c] : words) {
        const bool letters = std::any_of(w.begin(), w.end(), [](char ch) { return ch >= 'a' && ch <= 'z'; });
        const bool digits = std::any_of(w.begin(), w.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
        if (c >= 20 && w.size() >= 3 && letters && digits && !numberLike(w) && w.find('|') == std::string::npos
            && w.find(';') == std::string::npos)
            sorted.push_back({c, w});
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    std::vector<std::string> out;
    for (const auto& [c, w] : sorted) {
        if (out.size() >= count)
            break;
        out.push_back(w);
    }
    return out;
}

std::string libraryDeviceRows(const std::vector<ImportedLibraryBlock>& blocks, const std::string& family,
                              const std::vector<std::string>& keywords)
{
    std::string kw;
    for (const std::string& w : keywords)
        kw += (kw.empty() ? "" : "|") + w;
    std::string out = family + ";famiglia;;;;;;;;;;;" + kw + ";;\n";
    std::set<std::string> done;
    for (const ImportedLibraryBlock& b : blocks) {
        if (!b.known || std::string(b.known->kind) == "struttura" || !done.insert(b.known->code).second)
            continue;
        const KnownLibraryBlock& k = *b.known;
        std::string poles;
        if (!(k.polesMin == 0 && k.polesMax == 99))
            poles = k.polesMin == k.polesMax ? std::to_string(k.polesMin)
                                             : std::to_string(k.polesMin) + "-" + std::to_string(k.polesMax);
        const bool anyCurrent = k.ratedMin <= 0.0 && k.ratedMax >= 1e8;
        out += safeCode(family) + "-" + k.code + ";" + k.kind + ";" + poles + ";" + (anyCurrent ? "" : number(k.ratedMin))
               + ";" + (anyCurrent ? "" : number(k.ratedMax)) + ";" + (k.modules > 0.0 ? number(k.modules) : "") + ";;;"
               + b.path + ";;" + k.text + ";libreria importata (uso personale);;" + (k.plate ? "si" : "") + ";" + family
               + "\n";
    }
    return out;
}

std::string libraryEnclosureRows(const std::vector<ImportedLibraryBlock>& blocks, const std::string& family)
{
    std::string out;
    std::set<std::string> done;
    for (const ImportedLibraryBlock& b : blocks) {
        if (!b.known || std::string(b.known->kind) != "struttura" || !done.insert(b.known->code).second)
            continue;
        const double w = b.known->ratedMin, h = b.known->ratedMax;
        for (const int depth : {400, 600})
            out += safeCode(family) + "-" + b.known->code + "-" + std::to_string(depth) + ";pavimento;" + number(w) + ";"
                   + number(h) + ";" + std::to_string(depth) + ";31;0;" + b.known->text
                   + " (libreria importata);misure del disegno, profondita' e IP da confermare;" + b.path + ";;" + family
                   + "\n";
    }
    return out;
}

std::string mergeLibraryCatalog(const std::string& existing, const std::string& header, const std::string& rows,
                                const std::string& family, std::size_t familyColumn)
{
    std::string comments, kept;
    std::vector<std::string> oldWords;   // parole della famiglia già nel catalogo (scelte dall'utente): restano
    bool headerSeen = false;
    std::size_t start = 0;
    while (start < existing.size()) {
        std::size_t end = existing.find('\n', start);
        if (end == std::string::npos)
            end = existing.size();
        std::string line = existing.substr(start, end - start);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        start = end + 1;
        if (trimmed(line).empty())
            continue;
        if (line[0] == '#') {
            comments += line + "\n";
            continue;
        }
        if (!headerSeen) {   // intestazione: si usa quella nuova
            headerSeen = true;
            continue;
        }
        const std::vector<std::string> f = splitRow(line);
        const bool ownFamily = (f.size() > familyColumn && f[familyColumn] == family)
                               || (f.size() > 1 && lowerAscii(f[1]) == "famiglia" && f[0] == family);
        if (!ownFamily)
            kept += line + "\n";
        else if (f.size() > 1 && lowerAscii(f[1]) == "famiglia") {
            kept += "\x01\n";   // posto della dichiarazione della famiglia (resta nello stesso ordine)
            if (f.size() > 12)
                for (const std::string& w : tokensBy(f[12], '|'))
                    oldWords.push_back(w);
        }
    }
    std::string ownDeclaration;
    {
        const std::string first = rows.substr(0, rows.find('\n'));
        const std::vector<std::string> f = splitRow(first);
        if (f.size() > 1 && lowerAscii(f[1]) == "famiglia") {
            ownDeclaration = first;
            if (!oldWords.empty()) {   // prima le parole che c'erano, poi le nuove
                std::vector<std::string> words = oldWords;
                for (const std::string& w : f.size() > 12 ? tokensBy(f[12], '|') : std::vector<std::string>{})
                    if (std::find(words.begin(), words.end(), w) == words.end())
                        words.push_back(w);
                std::string joined;
                for (const std::string& w : words)
                    joined += (joined.empty() ? "" : "|") + w;
                std::vector<std::string> cells = f;
                cells.resize(std::max<std::size_t>(cells.size(), 15));
                cells[12] = joined;
                ownDeclaration.clear();
                for (std::size_t i = 0; i < cells.size(); ++i)
                    ownDeclaration += (i ? ";" : "") + cells[i];
            }
        }
    }
    if (const std::size_t at = kept.find("\x01\n"); at != std::string::npos)
        kept.replace(at, 2, ownDeclaration.empty() ? std::string() : ownDeclaration + "\n");
    else if (!ownDeclaration.empty())
        kept += ownDeclaration + "\n";
    const std::string newRows =
        ownDeclaration.empty() ? rows : rows.substr(std::min(rows.size(), rows.find('\n') + 1));
    // Le dichiarazioni di famiglia restano in testa nell'ordine (la prima è la predefinita).
    std::string families, others;
    for (const std::string& block : {kept, newRows}) {
        std::size_t s = 0;
        while (s < block.size()) {
            std::size_t e = block.find('\n', s);
            if (e == std::string::npos)
                e = block.size();
            const std::string line = block.substr(s, e - s);
            s = e + 1;
            if (line.empty())
                continue;
            const std::vector<std::string> f = splitRow(line);
            (f.size() > 1 && lowerAscii(f[1]) == "famiglia" ? families : others) += line + "\n";
        }
    }
    return comments + header + "\n" + families + others;
}

} // namespace cartesio
