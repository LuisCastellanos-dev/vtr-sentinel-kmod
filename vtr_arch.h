/*
 * vtr_arch.h — portability layer
 * vtr-sentinel-kmod — VTR-METH-001 v5.1
 *
 * Controla todas las diferencias de arquitectura en un solo archivo.
 * El código del módulo usa estas macros — nunca arquitectura directa.
 *
 * Arquitecturas soportadas:
 *   x86_64  (amd64)  — Pentium Silver, Core i3/i5
 *   aarch64          — Cortex-A53 (RPi 3/4, OrangePi)
 *   arm              — Cortex-M4 (embebido, microcontroladores)
 *   riscv            — futuro hardware
 */

#ifndef VTR_ARCH_H
#define VTR_ARCH_H

#include <sys/param.h>
#include <sys/endian.h>
#include <sys/types.h>
#include <sys/mutex.h>

/* ── Escritura/lectura portable — sin acceso no alineado ─────────────────
 *
 * En ARM Cortex-A53/A72, accesos no alineados a uint16_t/uint32_t
 * causan fault de bus. __attribute__((packed)) no garantiza acceso seguro.
 * Usamos memcpy() — el compilador genera instrucciones correctas por arch.
 */

static inline void
vtr_write_u16(uint8_t *dst, uint16_t val)
{
    val = htole16(val);     /* no-op en LE, byte-swap en BE (MIPS) */
    __builtin_memcpy(dst, &val, sizeof(val));
}

static inline void
vtr_write_u32(uint8_t *dst, uint32_t val)
{
    val = htole32(val);
    __builtin_memcpy(dst, &val, sizeof(val));
}

static inline uint16_t
vtr_read_u16(const uint8_t *src)
{
    uint16_t val;
    __builtin_memcpy(&val, src, sizeof(val));
    return le16toh(val);
}

static inline uint32_t
vtr_read_u32(const uint8_t *src)
{
    uint32_t val;
    __builtin_memcpy(&val, src, sizeof(val));
    return le32toh(val);
}

/* ── Spinlock portable ────────────────────────────────────────────────────
 *
 * SMP: usar mtx spin — correcto en x86_64 y aarch64 multi-core
 * UP:  usar critical_enter/exit — sin overhead de SMP
 *
 * El Pentium Silver J5005 tiene 4 cores reales — SMP path.
 * Cortex-M4 típicamente UP — UP path.
 */

typedef struct mtx vtr_lock_t;

static inline void
vtr_lock_init(vtr_lock_t *l, const char *name)
{
    mtx_init(l, name, NULL, MTX_SPIN);
}

static inline void
vtr_lock_acquire(vtr_lock_t *l)
{
    mtx_lock_spin(l);
}

static inline void
vtr_lock_release(vtr_lock_t *l)
{
    mtx_unlock_spin(l);
}

static inline void
vtr_lock_destroy(vtr_lock_t *l)
{
    mtx_destroy(l);
}

/* ── Ring buffer size por arquitectura ───────────────────────────────────
 *
 * Calculado para caber en L1 data cache del target.
 * EventRecord = 16 bytes. N debe ser potencia de 2.
 *
 * Pentium Silver J5005: L1D = 32KB → 2048 × 16 = 32KB exactos
 * Cortex-A53:           L1D = 32KB → 1024 × 16 = 16KB (margen OS)
 * Cortex-M4:            L1D = 16KB →  256 × 16 =  4KB (conservador)
 * RISC-V (genérico):    L1D = 32KB → 1024 × 16 = 16KB (conservador)
 */

#if defined(__amd64__)
#  define VTR_RING_SIZE     2048U
#  define VTR_ARCH_NAME     "x86_64"
#  define VTR_L1_CACHE_KB   32U

#elif defined(__aarch64__)
#  define VTR_RING_SIZE     1024U
#  define VTR_ARCH_NAME     "aarch64"
#  define VTR_L1_CACHE_KB   32U

#elif defined(__arm__)
#  define VTR_RING_SIZE      256U
#  define VTR_ARCH_NAME     "arm"
#  define VTR_L1_CACHE_KB   16U

#elif defined(__riscv)
#  define VTR_RING_SIZE     1024U
#  define VTR_ARCH_NAME     "riscv"
#  define VTR_L1_CACHE_KB   32U

#else
/* Safe default para arquitectura desconocida */
#  define VTR_RING_SIZE      256U
#  define VTR_ARCH_NAME     "unknown"
#  define VTR_L1_CACHE_KB   16U
#  warning "vtr_arch.h: arquitectura no reconocida — usando defaults conservadores"
#endif

/* Verificación en compile-time: N debe ser potencia de 2 */
_Static_assert(
    (VTR_RING_SIZE & (VTR_RING_SIZE - 1)) == 0,
    "VTR_RING_SIZE must be power of 2"
);

/* Verificación: ring buffer debe caber en L1 */
_Static_assert(
    VTR_RING_SIZE * 16U <= VTR_L1_CACHE_KB * 1024U,
    "VTR ring buffer must fit in L1 cache"
);

/* ── Overhead máximo por evento en el hot path ───────────────────────────
 *
 * En el Pentium Silver @ 1.5GHz base:
 *   500ns × 1.5GHz = 750 ciclos máximos por evento
 *
 * Desglose:
 *   CRC-32 sobre 12 bytes:      ~48 ciclos
 *   Escritura en ring buffer:    ~8 ciclos
 *   Spinlock acquire/release:   ~20 ciclos
 *   Total estimado:             ~76 ciclos  ✓
 */
#define VTR_MAX_CYCLES_PER_EVENT 750U

/* ── Prototipos de hooks (stub — Fase 2) ──────────────────────────────── */
struct vtr_ring;
int  vtr_hooks_register(struct vtr_ring *r);
void vtr_hooks_unregister(void);

#endif /* VTR_ARCH_H */
