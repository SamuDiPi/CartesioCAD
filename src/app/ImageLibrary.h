#pragma once

// Immagini raster dei disegni (IMAGE): ricerca del file e cache delle immagini caricate.
//
// Il file si cerca: nel percorso salvato nel disegno; se relativo, a partire dalla cartella del disegno; col solo
// nome nella cartella del disegno e nelle sue sottocartelle dirette; nelle cartelle indicate nelle Opzioni.

#include <QImage>
#include <QString>

#include <map>
#include <memory>
#include <string>

namespace cartesio {

class Document;
class ImageEntity;

class ImageLibrary {
public:
    static ImageLibrary& instance();

    // Percorso del file trovato, oppure vuoto.
    QString find(const Document& doc, const std::string& path);
    // Stessa ricerca senza cache (anche per i riferimenti esterni, da qualunque thread): `drawingFile` è il
    // disegno che contiene il riferimento.
    static QString locate(const QString& drawingFile, const std::string& path);
    // Sorgente dell'immagine di un oggetto: il file trovato, oppure "ole:..." per un oggetto OLE incorporato.
    QString source(const Document& doc, const ImageEntity& image);
    // Immagine caricata (ridotta se enorme), oppure nulla se il file manca o non si legge.
    QImage image(const QString& file);
    // Dopo un cambio delle cartelle nelle Opzioni o per ricaricare i file cambiati.
    void reset();

private:
    std::map<QString, QString> found_;    // cartella del disegno | percorso -> file
    std::map<QString, QImage> images_;
    std::map<QString, std::shared_ptr<const std::string>> ole_;   // dati degli oggetti OLE per sorgente
};

} // namespace cartesio
