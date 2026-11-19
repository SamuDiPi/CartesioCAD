#include "app/SpellCheck.h"

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <spellcheck.h>
#endif

namespace cartesio {

#ifdef _WIN32
namespace {
// {7AB36653-1796-484B-BDFA-E74F1DB7C1DC} e {8E018A9D-2415-4677-BF08-794EA61F94BB}
const CLSID kSpellCheckerFactory = {0x7ab36653, 0x1796, 0x484b, {0xbd, 0xfa, 0xe7, 0x4f, 0x1d, 0xb7, 0xc1, 0xdc}};
const IID kISpellCheckerFactory = {0x8e018a9d, 0x2415, 0x4677, {0xbf, 0x08, 0x79, 0x4e, 0xa6, 0x1f, 0x94, 0xbb}};

ISpellChecker* checkerOf(void* p)
{
    return static_cast<ISpellChecker*>(p);
}
} // namespace
#endif

SpellChecker& SpellChecker::instance()
{
    static SpellChecker checker;
    return checker;
}

SpellChecker::SpellChecker()
{
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // già fatto da Qt sul thread principale: innocuo
    ISpellCheckerFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(kSpellCheckerFactory, nullptr, CLSCTX_INPROC_SERVER, kISpellCheckerFactory,
                                reinterpret_cast<void**>(&factory)))
        || !factory)
        return;
    // Italiano se installato, altrimenti la lingua dell'utente.
    QStringList candidates{QStringLiteral("it-IT")};
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0)
        candidates << QString::fromWCharArray(locale);
    candidates << QStringLiteral("en-US");
    for (const QString& lang : candidates) {
        BOOL supported = FALSE;
        const std::wstring w = lang.toStdWString();
        if (SUCCEEDED(factory->IsSupported(w.c_str(), &supported)) && supported) {
            ISpellChecker* checker = nullptr;
            if (SUCCEEDED(factory->CreateSpellChecker(w.c_str(), &checker)) && checker) {
                checker_ = checker;
                language_ = lang;
                break;
            }
        }
    }
    factory->Release();
#endif
}

SpellChecker::~SpellChecker()
{
#ifdef _WIN32
    if (checker_)
        checkerOf(checker_)->Release();
#endif
}

std::vector<std::pair<int, int>> SpellChecker::errors(const QString& text) const
{
    std::vector<std::pair<int, int>> out;
#ifdef _WIN32
    if (!checker_ || !enabled_ || text.trimmed().isEmpty())
        return out;
    const std::wstring w = text.toStdWString();
    IEnumSpellingError* errors = nullptr;
    if (FAILED(checkerOf(checker_)->Check(w.c_str(), &errors)) || !errors)
        return out;
    ISpellingError* error = nullptr;
    while (errors->Next(&error) == S_OK && error) {
        ULONG start = 0, length = 0;
        CORRECTIVE_ACTION action = CORRECTIVE_ACTION_NONE;
        error->get_StartIndex(&start);
        error->get_Length(&length);
        error->get_CorrectiveAction(&action);
        if (action != CORRECTIVE_ACTION_NONE && length > 0)
            out.emplace_back(static_cast<int>(start), static_cast<int>(length));
        error->Release();
        error = nullptr;
    }
    errors->Release();
#else
    (void)text;
#endif
    return out;
}

QStringList SpellChecker::suggestions(const QString& word) const
{
    QStringList out;
#ifdef _WIN32
    if (!checker_ || word.isEmpty())
        return out;
    const std::wstring w = word.toStdWString();
    IEnumString* list = nullptr;
    if (FAILED(checkerOf(checker_)->Suggest(w.c_str(), &list)) || !list)
        return out;
    LPOLESTR s = nullptr;
    while (list->Next(1, &s, nullptr) == S_OK && s) {
        out << QString::fromWCharArray(s);
        CoTaskMemFree(s);
        s = nullptr;
        if (out.size() >= 8)
            break;
    }
    list->Release();
#else
    (void)word;
#endif
    return out;
}

void SpellChecker::addWord(const QString& word)
{
#ifdef _WIN32
    if (checker_ && !word.isEmpty()) {
        const std::wstring w = word.toStdWString();
        checkerOf(checker_)->Add(w.c_str());
    }
#else
    (void)word;
#endif
}

void SpellChecker::ignoreWord(const QString& word)
{
#ifdef _WIN32
    if (checker_ && !word.isEmpty()) {
        const std::wstring w = word.toStdWString();
        checkerOf(checker_)->Ignore(w.c_str());
    }
#else
    (void)word;
#endif
}

} // namespace cartesio
