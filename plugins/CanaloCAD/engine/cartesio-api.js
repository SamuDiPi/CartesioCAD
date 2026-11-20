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
  // Cavi propri (libreria personale di CartesioCAD, progetto dell'impianto) nel database della pagina.
  setOwnCables: function (json) {
    var own = JSON.parse(json);
    for (var i = CABLE_CATALOG.length - 1; i >= 0; i--)
      if (CABLE_CATALOG[i].__own) CABLE_CATALOG.splice(i, 1);
    own.forEach(function (c) { c.__own = true; CABLE_CATALOG.push(c); });
    return String(CABLE_CATALOG.length);
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
