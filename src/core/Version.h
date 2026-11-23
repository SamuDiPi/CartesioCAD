#pragma once

// Numeri di versione di CartesioCAD ("0.1.1", anche con la "v" del tag GitHub) e confronto, per gli aggiornamenti.

#include <optional>
#include <string>
#include <string_view>

namespace cartesio {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    // "1.2.3", "v1.2.3", "1.2" (patch 0). Nient'altro: niente suffissi, niente numeri negativi o enormi.
    static std::optional<Version> parse(std::string_view text);
    std::string text() const;

    friend bool operator==(const Version& a, const Version& b)
    {
        return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
    }
    friend bool operator<(const Version& a, const Version& b)
    {
        if (a.major != b.major)
            return a.major < b.major;
        if (a.minor != b.minor)
            return a.minor < b.minor;
        return a.patch < b.patch;
    }
};

// Aggiornamento "major" (cambi importanti, conferma esplicita): cambia il primo numero, oppure il pacchetto firmato
// lo dichiara tale (flag del manifesto).
bool isMajorUpdate(const Version& from, const Version& to, bool declaredMajor);

} // namespace cartesio
