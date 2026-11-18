/*
 * Gigaspark OS - pantalla y touch reales del Arduino GIGA Display Shield
 *
 * Sustituye al stub que solo parpadeaba un LED. Renderiza la misma interfaz
 * Material Design 3 que el simulador (common/ui_os.c) sobre el panel ST7701
 * por LTDC + MIPI-DSI, y lee el touch GT911 por el subsistema input.
 */

#ifndef GIGASPARK_DISPLAY_GIGA_H
#define GIGASPARK_DISPLAY_GIGA_H

/*
 * Comprueba que el panel existe, esta listo y coincide con la interfaz
 * (800x480, RGB565), y apaga el blanking.
 *
 * @return 0 si el panel queda utilizable, -ENODEV si no esta listo,
 *         -EINVAL si la resolucion o el formato no son los esperados.
 */
int display_giga_init(void);

/*
 * Bucle de interfaz: procesa touch, renderiza y hace scan-out. No retorna.
 * Debe llamarse desde un hilo dedicado, nunca desde main().
 */
void display_giga_run(void);

#endif /* GIGASPARK_DISPLAY_GIGA_H */
