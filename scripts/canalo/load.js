// Carica i file di calcolo di CanaloCAD in un contesto con document/window finti.
const fs = require("fs");
const vm = require("vm");
const path = require("path");
const SRC = process.env.CANALO_SRC || path.join(process.env.USERPROFILE || process.env.HOME, "Documents", "Canalo", "src", "js");

function load() {
  const nullEl = null;
  const ctx = {
    console, Math, JSON, Object, Array, String, Number, parseFloat, parseInt, isNaN, Date, Set, Map,
    safeStorage: { get: () => null, set: () => {} },
    document: { getElementById: () => nullEl, activeElement: null, title: "" },
    requestRender: () => {}, scheduleSectionSync: () => {}, drawScene: () => {}, fitAll: () => {},
    APP_NAME: "CanaloCAD",
  };
  vm.createContext(ctx);
  const code = ["01-data.js", "02-core.js"].map(f => fs.readFileSync(path.join(SRC, f), "utf8")).join("\n")
    + "\n;globalThis.__api = { get state(){return state;}, set state(v){state=v;}, get metrics(){return metrics;}, CABLE_CATALOG, BRAND_SERIES, SUPPORT_SPECS, recalculateAll, calcAllowedLinearLoadKgM, calcElasticDeflectionMm, inferCableSpecsByDiameter, parseSectionFromForm, isUnipolarCable };";
  vm.runInContext(code, ctx);
  return ctx.__api;
}
module.exports = { load };
