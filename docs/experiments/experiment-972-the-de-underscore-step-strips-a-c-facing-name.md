# Experiment 972 — the asm de-underscore step strips a C-facing underscore name

**Status:** ✅ **BUILT and PARKED (`armed-d13-0184b928`, entry bin `0184b928`) — NOT PRESSED.**
Continues the **971 press (2026-10-10)**, which CONFIRMED 971 (the RAM console survived
`set_mmu_ttb(cpu_ttb)` — `arm_vm_init: setting up segment information...` now appears) and then
stopped one rung later on a **missing symbol**:
`MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_disable_preemption`. The fix is in the **build tool**
(`tools/assemble_arm_layer.sh`), not the kernel tree: the de-underscore step now preserves names a
C header deliberately defines *with* the underscore. The entry image was relinked (`7c230cf9` →
`0184b928`) and the payload rebuilt (`6a4d0c23`); `verify_press_ready` 5/5; `make check` 0 (with
`check_deunderscore_guard.sh` wired in). **THE PRESS IS THE OPERATOR'S.**

Supersedes nothing; continues `experiment-971-the-console-dies-at-arm-vm-init.md` (whose press this
follows) and `experiment-958-the-3gb-rides-the-report-not-the-map.md` (folded into `7c230cf9`, unchanged here).

---

## 0. The arm

`armed-d13-0184b928` is `armed-d13-7c230cf9` (971's `bcopy` + 958's `hw.memsize` read) **plus one
build-tool change** in `tools/assemble_arm_layer.sh`. It is a *kernel-object* change (the entry
image's ARM asm layer is reassembled differently), so the entry bin hash moves and the payload is
rebuilt around it; the payload's own switch record `6c2b6038` is **unchanged** (no arm switch moved —
same `STAGE90_XNU_*` keys as `7c230cf9`). Entry image 6331476 bytes; the press sends
`stage90-qcdt.img` `6a4d0c23…` (9351168 bytes).

## 1. The 971 press — what it confirmed and what it stopped on

Capture `/tmp/cancro-last_kmsg.txt` (451350 B, 4530 lines), arm `armed-d13-7c230cf9`:

- `arm_vm_init: switching translation-tables now...` (line 3977) is followed by
  `arm_vm_init: setting up segment information...` (line 3978) — **971 is CONFIRMED**: the `bcopy`
  of the boot table into the system table carried the console's descriptors across
  `set_mmu_ttb(cpu_ttb)`, so records written *after* the switch are now emitted. This is the first
  time the D13 line has run past `arm_vm_init`.
- The boot then stops on a **missing symbol**, not a fault:
  `xnu_live_stub_hit_seq=0x00000001`, `xnu_live_stub_hit_caller=0x8002c140`
  (`caller - 4 = 0x8002c13c`, the `bl`), and the runner's line 4526
  `stub_hit=_disable_preemption`.

The run returned clean in ~17 s (`No errors detected`), no brick.

## 2. The root cause

`tools/assemble_arm_layer.sh` assembles `osfmk/arm/*.s` for the ELF and then strips ONE leading
underscore off every symbol — correct for Apple's Mach-O `_foo` → ELF `foo`, and it skips `_start`.
But one C header defines a name **with** the underscore deliberately:

```
osfmk/arm/cpu_data.h:145
    #define disable_preemption  _disable_preemption
```

so the D13 C caller (`printf`'s `DISABLE_PREEMPTION` path) reaches `_disable_preemption`. `asm_help.h`
`EnterARM(function)` also expands to `_##function`, and `machine_routines_asm.s` **defines**
`_disable_preemption`. The strip renamed that definition to `disable_preemption` (no underscore), so
the caller's `_disable_preemption` had no definer — it resolved to the entry's missing-symbol stub.
Confirmed **by value** in the previously-linked image: `printf+0x1c` `bl___disable_preemption` at
`0x8002c13c`, and the only asm-defined names were `disable_preemption` / `enable_preemption`.

## 3. The edit

`tools/assemble_arm_layer.sh`, two changes:

1. **Derive a keep-set** before the rename loop — every name a header aliases to an underscore form:
   ```
   UNDERSCORE_KEEP=$(grep -rhoE '^#define[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]+_[a-zA-Z][A-Za-z0-9_]*[[:space:]]*$' \
       "$XNU/osfmk/arm/"*.h | awk '{print $NF}' | sort -u)
   ```
   (the anchored `#define NAME _NAME` form is the builder's own; the guard re-derives the same set).
2. **Skip those names in the rename loop** — `continue 2` (out of the inner compare, past the
   `--redefine-sym`), keeping `_start`'s existing skip.

On D13 the keep-set is
`_COMM_PAGE32_AREA_LENGTH _COMM_PAGE32_AREA_USED _COMM_PAGE32_BASE_ADDRESS _COMM_PAGE32_SIGS_OFFSET
_COMM_PAGE32_START_ADDRESS _disable_preemption _enable_preemption _enable_preemption_no_check`.
On 4570 it is `_COMM_PAGE32_BASE_ADDRESS _COMM_PAGE64_BASE_ADDRESS`, and **4570's ASM objects
reference none of them**, so the keep-set is inert there (the 4570 objects are byte-identical).

## 4. By-value verification

- The reassembled `out/xnu_asm_obj_d13/machine_routines_asm.o` now **defines**
  `_disable_preemption` (1) and `_enable_preemption` (1) — before the fix it defined only the bare
  names.
- In the **linked** image `out/stage90/xnu_arm_entry.elf`:
  `8001ab60 T _disable_preemption` (and `__disable_preemption`/`___disable_preemption` aliases) is a
  **real** symbol, distinct from `8049bbe4 T disable_preemption` (the C function compiled from
  `machine_routines.c`). `printf+0x1c` (`0x8002c13c`) is `bl 8001ab60 <___disable_preemption>` — the
  caller now reaches the asm definer, not the stub.

## 5. What the press must show

- **`stub_hit` does not recur at `_disable_preemption`.** The next stop, if any, is a *further*
  missing symbol (one rung up the closure) or a real fault — recorded, not papered over.
- `arm_vm_init` keys keep coming (971 stays true), and the boot advances toward the SMC/USB/storage
  probes and, if clean, the Darwin banner.
- **Falsification:** if `stub_hit=_disable_preemption` reappears, the de-underscore step is not the
  (only) producer of the stripped name — record and re-derive.

## 6. Scope / non-goals

- **The 3 GB** is untouched (958's report is in `7c230cf9` and carried here; 915-B's low-bank pmap
  port is separate).
- **The keep-set is derived, not listed.** A future header that aliases a new underscore name is
  covered automatically; `check_deunderscore_guard.sh` re-derives it from both trees and refuses
  drift (its `--selftest` feeds a mutated builder and a stripped object and asserts each is refused).