#!/usr/bin/env python3
"""Genera common/include/roboto_font.h: atlas antialiasado de Roboto para el simulador.

Por que existe: el simulador dibujaba texto con una fuente bitmap de 5x7 escalada
por enteros, y eso es lo que le daba aspecto de terminal ochentera en vez de
Android. Android usa Roboto con antialiasing; sin curvas suaves no hay parecido
posible, por mucho que se arreglen los colores.

Roboto es de Google, licencia SIL Open Font License 1.1 (redistribuible). Se
rasteriza con FreeType via Pillow y se guarda la cobertura como alfa de 4 bits
(16 niveles), suficiente para texto suave sobre fondo oscuro y la mitad de grande
que 8 bits.

Uso:
    python3 tools/ui/gen_font.py --ttf /ruta/Roboto-VF.ttf

El .h generado se versiona para que el proyecto compile sin Pillow ni red.
"""
import argparse
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "common" / "include" / "roboto_font.h"

# ASCII imprimible + los caracteres que el espanol necesita de verdad.
# No olvidar los signos tecnicos: µ (base de tiempo), … (truncado), – (rangos),
# × (resolucion) y ² (I²C). Si faltan, la UI muestra '?' en su lugar.
EXTRA = "áéíóúñüÁÉÍÓÚÑÜ¿¡°·—–…‹›✓µ×²"
CHARS = [chr(c) for c in range(32, 127)] + list(EXTRA)

# (nombre, peso, tamano en px). Regular para texto, Medium para titulos y botones,
# que es como Material diferencia jerarquia sin cambiar de familia.
FACES = [
    ("R18", 400, 18),
    ("R24", 400, 24),
    ("R34", 400, 34),
    ("R48", 400, 48),
    ("M24", 500, 24),
    ("M34", 500, 34),
]


def build(ttf: str):
    from PIL import ImageFont

    blobs = bytearray()
    tables = []
    metas = []

    for name, weight, size in FACES:
        font = ImageFont.truetype(ttf, size)
        try:
            font.set_variation_by_axes([weight])
        except Exception:
            pass  # fuente estatica: se usa tal cual
        ascent, descent = font.getmetrics()
        # Cap height real: la altura de 'H'. Sirve para centrar texto en botones
        # sin que la descendente de la 'g' lo desplace hacia arriba.
        hb = font.getbbox("H", anchor="ls")
        cap = hb[3] - hb[1]

        rows = []
        for ch in CHARS:
            mask, (dx, dy) = font.getmask2(ch, mode="L", anchor="ls")
            w, h = mask.size
            adv = int(round(font.getlength(ch)))
            off = len(blobs)
            if w and h:
                data = bytes(mask)
                # Empaquetar 2 pixeles por byte, nibble alto primero.
                for y in range(h):
                    row = data[y * w:(y + 1) * w]
                    packed = bytearray((w + 1) // 2)
                    for x, v in enumerate(row):
                        a = v >> 4
                        if x & 1:
                            packed[x >> 1] |= a
                        else:
                            packed[x >> 1] = a << 4
                    blobs += packed
            rows.append((off, w, h, dx, dy, adv))
        tables.append((name, rows))
        metas.append((name, ascent, descent, cap, size))
        print(f"  {name}: {size}px peso {weight}, {len(rows)} glifos, "
              f"ascent {ascent} cap {cap}", file=sys.stderr)

    return blobs, tables, metas


def emit(blobs, tables, metas):
    L = []
    a = L.append
    a("/* GENERADO por tools/ui/gen_font.py -- NO editar a mano.")
    a(" *")
    a(" * Atlas de Roboto (Google, SIL OFL 1.1) con cobertura de 4 bits por pixel.")
    a(" * Reemplaza la fuente bitmap 5x7 que daba aspecto de terminal. Regenerar:")
    a(" *   python3 tools/ui/gen_font.py --ttf Roboto-VF.ttf")
    a(" */")
    a("#ifndef GIGASPARK_ROBOTO_FONT_H")
    a("#define GIGASPARK_ROBOTO_FONT_H")
    a("")
    a("#include <stdint.h>")
    a("")
    a(f"#define RF_NGLYPH {len(CHARS)}")
    a(f"#define RF_FIRST_EXTRA 95   /* indice del primer caracter no ASCII */")
    a("")
    a("typedef struct {")
    a("\tuint32_t off;              /* byte de inicio en rf_data */")
    a("\tuint8_t w, h;              /* caja del glifo en px */")
    a("\tint8_t dx, dy;             /* offset respecto al baseline */")
    a("\tuint8_t adv;               /* avance horizontal */")
    a("} RGlyph;")
    a("")
    a("typedef struct {")
    a("\tconst RGlyph *g;")
    a("\tuint8_t ascent, descent, cap, size;")
    a("} RFont;")
    a("")

    # Codepoints extra, para el decoder UTF-8.
    a("static const uint16_t rf_extra_cp[] = {")
    cps = [ord(c) for c in EXTRA]
    for i in range(0, len(cps), 12):
        a("\t" + " ".join("0x%04X," % c for c in cps[i:i + 12]))
    a("};")
    a("")

    a(f"static const uint8_t rf_data[{len(blobs)}] = {{")
    for i in range(0, len(blobs), 24):
        a("\t" + "".join("%d," % b for b in blobs[i:i + 24]))
    a("};")
    a("")

    for name, rows in tables:
        a(f"static const RGlyph rf_{name}[RF_NGLYPH] = {{")
        for off, w, h, dx, dy, adv in rows:
            a(f"\t{{{off},{w},{h},{dx},{dy},{adv}}},")
        a("};")
        a("")

    a("enum { " + ", ".join("RF_" + n for n, _ in tables) + ", RF_NFONT };")
    a("")
    a("static const RFont rf_fonts[RF_NFONT] = {")
    for name, ascent, descent, cap, size in metas:
        a(f"\t{{ rf_{name}, {ascent}, {descent}, {cap}, {size} }},")
    a("};")
    a("")
    a("#endif /* GIGASPARK_ROBOTO_FONT_H */")
    return "\n".join(L) + "\n"


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--ttf", default="/tmp/Roboto-VF.ttf")
    p.add_argument("--out", default=str(OUT))
    args = p.parse_args()
    if not Path(args.ttf).exists():
        sys.exit(f"No existe {args.ttf}. Descarga Roboto de google/fonts (ofl/roboto).")
    blobs, tables, metas = build(args.ttf)
    text = emit(blobs, tables, metas)
    Path(args.out).write_text(text)
    print(f"Escrito {args.out}: {len(text)} bytes, atlas {len(blobs)} bytes", file=sys.stderr)


if __name__ == "__main__":
    main()
