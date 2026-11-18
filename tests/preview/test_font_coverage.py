#!/usr/bin/env python3
"""Toda cadena no ASCII que la UI dibuja debe existir en el atlas de Roboto.

Bug real que motivo este test: el atlas solo tenia letras acentuadas, asi que
los signos tecnicos se dibujaban como '?'. En pantalla se veia "0 ? 24.0 V"
en vez de "0 – 24.0 V", y "100 ?s" en vez de "100 µs". Lo peor: el propio
truncado con puntos suspensivos usaba '…', que tampoco estaba, asi que cortar
texto producia un '?' al final.

Se extraen los literales de cadena del C real (no una lista escrita a mano,
que se desincronizaria) y se comprueba que cada codepoint tenga glifo.

Uso:  python3 -m unittest discover -s tests/preview -v
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "common" / "include" / "roboto_font.h"
SOURCES = [ROOT / "common" / "ui_os.c", ROOT / "common" / "ui_material.c",
           ROOT / "simulator" / "main.c"]

# Literales de cadena en C, admite escapes simples.
STRING_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def atlas_codepoints():
    text = HEADER.read_text()
    m = re.search(r"rf_extra_cp\[\] = \{(.*?)\};", text, re.S)
    if not m:
        raise AssertionError("no encontre rf_extra_cp en roboto_font.h")
    extras = {int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{4,6})", m.group(1))}
    n = re.search(r"#define RF_NGLYPH (\d+)", text)
    if not n:
        raise AssertionError("no encontre RF_NGLYPH")
    # ASCII imprimible 32..126 son los primeros 95 glifos.
    return set(range(32, 127)) | extras, int(n.group(1))


def ui_strings():
    for path in SOURCES:
        src = path.read_text()
        # Fuera comentarios: mencionan caracteres que no se dibujan.
        src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
        src = re.sub(r"//[^\n]*", "", src)
        for m in STRING_RE.finditer(src):
            yield path.name, m.group(1)


class TestFontCoverage(unittest.TestCase):
    def test_every_ui_character_has_a_glyph(self):
        have, _ = atlas_codepoints()
        missing = {}
        for name, s in ui_strings():
            for ch in s:
                cp = ord(ch)
                if cp < 32:      # \n, \t no se dibujan como glifo
                    continue
                if cp not in have:
                    missing.setdefault(ch, set()).add(name)
        self.assertFalse(
            missing,
            "Faltan glifos (salen como '?'): " + ", ".join(
                "%r U+%04X en %s" % (c, ord(c), ",".join(sorted(f)))
                for c, f in sorted(missing.items()))
            + ". Añadelos a EXTRA en tools/ui/gen_font.py y regenera el atlas.")

    def test_ellipsis_used_by_truncation_exists(self):
        """m_text_ell añade '…'; si falta, truncar mete un '?'."""
        have, _ = atlas_codepoints()
        self.assertIn(ord("…"), have, "el truncado necesita '…' en el atlas")

    def test_glyph_table_sizes_match_header(self):
        text = HEADER.read_text()
        _, n = atlas_codepoints()
        tables = re.findall(r"static const RGlyph rf_(\w+)\[RF_NGLYPH\] = \{(.*?)\n\};",
                            text, re.S)
        self.assertTrue(tables, "no encontre tablas de glifos")
        for name, body in tables:
            rows = re.findall(r"\{\d+,\d+,\d+,-?\d+,-?\d+,\d+\}", body)
            self.assertEqual(len(rows), n,
                             "rf_%s tiene %d glifos, RF_NGLYPH dice %d"
                             % (name, len(rows), n))

    def test_extra_index_base_is_consistent(self):
        """RF_FIRST_EXTRA debe seguir al bloque ASCII (32..126 = 95)."""
        text = HEADER.read_text()
        m = re.search(r"#define RF_FIRST_EXTRA (\d+)", text)
        self.assertTrue(m)
        self.assertEqual(int(m.group(1)), 127 - 32)


if __name__ == "__main__":
    unittest.main()
