// Funzioni chiamate da CartesioCAD (app/CanaloEngine) sul codice di CanaloCAD caricato in canalo-engine.js.
// Lavorano come la pagina: lo stato della sezione è la variabile globale `state`, normalizzata come all'apertura
// di un progetto (normalizeSection) e ricalcolata (recalculateAll). Dati in ingresso e in uscita in JSON.
var __cartesio = {
  // Stato iniziale di una sezione nuova (quello della pagina appena aperta).
  defaultState: function () {
    return JSON.stringify(DEFAULT_SECTION_STATE);
  },
  // Database: cavi, tipologie di canalina, staffaggi.
  database: function () {
    return JSON.stringify({ cables: CABLE_CATALOG, series: BRAND_SERIES.TIPOLOGIE, supports: SUPPORT_SPECS });
  },
  // Stato normalizzato e ricalcolato, con i risultati (metrics) come li mostra la pagina.
  calc: function (json) {
    state = normalizeSection(JSON.parse(json));
    recalculateAll();
    var out = JSON.parse(JSON.stringify(state));
    delete out.metrics;
    return JSON.stringify({ state: out, metrics: metrics });
  },
  // Aggiunge cavi del database come il pulsante "+" della pagina (addCableFromDbRow: sigla, fasi L1/L2/L3...,
  // terne a trifoglio, posizione iniziale); quantità e trifoglio al posto dei campi della finestra.
  addCable: function (json, id, qty, trefoil) {
    state = normalizeSection(JSON.parse(json));
    var getById = document.getElementById;
    document.getElementById = function (elId) {
      if (elId === "dbQty_" + id) return { value: String(qty) };
      if (elId === "dbTrf_" + id) return { checked: !!trefoil };
      return null;
    };
    try {
      addCableFromDbRow(id);
    } finally {
      document.getElementById = getById;
    }
    return __cartesio.calc(JSON.stringify(state));
  },
  // Larghezza della canalina per la riserva voluta (%), come "Dimensiona" della pagina (autoSizeTray).
  autoSize: function (json, reservePct) {
    state = normalizeSection(JSON.parse(json));
    var getById = document.getElementById;
    document.getElementById = function (elId) {
      return elId === "inpSpareReserve" ? { value: String(reservePct) } : getById(elId);
    };
    try {
      autoSizeTray();
    } finally {
      document.getElementById = getById;
    }
    return __cartesio.calc(JSON.stringify(state));
  },
  // Importazione di un disegno DXF (sezione con cerchi, bordo e sigle) come "Importa DWG/DXF" della pagina.
  importDxf: function (json, text, fileName) {
    state = normalizeSection(JSON.parse(json));
    parseDXFTextContent(text, fileName);
    return __cartesio.calc(JSON.stringify(state));
  },
  // Un file DXF come "Importa DWG/DXF" della pagina (processSingleCADFile): i byte del file (un carattere per byte)
  // passano da extractRawAscii e parseDXFTextContent. La sezione nuova prende il nome del file, come una scheda
  // nuova della pagina.
  importFileBytes: function (json, bytes, fileName) {
    state = normalizeSection(JSON.parse(json));
    var u = new Uint8Array(bytes.length);
    for (var i = 0; i < bytes.length; i++) u[i] = bytes.charCodeAt(i) & 0xff;
    parseDXFTextContent(extractRawAscii(u), fileName);
    return __cartesio.calc(JSON.stringify(state));
  },
  // Cavi propri (libreria personale di CartesioCAD, progetto dell'impianto) nel database della pagina.
  setOwnCables: function (json) {
    var own = JSON.parse(json);
    for (var i = CABLE_CATALOG.length - 1; i >= 0; i--)
      if (CABLE_CATALOG[i].__own) CABLE_CATALOG.splice(i, 1);
    own.forEach(function (c) { c.__own = true; CABLE_CATALOG.push(c); });
    return String(CABLE_CATALOG.length);
  },
  // Scala della tavola e dei testi come le scelte della pagina (la scala va anche nel cartiglio).
  setScales: function (json, scale, textScale) {
    state = normalizeSection(JSON.parse(json));
    if (scale) onScaleChange(scale);
    if (textScale) onTextScaleChange(String(textScale));
    return __cartesio.calc(JSON.stringify(state));
  },
  // Gli oggetti della tavola sui layer indicati (la sezione), come li scrive "Esporta DXF" della pagina ma senza
  // il testo DXF: generateDXFStringForCurrentState gira con uno scrittore che raccoglie le stesse entità (stesse
  // coordinate in scala 1:1, colori, testi, riempimenti). Più i layer della pagina (CAD_LAYERS_SPEC).
  sectionEntities: function (json, layersJson) {
    state = normalizeSection(JSON.parse(json));
    state.scale = "1:1";
    recalculateAll();
    var keep = {};
    JSON.parse(layersJson).forEach(function (l) { keep[l] = true; });
    var out = [];
    var finite = function (vals) { return vals.every(function (v) { return Number.isFinite(+v); }); };
    var unicode = function (s) {
      return String(s).replace(/\\U\+([0-9A-Fa-f]{4})/g, function (m, h) { return String.fromCharCode(parseInt(h, 16)); });
    };
    var real = createDxfWriter;
    createDxfWriter = function (r2000, scale) {
      var S = function (v) { return v * scale; };
      return {
        line: function (lay, x1, y1, x2, y2, aci, tc) {
          if (keep[lay] && finite([x1, y1, x2, y2])) out.push({ t: "line", l: lay, p: [S(x1), S(y1), S(x2), S(y2)], c: aci, tc: tc });
        },
        circle: function (lay, cx, cy, r, aci, tc) {
          if (keep[lay] && finite([cx, cy, r]) && r > 0) out.push({ t: "circle", l: lay, p: [S(cx), S(cy), S(r)], c: aci, tc: tc });
        },
        text: function (lay, x, y, h, str, style, hAlign, aci, tc) {
          var txt = dxfText(str);
          if (keep[lay] && txt.trim() && finite([x, y, h]))
            out.push({ t: "text", l: lay, p: [S(x), S(y), Math.max(0.0001, S(h))], s: unicode(txt), st: style, a: hAlign || 0, c: aci, tc: tc });
        },
        fillQuad: function (lay, x1, y1, x2, y2, x3, y3, x4, y4, aci, tc) {
          if (finite([x1, y1, x2, y2, x3, y3, x4, y4])) this._hatch(lay, [[x1, y1], [x2, y2], [x3, y3], [x4, y4]], false, aci, tc);
        },
        fillDisk: function (lay, cx, cy, r, aci, tc) {
          if (finite([cx, cy, r]) && r > 0) this._hatch(lay, [[cx - r, cy, 1], [cx + r, cy, 1]], true, aci, tc);
        },
        _hatch: function (lay, verts, withBulge, aci, tc) {
          if (keep[lay])
            out.push({ t: "hatch", l: lay, v: verts.map(function (v) { return [S(v[0]), S(v[1]), withBulge ? (v[2] || 0) : 0]; }), c: aci, tc: tc });
        },
        finish: function () { return ""; }
      };
    };
    try {
      generateDXFStringForCurrentState(state.title);
    } finally {
      createDxfWriter = real;
    }
    return JSON.stringify({ entities: out, layers: CAD_LAYERS_SPEC });
  },
  // Diagnosi: lo stato corrente della pagina.
  currentState: function () { return JSON.stringify(state); },
  // Tavola DXF della sezione: lo stesso testo che la pagina scarica con "Esporta DXF".
  dxf: function (json, title) {
    state = normalizeSection(JSON.parse(json));
    recalculateAll();
    return generateDXFStringForCurrentState(title);
  }
};
