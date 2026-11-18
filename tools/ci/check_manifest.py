#!/usr/bin/env python3
"""Verifica el name-allowlist de west.yml contra el manifiesto real de Zephyr.

Por que existe
--------------
Un nombre que no existe en el manifiesto de Zephyr NO produce un error: west
lo ignora en silencio y el modulo simplemente no se descarga. El sintoma
aparece mucho mas tarde y en otro sitio, como un header que no se encuentra.

Caso real que motivo este check: el allowlist tenia `cmsis` pero no `cmsis_6`.
Son dos proyectos distintos (Cortex-A/R y Cortex-M). Sin cmsis_6, el fichero
modules/cmsis_6/CMakeLists.txt de Zephyr nunca agrega su include, y el M7 y el
M4 abortaban con:

    fatal error: cmsis_core.h: No such file or directory

El error no menciona west ni el allowlist, asi que la causa no era evidente.
Este script la detecta en segundos, en el job de lint, antes de gastar un
workspace de Zephyr en descubrirlo.

Uso
---
    python3 tools/ci/check_manifest.py

Necesita red (consulta la API de GitHub) y PyYAML. Sale con 1 si hay nombres
inexistentes, 0 si todos son validos.
"""

from __future__ import annotations

import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

try:
    import yaml
except ImportError:
    print("PyYAML no esta instalado: pip install pyyaml", file=sys.stderr)
    raise SystemExit(2)

ROOT = Path(__file__).resolve().parents[2]
API = "https://api.github.com/repos"


def http_get(url: str, raw: bool = False) -> bytes:
    headers = {"User-Agent": "gigaspark-manifest-check"}
    # El limite anonimo de la API es 60 peticiones/hora por IP, y los runners
    # comparten IP. Con token son 1000/hora y el check deja de ser flaky.
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token}"
    headers["Accept"] = (
        "application/vnd.github.raw" if raw else "application/vnd.github+json"
    )
    req = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(req, timeout=60) as resp:
        return resp.read()


def main() -> int:
    manifest = yaml.safe_load((ROOT / "west.yml").read_text())["manifest"]

    zephyr = next(
        (p for p in manifest["projects"] if p["name"] == "zephyr"), None
    )
    if zephyr is None:
        print("west.yml no declara el proyecto zephyr", file=sys.stderr)
        return 1

    revision = zephyr["revision"]
    allow = list(zephyr.get("import", {}).get("name-allowlist", []))
    if not allow:
        print("west.yml no usa name-allowlist: nada que verificar")
        return 0

    print(f"Zephyr {revision}: verificando {len(allow)} nombres del allowlist")

    url = f"{API}/zephyrproject-rtos/zephyr/contents/west.yml?ref={revision}"
    try:
        upstream = yaml.safe_load(http_get(url, raw=True))
    except urllib.error.HTTPError as exc:
        print(f"No se pudo leer el manifiesto de Zephyr: {exc}", file=sys.stderr)
        if exc.code in (403, 429):
            print(
                "Probable limite de la API. Define GITHUB_TOKEN para subir de"
                " 60 a 1000 peticiones/hora.",
                file=sys.stderr,
            )
        return 2

    projects = {p["name"]: p for p in upstream["manifest"]["projects"]}
    print(f"El manifiesto upstream declara {len(projects)} proyectos")

    bad = [n for n in allow if n not in projects]
    for name in allow:
        if name in projects:
            print(f"  OK      {name:12s} -> {projects[name].get('path')}")
        else:
            print(f"  INVALID {name}")

    if bad:
        print(file=sys.stderr)
        for name in bad:
            print(
                f"ERROR: '{name}' no existe en el west.yml de Zephyr "
                f"{revision}. west lo ignora en silencio.",
                file=sys.stderr,
            )
            # Sugerir el nombre real mas parecido: el fallo tipico es confundir
            # guion medio y guion bajo (cmsis-6 frente a cmsis_6).
            key = name.replace("-", "").replace("_", "").lower()
            near = [
                p
                for p in projects
                if p.replace("-", "").replace("_", "").lower() == key
            ]
            if near:
                print(f"       Quiza querias decir: {', '.join(near)}", file=sys.stderr)
        return 1

    # Nombres duplicados: west los deduplica, pero suelen ser un despiste.
    dupes = {n for n in allow if allow.count(n) > 1}
    if dupes:
        print(f"ERROR: nombres duplicados: {sorted(dupes)}", file=sys.stderr)
        return 1

    print("Todos los nombres del allowlist existen en Zephyr.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
