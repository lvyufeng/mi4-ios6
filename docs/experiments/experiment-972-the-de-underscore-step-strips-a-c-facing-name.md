# Experiment 972 — the asm de-underscore step strips a C-facing underscore name

**Status:** ✅ **BUILT, PARKED (`armed-d13-0184b928`), AND PRESSED 2026-10-10 — 972 CONFIRMED.**
The press captured 451354 B / 4532 lines; `stub_hit=_disable_preemption` did **not** recur, and the
boot advanced one rung to a **new** missing symbol: `stub_hit=PE_init_SocSupport_stub`
(caller `0x8048bd50` = `PE_init_platform`). Device returned clean, `No errors detected`, no brick.
Capture `out/stage90/captures/972-press-armed-d13-0184b928-20261010-last_kmsg.txt` sha256 `a02b66cf…`.
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
---

## 7. THE PRESS (2026-10-10) — 972 CONFIRMED, and the next rung

Capture `out/stage90/captures/972-press-armed-d13-0184b928-20261010-last_kmsg.txt` (451354 B,
4532 lines, sha256 `a02b66cf1f87e6e3aca4a8d68db949b1e2bef19b8b91d60ec2c8e4e91dd4985c`). One gate exit 0,
one runner exit 0; `fastboot boot` only, nothing flashed; the device returned and `adb` lists
`4a2fe00b` again; `No errors detected`.

- **972 CONFIRMED.** `stub_hit=_disable_preemption` is **absent** — the de-underscore fix is real.
  971 also holds (line 3978 `arm_vm_init: setting up segment information...` after the switch at 3977).
- **The next stop is one rung up:** `stub_hit=PE_init_SocSupport_stub`, caller `0x8048bd50` =
  `PE_init_platform+0x2c` (`bl` at `0x8048bd4c`). `PE_init_platform` ran its body (it wrote its
  `0x8085f088` flag and `kprintf`'d first), so this is further along the generic PE than any prior run.

### 7.1 The cause of the next rung

The reported `PE_init_SocSupport_stub` is a **generated** stub (`0x8049bdc4`, body =
`movw r0,#0x16d0; movt r0,#0x8056; b entry_stub_hit`, name string `"PE_init_SocSupport_stub"`).
The real one is in **`out/xnu_arm_obj_d13/pe_msm8974.o`** (`T PE_init_SocSupport_stub` at `+0x15c`,
plus `T PE_init_SocSupport_msm8974` and the 12 `msm8974_*` driver functions). But that object is
**never linked**: `src/entry/build_entry.sh`'s pool glob (the `436` step, `:28695`) iterates only
`$XNU_KERNEL_OBJ_OUT/*.o` and `$XNU_ASM_OBJ_OUT/*.o` — **`$XNU_ARM_OBJ_OUT` (= `out/xnu_arm_obj_d13`)
is in no link list at all** (grep: zero references in `build_entry.sh`). So `PE_init_SocSupport_stub`
is undefined, and the stub generator fabricates the stub the run stops on.

This is the **914 board PE**, which that experiment *measured* (`1 of 1` compiles,
`nm` emits the two symbols) but never wired into a link — its last line is a host-side provenance
note. The Mi 4's PE methods (`msm8974_putc`/`getc`/`uart_init`, the QTimer timebase
`msm8974_timebase_init`/`get_timebase`, the GIC `msm8974_interrupt_init`/`handle_interrupt`) are
**absent from the final ELF** today (`nm … | grep msm8974_putc` = 0), even though the msm8974
**I/O Kit classes** (`MSM8974GIC`/`MSM8974Timer`/`MSM8974PlatformExpert`/`MSM8974RootResource`, from
the `xnu_platform_obj_d13` pool via `STAGE90_PSEUDO_INITS`) **are** present. That single missing
object is the cause of this stop.

### 7.2 The next rung (NOT built here)

Add `pe_msm8974.o` (only — see below) to the entry link, behind a new switch, so a reverted build
stops arming it. The obstacles, measured:

- **The arm pool is 16 objects, but only `pe_msm8974.o` contributes net-new symbols.** A per-file
  `defined - kernel_pool` delta gives `new=0` for all fifteen others (`arm_vm_init`, `cpu`, `pmap`,
  `machine_routines`, `locks_arm`, … all duplicate the kernel pool byte-for-byte because the kernel
  pool's glob builds them with the same flags) and `new=14` for `pe_msm8974.o`. Adding the whole pool
  would be ~551 duplicate definitions; adding the one object adds `PE_init_SocSupport_stub`,
  `PE_init_SocSupport_msm8974`, and the 12 `msm8974_*` names.
- **One apparent collision is benign:** `pe_msm8974.o`'s only overlapping defined name is the
  assembler-local label `.L.str` (local, non-global) — no real symbol clash.
- **The object must be compiled with `-DBOARD_CONFIG_MSM8974=1`** (the `#if defined(BOARD_CONFIG_MSM8974)`
  gate) and carries `U gPESocDispatch` + `U PE_early_puts`, both already defined in the kernel pool
  (`pexpert_arm_common_pe_socsupport.o` / `pe_kprintf.o`).

**Falsification:** if `pe_msm8974.o` is added and the stop moves past `PE_init_SocSupport`, the
missing-board-PE model is confirmed; if the stop recurs, the symbol was defined elsewhere and the
model is wrong one rung out — recorded, not papered over.
