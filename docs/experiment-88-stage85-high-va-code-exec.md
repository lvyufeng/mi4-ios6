# Experiment 88: Stage85 — High-Virtual Code Execution at 0x80000000 (L2 Pages)

**Date:** 2026-06-15
**Device:** Xiaomi Mi 4 cancro (MSM8974)
**Boot method:** Non-persistent `fastboot boot` (no flash, clean recovery to Android)
**Status:** ✅ **ACHIEVED** — `kernel_entry returned success`

## Goal

Prove that Stage-owned code can execute from the **L2-page-mapped high-virtual
kernel address** at `0x80000000+offset` — i.e., instruction fetch through L2
(4KB page) translation, not just L1 (1MB section) translation. This is the
region where XNU's real kernel `.text` would live (`virtBase=0x80000000`).

This bridges the gap left by Stage84:
- Stage84 proved high-VA **data** access at `0x80000000` via L2 pages.
- Stage84 proved high-VA **code** execution at `0xc0000000` via L1 sections.
- **Stage85 proves code execution at `0x80000000` via L2 pages** — the remaining gap.

## Result

The high-VA code-execution test runs to completion and the function called via
the high-VA pointer returns the correct value:

```
stage85_xnu_arm_vm_init_high_va_code_exec_status=0x85000001
stage85_xnu_arm_vm_init_high_va_code_exec_satisfied_mask=0x0000003f
stage85_xnu_arm_vm_init_high_va_code_exec_failure_mask=0x00000000
stage85_xnu_arm_vm_init_high_va_code_exec_fn_phys=0x0004744c
stage85_xnu_arm_vm_init_high_va_code_exec_fn_high_va=0x8004744c
stage85_xnu_arm_vm_init_high_va_code_exec_fn_called=0x00000001
stage85_xnu_arm_vm_init_high_va_code_exec_fn_input=0x11223344
stage85_xnu_arm_vm_init_high_va_code_exec_fn_result=0x02042002
stage85_xnu_arm_vm_init_high_va_code_exec_fn_expected=0x02042002
stage85_xnu_arm_vm_init_high_va_code_exec_fn_result_correct=0x00000001
stage85_xnu_arm_vm_init_high_va_code_exec_public_xnu_executed=0x00000000
stage85_xnu_arm_vm_init_high_va_code_exec_persistent_write_attempted=0x00000000
loader_status=0x85000001
kernel_entry ok
kernel_entry returned success
```

`fn_result_correct=0x00000001` is the proof: the Stage-owned function at
physical `0x0004744c` was fetched and executed via its L2-mapped projection
`0x8004744c`, and returned `(0x11223344 ^ 0xfeedface) + 0x12345678 = 0x02042002`
exactly as expected. Instruction fetch through L2 page translation at
`0x80000000` works.

## Three Root Causes Fixed

Getting here required fixing three independent, non-obvious bugs that the
earlier stages never exposed (Stage84 died earlier in the boot, so these code
paths were reached for the first time in Stage85).

### 1. Section 1 not mapped in `build_identity_table` (mmu.c)

**Symptom:** data-abort at `lr=0x0000cb9c` (`ldr fp,[r6]`, where `r6=args`) in
`stage85_loader_preflight_run` — the first `args->Version` deref.

**Root cause:** Stage85's image grew past the 1MB section-0 boundary
(`image_end=0x00115000`), placing BSS globals in section 1
(`0x01000000-0x01ffffff`): `g_boot_args=0x0010c04c`, `g_apple_dt=0x0010c18c`,
`stage85_candidate_l1=0x00108000`. `build_identity_table()` only mapped section
0, so once the identity MMU installed, any BSS access faulted. (Earlier stages
kept BSS inside section 0, so this was never hit.)

**Fix:** add `map_section(0x00100000u, 0x00100000u)` in `build_identity_table`.

### 2. Stale ST83 marker tokens (macho_probe.c)

**Symptom:** `macho_staging_failure_mask=0x01000000`
(`STAGE85_MACHO_FAIL_STAGE_MARKERS`) → staging failed → **all 20 dryrun
contracts cascaded-failed** → preflight failed before the entry stub ran.

**Root cause:** `stage85_macho_verify_marker` expected section prefixes
hardcoded as `"ST83-TEXT"/"ST83-DATA"/"ST83-PRELINK-TEXT"`, but the Stage85
Mach-O fixture embeds `ST85-*` markers (`ST85-TEXT-NOEXEC`, etc.). The
prefix-match failed, the staging plan was marked failed, and every downstream
contract OR'd that source failure into its own `failure_mask`. One stale token
broke the entire readiness cascade.

**Fix:** update the three expected prefixes to `ST85-TEXT/ST85-DATA/ST85-PRELINK-TEXT`.

### 3. Wrong high-VA address formula (full_pmap probe + high_va_code_exec)

**Symptom:** `arm_vm_init_full_pmap_failure_mask=0x80010000`
(`FAIL_HIGH_VA_DATA | FAIL_SAFETY_BOUNDARY`).

**Root cause:** Both the full_pmap data probe and the high_va_code_exec
computation used `virt_base + (addr - phys_base)`, but the candidate L2 maps
`VA 0x80000000+N -> PA 0x00000000+N` (identity offset, **not** a physBase
offset). The correct high-VA for physical address `X` is simply
`virt_base + X`. The wrong formula pointed at the wrong physical page, so the
read-back value never matched.

**Fix:** change both computations to `virt_base + addr`. Also: `full_pmap`
restores TTBR0 before returning, so `high_va_code_exec` must re-install the
Stage-owned candidate L1 (read from `pmap_result->candidate_l1_base`) for the
duration of the high-VA function call, then restore the original TTBR0 — using
its own inline-asm TTBR0/TLB helpers, mirroring the TTBR0 roundtrip selftest
pattern.

## Safety Boundaries Preserved

All Stage85 safety invariants hold (verified by the result fields):
- ✅ Stage-owned code only (`stage85_high_va_target`) — `public_xnu_executed=0`
- ✅ No public XNU execution
- ✅ No Mach-O execution (inert fixture parsed/staged, never executed)
- ✅ No persistent writes — `persistent_write_attempted=0`
- ✅ Non-persistent boot via `fastboot boot` (no flash)
- ✅ TTBR0 roundtrip: candidate L1 installed, used, restored (matches original)
- ✅ Clean device recovery to Android (`boot_completed=1`)

## How the Test Works

```
1. kernel_entry → MMU/IRQ/TTBR0 selftests (all pass)
2. → loader preflight:
   a. Mach-O probe + staging (marker check now passes)
   b. 20 dryrun contracts (all OK now)
   c. iokit_client_open_contract OK → invokes xnu_entry_stub_run
3. → entry stub:
   a. early_pmap_platform_init, pe_init_platform_false, post_pe_bootstrap (OK)
   b. arm_vm_init_full_pmap: install candidate L1, verify L2 high-VA DATA,
      restore TTBR0
   c. arm_vm_init_high_va_code_exec: re-install candidate L1,
      call fn via high-VA ptr 0x8004744c, restore TTBR0, verify result
4. → preflight ok, kernel_entry ok, kernel_entry returned success
```

## Verification Commands

```bash
# Build
cd stage85 && ./build.sh

# Non-persistent boot
sudo fastboot -s 4a2fe00b boot out/stage85/stage85-qcdt.img

# Capture log (needs root: /proc/last_kmsg is system:log)
sudo adb -s 4a2fe00b shell 'su -c "cat /proc/last_kmsg"' > /tmp/stage85-last_kmsg.txt

# Validate
grep "high_va_code_exec_status=0x85000001" /tmp/stage85-last_kmsg.txt
grep "fn_result_correct=0x00000001"        /tmp/stage85-last_kmsg.txt
grep "kernel_entry returned success"        /tmp/stage85-last_kmsg.txt
```

## What This Completes

The high-virtual code execution milestone. The L2-mapped region at
`0x80000000` is now proven **executable** (not just data-accessible), which is
the foundation for a full XNU kernel pmap: real kernel `.text` would live here,
and instruction fetch through L2 translation now demonstrably works on the
target hardware.
