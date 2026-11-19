#include "app/PdfWords.h"

#ifdef CARTESIO_HAS_POPPLER
#include <poppler-document.h>
#include <poppler-page.h>
#endif

#include <memory>

namespace cartesio {

bool pdfSupport()
{
#ifdef CARTESIO_HAS_POPPLER
    return true;
#else
    return false;
#endif
}

bool readPdfWords(const std::string& bytes, std::vector<PdfWord>& words, std::string* error)
{
#ifdef CARTESIO_HAS_POPPLER
    std::vector<char> data(bytes.begin(), bytes.end());
    std::unique_ptr<poppler::document> doc(
        poppler::document::load_from_raw_data(data.data(), static_cast<int>(data.size())));
    if (!doc) {
        if (error)
            *error = "Il file non è un PDF leggibile.";
        return false;
    }
    if (doc->is_locked()) {
        if (error)
            *error = "Il PDF è protetto da password.";
        return false;
    }
    for (int i = 0; i < doc->pages(); ++i) {
        std::unique_ptr<poppler::page> page(doc->create_page(i));
        if (!page)
            continue;
        for (const poppler::text_box& box : page->text_list()) {
            const poppler::rectf r = box.bbox();
            const poppler::byte_array utf8 = box.text().to_utf8();
            words.push_back(PdfWord{i + 1, r.x(), r.y(), r.width(), r.height(), std::string(utf8.begin(), utf8.end())});
        }
    }
    return true;
#else
    (void)bytes;
    (void)words;
    if (error)
        *error = "Questa versione di CartesioCAD è compilata senza Poppler: i PDF non si possono leggere.";
    return false;
#endif
}

} // namespace cartesio
