"""Genera l'icona di Windows di CartesioCAD (src/app/icons/cartesiocad.ico) dal logo disegnato in Icons.cpp
(Icon::AppLogo: arco aperto a destra e mirino, su un riquadro arrotondato del colore principale dell'interfaccia).

Uso:  uv run --with pillow python scripts/genera-icona.py
"""
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "src" / "app" / "icons" / "cartesiocad.ico"
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
TOP, BOTTOM = (0x4A, 0x9B, 0xF0), (0x24, 0x68, 0xC0)   # sfumatura del colore principale (#3b8eea)


def tile(size: int) -> Image.Image:
    k = 8                       # disegno 8 volte più grande, poi ridotto: bordi morbidi
    n = size * k
    u = n / 32.0                # unità del disegno originale (32 x 32)
    img = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    # riquadro arrotondato con sfumatura verticale
    mask = Image.new("L", (n, n), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0.5 * u, 0.5 * u, n - 0.5 * u, n - 0.5 * u], radius=6.5 * u, fill=255)
    grad = Image.new("RGBA", (n, n))
    gd = ImageDraw.Draw(grad)
    for y in range(n):
        t = y / (n - 1)
        gd.line([(0, y), (n, y)], fill=tuple(round(a + (b - a) * t) for a, b in zip(TOP, BOTTOM)) + (255,))
    img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)
    white = (255, 255, 255, 255)
    # nelle misure piccole le linee non scendono sotto un pixel
    thin = max(1.4 * u, 1.0 * k * (1.3 if size <= 24 else 1.0))
    thick = max(3.2 * u, 2.0 * k)
    # mirino
    d.line([(16 * u, 4 * u), (16 * u, 28 * u)], fill=white, width=round(thin))
    d.line([(4 * u, 16 * u), (28 * u, 16 * u)], fill=white, width=round(thin))
    # arco aperto a destra (da +45 a -45 gradi passando a sinistra), come in Icons.cpp
    d.arc([7 * u, 7 * u, 25 * u, 25 * u], start=45, end=315, fill=white, width=round(thick))
    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    images = [tile(s) for s in SIZES]
    images[-1].save(OUT, format="ICO", sizes=[(s, s) for s in SIZES], append_images=images[:-1])
    images[-1].save(OUT.with_suffix(".png"))   # anteprima a 256 pixel
    print("scritta", OUT)


if __name__ == "__main__":
    main()
