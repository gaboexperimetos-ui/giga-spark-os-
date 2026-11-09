#!/usr/bin/env bash
# Ejecuta un comando y, si falla, publica las ultimas lineas como anotacion de
# GitHub Actions.
#
# Por que existe: los logs completos de un run se descargan de un blob de Azure
# que puede no ser alcanzable desde todos los entornos (en el nuestro esta
# fuera del allowlist de red). Las ANOTACIONES si se leen por la API normal:
#
#   gh api repos/OWNER/REPO/check-runs/JOB_ID/annotations
#
# Sin esto, un fallo solo deja "Process completed with exit code 1" y no hay
# forma de saber que paso. Con esto el error real queda visible en la API.
#
# Uso:
#   bash tools/ci/annotate.sh "Nombre del paso" comando args...
#
# Fuera de Actions (GITHUB_ACTIONS != true) se comporta como un exec normal:
# imprime la salida y devuelve el mismo codigo de salida.

set -uo pipefail

if [ "$#" -lt 2 ]; then
  echo "uso: $0 \"nombre del paso\" comando [args...]" >&2
  exit 2
fi

LABEL="$1"; shift
LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

# Se muestra la salida en vivo y se guarda a la vez. El codigo de salida tiene
# que ser el del comando, no el de tee: de ahi PIPESTATUS.
"$@" 2>&1 | tee "$LOG"
rc="${PIPESTATUS[0]}"

if [ "$rc" -ne 0 ] && [ "${GITHUB_ACTIONS:-}" = "true" ]; then
  # Una anotacion es una sola linea: los saltos se escapan como %0A y los %
  # literales primero, para no corromper el mensaje.
  tail -n 40 "$LOG" \
    | sed -e 's/%/%25/g' -e 's/\r/%0D/g' \
    | awk -v label="$LABEL" -v rc="$rc" '
        { lines = lines $0 "%0A" }
        END { printf "::error::%s fallo (exit %d)%s%s\n", label, rc, "%0A", lines }
      '
fi

exit "$rc"
