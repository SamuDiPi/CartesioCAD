// Aggiunge a tests/canalo_expected.inc i casi di lettura della sezione disegnata, confrontati con l'importazione
// DXF della pagina (processRealWasmEntities).
const fs = require("fs");
const vm = require("vm");
const path = require("path");
const SRC = process.env.CANALO_SRC || path.join(process.env.USERPROFILE || process.env.HOME, "Documents", "Canalo", "src", "js");
const ctx = {
  console, Math, JSON, Object, Array, String, Number, parseFloat, parseInt, isNaN, Date, Set, Map,
  safeStorage: { get: () => null, set: () => {} },
  document: { getElementById: () => null, activeElement: null, title: "", querySelectorAll: () => [] },
  requestRender: () => {}, scheduleSectionSync: () => {}, drawScene: () => {}, fitAll: () => {}, updateLayoutUI: () => {},
  updateCableCalloutsUI: () => {}, APP_NAME: "CanaloCAD", currentSectionIdx: 0,
};
vm.createContext(ctx);
const code = ["01-data.js", "02-core.js", "02b-conduit.js", "04-import-cad.js", "05-import-pdf.js"].map(f => fs.readFileSync(path.join(SRC, f), "utf8")).join("\n")
  + "\n;globalThis.__api = { get state(){return state;}, processRealWasmEntities };";
vm.runInContext(code, ctx);
const api = ctx.__api;

const L = (x1, y1, x2, y2, layer) => ({ type: "LINE", layer, startPoint: { x: x1, y: y1 }, endPoint: { x: x2, y: y2 } });
const C = (x, y, r, layer = "Cavi") => ({ type: "CIRCLE", layer, center: { x, y }, radius: r });
const T = (s, x, y, layer = "Testi") => ({ type: "TEXT", layer, text: s, startPoint: { x, y } });
const X0 = 1000, Y0 = 500;
const scenarios = [
  { file: "sezione.dxf", ents: [
      L(X0, Y0, X0 + 303, Y0, "Canalina"), L(X0, Y0, X0, Y0 + 76.5, "Canalina"), L(X0 + 303, Y0, X0 + 303, Y0 + 76.5, "Canalina"),
      C(X0 + 20, Y0 + 10, 9.9), C(X0 + 41, Y0 + 10, 9.9), C(X0 + 62, Y0 + 10, 9.9), C(X0 + 83, Y0 + 10, 9.9),
      C(X0 + 120, Y0 + 8, 7.25), C(X0 + 120, Y0 + 8, 5.0),
      C(X0 + 160, Y0 + 7, 6.1), C(X0 + 200, Y0 + 4, 3.4), C(X0 + 240, Y0 + 5, 4.9),
      L(X0 + 20, Y0 + 19, X0 + 20, Y0 + 140, "Bandierine"), L(X0 + 20, Y0 + 140, X0 + 60, Y0 + 140, "Bandierine"),
      T("QGBT C-2 (4(1x95))", X0 + 62, Y0 + 139),
      L(X0 + 120, Y0 + 15, X0 + 120, Y0 + 120, "Bandierine"), L(X0 + 120, Y0 + 120, X0 + 150, Y0 + 120, "Bandierine"),
      T("QP1 C-3 Illuminazione corridoio", X0 + 152, Y0 + 119),
      T("IRAI C-4", X0 + 200, Y0 + 100), T("QE2-DX C-7", X0 + 240, Y0 + 90),
      T("CANALINA 75x300", X0, Y0 - 30, "Titoli"),
    ] },
  { file: "Passerella_400x100.dxf", ents: [
      C(50, 50, 12.5), C(80, 50, 12.5), C(110, 50, 8.4), C(140, 50, 8.4), C(170, 48, 3.5),
      T("LINEA FM C-12", 60, 120), T("TD1 C-1", 140, 130, "Bandierine"),
    ] },
];

const cpp = s => JSON.stringify(String(s));
const num = v => (String(v).includes(".") || String(v).includes("e") ? String(v) : String(v) + ".0");
let out = "\n// clang-format off\n";
scenarios.forEach((sc, idx) => {
  api.processRealWasmEntities(sc.ents, sc.file, "AC1032");
  const st = api.state;
  out += `void canaloSection${idx}()\n{\n    canalo::Drawn dr;\n    dr.fileName = ${cpp(sc.file)};\n`;
  for (const e of sc.ents) {
    if (e.type === "LINE") out += `    dr.segments.push_back({{${num(e.startPoint.x)}, ${num(e.startPoint.y)}}, {${num(e.endPoint.x)}, ${num(e.endPoint.y)}}, ${cpp(e.layer)}});\n`;
    else if (e.type === "CIRCLE") out += `    dr.circles.push_back({{${num(e.center.x)}, ${num(e.center.y)}}, ${num(e.radius)}, ${cpp(e.layer)}});\n`;
    else out += `    dr.texts.push_back({${cpp(e.text)}, {${num(e.startPoint.x)}, ${num(e.startPoint.y)}}, ${cpp(e.layer)}});\n`;
  }
  out += `    canalo::Tray t = canalo::readSection(dr);\n`;
  out += `    CHECK(approx(t.width, ${num(st.width)}) && approx(t.height, ${num(st.height)}));\n`;
  out += `    CHECK(t.cables.size() == ${st.cables.length});\n`;
  out += `    if (t.cables.size() != ${st.cables.length}) return;\n`;
  out += `    canalo::recalculate(t);\n`;
  st.cables.forEach((c, i) => {
    out += `    CHECK(t.cables[${i}].code == ${cpp(c.code)} && t.cables[${i}].form == ${cpp(c.form)} && t.cables[${i}].type == ${cpp(c.type)} && t.cables[${i}].cores == ${c.cores} && t.cables[${i}].sys == ${cpp(c.sys)});\n`;
    out += `    CHECK(approx(t.cables[${i}].d, ${num(c.d)}) && approx(t.cables[${i}].sec, ${num(c.sec)}) && approx(t.cables[${i}].x, ${num(c.x)}) && approx(t.cables[${i}].y, ${num(c.y)}));\n`;
  });
  const m = st.metrics;
  out += `    const canalo::Metrics m = canalo::recalculate(t);\n`;
  out += `    CHECK(approx(m.fillPct, ${num(m.fillPct)}) && approx(m.kr, ${num(m.kr)}) && approx(m.totalKgM, ${num(m.totalKgM)}) && approx(m.maxPullDaN, ${num(m.maxPullDaN)}));\n`;
  out += `    CHECK(m.segDetail == ${cpp(m.segDetail)});\n`;
  out += "}\n\n";
});
out += "void testCanaloSections()\n{\n";
scenarios.forEach((_, i) => (out += `    canaloSection${i}();\n`));
out += "}\n// clang-format on\n";
process.stdout.write(out);
