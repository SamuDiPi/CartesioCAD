#include "core/Version.h"

#include <vector>

namespace cartesio {

std::optional<Version> Version::parse(std::string_view text)
{
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V'))
        text.remove_prefix(1);
    if (text.empty() || text.size() > 32)
        return std::nullopt;
    std::vector<int> parts;
    int value = 0;
    int digits = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        const char c = i < text.size() ? text[i] : '.';
        if (c >= '0' && c <= '9') {
            if (++digits > 6)
                return std::nullopt;
            value = value * 10 + (c - '0');
        } else if (c == '.') {
            if (digits == 0)
                return std::nullopt;
            parts.push_back(value);
            value = 0;
            digits = 0;
        } else {
            return std::nullopt;
        }
    }
    if (parts.size() < 2 || parts.size() > 3)
        return std::nullopt;
    Version v;
    v.major = parts[0];
    v.minor = parts[1];
    v.patch = parts.size() > 2 ? parts[2] : 0;
    return v;
}

std::string Version::text() const
{
    return std::to_string(major) + '.' + std::to_string(minor) + '.' + std::to_string(patch);
}

bool isMajorUpdate(const Version& from, const Version& to, bool declaredMajor)
{
    return declaredMajor || to.major > from.major;
}

} // namespace cartesio
