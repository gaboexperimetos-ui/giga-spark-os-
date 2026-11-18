/*
 * Gigaspark OS - Memory Engine Header (Fase 16: Buddy Allocator)
 * Asignador buddy con bitmaps; sin eviccion LRU en esta capa.
* No es thread-safe: usar desde el hilo memory-engine del M4.
 */

#ifndef GIGASPARK_MEM_ENGINE_H
#define GIGASPARK_MEM_ENGINE_H

#include <stdint.h>
#include <stddef.h>

/*
 * Inicializa el motor de memoria y el buddy allocator.
 * Debe llamarse una vez antes de cualquier operacion alloc/free.
 */
void mem_engine_init(void);

/*
 * Alias para compatibilidad con codigo existente.
 */
#define mem_init mem_engine_init

/*
 * Asigna memoria usando el buddy allocator.
 *
 * @param size  Numero de bytes a asignar (debe ser > 0 y <= 4096).
 * @return      Handle (ID opaco) en exito, o 0 si falla.
 */
uint32_t mem_alloc(uint32_t size);

/*
 * Libera un bloque de memoria previamente asignado.
 *
 * @param handle  Handle retornado por mem_alloc().
 * @return        0 si ok, -1 si falla.
 */
int mem_free(uint32_t handle);

/*
 * Lee datos de un bloque a un buffer.
 *
 * @param handle    Handle retornado por mem_alloc().
 * @param buf       Buffer destino.
 * @param buf_size  Tamano del buffer destino.
 * @return          Numero de bytes leidos, o 0 si error.
 */
size_t mem_read(uint32_t handle, void *buf, size_t buf_size);

/*
 * Escribe datos en un bloque existente.
 *
 * @param handle    Handle retornado por mem_alloc().
 * @param data      Datos fuente a escribir.
 * @param data_len  Numero de bytes a escribir.
 * @return          0 si ok, -1 si error.
 */
int mem_write(uint32_t handle, const void *data, size_t data_len);

/*
 * Obtiene informacion de un handle.
 *
 * @param handle  Handle a consultar.
 * @param addr    Puntero para la direccion (o NULL).
 * @param size    Puntero para el tamano (o NULL).
 * @return        0 si ok, -1 si el handle es invalido.
 */
int mem_get_info(uint32_t handle, uint32_t *addr, uint32_t *size);

/*
 * Retorna el porcentaje de memoria usada (0-100).
 */
uint8_t mem_get_usage(void);

#endif /* GIGASPARK_MEM_ENGINE_H */
