/*
 * Gigaspark OS - zRAM Compression Header (Fase 16: LRU)
 * Compresion RLE con politica de eviccion LRU.
 * Cache volatil en C; una eviccion descarta datos, NO los guarda en SD.
* Las funciones de tabla comparten estado: usar un unico hilo propietario.
 */

#ifndef GIGASPARK_ZRAM_COMP_H
#define GIGASPARK_ZRAM_COMP_H

#include <stdint.h>
#include <stddef.h>

/*
 * Comprime datos usando RLE.
 *
 * Formato: pares [count][byte]. count es 1-255.
 * Si un run excede 255, se divide en multiples pares.
 * Bytes no repetidos se almacenan como [1][byte].
 *
 * @param src       Datos a comprimir.
 * @param src_len   Longitud de datos fuente.
 * @param dst       Buffer de salida para datos comprimidos.
 * @param dst_len   Tamano del buffer de salida.
 * @return          Bytes escritos en dst, o 0 si falla.
 */
size_t zram_compress(const uint8_t *src, size_t src_len,
		     uint8_t *dst, size_t dst_len);

/*
 * Descomprime datos codificados en RLE.
 *
 * @param src       Datos comprimidos.
 * @param src_len   Longitud de datos comprimidos.
 * @param dst       Buffer de salida para datos descomprimidos.
 * @param dst_len   Tamano del buffer de salida.
 * @return          Bytes escritos en dst, o 0 si falla.
 * Una longitud impar o un count cero se rechazan antes de escribir.
 */
size_t zram_decompress(const uint8_t *src, size_t src_len,
		       uint8_t *dst, size_t dst_len);

/*
 * Comprime y almacena un bloque en la tabla LRU.
 * Si la tabla esta llena, expulsa la victima LRU.
 *
 * @param handle    Handle del bloque a comprimir (distinto de 0).
 * @param src       Datos fuente.
 * @param src_len   Longitud de datos fuente.
 * @return          Tamano comprimido, o 0 si falla.
 */
size_t zram_compress_and_store(uint32_t handle,
			       const uint8_t *src, size_t src_len);

/*
 * Recupera y descomprime un bloque de la tabla LRU.
 * Actualiza el timestamp LRU del bloque.
 *
 * @param handle    Handle del bloque a recuperar.
 * @param dst       Buffer de salida.
 * @param dst_len   Tamano del buffer de salida.
 * @return          Tamano descomprimido, o 0 si no existe.
 */
size_t zram_retrieve(uint32_t handle, uint8_t *dst, size_t dst_len);

/*
 * Expulsa explicitamente un bloque del zRAM.
 *
 * @param handle    Handle del bloque a expulsar.
 * @return          Handle expulsado, o 0 si no existia.
 */
uint32_t zram_evict_block(uint32_t handle);

/*
 * Retorna estadisticas del zRAM.
 *
 * @param used      Puntero para bloques en uso (o NULL).
 * @param total     Puntero para bloques totales (o NULL).
 * @param counter   Puntero para contador LRU global (o NULL).
 */
void zram_get_stats(uint8_t *used, uint8_t *total, uint32_t *counter);

/*
 * Reinicia la tabla LRU (para tests).
 */
void zram_reset(void);

#endif /* GIGASPARK_ZRAM_COMP_H */
