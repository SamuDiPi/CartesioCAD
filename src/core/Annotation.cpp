#include "core/Annotation.h"

#include <cmath>

namespace cartesio {

std::vector<AnnoScale> defaultAnnoScales()
{
    std::vector<AnnoScale> scales;
    for (double d : {1.0, 2.0, 5.0, 10.0, 20.0, 25.0, 50.0, 100.0, 200.0, 250.0, 500.0, 1000.0, 2000.0})
        scales.push_back({"1:" + std::to_string(static_cast<int>(d)), 1.0, d});
    for (double p : {2.0, 5.0, 10.0})
        scales.push_back({std::to_string(static_cast<int>(p)) + ":1", p, 1.0});
    return scales;
}

bool canBeAnnotative(EntityType type)
{
    return type == EntityType::Text || type == EntityType::MText || type == EntityType::Dimension
           || type == EntityType::Leader;
}

bool sameFactor(double a, double b)
{
    return std::abs(a - b) <= 1e-6 * std::max(std::abs(a), std::abs(b));
}

void rescaleAnnotation(Entity& e, double ratio)
{
    if (!(ratio > 0.0) || sameFactor(ratio, 1.0))
        return;
    switch (e.type()) {
    case EntityType::Text: {
        auto& t = static_cast<TextEntity&>(e);
        t.height *= ratio;
        t.alignPoint = t.position + (t.alignPoint - t.position) * ratio;
        break;
    }
    case EntityType::MText: {
        auto& t = static_cast<MTextEntity&>(e);
        t.height *= ratio;
        t.width *= ratio;
        break;
    }
    case EntityType::Dimension: {
        auto& d = static_cast<DimensionEntity&>(e);
        d.style.scale *= ratio;
        // La grafica si ricalcola (il blocco letto dal file è a un'altra scala).
        d.block.reset();
        d.blockName.clear();
        break;
    }
    case EntityType::Leader: {
        auto& l = static_cast<LeaderEntity&>(e);
        l.textHeight *= ratio;
        l.arrowSize *= ratio;
        if (l.landingGap > 0.0)
            l.landingGap *= ratio;
        // Il tratto orizzontale di approdo (ultimo punto) è lungo quanto la freccia: segue la scala.
        if (l.points.size() >= 3) {
            const Vec2 last = l.points.back(), prev = l.points[l.points.size() - 2];
            if (std::abs(last.y - prev.y) < 1e-9 * (1.0 + std::abs(last.y)))
                l.points.back() = prev + (last - prev) * ratio;
        }
        break;
    }
    default:
        break;
    }
}

std::unique_ptr<Entity> annotationAt(const Entity& e, double factor)
{
    auto copy = e.clone();
    rescaleAnnotation(*copy, factor / e.annoFactor());
    copy->setAnnoFactor(factor);
    return copy;
}

} // namespace cartesio
