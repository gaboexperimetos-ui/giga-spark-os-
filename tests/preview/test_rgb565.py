#!/usr/bin/env python3
"""Regresion de la conversion RGB565 -> RGB888 del preview.

El panel real es RGB565. El preview (JS en server.py) y las capturas (shots.py)
lo expanden a 24 bits para mostrarlo. Esa expansion debe ser biyectiva: si un
color no vuelve a su valor RGB565 original, el preview esta ensenando un color
distinto al que tendria el hardware.

Bug real que motivo este test: el canal verde (6 bits) se expandia con
`(g << 2) | (g >> 2)`, que replica los 4 bits bajos en vez de los 2 altos.
28 de los 64 verdes no volvian a su valor, asi que el preview mentia sobre el
color del panel. Lo correcto es `(g << 2) | (g >> 4)`.

En vez de reimplementar la formula aqui (eso no probara nada), se extrae el
texto exacto de la expresion de cada fichero y se evalua. Las operaciones de
bits de JS y Python son identicas para estas expresiones, asi que no hace
falta node.

Uso:  python3 -m unittest discover -s tests/preview -v
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SERVER = ROOT / "tools" / "preview" / "server.py"
SHOTS = ROOT / "tools" / "preview" / "shots.py"

ALL_565 = range(1 << 16)


def channels(c):
    """Descompone un pixel RGB565 en sus campos de 5/6/5 bits."""
    return (c >> 11) & 31, (c >> 5) & 63, c & 31


def requant(r, g, b):
    """Vuelve a RGB565 truncando, como hace el panel."""
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def code_without_comments(text):
    out = []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("#") or s.startswith("//"):
            continue
        out.append(line)
    return "\n".join(out)


def js_expressions():
    """Saca las tres expansiones de canal del JS, sin el brillo.

    El JS aplica backlightScale() despues de expandir. Aqui se prueba la
    expansion pura (debe ser biyectiva) y el brillo se prueba aparte.
    """
    src = code_without_comments(SERVER.read_text())
    exprs = []
    # El canal rojo se escribe d[j], no d[j+0].
    for target in (r"d\[j\]", r"d\[j\+1\]", r"d\[j\+2\]"):
        m = re.search(target + r"\s*=\s*([^;]+);", src)
        if not m:
            raise AssertionError("no encontre %s en server.py" % target)
        e = m.group(1).strip()
        w = re.fullmatch(r"backlightScale\((.+)\)", e)
        if w:
            e = w.group(1).strip()
        exprs.append(e)
    return exprs


def js_backlight_body():
    """Cuerpo de backlightScale(), para probarlo con el mismo texto del JS."""
    src = code_without_comments(SERVER.read_text())
    m = re.search(r"function backlightScale\(v\)\s*\{\s*return\s*([^;]+);", src)
    if not m:
        raise AssertionError("no encontre backlightScale en server.py")
    # '| 0' es el truncado a entero de JS; en Python equivale a int().
    return m.group(1).replace("| 0", "").strip()


def py_expressions():
    """Saca la tupla RGB de shots.py."""
    src = code_without_comments(SHOTS.read_text())
    # Greedy: el cierre es bytes((...)), un .+? perezoso se pararia en el primer
    # ')' interno y dejaria la ultima expresion sin cerrar.
    m = re.search(r"rows\s*\+=\s*bytes\(\((.+)\)\)", src)
    if not m:
        raise AssertionError("no encontre la expansion RGB en shots.py")
    parts = [p.strip() for p in m.group(1).split(",")]
    if len(parts) != 3:
        raise AssertionError("esperaba 3 canales, encontre %d" % len(parts))
    return parts


class TestRgb565Expansion(unittest.TestCase):
    def test_server_js_round_trips_every_color(self):
        exprs = [compile(e, "<server.py>", "eval") for e in js_expressions()]
        for c in ALL_565:
            r, g, b = channels(c)
            env = {"r": r, "g": g, "b": b}
            rgb = tuple(eval(e, {"__builtins__": {}}, env) for e in exprs)
            self.assertEqual(
                requant(*rgb), c,
                "server.py: color 0x%04X -> %s no vuelve a RGB565" % (c, rgb))

    def test_shots_round_trips_every_color(self):
        exprs = [compile(e, "<shots.py>", "eval") for e in py_expressions()]
        for c in ALL_565:
            r, g, b = channels(c)
            env = {"r": r, "g": g, "b": b}
            rgb = tuple(eval(e, {"__builtins__": {}}, env) for e in exprs)
            self.assertEqual(
                requant(*rgb), c,
                "shots.py: color 0x%04X -> %s no vuelve a RGB565" % (c, rgb))

    def test_preview_and_screenshots_agree(self):
        """Mirar el preview y mirar un PNG debe dar el mismo color."""
        js = [compile(e, "<server.py>", "eval") for e in js_expressions()]
        py = [compile(e, "<shots.py>", "eval") for e in py_expressions()]
        for c in ALL_565:
            r, g, b = channels(c)
            env = {"r": r, "g": g, "b": b}
            a = tuple(eval(e, {"__builtins__": {}}, env) for e in js)
            b_ = tuple(eval(e, {"__builtins__": {}}, env) for e in py)
            self.assertEqual(a, b_, "server.py y shots.py difieren en 0x%04X" % c)

    def test_backlight_is_applied_after_expansion(self):
        """El brillo escala en 8 bits, no dentro del RGB565.

        Atenuar en RGB565 recuantiza tonos oscuros (solo 32/64 niveles) y el
        gris vira de tono: la tarjeta M_CARD pasaba de (33,36,41) a (24,32,33),
        con verde==azul. En 8 bits hay 256 niveles y el tono se conserva.
        """
        exprs = [compile(e, "<server.py>", "eval") for e in js_expressions()]
        scale = compile(js_backlight_body(), "<backlightScale>", "eval")

        def expand(c, bl):
            r, g, b = channels(c)
            rgb = tuple(eval(e, {"__builtins__": {}},
                             {"r": r, "g": g, "b": b}) for e in exprs)
            return tuple(int(eval(scale, {"__builtins__": {}},
                                  {"v": v, "backlight": bl})) for v in rgb)

        card = 0x2125  # M_CARD, superficie de tarjeta
        full = expand(card, 255)
        dim = expand(card, 222)  # brillo 85 %
        self.assertGreater(full[2], full[1], "el gris azulado debe tener B > G")
        self.assertGreater(dim[2], dim[1],
                           "atenuar no puede dejar B == G (gris verdoso)")
        for a, b_ in zip(full, dim):
            self.assertLessEqual(b_, a, "atenuar no puede aclarar un canal")
        # A oscuras casi todo es negro, y nunca se sale de rango.
        for c in (0x0000, 0xFFFF, card):
            for bl in (0, 40, 128, 222, 255):
                for v in expand(c, bl):
                    self.assertTrue(0 <= v <= 255)
        self.assertEqual(expand(0xFFFF, 255), (255, 255, 255))

    def test_extremes_and_range(self):
        exprs = [compile(e, "<server.py>", "eval") for e in js_expressions()]

        def expand(c):
            r, g, b = channels(c)
            env = {"r": r, "g": g, "b": b}
            return tuple(eval(e, {"__builtins__": {}}, env) for e in exprs)

        self.assertEqual(expand(0x0000), (0, 0, 0), "negro no es negro puro")
        self.assertEqual(expand(0xFFFF), (255, 255, 255), "blanco no es blanco puro")
        for c in (0xF800, 0x07E0, 0x001F):  # rojo, verde y azul puros
            for v in expand(c):
                self.assertTrue(0 <= v <= 255, "canal fuera de rango")
        self.assertEqual(expand(0xF800), (255, 0, 0))
        self.assertEqual(expand(0x07E0), (0, 255, 0))
        self.assertEqual(expand(0x001F), (0, 0, 255))


if __name__ == "__main__":
    unittest.main()
