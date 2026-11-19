#include "core/TitleBlock.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <sstream>

namespace cartesio {

const std::vector<PaperFormat>& isoPaperFormats()
{
    static const std::vector<PaperFormat> formats = {
        {"A4", 297.0, 210.0}, {"A3", 420.0, 297.0}, {"A2", 594.0, 420.0}, {"A1", 841.0, 594.0}, {"A0", 1189.0, 841.0},
    };
    return formats;
}

std::string paperFormatName(double width, double height)
{
    const double w = std::max(width, height);
    const double h = std::min(width, height);
    for (const PaperFormat& f : isoPaperFormats())
        if (std::abs(f.width - w) < 1.0 && std::abs(f.height - h) < 1.0)
            return f.name;
    std::ostringstream out;
    out << std::llround(width) << 'x' << std::llround(height);
    return out.str();
}

std::string scaleText(double scale)
{
    if (!(scale > 0.0))
        return "?";
    const auto fmt = [](double v) {
        std::ostringstream out;
        if (std::abs(v - std::round(v)) < 1e-6)
            out << std::llround(v);
        else
            out << std::round(v * 1000.0) / 1000.0;
        return out.str();
    };
    return scale >= 1.0 ? fmt(scale) + ":1" : "1:" + fmt(1.0 / scale);
}

std::string todayText()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[16];
    std::strftime(buffer, sizeof buffer, "%d/%m/%Y", &local);
    return buffer;
}

std::shared_ptr<BlockDef> makeTitleBlockDef()
{
    auto block = std::make_shared<BlockDef>();
    block->name = kTitleBlockName;
    block->base = {0.0, 0.0};
    constexpr double W = kTitleBlockWidth;
    constexpr double H = kTitleBlockHeight;
    constexpr double row = 9.0;   // altezza delle due righe in basso

    const auto line = [&](Vec2 a, Vec2 b) { block->entities.push_back(std::make_unique<LineEntity>(a, b)); };
    block->entities.push_back(std::make_unique<PolylineEntity>(
        std::vector<PolyVertex>{{{-W, 0}, 0}, {{0, 0}, 0}, {{0, H}, 0}, {{-W, H}, 0}}, true));
    line({-W, row}, {0, row});
    line({-W, 2 * row}, {0, 2 * row});
    for (double x : {-135.0, -90.0, -45.0})
        line({x, row}, {x, 2 * row});
    for (double x : {-90.0, -45.0})
        line({x, 0}, {x, row});

    // Etichette fisse (piccole, in alto a sinistra nella casella) e attributi (in basso).
    const auto label = [&](double x, double top, const char* text) {
        block->entities.push_back(std::make_unique<TextEntity>(Vec2{x + 1.5, top - 2.6}, 1.8, text));
    };
    const auto attribute = [&](double x, double bottom, double height, const char* tag, const char* prompt,
                               const char* value) {
        block->entities.push_back(
            std::make_unique<AttDefEntity>(tag, prompt, TextEntity({x + 1.5, bottom + 1.6}, height, value)));
    };

    label(-W, H, "Titolo");
    attribute(-W, 2 * row + 1.0, 5.0, "TITOLO", "Titolo del disegno", "");

    label(-180.0, 2 * row, "Disegnato");
    attribute(-180.0, row, 2.5, "AUTORE", "Autore", "");
    label(-135.0, 2 * row, "Data");
    attribute(-135.0, row, 2.5, "DATA", "Data", "");
    label(-90.0, 2 * row, "Scala");
    attribute(-90.0, row, 2.5, "SCALA", "Scala", "1:1");
    label(-45.0, 2 * row, "Foglio");
    attribute(-45.0, row, 2.5, "FOGLIO", "Formato del foglio", "");

    label(-180.0, row, "Committente / Societ\xC3\xA0");
    attribute(-180.0, 0.0, 2.5, "SOCIETA", "Committente o societ\xC3\xA0", "");
    label(-90.0, row, "N. disegno");
    attribute(-90.0, 0.0, 2.5, "NUMERO", "Numero del disegno", "01");
    label(-45.0, row, "Revisione");
    attribute(-45.0, 0.0, 2.5, "REVISIONE", "Revisione", "0");
    return block;
}

std::string titleBlockScale(const Document& doc, int layoutId)
{
    const ViewportEntity* largest = nullptr;
    for (const auto& e : doc.entities()) {
        if (e->space() != layoutId || e->type() != EntityType::Viewport)
            continue;
        const auto* vp = static_cast<const ViewportEntity*>(e.get());
        if (!largest || vp->width * vp->height > largest->width * largest->height)
            largest = vp;
    }
    return largest ? scaleText(largest->scale()) : std::string("1:1");
}

int updateTitleBlocks(Document& doc)
{
    std::vector<std::unique_ptr<Entity>> modified;
    std::vector<EntityId> ids;
    const std::string today = todayText();
    for (const auto& e : doc.entities()) {
        if (e->type() != EntityType::Insert || e->space() <= 0)
            continue;
        const auto& insert = static_cast<const InsertEntity&>(*e);
        if (insert.blockName != kTitleBlockName)
            continue;
        const Layout* layout = doc.findLayout(e->space());
        if (!layout)
            continue;
        auto copy = std::unique_ptr<InsertEntity>(static_cast<InsertEntity*>(e->clone().release()));
        bool changed = false;
        for (Attribute& a : copy->attributes) {
            std::string value = a.text.text;
            if (a.tag == "SCALA")
                value = titleBlockScale(doc, e->space());
            else if (a.tag == "DATA")
                value = today;
            else if (a.tag == "FOGLIO")
                value = paperFormatName(layout->paperWidth, layout->paperHeight);
            if (value != a.text.text) {
                a.text.text = value;
                changed = true;
            }
        }
        if (changed) {
            ids.push_back(e->id());
            modified.push_back(std::move(copy));
        }
    }
    const int count = static_cast<int>(modified.size());
    if (count > 0)
        doc.commit(std::move(modified), ids);
    return count;
}

} // namespace cartesio
