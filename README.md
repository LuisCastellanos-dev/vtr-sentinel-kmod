# vtr-sentinel-kmod

[![FreeBSD 14.4](https://img.shields.io/badge/FreeBSD-14.4--RELEASE--p8-red?logo=freebsd)](https://www.freebsd.org/)
[![Architecture](https://img.shields.io/badge/arch-amd64-blue)](https://github.com/LuisCastellanos-dev/vtr-sentinel-kmod)
[![Status](https://img.shields.io/badge/status-Phase%201%20complete-yellow)](https://github.com/LuisCastellanos-dev/vtr-sentinel-kmod)
[![License](https://img.shields.io/badge/license-BSD--2--Clause-green)](LICENSE)

> Read-only kernel event monitor for FreeBSD 14 — OT/ICS security telemetry module.  
> Developed by [Vector Telemetry Research (VTR)](https://github.com/LuisCastellanos-dev) for critical infrastructure environments.

---

## Overview

`vtr-sentinel-kmod` is a FreeBSD kernel module (`.ko`) that passively monitors kernel events and surfaces security-relevant telemetry without intervening in the monitored flow.

**Non-intervention contract:** all handlers return `0`. They never block, never modify execution flow, and never allocate on the hot path.

The module is the kernel-space half of a larger VTR telemetry pipeline. Events are structured as `vtr_event` — a 16-byte packed wire format shared with the userspace Rust daemon ([vtr-sentinel](https://github.com/LuisCastellanos-dev/vtr-sentinel)) via a binary contract enforced on both sides.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        FreeBSD Kernel                            │
│                                                                  │
│  syscall hook ─┐                                                 │
│  VFS hook     ─┼──► vtr_ring               ──► /dev/vtr0        │
│  pfil hook    ─┘    (bounded retention         (Phase 3)        │
│                      + synchronization)                          │
│                                                                  │
│  vtr_event: 16 bytes, little-endian, packed                      │
│  integrity:  CRC-32 IEEE 802.3, poly 0xEDB88320                  │
└──────────────────────────────┬──────────────────────────────────┘
                               │  /dev/vtr0 (Phase 3)
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                    userspace — vtr-sentinel                       │
│                                                                  │
│  EventRecord: same 16-byte layout, same CRC-32 verification      │
│  poly 0xEDB88320 · init 0xFFFFFFFF · xorout 0xFFFFFFFF          │
│  RefIn: true · RefOut: true                                      │
│                                                                  │
│  EventRecord → RingBuffer → custody chain                        │
└─────────────────────────────────────────────────────────────────┘
```

**Responsibilities:**

- `vtr_event` — integrity via CRC-32 (covers bytes [0..11])
- `vtr_ring` — bounded retention and synchronization
- `/dev/vtr0` — kernel/userspace transport boundary (Phase 3)

---

## Event Model

Each event is a 16-byte packed struct with CRC-32 over the first 12 bytes:

| Offset | Field       | Type     | Description                                   |
|--------|-------------|----------|-----------------------------------------------|
| 0      | `kind`      | `u8`     | Event type (see families below)               |
| 1      | `severity`  | `u8`     | `0`=Observed · `1`=Probable · `2`=Confirmed   |
| 2–3    | `source_id` | `u16 LE` | Probe source                                  |
| 4–7    | `pid`       | `u32 LE` | Process ID                                    |
| 8–11   | `ts_delta`  | `u32 LE` | Milliseconds since previous event             |
| 12–15  | `checksum`  | `u32 LE` | CRC-32 IEEE 802.3 of bytes [0..11]            |

Field offsets are enforced at compile time via `_Static_assert` on both `offsetof` and `sizeof`. Any layout change breaks the build on the C side; cross-language contract tests (Phase 2) will enforce the same on the Rust side.

### Event Families

| Family     | Range       | Count | Examples                                                   |
|------------|-------------|-------|------------------------------------------------------------|
| Memory     | `0x00–0x0F` | 6     | `BOUNDARY_VIOLATION`, `USE_AFTER_ERROR`, `REFCOUNT_LEAK`   |
| Contract   | `0x10–0x1F` | 5     | `UNTRUSTED_INPUT`, `UNDOCUMENTED_DELEGATION`               |
| State      | `0x20–0x2F` | 6     | `STATE_RACE_CONDITION`, `INVALID_STATE_TRANS`              |
| DoS        | `0x30–0x3F` | 5     | `UNBOUNDED_LOOP`, `EVENT_RATE_ANOMALY`                     |
| Provenance | `0x40–0x4F` | 7     | `HASH_MISMATCH`, `UNSIGNED_EXECUTION`, `TROJAN_SOURCE`     |
| Network    | `0x50–0x5F` | 10    | `DNP3_CRC_INVALID`, `TLS_WEAK_CIPHER`, `DNS_LABEL_ANOMALY` |
| System     | `0x60–0x6F` | 7     | `RING_BUFFER_OVERFLOW`, `PLEDGE_VIOLATION`                 |

**Total: 46 event kinds.** Full definitions and range comments in [`vtr_event.h`](vtr_event.h).

---

## Wire Contract

`vtr_event.h` is a shared contract between kernel C and userspace Rust:

```
vtr_event.h  ↔  src/event/record.rs  (EventRecord)
                src/event/kind.rs    (EventKind, EventSeverity)
```

**CRC-32 parameters (both sides must match exactly):**

| Parameter  | Value        |
|------------|--------------|
| Polynomial | `0xEDB88320` |
| Init       | `0xFFFFFFFF` |
| XorOut     | `0xFFFFFFFF` |
| RefIn      | `true`       |
| RefOut     | `true`       |

Any change to field layout, offsets, or CRC parameters must be reflected on both sides. The C side enforces `sizeof` and `offsetof` at compile time. Cross-language byte-level tests are planned for Phase 2.

---

## Build

### Requirements

- FreeBSD 14.x with kernel headers (`/usr/src/sys`)
- `clang` (system default on FreeBSD 14)
- `make`

### Compile on the target FreeBSD machine

```sh
cd /path/to/vtr-sentinel-kmod
make
```

### Remote build from Linux (via SSH)

The compiler, kernel headers, and build environment remain on the FreeBSD target. This is a remote build, not cross-compilation — the compilation context is FreeBSD throughout.

```sh
# Copy sources to FreeBSD host
scp Makefile vtr_arch.h vtr_event.h vtr_ring.h \
    vtr_ring.c vtr_sentinel.c vtr_hooks.c \
    root@<freebsd-host>:/tmp/vtr-sentinel-kmod/

# Build remotely
ssh root@<freebsd-host> "cd /tmp/vtr-sentinel-kmod && make 2>&1"
```

Tested on **FreeBSD 14.4-RELEASE-p8 / amd64**.

### Verify the build artifact

```sh
# On the FreeBSD host, after make
sha256 vtr_sentinel.ko
```

Record the SHA-256 and verify it matches on any validation node before loading.

---

## Load / Unload

```sh
# Load
kldload ./vtr_sentinel.ko

# Verify
kldstat | grep vtr

# Unload
kldunload vtr_sentinel
```

---

## Development Roadmap

| Phase | Status | Description |
|-------|--------|-------------|
| **1 — Stub** | ✅ Complete | Module loads cleanly. Ring buffer, event wire format, CRC-32, compile-time layout assertions. |
| **2 — Hooks** | ✅ Complete | EVENTHANDLER process hooks (exec, fork, exit). Verified on FreeBSD 14.4-RELEASE-p8: fork→exec sequence captured in /dev/vtr0, wire format correct, CRC-32 intact. |
| **3 — Device** | 🔲 Planned | `/dev/vtr0` character device. Userspace Rust daemon integration. |
| **4 — OT Probes** | 🔲 Planned | DNP3, Modbus, and ICS-specific event detection. |

---

## Lab Environment

Developed and validated in the **VTR three-plane lab**:

| Plane | Node | OS |
|-------|------|----|
| Development | Linux Mint workstation | Linux |
| **Experimental** | Dell bare metal (`dell-bsd`) | FreeBSD 14.4-RELEASE-p8 |
| Validation | Parrot OS VM | Parrot OS Echo |

Nodes connected via Tailscale (site mesh, `--accept-dns=false`).

---

## License

BSD 2-Clause. See [LICENSE](LICENSE).

---

*Vector Telemetry Research — OT/ICS Security · Tampico, Tamaulipas, México*
