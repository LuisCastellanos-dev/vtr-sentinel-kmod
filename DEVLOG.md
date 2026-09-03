# vtr-sentinel-kmod — Development Log

Engineering notes, FreeBSD-specific findings, and architecture decisions.
Each entry records what was observed, what was decided, and why.

Format: `[date] — Category: title`
Categories: FINDING, DECISION, VERIFIED, FIXED

---

## [2026-09-02] — FINDING: MODULE_AUTHOR/DESCRIPTION/LICENSE are Linux-only macros

**Context:** First compilation attempt on FreeBSD 14.4-RELEASE-p8.

**Observation:** `MODULE_AUTHOR()`, `MODULE_DESCRIPTION()`, and `MODULE_LICENSE()`
are Linux kernel macros. They do not exist in FreeBSD's kernel headers.
The compiler interpreted them as malformed function declarations, producing
12 parse errors.

**Fix:** Removed the three macros. FreeBSD module metadata goes in
`DECLARE_MODULE()` + `MODULE_VERSION()`. No equivalent for author/description
in FreeBSD kmod ABI.

**Lesson:** FreeBSD and Linux kmod APIs share no compatibility layer.
Never assume macro portability between the two.

---

## [2026-09-02] — FINDING: `<sys/vnode.h>` is not includable from an external kmod

**Context:** Phase 2 — first attempt at VFS hooks via `mac_policy`.

**Observation:** `<sys/vnode.h>` includes `"vnode_if.h"` at line 619.
`vnode_if.h` is a generated file — it is produced during a full kernel build
via `makeobjops.awk` and is not present in `/usr/src/sys` for external modules.

**Error:**
```
/usr/src/sys/sys/vnode.h:619:10: fatal error: 'vnode_if.h' file not found
```

**Fix:** Removed `#include <sys/vnode.h>` from `vtr_hooks.c`.
VFS hooks requiring vnode access need a different approach for external kmods.

**Impact on design:** VFS hooks (Phase 3) cannot use vnode directly.
Options: `dtrace SDT probes`, `kprobe`-equivalent, or building as part of
a kernel config rather than a loadable module.

---

## [2026-09-02] — FINDING: `mac_policy` is not dynamically registrable from an external kmod

**Context:** Phase 2 — attempt to use TrustedBSD MAC framework for
`proc_check_exec` and `vnode_check_open` hooks.

**Observation:** FreeBSD 14's MAC framework does not expose
`mac_policy_register()` / `mac_policy_unregister()` as public KPI for
external modules. The registration mechanism is `MAC_POLICY_SET()`, a macro
that generates a `DECLARE_MODULE()` with `mac_policy_modevent` as the handler.

This means a MAC policy **is** the module — it cannot be registered from
inside another module's `modevent`. The `struct mac_policy_conf` in FreeBSD 14
also lacks `mpc_labelnames` and `mpc_labelname_count` fields that exist in
older versions and other BSDs.

**Errors observed:**
```
error: field designator 'mpc_labelnames' does not refer to any field
error: unknown type name 'mac_policy_handle_t'
error: call to undeclared function 'mac_policy_register'
```

**Decision:** Abandoned `mac_policy` for Phase 2. See DECISION entry below.

**Reference:** `/usr/src/sys/security/mac/mac_policy.h` lines 1009–1058.
MAC_VERSION = 6 for FreeBSD 14.x (was 5 for FreeBSD 13.x).

---

## [2026-09-02] — DECISION: EVENTHANDLER over mac_policy for Phase 2 process hooks

**Context:** After confirming mac_policy is not viable for an external kmod.

**Options evaluated:**
1. `syscall_register()` — modifies syscall table, violates non-intervention contract
2. `mac_policy` via `MAC_POLICY_SET()` — requires the module to BE the policy, incompatible with vtr_sentinel's own modevent
3. `EVENTHANDLER` framework — pure observation, no intervention, external kmod friendly
4. `DTrace SDT probes` — more complex, deferred to Phase 3

**Decision:** `EVENTHANDLER` framework. Rationale:
- `process_exec`, `process_fork`, `process_exit` are declared in `<sys/eventhandler.h>`
- Callbacks are `void` — structurally cannot intervene
- `EVENTHANDLER_REGISTER` / `EVENTHANDLER_DEREGISTER` are public KPI
- Signatures verified against kernel source before implementation:
  - `execlist_fn`: `void (*)(void*, struct proc*, struct image_params*)`
  - `forklist_fn`: `void (*)(void*, struct proc*, struct proc*, int)`
  - `exitlist_fn`: `void (*)(void*, struct proc*)`
- Invocation confirmed at `kern/kern_exec.c:1137`

**Trade-off:** EVENTHANDLER covers process lifecycle, not individual syscalls.
Per-syscall granularity requires a different mechanism (Phase 3 research item).

---

## [2026-09-02] — DECISION: Remote build, not cross-compilation

**Context:** Build workflow from Linux Mint to FreeBSD 14.4-RELEASE-p8.

**Observation:** The build pipeline uses `scp` to transfer sources to the
FreeBSD host and `ssh` to invoke `make` there. The compiler, kernel headers,
`kmod.mk`, and all build tooling run on the FreeBSD target.

**Decision:** This is a **remote build**, not cross-compilation.
Cross-compilation would mean compiling on Linux with a FreeBSD-targeting
toolchain. Here, the compilation context is FreeBSD throughout.

**Why it matters:** This project's research context (compilation context as
a security variable) requires precise terminology. The compilation environment
determines what the binary actually is — calling a FreeBSD-native build
"cross-compilation" would misrepresent the compilation context.

---

## [2026-09-02] — FINDING: Module footprint is 32848B, not 32768B

**Context:** `dmesg` output after `kldload`:
```
vtr_sentinel: arch=x86_64 ring_size=2048 footprint=32848B
```

**Observation:** `sizeof(struct vtr_ring)` = 32848 bytes, not 32768 (32KB).
The difference is 80 bytes of metadata: `head`, `tail`, `count` (volatile
`size_t` × 3 = 24B), `lock` (`struct mtx` = 40B on amd64), `total_in`,
`total_out`, `overflows` (`uint64_t` × 3 = 24B). Total metadata ≈ 88B
(with alignment).

**Impact on architecture claims:**
- CORRECT: "ring buffer payload is compile-time bounded" (2048 × 16 = 32768B)
- INCORRECT: "ring buffer fits in L1" — the full struct exceeds 32KB L1D
- The `_Static_assert` in `vtr_arch.h` verifies payload size, not total struct

**Decision:** Documentation and README use "payload acotado en compile-time",
not "cabe en L1". The L1 assertion applies to the event array only.

---

## [2026-09-02] — VERIFIED: Phase 2 hooks produce correct wire format

**Context:** After `kldload` with Phase 2 hooks, triggered by `ssh root@dell-bsd "ls /tmp"`.

**Output from `/dev/vtr0` (hexdump -C, 4 events):**
```
00000000  61 00 00 00 00 00 00 00  00 00 00 00 71 08 86 90  genesis
00000010  20 00 07 00 2d 0a 00 00  01 00 00 00 50 fd c7 db  fork pid=2605
00000020  45 00 07 00 2d 0a 00 00  02 00 00 00 b4 fd c3 5d  exec pid=2605
00000030  20 00 07 00 2e 0a 00 00  03 00 00 00 1d 1b 78 2a  fork pid=2606
```

**Verified:**
- Genesis event: `kind=0x60` (SENTINEL_STARTED), `src=0x0000` (SYSTEM), `pid=0` ✓
- Fork event: `kind=0x20` (STATE_CONTAMINATION), `src=0x0007` (SYSCALL) ✓
- Exec event: `kind=0x45` (UNSIGNED_EXECUTION), `src=0x0007` (SYSCALL) ✓
- Observed event sequence `fork→exec→fork` while executing `ls /tmp`; sequence is consistent with expected shell/process behavior (INFERENCIA — causal correspondence requires additional evidence) ✓
- `ts_delta` increments 0→1→2→3 ✓
- CRC-32 present in bytes [12..15] of each event ✓

**C-side wire format generation verified against the defined 16-byte contract; Rust consumer interoperability remains pending.**

---

## [2026-09-02] — FINDING: Makefile duplicate target warnings are harmless

**Context:** Every `make` invocation produces warnings:
```
Makefile:89: warning: duplicate script for target "load" ignored
/usr/src/sys/conf/kmod.mk:396: warning: using previous script for "load" defined here
```

**Observation:** `vtr-sentinel-kmod/Makefile` defines custom `load`/`unload`
targets that conflict with the same targets in FreeBSD's `kmod.mk`.
`kmod.mk` is included by the build system and defines its own `load`/`unload`.

**Impact:** None on the build output. The warnings are cosmetic.
**Deferred fix:** Rename custom targets to `vtr-load`/`vtr-unload` in a
future Makefile cleanup commit.

---

## [2026-09-02] — FINDING: /dev/vtr0 cdev is already implemented in vtr_sentinel.c

**Context:** Code review of `vtr_sentinel.c` during Phase 3 planning session.

**Observation:** `vtr_sentinel.c` already contains a complete cdev implementation:
- `make_dev_p()` creates `/dev/vtr0` during `MOD_LOAD`
- `vtr_cdevsw` defines `open`, `close`, `read`, `write`, `ioctl` handlers
- `vtr_cdev_read()` enforces the wire contract: `EMSGSIZE` if `uio_resid < 16`,
  CRC verification before `uiomove`, non-blocking (daemon uses kqueue)
- `vtr_cdev_write()` returns `EPERM` — unidirectional by design
- `vtr_cdev_open()` enforces single-reader: `EBUSY` if `g_open_count > 0`
- Rollback in `MOD_LOAD` is correct: ring → cdev → hooks, reversed on failure

**Impact on Phase 3 definition:** The roadmap entry "Phase 3 — /dev/vtr0
character device" is imprecise. The char device exists. What Phase 3 actually
closes is:

1. **Rust daemon** (`vtr-sentinel`) reading from `/dev/vtr0` via kqueue
2. **Cross-language byte-level tests** — `EventRecord` in Rust reads exactly
   the same 16 bytes that `vtr_event` produces in C (currently PROBABLE,
   not CONFIRMED per VTR-METH-001 v5.1)
3. **End-to-end pipeline test** — kmod loaded → real event → daemon reads →
   custody chain appended → verifiable evidence

**Decision:** Phase 3 description will be updated to reflect the actual
remaining work: Rust daemon integration and cross-language contract verification,
not char device creation.

---
*Log maintained per VTR-METH-001 v5.1 — observations recorded as facts,*
*decisions recorded with rationale, projections marked as such.*

---

## [2026-09-02] — FINDING: CRC-32 lookup table had transcription errors — cross-language contract was silently broken

**Context:** Phase 3 — cross-language byte-level contract verification between
`vtr_event_build()` (C kernel module) and `EventRecord::from_bytes()` (Rust daemon).

**Observation:** Cross-language tests in `vtr-sentinel` failed for genesis and
fork events but passed for exec events. CRC computed by Rust and Python
(`0x5387AA67` for genesis) did not match the value stored by the C module
(`0x90860871`). The standard vector test (`crc32("123456789") == 0xCBF43926`)
passed on all three implementations — confirming the algorithm was correct
but the table was not.

**Root cause:** `vtr_crc32_table` in `vtr_ring.c` had transcription errors
in 10 entries across 5 rows:

| Index | Incorrect    | Correct      |
|-------|--------------|--------------|
| 10    | `0xE0D5E91B` | `0xE0D5E91E` |
| 13    | `0x7EB17CBF` | `0x7EB17CBD` |
| 14    | `0xE7B82D09` | `0xE7B82D07` |
| 15    | `0x90BF1DBD` | `0x90BF1D91` |
| 53    | `0x21B4F928` | `0x21B4F4B5` |
| 54    | `0x56B3C9BE` | `0x56B3C423` |
| 95    | `0x62DD1D7F` | `0x62DD1DDF` |
| 171   | `0xA8670955` | `0xA867DF55` |
| 172   | `0x316658EF` | `0x316E8EEF` |
| 173   | `0x46616879` | `0x4669BE79` |

**Why exec passed and genesis/fork failed:** Whether a CRC is affected
depends on which table indices the event bytes hit during computation.
Exec events happened to avoid the corrupted indices. Genesis and fork
events hit corrupted entries, producing wrong checksums.

**Fix:** Table regenerated programmatically from poly `0xEDB88320`.
Verified: `vtr_crc32("123456789", 9) == 0xCBF43926`. Committed as `f32bb11`.

**Methodological note:** This is exactly the class of defect VTR-METH-001 v5.1
exists to detect — a property declared correct that was not empirically verified.
The cross-language byte-level tests caught the error as soon as they were executed.
Classifying the contract as PROBABLE (not CONFIRMED) in Phase 2 was correct.

**Result:** Cross-language wire contract is now **CONFIRMED** per VTR-METH-001 v5.1.
All 5 cross-language tests pass. vtr-sentinel commit `2e55638`.

---

## [2026-09-02] — FINDING: EVFILT_READ not supported for character devices — select(2) required

**Context:** Phase 3 end-to-end integration test — Rust daemon connecting to /dev/vtr0.

**Observation:** openat(/dev/vtr0, O_RDONLY|O_NONBLOCK) succeeded (fd=3). kqueue() succeeded (fd=4). But kevent(EVFILT_READ) returned ERR#19 'Operation not supported by device'.

Confirmed via truss:
  openat(AT_FDCWD,"/dev/vtr0",O_RDONLY|O_NONBLOCK) = 3
  kqueue()                                          = 4
  kevent(4,{3,EVFILT_READ,EV_ADD|EV_ENABLE},...)   ERR#19

**Root cause:** EVFILT_READ is supported for sockets, pipes, and FIFOs in FreeBSD, but not for character devices. The cdev subsystem does not implement the kqueue filter for read events.

**Fix:** Replaced kqueue/EVFILT_READ with select(2) and 100ms timeout in DeviceReader::read_event(). select(2) works correctly for cdevs.

**Result:** Pipeline verified end-to-end on FreeBSD 14.4-RELEASE-p8: kmod loaded -> /dev/vtr0 opened -> select() polling -> EventRecord::from_bytes() -> CustodyChain::seal() SHA-256. Daemon runs cleanly for full duration.

**vtr-sentinel commit:** ca99e97
