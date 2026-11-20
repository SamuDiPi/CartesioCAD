// Generato da scripts/update-canalo.sh dai sorgenti di CanaloCAD: non modificare a mano.
// ---- 00-utils.js
    // ============================================================================
    // 0. UTILITÀ CONDIVISE (VERSIONE, NOTIFICHE, DOWNLOAD, STORAGE SICURO)
    // ============================================================================
    // Unica fonte della versione mostrata in titolo, menu, guida e file di progetto.
    const APP_VERSION = "1.0";
    const APP_VERSION_LABEL = `V${APP_VERSION}`; // come compare nell'interfaccia e nei documenti
    const APP_NAME = "CanaloCAD";

    function debounce(fn, ms) {
      let t = null;
      const d = (...args) => { clearTimeout(t); t = setTimeout(() => { t = null; fn(...args); }, ms); };
      d.flush = (...args) => { if (t) { clearTimeout(t); t = null; } fn(...args); };
      d.cancel = () => { if (t) { clearTimeout(t); t = null; } };
      d.pending = () => t !== null;
      return d;
    }

    // localStorage può lanciare eccezioni (navigazione privata, quota piena, file://)
    const safeStorage = {
      get(key) { try { return localStorage.getItem(key); } catch (e) { return null; } },
      set(key, val) { try { localStorage.setItem(key, val); return true; } catch (e) { return false; } },
      remove(key) { try { localStorage.removeItem(key); } catch (e) {} }
    };

    // Scarica un Blob con il nome indicato e libera l'URL temporaneo
    function downloadBlob(blob, fileName) {
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = fileName;
      document.body.appendChild(a);
      a.click();
      setTimeout(() => { a.remove(); URL.revokeObjectURL(a.href); }, 1000);
    }

    // Campo CSV sicuro (separatore ";"): virgolette raddoppiate
    function csvField(v) {
      return `"${String(v == null ? "" : v).replace(/"/g, '""')}"`;
    }

    // Notifica temporanea in basso a destra. tone: info | ok | warn | error.
    // action: { label, onClick } opzionale (es. "Annulla").
    function toast(message, tone = "info", action = null, ms = 4500) {
      let host = document.getElementById("toastHost");
      if (!host) {
        host = document.createElement("div");
        host.id = "toastHost";
        host.setAttribute("role", "status");
        host.setAttribute("aria-live", "polite");
        host.className = "fixed bottom-4 right-4 z-[100] flex flex-col gap-2 items-end pointer-events-none max-w-[calc(100vw-2rem)]";
        document.body.appendChild(host);
      }
      const tones = {
        info: "border-slate-600 text-slate-100",
        ok: "border-emerald-500/60 text-emerald-100",
        warn: "border-amber-500/60 text-amber-100",
        error: "border-red-500/70 text-red-100"
      };
      const el = document.createElement("div");
      el.className = `pointer-events-auto bg-slate-900/95 backdrop-blur border ${tones[tone] || tones.info} rounded-lg shadow-2xl px-3.5 py-2.5 text-[13px] flex items-center gap-3 transition-opacity duration-300`;
      const txt = document.createElement("span");
      txt.textContent = message;
      el.appendChild(txt);
      let timer = null;
      const close = () => { clearTimeout(timer); el.style.opacity = "0"; setTimeout(() => el.remove(), 300); };
      if (action && action.label) {
        const b = document.createElement("button");
        b.type = "button";
        b.textContent = action.label;
        b.className = "font-semibold text-amber-300 hover:text-amber-200 underline underline-offset-2";
        b.onclick = () => { close(); action.onClick && action.onClick(); };
        el.appendChild(b);
      }
      const x = document.createElement("button");
      x.type = "button";
      x.setAttribute("aria-label", "Chiudi notifica");
      x.textContent = "×";
      x.className = "text-slate-400 hover:text-white text-base leading-none";
      x.onclick = close;
      el.appendChild(x);
      host.appendChild(el);
      timer = setTimeout(close, ms);
      return close;
    }

// ---- 01-data.js
    // ============================================================================
    // 1. DATABASE TECNICO, PROFILI CAVI (CON UNIPOLARI 1X) E STATO APPLICAZIONE
    // ============================================================================
    // Tipologie di canalina (nessun costruttore né modello commerciale): nome e codice generico della distinta.
    // La tipologia del progetto è state.trayType; la serie la segue (BOM_SERIES_FOR).
    const BRAND_SERIES = {
      TIPOLOGIE: [
        { id: "PAS_ASO", name: "Passerella asolata in lamiera forata", code: "PAS", type: "perforated" },
        { id: "CAN_CIE", name: "Canale portacavi cieco in lamiera piena", code: "CAN", type: "solid" },
        { id: "PAS_FIL", name: "Passerella a filo (rete elettrosaldata)", code: "PFI", type: "mesh" },
        { id: "PAS_SCA", name: "Passerella a scala (a pioli)", code: "PSC", type: "ladder" }
      ]
    };
    // Serie della tipologia scelta.
    function BOM_SERIES_FOR(trayType) {
      const list = BRAND_SERIES.TIPOLOGIE;
      return list.find(s => s.type === trayType) || list[0];
    }

    let CABLE_CATALOG = [
      // ==========================================
      // 1. CAVI BT POTENZA UNIPOLARI (FG16M16 / FS17)
      // ==========================================
      { id: "FG16_1X1.5",  cat: "bt_uni", code: "FG16M16-1X1.5",  desc: "Cavo Unipolare BT 1x1.5 mm² FG16M16",   type: "FG16M16", form: "1(1x1,5)", d: 5.8,  w: 0.05, sec: 1.5, cores: 1, iz0: 24,  bendK: 10, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X2.5",  cat: "bt_uni", code: "FG16M16-1X2.5",  desc: "Cavo Unipolare BT 1x2.5 mm² FG16M16",   type: "FG16M16", form: "1(1x2,5)", d: 6.4,  w: 0.07, sec: 2.5, cores: 1, iz0: 32,  bendK: 10, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X4",    cat: "bt_uni", code: "FG16M16-1X4",    desc: "Cavo Unipolare BT 1x4 mm² FG16M16",     type: "FG16M16", form: "1(1x4)",   d: 7.0,  w: 0.09, sec: 4.0, cores: 1, iz0: 42,  bendK: 10, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X6",    cat: "bt_uni", code: "FG16M16-1X6",    desc: "Cavo Unipolare BT 1x6 mm² FG16M16",     type: "FG16M16", form: "1(1x6)",   d: 7.6,  w: 0.11, sec: 6.0, cores: 1, iz0: 54,  bendK: 10, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X10",   cat: "bt_uni", code: "FG16M16-1X10",   desc: "Cavo Unipolare BT 1x10 mm² FG16M16",    type: "FG16M16", form: "1(1x10)",  d: 9.2,  w: 0.17, sec: 10.0,cores: 1, iz0: 75,  bendK: 10, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X16",   cat: "bt_uni", code: "FG16M16-1X16",   desc: "Cavo Unipolare BT 1x16 mm² FG16M16",    type: "FG16M16", form: "1(1x16)",  d: 10.5, w: 0.23, sec: 16.0,cores: 1, iz0: 100, bendK: 10, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X25",   cat: "bt_uni", code: "FG16M16-1X25",   desc: "Cavo Unipolare BT 1x25 mm² FG16M16",    type: "FG16M16", form: "1(1x25)",  d: 12.2, w: 0.33, sec: 25.0,cores: 1, iz0: 135, bendK: 10, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X35",   cat: "bt_uni", code: "FG16M16-1X35",   desc: "Cavo Unipolare BT 1x35 mm² FG16M16",    type: "FG16M16", form: "1(1x35)",  d: 13.5, w: 0.44, sec: 35.0,cores: 1, iz0: 169, bendK: 12, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X50",   cat: "bt_uni", code: "FG16M16-1X50",   desc: "Cavo Unipolare BT 1x50 mm² FG16M16",    type: "FG16M16", form: "1(1x50)",  d: 15.4, w: 0.58, sec: 50.0,cores: 1, iz0: 207, bendK: 12, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X70",   cat: "bt_uni", code: "FG16M16-1X70",   desc: "Cavo Unipolare BT 1x70 mm² FG16M16",    type: "FG16M16", form: "1(1x70)",  d: 17.5, w: 0.81, sec: 70.0,cores: 1, iz0: 268, bendK: 12, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X95",   cat: "bt_uni", code: "FG16M16-1X95",   desc: "Cavo Unipolare BT 1x95 mm² FG16M16",    type: "FG16M16", form: "1(1x95)",  d: 19.8, w: 1.08, sec: 95.0,cores: 1, iz0: 328, bendK: 12, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X120",  cat: "bt_uni", code: "FG16M16-1X120",  desc: "Cavo Unipolare BT 1x120 mm² FG16M16",   type: "FG16M16", form: "1(1x120)", d: 22.0, w: 1.34, sec: 120.0, cores: 1, iz0: 383, bendK: 12, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X150",  cat: "bt_uni", code: "FG16M16-1X150",  desc: "Cavo Unipolare BT 1x150 mm² FG16M16",   type: "FG16M16", form: "1(1x150)", d: 24.2, w: 1.65, sec: 150.0, cores: 1, iz0: 444, bendK: 12, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X185",  cat: "bt_uni", code: "FG16M16-1X185",  desc: "Cavo Unipolare BT 1x185 mm² FG16M16",   type: "FG16M16", form: "1(1x185)", d: 26.8, w: 2.05, sec: 185.0, cores: 1, iz0: 510, bendK: 12, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X240",  cat: "bt_uni", code: "FG16M16-1X240",  desc: "Cavo Unipolare BT 1x240 mm² FG16M16",   type: "FG16M16", form: "1(1x240)", d: 30.2, w: 2.65, sec: 240.0, cores: 1, iz0: 607, bendK: 12, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_1X300",  cat: "bt_uni", code: "FG16M16-1X300",  desc: "Cavo Unipolare BT 1x300 mm² FG16M16",   type: "FG16M16", form: "1(1x300)", d: 33.5, w: 3.30, sec: 300.0, cores: 1, iz0: 703, bendK: 12, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FS17_1X16_GV",cat: "bt_uni", code: "FS17-1X16-PE",   desc: "Conduttore PE Giallo/Verde 1x16 mm² FS17", type: "FS17", form: "1(1x16) PE", d: 7.4, w: 0.18, sec: 16.0, cores: 1, iz0: 76, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35716 Cca-s3,d1,a3" },
      { id: "FS17_1X25_GV",cat: "bt_uni", code: "FS17-1X25-PE",   desc: "Conduttore PE Giallo/Verde 1x25 mm² FS17", type: "FS17", form: "1(1x25) PE", d: 9.2, w: 0.28, sec: 25.0, cores: 1, iz0: 101, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35716 Cca-s3,d1,a3" },
      { id: "FS17_1X35_GV",cat: "bt_uni", code: "FS17-1X35-PE",   desc: "Conduttore PE Giallo/Verde 1x35 mm² FS17", type: "FS17", form: "1(1x35) PE", d: 10.4, w: 0.38, sec: 35.0, cores: 1, iz0: 125, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35716 Cca-s3,d1,a3" },

      // ==========================================
      // 2. CAVI BT POTENZA MULTIPOLARI (FG16OM16 / H07RN-F)
      // ==========================================
      { id: "FG16_2X1.5",  cat: "bt_multi", code: "FG16OM16-2X1.5", desc: "Cavo Multipolare BT 2x1.5 mm² FG16OM16", type: "FG16OM16", form: "1(2x1,5)", d: 10.2, w: 0.14, sec: 1.5, cores: 2, iz0: 22, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 0.6/1kV" },
      { id: "FG16_2X2.5",  cat: "bt_multi", code: "FG16OM16-2X2.5", desc: "Cavo Multipolare BT 2x2.5 mm² FG16OM16", type: "FG16OM16", form: "1(2x2,5)", d: 11.5, w: 0.18, sec: 2.5, cores: 2, iz0: 30, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 0.6/1kV" },
      { id: "FG16_3G1.5",  cat: "bt_multi", code: "FG16OM16-3G1.5", desc: "Cavo Multipolare BT 3G1.5 mm² FG16OM16", type: "FG16OM16", form: "1(3G1,5)", d: 11.0, w: 0.16, sec: 1.5, cores: 2, iz0: 22, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_3G2.5",  cat: "bt_multi", code: "FG16OM16-3G2.5", desc: "Cavo Multipolare BT 3G2.5 mm² FG16OM16", type: "FG16OM16", form: "1(3G2,5)", d: 12.2, w: 0.22, sec: 2.5, cores: 2, iz0: 30, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_3G4",    cat: "bt_multi", code: "FG16OM16-3G4",   desc: "Cavo Multipolare BT 3G4 mm² FG16OM16",   type: "FG16OM16", form: "1(3G4)",   d: 13.8, w: 0.31, sec: 4.0, cores: 2, iz0: 40, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_3G6",    cat: "bt_multi", code: "FG16OM16-3G6",   desc: "Cavo Multipolare BT 3G6 mm² FG16OM16",   type: "FG16OM16", form: "1(3G6)",   d: 15.2, w: 0.42, sec: 6.0, cores: 2, iz0: 51, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_4G1.5",  cat: "bt_multi", code: "FG16OM16-4G1.5", desc: "Cavo Multipolare BT 4G1.5 mm² FG16OM16", type: "FG16OM16", form: "1(4G1,5)", d: 12.0, w: 0.20, sec: 1.5, cores: 3, iz0: 19, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_4G2.5",  cat: "bt_multi", code: "FG16OM16-4G2.5", desc: "Cavo Multipolare BT 4G2.5 mm² FG16OM16", type: "FG16OM16", form: "1(4G2,5)", d: 13.4, w: 0.27, sec: 2.5, cores: 3, iz0: 26, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_4G4",    cat: "bt_multi", code: "FG16OM16-4G4",   desc: "Cavo Multipolare BT 4G4 mm² FG16OM16",   type: "FG16OM16", form: "1(4G4)",   d: 15.2, w: 0.38, sec: 4.0, cores: 3, iz0: 35, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_4G6",    cat: "bt_multi", code: "FG16OM16-4G6",   desc: "Cavo Multipolare BT 4G6 mm² FG16OM16",   type: "FG16OM16", form: "1(4G6)",   d: 16.8, w: 0.52, sec: 6.0, cores: 3, iz0: 45, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_4G10",   cat: "bt_multi", code: "FG16OM16-4G10",  desc: "Cavo Multipolare BT 4G10 mm² FG16OM16",  type: "FG16OM16", form: "1(4G10)",  d: 19.5, w: 0.78, sec: 10.0,cores: 3, iz0: 63, bendK: 10,sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_4G16",   cat: "bt_multi", code: "FG16OM16-4G16",  desc: "Cavo Multipolare BT 4G16 mm² FG16OM16",  type: "FG16OM16", form: "1(4G16)",  d: 22.8, w: 1.15, sec: 16.0,cores: 3, iz0: 85, bendK: 10,sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_4G25",   cat: "bt_multi", code: "FG16OM16-4G25",  desc: "Cavo Multipolare BT 4G25 mm² FG16OM16",  type: "FG16OM16", form: "1(4G25)",  d: 27.2, w: 1.70, sec: 25.0,cores: 3, iz0: 115,bendK: 10,sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_4G35",   cat: "bt_multi", code: "FG16OM16-4G35",  desc: "Cavo Multipolare BT 4G35 mm² FG16OM16",  type: "FG16OM16", form: "1(4G35)",  d: 30.5, w: 2.25, sec: 35.0,cores: 3, iz0: 143,bendK: 10,sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_4G50",   cat: "bt_multi", code: "FG16OM16-4G50",  desc: "Cavo Multipolare BT 4G50 mm² FG16OM16",  type: "FG16OM16", form: "1(4G50)",  d: 35.0, w: 3.10, sec: 50.0,cores: 3, iz0: 178,bendK: 12,sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_5G1.5",  cat: "bt_multi", code: "FG16OM16-5G1.5", desc: "Cavo Multipolare BT 5G1.5 mm² FG16OM16", type: "FG16OM16", form: "1(5G1,5)", d: 13.0, w: 0.24, sec: 1.5, cores: 3, iz0: 19, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_5G2.5",  cat: "bt_multi", code: "FG16OM16-5G2.5", desc: "Cavo Multipolare BT 5G2.5 mm² FG16OM16", type: "FG16OM16", form: "1(5G2,5)", d: 14.5, w: 0.32, sec: 2.5, cores: 3, iz0: 26, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_5G4",    cat: "bt_multi", code: "FG16OM16-5G4",   desc: "Cavo Multipolare BT 5G4 mm² FG16OM16",   type: "FG16OM16", form: "1(5G4)",   d: 16.5, w: 0.45, sec: 4.0, cores: 3, iz0: 35, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_5G6",    cat: "bt_multi", code: "FG16OM16-5G6",   desc: "Cavo Multipolare BT 5G6 mm² FG16OM16",   type: "FG16OM16", form: "1(5G6)",   d: 18.2, w: 0.60, sec: 6.0, cores: 3, iz0: 45, bendK: 8, sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_5G10",   cat: "bt_multi", code: "FG16OM16-5G10",  desc: "Cavo Multipolare BT 5G10 mm² FG16OM16",  type: "FG16OM16", form: "1(5G10)",  d: 21.4, w: 0.92, sec: 10.0,cores: 3, iz0: 63, bendK: 10,sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_5G16",   cat: "bt_multi", code: "FG16OM16-5G16",  desc: "Cavo Multipolare BT 5G16 mm² FG16OM16",  type: "FG16OM16", form: "1(5G16)",  d: 24.8, w: 1.35, sec: 16.0,cores: 3, iz0: 85, bendK: 10,sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "FG16_5G25",   cat: "bt_multi", code: "FG16OM16-5G25",  desc: "Cavo Multipolare BT 5G25 mm² FG16OM16",  type: "FG16OM16", form: "1(5G25)",  d: 29.8, w: 2.05, sec: 25.0,cores: 3, iz0: 115,bendK: 10,sys: "BT_POWER", std: "CEI UNEL 35375 Cca-s3,d1,a3" },
      { id: "H07RN_3G2.5", cat: "bt_multi", code: "H07RN-F-3G2.5",  desc: "Cavo Gomma Neoprene H07RN-F 3G2.5",     type: "H07RN-F",  form: "1(3G2,5)", d: 13.5, w: 0.28, sec: 2.5, cores: 2, iz0: 30, bendK: 8, sys: "BT_POWER", std: "CEI EN 50525-2-21 450/750V" },
      { id: "H07RN_4G4",   cat: "bt_multi", code: "H07RN-F-4G4",    desc: "Cavo Gomma Sommerso Pompa H07RN-F 4G4", type: "H07RN-F",  form: "1(4G4)",   d: 17.5, w: 0.48, sec: 4.0, cores: 3, iz0: 35, bendK: 8, sys: "BT_POWER", std: "CEI EN 50525-2-21 450/750V" },

      // ==========================================
      // 3. CAVI SEGNALI, COMANDO & AUSILIARI (FG16OH2R16 / LiYCY / KNX)
      // ==========================================
      { id: "OH2_2X1",     cat: "signal",   code: "FG16OH2R16-2X1",    desc: "Cavo Segnale Schermato FG16OH2R16 2x1",  type: "FG16OH2R16", form: "1(2x1)",  d: 9.5,  w: 0.13, sec: 1.0, cores: 2, iz0: 15, bendK: 10,sys: "BT_POWER", std: "CEI 20-13 Schermato (Compatibile BT)" },
      { id: "OH2_2X1.5",   cat: "signal",   code: "FG16OH2R16-2X1.5",  desc: "Cavo Segnale Schermato FG16OH2R16 2x1.5",type: "FG16OH2R16", form: "1(2x1,5)",d: 10.2, w: 0.16, sec: 1.5, cores: 2, iz0: 18, bendK: 10,sys: "BT_POWER", std: "CEI 20-13 Schermato (Compatibile BT)" },
      { id: "OH2_4G1.5",   cat: "signal",   code: "FG16OH2R16-4G1.5",  desc: "Cavo Comando Schermato FG16OH2R16 4G1.5", type: "FG16OH2R16", form: "1(4G1,5)",d: 12.2, w: 0.23, sec: 1.5, cores: 3, iz0: 18, bendK: 10,sys: "BT_POWER", std: "CEI 20-13 Schermato (Compatibile BT)" },
      { id: "OH2_7G1.5",   cat: "signal",   code: "FG16OH2R16-7G1.5",  desc: "Cavo Ausiliari Schermato FG16OH2R16 7G1.5",  type: "FG16OH2R16", form: "1(7G1,5)",d: 14.5, w: 0.34, sec: 1.5, cores: 3, iz0: 14, bendK: 10,sys: "BT_POWER", std: "CEI 20-13 Schermato (Compatibile BT)" },
      { id: "OH2_12G1.5",  cat: "signal",   code: "FG16OH2R16-12G1.5", desc: "Cavo Ausiliari Schermato FG16OH2R16 12G1.5",type: "FG16OH2R16", form: "1(12G1,5)",d: 18.2, w: 0.55, sec: 1.5, cores: 3, iz0: 12, bendK: 10,sys: "BT_POWER", std: "CEI 20-13 Schermato (Compatibile BT)" },
      { id: "LIYCY_2X0.75",cat: "signal",   code: "LiYCY-2X0.75",      desc: "Cavo Trasmissione Dati LiYCY 2x0.75",    type: "LiYCY",     form: "2x0,75",   d: 5.8,  w: 0.05, sec: 0.75,cores: 2, iz0: 8,  bendK: 10,sys: "SPECIAL_ELV", std: "DIN 47100 Schermato Treccia" },
      { id: "LIYCY_4X0.75",cat: "signal",   code: "LiYCY-4X0.75",      desc: "Cavo Sonde Temperatura LiYCY 4x0.75",    type: "LiYCY",     form: "4x0,75",   d: 6.8,  w: 0.08, sec: 0.75,cores: 2, iz0: 8,  bendK: 10,sys: "SPECIAL_ELV", std: "DIN 47100 Schermato Treccia" },
      { id: "BUS_KNX",     cat: "signal",   code: "KNX-TP1-2X2X0.8",   desc: "Cavo Bus Domotica KNX TP1 2x2x0.8",      type: "KNX Bus TP1", form: "2x2x0,8", d: 6.8,  w: 0.06, sec: 0.5, cores: 2, iz0: 6,  bendK: 10,sys: "SPECIAL_ELV", std: "CEI EN 50090 Certificato Verde" },

      // ==========================================
      // 4. RESISTENTI AL FUOCO PH120 & SICUREZZA (FTG10OM1 / FTG18OM16 / IRAI / EVAC)
      // ==========================================
      { id: "FTG18_2X1.5", cat: "fire",     code: "FTG18OM16-2X1.5",  desc: "Cavo Resistente al Fuoco PH120 FTG18OM16 2x1.5",   type: "FTG18OM16", form: "1(2x1,5)", d: 10.5, w: 0.15, sec: 1.5, cores: 2, iz0: 22, bendK: 10, sys: "BT_FIRE", std: "CEI 20-45 / UNI 9795 PH120" },
      { id: "FTG18_3G2.5", cat: "fire",     code: "FTG18OM16-3G2.5",  desc: "Cavo Sicurezza Antincendio PH120 FTG18OM16 3G2.5", type: "FTG18OM16", form: "1(3G2,5)", d: 12.8, w: 0.24, sec: 2.5, cores: 2, iz0: 30, bendK: 10, sys: "BT_FIRE", std: "CEI 20-45 / UNI 9795 PH120" },
      { id: "FTG18_4G4",   cat: "fire",     code: "FTG18OM16-4G4",    desc: "Cavo Gruppo Emergenza PH120 FTG18OM16 4G4",        type: "FTG18OM16", form: "1(4G4)",   d: 16.0, w: 0.42, sec: 4.0, cores: 3, iz0: 35, bendK: 10, sys: "BT_FIRE", std: "CEI 20-45 / UNI 9795 PH120" },
      { id: "FTG18_4G10",  cat: "fire",     code: "FTG18OM16-4G10",   desc: "Cavo Pompe Sprinkler PH120 FTG18OM16 4G10",        type: "FTG18OM16", form: "1(4G10)",  d: 20.2, w: 0.85, sec: 10.0,cores: 3, iz0: 63, bendK: 10, sys: "BT_FIRE", std: "CEI 20-45 / UNI 9795 PH120" },
      { id: "IRAI_LOOP",   cat: "fire",     code: "FTG18(O)M16-2X1.5",desc: "Cavo Rivelazione Incendio IRAI FTG18(O)M16 2x1.5",type: "FTG18(O)M16", form: "1(2x1,5)", d: 8.2,  w: 0.09, sec: 1.5, cores: 2, iz0: 15, bendK: 10, sys: "SPECIAL_ELV", std: "CEI 20-105 Rosso Allarme" },
      { id: "EVAC_100V",   cat: "fire",     code: "FTG18(O)M16-2X2.5",desc: "Cavo Diffusione Sonora EVAC FTG18(O)M16 2x2.5",   type: "FTG18(O)M16", form: "1(2x2,5)", d: 9.8, w: 0.14, sec: 2.5, cores: 2, iz0: 20, bendK: 10, sys: "SPECIAL_ELV", std: "EN 54-24 / CEI 20-105 PH120" },

      // ==========================================
      // 5. RETI DATI STRUTTURATE & FIBRA OTTICA
      // ==========================================
      { id: "DATA_CAT6",   cat: "data",     code: "UTP-CAT6-4X2",     desc: "Cavo Dati U/UTP Cat.6 LSZH",        type: "U/UTP Cat.6", form: "4x2x24AWG", d: 5.8,  w: 0.04, sec: 0.5, cores: 2, iz0: 4,  bendK: 8,  sys: "SPECIAL_ELV", std: "ISO/IEC 11801 Class E" },
      { id: "DATA_CAT6A",  cat: "data",     code: "FUTP-CAT6A-4X2",   desc: "Cavo Dati Schermato F/UTP Cat.6A",   type: "F/UTP Cat.6A", form: "4x2x23AWG", d: 7.3, w: 0.06, sec: 0.5, cores: 2, iz0: 5, bendK: 10, sys: "SPECIAL_ELV", std: "ISO/IEC 11801 Class EA" },
      { id: "DATA_CAT7",   cat: "data",     code: "SFTP-CAT7-4X2",    desc: "Dorsale Dati S/FTP Cat.7 LSZH",     type: "S/FTP Cat.7", form: "4x2x23AWG", d: 8.2, w: 0.07, sec: 0.5, cores: 2, iz0: 5, bendK: 10, sys: "SPECIAL_ELV", std: "ISO/IEC 11801 Class F" },
      { id: "FO_4_OM4",    cat: "data",     code: "FO-OM4-4X",        desc: "Fibra Ottica Breakout 4 FO OM4 50/125",type: "FO Breakout 4x", form: "4 FO 50/125", d: 6.2, w: 0.04, sec: 0.2, cores: 2, iz0: 1, bendK: 12, sys: "SPECIAL_ELV", std: "CEI EN 60794-2" },
      { id: "FO_8_OM4",    cat: "data",     code: "FO-OM4-8X",        desc: "Fibra Ottica Breakout 8 FO OM4 50/125",type: "FO Breakout 8x", form: "8 FO 50/125", d: 7.8, w: 0.06, sec: 0.2, cores: 2, iz0: 1, bendK: 12, sys: "SPECIAL_ELV", std: "CEI EN 60794-2" },
      { id: "FO_12_OS2",   cat: "data",     code: "FO-OS2-12X",       desc: "Fibra Ottica Monomodale 12 FO OS2 9/125",type: "FO Loose 12x", form: "12 FO 9/125", d: 7.0, w: 0.05, sec: 0.2, cores: 2, iz0: 1, bendK: 12, sys: "SPECIAL_ELV", std: "ITU-T G.652.D / EN 60794" },
      { id: "FO_24_OS2",   cat: "data",     code: "FO-24OS2",         desc: "Fibra Ottica Monomodale 24 FO OS2 9/125",type: "FO Loose 24x", form: "24 FO 9/125", d: 8.5, w: 0.07, sec: 0.2, cores: 2, iz0: 1, bendK: 12, sys: "SPECIAL_ELV", std: "ITU-T G.652.D / EN 60794" }
    ];

    // Registrazione dinamica automatica di nuovi cavi non presenti nel catalogo (Punto 1)
    function registerCableInCatalog(c) {
      if (!c) return null;
      const normType = String(c.type || "FG16M16").trim();
      const normForm = String(c.form || "1(3G2,5)").trim();
      const d = +(c.d || 12.0).toFixed(1);
      const isUni = isUnipolarCable(c);
      const sec = c.sec || parseSectionFromForm(normForm, 2.5);

      // Cerca se esiste già nel catalogo per tipo e formazione oppure per codice/id cavo
      const candidateCode = String(c.siglaCavo || c.cableCode || "").trim();
      const isUtenzaLike = (s) => /^(?:[A-Z0-9]{2,12}(?:[-._/][A-Z0-9]{1,10})*\s+)?C-?\d+$/i.test(s) ||
                                  /^(?:QGBT|QP|QE|QUADRO|UPS|IRAI|EVAC|TD)[A-Z0-9_.-]*/i.test(s) ||
                                  /^#\d+/i.test(s) || /^Alimentazione/i.test(s);

      let existing = null;
      if (candidateCode && !isUtenzaLike(candidateCode)) {
        const found = CABLE_CATALOG.find(it => it.code.toUpperCase() === candidateCode.toUpperCase() || it.id.toUpperCase() === candidateCode.toUpperCase());
        if (found && Math.abs(found.d - d) < 0.2) {
          existing = found;
        }
      }
      if (!existing) {
        existing = CABLE_CATALOG.find(it =>
          it.type.toUpperCase() === normType.toUpperCase() &&
          it.form.toUpperCase() === normForm.toUpperCase() &&
          Math.abs(it.d - d) < 0.2
        );
      }
      if (existing) return existing;

      const w = c.w || +(Math.pow(d / 10, 2) * 0.145).toFixed(2);
      const cores = isUni ? 1 : (c.cores || (normForm.includes("5G") || normForm.includes("4G") ? 3 : 2));
      const sys = c.sys || (/IRAI|EVAC|KNX|BUS|FIBRA|CAT|UTP|FTP/i.test(normType) ? "SPECIAL_ELV" : "BT_POWER");
      const cat = sys === "SPECIAL_ELV" ? "signal" : isUni ? "bt_uni" : "bt_multi";

      // Generazione di una sigla cavo univoca tecnica (senza riferimenti alle sigle utenza come da specifica)
      let baseCode = (candidateCode && !isUtenzaLike(candidateCode))
        ? candidateCode.toUpperCase().replace(/[^a-zA-Z0-9_-]+/g, "_")
        : `${normType}-${normForm.replace(/[^a-zA-Z0-9,.]+/g, "")}`.toUpperCase();
      
      let uniqueCode = baseCode;
      let counter = 1;
      while (CABLE_CATALOG.some(it => it.code.toUpperCase() === uniqueCode.toUpperCase() || it.id.toUpperCase() === `CST_${uniqueCode.replace(/[^a-zA-Z0-9]+/g, "_")}`.toUpperCase())) {
        uniqueCode = `${baseCode}_${counter++}`;
      }
      const newId = `CST_${uniqueCode.replace(/[^a-zA-Z0-9]+/g, "_")}`;

      const newEntry = {
        id: newId,
        cat,
        code: uniqueCode,
        desc: (c.desc && !isUtenzaLike(c.desc)) ? c.desc : `Cavo ${normType} ${normForm} (Ø=${d}mm, ${sec}mm²)`,
        type: normType,
        form: normForm,
        d,
        w,
        sec,
        cores,
        iz0: c.iz0 || (sec >= 50 ? 200 : sec >= 16 ? 100 : sec >= 6 ? 54 : sec >= 2.5 ? 32 : 18),
        bendK: c.bendK || (isUni ? 10 : 8),
        sys,
        std: "Aggiunto automaticamente da file/input"
      };

      CABLE_CATALOG.push(newEntry);
      if (typeof initCatalogDropdown === "function") initCatalogDropdown();
      if (typeof renderCatalogTable === "function") renderCatalogTable();
      const countEl = document.getElementById("dbItemsCount");
      if (countEl) countEl.textContent = `${CABLE_CATALOG.length} modelli certificati disponibili`;
      const banner = document.getElementById("statusBanner");
      if (banner) {
        banner.textContent = `✓ Nuova tipologia cavo registrata nel DB: ${uniqueCode} (Ø=${d}mm, ${sec}mm²)`;
        banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-emerald-500/20 text-emerald-300 border border-emerald-500/40 font-bold";
      }
      return newEntry;
    }

    // Mappa granulare basata sul diametro reale (mm) secondo tabelle dimensionali CEI UNEL 35310 / Prysmian / Baldassari
    // NOTA NORMATIVA: I cavi importati sono SEMPRE BT (sys: "BT_POWER"), salvo esplicita presenza di sigle di segnale/speciali.
    function inferCableSpecsByDiameter(d, codeStr = "", layerStr = "") {
      const combined = (String(codeStr || "") + " " + String(layerStr || "")).toUpperCase();
      
      // Controllo esplicito di cavi ausiliari / segnali / reti speciali
      const isExplicitSignal = /(?:IRAI|FUMI|LOOP|EVAC|SPK|DIFFUS|CAT\s*[567]|DATI|LAN|UTP|FTP|BUS|KNX|FONIA|FIBRA|ELV|SEGNAL|SONOR)/i.test(combined);
      const isExplicitFire = /(?:FTG10|FTG18|RESIST|INCEND|TAGLIAFUOCO|REI)/i.test(combined);

      if (isExplicitSignal) {
        if (/IRAI|FUMI|LOOP/i.test(combined)) {
          return { type: "FTG18(O)M16", form: "1(2x1,5)", sec: 1.5, cores: 2, iz0: 15, bendK: 10, sys: "SPECIAL_ELV" };
        }
        if (/EVAC|SPK|DIFFUS|SONOR/i.test(combined)) {
          return { type: "FTG18(O)M16", form: "1(2x2,5)", sec: 2.5, cores: 2, iz0: 20, bendK: 10, sys: "SPECIAL_ELV" };
        }
        if (/BUS|KNX/i.test(combined)) {
          return { type: "KNX Bus TP1", form: "2x2x0,8", sec: 0.5, cores: 2, iz0: 6, bendK: 10, sys: "SPECIAL_ELV" };
        }
        if (/FIBRA/i.test(combined)) {
          return { type: "Fibra Ottica", form: "8 FO OM4", sec: 0.2, cores: 2, iz0: 1, bendK: 12, sys: "SPECIAL_ELV" };
        }
        return { type: "F/UTP Cat.6A", form: "4x2x23AWG", sec: 0.5, cores: 2, iz0: 5, bendK: 10, sys: "SPECIAL_ELV" };
      }

      if (isExplicitFire) {
        if (d <= 14.5) return { type: "FTG18OM16", form: "1(3G2,5)", sec: 2.5, cores: 2, iz0: 27, bendK: 10, sys: "BT_FIRE" };
        if (d <= 18.0) return { type: "FTG18OM16", form: "1(4G4)",   sec: 4.0, cores: 3, iz0: 36, bendK: 10, sys: "BT_FIRE" };
        return { type: "FTG18OM16", form: "1(4G10)",  sec: 10.0, cores: 3, iz0: 60, bendK: 10, sys: "BT_FIRE" };
      }

      // TUTTI GLI ALTRI CAVI SONO SEMPRE BASSA TENSIONE (BT_POWER)
      // Mappatura continua secondo tabelle dimensionali standard CEI UNEL 35375 / CEI 20-22
      if (d <= 6.8)  return { type: "FS17",     form: "1(1x2,5)", sec: 2.5,  cores: 1, iz0: 24,  bendK: 8,  sys: "BT_POWER" };
      if (d <= 7.8)  return { type: "FS17",     form: "1(1x4)",   sec: 4.0,  cores: 1, iz0: 32,  bendK: 8,  sys: "BT_POWER" };
      if (d <= 8.8)  return { type: "FS17",     form: "1(1x6)",   sec: 6.0,  cores: 1, iz0: 42,  bendK: 8,  sys: "BT_POWER" };
      if (d <= 9.8)  return { type: "FG16M16",  form: "1(1x10)",  sec: 10.0, cores: 1, iz0: 75,  bendK: 10, sys: "BT_POWER" };
      if (d <= 10.8) return { type: "FG16M16",  form: "1(1x16)",  sec: 16.0, cores: 1, iz0: 100, bendK: 10, sys: "BT_POWER" };
      if (d <= 11.6) return { type: "FG16OM16", form: "1(3G1,5)", sec: 1.5,  cores: 2, iz0: 22,  bendK: 8,  sys: "BT_POWER" };
      if (d <= 12.8) return { type: "FG16M16",  form: "1(1x25)",  sec: 25.0, cores: 1, iz0: 135, bendK: 10, sys: "BT_POWER" };
      if (d <= 13.5) return { type: "FG16OM16", form: "1(4G1,5)", sec: 1.5,  cores: 3, iz0: 19,  bendK: 8,  sys: "BT_POWER" };
      if (d <= 14.4) return { type: "FG16OM16", form: "1(3G2,5)", sec: 2.5,  cores: 2, iz0: 30,  bendK: 8,  sys: "BT_POWER" };
      if (d <= 15.2) return { type: "FG16OM16", form: "1(5G2,5)", sec: 2.5,  cores: 3, iz0: 26,  bendK: 8,  sys: "BT_POWER" };
      if (d <= 16.0) return { type: "FG16OM16", form: "1(3G4)",   sec: 4.0,  cores: 2, iz0: 40,  bendK: 8,  sys: "BT_POWER" };
      if (d <= 17.2) return { type: "FG16OM16", form: "1(5G4)",   sec: 4.0,  cores: 3, iz0: 35,  bendK: 8,  sys: "BT_POWER" };
      if (d <= 18.2) return { type: "FG16OM16", form: "1(5G6)",   sec: 6.0,  cores: 3, iz0: 45,  bendK: 8,  sys: "BT_POWER" };
      if (d <= 20.5) return { type: "FG16M16",  form: "1(1x95)",  sec: 95.0, cores: 1, iz0: 328, bendK: 12, sys: "BT_POWER" };
      if (d <= 21.8) return { type: "FG16OM16", form: "1(5G10)",  sec: 10.0, cores: 3, iz0: 63,  bendK: 10, sys: "BT_POWER" };
      if (d <= 23.5) return { type: "FG16M16",  form: "1(1x120)", sec: 120.0, cores: 1, iz0: 383, bendK: 12, sys: "BT_POWER" };
      if (d <= 25.0) return { type: "FG16OM16", form: "1(5G16)",  sec: 16.0, cores: 3, iz0: 85,  bendK: 10, sys: "BT_POWER" };
      if (d <= 27.5) return { type: "FG16M16",  form: "1(1x185)", sec: 185.0, cores: 1, iz0: 510, bendK: 12, sys: "BT_POWER" };
      if (d <= 31.0) return { type: "FG16M16",  form: "1(1x240)", sec: 240.0, cores: 1, iz0: 607, bendK: 12, sys: "BT_POWER" };
      if (d <= 35.0) return { type: "FG16M16",  form: "1(1x300)", sec: 300.0, cores: 1, iz0: 703, bendK: 12, sys: "BT_POWER" };
      return { type: "FG16M16", form: "1(1x400)", sec: 400.0, cores: 1, iz0: 823, bendK: 12, sys: "BT_POWER" };
    }

    // Determina se un cavo è Unipolare (es. "1x...", "4(1x10)", "FG16M16", "FS17")
    function isUnipolarCable(c) {
      const f = String(c.form || "").toUpperCase();
      const t = String(c.type || "").toUpperCase();
      return /\b1X\d+|\(\s*1X\d+/i.test(f) || /\bFG16M16\b|\bFS17\b|\bFG17\b|\bH07Z1-K\b/i.test(t) || c.cores === 1;
    }

    // Estrae la sezione nominale in mm² da una stringa formazione con fix standard metrico
    function parseSectionFromForm(formStr, fallbackSec = 2.5) {
      if (/AWG/i.test(formStr)) return fallbackSec;
      const m = String(formStr || "").match(/[xXGg](\d+(?:[.,]\d+)?)/);
      if (m) {
        let val = parseFloat(m[1].replace(",", "."));
        const standardSections = [0.5, 1, 1.5, 2.5, 4, 6, 10, 16, 25, 35, 50, 70, 95, 120, 150, 185, 240, 300, 400, 500, 630];
        let closest = standardSections.reduce((prev, curr) => 
          Math.abs(curr - val) < Math.abs(prev - val) ? curr : prev
        );
        return closest;
      }
      return fallbackSec;
    }

    const SUPPORT_SPECS = {
      none: {
        name: "No Staffaggio",
        desc: "Nessun supporto CAD o mensola prevista per questo tratto di canalizzazione",
        swl10m: 0, swl15m: 0, swl20m: 0, swl25m: 0, swl30m: 0,
        armH: 0, strut: false, type: "none"
      },
      wall_light: {
        name: "Mensola Parete Leggera C30",
        desc: "Profilato a C 30x15 sp.1.5mm, fissaggio a parete con 1 tassello M8, indicata per carichi leggeri e segnali",
        swl10m: 55, swl15m: 35, swl20m: 22, swl25m: 14, swl30m: 10,
        armH: 8, strut: false, type: "wall"
      },
      wall_med: {
        name: "Mensola Rinforzata con Saetta",
        desc: "Braccio profilato 40x20 sp.2.0mm con saetta diagonale di rinforzo saldata, fissaggio a 2 tasselli M10",
        swl10m: 120, swl15m: 75, swl20m: 50, swl25m: 35, swl30m: 24,
        armH: 10, strut: true, type: "wall"
      },
      wall_heavy: {
        name: "Mensola Parete Pesante Doppia C50",
        desc: "Doppio profilato a C 50x30 sp.2.5mm con tirante superiore e piastra 4 fori M12 per canalizzazioni pesanti",
        swl10m: 240, swl15m: 150, swl20m: 100, swl25m: 75, swl30m: 55,
        armH: 14, strut: true, type: "wall"
      },
      ceiling_trap: {
        name: "Trapezio Soffitto 2xM10 + C40",
        desc: "Traversina a C 40x40 sospesa con 2 barre filettate M10 ancorate a solaio con tasselli chimici / antisfilamento",
        swl10m: 320, swl15m: 210, swl20m: 145, swl25m: 105, swl30m: 80,
        armH: 12, strut: false, type: "ceiling"
      },
      ceiling_single: {
        name: "Pendino Centrale Monotirante M10",
        desc: "Sospensione centrale a singola barra filettata M10 con bilancino basculante per canali compatti (L max 200mm)",
        swl10m: 70, swl15m: 45, swl20m: 30, swl25m: 20, swl30m: 12,
        armH: 10, strut: false, type: "ceiling"
      },
      floor_omega: {
        name: "Supporto a Pavimento su Omega C40",
        desc: "Profilato a Omega con piastre di base e piedini antivibranti in gomma per canalizzazioni a pavimento flottante / CED",
        swl10m: 260, swl15m: 180, swl20m: 130, swl25m: 95, swl30m: 70,
        armH: 15, strut: false, type: "floor"
      }
    };

    const MAT_SPECS = {
      sendzimir: { name: "Acciaio Zincato Sendzimir (UNI EN 10346)", stroke: "#94a3b8", fill: "#334155", aci: 8 },
      hdg:       { name: "Acciaio Zincato a Caldo HDG (EN 1461)", stroke: "#cbd5e1", fill: "#475569", aci: 9 },
      inox304:   { name: "Acciaio Inox AISI 304 Satinato", stroke: "#67e8f9", fill: "#164e63", aci: 4 },
      inox316:   { name: "Acciaio Inox AISI 316L Marino", stroke: "#38bdf8", fill: "#0c4a6e", aci: 5 },
      ral7035:   { name: "Verniciato Polveri RAL 7035", stroke: "#e2e8f0", fill: "#64748b", aci: 7 },
      ral3000:   { name: "Verniciato RAL 3000 Antincendio", stroke: "#f87171", fill: "#991b1b", aci: 1 }
    };

    let state = {
      pdfLoaded: false,
      theme: "dark",
      brand: "TIPOLOGIE",
      seriesId: "PAS_ASO",
      title: "NUOVA CANALINA 01 - L=200mm H=75mm",
      planTag: "SEZ. A-A | H=+3.20m",
      width: 200,
      height: 75,
      thk: 1.5,
      lengthM: 30,
      bends90: 0,
      tees: 0,
      reducers: 0,
      seismic: "none",
      trayType: "perforated",
      packing: "gravity",
      material: "sendzimir",
      cover: "snap",
      septums: 0,
      septumPositions: [],
      earth: "gv16",
      clearance: 150,
      reiCount: 0,
      edgeGuard: true,
      clamps: false,
      showIso3D: true,
      showPlanTag: false,
      showTableDesc: true,
      hideTableDesc: false,
      layoutMode: "standard",
      showCableCallouts: true,
      support: "none",
      spanM: 1.5,
      scale: "1:1",
      textScale: parseFloat(safeStorage.get("canaloCAD_textScale")) || 1.0,
      showLoadChart: true,
      showBOM: true,
      showCartiglio: true,
      tb: { client: "PROGETTO ESECUTIVO", project: "IMPIANTI ELETTRICI E SPECIALI", docCode: "PE-IMP-EL-PART-02", author: "STUDIO TECNICO", checker: "RESPONSABILE TECNICO", rev: "Rev. 00", date: new Date().toLocaleDateString("it-IT"), scaleRev: "Scala 1:1 - Rev.00" },
      cables: []
    };

    let unifilareDict = {};
    let metrics = {
      fillPct: 0, sparePct: 100, kr: 1, totalKgM: 0, qMaxKgM: 50,
      pointLoad: 0, maxPoint: 75, loadPct: 0,
      deflectionMm: 0.4, maxDeflectionMm: 7.5,
      pullDaN: 0, maxPullDaN: 100, maxBendR: 0, critCode: "-", fitR: 150,
      jouleWm: 0, maxDv: 0, bom: []
    };

    // Normalizza una sigla linea per garantire l'accoppiamento esatto tra DWG e PDF Unifilare
    function normalizeLineKey(str) {
      return String(str || "")
        .trim()
        .replace(/\s+/g, " ")
        .toUpperCase();
    }

    // Estrae il codice circuito base eliminando suffissi di fase / polo / conduttore unipolare
    function getBaseCircuitCode(str) {
      let s = String(str || "").trim();
      // Elimina indicazioni di formazione inline tra parentesi, es: " (4(1x10))" o " (1x185)"
      s = s.replace(/\s*\(\s*\d*\(?\d+[xXGg]\d+(?:[.,]\d+)?\)?\s*\)\s*$/i, "").trim();
      // Non tocca sigle temporanee generiche come CAVO-1 se iniziano con CAVO-
      if (/^CAVO-\d+$/i.test(s)) return s;
      // Elimina suffissi conduttore: es. - L1, -L2, L3, _N, - PE, (L1), (PE), .1, /2, (1), ecc.
      // ma NON intacca numerazioni di circuito come C-3 o C-12
      s = s.replace(/(?:[-_/\s]+(?:(?:FASE|COND|POLO)\s*)?(?:L[123]|F[123]|R|S|T|N|PE|TERRA)|[./]\d+|\(\s*(?:L[123]|F[123]|R|S|T|N|PE|TERRA|[1-9])\s*\))$/i, "").trim();
      return s;
    }

    function findInUnifilareDict(codeStr) {
      if (!codeStr) return null;
      const k1 = normalizeLineKey(codeStr);
      if (unifilareDict[k1]) return unifilareDict[k1];
      
      const base = getBaseCircuitCode(codeStr);
      const kBase = normalizeLineKey(base);
      if (unifilareDict[kBase]) return unifilareDict[kBase];

      // Fallback compatto senza spazi o caratteri speciali
      const kCompact = k1.replace(/[^A-Z0-9]/g, "");
      const kBaseCompact = kBase.replace(/[^A-Z0-9]/g, "");
      for (const [k, val] of Object.entries(unifilareDict)) {
        const c = k.replace(/[^A-Z0-9]/g, "");
        if (c === kCompact || c === kBaseCompact) return val;
      }

      // Ricerca robusta per numero di circuito (es. DWG "QGBT-C C-3 - L1" -> PDF "C-3" o "C3")
      const mCirc = base.match(/\b(?:C|L)-?(\d+)\b/i) || codeStr.match(/\b(?:C|L)-?(\d+)\b/i);
      if (mCirc) {
        const num = parseInt(mCirc[1], 10);
        for (const [k, val] of Object.entries(unifilareDict)) {
          const mk = k.match(/\b(?:C|L)-?(\d+)\b/i);
          if (mk && parseInt(mk[1], 10) === num) return val;
        }
      }
      return null;
    }

    function calcAllowedLinearLoadKgM(supportKey, spanM) {
      if (!supportKey || supportKey === "none") return 0;
      const spec = SUPPORT_SPECS[supportKey] || SUPPORT_SPECS.wall_med;
      const baseKgMAt15 = (spec.swl15m || 75) / 1.5;
      const hFactor = Math.pow(state.height / 75, 0.35) * Math.pow(state.thk / 1.5, 0.5);
      return +(baseKgMAt15 * Math.pow(1.5 / spanM, 1.55) * hFactor).toFixed(1);
    }

    function calcElasticDeflectionMm(qTotKgM, supportKey, spanM) {
      if (!supportKey || supportKey === "none") return 0;
      const qAmm = calcAllowedLinearLoadKgM(supportKey, spanM);
      const fLim = Math.min(10.0, (spanM * 1000) / 200);
      return +Math.min(25.0, fLim * (qTotKgM / Math.max(1, qAmm))).toFixed(2);
    }

// ---- 02-core.js
    // ============================================================================
    // 2. INIZIALIZZAZIONE E CONTROLLI INTERFACCIA
    // ============================================================================
    // Compatibilità con i vecchi nomi delle schede: ora i pannelli sono quelli della rail
    function showTab(id) {
      const map = { geom: "sezione", prod: "sezione", staff: "staffaggio", cavi: "cavi" };
      showPanel(map[id] || id, true);
    }

    // Niente costruttori: la "serie" della distinta è la tipologia di canalina scelta (state.trayType).
    function populateBrandSeries() {
      state.brand = "TIPOLOGIE";
      state.seriesId = BOM_SERIES_FOR(state.trayType).id;
      syncFromUI();
    }

    function initCatalogDropdown() {
      const sel = document.getElementById("selCableCatalog");
      if (!sel) return;
      sel.innerHTML = "";
      const categories = [
        { cat: "bt_uni",   label: "⚡ Cavi BT Unipolari (1X) — FG16M16 / FS17" },
        { cat: "bt_multi", label: "🔌 Cavi BT Multipolari — FG16OM16 / H07RN-F" },
        { cat: "signal",   label: "📡 Segnali, Comando & Bus KNX — FG16OH2R16 / LiYCY" },
        { cat: "fire",     label: "🔥 Resistenti al Fuoco PH120 & EVAC — FTG18OM16" },
        { cat: "data",     label: "🌐 Reti Dati LAN & Fibra Ottica — Cat.6A/7 / FO" }
      ];

      categories.forEach(group => {
        const grpEl = document.createElement("optgroup");
        grpEl.label = group.label;
        const items = CABLE_CATALOG.filter(c => c.cat === group.cat);
        items.forEach(c => {
          const opt = document.createElement("option");
          opt.value = c.id;
          opt.textContent = `${c.type} ${c.form} — ${c.desc} (Ø${c.d}mm, Iz0=${c.iz0}A)`;
          grpEl.appendChild(opt);
        });
        sel.appendChild(grpEl);
      });
    }

    function syncFromUI() {
      const getVal = (id, def) => {
        const el = document.getElementById(id);
        return el ? el.value : def;
      };
      const getNum = (id, def) => {
        const v = parseFloat(getVal(id, def));
        return isNaN(v) ? def : v;
      };
      const getChk = (id, def) => {
        const el = document.getElementById(id);
        return el ? el.checked : def;
      };

      const elTitle = document.getElementById("inpTitle");
      if (elTitle) state.title = elTitle.value;
      const elTitleQuick = document.getElementById("inpTitleQuick");
      if (elTitleQuick && document.activeElement === elTitleQuick) {
        state.title = elTitleQuick.value;
        if (elTitle) elTitle.value = state.title;
      }

      state.width = getNum("inpWidth", state.width);
      state.height = getNum("inpHeight", state.height);
      state.thk = getNum("inpThk", state.thk);
      state.lengthM = getNum("inpLength", state.lengthM);
      state.trayType = getVal("inpTrayType", state.trayType);
      state.packing = getVal("inpPacking", state.packing);
      state.planTag = getVal("inpPlanTag", state.planTag);
      state.edgeGuard = getChk("chkEdge", state.edgeGuard);
      state.clamps = getChk("chkClamps", state.clamps);
      state.showIso3D = getChk("chkIso3D", state.showIso3D);
      state.showPlanTag = getChk("chkPlanTag", state.showPlanTag);
      state.brand = "TIPOLOGIE";
      state.seriesId = BOM_SERIES_FOR(state.trayType).id;
      state.material = getVal("inpMaterial", state.material);
      state.cover = getVal("inpCover", state.cover);
      state.septums = parseInt(getVal("inpSeptums", state.septums || 0)) || 0;
      state.earth = getVal("inpEarth", state.earth);
      state.clearance = getNum("inpClearance", state.clearance);
      state.reiCount = getNum("inpRei", state.reiCount);
      state.support = getVal("inpSupport", state.support);
      state.spanM = getNum("inpSpan", state.spanM);
      state.seismic = getVal("inpSeismic", state.seismic);
      state.bends90 = getNum("inpBends", state.bends90);
      state.tees = getNum("inpTees", state.tees);
      state.reducers = getNum("inpReducers", state.reducers);

      const elSpanTxt = document.getElementById("lblSpan");
      if (elSpanTxt) elSpanTxt.textContent = `${state.spanM.toFixed(2)} m (${Math.round(state.spanM * 1000)} mm)`;

      updateSeptumUI();
      recalculateAll(); // ridisegna e sincronizza la scheda (in modo differito)
    }

    function resetProject() {
      state.width = 200;
      state.height = 75;
      state.bends90 = 0;
      state.tees = 0;
      state.reducers = 0;
      state.title = "NUOVA CANALINA 01 - L=200mm H=75mm";
      state.cables = [];
      state.septums = 0;
      state.septumPositions = [];
      state.showIso3D = true;
      state.showLoadChart = true;
      state.showBOM = true;
      state.showCartiglio = true;
      state.showTableDesc = true;
      state.hideTableDesc = false;
      state.layoutMode = "standard";
      state.showCableCallouts = true;
      state.support = "none";
      state.clamps = false;
      state.showPlanTag = false;
      state.pdfLoaded = false;
      pushStateToUI();
      recalculateAll();
      fitAll();
    }

    function swapDimensions() {
      const oldW = state.width;
      state.width = Math.max(50, state.height);
      state.height = Math.min(300, Math.max(35, oldW));
      state.title = state.title.replace(/L=\s*\d+\s*mm/i, `L=${state.width}mm`).replace(/H=\s*\d+\s*mm/i, `H=${state.height}mm`);
      pushStateToUI();
      recalculateAll();
      fitAll();
    }

    // Il tema riguarda solo il foglio CAD (sfondo e colori del disegno), non l'interfaccia
    function toggleTheme() {
      state.theme = state.theme === "dark" ? "light" : "dark";
      applyThemeUI();
      recalculateAll();
    }

    function applyThemeUI() {
      const bTheme = document.getElementById("btnTheme");
      if (bTheme) {
        const lbl = bTheme.querySelector("[data-theme-label]") || bTheme;
        lbl.textContent = state.theme === "dark" ? "Foglio: scuro" : "Foglio: chiaro";
      }
      const cadCanv = document.getElementById("cadCanvas");
      if (cadCanv) {
        cadCanv.classList.toggle("cad-grid-dark", state.theme !== "light");
        cadCanv.classList.toggle("cad-grid-light", state.theme === "light");
      }
    }

    function pushStateToUI() {
      const safeVal = (id, val) => { const el = document.getElementById(id); if (el) el.value = val; };
      const safeChk = (id, chk) => { const el = document.getElementById(id); if (el) el.checked = chk; };

      safeVal("inpTitle", state.title);
      safeVal("inpTitleQuick", state.title);
      safeVal("exportName", state.title.replace(/[^a-zA-Z0-9_-]+/g, "_").replace(/^_+|_+$/g, ""));
      safeVal("inpScale", state.scale || "1:1");
      safeVal("selDxfScale", state.scale || "1:1");
      safeVal("cfgScale", state.scale || "1:1");

      // Le option hanno valori "1.0", "1.2"...: String(1) darebbe "1" e lascerebbe la select vuota
      const valTextScale = (+state.textScale || 1.0).toFixed(1);
      safeVal("selTextScale", valTextScale);
      safeVal("optDxfTextScale", valTextScale);
      safeVal("cfgTextScale", valTextScale);

      safeChk("chkExportChart", state.showLoadChart !== false);
      safeChk("menuChkChart", state.showLoadChart !== false);
      safeChk("chkExportBOM", state.showBOM !== false);
      safeChk("menuChkBOM", state.showBOM !== false);
      safeChk("chkExportIso3D", state.showIso3D !== false);
      safeChk("menuChk3D", state.showIso3D !== false);
      safeChk("chkIso3D", state.showIso3D !== false);
      safeChk("chkExportCartiglio", state.showCartiglio !== false);
      safeChk("menuChkCartiglio", state.showCartiglio !== false);
      safeChk("chkExportTableDesc", state.showTableDesc !== false && !state.hideTableDesc);
      safeChk("menuChkTableDesc", state.showTableDesc !== false && !state.hideTableDesc);
      updateLayoutUI();
      updateCableCalloutsUI();

      safeVal("inpWidth", state.width);
      safeVal("inpHeight", state.height);
      safeVal("inpThk", (+state.thk || 1.5).toFixed(1)); // le option sono "1.5", "2.0", "2.5"
      safeVal("inpLength", state.lengthM);
      safeVal("inpTrayType", state.trayType);
      safeVal("inpPacking", state.packing);
      safeVal("inpPlanTag", state.planTag);
      safeChk("chkEdge", state.edgeGuard);
      safeChk("chkClamps", state.clamps);
      safeChk("chkPlanTag", state.showPlanTag);
      safeVal("inpMaterial", state.material);
      safeVal("inpCover", state.cover);
      safeVal("inpSeptums", String(state.septums || 0));

      updateSeptumUI();
      safeVal("inpEarth", state.earth);
      safeVal("inpClearance", String(state.clearance));
      safeVal("inpRei", state.reiCount);
      safeVal("inpSupport", state.support);
      safeVal("inpSpan", state.spanM);
      safeVal("inpSeismic", state.seismic);
      safeVal("inpBends", state.bends90);
      safeVal("inpTees", state.tees);
      safeVal("inpReducers", state.reducers);

      const elClient = document.getElementById("inpClient"); if (elClient) state.tb.client = elClient.value;
      const elDoc = document.getElementById("inpDocCode"); if (elDoc) state.tb.docCode = elDoc.value;
      const elAuthor = document.getElementById("inpAuthor"); if (elAuthor) state.tb.author = elAuthor.value;
      const elScaleRev = document.getElementById("inpScaleRev"); if (elScaleRev) state.tb.scaleRev = elScaleRev.value;
      const elScale = document.getElementById("inpScale"); if (elScale) state.scale = elScale.value || "1:1";

      state.title = state.title.replace(/L=\s*\d+\s*mm/i, `L=${state.width}mm`).replace(/H=\s*\d+\s*mm/i, `H=${state.height}mm`);
      safeVal("inpTitle", state.title);
      const hTitle = document.getElementById("headerTitle"); if (hTitle) hTitle.textContent = state.title;
      document.title = `${state.title} — ${APP_NAME}`;
      const elSpanTxt = document.getElementById("lblSpan"); if (elSpanTxt) elSpanTxt.textContent = `${state.spanM.toFixed(2)} m (${Math.round(state.spanM * 1000)} mm)`;
      const elElec = document.getElementById("kpiElectricalCard"); if (elElec) elElec.classList.toggle("hidden", !state.pdfLoaded);
      applyThemeUI();
      safeVal("exportName", state.title.replace(/[^a-zA-Z0-9_-]+/g, "_").replace(/^_+|_+$/g, ""));

      recalculateAll();
    }

    function getSeptumXList() {
      const n = state.septums || 0;
      if (n <= 0) {
        state.septumPositions = [];
        return [];
      }
      const W = state.width;
      if (!Array.isArray(state.septumPositions) || state.septumPositions.length !== n) {
        if (n === 1) {
          state.septumPositions = [Math.round(W * 0.6)];
        } else if (n === 2) {
          state.septumPositions = [Math.round(W * 0.34), Math.round(W * 0.68)];
        } else {
          state.septumPositions = [];
          for (let i = 1; i <= n; i++) {
            state.septumPositions.push(Math.round((W * i) / (n + 1)));
          }
        }
      }
      for (let i = 0; i < state.septumPositions.length; i++) {
        const minX = i === 0 ? 15 : state.septumPositions[i - 1] + 15;
        const maxX = i === state.septumPositions.length - 1 ? W - 15 : state.septumPositions[i + 1] - 15;
        if (state.septumPositions[i] < minX) state.septumPositions[i] = minX;
        if (state.septumPositions[i] > maxX) state.septumPositions[i] = maxX;
        state.septumPositions[i] = Math.round(state.septumPositions[i]);
      }
      return state.septumPositions;
    }

    function moveSeptum(idx, targetX) {
      if (idx < 0 || idx >= (state.septums || 0)) return;
      const W = state.width;
      const seps = getSeptumXList();
      const minX = idx === 0 ? 15 : seps[idx - 1] + 15;
      const maxX = idx === seps.length - 1 ? W - 15 : seps[idx + 1] - 15;
      const clampedX = Math.round(Math.max(minX, Math.min(maxX, targetX)));
      state.septumPositions[idx] = clampedX;

      // Mantieni i cavi distanziati dal setto per evitare sovrapposizioni grafiche
      const thkHalf = (state.thk || 1.5) / 2;
      state.cables.forEach(c => {
        if (c.x < 0) return;
        const r = c.d / 2;
        const safeD = r + thkHalf + 0.4;
        if (Math.abs(c.x - clampedX) < safeD) {
          if (c.x < clampedX) c.x = Math.max(r + 0.4, clampedX - safeD);
          else c.x = Math.min(W - r - 0.4, clampedX + safeD);
          c.origX = c.x;
        }
      });

      recalculateAll();
      drawScene();
      updateSeptumUI();
    }

    function updateSeptumUI() {
      const box = document.getElementById("septumPosControls");
      if (!box) return;
      if (!state.septums || state.septums <= 0) {
        box.classList.add("hidden");
        box.innerHTML = "";
        return;
      }
      box.classList.remove("hidden");
      const seps = getSeptumXList();
      const W = state.width;

      const existingInputs = box.querySelectorAll(".septum-range-input");
      if (existingInputs.length === seps.length) {
        seps.forEach((sx, idx) => {
          const rng = document.getElementById(`septumRange_${idx}`);
          const num = document.getElementById(`septumNum_${idx}`);
          const lbl = document.getElementById(`septumDimLabel_${idx}`);
          const minX = idx === 0 ? 15 : seps[idx - 1] + 15;
          const maxX = idx === seps.length - 1 ? W - 15 : seps[idx + 1] - 15;
          const wSx = idx === 0 ? sx : sx - seps[idx - 1];
          const wDx = idx === seps.length - 1 ? W - sx : seps[idx + 1] - sx;

          if (rng) {
            rng.min = minX; rng.max = maxX;
            if (document.activeElement !== rng) rng.value = sx;
          }
          if (num) {
            num.min = minX; num.max = maxX;
            if (document.activeElement !== num) num.value = sx;
          }
          if (lbl) {
            lbl.innerHTML = `SX: <strong class="text-white">${Math.round(wSx)}</strong>mm | DX: <strong class="text-white">${Math.round(wDx)}</strong>mm`;
          }
        });
        return;
      }

      let html = `<div class="text-[11px] font-semibold text-cyan-400 uppercase tracking-wider mb-1 flex items-center justify-between">
        <span>Regolazione Posizione Setti (Trascina nel CAD o Regola)</span>
      </div>`;
      seps.forEach((sx, idx) => {
        const minX = idx === 0 ? 15 : seps[idx - 1] + 15;
        const maxX = idx === seps.length - 1 ? W - 15 : seps[idx + 1] - 15;
        const wSx = idx === 0 ? sx : sx - seps[idx - 1];
        const wDx = idx === seps.length - 1 ? W - sx : seps[idx + 1] - sx;
        html += `
          <div class="bg-slate-900/90 p-1.5 rounded border border-slate-800">
            <div class="flex items-center justify-between text-[11px] text-slate-300 mb-1">
              <span class="font-bold text-cyan-300">Setto #${idx + 1}</span>
              <span id="septumDimLabel_${idx}" class="mono text-slate-400">SX: <strong class="text-white">${Math.round(wSx)}</strong>mm | DX: <strong class="text-white">${Math.round(wDx)}</strong>mm</span>
            </div>
            <div class="flex items-center gap-1.5">
              <input type="range" id="septumRange_${idx}" class="septum-range-input flex-1 accent-cyan-400 h-1.5 bg-slate-700 rounded cursor-pointer" min="${minX}" max="${maxX}" value="${sx}" oninput="moveSeptum(${idx}, parseFloat(this.value))">
              <input type="number" id="septumNum_${idx}" class="w-16 bg-slate-950 border border-slate-700 rounded px-1.5 py-0.5 text-[11px] mono text-cyan-300 text-center font-bold" min="${minX}" max="${maxX}" value="${sx}" oninput="moveSeptum(${idx}, parseFloat(this.value))">
              <span class="text-[11px] text-slate-400">mm</span>
            </div>
          </div>
        `;
      });
      box.innerHTML = html;
    }

    function isSpotFree(cable, x, y) {
      const W = state.width, H = state.height, r = cable.d / 2;
      if (x < r + 0.3 || x > W - r - 0.3) return false;
      if (y < r - 0.1 || y > H - r - 0.3) return false;

      for (const sx of getSeptumXList()) {
        if (Math.abs(x - sx) < r + state.thk / 2 + 0.3) return false;
      }

      for (const other of state.cables) {
        if (other === cable || other.x < 0) continue;
        if (Math.hypot(x - other.x, y - other.y) < r + other.d / 2 + 0.2) return false;
      }
      return true;
    }

    function resolveNonOverlappingPosition(dragCable, targetX, targetY) {
      const W = state.width, H = state.height, r = dragCable.d / 2;
      const clampedX = Math.max(r + 0.4, Math.min(W - r - 0.4, targetX));
      const clampedY = Math.max(r, Math.min(H - r - 0.4, targetY));

      if (isSpotFree(dragCable, clampedX, clampedY)) {
        return { x: +(clampedX.toFixed(1)), y: +(clampedY.toFixed(1)) };
      }

      const startX = dragCable.x, startY = dragCable.y;
      const dx = clampedX - startX, dy = clampedY - startY;
      const dist = Math.hypot(dx, dy);
      let bestX = startX, bestY = startY;

      if (dist > 0.05) {
        const steps = Math.min(70, Math.ceil(dist / 0.25));
        for (let s = 1; s <= steps; s++) {
          const tx = startX + (dx * s) / steps;
          const ty = startY + (dy * s) / steps;
          if (isSpotFree(dragCable, tx, ty)) {
            bestX = tx; bestY = ty;
          } else {
            break;
          }
        }
      }
      return { x: +(bestX.toFixed(1)), y: +(bestY.toFixed(1)) };
    }

    function findFreePositionNear(cable, targetX, targetY) {
      const W = state.width, H = state.height, r = cable.d / 2;
      const defX = Number.isFinite(targetX) ? targetX : (Number.isFinite(cable.x) && cable.x >= 0 ? cable.x : W / 2);
      const defY = Number.isFinite(targetY) ? targetY : (Number.isFinite(cable.y) && cable.y >= 0 ? cable.y : r + 1);
      const cx = Math.max(r + 0.4, Math.min(W - r - 0.4, defX));
      const cy = Math.max(r, Math.min(H - r - 0.4, defY));
      if (isSpotFree(cable, cx, cy)) return { x: +(cx.toFixed(1)), y: +(cy.toFixed(1)) };

      for (let rad = 0.8; rad <= Math.max(W, H); rad += 0.8) {
        for (let a = 0; a < Math.PI * 2; a += Math.PI / 10) {
          const tx = cx + Math.cos(a) * rad;
          const ty = cy + Math.sin(a) * rad;
          if (isSpotFree(cable, tx, ty)) return { x: +(tx.toFixed(1)), y: +(ty.toFixed(1)) };
        }
      }
      return { x: +(cx.toFixed(1)), y: +(cy.toFixed(1)) };
    }

    function packByGravity(list, xMin, xMax) {
      const placed = [];
      const trefoilBuckets = {};
      const singleCables = [];
      list.forEach(c => {
        if (c.trefoil && c.trefoilGroupId) {
          if (!trefoilBuckets[c.trefoilGroupId]) trefoilBuckets[c.trefoilGroupId] = [];
          trefoilBuckets[c.trefoilGroupId].push(c);
        } else {
          singleCables.push(c);
        }
      });

      const packUnits = [];
      Object.entries(trefoilBuckets).forEach(([gid, group]) => {
        if (group.length === 3) {
          const d0 = group[0].d;
          packUnits.push({ isTrefoil: true, d: d0 * 2.02, baseD: d0, members: group });
        } else {
          group.forEach(c => packUnits.push({ isTrefoil: false, d: c.d, cable: c }));
        }
      });
      singleCables.forEach(c => packUnits.push({ isTrefoil: false, d: c.d, cable: c }));

      const sorted = packUnits.sort((a, b) => b.d - a.d);
      sorted.forEach(u => {
        if (u.isTrefoil) {
          const r = u.baseD / 2;
          const spanX = 2 * r;
          const startX = xMin + spanX + 0.6;
          const endX = Math.max(startX, xMax - spanX - 0.6);
          let bestX = startX, bestY = r, bestScore = 1e9;
          for (let tx = startX; tx <= endX; tx += 0.6) {
            let ty = r;
            const testCenters = [
              { dx: -r, dy: 0, r },
              { dx: +r, dy: 0, r },
              { dx: 0,  dy: r * 1.73205, r }
            ];
            for (const p of placed) {
              for (const tc of testCenters) {
                const distX = Math.abs((tx + tc.dx) - p.x);
                const minD = tc.r + p.d / 2 + 0.35;
                if (distX < minD) {
                  const reqBaseY = p.y + Math.sqrt(minD * minD - distX * distX) - tc.dy;
                  if (reqBaseY > ty) ty = reqBaseY;
                }
              }
            }
            const score = ty * 100 + tx;
            if (score < bestScore) { bestScore = score; bestX = tx; bestY = ty; }
          }
          const c0 = u.members[0], c1 = u.members[1], c2 = u.members[2];
          c0.x = +((bestX - r).toFixed(1)); c0.y = +(bestY.toFixed(1));
          c1.x = +((bestX + r).toFixed(1)); c1.y = +(bestY.toFixed(1));
          c2.x = +(bestX.toFixed(1));       c2.y = +((bestY + r * 1.73205).toFixed(1));
          placed.push(c0, c1, c2);
        } else {
          const c = u.cable;
          const r = c.d / 2;
          const startX = xMin + r + 0.5;
          const endX = Math.max(startX, xMax - r - 0.5);
          let bestX = startX, bestY = r, bestScore = 1e9;
          for (let tx = startX; tx <= endX; tx += 0.6) {
            let ty = r;
            for (const p of placed) {
              const dx = Math.abs(tx - p.x);
              const minD = r + p.d / 2 + 0.4;
              if (dx < minD) ty = Math.max(ty, p.y + Math.sqrt(minD * minD - dx * dx));
            }
            const score = ty * 100 + tx;
            if (score < bestScore) { bestScore = score; bestX = tx; bestY = ty; }
          }
          c.x = +(bestX.toFixed(1));
          c.y = +(bestY.toFixed(1));
          placed.push(c);
        }
      });
    }

    function recalculateAll() {
      const W = state.width, H = state.height;
      if (state.packing === "input") {
        state.cables.forEach(c => { c.x = -999; c.y = -999; });
        state.cables.forEach(c => {
          const pos = findFreePositionNear(c, c.origX, c.origY);
          c.x = pos.x; c.y = pos.y;
        });
      } else {
        const seps = getSeptumXList();
        const hasSpecialElv = state.cables.some(c => c.sys === "SPECIAL_ELV");
        const tHalf = (state.thk || 1.5) / 2;
        if (seps.length === 0) {
          packByGravity(state.cables, 0, W);
        } else if (hasSpecialElv) {
          if (seps.length === 1) {
            packByGravity(state.cables.filter(c => c.sys !== "SPECIAL_ELV"), 0, seps[0] - tHalf);
            packByGravity(state.cables.filter(c => c.sys === "SPECIAL_ELV"), seps[0] + tHalf, W);
          } else {
            packByGravity(state.cables.filter(c => c.sys === "BT_POWER"), 0, seps[0] - tHalf);
            packByGravity(state.cables.filter(c => c.sys === "BT_FIRE"), seps[0] + tHalf, seps[1] - tHalf);
            packByGravity(state.cables.filter(c => c.sys === "SPECIAL_ELV"), seps[1] + tHalf, W);
          }
        } else {
          // Presenza di setti con soli cavi BT: rispetta la segregazione fisica dei setti
          if (seps.length === 1) {
            const leftCables = state.cables.filter(c => c.x >= 0 && c.x < seps[0]);
            const rightCables = state.cables.filter(c => c.x >= seps[0]);
            if (leftCables.length === 0 && rightCables.length === 0) {
              packByGravity(state.cables, 0, seps[0] - tHalf);
            } else {
              packByGravity(leftCables, 0, seps[0] - tHalf);
              packByGravity(rightCables, seps[0] + tHalf, W);
            }
          } else {
            const c1 = state.cables.filter(c => c.x >= 0 && c.x < seps[0]);
            const c2 = state.cables.filter(c => c.x >= seps[0] && c.x < seps[1]);
            const c3 = state.cables.filter(c => c.x >= seps[1]);
            if (c1.length === 0 && c2.length === 0 && c3.length === 0) {
              packByGravity(state.cables, 0, seps[0] - tHalf);
            } else {
              packByGravity(c1, 0, seps[0] - tHalf);
              packByGravity(c2, seps[0] + tHalf, seps[1] - tHalf);
              packByGravity(c3, seps[1] + tHalf, W);
            }
          }
        }
      }

      const trayArea = W * H;
      let usedArea = 0, cablesKgM = 0, cuArea = 0, maxBendR = 0, critCode = "-", jouleWm = 0, maxDv = 0;

      // Rilevamento automatico degli strati verticali (CEI UNEL 35024/1)
      const yLayers = [];
      state.cables.forEach(c => {
        if (!yLayers.some(yl => Math.abs(yl - c.y) < c.d * 0.72)) yLayers.push(c.y);
      });
      const nLayers = Math.max(1, yLayers.length);
      const nCirc = state.cables.length;
      const kFascio = nCirc <= 1 ? 1.0 : nCirc <= 2 ? 0.85 : nCirc <= 3 ? 0.79 : nCirc <= 6 ? 0.75 : nCirc <= 9 ? 0.72 : 0.68;
      const kStrati = nLayers === 1 ? 1.0 : nLayers === 2 ? 0.80 : 0.73;
      const kPosa = (state.trayType === "solid" && state.cover !== "none") ? 0.85
                  : (state.cover === "snap" || state.cover === "clamped") ? 0.90
                  : (state.trayType === "mesh" || state.trayType === "ladder") ? 1.00 : 0.96;
      const kr = +(kFascio * kStrati * kPosa).toFixed(2);

      let deratingFailCode = null;
      state.cables.forEach(c => {
        usedArea += Math.PI * Math.pow(c.d / 2, 2);
        cablesKgM += c.w;
        cuArea += (c.sec || 2.5) * (c.cores || 2);
        const rMin = Math.round(c.d * (c.bendK || 10));
        if (rMin > maxBendR) { maxBendR = rMin; critCode = c.code; }
        if (state.pdfLoaded && c.ib > 0) {
          const pj = (c.cores || 2) * (0.0214 / Math.max(0.5, c.sec || 2.5)) * Math.pow(c.ib, 2);
          jouleWm += pj;
          const kP = c.cores === 3 ? 1.732 : 2.0;
          const vN = c.cores === 3 ? 400 : 230;
          const dv = ((kP * state.lengthM * c.ib * (0.0214 / Math.max(0.5, c.sec || 2.5)) * 0.9) / vN) * 100;
          if (dv > maxDv) maxDv = dv;
          if ((c.iz0 || 27) * kr < c.ib && !deratingFailCode) deratingFailCode = c.code;
        }
      });

      // Verifica Segregazione Normativa CEI 64-8 Art. 528.1 per ciascuno scomparto
      const sepBounds = [0, ...getSeptumXList(), W];
      let segConform = true;
      let segDetail = `Art. 528.1 OK (Kr=${kr} su ${nLayers} str.)`;
      const hasAnySignal = state.cables.some(c => c.sys === "SPECIAL_ELV");

      if (!hasAnySignal) {
        segConform = true;
        segDetail = `Solo Cavi BT Potenza (Kr=${kr} su ${nLayers} str.)`;
      } else {
        for (let sIdx = 0; sIdx < sepBounds.length - 1; sIdx++) {
          const x0 = sepBounds[sIdx], x1 = sepBounds[sIdx + 1];
          const compCables = state.cables.filter(c => c.x >= x0 - 0.5 && c.x <= x1 + 0.5);
          const hasPower = compCables.some(c => c.sys === "BT_POWER" || c.sys === "BT_FIRE");
          const unshieldedElv = compCables.filter(c => c.sys === "SPECIAL_ELV" && !/FTG18|FG16|0,6\/1|1kV|C-4|SCHERM|OH2/i.test(c.type));
          if (hasPower && unshieldedElv.length > 0) {
            segConform = false;
            segDetail = `Conflitto ${unshieldedElv[0].code}: inserire Setto!`;
            break;
          }
        }
      }

      const fillPct = trayArea > 0 ? +((usedArea / trayArea) * 100).toFixed(1) : 0;
      const sparePct = +(Math.max(0, 100 - fillPct)).toFixed(1);
      const unfolded = W + 2 * H + 16 + (state.cover !== "none" ? W + 20 : 0);
      const trayKgM = +(unfolded * state.thk * 0.00785 * (state.trayType === "perforated" ? 0.85 : 1.0)).toFixed(2);
      const totalKgM = +(cablesKgM + trayKgM).toFixed(2);

      const hasSupport = state.support && state.support !== "none";
      const supSpec = SUPPORT_SPECS[state.support] || SUPPORT_SPECS.none;
      const qMaxKgM = hasSupport ? calcAllowedLinearLoadKgM(state.support, state.spanM) : 999;
      const pointLoad = +(totalKgM * state.spanM).toFixed(1);
      const maxPoint = hasSupport ? Math.round(qMaxKgM * state.spanM) : 0;
      const loadPct = hasSupport && qMaxKgM > 0 ? +((totalKgM / qMaxKgM) * 100).toFixed(1) : 0;
      const maxDeflectionMm = +Math.min(10.0, (state.spanM * 1000) / 200).toFixed(1);
      const deflectionMm = hasSupport ? calcElasticDeflectionMm(totalKgM, state.support, state.spanM) : 0;

      const pullDaN = +(cablesKgM * state.lengthM * 0.35 * Math.exp(0.35 * state.bends90 * (Math.PI / 2))).toFixed(1);
      const maxPullDaN = Math.round(Math.max(15, cuArea * 5.0));
      const fitR = maxBendR <= 150 ? 150 : maxBendR <= 300 ? 300 : 600;

      const nVerghe = Math.ceil(state.lengthM / 3);
      const nStaffe = Math.ceil(state.lengthM / state.spanM) + 1;
      // Tipologia di canalina (nessun costruttore).
      const serObj = BOM_SERIES_FOR(state.trayType);
      const pCode = serObj.code;

      const freeAreaCm2 = Math.max(0, Math.round((trayArea - usedArea) / 100));
      const nSacchettiRei = Math.max(2, Math.ceil(freeAreaCm2 / 35)) * state.reiCount;

      const bom = [
        { code: `${pCode}-${W}x${H}`, desc: `${serObj.name} L=${W}xH=${H}mm sp.${state.thk}mm`, qty: nVerghe * 3, um: "m", note: `${nVerghe} verghe da 3m` },
        ...(state.cover !== "none" ? [{ code: `${pCode}-COP-${W}`, desc: `Coperchio superiore (${state.cover}) L=${W}mm`, qty: nVerghe * 3, um: "m", note: "Inclusi fermacoperchi" }] : []),
        ...(state.septums > 0 ? [{ code: `${pCode}-SET-H${H}`, desc: `Setto separatore longitudinale H=${H}mm (${state.septums} file)`, qty: nVerghe * 3 * state.septums, um: "m", note: "Segregazione CEI 64-8" }] : []),
        { code: `${pCode}-GIU-M6`, desc: `Kit giunzione lineare rinforzata con bulloneria M6`, qty: nVerghe, um: "cad", note: "1 ogni 3m" },
        ...(hasSupport ? [{ code: `${pCode}-STF-${W}`, desc: `${supSpec.name} L=${W + 50}mm (Passo Ls=${state.spanM}m)`, qty: nStaffe, um: "cad", note: `Inclusi ${nStaffe * 2} tasselli M10` }] : []),
        ...(state.bends90 > 0 ? [{ code: `${pCode}-CRV90-${W}-R${fitR}`, desc: `Curva piana 90° L=${W}mm H=${H}mm (R_acc=${fitR}mm >= R_min=${maxBendR}mm)`, qty: state.bends90, um: "cad", note: `Cavo critico: ${critCode}` }] : []),
        ...(state.tees > 0 ? [{ code: `${pCode}-DEVT-${W}`, desc: `Deviazione piana a T L=${W}mm H=${H}mm completa di giunti`, qty: state.tees, um: "cad", note: "Raggio R=" + fitR + "mm" }] : []),
        ...(state.reducers > 0 ? [{ code: `${pCode}-RID-${W}`, desc: `Riduzione simmetrica/asimmetrica di larghezza L=${W}mm`, qty: state.reducers, um: "cad", note: "Acciaio zincato" }] : []),
        ...(state.seismic !== "none" ? [{ code: `${pCode}-SISM-${state.seismic.toUpperCase()}`, desc: `Kit controvento antisismico NTC 2018 (${state.seismic === "cable_9m" ? "Funi 45° ogni 9m" : "Strut rigido 45° ogni 6m"})`, qty: Math.max(1, Math.ceil(state.lengthM / (state.seismic === "cable_9m" ? 9 : 6))), um: "kit", note: "Rif. NTC 2018" }] : []),
        ...(state.reiCount > 0 ? [{ code: `FIRE-EI120-${W}x${H}`, desc: `Barriera tagliafuoco intumescente EI120 (cuscini + mastice endotermico per ${freeAreaCm2} cm² netti/varco)`, qty: state.reiCount, um: "varco", note: `${nSacchettiRei} cuscini intumescenti tot.` }] : []),
        ...(state.earth !== "none" ? [{ code: `${pCode}-PE-16`, desc: `Morsetto equipotenziale + capocorda M6 + conduttore PE (${state.earth.toUpperCase()})`, qty: Math.ceil(state.lengthM / 12) + 1, um: "cad", note: "1 ogni 12m" }] : [])
      ];

      metrics = {
        fillPct, sparePct, kr, nLayers, kFascio, kStrati, kPosa, segConform, segDetail,
        totalKgM, qMaxKgM, pointLoad, maxPoint, loadPct,
        deflectionMm, maxDeflectionMm,
        pullDaN, maxPullDaN, maxBendR, critCode, fitR,
        jouleWm: +jouleWm.toFixed(1), maxDv: +maxDv.toFixed(2), bom,
        deratingFailCode
      };
      state.metrics = metrics;
      if (typeof window !== "undefined") window.metrics = metrics;

      const safeSetText = (id, val) => { const el = document.getElementById(id); if (el) el.textContent = val; };
      safeSetText("kpiAreaUsed", Math.round(usedArea));
      safeSetText("kpiAreaTot", Math.round(trayArea));
      safeSetText("kpiFill", `${fillPct}%`);
      safeSetText("kpiSpare", `Riserva +${sparePct}%`);
      safeSetText("kpiLoadPct", `${loadPct}%`);
      safeSetText("kpiSpan", `${state.spanM.toFixed(2)}m`);
      const kpiLoadEl = document.getElementById("kpiLoadTxt");
      if (kpiLoadEl) {
        if (!hasSupport) {
          kpiLoadEl.textContent = "No Staffaggio";
          kpiLoadEl.className = "text-[11px] text-slate-400";
          safeSetText("kpiLoadPct", "N/D");
        } else {
          kpiLoadEl.textContent = loadPct <= 100 ? `${pointLoad} kg / ${maxPoint} kg OK` : `SOVRACCARICO (${pointLoad}/${maxPoint} kg)!`;
          kpiLoadEl.className = loadPct <= 100 ? "text-[11px] text-emerald-400" : "text-[11px] text-red-400 font-bold";
        }
      }
      safeSetText("kpiPull", pullDaN);
      safeSetText("kpiBendR", maxBendR);
      const pullOk = pullDaN <= maxPullDaN;
      const pullTxtEl = document.getElementById("kpiPullTxt");
      if (pullTxtEl) {
        pullTxtEl.textContent = pullOk
          ? `Tiro OK (<= ${maxPullDaN} daN) | R_acc=${fitR}mm`
          : `TIRO ECCESSIVO (> ${maxPullDaN} daN)!`;
        pullTxtEl.className = pullOk ? "text-[11px] text-emerald-400" : "text-[11px] text-red-400 font-bold";
      }
      safeSetText("kpiReiTxt", state.reiCount > 0 ? `${state.reiCount} Varchi EI120 (${nSacchettiRei} cuscini)` : "0 Varchi EI120");
      const seisTxtEl = document.getElementById("kpiSeisTxt");
      if (seisTxtEl) {
        seisTxtEl.textContent = state.seismic === "none" ? "NTC 2018: Standard" : `NTC 2018: ${ Math.max(1, Math.ceil(state.lengthM / (state.seismic === "cable_9m" ? 9 : 6))) } Kit Sismici`;
        seisTxtEl.className = state.seismic === "none" ? "text-[11px] text-slate-500" : "text-[11px] text-amber-300 font-semibold";
      }

      if (state.pdfLoaded) {
        safeSetText("kpiJoule", metrics.jouleWm);
        safeSetText("kpiDv", `${metrics.maxDv}%`);
        const deratEl = document.getElementById("kpiDeratTxt");
        if (deratEl) {
          deratEl.textContent = deratingFailCode ? `ATTENZIONE: Iz' < Ib su ${deratingFailCode} (Kr=${kr})` : `Kr=${kr} (${nLayers} str.) • Iz'=Iz·Kr >= Ib OK`;
          deratEl.className = deratingFailCode ? "text-[11px] text-red-400 font-bold" : "text-[11px] text-emerald-400";
        }
      }

      const segCard = document.getElementById("kpiSegCard");
      const segTitle = document.getElementById("kpiSegTitle");
      if (segCard && segTitle) {
        const segSub = (typeof segCard.querySelector === "function") ? segCard.querySelector("div:last-child") : null;
        if (segConform) {
          segCard.className = "bg-slate-900 border border-emerald-500/40 rounded-xl p-2.5";
          segTitle.textContent = "CONFORME";
          segTitle.className = "text-xs font-bold text-emerald-400 mt-0.5";
          if (segSub) segSub.textContent = segDetail;
        } else {
          segCard.className = "bg-red-950/30 border border-red-500/60 rounded-xl p-2.5";
          segTitle.textContent = "NON CONFORME (528.1)";
          segTitle.className = "text-xs font-bold text-red-400 mt-0.5 animate-pulse";
          if (segSub) segSub.textContent = segDetail;
        }
      }

      // Tabella, grafico e scena vengono ridisegnati al prossimo frame (più chiamate ravvicinate = 1 solo render);
      // la copia nella scheda, il salvataggio automatico e la cronologia Annulla sono differiti.
      requestRender({ table: true, chart: true, scene: true });
      scheduleSectionSync();
    }

// ---- 03-loadcurve-cables.js
    // ============================================================================
    // 4. WIDGET INTERATTIVO DEDICATO CURVA DI CARICO & FRECCIA (CEI EN 61537)
    // ============================================================================
    const lcCanvas = document.getElementById("loadCurveCanvas");
    const lcCtx = lcCanvas.getContext("2d");
    let isDraggingSpanOnChart = false;
    // Coordinate logiche del grafico (380x175): il buffer reale viene scalato su larghezza CSS x devicePixelRatio
    const LC_W = 380, LC_H = 175;

    function drawInteractiveLoadCurveWidget() {
      const dpr = window.devicePixelRatio || 1;
      const cssW = lcCanvas.clientWidth || LC_W;
      const bw = Math.round(cssW * dpr), bh = Math.round(cssW * dpr * LC_H / LC_W);
      if (lcCanvas.width !== bw || lcCanvas.height !== bh) { lcCanvas.width = bw; lcCanvas.height = bh; }
      lcCtx.setTransform(bw / LC_W, 0, 0, bh / LC_H, 0, 0);
      // Il grafico funziona anche da slider: valore corrente per tastiera e screen reader
      lcCanvas.setAttribute("aria-valuemin", "1");
      lcCanvas.setAttribute("aria-valuemax", "3");
      lcCanvas.setAttribute("aria-valuenow", String(state.spanM));
      lcCanvas.setAttribute("aria-valuetext", `${String(state.spanM.toFixed(2)).replace(".", ",")} metri`);
      const w = LC_W, h = LC_H;
      lcCtx.clearRect(0, 0, w, h);

      if (state.support === "none") {
        lcCtx.fillStyle = "#64748b";
        lcCtx.font = "11px 'JetBrains Mono', monospace";
        lcCtx.textAlign = "center";
        lcCtx.fillText("Staffaggio escluso (No Staffaggio)", w / 2, h / 2 - 6);
        lcCtx.font = "10px 'JetBrains Mono', monospace";
        lcCtx.fillStyle = "#475569";
        lcCtx.fillText("Seleziona una mensola o pendino per calcolare la curva SWL", w / 2, h / 2 + 10);
        return;
      }

      const padL = 38, padR = 36, padT = 18, padB = 26;
      const gw = w - padL - padR, gh = h - padT - padB;

      const qAt1m = calcAllowedLinearLoadKgM(state.support, 1.0);
      const maxScaleY = Math.max(25, Math.ceil(Math.max(qAt1m, metrics.totalKgM * 1.3) / 10) * 10);

      lcCtx.lineWidth = 1;
      lcCtx.font = "10px 'JetBrains Mono', monospace";
      for (let i = 0; i <= 4; i++) {
        const frac = i / 4;
        const y = padT + gh - frac * gh;
        lcCtx.strokeStyle = "#1e293b";
        lcCtx.beginPath(); lcCtx.moveTo(padL, y); lcCtx.lineTo(padL + gw, y); lcCtx.stroke();
        lcCtx.fillStyle = "#94a3b8";
        lcCtx.textAlign = "right";
        lcCtx.fillText(`${Math.round(frac * maxScaleY)}`, padL - 5, y + 3);
      }

      const spans = [1.0, 1.5, 2.0, 2.5, 3.0];
      spans.forEach(s => {
        const x = padL + ((s - 1.0) / 2.0) * gw;
        lcCtx.strokeStyle = "#1e293b";
        lcCtx.beginPath(); lcCtx.moveTo(x, padT); lcCtx.lineTo(x, padT + gh); lcCtx.stroke();
        lcCtx.fillStyle = "#94a3b8";
        lcCtx.textAlign = "center";
        lcCtx.fillText(`${s.toFixed(1)}m`, x, padT + gh + 14);
      });

      lcCtx.beginPath();
      lcCtx.moveTo(padL, padT + gh);
      for (let s = 1.0; s <= 3.001; s += 0.05) {
        const x = padL + ((s - 1.0) / 2.0) * gw;
        const q = Math.min(maxScaleY, calcAllowedLinearLoadKgM(state.support, s));
        const y = padT + gh - (q / maxScaleY) * gh;
        lcCtx.lineTo(x, y);
      }
      lcCtx.lineTo(padL + gw, padT + gh);
      lcCtx.closePath();
      lcCtx.fillStyle = "rgba(34, 197, 94, 0.12)";
      lcCtx.fill();

      lcCtx.beginPath();
      for (let s = 1.0; s <= 3.001; s += 0.05) {
        const x = padL + ((s - 1.0) / 2.0) * gw;
        const q = Math.min(maxScaleY, calcAllowedLinearLoadKgM(state.support, s));
        const y = padT + gh - (q / maxScaleY) * gh;
        if (s === 1.0) lcCtx.moveTo(x, y); else lcCtx.lineTo(x, y);
      }
      lcCtx.strokeStyle = "#22c55e";
      lcCtx.lineWidth = 2.2;
      lcCtx.stroke();

      lcCtx.beginPath();
      lcCtx.setLineDash([4, 3]);
      for (let s = 1.0; s <= 3.001; s += 0.05) {
        const x = padL + ((s - 1.0) / 2.0) * gw;
        const defl = calcElasticDeflectionMm(metrics.totalKgM, state.support, s);
        const y = padT + gh - Math.min(1.0, defl / 10.0) * gh;
        if (s === 1.0) lcCtx.moveTo(x, y); else lcCtx.lineTo(x, y);
      }
      lcCtx.strokeStyle = "#38bdf8";
      lcCtx.lineWidth = 1.5;
      lcCtx.stroke();
      lcCtx.setLineDash([]);

      lcCtx.fillStyle = "#38bdf8";
      lcCtx.textAlign = "left";
      lcCtx.fillText("10mm", padL + gw + 4, padT + 4);
      lcCtx.fillText("5mm", padL + gw + 4, padT + gh * 0.5 + 3);
      lcCtx.fillText("0mm", padL + gw + 4, padT + gh + 3);

      const yWork = padT + gh - Math.min(1.0, metrics.totalKgM / maxScaleY) * gh;
      lcCtx.strokeStyle = "rgba(251, 191, 36, 0.45)";
      lcCtx.lineWidth = 1.2;
      lcCtx.beginPath(); lcCtx.moveTo(padL, yWork); lcCtx.lineTo(padL + gw, yWork); lcCtx.stroke();

      const xWork = padL + ((state.spanM - 1.0) / 2.0) * gw;
      const yLim = padT + gh - Math.min(1.0, metrics.qMaxKgM / maxScaleY) * gh;
      const isOk = metrics.totalKgM <= metrics.qMaxKgM;

      lcCtx.strokeStyle = "#f59e0b";
      lcCtx.lineWidth = 1.4;
      lcCtx.beginPath(); lcCtx.moveTo(xWork, padT); lcCtx.lineTo(xWork, padT + gh); lcCtx.stroke();

      lcCtx.beginPath(); lcCtx.arc(xWork, yLim, 4, 0, Math.PI * 2);
      lcCtx.fillStyle = "#22c55e"; lcCtx.fill();

      lcCtx.beginPath(); lcCtx.arc(xWork, yWork, 5.5, 0, Math.PI * 2);
      lcCtx.fillStyle = isOk ? "#fbbf24" : "#ef4444";
      lcCtx.fill();
      lcCtx.strokeStyle = "#0f172a"; lcCtx.lineWidth = 1.5; lcCtx.stroke();

      const setEl = (id, val) => { const el = document.getElementById(id); if (el) el.textContent = val; };
      setEl("lblChartQtot", `${metrics.totalKgM} kg/m`);
      setEl("lblChartQamm", `${metrics.qMaxKgM} kg/m`);
      setEl("lblChartDefl", `${metrics.deflectionMm} / ${metrics.maxDeflectionMm} mm`);

      const badge = document.getElementById("curveStatusBadge");
      if (badge) {
        if (isOk) {
          badge.textContent = `OK (${metrics.loadPct}%)`;
          badge.className = "text-[11px] mono px-1.5 py-0.5 rounded bg-emerald-500/20 text-emerald-300 font-bold";
        } else {
          badge.textContent = `SOVRACCARICO (${metrics.loadPct}%)`;
          badge.className = "text-[11px] mono px-1.5 py-0.5 rounded bg-red-500/20 text-red-300 font-bold";
        }
      }
    }

    function setSpanM(v) {
      state.spanM = Math.round(Math.max(1, Math.min(3, v)) * 20) / 20; // passo 0.05 m, come lo slider
      const inpSpanEl = document.getElementById("inpSpan"); if (inpSpanEl) inpSpanEl.value = state.spanM;
      const lblSpanEl = document.getElementById("lblSpan"); if (lblSpanEl) lblSpanEl.textContent = `${state.spanM.toFixed(2)} m (${Math.round(state.spanM * 1000)} mm)`;
      recalculateAll();
    }

    function updateSpanFromChartMouse(e) {
      const rect = lcCanvas.getBoundingClientRect();
      const mx = ((e.clientX - rect.left) / Math.max(1, rect.width)) * LC_W;
      const padL = 38, padR = 36;
      const gw = LC_W - padL - padR;
      const frac = Math.max(0, Math.min(1, (mx - padL) / gw));
      setSpanM(1.0 + frac * 2.0);
    }

    // Pointer Events: stesso codice per mouse, penna e touch
    lcCanvas.style.touchAction = "none";
    lcCanvas.addEventListener("pointerdown", (e) => {
      isDraggingSpanOnChart = true;
      try { lcCanvas.setPointerCapture(e.pointerId); } catch (err) {}
      updateSpanFromChartMouse(e);
    });
    lcCanvas.addEventListener("pointermove", (e) => { if (isDraggingSpanOnChart) updateSpanFromChartMouse(e); });
    const endChartDrag = () => { isDraggingSpanOnChart = false; };
    lcCanvas.addEventListener("pointerup", endChartDrag);
    lcCanvas.addEventListener("pointercancel", endChartDrag);
    // Tastiera: frecce per variare il passo quando il grafico ha il focus
    lcCanvas.tabIndex = 0;
    lcCanvas.setAttribute("role", "slider");
    lcCanvas.setAttribute("aria-label", "Passo staffaggio Ls (metri)");
    lcCanvas.addEventListener("keydown", (e) => {
      if (e.key === "ArrowRight" || e.key === "ArrowUp") { e.preventDefault(); setSpanM(state.spanM + 0.05); }
      else if (e.key === "ArrowLeft" || e.key === "ArrowDown") { e.preventDefault(); setSpanM(state.spanM - 0.05); }
    });

    function addCableFromPortfolio() {
      const id = document.getElementById("selCableCatalog").value;
      const qty = Math.max(1, Math.min(30, parseInt(document.getElementById("inpCableQty").value, 10) || 1));
      const customCode = document.getElementById("inpCableCode").value.trim();
      const trefoil = document.getElementById("chkTrefoil").checked;
      const item = CABLE_CATALOG.find(c => c.id === id) || CABLE_CATALOG[0];
      const effectiveQty = (trefoil && qty === 1 && isUnipolarCable(item)) ? 3 : qty;
      const batchId = Date.now();

      for (let q = 0; q < effectiveQty; q++) {
        const idx = state.cables.length + 1;
        const baseCode = customCode ? customCode : item.code;
        const phaseLabels = ["L1", "L2", "L3"];
        const suffix = trefoil ? phaseLabels[q % 3] : `${q + 1}`;
        const code = (customCode && effectiveQty > 1) || trefoil ? `${baseCode}-${suffix}` : baseCode;
        const trefoilGroupId = trefoil ? `TRF_${batchId}_${Math.floor(q / 3)}` : null;
        
        const uInfo = findInUnifilareDict(code) || findInUnifilareDict(baseCode);
        // Una descrizione è utile solo se contiene almeno una parola vera (es. non solo "mm²")
        const isMeaningful = (s) => !!s && /[A-Za-zÀ-ÿ]{3,}/.test(String(s).replace(/mm²|mm2/gi, ""));
        let finalDesc = (uInfo && uInfo.desc) || "";
        if (!isMeaningful(finalDesc) && item.desc) {
          const cd = cleanUtenzaDescription(item.desc, baseCode);
          finalDesc = isMeaningful(cd) ? cd : (code.startsWith("CAVO-") ? `Utenza ${code.replace("CAVO-", "")}` : `Alimentazione ${baseCode}`);
        }
        if (!isMeaningful(finalDesc)) finalDesc = `Alimentazione ${baseCode}`;
        const cleaned = cleanUtenzaDescription(finalDesc, baseCode);
        if (isMeaningful(cleaned)) finalDesc = cleaned;
        
        if (isUnipolarCable(item)) {
          const match = state.cables.find(c => c.code.startsWith(baseCode) || baseCode.startsWith(c.code));
          if (match && match.desc) finalDesc = match.desc;
        }

        state.cables.push({
          id: idx,
          code,
          d: item.d,
          w: item.w,
          sec: item.sec,
          cores: item.cores,
          iz0: item.iz0,
          ib: (state.pdfLoaded && uInfo) ? uInfo.ib : (state.pdfLoaded ? 12 : 0),
          bendK: item.bendK,
          trefoil,
          trefoilGroupId,
          origX: 18 + ((idx - 1) % 5) * 15,
          origY: item.d / 2 + Math.floor((idx - 1) / 5) * 15,
          x: 18,
          y: item.d / 2,
          desc: finalDesc,
          type: (uInfo && uInfo.cable) || item.type,
          form: (uInfo && uInfo.form) || item.form,
          sys: item.sys
        });
      }
      document.getElementById("inpCableCode").value = "";
      recalculateAll();
    }

    function addCustomCableToDB() {
      const form = document.getElementById("newCbForm").value || "1(1x1.5)";
      const desc = document.getElementById("newCbDesc").value || "Cavo Custom Inserito";
      const d = parseFloat(document.getElementById("newCbD").value) || 10.0;
      const w = +(Math.pow(d / 10, 2) * 0.145).toFixed(2);
      
      CABLE_CATALOG.push({
        id: "CUSTOM_" + Date.now(),
        code: "CST-00",
        desc: desc,
        type: "CUSTOM",
        form: form,
        d: d,
        w: w,
        sec: parseSectionFromForm(form),
        cores: /1[xX]|1\s*\(/.test(form) ? 1 : 3,
        iz0: 20,
        bendK: 10,
        sys: "BT_POWER"
      });
      
      initCatalogDropdown();
      document.getElementById("selCableCatalog").selectedIndex = CABLE_CATALOG.length - 1;
    }

    function autoSizeTray() {
      if (!state.cables.length) return;
      const reservePct = parseFloat(document.getElementById("inpSpareReserve").value) || 30;
      const targetFillFactor = (100 - reservePct) / 100;
      
      const used = state.cables.reduce((s, c) => s + Math.PI * Math.pow(c.d / 2, 2), 0);
      const widths = [50, 100, 150, 200, 300, 400, 500, 600, 800, 1000];
      
      state.width = widths.find(w => w * state.height >= used / targetFillFactor) || widths[widths.length - 1];
      state.title = state.title.replace(/L=\s*\d+\s*mm/i, `L=${state.width}mm`);
      
      pushStateToUI();
      recalculateAll();
      fitAll();
    }

    function escapeHtml(str) {
      if (str === null || str === undefined) return "";
      return String(str)
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");
    }

    function updateTableUI() {
      const head = document.getElementById("tblHead");
      const body = document.getElementById("tblBody");
      // Conserva il focus (e il cursore) se l'utente sta modificando una cella della tabella
      const act = document.activeElement;
      const keep = (act && body.contains(act) && act.dataset.row != null)
        ? { row: act.dataset.row, field: act.dataset.field, s: act.selectionStart, e: act.selectionEnd } : null;

      head.innerHTML = `
        <tr class="border-b border-slate-800 text-xs uppercase text-slate-400 mono bg-slate-950/60 whitespace-nowrap">
          <th scope="col" class="py-1.5 px-2">Pos</th>
          <th scope="col" class="py-1.5 px-2">Sigla utenza / linea</th>
          <th scope="col" class="py-1.5 px-2">Ø (mm)</th>
          <th scope="col" class="py-1.5 px-2">Peso (kg/m)</th>
          <th scope="col" class="py-1.5 px-2">R min</th>
          <th scope="col" class="py-1.5 px-2">Coord (X, Y)</th>
          <th scope="col" class="py-1.5 px-2">Descrizione utenza</th>
          <th scope="col" class="py-1.5 px-2">Tipo cavo</th>
          <th scope="col" class="py-1.5 px-2">Formazione</th>
          <th scope="col" class="py-1.5 px-2 text-right"><span class="sr-only">Azioni</span></th>
        </tr>`;
      if (!state.cables.length) {
        body.innerHTML = `<tr><td colspan="10" class="py-6 text-center text-slate-400 font-sans text-[13px]">Nessun cavo nella sezione. Importa un file DWG/DXF oppure aggiungi cavi dalla scheda <strong class="text-slate-200">4. Cavi</strong>.</td></tr>`;
        return;
      }
      const num = (v, def) => (v != null && !isNaN(v)) ? v : def;
      if (selectedCableIdx >= state.cables.length) selectedCableIdx = -1;
      body.innerHTML = state.cables.map((c, i) => {
        const isUni = isUnipolarCable(c);
        const n = i + 1;
        const sel = i === selectedCableIdx;
        return `<tr class="hover:bg-slate-800/50 whitespace-nowrap${sel ? " row-sel" : ""}" data-idx="${i}" aria-selected="${sel}">
          <td class="py-1 px-2 text-amber-400 font-bold">#${n}</td>
          <td class="py-1 px-2"><input type="text" id="codeInp_${i}" data-row="${i}" data-field="code" aria-label="Sigla cavo ${n}" value="${escapeHtml(c.code)}" oninput="onCableCodeInput(${i}, this.value)" onchange="updateCableField(${i}, 'code', this.value)" class="cell-input w-36 font-semibold"></td>
          <td class="py-1 px-2"><input type="number" data-row="${i}" data-field="d" aria-label="Diametro cavo ${n} in mm" value="${c.d}" step="0.5" min="4" max="90" onchange="updateCableField(${i}, 'd', this.value)" class="cell-input w-16 text-center"></td>
          <td class="py-1 px-2 text-slate-300">${num(c.w, Math.pow((c.d || 10) / 10, 2) * 0.145).toFixed(2)}</td>
          <td class="py-1 px-2 text-slate-300">${Math.round((c.d || 10) * (c.bendK || 10))} mm</td>
          <td class="py-1 px-2 text-slate-400">(${num(c.x, 0).toFixed(1)}, ${num(c.y, 0).toFixed(1)})</td>
          <td class="py-1 px-2"><input type="text" id="descInp_${i}" data-row="${i}" data-field="desc" aria-label="Descrizione utenza cavo ${n}" value="${escapeHtml(c.desc)}" oninput="onCableDescInput(${i}, this.value)" onchange="updateCableField(${i}, 'desc', this.value)" class="cell-input w-64 font-sans"></td>
          <td class="py-1 px-2"><input type="text" data-row="${i}" data-field="type" aria-label="Tipo cavo ${n}" value="${escapeHtml(getDisplayCableType(c, state.cables))}" onchange="updateCableField(${i}, 'type', this.value)" class="cell-input w-32"></td>
          <td class="py-1 px-2">
            <div class="flex items-center gap-1">
              <input type="text" data-row="${i}" data-field="form" aria-label="Formazione cavo ${n}" value="${escapeHtml(c.form)}" onchange="updateCableField(${i}, 'form', this.value)" class="cell-input w-28">
              ${isUni ? `<span class="text-xs bg-amber-500/15 text-amber-300 border border-amber-500/40 px-1 rounded font-bold" title="Cavo unipolare">1X</span>` : ""}
              ${c.trefoil ? `<span class="text-xs bg-cyan-500/15 text-cyan-300 border border-cyan-500/40 px-1 rounded font-bold" title="Posa a trifoglio">3F</span>` : ""}
            </div>
          </td>
          <td class="py-1 px-2 text-right"><button type="button" onclick="deleteCable(${i})" aria-label="Elimina cavo ${n}" title="Elimina cavo" class="text-slate-400 hover:text-red-400 hover:bg-red-500/10 rounded px-2 py-0.5 text-base leading-none">&times;</button></td>
        </tr>`;
      }).join("");

      if (keep) {
        const el = body.querySelector(`[data-row="${keep.row}"][data-field="${keep.field}"]`);
        if (el) {
          el.focus({ preventScroll: true });
          try { if (keep.s != null) el.setSelectionRange(keep.s, keep.e); } catch (err) {}
        }
      }
    }

    function onCableDescInput(idx, val) {
      const c = state.cables[idx];
      if (!c) return;
      c.desc = val;
      const base = getBaseCircuitCode(c.code);
      if (base && !base.startsWith("CAVO-")) {
        state.cables.forEach((other, oIdx) => {
          if (other !== c && getBaseCircuitCode(other.code) === base) {
            other.desc = val;
            const oEl = document.getElementById(`descInp_${oIdx}`);
            if (oEl && oEl.value !== val) oEl.value = val;
          }
        });
      }
      drawScene();
    }

    function onCableCodeInput(idx, val) {
      const c = state.cables[idx];
      if (!c) return;
      const parsed = parseCalloutLabelString(val);
      const cleanCode = parsed.cleanCode || val.trim();
      c.code = cleanCode;
      if (parsed.inlineDesc && (!c.desc || c.desc.startsWith("Alimentazione") || c.desc.startsWith("Utenza"))) {
        c.desc = parsed.inlineDesc;
        const inpDesc = document.getElementById(`descInp_${idx}`);
        if (inpDesc && inpDesc.value !== c.desc) inpDesc.value = c.desc;
      }
      const u = findInUnifilareDict(c.code) || findInUnifilareDict(getBaseCircuitCode(c.code));
      if (u) {
        if (u.desc) {
          c.desc = u.desc;
          const descEl = document.getElementById(`descInp_${idx}`);
          if (descEl && descEl.value !== u.desc) descEl.value = u.desc;
        }
        if (u.cable) c.type = u.cable;
        if (u.form) {
          c.form = u.form;
          c.sec = parseSectionFromForm(u.form, c.sec);
          c.cores = isUnipolarCable(c) ? 1 : (u.form.includes("5G") || u.form.includes("4G") ? 3 : 2);
        }
        const base = getBaseCircuitCode(c.code);
        if (base && !base.startsWith("CAVO-")) {
          state.cables.forEach((other, oIdx) => {
            if (other !== c && getBaseCircuitCode(other.code) === base) {
              if (u.desc) {
                other.desc = u.desc;
                const oEl = document.getElementById(`descInp_${oIdx}`);
                if (oEl && oEl.value !== u.desc) oEl.value = u.desc;
              }
            }
          });
        }
      }
      drawScene();
    }

    function updateCableField(idx, field, val) {
      const c = state.cables[idx];
      if (!c) return;
      if (field === "d") {
        c.d = parseFloat(val) || 10;
        c.w = +(Math.pow(c.d / 10, 2) * 0.145).toFixed(2);
      } else if (field === "desc") {
        c.desc = cleanUtenzaDescription(val, c.code);
        const inpEl = document.getElementById(`descInp_${idx}`);
        if (inpEl && inpEl.value !== c.desc) inpEl.value = c.desc;
        const base = getBaseCircuitCode(c.code);
        if (base && !base.startsWith("CAVO-")) {
          state.cables.forEach((other, oIdx) => {
            if (other !== c && getBaseCircuitCode(other.code) === base) {
              other.desc = c.desc;
              const oEl = document.getElementById(`descInp_${oIdx}`);
              if (oEl && oEl.value !== c.desc) oEl.value = c.desc;
            }
          });
        }
      } else if (field === "code") {
        const parsed = parseCalloutLabelString(val);
        c.code = parsed.cleanCode || val.trim();
        const inpCode = document.getElementById(`codeInp_${idx}`);
        if (inpCode && inpCode.value !== c.code) inpCode.value = c.code;
        if (parsed.inlineDesc && (!c.desc || c.desc.startsWith("Alimentazione") || c.desc.startsWith("Utenza"))) {
          c.desc = parsed.inlineDesc;
          const inpDesc = document.getElementById(`descInp_${idx}`);
          if (inpDesc && inpDesc.value !== c.desc) inpDesc.value = c.desc;
        }
      } else {
        c[field] = val;
      }
      recalculateAll();
      updateTableUI();
    }

    function deleteCable(idx) {
      const c = state.cables[idx];
      if (!c) return;
      state.cables.splice(idx, 1);
      recalculateAll();
      toast(`Cavo ${c.code} eliminato`, "info", { label: "Annulla", onClick: undoLastChange });
    }

    function clearCables() {
      if (!state.cables.length) return;
      const n = state.cables.length;
      state.cables = [];
      recalculateAll();
      toast(`Rimossi ${n} cavi dalla sezione`, "warn", { label: "Annulla", onClick: undoLastChange }, 7000);
    }

// ---- 04-import-cad.js
    // ============================================================================
    // 5. VERO PARSER GEOMETRICO .DWG (LIBREDWG C/WASM) E MATCHING CON TABELLE/PDF
    // ============================================================================
    function extractRawAscii(uint8) {
      let s = "";
      for (let i = 0; i < uint8.length; i++) {
        const b = uint8[i];
        s += (b >= 32 && b <= 126) || b === 10 || b === 13 ? String.fromCharCode(b) : " ";
      }
      return s;
    }

    // Pulisce codici di formattazione MTEXT (es. \A1; \fArial|b0; {\H0.8x;...}) e decodifica sequenze \U+XXXX
    function cleanAcadMText(rawStr) {
      return String(rawStr || "")
        .replace(/\\U\+([0-9A-Fa-f]{4})/g, (m, hex) => String.fromCharCode(parseInt(hex, 16)))
        .replace(/\\P/gi, " ")
        .replace(/\\~|\^J/g, " ")
        .replace(/\\[A-Za-z][^;]*;/g, "")
        .replace(/[{}]/g, "")
        .replace(/\s+/g, " ")
        .trim();
    }
    // Estrae e separa codice circuito pulito, formazione inline e descrizione inline
    function parseCalloutLabelString(rawLblStr) {
      let s = String(rawLblStr || "").trim();
      s = s.replace(/^#\d+\s+/, "").trim();
      let inlineForm = null;
      const mParen = s.match(/\(([^)]+)\)\s*$/);
      if (mParen && /[xXGg]\d+|Ø/i.test(mParen[1])) {
        const fm = mParen[1].match(/\d*\(\d+[xXGg]\d+(?:[.,]\d+)?\)|\b\d+[xXGg]\d+(?:[.,]\d+)?\b/i);
        if (fm) inlineForm = fm[0];
        s = s.substring(0, mParen.index).trim();
      }

      let cleanCode = s;
      let inlineDesc = null;

      // Se la stringa contiene un codice circuito seguito da una descrizione
      // es. "QPT-DX-N C-21 Alimentazione Centralina DALI 1 / 1,053"
      const mCode = s.match(/^([A-Z0-9]{2,12}(?:[-._/][A-Z0-9]{1,10})*\s+C-?\d+|C-?\d+|[A-Z0-9_-]+-C\d+)\b/i);
      if (mCode) {
        cleanCode = mCode[1].trim();
        const rem = s.substring(mCode[0].length).trim();
        if (rem.length > 0) {
          inlineDesc = cleanUtenzaDescription(rem, cleanCode);
        }
      }

      return { cleanCode, inlineForm, inlineDesc };
    }


    function processSingleCADFile(file) {
      const buf = file.arrayBuffer();
      const uint8 = new Uint8Array(buf);
      const rawAscii = extractRawAscii(uint8);
      const headerVer = rawAscii.substring(0, 6).trim();

      if (file.name.toLowerCase().endsWith(".dxf") || (rawAscii.includes("SECTION") && rawAscii.includes("ENTITIES"))) {
        const log = parseDXFTextContent(rawAscii, file.name);
        return { success: true, log };
      }

      if (!globalLibreDwgInstance) {
        globalLibreDwgInstance = loadLibreDwgWasmInstance();
      }

      const fileType = (globalLibreDwgLib && globalLibreDwgLib.Dwg_File_Type) ? globalLibreDwgLib.Dwg_File_Type.DWG : 0;
      let dwgDataPtr;
      try {
        dwgDataPtr = globalLibreDwgInstance.dwg_read_data(buf, fileType);
      } catch (eRead) {
        dwgDataPtr = globalLibreDwgInstance.dwg_read_data(uint8, fileType);
      }

      if (!dwgDataPtr) {
        throw new Error(`Il file DWG (${headerVer}) non è stato decodificato dal parser (puntatore nullo).`);
      }

      const db = globalLibreDwgInstance.convert(dwgDataPtr);
      let allEntities = (db && Array.isArray(db.entities)) ? [...db.entities] : [];
      if (allEntities.length === 0 && db && db.tables && db.tables.BLOCK_RECORD && Array.isArray(db.tables.BLOCK_RECORD.entries)) {
        db.tables.BLOCK_RECORD.entries.forEach(br => {
          if (Array.isArray(br.entities)) allEntities.push(...br.entities);
        });
      }

      if (allEntities.length > 0) {
        const log = processRealWasmEntities(allEntities, file.name, headerVer);
        return { success: true, log };
      } else {
        throw new Error(`Lettura completata, ma 0 entità visibili estratte dal DWG (${headerVer}).`);
      }
    }

    function importCADFile(inputEl) {
      let files = Array.from((inputEl && inputEl.files) || []);
      if (!files || files.length === 0) return;
      // Un progetto .ccad scelto da "Importa disegno" viene aperto come progetto
      const proj = files.find(f => /\.(ccad|json)$/i.test(f.name));
      if (proj) { if (inputEl) inputEl.value = ""; openProjectFile(proj); return; }
      const banner = document.getElementById("statusBanner");
      const inspBox = document.getElementById("dwgInspectorContent");

      if (banner) {
        banner.textContent = `Caricamento di ${files.length} file CAD in corso...`;
        banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-cyan-500/20 text-cyan-300 border border-cyan-500/40 font-bold";
      }

      if (projectSections.length === 0) {
        projectSections.push(cloneStateSnapshot(state));
        currentSectionIdx = 0;
      } else {
        projectSections[currentSectionIdx] = cloneStateSnapshot(state);
      }

      let loadedCount = 0;
      const logs = [];
      const errorMsgs = [];

      for (let fIdx = 0; fIdx < files.length; fIdx++) {
        const file = files[fIdx];
        if (banner) banner.textContent = `Caricamento file ${fIdx + 1}/${files.length}: "${file.name}"...`;

        const isCurrentEmpty = state.cables.length === 0 && state.title.startsWith("NUOVA CANALINA");
        if (fIdx > 0 || !isCurrentEmpty) {
          const newSec = cloneStateSnapshot(state);
          newSec.cables = [];
          newSec.title = file.name.replace(/\.(dwg|dxf)$/i, "");
          const nextLetter = String.fromCharCode(65 + Math.min(25, projectSections.length));
          newSec.planTag = `SEZ. ${nextLetter}-${nextLetter} | H=+3.20m`;
          projectSections.push(newSec);
          currentSectionIdx = projectSections.length - 1;
          state = cloneStateSnapshot(newSec);
        }

        try {
          const res = processSingleCADFile(file);
          if (res && res.log) logs.push(res.log);
          projectSections[currentSectionIdx] = cloneStateSnapshot(state);
          loadedCount++;
        } catch (err) {
          console.error(`Errore caricamento ${file.name}:`, err);
          const msg = (err && err.message) ? err.message : String(err);
          logs.push(`ERRORE "${file.name}": ${msg}`);
          errorMsgs.push(`${file.name}: ${msg}`);
        }
      }

      renderMultiSectionTabs();
      pushStateToUI();
      recalculateAll();
      updateTableUI();
      fitAll();

      if (inspBox && logs.length > 0) inspBox.textContent = logs.join("\n\n" + "=".repeat(60) + "\n\n");
      const inspModal = document.getElementById("dwgInspectorModal");
      // L'ispettore delle entità resta chiuso: si apre da Importa → Ispettore entità DWG


      if (banner) {
        if (loadedCount > 0 && errorMsgs.length === 0) {
          banner.textContent = `✓ Caricati con successo ${loadedCount} file CAD in ${projectSections.length} schede canalina distinte!`;
          banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-emerald-500/20 text-emerald-300 border border-emerald-500/40 font-bold";
        } else if (loadedCount > 0 && errorMsgs.length > 0) {
          banner.textContent = `⚠ Caricati ${loadedCount}/${files.length} file CAD (${errorMsgs.length} errori: ${errorMsgs[0]})`;
          banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-amber-500/20 text-amber-300 border border-amber-500/40 font-bold";
        } else {
          banner.textContent = `Errore lettura DWG/DXF: ${errorMsgs[0] || 'Impossibile leggere il file'}`;
          banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-red-500/20 text-red-300 border border-red-500/40 font-bold";
        }
      }
      if (inputEl) inputEl.value = "";
    }

    // Estrazione geometrica avanzata + ricostruzione catene Bandierine + lettura Tabelle DWG interne
    function processRealWasmEntities(entities, fileName, headerVer) {
      const traySegments = [];
      const rawCircles = [];
      const rawTexts = [];
      const bandierineSegments = [];

      const isTrayLay = (lay) => /CANAL|PASSERELL|TRAY|PROFIL|SEZION|CONDOTT|BORDO|CONTORN/i.test(lay);
      const isLeaderLay = (lay) => /BANDIERINE|LEADER|RICHIAM/i.test(lay);

      entities.forEach(ent => {
        const lay = String(ent.layer || "0").trim();
        const t = String(ent.type || "").toUpperCase();

        if (t === "LINE" && ent.startPoint && ent.endPoint) {
          if (isTrayLay(lay)) {
            traySegments.push({ x1: ent.startPoint.x, y1: ent.startPoint.y, x2: ent.endPoint.x, y2: ent.endPoint.y, lay });
          } else if (isLeaderLay(lay)) {
            bandierineSegments.push({ x1: ent.startPoint.x, y1: ent.startPoint.y, x2: ent.endPoint.x, y2: ent.endPoint.y });
          }
        } else if ((t === "LWPOLYLINE" || t === "POLYLINE") && Array.isArray(ent.vertices)) {
          for (let i = 0; i < ent.vertices.length - 1; i++) {
            const seg = { x1: ent.vertices[i].x, y1: ent.vertices[i].y, x2: ent.vertices[i + 1].x, y2: ent.vertices[i + 1].y, lay };
            if (isTrayLay(lay)) traySegments.push(seg);
            else if (isLeaderLay(lay)) bandierineSegments.push(seg);
          }
        } else if (t === "CIRCLE" && ent.center && typeof ent.radius === "number") {
          const d = +(ent.radius * 2).toFixed(1);
          // Escludiamo i micro-cerchi delle anime interne (< 4.5 mm) o simboli cornice
          if (d >= 4.5 && d <= 110.0 && (/CAVI/i.test(lay) || !/ANIME|CORNICE|CARTIGLIO|SIMB/i.test(lay))) {
            rawCircles.push({ cx: +ent.center.x.toFixed(2), cy: +ent.center.y.toFixed(2), r: +ent.radius.toFixed(2), d, lay });
          }
        } else if (t === "TEXT" || t === "MTEXT") {
          const rawStr = ent.text || ent.value || ent.string || "";
          const str = cleanAcadMText(rawStr);
          const pt = ent.startPoint || ent.insertionPoint || ent.position || { x: 0, y: 0 };
          if (str.length > 0) {
            rawTexts.push({ str, x: +(pt.x || 0).toFixed(2), y: +(pt.y || 0).toFixed(2), lay });
          }
        }
      });

      // Se non abbiamo trovato segmenti su layer specifici, recuperiamo le linee non-cavi e non-testi (es. layer '0' o generico)
      if (traySegments.length === 0) {
        entities.forEach(ent => {
          const lay = String(ent.layer || "0").trim();
          const t = String(ent.type || "").toUpperCase();
          if (/CAVI|ANIME|BANDIERINE|LEADER|RICHIAM|QUOTE|DIM|CORNICE|CARTIGLIO|TABELLA/i.test(lay)) return;
          if (t === "LINE" && ent.startPoint && ent.endPoint) {
            traySegments.push({ x1: ent.startPoint.x, y1: ent.startPoint.y, x2: ent.endPoint.x, y2: ent.endPoint.y, lay });
          } else if ((t === "LWPOLYLINE" || t === "POLYLINE") && Array.isArray(ent.vertices)) {
            for (let i = 0; i < ent.vertices.length - 1; i++) {
              traySegments.push({ x1: ent.vertices[i].x, y1: ent.vertices[i].y, x2: ent.vertices[i + 1].x, y2: ent.vertices[i + 1].y, lay });
            }
          }
        });
      }

      // Filtra eventuali cerchi concentrici (es. guaina interna disegnata sul layer Cavi dentro il cavo esterno)
      rawCircles.sort((a, b) => b.r - a.r);
      const outerCircles = [];
      rawCircles.forEach(c => {
        const isInsideExisting = outerCircles.some(outer => Math.hypot(c.cx - outer.cx, c.cy - outer.cy) < outer.r * 0.85);
        if (!isInsideExisting) outerCircles.push(c);
      });

      // =========================================================================
      // 1. ESTRAZIONE DIMENSIONI CANALINA (L, H) - LETTURA DIRETTA DAL FILE DI INPUT
      // Convenzione passerelle/canaline: se legge "75x300", 75 è l'altezza H e 300 la base L.
      // La dimensione maggiore è sempre la base (larghezza L), la minore è l'altezza (sponda H).
      // =========================================================================
      const allTextCorpus = rawTexts.map(t => t.str).join(" | ");
      let detectedW = null, detectedH = null;

      // Helper di assegnazione corretta (base L = maggiore, altezza H = minore)
      function assignTrayDims(dimA, dimB) {
        if (!dimA || !dimB) return;
        const d1 = parseInt(dimA, 10), d2 = parseInt(dimB, 10);
        if (isNaN(d1) || isNaN(d2)) return;
        detectedW = Math.max(d1, d2); // 300 è sempre la base
        detectedH = Math.min(d1, d2); // 75 è sempre l'altezza
      }

      // Priorità A: Ricerca esplicita "L=... H=..." o "B=... H=..." o "W=... H=..."
      const mExplicitLH = allTextCorpus.match(/(?:L|B|W|BASE|LARGHEZZA)\s*[:=]?\s*(\d{2,4})\s*(?:mm)?\s*[,;\s|/-]+\s*(?:H|ALT|ALTEZZA)\s*[:=]?\s*(\d{2,4})\s*(?:mm)?/i)
                       || allTextCorpus.match(/(?:H|ALT|ALTEZZA)\s*[:=]?\s*(\d{2,4})\s*(?:mm)?\s*[,;\s|/-]+\s*(?:L|B|W|BASE|LARGHEZZA)\s*[:=]?\s*(\d{2,4})\s*(?:mm)?/i);
      if (mExplicitLH) {
        const v1 = parseInt(mExplicitLH[1], 10), v2 = parseInt(mExplicitLH[2], 10);
        if (/H|ALT/i.test(mExplicitLH[0].split(/[,;\s|/-]+/)[0])) {
          assignTrayDims(v2, v1);
        } else {
          assignTrayDims(v1, v2);
        }
      }

      // Priorità B: Parola chiave canalina seguita da WxH o HxW (es. "CANALINA 75x300", "CANALE 400x75", "P31 75x300")
      if (!detectedW || !detectedH) {
        const mTrayKeyword = allTextCorpus.match(/(?:CANALINA|CANALE|PASSERELLA|TRAY|P31|CF|RKS|MKS)\b[^\d\n\r]*(\d{2,4})\s*[xX×*]\s*(\d{2,4})/i);
        if (mTrayKeyword) {
          assignTrayDims(mTrayKeyword[1], mTrayKeyword[2]);
        }
      }

      // Priorità C: Dimensioni isolate presenti nei testi del disegno (es. "75x300", "300x100", "75 x 300mm")
      if (!detectedW || !detectedH) {
        const allMatches = [...allTextCorpus.matchAll(/\b(\d{2,4})\s*[xX×*]\s*(\d{2,4})\s*(?:mm)?\b/gi)];
        for (const m of allMatches) {
          const d1 = parseInt(m[1], 10), d2 = parseInt(m[2], 10);
          const maxD = Math.max(d1, d2), minD = Math.min(d1, d2);
          if (maxD >= 40 && maxD <= 1200 && minD >= 20 && minD <= 300) {
            assignTrayDims(d1, d2);
            break;
          }
        }
      }

      // Priorità D: Nome del file di input (es. "Canale_75x300.dxf", "Passerella_400x75.dwg")
      if (!detectedW || !detectedH) {
        const mFile = fileName.match(/(\d{2,4})\s*[xX×*_-]\s*(\d{2,4})/);
        if (mFile) {
          const d1 = parseInt(mFile[1], 10), d2 = parseInt(mFile[2], 10);
          const maxD = Math.max(d1, d2), minD = Math.min(d1, d2);
          if (maxD >= 40 && maxD <= 1200 && minD >= 20 && minD <= 300) {
            assignTrayDims(d1, d2);
          }
        }
      }

      // Priorità E: Calcolo geometrico dai segmenti rilevati
      const VALID_WIDTHS = [50, 60, 75, 100, 150, 200, 250, 300, 400, 500, 600, 700, 800, 900, 1000, 1200];
      const VALID_HEIGHTS = [25, 30, 35, 40, 50, 60, 75, 80, 85, 100, 110, 120, 150, 200];
      const snapToStandard = (val, list) => list.reduce((prev, curr) => Math.abs(curr - val) < Math.abs(prev - val) ? curr : prev);

      let origX = 0, origY = 0;
      if (traySegments.length > 0) {
        const xs = traySegments.flatMap(s => [s.x1, s.x2]);
        const ys = traySegments.flatMap(s => [s.y1, s.y2]);
        origX = Math.min(...xs);
        origY = Math.min(...ys);
        const spanX = Math.max(...xs) - origX;
        const spanY = Math.max(...ys) - origY;
        if (!detectedW && spanX >= 40 && spanX <= 1200) detectedW = snapToStandard(spanX - 3, VALID_WIDTHS);
        if (!detectedH && spanY >= 20 && spanY <= 400)  detectedH = snapToStandard(spanY - 1.5, VALID_HEIGHTS);
      } else if (outerCircles.length > 0) {
        origX = Math.min(...outerCircles.map(c => c.cx - c.r)) - 12;
        origY = Math.min(...outerCircles.map(c => c.cy - c.r));
      }

      // Assicurazione finale: se detectedW < detectedH, inverti (base è sempre >= altezza)
      if (detectedW && detectedH && detectedW < detectedH) {
        const tmp = detectedW;
        detectedW = detectedH;
        detectedH = tmp;
      }

      // Assegna le dimensioni finali trovate direttamente dal file di input
      state.width = detectedW || state.width || 200;
      state.height = detectedH || state.height || 75;
      const geomW = state.width, geomH = state.height;

      const trayNameInfo = extractTrayNameFromInput(rawTexts, fileName, state.width, state.height);
      state.title = trayNameInfo.title;
      const elExp = document.getElementById("exportName"); if (elExp) elExp.value = trayNameInfo.exportFile;

      const elW = document.getElementById("inpWidth"); if (elW) elW.value = state.width;
      const elH = document.getElementById("inpHeight"); if (elH) elH.value = state.height;
      const elT = document.getElementById("inpTitle"); if (elT) elT.value = state.title;
      const qTitleWasm = document.getElementById("inpTitleQuick");
      if (qTitleWasm) qTitleWasm.value = state.title;
      const hTitle = document.getElementById("headerTitle"); if (hTitle) hTitle.textContent = state.title;
      document.title = `${state.title} — ${APP_NAME}`;

      // Default modalità di posa: Gravità & Scomparti (salvando le coordinate DWG in origX/origY)
      state.packing = "gravity";
      const elPack = document.getElementById("inpPacking"); if (elPack) elPack.value = "gravity";

      // 3. RICOSTRUZIONE EVENTUALI TABELLE CAVI PRESENTI NEL DWG (Raggruppamento per Righe Y)
      // Se nel DWG c'è già una tabella o legenda con Sigla, Descrizione, Tipo Cavo e Formazione, la leggiamo!
      const dwgTableByCode = {};
      const rowToleranceY = 2.8;
      const textRows = [];
      rawTexts.forEach(t => {
        let row = textRows.find(r => Math.abs(r.y - t.y) <= rowToleranceY);
        if (!row) {
          row = { y: t.y, items: [] };
          textRows.push(row);
        }
        row.items.push(t);
      });

      // Funzione di riconoscimento sigle/codici linea per evitare falsi positivi come descrizione
      const isCodeLike = (s) =>
        !/^CANALINA\b|^SCALA|^DATA|^DISEGNATORE|^VISTO|^DESCRIZIONE|^REV\b|^POS\b|^SIGLA\b|^Base\s+L|^H\s*=/i.test(s) &&
        (/[A-Z0-9]+.*C-\d+/i.test(s) || /^(?:QGBT|QP|QE|UPS|IRAI|EVAC|TD|LINEA|CAVO)/i.test(s) || /^#\d+\s+/i.test(s));

      textRows.forEach(row => {
        if (row.items.length < 2) return;
        row.items.sort((a, b) => a.x - b.x);
        const textsInRow = row.items.map(it => it.str.trim());

        // Cerca se nella riga c'è una sigla utenza (es. "QGBT-P C-22" o "QP1-DX-N C-4")
        const codeCandidate = textsInRow.find(s =>
          /^[A-Z0-9]{2,12}(?:[-._/][A-Z0-9]{1,10})*\s+C-\d+/i.test(s) ||
          /^(?:QGBT|QP|QE|QUADRO|UPS|IRAI|EVAC|TD)[A-Z0-9_.-]*(?:\s+C-\d+)?$/i.test(s)
        );
        if (!codeCandidate) return;

        const formCandidate = textsInRow.find(s => /\d*\s*\(\s*\d+[xXGg]\d+(?:[.,]\d+)?\s*\)|\b\d+[xXGg]\d+(?:[.,]\d+)?\b/i.test(s) && s !== codeCandidate);
        const typeCandidate = textsInRow.find(s => /FG16|FTG18|FS17|FG17|H07|RG16|UTP|CAT\./i.test(s) && s !== codeCandidate && s !== formCandidate);
        
        // Cerca colonna sigla cavo dedicata
        const siglaCavoCandidate = textsInRow.find(s =>
          s !== codeCandidate && s !== formCandidate && s !== typeCandidate &&
          /(?:^CV-?\d+|^CAV-?\d+|^(?:FG16|FTG18|FS17|FG17)[A-Z0-9_.-]*)/i.test(s)
        );

        // Estrazione diametro da colonna tabella (es. "18.5", "Ø 18.5", "18,5 mm")
        let diamCandidate = null;
        for (const t of textsInRow) {
          if (t === codeCandidate || t === formCandidate || t === typeCandidate || t === siglaCavoCandidate) continue;
          const mDiam = t.match(/^(?:[ØøD]|diam(?:\.|etro)?\s*:?)?\s*(\d+[.,]\d+)\s*(?:mm)?$/i);
          if (mDiam) {
            const v = parseFloat(mDiam[1].replace(",", "."));
            if (v >= 3 && v <= 80) { diamCandidate = v; break; }
          }
        }

        // Estrazione sezione da colonna tabella (es. "2.5 mm²", "16 mm2", "70")
        let secCandidate = null;
        for (const t of textsInRow) {
          if (t === codeCandidate || t === formCandidate || t === typeCandidate || t === siglaCavoCandidate) continue;
          const mSec = t.match(/^(?:sez(?:\.|ione)?\s*:?)?\s*(\d+(?:[.,]\d+)?)\s*(?:mm[²2]|mq)?$/i);
          if (mSec) {
            const v = parseFloat(mSec[1].replace(",", "."));
            if ([1.5, 2.5, 4, 6, 10, 16, 25, 35, 50, 70, 95, 120, 150, 185, 240, 300, 400].includes(v)) {
              secCandidate = v; break;
            }
          }
        }

        const descCandidate = textsInRow.find(s =>
          s !== codeCandidate &&
          s !== formCandidate &&
          s !== typeCandidate &&
          s !== siglaCavoCandidate &&
          !isCodeLike(s) &&
          !/^#?\d+$/.test(s) &&
          !/^\d+(?:[.,]\d+)?\s*(?:mm|kg|daN)?$/i.test(s) &&
          !/^(?:BT|ELV|POS|SIGLA|TIPO|FORMAZIONE)$/i.test(s) &&
          s.length >= 4
        );

        const normKey = normalizeLineKey(codeCandidate);
        const baseCandidate = getBaseCircuitCode(codeCandidate);
        const normBaseKey = normalizeLineKey(baseCandidate);
        const rowData = {
          rawCode: codeCandidate,
          baseCode: baseCandidate,
          form: formCandidate || null,
          type: typeCandidate || null,
          siglaCavo: siglaCavoCandidate || null,
          d: diamCandidate || null,
          sec: secCandidate || null,
          desc: descCandidate ? cleanUtenzaDescription(descCandidate) : null
        };
        dwgTableByCode[normKey] = rowData;
        if (!dwgTableByCode[normBaseKey]) {
          dwgTableByCode[normBaseKey] = rowData;
        }
      });

      // 4. ESTRAZIONE SIGLE SULLE BANDIERINE VICINO ALLA SEZIONE

      const calloutTexts = rawTexts.filter(t =>
        (isCodeLike(t.str) || /Testi|Bandierine/i.test(t.lay)) &&
        t.x >= origX - 350 && t.x <= origX + geomW + 350 &&
        !/^CANALINA\b|^Base\s+L|^H\s*=/i.test(t.str) &&
        !/^\d+$/.test(t.str.trim()) 
      );

      function traceLeaderFromCircle(cir) {
        const startSegs = bandierineSegments.filter(seg => {
          const d1 = Math.hypot(seg.x1 - cir.cx, seg.y1 - cir.cy);
          const d2 = Math.hypot(seg.x2 - cir.cx, seg.y2 - cir.cy);
          return d1 <= cir.r + 4.5 || d2 <= cir.r + 4.5;
        });

        let bestLabel = null;
        let bestLabelDist = 9999;

        for (const s0 of startSegs) {
          const d1 = Math.hypot(s0.x1 - cir.cx, s0.y1 - cir.cy);
          const d2 = Math.hypot(s0.x2 - cir.cx, s0.y2 - cir.cy);
          let curEnd = d1 < d2 ? { x: s0.x2, y: s0.y2 } : { x: s0.x1, y: s0.y1 };
          const visited = new Set([s0]);

          for (let hop = 0; hop < 3; hop++) {
            const nextSeg = bandierineSegments.find(s => {
              if (visited.has(s)) return false;
              return Math.hypot(s.x1 - curEnd.x, s.y1 - curEnd.y) < 3.5 || Math.hypot(s.x2 - curEnd.x, s.y2 - curEnd.y) < 3.5;
            });
            if (!nextSeg) break;
            visited.add(nextSeg);
            const nd1 = Math.hypot(nextSeg.x1 - curEnd.x, nextSeg.y1 - curEnd.y);
            curEnd = nd1 < 3.5 ? { x: nextSeg.x2, y: nextSeg.y2 } : { x: nextSeg.x1, y: nextSeg.y1 };
          }

          calloutTexts.forEach(lbl => {
            const dy = Math.abs(lbl.y - curEnd.y);
            const dx = Math.abs(lbl.x - curEnd.x);
            const dist = Math.hypot(dx * 0.45, dy * 2.2); 
            if (dy < 12 && dx < 120 && dist < bestLabelDist) {
              bestLabelDist = dist;
              bestLabel = lbl;
            }
          });
        }
        return bestLabel;
      }

      outerCircles.sort((a, b) => (a.cy - b.cy) || (a.cx - b.cx));

      // parseCalloutLabelString: usa la versione globale definita in testa al file
      const usedCalloutSet = new Set();

      state.cables = outerCircles.map((cir, idx) => {
        let matchedLblObj = traceLeaderFromCircle(cir);

        if (!matchedLblObj) {
          const unused = calloutTexts.filter(l => !usedCalloutSet.has(l));
          if (unused.length > 0) {
            unused.sort((a, b) => Math.hypot(a.x - cir.cx, a.y - cir.cy) - Math.hypot(b.x - cir.cx, b.y - cir.cy));
            matchedLblObj = unused[0];
          }
        }
        if (matchedLblObj) usedCalloutSet.add(matchedLblObj);

        const parsedLbl = parseCalloutLabelString(matchedLblObj ? matchedLblObj.str : `CAVO-${idx + 1}`);
        const exactCode = parsedLbl.cleanCode || `CAVO-${idx + 1}`;
        const baseCircuit = getBaseCircuitCode(exactCode);
        const normKey = normalizeLineKey(exactCode);
        const normBaseKey = normalizeLineKey(baseCircuit);

        const relX = +(cir.cx - origX).toFixed(1);
        const relY = +(cir.cy - origY).toFixed(1);
        const d = cir.d;

        const uInfo = findInUnifilareDict(exactCode) || findInUnifilareDict(baseCircuit);
        const dwgRow = dwgTableByCode[normKey] || dwgTableByCode[normBaseKey];
        const diamSpec = inferCableSpecsByDiameter(d, exactCode, matchedLblObj ? matchedLblObj.lay : "");

        const finalForm = (uInfo && uInfo.form) || (dwgRow && dwgRow.form) || parsedLbl.inlineForm || diamSpec.form;
        const finalType = (uInfo && uInfo.cable) || (dwgRow && dwgRow.type) || diamSpec.type;
        const fallbackDwgDesc = exactCode.startsWith("CAVO-") ? `Utenza ${exactCode.replace("CAVO-", "")}` : `Alimentazione ${baseCircuit}`;
        const finalDesc = (uInfo && uInfo.desc) || (dwgRow && cleanUtenzaDescription(dwgRow.desc, exactCode)) || (parsedLbl && parsedLbl.inlineDesc) || fallbackDwgDesc;
        const finalSec  = (dwgRow && dwgRow.sec) || parseSectionFromForm(finalForm, diamSpec.sec);
        const isUni     = /\b1X\d+|\(\s*1X\d+/i.test(finalForm) || /\bFG16M16\b|\bFS17\b|\bFG17\b/i.test(finalType);
        const finalCores = isUni ? 1 : (finalForm.includes("5G") || finalForm.includes("4G") ? 3 : 2);
        const finalD    = (dwgRow && dwgRow.d) ? dwgRow.d : d;

        return {
          id: idx + 1,
          code: exactCode,
          siglaCavo: (dwgRow && dwgRow.siglaCavo) || null,
          d: finalD,
          w: +(Math.pow(finalD / 10, 2) * 0.145).toFixed(2),
          sec: finalSec,
          cores: finalCores,
          iz0: diamSpec.iz0,
          ib: (state.pdfLoaded && uInfo) ? uInfo.ib : 0,
          bendK: diamSpec.bendK,
          trefoil: false,
          origX: relX,
          origY: relY,
          x: relX,
          y: relY,
          desc: finalDesc,
          type: finalType,
          form: finalForm,
          sys: diamSpec.sys
        };
      });

      // =========================================================================
      // EREDITARIETÀ DESCRIZIONE PER CIRCUITI UNIPOLARI E RAGGRUPPAMENTO
      // =========================================================================
      const circuitGroups = {};
      state.cables.forEach(c => {
        const base = getBaseCircuitCode(c.code);
        if (!circuitGroups[base]) circuitGroups[base] = [];
        circuitGroups[base].push(c);
      });

      // Associazione conduttori per formazioni unipolari (es. 4(1x185), 3(1x240)) con cerchi CAVO-x adiacenti
      Object.entries(circuitGroups).forEach(([base, group]) => {
        if (!base || base.startsWith("CAVO-")) return;
        const main = group[0];
        const formStr = String(main.form || "");
        let expectedCount = 1;
        const mCount = formStr.match(/^(\d+)\s*\(?\s*1[xX]\d+/i) || formStr.match(/^(\d+)[xX]\s*1[xX]\d+/i);
        if (mCount) expectedCount = parseInt(mCount[1], 10);
        else if (/3\(1x.*PE/i.test(formStr)) expectedCount = 5;
        else if (/3\(1x/i.test(formStr)) expectedCount = 4;

        if (expectedCount > group.length) {
          const missing = expectedCount - group.length;
          const candidates = state.cables.filter(c =>
            c.code.startsWith("CAVO-") && Math.abs(c.d - main.d) <= 3.0
          );
          candidates.sort((a, b) => Math.hypot(a.x - main.x, a.y - main.y) - Math.hypot(b.x - main.x, b.y - main.y));
          const toAdd = candidates.slice(0, missing);
          toAdd.forEach((tc, idx) => {
            const phaseSuffixes = ["L2", "L3", "N", "PE"];
            tc.code = `${base} - ${phaseSuffixes[group.length - 1 + idx] || (group.length + idx + 1)}`;
            tc.type = main.type;
            tc.form = main.form;
            tc.cores = 1;
            tc.sys = main.sys;
            group.push(tc);
          });
        }
      });

      // Propagazione rigorosa della descrizione a TUTTI i conduttori di ciascun circuito unipolare
      Object.entries(circuitGroups).forEach(([base, group]) => {
        if (!base || base.startsWith("CAVO-")) return;
        
        let validDesc = null;
        for (const c of group) {
          if (c.desc && !c.desc.startsWith("Alimentazione linea CAVO-") && c.desc !== `Alimentazione linea ${c.code}`) {
            validDesc = c.desc;
            break;
          }
        }
        if (!validDesc) {
          const u = findInUnifilareDict(base);
          if (u && u.desc) validDesc = u.desc;
          const dRow = dwgTableByCode[normalizeLineKey(base)];
          if (dRow && dRow.desc) validDesc = dRow.desc;
        }

        const isAnyUni = group.some(c => isUnipolarCable(c));
        if (validDesc && (isAnyUni || group.length > 1)) {
          group.forEach(c => {
            c.desc = validDesc;
          });
        }
      });

      // Registra automaticamente nel database cavi qualsiasi tipologia non presente (Punto 4)
      state.cables.forEach(c => registerCableInCatalog(c));

      pushStateToUI();
      recalculateAll();
      fitAll();

      let log = `=== REPORT DECOMPRESSIONE REALE LIBREDWG (${fileName} — ${headerVer}) ===\n`;
      log += `• Entità totali lette nel ModelSpace: ${entities.length}\n`;
      log += `• Segmenti sul layer 'Canalina': ${traySegments.length} -> Origine CAD=(${origX.toFixed(1)}, ${origY.toFixed(1)}) -> Dimensioni calcolate: Base L=${geomW}mm, Altezza H=${geomH}mm\n`;
      log += `• Righe Tabella DWG riconosciute: ${Object.keys(dwgTableByCode).length}\n`;
      log += `• Cerchi Cavi Estratti: ${outerCircles.length}\n`;
      state.cables.forEach((c, i) => {
        log += `   [Cavo #${i + 1}] Locale=(${c.x}, ${c.y}) mm | Ø=${c.d}mm | Sigla DWG: "${c.code}" | Tipo: ${c.type} | Formazione: ${c.form} (${isUnipolarCable(c) ? "UNIPOLARE 1X" : "MULTIPOLARE"}) | Desc: "${c.desc}"\n`;
      });
      return log;
    }

    function parseDXFTextContent(rawText, fileName) {
      const lines = rawText.split(/\r?\n/);
      const entities = [];
      let i = 0;

      while (i < lines.length - 1) {
        const code = lines[i].trim();
        const val = (lines[i + 1] || "").trim();

        if (code === "0" && (val === "LINE" || val === "CIRCLE" || val === "ARC" || val === "TEXT" || val === "MTEXT" || val === "LWPOLYLINE" || val === "POLYLINE" || val === "DIMENSION")) {
          const entType = val;
          let lay = "0", x1 = 0, y1 = 0, x2 = 0, y2 = 0, r = 0, txt = "";
          const polyVertices = [];
          let curX = null, curY = null;
          i += 2;

          while (i < lines.length - 1) {
            const c = lines[i].trim();
            const v = (lines[i + 1] || "").trim();
            if (c === "0") break; // Inizio nuova entità

            if (c === "8") lay = v;
            else if (c === "10") {
              x1 = parseFloat(v) || 0;
              curX = x1;
            } else if (c === "20") {
              y1 = parseFloat(v) || 0;
              curY = y1;
              if ((entType === "LWPOLYLINE" || entType === "POLYLINE") && curX !== null) {
                polyVertices.push({ x: curX, y: curY });
                curX = null;
              }
            } else if (c === "11") x2 = parseFloat(v) || 0;
            else if (c === "21") y2 = parseFloat(v) || 0;
            else if (c === "40") r = parseFloat(v) || 0;
            else if (c === "1" || c === "3") txt += (txt ? " " : "") + v;
            i += 2;
          }

          if (entType === "LINE") {
            entities.push({ type: "LINE", layer: lay, startPoint: { x: x1, y: y1 }, endPoint: { x: x2, y: y2 } });
          } else if (entType === "CIRCLE") {
            entities.push({ type: "CIRCLE", layer: lay, center: { x: x1, y: y1 }, radius: r });
          } else if ((entType === "LWPOLYLINE" || entType === "POLYLINE") && polyVertices.length >= 2) {
            entities.push({ type: "LWPOLYLINE", layer: lay, vertices: polyVertices });
          } else if (entType === "TEXT" || entType === "MTEXT" || entType === "DIMENSION") {
            if (txt.trim()) entities.push({ type: "TEXT", layer: lay, text: txt.trim(), startPoint: { x: x1, y: y1 } });
          }
          continue;
        }
        i += 2;
      }
      return processRealWasmEntities(entities, fileName, "DXF");
    }

// ---- 05-import-pdf.js
    // Importazione PDF Unifilare con accoppiamento esatto (Punti 3 e 4)
    function importPDFFile(inputEl) {
      const files = Array.from((inputEl && inputEl.files) || []);
      if (!files || files.length === 0) return;
      const file = files[0];
      const badge = document.getElementById("wasmStatusBadge");
      const banner = document.getElementById("statusBanner");
      if (badge) badge.textContent = `Lettura PDF "${file.name}"...`;
      if (banner) {
        banner.textContent = `Lettura PDF Unifilare "${file.name}" in corso...`;
        banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-purple-500/20 text-purple-200 border border-purple-500/40 font-bold";
      }

      const allPdfItems = [];
      try {
        const buf = file.arrayBuffer();
        const pdfjsLib = loadPdfJs();
        const pdf = pdfjsLib.getDocument({ data: buf }).promise;
        for (let p = 1; p <= pdf.numPages; p++) {
          const page = pdf.getPage(p);
          const tc = page.getTextContent();
          const items = tc.items.map(it => ({ str: it.str.trim(), x: it.transform[4], y: it.transform[5] })).filter(it => it.str.length > 0);
          allPdfItems.push(...items);

          // Riconosce qualsiasi intestazione colonna di schema unifilare (es. "QGBT-P C-22", "QP1-DX-N C-4", ecc.)
          const headers = items.filter(it => /^[A-Z0-9]{2,12}(?:[-._/][A-Z0-9]{1,10})*\s+C-\d+/i.test(it.str) || /\bC-\d+\b/i.test(it.str));
          const descLabels = items.filter(it => /^(?:Descrizione(?:\s*(?:utenza|circuito|linea))?|Designazione|Utenza|Oggetto)\s*:?$/i.test(it.str.trim()));

          headers.forEach(ch => {
            const mHeaderCode = ch.str.match(/^([A-Z0-9]{2,12}(?:[-._/][A-Z0-9]{1,10})*\s+C-?\d+|C-?\d+|[A-Z0-9_-]+-C\d+)\b/i);
            const rawCode = mHeaderCode ? mHeaderCode[1].trim() : ch.str.replace(/\s+/g, " ").trim();
            const inlineDesc = (mHeaderCode && ch.str.length > mHeaderCode[0].length) ? cleanUtenzaDescription(ch.str.substring(mHeaderCode[0].length), rawCode) : null;
            const normCode = normalizeLineKey(rawCode);

            const colItems = items.filter(it => Math.abs(it.x - ch.x) < 48 && it.y < ch.y).sort((a, b) => b.y - a.y);
            const colTexts = colItems.map(c => c.str);

            const cableMatch = colTexts.find(t => /FG16|FTG18|H1Z2Z2|FS17|FG17|RG16|H07/i.test(t)) || null;
            const peMatch = colTexts.find(t => /(?:PE\s*[-:_]?\s*|\+)?(?:FG17|FS17|H07V-K|N07V-K)\b/i.test(t) && t !== cableMatch) || null;
            const peCableMatch = peMatch ? (peMatch.match(/FG17|FS17|H07V-K|N07V-K/i) || [null])[0] : null;

            const formMatch = colTexts.find(t => /\d+\s*\([0-9xXGg,+]+\)|\b[1-5][xXGg]\d+(?:[.,]\d+)?/i.test(t)) || null;
            const ibMatch = colTexts.find(t => /^\d+(?:[.,]\d+)?\s*\/\s*0[.,]\d+$/.test(t));
            const parsedIb = ibMatch ? parseFloat(ibMatch.split("/")[0].replace(",", ".")) : 10;

            // Estrazione mirata e pulita della sola descrizione utenza dalla riga Descrizione
            let descStr = null;
            let targetLabel = descLabels.filter(dl => dl.y < ch.y).sort((a, b) => (ch.y - a.y) - (ch.y - b.y))[0];
            if (!targetLabel && descLabels.length > 0) {
              targetLabel = descLabels.slice().sort((a, b) => Math.abs(a.y - ch.y) - Math.abs(b.y - ch.y))[0];
            }

            const isCircuitCodeToken = (s) => {
              const t = String(s || "").trim();
              if (!t) return false;
              if (t === rawCode || t === ch.str.trim()) return true;
              if (/^(?:[A-Z0-9]{2,12}(?:[-._/][A-Z0-9]{1,10})*\s+)?C-?\d+$/i.test(t)) return true;
              if (/^[A-Z0-9]{2,12}(?:[-._/][A-Z0-9]{1,10})*$/i.test(t) && rawCode.includes(t)) return true;
              return false;
            };

            const isElectricalRatingToken = (s) => {
              const t = String(s || "").trim();
              if (!t) return false;
              if (/^\d+(?:[.,]\d+)?\s*\/\s*\d+(?:[.,]\d+)?$/.test(t)) return true;
              if (/^\d+(?:[.,]\d+)?\s*(?:k?W|k?VA|k?VAR|Hz|V|A|kA|mA)$/i.test(t)) return true;
              if (/^(?:P|In|Ib|Idn|Icu|Ics)\s*=/i.test(t)) return true;
              return false;
            };

            if (targetLabel) {
              const descItems = items.filter(it =>
                Math.abs(it.x - ch.x) < 48 &&
                Math.abs(it.y - targetLabel.y) <= 10 &&
                it !== targetLabel &&
                !isCircuitCodeToken(it.str) &&
                !isElectricalRatingToken(it.str) &&
                it.str.trim() !== cableMatch &&
                it.str.trim() !== formMatch &&
                it.str.trim() !== peCableMatch &&
                it.str.trim() !== peMatch
              ).sort((a, b) => (Math.abs(b.y - a.y) > 3 ? (b.y - a.y) : (a.x - b.x)));

              if (descItems.length > 0) {
                const merged = descItems.map(it => it.str.trim()).join(" ");
                descStr = cleanUtenzaDescription(merged, rawCode);
              }
            }

            if (!descStr) {
              const fallbackItems = colItems.filter(it =>
                it.y < ch.y &&
                (ch.y - it.y) < 85 &&
                !isCircuitCodeToken(it.str) &&
                !isElectricalRatingToken(it.str) &&
                it.str.trim() !== cableMatch &&
                it.str.trim() !== formMatch &&
                it.str.trim() !== peCableMatch &&
                it.str.trim() !== peMatch &&
                !/^\d+$/.test(it.str.trim())
              ).sort((a, b) => (Math.abs(b.y - a.y) > 3 ? (b.y - a.y) : (a.x - b.x)));

              if (fallbackItems.length > 0) {
                const merged = fallbackItems.map(it => it.str.trim()).join(" ");
                descStr = cleanUtenzaDescription(merged, rawCode) || null;
              }
            }

            if (!descStr && inlineDesc) {
              descStr = inlineDesc;
            }

            const entry = { rawCode, desc: descStr, cable: cableMatch, peCable: peCableMatch, form: formMatch, ib: parsedIb };
            unifilareDict[normCode] = entry;
            const baseCode = getBaseCircuitCode(rawCode);
            unifilareDict[normalizeLineKey(baseCode)] = entry;

            // Registra anche indicizzazioni aggiuntive per numero circuito (es. "C-21", "C21")
            const mC = rawCode.match(/\b(C-?\d+)\b/i);
            if (mC) {
              const cTag = normalizeLineKey(mC[1]);
              if (!unifilareDict[cTag]) unifilareDict[cTag] = entry;
              const cNoHyphen = cTag.replace("-", "");
              if (!unifilareDict[cNoHyphen]) unifilareDict[cNoHyphen] = entry;
            }
            if (normCode.startsWith("OGBT")) unifilareDict[normCode.replace(/^OGBT/, "QGBT")] = entry;
          });
        }
        const linesCount = Object.keys(unifilareDict).length;
        if (badge) badge.textContent = `PDF Unifilare Attivo (${linesCount} linee)`;

        // Se c'è solo una scheda aperta, la scelta è immediata e automatica (nessun popup)
        if (!projectSections || projectSections.length <= 1) {
          pendingPdfImport = { fileName: file.name, allPdfItems, linesCount };
          applyPdfImportScope("current");
        } else {
          // Più schede aperte: apri il modal per permettere all'utente di scegliere
          openPdfImportScopeModal(file.name, linesCount, allPdfItems);
        }
      } catch (err) {
        console.error("Errore lettura PDF:", err);
        if (banner) {
          banner.textContent = `Errore lettura PDF "${file.name}": ${err.message || err}`;
          banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-red-500/20 text-red-300 border border-red-500/40 font-bold";
        }
      } finally {
        if (inputEl) inputEl.value = "";
      }
    }

    // ============================================================================
    // GESTIONE AMBITO IMPORTAZIONE PDF UNIFILARE (SCHEDA CORRENTE VS TUTTE LE SCHEDE)
    // ============================================================================
    let pendingPdfImport = null;

    function applyUnifilareToSection(sec, dict, allPdfItems, fileName, isCurrentSection = false) {
      if (!sec) return 0;
      let matchedCount = 0;
      sec.pdfLoaded = true;

      if (Array.isArray(sec.cables)) {
        sec.cables.forEach(c => {
          const u = findInUnifilareDict(c.code) || findInUnifilareDict(getBaseCircuitCode(c.code));
          if (u) {
            matchedCount++;
            if (u.desc) c.desc = u.desc;
            if (u.cable) c.type = u.cable;
            if (u.peCable) c.peType = u.peCable;
            if (u.form) {
              c.form = u.form;
              c.sec = parseSectionFromForm(u.form, c.sec);
              c.cores = isUnipolarCable(c) ? 1 : (u.form.includes("5G") || u.form.includes("4G") ? 3 : 2);
            }
            c.ib = u.ib || 10;

            if (isUnipolarCable(c)) {
              const base = getBaseCircuitCode(c.code);
              sec.cables.forEach(other => {
                if (other !== c && getBaseCircuitCode(other.code) === base) {
                  if (u.desc) other.desc = u.desc;
                  if (u.cable && !(/PE|TERRA/i.test(other.code))) other.type = u.cable;
                  if (u.peCable && (/PE|TERRA/i.test(other.code))) {
                    other.type = u.peCable;
                    other.peType = u.peCable;
                  }
                }
              });
            }
          } else {
            if (c.desc) c.desc = cleanUtenzaDescription(c.desc, c.code);
          }
        });

        // Registra automaticamente nel DB qualsiasi nuovo cavo identificato dallo schema
        sec.cables.forEach(c => registerCableInCatalog(c));
      }

      // Aggiorna titolo canalina e nome file se era generico o se trovato nel documento
      if (allPdfItems && fileName) {
        const pdfTrayName = extractTrayNameFromInput(allPdfItems, fileName, sec.width, sec.height);
        if (pdfTrayName && pdfTrayName.isExplicit && (isCurrentSection || (sec.title && sec.title.startsWith("NUOVA CANALINA")))) {
          sec.title = pdfTrayName.title;
          if (isCurrentSection) {
            const elInpTitle = document.getElementById("inpTitle"); if (elInpTitle) elInpTitle.value = sec.title;
            const qTitlePdf = document.getElementById("inpTitleQuick"); if (qTitlePdf) qTitlePdf.value = sec.title;
            const hTitle = document.getElementById("headerTitle"); if (hTitle) hTitle.textContent = sec.title;
            const elExpName = document.getElementById("exportName"); if (elExpName) elExpName.value = pdfTrayName.exportFile;
          }
        } else if (sec.title && sec.title.startsWith("NUOVA CANALINA") && fileName) {
          const pdfBase = fileName.replace(/\.pdf$/i, "").replace(/[^a-zA-Z0-9_-]+/g, " ").trim();
          if (pdfBase && !/schema|unifilare/i.test(pdfBase)) {
            sec.title = `${pdfBase} - L=${sec.width}mm H=${sec.height}mm`;
            if (isCurrentSection) {
              const elInpTitle = document.getElementById("inpTitle"); if (elInpTitle) elInpTitle.value = sec.title;
              const hTitle = document.getElementById("headerTitle"); if (hTitle) hTitle.textContent = sec.title;
              const cleanTag = pdfBase.replace(/[^a-zA-Z0-9_-]+/g, "_").replace(/^_+|_+$/g, "");
              const elExpName = document.getElementById("exportName"); if (elExpName) elExpName.value = cleanTag || "CANALINA_01";
            }
          }
        }
      }

      return matchedCount;
    }

    function openPdfImportScopeModal(fileName, linesCount, allPdfItems) {
      pendingPdfImport = { fileName, linesCount, allPdfItems };
      const m = document.getElementById("pdfImportScopeModal");
      if (!m) return;

      const elFile = document.getElementById("pdfScopeFileName");
      if (elFile) elFile.textContent = fileName || "schema_unifilare.pdf";

      const elLines = document.getElementById("pdfScopeLinesCount");
      if (elLines) elLines.textContent = `${linesCount || 0} linee trovate`;

      const elOpenTabs = document.getElementById("pdfScopeOpenTabsCount");
      if (elOpenTabs) elOpenTabs.textContent = `${projectSections.length} schede canalina`;

      const mParen = String(state.title || "").match(/\(([^)]+)\)/);
      const curName = mParen ? mParen[1].trim() : (state.title || `Canalina ${currentSectionIdx + 1}`);

      const elCurBadge = document.getElementById("pdfScopeCurrentBadge");
      if (elCurBadge) elCurBadge.textContent = `#${currentSectionIdx + 1}`;

      const elCurDetails = document.getElementById("pdfScopeCurrentDetails");
      if (elCurDetails) {
        elCurDetails.textContent = `${curName} (${state.width}x${state.height} mm • ${state.cables.length} cavi posati)`;
      }

      const elAllBadge = document.getElementById("pdfScopeAllBadge");
      if (elAllBadge) elAllBadge.textContent = `Tutte (${projectSections.length})`;

      m.classList.remove("hidden");
    }

    function closePdfImportScopeModal() {
      const m = document.getElementById("pdfImportScopeModal");
      if (m) m.classList.add("hidden");
      pendingPdfImport = null;
    }

    function applyPdfImportScope(scope) {
      if (!pendingPdfImport) return;
      const fileName = pendingPdfImport.fileName;
      const allPdfItems = pendingPdfImport.allPdfItems;
      closePdfImportScopeModal();

      const badge = document.getElementById("wasmStatusBadge");
      const banner = document.getElementById("statusBanner");

      if (scope === "all") {
        syncActiveSectionToMultiStore();

        let totalMatched = 0;
        let totalCables = 0;
        let updatedSectionsCount = 0;

        projectSections.forEach((sec, idx) => {
          const isCur = idx === currentSectionIdx;
          const matched = applyUnifilareToSection(sec, unifilareDict, allPdfItems, fileName, isCur);
          totalMatched += matched;
          totalCables += (sec.cables ? sec.cables.length : 0);
          if (matched > 0) updatedSectionsCount++;
        });

        state = cloneStateSnapshot(projectSections[currentSectionIdx]);

        if (badge) badge.textContent = `PDF Unifilare Attivo (${Object.keys(unifilareDict).length} linee)`;
        if (banner) {
          banner.textContent = `✓ PDF Unifilare sincronizzato per TUTTE le ${projectSections.length} schede aperte (${totalMatched}/${totalCables} cavi aggiornati in ${updatedSectionsCount} tratte canalina)!`;
          banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-purple-500/20 text-purple-200 border border-purple-500/40 font-bold";
        }
      } else {
        // Solo scheda corrente
        const matched = applyUnifilareToSection(state, unifilareDict, allPdfItems, fileName, true);
        syncActiveSectionToMultiStore();

        if (badge) badge.textContent = `PDF Unifilare Attivo (${Object.keys(unifilareDict).length} linee)`;
        if (banner) {
          const mParen = String(state.title || "").match(/\(([^)]+)\)/);
          const curName = mParen ? mParen[1].trim() : (state.title || `Canalina ${currentSectionIdx + 1}`);
          banner.textContent = `✓ PDF Unifilare sincronizzato per la scheda corrente #${currentSectionIdx + 1} "${curName}" (${matched}/${state.cables.length} cavi DWG abbinati)!`;
          banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-purple-500/20 text-purple-200 border border-purple-500/40 font-bold";
        }
      }

      const elElec = document.getElementById("kpiElectricalCard"); if (elElec) elElec.classList.remove("hidden");
      const elBar = document.getElementById("kpiBar"); if (elBar) elBar.className = "grid grid-cols-2 sm:grid-cols-3 xl:grid-cols-6 gap-2";

      pushStateToUI();
      recalculateAll();
      updateTableUI();
      drawScene();
      renderMultiSectionTabs();
    }

        function cleanUtenzaDescription(raw, codeToStrip = "") {
      if (!raw) return "";
      let s = String(raw).replace(/\r?\n/g, " ").trim();

      // 1. Rimuove prefissi di etichetta (es. "Descrizione:", "Descrizione utenza:", "Designazione:")
      s = s.replace(/^(?:Descrizione(?:\s*(?:utenza|circuito|linea))?|Designazione|Utenza|Oggetto)\s*[:=-]\s*/i, "");

      // 2. Rimuove la sigla specifica del circuito passata come argomento se presente all'inizio o alla fine o tra parentesi
      if (codeToStrip) {
        const esc = String(codeToStrip).trim().replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
        s = s.replace(new RegExp(`^${esc}\\s*[-:–—]?\\s*`, "i"), "");
        s = s.replace(new RegExp(`\\s*[-:–—]?\\s*${esc}$`, "i"), "");
        s = s.replace(new RegExp(`\\(\\s*${esc}\\s*\\)`, "gi"), "");
      }

      // 3. Rimuove codici circuito tra parentesi (es. "(QPT-DX-N C-21)", "(C-21)")
      s = s.replace(/\([^)]*\bC-?\d+\b[^)]*\)/gi, "");
      s = s.replace(/\([^)]*\b(?:QGBT|QP|QE|QUADRO|UPS|IRAI|EVAC|TD)[^)]*\)/gi, "");

      // 4. Rimuove qualsiasi sigla utenza / codice quadro-circuito all'inizio o alla fine (es. "QPT-DX-N C-21", "QGBT-P C-22", "C-21")
      const panelPrefix = `(?:(?:[QO][A-Z0-9._-]*|[A-Z0-9]+-[A-Z0-9._-]+)\\s+)?`;
      s = s.replace(new RegExp(`^${panelPrefix}C-?\\d+\\b\\s*[-:–—]?\\s*`, "i"), "");
      s = s.replace(new RegExp(`\\s*[-:–—]?\\s*${panelPrefix}C-?\\d+\\b\\s*$`, "i"), "");

      // 5. Rimuove rapporti di potenza / corrente anche con unità (es. "1 / 1,053", "/ 1,053", "1 / 1.053 kW", "15 / 24,5")
      s = s.replace(/\b\d+(?:[.,]\d+)?\s*\/\s*\d+(?:[.,]\d+)?\s*(?:k?W|k?VA|k?VAR|Hz|V|A|kA|mA)?\b/gi, "");
      s = s.replace(/(?:^|\s)\/?\s*\d+(?:[.,]\d+)?\s*\/\s*\d+(?:[.,]\d+)?\s*(?:k?W|k?VA|k?VAR|Hz|V|A|kA|mA)?\b/gi, "");
      s = s.replace(/(?:^|\s)\/\s*\d+(?:[.,]\d+)?\s*(?:k?W|k?VA|k?VAR|Hz|V|A|kA|mA)?\b/gi, "");

      // 6. Rimuove grandezze elettriche pure isolate (potenza, tensione, frequenza, corrente, cosphi)
      s = s.replace(/\b(?:P\s*=\s*)?\d+(?:[.,]\d+)?\s*(?:k?W|k?VA|k?VAR)\b/gi, "");
      s = s.replace(/\b(?:400|230|110|24)\s*V\b/gi, "");
      s = s.replace(/\b\d+(?:[.,]\d+)?\s*Hz\b/gi, "");
      s = s.replace(/\b(?:cos\s*phi|cos\s*φ)\s*=?\s*\d+(?:[.,]\d+)?\b/gi, "");
      s = s.replace(/\b(?:I[nbd]|Idn|Icu|Ics|Icn)\s*=?\s*\d+(?:[.,]\d+)?\s*(?:m?A|kA)?\b/gi, "");
      s = s.replace(/\b(?:k?W|k?VA|k?VAR)\b/gi, "");

      // 7. Rimuove formazioni cavi e tipi cavo residui
      s = s.replace(/\b(?:FG16[A-Z0-9]*|FTG18[A-Z0-9]*|FS17|FG17|H07[A-Z0-9-]*|H1Z2Z2-K|RG16[A-Z0-9]*)\b/gi, "");
      s = s.replace(/\b\d+\s*\([0-9xXGg,+]+\)|\b[1-5][xXGg]\d+(?:[.,]\d+)?\b/g, "");

      // 7b. Rimuove termini di polarità spuria e tipologie costruttive cavo (es. "Quadripolare", "Multipolare", "Cavo Unipolare")
      s = s.replace(/\b(?:cavo\s+|linea\s+)?(?:uni|bi|tri|quadri|penta|multi)polare\b/gi, "");
      s = s.replace(/\b(?:unipolare|bipolare|tripolare|quadripolare|pentapolare|multipolare)\b/gi, "");
      s = s.replace(/\b(?:cavo\s+)?(?:bt|mt|b\.t\.|m\.t\.)\b/gi, "");
      s = s.replace(/\b(?:monofase|trifase(?:\s*\+\s*n(?:eutro)?)?)\b/gi, "");

      // 8. Pulizia punteggiatura e spazi residui
      s = s.replace(/^[-+\s|/,.:;–—]+|[-+\s|/,.:;–—]+$/g, "");
      s = s.replace(/\s{2,}/g, " ").trim();

      // 9. Se molto lunga (> 65 caratteri), tronca con ellissi
      if (s.length > 65) {
        s = s.substring(0, 62).trim() + "..";
      }
      return s;
    }

    function getDisplayCableType(cable, allCables = []) {
      if (!cable) return "";
      if (cable.type && cable.type.includes("/")) return cable.type;
      if (cable.peType && cable.peType !== cable.type) {
        return `${cable.type} / ${cable.peType}`;
      }
      const base = getBaseCircuitCode(cable.code);
      if (base && Array.isArray(allCables) && allCables.length > 0) {
        const related = allCables.filter(c => getBaseCircuitCode(c.code) === base);
        if (related.length > 1) {
          const peConductor = related.find(c => /PE|TERRA|G\/V|GV/i.test(c.code) || /G\/V|PE/i.test(c.form) || /FG17|FS17/i.test(c.type));
          const phaseConductor = related.find(c => !(/PE|TERRA|G\/V|GV/i.test(c.code) || /FG17|FS17/i.test(c.type)));
          if (peConductor && phaseConductor && peConductor.type && phaseConductor.type && peConductor.type !== phaseConductor.type) {
            return `${phaseConductor.type} / ${peConductor.type}`;
          }
        }
      }
      return cable.type || "FG16M16";
    }

    function extractTrayNameFromInput(rawTexts, fileName, width = 200, height = 75) {
      let foundName = null;
      let isExplicit = false;

      function scoreTrayCandidate(cand) {
        if (!cand) return -100;
        const s = String(cand).trim();
        if (s.length < 2 || s.length > 80) return -100;
        // Escludi se sono solo dimensioni es. "300x75", "75 x 300 mm"
        if (/^\d+\s*[xX]\s*\d+/i.test(s)) return -100;
        // Escludi unità singole o acronimi normativi standard
        if (/^(mm|cm|m|kg|dan|n|kw|a|v|hz|cei|uni|iec|en|iso|rev\.?\s*\d*|scala|scale|foglio|tavola|pag\.?\s*\d*)$/i.test(s)) return -100;
        if (/^cei\b/i.test(s)) return -100;
        // Escludi formazioni cavo es. "C-21", "3G2.5", "1x16"
        if (/^C-?\d+$/i.test(s) || /^[1-5][xXGg]\d+/i.test(s)) return -100;
        if (!/[a-zA-Z]{2,}/.test(s)) return -100;

        let score = 10;
        if (/anello/i.test(s)) score += 60;
        if (/lato\s+(?:dx|sx|nord|sud|est|ovest)/i.test(s)) score += 50;
        if (/\b(?:bt|mt|at)\b/i.test(s)) score += 35;
        if (/\b(?:pt|p1|p2|p3|p-1|piano|terra|interrato|copertura|quadro|qgbt|edp|dorsale|montante|distribuzione)\b/i.test(s)) score += 30;
        if (/\b(?:sx|dx)\b/i.test(s)) score += 25;
        if (/\b(?:canale|canalina|passerella|tratta|tronco)\b/i.test(s)) score += 30;
        if (s.includes("-") || s.includes("/")) score += 15;
        if (s.length >= 6 && s.length <= 45) score += 10;
        return score;
      }

      const allCandidates = [];

      // A. Controlla il nome del file di input se contiene parentesi (...)
      if (fileName) {
        const fileRegex = /\(([^)]+)\)/g;
        let fm;
        while ((fm = fileRegex.exec(fileName)) !== null) {
          const c = fm[1].trim();
          const sc = scoreTrayCandidate(c);
          if (sc > 0) allCandidates.push({ name: c, score: sc + 30 });
        }
      }

      // B. Controlla i testi estratti dal DWG / DXF / PDF
      if (Array.isArray(rawTexts) && rawTexts.length > 0) {
        const cleanedItems = rawTexts.map(t => ({
          str: cleanAcadMText(t.str || ""),
          x: t.x || 0,
          y: t.y || 0
        })).filter(t => t.str.length > 0);

        // 1. Cerca nei singoli testi diciture con parentesi
        cleanedItems.forEach(t => {
          const s = t.str;
          const hasPrefix = /(?:Dati\s+caratteristici|Dati\s+tecnici|Dati|Canalina|Canale|Passerella|Condotto|Tratta|Tronco|Sezione|Particolare)/i.test(s);
          const regexP = /\(([^)]+)\)/g;
          let m;
          while ((m = regexP.exec(s)) !== null) {
            const cand = m[1].trim();
            const sc = scoreTrayCandidate(cand);
            if (sc > 0) {
              allCandidates.push({ name: cand, score: sc + (hasPrefix ? 50 : 20) });
            }
          }
        });

        // 2. Cerca nel testo unito se spezzato in entità adiacenti
        const allJoined = cleanedItems.map(t => t.str).join(" ");
        const regexAll = /\(([^)]+)\)/g;
        let jm;
        while ((jm = regexAll.exec(allJoined)) !== null) {
          const cand = jm[1].trim();
          const sc = scoreTrayCandidate(cand);
          if (sc > 0) {
            allCandidates.push({ name: cand, score: sc + 15 });
          }
        }

        // 3. Cerca diciture con due punti o trattino: "Dati caratteristici canalina: [Nome]"
        cleanedItems.forEach(t => {
          const mColon = t.str.match(/(?:Dati\s+caratteristici(?:\s+della)?\s+(?:canalina|passerella|canale)|(?:canalina|passerella|canale))\s*[:=-]\s*([A-Za-z0-9_\-./\s]{3,50})/i);
          if (mColon && mColon[1]) {
            const cand = mColon[1].trim();
            const sc = scoreTrayCandidate(cand);
            if (sc > 0) allCandidates.push({ name: cand, score: sc + 10 });
          }
        });

        // 4. Header convenzionali canalina/passerella
        cleanedItems.forEach(t => {
          const mHead = t.str.match(/^(?:CANALINA|CANALE|PASSERELLA|CONDOTTO|TRONCO)\s+([A-Za-z0-9_\-./\s]{2,40})/i);
          if (mHead && mHead[1]) {
            const cand = mHead[1].trim();
            const sc = scoreTrayCandidate(cand);
            if (sc > 0) allCandidates.push({ name: cand, score: sc + 5 });
          }
        });
      }

      // Scegli il candidato con il punteggio più alto
      if (allCandidates.length > 0) {
        allCandidates.sort((a, b) => b.score - a.score);
        foundName = allCandidates[0].name;
        isExplicit = true;
      }

      let cleanBase = foundName ? foundName : (fileName || "CANALINA").replace(/\.[^.]+$/, "").replace(/[^a-zA-Z0-9_-]+/g, " ").trim();
      cleanBase = cleanBase.replace(/^(?:Dati\s+caratteristici\s+(?:della\s+)?(?:canalina|passerella|canale)\s*[:=-]?\s*)/i, "")
                          .replace(/\s*-\s*L=\s*\d+\s*mm\s*H=\s*\d+\s*mm/i, "")
                          .replace(/\b\d+\s*[xX]\s*\d+(?:\s*mm)?\b/gi, "")
                          .replace(/L=\s*\d+\s*mm/i, "")
                          .replace(/H=\s*\d+\s*mm/i, "")
                          .replace(/^[\"'\s]+|[\"'\s]+$/g, "")
                          .trim();
      if (!cleanBase) cleanBase = "CANALINA 01";

      const title = cleanBase;
      const exportFile = cleanBase.replace(/[^a-zA-Z0-9_-]+/g, "_").replace(/^_+|_+$/g, "");
      return { title, exportFile, cleanBase, isExplicit };
    }

    function onScaleChange(val) {
      state.scale = val || "1:1";
      state.tb.scaleRev = `Scala ${state.scale} - ${state.tb.rev || "Rev. 00"}`;
      const elInp = document.getElementById("inpScale");
      if (elInp) elInp.value = state.scale;
      const elDxf = document.getElementById("selDxfScale");
      if (elDxf) elDxf.value = state.scale;
      const elCfg = document.getElementById("cfgScale");
      if (elCfg) elCfg.value = state.scale;
      const elRev = document.getElementById("inpScaleRev");
      if (elRev) elRev.value = state.tb.scaleRev;
      drawScene();
    }

    function onTextScaleChange(val) {
      const num = parseFloat(val);
      if (isNaN(num) || num <= 0.2 || num > 3.0) return;
      state.textScale = num;
      try {
        localStorage.setItem("canaloCAD_textScale", num.toString());
      } catch (e) {}
      const valStr = num.toString();
      const el1 = document.getElementById("selTextScale"); if (el1) el1.value = valStr;
      const el2 = document.getElementById("optDxfTextScale"); if (el2) el2.value = valStr;
      const el3 = document.getElementById("cfgTextScale"); if (el3) el3.value = valStr;
      drawScene();
    }

    function toggleSectionExport(sec, enabled) {
      if (sec === "chart") {
        state.showLoadChart = enabled;
        const c1 = document.getElementById("chkExportChart"); if (c1) c1.checked = enabled;
        const c2 = document.getElementById("menuChkChart"); if (c2) c2.checked = enabled;
      } else if (sec === "bom") {
        state.showBOM = enabled;
        const c1 = document.getElementById("chkExportBOM"); if (c1) c1.checked = enabled;
        const c2 = document.getElementById("menuChkBOM"); if (c2) c2.checked = enabled;
      } else if (sec === "iso3d") {
        state.showIso3D = enabled;
        const c1 = document.getElementById("chkExportIso3D"); if (c1) c1.checked = enabled;
        const c2 = document.getElementById("menuChk3D"); if (c2) c2.checked = enabled;
        const c3 = document.getElementById("chkIso3D"); if (c3) c3.checked = enabled;
      } else if (sec === "cartiglio") {
        state.showCartiglio = enabled;
        const c1 = document.getElementById("chkExportCartiglio"); if (c1) c1.checked = enabled;
        const c2 = document.getElementById("menuChkCartiglio"); if (c2) c2.checked = enabled;
      } else if (sec === "tableDesc") {
        state.showTableDesc = Boolean(enabled);
        state.hideTableDesc = !enabled;
        const c1 = document.getElementById("chkExportTableDesc"); if (c1) c1.checked = state.showTableDesc;
        const c2 = document.getElementById("menuChkTableDesc"); if (c2) c2.checked = state.showTableDesc;
      }
      drawScene();
    }

    function toggleTableDesc(enabled) {
      toggleSectionExport("tableDesc", enabled);
      syncActiveSectionToMultiStore();
    }

    function toggleLayoutMode() {
      state.layoutMode = (state.layoutMode === "minimal") ? "standard" : "minimal";
      updateLayoutUI();
      syncActiveSectionToMultiStore();
      recalculateAll();
      fitAll();
    }

    function setLayoutMode(mode) {
      state.layoutMode = mode === "minimal" ? "minimal" : "standard";
      updateLayoutUI();
      syncActiveSectionToMultiStore();
      recalculateAll();
      fitAll();
    }

    function updateLayoutUI() {
      const isMin = state.layoutMode === "minimal";
      const btn = document.getElementById("btnToggleLayout");
      if (btn) {
        btn.innerHTML = isMin
          ? `<span>📐 Layout: Minimal (2D+Tab)</span>`
          : `<span>📑 Layout: Standard (Affiancato)</span>`;
        btn.className = isMin
          ? "inline-flex items-center gap-1 bg-cyan-500/20 hover:bg-cyan-500/30 text-cyan-300 border border-cyan-500/40 px-2 py-0.5 rounded font-bold text-xs transition shadow-sm"
          : "inline-flex items-center gap-1 bg-slate-800 hover:bg-slate-700 text-slate-300 border border-slate-700 px-2 py-0.5 rounded font-medium text-xs transition";
      }
      const chkMenu = document.getElementById("menuChkLayoutMinimal");
      if (chkMenu) chkMenu.checked = isMin;
      const chkExp = document.getElementById("chkExportMinimalLayout");
      if (chkExp) chkExp.checked = isMin;
    }

    function toggleCableCallouts(enabled) {
      if (typeof enabled === "boolean") {
        state.showCableCallouts = enabled;
      } else {
        state.showCableCallouts = !(state.showCableCallouts !== false);
      }
      updateCableCalloutsUI();
      syncActiveSectionToMultiStore();
      drawScene();
    }

    function updateCableCalloutsUI() {
      const showCallouts = state.showCableCallouts !== false;
      const btn = document.getElementById("btnToggleCallouts");
      if (btn) {
        btn.innerHTML = showCallouts
          ? `<span>🏷️ Testi Cavi 2D: Estesi</span>`
          : `<span>🏷️ Testi Cavi 2D: Solo #</span>`;
        btn.className = showCallouts
          ? "inline-flex items-center gap-1 bg-slate-800 hover:bg-slate-700 text-slate-300 border border-slate-700 px-2 py-0.5 rounded font-medium text-xs transition"
          : "inline-flex items-center gap-1 bg-amber-500/20 hover:bg-amber-500/30 text-amber-300 border border-amber-500/40 px-2 py-0.5 rounded font-bold text-xs transition shadow-sm";
      }
      const chkMenu = document.getElementById("menuChkCallouts");
      if (chkMenu) chkMenu.checked = showCallouts;
      const chkExp = document.getElementById("chkExportCallouts");
      if (chkExp) chkExp.checked = showCallouts;
    }
// ---- 06-scene-iso.js

    function build3DIsoScene(prims, leftBoxX, lowestY, leftBoxW, W, H, thk, mat, sup) {
      const isoH = 126;
      const isoY = lowestY - isoH - 12;
      const isLight = state.theme === "light";
      const boxBg = isLight ? "#f8fafc" : "#0f172a";
      const boxBorder = isLight ? "#cbd5e1" : "#38bdf8";

      const line = (layer, x1, y1, x2, y2, color, lw = 0.4, alpha = 1) => prims.push({ t: "line", layer, x1, y1, x2, y2, color, lw, alpha });
      const rect = (layer, x, y, w, h, color, fill, solid = false) => prims.push({ t: "rect", layer, x, y, w, h, color, fill, solid });
      const poly = (layer, pts, color, fill = null, lw = 0.4, alpha = 1) => prims.push({ t: "poly", layer, pts, color, fill, lw, alpha });
      const txtMul = (typeof state.textScale === "number" && state.textScale > 0) ? state.textScale : 1.0;
      const text = (layer, x, y, h, str, color, align = "left", maxW = null) => {
        let s = String(str || "");
        if (maxW) {
          const maxCh = Math.max(4, Math.floor((maxW - 2) / (h * txtMul * 0.56)));
          if (s.length > maxCh) s = s.substring(0, maxCh - 2) + "..";
        }
        const effectiveH = +(h * txtMul).toFixed(3);
        prims.push({ t: "text", layer, x, y, h: effectiveH, str: s, color, align });
      };

      // Riquadro contenitore
      rect("VISTA_3D_ISO", leftBoxX, isoY, leftBoxW, isoH, boxBorder, boxBg, true);

      const trayLabels = {
        solid: "Canale Chiuso Cieco",
        perforated: "Passerella Asolata / Forata",
        mesh: "Passerella a Filo / Rete",
        ladder: "Passerella a Pioli / Scaletta"
      };
      const trayTypeStr = trayLabels[state.trayType] || "Passerella Portacavi";

      const hasSupport = Boolean(state.support && state.support !== "none");

      if (hasSupport) {
        text("VISTA_3D_ISO", leftBoxX + 6, isoY + isoH - 7, 2.3,
          `ASSONOMETRIA 3D COSTRUTTIVA — ${trayTypeStr.toUpperCase()} (PASSO MENSOLE Ls = ${state.spanM.toFixed(2)} m)`,
          "#38bdf8", "left", leftBoxW - 12);
        text("VISTA_3D_ISO", leftBoxX + 6, isoY + isoH - 12, 1.85,
          `Supporto: ${sup.name} | Portata Limite SWL: ${metrics.qMaxKgM} kg/m | Freccia f = ${metrics.deflectionMm} mm (Amm. ${metrics.maxDeflectionMm} mm)`,
          isLight ? "#475569" : "#94a3b8", "left", leftBoxW - 12);
      } else {
        text("VISTA_3D_ISO", leftBoxX + 6, isoY + isoH - 7, 2.3,
          `ASSONOMETRIA 3D COSTRUTTIVA — ${trayTypeStr.toUpperCase()}`,
          "#38bdf8", "left", leftBoxW - 12);
        text("VISTA_3D_ISO", leftBoxX + 6, isoY + isoH - 12, 1.85,
          `Canale L=${W}mm H=${H}mm | Peso Cavi: ${metrics.totalKgM} kg/m | Riempimento: ${metrics.fillPct}%`,
          isLight ? "#475569" : "#94a3b8", "left", leftBoxW - 12);
      }

      // Area interna utile per proiezione 3D
      const padX = 14, padY = 10;
      const uMinX = leftBoxX + padX;
      const uMaxX = leftBoxX + leftBoxW - padX;
      const uMinY = isoY + padY;
      const uMaxY = isoY + isoH - padY - 14;
      const availW = uMaxX - uMinX;
      const availH = uMaxY - uMinY;

      // Geometria assonometrica calibrata
      const angZ = 18 * Math.PI / 180;
      const angX = 34 * Math.PI / 180;
      const cosZ = Math.cos(angZ), sinZ = Math.sin(angZ);
      const cosX = Math.cos(angX), sinX = Math.sin(angX);

      const zMax = 6.4;
      const z1 = 1.4, z2 = 5.0;

      const keyPts = [
        { x: 0, y: 0, z: 0 },
        { x: 1, y: 0, z: 0 },
        { x: 0, y: 0.45, z: 0 },
        { x: 1, y: 0.45, z: 0 },
        { x: 0, y: 0, z: zMax },
        { x: 1, y: 0, z: zMax },
        { x: 0, y: 0.45, z: zMax },
        { x: 1, y: 0.45, z: zMax }
      ];

      if (hasSupport) {
        keyPts.push(
          { x: 1.25, y: -0.42, z: z1 },
          { x: -0.20, y: -0.42, z: z1 },
          { x: 1.25, y: 0.65, z: z2 },
          { x: -0.20, y: 0.65, z: z2 },
          { x: 0.5, y: -0.32, z: z1 },
          { x: 0.5, y: -0.32, z: z2 }
        );
      }

      const rawProj = keyPts.map(p => ({
        u: p.z * cosZ - p.x * cosX,
        v: p.z * sinZ + p.x * sinX + p.y * 1.05
      }));

      const minU = Math.min(...rawProj.map(p => p.u));
      const maxU = Math.max(...rawProj.map(p => p.u));
      const minV = Math.min(...rawProj.map(p => p.v));
      const maxV = Math.max(...rawProj.map(p => p.v));
      const uSpan = maxU - minU;
      const vSpan = maxV - minV;

      const scale = Math.min((availW - 6) / uSpan, (availH - 4) / vSpan);
      const fittedW = uSpan * scale;
      const fittedH = vSpan * scale;
      const offsetX = uMinX + (availW - fittedW) / 2 - minU * scale;
      const offsetY = uMinY + (availH - fittedH) / 2 - minV * scale;

      const iso = (x, y, z) => ({
        x: +(offsetX + (z * cosZ - x * cosX) * scale).toFixed(2),
        y: +(offsetY + (z * sinZ + x * sinX + y * 1.05) * scale).toFixed(2)
      });

      const iLine = (x1, y1, z1, x2, y2, z2, c, lw = 0.4, alpha = 1) => {
        const p1 = iso(x1, y1, z1), p2 = iso(x2, y2, z2);
        line("VISTA_3D_ISO", p1.x, p1.y, p2.x, p2.y, c, lw, alpha);
      };

      const iPoly = (pts3d, cStroke, cFill, lw = 0.4, alpha = 1) => {
        const pts2d = pts3d.map(p => iso(p.x, p.y, p.z));
        poly("VISTA_3D_ISO", pts2d, cStroke, cFill, lw, alpha);
      };

      // Palette cromatica materiali e finiture
      const colSteelStroke = isLight ? "#64748b" : "#94a3b8";
      const colWallPlateFill = isLight ? "#e2e8f0" : "#1e293b";
      const colArmTop = isLight ? "#f1f5f9" : "#475569";
      const colArmSide = isLight ? "#cbd5e1" : "#334155";
      const colTrayRear = isLight ? "#cbd5e1" : "#1e293b";
      const colTrayFloor = isLight ? "#e2e8f0" : "#223147";
      const colTrayFront = isLight ? "#f1f5f9" : "#2a3b52";

      // Testate frontali dei cavi (sezione a z = 0 con guaina e anima in rame)
      const iCableCap = (cx, cy, r, cSheathStroke, cSheathFill, cCoreFill = "#f59e0b") => {
        const ptsOuter = [];
        const nSteps = 14;
        for (let i = 0; i < nSteps; i++) {
          const ang = (i * 2 * Math.PI) / nSteps;
          ptsOuter.push({ x: cx + r * Math.cos(ang), y: cy + r * Math.sin(ang), z: 0 });
        }
        iPoly(ptsOuter, cSheathStroke, cSheathFill, 0.6);

        const ptsInner = [];
        const rCore = r * 0.65;
        for (let i = 0; i < nSteps; i++) {
          const ang = (i * 2 * Math.PI) / nSteps;
          ptsInner.push({ x: cx + rCore * Math.cos(ang), y: cy + rCore * Math.sin(ang), z: 0 });
        }
        iPoly(ptsInner, "#d97706", cCoreFill, 0.5);
      };

      // 1. PARETE DI FONDO (Linea guida e retino - disegnata solo con staffe a parete)
      if (hasSupport && state.support.startsWith("wall_")) {
        iLine(1.18, -0.42, 0.3, 1.18, -0.42, zMax - 0.1, isLight ? "#cbd5e1" : "#334155", 0.35);
        iLine(1.18, 0.60, 0.3, 1.18, 0.60, zMax - 0.1, isLight ? "#cbd5e1" : "#334155", 0.35);
      }

      // 2. SUPPORTI E MENSOLE A SBALZO (disegnati solo se presente staffaggio)
      if (hasSupport) {
        [z1, z2].forEach(zs => {
          if (state.support.startsWith("wall_")) {
          // Piastra profilata di ancoraggio a parete
          iPoly([
            { x: 1.18, y: -0.40, z: zs - 0.07 },
            { x: 1.18, y: 0.50, z: zs - 0.07 },
            { x: 1.18, y: 0.50, z: zs + 0.07 },
            { x: 1.18, y: -0.40, z: zs + 0.07 }
          ], colSteelStroke, colWallPlateFill, 0.6);
          // Tasselli M10 / Asola piastra
          iLine(1.18, 0.38, zs - 0.03, 1.18, 0.38, zs + 0.03, "#38bdf8", 0.8);
          iLine(1.18, -0.32, zs - 0.03, 1.18, -0.32, zs + 0.03, "#38bdf8", 0.8);

          // Braccio mensola a sbalzo (profilato sagomato a C con spessore e sbalzo)
          iPoly([
            { x: 1.18, y: -0.05, z: zs - 0.05 },
            { x: -0.12, y: -0.05, z: zs - 0.05 },
            { x: -0.12, y: -0.12, z: zs - 0.05 },
            { x: 1.18, y: -0.12, z: zs - 0.05 }
          ], colSteelStroke, colArmSide, 0.65);
          iPoly([
            { x: 1.18, y: -0.05, z: zs - 0.05 },
            { x: -0.12, y: -0.05, z: zs - 0.05 },
            { x: -0.12, y: -0.05, z: zs + 0.05 },
            { x: 1.18, y: -0.05, z: zs + 0.05 }
          ], "#cbd5e1", colArmTop, 0.5);

          // Puntone diagonale di scarico a 45°
          if (state.support !== "wall_light") {
            iPoly([
              { x: 1.18, y: -0.35, z: zs },
              { x: 0.35, y: -0.12, z: zs },
              { x: 0.42, y: -0.12, z: zs },
              { x: 1.18, y: -0.29, z: zs }
            ], colSteelStroke, colArmSide, 0.6);
          }
        } else if (state.support === "ceiling_trap") {
          // Traversa orizzontale a trapezio sotto canalina
          iPoly([
            { x: -0.14, y: -0.06, z: zs - 0.04 },
            { x: 1.14, y: -0.06, z: zs - 0.04 },
            { x: 1.14, y: -0.12, z: zs - 0.04 },
            { x: -0.14, y: -0.12, z: zs - 0.04 }
          ], colSteelStroke, colArmSide, 0.65);
          // Tiranti verticali M8/M10 verso soffitto
          iLine(-0.10, -0.06, zs, -0.10, 0.68, zs, "#cbd5e1", 0.7);
          iLine(1.10, -0.06, zs, 1.10, 0.68, zs, "#cbd5e1", 0.7);
          // Morsetti di ancoraggio a soffitto
          iPoly([
            { x: -0.14, y: 0.68, z: zs - 0.05 },
            { x: -0.06, y: 0.68, z: zs - 0.05 },
            { x: -0.06, y: 0.68, z: zs + 0.05 },
            { x: -0.14, y: 0.68, z: zs + 0.05 }
          ], "#38bdf8", colArmSide, 0.6);
          iPoly([
            { x: 1.06, y: 0.68, z: zs - 0.05 },
            { x: 1.14, y: 0.68, z: zs - 0.05 },
            { x: 1.14, y: 0.68, z: zs + 0.05 },
            { x: 1.06, y: 0.68, z: zs + 0.05 }
          ], "#38bdf8", colArmSide, 0.6);
        } else if (state.support === "ceiling_single") {
          // Pendino singolo centrale
          iLine(0.50, -0.06, zs, 0.50, 0.68, zs, "#cbd5e1", 0.85);
          iPoly([
            { x: -0.08, y: -0.06, z: zs - 0.04 },
            { x: 1.08, y: -0.06, z: zs - 0.04 },
            { x: 1.08, y: -0.12, z: zs - 0.04 },
            { x: -0.08, y: -0.12, z: zs - 0.04 }
          ], colSteelStroke, colArmSide, 0.65);
        } else if (state.support === "floor_omega") {
          // Supporto a pavimento omega
          iPoly([
            { x: -0.06, y: -0.05, z: zs },
            { x: -0.06, y: -0.32, z: zs },
            { x: 0.02, y: -0.32, z: zs },
            { x: 0.02, y: -0.05, z: zs }
          ], colSteelStroke, colArmSide, 0.65);
          iPoly([
            { x: 0.98, y: -0.05, z: zs },
            { x: 0.98, y: -0.32, z: zs },
            { x: 1.06, y: -0.32, z: zs },
            { x: 1.06, y: -0.05, z: zs }
          ], colSteelStroke, colArmSide, 0.65);
          iLine(-0.10, -0.32, zs, 1.10, -0.32, "#64748b", 0.5);
        }
      });
      }

      // 3. CANALINA — PARETE POSTERIORE E FONDO IN 3D
      // Parete posteriore solida (x = 1.0)
      iPoly([
        { x: 1.0, y: 0, z: 0 },
        { x: 1.0, y: 0.45, z: 0 },
        { x: 1.0, y: 0.45, z: zMax },
        { x: 1.0, y: 0, z: zMax }
      ], colSteelStroke, colTrayRear, 0.65);
      // Bordino superiore posteriore a C
      iLine(1.0, 0.45, 0, 0.95, 0.45, 0, "#94a3b8", 0.5);
      iLine(0.95, 0.45, 0, 0.95, 0.45, zMax, "#94a3b8", 0.45);
      iLine(0.95, 0.45, zMax, 1.0, 0.45, zMax, "#94a3b8", 0.5);

      // Piano del fondo canalina solido (y = 0, da x=0 a x=1)
      iPoly([
        { x: 0, y: 0, z: 0 },
        { x: 1.0, y: 0, z: 0 },
        { x: 1.0, y: 0, z: zMax },
        { x: 0, y: 0, z: zMax }
      ], colSteelStroke, colTrayFloor, 0.65);

      // 4. STILIZZAZIONE CARATTERISTICA DELLA TIPOLOGIA DI PASSERELLA
      if (state.trayType === "perforated") {
        // Canale asolato con file di forature regolari
        for (let z = 0.45; z <= zMax - 0.45; z += 0.55) {
          iLine(0.25, 0.005, z, 0.25, 0.005, z + 0.28, "#f59e0b", 0.5);
          iLine(0.50, 0.005, z, 0.50, 0.005, z + 0.28, "#f59e0b", 0.5);
          iLine(0.75, 0.005, z, 0.75, 0.005, z + 0.28, "#f59e0b", 0.5);
          iLine(1.0, 0.22, z, 1.0, 0.22, z + 0.25, "#f59e0b", 0.45);
        }
      } else if (state.trayType === "ladder") {
        // Passerella a pioli (traversine sagomate a C con spessore ogni passo)
        for (let z = 0.35; z <= zMax - 0.35; z += 0.65) {
          iPoly([
            { x: 0, y: 0.03, z: z },
            { x: 1.0, y: 0.03, z: z },
            { x: 1.0, y: 0.03, z: z + 0.12 },
            { x: 0, y: 0.03, z: z + 0.12 }
          ], "#f59e0b", isLight ? "#fef3c7" : "#78350f", 0.65);
          iLine(0, 0.035, z + 0.06, 1.0, 0.035, z + 0.06, "#fbbf24", 0.5);
        }
      } else if (state.trayType === "mesh") {
        // Passerella a filo / rete
        [0.25, 0.50, 0.75].forEach(rx => {
          iLine(rx, 0.01, 0, rx, 0.01, zMax, "#0284c7", 0.55);
        });
        for (let z = 0.25; z <= zMax - 0.25; z += 0.45) {
          iLine(0, 0.40, z, 0, 0, z, "#38bdf8", 0.6);
          iLine(0, 0, z, 1.0, 0, z, "#38bdf8", 0.6);
          iLine(1.0, 0, z, 1.0, 0.40, z, "#38bdf8", 0.6);
        }
      } else if (state.trayType === "solid") {
        // Canale cieco liscio con pieghe di irrigidimento
        iLine(0.33, 0.005, 0, 0.33, 0.005, zMax, isLight ? "#cbd5e1" : "#334155", 0.45);
        iLine(0.67, 0.005, 0, 0.67, 0.005, zMax, isLight ? "#cbd5e1" : "#334155", 0.45);
      }

      // Coperchio superiore 3D a scatto se abilitato
      if (state.cover !== "none") {
        iPoly([
          { x: -0.04, y: 0.48, z: 0 },
          { x: 1.04, y: 0.48, z: 0 },
          { x: 1.04, y: 0.48, z: zMax },
          { x: -0.04, y: 0.48, z: zMax }
        ], "#38bdf8", isLight ? "#e0f2fe" : "#0c4a6e", 0.65, 0.85);
      }

      // 5. FASCI CAVI 3D REALI CORRISPONDENTI ESATTAMENTE ALLA SEZIONE 1:1
      if (state.cables && state.cables.length > 0) {
        // Ordina i cavi per profondità isometrica decrescente (Painter's algorithm):
        // Cavi con X maggiore (vicini alla parete posteriore x=1.0) e Y minore disegnati per primi,
        // cavi in primo piano (X minore verso il bordo anteriore x=0) disegnati sopra.
        const sortedCables = [...state.cables].sort((a, b) => {
          const depthA = a.x * 10 - a.y * 3;
          const depthB = b.x * 10 - b.y * 3;
          return depthB - depthA;
        });

        sortedCables.forEach((c) => {
          const isUni = isUnipolarCable(c);
          const isPE = /PE|TERRA/i.test(c.code) || c.sys === "PE" || /PE/i.test(c.form || "");
          const isSignal = c.sys === "SPECIAL_ELV" || /IRAI|EVAC|KNX|BUS|FIBRA|CAT|UTP|FTP/i.test(c.type || "");
          const isFire = c.sys === "BT_FIRE" || /FTG|REI|INCEND/i.test(c.type || "");

          let cSheathStroke, cSheathFill, cCrest, cCoreFill;
          if (isPE) {
            cSheathStroke = "#15803d"; cSheathFill = "#22c55e"; cCrest = "#86efac"; cCoreFill = "#f59e0b";
          } else if (isSignal) {
            cSheathStroke = "#0369a1"; cSheathFill = "#0284c7"; cCrest = "#7dd3fc"; cCoreFill = "#38bdf8";
          } else if (isFire) {
            cSheathStroke = "#991b1b"; cSheathFill = "#dc2626"; cCrest = "#fca5a5"; cCoreFill = "#fbbf24";
          } else if (isUni) {
            cSheathStroke = "#b45309"; cSheathFill = "#d97706"; cCrest = "#fde68a"; cCoreFill = "#ea580c";
          } else {
            cSheathStroke = isLight ? "#475569" : "#1e293b";
            cSheathFill   = isLight ? "#64748b" : "#334155";
            cCrest        = isLight ? "#94a3b8" : "#64748b";
            cCoreFill     = "#f59e0b";
          }

          const normCx = Math.max(0.04, Math.min(0.96, c.x / W));
          const normCy = Math.max(0.02, Math.min(0.40, (c.y / H) * 0.38));
          const normR  = Math.max(0.010, Math.min(0.065, ((c.d / 2) / W) * 0.92));

          const xLeft  = Math.max(0.01, normCx - normR);
          const xRight = Math.min(0.99, normCx + normR);
          const yTop   = normCy + normR;

          // Corpo cilindrico estruso lungo l'asse Z da z=0 a z=zMax
          iPoly([
            { x: xLeft,  y: normCy, z: 0 },
            { x: xRight, y: normCy, z: 0 },
            { x: xRight, y: normCy, z: zMax },
            { x: xLeft,  y: normCy, z: zMax }
          ], cSheathStroke, cSheathFill, 0.55);

          // Linea di cresta longitudinale / riflesso luce
          iLine(normCx, yTop, 0, normCx, yTop, zMax, cCrest, 0.45);

          // Calotta frontale circolare della sezione a z = 0
          iCableCap(normCx, normCy, normR, cSheathStroke, cSheathFill, cCoreFill);
        });
      }

      // 6. FASCETTE DI ANCORAGGIO CAVI IN 3D ALLE MENSOLE (avvolgenti il fascio reale)
      if (hasSupport && state.clamps && state.cables && state.cables.length > 0) {
        const minX = Math.max(0.04, Math.min(...state.cables.map(c => Math.max(0.04, (c.x - c.d / 2) / W))));
        const maxX = Math.min(0.96, Math.max(...state.cables.map(c => Math.min(0.96, (c.x + c.d / 2) / W))));
        const maxY = Math.min(0.42, Math.max(...state.cables.map(c => ((c.y + c.d / 2) / H) * 0.38)) + 0.02);
        [z1, z2].forEach(zs => {
          iPoly([
            { x: Math.max(0.02, minX - 0.02), y: maxY, z: zs - 0.04 },
            { x: Math.min(0.98, maxX + 0.02), y: maxY, z: zs - 0.04 },
            { x: Math.min(0.98, maxX + 0.02), y: maxY, z: zs + 0.04 },
            { x: Math.max(0.02, minX - 0.02), y: maxY, z: zs + 0.04 }
          ], "#0891b2", "#06b6d4", 0.75);
        });
      }

      // 7. SETTI SEPARATORI METALLICI LONGITUDINALI (Se presenti)
      if (state.septums > 0) {
        getSeptumXList().forEach((sx, sIdx) => {
          const normX = +(sx / state.width).toFixed(3);
          iPoly([
            { x: normX, y: 0, z: 0 },
            { x: normX, y: 0.42, z: 0 },
            { x: normX, y: 0.42, z: zMax },
            { x: normX, y: 0, z: zMax }
          ], colSteelStroke, isLight ? "#cbd5e1" : "#475569", 0.6, 0.85);
          iLine(normX, 0.42, 0, normX, 0.42, zMax, "#38bdf8", 0.75);
          const pSep = iso(normX, 0.45, 0);
          text("VISTA_3D_ISO", pSep.x, pSep.y - 1.5, 1.6, `SETTO #${sIdx + 1}`, "#38bdf8", "center");
        });
      }

      // 8. BARRIERA TAGLIAFUOCO EI120 (Se reiCount > 0)
      if (state.reiCount > 0) {
        const midZ = (z1 + z2) / 2;
        iPoly([
          { x: -0.06, y: -0.06, z: midZ },
          { x: 1.06, y: -0.06, z: midZ },
          { x: 1.06, y: 0.50, z: midZ },
          { x: -0.06, y: 0.50, z: midZ }
        ], "#b91c1c", "#ef4444", 0.8, 0.75);
        const pRei = iso(1.08, 0.25, midZ);
        text("VISTA_3D_ISO", pRei.x + 2, pRei.y, 1.85, `SETTO EI120 (${state.reiCount}x)`, "#f87171", "left");
      }

      // 9. PARETE ANTERIORE DELLA CANALINA (Visibilità trasparente dei cavi interni)
      iPoly([
        { x: 0, y: 0, z: 0 },
        { x: 0, y: 0.35, z: 0 },
        { x: 0, y: 0.35, z: zMax },
        { x: 0, y: 0, z: zMax }
      ], colSteelStroke, colTrayFront, 0.65, 0.55);
      iLine(0, 0.35, 0, 0, 0.35, zMax, "#38bdf8", 0.8);
      iLine(0.04, 0.35, 0, 0.04, 0.35, zMax, "#0284c7", 0.45);

      // 10. TESTATA SEZIONE FRONTALE A Z = 0 (Sagoma a C in evidenza con spessore)
      iPoly([
        { x: -0.02, y: 0.36, z: 0 },
        { x: -0.02, y: -0.02, z: 0 },
        { x: 1.02, y: -0.02, z: 0 },
        { x: 1.02, y: 0.46, z: 0 },
        { x: 0.96, y: 0.46, z: 0 },
        { x: 0.96, y: 0.02, z: 0 },
        { x: 0.04, y: 0.02, z: 0 },
        { x: 0.04, y: 0.36, z: 0 }
      ], "#38bdf8", isLight ? "#e0f2fe" : "#0c4a6e", 0.85);

      // 11. QUOTA ASSONOMETRICA DEL PASSO TRA LE MENSOLE Ls (solo se presente staffaggio)
      if (hasSupport) {
        const dimY = -0.28;
        const pZ1 = iso(0.50, dimY, z1);
        const pZ2 = iso(0.50, dimY, z2);
        // Linee di testimone
        iLine(0.50, -0.10, z1, 0.50, dimY - 0.02, z1, "#38bdf8", 0.35);
        iLine(0.50, -0.10, z2, 0.50, dimY - 0.02, z2, "#38bdf8", 0.35);
        // Linea di quota
        line("VISTA_3D_ISO", pZ1.x, pZ1.y, pZ2.x, pZ2.y, "#f59e0b", 0.6);
        // Ticks a 45°
        line("VISTA_3D_ISO", pZ1.x - 1.5, pZ1.y - 1.5, pZ1.x + 1.5, pZ1.y + 1.5, "#f59e0b", 0.65);
        line("VISTA_3D_ISO", pZ2.x - 1.5, pZ2.y - 1.5, pZ2.x + 1.5, pZ2.y + 1.5, "#f59e0b", 0.65);

        const midPt = iso(0.50, dimY, (z1 + z2) / 2);
        text("VISTA_3D_ISO", midPt.x, midPt.y - 3.5, 2.15,
          `PASSO MENSOLE Ls = ${state.spanM.toFixed(2)} m (SWL = ${metrics.qMaxKgM} kg/m)`,
          "#fbbf24", "center");
      }

      return isoY;
    }

// ---- 07-scene-2d.js
    // ============================================================================
    // 6. COSTRUZIONE SCENA VETTORIALE 1:1 (CON UNIPOLARI 1X & MORSETTO PE REALISTICO)
    // ============================================================================
    // ============================================================================
    // MISURA E "A CAPO" DEI TESTI DELLA TAVOLA
    // Altezza CAD = altezza delle maiuscole (cap height): per Arial 0,716 em.
    // La larghezza è misurata sul font reale, così le celle delle tabelle bastano anche nei CAD.
    // ============================================================================
    const CAD_CAP_HEIGHT_EM = 0.716;
    const cadMeasureCtx = document.createElement("canvas").getContext("2d");
    const cadWidthCache = new Map();
    function cadTextWidth(str, capH) {
      const s = String(str == null ? "" : str).replace(/%%[cdp]/gi, "Ø");
      let w100 = cadWidthCache.get(s);
      if (w100 === undefined) {
        cadMeasureCtx.font = "100px Arial, 'Liberation Sans', Helvetica, sans-serif";
        w100 = cadMeasureCtx.measureText(s).width;
        if (cadWidthCache.size > 5000) cadWidthCache.clear();
        cadWidthCache.set(s, w100);
      }
      return (w100 / 100) * (capH / CAD_CAP_HEIGHT_EM);
    }

    // Divide il testo in righe larghe al massimo maxW (a parole; una parola troppo lunga va spezzata)
    function wrapCadText(str, maxW, capH) {
      const words = String(str).trim().split(/\s+/).filter(Boolean);
      if (!words.length) return [""];
      const lines = [];
      let cur = "";
      const pushWord = (w) => {
        const test = cur ? `${cur} ${w}` : w;
        if (cadTextWidth(test, capH) <= maxW) { cur = test; return; }
        if (cur) { lines.push(cur); cur = ""; }
        if (cadTextWidth(w, capH) <= maxW) { cur = w; return; }
        // parola più larga della cella: spezza preferendo / - ( dopo il carattere
        let part = "";
        for (const ch of w) {
          if (part && cadTextWidth(part + ch, capH) > maxW) { lines.push(part); part = ""; }
          part += ch;
        }
        cur = part;
      };
      words.forEach(pushWord);
      if (cur) lines.push(cur);
      return lines;
    }

    function buildCADScene() {
      const prims = [];
      const W = state.width, H = state.height, thk = state.thk;
      const isLight = state.theme === "light";
      const mat = MAT_SPECS[state.material] || MAT_SPECS.sendzimir;
      const sup = SUPPORT_SPECS[state.support] || SUPPORT_SPECS.wall_med;
      const txtCol = isLight ? "#0f172a" : "#f8fafc";
      const boxBg = isLight ? "#f1f5f9" : "#0f172a";
      const boxAlt = isLight ? "#e2e8f0" : "#1e293b";

      const line = (layer, x1, y1, x2, y2, color, lw = 0.4, alpha = 1) => prims.push({ t: "line", layer, x1, y1, x2, y2, color, lw, alpha });
      const rect = (layer, x, y, w, h, color, fill, solid = false) => prims.push({ t: "rect", layer, x, y, w, h, color, fill, solid });
      const circle = (layer, cx, cy, r, color, fill, solid = false) => prims.push({ t: "circle", layer, cx, cy, r, color, fill, solid });
      const txtMul = (typeof state.textScale === "number" && state.textScale > 0) ? state.textScale : 1.0;
      // Standardizzazione altezza testi: nominale 2.5mm moltiplicata per txtMul
      const text = (layer, x, y, h = 2.5, str, color, align = "left", maxW = null) => {
        let s = String(str || "");
        const nominalH = (typeof h === "number" && h > 0) ? h : 2.5;
        if (maxW) {
          const maxCh = Math.max(4, Math.floor((maxW - 2) / (nominalH * txtMul * 0.56)));
          if (s.length > maxCh) s = s.substring(0, maxCh - 2) + "..";
        }
        const effectiveH = +(nominalH * txtMul).toFixed(3);
        prims.push({ t: "text", layer, x, y, h: effectiveH, str: s, color: isLight && color === "#f8fafc" ? txtCol : color, align });
      };

      const ceilY = H + (state.clearance > 0 ? 42 : 18);
      if (state.clearance > 0) {
        line("STAFFAGGI", -25, ceilY, W + 25, ceilY, "#64748b", 0.6);
        for (let hx = -22; hx <= W + 20; hx += 8) line("STAFFAGGI", hx, ceilY, hx + 5, ceilY + 5, "#475569", 0.3);
        line("QUOTE", -15, H, -15, ceilY, "#38bdf8", 0.4);
        text("QUOTE", -18, (H + ceilY) / 2 - 1, 2.5, `Rispetto >= ${state.clearance}mm`, "#38bdf8", "right");
      }

      // Profilo Canalina
      rect("Canalina", -thk, -thk, thk, H + thk, mat.stroke, mat.fill, true);
      rect("Canalina", W, -thk, thk, H + thk, mat.stroke, mat.fill, true);
      rect("Canalina", 0, -thk, W, thk, mat.stroke, mat.fill, true);
      if (state.trayType === "perforated") {
        for (let x = 10; x < W - 10; x += 16) rect("Canalina", x, -thk, 7, thk, "#f59e0b", isLight ? "#ffffff" : "#04070d", true);
      }
      if (state.edgeGuard) {
        circle("Canalina", 1.3, H - 1.3, 1.4, mat.stroke, mat.fill, true);
        circle("Canalina", W - 1.3, H - 1.3, 1.4, mat.stroke, mat.fill, true);
      }
      if (state.cover !== "none") {
        const cy = H + (state.cover === "ventilated" ? 8 : 0.8);
        rect("Canalina", -thk - 2, cy, W + 2 * thk + 4, thk, mat.stroke, mat.fill, true);
        line("Canalina", -thk - 2, cy - 4, -thk - 2, cy, mat.stroke, 0.6);
        line("Canalina", W + thk + 2, cy - 4, W + thk + 2, cy, mat.stroke, 0.6);
      }
      // Setti Separatori con maniglia di presa ergonomica salvacavi
      getSeptumXList().forEach((sx, sIdx) => {
        // Piede di fissaggio inferiore al fondo canalina
        rect("Canalina", sx - 6, 0, 12, thk, "#38bdf8", "#0e7490", true);
        circle("Canalina", sx, thk * 0.5, 0.8, "#38bdf8", "#38bdf8", true);

        // Lama verticale del setto
        rect("Canalina", sx - thk / 2, 0, thk, H - 3, "#38bdf8", "#0e7490", true);

        // Maniglia/Bordino superiore arrotondato salvacavi (CEI 64-8) e grip di trascinamento
        rect("Canalina", sx - 3.5, H - 3, 7, 3.5, "#38bdf8", "#0284c7", true);
        circle("Canalina", sx - 3.5, H - 1.25, 1.75, "#38bdf8", "#0284c7", true);
        circle("Canalina", sx + 3.5, H - 1.25, 1.75, "#38bdf8", "#0284c7", true);

        // Frecce interattive trascinamento / maniglia visibile '◄ ►'
        text("Canalina", sx, H + 2.8, 2.0, "◄ ►", "#38bdf8", "center");
        text("Canalina", sx, H + 5.5, 1.7, `SETTO #${sIdx + 1} (${Math.round(sx)}mm)`, "#06b6d4", "center");
      });

      // Quotatura larghezze scomparti generati dai setti
      if (state.septums > 0) {
        const sepXList = [0, ...getSeptumXList(), W];
        for (let sIdx = 0; sIdx < sepXList.length - 1; sIdx++) {
          const x0 = sepXList[sIdx], x1 = sepXList[sIdx + 1];
          const cMid = (x0 + x1) / 2;
          const dimY = -thk - 10;
          line("Quote", x0 + 1, dimY, x1 - 1, dimY, "#0284c7", 0.35);
          line("Quote", x0, dimY - 1.5, x0, dimY + 1.5, "#0284c7", 0.4);
          line("Quote", x1, dimY - 1.5, x1, dimY + 1.5, "#0284c7", 0.4);
          line("Quote", x0 - 1, dimY - 1, x0 + 1, dimY + 1, "#0284c7", 0.4);
          line("Quote", x1 - 1, dimY - 1, x1 + 1, dimY + 1, "#0284c7", 0.4);
          text("Quote", cMid, dimY - 2.5, 2.0, `L${sIdx + 1} = ${Math.round(x1 - x0)} mm`, "#38bdf8", "center");
        }
      }

      // Staffaggi (Disegnati sul layer STAFFAGGI solo se configurato un supporto)
      let lowestY = -thk - 18;
      if (state.support && state.support !== "none") {
        if (state.support.startsWith("wall_")) {
          const wallX = -25, armH = sup.armH;
          line("STAFFAGGI", wallX, -50, wallX, H + 15, "#64748b", 0.6);
          rect("STAFFAGGI", wallX, -38, 4, 44, "#94a3b8", "#475569", true);
          rect("STAFFAGGI", wallX + 4, -thk - armH, W + 38, armH, "#94a3b8", "#334155", true);
          if (sup.strut) line("STAFFAGGI", wallX + 4, -34, W * 0.7, -thk - armH, "#94a3b8", 0.65);
          lowestY = -52;
        } else if (state.support === "ceiling_trap") {
          rect("STAFFAGGI", -22, -thk - 12, W + 44, 12, "#94a3b8", "#334155", true);
          [-14, W + 14].forEach(rx => line("STAFFAGGI", rx, -thk - 12, rx, ceilY, "#cbd5e1", 0.5));
          lowestY = -32;
        } else if (state.support === "ceiling_single") {
          line("STAFFAGGI", W / 2, -thk - 10, W / 2, ceilY, "#cbd5e1", 0.5);
          rect("STAFFAGGI", -15, -thk - 10, W + 30, 10, "#94a3b8", "#334155", true);
          lowestY = -28;
        } else if (state.support === "floor_omega") {
          rect("STAFFAGGI", -10, -thk - 18, 12, 18, "#94a3b8", "#334155", true);
          rect("STAFFAGGI", W - 2, -thk - 18, 12, 18, "#94a3b8", "#334155", true);
          lowestY = -36;
        }
      }

      // ==========================================================================
      // PUNTO 5: PARTICOLARE COSTRUTTIVO REALISTICO MORSETTO PE / TRECCIA DI RAME
      // Posizionato sul fianco esterno destro della sponda (oppure sinistro) con
      // bullone M6 esagonale, rondella dentellata, capocorda a occhiello e treccia
      // ==========================================================================
      if (state.earth !== "none") {
        const isGV = state.earth.startsWith("gv");
        const secPE = state.earth.replace(/\D+/g, "") || "16";
        // Posizioniamo il morsetto sulla sponda sinistra a metà altezza (Y = H * 0.52)
        const baseX = -thk;
        const baseY = H * 0.52;

        // 1. Piastrina / Morsetto equipotenziale in acciaio inox/ottone ancorato alla sponda
        rect("STAFFAGGI", baseX - 3.2, baseY - 5.5, 3.2, 11.0, "#cbd5e1", "#475569", true);
        // Rondella zigrinata antisvitamento
        rect("STAFFAGGI", baseX - 4.0, baseY - 3.8, 0.8, 7.6, "#94a3b8", "#64748b", true);
        // Bullone di serraggio M6x16 (testa esagonale + asse filettato passante nella sponda)
        rect("STAFFAGGI", baseX - 6.2, baseY - 2.6, 2.2, 5.2, "#e2e8f0", "#94a3b8", true);
        line("STAFFAGGI", baseX - 6.2, baseY - 1.2, baseX - 4.0, baseY - 1.2, "#475569", 0.35);
        line("STAFFAGGI", baseX - 6.2, baseY + 1.2, baseX - 4.0, baseY + 1.2, "#475569", 0.35);
        // Dado interno M6 sulla sponda
        rect("STAFFAGGI", 0, baseY - 2.2, 1.4, 4.4, "#cbd5e1", "#64748b", true);
        // Asse vite M6 (linea tratteggiata rossa/ambra)
        line("STAFFAGGI", baseX - 7.5, baseY, 2.5, baseY, "#f59e0b", 0.3, 0.8);

        // 2. Capocorda a occhiello / tubolare pressato verso il basso
        rect("STAFFAGGI", baseX - 4.8, baseY - 9.0, 2.4, 4.2, "#fbbf24", "#b45309", true);

        // 3. Conduttore PE: Treccia piatta in rame stagnato/nudo oppure Cavo Giallo/Verde bicolore
        const braidTopY = baseY - 9.0;
        const braidBotY = baseY - 23.0;
        const braidX = baseX - 3.6;

        if (isGV) {
          // Cavo unipolare Giallo/Verde con fasciatura elicoidale bicolore realistica
          const segments = 6;
          const segH = (braidTopY - braidBotY) / segments;
          for (let s = 0; s < segments; s++) {
            const sy = braidBotY + s * segH;
            const colFill = s % 2 === 0 ? "#16a34a" : "#eab308";
            const colStroke = s % 2 === 0 ? "#4ade80" : "#fde047";
            rect("STAFFAGGI", braidX - 1.3, sy, 2.6, segH, colStroke, colFill, true);
          }
          // Sezione terminale del cavo PE in vista (anima in rame + isolamento G/V)
          circle("STAFFAGGI", braidX, braidBotY - 2.2, 2.6, "#fde047", "#16a34a", true);
          circle("STAFFAGGI", braidX, braidBotY - 2.2, 1.4, "#fb923c", "#ea580c", true);
        } else {
          // Treccia piatta flessibile in rame con trama incrociata a losanghe
          rect("STAFFAGGI", braidX - 1.8, braidBotY, 3.6, braidTopY - braidBotY, "#fb923c", "#9a3412", true);
          for (let ty = braidBotY + 1; ty < braidTopY - 1.5; ty += 2.2) {
            line("STAFFAGGI", braidX - 1.6, ty, braidX + 1.6, ty + 1.8, "#fdba74", 0.35);
            line("STAFFAGGI", braidX + 1.6, ty, braidX - 1.6, ty + 1.8, "#fdba74", 0.35);
          }
        }

        // 4. Cartellino tecnico di richiamo del nodo equipotenziale PE
        const tagX = baseX - 11.5;
        const tagY = baseY - 11.0;
        line("Bandierine", baseX - 6.2, baseY, tagX + 2, tagY + 4.5, isGV ? "#4ade80" : "#fb923c", 0.35);
        line("Bandierine", tagX + 2, tagY + 4.5, tagX - 36, tagY + 4.5, isGV ? "#4ade80" : "#fb923c", 0.35);
        text("Testi", tagX - 2, tagY + 6.0, 1.95, `NODO PE M6 (${isGV ? `FS17 G/V ${secPE}mmq` : `Treccia Cu ${secPE}mmq`})`, isGV ? "#4ade80" : "#fb923c", "right");
        text("Testi", tagX - 2, tagY + 1.8, 1.65, `Continuità CEI 64-8 Art.543`, "#94a3b8", "right");
      }

      // ==========================================================================
      // PUNTO 2: DISEGNO DIFFERENZIATO CAVI UNIPOLARI (1X) VS MULTIPOLARI
      // ==========================================================================
      const leftC = [], rightC = [];
      state.cables.forEach((c, idx) => {
        const r = c.d / 2;
        const isUni = isUnipolarCable(c);
        const col = c.sys === "SPECIAL_ELV" ? "#38bdf8" : c.sys === "BT_FIRE" ? "#f87171" : isUni ? "#fbbf24" : "#f59e0b";
        const fill = c.sys === "SPECIAL_ELV" ? "#0e7490" : c.sys === "BT_FIRE" ? "#991b1b" : isUni ? "#451a03" : "#78350f";

        // Guaina esterna
        circle("Cavi", c.x, c.y, r, col, fill, true);
        // Isolamento interno (anello elastomerico HEPR/G16)
        circle("Cavi", c.x, c.y, r * 0.78, col, isUni ? "#1e293b" : null, isUni);

        if (isUni) {
          // STILIZZAZIONE CAVO UNIPOLARE (1X): Unica anima centrale in rame cordato con corona di trefoli!
          const rCu = r * 0.50;
          circle("Anime_Cavi", c.x, c.y, rCu, "#fdba74", "#ea580c", true);
          // Corona di 6 trefoli elementari attorno al trefolo centrale per rendere evidente la cordatura 1X
          if (r >= 4.0) {
            circle("Anime_Cavi", c.x, c.y, rCu * 0.34, "#fed7aa", null, false);
            for (let s = 0; s < 6; s++) {
              const ang = (s * Math.PI) / 3;
              circle("Anime_Cavi", c.x + Math.cos(ang) * rCu * 0.64, c.y + Math.sin(ang) * rCu * 0.64, rCu * 0.28, "#fed7aa", null, false);
            }
          }
          // Croce assiale sottile da vero simbolo unipolare CAD
          line("Anime_Cavi", c.x - r * 0.68, c.y, c.x + r * 0.68, c.y, "#fde047", 0.22, 0.65);
          line("Anime_Cavi", c.x, c.y - r * 0.68, c.x, c.y + r * 0.68, "#fde047", 0.22, 0.65);
        } else {
          // STILIZZAZIONE CAVO MULTIPOLARE (2x, 3G, 4G, 5G): Anime multiple distribuite a stella
          const formStr = String(c.form || "");
          const nCores = formStr.includes("5G") ? 5 : formStr.includes("4G") || formStr.includes("4x") ? 4 : formStr.includes("2x") ? 2 : 3;
          const ringR = nCores === 2 ? r * 0.36 : r * 0.42;
          const coreR = nCores === 5 ? r * 0.18 : r * 0.21;
          for (let k = 0; k < nCores; k++) {
            const a = (k * 2 * Math.PI) / nCores + Math.PI / 6;
            // Se c'è il conduttore G/V (formazione con 'G') coloriamo l'ultima anima in verde/giallo
            const isPECore = /G/i.test(c.form) && k === nCores - 1;
            circle(
              "Anime_Cavi",
              c.x + Math.cos(a) * ringR,
              c.y + Math.sin(a) * ringR,
              coreR,
              isPECore ? "#fde047" : "#fb923c",
              isPECore ? "#16a34a" : "#ea580c",
              true
            );
          }
        }

        // Numero identificativo posizione cavo: 2,5 mm (standard di tavola); ridotto solo se uscirebbe dal contorno del cavo.
        // Ingombro testo ~ (n.cifre x 0.6h) x 0.72h: il rettangolo deve stare nel cerchio di raggio r (margine 5%).
        const numStr = String(idx + 1);
        // Altezza reale fissa 2,5 mm: non segue il moltiplicatore "Grandezza testi" (compensato dividendo per txtMul).
        const fitH = (2 * r * 0.95) / Math.hypot(numStr.length * 0.6, 0.72);
        const numH = Math.min(2.5, fitH);
        text("Testi", c.x, c.y - numH * 0.28, numH / txtMul, numStr, "#ffffff", "center");
        if (c.x <= W / 2) leftC.push({ c, idx }); else rightC.push({ c, idx });
      });

      // ========================================================================
      // FASCETTE DI ANCORAGGIO CAVI (CEI EN 61537) SU LAYER DEDICATO "FASCETTE_ANCORAGGIO"
      // ========================================================================
      if (state.clamps && state.cables.length > 0) {
        // A. Fascette amagnetiche per terne a trifoglio
        const trfGroups = {};
        state.cables.forEach(c => {
          if (c.trefoil && c.trefoilGroupId) {
            if (!trfGroups[c.trefoilGroupId]) trfGroups[c.trefoilGroupId] = [];
            trfGroups[c.trefoilGroupId].push(c);
          }
        });
        Object.values(trfGroups).forEach(grp => {
          if (grp.length === 3) {
            const gcx = (grp[0].x + grp[1].x + grp[2].x) / 3;
            const gcy = (grp[0].y + grp[1].y + grp[2].y) / 3;
            const gr = grp[0].d * 1.08;
            circle("FASCETTE_ANCORAGGIO", gcx, gcy, gr, "#06b6d4", null, false);
            circle("FASCETTE_ANCORAGGIO", gcx, gcy, gr + 0.6, "#0891b2", null, false);
            rect("FASCETTE_ANCORAGGIO", gcx - 2.5, gcy + gr - 1.2, 5, 2.8, "#22d3ee", "#0e7490", true);
          }
        });

        // B. Fascetta di ancoraggio e serraggio a pettine / nastro per scomparti
        const sepXList = [0, ...getSeptumXList(), W];
        for (let sIdx = 0; sIdx < sepXList.length - 1; sIdx++) {
          const x0 = sepXList[sIdx], x1 = sepXList[sIdx + 1];
          const compCables = state.cables.filter(c => c.x >= x0 && c.x <= x1 && !c.trefoil);
          if (compCables.length > 0) {
            const minX = Math.max(x0 + 1.5, Math.min(...compCables.map(c => c.x - c.d / 2)) - 1.5);
            const maxX = Math.min(x1 - 1.5, Math.max(...compCables.map(c => c.x + c.d / 2)) + 1.5);
            const maxY = Math.max(...compCables.map(c => c.y + c.d / 2));

            // Clip di ancoraggio alla base della canalina
            rect("FASCETTE_ANCORAGGIO", minX - 2.5, -thk, 2.5, 4.0, "#06b6d4", "#0891b2", true);
            rect("FASCETTE_ANCORAGGIO", maxX, -thk, 2.5, 4.0, "#06b6d4", "#0891b2", true);

            // Nastro fascetta serrato sopra i cavi
            line("FASCETTE_ANCORAGGIO", minX, 0, minX + 1.5, maxY + 0.5, "#06b6d4", 0.5);
            line("FASCETTE_ANCORAGGIO", minX + 1.5, maxY + 0.5, maxX - 1.5, maxY + 0.5, "#06b6d4", 0.55);
            line("FASCETTE_ANCORAGGIO", maxX - 1.5, maxY + 0.5, maxX, 0, "#06b6d4", 0.5);
            rect("FASCETTE_ANCORAGGIO", (minX + maxX) / 2 - 2, maxY + 0.5, 4, 2.0, "#22d3ee", "#0e7490", true);

            // Bandierina descrittiva sulla prima fascetta
            if (sIdx === 0) {
              const bX = (minX + maxX) / 2;
              const bY = maxY + 1.5;
              line("Bandierine", bX, bY, bX - 10, bY + 12, "#06b6d4", 0.25);
              line("Bandierine", bX - 10, bY + 12, bX - 18, bY + 12, "#06b6d4", 0.25);
              text("Testi", bX - 20, bY + 11.4, 1.9, "Fascette ancoraggio amagnetiche (passo 600mm)", "#38bdf8", "right");
            }
          }
        }
      }

      const calloutBaseY = ceilY + 8, stepY = 7.5;
      let maxLW = 0, maxRW = 0;
      const showCallouts = state.showCableCallouts !== false;

      if (showCallouts) {
        maxLW = 75; maxRW = 75;
        leftC.forEach((it, i) => {
          const ty = calloutBaseY + i * stepY;
          const lbl = `#${it.idx + 1} ${it.c.code} (${it.c.form} Ø${it.c.d})`;
          maxLW = Math.max(maxLW, lbl.length * 1.55);
          line("Bandierine", it.c.x, it.c.y + it.c.d / 2, -16, ty, "#fbbf24", 0.22, 0.6);
          line("Bandierine", -16, ty, -24, ty, "#fbbf24", 0.22, 0.6);
          text("Testi", -26, ty - 0.6, 2.5, lbl, "#cbd5e1", "right");
        });
        rightC.forEach((it, i) => {
          const ty = calloutBaseY + i * stepY;
          const lbl = `#${it.idx + 1} ${it.c.code} (${it.c.form} Ø${it.c.d})`;
          maxRW = Math.max(maxRW, lbl.length * 1.55);
          line("Bandierine", it.c.x, it.c.y + it.c.d / 2, W + 16, ty, "#fbbf24", 0.22, 0.6);
          line("Bandierine", W + 16, ty, W + 24, ty, "#fbbf24", 0.22, 0.6);
          text("Testi", W + 26, ty - 0.6, 2.5, lbl, "#cbd5e1", "left");
        });
      }

      const qY = -thk - 22;
      line("QUOTE", 0, qY, W, qY, "#f59e0b", 0.45);
      text("QUOTE", W / 2, qY + 2.5, 2.5, `Base L = ${W} mm`, "#fbbf24", "center");
      line("QUOTE", W + 10, 0, W + 10, H, "#f59e0b", 0.45);
      text("QUOTE", W + 12, H / 2 - 1, 2.5, `H=${H}mm`, "#fbbf24", "left");

      const isMinimal = state.layoutMode === "minimal";
      const leftBoxX = -48, leftBoxW = Math.max(200, W + 92);

      if (!isMinimal && state.showPlanTag) {
        const ptY = lowestY - 38;
        rect("ETICHETTA_PIANTA", leftBoxX, ptY, leftBoxW, 24, "#4ade80", boxBg, true);
        text("ETICHETTA_PIANTA", leftBoxX + 5, ptY + 16, 2.5, `ETICHETTA PLANIMETRIA: ${state.title} (${state.planTag})`, "#4ade80", "left", leftBoxW - 10);
        text("ETICHETTA_PIANTA", leftBoxX + 5, ptY + 6, 2.5, `Riempimento: ${metrics.fillPct}% (Riserva +${metrics.sparePct}%) | Carico: ${metrics.totalKgM} kg/m`, "#f8fafc", "left", leftBoxW - 10);
        lowestY = ptY;
      }

      if (!isMinimal && state.showIso3D) {
        lowestY = build3DIsoScene(prims, leftBoxX, lowestY, leftBoxW, W, H, thk, mat, sup);
      }

      // ------------------------------------------------------------------------
      // TABELLE DELLA COLONNA DESTRA: larghezza massima 200 mm (20 cm sulla tavola 1:1),
      // colonne dimensionate sul contenuto, testi a capo invece che troncati.
      // Le larghezze sono misurate con le metriche Arial e la convenzione CAD
      // (altezza testo = altezza delle maiuscole), così valgono anche nei CAD.
      // ------------------------------------------------------------------------
      const TAB_MAX_W = 200;
      const T_BODY = 2.5, T_HEAD = 2.5, T_TITLE = 2.5, T_BOM = 2.5, T_TB = 2.5; // testi di distinta, computo e cartiglio: 2,5 mm (standard di tavola)
      const PAD_X = 1.4, PAD_Y = 1.8, LINE_K = 1.45;
      const effH = h => h * txtMul;
      const tw = (s, h) => cadTextWidth(s, effH(h));
      const wrap = (s, maxW, h) => wrapCadText(String(s == null ? "" : s), maxW, effH(h));
      const blockH = (nLines, h) => Math.max(6.5, 2 * PAD_Y + effH(h) + (nLines - 1) * effH(h) * LINE_K);
      // testo su più righe in una cella; rowTop = bordo superiore della cella
      const textLines = (layer, x, rowTop, h, lines, color) => {
        lines.forEach((ln, k) => {
          const y = rowTop - PAD_Y - effH(h) - k * effH(h) * LINE_K;
          prims.push({ t: "text", layer, x, y, h: +effH(h).toFixed(3), str: ln, color: isLight && color === "#f8fafc" ? txtCol : color, align: "left" });
        });
      };

      const showDesc = state.showTableDesc !== false && !state.hideTableDesc;
      const colDefs = [
        { key: "pos", header: "POS", col: "#cbd5e1", wrap: false },
        { key: "code", header: "SIGLA LINEA", col: "#38bdf8", wrap: true, prio: 2 },
        ...(showDesc ? [{ key: "desc", header: "DESCRIZIONE UTENZA", col: "#f8fafc", wrap: true, prio: 1, grow: true, minW: 30 }] : []),
        { key: "type", header: "TIPO CAVO", col: "#f8fafc", wrap: true, prio: 3 },
        { key: "form", header: "FORMAZIONE", col: "#f8fafc", wrap: true, prio: 4 },
        { key: "diam", header: "Ø mm", col: "#f8fafc", wrap: false },
        { key: "rmin", header: "R min", col: "#f8fafc", wrap: false },
        { key: "posa", header: "POSA", col: "#f8fafc", wrap: false }
      ];

      const rowsData = state.cables.map((c, idx) => {
        const isUni = isUnipolarCable(c);
        return {
          isUni,
          pos: `#${idx + 1}`,
          code: c.code,
          desc: cleanUtenzaDescription(c.desc, c.code) || c.desc || "-",
          type: getDisplayCableType(c, state.cables),
          form: c.form,
          diam: String(c.d),
          rmin: `${Math.round(c.d * (c.bendK || 10))}mm`,
          posa: isUni ? "1X UNI" : (c.sys === "SPECIAL_ELV" ? "ELV" : "MULTI")
        };
      });

      // Larghezze: naturale (tutto su una riga) e minima (parola più lunga)
      const longestWord = (s, h) => Math.max(0, ...String(s || "").split(/\s+/).map(w => tw(w, h)));
      colDefs.forEach(cd => {
        const vals = rowsData.map(r => r[cd.key]);
        const nat = Math.max(tw(cd.header, T_HEAD), ...vals.map(v => tw(v, T_BODY)));
        const minHead = longestWord(cd.header, T_HEAD);
        const minVal = cd.wrap ? Math.max(0, ...vals.map(v => longestWord(v, T_BODY))) : Math.max(0, ...vals.map(v => tw(v, T_BODY)));
        cd.nat = Math.ceil(nat + 2 * PAD_X);
        cd.min = Math.ceil(Math.max(minHead, minVal, cd.minW ? cd.minW - 2 * PAD_X : 0) + 2 * PAD_X);
        cd.w = cd.nat;
      });
      let tabSum = colDefs.reduce((a, c) => a + c.w, 0);
      if (tabSum > TAB_MAX_W) {
        let excess = tabSum - TAB_MAX_W;
        colDefs.filter(c => c.wrap).sort((a, b) => a.prio - b.prio).forEach(c => {
          if (excess <= 0) return;
          const r = Math.min(excess, c.w - c.min);
          if (r > 0) { c.w -= r; excess -= r; }
        });
      } else {
        const grow = colDefs.find(c => c.grow) || colDefs.find(c => c.key === "code");
        grow.w += TAB_MAX_W - tabSum;
      }
      colDefs.forEach(c => { c.w = +c.w.toFixed(1); });
      const colW = colDefs.map(c => c.w);
      const tabW = +colW.reduce((a, b) => a + b, 0).toFixed(1);

      const cellLines = (txt, cd, h) => cd.wrap || tw(txt, h) > cd.w - 2 * PAD_X ? wrap(txt, cd.w - 2 * PAD_X, h) : [String(txt)];
      const titleLines = wrap(`1. DISTINTA CAVI POSATI NELLA SEZIONE - ${state.title}`, tabW - 8, T_TITLE);
      const titleH = Math.max(10, blockH(titleLines.length, T_TITLE));
      const headCells = colDefs.map(cd => wrap(cd.header, cd.w - 2 * PAD_X, T_HEAD));
      const headerH = blockH(Math.max(...headCells.map(l => l.length)), T_HEAD);
      const bodyRows = rowsData.map(r => {
        const cells = colDefs.map(cd => cellLines(r[cd.key], cd, T_BODY));
        return { r, cells, h: blockH(Math.max(...cells.map(l => l.length)), T_BODY) };
      });
      const emptyLines = wrap("Canalina vuota (0 cavi posati) - Importa un DWG/DXF reale", tabW - 12, T_BODY);
      const bodyH = bodyRows.length ? bodyRows.reduce((a, b) => a + b.h, 0) : blockH(emptyLines.length, T_BODY);
      const tableInnerH = titleH + headerH + bodyH;

      let rightX, topY;
      if (isMinimal) {
        // LAYOUT MINIMAL: Tabella posizionata SOPRA la vista 2D, centrata sull'asse della canalina
        const xCenter = W / 2;
        rightX = xCenter - tabW / 2;
        const topOf2D = showCallouts && (leftC.length > 0 || rightC.length > 0)
          ? calloutBaseY + Math.max(leftC.length, rightC.length) * stepY
          : ceilY;
        const spacingAbove2D = 28;
        topY = topOf2D + spacingAbove2D + tableInnerH;
      } else {
        // LAYOUT STANDARD: Tabella sulla colonna destra
        rightX = Math.max(W + 35 + maxRW, leftBoxX + leftBoxW) + 24;
        topY = Math.max(H + 38, calloutBaseY + (showCallouts ? Math.max(leftC.length, rightC.length) * stepY : 0)) + titleH;
      }

      // Titolo
      rect("TABELLA", rightX, topY - titleH, tabW, titleH, "#38bdf8", boxBg, true);
      textLines("TABELLA", rightX + 4, topY, T_TITLE, titleLines, "#38bdf8");
      // Intestazioni
      let cursorY = topY - titleH;
      rect("TABELLA", rightX, cursorY - headerH, tabW, headerH, "#64748b", boxAlt, true);
      let cx = rightX;
      colDefs.forEach((cd, i) => {
        textLines("TABELLA", cx + PAD_X, cursorY, T_HEAD, headCells[i], "#cbd5e1");
        cx += cd.w;
      });
      cursorY -= headerH;
      // Righe
      if (!bodyRows.length) {
        const h = blockH(emptyLines.length, T_BODY);
        rect("TABELLA", rightX, cursorY - h, tabW, h, "#475569", boxBg, true);
        textLines("TABELLA", rightX + 6, cursorY, T_BODY, emptyLines, "#94a3b8");
        cursorY -= h;
      } else {
        bodyRows.forEach((row, idx) => {
          rect("TABELLA", rightX, cursorY - row.h, tabW, row.h, "#475569", idx % 2 === 0 ? boxBg : boxAlt, true);
          let rx = rightX;
          colDefs.forEach((cd, i) => {
            const color = cd.key === "code" ? "#38bdf8" : (cd.key === "posa" && row.r.isUni ? "#fbbf24" : cd.col);
            textLines("TABELLA", rx + PAD_X, cursorY, T_BODY, row.cells[i], color);
            rx += cd.w;
          });
          cursorY -= row.h;
        });
      }

      let currentRightY = cursorY;

      // 2. DIAGRAMMA CURVA DI CARICO IN TAVOLA CAD (Opzionale - escluso in Stampa Minimal)
      if (!isMinimal && state.showLoadChart !== false) {
        const chartH = 54, chartY = currentRightY - chartH - 22;
        currentRightY = chartY - 6;
        rect("CURVA_CARICO", rightX, chartY, tabW, chartH + 16, "#38bdf8", boxBg, true);
        line("CURVA_CARICO", rightX, chartY + chartH + 1, rightX + tabW, chartY + chartH + 1, "#334155", 0.4);
        if (state.support === "none") {
          text("CURVA_CARICO", rightX + 4, chartY + chartH + 9.5, 2.5, `2. DIAGRAMMA CURVA DI CARICO E FRECCIA - RIF. CEI EN 61537 (SWL)`, "#38bdf8", "left", tabW - 8);
          text("CURVA_CARICO", rightX + 4, chartY + chartH + 3.8, 2.0, `Configurazione: Nessun supporto previsto (No Staffaggio)`, "#cbd5e1", "left", tabW - 8);
          text("CURVA_CARICO", rightX + tabW / 2, chartY + chartH / 2 + 2, 2.6, `STAFFAGGIO NON PREVISTO (NO STAFFAGGIO)`, "#64748b", "center");
          text("CURVA_CARICO", rightX + tabW / 2, chartY + chartH / 2 - 5, 1.85, `Canalizzazione autoportante / staffata a cura dell'installatore`, "#475569", "center");
        } else {
        text("CURVA_CARICO", rightX + 4, chartY + chartH + 9.5, 2.5, `2. DIAGRAMMA CURVA DI CARICO E FRECCIA - RIF. CEI EN 61537 (SWL)`, "#38bdf8", "left", tabW - 8);
        text("CURVA_CARICO", rightX + 4, chartY + chartH + 3.8, 2.0, `Supporto: ${sup.name} | Passo Ls = ${state.spanM.toFixed(2)} m | Freccia f = ${metrics.deflectionMm} mm (Lim. ${metrics.maxDeflectionMm} mm)`, "#cbd5e1", "left", tabW - 8);

        const gx0 = rightX + 26, gy0 = chartY + 11, gw = tabW - 42, gh = chartH - 14;
        line("CURVA_CARICO", gx0, gy0, gx0 + gw, gy0, "#94a3b8", 0.55);
        line("CURVA_CARICO", gx0, gy0, gx0, gy0 + gh, "#94a3b8", 0.55);

        const qAt1m = calcAllowedLinearLoadKgM(state.support, 1.0);
        const maxScaleKgM = Math.max(25, Math.ceil(Math.max(qAt1m, metrics.totalKgM * 1.3) / 10) * 10);
        for (let frac = 0; frac <= 1.0; frac += 0.25) {
          const gy = gy0 + frac * gh;
          line("CURVA_CARICO", gx0, gy, gx0 + gw, gy, "#334155", 0.28);
          text("CURVA_CARICO", gx0 - 2.5, gy - 0.8, 1.85, `${Math.round(frac * maxScaleKgM)}`, "#94a3b8", "right");
        }
        text("CURVA_CARICO", gx0 - 3, gy0 + gh + 1.8, 1.85, "kg/m", "#38bdf8", "right");

        const spans = [1.0, 1.1, 1.25, 1.4, 1.5, 1.65, 1.75, 1.9, 2.0, 2.25, 2.5, 2.75, 3.0];
        let prevPt = null;
        spans.forEach(s => {
          const gx = gx0 + ((s - 1.0) / 2.0) * gw;
          if (Math.abs(s * 2 - Math.round(s * 2)) < 0.01) {
            line("CURVA_CARICO", gx, gy0 - 1.5, gx, gy0 + gh, "#334155", 0.28);
            text("CURVA_CARICO", gx, gy0 - 4.8, 1.85, `Ls=${s.toFixed(1)}m`, "#94a3b8", "center");
          }
          const qVal = Math.min(maxScaleKgM, calcAllowedLinearLoadKgM(state.support, s));
          const gy = gy0 + (qVal / maxScaleKgM) * gh;
          if (prevPt) line("CURVA_CARICO", prevPt.x, prevPt.y, gx, gy, "#22c55e", 0.85);
          prevPt = { x: gx, y: gy };
        });

        const wpX = gx0 + ((state.spanM - 1.0) / 2.0) * gw;
        const wpY = gy0 + Math.min(1.0, metrics.totalKgM / maxScaleKgM) * gh;
        const limY = gy0 + Math.min(1.0, metrics.qMaxKgM / maxScaleKgM) * gh;

        line("CURVA_CARICO", wpX, gy0, wpX, limY, "#f59e0b", 0.5, 0.85);
        line("CURVA_CARICO", gx0, wpY, wpX, wpY, "#f59e0b", 0.5, 0.85);
        circle("CURVA_CARICO", wpX, limY, 1.6, "#22c55e", "#22c55e", true);
        circle("CURVA_CARICO", wpX, wpY, 2.4, metrics.totalKgM <= metrics.qMaxKgM ? "#f59e0b" : "#ef4444", metrics.totalKgM <= metrics.qMaxKgM ? "#fbbf24" : "#ef4444", true);

        const wpAlign = wpX > gx0 + gw * 0.55 ? "right" : "left";
        const wpTextX = wpAlign === "right" ? wpX - 4 : wpX + 4;
        text("CURVA_CARICO", wpTextX, wpY + 1.5, 2.0, `Lavoro: ${metrics.totalKgM} kg/m @ ${state.spanM}m (Max ${metrics.qMaxKgM} kg/m)`, "#fbbf24", wpAlign);
        }
      }

      // 3. COMPUTO METRICO BOM (Opzionale - escluso in Stampa Minimal)
      if (!isMinimal && state.showBOM !== false) {
        const bomTop = currentRightY - 2;
        const bTitle = wrap(`3. COMPUTO METRICO E DISTINTA MATERIALI (BOM - Tratto ${state.lengthM}m)`, tabW - 8, 2.5);
        const bTitleH = Math.max(9, blockH(bTitle.length, 2.5));
        rect("TABELLA", rightX, bomTop - bTitleH, tabW, bTitleH, "#4ade80", boxBg, true);
        textLines("TABELLA", rightX + 4, bomTop, 2.5, bTitle, "#4ade80");
        let by = bomTop - bTitleH;
        currentRightY = by;
        const qtyW = 64, descW = tabW - qtyW;
        metrics.bom.forEach((b, i) => {
          const l1 = wrap(`${b.code} | ${b.desc}`, descW - 6, T_BOM);
          const l2 = wrap(`Q.ta: ${b.qty} ${b.um} (${b.note})`, qtyW - 4, T_BOM);
          const h = Math.max(7, blockH(Math.max(l1.length, l2.length), T_BOM));
          rect("TABELLA", rightX, by - h, tabW, h, "#334155", i % 2 === 0 ? boxBg : boxAlt, true);
          textLines("TABELLA", rightX + 3, by, T_BOM, l1, "#f8fafc");
          textLines("TABELLA", rightX + descW + 1, by, T_BOM, l2, "#4ade80");
          by -= h;
          currentRightY = by;
        });
      }

      // 4. CARTIGLIO PROGETTO E COMMITTENTE (Opzionale - escluso in Stampa Minimal)
      if (!isMinimal && state.showCartiglio !== false) {
        const c1 = wrap(`COMMITTENTE: ${state.tb.client} | CODICE: ${state.tb.docCode}`, tabW - 8, T_TB);
        const c2 = wrap(`PROGETTISTA: ${state.tb.author} | SCALA: ${state.scale || "1:1"} (${state.tb.scaleRev})`, tabW - 8, T_TB);
        const h1 = blockH(c1.length, T_TB), h2 = blockH(c2.length, T_TB);
        const tbH = Math.max(20, h1 + h2 - PAD_Y);
        const tbY = currentRightY - 4 - tbH;
        currentRightY = tbY;
        rect("CORNICE", rightX, tbY, tabW, tbH, "#38bdf8", boxBg, true);
        textLines("CORNICE", rightX + 4, tbY + tbH, T_TB, c1, "#f8fafc");
        textLines("CORNICE", rightX + 4, tbY + tbH - h1 + PAD_Y, T_TB, c2, "#fbbf24");
      }

      let minX, maxX, minY, maxY;
      if (isMinimal) {
        minX = Math.min(-24, rightX - 12);
        maxX = Math.max(W + 24, rightX + tabW + 12);
        minY = Math.min(-thk - 28, lowestY - 10);
        maxY = topY + 18;
      } else {
        minX = Math.min(-48 - maxLW - 10, leftBoxX - 10);
        maxX = rightX + tabW + 12;
        minY = Math.min(lowestY - 12, currentRightY - 10);
        maxY = topY + 12; // topY = bordo superiore del titolo della distinta
      }
      rect("CORNICE", minX, minY, maxX - minX, maxY - minY, "#475569", null, false);
      text("CORNICE", minX + 6, maxY - 8, 3.5, `PARTICOLARE COSTRUTTIVO ESECUTIVO: ${state.title} (SCALA ${state.scale || "1:1"})`, "#f8fafc", "left", (maxX - minX) * 0.65);

      return { prims, bounds: { minX, maxX, minY, maxY }, rightBounds: { minX: rightX - 8, maxX, minY: currentRightY - 8, maxY: topY + (isMinimal ? 12 : 2) } };
    }

// ---- 09-database-ui.js
    // ============================================================================
    // GESTIONE RESPONSIVE MOBILE VIEW SWITCHER & MODALE DATABASE TECNICO
    // ============================================================================
    let currentMobileView = "controls"; // "controls" | "cad" | "results"
    if (typeof window !== "undefined") window.currentMobileView = currentMobileView;
    let currentDbCategory = "all";

    function setMobileView(view) {
      currentMobileView = view;
      if (typeof window !== "undefined") window.currentMobileView = view;
      const isMobile = window.innerWidth < 1024;
      const leftCol = document.getElementById("leftColSection");
      const cadView = document.getElementById("cadViewportContainer");
      const distView = document.getElementById("distintaContainer");
      const kpiBar = document.getElementById("kpiBar");

      const btnControls = document.getElementById("mobTabBtn_controls");
      const btnCad = document.getElementById("mobTabBtn_cad");
      const btnResults = document.getElementById("mobTabBtn_results");

      if (btnControls) btnControls.className = view === "controls" ? "flex-1 py-2 text-center rounded-lg bg-amber-500/20 text-amber-300 font-bold transition" : "flex-1 py-2 text-center rounded-lg text-slate-400 hover:text-white transition";
      if (btnCad) btnCad.className = view === "cad" ? "flex-1 py-2 text-center rounded-lg bg-cyan-500/20 text-cyan-300 font-bold transition" : "flex-1 py-2 text-center rounded-lg text-slate-400 hover:text-white transition";
      if (btnResults) btnResults.className = view === "results" ? "flex-1 py-2 text-center rounded-lg bg-emerald-500/20 text-emerald-300 font-bold transition" : "flex-1 py-2 text-center rounded-lg text-slate-400 hover:text-white transition";

      if (isMobile) {
        if (leftCol) leftCol.classList.toggle("hidden", view !== "controls");
        if (cadView) cadView.classList.toggle("hidden", view !== "cad");
        if (distView) distView.classList.toggle("hidden", view !== "results");
        if (kpiBar) kpiBar.classList.toggle("hidden", view === "controls");
        if (view === "cad") {
          setTimeout(() => { fitAll(); drawScene(); }, 60);
        }
      } else {
        if (leftCol) leftCol.classList.remove("hidden");
        if (cadView) cadView.classList.remove("hidden");
        if (distView) distView.classList.remove("hidden");
        if (kpiBar) kpiBar.classList.remove("hidden");
      }
    }

    function toggleDatabaseModal(forceOpen) {
      const modal = document.getElementById("databaseCatalogModal");
      if (!modal) return;
      if (forceOpen === true) {
        modal.classList.remove("hidden");
        renderDatabaseItems();
        return;
      }
      const isHidden = modal.classList.contains("hidden");
      if (isHidden) {
        modal.classList.remove("hidden");
        renderDatabaseItems();
      } else {
        modal.classList.add("hidden");
      }
    }

    function filterDbCategory(cat) {
      currentDbCategory = cat;
      const categories = ['all', 'bt_uni', 'bt_multi', 'signal', 'fire', 'data', 'trays_supports'];
      categories.forEach(c => {
        const btn = document.getElementById(`dbCatBtn_${c}`);
        if (!btn) return;
        if (c === cat) {
          btn.className = "px-2.5 py-1 rounded-lg bg-cyan-500/20 text-cyan-300 border border-cyan-500/40 font-bold transition";
        } else {
          btn.className = "px-2.5 py-1 rounded-lg text-slate-300 hover:bg-slate-800 transition";
        }
      });
      renderDatabaseItems();
    }

    function renderDatabaseItems() {
      const searchTxt = (document.getElementById("dbSearchInp").value || "").trim().toLowerCase();
      const cablesTable = document.getElementById("dbCablesTableView");
      const supportsTable = document.getElementById("dbSupportsTableView");
      const tbody = document.getElementById("dbCablesTableBody");

      if (currentDbCategory === "trays_supports") {
        cablesTable.classList.add("hidden");
        supportsTable.classList.remove("hidden");
        renderDbSupportsAndTrays(searchTxt);
        return;
      }

      cablesTable.classList.remove("hidden");
      supportsTable.classList.add("hidden");
      tbody.innerHTML = "";

      let items = CABLE_CATALOG;
      if (currentDbCategory !== "all") {
        items = items.filter(c => c.cat === currentDbCategory);
      }
      if (searchTxt) {
        items = items.filter(c => 
          c.type.toLowerCase().includes(searchTxt) ||
          c.form.toLowerCase().includes(searchTxt) ||
          c.desc.toLowerCase().includes(searchTxt) ||
          (c.std && c.std.toLowerCase().includes(searchTxt))
        );
      }

      const dbCnt = document.getElementById("dbItemsCount");
      if (dbCnt) dbCnt.textContent = items.length;

      items.forEach(c => {
        const isUni = isUnipolarCable(c);
        const tr = document.createElement("tr");
        tr.className = "hover:bg-slate-800/60 transition";
        const catBadge = c.cat === "bt_uni" ? '<span class="px-1.5 py-0.5 rounded bg-amber-500/20 text-amber-300 text-[11px] font-bold">1X UNI</span>'
          : c.cat === "bt_multi" ? '<span class="px-1.5 py-0.5 rounded bg-blue-500/20 text-blue-300 text-[11px] font-bold">MULTI</span>'
          : c.cat === "signal" ? '<span class="px-1.5 py-0.5 rounded bg-purple-500/20 text-purple-300 text-[11px] font-bold">SEGN</span>'
          : c.cat === "fire" ? '<span class="px-1.5 py-0.5 rounded bg-red-500/20 text-red-300 text-[11px] font-bold">FIRE</span>'
          : '<span class="px-1.5 py-0.5 rounded bg-cyan-500/20 text-cyan-300 text-[11px] font-bold">DATI</span>';

        tr.innerHTML = `
          <td class="py-2 px-2">${catBadge}</td>
          <td class="py-2 px-2 font-bold text-white">${c.type}</td>
          <td class="py-2 px-2 text-emerald-300 font-bold">${c.form}</td>
          <td class="py-2 px-2 text-center text-cyan-300 font-bold">${c.d}</td>
          <td class="py-2 px-2 text-center text-slate-300">${c.w.toFixed(2)}</td>
          <td class="py-2 px-2 text-center text-amber-300">${c.sec}</td>
          <td class="py-2 px-2 text-center text-emerald-400 font-bold">${c.iz0} A</td>
          <td class="py-2 px-2 text-center text-purple-300">${Math.round(c.d * c.bendK)}mm</td>
          <td class="py-2 px-2 text-slate-300 font-sans max-w-xs">
            <div>${c.desc}</div>
            <div class="text-[11px] text-slate-400">${c.std || ""}</div>
          </td>
          <td class="py-2 px-2 text-right">
            <div class="flex items-center justify-end gap-1.5">
              <input type="number" min="1" max="24" value="${isUni ? 3 : 1}" id="dbQty_${c.id}" class="w-10 bg-slate-950 border border-slate-700 rounded px-1 py-0.5 text-center text-white text-[11px] mono">
              ${isUni ? `<label class="flex items-center gap-1 text-[11px] text-amber-300"><input type="checkbox" checked id="dbTrf_${c.id}" class="accent-amber-400">🔺3F</label>` : ''}
              <button type="button" onclick="addCableFromDbRow('${c.id}')" class="px-2 py-1 bg-cyan-500/20 hover:bg-cyan-500/30 text-cyan-300 border border-cyan-500/40 rounded font-semibold transition text-[11px]">
                + Inserisci
              </button>
            </div>
          </td>`;
        tbody.appendChild(tr);
      });
    }

    function renderDbSupportsAndTrays(searchTxt) {
      const brandContainer = document.getElementById("dbBrandSeriesCards");
      const supContainer = document.getElementById("dbSupportsCards");
      brandContainer.innerHTML = "";
      supContainer.innerHTML = "";

      Object.entries(BRAND_SERIES).forEach(([brand, seriesList]) => {
        seriesList.forEach(ser => {
          if (searchTxt && !ser.name.toLowerCase().includes(searchTxt)) return;
          const card = document.createElement("div");
          card.className = "bg-slate-950 border border-slate-800 rounded-xl p-2.5 flex flex-col justify-between";
          card.innerHTML = `
            <div>
              <div class="flex items-center justify-end mb-1">
                <span class="text-[11px] text-slate-400 font-mono">${ser.code}</span>
              </div>
              <h5 class="text-xs font-bold text-white mb-1">${ser.name}</h5>
            </div>
            <button type="button" onclick="applyBrandSeriesFromDb('${brand}', '${ser.id}')" class="mt-2.5 w-full py-1 bg-amber-500/20 hover:bg-amber-500/30 text-amber-300 border border-amber-500/40 rounded font-semibold text-[11px] transition">
              ✓ Applica a Progetto
            </button>`;
          brandContainer.appendChild(card);
        });
      });

      Object.entries(SUPPORT_SPECS).forEach(([key, spec]) => {
        if (searchTxt && !spec.name.toLowerCase().includes(searchTxt) && !spec.desc.toLowerCase().includes(searchTxt)) return;
        const card = document.createElement("div");
        card.className = "bg-slate-950 border border-slate-800 rounded-xl p-2.5 flex flex-col justify-between";
        const badgeType = spec.type === "none" ? "NO STAFFAGGIO" : spec.type === "wall" ? "PARETE" : spec.type === "ceiling" ? "SOFFITTO" : "PAVIMENTO";
        const badgeSwl = spec.type === "none" ? "SWL: N/D" : `SWL: ${spec.swl15m} kg @1.5m`;
        card.innerHTML = `
          <div>
            <div class="flex items-center justify-between mb-1">
              <span class="text-[11px] mono font-bold px-1.5 py-0.5 rounded ${spec.type === 'none' ? 'bg-slate-800 text-slate-300' : 'bg-cyan-500/20 text-cyan-300'}">${badgeType}</span>
              <span class="text-[11px] text-emerald-400 font-bold mono">${badgeSwl}</span>
            </div>
            <h5 class="text-xs font-bold text-white mb-1">${spec.name}</h5>
            <p class="text-[11px] text-slate-400 mb-2 leading-relaxed">${spec.desc}</p>
            ${spec.type !== 'none' ? `
            <div class="grid grid-cols-4 gap-1 text-[11px] mono text-center bg-slate-900/80 p-1 rounded border border-slate-800 mb-2">
              <div><span class="text-slate-500">1.0m:</span> <span class="text-amber-300">${spec.swl10m}kg</span></div>
              <div><span class="text-slate-500">1.5m:</span> <span class="text-emerald-400 font-bold">${spec.swl15m}kg</span></div>
              <div><span class="text-slate-500">2.0m:</span> <span class="text-cyan-300">${spec.swl20m}kg</span></div>
              <div><span class="text-slate-500">2.5m:</span> <span class="text-purple-300">${spec.swl25m}kg</span></div>
            </div>` : ''}
          </div>
          <button type="button" onclick="applySupportFromDb('${key}')" class="w-full py-1 ${key === 'none' ? 'bg-slate-800 hover:bg-slate-700 text-slate-300' : 'bg-cyan-500/20 hover:bg-cyan-500/30 text-cyan-300 border border-cyan-500/40'} rounded font-semibold text-[11px] transition">
            ✓ Seleziona Supporto
          </button>`;
        supContainer.appendChild(card);
      });
    }

    function addCableFromDbRow(id) {
      const item = CABLE_CATALOG.find(c => c.id === id);
      if (!item) return;
      const qtyInp = document.getElementById(`dbQty_${id}`);
      const qty = Math.max(1, Math.min(30, parseInt(qtyInp ? qtyInp.value : 1, 10) || 1));
      const trfInp = document.getElementById(`dbTrf_${id}`);
      const trefoil = trfInp ? trfInp.checked : false;

      const isUni = isUnipolarCable(item);
      const effectiveQty = (trefoil && isUni && qty === 1) ? 3 : qty;
      const batchId = Date.now();

      for (let q = 0; q < effectiveQty; q++) {
        const idx = state.cables.length + 1;
        const phaseLabels = ["L1", "L2", "L3", "N", "PE"];
        const suffix = trefoil ? phaseLabels[q % 3] : (effectiveQty > 1 ? phaseLabels[q % 5] || `${q + 1}` : "");
        const code = suffix ? `${item.code} - ${suffix}` : item.code;
        const trefoilGroupId = trefoil ? `TRF_${batchId}_${Math.floor(q / 3)}` : null;

        state.cables.push({
          id: idx,
          code,
          d: item.d,
          w: item.w,
          sec: item.sec,
          cores: item.cores,
          iz0: item.iz0,
          ib: 0,
          bendK: item.bendK,
          trefoil,
          trefoilGroupId,
          origX: 20 + ((idx - 1) % 6) * 16,
          origY: item.d / 2 + Math.floor((idx - 1) / 6) * 16,
          x: 20,
          y: item.d / 2,
          desc: item.desc,
          type: item.type,
          form: item.form,
          sys: item.sys
        });
      }

      recalculateAll();
      drawScene();
      const banner = document.getElementById("statusBanner");
      if (banner) {
        banner.textContent = `✓ Inserito a progetto: ${effectiveQty}x ${item.type} ${item.form} (${item.desc})`;
        banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-emerald-500/20 text-emerald-300 border border-emerald-500/40 font-bold";
      }
    }

    // Applica la tipologia della scheda (la distinta segue la tipologia).
    function applyBrandSeriesFromDb(brand, seriesId) {
      const ser = BRAND_SERIES.TIPOLOGIE.find(s => s.id === seriesId);
      const sel = document.getElementById("inpTrayType");
      if (ser && sel) sel.value = ser.type;
      syncFromUI();
      toggleDatabaseModal();
    }

    function applySupportFromDb(supKey) {
      document.getElementById("inpSupport").value = supKey;
      state.support = supKey;
      syncFromUI();
      toggleDatabaseModal();
    }

    const dropArea = document.getElementById("cadDropArea");
    dropArea.addEventListener("dragover", e => { e.preventDefault(); dropArea.classList.add("border-cyan-400"); });
    dropArea.addEventListener("dragleave", () => dropArea.classList.remove("border-cyan-400"));
    // I file si possono rilasciare in qualunque punto della pagina: senza questo il browser
    // aprirebbe o scaricherebbe il file al posto dell'app.
    const hasFiles = e => e.dataTransfer && Array.from(e.dataTransfer.types || []).includes("Files");
    window.addEventListener("dragover", e => { if (hasFiles(e)) { e.preventDefault(); e.dataTransfer.dropEffect = "copy"; } });
    window.addEventListener("drop", e => {
      if (!hasFiles(e)) return;
      e.preventDefault();
      dropArea.classList.remove("border-cyan-400");
      routeImportedFiles(e.dataTransfer.files);
    });

// ---- 10-sections.js
    // ============================================================================
    // 8. PROGETTO MULTI-SEZIONE: SCHEDE, SALVATAGGIO AUTOMATICO, ANNULLA/RIPETI
    // ============================================================================
    let projectSections = [];
    let currentSectionIdx = 0;

    const AUTOSAVE_KEY = "canalo_v16_multisections";
    const HISTORY_LIMIT = 60;
    // Stato "pulito" di riferimento: completa i campi mancanti di progetti salvati con versioni precedenti
    const DEFAULT_SECTION_STATE = JSON.parse(JSON.stringify(state));

    function cloneStateSnapshot(st) {
      return JSON.parse(JSON.stringify(st));
    }

    function normalizeSection(sec) {
      const base = cloneStateSnapshot(DEFAULT_SECTION_STATE);
      const s = (sec && typeof sec === "object") ? sec : {};
      const out = Object.assign(base, s);
      out.tb = Object.assign({}, DEFAULT_SECTION_STATE.tb, s.tb || {});
      out.cables = Array.isArray(s.cables) ? s.cables.filter(c => c && typeof c === "object" && +c.d > 0) : [];
      out.septumPositions = Array.isArray(s.septumPositions) ? s.septumPositions : [];
      // Progetti con le vecchie serie dei costruttori: la serie diventa quella della tipologia.
      out.brand = "TIPOLOGIE";
      out.seriesId = BOM_SERIES_FOR(out.trayType).id;
      ["width", "height", "thk", "lengthM", "spanM"].forEach(k => {
        out[k] = (+out[k] > 0) ? +out[k] : DEFAULT_SECTION_STATE[k];
      });
      return out;
    }

    // --- Cronologia Annulla / Ripeti -------------------------------------------
    // Ogni voce è lo stato serializzato di tutte le schede; le modifiche ravvicinate
    // (es. un trascinamento) confluiscono in un'unica voce grazie alla sincronizzazione differita.
    let historyStack = [];
    let historyIdx = -1;

    function serializeProject() {
      return JSON.stringify(projectSections);
    }

    function recordHistory(json) {
      if (historyIdx >= 0 && historyStack[historyIdx].json === json) {
        historyStack[historyIdx].idx = currentSectionIdx;
        return;
      }
      historyStack = historyStack.slice(0, historyIdx + 1);
      historyStack.push({ json, idx: currentSectionIdx });
      if (historyStack.length > HISTORY_LIMIT) historyStack.shift();
      historyIdx = historyStack.length - 1;
      updateUndoButtons();
    }

    function updateUndoButtons() {
      document.querySelectorAll("[data-undo]").forEach(b => { b.disabled = historyIdx <= 0; });
      document.querySelectorAll("[data-redo]").forEach(b => { b.disabled = historyIdx >= historyStack.length - 1; });
    }

    function applyHistoryEntry(entry) {
      projectSections = JSON.parse(entry.json).map(normalizeSection);
      currentSectionIdx = Math.min(Math.max(0, entry.idx || 0), projectSections.length - 1);
      state = cloneStateSnapshot(projectSections[currentSectionIdx]);
      pushStateToUI(); // include recalculateAll()
      // Allinea subito archivio e voce di cronologia allo stato ricalcolato, senza creare nuove voci
      scheduleSectionSync.cancel();
      projectSections[currentSectionIdx] = cloneStateSnapshot(state);
      historyStack[historyIdx] = { json: serializeProject(), idx: currentSectionIdx };
      renderMultiSectionTabs();
      persistProject();
      updateUndoButtons();
    }

    function undoLastChange() {
      if (scheduleSectionSync.pending()) scheduleSectionSync.flush();
      if (historyIdx <= 0) { toast("Niente da annullare", "info", null, 2000); return; }
      historyIdx--;
      applyHistoryEntry(historyStack[historyIdx]);
      toast("Modifica annullata", "info", { label: "Ripeti", onClick: redoLastChange }, 3000);
    }

    function redoLastChange() {
      if (scheduleSectionSync.pending()) scheduleSectionSync.flush();
      if (historyIdx >= historyStack.length - 1) { toast("Niente da ripetere", "info", null, 2000); return; }
      historyIdx++;
      applyHistoryEntry(historyStack[historyIdx]);
      toast("Modifica ripetuta", "info", null, 2000);
    }

    // --- Salvataggio automatico ------------------------------------------------
    let autosaveWarned = false;
    function persistProject() {
      const ok = safeStorage.set(AUTOSAVE_KEY, JSON.stringify({ version: APP_VERSION, savedAt: new Date().toISOString(), currentSectionIdx, projectSections }));
      if (!ok && !autosaveWarned) {
        autosaveWarned = true;
        toast("Salvataggio automatico non disponibile in questo browser: salva il progetto come file (Progetto → Salva file).", "warn", null, 9000);
      }
      setSaveState(ok);
    }

    // Ripristina il progetto dell'ultima sessione. Restituisce true se ha caricato qualcosa.
    function restoreAutosave() {
      const raw = safeStorage.get(AUTOSAVE_KEY);
      if (!raw) return false;
      try {
        const data = JSON.parse(raw);
        if (!Array.isArray(data.projectSections) || data.projectSections.length === 0) return false;
        projectSections = data.projectSections.map(normalizeSection);
        currentSectionIdx = Math.min(Math.max(0, data.currentSectionIdx || 0), projectSections.length - 1);
        state = cloneStateSnapshot(projectSections[currentSectionIdx]);
        return true;
      } catch (e) {
        console.warn("Salvataggio automatico illeggibile, ignorato:", e);
        return false;
      }
    }

    // --- Sincronizzazione scheda attiva ----------------------------------------
    // Immediata: copia lo stato nella scheda, aggiorna le schede, salva e registra la cronologia.
    function syncActiveSectionToMultiStore() {
      scheduleSectionSync.cancel();
      if (projectSections.length === 0) {
        projectSections.push(cloneStateSnapshot(state));
        currentSectionIdx = 0;
      } else {
        projectSections[currentSectionIdx] = cloneStateSnapshot(state);
      }
      renderMultiSectionTabs();
      persistProject();
      recordHistory(serializeProject());
    }
    // Differita: usata nei percorsi frequenti (ricalcolo, trascinamenti, digitazione)
    const scheduleSectionSync = debounce(() => syncActiveSectionToMultiStore(), 350);

    // Nuovo progetto: una sola scheda vuota (annullabile)
    function newProject() {
      if (scheduleSectionSync.pending()) scheduleSectionSync.flush();
      const hadContent = projectSections.length > 1 || state.cables.length > 0;
      const keepTb = state.tb;
      state = cloneStateSnapshot(DEFAULT_SECTION_STATE);
      state.tb = cloneStateSnapshot(keepTb); // i dati cartiglio restano quelli dello studio
      projectSections = [cloneStateSnapshot(state)];
      currentSectionIdx = 0;
      resetProject();
      syncActiveSectionToMultiStore();
      if (hadContent) toast("Nuovo progetto creato", "info", { label: "Annulla", onClick: undoLastChange }, 7000);
    }

    function deleteSectionTabAt(idx, event) {
      if (event) event.stopPropagation();
      if (projectSections.length <= 1) {
        clearCables();
        return;
      }
      if (scheduleSectionSync.pending()) scheduleSectionSync.flush();
      projectSections[currentSectionIdx] = cloneStateSnapshot(state);
      const removed = projectSections[idx];
      projectSections.splice(idx, 1);
      if (currentSectionIdx >= projectSections.length) {
        currentSectionIdx = projectSections.length - 1;
      } else if (currentSectionIdx > idx) {
        currentSectionIdx--;
      }
      state = cloneStateSnapshot(projectSections[currentSectionIdx]);
      pushStateToUI();
      fitAll();
      syncActiveSectionToMultiStore();
      toast(`Scheda "${(removed && removed.title) || idx + 1}" chiusa`, "info", { label: "Annulla", onClick: undoLastChange }, 7000);
    }

    function scrollTabsLeft() {
      const box = document.getElementById("multiSectionTabs");
      if (box) box.scrollBy({ left: -240, behavior: "smooth" });
    }

    function scrollTabsRight() {
      const box = document.getElementById("multiSectionTabs");
      if (box) box.scrollBy({ left: 240, behavior: "smooth" });
    }

    function toggleTabsDropdown(forceState) {
      const menu = document.getElementById("tabsDropdownMenu");
      if (!menu) return;
      const isHidden = menu.classList.contains("hidden");
      const shouldOpen = (forceState !== undefined) ? forceState : isHidden;
      if (shouldOpen) {
        closeAllMenus();
        menu.classList.remove("hidden");
        const activeItem = menu.querySelector(".tab-dropdown-active");
        if (activeItem) {
          setTimeout(() => { if (activeItem && typeof activeItem.scrollIntoView === 'function') activeItem.scrollIntoView({ behavior: 'smooth', block: 'nearest' }); }, 30);
        }
      } else {
        menu.classList.add("hidden");
      }
    }

    function initMultiSectionTabs() {
      const box = document.getElementById("multiSectionTabs");
      if (box && !box._wheelBound) {
        box._wheelBound = true;
        box.addEventListener("wheel", (e) => {
          if (e.deltaY !== 0) {
            e.preventDefault();
            box.scrollLeft += e.deltaY;
          }
        }, { passive: false });
      }
    }

    function renderMultiSectionTabs() {
      const box = document.getElementById("multiSectionTabs");
      if (!box) return;
      box.innerHTML = "";

      const lblTabsCount = document.getElementById("lblTabsCount");
      if (lblTabsCount) lblTabsCount.textContent = `Schede (${projectSections.length})`;

      const tabsDropdownCountBadge = document.getElementById("tabsDropdownCountBadge");
      if (tabsDropdownCountBadge) {
        tabsDropdownCountBadge.textContent = `${projectSections.length} ${projectSections.length === 1 ? 'scheda' : 'schede'}`;
      }

      const dropList = document.getElementById("tabsDropdownList");
      if (dropList) dropList.innerHTML = "";

      let activeTabWrapper = null;

      projectSections.forEach((sec, idx) => {
        const active = idx === currentSectionIdx;

        // Estrazione titolo pulito tra parentesi (es. Anello BT - Lato SX)
        const mParen = String(sec.title || "").match(/\(([^)]+)\)/);
        const displayName = mParen ? mParen[1].trim() : (sec.title || `Canalina ${idx + 1}`);
        const cableCount = Array.isArray(sec.cables) ? sec.cables.length : 0;
        const dims = `${sec.width}x${sec.height} mm`;
        const tooltip = `Scheda #${idx + 1}: ${sec.title || displayName}\nDimensioni: ${dims}\nCavi posati: ${cableCount}\nSerie: ${sec.type || 'P2000'}`;

        // 1. Creazione Tab nello Strip orizzontale
        const tabWrapper = document.createElement("div");
        tabWrapper.className = "group inline-flex items-center rounded-md text-xs mono transition shadow-sm flex-shrink-0 " +
          (active
            ? "bg-amber-500 text-slate-950 font-bold border border-amber-400 active-tab shadow-amber-500/20"
            : "bg-slate-800 hover:bg-slate-700/90 text-slate-300 hover:text-white border border-slate-700/80");

        const btn = document.createElement("button");
        btn.type = "button";
        btn.onclick = () => switchSectionTab(idx);
        btn.className = "px-2.5 py-1 flex items-center gap-1.5 focus:outline-none max-w-[210px] text-left";
        btn.title = tooltip;

        const idxBadge = document.createElement("span");
        idxBadge.className = "text-[11px] " + (active ? "text-slate-900 font-extrabold" : "text-amber-400 font-semibold");
        idxBadge.textContent = `#${idx + 1}`;

        const nameSpan = document.createElement("span");
        nameSpan.className = "truncate max-w-[110px] font-medium";
        nameSpan.textContent = displayName;

        const dimsSpan = document.createElement("span");
        dimsSpan.className = "text-[11px] " + (active ? "text-slate-900/80" : "text-slate-400") + " whitespace-nowrap hidden sm:inline";
        dimsSpan.textContent = `(${sec.width}x${sec.height})`;

        btn.appendChild(idxBadge);
        btn.appendChild(nameSpan);
        btn.appendChild(dimsSpan);
        tabWrapper.appendChild(btn);

        if (projectSections.length > 1) {
          const btnClose = document.createElement("button");
          btnClose.type = "button";
          btnClose.innerHTML = "&times;";
          btnClose.title = `Chiudi scheda #${idx + 1} "${displayName}"`;
          btnClose.className = "px-1.5 py-1 text-xs " +
            (active ? "text-slate-950/70 hover:text-slate-950 hover:bg-amber-600/30" : "text-slate-400 hover:text-white hover:bg-red-500") +
            " rounded-r-md transition focus:outline-none";
          btnClose.onclick = (e) => deleteSectionTabAt(idx, e);
          tabWrapper.appendChild(btnClose);
        }

        box.appendChild(tabWrapper);
        if (active) activeTabWrapper = tabWrapper;

        // 2. Popolamento Dropdown Salto Rapido
        if (dropList) {
          const item = document.createElement("div");
          item.className = "flex items-center justify-between px-3 py-2 hover:bg-slate-800 transition cursor-pointer " +
            (active ? "bg-[#243045] border-l-4 border-l-[#A8C7FA] tab-dropdown-active" : "");

          const leftBtn = document.createElement("button");
          leftBtn.type = "button";
          leftBtn.className = "flex-1 text-left min-w-0 pr-2 focus:outline-none";
          leftBtn.onclick = () => {
            switchSectionTab(idx);
            toggleTabsDropdown(false);
          };

          leftBtn.innerHTML = `
            <div class="flex items-center gap-1.5 min-w-0">
              <span class="text-[11px] font-mono font-bold ${active ? 'text-sky-200' : 'text-slate-400'}">#${idx + 1}</span>
              <span class="text-xs font-semibold ${active ? 'text-sky-100' : 'text-slate-200'} truncate">${escapeHtml(displayName)}</span>
              ${active ? '<span class="text-[11px] px-1 py-0.2 rounded bg-[#284777] text-[#D6E3FF] font-bold ml-auto flex-shrink-0">Aperta</span>' : ''}
            </div>
            <div class="text-[11px] text-slate-400 mono mt-0.5 truncate flex items-center gap-2">
              <span>📏 ${dims}</span>
              <span>&bull;</span>
              <span>⚡ ${cableCount} cavi</span>
              <span>&bull;</span>
              <span>${escapeHtml(sec.type || 'P2000')}</span>
            </div>
          `;
          item.appendChild(leftBtn);

          if (projectSections.length > 1) {
            const delBtn = document.createElement("button");
            delBtn.type = "button";
            delBtn.className = "px-1.5 py-1 text-xs text-slate-400 hover:text-white hover:bg-red-500/80 rounded transition";
            delBtn.innerHTML = "&times;";
            delBtn.title = `Elimina scheda #${idx + 1}`;
            delBtn.onclick = (e) => {
              deleteSectionTabAt(idx, e);
              toggleTabsDropdown(false);
            };
            item.appendChild(delBtn);
          }

          dropList.appendChild(item);
        }
      });


      // Porta in vista la scheda attiva solo quando cambia (non a ogni ricalcolo)
      const scrollKey = `${currentSectionIdx}/${projectSections.length}`;
      if (activeTabWrapper && scrollKey !== lastTabScrollKey) {
        lastTabScrollKey = scrollKey;
        requestAnimationFrame(() => {
          if (activeTabWrapper.isConnected) activeTabWrapper.scrollIntoView({ behavior: "smooth", inline: "center", block: "nearest" });
        });
      }
    }
    let lastTabScrollKey = "";
    // Dopo il renderer delle schede: aggiorna selettore e pannello Progetto con le etichette nuove
    const _renderTabsBase = renderMultiSectionTabs;
    renderMultiSectionTabs = function () { _renderTabsBase(); renderProjectPanel(); };

    function switchSectionTab(idx) {
      if (idx < 0 || idx >= projectSections.length || idx === currentSectionIdx) return;
      projectSections[currentSectionIdx] = cloneStateSnapshot(state);
      currentSectionIdx = idx;
      state = cloneStateSnapshot(projectSections[currentSectionIdx]);
      pushStateToUI(); // include recalculateAll()
      fitAll();
      renderMultiSectionTabs();
    }

    function addNewSectionTab(duplicateCurrent = false) {
      projectSections[currentSectionIdx] = cloneStateSnapshot(state);
      const nextNum = projectSections.length + 1;
      const letter = String.fromCharCode(64 + Math.min(26, nextNum));
      const newSec = cloneStateSnapshot(state);
      newSec.planTag = `SEZ. ${letter}-${letter} | H=+3.20m`;
      if (duplicateCurrent) {
        newSec.title = `${state.title.replace(/\s*\(Copia\)/i, "")} (Sez. ${letter}-${letter})`;
      } else {
        newSec.title = `NUOVA CANALINA ${String(nextNum).padStart(2, "0")} - L=${state.width}mm H=${state.height}mm`;
        newSec.cables = [];
        newSec.support = "none";
        newSec.pdfLoaded = false;
      }
      projectSections.push(newSec);
      currentSectionIdx = projectSections.length - 1;
      state = cloneStateSnapshot(newSec);
      pushStateToUI();
      fitAll();
      syncActiveSectionToMultiStore();
    }

    function deleteCurrentSectionTab() {
      deleteSectionTabAt(currentSectionIdx);
    }

    // ---------------------------------------------------------------------------
    // FILE DI PROGETTO .ccad (CanaloCAD): contiene TUTTE le sezioni del progetto.
    // Dentro è JSON leggibile; si riaprono anche i vecchi file .json.
    // Dove il browser lo consente (File System Access API) "Salva" riscrive lo stesso file.
    // ---------------------------------------------------------------------------
    const CCAD_EXT = ".ccad";
    const CCAD_MIME = "application/x-canalocad";
    const CCAD_PICKER_TYPES = [{ description: "Progetto CanaloCAD", accept: { [CCAD_MIME]: [CCAD_EXT] } }];
    let projectFileHandle = null;
    let projectFileName = "";

    function buildProjectPayload() {
      syncActiveSectionToMultiStore();
      return JSON.stringify({ format: "CanaloCAD project", app: APP_NAME, version: APP_VERSION, savedAt: new Date().toISOString(), currentSectionIdx, projectSections }, null, 2);
    }

    function suggestedProjectName() {
      const base = (projectFileName || state.tb.docCode || "PROGETTO_CANALINE").replace(/\.(ccad|json)$/i, "").replace(/[^a-zA-Z0-9_ -]/g, "_").trim();
      return `${base || "PROGETTO_CANALINE"}${CCAD_EXT}`;
    }

    // Salva il progetto. saveAs = true chiede sempre dove salvare.
    function saveProject(saveAs = false) {
      const text = buildProjectPayload();
      const n = projectSections.length;
      const done = (name) => toast(`Progetto salvato: ${name} (${n} ${n === 1 ? "sezione" : "sezioni"})`, "ok", null, 3000);
      if (window.showSaveFilePicker) {
        try {
          if (!projectFileHandle || saveAs) {
            projectFileHandle = window.showSaveFilePicker({ suggestedName: suggestedProjectName(), types: CCAD_PICKER_TYPES });
          }
          const w = projectFileHandle.createWritable();
          w.write(new Blob([text], { type: CCAD_MIME }));
          w.close();
          projectFileName = projectFileHandle.name;
          done(projectFileName);
          return;
        } catch (e) {
          if (e && e.name === "AbortError") return; // l'utente ha annullato
          console.warn("Salvataggio diretto non riuscito, uso il download:", e);
          projectFileHandle = null;
        }
      }
      const name = suggestedProjectName();
      downloadBlob(new Blob([text], { type: CCAD_MIME }), name);
      done(name);
    }
    // Nome storico usato da menu e scorciatoie
    function exportProjectJSON() { return saveProject(false); }

    function loadProjectText(text, fileName) {
      let data;
      try { data = JSON.parse(text); } catch (e) { throw new Error("il file non è un progetto CanaloCAD valido"); }
      if (!data || !Array.isArray(data.projectSections) || data.projectSections.length === 0) {
        throw new Error("il file non contiene sezioni di progetto CanaloCAD");
      }
      if (scheduleSectionSync.pending()) scheduleSectionSync.flush();
      projectSections = data.projectSections.map(normalizeSection);
      currentSectionIdx = Math.min(Math.max(0, data.currentSectionIdx || 0), projectSections.length - 1);
      state = cloneStateSnapshot(projectSections[currentSectionIdx]);
      projectFileName = fileName || "";
      pushStateToUI();
      fitAll();
      syncActiveSectionToMultiStore();
      toast(`Progetto "${fileName}" aperto (${projectSections.length} ${projectSections.length === 1 ? "sezione" : "sezioni"})`, "ok", { label: "Annulla", onClick: undoLastChange });
    }

    function openProjectFile(file, handle = null) {
      try {
        loadProjectText(file.text(), file.name);
        projectFileHandle = handle; // con l'handle, "Salva" riscrive lo stesso file
      } catch (e) {
        console.error("Errore apertura progetto:", e);
        toast(`Impossibile aprire "${file.name}": ${e.message}`, "error", null, 8000);
      }
    }

    // Apri progetto: selettore del sistema se disponibile (mantiene il file per "Salva"), altrimenti <input>
    // Apri progetto: si usa sempre il selettore classico <input type="file">, sincrono rispetto al clic.
    // (Il selettore moderno, se falliva, ripiegava sul classico dopo un await: il browser lo
    // considerava non più un gesto dell'utente e lo bloccava senza alcun messaggio.)
    function openProjectDialog() {
      document.getElementById("jsonProjectInput").click();
    }

    // Smista un insieme di file al lettore giusto in base all'estensione (import, trascinamento)
    function routeImportedFiles(files) {
      const list = Array.from(files || []);
      if (!list.length) return;
      const ext = f => (f.name.toLowerCase().match(/\.[a-z0-9]+$/) || [""])[0];
      const project = list.find(f => ext(f) === ".ccad" || ext(f) === ".json");
      if (project) { openProjectFile(project); return; }
      const pdfs = list.filter(f => ext(f) === ".pdf");
      const cads = list.filter(f => ext(f) === ".dwg" || ext(f) === ".dxf");
      if (pdfs.length) importPDFFile({ files: pdfs, value: "" });
      if (cads.length) importCADFile({ files: cads, value: "" });
      if (!pdfs.length && !cads.length) toast(`Formato non riconosciuto: "${list[0].name}". Si possono aprire file .ccad, .dwg, .dxf e .pdf.`, "warn", null, 7000);
    }

    function importProjectJSON(inputEl) {
      const file = inputEl.files && inputEl.files[0];
      if (!file) return;
      try { openProjectFile(file, null); } finally { inputEl.value = ""; }
    }

    // App installata (PWA): doppio clic su un file .ccad apre direttamente il progetto
    function initProjectFileLaunch() {
      if (!("launchQueue" in window)) return;
      window.launchQueue.setConsumer((params) => {
        const handle = params.files && params.files[0];
        if (handle) openProjectFile(handle.getFile(), handle);
      });
    }

// ---- 11-dxf-template.js
    // GENERATO da tools/make_dxf_template.py (ezdxf 1.3.4) - non modificare a mano.
    // Scheletro DXF R2000 (AC1015) con tabelle, blocchi, layout e dizionari coerenti.
    const DXF_R2000_TEMPLATE = {
      head: "  0\nSECTION\n  2\nHEADER\n  9\n$ACADVER\n  1\nAC1015\n  9\n$ACADMAINTVER\n 70\n6\n  9\n$DWGCODEPAGE\n  3\nANSI_1252\n  9\n$INSBASE\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$EXTMIN\n 10\n@@EXTMINX@@\n 20\n@@EXTMINY@@\n 30\n0.0\n  9\n$EXTMAX\n 10\n@@EXTMAXX@@\n 20\n@@EXTMAXY@@\n 30\n0.0\n  9\n$LIMMIN\n 10\n0.0\n 20\n0.0\n  9\n$LIMMAX\n 10\n420.0\n 20\n297.0\n  9\n$ORTHOMODE\n 70\n0\n  9\n$REGENMODE\n 70\n1\n  9\n$FILLMODE\n 70\n1\n  9\n$QTEXTMODE\n 70\n0\n  9\n$MIRRTEXT\n 70\n1\n  9\n$LTSCALE\n 40\n1.0\n  9\n$ATTMODE\n 70\n1\n  9\n$TEXTSIZE\n 40\n2.5\n  9\n$TRACEWID\n 40\n1.0\n  9\n$TEXTSTYLE\n  7\nStandard\n  9\n$CLAYER\n  8\n0\n  9\n$CELTYPE\n  6\nByLayer\n  9\n$CECOLOR\n 62\n256\n  9\n$CELTSCALE\n 40\n1.0\n  9\n$DISPSILH\n 70\n0\n  9\n$DIMSCALE\n 40\n1.0\n  9\n$DIMASZ\n 40\n2.5\n  9\n$DIMEXO\n 40\n0.625\n  9\n$DIMDLI\n 40\n3.75\n  9\n$DIMRND\n 40\n0.0\n  9\n$DIMDLE\n 40\n0.0\n  9\n$DIMEXE\n 40\n1.25\n  9\n$DIMTP\n 40\n0.0\n  9\n$DIMTM\n 40\n0.0\n  9\n$DIMTXT\n 40\n2.5\n  9\n$DIMCEN\n 40\n2.5\n  9\n$DIMTSZ\n 40\n0.0\n  9\n$DIMTOL\n 70\n0\n  9\n$DIMLIM\n 70\n0\n  9\n$DIMTIH\n 70\n0\n  9\n$DIMTOH\n 70\n0\n  9\n$DIMSE1\n 70\n0\n  9\n$DIMSE2\n 70\n0\n  9\n$DIMTAD\n 70\n1\n  9\n$DIMZIN\n 70\n8\n  9\n$DIMBLK\n  1\n\n  9\n$DIMASO\n 70\n1\n  9\n$DIMSHO\n 70\n1\n  9\n$DIMPOST\n  1\n\n  9\n$DIMAPOST\n  1\n\n  9\n$DIMALT\n 70\n0\n  9\n$DIMALTD\n 70\n3\n  9\n$DIMALTF\n 40\n0.03937007874\n  9\n$DIMLFAC\n 40\n1.0\n  9\n$DIMTOFL\n 70\n1\n  9\n$DIMTVP\n 40\n0.0\n  9\n$DIMTIX\n 70\n0\n  9\n$DIMSOXD\n 70\n0\n  9\n$DIMSAH\n 70\n0\n  9\n$DIMBLK1\n  1\n\n  9\n$DIMBLK2\n  1\n\n  9\n$DIMSTYLE\n  2\nISO-25\n  9\n$DIMCLRD\n 70\n0\n  9\n$DIMCLRE\n 70\n0\n  9\n$DIMCLRT\n 70\n0\n  9\n$DIMTFAC\n 40\n1.0\n  9\n$DIMGAP\n 40\n0.625\n  9\n$DIMJUST\n 70\n0\n  9\n$DIMSD1\n 70\n0\n  9\n$DIMSD2\n 70\n0\n  9\n$DIMTOLJ\n 70\n0\n  9\n$DIMTZIN\n 70\n8\n  9\n$DIMALTZ\n 70\n0\n  9\n$DIMALTTZ\n 70\n0\n  9\n$DIMUPT\n 70\n0\n  9\n$DIMDEC\n 70\n2\n  9\n$DIMTDEC\n 70\n2\n  9\n$DIMALTU\n 70\n2\n  9\n$DIMALTTD\n 70\n3\n  9\n$DIMTXSTY\n  7\nStandard\n  9\n$DIMAUNIT\n 70\n0\n  9\n$DIMADEC\n 70\n0\n  9\n$DIMALTRND\n 40\n0.0\n  9\n$DIMAZIN\n 70\n0\n  9\n$DIMDSEP\n 70\n44\n  9\n$DIMATFIT\n 70\n3\n  9\n$DIMFRAC\n 70\n0\n  9\n$DIMLDRBLK\n  1\n\n  9\n$DIMLUNIT\n 70\n2\n  9\n$DIMLWD\n 70\n-2\n  9\n$DIMLWE\n 70\n-2\n  9\n$DIMTMOVE\n 70\n0\n  9\n$LUNITS\n 70\n2\n  9\n$LUPREC\n 70\n4\n  9\n$SKETCHINC\n 40\n1.0\n  9\n$FILLETRAD\n 40\n10.0\n  9\n$AUNITS\n 70\n0\n  9\n$AUPREC\n 70\n2\n  9\n$MENU\n  1\n.\n  9\n$ELEVATION\n 40\n0.0\n  9\n$PELEVATION\n 40\n0.0\n  9\n$THICKNESS\n 40\n0.0\n  9\n$LIMCHECK\n 70\n0\n  9\n$CHAMFERA\n 40\n0.0\n  9\n$CHAMFERB\n 40\n0.0\n  9\n$CHAMFERC\n 40\n0.0\n  9\n$CHAMFERD\n 40\n0.0\n  9\n$SKPOLY\n 70\n0\n  9\n$TDCREATE\n 40\n2461350.650787037\n  9\n$TDUCREATE\n 40\n2458532.153996898\n  9\n$TDUPDATE\n 40\n2461350.650787037\n  9\n$TDUUPDATE\n 40\n2458532.1544311\n  9\n$TDINDWG\n 40\n0.0\n  9\n$TDUSRTIMER\n 40\n0.0\n  9\n$USRTIMER\n 70\n1\n  9\n$ANGBASE\n 50\n0.0\n  9\n$ANGDIR\n 70\n0\n  9\n$PDMODE\n 70\n0\n  9\n$PDSIZE\n 40\n0.0\n  9\n$PLINEWID\n 40\n0.0\n  9\n$SPLFRAME\n 70\n0\n  9\n$SPLINETYPE\n 70\n6\n  9\n$SPLINESEGS\n 70\n8\n  9\n$HANDSEED\n  5\n@@HANDSEED@@\n  9\n$SURFTAB1\n 70\n6\n  9\n$SURFTAB2\n 70\n6\n  9\n$SURFTYPE\n 70\n6\n  9\n$SURFU\n 70\n6\n  9\n$SURFV\n 70\n6\n  9\n$UCSBASE\n  2\n\n  9\n$UCSNAME\n  2\n\n  9\n$UCSORG\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$UCSXDIR\n 10\n1.0\n 20\n0.0\n 30\n0.0\n  9\n$UCSYDIR\n 10\n0.0\n 20\n1.0\n 30\n0.0\n  9\n$UCSORTHOREF\n  2\n\n  9\n$UCSORTHOVIEW\n 70\n0\n  9\n$UCSORGTOP\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$UCSORGBOTTOM\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$UCSORGLEFT\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$UCSORGRIGHT\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$UCSORGFRONT\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$UCSORGBACK\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$PUCSBASE\n  2\n\n  9\n$PUCSNAME\n  2\n\n  9\n$PUCSORG\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$PUCSXDIR\n 10\n1.0\n 20\n0.0\n 30\n0.0\n  9\n$PUCSYDIR\n 10\n0.0\n 20\n1.0\n 30\n0.0\n  9\n$PUCSORTHOREF\n  2\n\n  9\n$PUCSORTHOVIEW\n 70\n0\n  9\n$PUCSORGTOP\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$PUCSORGBOTTOM\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$PUCSORGLEFT\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$PUCSORGRIGHT\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$PUCSORGFRONT\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$PUCSORGBACK\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$USERI1\n 70\n0\n  9\n$USERI2\n 70\n0\n  9\n$USERI3\n 70\n0\n  9\n$USERI4\n 70\n0\n  9\n$USERI5\n 70\n0\n  9\n$USERR1\n 40\n0.0\n  9\n$USERR2\n 40\n0.0\n  9\n$USERR3\n 40\n0.0\n  9\n$USERR4\n 40\n0.0\n  9\n$USERR5\n 40\n0.0\n  9\n$WORLDVIEW\n 70\n1\n  9\n$SHADEDGE\n 70\n3\n  9\n$SHADEDIF\n 70\n70\n  9\n$TILEMODE\n 70\n1\n  9\n$MAXACTVP\n 70\n64\n  9\n$PINSBASE\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  9\n$PLIMCHECK\n 70\n0\n  9\n$PEXTMIN\n 10\n1e+20\n 20\n1e+20\n 30\n1e+20\n  9\n$PEXTMAX\n 10\n-1e+20\n 20\n-1e+20\n 30\n-1e+20\n  9\n$PLIMMIN\n 10\n0.0\n 20\n0.0\n  9\n$PLIMMAX\n 10\n420.0\n 20\n297.0\n  9\n$UNITMODE\n 70\n0\n  9\n$VISRETAIN\n 70\n1\n  9\n$PLINEGEN\n 70\n0\n  9\n$PSLTSCALE\n 70\n1\n  9\n$TREEDEPTH\n 70\n3020\n  9\n$CMLSTYLE\n  2\nStandard\n  9\n$CMLJUST\n 70\n0\n  9\n$CMLSCALE\n 40\n20.0\n  9\n$PROXYGRAPHICS\n 70\n1\n  9\n$MEASUREMENT\n 70\n1\n  9\n$CELWEIGHT\n370\n-1\n  9\n$ENDCAPS\n280\n0\n  9\n$JOINSTYLE\n280\n0\n  9\n$LWDISPLAY\n290\n0\n  9\n$INSUNITS\n 70\n4\n  9\n$HYPERLINKBASE\n  1\n\n  9\n$STYLESHEET\n  1\n\n  9\n$XEDIT\n290\n1\n  9\n$CEPSNTYPE\n380\n0\n  9\n$PSTYLEMODE\n290\n1\n  9\n$FINGERPRINTGUID\n  2\n{CD5D72D4-7A55-4C2A-BD8C-5C9998AD97D0}\n  9\n$VERSIONGUID\n  2\n{4AF2C607-3DC3-4D67-954B-46107642A778}\n  9\n$EXTNAMES\n290\n1\n  9\n$PSVPSCALE\n 40\n0.0\n  9\n$OLESTARTUP\n290\n0\n  0\nENDSEC\n  0\nSECTION\n  2\nCLASSES\n  0\nCLASS\n  1\nACDBDICTIONARYWDFLT\n  2\nAcDbDictionaryWithDefault\n  3\nObjectDBX Classes\n 90\n0\n280\n0\n281\n0\n  0\nCLASS\n  1\nSUN\n  2\nAcDbSun\n  3\nSCENEOE\n 90\n1153\n280\n0\n281\n0\n  0\nCLASS\n  1\nVISUALSTYLE\n  2\nAcDbVisualStyle\n  3\nObjectDBX Classes\n 90\n4095\n280\n0\n281\n0\n  0\nCLASS\n  1\nMATERIAL\n  2\nAcDbMaterial\n  3\nObjectDBX Classes\n 90\n1153\n280\n0\n281\n0\n  0\nCLASS\n  1\nSCALE\n  2\nAcDbScale\n  3\nObjectDBX Classes\n 90\n1153\n280\n0\n281\n0\n  0\nCLASS\n  1\nTABLESTYLE\n  2\nAcDbTableStyle\n  3\nObjectDBX Classes\n 90\n4095\n280\n0\n281\n0\n  0\nCLASS\n  1\nMLEADERSTYLE\n  2\nAcDbMLeaderStyle\n  3\nACDB_MLEADERSTYLE_CLASS\n 90\n4095\n280\n0\n281\n0\n  0\nCLASS\n  1\nDICTIONARYVAR\n  2\nAcDbDictionaryVar\n  3\nObjectDBX Classes\n 90\n0\n280\n0\n281\n0\n  0\nCLASS\n  1\nCELLSTYLEMAP\n  2\nAcDbCellStyleMap\n  3\nObjectDBX Classes\n 90\n1152\n280\n0\n281\n0\n  0\nCLASS\n  1\nMENTALRAYRENDERSETTINGS\n  2\nAcDbMentalRayRenderSettings\n  3\nSCENEOE\n 90\n1024\n280\n0\n281\n0\n  0\nCLASS\n  1\nACDBDETAILVIEWSTYLE\n  2\nAcDbDetailViewStyle\n  3\nObjectDBX Classes\n 90\n1025\n280\n0\n281\n0\n  0\nCLASS\n  1\nACDBSECTIONVIEWSTYLE\n  2\nAcDbSectionViewStyle\n  3\nObjectDBX Classes\n 90\n1025\n280\n0\n281\n0\n  0\nCLASS\n  1\nRASTERVARIABLES\n  2\nAcDbRasterVariables\n  3\nISM\n 90\n0\n280\n0\n281\n0\n  0\nCLASS\n  1\nACDBPLACEHOLDER\n  2\nAcDbPlaceHolder\n  3\nObjectDBX Classes\n 90\n0\n280\n0\n281\n0\n  0\nCLASS\n  1\nLAYOUT\n  2\nAcDbLayout\n  3\nObjectDBX Classes\n 90\n0\n280\n0\n281\n0\n  0\nENDSEC\n  0\nSECTION\n  2\nTABLES\n  0\nTABLE\n  2\nVPORT\n  5\n8\n330\n0\n100\nAcDbSymbolTable\n 70\n1\n  0\nVPORT\n  5\n23\n330\n8\n100\nAcDbSymbolTableRecord\n100\nAcDbViewportTableRecord\n  2\n*Active\n 70\n0\n 10\n0.0\n 20\n0.0\n 11\n1.0\n 21\n1.0\n 12\n0.0\n 22\n0.0\n 13\n0.0\n 23\n0.0\n 14\n0.5\n 24\n0.5\n 15\n0.5\n 25\n0.5\n 16\n0.0\n 26\n0.0\n 36\n1.0\n 17\n0.0\n 27\n0.0\n 37\n0.0\n 40\n1000.0\n 41\n1.34\n 42\n50.0\n 43\n0.0\n 44\n0.0\n 50\n0.0\n 51\n0.0\n 71\n0\n 72\n1000\n 73\n1\n 74\n3\n 75\n0\n 76\n0\n 77\n0\n 78\n0\n281\n0\n 65\n0\n146\n0.0\n  0\nENDTAB\n  0\nTABLE\n  2\nLTYPE\n  5\n2\n330\n0\n100\nAcDbSymbolTable\n 70\n3\n  0\nLTYPE\n  5\n24\n330\n2\n100\nAcDbSymbolTableRecord\n100\nAcDbLinetypeTableRecord\n  2\nByBlock\n 70\n0\n  3\n\n 72\n65\n 73\n0\n 40\n0.0\n  0\nLTYPE\n  5\n25\n330\n2\n100\nAcDbSymbolTableRecord\n100\nAcDbLinetypeTableRecord\n  2\nByLayer\n 70\n0\n  3\n\n 72\n65\n 73\n0\n 40\n0.0\n  0\nLTYPE\n  5\n26\n330\n2\n100\nAcDbSymbolTableRecord\n100\nAcDbLinetypeTableRecord\n  2\nContinuous\n 70\n0\n  3\n\n 72\n65\n 73\n0\n 40\n0.0\n  0\nENDTAB\n  0\nTABLE\n  2\nLAYER\n  5\n1\n330\n0\n100\nAcDbSymbolTable\n 70\n16\n  0\nLAYER\n  5\n27\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\n0\n 70\n0\n 62\n7\n  6\nContinuous\n370\n-3\n390\n13\n  0\nLAYER\n  5\n28\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nDefpoints\n 70\n0\n 62\n7\n  6\nContinuous\n290\n0\n370\n-3\n390\n13\n  0\nLAYER\n  5\n2F\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nCanalina\n 70\n0\n 62\n140\n  6\nContinuous\n370\n50\n390\n13\n  0\nLAYER\n  5\n30\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nCavi\n 70\n0\n 62\n30\n  6\nContinuous\n370\n35\n390\n13\n  0\nLAYER\n  5\n31\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nAnime_Cavi\n 70\n0\n 62\n40\n  6\nContinuous\n370\n25\n390\n13\n  0\nLAYER\n  5\n32\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nSTAFFAGGI\n 70\n0\n 62\n252\n  6\nContinuous\n370\n35\n390\n13\n  0\nLAYER\n  5\n33\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nFASCETTE_ANCORAGGIO\n 70\n0\n 62\n4\n  6\nContinuous\n370\n35\n390\n13\n  0\nLAYER\n  5\n34\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nBandierine\n 70\n0\n 62\n40\n  6\nContinuous\n370\n20\n390\n13\n  0\nLAYER\n  5\n35\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nTesti\n 70\n0\n 62\n7\n  6\nContinuous\n370\n25\n390\n13\n  0\nLAYER\n  5\n36\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nQUOTE\n 70\n0\n 62\n40\n  6\nContinuous\n370\n25\n390\n13\n  0\nLAYER\n  5\n37\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nTABELLA\n 70\n0\n 62\n140\n  6\nContinuous\n370\n30\n390\n13\n  0\nLAYER\n  5\n38\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nCURVA_CARICO\n 70\n0\n 62\n90\n  6\nContinuous\n370\n35\n390\n13\n  0\nLAYER\n  5\n39\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nETICHETTA_PIANTA\n 70\n0\n 62\n80\n  6\nContinuous\n370\n35\n390\n13\n  0\nLAYER\n  5\n3A\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nVISTA_3D_ISO\n 70\n0\n 62\n251\n  6\nContinuous\n370\n30\n390\n13\n  0\nLAYER\n  5\n3B\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nCORNICE\n 70\n0\n 62\n251\n  6\nContinuous\n370\n60\n390\n13\n  0\nLAYER\n  5\n3C\n330\n1\n100\nAcDbSymbolTableRecord\n100\nAcDbLayerTableRecord\n  2\nSFONDI_RIEMPIMENTI\n 70\n0\n 62\n250\n  6\nContinuous\n370\n0\n390\n13\n  0\nENDTAB\n  0\nTABLE\n  2\nSTYLE\n  5\n5\n330\n0\n100\nAcDbSymbolTable\n 70\n4\n  0\nSTYLE\n  5\n29\n330\n5\n100\nAcDbSymbolTableRecord\n100\nAcDbTextStyleTableRecord\n  2\nStandard\n 70\n0\n 40\n0.0\n 41\n1.0\n 50\n0.0\n 71\n0\n 42\n2.5\n  3\ntxt\n  4\n\n  0\nSTYLE\n  5\n3D\n330\n5\n100\nAcDbSymbolTableRecord\n100\nAcDbTextStyleTableRecord\n  2\nARIAL\n 70\n0\n 40\n0.0\n 41\n1.0\n 50\n0.0\n 71\n0\n 42\n2.5\n  3\narial.ttf\n  4\n\n  0\nSTYLE\n  5\n3E\n330\n5\n100\nAcDbSymbolTableRecord\n100\nAcDbTextStyleTableRecord\n  2\nMONO_CAD\n 70\n0\n 40\n0.0\n 41\n1.0\n 50\n0.0\n 71\n0\n 42\n2.5\n  3\nisocp.shx\n  4\n\n  0\nSTYLE\n  5\n3F\n330\n5\n100\nAcDbSymbolTableRecord\n100\nAcDbTextStyleTableRecord\n  2\nTITLE_CAD\n 70\n0\n 40\n0.0\n 41\n1.0\n 50\n0.0\n 71\n0\n 42\n2.5\n  3\narial.ttf\n  4\n\n  0\nENDTAB\n  0\nTABLE\n  2\nVIEW\n  5\n7\n330\n0\n100\nAcDbSymbolTable\n 70\n0\n  0\nENDTAB\n  0\nTABLE\n  2\nUCS\n  5\n6\n330\n0\n100\nAcDbSymbolTable\n 70\n0\n  0\nENDTAB\n  0\nTABLE\n  2\nAPPID\n  5\n3\n330\n0\n100\nAcDbSymbolTable\n 70\n3\n  0\nAPPID\n  5\n2A\n330\n3\n100\nAcDbSymbolTableRecord\n100\nAcDbRegAppTableRecord\n  2\nACAD\n 70\n0\n  0\nAPPID\n  5\n40\n330\n3\n100\nAcDbSymbolTableRecord\n100\nAcDbRegAppTableRecord\n  2\nHATCHBACKGROUNDCOLOR\n 70\n0\n  0\nAPPID\n  5\n41\n330\n3\n100\nAcDbSymbolTableRecord\n100\nAcDbRegAppTableRecord\n  2\nEZDXF\n 70\n0\n  0\nENDTAB\n  0\nTABLE\n  2\nDIMSTYLE\n  5\n4\n330\n0\n100\nAcDbSymbolTable\n 70\n1\n100\nAcDbDimStyleTable\n  0\nDIMSTYLE\n105\n2B\n330\n4\n100\nAcDbSymbolTableRecord\n100\nAcDbDimStyleTableRecord\n  2\nStandard\n 70\n0\n  3\n\n  4\n\n 40\n1.0\n 41\n2.5\n 42\n0.625\n 43\n3.75\n 44\n1.25\n 45\n0.0\n 46\n0.0\n 47\n0.0\n 48\n0.0\n140\n2.5\n141\n2.5\n142\n0.0\n143\n0.03937007874\n144\n1.0\n145\n0.0\n146\n1.0\n147\n0.625\n148\n0.0\n 71\n0\n 72\n0\n 73\n0\n 74\n0\n 75\n0\n 76\n0\n 77\n1\n 78\n8\n 79\n3\n170\n0\n171\n3\n172\n1\n173\n0\n174\n0\n175\n0\n176\n0\n177\n0\n178\n0\n179\n2\n271\n2\n272\n2\n273\n2\n274\n3\n275\n0\n276\n0\n277\n2\n278\n44\n279\n0\n280\n0\n281\n0\n282\n0\n283\n0\n284\n8\n285\n0\n286\n0\n288\n0\n289\n3\n371\n-2\n372\n-2\n  0\nENDTAB\n  0\nTABLE\n  2\nBLOCK_RECORD\n  5\n9\n330\n0\n100\nAcDbSymbolTable\n 70\n2\n  0\nBLOCK_RECORD\n  5\n17\n330\n9\n100\nAcDbSymbolTableRecord\n100\nAcDbBlockTableRecord\n  2\n*Model_Space\n340\n1A\n  0\nBLOCK_RECORD\n  5\n1B\n330\n9\n100\nAcDbSymbolTableRecord\n100\nAcDbBlockTableRecord\n  2\n*Paper_Space\n340\n1E\n  0\nENDTAB\n  0\nENDSEC\n  0\nSECTION\n  2\nBLOCKS\n  0\nBLOCK\n  5\n18\n330\n17\n100\nAcDbEntity\n  8\n0\n100\nAcDbBlockBegin\n  2\n*Model_Space\n 70\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  3\n*Model_Space\n  1\n\n  0\nENDBLK\n  5\n19\n330\n17\n100\nAcDbEntity\n  8\n0\n100\nAcDbBlockEnd\n  0\nBLOCK\n  5\n1C\n330\n1B\n100\nAcDbEntity\n  8\n0\n100\nAcDbBlockBegin\n  2\n*Paper_Space\n 70\n0\n 10\n0.0\n 20\n0.0\n 30\n0.0\n  3\n*Paper_Space\n  1\n\n  0\nENDBLK\n  5\n1D\n330\n1B\n100\nAcDbEntity\n  8\n0\n100\nAcDbBlockEnd\n  0\nENDSEC\n  0\nSECTION\n  2\nENTITIES\n",
      tail: "  0\nENDSEC\n  0\nSECTION\n  2\nOBJECTS\n  0\nDICTIONARY\n  5\nA\n330\n0\n100\nAcDbDictionary\n280\n0\n281\n1\n  3\nACAD_COLOR\n350\nB\n  3\nACAD_GROUP\n350\nC\n  3\nACAD_LAYOUT\n350\nD\n  3\nACAD_MATERIAL\n350\nE\n  3\nACAD_MLEADERSTYLE\n350\nF\n  3\nACAD_MLINESTYLE\n350\n10\n  3\nACAD_PLOTSETTINGS\n350\n11\n  3\nACAD_PLOTSTYLENAME\n350\n12\n  3\nACAD_SCALELIST\n350\n14\n  3\nACAD_TABLESTYLE\n350\n15\n  3\nACAD_VISUALSTYLE\n350\n16\n  3\nEZDXF_META\n350\n2D\n  0\nDICTIONARY\n  5\nB\n330\nA\n100\nAcDbDictionary\n280\n0\n281\n1\n  0\nDICTIONARY\n  5\nC\n330\nA\n100\nAcDbDictionary\n280\n0\n281\n1\n  0\nDICTIONARY\n  5\nD\n330\nA\n100\nAcDbDictionary\n280\n0\n281\n1\n  3\nModel\n350\n1A\n  3\nLayout1\n350\n1E\n  0\nDICTIONARY\n  5\nE\n330\nA\n100\nAcDbDictionary\n280\n0\n281\n1\n  3\nByBlock\n350\n1F\n  3\nByLayer\n350\n20\n  3\nGlobal\n350\n21\n  0\nDICTIONARY\n  5\nF\n330\nA\n100\nAcDbDictionary\n280\n0\n281\n1\n  3\nStandard\n350\n2C\n  0\nDICTIONARY\n  5\n10\n330\nA\n100\nAcDbDictionary\n280\n0\n281\n1\n  3\nStandard\n350\n22\n  0\nDICTIONARY\n  5\n11\n330\nA\n100\nAcDbDictionary\n280\n0\n281\n1\n  0\nACDBDICTIONARYWDFLT\n  5\n12\n330\nA\n100\nAcDbDictionary\n280\n0\n281\n1\n  3\nNormal\n350\n13\n100\nAcDbDictionaryWithDefault\n340\n13\n  0\nACDBPLACEHOLDER\n  5\n13\n330\n12\n  0\nDICTIONARY\n  5\n14\n330\nA\n100\nAcDbDictionary\n280\n0\n281\n1\n  0\nDICTIONARY\n  5\n15\n330\nA\n100\nAcDbDictionary\n280\n0\n281\n1\n  0\nDICTIONARY\n  5\n16\n330\nA\n100\nAcDbDictionary\n280\n0\n281\n1\n  0\nLAYOUT\n  5\n1A\n330\nD\n100\nAcDbPlotSettings\n  1\n\n  4\nA3\n  6\n\n 40\n7.5\n 41\n20.0\n 42\n7.5\n 43\n20.0\n 44\n420.0\n 45\n297.0\n 46\n0.0\n 47\n0.0\n 48\n0.0\n 49\n0.0\n140\n0.0\n141\n0.0\n142\n1.0\n143\n1.0\n 70\n1024\n 72\n1\n 73\n0\n 74\n5\n  7\n\n 75\n16\n 76\n0\n 77\n2\n 78\n300\n147\n1.0\n148\n0.0\n149\n0.0\n100\nAcDbLayout\n  1\nModel\n 70\n1\n 71\n0\n 10\n0.0\n 20\n0.0\n 11\n420.0\n 21\n297.0\n 12\n0.0\n 22\n0.0\n 32\n0.0\n 14\n1e+20\n 24\n1e+20\n 34\n1e+20\n 15\n-1e+20\n 25\n-1e+20\n 35\n-1e+20\n146\n0.0\n 13\n0.0\n 23\n0.0\n 33\n0.0\n 16\n1.0\n 26\n0.0\n 36\n0.0\n 17\n0.0\n 27\n1.0\n 37\n0.0\n 76\n1\n330\n17\n  0\nLAYOUT\n  5\n1E\n330\nD\n100\nAcDbPlotSettings\n  1\n\n  4\nA3\n  6\n\n 40\n7.5\n 41\n20.0\n 42\n7.5\n 43\n20.0\n 44\n420.0\n 45\n297.0\n 46\n0.0\n 47\n0.0\n 48\n0.0\n 49\n0.0\n140\n0.0\n141\n0.0\n142\n1.0\n143\n1.0\n 70\n0\n 72\n1\n 73\n0\n 74\n5\n  7\n\n 75\n16\n 76\n0\n 77\n2\n 78\n300\n147\n1.0\n148\n0.0\n149\n0.0\n100\nAcDbLayout\n  1\nLayout1\n 70\n1\n 71\n1\n 10\n0.0\n 20\n0.0\n 11\n420.0\n 21\n297.0\n 12\n0.0\n 22\n0.0\n 32\n0.0\n 14\n1e+20\n 24\n1e+20\n 34\n1e+20\n 15\n-1e+20\n 25\n-1e+20\n 35\n-1e+20\n146\n0.0\n 13\n0.0\n 23\n0.0\n 33\n0.0\n 16\n1.0\n 26\n0.0\n 36\n0.0\n 17\n0.0\n 27\n1.0\n 37\n0.0\n 76\n1\n330\n1B\n  0\nMATERIAL\n  5\n1F\n102\n{ACAD_REACTORS\n330\nE\n102\n}\n330\nE\n100\nAcDbMaterial\n  1\nByBlock\n  2\n\n 70\n0\n 40\n1.0\n 71\n1\n 41\n1.0\n 91\n-1023410177\n 42\n1.0\n 72\n1\n  3\n\n 73\n1\n 74\n1\n 75\n1\n 44\n0.5\n 73\n0\n 45\n1.0\n 46\n1.0\n 77\n1\n  4\n\n 78\n1\n 79\n1\n170\n1\n 48\n1.0\n171\n1\n  6\n\n172\n1\n173\n1\n174\n1\n140\n1.0\n141\n1.0\n175\n1\n  7\n\n176\n1\n177\n1\n178\n1\n143\n1.0\n179\n1\n  8\n\n270\n1\n271\n1\n272\n1\n145\n1.0\n146\n1.0\n273\n1\n  9\n\n274\n1\n275\n1\n276\n1\n 42\n1.0\n 72\n1\n  3\n\n 73\n1\n 74\n1\n 75\n1\n 94\n63\n  0\nMATERIAL\n  5\n20\n102\n{ACAD_REACTORS\n330\nE\n102\n}\n330\nE\n100\nAcDbMaterial\n  1\nByLayer\n  2\n\n 70\n0\n 40\n1.0\n 71\n1\n 41\n1.0\n 91\n-1023410177\n 42\n1.0\n 72\n1\n  3\n\n 73\n1\n 74\n1\n 75\n1\n 44\n0.5\n 73\n0\n 45\n1.0\n 46\n1.0\n 77\n1\n  4\n\n 78\n1\n 79\n1\n170\n1\n 48\n1.0\n171\n1\n  6\n\n172\n1\n173\n1\n174\n1\n140\n1.0\n141\n1.0\n175\n1\n  7\n\n176\n1\n177\n1\n178\n1\n143\n1.0\n179\n1\n  8\n\n270\n1\n271\n1\n272\n1\n145\n1.0\n146\n1.0\n273\n1\n  9\n\n274\n1\n275\n1\n276\n1\n 42\n1.0\n 72\n1\n  3\n\n 73\n1\n 74\n1\n 75\n1\n 94\n63\n  0\nMATERIAL\n  5\n21\n102\n{ACAD_REACTORS\n330\nE\n102\n}\n330\nE\n100\nAcDbMaterial\n  1\nGlobal\n  2\n\n 70\n0\n 40\n1.0\n 71\n1\n 41\n1.0\n 91\n-1023410177\n 42\n1.0\n 72\n1\n  3\n\n 73\n1\n 74\n1\n 75\n1\n 44\n0.5\n 73\n0\n 45\n1.0\n 46\n1.0\n 77\n1\n  4\n\n 78\n1\n 79\n1\n170\n1\n 48\n1.0\n171\n1\n  6\n\n172\n1\n173\n1\n174\n1\n140\n1.0\n141\n1.0\n175\n1\n  7\n\n176\n1\n177\n1\n178\n1\n143\n1.0\n179\n1\n  8\n\n270\n1\n271\n1\n272\n1\n145\n1.0\n146\n1.0\n273\n1\n  9\n\n274\n1\n275\n1\n276\n1\n 42\n1.0\n 72\n1\n  3\n\n 73\n1\n 74\n1\n 75\n1\n 94\n63\n  0\nMLINESTYLE\n  5\n22\n102\n{ACAD_REACTORS\n330\n10\n102\n}\n330\n10\n100\nAcDbMlineStyle\n  2\nStandard\n 70\n0\n  3\n\n 62\n256\n 51\n90.0\n 52\n90.0\n 71\n2\n 49\n0.5\n 62\n256\n  6\nBYLAYER\n 49\n-0.5\n 62\n256\n  6\nBYLAYER\n  0\nMLEADERSTYLE\n  5\n2C\n102\n{ACAD_REACTORS\n330\nF\n102\n}\n330\nF\n100\nAcDbMLeaderStyle\n179\n2\n170\n2\n171\n1\n172\n0\n 90\n2\n 40\n0.0\n 41\n0.0\n173\n1\n 91\n-1056964608\n 92\n-2\n290\n1\n 42\n2.0\n291\n1\n 43\n8.0\n  3\nStandard\n 44\n4.0\n300\n\n342\n29\n174\n1\n175\n1\n176\n0\n178\n1\n 93\n-1056964608\n 45\n4.0\n292\n0\n297\n0\n 46\n4.0\n 94\n-1056964608\n 47\n1.0\n 49\n1.0\n140\n1.0\n294\n1\n141\n0.0\n177\n0\n142\n1.0\n295\n0\n296\n0\n143\n3.75\n271\n0\n272\n9\n273\n9\n  0\nDICTIONARY\n  5\n2D\n330\nA\n100\nAcDbDictionary\n281\n1\n  3\nCREATED_BY_EZDXF\n350\n2E\n  3\nWRITTEN_BY_EZDXF\n350\n42\n  0\nDICTIONARYVAR\n  5\n2E\n330\n2D\n100\nDictionaryVariables\n280\n0\n  1\n1.3.4 @ 2026-11-05T14:37:08.100657+00:00\n  0\nDICTIONARYVAR\n  5\n42\n330\n2D\n100\nDictionaryVariables\n280\n0\n  1\n1.3.4 @ 2026-11-05T14:37:08.101656+00:00\n  0\nENDSEC\n  0\nEOF\n",
      modelSpaceOwner: "17",
      firstHandle: 67
    };

// ---- 11-export.js
    // ============================================================================
    // 8. ESPORTATORI CERTIFICATI FEDELI 1:1 (.DXF TRUECOLOR, .SCR, .DOC, .CSV) + MULTI-SEZIONE
    // ============================================================================
    let exportDxfOptions = {
      format: "AC1015", // "AC1015" = DXF R2000 completo (predefinito) · "AC1009" = R12 minimo
      fontFamily: "arial", // Default: "arial" (Arial standardizzato a 2.5mm)
      includeBackgrounds: true,
      includeLineweights: true,
      includeUnicode: true
    };

    function toggleExportOptionsModal() {
      const modal = document.getElementById("exportOptionsModal");
      if (!modal) return;
      const isOpening = modal.classList.contains("hidden");
      if (isOpening) {
        const selFmt = document.getElementById("selDxfFormat");
        const selFnt = document.getElementById("selDxfFont");
        const chkBg = document.getElementById("chkDxfBg");
        const chkLw = document.getElementById("chkDxfLw");
        const chkUni = document.getElementById("chkDxfUnicode");
        const selSc = document.getElementById("selDxfScale");
        const chkChart = document.getElementById("chkExportChart");
        const chkBOM = document.getElementById("chkExportBOM");
        const chkIso = document.getElementById("chkExportIso3D");
        const chkCart = document.getElementById("chkExportCartiglio");
        if (selFmt) selFmt.value = exportDxfOptions.format;
        if (selFnt) selFnt.value = exportDxfOptions.fontFamily;
        if (chkBg) chkBg.checked = exportDxfOptions.includeBackgrounds;
        if (chkLw) chkLw.checked = exportDxfOptions.includeLineweights;
        if (chkUni) chkUni.checked = exportDxfOptions.includeUnicode;
        if (selSc) selSc.value = state.scale || "1:1";
        if (chkChart) chkChart.checked = state.showLoadChart !== false;
        if (chkBOM) chkBOM.checked = state.showBOM !== false;
        if (chkIso) chkIso.checked = state.showIso3D !== false;
        if (chkCart) chkCart.checked = state.showCartiglio !== false;
        const chkTableDesc = document.getElementById("chkExportTableDesc");
        if (chkTableDesc) chkTableDesc.checked = state.showTableDesc !== false && !state.hideTableDesc;
      }
      modal.classList.toggle("hidden");
    }

    function syncExportOptionsFromUI() {
      const selFmt = document.getElementById("selDxfFormat");
      const selFnt = document.getElementById("selDxfFont");
      const chkBg = document.getElementById("chkDxfBg");
      const chkLw = document.getElementById("chkDxfLw");
      const chkUni = document.getElementById("chkDxfUnicode");
      const selSc = document.getElementById("selDxfScale");
      const optTxtSc = document.getElementById("optDxfTextScale");
      const chkChart = document.getElementById("chkExportChart");
      const chkBOM = document.getElementById("chkExportBOM");
      const chkIso = document.getElementById("chkExportIso3D");
      const chkCart = document.getElementById("chkExportCartiglio");
      const chkTableDesc = document.getElementById("chkExportTableDesc");

      if (selFmt) exportDxfOptions.format = selFmt.value === "AC1009" ? "AC1009" : "AC1015";
      if (selFnt) exportDxfOptions.fontFamily = selFnt.value;
      if (chkBg) exportDxfOptions.includeBackgrounds = chkBg.checked;
      if (chkLw) exportDxfOptions.includeLineweights = chkLw.checked;
      if (chkUni) exportDxfOptions.includeUnicode = chkUni.checked;
      if (selSc) onScaleChange(selSc.value);
      if (optTxtSc) onTextScaleChange(optTxtSc.value);
      if (chkChart) state.showLoadChart = chkChart.checked;
      if (chkBOM) state.showBOM = chkBOM.checked;
      if (chkIso) state.showIso3D = chkIso.checked;
      if (chkCart) state.showCartiglio = chkCart.checked;
      if (chkTableDesc) {
        state.showTableDesc = chkTableDesc.checked;
        state.hideTableDesc = !chkTableDesc.checked;
      }
    }

    // Tabella palette standard ACI (1-255) per calcolo distanza euclidea RGB esatta
    const ACI_PALETTE = {
      1: [255, 0, 0], 2: [255, 255, 0], 3: [0, 255, 0], 4: [0, 255, 255],
      5: [0, 0, 255], 6: [255, 0, 255], 7: [255, 255, 255], 8: [128, 128, 128],
      9: [192, 192, 192], 10: [255, 0, 0], 11: [255, 127, 127], 12: [204, 0, 0],
      14: [153, 27, 27], 20: [234, 88, 12], 22: [154, 52, 18], 30: [245, 158, 11],
      31: [253, 186, 116], 32: [180, 83, 9], 34: [120, 53, 15], 36: [69, 26, 3],
      40: [251, 191, 36], 50: [234, 179, 8], 80: [74, 222, 128], 90: [34, 197, 94],
      92: [22, 163, 74], 130: [34, 211, 238], 131: [103, 232, 249], 140: [56, 189, 248],
      144: [14, 116, 144], 146: [22, 78, 99], 154: [12, 74, 110], 250: [30, 41, 59],
      251: [71, 85, 105], 252: [100, 116, 139], 253: [148, 163, 184], 254: [203, 213, 225], 255: [255, 255, 255]
    };

    function hexToRgb(hex) {
      if (!hex || !String(hex).startsWith("#") || hex.length < 7) return [255, 255, 255];
      return [
        parseInt(hex.slice(1, 3), 16) || 0,
        parseInt(hex.slice(3, 5), 16) || 0,
        parseInt(hex.slice(5, 7), 16) || 0
      ];
    }

    // Variante a oggetto {r,g,b} usata dall'export Lisp (TrueColor 420); null se il colore non è #RRGGBB
    function hexToRGB(hex) {
      if (!hex || !/^#[0-9a-f]{6}/i.test(String(hex))) return null;
      const [r, g, b] = hexToRgb(hex);
      return { r, g, b };
    }

    function hexToClosestACI(hex) {
      if (!hex) return 7;
      const [r, g, b] = hexToRgb(hex);
      let bestDist = Infinity;
      let bestAci = 7;
      for (const [aci, rgb] of Object.entries(ACI_PALETTE)) {
        const dr = r - rgb[0], dg = g - rgb[1], db = b - rgb[2];
        const dist = dr * dr + dg * dg + db * db;
        if (dist < bestDist) {
          bestDist = dist;
          bestAci = parseInt(aci, 10);
        }
      }
      return bestAci;
    }

    function hexToTrueColorInt(hex) {
      if (!hex || !String(hex).startsWith("#") || hex.length < 7) return null;
      const [r, g, b] = hexToRgb(hex);
      return (r << 16) | (g << 8) | b;
    }

    // Codifica Unicode dei DXF (\U+XXXX) e codici speciali (%%c, %%d, %%p) per simboli e caratteri
    function formatCADText(s, useUnicode = true) {
      if (!s) return "";
      if (!useUnicode) return cleanCADStr(s);
      return String(s)
        .replace(/Ø/g, "%%c")
        .replace(/ø/g, "%%c")
        .replace(/°/g, "%%d")
        .replace(/±/g, "%%p")
        .replace(/²/g, "\\U+00B2")
        .replace(/³/g, "\\U+00B3")
        .replace(/≥/g, "\\U+2265")
        .replace(/≤/g, "\\U+2264")
        .replace(/Δ/g, "\\U+0394")
        .replace(/\u2014/g, " - ")
        .replace(/\u2013/g, " - ")
        .replace(/\u2012/g, "-")
        .replace(/\u2010/g, "-")
        .replace(/\u2011/g, "-")
        .replace(/•/g, "-")
        .replace(/à/g, "\\U+00E0")
        .replace(/á/g, "\\U+00E1")
        .replace(/è/g, "\\U+00E8")
        .replace(/é/g, "\\U+00E9")
        .replace(/ì/g, "\\U+00EC")
        .replace(/í/g, "\\U+00ED")
        .replace(/ò/g, "\\U+00F2")
        .replace(/ó/g, "\\U+00F3")
        .replace(/ù/g, "\\U+00F9")
        .replace(/ú/g, "\\U+00FA")
        .replace(/À/g, "\\U+00C0")
        .replace(/Á/g, "\\U+00C1")
        .replace(/È/g, "\\U+00C8")
        .replace(/É/g, "\\U+00C9")
        .replace(/Ì/g, "\\U+00CC")
        .replace(/Í/g, "\\U+00CD")
        .replace(/Ò/g, "\\U+00D2")
        .replace(/Ó/g, "\\U+00D3")
        .replace(/Ù/g, "\\U+00D9")
        .replace(/Ú/g, "\\U+00DA")
        // Qualsiasi altro carattere esteso o simbolo viene preservato tramite sequenza standard \U+XXXX
        .replace(/[^\x00-\x7F]/g, ch => "\\U+" + ch.charCodeAt(0).toString(16).toUpperCase().padStart(4, "0"));
    }

    function cleanCADStr(s) {
      return String(s || "")
        .replace(/\u2014/g, " - ")   // Em-dash
        .replace(/\u2013/g, " - ")   // En-dash
        .replace(/•/g, "-")
        .replace(/Ø/g, "%%c")
        .replace(/±/g, "%%p")
        .replace(/°/g, "%%d")
        .replace(/²/g, "^2")
        .replace(/≥/g, ">=")
        .replace(/≤/g, "<=")
        .replace(/Δ/g, "Delta ")
        .replace(/[àáâãä]/g, "a'")
        .replace(/[èéêë]/g, "e'")
        .replace(/[ìíîï]/g, "i'")
        .replace(/[òóôõö]/g, "o'")
        .replace(/[ùúûü]/g, "u'")
        .replace(/[ÀÁÂÃÄ]/g, "A'")
        .replace(/[ÈÉÊË]/g, "E'")
        .replace(/[^\x20-\x7E]/g, "");
    }

    const CAD_LAYERS_SPEC = {
      "0":                  { aci: 7,   tc: 0xffffff, lw: 25, desc: "Default" },
      "Canalina":           { aci: 140, tc: 0x38bdf8, lw: 50, desc: "Profilo e accessori canalina" },
      "Cavi":               { aci: 30,  tc: 0xf59e0b, lw: 35, desc: "Guaine esterne cavi" },
      "Anime_Cavi":         { aci: 40,  tc: 0xfbbf24, lw: 25, desc: "Conduttori e trefoli cordati" },
      "STAFFAGGI":          { aci: 252, tc: 0x94a3b8, lw: 35, desc: "Supporti, mensole, tiranti, PE" },
      "FASCETTE_ANCORAGGIO":{ aci: 4,   tc: 0x06b6d4, lw: 35, desc: "Fascette e bloccacavi amagnetici" },
      "Bandierine":         { aci: 40,  tc: 0xfbbf24, lw: 20, desc: "Linee richiamo e direttrici" },
      "Testi":              { aci: 7,   tc: 0xf8fafc, lw: 25, desc: "Testi tecnici e sigle" },
      "QUOTE":              { aci: 40,  tc: 0xfbbf24, lw: 25, desc: "Linee di quota e valori" },
      "TABELLA":            { aci: 140, tc: 0x38bdf8, lw: 30, desc: "Tabelle distinta cavi e BOM" },
      "CURVA_CARICO":       { aci: 90,  tc: 0x22c55e, lw: 35, desc: "Diagramma di carico EN 61537" },
      "ETICHETTA_PIANTA":   { aci: 80,  tc: 0x4ade80, lw: 35, desc: "Cartellino planimetrico" },
      "VISTA_3D_ISO":       { aci: 251, tc: 0x475569, lw: 30, desc: "Assonometria 3D passo staffe" },
      "CORNICE":            { aci: 251, tc: 0x475569, lw: 60, desc: "Riquadro tavola e cartiglio" },
      "SFONDI_RIEMPIMENTI": { aci: 250, tc: 0x1e293b, lw: 0,  desc: "Sfondi colorati e retini" }
    };

    function getScaleFactor(scaleStr) {
      if (!scaleStr) return 1.0;
      const parts = String(scaleStr).split(":");
      if (parts.length === 2) {
        const num = parseFloat(parts[0]) || 1;
        const den = parseFloat(parts[1]) || 1;
        if (den > 0) return num / den;
      }
      return 1.0;
    }

    function computeExportFileName(st) {
      if (!st) st = state;
      const rawTitle = String(st.title || "").trim();
      const mParen = rawTitle.match(/\(([^)]+)\)/);
      let name = mParen ? mParen[1].trim() : rawTitle;
      name = name.replace(/[^a-zA-Z0-9_-]+/g, "_").replace(/^_+|_+$/g, "");
      return name || "Canalina";
    }

    // ---------------------------------------------------------------------------
    // WRITER DXF
    // R2000 (AC1015, predefinito): scheletro generato da ezdxf (DXF_R2000_TEMPLATE) + entità con
    //   handle, owner, marcatori di sottoclasse, TrueColor e retini HATCH SOLID (regioni piene nei programmi BIM).
    // R12 (AC1009): formato minimo senza handle, riempimenti come SOLID. Massima compatibilità.
    // Ogni entità con coordinate non numeriche viene scartata invece di corrompere il file.
    // ---------------------------------------------------------------------------
    function dxfText(str) {
      // Il file è in Windows-1252 ($DWGCODEPAGE = ANSI_1252): i caratteri Latin-1 (à è ° ² ±...) restano
      // tali, quelli fuori codepage (≥ ≤ Δ...) diventano \U+XXXX, come li scrivono i CAD.
      return String(str == null ? "" : str)
        .replace(/\\U\+00([A-F0-9]{2})/gi, (m, hx) => parseInt(hx, 16) >= 0xA0 ? String.fromCharCode(parseInt(hx, 16)) : m)
        .replace(/[\r\n\t]+/g, " ")
        .replace(/[\x00-\x1f\x7f-\x9f]/g, "")
        .replace(/[^\x00-\xff]/g, ch => "\\U+" + ch.codePointAt(0).toString(16).toUpperCase().padStart(4, "0"))
        .slice(0, 250);
    }

    function createDxfWriter(r2000, scale) {
      const out = [];
      const T = DXF_R2000_TEMPLATE;
      let handle = T.firstHandle;
      const owner = T.modelSpaceOwner;
      const g = (code, val) => { out.push(String(code).padStart(3, " "), String(val)); };
      const f = v => (+v).toFixed(3);
      const ok = (...vals) => vals.every(v => Number.isFinite(+v));
      const S = v => v * scale;

      function head(type, lay, aci, tc) {
        g(0, type);
        if (r2000) { g(5, (handle++).toString(16).toUpperCase()); g(330, owner); g(100, "AcDbEntity"); }
        g(8, lay);
        g(62, aci);
        if (r2000 && Number.isInteger(tc)) g(420, tc);
      }

      return {
        line(lay, x1, y1, x2, y2, aci, tc) {
          if (!ok(x1, y1, x2, y2)) return;
          head("LINE", lay, aci, tc);
          if (r2000) g(100, "AcDbLine");
          g(10, f(S(x1))); g(20, f(S(y1))); g(30, "0.0");
          g(11, f(S(x2))); g(21, f(S(y2))); g(31, "0.0");
        },
        circle(lay, cx, cy, r, aci, tc) {
          if (!ok(cx, cy, r) || r <= 0) return;
          head("CIRCLE", lay, aci, tc);
          if (r2000) g(100, "AcDbCircle");
          g(10, f(S(cx))); g(20, f(S(cy))); g(30, "0.0");
          g(40, f(S(r)));
        },
        text(lay, x, y, h, str, style, hAlign, aci, tc) {
          const txt = dxfText(str);
          if (!txt.trim() || !ok(x, y, h)) return;
          head("TEXT", lay, aci, tc);
          if (r2000) g(100, "AcDbText");
          g(10, f(S(x))); g(20, f(S(y))); g(30, "0.0");
          g(40, Math.max(0.0001, S(h)).toFixed(5));
          g(1, txt);
          g(41, "1.0");
          g(7, style);
          if (hAlign) {
            g(72, hAlign);
            g(11, f(S(x))); g(21, f(S(y))); g(31, "0.0");
          }
          if (r2000) g(100, "AcDbText");
        },
        // Quadrilatero pieno (p1..p4 in ordine di contorno)
        fillQuad(lay, x1, y1, x2, y2, x3, y3, x4, y4, aci, tc) {
          if (!ok(x1, y1, x2, y2, x3, y3, x4, y4)) return;
          if (r2000) {
            this._hatch(lay, [[x1, y1], [x2, y2], [x3, y3], [x4, y4]], false, aci, tc);
            return;
          }
          head("SOLID", lay, aci, tc);
          // SOLID usa l'ordine "a zig-zag": 1, 2, 4, 3
          g(10, f(S(x1))); g(20, f(S(y1))); g(30, "0.0");
          g(11, f(S(x2))); g(21, f(S(y2))); g(31, "0.0");
          g(12, f(S(x4))); g(22, f(S(y4))); g(32, "0.0");
          g(13, f(S(x3))); g(23, f(S(y3))); g(33, "0.0");
        },
        fillDisk(lay, cx, cy, r, aci, tc) {
          if (!ok(cx, cy, r) || r <= 0) return;
          if (r2000) {
            // Due semicerchi con bulge 1 (180°)
            this._hatch(lay, [[cx - r, cy, 1], [cx + r, cy, 1]], true, aci, tc);
            return;
          }
          const steps = r > 10 ? 24 : r > 4 ? 16 : 12;
          for (let i = 0; i < steps; i += 2) {
            const a = k => ((i + k) * 2 * Math.PI) / steps;
            this.fillQuad(lay, cx, cy,
              cx + Math.cos(a(0)) * r, cy + Math.sin(a(0)) * r,
              cx + Math.cos(a(1)) * r, cy + Math.sin(a(1)) * r,
              cx + Math.cos(a(2)) * r, cy + Math.sin(a(2)) * r, aci, tc);
          }
        },
        _hatch(lay, verts, withBulge, aci, tc) {
          head("HATCH", lay, aci, tc);
          g(100, "AcDbHatch");
          g(10, "0.0"); g(20, "0.0"); g(30, "0.0");
          g(210, "0.0"); g(220, "0.0"); g(230, "1.0");
          g(2, "SOLID");
          g(70, 1);            // riempimento pieno
          g(71, 0);            // non associativo
          g(91, 1);            // un contorno
          g(92, 3);            // contorno esterno + polilinea
          g(72, withBulge ? 1 : 0);
          g(73, 1);            // chiuso
          g(93, verts.length);
          verts.forEach(v => {
            g(10, f(S(v[0]))); g(20, f(S(v[1])));
            if (withBulge) g(42, (v[2] || 0).toFixed(6));
          });
          g(97, 0);            // nessun oggetto di contorno associato
          g(75, 1);            // stile: solo esterno
          g(76, 1);            // motivo predefinito
          g(98, 0);            // nessun punto seme
        },
        finish(bounds, title) {
          const body = out.join("\n") + "\n";
          let doc;
          if (r2000) {
            doc = T.head
              .replace("@@HANDSEED@@", (handle + 1).toString(16).toUpperCase())
              .replace("@@EXTMINX@@", f(S(bounds.minX))).replace("@@EXTMINY@@", f(S(bounds.minY)))
              .replace("@@EXTMAXX@@", f(S(bounds.maxX))).replace("@@EXTMAXY@@", f(S(bounds.maxY)))
              + body + T.tail;
          } else {
            doc = r12Header(bounds, scale) + body + "  0\nENDSEC\n  0\nEOF\n";
          }
          return doc.replace(/\r?\n/g, "\r\n");
        }
      };
    }

    // Intestazione e tabelle R12 (AC1009): nessun handle, solo ciò che R12 prevede
    function r12Header(bounds, scale) {
      const L = [];
      const g = (c, v) => L.push(String(c).padStart(3, " "), String(v));
      g(0, "SECTION"); g(2, "HEADER");
      g(9, "$ACADVER"); g(1, "AC1009");
      g(9, "$DWGCODEPAGE"); g(3, "ANSI_1252");
      g(9, "$INSBASE"); g(10, "0.0"); g(20, "0.0"); g(30, "0.0");
      g(9, "$EXTMIN"); g(10, (bounds.minX * scale).toFixed(3)); g(20, (bounds.minY * scale).toFixed(3)); g(30, "0.0");
      g(9, "$EXTMAX"); g(10, (bounds.maxX * scale).toFixed(3)); g(20, (bounds.maxY * scale).toFixed(3)); g(30, "0.0");
      g(0, "ENDSEC");
      g(0, "SECTION"); g(2, "TABLES");
      g(0, "TABLE"); g(2, "LTYPE"); g(70, 1);
      g(0, "LTYPE"); g(2, "CONTINUOUS"); g(70, 0); g(3, "Solid line"); g(72, 65); g(73, 0); g(40, "0.0");
      g(0, "ENDTAB");
      const layers = Object.entries(CAD_LAYERS_SPEC);
      g(0, "TABLE"); g(2, "LAYER"); g(70, layers.length);
      layers.forEach(([name, cfg]) => { g(0, "LAYER"); g(2, name); g(70, 0); g(62, cfg.aci || 7); g(6, "CONTINUOUS"); });
      g(0, "ENDTAB");
      const styles = [["STANDARD", "txt"], ["ARIAL", "arial.ttf"], ["MONO_CAD", "isocp.shx"], ["TITLE_CAD", "arial.ttf"]];
      g(0, "TABLE"); g(2, "STYLE"); g(70, styles.length);
      styles.forEach(([name, font]) => { g(0, "STYLE"); g(2, name); g(70, 0); g(40, "0.0"); g(41, "1.0"); g(50, "0.0"); g(71, 0); g(42, "2.5"); g(3, font); g(4, ""); });
      g(0, "ENDTAB");
      g(0, "ENDSEC");
      g(0, "SECTION"); g(2, "BLOCKS"); g(0, "ENDSEC");
      g(0, "SECTION"); g(2, "ENTITIES");
      return L.join("\n") + "\n";
    }

    // Converte il testo DXF (solo caratteri <= U+00FF, garantito da dxfText) in byte Windows-1252 / Latin-1
    function dxfToBlob(text) {
      const bytes = new Uint8Array(text.length);
      for (let i = 0; i < text.length; i++) {
        const c = text.charCodeAt(i);
        bytes[i] = c <= 0xff ? c : 0x3f;
      }
      return new Blob([bytes], { type: "application/dxf" });
    }

    function hexToTrueColorOrNull(hex) {
      return (hex && /^#[0-9a-f]{6}$/i.test(hex)) ? parseInt(hex.slice(1), 16) : null;
    }

    function generateDXFStringForCurrentState(fileTitle) {
      const { prims, bounds } = buildCADScene();
      const s = getScaleFactor(state.scale);
      const r2000 = exportDxfOptions.format !== "AC1009";
      const W = createDxfWriter(r2000, s);

      // 1) Riempimenti (sotto i contorni)
      prims.forEach(p => {
        if (!p.fill) return;
        const isContainerBoxBg = p.t === "rect" &&
          (p.fill === "#0f172a" || p.fill === "#04070d" || p.fill === "#1e293b") &&
          (p.layer === "TABELLA" || p.layer === "VISTA_3D_ISO" || p.layer === "CURVA_CARICO" || p.layer === "CORNICE");
        if (isContainerBoxBg) return;

        const aci = hexToClosestACI(p.fill);
        const tc = hexToTrueColorOrNull(p.fill);
        const lay = (p.layer === "Cavi" || p.layer === "Anime_Cavi" || p.layer === "Canalina" || p.layer === "VISTA_3D_ISO") ? p.layer : "SFONDI_RIEMPIMENTI";

        if (p.t === "rect") {
          W.fillQuad(lay, p.x, p.y, p.x + p.w, p.y, p.x + p.w, p.y + p.h, p.x, p.y + p.h, aci, tc);
        } else if (p.t === "circle") {
          W.fillDisk(lay, p.cx, p.cy, p.r, aci, tc);
        } else if (p.t === "poly" && p.pts && p.pts.length >= 3) {
          const P = p.pts;
          if (P.length === 3) {
            W.fillQuad(lay, P[0].x, P[0].y, P[1].x, P[1].y, P[2].x, P[2].y, P[2].x, P[2].y, aci, tc);
          } else if (P.length === 4) {
            W.fillQuad(lay, P[0].x, P[0].y, P[1].x, P[1].y, P[2].x, P[2].y, P[3].x, P[3].y, aci, tc);
          } else {
            for (let i = 1; i < P.length - 1; i++) {
              W.fillQuad(lay, P[0].x, P[0].y, P[i].x, P[i].y, P[i + 1].x, P[i + 1].y, P[i + 1].x, P[i + 1].y, aci, tc);
            }
          }
        }
      });

      // 2) Contorni e testi
      prims.forEach(p => {
        const lay = CAD_LAYERS_SPEC[p.layer] ? p.layer : "0";
        const aci = hexToClosestACI(p.color);
        const tc = hexToTrueColorOrNull(p.color);

        if (p.t === "line") {
          W.line(lay, p.x1, p.y1, p.x2, p.y2, aci, tc);
        } else if (p.t === "rect") {
          const x1 = p.x, y1 = p.y, x2 = p.x + p.w, y2 = p.y + p.h;
          W.line(lay, x1, y1, x2, y1, aci, tc);
          W.line(lay, x2, y1, x2, y2, aci, tc);
          W.line(lay, x2, y2, x1, y2, aci, tc);
          W.line(lay, x1, y2, x1, y1, aci, tc);
        } else if (p.t === "circle") {
          W.circle(lay, p.cx, p.cy, p.r, aci, tc);
        } else if (p.t === "poly" && p.pts && p.pts.length >= 2) {
          for (let i = 0; i < p.pts.length; i++) {
            const a = p.pts[i], b = p.pts[(i + 1) % p.pts.length];
            W.line(lay, a.x, a.y, b.x, b.y, aci, tc);
          }
        } else if (p.t === "text") {
          const str = formatCADText(p.str);
          const isTitle = (p.layer === "CORNICE" && p.h >= 2.3) ||
                          p.layer === "ETICHETTA_PIANTA" ||
                          (p.layer === "TABELLA" && /^[0-9]\./.test(p.str)) ||
                          (p.layer === "CURVA_CARICO" && /^[0-9]\./.test(p.str)) ||
                          (p.layer === "VISTA_3D_ISO" && p.h >= 2.2);
          const hAlign = p.align === "center" ? 1 : p.align === "right" ? 2 : 0;
          W.text(lay, p.x, p.y, p.h || 2.2, str, isTitle ? "TITLE_CAD" : "ARIAL", hAlign, aci, tc);
        }
      });

      return W.finish(bounds, fileTitle || state.title);
    }

    function exportCadDXF() {
      const name = (document.getElementById("exportName").value || computeExportFileName(state) || "Canalina_1_1").trim().replace(/\.(dxf|dwg)$/i, "");
      const dxfContent = generateDXFStringForCurrentState(name);
      const blob = dxfToBlob(dxfContent);
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = `${name}.dxf`;
      document.body.appendChild(a);
      a.click();
      setTimeout(() => { document.body.removeChild(a); URL.revokeObjectURL(a.href); }, 150);
    }

    function exportAllSectionsDXF() {
      syncActiveSectionToMultiStore();
      if (!projectSections || projectSections.length === 0) {
        exportCadDXF();
        return;
      }
      syncExportOptionsFromUI();
      const exportOptsToApply = {
        showLoadChart: state.showLoadChart !== false,
        showBOM: state.showBOM !== false,
        showIso3D: state.showIso3D !== false,
        showCartiglio: state.showCartiglio !== false,
        showTableDesc: state.showTableDesc !== false && !state.hideTableDesc,
        hideTableDesc: Boolean(state.hideTableDesc),
        layoutMode: state.layoutMode || "standard",
        showCableCallouts: state.showCableCallouts !== false,
        textScale: typeof state.textScale === "number" && state.textScale > 0 ? state.textScale : 1.0,
        scale: state.scale || "1:1"
      };

      const originalIdx = currentSectionIdx;
      const originalState = cloneStateSnapshot(state);
      const count = projectSections.length;
      
      const banner = document.getElementById("statusBanner");
      if (banner) {
        banner.textContent = `Esportazione batch di ${count} canaline (.DXF) in corso...`;
        banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-amber-500/20 text-amber-300 border border-amber-500/40 font-bold";
      }

      for (let i = 0; i < projectSections.length; i++) {
        currentSectionIdx = i;
        state = cloneStateSnapshot(projectSections[i]);
        Object.assign(state, exportOptsToApply);
        recalculateAll();
        const fileBase = computeExportFileName(state) || `Canalina_${i + 1}`;
        const dxfContent = generateDXFStringForCurrentState(fileBase);
        
        const blob = dxfToBlob(dxfContent);
        const a = document.createElement("a");
        a.href = URL.createObjectURL(blob);
        a.download = `${fileBase}.dxf`;
        document.body.appendChild(a);
        a.click();
        setTimeout(() => {
          document.body.removeChild(a);
          URL.revokeObjectURL(a.href);
        }, 150);

        if (i < projectSections.length - 1) {
          new Promise(r => setTimeout(r, 350));
        }
      }

      currentSectionIdx = originalIdx;
      state = cloneStateSnapshot(originalState);
      pushStateToUI();
      recalculateAll();
      fitAll();

      if (banner) {
        banner.textContent = `✓ Esportate con successo tutte le ${count} canaline in file .DXF separati!`;
        banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-emerald-500/20 text-emerald-300 border border-emerald-500/40 font-bold";
      }
    }

    function generateLispStringForCurrentState(fileTitle) {
      const { prims } = buildCADScene();
      const s = getScaleFactor(state.scale);

      // Uno script .SCR viene letto dal CAD come tasti premuti, riga per riga. Per questo:
      // - nessuna riga vuota (equivale a INVIO e ripeterebbe l'ultimo comando dell'utente);
      // - nessun comando interattivo (STYLE/LAYER fanno domande che variano per font e versione
      //   e lasciavano il CAD in attesa): stili e layer sono creati con entmake;
      // - ogni riga è un'espressione Lisp completa, così un errore non blocca le righe seguenti.
      let scr = `;; CanaloCAD ${APP_VERSION_LABEL} - sezione canalina 1:1. Trascina il file nel disegno o usa il comando SCRIPT.\n`;
      scr += `(setq ccad:oldecho (getvar "CMDECHO") ccad:oldos (getvar "OSMODE"))\n`;
      scr += `(setvar "CMDECHO" 0)\n`;
      scr += `(setvar "OSMODE" 0)\n`;
      // ccad:e crea un'entità senza stampare la lista restituita (molto più veloce su disegni grandi)
      scr += `(defun ccad:e (l) (entmake l) (princ))\n`;

      const styleRec = (name, font) =>
        `(if (not (tblsearch "STYLE" "${name}")) (entmake '((0 . "STYLE") (100 . "AcDbSymbolTableRecord") (100 . "AcDbTextStyleTableRecord") (2 . "${name}") (70 . 0) (40 . 0.0) (41 . 1.0) (50 . 0.0) (71 . 0) (42 . 2.5) (3 . "${font}") (4 . ""))))\n`;
      scr += styleRec("MONO_CAD", "isocp.shx");
      scr += styleRec("TITLE_CAD", "arial.ttf");

      Object.entries(CAD_LAYERS_SPEC).forEach(([lay, cfg]) => {
        if (lay === "0") return; // il layer 0 esiste sempre
        scr += `(if (not (tblsearch "LAYER" "${lay}")) (entmake '((0 . "LAYER") (100 . "AcDbSymbolTableRecord") (100 . "AcDbLayerTableRecord") (2 . "${lay}") (70 . 0) (62 . ${cfg.aci || 7}) (6 . "Continuous") (370 . ${cfg.lw != null ? cfg.lw : 25}))))\n`;
      });

      const emitSolidTri = (lay, x1, y1, x2, y2, x3, y3, hex) => {
        const aci = hexToClosestACI(hex);
        const rgb = hexToRGB(hex);
        const tcStr = rgb ? ` '(420 . ${(rgb.r << 16) + (rgb.g << 8) + rgb.b})` : "";
        scr += `(ccad:e (list '(0 . "SOLID") '(8 . "${lay}") '(62 . ${aci})${tcStr} '(10 ${(x1 * s).toFixed(3)} ${(y1 * s).toFixed(3)} 0.0) '(11 ${(x2 * s).toFixed(3)} ${(y2 * s).toFixed(3)} 0.0) '(12 ${(x3 * s).toFixed(3)} ${(y3 * s).toFixed(3)} 0.0) '(13 ${(x3 * s).toFixed(3)} ${(y3 * s).toFixed(3)} 0.0)))\n`;
      };

      const emitSolidQuad = (lay, x1, y1, x2, y2, x3, y3, x4, y4, hex) => {
        const aci = hexToClosestACI(hex);
        const rgb = hexToRGB(hex);
        const tcStr = rgb ? ` '(420 . ${(rgb.r << 16) + (rgb.g << 8) + rgb.b})` : "";
        scr += `(ccad:e (list '(0 . "SOLID") '(8 . "${lay}") '(62 . ${aci})${tcStr} '(10 ${(x1 * s).toFixed(3)} ${(y1 * s).toFixed(3)} 0.0) '(11 ${(x2 * s).toFixed(3)} ${(y2 * s).toFixed(3)} 0.0) '(12 ${(x4 * s).toFixed(3)} ${(y4 * s).toFixed(3)} 0.0) '(13 ${(x3 * s).toFixed(3)} ${(y3 * s).toFixed(3)} 0.0)))\n`;
      };

      prims.forEach(p => {
        if (!p.fill) return;
        const isContainerBoxBg = p.t === "rect" && (p.fill === "#0f172a" || p.fill === "#04070d" || p.fill === "#1e293b") && (p.layer === "TABELLA" || p.layer === "VISTA_3D_ISO" || p.layer === "CURVA_CARICO" || p.layer === "CORNICE");
        if (!exportDxfOptions.includeBackgrounds && isContainerBoxBg) return;
        const fillLay = (p.layer === "Cavi" || p.layer === "Anime_Cavi" || p.layer === "Canalina" || p.layer === "VISTA_3D_ISO") ? p.layer : "SFONDI_RIEMPIMENTI";
        if (p.t === "rect") {
          emitSolidQuad(fillLay, p.x, p.y, p.x + p.w, p.y, p.x + p.w, p.y + p.h, p.x, p.y + p.h, p.fill);
        } else if (p.t === "circle") {
          const steps = p.r > 10 ? 20 : p.r > 4 ? 14 : 10;
          for (let i = 0; i < steps; i++) {
            const a1 = (i * 2 * Math.PI) / steps;
            const a2 = ((i + 1) * 2 * Math.PI) / steps;
            emitSolidTri(
              fillLay,
              p.cx, p.cy,
              p.cx + Math.cos(a1) * p.r, p.cy + Math.sin(a1) * p.r,
              p.cx + Math.cos(a2) * p.r, p.cy + Math.sin(a2) * p.r,
              p.fill
            );
          }
        } else if (p.t === "poly" && p.fill && p.pts && p.pts.length >= 3) {
          if (p.pts.length === 3) {
            emitSolidTri(fillLay, p.pts[0].x, p.pts[0].y, p.pts[1].x, p.pts[1].y, p.pts[2].x, p.pts[2].y, p.fill);
          } else if (p.pts.length === 4) {
            emitSolidQuad(fillLay, p.pts[0].x, p.pts[0].y, p.pts[1].x, p.pts[1].y, p.pts[2].x, p.pts[2].y, p.pts[3].x, p.pts[3].y, p.fill);
          } else {
            for (let i = 1; i < p.pts.length - 1; i++) {
              emitSolidTri(fillLay, p.pts[0].x, p.pts[0].y, p.pts[i].x, p.pts[i].y, p.pts[i + 1].x, p.pts[i + 1].y, p.fill);
            }
          }
        }
      });

      prims.forEach(p => {
        const lay = CAD_LAYERS_SPEC[p.layer] ? p.layer : "0";
        const aci = hexToClosestACI(p.color);
        const rgb = hexToRGB(p.color);
        const tcStr = rgb ? ` '(420 . ${(rgb.r << 16) + (rgb.g << 8) + rgb.b})` : "";

        if (p.t === "line") {
          scr += `(ccad:e (list '(0 . "LINE") '(8 . "${lay}") '(62 . ${aci})${tcStr} '(370 . 25) '(10 ${(p.x1 * s).toFixed(3)} ${(p.y1 * s).toFixed(3)} 0.0) '(11 ${(p.x2 * s).toFixed(3)} ${(p.y2 * s).toFixed(3)} 0.0)))\n`;
        } else if (p.t === "rect") {
          const x1 = p.x, y1 = p.y, x2 = p.x + p.w, y2 = p.y + p.h;
          scr += `(ccad:e (list '(0 . "LINE") '(8 . "${lay}") '(62 . ${aci})${tcStr} '(370 . 25) '(10 ${(x1 * s).toFixed(3)} ${(y1 * s).toFixed(3)} 0.0) '(11 ${(x2 * s).toFixed(3)} ${(y1 * s).toFixed(3)} 0.0)))\n`;
          scr += `(ccad:e (list '(0 . "LINE") '(8 . "${lay}") '(62 . ${aci})${tcStr} '(370 . 25) '(10 ${(x2 * s).toFixed(3)} ${(y1 * s).toFixed(3)} 0.0) '(11 ${(x2 * s).toFixed(3)} ${(y2 * s).toFixed(3)} 0.0)))\n`;
          scr += `(ccad:e (list '(0 . "LINE") '(8 . "${lay}") '(62 . ${aci})${tcStr} '(370 . 25) '(10 ${(x2 * s).toFixed(3)} ${(y2 * s).toFixed(3)} 0.0) '(11 ${(x1 * s).toFixed(3)} ${(y2 * s).toFixed(3)} 0.0)))\n`;
          scr += `(ccad:e (list '(0 . "LINE") '(8 . "${lay}") '(62 . ${aci})${tcStr} '(370 . 25) '(10 ${(x1 * s).toFixed(3)} ${(y2 * s).toFixed(3)} 0.0) '(11 ${(x1 * s).toFixed(3)} ${(y1 * s).toFixed(3)} 0.0)))\n`;
        } else if (p.t === "circle") {
          scr += `(ccad:e (list '(0 . "CIRCLE") '(8 . "${lay}") '(62 . ${aci})${tcStr} '(370 . 25) '(10 ${(p.cx * s).toFixed(3)} ${(p.cy * s).toFixed(3)} 0.0) '(40 . ${(p.r * s).toFixed(3)})))\n`;
        } else if (p.t === "poly" && p.pts && p.pts.length >= 2) {
          for (let i = 0; i < p.pts.length; i++) {
            const pt1 = p.pts[i];
            const pt2 = p.pts[(i + 1) % p.pts.length];
            scr += `(ccad:e (list '(0 . "LINE") '(8 . "${lay}") '(62 . ${aci})${tcStr} '(370 . 25) '(10 ${(pt1.x * s).toFixed(3)} ${(pt1.y * s).toFixed(3)} 0.0) '(11 ${(pt2.x * s).toFixed(3)} ${(pt2.y * s).toFixed(3)} 0.0)))\n`;
          }
        } else if (p.t === "text") {
          // Un a-capo dentro il testo spezzerebbe la riga dello script: lo sostituisco con uno spazio
          const sText = formatCADText(String(p.str || "").replace(/[\r\n\t]+/g, " "), exportDxfOptions.includeUnicode).replace(/\\/g, "\\\\").replace(/"/g, '\\"');
          if (!sText) return;
          const th = Math.max(0.0001, +(((p.h || 2.2) * 0.88 * s)).toFixed(5));
          const isTitle = (p.layer === "CORNICE" && p.h >= 2.3) ||
                          p.layer === "ETICHETTA_PIANTA" ||
                          (p.layer === "TABELLA" && /^[0-9]\./.test(p.str)) ||
                          (p.layer === "CURVA_CARICO" && /^[0-9]\./.test(p.str)) ||
                          (p.layer === "VISTA_3D_ISO" && p.h >= 2.2);
          const styleName = isTitle ? "TITLE_CAD" : "MONO_CAD";
          const hAlign = p.align === "center" ? 1 : p.align === "right" ? 2 : 0;
          const alignStr = hAlign !== 0 ? ` '(72 . ${hAlign}) '(11 ${(p.x * s).toFixed(3)} ${(p.y * s).toFixed(3)} 0.0) '(73 . 0)` : "";
          scr += `(ccad:e (list '(0 . "TEXT") '(8 . "${lay}") '(7 . "${styleName}") '(62 . ${aci})${tcStr} '(370 . 25) '(10 ${(p.x * s).toFixed(3)} ${(p.y * s).toFixed(3)} 0.0) '(40 . ${th.toFixed(5)}) '(41 . 1.0) '(1 . "${sText}")${alignStr}))\n`;
        }
      });
      scr += `(setvar "OSMODE" ccad:oldos)\n`;
      scr += `(setvar "CMDECHO" ccad:oldecho)\n`;
      scr += `(command "_.ZOOM" "_E")\n`;
      scr += `(princ "\\nCanaloCAD: sezione disegnata.")\n`;
      return scr;
    }

    function exportLispDWG() {
      const name = (document.getElementById("exportName").value || computeExportFileName(state) || "Canalina_1_1").trim().replace(/\.(dxf|dwg|scr)$/i, "");
      const scr = generateLispStringForCurrentState(name);
      const blob = new Blob([scr], { type: "text/plain" });
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = `${name}_TrascinaNelCAD.scr`;
      document.body.appendChild(a);
      a.click();
      setTimeout(() => { document.body.removeChild(a); URL.revokeObjectURL(a.href); }, 150);
    }

    function exportAllSectionsLisp() {
      syncActiveSectionToMultiStore();
      if (!projectSections || projectSections.length === 0) {
        exportLispDWG();
        return;
      }
      syncExportOptionsFromUI();
      const exportOptsToApply = {
        showLoadChart: state.showLoadChart !== false,
        showBOM: state.showBOM !== false,
        showIso3D: state.showIso3D !== false,
        showCartiglio: state.showCartiglio !== false,
        showTableDesc: state.showTableDesc !== false && !state.hideTableDesc,
        hideTableDesc: Boolean(state.hideTableDesc),
        layoutMode: state.layoutMode || "standard",
        showCableCallouts: state.showCableCallouts !== false,
        textScale: typeof state.textScale === "number" && state.textScale > 0 ? state.textScale : 1.0,
        scale: state.scale || "1:1"
      };

      const originalIdx = currentSectionIdx;
      const originalState = cloneStateSnapshot(state);
      const count = projectSections.length;
      
      const banner = document.getElementById("statusBanner");
      if (banner) {
        banner.textContent = `Esportazione batch di ${count} canaline (.SCR) in corso...`;
        banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-amber-500/20 text-amber-300 border border-amber-500/40 font-bold";
      }

      for (let i = 0; i < projectSections.length; i++) {
        currentSectionIdx = i;
        state = cloneStateSnapshot(projectSections[i]);
        Object.assign(state, exportOptsToApply);
        recalculateAll();
        const fileBase = computeExportFileName(state) || `Canalina_${i + 1}`;
        const scrContent = generateLispStringForCurrentState(fileBase);
        
        const blob = new Blob([scrContent], { type: "text/plain" });
        const a = document.createElement("a");
        a.href = URL.createObjectURL(blob);
        a.download = `${fileBase}_TrascinaNelCAD.scr`;
        document.body.appendChild(a);
        a.click();
        setTimeout(() => {
          document.body.removeChild(a);
          URL.revokeObjectURL(a.href);
        }, 150);

        if (i < projectSections.length - 1) {
          new Promise(r => setTimeout(r, 350));
        }
      }

      currentSectionIdx = originalIdx;
      state = cloneStateSnapshot(originalState);
      pushStateToUI();
      recalculateAll();
      fitAll();

      if (banner) {
        banner.textContent = `✓ Esportate con successo tutte le ${count} canaline in file .SCR separati!`;
        banner.className = "mono text-xs px-2.5 py-0.5 rounded bg-emerald-500/20 text-emerald-300 border border-emerald-500/40 font-bold";
      }
    }

    function exportWordReport() {
      syncActiveSectionToMultiStore();
      const name = (document.getElementById("exportName").value || "Relazione").trim();
      const e = escapeHtml;
      const html = `<html><head><meta charset="utf-8"></head><body style="font-family:Arial">
        <h2>RELAZIONE TECNICA DI POSA: ${e(state.title)}</h2>
        <p>Dimensioni: L=${state.width}mm x H=${state.height}mm | Riempimento: ${metrics.fillPct}% (Riserva +${metrics.sparePct}%) | Derating CEI 35024 Kr=${metrics.kr} | Segregazione 64-8: ${metrics.segConform ? "CONFORME" : "NON CONFORME"}</p>
        <table border="1" cellpadding="5" style="border-collapse:collapse;width:100%">
          <tr><th>Pos</th><th>Sigla</th><th>Descrizione</th><th>Tipo</th><th>Formazione</th><th>Ø (mm)</th></tr>
          ${state.cables.map((c, i) => `<tr><td>#${i + 1}</td><td>${e(c.code)}</td><td>${e(c.desc)}</td><td>${e(c.type)}</td><td>${e(c.form)}</td><td>${e(c.d)}</td></tr>`).join("")}
        </table>
        <p style="font-size:9pt;color:#555">Generato con ${APP_NAME} ${APP_VERSION_LABEL}. Portate, frecce e coefficienti sono stime di calcolo: verificarle con i dati del costruttore.</p>
        </body></html>`;
      downloadBlob(new Blob(["\ufeff", html], { type: "application/msword" }), `${name}.doc`);
    }

    function exportCSV() {
      const name = (document.getElementById("exportName").value || "Computo_BOM").trim();
      const rows = [["CODICE", "DESCRIZIONE", "QUANTITA", "UM", "NOTE"]];
      metrics.bom.forEach(b => rows.push([b.code, b.desc, String(b.qty).replace(".", ","), b.um, b.note]));
      const csv = rows.map(r => r.map(csvField).join(";")).join("\r\n") + "\r\n";
      downloadBlob(new Blob(["\ufeff" + csv], { type: "text/csv;charset=utf-8" }), `${name}.csv`);
    }

