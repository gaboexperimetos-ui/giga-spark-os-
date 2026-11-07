# Preview web del simulador C

Expone el simulador SDL2 real (`simulator/main.c`) en el navegador, sin necesidad
de servidor X. El puente reutiliza el código C tal cual: incluye `main.c` renombrando
su `main`, y llama a `render_screen()` y `handle_event()` directamente.

## Arrancar

```bash
bash tools/preview/build.sh          # compila build_preview/bridge + smoke test
python3 tools/preview/server.py --port 8080
```

Abrir el puerto 8080 en la vista previa. El servidor escucha en `0.0.0.0` y usa
rutas relativas, así que funciona detrás del proxy de la plataforma.

## Comprobar que está vivo

```bash
curl -s http://127.0.0.1:8080/health          # -> ok
ps -eo pid,args | grep -E '[t]ools/preview/server.py|[b]uild_preview/bridge'
```

Si el proceso murió pero el puerto sigue ocupado, identificar y matar el PID
antes de rearrancar. Si `build_preview/bridge` no existe (los directorios
`build_*/` no persisten entre snapshots), volver a correr `build.sh`.

## Por qué las fuentes viven aquí

Antes todo el arnés estaba en `build_preview/`, que `.gitignore` excluye con
`build_*/`. Eso hacía que el preview se perdiera en cada snapshot y hubiera que
reescribirlo a mano. Ahora:

- `tools/preview/bridge.c` — puente C, versionado.
- `tools/preview/server.py` — servidor HTTP, versionado.
- `tools/preview/build.sh` — build reproducible, versionado.
- `build_preview/bridge` — binario generado, ignorado (correcto).

`build.sh` busca SDL2 en este orden: `pkg-config`, `sdl2-config` en PATH, y por
último el prefijo local `~/.cache/giga-deps/install`. Añade el padre de
`includedir` porque `main.c` incluye `<SDL2/SDL.h>`, no `<SDL.h>`.

## Protocolo

El puente lee comandos por stdin y responde por stdout un fotograma crudo:

| Comando | Efecto |
|---|---|
| `F` | renderizar el estado actual |
| `K <keycode>` | inyectar tecla SDL (27=Escape, 13=Enter, 49–54=1–6) |
| `M <x> <y>` | toque en coordenadas de pantalla (0–479, 0–271) |
| `Q` | terminar |

Respuesta: cabecera `<II` (pantalla, batería) + `480*272*2` bytes RGB565.
El servidor HTTP traduce eso a `/frame.raw` y `/input`.

## Alcance

Renderiza el simulador tal como está hoy: **480×272**. El GIGA Display Shield
real es 480×800 (ST7701, DSI); esa migración está pendiente y documentada en
`docs/HARDWARE_FIDELITY.md`. Este preview no mide rendimiento ni valida hardware.
