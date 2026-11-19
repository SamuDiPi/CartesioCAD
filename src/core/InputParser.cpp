#include "core/InputParser.h"

#include <cctype>
#include <charconv>
#include <cmath>

namespace cartesio {

namespace {

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

std::optional<double> parseNumber(std::string_view s)
{
    s = trim(s);
    if (!s.empty() && s.front() == '+')
        s.remove_prefix(1);
    if (s.empty())
        return std::nullopt;

    double value = 0.0;
    const char* first = s.data();
    const char* last = s.data() + s.size();
    const auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc() || ptr != last || !std::isfinite(value))
        return std::nullopt;
    return value;
}

// "x,y" oppure "distanza<angolo"
std::optional<Vec2> parseCoordinates(std::string_view s)
{
    if (const auto lt = s.find('<'); lt != std::string_view::npos) {
        const auto dist = parseNumber(s.substr(0, lt));
        const auto angle = parseNumber(s.substr(lt + 1));
        if (!dist || !angle)
            return std::nullopt;
        return Vec2::polar(*dist, degToRad(*angle));
    }

    const auto comma = s.find(',');
    if (comma == std::string_view::npos)
        return std::nullopt;
    const auto x = parseNumber(s.substr(0, comma));
    const auto y = parseNumber(s.substr(comma + 1));
    if (!x || !y)
        return std::nullopt;
    return Vec2{*x, *y};
}

} // namespace

std::optional<ParsedInput> parseInput(std::string_view text, std::optional<Vec2> lastPoint)
{
    ParsedInput result;
    const std::string_view s = trim(text);

    if (s.empty())
        return result; // Kind::Empty

    if (s.front() == '@') {
        const Vec2 base = lastPoint.value_or(Vec2{});
        const std::string_view rest = trim(s.substr(1));
        if (rest.empty()) {
            result.kind = ParsedInput::Kind::Point;
            result.point = base;
            return result;
        }
        const auto offset = parseCoordinates(rest);
        if (!offset)
            return std::nullopt;
        result.kind = ParsedInput::Kind::Point;
        result.point = base + *offset;
        return result;
    }

    if (s.find_first_of(",<") != std::string_view::npos) {
        const auto p = parseCoordinates(s);
        if (!p)
            return std::nullopt;
        result.kind = ParsedInput::Kind::Point;
        result.point = *p;
        return result;
    }

    if (const auto v = parseNumber(s)) {
        result.kind = ParsedInput::Kind::Value;
        result.value = *v;
        return result;
    }

    // Parola. Il prefisso "_" (nomi internazionali, es. "_LINE") viene ignorato.
    std::string_view word = s;
    if (word.front() == '_')
        word.remove_prefix(1);
    if (word.empty())
        return std::nullopt;
    for (char c : word) {
        const auto uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-')
            return std::nullopt;
        result.word.push_back(static_cast<char>(std::toupper(uc)));
    }
    result.kind = ParsedInput::Kind::Word;
    return result;
}

} // namespace cartesio
