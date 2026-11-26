"""Prova del connettore dell'Assistente IA nel pacchetto Flatpak, senza schermo (GitHub Actions).

CartesioCAD parte fuori schermo con un disegno e il connettore attivo; questo script fa la parte di Claude Code:
avvia il connettore come lo avvia Claude Code (flatpak run --command=cartesio-mcp, un'altra istanza della sandbox)
e gli parla in MCP (JSON-RPC su stdin/stdout, una riga per messaggio). Poi, se "flatpak enter" funziona, prova
anche dall'interno della stessa istanza: il connettore giusto passa, una sua copia in un'altra cartella no.

uso: python3 prova_connettore_flatpak.py <cartella di lavoro>
"""
import json, os, subprocess, sys, time
from pathlib import Path

APP = "io.github.samudipi.CartesioCAD"
work = Path(sys.argv[1]).resolve()
work.mkdir(parents=True, exist_ok=True)
failures = []


def check(cond, label):
    print(("OK   " if cond else "ERR  ") + label, flush=True)
    if not cond:
        failures.append(label)


class Bridge:
    def __init__(self, command):
        self.p = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.n = 0
        init = self.rpc("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                       "clientInfo": {"name": "prova", "version": "1"}})
        self.info = init.get("result", {}).get("serverInfo", {})

    def rpc(self, method, params):
        self.n += 1
        self.p.stdin.write((json.dumps({"jsonrpc": "2.0", "id": self.n, "method": method, "params": params}) + "\n").encode())
        self.p.stdin.flush()
        line = self.p.stdout.readline()
        if not line:
            raise RuntimeError("il connettore si è chiuso: " + self.p.stderr.read().decode("utf-8", "replace")[-2000:])
        return json.loads(line)

    def tool(self, name, arguments=None):
        r = self.rpc("tools/call", {"name": name, "arguments": arguments or {}})["result"]
        return r.get("isError", False), r["content"][0]["text"]

    def close(self):
        try:
            self.p.stdin.close()
            self.p.wait(10)
        except Exception:
            self.p.kill()


dxf = work / "pianta.dxf"
dxf.write_text("\n".join(["0", "SECTION", "2", "ENTITIES", "0", "LINE", "8", "Muri", "10", "0", "20", "0", "11", "10",
                          "21", "0", "0", "ENDSEC", "0", "EOF"]) + "\n", encoding="utf-8")
script = work / "assistente.scr"
script.write_text(f'LOGFILEON {work}/assistente.txt\nOPEN "{dxf}"\n-ASSISTENTEATTIVA AUTO\n', encoding="utf-8")
args = [f"--filesystem={work}", "--env=QT_QPA_PLATFORM=offscreen", "--env=QT_QUICK_BACKEND=software",
        "--env=CARTESIO_RENDER=software", "--env=CARTESIO_LANG=it"]
app = subprocess.Popen(["flatpak", "run", *args, APP, str(script)], stdout=open(work / "assistente.log", "wb"),
                       stderr=subprocess.STDOUT)
try:
    # 1. Come Claude Code: un'altra istanza della sandbox (il processo del connettore da CartesioCAD non si vede).
    deadline = time.time() + 120
    text = ""
    while True:
        try:
            b = Bridge(["flatpak", "run", "--command=cartesio-mcp", APP])
            err, text = b.tool("disegno_info")
            b.close()
            if not err and "pianta" in text:
                break
        except Exception as e:   # noqa: BLE001 - si riprova finché CartesioCAD non è pronto
            text = str(e)
        if app.poll() is not None or time.time() > deadline:
            break
        time.sleep(2)
    print("      ", text[:300])
    check("pianta" in text, "connettore da flatpak run (come Claude Code): legge il disegno")

    b = Bridge(["flatpak", "run", "--command=cartesio-mcp", APP])
    check(b.info.get("name") == "cartesiocad", "nome del connettore")
    tools = b.rpc("tools/list", {})["result"]["tools"]
    check(any(t["name"] == "proponi_modifiche" for t in tools), f"elenco degli strumenti ({len(tools)})")
    err, text = b.tool("proponi_modifiche", {"operazioni": [{"op": "crea_layer", "nome": "Prova-Linux"}],
                                             "descrizione": "prova"})
    print("      ", text[:300])
    check(not err, "proposta applicata (conferma automatica della prova)")
    err, text = b.tool("disegno_info")
    check('"layer":3' in text.replace(" ", ""), "il layer nuovo c'è (3 layer)")
    b.close()

    # 2. Dentro la stessa istanza (processo visibile): passa solo cartesio-mcp della cartella del programma.
    ps = subprocess.run(["flatpak", "ps", "--columns=child-pid,application"], capture_output=True, text=True).stdout
    instance = next((line.split()[0] for line in ps.splitlines() if APP in line), None)
    probe = subprocess.run(["flatpak", "enter", instance or "?", "true"], capture_output=True, text=True) if instance else None
    if not probe or probe.returncode != 0:
        print("SKIP flatpak enter non disponibile qui:", (probe.stderr.strip() if probe else "istanza non trovata"))
    else:
        b = Bridge(["flatpak", "enter", instance, "/app/bin/cartesio-mcp"])
        err, text = b.tool("disegno_info")
        b.close()
        check(not err and "pianta" in text, "stessa istanza, connettore giusto: legge il disegno")
        copy = f"{work}/altro-mcp"
        b = Bridge(["flatpak", "enter", instance, "sh", "-c", f"cp /app/lib/cartesiocad/cartesio-mcp {copy} && exec {copy}"])
        err, text = b.tool("disegno_info")
        b.close()
        print("      ", text[:300])
        check(err and "rifiutato" in text and str(Path.home()) not in text, "copia del connettore in un'altra cartella: rifiutata")
finally:
    app.terminate()
    try:
        app.wait(20)
    except subprocess.TimeoutExpired:
        app.kill()
    for name in ("assistente.txt", "assistente.log"):
        f = work / name
        if f.exists():
            print(f"--- {name}")
            print(f.read_text("utf-8", "replace")[-3000:])

print("\nRISULTATO:", "tutto ok" if not failures else f"{len(failures)} errori: {failures}")
sys.exit(1 if failures else 0)
