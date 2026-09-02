/*
 * vtr_hooks.c — syscall, VFS y pfil hooks
 * vtr-sentinel-kmod — VTR-METH-001 v5.1
 *
 * Stub para compilación inicial.
 * Los hooks reales se implementan en la siguiente iteración.
 *
 * CONTRATO DE NO-INTERVENCIÓN:
 * Todos los handlers retornan 0 — nunca modifican el flujo.
 * Nunca bloquean. Nunca alloca en el hot path.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include "vtr_arch.h"
#include "vtr_event.h"
#include "vtr_ring.h"

/*
 * TODO — Fase 2: implementar hooks reales
 *
 * Syscall hook:
 *   Interceptar execve(), open(), mmap(), ioctl()
 *   Detectar: VTR_KIND_UNSIGNED_EXECUTION, VTR_KIND_BOUNDARY_VIOLATION
 *
 * VFS hook:
 *   Interceptar vop_open, vop_write en paths críticos
 *   Detectar: VTR_KIND_POST_SEAL_MODIFICATION, VTR_KIND_HASH_MISMATCH
 *
 * pfil hook:
 *   Capturar paquetes antes del stack TCP/IP
 *   Detectar: VTR_KIND_HIGH_ENTROPY_PAYLOAD, VTR_KIND_DNP3_CRC_INVALID
 */

int
vtr_hooks_register(struct vtr_ring *r)
{
    (void)r;
    /* Stub — retorna 0 para permitir carga del módulo */
    return 0;
}

void
vtr_hooks_unregister(void)
{
    /* Stub */
}
