#pragma once

// Testi: stili di testo (tabella STYLE) con font SHX o TrueType, fattore di
// larghezza, obliquità e allineamenti. Lo stesso impaginato serve al disegno su GPU, a QPainter e alla stampa.
//
// I font SHX (romans.shx, simplex.shx, isocp.shx...) sono letti dalle cartelle indicate nelle Opzioni e dalla
// cartella "fonts" accanto al programma, e trasformati in tratti; CartesioCAD non ne distribuisce; i TrueType sono cercati per famiglia o per
// nome di file (Windows, cartella dei font).

#include "core/Entity.h"
#include "core/Layer.h"

#include <QFont>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QTransform>

#include <map>
#include <memory>
#include <vector>

namespace cartesio {

class Document;

// Un carattere di un font SHX: tratti (spezzate) in unità del font, Y verso l'alto, e avanzamento.
struct ShxGlyph {
    std::vector<std::vector<QPointF>> strokes;
    double advance = 0.0;
};

// Font a forma (shapes 1.0/1.1, unifont 1.0, bigfont 1.0).
class ShxFont {
public:
    static std::shared_ptr<ShxFont> load(const QString& path, QString* error = nullptr);
    // Altezza delle maiuscole e discesa, in unità del font.
    double above() const { return above_; }
    double below() const { return below_; }
    bool unicode() const { return unifont_; }
    // Carattere già compilato (nullptr se il font non lo ha).
    const ShxGlyph* glyph(char32_t code) const;
    QString name() const { return name_; }

private:
    bool parse(const QByteArray& data);
    void run(const QByteArray& def, std::size_t start, struct ShxPen& pen, int depth) const;

    QString name_;
    double above_ = 1.0;
    double below_ = 0.0;
    bool unifont_ = false;
    bool bigfont_ = false;
    std::map<unsigned, QByteArray> shapes_;   // numero -> definizione (dopo il nome)
    mutable std::map<char32_t, std::unique_ptr<ShxGlyph>> cache_;
};

// Font da usare per uno stile: SHX (se trovato) oppure TrueType.
struct ResolvedFont {
    std::shared_ptr<ShxFont> shx;
    QFont font;   // TrueType, dimensione in pixel 100
};

class FontLibrary {
public:
    static FontLibrary& instance();
    ResolvedFont resolve(const TextStyle& style);
    // Percorso di un file di font (SHX o TTF) nelle cartelle note, oppure vuoto.
    QString findFile(const QString& name) const;
    // Da chiamare se cambiano le cartelle dei font nelle Opzioni.
    void reset();

    // Cartelle in cui si cercano i font: Opzioni, "fonts" accanto al programma, altri programmi (se chiesto), Windows.
    std::vector<QString> searchFolders() const { return folders(); }

private:
    FontLibrary();
    std::vector<QString> folders() const;
    std::map<QString, ResolvedFont> cache_;
    std::map<QString, std::shared_ptr<ShxFont>> shx_;
    std::map<QString, QString> ttfFamilies_;   // file -> famiglia
};

// Una riga di testo impaginata nelle sue coordinate: linea di base su y = 0, da x = 0 ad `advance`,
// maiuscole alte `height`, Y verso l'alto. `toEntity` la porta nelle coordinate dell'oggetto (posizione,
// rotazione, obliquità, larghezza, allineamento).
struct PlacedText {
    QString text;
    double height = 0.0;
    double advance = 0.0;
    QTransform toEntity;
    std::shared_ptr<ShxFont> shx;       // tratti da `shx` se c'è...
    QFont font;                         // ...altrimenti testo TrueType (pixel 100)
    double fontScale = 1.0;             // unità del testo per pixel del font
    int aci = 0;                        // colore del pezzo di testo (\C dei testi multilinea), 0 = dell'oggetto
    long long rgb = -1;                 // colore RGB (\c), -1 = nessuno
    // Sottolineato, sopralineato, barrato: segmenti già nelle coordinate dell'oggetto.
    std::vector<std::pair<QPointF, QPointF>> decorations;

    // Tratti SHX già nelle coordinate dell'oggetto.
    std::vector<std::vector<QPointF>> strokes() const;
    // Trattino al posto del testo quando è troppo piccolo sullo schermo (coordinate dell'oggetto).
    std::pair<QPointF, QPointF> dash() const;
    // Trasformazione dai pixel del font (Y verso il basso, linea di base a y = 0) alle coordinate dell'oggetto.
    QTransform fontToEntity() const;
};

std::vector<PlacedText> layoutText(const Document& doc, const TextEntity& t);
// Testo multilinea con i codici di formattazione (\H altezza, \W larghezza, \Q obliquità, \f e \F font,
// \C e \c colore, \S frazioni, \P a capo, gruppi { }): un pezzo per ogni tratto con lo stesso formato, a capo
// automatico sulla larghezza con le misure vere dei font.
std::vector<PlacedText> layoutMText(const Document& doc, const MTextEntity& m);
// Testo leggibile di un testo multilinea: senza i codici di formattazione ({\W0.7;...}, \P, \~...), su una riga.
QString plainMText(const std::string& text);

// Formato di un pezzo di testo multilinea (cambia con i codici e si ripristina alla fine dei gruppi { }).
struct MTextFormat {
    double height = 1.0;        // altezza assoluta (unità del disegno)
    double width = 1.0;         // fattore di larghezza
    double oblique = 0.0;       // radianti
    QString family;             // \f famiglia TrueType
    QString shxFile;            // \F file SHX
    bool bold = false;
    bool italic = false;
    int aci = 0;
    long long rgb = -1;
    bool underline = false;     // \L ... \l
    bool overline = false;      // \O ... \o
    bool strike = false;        // \K ... \k
    // Allineamento del paragrafo (\pxql; c r j d): -1 = quello del punto d'inserimento, 0 sinistra, 1 centro,
    // 2 destra, 3 giustificato, 4 distribuito (gli ultimi due disegnati a sinistra).
    int align = -1;

    bool sameAs(const MTextFormat& o) const
    {
        return height == o.height && width == o.width && oblique == o.oblique && family == o.family
               && shxFile == o.shxFile && bold == o.bold && italic == o.italic && aci == o.aci && rgb == o.rgb
               && underline == o.underline && overline == o.overline && strike == o.strike && align == o.align;
    }
};

struct MTextRun {
    MTextFormat format;
    QString text;
};

using MTextParagraph = std::vector<MTextRun>;

// Legge i codici di formattazione: i paragrafi (\P) con i loro pezzi di testo.
std::vector<MTextParagraph> parseMText(const QString& s, const MTextFormat& initial);
// Formato di partenza di un testo multilinea (altezza dell'oggetto, larghezza e obliquità dello stile).
MTextFormat mtextInitialFormat(const Document& doc, const MTextEntity& m);

// Dove stanno le lettere, per modificare il testo direttamente nel disegno (coordinate dell'oggetto). Gli
// indici contano i caratteri come li dà parseMText; l'a capo fra due paragrafi vale un carattere.
struct TextCaretGeometry {
    struct Stop {
        int index = 0;          // il cursore sta prima del carattere `index`
        QPointF bottom, top;    // segmento del cursore
        int line = 0;
    };
    std::vector<Stop> stops;          // in ordine di riga e di posizione
    std::vector<QPolygonF> boxes;     // riquadro di ogni carattere (vuoto per gli a capo)
    QPolygonF frame;                  // riquadro di tutto il testo (cornice della modifica)
};
TextCaretGeometry caretGeometry(const Document& doc, const TextEntity& t);
TextCaretGeometry caretGeometry(const Document& doc, const MTextEntity& m);


} // namespace cartesio
