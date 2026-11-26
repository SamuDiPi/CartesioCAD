#pragma once

// Fronte quadro: blocchi generici di CartesioCAD, disegnati qui (parametrici, nessun materiale di terzi) per ogni
// tipo di apparecchio, piastra, morsettiera, sistema di sbarre e carpenteria. Valgono quando il catalogo dell'utente
// non ha una voce adatta, così ogni simbolo dello schema finisce su un blocco. Le misure sono tipiche: servono a
// impaginare il quadro e vanno confermate sul catalogo del costruttore scelto.

#include "core/Document.h"
#include "core/Switchboard.h"

#include <string>
#include <vector>

namespace cartesio {

// Voci del catalogo con i blocchi generici (DeviceType::generic): tipo, poli, intervallo di corrente, misure e nome
// del blocco (FQG_...). Coprono tutti i tipi di apparecchio, gli accessori su guida, la morsettiera, le piastre
// modulari e cieche (colonne da 600 e 800 mm) e i sistemi di sbarre verticali.
std::vector<DeviceType> genericDeviceTypes();

// Nome del blocco generico della carpenteria larga `width` e alta `height` (mm), per esempio FQG_ENC_600x2000.
std::string genericEnclosureBlock(double width, double height);

// Crea in `doc` il blocco generico `name` (FQG_...) se non c'è già. Falso se il nome non è un blocco generico.
bool addGenericBlock(Document& doc, const std::string& name);

} // namespace cartesio
