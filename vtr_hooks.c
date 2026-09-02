/*
 * vtr_hooks.c — EVENTHANDLER hooks (observación pasiva de procesos)
 * vtr-sentinel-kmod — VTR-METH-001 v5.1
 *
 * Implementación Phase 2: observación de eventos de proceso via
 * EVENTHANDLER framework de FreeBSD.
 *
 * CONTRATO DE NO-INTERVENCIÓN:
 * Todos los handlers son void — nunca modifican el flujo.
 * Nunca bloquean. Nunca alloca en el hot path.
 * EVENTHANDLER invoca estos callbacks como observadores puros.
 *
 * Hooks implementados (Phase 2):
 *   process_exec  → VTR_KIND_UNSIGNED_EXECUTION
 *   process_fork  → VTR_KIND_STATE_CONTAMINATION
 *   process_exit  → VTR_KIND_SENTINEL_STOPPED (reuso semántico: fin de proceso)
 *
 * Hooks reservados (Phase 3):
 *   VFS — mac_policy o kprobe equivalente para vnode write/open
 *   pfil — captura de paquetes pre-stack TCP/IP
 *
 * Referencias:
 *   sys/sys/eventhandler.h:241  execlist_fn  (void*, struct proc*, struct image_params*)
 *   sys/kern/kern_exec.c:1137   EVENTHANDLER_DIRECT_INVOKE(process_exec, ...)
 *   sys/kern/kern_proc.c:164    EVENTHANDLER_LIST_DEFINE(process_exec)
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/eventhandler.h>

#include "vtr_arch.h"
#include "vtr_event.h"
#include "vtr_ring.h"

/* ── Estado compartido ───────────────────────────────────────────────── */

/*
 * Puntero al ring buffer principal — inicializado en vtr_hooks_register().
 * Escrito una sola vez antes de registrar handlers.
 * Leído en cada invocación del handler — sin lock sobre el puntero.
 * vtr_ring_push() gestiona su propio spinlock internamente.
 */
static struct vtr_ring *g_ring_ptr = NULL;

/*
 * Handles de EVENTHANDLER — necesarios para EVENTHANDLER_DEREGISTER()
 * en vtr_hooks_unregister(). Sin estos handles no podemos desregistrar.
 */
static eventhandler_tag g_exec_tag  = NULL;
static eventhandler_tag g_fork_tag  = NULL;
static eventhandler_tag g_exit_tag  = NULL;

/*
 * Contador de secuencia — proxy de tiempo relativo en el hot path.
 * El hot path no puede llamar gettimeofday() sin violar el contrato.
 * Resolución: un tick por evento. El daemon Rust tiene timestamp real
 * de recepción vía /dev/vtr0.
 *
 * atomic_fetchadd_32: O(1), sin spinlock, correcto en SMP.
 */
static volatile uint32_t g_event_seq = 0;

/* ── Helper inline — hot path ────────────────────────────────────────── */

/*
 * vtr_push_event — construir y encolar un evento
 *
 * Construye el evento en stack local (sin alloc), luego llama
 * vtr_ring_push() que adquiere su propio spinlock.
 *
 * CONTRATO:
 *   - Sin bloqueo visible al caller
 *   - Sin alloc
 *   - Sin panic posible (guard en g_ring_ptr == NULL)
 *   - Llamable desde cualquier contexto de kernel (incluyendo softirq)
 */
static inline void
vtr_push_event(uint8_t kind, uint8_t severity,
               uint16_t source_id, uint32_t pid)
{
    struct vtr_event ev;
    uint32_t seq;

    if (g_ring_ptr == NULL)
        return;

    seq = atomic_fetchadd_32(&g_event_seq, 1);
    vtr_event_build(&ev, kind, severity, source_id, pid, seq);
    vtr_ring_push(g_ring_ptr, &ev);
}

/* ── Handler: process_exec ───────────────────────────────────────────── */

/*
 * vtr_on_exec — observar ejecución de binarios
 *
 * Firma requerida: execlist_fn = void (*)(void*, struct proc*, struct image_params*)
 * Invocado desde kern_exec.c:1137 después de que el nuevo binario
 * está cargado pero antes de que el proceso retorne al userspace.
 *
 * Detecta: VTR_KIND_UNSIGNED_EXECUTION
 *   En OT/ICS, todo exec inesperado es un evento observable.
 *   Phase 3 añadirá verificación de hash del binario via imgp.
 *
 * Severity: OBSERVED — registramos la ocurrencia, no juzgamos.
 */
static void
vtr_on_exec(void *arg, struct proc *p, struct image_params *imgp)
{
    (void)arg; (void)imgp;

    vtr_push_event(
        VTR_KIND_UNSIGNED_EXECUTION,
        VTR_SEV_OBSERVED,
        VTR_SRC_SYSCALL,
        (uint32_t)p->p_pid
    );
}

/* ── Handler: process_fork ───────────────────────────────────────────── */

/*
 * vtr_on_fork — observar creación de procesos hijos
 *
 * Firma requerida: forklist_fn = void (*)(void*, struct proc*, struct proc*, int)
 * p1 = proceso padre, p2 = proceso hijo, flags = flags del fork.
 *
 * Detecta: VTR_KIND_STATE_CONTAMINATION
 *   Fork propaga el espacio de direcciones del padre al hijo.
 *   En contexto OT: un proceso de control que forkea inesperadamente
 *   puede indicar contaminación de estado o ejecución de código ajeno.
 *
 * Registramos el PID del hijo — el padre es inferible vía p_pptr en userspace.
 */
static void
vtr_on_fork(void *arg, struct proc *p1, struct proc *p2, int flags)
{
    (void)arg; (void)p1; (void)flags;

    vtr_push_event(
        VTR_KIND_STATE_CONTAMINATION,
        VTR_SEV_OBSERVED,
        VTR_SRC_SYSCALL,
        (uint32_t)p2->p_pid
    );
}

/* ── Handler: process_exit ───────────────────────────────────────────── */

/*
 * vtr_on_exit — observar terminación de procesos
 *
 * Firma requerida: exitlist_fn = void (*)(void*, struct proc*)
 *
 * Detecta: VTR_KIND_SENTINEL_STOPPED (reuso semántico)
 *   Un proceso OT que termina inesperadamente es un evento de seguridad.
 *   Phase 3 añadirá correlación con el PID del proceso de control esperado.
 *
 * Nota: VTR_KIND_SENTINEL_STOPPED es el kind más cercano a "proceso terminado"
 * en el vocabulario actual. Phase 3 puede agregar VTR_KIND_PROCESS_EXIT
 * como kind dedicado si el volumen de eventos lo justifica.
 */
static void
vtr_on_exit(void *arg, struct proc *p)
{
    (void)arg;

    vtr_push_event(
        VTR_KIND_SENTINEL_STOPPED,
        VTR_SEV_OBSERVED,
        VTR_SRC_SYSCALL,
        (uint32_t)p->p_pid
    );
}

/* ── API pública ─────────────────────────────────────────────────────── */

int
vtr_hooks_register(struct vtr_ring *r)
{
    /*
     * Orden crítico:
     *   1. g_ring_ptr = r         — ring disponible antes de activar handlers
     *   2. EVENTHANDLER_REGISTER  — handlers activos
     *
     * Si el handler dispara entre 1 y 2: imposible, somos single-threaded
     * durante MOD_LOAD. Pero el guard NULL en vtr_push_event protege de
     * cualquier race futuro.
     *
     * EVENTHANDLER_PRI_ANY: prioridad estándar — sin necesidad de orden
     * específico respecto a otros handlers del sistema.
     */
    g_ring_ptr = r;

    g_exec_tag = EVENTHANDLER_REGISTER(process_exec, vtr_on_exec,
                                        NULL, EVENTHANDLER_PRI_ANY);
    g_fork_tag = EVENTHANDLER_REGISTER(process_fork, vtr_on_fork,
                                        NULL, EVENTHANDLER_PRI_ANY);
    g_exit_tag = EVENTHANDLER_REGISTER(process_exit, vtr_on_exit,
                                        NULL, EVENTHANDLER_PRI_ANY);

    if (g_exec_tag == NULL || g_fork_tag == NULL || g_exit_tag == NULL) {
        /* Rollback parcial si algún registro falla */
        if (g_exec_tag != NULL)
            EVENTHANDLER_DEREGISTER(process_exec, g_exec_tag);
        if (g_fork_tag != NULL)
            EVENTHANDLER_DEREGISTER(process_fork, g_fork_tag);
        if (g_exit_tag != NULL)
            EVENTHANDLER_DEREGISTER(process_exit, g_exit_tag);

        g_exec_tag = g_fork_tag = g_exit_tag = NULL;
        g_ring_ptr = NULL;

        printf("vtr_sentinel: EVENTHANDLER_REGISTER failed\n");
        return ENOMEM;
    }

    printf("vtr_sentinel: process hooks registered "
           "(exec, fork, exit)\n");
    return 0;
}

void
vtr_hooks_unregister(void)
{
    if (g_ring_ptr == NULL)
        return;

    /*
     * Orden de descarga — inverso al registro:
     *   1. EVENTHANDLER_DEREGISTER — desactivar handlers
     *   2. g_ring_ptr = NULL       — invalidar puntero
     *
     * EVENTHANDLER_DEREGISTER garantiza que ningún handler
     * en vuelo puede ejecutarse después de retornar.
     * Por eso g_ring_ptr = NULL va DESPUÉS.
     */
    EVENTHANDLER_DEREGISTER(process_exec, g_exec_tag);
    EVENTHANDLER_DEREGISTER(process_fork, g_fork_tag);
    EVENTHANDLER_DEREGISTER(process_exit, g_exit_tag);

    g_exec_tag = g_fork_tag = g_exit_tag = NULL;
    g_ring_ptr = NULL;

    printf("vtr_sentinel: process hooks unregistered\n");
}
