# D59140 — Epistemic Record

## Status
- Original hypothesis: REFUTED
- Reproducer (original): REFUTED
- Reproducer (corrected): CONFIRMED (independent: glebius; local: 2026-09-12)
- MPASS(hlen <= sizeof(sa->sa_data)): PROPOSED

## Observation
write(2) to BPF descriptor on lo0 returned:
  write: Address family not supported by protocol family

## Original attribution
bpfwrite() -> bpf_movein() sets sa_family but not sa_len.
if_output reads sa_data without verifying sa_len >= sizeof(af).
Proposed fix: add sa_len check in five drivers.

## Independent review (glebius, 2026-09-12)
The reproducer violated the DLT_NULL contract.
A write(2) on a BPF descriptor must prepend a 32-bit word in host
byte order containing the address family. For IPv4: AF_INET = 2.
The original pkt[] started directly with the IP header.

bpf_ifnet_write() already ensures dst.sa_data contains the correct
hlen bytes before calling if_output. The sa_len check in the
individual drivers defends against a condition that cannot arise
via that path. The contract is enforced upstream.

## Evidence chain

### E_origin (repro-bpf-sa-len.c)
pkt[] without DLT_NULL header.
Output: write: Address family not supported by protocol family
Role: generated the observation and the original hypothesis.

### E_validation (repro-bpf-sa-len-corrected.c)
pkt[] with AF_INET prepended in host byte order:
  0x02, 0x00, 0x00, 0x00  <- AF_INET little-endian
Output: wrote 24 bytes -- DLT_NULL header correct, packet injected
Verified locally on FreeBSD 14.5-RELEASE (dell-bsd) 2026-09-12.
Independent reproduction by glebius confirmed prior to local run.

## Falsification record

### Falsification 1 — experimental
The observation was real and reproducible.
The attribution to the kernel was falsified by independent review.
E_origin != E_validation: the instrument defect explained the phenomenon.
Classification: attribution falsified by external reviewer.

### Falsification 2 — semantic
The corrected reproducer initially retained a printf message describing
the behavior of the original (incorrect) reproducer:
  "sa_len=0 path triggered"
This message no longer corresponded to what the corrected program
demonstrated. The discrepancy was identified and corrected (commit 7a18745).
Classification: semantic inconsistency between artifact and experiment state.
Note: 7a18745 is not new experimental evidence. It is a correction of
artifact integrity — ensuring the instrument does not misrepresent
the evidence it was corrected to produce.

## Artifact integrity
- repro-bpf-sa-len.c: E_origin — unchanged, preserved as-is
- repro-bpf-sa-len-corrected.c: E_validation instrument
  - pkt[] corrected: added DLT_NULL AF_INET header
  - printf corrected (7a18745): output now reflects corrected behavior

## Next action
Update D59140 to add MPASS(hlen <= sizeof(sa->sa_data)) in
bpf_ifnet_write() as a hardening measure, pending confirmation
from glebius on whether to update this revision or open a new diff.
