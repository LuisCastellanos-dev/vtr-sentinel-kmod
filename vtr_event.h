/*
 * vtr_event.h — wire format EventRecord
 * vtr-sentinel-kmod — VTR-METH-001 v5.1
 *
 * CONTRATO DE FRONTERA CRÍTICO:
 * Este archivo define el layout que el módulo de kernel produce
 * y que el daemon Rust consume vía /dev/vtr0.
 *
 * Cualquier cambio aquí DEBE reflejarse en:
 *   src/event/record.rs — EventRecord
 *   src/event/kind.rs   — EventKind, EventSeverity
 *
 * CRC-32 IEEE 802.3 — parámetros completos del algoritmo:
 *   Polinomio:   0xEDB88320 (reflected)
 *   Init:        0xFFFFFFFF
 *   XorOut:      0xFFFFFFFF
 *   RefIn:       true
 *   RefOut:      true
 * Mismo algoritmo que Crc32::compute() en record.rs.
 * Test de contrato obligatorio antes de integrar.
 *
 * Layout (16 bytes, little-endian, sin padding):
 *   [0]      kind      — EventKind (u8)
 *   [1]      severity  — EventSeverity (u8): 0=Observed,1=Probable,2=Confirmed
 *   [2..3]   source_id — SourceId (u16 LE)
 *   [4..7]   pid       — process id (u32 LE)
 *   [8..11]  seq_delta — sequence counter since module load (u32 LE, monotonic)
 *   [12..15] checksum  — CRC-32 IEEE 802.3 de bytes [0..11] (u32 LE)
 */

#ifndef VTR_EVENT_H
#define VTR_EVENT_H

#include <sys/types.h>

/* ── Layout de EventRecord ───────────────────────────────────────────── */

struct vtr_event {
    uint8_t  kind;
    uint8_t  severity;
    uint16_t source_id;
    uint32_t pid;
    uint32_t seq_delta;  /* monotonic sequence counter — NOT milliseconds */
    uint32_t checksum;
} __attribute__((packed));

/*
 * Verificaciones en compile-time.
 * Cualquier cambio en el struct que altere tamaño u offsets
 * rompe el build — las mismas propiedades deben verificarse
 * en el lado Rust con tests cruzados (Phase 2).
 */
_Static_assert(
    sizeof(struct vtr_event) == 16,
    "vtr_event must be 16 bytes — must match Rust EventRecord"
);
_Static_assert(
    __builtin_offsetof(struct vtr_event, kind)      == 0,
    "vtr_event.kind must be at offset 0"
);
_Static_assert(
    __builtin_offsetof(struct vtr_event, severity)  == 1,
    "vtr_event.severity must be at offset 1"
);
_Static_assert(
    __builtin_offsetof(struct vtr_event, source_id) == 2,
    "vtr_event.source_id must be at offset 2"
);
_Static_assert(
    __builtin_offsetof(struct vtr_event, pid)       == 4,
    "vtr_event.pid must be at offset 4"
);
_Static_assert(
    __builtin_offsetof(struct vtr_event, seq_delta) == 8,
    "vtr_event.seq_delta must be at offset 8"
);
_Static_assert(
    __builtin_offsetof(struct vtr_event, checksum)  == 12,
    "vtr_event.checksum must be at offset 12"
);

/* ── EventKind — debe coincidir con src/event/kind.rs repr(u8) ─────── */

/* Familia Memory — Range 0x00–0x0F */
#define VTR_KIND_ALLOC_WITHOUT_RELEASE   0x00
#define VTR_KIND_IOCTL_ROLLBACK_MISSING  0x01
#define VTR_KIND_BOUNDARY_VIOLATION      0x02
#define VTR_KIND_REFCOUNT_LEAK           0x03
#define VTR_KIND_USE_AFTER_ERROR         0x04
#define VTR_KIND_STACK_LAYOUT_VIOLATION  0x05

/* Familia Contract — Range 0x10–0x1F */
#define VTR_KIND_IMPLICIT_CONTRACT       0x10
#define VTR_KIND_UNTRUSTED_INPUT         0x11
#define VTR_KIND_EXPORT_SYMBOL_IMPLICIT  0x12
#define VTR_KIND_UNDOCUMENTED_DELEGATION 0x13
#define VTR_KIND_STATEFUL_API_IMPLICIT   0x14

/* Familia State — Range 0x20–0x2F */
#define VTR_KIND_STATE_CONTAMINATION     0x20
#define VTR_KIND_SINGLETON_REUSE         0x21
#define VTR_KIND_MUTABLE_LIMIT_EXPOSED   0x22
#define VTR_KIND_INVALID_STATE_TRANS     0x23
#define VTR_KIND_STATE_RACE_CONDITION    0x24
#define VTR_KIND_GLOBAL_STATE_RESIDUAL   0x25

/* Familia DoS — Range 0x30–0x3F */
#define VTR_KIND_ALGORITHMIC_DOS         0x30
#define VTR_KIND_UNBOUNDED_LOOP          0x31
#define VTR_KIND_UNBOUNDED_RECURSION     0x32
#define VTR_KIND_EVENT_RATE_ANOMALY      0x33
#define VTR_KIND_MEMORY_GROWTH_ANOMALY   0x34

/* Familia Provenance — Range 0x40–0x4F */
#define VTR_KIND_PROVENANCE_BROKEN       0x40
#define VTR_KIND_UNSEALED_DEPENDENCY     0x41
#define VTR_KIND_HASH_MISMATCH           0x42
#define VTR_KIND_NON_REPRODUCIBLE_BUILD  0x43
#define VTR_KIND_TROJAN_SOURCE           0x44
#define VTR_KIND_UNSIGNED_EXECUTION      0x45
#define VTR_KIND_POST_SEAL_MODIFICATION  0x46

/* Familia Network — Range 0x50–0x5F */
#define VTR_KIND_HIGH_ENTROPY_PAYLOAD    0x50
#define VTR_KIND_DNP3_CRC_INVALID        0x51
#define VTR_KIND_DNP3_UNKNOWN_FUNCTION   0x52
#define VTR_KIND_DNP3_UNAUTHORIZED       0x53
#define VTR_KIND_TLS_OBSOLETE_VERSION    0x54
#define VTR_KIND_TLS_WEAK_CIPHER         0x55
#define VTR_KIND_TLS_MISSING_SNI         0x56
#define VTR_KIND_DNS_RATE_ANOMALY        0x57
#define VTR_KIND_DNS_LABEL_ANOMALY       0x58
#define VTR_KIND_HTTP_ANOMALOUS_REQUEST  0x59

/* Familia System — Range 0x60–0x6F */
#define VTR_KIND_SENTINEL_STARTED        0x60
#define VTR_KIND_SENTINEL_STOPPED        0x61
#define VTR_KIND_PROBE_RESTARTED         0x62
#define VTR_KIND_RING_BUFFER_OVERFLOW    0x63
#define VTR_KIND_CUSTODY_SEQUENCE_JUMP   0x64
#define VTR_KIND_PLEDGE_VIOLATION        0x65
#define VTR_KIND_CONFIG_RELOADED         0x66

/* ── EventSeverity ───────────────────────────────────────────────────── */

#define VTR_SEV_OBSERVED   0
#define VTR_SEV_PROBABLE   1
#define VTR_SEV_CONFIRMED  2

/* ── SourceId ────────────────────────────────────────────────────────── */

#define VTR_SRC_SYSTEM     0x0000
#define VTR_SRC_KQUEUE     0x0001
#define VTR_SRC_DNP3       0x0002
#define VTR_SRC_DNS        0x0003
#define VTR_SRC_TLS        0x0004
#define VTR_SRC_HTTP       0x0005
#define VTR_SRC_CUSTODY    0x0006
/* Kernel space únicamente */
#define VTR_SRC_SYSCALL    0x0007
#define VTR_SRC_VFS        0x0008
#define VTR_SRC_PFIL       0x0009

/* ── CRC-32 IEEE 802.3 ───────────────────────────────────────────────── */

/*
 * Parámetros del algoritmo (deben coincidir con ambos lados del contrato):
 *   Polinomio:  0xEDB88320 (reflected)
 *   Init:       0xFFFFFFFF
 *   XorOut:     0xFFFFFFFF
 *   RefIn:      true
 *   RefOut:     true
 *
 * Responsabilidad: integridad del EventRecord.
 * Tabla precalculada en vtr_ring.c — no alloca en el hot path.
 * Declaración extern — definición en vtr_ring.c.
 *
 * Nota: vtr_ring provee bounded retention + sincronización.
 *       El CRC pertenece al evento, no al ring.
 */
extern const uint32_t vtr_crc32_table[256];

static inline uint32_t
vtr_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc = vtr_crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFU;
}

/*
 * Construir y verificar un vtr_event de forma portable.
 * El checksum cubre los primeros 12 bytes — igual que Rust.
 */
static inline void
vtr_event_build(struct vtr_event *ev,
                uint8_t kind, uint8_t severity,
                uint16_t source_id, uint32_t pid,
                uint32_t seq_delta)
{
    uint8_t *b = (uint8_t *)ev;

    b[0] = kind;
    b[1] = severity;
    uint16_t sid_le = htole16(source_id);
    uint32_t pid_le = htole32(pid);
    uint32_t seq_le = htole32(seq_delta);

    __builtin_memcpy(&b[2], &sid_le, 2);
    __builtin_memcpy(&b[4], &pid_le, 4);
    __builtin_memcpy(&b[8], &seq_le, 4);

    uint32_t crc = vtr_crc32(b, 12);
    uint32_t crc_le = htole32(crc);
    __builtin_memcpy(&b[12], &crc_le, 4);
}

static inline int
vtr_event_verify(const struct vtr_event *ev)
{
    const uint8_t *b = (const uint8_t *)ev;
    uint32_t expected = vtr_crc32(b, 12);
    uint32_t stored;
    __builtin_memcpy(&stored, &b[12], 4);
    stored = le32toh(stored);
    return (expected == stored) ? 0 : -1;
}

#endif /* VTR_EVENT_H */
