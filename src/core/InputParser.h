#pragma once

#include "core/Vec2.h"

#include <optional>
#include <string>
#include <string_view>

namespace cartesio {

// Interpretazione del testo digitato nella riga di comando, con la sintassi:
//   10,20      punto assoluto
//   @5,0       punto relativo all'ultimo punto
//   50<30      punto polare assoluto (distanza<angolo in gradi)
//   @50<30     punto polare relativo
//   @          l'ultimo punto
//   25         valore numerico (es. raggio, o distanza nella direzione del cursore)
//   LINE, L    parola: nome di comando o opzione (convertita in maiuscolo)
struct ParsedInput {
    enum class Kind { Empty, Point, Value, Word };

    Kind kind = Kind::Empty;
    Vec2 point;
    double value = 0.0;
    std::string word;
};

// Restituisce std::nullopt se il testo non è valido.
std::optional<ParsedInput> parseInput(std::string_view text, std::optional<Vec2> lastPoint);

} // namespace cartesio
