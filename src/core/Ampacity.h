#pragma once

// Portate dei cavi in rame secondo le tabelle IEC 60364-5-52 (da cui derivano le CEI-UNEL 35024/1): isolante PVC
// (70 °C) o EPR/XLPE (90 °C), metodi di posa A1, A2, B1, B2, C ed E, 2 o 3 conduttori caricati, temperatura ambiente
// di riferimento 30 °C. Valori da confrontare con la norma: la portata di un circuito si può sempre imporre a mano.

#include <optional>
#include <string>

namespace cartesio::plant {

enum class Insulation { PVC, EPR };
// A1 unipolari in tubo in parete isolante; A2 multipolare in tubo in parete isolante; B1 unipolari in tubo o canale
// a vista; B2 multipolare in tubo o canale a vista; C su parete o passerella non forata; E multipolare in aria libera
// (passerella forata, a filo, mensole).
enum class Method { A1, A2, B1, B2, C, E };

const char* methodName(Method m);
// Isolante dalla sigla del cavo: FG16, FG18, FTG18, FG17, FG7 (gomma HEPR/EPR, 90 °C) oppure PVC (FS17, N07V-K,
// H07V, FROR...). Se non si capisce: EPR.
Insulation insulationOf(const std::string& cableType);
// Portata di tabella (A), oppure nullopt se la sezione o il metodo non sono in tabella.
std::optional<double> tableAmpacity(Insulation insulation, Method method, int loadedConductors, double section);
// Fattore di correzione per la temperatura ambiente (aria), 30 °C = 1.
double temperatureFactor(Insulation insulation, double ambientC);
// Raggruppamento in un solo strato su passerella forata (IEC 60364-5-52 tab. B.52.17, riga 4): 1,00; 0,88; 0,82;
// 0,77; 0,75; 0,73; 0,73; 0,72; 0,72 da 9 circuiti in su.
double trayGroupingFactor(int circuits);

} // namespace cartesio::plant
