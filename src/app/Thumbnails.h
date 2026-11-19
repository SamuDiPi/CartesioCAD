#pragma once

#include <QImage>
#include <QString>

namespace cartesio {

// Miniature dei disegni per la pagina Inizia.
// Si usa, nell'ordine: la copia in cache se è più recente del file, l'anteprima salvata dentro
// DWG/DWT (BMP o PNG) e DXF (sezione THUMBNAILIMAGE), una copia in cache vecchia. I disegni salvati da
// CartesioCAD hanno la miniatura disegnata al salvataggio (storeThumbnail).
// La cache sta nei dati locali del programma (cartella "Miniature"), mai accanto ai disegni.

// Dimensione delle miniature salvate (rapporto della pagina Inizia).
inline constexpr int kThumbnailWidth = 352;
inline constexpr int kThumbnailHeight = 224;

// File PNG della miniatura di `path` (vuoto se non c'è). Legge il disegno: da chiamare fuori dal thread
// dell'interfaccia quando i file possono essere lenti (cartelle di rete).
QString thumbnailFile(const QString& path);
// Anteprima salvata dentro il disegno dal programma che l'ha scritto (immagine nulla se manca).
QImage embeddedPreview(const QString& path);
// Salva la miniatura di `path` nella cache (es. disegnata dalla vista al salvataggio).
void storeThumbnail(const QString& path, const QImage& image);

} // namespace cartesio
