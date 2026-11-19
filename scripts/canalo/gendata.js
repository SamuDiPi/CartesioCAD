// Genera src/core/CanaloData.inc dai dati di CanaloCAD (01-data.js).
const { load } = require("./load.js");
const api = load();
const q = s => JSON.stringify(String(s));
const n = v => {
  const s = String(v);
  return /[.e]/.test(s) ? s : s + ".0";
};
let out = "// Generato da CanaloCAD (src/js/01-data.js) con scripts/canalo/gendata.js: non modificare a mano.\n";
out += "// clang-format off\n";
out += "const std::vector<CableType> kCatalog = {\n";
for (const c of api.CABLE_CATALOG)
  out += `    {${q(c.id)}, ${q(c.cat)}, ${q(c.code)}, ${q(c.desc)}, ${q(c.type)}, ${q(c.form)}, ${n(c.d)}, ${n(c.w)}, ${n(c.sec)}, ${c.cores}, ${n(c.iz0)}, ${n(c.bendK)}, ${q(c.sys)}, ${q(c.std)}},\n`;
out += "};\n\n";
out += "const std::vector<std::pair<std::string, std::vector<Series>>> kBrands = {\n";
for (const [brand, list] of Object.entries(api.BRAND_SERIES)) {
  out += `    {${q(brand)}, {\n`;
  for (const s of list) out += `        {${q(s.id)}, ${q(s.name)}, ${q(s.code)}, ${q(s.type)}},\n`;
  out += "    }},\n";
}
out += "};\n\n";
out += "const std::vector<Support> kSupports = {\n";
for (const [key, s] of Object.entries(api.SUPPORT_SPECS))
  out += `    {${q(key)}, ${q(s.name)}, ${q(s.desc)}, {${n(s.swl10m)}, ${n(s.swl15m)}, ${n(s.swl20m)}, ${n(s.swl25m)}, ${n(s.swl30m)}}, ${n(s.armH)}, ${s.strut}, ${q(s.type)}},\n`;
out += "};\n// clang-format on\n";
process.stdout.write(out);
