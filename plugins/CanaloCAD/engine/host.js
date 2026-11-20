// Ambiente minimo per eseguire i calcoli e la scena di CanaloCAD fuori dal browser (CartesioCAD, test in Node).
// La pagina usa il DOM solo per l'interfaccia: qui gli elementi non esistono (getElementById -> null) e le funzioni
// che li cercano lo tollerano. L'unica misura che serve davvero è la larghezza dei testi in Arial (canvas
// measureText), che chiede all'ospite: __hostMeasureText(testo, font) -> larghezza in pixel.
var window = this;
var navigator = { userAgent: "CartesioCAD", platform: "Win32", language: "it-IT" };
var localStorage = { getItem: function () { return null; }, setItem: function () {}, removeItem: function () {} };
var location = { href: "file:///CanaloCAD", protocol: "file:", search: "", hash: "" };
function setTimeout(fn) { return 0; }
function clearTimeout() {}
function setInterval() { return 0; }
function clearInterval() {}
function requestAnimationFrame() { return 0; }
function __canvasContext() {
  return {
    font: "10px sans-serif",
    measureText: function (s) { return { width: __hostMeasureText(String(s), this.font) }; },
    fillText: function () {}, strokeText: function () {}, beginPath: function () {}, moveTo: function () {},
    lineTo: function () {}, stroke: function () {}, fill: function () {}, arc: function () {}, rect: function () {},
    fillRect: function () {}, strokeRect: function () {}, clearRect: function () {}, save: function () {},
    restore: function () {}, translate: function () {}, scale: function () {}, rotate: function () {},
    setTransform: function () {}, setLineDash: function () {}, closePath: function () {}, clip: function () {}
  };
}
function __element(tag) {
  var el = {
    tagName: String(tag || "div").toUpperCase(), style: {}, dataset: {}, children: [], childNodes: [],
    classList: { add: function () {}, remove: function () {}, toggle: function () {}, contains: function () { return false; } },
    setAttribute: function () {}, getAttribute: function () { return null; }, removeAttribute: function () {},
    appendChild: function (c) { return c; }, removeChild: function (c) { return c; }, remove: function () {},
    addEventListener: function () {}, removeEventListener: function () {}, querySelector: function () { return null; },
    querySelectorAll: function () { return []; }, getBoundingClientRect: function () { return { left: 0, top: 0, width: 0, height: 0 }; },
    innerHTML: "", textContent: "", value: ""
  };
  if (el.tagName === "CANVAS") el.getContext = function () { return __canvasContext(); };
  return el;
}
var document = {
  title: "", activeElement: null, body: __element("body"), documentElement: __element("html"),
  // Gli elementi non ci sono, tranne quelli che i moduli usano appena caricati (area di trascinamento dei file).
  getElementById: function (id) { return __hostElements[id] || null; },
  querySelector: function () { return null; },
  querySelectorAll: function () { return []; },
  createElement: function (tag) { return __element(tag); },
  createElementNS: function (ns, tag) { return __element(tag); },
  addEventListener: function () {}, removeEventListener: function () {}
};
// Aggiornamenti dell'interfaccia della pagina (viewport, tabelle, schede): qui non c'è niente da ridisegnare.
function requestRender() {}
function drawScene() {}
function fitAll() {}
// Data del cartiglio come la scrive il browser in italiano (toLocaleDateString("it-IT") = "5/3/2026"): il motore
// JavaScript di Qt non ha le impostazioni locali del browser.
Date.prototype.toLocaleDateString = function () {
  return this.getDate() + "/" + (this.getMonth() + 1) + "/" + this.getFullYear();
};
var __hostElements = { cadDropArea: __element("div"), loadCurveCanvas: __element("canvas") };
function addEventListener() {}
function removeEventListener() {}
// Array.prototype.sort stabile, come vuole lo standard (ES2019) e come fa il browser: il motore di Qt non lo è, e
// con elementi uguali (cavi dello stesso diametro) la disposizione dei cavi cambierebbe. Ordinamento per fusione.
Object.defineProperty(Array.prototype, "sort", {
  configurable: true, writable: true, enumerable: false,
  value: function (compare) {
    var cmp = typeof compare === "function"
      ? function (a, b) { var r = +compare(a, b); return r !== r ? 0 : r; }
      : function (a, b) { var x = String(a), y = String(b); return x < y ? -1 : x > y ? 1 : 0; };
    var items = [], undef = 0, len = this.length >>> 0;
    for (var i = 0; i < len; i++) {
      if (!(i in this)) continue;
      if (this[i] === undefined) undef++; else items.push(this[i]);
    }
    var buf = new Array(items.length);
    (function mergeSort(lo, hi) {
      if (hi - lo < 2) return;
      var mid = (lo + hi) >>> 1;
      mergeSort(lo, mid);
      mergeSort(mid, hi);
      var a = lo, b = mid, k = lo;
      while (a < mid && b < hi) buf[k++] = cmp(items[b], items[a]) < 0 ? items[b++] : items[a++];
      while (a < mid) buf[k++] = items[a++];
      while (b < hi) buf[k++] = items[b++];
      for (k = lo; k < hi; k++) items[k] = buf[k];
    })(0, items.length);
    var n = 0;
    for (i = 0; i < items.length; i++) this[n++] = items[i];
    for (i = 0; i < undef; i++) this[n++] = undefined;
    for (; n < len; n++) delete this[n];
    return this;
  }
});
// Funzioni recenti dello standard che il motore di Qt non ha (il browser sì): con lo stesso comportamento.
(function () {
  function define(obj, name, fn) {
    if (typeof obj[name] !== "function")
      Object.defineProperty(obj, name, { configurable: true, writable: true, enumerable: false, value: fn });
  }
  function flatten(out, arr, depth) {
    for (var i = 0; i < arr.length; i++) {
      if (!(i in arr)) continue;
      if (Array.isArray(arr[i]) && depth > 0) flatten(out, arr[i], depth - 1); else out.push(arr[i]);
    }
    return out;
  }
  define(Array.prototype, "flat", function (depth) { return flatten([], this, depth === undefined ? 1 : Math.floor(depth)); });
  define(Array.prototype, "flatMap", function (fn, thisArg) {
    var out = [];
    for (var i = 0; i < this.length; i++) {
      if (!(i in this)) continue;
      var v = fn.call(thisArg, this[i], i, this);
      if (Array.isArray(v)) for (var k = 0; k < v.length; k++) out.push(v[k]); else out.push(v);
    }
    return out;
  });
  define(Array.prototype, "at", function (n) { n = Math.trunc(n) || 0; if (n < 0) n += this.length; return this[n]; });
  define(Array.prototype, "findLast", function (fn, thisArg) {
    for (var i = this.length - 1; i >= 0; i--) if (fn.call(thisArg, this[i], i, this)) return this[i];
    return undefined;
  });
  define(String.prototype, "at", function (n) { n = Math.trunc(n) || 0; if (n < 0) n += this.length; return this.charAt(n) || undefined; });
  // matchAll: le corrispondenze in un array (si usa con for...of, spread e Array.from come l'iteratore).
  define(String.prototype, "matchAll", function (re) {
    var flags = re.flags !== undefined ? re.flags : (re.global ? "g" : "") + (re.ignoreCase ? "i" : "") + (re.multiline ? "m" : "");
    if (flags.indexOf("g") < 0) throw new TypeError("matchAll vuole un'espressione con /g");
    var r = new RegExp(re.source, flags), out = [], m, s = String(this);
    while ((m = r.exec(s)) !== null) {
      out.push(m);
      if (m[0] === "") r.lastIndex++;
    }
    return out;
  });
  define(String.prototype, "replaceAll", function (pattern, replacement) {
    if (pattern instanceof RegExp) return this.replace(pattern, replacement);
    return this.split(String(pattern)).join(typeof replacement === "function" ? replacement(String(pattern)) : replacement);
  });
  define(Object, "fromEntries", function (entries) {
    var o = {};
    Array.from(entries).forEach(function (e) { o[e[0]] = e[1]; });
    return o;
  });
  define(Object, "hasOwn", function (o, k) { return Object.prototype.hasOwnProperty.call(o, k); });
})();
var globalThis = this;
