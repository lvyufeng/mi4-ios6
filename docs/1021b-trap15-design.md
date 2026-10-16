# 1021b — INSTALL `mach_vm_map` AT `mach_trap_table[15]` FOR D13/CANCRO

**This is a rung design. No build, no press, no device action, no `git add`, no write to `out/stage90`
was performed while authoring it.** Every value in §1-§3 below was read out of the tree or out of the
LINKED image / the extracted dyld cache **by value**, with the command shown.

It answers the ONE question `docs/1021-design.md` §1 put on the table for the R-RAISES-*/R-VFP-CONTINUES
branch: *`mach_trap_table[15]` is `kern_invalid` by value, so a `-15` svc panics at `swi_mach_error` —
was that row ever installed, and if not, what is the minimal rung that installs it?* The dispatch
arithmetic is settled in `records/1020-trap-dispatch.txt` §1 (`mvn ip,#14` ⇒ `ip = 0xFFFFFFF1 = -15` ⇒
`rsbs r5,r11,#0` ⇒ `r5 = 15` ⇒ stride 12 ⇒ fn at `table+184`); this document does not re-derive it, it
acts on it.

> **STATUS ON WRITE: 1020b (`armed-d13-b5093f00`, `362494f`) is PARKED and PRESSED and did not return;
> its capture is owed.** The 1020b press is the run that decides whether trap 15 is *reachable at all*
> on pid 1's path, because 1015b's GLOBAL-FIX is restored in that arm (the storm is gone) and the `udf`
> now raises instead of looping. **1021b is therefore designed to be built on top of 1020b's switch set,
> and the first capture from it is also the first honest reading of the §5 cells.** Until that capture
> exists, this rung must not be built: a rung that installs a trap handler the boot never issues looks
> exactly like a rung that installs nothing.

> **⚑ ADDENDUM 2026-10-16 (added after the sibling read landed; supersedes the STATUS above on ONE
> point). `records/1021-guard-leg.txt` + `records/1021-fpu-rootcause.txt` moved the frontier off trap 15
> entirely. This rung is DEFERRED.** See **§9** at the foot of this document. §1-§5 are kept intact as a
> deferred-design reference (they remain correct *if* a future capture shows a `-15`); **§9(a)** records
> why trap 15 is *deliberately not the wall*, and **§9(b)** re-states that the table stays index-stable
> whether or not the rung is revived.

---

## 0. THE RESULT IN ONE LINE, AND THE THREE SHAPES THAT WERE CONSIDERED

**The row exists in exactly one place in this tree and it is gated shut:**
`external/xnu-hd2-darwin13/xnu/osfmk/kern/syscall_sw.c:126-130`

```c
#if IOS7_COMPAT_PLATFORM
/* 15 */	MACH_TRAP(ios7lab_mach_vm_map_trap, 8, NULL, NULL),
#else
/* 15 */	MACH_TRAP(kern_invalid, 0, NULL, NULL),
#endif
```

with `IOS7_COMPAT_PLATFORM = (BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO)`
(`osfmk/arm/ios7_compat.h:5`) and the D13 build defining only `-DBOARD_CONFIG_MSM8974_CANCRO=1`
(`tools/build_xnu_arm_kernel.sh:202`). `records/1020-trap-dispatch.txt` §4(a) read the consequence out
of the LINKED image: entry 15 = `{argc 0, fn 0x80486408, name 'kern_invalid'}`.

| shape | where | verdict |
|---|---|---|
| **(i) widen `IOS7_COMPAT_PLATFORM`** to include `MSM8974_CANCRO` | `ios7_compat.h:5` | **REJECTED.** The gate has **32 occurrences in 13 files** (census, §3.4, measured). Widening it flips `ios7_compat_vmabi_enabled()` from `0` to a **constant 1** on D13, which turns on `kern_fork`'s "reserve early pids", `kern_exec`, `kern_descrip`, `unix_syscalls.c`'s 4 psynch/kqueue dispatch arms, `ipc_kobject.c`'s 4 arms, `vm_map.c`'s 3, `vm_user.c`'s 3 — i.e. it changes the ABI and the boot for the whole image, to open **one table row**. It is also self-defeating: the row's body calls `ios7lab_mach_vm_map_trap`, whose own first statement is `if (!ios7_compat_vmabi_enabled()) return MACH_SEND_INVALID_DEST;` (`mach_kernelrpc.c:217`) — the same gate that `(mach_kernelrpc.c:59) #if IOS7_COMPAT_PLATFORM` excludes from the file on D13. So the "row install" is compilable but the body is not. **Not a rung, a rebase.** |
| **(ii) a new `STAGE90_XNU_MACHVM_TRAP15` switch that replaces only the index-15 `kern_invalid` row** | `syscall_sw.c:126-130` (row) + `mach_kernelrpc.c:59` (the gate that re-admits the **existing** body, §2.2) | **CHOSEN.** One `#if` inside the already-existing `#else` plus one widened `#if` — **no new function, no board-gate widening** — and OFF is the tree's bytes exactly (the `#else` arm is the tree's line verbatim). Checkable BY VALUE on the linked image (§5). |
| **(iii) copy Apple 4570's `_kernelrpc_mach_vm_map_trap`** | `external/xnu-4570.1.46/osfmk/ipc/mach_kernelrpc.c:100` | **REJECTED.** 4570's body is reached through a **MIG-generated args struct** and calls `mach_vm_map_external` with `args->size`/`args->mask` as `mach_vm_size_t`. On this tree `mach_vm_offset_t`/`mach_vm_size_t` are **32-bit** (`ios7_compat.h:27-29`, "we target iOS 4.3"), so 4570's wire is the *native* one, not iOS 7's — adopting it would install a row that reads the iOS-7 frame one field early, which is the exact defect the 994 wire rung exists to fix. |

**The chosen shape is (ii), and it reuses the lab adapter by gating it, not by rewriting it.**

> **⚠ AN INERT ROW IS WORSE THAN NO ROW — the one thing about this rung that must not be got wrong.**
> The adapter's early-out is a **positive** kern_return (`MACH_SEND_INVALID_DEST`,
> `mach_kernelrpc.c:217`), so `swi_exit` returns it to userspace and **no panic** — the row is *safe to
> install in the panic sense*. But `_mach_vm_map`'s sentinel test (`0x381c667e-0x381c668a`:
> `mov r4,r0; movs r0,#3; movt r0,#0x1000; cmp r4,r0; bne 0x381c66ba`) treats **any non-sentinel
> return** as "the fast trap ran, the result is real" and **branches away from `bl 0x381c7f74`** (the
> MIG demux that currently works). A row that returns `MACH_SEND_INVALID_DEST` therefore makes the shim
> report **failure without ever trying the path that succeeds** — strictly worse than today, where
> trap 15 is `kern_invalid` and `swi_mach_error` panics *before* the shim can do anything.
>
> ⇒ **`TRAP15=1` without §2.2's S-A is a regression, not an intermediate rung.** S-A is the difference
> between a fix and a regression, and that is why it is part of this rung rather than a follow-up.
> (Naming the mechanism honestly, by value: `swi_mach_error:` is defined **once** (`traps_lo.s:252-260`)
> and has exactly **two** branch sites — `:202` `bge` (index `>= 0x80`, out of range) and `:213` `beq`
> (the `teq r1, kern_invalid`). Installing a row at index 15 removes the `:213` site for that index, so
> this rung is the first time on **any** board in this tree that index 15 leaves the dispatcher as a
> *value* rather than a *panic*.) If the first build cannot carry S-A, **do not press it**.

---

## 1. THE WIRE, BY VALUE — WHAT THE TABLE ROW'S BODY MUST READ

The row's second word is `kern_return_t (*)(void *)` and `traps_lo.s:216-219` calls it as
`mov r0, r8; ... bx r1` where `r8` is the **saved-state frame** (`traps_lo.s:126 mov r8, sp`, the frame
built at `:122 stmea sp, {r0-lr}^`). So `regs[n]` in any `ios7lab_*` body is the **user's r<n>**, and the
libsystem stub that issues the svc is a **register-image** walker, not the C ABI:

```
# read by value out of the extracted cache (byte-identical to rootfs.hfs @0x455d000, 1017 §1.2)
$ python3 -I -c "d=open('/tmp/dyld-extract/dyld_shared_cache_armv7','rb').read();
                 open('/tmp/st2.bin','wb').write(d[0x381c5940-0x2c000000:][:0x60])"
$ arm-none-eabi-objdump -D -b binary -m arm --adjust-vma=0x381c5940 /tmp/st2.bin
381c5960: mov ip, sp ; push {r4,r5,r6,r8} ; ldm ip, {r4,r5,r6,r8}
381c596c: mvn ip, #14                      ; ip = ~14 = 0xFFFFFFF1 = -15  == table index 15
381c5970: svc 0x80
```

The stub copies **user r0,r1,r2 into stub r4,r5,r6** and **user r3 into stub r8** — so the trap-15
frame is exactly:

| frame word | `regs[n]` | meaning |
|---|---|---|
| `r0` | `regs[0]` | `target` (task port name) |
| `r1` | `regs[1]` | user `mach_vm_address_t *address` |
| `r2` | `regs[2]` | `size_low` |
| `r3` | `regs[3]` | `size_high` |
| `r4` | `regs[4]` | `mask_low` |
| `r5` | `regs[5]` | `mask_high` |
| `r6` | `regs[6]` | `flags` |
| `r7` | `regs[7]` | **no default value**: `r7` is `frame` for the fast path, but **`r5` (hence frame `r7`) for the MIG fall-through** — see the ⚠ below |
| `r8` | `regs[8]` | `cur_protection` |

**This is a by-value confirmation that the existing adapter was authored for exactly this frame** —
`mach_kernelrpc.c:208-243` reads `regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[8]`
and tests `if (regs[3] || regs[5] || (address64 >> 32))` → `KERN_INVALID_ARGUMENT`, then calls
`mach_vm_map(task->map, &address32, regs[2], regs[4], regs[6], IPC_PORT_NULL, 0, FALSE, regs[8],
VM_PROT_ALL, VM_INHERIT_DEFAULT)` and `copyout`'s the 64-bit answer back to `regs[1]`. **The `8` in the
row's `MACH_TRAP(..., 8, ...)` is not a contradiction of this 9-word picture: word 7 is unread by
either side.** (The table's `mach_trap_arg_count` is never consulted on ARM — `traps_lo.s` reads only
`[entry+4]`, and the only `mach_trap_arg_count` readers in the tree are `i386/bsd_i386.c:478/550` and
`dtrace/systrace.c:743`, none of them in this image. So the count is documentation on ARM.)

> **⚠ WORD 7 IS NOT DEAD WHEN THE MATCHER DID NOT RUN IN "USER" MODE (read by value, and it is the
> single sharpest unknown in this design).** `entry_note_user_svc` (`entry_stubs.c:7341`) records
> `xnu_live_usvc_num` as `r12` of the **saved user frame** — the value that crossed the exception — and
> the capture's own `-3` reading (`records/1020-trap-dispatch.txt` §4(b)) is exactly the `stub r12 =
> 0xFFFFFFFD` set by `0x381c57e4`'s `mvn ip,#2`, i.e. the stub's own write, which lands in the user
> frame's r12 slot regardless of the stub's operating mode. That is why **`xnu_live_usvc_num == -15`
> does NOT prove the frame's `r8` was `cur_protection`**: in the `0x381c5960` stub, `r8` is written
> **only on the fast path** (`ldm ip,{r4,r5,r6,r8}` at `0x381c5968`); on the MIG fall-through the stub
> reaches `mvn ip,#14; svc` **without that `ldm`**, so `regs[8]` carries whatever the shim left in
> `r5`/`r8` — and `0x381c66ba mov r6,r0` (`= *(0x39f38086)`) is the one write the shim does make.
> **So a `-15` in `usvc_num` combined with a nonsense `prot=` in the adapter's own printf is a reading
> about the FRAME, not about the trap number** — and it is precisely what the `T15-CHECKSUM-MISS` cell
> exists to catch. This rung must not report `-15` as "the wire is good".

**Honest scope, stated because the rung must not over-claim.** What §1 establishes is that the frame
*looks like* iOS 7's wire and that the adapter matches it. What it does **not** establish is that
libsystem_kernel's `_mach_vm_map` was actually entered with the arguments it was written for on pid 1's
run — the shim's three-field guard (`0x381c663c`: arg `[r7,#20] == 0` and `lr == 7` and `r9 == 1`) is
**unmeasured**, and on the fall-through at `0x381c66ba` the mixed path writes `r4 = r0` (the MIG demux's
return) with **no sentinel**, which then rides into the same stub's r8. That is `records/1020-trap-dispatch.txt`
§3's amputated tail, and §5's `T15-CHECKSUM-MISS` cell is how this rung reads it instead of assuming it.

---

## 2. THE EXACT EDIT — ONE FILE, ONE GATE, ONE BODY

### 2.1 The table row — `osfmk/kern/syscall_sw.c` (POOL file)

**File:** `external/xnu-hd2-darwin13/xnu/osfmk/kern/syscall_sw.c`, the `#else` at **line 128-130**.
Switch **ON** (staged by a script, §3):

```c
#if IOS7_COMPAT_PLATFORM
/* 15 */	MACH_TRAP(ios7lab_mach_vm_map_trap, 8, NULL, NULL),
#else
/* 15 */	MACH_TRAP(kern_invalid, 0, NULL, NULL),
#endif
```
becomes
```c
#if IOS7_COMPAT_PLATFORM
/* 15 */	MACH_TRAP(ios7lab_mach_vm_map_trap, 8, NULL, NULL),
#else
#ifdef STAGE90_XNU_MACHVM_TRAP15          /* IOS7MVT15_BEGIN */
/* The extern at :103-107 is inside the IOS7_COMPAT_PLATFORM gate above, so on this board the
 * switched row must bring its OWN declaration - an implicitly-declared `ios7lab_mach_vm_map_trap`
 * would still relocate (the table takes its ADDRESS) and `-w` would hide it, which is exactly the
 * class of thing this rung must not ship. */
extern int ios7lab_mach_vm_map_trap(void *saved_registers);
/* 15 */	MACH_TRAP(ios7lab_mach_vm_map_trap, 8, NULL, NULL),
#else
/* 15 */	MACH_TRAP(kern_invalid, 0, NULL, NULL),
#endif
#endif                                    /* IOS7MVT15_END */
```
**⚠ THIS EXTERN IS NOT OPTIONAL AND IT IS THE EDIT'S EASIEST THING TO GET WRONG** (measured:
`syscall_sw.c:102` opens `#if IOS7_COMPAT_PLATFORM` and `:108` closes it, so `:103-107`'s three
`extern int ios7lab_mach_vm_map_trap(void *);`-style declarations are **compiled out on CANCRO** — see
the read in §2.2). A build that omits it may still *link* (an implicit declaration still emits the
symbol reference) and `-w` is in `CC_ARGS` for every pool file, so **nothing in the build would say
so** — [[mi4-silence-is-a-reading-only-if-success-is-silent]] and
[[mi4-a-claim-in-a-comment-is-not-a-check]]. The prototype in the tree is `int
ios7lab_mach_vm_map_trap(void *saved_registers)` (`mach_kernelrpc.c:208`, and the extern form at
`:103`), so use that text verbatim.
**OFF is byte-identical by construction:** the `#else` arm of the new `#ifdef` is the tree's line
**verbatim**, and `STAGE90_XNU_MACHVM_TRAP15` is `defined()`-first nowhere in the tree when the switch is
off, so the preprocessor emits exactly the tree's tokens ([[mi4-off-option-two-definitions]], i.e.
[[mi4-off-option-two-spellings]]: `#ifdef` on an undefined identifier is the safe spelling, and the
simpler `#if defined(X) && X` form must not be used here **because OFF must be the tree's text in the
built object for THIS file only if the switch is undefined — `#if defined(...) && ...` would still be
byte-identical, so either is legal; `#ifdef` is chosen so the exit criterion reads the same in both
files.** If the reviewer prefers symmetry with every other rung, use
`#if defined(STAGE90_XNU_MACHVM_TRAP15) && STAGE90_XNU_MACHVM_TRAP15`; the OFF bytes are the same.)

**The name table is deliberately NOT edited.** `mach_syscall_name_table[15]` stays `"kern_invalid"`
(`syscall_sw.c:282`). Two reasons, both by value: (a) its only reader in this tree is
`mach_syscall_trace` (`osfmk/arm/model_dep.c:713-734`), and on CANCRO that function's body is entirely
inside `#if BOARD_CONFIG_ARMPBA8` — it is **inert**, so editing it changes no behaviour; (b) leaving it
means this rung's diff is one table word + one body, and the by-value clause in §5 can key on the
`fn` word alone (`[[mi4-a-claim-in-a-comment-is-not-a-check]]`: a name-table edit would be a claim in
data nothing reads).

### 2.2 The body — `osfmk/ipc/mach_kernelrpc.c` (POOL file), **no new function**

The adapter already exists and is already correct for this frame; it is excluded from the D13 compile
by exactly one line. Gate **that one line** so the existing body is reused unmodified.

**File:** `external/xnu-hd2-darwin13/xnu/osfmk/ipc/mach_kernelrpc.c`, **line 59**:

```c
#if IOS7_COMPAT_PLATFORM /* port/vm_map lab adapters: ARMPBA8/LEO only */
```
becomes
```c
#if IOS7_COMPAT_PLATFORM || defined(STAGE90_XNU_MACHVM_TRAP15)   /* IOS7MVT15_BOTH_BEGIN */
```
Its `#endif` is at **line 244** (`#endif /* port/vm_map lab adapters */`) and is **not edited**.

**What this opens, exactly (by value, `mach_kernelrpc.c:59-244`):** five function definitions —
`ios7lab_mach_port_construct_trap` (`:64`), `ios7lab_mach_port_context_guard` (`:128`, `static`),
`ios7lab_mach_port_guard_trap` (`:163`), `ios7lab_mach_port_unguard_trap` (`:168`),
`ios7lab_mach_port_destruct_trap` (`:173`), and **`ios7lab_mach_vm_map_trap` (`:208`)** — plus the
`extern` declarations at `:103-107`, which sit **outside** this block already (`:102` opens, `:108`
closes) and are therefore already compiled on D13.

**Why 59 and not an inner `#if` around `:208-243` alone.** Two reasons, both by value:
1. **`:64-203` is dead code after this rung, and that costs bytes but nothing else.** No trap row
   references `ios7lab_mach_port_*` on CANCRO (the rows 24/25/41/42 stay `kern_invalid`, and the rows
  stay `kern_invalid` because only `syscall_sw.c`'s row **15** is edited, and
   `tools/check_d13_ios7_vmwire.py:380-403` **asserts** they do — this rung must keep that checker's
   rule 6 passing, see §5). So the five extra bodies are linked-but-unreachable, and the five
   `IOS7LAB port_*` format strings appear in the image without ever printing. That is acceptable and it
   is honest; the alternative is a second, narrower anchor.
2. **The narrower edit is a two-line edit and is available if the reviewer wants the byte cost back:**
   wrap `:208` in `#if IOS7_COMPAT_PLATFORM || defined(STAGE90_XNU_MACHVM_TRAP15)` … `#endif` (its
   `#endif` would be the line before `:244`). **It is NOT recommended for the first build:** the
   `#if`-at-`:59` form is a **one-token** change whose OFF shape is the tree's bytes exactly, and a
   two-anchor patch is the shape that produced `records/1020-build-defects.txt`. If the byte cost
   matters, it is a follow-up rung with its own clause, not a variant of this one.

**What `TRAP15=1` alone does, stated exactly.** The body's own early-out is
`ios7_compat_vmabi_enabled()` (`:217`) → `MACH_SEND_INVALID_DEST` (a **positive** kern_return, so
`swi_exit` returns it to userspace and **no panic**). On CANCRO that inline is the constant-0 `#else`
of `ios7_compat.h:11-21`, so **`STAGE90_XNU_MACHVM_TRAP15` alone installs a row that always returns
`MACH_SEND_INVALID_DEST`** — and, per §0's ⚠, a shim that sees a non-sentinel return **skips its MIG
fallback**. **`TRAP15=1` alone is therefore a REGRESSION, not an intermediate rung, and must not be
pressed.** The working row needs the selector to answer 1, and there are three ways; the design
**requires the first** and treats it as part of this rung rather than as a follow-up:

| # | how the selector answers 1 on CANCRO | cost |
|---|---|---|
| **S-A (preferred)** | add a `defined(STAGE90_XNU_IOS7_BOOTARG)` arm to `ios7_compat_vmabi_enabled()`'s `#else` that reads the **existing** arm-extra carrier's `role=` token as a boot argument | one more small switch; **portable at runtime** (a build can carry the row and still be inert unless the press declares it); mirrors `ARMPBA8`'s `ios7lab_vmabi=1` exactly (`ios7_compat.h:16-17`) |
| S-B | make the D13 arm of `ios7_compat_vmabi_enabled()` return 1 whenever `STAGE90_XNU_MACHVM_TRAP15` is set | two constants; turns on **every** `… && ios7_compat_vmabi_enabled()` arm in the census (`kern_fork`, `kern_exec`, `kern_descrip`, `unix_syscalls`, `ipc_kobject`, `vm_map`, `vm_user`, `mach_kernelrpc`'s four port adapters) — see §3.4. **Wider than this rung's question** |
| S-C | widen `IOS7_COMPAT_PLATFORM` | §0(i). No. |

**S-A is REQUIRED for this rung to be a fix rather than a regression (§0's ⚠: an inert row makes the
shim skip its working fallback).** It is deliberately a *second* switch, because it is the one that
makes the rung falsifiable: with S-A the arm is `TRAP15=1` **+** the arm-extra role token, and a run
whose capture shows `-15` never arriving in `usvc_num` is a reading about the *trap*, while S-B would
have changed eight other subsystems on the same arm and made the reading uninterpretable
([[mi4-measurement-defects]]). **The build must carry `TRAP15=1` + S-A; there is no "first build without
S-A" that is safe to press.**

⚠ **S-A is the one part of this design that is NOT already proven to exist in the tree.** It needs a
boot-arg name, a place in the arm-extra line, and a reader. It is specified here at the level the
evidence supports (one selector arm + one carrier token + one press grep) and **if the reviewer wants
`TRAP15=1` landed before S-A, the row must be built as a NON-PRESSED arm** — its only legitimate reading
is the object/linked-image clause (§5), i.e. "the `-D` reached the pool and the row is in the image",
and even that arm must not be booted. The rest of this document treats `TRAP15=1` **+ S-A** as the rung.

### 2.3 The staging (external/ is re-provisionable)

Per the six-pool-edit precedent (`tools/stage_d13_undef_raise.sh`, `tools/stage_d13_shreg_wire.sh`), the
edit is a **script**, not a hand edit, and the receiver of the edits is **two files**:

* `tools/patch_d13_machvm_trap15.py` — idempotent; asserts **each** anchor's occurrence count
  **exactly** (`syscall_sw.c`'s **five-line** text `:126-130` — `#if IOS7_COMPAT_PLATFORM` /
  `/* 15 */\tMACH_TRAP(ios7lab_mach_vm_map_trap, 8, NULL, NULL),` / `#else` /
  `/* 15 */\tMACH_TRAP(kern_invalid, 0, NULL, NULL),` / `#endif` — **exactly once in the file**;
  **measured**: `grep -c 'MACH_TRAP(kern_invalid, 0, NULL, NULL)' osfmk/kern/syscall_sw.c` is **84** and
  `mach_kernelrpc.c:59`'s inner-gate line is matched **once** but the substring
  `#if IOS7_COMPAT_PLATFORM` occurs there twice (`:39`, `:59`), so **neither anchor may be a one-line
  grep** — the anchor is the unique text that contains `ios7lab_mach_vm_map_trap`, and the second file's
  anchor is the whole `#if … /* port/vm_map lab adapters: ARMPBA8/LEO only */` comment line);
  **REFUSES, file untouched**, on a partial/double/foreign edit; and **proves switch-OFF byte-identity
  with real `clang -E -P` runs** before writing (the `patch_d13_undef_raise.py` discipline). It must also
  refuse if the **set of switches either file evaluates** grows beyond what it can see — the
  `patch_d13_undef_raise.py` guard, because `mach_kernelrpc.c` is edited by `patch_d13_ios7_vmwire.py`
  (the 994 rung) and `syscall_sw.c` is edited by nobody yet: **the two patchers' anchor sets must be
  disjoint, and the new one must say so.**
* `tools/stage_d13_machvm_trap15.sh` — the single entry point; calls the patcher, edits nothing itself.
* `tools/check_d13_machvm_trap15.py` — §5's by-value clause as a tool, with `--expect on|off` and a
  `--control` mode against a frozen pre-rung image.

---

## 3. THE CARRIERS — EVERY SITE THE SWITCH MUST REACH

`[[mi4-one-value-two-definitions]]`, template `docs/905-design.md` §7. This switch is a **POOL** switch
whose effect is in **two** pool files, and `build_entry.sh` only **LINKS**
`out/xnu_kernel_obj_d13/{osfmk_kern_syscall_sw,osfmk_ipc_mach_kernelrpc}.o`. **It is therefore a
two-carrier switch exactly like the `S-B` column of §2.2 and like `STAGE90_XNU_HFS_ROOT_RW`
(905 §7.3): the environment variable AND the `-D` are both required.**

### 3.1 The pool carrier (what actually compiles the row)

1. **`tools/build_xnu_arm_kernel.sh` — the `DEFINES` array, at the tail beside `:872`.** The exact line,
   `[[ ${STAGE90_XNU_UNDEF_RAISE:-0} -eq 1 ]] && DEFINES+=(-DSTAGE90_XNU_UNDEF_RAISE=1)` at `:872`, gains
   a sibling:
   `[[ ${STAGE90_XNU_MACHVM_TRAP15:-0} -eq 1 ]] && DEFINES+=(-DSTAGE90_XNU_MACHVM_TRAP15=1)`
   ⚠ **Both files ARE in the pool compile list — measured, not assumed:** the objects exist and are
   fresh from the 1020b build (`out/xnu_kernel_obj_d13/osfmk_kern_syscall_sw.o`, 9148 B, 2026-10-16
   00:59; `out/xnu_kernel_obj_d13/osfmk_ipc_mach_kernelrpc.o`, 4348 B, 00:58), and the latter's `nm`
   already carries the 994 rung's `_kernelrpc_mach_vm_allocate/deallocate/protect_trap` bodies — so the
   994 `-D` reached it through this same array. **`syscall_sw.c`'s presence is what the rung's FIRST
   build must confirm by object** (`§3.5`), because `:836`'s comment ("`syscall_sw.c` needs NO edit")
   is a statement about 994 and not about the compile list.
2. **`XNU_KERNEL_EXTRA_DEFINES` (== the build script's `EXTRA`)** — the second pool channel, read at
   `tools/build_xnu_arm_kernel.sh:1257-1260`. The new build script must carry
   `EXTRA="$EXTRA -DSTAGE90_XNU_MACHVM_TRAP15=1"` and assert both carriers live at the site that
   establishes them (the `build_1020b.sh:193` pattern):

   ```bash
   [[ -n ${STAGE90_XNU_MACHVM_TRAP15:-} ]] || { echo "FAIL: …not exported…"; exit 1; }
   [[ "$EXTRA" == *"-DSTAGE90_XNU_MACHVM_TRAP15=1"* ]] || { echo "FAIL: …not in EXTRA…"; exit 1; }
   ```

   **Both are required** and neither alone is sufficient: §2.2's `#if` is in a pool `.c` file, so the
   `EXTRA` token reaches it; the `DEFINES` line reaches it too, and the pair is what the other six
   rungs use. **A build that sets only one is a rung whose row is silently `kern_invalid` again.**

### 3.2 The entry-side record carrier (what the press gate reads)

3. **Resolve + validate.** `src/entry/build_entry.sh`, the block at `:1636-1652` (the `UNDEF_RAISE`
   pattern): `MACHVM_TRAP15=${STAGE90_XNU_MACHVM_TRAP15:-0}` at `:1648`-style, then
   `case "$MACHVM_TRAP15" in 0|1) ;; *) echo "REFUSING: STAGE90_XNU_MACHVM_TRAP15='$MACHVM_TRAP15' is
   neither 0 nor 1" >&2; exit 1 ;; esac` — **the 0/1 refusal is mandatory**
   ([[mi4-off-option-two-spellings]]: an unvalidated `2` is not an error, it is a third value).
4. **`ENTRY_ARM_KEYS`**, `src/entry/build_entry.sh:2054-2108` — append `STAGE90_XNU_MACHVM_TRAP15` at the
   tail (after `STAGE90_XNU_UNDEF_RAISE` at `:2107`).
5. **The resolver loop's `case`**, `build_entry.sh:2289/2296` region — add
   `STAGE90_XNU_MACHVM_TRAP15) _v=$MACHVM_TRAP15 ;;` reading the **validated** variable, never
   `${STAGE90_XNU_MACHVM_TRAP15}`.
6. **The record echo** (the RESOLVED value), `build_entry.sh:40251` region — add
   `echo "STAGE90_XNU_MACHVM_TRAP15=$MACHVM_TRAP15"` beside the `UNDEF_RAISE` line at `:40251`. A key in
   the list with no echo is an arm nobody read (`:40230`'s own note).
7. **`scripts/preflight_boot_check.sh`'s `ENTRY_CFG_KEYS`**, `:716-762` — append
   `STAGE90_XNU_MACHVM_TRAP15` at the tail (after `STAGE90_XNU_UNDEF_RAISE` at **`:762`**). This is
   mandatory in **both** directions: the loop at `:890` refuses a record missing a listed key, and the
   converse at `:1209` refuses a recorded key the list does not name (**a switch recorded and not shown
   is a switch a run goes out with unread**). No count moves — 1005 made that check a sorted set
   difference (`:1209`), so both sides grow by exactly one name.

### 3.3 The asm layer — **none, and the omission is stated**

Unlike `STAGE90_XNU_IOS7_TRAPS` (which also rides `tools/assemble_arm_layer.sh`'s `ASFLAGS` through
`traps_lo.s:116/182/284`), this switch touches **no `.s` file**: `traps_lo.s`'s `swi_mach` already loads
the row's function pointer generically (`:203 LOAD_ADDR(r1, mach_trap_table)` / `:207 ldr r1,[r1,#4]`),
so installing a row is a **data** change. §3.3 exists to say so out loud, because the 994 rung's
checklist has an asm half and a reviewer will look for it (905 §7.1 site 10).

### 3.4 The BLAST RADIUS — the census that justifies not widening the board gate

Kept in this document because §0(i) was rejected on it and a future rung will want the number.
**Measured over the staged tree** (`IOS7_COMPAT_PLATFORM` occurrences **excluding** `ios7_compat.h`'s own
definition line):

```
bsd/kern/kern_descrip.c 1 | bsd/kern/kern_exec.c 1 | bsd/kern/kern_fork.c 2
bsd/dev/arm/unix_syscalls.c 5 | bsd/vm/vm_unix.c 3 | osfmk/ipc/mach_kernelrpc.c 2
osfmk/kern/syscall_sw.c 5 | osfmk/kern/ipc_kobject.c 4 | osfmk/vm/vm_map.c 3
osfmk/vm/vm_shared_region.c 1 | osfmk/vm/vm_user.c 3 | osfmk/arm/status.c 1
osfmk/arm/ios7_compat.h 1 (the definition itself)
=> 32 occurrences in 13 files
```

**⚠ And this census is itself a `[[mi4-one-value-two-definitions]]` instance worth naming:** the
staged tree's `mach_kernelrpc.c` has **2** occurrences — the file-wide gate at `:39` and the
`#else`-less `#if` at `:59` — while `check_d13_ios7_abi_staged.sh:65` still records **1**. That tool
was written for the **pre-994** tree (before `stage_d13_ios7_traps.sh` added `:39`), so its `EXPECT`
table and its total (`:74-86`) **already fail on the current tree** and it would have to be updated
before it could gate anything. That is not this rung's defect to fix, but it is why §3.4 says the
census is **evidence, not a gate** — and it is a second reason the tool must not be the thing a
widened-gate rung is judged by.

`out/stage90/ios7_team/check_d13_ios7_abi_staged.sh` already carries this census as `EXPECT` (`:59-73`,
13 files) and **refuses** a build whose `-D` set names `ARMPBA8`/`QSD8250_LEO`/`IOS7_COMPAT_PLATFORM`
(`:49-56`). **It is NOT wired into `make check`** (grep of the Makefile for `ios7_abi_staged`/`vmwire`:
**0 hits**), so it cannot save this rung — it is evidence, not a gate. **A follow-up should wire it**, or
at minimum the new build script must run it, because it is precisely the check that a widened gate would
have to falsify.

### 3.5 The linked-image / `nm` clause, and the object assertion

Two by-value assertions at the build site, because `build_entry.sh` only LINKS the two pool objects:

* **The objects exist and are fresh.** `out/xnu_kernel_obj_d13/osfmk_kern_syscall_sw.o` **and**
  `out/xnu_kernel_obj_d13/osfmk_ipc_mach_kernelrpc.o` must both exist and their mtimes must postdate the
  pool build (the 1020b step-5 pattern). **`osfmk_kernelrpc.o` is the one that will bite:** the 994 rung
  already compiles it, and its `nm` today shows `_kernelrpc_mach_vm_allocate/deallocate/protect_trap`
  and **no** `ios7lab_*` symbol — after this rung it must show `ios7lab_mach_vm_map_trap` as a defined
  `T`. **`osfmk_kern_syscall_sw.o` must show a relocation of the table's entry-15 fn slot.** Today the
  object's `.rodata` reloc at offset `0xB8` (= `15*12+4`) is `kern_invalid`
  (`records/1020-trap-dispatch.txt` §4(a)); after this rung it must be `ios7lab_mach_vm_map_trap`.
  **This is the rung's object-side fingerprint and it is exact** — unlike 1020's moved `return`, this
  edit **introduces a new symbol reference**, so there IS a census to read:

  ```bash
  arm-none-eabi-nm out/xnu_kernel_obj_d13/osfmk_ipc_mach_kernelrpc.o | grep -E ' T ios7lab_mach_vm_map_trap$'
  arm-none-eabi-objdump -r out/xnu_kernel_obj_d13/osfmk_kern_syscall_sw.o \
    | awk '$1>="000000b8" && $1<="000000bb"' | grep -c 'ios7lab_mach_vm_map_trap'   # want 1
  ```
  (⚠ **the awk range is the reviewer's to confirm at build time** — the offset `0xB8` is quoted from
  1020's read of the **pre-rung** object; assert it **by resolving the table's own relocation at
  `15*12+4` relative to the `.rodata` section base**, never by trusting `0xB8` as a constant in a
  second script. That is the [[mi4-measurement-defects]] shape.)

* **`preflight_boot_check.sh`** (§3.2 site 7) needs nothing new — it reads the record, and the record
  now carries the key.

---

## 4. PRE-REGISTERED VERDICT CELLS

The ONE question:

> **Does pid 1's `_mach_vm_map` fast-trap `svc` (`mvn ip,#14` ⇒ index 15) reach
> `ios7lab_mach_vm_map_trap` — i.e. does the image's `mach_trap_table[15]` get USED — and if so, does
> the adapter's own view of the frame match the frame libsystem actually sent?**

`T-CTL` is **mandatory and comes first** (the 1018 lesson, `[[mi4-998-g1-confirmed-map-file-kr0]]`: a
record whose absence is evidence must be cap-exempt — and this rung's key is the `usvc_num` recorder,
which rides `entry_note_user_svc`, gated by `STAGE90_XNU_USVC`, whose channel is **counted**, i.e.
`entry_live_write`). **If the arm carries `USVC` without `UNDEF_DIRECT`'s direct-channel sibling for
this key, `T-CTL` withholds and nothing below is a reading.**

| # | condition (read from the capture) | verdict | what it means |
|---|---|---|---|
| **T-CTL** | no boot self-test row (`stage90_xnu_arm_vm_init_high_va_undef_handler_undef_handled=0x1`) **AND** no `xnu_live_usvc_*` traffic anywhere in the capture | **INSTRUMENT_UNSOUND — WITHHOLD** | the arm's witness was not live (or was capped out); the absence of `-15` says nothing. 1018's defect, verbatim |
| **T15-FIRES** | `xnu_live_usvc_num=0xfffffff1` (**-15**) appears ≥1×, and the boot **does not panic** (`swi_mach_error` is unreachable), and the `IOS7LAB vm_map64 …result=0` printf fires | **ROW INSTALLED AND USED; THE WIRE IS GOOD** | `mach_vm_map` came in as a fast trap, the adapter validated the frame, and `mach_vm_map()` returned `KERN_SUCCESS`. The allocator's mapping is **not** refused — the frontier is downstream |
| **T15-REACH-REFUSED** | `-15` ≥1×, no panic, but the `IOS7LAB vm_map64` line shows `result=4` (`KERN_INVALID_ARGUMENT`) **at least once** | **ROW USED; THE WIRE IS REFUSED** | the adapter's own validation (`regs[3]‖regs[5]‖address>>32`) rejects the frame — the iOS-7 wire is malformed on this port. This is 1019 §1's **C-RET-INVAL**, read from the kernel side instead of by `--wrap` |
| **`result=3` `MACH_SEND_INVALID_DEST`** | the `IOS7LAB vm_map64` line shows `result=3` | **NOT A WIRE READING — READ `T15-INERT`** | `result=3` is the adapter's **`MACH_SEND_INVALID_DEST`**, which it returns only from the `ios7_compat_vmabi_enabled()` early-out (`:217`) or a `port_name_to_task`/`task != current_task()` mismatch. It says nothing about the wire and everything about S-A. Keeping it out of the refused cell is the point of the split |
| **T15-SILENT** | zero `-15` in `usvc_num` **and** `T-CTL` cleared **and** the run reached a frontier at or past the allocator | **THE SVCS OF TRAP 15 ARE NOT ISSUED** | libsystem's fast-trap guard failed and the call fell to `bl 0x381c7f74` (MIG) and/or `blx r6` (a runtime hook). This is `records/1020-trap-dispatch.txt` §3/§6's reading **(a)**. ⚠ **the row is UNEXERCISED here, not "correct"** — an unexercised row is not evidence of anything, and 1019's `--wrap=mach_vm_map` is then the only remaining witness |
| **T15-CHECKSUM-MISS** | `-15` ≥1×, the boot advanced past the allocator, but a `IOS7LAB vm_map64` line shows `result=0` **with an `address=` that is not a plausible mapping** (e.g. non-zero `prot` echoed, or the `size`/`mask` hi halves non-zero on a 32-bit caller) | **ROW USED; THE FRAME IS NOT THE ONE §1 DERIVES** | the stub's register image does not reach `regs[8]` as `cur_protection` on this path (the fall-through at `0x381c66ba` writes `r5`, which the stub maps to r8 — §1's ⚠). **This cell is where the design must be revised, and it is the reason the row publishes the FULL frame, not just the result** |
| **T15-PANICS** | a panic whose string is `swi_mach_error` / `trap 15`-shaped | **STALE POOL — NOT A BEHAVIOUR READING** | the `-D` did not reach one of the two pool objects (§3.1), or a **stale** object was linked. Read `nm` on the objects (§3.5), not the env |
| **T15-INERT** | `-15` ≥1×, **no** `IOS7LAB vm_map64` line at all, and the boot failed *earlier or differently* than 1020b's (the shim's MIG fallback skipped on a non-sentinel return, §0's ⚠) | **REGRESSION — THE ROW IS INSTALLED BUT THE SELECTOR IS STILL 0** | `TRAP15=1` landed and **S-A did not** (§2.2). This is the one cell that is a *defect of this rung*, and it is why S-A is required rather than optional. Do not report it as a frontier |

**Falsification, stated as a predicate.** This design is falsified if **`T15-SILENT`** holds **and** the
run also reached the allocator with no `udf` loop: then `mach_trap_table[15]` is **not on pid 1's path
under any value**, the row's installation is cosmetic, and the next rung must be 1019's `--wrap=mach_vm_map`
on the kernel MIG demux (`docs/1019-design.md` §2) — *not* a further trap-table rung. The design is
**also** falsified as *stated* (though not as a mechanism) if `T15-CHECKSUM-MISS` holds, because §1's
frame derivation would then be wrong; the repair is a re-derivation from the capture's own numbers.

---

## 5. THE BY-VALUE BUILD CLAUSE (both directions)

At the link site, in the `build_entry.sh:31249-31330` family (the 1018/1020 clause cluster), the rung
adds one clause whose read is **the table entry's fn word at `mach_trap_table + 15*12 + 4`, resolved
from the table's own `nm` address — never a hard-coded VA.**

**The read (verified by value on the CURRENT image, so the reviewer sees the OFF shape):**

```bash
# the table's base, resolved — NOT 0x8055d4b4 as a constant
$ arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="mach_trap_table"'
8055d4b4 T mach_trap_table
$ arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="kern_invalid"'
80486408 T kern_invalid
```

and, by value, **today** (`.text` base `0x80000000`, fileoff `0x010000`; the ELF32 program headers give
the VA→fileoff map — `p_vaddr 0x80000000 → p_offset 0x10000`, `p_vaddr 0x80570000 → p_offset 0x580000`):

```
mach_trap_table va 0x8055d4b4
entry  0 argc=0 fn=0x80486408 name='kern_invalid'
entry 10 argc=5 fn=0x8047aa34 name='_kernelrpc_mach_vm_allocate_trap'
entry 12 argc=5 fn=0x8047ab5c name='_kernelrpc_mach_vm_deallocate_trap'
entry 14 argc=7 fn=0x8047ac10 name='_kernelrpc_mach_vm_protect_trap'
entry 15 argc=0 fn=0x80486408 name='kern_invalid'      <-- OFF
entry 16 argc=3 fn=0x8047acdc name='_kernelrpc_mach_port_allocate_trap'
FN SLOT of entry 15: va 0x8055d56c  fileoff 0x56d56c       (= 0x8055d4b4 + 15*12 + 4)
```

The clause (both directions, exact):

* **`STAGE90_XNU_MACHVM_TRAP15=1`** → **REFUSE** unless
  `entry15.fn != entry0.fn` **and** `entry15.fn ==` the linked address of the defined symbol
  `ios7lab_mach_vm_map_trap`, **and** `entry15.name` is still `"kern_invalid"` (§2.1 leaves the name
  table alone — asserting the name here is how the clause proves it is reading **entry 15's fn word**
  and not a differently-indexed row), **and** `entry0.name == "kern_invalid"` (the key the deferral
  check uses), **and** `entry 16`'s `{argc 3, fn _kernelrpc_mach_port_allocate_trap}` is **unchanged**
  → this last conjunct is the **index-stability** assertion of §5.1.
  Refusal text must name the pool: *"the row's `-D` did not reach `out/xnu_kernel_obj_d13/osfmk_kern_syscall_sw.o`,
  or a stale object was linked — `build_entry.sh` only LINKS it"*.
* **`STAGE90_XNU_MACHVM_TRAP15=0`** → **REFUSE** unless `entry15.fn == entry0.fn` **and**
  `entry15.name == "kern_invalid"`: the OFF image must be the pre-rung one **byte for byte at that
  word** ([[mi4-off-option-two-spellings]] — OFF is a shape, and the shape is the tree's bytes).

**Reuse, not reinvention, for the neighbours.** `tools/check_d13_ios7_vmwire.py` already reads this
table by value, keys the deferral on **entry 0's own `{fn, name}`** rather than on a symbol name
(`:380-403`, `DEFERRED_TRAPS = (15, 24, 25, 41, 42)` at `:85`), and is run by the ladder's andons
(`scripts/build_1015.sh:1133`). **Its rule 6 will FAIL after this rung, by design.** The rung must
therefore (a) run it, (b) treat its single rule-6 failure as expected and assert exactly that, and
(c) not weaken the tool for everyone — the cleanest is a `--trap15 on|off` flag beside the existing
`--off` (`:334-337`), so the 994 rung's own reading of its own image stays frozen. **A rung that
silently deletes `15` from `DEFERRED_TRAPS` has deleted the check that the other four rows are still
closed.**

### 5.1 Is the table index-stable? — YES, and here is why (the brief's risk question)

**Installing this row cannot change any other trap's index**, for four reasons, three of them structural
and one measured:

1. **`mach_trap_t` is 3 words on `__arm__`** — `syscall_sw.h:72-77` (`int mach_trap_arg_count;
   kern_return_t (*mach_trap_function)(void *); const char *mach_trap_name;`) and every `MACH_TRAP`
   expansion on `__arm__` (`syscall_sw.h:91-97`) is `{ (arg_count), (fn), #name }` — **exactly 3
   initializers, in both `MACH_ASSERT` arms**. There is no arch/assert variant that emits a different
   word count on this target.
2. **The edit replaces an initializer, never a row.** No `MACH_TRAP` line is added, removed, or
   reordered; the array still has `MACH_TRAP_TABLE_COUNT = 128` (`syscall_sw.h:56`) elements and
   `mach_trap_count` (`syscall_sw.c:402`) is unchanged.
3. **`swi_mach` addresses entries by arithmetic, not by a section symbol** — `traps_lo.s:205-208`
   (`add r11, r5, r5, lsl#1` / `add r1, r1, r11, lsl#2` / `ldr r1, [r1, #4]`), i.e. `table + idx*12 + 4`.
   A change *inside* a row cannot move a *different* row's offset.
4. **Measured, in the current image:** idx 10/12/14/16 resolve, by value, to
   `_kernelrpc_mach_vm_allocate/deallocate/protect/port_allocate_trap` with argc `5/5/7/3` (§5's read) —
   the same numbers `check_d13_ios7_vmwire.py`'s `TRAPS` table (`:61-75`) and `records/1020-trap-dispatch.txt`
   §4(a) report from three different runs. **The clause's `entry 16` conjunct freezes that.**

**The one residual risk, named honestly:** the relink of two pool objects could in principle move
**`.rodata`'s layout**, hence the table's own VA and every name pointer inside it. That is why the
clause resolves `mach_trap_table` from `nm` **per image** and reads `entry0`'s `{fn, name}` as its
reference — a clause that hard-coded `0x8055d4b4` would report a **relocation** as a **defect**
([[mi4-measurement-defects]]), and on the second build of the same rung it would.

---

## 6. THE ARM, THE ORDER, AND WHAT IS OWED

* **The switch set:** `1020b`'s switches **VERBATIM, read out of `out/stage90/xnu_arm_entry-config.txt`**
  (the `build_1020b.sh:66-113` pattern, `[[mi4-build-variant-comes-from-an-env-default]]`) **plus**
  `EXTRA="$EXTRA -DSTAGE90_XNU_GLOBAL_FIX=1"` **plus** `STAGE90_XNU_MACHVM_TRAP15=1` exported and
  `-D`'d. **`GLOBAL_FIX` has no record carrier** (`build_1020b.sh:117-128`,
  `records/1020-build-defect.txt`) — a build that derives `EXTRA` from the record alone re-drops 1015b
  and the storm returns, and the run reads `T-CTL`.
* **The arm-extra file** must be written by the **build** (the `build_1020b.sh:287` pattern), not
  hand-written, carrying `STAGE90_XNU_GLOBAL_FIX=1 role=1021b-globalfix` **and** (if S-A lands) the S-A
  role token. A stale 1020b line would be a declaration of a different build
  ([[mi4-self-written-record-is-not-a-constraint]]).
* **The order:** all stage drivers in `build_1020b.sh`'s order, then `stage_d13_machvm_trap15.sh`
  **LAST** (it touches `syscall_sw.c`, which no other driver touches, and `mach_kernelrpc.c`, which
  `stage_d13_ios7_traps.sh` touches — disjoint anchors, but the tested order is the sequential one).
* **The pool rebuild, the payload rebuild, the checkers** — `build_1020b.sh`'s steps 4-7 verbatim, with
  the new §3.5 object assertions inserted at step 5 and the §5 clause replacing nothing (it is additive).

**What this rung OWES and does not contain (each is a real gap, not a rounding):**

1. **S-A (§2.2)** — the switch that makes the row's body *run* rather than return
   `MACH_SEND_INVALID_DEST`. Without it the row is **actively worse than no row** (§0's ⚠: the shim's
   sentinel test treats a non-sentinel return as "the fast trap ran" and **skips the MIG fallback**),
   which is cell `T15-INERT`. **`TRAP15=1` without S-A is a build-and-check rung only: it may be built
   and read against the linked image (§5, proving the `-D` reached the pool and the row is in the
   image) and it must NOT be pressed.** S-A is the gating half of the arm, and if it cannot land, this
   rung is not ready.
2. **The `USVC` channel question** — this rung's witness is `xnu_live_usvc_num`, whose recorder
   (`entry_note_user_svc`, `traps_lo.s:135-146`, six-word frame) rides `STAGE90_XNU_USVC`. **The 1018
   lesson applies:** if that key can be dropped by `xnu_live_capped` before the frontier
   (`records/1018-recon.txt` §0-§2, capture `:29963`), `T-CTL` will withhold on a run that *did* see
   `-15`. **Check the channel before the press, not after** — the honest move is to make this key
   cap-exempt the way `STAGE90_XNU_UNDEF_DIRECT` made `xnu_live_undef_*` cap-exempt.
3. **The guard measurement** (`records/1020-trap-dispatch.txt` §3, `docs/1021-design.md` §1): the shim's
   three fields at `0x381c663c`/`:640`/`:646`. `T15-SILENT` **implies** the guard failed, but does not
   *read* it. A sibling witness at the shim is the only thing that decides (a) vs (b).
4. **The 1019 `--wrap=mach_vm_map` witness** remains the correct instrument for the kernel-side return
   and is **orthogonal** to this rung — they may ride the same arm, and if they do, the two readings
   (kernel-side `mach_vm_map()` result vs the *shim's* result) are the pair that pins the wire.

---

## 7. WHAT THIS DOES AND DOES NOT CLAIM

* **CONFIRMS (by value, this document):** the row exists at `syscall_sw.c:126-130`; it is `kern_invalid`
  in the linked image; the D13 build closes the gate at `ios7_compat.h:5`; the trap-15 frame is the
  9-word register image the existing `ios7lab_mach_vm_map_trap` adapter already reads (stub `0x381c5960`
  ⇒ `regs[8] = cur_protection`, word 7 dead); installing the row is **index-stable** (3-word entries,
  initializer-only edit, arithmetic dispatch, measured 10/12/14/16); and widening `IOS7_COMPAT_PLATFORM`
  would flip **30 guarded occurrences in 12 files**.
* **does NOT claim:** that pid 1's `mach_vm_map` uses this trap (the fast-trap guard is unmeasured — that
  is `T15-SILENT`'s whole purpose); that `ios7_compat_vmabi_enabled()` can be made to answer 1 on CANCRO
  without a second switch (S-A is **designed here, owed**); that installing the row boots iOS 7; or that
  `mach_trap_table[15]` is the frontier. **The frontier is whatever the 1020b capture names**, and this
  rung exists because that capture put trap 15 on the table.
* **SAFETY:** one table initializer + one `#if` in a pool `.c` file. Blast radius: trap 15 only (all
  other rows byte-identical, §5.1). The device is booted with `fastboot boot` (never flashed), so
  flash-resident Android and `fastboot` stay reachable — [[mi4-hardware-run-safety-gate]]. **No device
  action of any kind was taken while authoring this file.**

---

## 8. FILE:LINE ANCHORS

**The row and its gate.**
`external/xnu-hd2-darwin13/xnu/osfmk/kern/syscall_sw.c:102` (the file-wide `#if IOS7_COMPAT_PLATFORM`,
the `extern`s `:103-107`), `:126-130` (**the row**), `:139-148` (rows 24/25, same gate), `:164-170`
(rows 41/42), `:282` (`mach_syscall_name_table[15] = "kern_invalid"` — **not edited**),
`:402` (`mach_trap_count`).
`external/xnu-hd2-darwin13/xnu/osfmk/arm/ios7_compat.h:5` (`IOS7_COMPAT_PLATFORM`), `:11-21`
(`ios7_compat_vmabi_enabled()`, the S-A site), `:24-40` (`ios7_traps_enabled`),
`:42-53` (`ios7_shreg_geom_enabled`).
`external/xnu-hd2-darwin13/xnu/osfmk/kern/syscall_sw.h:56` (`MACH_TRAP_TABLE_COUNT`),
`:72-77` (`mach_trap_t`, 3 words), `:91-97` (the `__arm__` `MACH_TRAP` expansion).

**The body (reused, not rewritten).**
`external/xnu-hd2-darwin13/xnu/osfmk/ipc/mach_kernelrpc.c:39` (the file-wide gate, opened by 994),
**`:59`** (the block this rung opens) … `:244` (`#endif /* port/vm_map lab adapters */`),
`:208-243` (`ios7lab_mach_vm_map_trap` — the body this rung installs), `:217` (the
`ios7_compat_vmabi_enabled()` early-out = S-A's target), `:103-107` (the `extern`s),
`:487-495` / `:519-525` / `:543-549` (rows 10/12/14's routing, the pattern a trap-15 router would follow
if the adapter is ever inlined instead of called).

**The dispatcher (unchanged, read for the index arithmetic).**
`external/xnu-hd2-darwin13/xnu/osfmk/arm/traps_lo.s:113` (`EnterARM(fleh_swi)`), `:116` (the `-3`
special case, switch-opened), `:122` (the frame), `:126` (`mov r8, sp`), `:133` (`mov r11, r12`),
`:186-187`, `:194-195`, `:197-219` (`swi_mach`: `:203` `LOAD_ADDR(r1, mach_trap_table)`, `:207`
`ldr r1,[r1,#4]`, `:213` `beq swi_mach_error`, `:214` `bx r1`), `:221` (`blx _mach_syscall_trace`),
`:252-260` (`swi_mach_error` → `irqvec_panic`), `:677` (`LOAD_ADDR_GEN_DEF(mach_trap_table)`).

**The carriers (this rung's plumbing).**
`tools/build_xnu_arm_kernel.sh:202` (`-DBOARD_CONFIG_MSM8974_CANCRO=1`), `:831-845` (the 994 comment
that names `mach_kernelrpc.c` as a pool file and `syscall_sw.c` as unedited), **`:872`** (the
`DEFINES+=` site to sit beside), `:1257-1260` (`EXTRA_DEFINES` ← `XNU_KERNEL_EXTRA_DEFINES`).
`src/entry/build_entry.sh`: `:1648-1652` (resolve+validate pattern), `:2054-2108` (`ENTRY_ARM_KEYS`,
tail `:2107`), `:2289`/`:2296` (the resolver `case`), `:40251` (the record echo), `:31249-31330` (the
1018/1020 by-value clause cluster the new clause joins).
`scripts/preflight_boot_check.sh:716-762` (`ENTRY_CFG_KEYS`, tail `:762`), `:890` (the missing-key
refusal), `:1209` (the converse set-difference refusal).
Staging precedent: `tools/stage_d13_undef_raise.sh`, `tools/patch_d13_undef_raise.py`,
`tools/patch_d13_ios7_vmwire.py:66-132/296-319` (the anchor-set discipline), `records/1020-build-defects.txt`.

**The checkers.**
`tools/check_d13_ios7_vmwire.py:61-75` (`TRAPS`), `:85` (`DEFERRED_TRAPS = (15, 24, 25, 41, 42)`),
`:334-337` (the `--off` flag a `--trap15` flag would sit beside), `:380-403` (**rule 6, which this rung
makes fail by design**); callers `scripts/build_1015.sh:1133`, `scripts/build_1005.sh:383`.
`out/stage90/ios7_team/check_d13_ios7_abi_staged.sh:39-56` (the gate census + the `-D` refusal),
`:59-73` (`EXPECT`, 13 files), `:88-99` (the ABI-crossing table). **Not wired into `make check`**
(measured: `grep -n 'ios7_abi_staged\|ios7_vmwire' Makefile` → 0 hits).

**The evidence.**
`records/1020-trap-dispatch.txt` §1 (the dispatch arithmetic), §3 (the shim + `blx 0x381c5960`), §4(a)
(the linked table read), §6 (the verdict); `records/1019-rootcause.txt` §6 (the open
reconciliation this rung closes a side of); `docs/1021-design.md` §1 (the branch that named this
question); `docs/1020-design.md`; `docs/905-design.md` §7 (the carrier-survey template).

**The cache reads (all of §1, reproducible).**
`/tmp/dyld-extract/dyld_shared_cache_armv7`; `0x381c5940..0x381c59a0` (the fast-trap stubs, incl.
`0x381c5960`'s `ldm ip,{r4,r5,r6,r8}` and `mvn ip,#14`), `0x381c57e4` (`mvn ip,#2` = the `-3` timebase
stub, 1020 §4(b)), `0x381c6618..0x381c66ec` (the `_mach_vm_map` shim), `0x381c66a8-0x381c66e6` (the
fall-through, `bl 0x381c7f74` then `mov r4,r0` — the `r5`-rides-into-`r8` observation behind
`T15-CHECKSUM-MISS`).

**No build, no press, no device action, no `git add`, and no write to `out/stage90` was performed while
authoring this file.** The only file written is this one.

---

## 9. DEFERRED — TRAP 15 IS NOT THE WALL (`records/1021-guard-leg.txt`)

### 9(a) The premise is falsified: pid 1's `_mach_vm_map` takes the SLOW leg, so no `-15` svc is issued

**This rung is DEFERRED. It is not withdrawn — it is parked behind a measurement.** `docs/1021-design.md`
§1 and `records/1020-trap-dispatch.txt` named `mach_trap_table[15] = kern_invalid` as a wall *conditional
on* a `-15` svc being issued; the sibling read `records/1021-guard-leg.txt` shows that condition does not
hold on the run the 1018/1020 captures recorded. The `_mach_vm_map` shim (`0x381c6618`, Thumb) has a
three-leg fast-trap guard — leg 1 `[caller_sp+20] == 0`, leg 2 `[caller_sp+40] == 7`, leg 3
`[caller_sp+44] == 1` (`:37-42`) — and only on all three does it reach `blx 0x381c5960`, the stub that
carries `mvn ip,#14` (`ip = 0xFFFFFFF1 = -15`) then `svc #0x80` (`:88-97`). Its caller `__os_alloc_slow`
(`0x3823a94c`) **hard-codes the size field**: `mov.w r2, #4096` (`0x1000`) at `0x3823a98e`, stored into
slot 11 = the guard-leg-2 word (`:69`, `:78`). The only value leg 2 accepts is **7**, a size no page
allocation ever asks for, so the guard **cannot pass on a page-size `mach_vm_map`** and the shim branches
to the slow leg — the MIG demux `bl 0x381c7f74` (`:48`, `:81`) and/or the indirect runtime hook
`blx r6` at GOT slot `0x39f38096` (`:49-50`) — **leg (b), not leg (a)**. Corroborated live: across the
1018 and 1020 captures, `fffffff1` occurs **0 times** and `panic|doexception|EXC_BAD` **0 hits**
(`:122-124`) — consistent with a `-15` svc never having fired, since a `-15` svc against `kern_invalid`
would have routed to `swi_mach_error` and panicked. `records/1021-fpu-rootcause.txt` independently moved
the D13 frontier to a **kernel-mode NEON undef** in `mach_msg_send` (`vld1.32 {d16-d17},[r0]!` at
`0x8047b0ac`, `spsr=0x40000093`), which is where the live work now is.

**Consequence for this document.** §1-§5 stay exactly as written — they are the deferred-design reference
and nothing in them is retracted. The trap-15 wire (§1), the two-file edit (§2), the carrier census (§3),
the verdict cells (§4) and the by-value build clause (§5) are all still the minimal rung that installs the
row. What changes is **priority and the gate on reviving it**: this rung must NOT be built on the strength
of the 1020b capture alone. **Revive it only if a future capture shows a `-15`** — i.e. a recorded
`xnu_live_usvc_num == 0xfffffff1` (or a `swi_mach_error` / `doexception` attributable to entry 15) — or
if the §7-item-1 measurement of `records/1021-guard-leg.txt` (the guard's live `[r7,#20]/[r7,#40]/[r7,#44]`
words) comes back `0/7/1`, which would put pid 1 on leg (a) after all. Absent one of those, installing row
15 is **inert by construction**: the shim never issues the trap, so the row would be exercised by nothing
and its build would burn a press to learn nothing. (And §0's warning stands: an inert row is *worse* than
no row, because the shim's `0x10000003` sentinel test would let a non-sentinel return branch *away* from
the working MIG demux — which is why §2.2 makes S-A (**§2.2**) a **required** companion, not a
follow-up.)

### 9(b) Index stability — re-confirmed; the design stays correct if revived

**Yes: installing row 15 does not shift entries 0-14 or 16+.** Re-stated here rather than only in §5.1 so
the deferral does not lose it, and re-derived the same four ways (all unchanged by anything in §9(a)):

1. **The entry stride is fixed at 3 words** — `mach_trap_t` is
   `{int mach_trap_arg_count; kern_return_t (*mach_trap_function)(void *); const char *mach_trap_name;}`
   on `__arm__` (`syscall_sw.h:72-77`), and the `MACH_TRAP` macro emits exactly those 3 initializers on
   `__arm__` (`:91-97`). The edit at `syscall_sw.c:126-130` **replaces an initializer inside an existing
   `MACH_TRAP(...)` row** — it does not add, remove, or reorder a row — so `sizeof(mach_trap_t)` and the
   row count are both untouched.
2. **The dispatch is arithmetic, not symbolic** — `swi_mach` computes `add r11, r5, r5, lsl#1` /
   `add r1, r1, r11, lsl#2` / `ldr r1, [r1, #4]` (`traps_lo.s:205-208` staged: `:214-217`), i.e. `idx*12`
   and `+4`. Any change to the stride or the row count would move every index; neither is changed.
3. **Measured in the current linked image** — indices 10/12/14/16 resolve by value to their real symbols
   (`_kernelrpc_mach_vm_allocate_trap` etc., §5.1 item 4), so the table's indexing is intact *today*; the
   edit is local to index 15.
4. **The blast-radius census in §3.4 stands** — the switched initializer changes trap **15 only**; every
   other row's bytes are identical (§7's SAFETY line). Widening `IOS7_COMPAT_PLATFORM` instead (shape
   (i), rejected in §0) is the *only* variant that moves other rows, and it is not the chosen shape.

**So if the rung is revived, §2's edit installs the row at index 15 with 0-14 and 16+ unchanged, and §5's
by-value clause (`mach_trap_table[15].mach_trap_function != kern_invalid`, table base resolved at
`0x8055d4b4`, stride 12, fn at entry+4) remains the correct refusal for it.**

**The 5-line summary of where index 15 is emitted, for the record** (the "if you have NOT started the
file" fallback, kept here because it is the load-bearing fact):
1. The row lives at `external/xnu-hd2-darwin13/xnu/osfmk/kern/syscall_sw.c:126-130`, inside the
   file-wide `#if IOS7_COMPAT_PLATFORM` at `:102`.
2. It is `/* 15 */ MACH_TRAP(kern_invalid, 0, NULL, NULL)` for D13, because
   `IOS7_COMPAT_PLATFORM = (ARMPBA8 || QSD8250_LEO)` (`ios7_compat.h:5`) and D13 defines only
   `MSM8974_CANCRO` (`tools/build_xnu_arm_kernel.sh:202`) ⇒ the `ios7lab_mach_vm_map_trap` row is gated out.
3. The body that row needs already exists — `ios7lab_mach_vm_map_trap` at
   `osfmk/ipc/mach_kernelrpc.c:208-243` — but is itself gated shut at `mach_kernelrpc.c:59`.
4. The minimal install is a **two-file edit**: the `syscall_sw.c:126-130` row switched on a new
   `STAGE90_XNU_MACHVM_TRAP15`, plus `mach_kernelrpc.c:59`'s gate widened to
   `#if IOS7_COMPAT_PLATFORM || defined(STAGE90_XNU_MACHVM_TRAP15)` (§2.1/§2.2).
5. That switch must be carried on **both** channels (pool `-D` **and** `XNU_KERNEL_EXTRA_DEFINES`) plus
   the entry-side record/echo/preflight set — the 7 sites enumerated in §3 — or the pool rebuild silently
   drops it, exactly as `records/1020-build-defect.txt` shows `GLOBAL_FIX` was dropped.

### 9(c) What is owed for the revival gate

Named so the deferral is checkable rather than a mood: **one capture showing a `-15`**, i.e.
`xnu_live_usvc_num == 0xfffffff1`, or the guard's live words `[r7,#20]/[r7,#40]/[r7,#44] == 0/7/1`
(`records/1021-guard-leg.txt` §7 item 1). Until then the D13 frontier is `records/1021-fpu-rootcause.txt`'s
kernel-mode NEON undef, and this file is a parked design.