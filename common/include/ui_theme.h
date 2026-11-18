/*
 * Gigaspark OS - Tema visual canonico (Material Design 3, esquema oscuro azul)
 *
 * FUENTE UNICA DE VERDAD del color para el firmware M7 y el simulador de PC.
 * Ambos usan el nucleo portable common/ui_os.c, asi que no hay una paleta
 * distinta en escritorio y en el Arduino.
 *
 * Los tonos se definen en RGB888, notacion de Material, y se cuantizan a
 * RGB565 para el framebuffer compartido. LTDC convierte a RGB888 al transmitir
 * por MIPI-DSI al panel ST7701.
 *
 * IMPORTANTE: cuantizar REDONDEANDO al mas cercano, nunca truncando.
 * En superficies oscuras los canales son diminutos y truncar (v>>3) pierde
 * proporcionalmente mas en un canal, desplazando el tono. Ejemplo medido:
 * el tono de tarjeta 0x263036 truncado daba (33,48,49), con verde==azul, y las
 * tarjetas se veian verdosas; redondeando da (41,48,57), el gris azulado real.
 *
 * El brillo NO pertenece al tema: la retroiluminacion fisica usa SWIRE (PB12),
 * no PWM. Ver backlight_scale() en common/ui_os.c; el protocolo SWIRE queda
 * pendiente de validacion en la placa.
 */
#ifndef GIGASPARK_UI_THEME_H
#define GIGASPARK_UI_THEME_H

/* ---- Superficies (la elevacion se expresa aclarando, no con sombras) ---- */
#define UI_BG        0x101418  /* fondo de la ventana                  */
#define UI_SURFACE   0x151A1F  /* barra de estado / app bar            */
#define UI_CARD      0x1D2328  /* tarjeta en reposo                    */
#define UI_CARD_HIGH 0x263036  /* tarjeta elevada / fila activa        */
#define UI_OUTLINE   0x39434B  /* borde de 1 px, divisores             */

/* ---- Acento primario y sus contenedores ---- */
#define UI_PRIMARY      0xA8C7FA  /* azul M3: titulos, switch activo   */
#define UI_PRIMARY_CONT 0x1B3A5C  /* boton tonal, chip seleccionado    */
#define UI_ON_PRIMARY   0x0A2A4A  /* texto/icono sobre primario        */

/* ---- Texto ---- */
#define UI_TEXT      0xE3E3E8  /* texto de alto enfasis                */
#define UI_TEXT_MUTED 0xA5ADB5 /* subtitulos, texto secundario         */

/* ---- Semanticos y sus contenedores ---- */
#define UI_GREEN      0x6DD58C  /* ok, salida activa, en marcha        */
#define UI_GREEN_CONT 0x11331F
#define UI_RED        0xFFB4AB  /* error, apagado, peligro             */
#define UI_RED_CONT   0x4A1B18
#define UI_YELLOW     0xFFD580  /* aviso, canal 2, brillo              */
#define UI_ORANGE     0xFFB77C
#define UI_PURPLE     0xD0BCFF  /* RF                                  */
#define UI_CYAN       0x80DEEA  /* osciloscopio, canal 1               */

/* ---- Radios M3 (px a 800x480, ~235 ppi) ---- */
#define UI_RADIUS_CARD  24
#define UI_RADIUS_BTN   28
#define UI_RADIUS_SMALL 16

/*
 * RGB888 -> RGB565 redondeando al mas cercano.
 * Constante: usable en inicializadores estaticos y en _Static_assert.
 * El min() evita que el redondeo desborde el canal (31/63/31).
 */
#define UI_MIN_(a, b) ((a) < (b) ? (a) : (b))
#define UI_R5_(rgb) UI_MIN_(((((rgb) >> 16) & 0xFF) + 4) / 8, 31)
#define UI_G6_(rgb) UI_MIN_(((((rgb) >> 8) & 0xFF) + 2) / 4, 63)
#define UI_B5_(rgb) UI_MIN_((((rgb) & 0xFF) + 4) / 8, 31)
#define UI_RGB565(rgb) \
	((unsigned short)((UI_R5_(rgb) << 11) | (UI_G6_(rgb) << 5) | UI_B5_(rgb)))

#endif /* GIGASPARK_UI_THEME_H */
