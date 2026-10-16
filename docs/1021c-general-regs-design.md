# 1021c — REMOVING COMPILER-EMITTED FP/NEON FROM THE KERNEL POOL: design

**READ-ONLY design. No build, no press, no device action, no write to `out/stage90` was performed
while authoring this.** Everything below is measured against the tree as it stands at HEAD
(`2a03f97`) and against the installed toolchain, or is marked PREDICTED.

**Verdict up front — the task's premise is FALSIFIED, and the fix has a different spelling.**

The task says `-mgeneral-regs-only` is CONFIRMED on this host's `clang`. It is not, for **armv7**.
`-mgeneral-regs-only` is accepted by the driver, forwarded to `cc1`, and **silently ignored**:

```
$ clang --target=armv7-unknown-netbsd-eabi -mcpu=cortex-a15 -marm -mfpu=neon-vfpv4 \
        -mfloat-abi=softfp -mgeneral-regs-only -O2 -c t.c
clang: warning: argument unused during compilation: '-mgeneral-regs-only' [-Wunused-command-line-argument]
$ clang ... -mgeneral-regs-only -S …  |  cmp - <(clang ... -S …)   ->  IDENTICAL
```

The same flag on `--target=aarch64-linux-gnu` **does** change codegen (`aarch64 DIFFERS`). So
`-mgeneral-regs-only` is an **aarch64-only** feature in clang 14; on armv7 the driver hands it to
`cc1` (`-###` shows it in the argv) and `cc1` drops it. A build that added it would produce a pool
**byte-identical to today's**, the switch would read as ON, and 1021d's press would re-measure
`0x8047b0ac` — this project's most-repeated failure class (`[[mi4-off-option-two-spellings]]`,
`[[mi4-a-claim-in-a-comment-is-not-a-check]]`). It must not be used.

**The measured fix is `-msoft-float`** — which is exactly what Apple's own ARM kernel build already
passes (`external/xnu-hd2-darwin13/xnu/makedefs/MakeInc.def:377-379`:

```
export CFLAGS_ARM = -Darm -DARM -D__ARM__ -DPAGE_SIZE_FIXED \
                    -fno-strict-aliasing -fno-keep-inline-functions \
                    -msoft-float
```

). Apple's ARM *kernel* is built soft-float. This project's kernel is built
`-mfpu=neon-vfpv4 -mfloat-abi=softfp`, i.e. **hard-float codegen with a soft-float calling
convention** — the one combination that lets clang emit NEON/VFP into the kernel. The rung's work is
to stop passing that, and the flag that does it is the one Apple passes.

---

## 1. THE MECHANISM, BY VALUE

### 1.1 What actually emits the FP instructions

The flag responsible is **`-mfloat-abi=softfp`**, not `-mfpu=neon-vfpv4` on its own. Measured on a
kernel-shaped TU (integer loops, 64-bit struct copies, no FP types):

| flags | `vld1/vst1` | `vldr/vstr/vpush/vpop` | `__aeabi_*` |
| --- | --- | --- | --- |
| `-mcpu=cortex-a15 -marm -mfpu=neon-vfpv4 -mfloat-abi=softfp` (TODAY) | **6** | 0 | 0 |
| `… -mfpu=neon-vfpv4` (no `-mfloat-abi`) | **0** | 0 | 0 |
| `… -mfpu=neon-vfpv4 -mfloat-abi=softfp -msoft-float` | **0** | 0 | 0 |
| `… -mfpu=neon-vfpv4 -mfloat-abi=soft` | **0** | 0 | 0 |
| `… -mfloat-abi=softfp -Xclang -target-feature -Xclang -neon` | **0** | 0 | 0 |
| `… -mfloat-abi=softfp -mgeneral-regs-only` | **6** (no-op) | 0 | 0 |

`-mfloat-abi=softfp` tells clang the *calling convention* is soft but that a **hardware FPU is
present and may be used for codegen** — so the vectorizer is free to use NEON. `-mfloat-abi=soft`
(and equivalently `-msoft-float`, which the driver maps to it) says the FPU is **not** usable, so no
FP register is allocated at all. That is the whole difference, and it is the one Apple's own ARM
kernel flags make.

### 1.2 The pool, by value (measured on `out/xnu_kernel_obj_d13/`, 709 objects)

* **273 / 705** objects contain at least one FP/NEON instruction.
* Every FP instruction in the pool is an **auto-vectorized NEON load/store or data op**:
  `vst1.32` ×1545, `vst1.64` ×912, `vld1.64` ×907, `vld1.32` ×867, `vst1.8` ×531, `vld1.8` ×312,
  `vmov.i32` ×268, `vadd.i32` ×110, `vorr` ×93, `vdup.32` ×79, `veor` ×76, … plus a real but small
  scalar-VFP residue (`vldr` ×188, `vstr` ×169, `vpush`/`vpop` ×49/45 — 64-bit integer moves and
  register spills, e.g. `ip_dn_ctl`'s `vpush {d8-d9}` at `ip_dummynet.o`).
* **No `__ARM_NEON`-gated source** exists in the D13 tree: `grep -rl __ARM_NEON
  external/xnu-hd2-darwin13/xnu` returns **nothing** (the 4570 tree's `aes.h` had such a branch; D13
  does not). So the macro side-effect of any candidate flag is a non-issue for this tree.
* **No FP types in kernel C.** Every `float`/`double` hit in `osfmk`/`libkern`/`iokit`/`bsd` is a
  comment or the English word ("double FREE", "double hop"). The only real declarations are in
  `osfmk/console/panic_ui/genimage.c` and `qtif2kraw.c`, which are **host tools not in the manifest**
  (`ls out/xnu_kernel_obj_d13 | grep genimage` → empty).
* **No explicit NEON intrinsics**: `grep -rl arm_neon.h osfmk libkern iokit bsd` → empty.
* **No `vmrs`/`vldr` inline asm in pool C.** The only VFP instructions in a pool `.c`'s includes are
  `proc_reg.h`'s coprocessor *moves* (`mrc p10,7,…,c8,c0,0`), which the assembler accepts under
  `-msoft-float` (measured; §3.2). The `vmrs`/`vmsr` register-file instructions live only in
  `osfmk/arm/armv/vfp.s`, a **`.s`** file the pool does not compile.

### 1.3 The `.s` half — nothing to do, and that is by construction

`build_xnu_arm_kernel.sh:1346` **skips** `*.s|*.S` (`skipped=$((skipped + 1))`); assembly is
`xnu_arm_assemble.sh`'s job (`:13-14`, `:1334-1335`). So a flag added to `CC_ARGS`/`CXX_ARGS` is
incapable of reaching the VFP save/restore. The files that **intentionally** use VFP/NEON outside the
compiler are:

| file | FP insns | why it must stay FP-enabled | affected by a C/C++ flag edit? |
| --- | --- | --- | --- |
| `osfmk/arm/armv/vfp.s` | 12 (`vmrs`/`vmsr` of `fpexc`/`fpscr`/`fpsid`/`mvfr0`, `vldm`/`vstm` of d0–d31) | it IS the VFP context save/restore and the CPACR grant | **no** — `.s`, not compiled by this script |
| `osfmk/arm/machine_routines_asm.s` | 0 | — (its NEON note is a comment) | no |
| `osfmk/arm/armv/cpufunc-v6.s`, `cpufunc-v7.s`, `cache.s` | 0 | — | no |

So the answer to the task's question 2 is: **the flag goes only to the C and C++ flag blocks; the
`.s` compiles must not (and cannot) see it, and the one file that legitimately uses the VFP register
file is a `.s`.**

---

## 2. THE EDIT

### 2.1 Recommendation

One edit, in `tools/build_xnu_arm_kernel.sh`, in the two flag arrays:

* **`:1227-1232 `CC_ARGS`** — change `:1229` `-mfpu=neon-vfpv4 -mfloat-abi=softfp` to:
  ```
  -msoft-float
  ```
* **`:1238-1244 `CXX_ARGS`** — change `:1240` the same way.

`-msoft-float` is preferred over `-mfloat-abi=soft` because it is the **exact spelling Apple's own
`CFLAGS_ARM` uses** (`MakeInc.def:377-379`) — one value, one definition, and the citation is a tree
we already vendor. It is also preferred over `-Xclang -target-feature -Xclang -neon` (which also
measured 0 NEON) because that suppresses the *feature*, not the *FP register allocation*, so a
16-byte struct copy can still be lowered through scalar VFP `d`-registers — and **scalar VFP faults
identically to NEON when `FPEXC.EN` is off**. Only the soft-float ABI removes the FP register file
from the allocator entirely, which is the property the rung needs.

Keep `-mcpu=cortex-a15 -marm` (the `mrc p10` feature reads and the `-marm` ISA selection still need
a CPU; `-msoft-float` does not change either). Do **not** keep `-mfpu=neon-vfpv4`: with
`-msoft-float` it is inert for codegen but it still stamps `.fpu neon-vfpv4` on every object, and
leaving a flag that *reads* as "NEON is on" beside the switch that says it is off is the
two-spellings shape. Drop both and pass `-msoft-float`.

### 2.2 Why not per-file opt-outs

No file needs one. The only sources that would break under `-msoft-float` are ones that use FP
**types or register-file asm**, and §1.2 measured none in the pool. The VFP register-file asm is in a
`.s` file that this script never compiles (§1.3). The one inline-asm family that *is* in pool C
(`proc_reg.h`'s `mrc p10,7,…`) assembles under `-msoft-float` (measured, §3.2). So the flag is
whole-pool, and a per-file opt-out list would be dead code that a future reader would take as
evidence the flag is dangerous.

### 2.3 The two-carrier trap this switch does NOT have (and why)

Unlike `STAGE90_XNU_HFS_ROOT_RW` (905-design §7.3), this switch is **not** about a `#if`. It changes
the **compiler flag list**. So its carriers are not `-D`/record pairs — the record still has to carry
the switch (so the arm is readable and the linked image can be checked), but the *substance* is in
the flags, and the only proof the switch reached the compile is a **by-value property of the
object**: the pool's FP-instruction count (§5).

---

## 3. RISKS

### 3.1 The scalar-VFP residue is removed too, and it must be

`vldr`/`vstr`/`vpush`/`vpop` (188/169/49/45 in the pool) are **scalar** VFP, but they read and write
the same `d`-registers and obey the same `FPEXC.EN` gate as NEON. `ip_dn_ctl`'s
`vpush {d8-d9}` (`ip_dummynet.o`) would fault exactly as `mach_msg_send`'s `vld1.32` does, the first
time that function ran with `FPEXC.EN` off. **A flag that removed only NEON would leave 451 faulting
instruction sites behind.** This is the strongest argument against the `-Xclang -target-feature
-neon` spelling and for `-msoft-float`: soft-float removes the FP register file from the allocator,
so spills and 64-bit moves go through `r`-pairs.

### 3.2 What legitimately needs the FP coprocessor, and survives

* **`proc_reg.h`'s `armreg_*_read/write`** — `mrc`/`mcr p10,7,…`: **verified** to assemble under
  `-msoft-float` (both with and without `-mfpu`) in a standalone TU.
* **`arm_init.c`'s LEO VFP self-test** (`:230-249`): `mrc p15,0,%0,c1,c0,2` (CPACR read) and calls to
  `ios7leo_read_fpsid`/`ios7leo_read_mvfr0`, which are **`vfp.s` functions** (`.s`, unaffected). No
  FP register is used in the C.
* **`init_vfp()`** (`vfp.s:44`) enables CP10/CP11 — unaffected, `.s`.
* **`vfp_context_save`/`_load`/`vfp_enable_exception`** (`vfp.s`) — unaffected, `.s`.
* **libgcc soft-float helpers**: with `-msoft-float` a TU that genuinely does FP arithmetic calls
  `__aeabi_f*`/`__aeabi_d*`. No pool object today references any (measured:
  `grep -c __aeabi_{f,d}… out/xnu_kernel_obj_d13/*.o` → 0), and the pool has no FP types (§1.2), so
  **no new libgcc symbol is expected**. `__aeabi_uldivmod`/`__aeabi_ldivmod` (60/23 refs today) are
  already supplied by libgcc and already in the entry link
  (`out/stage90/xnu_arm_entry.elf: __aeabi_uldivmod @ 0x804a1bcc`). The build's existing
  undefined-symbol pass (below) is what turns "unexpectedly needed" into a refusal rather than a
  link error.

### 3.3 The undefined-symbol / stub surface

A flag change can add references (e.g. an `__aeabi_fadd`) that were not there before. The entry
build already has a **pass-1 undefined-symbol list and a stub generator**
(`build_entry.sh`, the `xnu_entry_callwalk`/stub machinery around `:3097`+), and
`STAGE90_ENTRY_*` link knobs are fatal when a name is missing. The rung must not treat a *new*
`__aeabi_*` as unexpected: it is a legitimate consequence of soft-float, and the fix is to let libgcc
(`$LIBGCC`, `build_entry.sh:2483`) resolve it, exactly as `__aeabi_uldivmod` is resolved today. The
design below makes this an explicit pre-registered cell (§4, cell C).

### 3.4 Performance

`-msoft-float` removes vectorized loops from the kernel. This is a bring-up kernel whose frontier is
a fault, not a throughput measurement; correctness dominates. The cost is confined to the pool and
does not touch the entry image's own `arm-none-eabi-gcc` objects (`build_entry.sh:2551`+), which
already default to `__SOFTFP__` (measured: `arm-none-eabi-gcc -mcpu=cortex-a15 -marm -dM -E` prints
`#define __SOFTFP__ 1`) and emit **0** FP instructions on the same TU.

### 3.5 The other builders

`-mfpu=neon-vfpv4 -mfloat-abi=softfp` appears in **eight** scripts
(`build_xnu_arm_kernel.sh`, `build_xnu_arm_layer.sh`, `build_xnu_arm_macho.sh`,
`sweep_xnu_osfmk.sh`, `hfs_port_probe.sh`, `assemble_arm_layer.sh`, `gen_assym.sh`,
`measure_link.sh`, `link_xnu_arm.sh`). The rung must establish which of these produce objects the
**linked entry image or the D13 pool** consumes, and edit **only those** — the rest are measurement
or stage-85/4570 tools whose objects are not in the D13 image, and changing them would move
comparison baselines for no reason. **`build_xnu_arm_layer.sh:198` is the one to check by value**:
it is the osfmk/arm layer builder, and `osfmk_arm_locks_arm.o`, `osfmk_arm_machine_routines.o` etc.
are linked into the D13 image by name (`build_entry.sh:3102`+). If the D13 pool is built by
`build_xnu_arm_kernel.sh` alone, its objects come from there and only that script needs the edit;
**this is the one open item the build must resolve, and it is a `grep` over the entry link's object
names** (§5, the by-value clause covers it either way).

---

## 4. THE SWITCH, ITS CARRIERS, AND THE ONE QUESTION

### 4.1 The switch

`STAGE90_XNU_GENERAL_REGS`, 0/1, with `[[mi4-off-option-two-spellings]]` obeyed (`#define X 0` is
not "off"): prefer an **array append**, not a `-D`:

```
# in tools/build_xnu_arm_kernel.sh, beside the kext-guard/alignment/… switches (the same array class
# as STAGE90_XNU_UNDEF_RAISE at :872):
GENERAL_REGS=${STAGE90_XNU_GENERAL_REGS:-0}
case "$GENERAL_REGS" in 0|1) ;; *) echo "REFUSING: STAGE90_XNU_GENERAL_REGS='$GENERAL_REGS' is neither 0 nor 1" >&2; exit 2 ;; esac
if [[ $GENERAL_REGS -eq 1 ]]; then
    CC_ARGS+=( -msoft-float )    # and CXX_ARGS+=(-msoft-float)
fi
```

**OFF = byte-identical**: with the switch at 0 the arrays are exactly today's, because the flag is
appended only under `-eq 1`. This is the correct shape here — a `#if` guard in a header would change
`DEFINES` and is not what this switch is.

### 4.2 The carriers (POOL-side switch — the `UNDEF_RAISE` shape of 905-design §7.2)

1. **Env resolve + validate**: `build_xnu_arm_kernel.sh`, new lines beside `:872` (0/1 refusal).
2. **The pool flag channel**: the append above. **There is no `-D`** — the switch's substance is the
   flag, not a define. `XNU_KERNEL_EXTRA_DEFINES` is *not* the carrier (it carries `-D` only;
   `:1257-1260`). If the arm must also be visible to source, add a **separate** `-D` alongside, but
   the rung does not need one.
3. **`build_entry.sh` resolve**: `:1648`-style, e.g. `GENERAL_REGS=${STAGE90_XNU_GENERAL_REGS:-0}` +
   the same 0/1 refusal (mirror `UNDEF_RAISE` at `:1648-1651`).
4. **`ENTRY_ARM_KEYS`**: `build_entry.sh:2054`… — add the name at the tail (the list already ends
   with `STAGE90_XNU_UNDEF_RAISE STAGE90_XNU_TREE_D13`).
5. **The record resolver**: `build_entry.sh:2296`-style `case` — add
   `STAGE90_XNU_GENERAL_REGS) _v=$GENERAL_REGS ;;`.
6. **The record echo**: `build_entry.sh:40251`-style — `echo "STAGE90_XNU_GENERAL_REGS=$GENERAL_REGS"`,
   writing the **RESOLVED** value, never `${STAGE90_XNU_GENERAL_REGS}`.
7. **`preflight_boot_check.sh` `ENTRY_CFG_KEYS`**: `:716` — add the name (the converse check at
   `:1209-1211` is a set difference and moves no count).
8. **The by-value build clause** (§5) — the analogue of 1020's `check_d13_undef_raise.py` clause at
   `build_entry.sh:31344-31358`.
9. **No `.s` carrier** — the flag cannot reach assembly (§1.3).
10. **`out/stage90/xnu_arm_entry-arm-extra.txt`**: the cross-lane carrier file the press/park scripts
    grep. `GLOBAL_FIX` shows the hazard (`[[mi4-self-written-record-is-not-a-constraint]]`): if the
    press scripts grep this file for the arm's switches, `GENERAL_REGS` must be written into it by the
    **build**, not by a hand-edit, or the grep is a self-written record check.

**Two-carrier trap to state and avoid**: because this switch's substance is a *flag*, the pool's
`-D` channel is not involved, and a build that sets the record key without rebuilding the pool would
produce a record that says "NEON-free" over a pool full of `vld1`. §5's clause is what makes that a
refusal.

### 4.3 The ONE question

**Does removing compiler-emitted FP/NEON from the kernel pool retire the kernel-mode NEON undef at
`0x8047b0ac`, and does the boot then move past `mach_msg_send`?**

Not "does the pool have no NEON" (that is the build's own by-value cell, §5, and is not a device
question). The device question is whether **this** was the frontier, or whether the undef was an
*effect* of the user-mode `udf`/exception delivery path (1020b's open question 1: "did pid 1 die, or
is the kernel undef independent?"). The rung answers it by what the *next* stop is, or by reaching
the end of the run.

### 4.4 Pre-registered verdict cells

| cell | observation | verdict |
| --- | --- | --- |
| **A — RETIRED** | no kernel-mode undef at `0x8047b0ac`; the run proceeds past `mach_msg_send`; the frontier moves (or the run ends at `entry_epilogue`) | the kernel-pool FP was the frontier. **The fix STANDS.** |
| **B — STILL THERE** | the same kernel-mode undef at `0x8047b0ac` (`vld1.32 {d16-d17},[r0]!`), same `lr=0x8047b0b0` | the pool was NOT rebuilt, OR the object at that address came from another builder (§3.5). **Read the by-value cell first.** |
| **C — NEW FAULT / NEW UNDEFINED SYMBOL** | a *different* undef PC, or a link/undefined-name refusal on an `__aeabi_f*`/`__aeabi_d*` | soft-float introduced a libgcc reference the link does not resolve. Expected-but-unhandled — name it, add `$LIBGCC`. |
| **D — DIFFERENT FAILURE BEFORE THE MACH_MSG PATH** | the run stops somewhere earlier (e.g. in `arm_init`'s VFP self-test, or a `panic` from `init_vfp`) | the flag broke something that legitimately needs the FPU. Read §3.2. |
| **E — NO CHANGE AT ALL, INCLUDING THE UNDEF PC** | byte-for-byte the 1020b frontier | the switch did not reach the compile (OFF-spelling / wrong builder). The by-value clause should have caught this — if it passed, the clause is wrong. |

### 4.5 The falsification

**Falsifier for the whole rung**: the by-value cell (§5) reports the pool NEON-free **and** cell B
still obtains. That means the FP instructions at the frontier were **not** the compiler's — i.e. the
`0x8047b0ac` object came from a builder other than `build_xnu_arm_kernel.sh` (or the linked address
is a different function than the symbol table says), and the fix direction is wrong.

**Falsifier for the preferred spelling**: any `__aeabi_d*`/`__aeabi_f*` reference in a pool object
after the change. That would mean a pool source does real FP after all, contradicting §1.2 — and the
per-file opt-out the design rejected would become necessary.

---

## 5. THE BY-VALUE BUILD CLAUSE

The switch's effect is a property of the **object**, so the clause reads the object.

### 5.1 Primary — the named object the frontier is in

1020b named the faulting instruction **inside `mach_msg_send`**, which is in
`out/xnu_kernel_obj_d13/osfmk_ipc_mach_msg.o`. That object today contains exactly four FP
instructions (all in `mach_msg_send`, offsets `0xb8`,`0xbc`,`0xd4`,`0xd8`):

```
$ arm-none-eabi-objdump -d out/xnu_kernel_obj_d13/osfmk_ipc_mach_msg.o | grep -E 'vld1|vst1'
  b8: f4600a8d  vld1.32 {d16-d17}, [r0]!
  bc: f4602a8f  vld1.32 {d18-d19}, [r0]
  d4: f4400a8d  vst1.32 {d16-d17}, [r0]!
  d8: f4402a8f  vst1.32 {d18-d19}, [r0]
```

The clause, in the same shape as 1020's `check_d13_undef_raise.py` clause
(`build_entry.sh:31344-31358`, gated on the switch, reading the **linked** image by value):

* **ON direction**: `REFUSE` unless the linked image's `mach_msg_send` body contains **0**
  `vld1`/`vst1`/`vldr`/`vstr`/`vpush`/`vpop`/`vmov` instructions.
* **OFF direction**: `REFUSE` unless it contains **> 0** (today: 4). This is the direction that
  proves the OFF arm is byte-identical to the pre-rung pool — `[[mi4-off-option-two-spellings]]`.
* **Both directions read the LINKED image** (`out/stage90/xnu_arm_entry.elf`), not the source and not
  the `.o`, because `build_entry.sh` only **LINKS** `osfmk_ipc_mach_msg.o` — the 1020 defect exactly
  (`records/1020-build-defect.txt`): a stale pool object that the entry build never recompiles is
  invisible to a green entry build.

Implementation: `tools/check_d13_no_neon.py <elf-or-obj> --symbol mach_msg_send --expect none|any`,
decoding a fixed window from the symbol's `st_value` in a `nm -S`-derived range (the same technique
`check_d13_undef_raise.py` uses). The window is `mach_msg_send`'s own extent, not the whole object,
so `mach_msg_trap`'s neighbours cannot satisfy the OFF check on the ON arm's behalf.

### 5.2 Secondary — the whole-pool census (the ON arm's real assertion)

A switch that suppressed NEON in `mach_msg.o` but not in `ip_dummynet.o` would pass §5.1 and leave
`vpush {d8-d9}` waiting. So the ON arm also requires a **whole-pool** count:

* **ON**: `Σ FP-instruction count over out/xnu_kernel_obj_d13/*.o` == **0**. (Today: 273/705 objects
  have ≥1; the raw mnemonic census is §1.2.)
* **OFF**: the count is **> 0** and, for a true byte-identity check, the pool is **unchanged** — which
  must be proved the project's existing way (**rebuild + `cmp`** the objects, or the content record
  `out/xnu_kernel_src…sha256` plus a fresh pool hash). Do **not** assert a remembered number.

This is the clause that carries `[[mi4-a-claim-in-a-comment-is-not-a-check]]`: the claim "the pool is
NEON-free" is checked by counting instructions in the objects the image actually links.

### 5.3 The clause must also name the builder (defect class m1015b)

1020b's `records/1020-build-defect.txt` and 1015b's `m1015b` both record a census that passed while
the substance was absent because the check's *spelling* could not fail. Here the analogue is: the
pool could be NEON-free **because the wrong pool was rebuilt**. The clause must print **which
filesystem paths it counted** (the object directory and the manifest), so an ON arm over an empty or
stale directory is a failure, not a vacuous pass. Refuse if the count of objects examined is not the
manifest's count.

---

## 6. THE ALTERNATE FIX, AND WHY THE FLAG IS PREFERRED

### 6.1 The alternate: keep `FPEXC.EN` on for kernel mode

XNU turns the FPU off for kernel execution deliberately. `save_vfp_context`
(`osfmk/arm/pcb.c:298-306`) calls `vfp_enable_exception(FALSE)` on every context switch when the
thread's VFP is clean:

```c
static void save_vfp_context(thread_t thread)
{
    if (thread->machine.vfp_enable && !thread->machine.vfp_dirty) {
        vfp_context_save(thread->machine.uvfp);
        vfp_enable_exception(FALSE);          /* <-- CPACR/FPEXC off for kernel mode */
        thread->machine.vfp_enable = FALSE;
    }
}
```

The alternate fix is to stop doing that — grant CP10/CP11 unconditionally at `init_vfp()`/
`join_start` and never clear `FPEXC.EN`. The observed `sleh_undef` lazy-enable path
(`trap.c:1118-1136`) already implements exactly this *once*, for user mode: on the first NEON undef it
sets `FPEXC.EN`, loads the thread's VFP context, and returns. Making it global is a two-line change.

### 6.2 Why it is worse

1. **It breaks the kernel's VFP contract with user threads.** `FPEXC.EN` off is what makes the
   hardware lazily trap on the *first* FP instruction of a thread so XNU can load that thread's
   saved vector state (`vfp_context_load`, `trap.c:1122/1134`). With `EN` globally on, a kernel-mode
   NEON instruction runs against **whatever vector state the preempted user thread left in the
   register file** — the kernel silently clobbers a live user VFP context, and the corruption is
   invisible until the user thread reads a wrong `d`-register. That is worse than a fault.
2. **It requires per-switch FPEXC churn.** `save_vfp_context` would have to stop clearing `EN` and
   start saving/restoring around *every* kernel-mode FP use, or accept the clobber — i.e. it
   re-introduces the save/restore the discipline exists to avoid on context switches that never
   touched FP.
3. **It is not Apple's discipline.** Apple's ARM kernel is built `-msoft-float`
   (`MakeInc.def:377-379`) precisely so this question does not arise. Following the compiler flag is
   following the vendor, and `[[mi4-reference-xnu-repo]]` says consult the canonical repo first.
4. **The alternate makes the frontier *disappear* rather than *retire*.** A kernel that keeps `EN` on
   would stop faulting at `0x8047b0ac` and silently execute NEON in whatever state it found — the
   run would move on carrying an unmeasured corruption, which is the opposite of what a frontier
   answer is for (`[[mi4-stub-walk-frontier-kinds]]`: a step's object retires the name the previous
   run stopped on — here nothing would be *named*).

### 6.3 What the alternate is still useful for

As a **discriminator**, not a fix: if the flag fix is controversial, a *temporary* "enable FPEXC.EN
globally" build answers "is the fault really the FPU grant?" in one press. It is a **probe, and it
is not the fix.** If it is ever built, it is its own switch, it changes `trap.c`/`pcb.c` (pool files,
`UNDEF_RAISE`-shaped carriers), and its record must say so — it must never be the build that is
pressed as the rung.

---

## 7. LINE REFERENCES (checked against HEAD `2a03f97`)

| what | site |
| --- | --- |
| `CC_ARGS` (C half of the pool) | `tools/build_xnu_arm_kernel.sh:1227-1232`; the FP flags are `:1229` |
| `CXX_ARGS` (C++ half) | `tools/build_xnu_arm_kernel.sh:1238-1244`; FP flags `:1240` |
| `.s` skipped by the pool builder | `tools/build_xnu_arm_kernel.sh:1346` |
| the per-file compile (uses `CC_ARGS`/`CXX_ARGS`) | `:1603-1610` |
| the C++ platform block (same arrays) | `:1974` |
| the two `CC_ARGS` platform blocks | `:1987`, `:2005`, `:2025` |
| the `-D` switch array pattern (`UNDEF_RAISE` etc.) | `:872`, `:888`, `:901`, `:915` |
| env switch 0/1 refusal pattern (entry side) | `src/entry/build_entry.sh:1648-1651` (`UNDEF_RAISE`) |
| `ENTRY_ARM_KEYS` | `src/entry/build_entry.sh:2054` |
| the record resolver `case` | `src/entry/build_entry.sh:2296` |
| the record echo | `src/entry/build_entry.sh:40251` |
| the by-value linked-image clause (model) | `src/entry/build_entry.sh:31344-31358` |
| `ENTRY_CFG_KEYS` | `scripts/preflight_boot_check.sh:716`; converse check `:1209-1211` |
| Apple's own ARM kernel `-msoft-float` | `external/xnu-hd2-darwin13/xnu/makedefs/MakeInc.def:377-379` |
| the VFP context switch (the discipline) | `external/xnu-hd2-darwin13/xnu/osfmk/arm/pcb.c:298-306` |
| the lazy-enable undef path | `external/xnu-hd2-darwin13/xnu/osfmk/arm/trap.c:1118-1136` |
| VFP save/restore + `init_vfp` (`.s`, keep) | `external/xnu-hd2-darwin13/xnu/osfmk/arm/armv/vfp.s` |
| LEO VFP self-test (C, survives soft-float) | `external/xnu-hd2-darwin13/xnu/osfmk/arm/arm_init.c:230-249` |
| `proc_reg.h`'s cp10 moves (C, survive soft-float) | `external/xnu-hd2-darwin13/xnu/osfmk/arm/proc_reg.h:438-444`, `:467-478` |
| the other `-mfpu` builders | `tools/build_xnu_arm_layer.sh:194-198` (+ §3.5's list) |

**The one item this design leaves open, deliberately**: which of §3.5's eight scripts produce
objects the D13 image links. That is a `grep` over the entry link's object names against each
script's output directory, and it is the build's first step — with the by-value clause (§5) it cannot
be silently wrong.