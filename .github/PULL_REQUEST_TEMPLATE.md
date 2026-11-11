<!--
Gracias por la PR. Sé concreto: qué cambia y por qué.
Lee CONTRIBUTING.md si es tu primera contribución.
-->

## Qué cambia y por qué

<!-- Empieza por el problema, no por la solución. -->

## Cómo se verificó

<!-- Marca solo lo que REALMENTE ejecutaste. -->

- [ ] `ctest` en `build_host` (tests de host)
- [ ] `ctest` en `build_simulator` (smoke)
- [ ] `ctest` con ASAN/UBSAN (`-DGIGASPARK_SANITIZE=ON`)
- [ ] `python3 -m unittest discover -s tests/preview` (con el puente compilado)
- [ ] Compila el firmware (`west build`)
- [ ] Probado en una placa GIGA física

```
# Pega aquí la salida relevante de los tests, no "todo pasa".
```

## Si toca la interfaz

<!--
Los tests en verde NO detectan layout roto: ya tuvimos un smoke pasando con
botones invisibles y texto tapado por controles. Adjunta imágenes.
-->

- [ ] Regeneré las capturas (`python3 tools/preview/shots.py`) y **las miré**
- [ ] Los objetivos táctiles siguen midiendo ≥ 80 px
- [ ] Adjunto PNG del antes y el después

| Antes | Después |
|---|---|
|  |  |

## Si cambia el dibujo a propósito

- [ ] Ejecuté `compare.py --allow-visual-change`
- [ ] Anoté los hashes nuevos en `docs/PERFORMANCE.md`
- [ ] Justifiqué el cambio de rendimiento, si lo hay

## Límites de esta PR

<!--
Qué NO se pudo verificar y por qué. El CI sí compila M7/M4; si no hubo
workspace local, indícalo, y separa eso de la prueba física que aún requiera placa.
-->

## Issues relacionados

<!-- Closes #123 -->
