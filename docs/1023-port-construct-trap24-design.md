# Rung 1023 — open `mach_trap_table[24]` (`mach_port_construct`) AND measure whether trap 24 is dispatched at all

Author: lead. Written 2026-10-16, after the 1022 press (`records/1022-press-read.txt`, verdict
**T15-FIRES**). The whole chain below is by value (cache bytes, linked-image words, kernel source
rows); every claim names where it was read. This doc is the design; it is **not** the verdict — the
press writes that.

> ⚠ **REVISED 2026-10-16 after `records/1023-adjudication.txt`.** The rung's *number* stands (the
> wrapper's trap IS 24 — Apple's `syscall_sw.h:99` `kernel_trap(...,-24,5)` + the stub `mvn ip,#23`
> + `locore.s`'s `rsbs r5,r11,#0` all agree), but its *justification* changes. §0.1's old claim that
> "the value it actually received is 4" was **inference, and the adjudication refutes the route**:
> a dispatched trap 24 would take `swi_mach`'s `teq`-match → `swi_mach_error` → `irqvec_panic` →
> **panic with no return path**, and the 1022 capture has **zero panics**. So trap 24 was **never
> dispatched** in that run, and this rung's real job is to **decide that question by measurement**
> (the `IOS7LAB trap24 frame` dump appears ⟺ trap 24 is dispatched) *and*, if it is, to give it a
> working body. Both outcomes are readings. See §0.1/§0.2 below.

---

## 0. The result (what this rung claims, pre-registered)

0.1 **Defect, restated BY VALUE.** The frontier left by 1022 is pid 1's `udf #254` at cache-rel VA
`0x3824e022` — the `else` arm of libxpc's `__xpc_mach_port_allocate` wrapper (`0x3824dfc8`), taken
when the rv in r0 is **not** `{0, 3, 6}` (0 = success, 3 = `MACH_SEND_INVALID_DEST`, 6 = retry
sentinel). **What r0 actually held is NOT measured** (the old "it is 4" was inference). The wrapper
reaches that `udf` through two calls: `3824e00e blx 0x38260b60` (a dyld stub → slot `0x39f45174` →
the shim `0x381c66ec`, which **is** the trap-24 path: `blx 0x381c59dc` = `mvn ip,#23` = trap 24) and
`3824e01e blx 0x38260ee0` (a dyld stub → slot `0x39f45254` → `0x3815bc38`, a Thumb helper with a
476-byte frame) immediately before the `udf`. Which of the two produced the fatal r0 is unmeasured.

> **CORRECTION (2026-10-16, read BY VALUE from the card cache — `out/stage90/xnu_card_hfs.img`,
> dyld header at card off `0x455d000`, map0 base `0x2c000000`).** Both slots decoded from the raw
> bytes: `0x39f45174 = 0x381c66ed` (shim `0x381c66ec`) and **`0x39f45254 = 0x3815bc39`** (helper
> `0x3815bc38`). The **owner** of `0x3815bc38` is resolved from the cache image table (696 images):
> nearest image base `0x38140000 = /usr/lib/system/libsystem_c.dylib` — **NOT libxpc** (the earlier
> "libxpc helper" label was inference, refuted). The helper's prologue, disassembled as **Thumb**
> (`0x3815bc38`): `push {r4-r7,lr}; add r7,sp,#12; stmdb sp!,{r8,sl,fp}; sub sp,#476; ... movw/movt
> r0,#0x13bc/#0x1dd; add r0,pc; ldr r0,[r0]; ... blx 0x3815d ?` — a libsystem_c routine with a
> 476-byte local frame reached one call before the `udf`. **The peer design doc
> (`docs/1023-port-allocate-design.md:100`) claims `0x38260ee0 → 0x38145110` (libsystem_c); that
> address is ALSO in libsystem_c but is NOT the slot value — the slot holds `0x3815bc39`, so the peer's
> target is one hop wrong.** The frontier for the fallback rung is `0x3815bc38` in libsystem_c.

0.2 **The decisive measurement (the load-bearing half).** On CANCRO `mach_trap_table[24]` reads
`kern_invalid` (`syscall_sw.c:139-143`, gate is bare `IOS7_COMPAT_PLATFORM`; confirmed in the linked
image: row 24 == row 0 == `{argc=0, fn=0x8047f774, name='kern_invalid'}`). `swi_mach` (`0x8001cea4`)
does `teq row[24].fn, &kern_invalid; beq swi_mach_error`, and `swi_mach_error` (`0x8001cef4`) ends in
`irqvec_panic` (`0x8001d258`) → `panic()` — **a dispatched trap 24 PANICS, and the 1022 capture has
zero panics.** Therefore **trap 24 is not being dispatched** (else the run would have ended at the
trap, not at the `udf`). This rung instruments exactly that: widen row 24's gate + retarget the
body's selector (`ios7_compat_vmabi_enabled()` is a compile-time 0 on CANCRO, `ios7_compat.h:11-21`,
so an opened row would otherwise return the shim's own `MACH_SEND_INVALID_DEST` sentinel and be
strictly worse) + add a bounded `IOS7LAB trap24 frame` dump. **If the dump appears, trap 24 IS
dispatched and the frontier is the row; if it does not, the frontier is the wrapper's helper call
(`0x3815bc38`) and the trap row is exonerated.** Either way the press is decisive.

0.3 **The fix (shape (B), the narrow switch), three edits, no new switch.** Reuse
`STAGE90_XNU_IOS7_TRAPS` (already record key 19, already in the pool `DEFINES`, already in `EXTRA`):
(i) widen row 24's gate in `syscall_sw.c:139`; (ii) widen the two port-name externs in
`syscall_sw.c:104-105`; (iii) change trap 24's body runtime selector at `mach_kernelrpc.c:74` from
`ios7_compat_vmabi_enabled()` to `ios7_traps_enabled()`, written so the OFF arm is the tree's own
line. Plus the composition fix to `tools/patch_d13_mach_trap15.py` (§2.4) and the deferral-drop in
`tools/check_d13_ios7_vmwire.py` (§3).

0.4 **Pre-registered verdict cells** (the press picks one; §7 lists the exact witness each needs).

---

## 1. What 1022 settled, and the frontier it left

1022 (`98b278d`, press-read `1d72e9a`) installed `mach_trap_table[15]` and measured the whole chain:
seven `IOS7LAB vm_map64` calls, every one `result=0`, every one a non-zero address. The 1021d wall pc
`0x3823aa54` appears **0** times. So `svc #0x80`, r12=-15 → row 15 → the adapter's frame validation →
`mach_vm_map` → `KERN_SUCCESS` is now measured end to end; the allocator did not trap at trap 15.

The run still stops, elsewhere. From `records/1022-press-read.txt` PART 5, read off the capture:

    xnu_live_undef_pc   = 0x3b418022      slide = 0x031ca000
    => FILE OFFSET 0x3b418022 - 0x031ca000 = 0x3824e022, bytes `fede` = `0xdefe` = `udf #254`
    nearest local symbol <= 0x3824e022   = 0x3824dfc8  (libxpc.dylib, base 0x3824b000)
    fault regs: r0=0x14548ad0 r1=0xe0f33713 r2=0x2 r3=0 r4=0x14548920
                r5=0x3d102068 r6=0x2bda0368 r7=0x2bda037c r8=0x2bda0350
                sp=0x2bda0350 lr=0x3b40bced pc=0x3b418022 cpsr=0x60000030

The wrapper's shape (VA `0x3824dfc8`, disassembled by value):

    3824e00e  blx 0x38260b60          ; a dyld lazy-bind veneer
    3824e012  cbz r0, 0x3824e024      ; KERN_SUCCESS -> return
    3824e014  cmp r0,#3 ; cmpne r0,#6 ; beq retry
    3824e01c  movs r1,#0 ; blx 0x38260ee0 ; udf #254   ; ANY other kern_return_t -> trap

So the wrapper traps on a `kern_return_t` ∉ {0, 3, 6}. The fault's `r0=0x14548ad0` is inside the
1 MiB mapping the *same run* made at `0x14500000` — consistent with the wrapper's own return being
piled on a live `udf` (`records/1022-press-read.txt` PART 5 reads it the same way).

---

## 2. The defect, by value: the trap-24 row is `kern_invalid` on CANCRO

### 2.1 The `blx` target is mach trap 24, not 16

`0x38260b60` is a dyld lazy-bind veneer. Its resolver slot lives in the third cache mapping
(`[0x39f48000, +0x022ed000) fileoff 0xdf46000`), and the slot at VA `0x39f45174` holds `0x381c66ed`
(bind bit set), i.e. the shim entry `0x381c66ec`. The shim (`0x381c66ec`, by value):

    381c66ec  push {r4,r5,r6,r8,lr}      (frame = 5 words, matching a 5-arg trap)
    381c6702  blx 0x381c59dc             ; the fast-trap stub
    381c6706  movs r1,#3 ; movt r1,#0x1000   ; r1 = 0x10000003
    381c670c  cmp r0,r1 ; bne return-r0      ; only the sentinel falls through to the slow leg
    ...

`0x381c59dc` is an **ARM** mach-trap fast-trap stub; its words are, by value:

    e1a0c00d  mov r12, sp
    e92d0030  push {r4,r5}
    e59c4000  ldr r4, [r12]
    e3e0c017  mvn r12, #0x17       ; r12 = ~0x17 = -24
    ef000080  svc #0x80

`svc #0x80` with r12 = -24 indexes `mach_trap_table[24]`. **The encoding is `imm = index - 1`**
(anchor: the trap-15 stub `0x381c5960` is `mvn ip,#14` → -15, `records/1021d-mmap-failure.txt`), so
`#0x17` → 24 — **trap 24, not trap 16.** (An earlier decode of mine read `0x391c59de` by an
arithmetic slip and said 16; the objdump with `--adjust-vma` fixes it. Trap 16
`_kernelrpc_mach_port_allocate_trap` is already installed UNCONDITIONALLY and is not the frontier.)

### 2.2 Row 24's gate, by value

`osfmk/kern/syscall_sw.c` rows (by value):

    139  #if IOS7_COMPAT_PLATFORM
    140  /* 24 */  MACH_TRAP(ios7lab_mach_port_construct_trap, 5, NULL, NULL),
    141  #else
    142  /* 24 */  MACH_TRAP(kern_invalid, 0, NULL, NULL),
    143  #endif
    ...
    144  #if IOS7_COMPAT_PLATFORM
    145  /* 25 */  MACH_TRAP(ios7lab_mach_port_destruct_trap, 5, NULL, NULL),
    ...
    164  #if IOS7_COMPAT_PLATFORM
    165  /* 41 */  MACH_TRAP(ios7lab_mach_port_guard_trap, 5, NULL, NULL),
    166  /* 42 */  MACH_TRAP(ios7lab_mach_port_unguard_trap, 4, NULL, NULL),
    167  #else
    168  /* 41 */  MACH_TRAP(kern_invalid, 0, NULL, NULL),
    169  /* 42 */  MACH_TRAP(kern_invalid, 0, NULL, NULL),
    170  #endif

`IOS7_COMPAT_PLATFORM = BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO` (`ios7_compat.h:5`) — **0
on CANCRO** (`build_xnu_arm_kernel.sh` passes `-DBOARD_CONFIG_MSM8974_CANCRO=1`; the other two are
undefined). So on this board row 24 is `kern_invalid`, whose body (`syscall_sw.c:405-410`) returns
`KERN_INVALID_ARGUMENT` = **4**.

### 2.3 Why 4 makes the wrapper trap, and why 0x10000003 is the sentinel

`MACH_SEND_INVALID_DEST 0x10000003` (`osfmk/mach/message.h:692`, by value). The shim (§2.1) treats
**only** that value as "fast trap unavailable, take the slow leg". Any other non-{0,3,6} return —
including 4 — falls to the wrapper's `udf #254`. This is the direct, by-value cause of the frontier.

**The trap-24 row gate is a SEPARATE `#if` from row 25's** (`:139` vs `:144`): so widening only row 24
leaves row 25 bare and keeps its deferral honest. Rows 41/42 share one `#if` (`:164-170`) and stay
deferred. **This rung opens row 24 only.**

### 2.4 The composition defect, pre-empted (must be fixed in the same change)

`tools/patch_d13_mach_trap15.py` (`main()`, `:351`) asserts:

    if ssw_text.count(GATE_NEW) != 2:  REFUSING  # expected 2 (the extern block + row 15)

A 1023 that widens row 24 makes `syscall_sw.c` carry **3** widened gates, so 1022's patcher would
REFUSE and `scripts/build_1023.sh` step 2 would stop — the same class as
`records/1022-patcher-composition-defect.txt` (one line/value, two owners, the second owner's edit
rewrites the first owner's idempotence assertion). The fix, **a strict widening that alters no staged
byte on any tree state that previously succeeded**:

  a. the expected count becomes "≥ 2", the load-bearing part being that **no bare gate AND no bare
     row-24 row survives** — i.e. assert `GATE_OLD` is absent as a whole line in `syscall_sw.c`
     (and rows 25/41/42 are still the `#else` arms, checked structurally);
  b. also accept 1023's widened extern spelling as an alternate staged form, so a rebuild where 1023
     has run reports "already staged" instead of re-widening.

This is recorded, not silent: the file is 1022's lane by name (`mi4-file-lanes`); the edit is made
because it **blocks** 1023's build and is a strict predicate widening. `records/1023-patcher-
composition.txt` will record it exactly as 1022's did.

---

## 3. The fix, edit by edit

All three code edits reuse `STAGE90_XNU_IOS7_TRAPS`; every new `#if` is written
`defined(...) && ...` (`defined()` first, so an unset switch never evaluates the identifier —
`mi4-off-option-two-spellings`), and every OFF arm is the tree's own text byte for byte.

| # | file:line | old | new |
|---|-----------|-----|-----|
| 1 | `syscall_sw.c:139` | `#if IOS7_COMPAT_PLATFORM` | `#if IOS7_COMPAT_PLATFORM \|\| (defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS)` |
| 2 | `syscall_sw.c:104-105` (the two port externs) | bare-decl block under `#if IOS7_COMPAT_PLATFORM` | the same block, gate widened (identity is per-line; 1022 already widened the block's FIRST line, so 1023 must not re-anchor on it — see §2.4b) |
| 3 | `mach_kernelrpc.c:74` (`ios7lab_mach_port_construct_trap`) | `if (!ios7_compat_vmabi_enabled())` | `#if defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS` → `if (!ios7_traps_enabled())` `#else` → the tree's line `#endif` |

`ios7lab_mach_port_construct_trap` is already reachable: the file-wide gate at `mach_kernelrpc.c:39`
and the inner lab-adapter gate at `:59` were both widened by 1022, so the body **compiles** on CANCRO
today — only its row and its runtime selector keep it dead. (By value: `nm
out/xnu_kernel_obj_d13/osfmk_ipc_mach_kernelrpc.o | grep ios7lab` after 1022 lists the body.)

**Deliberately NOT touched in 1023** (the same discipline 1022 described at its `:28`): the rows
25/41/42 gates; `ios7lab_mach_port_context_guard`'s and `ios7lab_mach_port_destruct_trap`'s runtime
selectors (`:136`, `:181`); `IOS7_COMPAT_PLATFORM` itself (`ios7_compat.h:5`), whose 24 bare `#if`
sites include `osfmk/mach/arm/vm_param.h:63` (`VM_MAX_ADDRESS` halves — a real image change, not an
inert one). Opening rows 41/42 with 1022's machinery is the **next** rung's question, if the press
names it.

---

## 4. Body semantics — the trap-24 wire, and the two unmeasured inputs

`ios7lab_mach_port_construct_trap(void *saved_registers)` reads the SAVED-REGISTER FRAME, not a C ABI
(the same discipline as 1022's vm_map). Required semantics for `mach_port_construct` (iOS 7) are
`{target, options*, context, options-flags}` — but this body reads a **different, wider** frame than
the vm_map one, by value:

    rv = MACH_SEND_INVALID_DEST;
    if (!ios7_compat_vmabi_enabled()) return rv;       # <- edit 3 moves this to ios7_traps_enabled()
    task = port_name_to_task(regs[0]);
    if (task != current_task()) goto done;             # -> MACH_SEND_INVALID_DEST
    copyin((user_addr_t)regs[1], options, sizeof(options));   # 24 bytes read from USER
    if ((options[0] & ~0x7FU) || regs[3] || options[2] || options[3] || options[4] || options[5])
        rv = KERN_INVALID_ARGUMENT;                    # -> 4
    rv = mach_port_allocate(task->itk_space, MACH_PORT_RIGHT_RECEIVE, &name);
    rv = mach_port_set_context(task->itk_space, name, regs[2]);
    if (options[0] & 0x2U) mach_port_set_attributes(.., MACH_PORT_LIMITS_INFO, ..);
    if (options[0] & 0x10U) { ipc_object_copyin(.., MAKE_SEND, ..); mach_port_insert_right(..); }
    rv = copyout(&name, (user_addr_t)regs[4], sizeof(name));   # 4 bytes written to USER

**Two of these are UNMEASURED and this rung must not assume them** (`mi4-a-claim-in-a-comment-is-not-
a-check`):

* `regs[1]` is treated as a user pointer to 24 bytes of options. The fault frame has
  `r1 = 0xe0f33713` — an **unmapped-looking** value (top half nonzero), not a plausible user text
  pointer like the `0x2be8....` values seen elsewhere in this process. If `regs[1]` is really the
  options **word** (not a pointer), the `copyin` faults, `MACH_SEND_INVALID_DATA` comes back, and the
  wrapper traps identically — a **named** outcome, not a pass.
* `regs[4]` is treated as the output name pointer; the fault frame has `r4 = 0x14548920`, and
  `r0 = 0x14548ad0` sits **0xd0 below** it inside the same 1 MiB mapping. The gap is consistent with
  `r4` being a stack/scratch base and `r0` a returned out-pointer, but that is a **hypothesis**, and
  the press states which it is.

The design therefore **does not silently "fix" the frame mapping by guesswork.** It installs the row
and retargets the selector (the two changes the by-value chain *proves*), and it **stages a
dispute-resolver frame dump** inside the body, exactly as 1022 did for trap 15: a switch-gated
`IOS7LAB trap24 frame r0..r5,r8` and, on the two failure paths, the concrete `copyin` error and the
decoded `options[0]`. The capture then decides:

    3824e022  udf #254    -> the frame the body read was not the one xpc sent (body reached `done`
                             with rv ∉ {0,3,6}); the dump names which input was refused
    result=0 + no udf     -> the row + selector are the whole fix; the wrapper completed

---

## 5. Coupling risk (stated, then gated)

* **`mach_port_construct` is a real kernel operation**, unlike 1022's pure `mach_vm_map` pass-through.
  It allocates a receive right and, under `options[0] & 0x10`, inserts a send right. The body is the
  LEO lab reimplementation (guard enforcement / temp-owner / importance deliberately bypassed, per its
  own comment) and is already compiled for ARMPBA8. So this is not new code — it is code that has run
  on another board. Still: a wrong `regs[1]` (a bogus 24-byte `copyin`) is contained by
  `copyin`'s own fault handling (returns non-zero), and a wrong `regs[4]` `copyout` is contained the
  same way — neither can corrupt the kernel heap.
* **The one bad shape is the sentinel**: retargeting the selector is what prevents the
  `MACH_SEND_INVALID_DEST` regression; with edit 3, OFF reproduces the tree exactly and ON returns a
  real `mach_port_allocate` result.
* **The deferral must hold**: rows 25/41/42 and the name table rows 24/25/41/42 all stay
  `kern_invalid` (see §6.1 — this rung does **not** touch `mach_syscall_name_table`).

---

## 6. Two open questions this doc leaves for the recon and the press (honesty section)

6.1 **The name-table row.** `mach_syscall_name_table[15]` and `[24]` both still read `"kern_invalid"`
(`syscall_sw.c:282`, `:291`) — the name array is a SEPARATE, **ungated** table. Note the table's own
convention: every implemented trap has its implementation string, every unimplemented one says
`"kern_invalid"` (`:16` `_kernelrpc_mach_port_allocate_trap`, `:17` `_kernelrpc_mach_port_destroy_trap`
…). Trap 15 was opened by 1022 **without** touching its name string, and 1022 passed its by-value
clause — so the name word is read by nothing on this boot path (`mach_trap_table`'s own
`mach_trap_name` is what the checker reads). 1023 **will not** change it either (changing it would
move `osfmk/arm/model_dep.c:713`'s panic decode and the dtrace provider names for a row the crash
never consults, against `mi4-...-never-move-what-you-did-not-measure`). This is recorded as SUNKNOWN,
and the recon confirms no consumer is on the 1023 path.

6.2 **Is trap 24 really `mach_port_construct`?** By value the *row* points at
`ios7lab_mach_port_construct_trap` when opened, and the cache-link chain says the shim is reached from
`__xpc_mach_port_allocate`; the recon (running now, READ-ONLY) names the dyld-side symbol and the
neighbouring stub indices. If the recon shows the stub is actually a different index, the design's edit
1 retargets trivially — the defect proof (§2.1-2.3) does not depend on the name, only on the index and
the gate.

---

## 7. Pre-registered verdict cells (the press picks exactly one)

| Cell | Witness in the capture | Reading |
|------|------------------------|---------|
| **T24-FIRES** | the 1022 frontier pc `0x3b418022` (`udf #254`) appears **0** times; ≥1 `IOS7LAB port_construct ... result=0`; `xnu_live_undef_pc` is either absent or differs | row + selector retired the frontier; the new stop (if any) is named by the NEXT `xnu_live_undef_pc` |
| **T24-SENTINEL** | the body printed `result=268435459` (`0x10000003`) **and** the shim took its slow leg | edit 3 did not take (selector still `vmabi`) — the 1022 §0.2 trap, now for the port family |
| **T24-BADFRAME** | the body printed a `copyin`/`KERN_INVALID_ARGUMENT` result with the frame dump showing `r1` not a user pointer | the trap-24 wire differs from the body's assumption (§4); the dump names the slot |
| **T24-DEFERRAL-BROKEN** | any of rows 25/41/42 or rows 16-23 changed, or the row did not install at all | the build's clause failed to hold; recorded as a defect |

The by-value clause for the build side is `tools/check_1023_trap24.py <elf> --expect on|off` mirroring
`check_1022_trap15.py`: it reads `mach_trap_table` off the section table and asserts row 24 ==
{arg_count 5, fn == `&ios7lab_mach_port_construct_trap`, name == the handler} (ON) or == entry 0's own
{fn,name} (OFF), **and** that rows 25/41/42 still read `kern_invalid` keyed on entry 0.

---

## 8. Carriers / plumbing (all already present — no new switch)

* `STAGE90_XNU_IOS7_TRAPS=1` — record line 19; pool `DEFINES` (`tools/build_xnu_arm_kernel.sh:902`);
  `EXTRA` (`src/entry/build_entry.sh:2109`); `ENTRY_ARM_KEYS` (`:2312`).
* `STAGE90_XNU_GLOBAL_FIX=1` — **no record carrier** (`records/1020-build-defect.txt`); carried only
  in `out/stage90/xnu_arm_entry-arm-extra.txt`. `build_1023.sh` must re-assert it explicitly, exactly
  as `build_1022.sh` does.
* New: `records/1023-patcher-composition.txt` (the §2.4 fix), `tools/patch_d13_mach_trap_ports.py`
  (this rung's patcher), `tools/check_1023_trap24.py`, `scripts/build_1023.sh`, `scripts/park_1023.sh`,
  `scripts/press_1023.sh`, `scripts/check_1023_capture.py`.

---

## 9. Unmeasured / assumed (so the press can falsify, not paper over)

* The exact trap-24 user-side wire (`regs[1]` pointer-vs-word, `regs[4]` out-pointer) — §4.
* Whether `ios7lab_mach_port_construct_trap` returns {0,3,6}-free values for the real xpc call.
* Whether the *next* frontier is row 25 (destruct) or rows 41/42 (guard/unguard) — the press names it.
* The `mach_syscall_name_table[24]` string — deliberately left (§6.1).

---

## 10. Claims (falsifiable)

C1. `mach_trap_table[24]` reads `kern_invalid` in the pre-1023 linked image. *(check: `check_1023_
   trap24.py --expect off` passes on `out/stage90/xnu_arm_entry.elf` today.)*
C2. The 1022 frontier udf is caused by trap 24 returning 4, not by trap 16 (already installed).
   *(evidence: §2.1 stub index 24; §2.2 row 24 `#else` = `kern_invalid`.)*
C3. Opening the row without retargeting the selector reproduces the T24-SENTINEL cell. *(evidence:
   §0.2; the OFF arm byte-identity proof in `patch_d13_mach_trap_ports.py`.)*
C4. The 1022 patcher's `count != 2` assertion blocks a row-24 widening; the §2.4 fix removes the block
   without changing any tree state that previously succeeded. *(evidence: run both patchers in
   plan mode; `#if`/`#endif` balance stays 6/6 in `syscall_sw.c`.)*

---

## 11. File:line anchors (for the patcher's exact-match asserts)

    syscall_sw.c:102        the extern-decl gate (1022 widened its FIRST line; anchor the port lines)
    syscall_sw.c:104-105    extern int ios7lab_mach_port_construct_trap / _destruct_trap
    syscall_sw.c:139        #if IOS7_COMPAT_PLATFORM      <- row 24's gate (edit 1)
    syscall_sw.c:140        /* 24 */ MACH_TRAP(ios7lab_mach_port_construct_trap, 5, NULL, NULL),
    syscall_sw.c:142        /* 24 */ MACH_TRAP(kern_invalid, 0, NULL, NULL),   <- the #else
    syscall_sw.c:291        /* 24 */ "kern_invalid",   (name table; NOT edited — §6.1)
    mach_kernelrpc.c:39     file-wide gate (already widened by 994)
    mach_kernelrpc.c:59     inner lab-adapter gate (already widened by 1022)
    mach_kernelrpc.c:64     ios7lab_mach_port_construct_trap
    mach_kernelrpc.c:74     if (!ios7_compat_vmabi_enabled())   <- edit 3
    mach_kernelrpc.c:136    ios7lab_mach_port_context_guard selector (guard/unguard; NOT edited)
    mach_kernelrpc.c:181    ios7lab_mach_port_destruct_trap selector (NOT edited)
    osfmk/arm/ios7_compat.h:11-21   ios7_compat_vmabi_enabled() (compile-time 0 on CANCRO)
    osfmk/arm/ios7_compat.h:36-45   ios7_traps_enabled() (the switch-respecting selector)
    osfmk/mach/message.h:692        MACH_SEND_INVALID_DEST 0x10000003
    tools/patch_d13_mach_trap15.py:351   the `!= 2` assertion (the §2.4 composition defect)
    tools/check_d13_ios7_vmwire.py:90    DEFERRED_TRAPS = (24, 25, 41, 42)  <- drop 24