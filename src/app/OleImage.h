#pragma once

// Immagine di un oggetto OLE incorporato nei disegni (OLE2FRAME): loghi incollati da Paint, Word, Excel...
//
// I dati sono un'intestazione seguita da un file "compound" OLE (la struttura dei vecchi .doc): se
// ne leggono i flussi. Si usa, nell'ordine: l'immagine nativa (\1Ole10Native: BMP di Paint, o un file PNG/JPG/
// BMP/GIF di un pacchetto), il flusso CONTENTS, e infine la rappresentazione salvata dall'applicazione
// (\2OlePres000: DIB, WMF o EMF; i metafile si disegnano con la grafica di Windows).

#include <QImage>

#include <string>

namespace cartesio {

QImage decodeOleImage(const std::string& data);

} // namespace cartesio
