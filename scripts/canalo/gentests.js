// Genera tests/canalo_expected.inc: casi di prova calcolati con la pagina CanaloCAD, da confrontare con il C++.
const { load } = require("./load.js");

// Ogni cavo: [id catalogo, opzioni]
const scenarios = [
  { name: "base", tray: {}, cables: [["FG16_4G10"], ["FG16_4G10"], ["FG16_5G6"], ["FG16_3G2.5"]] },
  { name: "trifoglio_dati", tray: { support: "wall_med", spanM: 1.5, bends90: 2 },
    cables: [["FG16_1X95", { trefoil: true, g: "T1" }], ["FG16_1X95", { trefoil: true, g: "T1" }], ["FG16_1X95", { trefoil: true, g: "T1" }],
             ["FG16_5G6"], ["FG16_5G6"], ["DATA_CAT6"], ["DATA_CAT6"], ["DATA_CAT6"], ["DATA_CAT6"]] },
  { name: "setto_segnali", tray: { septums: 1, support: "ceiling_trap", spanM: 2.0, tees: 1, reducers: 2 },
    cables: [["FG16_4G16"], ["FG16_4G16"], ["FG16_3G4"], ["LIYCY_2X0.75"], ["BUS_KNX"], ["DATA_CAT6A"]] },
  { name: "due_setti_fuoco", tray: { width: 300, height: 100, thk: 2.0, septums: 2, trayType: "solid", cover: "none", support: "wall_heavy",
             spanM: 2.5, seismic: "cable_9m", reiCount: 2, earth: "none", brand: "OBO", seriesId: "MKS_MAGIC", lengthM: 47, pdfLoaded: true },
    cables: [["FG16_4G25", { ib: 90 }], ["FG16_4G25", { ib: 80 }], ["FG16_5G10", { ib: 40 }], ["FTG18_4G10", { ib: 50 }], ["FTG18_3G2.5", { ib: 10 }],
             ["IRAI_LOOP"], ["EVAC_100V"], ["FO_8_OM4"]] },
  { name: "setto_solo_bt", tray: { septums: 1, cover: "clamped", trayType: "mesh", support: "floor_omega", spanM: 3.0 },
    cables: [["FG16_1X50", { x: 20 }], ["FG16_1X50", { x: 40 }], ["FG16_4G4", { x: 150 }], ["FG16_4G4", { x: 170 }], ["H07RN_4G4", { x: 180 }]] },
  { name: "come_disegnati", tray: { packing: "input", width: 100, height: 60 },
    cables: [["FG16_5G16", { ox: 20, oy: 13 }], ["FG16_5G16", { ox: 25, oy: 14 }], ["FG16_3G6", { ox: 70, oy: 30 }], ["OH2_7G1.5", { ox: 90, oy: 8 }]] },
  { name: "pieno", tray: { width: 100, height: 50, bends90: 4, lengthM: 80, support: "ceiling_single", spanM: 1.0 },
    cables: Array.from({ length: 14 }, (_, i) => [i % 2 ? "FG16_4G6" : "FG16_1X25"]) },
];

const api = load();
const cpp = s => JSON.stringify(String(s));
const num = v => (Number.isFinite(v) ? (String(v).includes(".") || String(v).includes("e") ? String(v) : String(v) + ".0") : "0.0");
let out = "// Generato da scripts/canalo/gentests.js con i calcoli della pagina CanaloCAD: non modificare a mano.\n";
out += "// clang-format off\n";
let idx = 0;
for (const sc of scenarios) {
  const st = api.state;
  // valori iniziali della pagina
  Object.assign(st, { width: 200, height: 75, thk: 1.5, lengthM: 30, bends90: 0, tees: 0, reducers: 0, seismic: "none", trayType: "perforated",
                      packing: "gravity", cover: "snap", septums: 0, septumPositions: [], earth: "gv16", reiCount: 0, support: "none", spanM: 1.5,
                      brand: "LEGRAND", seriesId: "P31_ASO", pdfLoaded: false }, sc.tray);
  st.cables = sc.cables.map(([id, o = {}], i) => {
    const it = api.CABLE_CATALOG.find(c => c.id === id);
    return { id: i + 1, code: `${it.code}-${i + 1}`, d: it.d, w: it.w, sec: it.sec, cores: it.cores, iz0: it.iz0, ib: o.ib || 0, bendK: it.bendK,
             trefoil: !!o.trefoil, trefoilGroupId: o.g || null, origX: o.ox ?? 18, origY: o.oy ?? it.d / 2, x: o.x ?? 18, y: it.d / 2,
             desc: it.desc, type: it.type, form: it.form, sys: it.sys };
  });
  api.recalculateAll();
  const m = api.metrics;
  out += `void canaloCase${idx}()\n{\n    canalo::Tray t;\n`;
  for (const [k, v] of Object.entries(sc.tray)) {
    if (typeof v === "string") out += `    t.${k} = ${cpp(v)};\n`;
    else if (typeof v === "boolean") out += `    t.${k} = ${v};\n`;
    else out += `    t.${k} = ${["septums", "bends90", "tees", "reducers", "reiCount"].includes(k) ? v : num(v)};\n`;
  }
  sc.cables.forEach(([id, o = {}], i) => {
    out += `    { auto c = canalo::cableFrom(*canalo::catalogItem(${cpp(id)})); c.code += "-${i + 1}";`;
    if (o.trefoil) out += ` c.trefoil = true; c.trefoilGroup = ${cpp(o.g)};`;
    if (o.ib) out += ` c.ib = ${num(o.ib)};`;
    if (o.x != null) out += ` c.x = ${num(o.x)};`;
    out += ` c.origX = ${num(o.ox ?? 18)}; c.origY = ${o.oy != null ? num(o.oy) : "c.d / 2.0"}; t.cables.push_back(c); }\n`;
  });
  out += `    const canalo::Metrics m = canalo::recalculate(t);\n`;
  const checks = ["fillPct", "sparePct", "kr", "nLayers", "totalKgM", "qMaxKgM", "pointLoad", "maxPoint", "loadPct", "deflectionMm", "maxDeflectionMm",
                  "pullDaN", "maxPullDaN", "maxBendR", "fitR", "jouleWm", "maxDv"];
  for (const k of checks) out += `    CHECK(approx(m.${k}, ${num(m[k])}, 1e-9));\n`;
  out += `    CHECK(m.segConform == ${m.segConform});\n`;
  out += `    CHECK(m.segDetail == ${cpp(m.segDetail)});\n`;
  out += `    CHECK(m.critCode == ${cpp(m.critCode)});\n`;
  out += `    CHECK(m.deratingFailCode == ${cpp(m.deratingFailCode || "")});\n`;
  st.cables.forEach((c, i) => {
    out += `    CHECK(approx(t.cables[${i}].x, ${num(c.x)}, 1e-9) && approx(t.cables[${i}].y, ${num(c.y)}, 1e-9));\n`;
  });
  out += `    CHECK(m.bom.size() == ${m.bom.length});\n`;
  m.bom.forEach((b, i) => {
    out += `    CHECK(m.bom.size() > ${i} && m.bom[${i}].code == ${cpp(b.code)} && m.bom[${i}].desc == ${cpp(b.desc)} && approx(m.bom[${i}].qty, ${num(b.qty)}) && m.bom[${i}].note == ${cpp(b.note)});\n`;
  });
  out += "}\n\n";
  idx++;
}
out += "void testCanaloAgainstPage()\n{\n";
for (let i = 0; i < idx; i++) out += `    canaloCase${i}();\n`;
out += "}\n// clang-format on\n";
process.stdout.write(out);
