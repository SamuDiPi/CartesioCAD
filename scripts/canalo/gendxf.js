// Casi di prova della tavola DXF di CanaloCAD (tests/canalo_dxf): per ogni caso
//   <nome>.json  stato della sezione, costruito con le funzioni della pagina (motore di plugins/CanaloCAD/engine)
//   <nome>.dxf   tavola esportata dalla pagina vera (plugins/CanaloCAD/Canalo.html) in Microsoft Edge senza finestra
// Il test cartesio_canalo_engine_tests controlla che CartesioCAD produca gli stessi byte.
// Uso: node scripts/canalo/gendxf.js   (dopo scripts/update-canalo.sh)
const fs = require("fs");
const os = require("os");
const path = require("path");
const vm = require("vm");
const { execFileSync } = require("child_process");

const root = path.join(__dirname, "..", "..");
const engineDir = path.join(root, "plugins", "CanaloCAD", "engine");
const outDir = path.join(root, "tests", "canalo_dxf");
const edge = process.env.EDGE || "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe";

// Casi: dati della sezione (sovrascrivono quelli iniziali) e cavi dal database [id, quantità, trifoglio].
const CASES = {
  base: {
    set: {},
    add: [["FG16_4G10", 2], ["FG16_5G6", 1], ["FG16_3G2.5", 3], ["FTG18_3G2.5", 1], ["DATA_CAT6", 4]],
  },
  setto_staffe: {
    set: { width: 300, height: 100, septums: 1, support: "wall_med", spanM: 2, cover: "clamped", trayType: "solid",
           seismic: "cable_9m", reiCount: 2, bends90: 3, tees: 1, reducers: 1, lengthM: 45 },
    add: [["FG16_1X95", 4], ["FG16_4G16", 2], ["FTG18_4G4", 1], ["OH2_4G1.5", 2], ["FO_12_OS2", 1], ["BUS_KNX", 2]],
  },
  filo_trifoglio: {
    set: { width: 400, height: 75, trayType: "mesh", support: "ceiling_trap", spanM: 1.5, theme: "light", textScale: 1.2,
           clearance: 0, showIso3D: true, showLoadChart: true },
    add: [["FG16_1X150", 1, true], ["FG16_1X150", 1, true], ["FG16_3G1.5", 6], ["LIYCY_2X0.75", 3]],
  },
  scala_minima: {
    set: { width: 200, height: 60, trayType: "ladder", layoutMode: "minimal", showBOM: false, showCartiglio: true,
           showLoadChart: false, showIso3D: false, scale: "1:2", earth: "none", cover: "none" },
    add: [["FG16_2X1.5", 5], ["FG16_1X25", 4]],
  },
};
const DATE = "20/11/2026";

function loadEngine() {
  const ctx = { console, __hostMeasureText: (s) => s.length * 55 };   // le misure non contano per lo stato
  vm.createContext(ctx);
  for (const f of ["host.js", "canalo-engine.js", "cartesio-api.js"])
    vm.runInContext(fs.readFileSync(path.join(engineDir, f), "utf8"), ctx, { filename: f });
  return ctx.__cartesio;
}

function buildState(api, spec) {
  let st = JSON.parse(api.defaultState());
  Object.assign(st, spec.set);
  st.tb = Object.assign({}, st.tb, { date: DATE });
  st.title = spec.title || st.title;
  for (const [id, qty, trefoil] of spec.add) {
    const out = JSON.parse(api.addCable(JSON.stringify(st), id, qty || 1, !!trefoil));
    st = out.state;
  }
  // Senza i gruppi a trifoglio con l'ora: lo stato deve essere lo stesso a ogni generazione.
  let g = 0;
  const groups = {};
  st.cables.forEach(c => { if (c.trefoilGroupId) c.trefoilGroupId = groups[c.trefoilGroupId] || (groups[c.trefoilGroupId] = "TRF_" + (++g)); });
  return st;
}

function edgeDxf(state, title) {
  const html = fs.readFileSync(path.join(root, "plugins", "CanaloCAD", "Canalo.html"), "utf8");
  const script = `<script>
window.addEventListener("load", function () { setTimeout(function () {
  var out = document.createElement("pre");
  var fail = function (e) { out.id = "dxferr"; out.textContent = String(e && e.stack || e); document.body.appendChild(out); };
  try {
    state = normalizeSection(${JSON.stringify(state)});
    recalculateAll();
    var dxf = generateDXFStringForCurrentState(${JSON.stringify(title)});
    // La stessa tavola riletta come file con "Importa DWG/DXF" partendo da una sezione nuova:
    // misure, sigle e diametri.
    state = normalizeSection(DEFAULT_SECTION_STATE);
    // Come processSingleCADFile con un file .dxf: i byte del file (dxfToBlob) passano da extractRawAscii e
    // parseDXFTextContent (qui senza la lettura asincrona del file).
    var u = new Uint8Array(dxf.length);
    for (var i = 0; i < dxf.length; i++) { var ch = dxf.charCodeAt(i); u[i] = ch <= 0xff ? ch : 0x3f; }
    parseDXFTextContent(extractRawAscii(u), ${JSON.stringify(title + ".dxf")});
    recalculateAll();
    var imported = [state.width + "x" + state.height].concat(state.cables.map(function (c) { return c.code + " | " + c.d; })).join("\\n");
    out.id = "dxfout"; out.textContent = btoa(unescape(encodeURIComponent(JSON.stringify({ dxf: dxf, imported: imported }))));
    document.body.appendChild(out);
  } catch (e) { fail(e); }
}, 300); });
</script>`;
  const page = path.join(os.tmpdir(), "canalo_gendxf.html");
  fs.writeFileSync(page, html + script);   // in fondo: "</body>" compare anche dentro il codice della pagina
  const dom = execFileSync(edge, ["--headless=new", "--disable-gpu", "--virtual-time-budget=8000", "--dump-dom",
                                  "file:///" + page.replace(/\\/g, "/")], { maxBuffer: 64 << 20 }).toString("utf8");
  fs.unlinkSync(page);
  const err = dom.match(/<pre id="dxferr">([^<]*)<\/pre>/);
  if (err) throw new Error(err[1]);
  const m = dom.match(/<pre id="dxfout">([^<]*)<\/pre>/);
  if (!m) throw new Error("Edge: nessuna tavola");
  return JSON.parse(decodeURIComponent(escape(Buffer.from(m[1], "base64").toString("binary"))));
}

const api = loadEngine();
fs.mkdirSync(outDir, { recursive: true });
for (const [name, spec] of Object.entries(CASES)) {
  const state = buildState(api, spec);
  fs.writeFileSync(path.join(outDir, name + ".json"), JSON.stringify(state, null, 1));
  const { dxf, imported } = edgeDxf(state, name);
  // Come dxfToBlob della pagina: un byte per carattere.
  fs.writeFileSync(path.join(outDir, name + ".dxf"), Buffer.from(dxf.split("").map(ch => ch.charCodeAt(0) <= 0xff ? ch.charCodeAt(0) : 0x3f)));
  // La tavola riletta con l'importazione della pagina (misure e cavi).
  fs.writeFileSync(path.join(outDir, name + ".import.txt"), imported + "\n");
  console.log(`${name}: ${state.cables.length} cavi, tavola ${dxf.length} caratteri`);
}
