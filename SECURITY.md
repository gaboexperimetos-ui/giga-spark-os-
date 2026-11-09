# Política de seguridad

## Estado del proyecto

Gigaspark OS es un proyecto experimental de maker, **sin hardware físico
verificado**. El firmware todavía no se ha probado en una placa real. Tenlo en
cuenta antes de conectarlo a cualquier cosa que pueda dañarse: una fuente de
alimentación o una etapa de RF mal controlada pueden destruir componentes o
lesionar a alguien.

No hay versiones con soporte ni compromiso de parches. Se trabaja sobre `main`.

## Reportar una vulnerabilidad

**No abras un issue público** con detalles de una vulnerabilidad.

Usa la vía privada de GitHub: en la pestaña **Security** del repositorio,
*Report a vulnerability* (GitHub Security Advisories). Así el reporte queda
visible solo para quien mantiene el repo.

Si esa vía no está disponible, escribe a **gruposalas2025@gmail.com** con el
asunto `[security] gigaspark-os` y una descripción sin publicar detalles en
ningún otro sitio.

Incluye, si puedes:

- Qué componente afecta (`m7/`, `m4/`, `sdk/`, `simulator/`, `tools/preview/`).
- Pasos para reproducirlo o una prueba de concepto mínima.
- Qué impacto crees que tiene y en qué configuración.

Respuesta esperada: acuse de recibo en unos días. Al ser un proyecto personal,
no hay un SLA formal.

## Qué entra en el alcance

Lo más relevante aquí no es la seguridad web clásica, sino la memoria y el
control del hardware:

- Desbordamientos, use-after-free o fugas en el núcleo C (`common/`, `m7/src/`,
  `m4/src/`): buddy allocator, zRAM/LRU, framebuffer, decodificador de vídeo.
- Entradas del SDK público (`sdk/gigaspark_api.c`) que no validan sus argumentos.
- Parsing de datos no confiables: respuestas IPC entre núcleos, tramas de RF,
  tarjetas NFC, ficheros de ajustes en flash.
- El servidor de preview (`tools/preview/server.py`) si permite leer ficheros
  fuera del repo o ejecutar comandos con una petición manipulada.

## Fuera de alcance

- El servidor de preview está pensado para **localhost o un sandbox
  desechable**. Escucha en `0.0.0.0` a propósito y no tiene autenticación: no lo
  expongas a Internet. Que falte auth ahí es una decisión de diseño, no un bug.
- Ataques que requieran acceso físico a la placa o al bus de depuración SWD.
- Vulnerabilidades en Zephyr, LVGL o SDL: repórtalas a sus propios proyectos. Si
  afecta a la versión que fijamos en `west.yml`, avísanos para actualizar el pin.

## Verificación que ya hacemos

En CI se ejecutan AddressSanitizer y UndefinedBehaviorSanitizer sobre los tests
de host, y el código compila con `-Wall -Wextra -Werror`. Eso cubre bastante de la
clase de bugs de memoria, pero **no sustituye una revisión** ni pruebas en
hardware real.
