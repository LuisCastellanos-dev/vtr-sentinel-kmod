/*
 * vtr_sentinel.c — módulo principal
 * vtr-sentinel-kmod — VTR-METH-001 v5.1
 *
 * modevent: carga/descarga con rollback completo (lección VTR-RPi-001)
 * cdev /dev/vtr0: canal unidireccional kernel→userspace
 * wire format: vtr_event de 16 bytes exactos
 *
 * CONTRATO DE NO-INTERVENCIÓN:
 * Este módulo solo observa. Nunca modifica el flujo de ejecución.
 * Nunca retorna valor distinto de 0 en hooks de solo lectura.
 */

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/errno.h>
#include <sys/proc.h>

#include "vtr_arch.h"
#include "vtr_event.h"
#include "vtr_ring.h"

/* ── Identificación del módulo ───────────────────────────────────────── */

#define VTR_MODULE_NAME     "vtr_sentinel"
#define VTR_DEV_NAME        "vtr0"
#define VTR_VERSION_MAJOR   0
#define VTR_VERSION_MINOR   1

/* ── Estado global del módulo ────────────────────────────────────────── */

/*
 * Estado en BSS — inicializado a cero por el linker del kernel.
 * Sin malloc para el estado principal — footprint predecible.
 */
static struct vtr_ring      g_ring;
static struct cdev          *g_cdev;
static int                   g_open_count;  /* cuántos fd abiertos */

/* ── Operaciones del cdev ────────────────────────────────────────────── */

static int
vtr_cdev_open(struct cdev *dev, int flags, int fmt, struct thread *td)
{
    (void)dev; (void)flags; (void)fmt; (void)td;

    /*
     * Solo permitir un fd abierto a la vez.
     * El daemon Rust abre /dev/vtr0 una sola vez al inicio.
     * Múltiples lectores romperían el orden FIFO del ring buffer.
     */
    if (g_open_count > 0)
        return EBUSY;

    g_open_count++;
    return 0;
}

static int
vtr_cdev_close(struct cdev *dev, int flags, int fmt, struct thread *td)
{
    (void)dev; (void)flags; (void)fmt; (void)td;

    if (g_open_count > 0)
        g_open_count--;

    return 0;
}

static int
vtr_cdev_read(struct cdev *dev, struct uio *uio, int ioflag)
{
    struct vtr_event ev;
    int error;

    (void)dev; (void)ioflag;

    /*
     * CONTRATO DE FRONTERA — lección VTR-RPi-002:
     * Verificar uio_resid ANTES de copyout.
     * Si el userspace no tiene espacio para 16 bytes exactos,
     * retornar EMSGSIZE — no hacer copyout parcial.
     *
     * En Rust: EventRecord::from_bytes(&[u8; 16]) — exactamente 16.
     */
    if (uio->uio_resid < (ssize_t)sizeof(ev))
        return EMSGSIZE;

    /*
     * Si el ring está vacío, retornar 0 bytes — no bloquear.
     * El daemon Rust usa kqueue para saber cuándo hay datos.
     * Sin bloqueo en cdev — sin deadlock posible.
     */
    if (!vtr_ring_pop(&g_ring, &ev))
        return 0;

    /*
     * Verificar CRC antes de enviar a userspace.
     * Si el evento está corrupto, es evidencia forense — descartarlo
     * y registrar VTR_KIND_HASH_MISMATCH en el siguiente evento.
     *
     * Por ahora: descartar silenciosamente — el daemon detectará
     * la discrepancia en su propia verificación CRC.
     */
    if (vtr_event_verify(&ev) != 0) {
        /* Construir evento de hash mismatch para el próximo read */
        struct vtr_event err_ev;
        vtr_event_build(&err_ev,
            VTR_KIND_HASH_MISMATCH, VTR_SEV_CONFIRMED,
            VTR_SRC_SYSTEM, 0, 0);
        vtr_ring_push(&g_ring, &err_ev);
        return EIO;
    }

    /* copyout exactamente 16 bytes — contrato de wire format */
    error = uiomove(&ev, sizeof(ev), uio);
    return error;
}

static int
vtr_cdev_write(struct cdev *dev, struct uio *uio, int ioflag)
{
    (void)dev; (void)uio; (void)ioflag;

    /*
     * Canal unidireccional — escritura desde userspace prohibida.
     * EPERM: operación no permitida por diseño, no por permisos.
     * Sin ioctl tampoco — sin configuración desde userspace.
     */
    return EPERM;
}

static int
vtr_cdev_ioctl(struct cdev *dev, u_long cmd, caddr_t data,
               int fflag, struct thread *td)
{
    (void)dev; (void)cmd; (void)data; (void)fflag; (void)td;

    /* Sin ioctls — canal de solo lectura */
    return ENOTTY;
}

static struct cdevsw vtr_cdevsw = {
    .d_version  = D_VERSION,
    .d_open     = vtr_cdev_open,
    .d_close    = vtr_cdev_close,
    .d_read     = vtr_cdev_read,
    .d_write    = vtr_cdev_write,
    .d_ioctl    = vtr_cdev_ioctl,
    .d_name     = VTR_DEV_NAME,
};

/* ── modevent — carga y descarga del módulo ──────────────────────────── */

static int
vtr_modevent(struct module *m, int what, void *arg)
{
    struct vtr_event genesis;
    int error;

    (void)m; (void)arg;

    switch (what) {
    case MOD_LOAD:
        /*
         * CONTRATO DE CARGA — lección VTR-RPi-001:
         * Rollback completo si cualquier paso falla.
         * Orden: ring → cdev → hooks
         * Rollback: hooks → cdev → ring (inverso)
         *
         * Si un paso falla a mitad, el sistema queda limpio.
         * Sin recursos parcialmente registrados.
         */

        /* Paso 1: inicializar ring buffer */
        error = vtr_ring_init(&g_ring);
        if (error != 0) {
            printf(VTR_MODULE_NAME ": ring init failed: %d\n", error);
            return error;
        }

        /* Paso 2: crear cdev /dev/vtr0 */
        error = make_dev_p(MAKEDEV_CHECKNAME | MAKEDEV_WAITOK,
                           &g_cdev, &vtr_cdevsw, 0,
                           UID_ROOT, GID_WHEEL, 0440,
                           VTR_DEV_NAME);
        if (error != 0) {
            printf(VTR_MODULE_NAME ": make_dev failed: %d\n", error);
            /* Rollback paso 1 */
            vtr_ring_destroy(&g_ring);
            return error;
        }

        /* Paso 3: registrar hooks (vtr_hooks.c — próxima iteración) */
        /* TODO: vtr_hooks_register() */
        /* Si falla: destroy_dev(g_cdev); vtr_ring_destroy(&g_ring); */

        /* Evento de génesis — primer registro de la sesión */
        g_open_count = 0;
        vtr_event_build(&genesis,
            VTR_KIND_SENTINEL_STARTED, VTR_SEV_OBSERVED,
            VTR_SRC_SYSTEM, 0, 0);
        vtr_ring_push(&g_ring, &genesis);

        printf(VTR_MODULE_NAME " v%d.%d loaded — /dev/%s ready\n",
               VTR_VERSION_MAJOR, VTR_VERSION_MINOR, VTR_DEV_NAME);
        printf(VTR_MODULE_NAME ": arch=%s ring_size=%u footprint=%zuB\n",
               VTR_ARCH_NAME, VTR_RING_SIZE,
               sizeof(g_ring));
        return 0;

    case MOD_UNLOAD:
        /*
         * CONTRATO DE DESCARGA:
         * Si hay un fd abierto, rechazar el unload.
         * El daemon debe cerrar /dev/vtr0 antes de kldunload.
         * Sin forzar — sin corrupción de estado.
         */
        if (g_open_count > 0) {
            printf(VTR_MODULE_NAME ": unload rejected — %d fd open\n",
                   g_open_count);
            return EBUSY;
        }

        /* Rollback en orden inverso al load */
        /* TODO: vtr_hooks_unregister() */
        destroy_dev(g_cdev);
        vtr_ring_destroy(&g_ring);

        printf(VTR_MODULE_NAME " unloaded\n");
        return 0;

    default:
        return EOPNOTSUPP;
    }
}

/* ── Declaración del módulo ──────────────────────────────────────────── */

static moduledata_t vtr_sentinel_mod = {
    VTR_MODULE_NAME,
    vtr_modevent,
    NULL,
};

DECLARE_MODULE(vtr_sentinel, vtr_sentinel_mod, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(vtr_sentinel, 1);
