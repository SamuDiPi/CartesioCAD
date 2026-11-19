#pragma once

#include "core/Entity.h"

#include <memory>
#include <optional>
#include <vector>

namespace cartesio {

// Punti di intersezione fra due entità (linee, cerchi, archi).
std::vector<Vec2> intersections(const Entity& a, const Entity& b);

// True se una parte qualsiasi dell'entità cade nel rettangolo (selezione "intersezione").
bool touchesRect(const Entity& e, const BBox& rect);

// True se `inner` è interamente contenuto in `outer` (selezione "finestra").
bool containsBox(const BBox& outer, const BBox& inner);

// Copia parallela di `e` a distanza `distance`, dal lato del punto `side`.
// Restituisce nullptr se non è possibile (es. raggio che diventerebbe negativo).
std::unique_ptr<Entity> offsetEntity(const Entity& e, double distance, Vec2 side);

// Taglia `target` nel tratto che contiene `pick`, delimitato dai punti di taglio `cuts`.
// Restituisce i pezzi che restano (vuoto = l'oggetto va cancellato del tutto),
// oppure std::nullopt se il taglio non è possibile (cerchio con un solo punto di taglio).
std::optional<std::vector<std::unique_ptr<Entity>>> trimEntity(const Entity& target,
                                                              const std::vector<Vec2>& cuts,
                                                              Vec2 pick);

// Arco passante per tre punti (dal primo al terzo, passando per il secondo).
// nullptr se i punti sono allineati.
std::unique_ptr<ArcEntity> arcThroughPoints(Vec2 p1, Vec2 p2, Vec2 p3);

// Contorno chiuso di un oggetto come poligono (archi approssimati): polilinee chiuse, cerchi,
// ellissi e spline chiuse. Vuoto se l'oggetto non è chiuso. Con `force` accetta anche oggetti aperti.
std::vector<Vec2> closedOutline(const Entity& e, bool force = false);

} // namespace cartesio

namespace cartesio {

// Allunga una linea o un arco, dall'estremo più vicino a `pick`, fino al primo dei `boundaries`
// incontrato. nullptr se non incontra nulla.
std::unique_ptr<Entity> extendEntity(const Entity& target, const std::vector<const Entity*>& boundaries, Vec2 pick);

// Raccordo fra due linee (FILLET): le linee accorciate o allungate e, con raggio > 0, l'arco.
struct FilletResult {
    std::unique_ptr<LineEntity> first;
    std::unique_ptr<LineEntity> second;
    std::unique_ptr<ArcEntity> arc;
};
std::optional<FilletResult> filletLines(const LineEntity& a, Vec2 pickA, const LineEntity& b, Vec2 pickB, double radius);

// Modifica con le maniglie: copia dell'entità con la maniglia `index` (indice in snapPoints()) portata in `to`.
std::unique_ptr<Entity> gripEdit(const Entity& e, std::size_t index, Vec2 to);

// Maniglie multifunzione (menu passando sopra la maniglia): polilinee (vertici e punti medi),
// linee e archi (estremi).
enum class GripAction { Stretch, AddVertex, RemoveVertex, ToArc, ToLine, Lengthen };
// Azioni disponibili sulla maniglia `index` (indice in snapPoints()); vuoto = solo stiramento, niente menu.
std::vector<GripAction> gripActions(const Entity& e, std::size_t index);
// Le azioni senza punto (RemoveVertex, ToLine) si applicano subito.
bool gripActionNeedsPoint(GripAction action);
// Risultato dell'azione portata al punto `to`; nullptr se non è possibile.
std::unique_ptr<Entity> gripApply(const Entity& e, std::size_t index, GripAction action, Vec2 to);

// CHAMFER: smusso fra due linee con le distanze d1 (sulla prima) e d2 (sulla seconda); 0 = spigolo vivo.
struct ChamferResult {
    std::unique_ptr<LineEntity> first;
    std::unique_ptr<LineEntity> second;
    std::unique_ptr<LineEntity> chamfer;   // nullptr se le distanze sono zero
};
std::optional<ChamferResult> chamferLines(const LineEntity& a, Vec2 pickA, const LineEntity& b, Vec2 pickB, double d1,
                                          double d2);

// BREAK: toglie il tratto fra p1 e p2 (con p1 == p2 spezza l'oggetto in due). Linee, archi, cerchi
// (si toglie il tratto antiorario da p1 a p2) e polilinee. nullopt se l'oggetto non si può spezzare.
std::optional<std::vector<std::unique_ptr<Entity>>> breakEntity(const Entity& e, Vec2 p1, Vec2 p2);

// JOIN: unisce linee allineate, archi dello stesso cerchio e catene di linee, archi e polilinee aperte
// che si toccano agli estremi. Restituisce i nuovi oggetti e, in `consumed`, gli id degli oggetti uniti.
std::vector<std::unique_ptr<Entity>> joinEntities(const std::vector<const Entity*>& entities, double tolerance,
                                                  std::vector<EntityId>& consumed);

// STRETCH: copia dell'oggetto con i punti dentro la finestra spostati di `delta` (gli oggetti interamente
// dentro si spostano). nullptr se l'oggetto non cambia.
std::unique_ptr<Entity> stretchEntity(const Entity& e, const BBox& window, Vec2 delta);

// Dopo una specchiatura i testi restano leggibili (MIRRTEXT = 0).
void keepTextReadable(Entity& e);

// SPLINE: B-spline (grado 3, o meno con pochi punti) che passa per i punti di `fit` (punti di controllo e nodi
// calcolati, come li vogliono i DXF). Con `closed` la curva torna al primo punto. False se i punti non bastano.
bool interpolateSpline(SplineEntity& s);

} // namespace cartesio
