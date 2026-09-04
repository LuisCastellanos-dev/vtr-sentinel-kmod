/*
 * vtr_ring.h — ring buffer en kernel space
 * vtr-sentinel-kmod — VTR-METH-001 v5.1
 *
 * Ring buffer de tamaño fijo para eventos del módulo.
 * Sin malloc en el hot path. O(1) todas las operaciones.
 * Thread-safe via spinlock de vtr_arch.h.
 *
 * El tamaño VTR_RING_SIZE se define en vtr_arch.h por arquitectura:
 *   x86_64 (Pentium Silver): 2047 eventos efectivos (VTR_RING_SIZE-1) = ~32KB = L1 cache
 *   aarch64 (Cortex-A53):   1024 eventos = 16KB
 *   arm (Cortex-M4):         256 eventos =  4KB
 */

#ifndef VTR_RING_H
#define VTR_RING_H

#include "vtr_arch.h"
#include "vtr_event.h"

/* ── Estado del ring buffer ──────────────────────────────────────────── */

struct vtr_ring {
    struct vtr_event    buf[VTR_RING_SIZE];
    volatile size_t     head;       /* índice de lectura */
    volatile size_t     tail;       /* índice de escritura */
    volatile size_t     count;      /* elementos actuales */
    vtr_lock_t          lock;       /* spinlock */
    /* Estadísticas — no en el hot path */
    uint64_t            total_in;   /* eventos totales escritos */
    uint64_t            total_out;  /* eventos totales leídos */
    uint64_t            overflows;  /* eventos descartados por full */
};

/* ── API pública ─────────────────────────────────────────────────────── */

/*
 * vtr_ring_init — inicializar el ring buffer
 * Debe llamarse desde modevent MOD_LOAD antes de registrar hooks.
 * Retorna 0 en éxito, ENOMEM si no puede inicializar el lock.
 */
int vtr_ring_init(struct vtr_ring *r);

/*
 * vtr_ring_destroy — liberar recursos del ring buffer
 * Debe llamarse desde modevent MOD_UNLOAD después de desregistrar hooks.
 * El lock debe estar libre — no llamar si hay hooks activos.
 */
void vtr_ring_destroy(struct vtr_ring *r);

/*
 * vtr_ring_push — escribir un evento (llamado desde hooks)
 *
 * CONTRATO DEL HOT PATH:
 *   - No bloquea nunca
 *   - No alloca memoria
 *   - Si lleno: descarta el evento y retorna 0
 *   - Retorna 1 si el evento fue almacenado
 *   - Retorna 0 si fue descartado (ring lleno)
 *
 * El caller es responsable de construir ev con vtr_event_build()
 * antes de llamar push — no se construye el evento dentro del lock.
 */
int vtr_ring_push(struct vtr_ring *r, const struct vtr_event *ev);

/*
 * vtr_ring_pop — leer un evento (llamado desde cdev read())
 *
 * Retorna 1 y copia el evento en *ev si hay datos.
 * Retorna 0 si el ring está vacío.
 * No bloquea — el caller (cdev) gestiona el bloqueo de userspace.
 */
int vtr_ring_pop(struct vtr_ring *r, struct vtr_event *ev);

/*
 * vtr_ring_count — número de eventos disponibles
 * Usado por cdev para decidir si bloquear en read().
 */
size_t vtr_ring_count(struct vtr_ring *r);

/*
 * vtr_ring_is_empty — verificación O(1)
 */
int vtr_ring_is_empty(struct vtr_ring *r);

/*
 * vtr_ring_usage_pct — uso como porcentaje 0-100
 * Para estadísticas — no llamar en el hot path.
 */
uint8_t vtr_ring_usage_pct(struct vtr_ring *r);

#endif /* VTR_RING_H */
