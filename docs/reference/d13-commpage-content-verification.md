# D13 commpage content verification — what the kernel writes, what dyld reads (993 follow-up)

Read-only recon; one file written. Sources read BY VALUE: our build tree
`external/xnu-hd2-darwin13/xnu` (paths below are relative to it unless prefixed), the reference
`external/xnu-4570.1.46`, the built object `out/xnu_asm_obj_d13/traps_lo.o` (objdump), the dyld image
extracted from the shared cache (`/tmp/dyld_ios7`, disassembled `/tmp/dyld.llvm`, symbols
`/tmp/dyld_syms.txt`), and the 993 capture `cancro-last_kmsg.txt` (= `/tmp/cancro-last_kmsg.txt`,
1084884 B, 2026-10-12 15:49; the file whose `IOS7LAB` markers 993 pre-registered).

**Headline.** (1) The D13 kernel writes 10 commpage fields and never writes `+0x90`; the alias maps
the SAME physical page, so `0xFFFF4090` reads 0 by construction. (2) The `-3`/`svc #0x80` fast path
IS compiled in on CANCRO (the task premise and one reading of the memo said "compiled out" — the
source gate is `#if !(...)`, and the built object proves it in). (3) The 993 stopper is **not** a
commpage read: `pc 0x2be7e0c4` = `_dyld_fatal_error`, `lr 0x2be7ef07` = `dyld::halt+0x4b`, and halt's
message register points at dyld's own literal **`"throwf, out of memory error"`**. dyld raised a
`const char*` exception from `throwf`'s allocation-failure branch and `_main`'s
`catch (const char* message) { halt(message); }` aborted the process. No commpage byte is read on
that path.

## 1. Every commpage writer in the D13 tree (exhaustive)

The whole page is zeroed first: `pmap.c:2847` `memset((void *)_COMM_PAGE_BASE_ADDRESS, 0x00, PAGE_SIZE)`
inside `pmap_create_sharedpage()` (called from `commpage_populate()`, `commpage.c:78`). Everything
below then writes into that page. A grep for `_COMM_PAGE_[A-Z0-9_]* *=` over `osfmk/` + `bsd/` returns
exactly the seven lines in `commpage.c`, plus the raw-offset writes in the same file, plus the
`commpage_set_timestamp` body; there is no other writer anywhere in the tree.

| # | file:line | offset (base 0x40000000 == 0xFFFF4000) | width | value |
|---|---|---|---|---|
| 1 | `commpage/commpage.c:90` | +0x080 `_COMM_PAGE_CPUFAMILY` | u32 | `CPUFAMILY_ARM_13` (CANCRO takes the `#else`) |
| 2 | `commpage/commpage.c:92` | +0x01E `_COMM_PAGE_MYSTERY_VALUE` | u16 | 3 |
| 3 | `commpage/commpage.c:93` | +0x020 `_COMM_PAGE_CPU_CAPABILITIES` | u32 | `kUP` = 0x8000 |
| 4 | `commpage/commpage.c:94` | +0x038 `_COMM_PAGE_MEMORY_SIZE` | u64 | `ml_cpu_cache_size(0)` |
| 5 | `commpage/commpage.c:61` (alias branch; :66 is the dead `#else`) | +0x022 `_COMM_PAGE_NCPUS` | u8 | `processor_avail_count` = 1 |
| 6 | `commpage/commpage.c:62` | +0x034 | u8 | 1 (ACTIVE_CPUS) |
| 7 | `commpage/commpage.c:63` | +0x035 | u8 | 1 (PHYSICAL_CPUS) |
| 8 | `commpage/commpage.c:64` | +0x036 | u8 | 1 (LOGICAL_CPUS) |
| 9 | `commpage/commpage.c:106` (CANCRO arm; :104 is LEO) | +0x026 | u16 | 64 (CACHE_LINESIZE) |
| 10 | `commpage/commpage.c:127-131` `commpage_set_timestamp()` | +0x040 `_COMM_PAGE_TIMEOFDAY_DATA` | | see below |

`commpage_set_timestamp` (reached only if `clock_gettimeofday()` runs: `kern/clock.c:349` →
`arm/rtclock.c:341-351`, `ticks_per_sec` argument is hard-coded 0 at the call site) writes, through
**this tree's own struct order** (`cpu_capabilities.h:66-74`):

- `+0x40` `TimeBase` (u64) = `abstime - remain`
- `+0x4C` `TimeStamp_sec` (u32) = `*secs`
- `+0x50` `TimeBaseTicks_per_sec` = **0** (literal; the `ticks_per_sec` parameter is ignored, `commpage.c:131`)

`TimeStamp_usec` (+0x48), `TimeBase_magic` (+0x58), `TimeBase_add` (+0x60), `TimeBase_shift` (+0x64)
are never written (they stay 0 from the memset).

> Defect note (the "one value, two definitions" class): the 4570 header orders this struct
> `TimeBase, TimeStamp_sec(+8), TimeStamp_usec(+12), Ticks_per_sec(+16), Ticks_per_usec(+20),
> magic(+24), add(+32), shift(+36)` (`xnu-4570.1.46/osfmk/arm/cpu_capabilities.h:83-92`), while this
> tree orders it `TimeBase, TimeStamp_usec(+8), TimeStamp_sec(+12), ...`. Both are "the commpage
> timeofday data", so the kernel's seconds land in the userspace field named `usec`. The
> `TimeBaseTicks_per_sec = 0` write is deliberate, not a bug.

Also written later by other paths, same two bytes: `kern/machine.c:133` and `:276` call
`commpage_update_active_cpus()` when processors come online (rewrites rows 5-8).

**Fields that are NEVER written (0 for the whole boot), against the real iOS 7 / 4570 layout**
(`xnu-4570.1.46/osfmk/arm/cpu_capabilities.h:171-209`): `+0x000` SIGNATURE, `+0x024`/`+0x025`
USER_PAGE_SHIFT_32/64, `+0x028` SCHED_GEN, `+0x02C` SPIN_COUNT, `+0x030` MEMORY_PRESSURE,
`+0x037` KERNEL_PAGE_SHIFT, `+0x084` DEV_FIRM, **`+0x088` TIMEBASE_OFFSET**, **`+0x090`
USER_TIMEBASE**, `+0x091` CONT_HWCLOCK, `+0x098` CONT_TIMEBASE, `+0x0A0` BOOTTIME_USEC,
`+0x0C0`/`+0x0C8` APPROX_TIME[_SUPPORTED], `+0x120` NEWTIMEOFDAY_DATA. For comparison, the reference
`xnu-4570.1.46/osfmk/arm/commpage/commpage.c:79-137,327` writes all of the timers: `VERSION`,
`CACHE_LINESIZE`, `SPIN_COUNT=1`, `PHYSICAL/LOGICAL_CPUS`, `MEMORY_SIZE`, `CPUFAMILY`, `DEV_FIRM`,
**`USER_TIMEBASE = user_timebase_allowed()`** (:105), `CONT_HWCLOCK`, `KERNEL_PAGE_SHIFT`,
`USER_PAGE_SHIFT_*`, `APPROX_TIME_SUPPORTED`, and `TIMEBASE_OFFSET = rtclock_base_abstime` (:327).

Answer to the direct question: **`_COMM_PAGE_USER_TIMEBASE` (+0x90) is written by nothing in this
tree — 0 grep hits** (the only textual match is a comment at `pmap.c:2850`). `_COMM_PAGE_TIMEBASE_*`
conversion factors: there is no such writer either; the legacy `TimeBaseTicks_per_sec` is written as
0. `_COMM_PAGE_TIMEOFDAY_DATA` (+0x40) is written only via `commpage_set_timestamp`, and only the
three fields above.

## 2. Offset 0x90's identity, and the `+8` relationship

`xnu-4570.1.46/osfmk/arm/cpu_capabilities.h:191-192` (base = `_COMM_PAGE32_BASE_ADDRESS` =
**0xFFFF4000**, :153):

```
#define _COMM_PAGE_TIMEBASE_OFFSET (_COMM_PAGE_START_ADDRESS+0x088)  // u64 timebase offset for constructing mach_absolute_time()
#define _COMM_PAGE_USER_TIMEBASE   (_COMM_PAGE_START_ADDRESS+0x090)  // u8  is userspace mach_absolute_time supported
```

So `+0x90` IS `_COMM_PAGE_USER_TIMEBASE`, and `+0x88` IS the u64 `_COMM_PAGE_TIMEBASE_OFFSET`,
which is why dyld's code derives 0xFFFF4090 from a base of 0xFFFF4088 with `[r12, #8]`. The D13 tree's
own header (`osfmk/arm/cpu_capabilities.h`) has **no macro at 0x90 at all** — its `_COMM_PAGE_*` list
stops at `_COMM_PAGE_CPUFAMILY 0x40000080` (:47) plus `MYSTERY_VALUE 0x4000001E` (:52); this is the
winocm-era reduced namespace, whose six names happen to land on the same offsets as six of the real
ones (0x1E = VERSION, 0x20, 0x22, 0x26 raw, 0x34-0x36 raw, 0x38, 0x40, 0x80).

Same-page proof (why 0xFFFF4090 mirrors 0x40000090): `pmap.c:2869-2872` enters
`commpage->phys_page` at `IOS7LAB_COMMPAGE_ALIAS` (defined `0xFFFF4000U`, `pmap.c:207`) with
`VM_PROT_READ`, and `pmap.c:1644` adds user + `L2_NX_BIT` for that VA; the same page was entered RW
at `_COMM_PAGE_BASE_ADDRESS` at `pmap.c:2842`. The 993 log confirms at runtime:
`IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN` (log:4020).

## 3. `mach_absolute_time`, both sides, by value

**Kernel.** `osfmk/arm/rtclock.c:188-192`:

```c
uint64_t mach_absolute_time(void) { if (!rtclock_sec_divisor) return 0; return rtclock_timer_scale_convert(); }
```

`rtclock_sec_divisor` is set by `rtc_configure()` (`rtclock.c:281-284`; the L2.1 fix), and
`rtclock_bus_configure` panics if it is still 0 (`:296`).

**Userspace — dyld's own copy, `/tmp/dyld.llvm` (`_mach_absolute_time`, unslid 0x2BE1B2C0):**

```
2be1b2c0  movw r12, #0x4088 ; movt r12, #0xffff     ; r12 = 0xFFFF4088
2be1b2c8  ldrb r0, [r12, #8]                        ; <-- _COMM_PAGE_USER_TIMEBASE (0xFFFF4090)
2be1b2cc  cmp  r0, #0
2be1b2d0  beq  0x2be1b320                           ; byte == 0 -> fallback
2be1b2e0  ldr  r4, [r12]      ; ldr r5, [r12,#4]    ; <-- _COMM_PAGE_TIMEBASE_OFFSET (u64)
2be1b2e8  mrrc p15,#0,r3,r1,c14  ... (re-read until stable, adds r0/r1 + offset)
2be1b320  mvn  r12, #2                              ; r12 = -3
2be1b324  svc  #0x80
2be1b328  bx   lr
```

I scanned all of dyld's `__TEXT` for the commpage addresses in both instruction (`movw #16520`) and
literal-pool (`88 40 ff ff`) forms: the `movw r12, #16520` at 0x2BE1B2C0 is the **only** reference to
any 0xFFFF4xxx address in the whole dyld image. So dyld reads exactly two commpage fields, both of
them ones this tree never writes: `+0x90` (the gate) and `+0x88` (only if the gate passes).

**Kernel SWI side (`osfmk/arm/traps_lo.s`), against the built object `out/xnu_asm_obj_d13/traps_lo.o`:**

- `:116-119` is guarded `#if !(BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO)`. CANCRO defines
  neither board macro (`tools/stage_d13_board.sh` adds `-DBOARD_CONFIG_MSM8974_CANCRO`), so
  `!(0||0)` = 1 → **compiled IN**. Objdump confirms: `fleh_swi` = `a0: e37c0003 cmn ip,#3` /
  `a4: 0a000033 beq 178 <swi_trap_tb>`.
- `:162-168` (`cmn r11,#3 ; beq ios7lab_timebase`) and `:264-268` (the `ios7lab_timebase:
  blx _mach_absolute_time; b swi_exit64` body) are `#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO`
  → `#if 0` on CANCRO → **compiled OUT** (no such symbol in the .o; the disassembly goes straight from
  `mov fp,ip` to the `cmp fp,#0x80000000` trap test).
- `swi_trap_tb` = `traps_lo.s:260-262` `movs pc, lr` — a bare exception return (objdump `178: e1b0f00e`).

So the actual 993 behaviour of `svc #0x80` with r12 = −3 is: **recognized, then returned through the
fast path that never writes r0/r1.** Userspace's read gets, verbatim: `r0` = 0 (the byte it just
loaded) and `r1` = whatever the caller left in r1 — dyld's `_mach_absolute_time` does not touch r1
before the `svc`. That is the memo §2 reading, confirmed; the task-premise wording ("guard compiled
out … not recognized") is the inverted form — the `#if !` makes the fast return the *included*
branch on a non-Apple board, and it is the *correct* timebase arm that is missing.

Contrast, by value: Apple's `xnu-4570.1.46/osfmk/arm/locore.s:800-804` jumps to `fleh_swi_trap_tb`
which does `bl EXT(ml_get_timebase)` **before** the same `movs pc, lr`, so the return registers are
the timebase. This tree cannot copy that: `ml_get_timebase` has no ARM definition here (declared
`osfmk/arm/machine_routines.h:80`; only i386/x86_64 implement it) — which is why the fork wrote
`ios7lab_timebase` calling `_mach_absolute_time` instead, and why that arm being board-gated is the
whole defect.

Fix shape (inference, one line each, no edit made): the −3 trap must take the `ios7lab_timebase` arm
and not the fast return. That means both `:162` and `:264` must be true for CANCRO **and**
`:116` must be false for CANCRO — the `#if !(ARMPBA8 || LEO)` form does exactly that pair for the two
existing boards, so the minimal change is to admit the CANCRO board macro to the same two
expressions (`!(A || B || C)` and `(A || B || C)`). Note the fast-return branch is tested first at
runtime, so opening `:162` alone changes nothing.

## 4. Reconciling with the 993 capture: the `udf` is dyld's own halt, not a commpage read

Measured in `/tmp/cancro-last_kmsg.txt`:

- Alias markers all present (log:3977/3978/4018/4019/4020/4021); `grep -c IOS7LAB` = 6.
- **`grep -c 0xffff4090` = 0** — 991's 231× translation fault is gone; the alias did its job.
- New event: `usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x2be7e0c4` (first at log:4101),
  **236** identical blocks (`grep -c 'usermode undefined instruction'` = 236, pc appears 471× = once
  per block + once in each register dump). Every block repeats r0-r12/sp/lr/pc/cpsr byte for byte —
  i.e. **one** abort, re-executed by the kernel's undefined-instruction delivery, not 236 events.
- Register block: `r5: 0x2be997fd  r7: 0x2fd829f8  sp: 0x2fd829f0  lr: 0x2be7ef07  pc: 0x2be7e0c4`,
  `cpsr: 0x80000010` (ARM state, user mode).

Slide = **0x7d000**, proven twice against dyld's own symbol table: `0x2be7e028 − 0x7d000 =
0x2BE01028 = __dyld_start` (the same address the log calls "the OS starts the process at"), and
`0x2be7e0c4 − 0x7d000 = 0x2BE010C4`. Unslid identity of every crash register:

| log value | unslid | dyld symbol |
|---|---|---|
| pc `0x2be7e0c4` | 0x2BE010C4 | **`_dyld_fatal_error`** (body: `udf; nop; bx lr`; `/tmp/dyld.llvm`) |
| lr `0x2be7ef07` | 0x2BE01F07 | `dyld::halt`+0x4b — the return site of `blx _dyld_fatal_error` at `halt+0x46` (0x2BE01F02) |
| r5 `0x2be997fd` | 0x2BE1C7FD | string **`"throwf, out of memory error"`** |
| r4 = r0 `0x2bec6564` | 0x2BE49564 | `halt`'s 1024-byte message buffer (`strlcpy` dest) |
| r7/sp `0x2fd829f8`/`f0` | — | exactly `sp = r7 − 8`, `halt`'s own frame (`push {r4,r5,r7,lr}; add r7, sp, #8`) |

`dyld::halt` (Thumb, 0x2BE01EBC-0x2BE01F06) is `log("%s\n", msg)` → `strlcpy(buf, msg, 1024)` →
`setAlImageInfosHalt(buf, …)` → `blx _dyld_fatal_error`. No commpage access anywhere in it.

Where the message comes from, by value: the literal at 0x2BE1C7FD is referenced **once** in the whole
image — inside `dyld::throwf` at 0x2BE0168C, on its allocation-failure branch, where it is stored as
the payload of a `__cxa_throw`n `const char*` exception (`movw r2,#45411; movt r2,#1; add r2,pc`
→ 0x2BE1C7FD). The exception is caught in `dyld::_main` at 0x2BE01E2C (`bl ___cxa_begin_catch`)
which calls `dyld::halt(message)` two instructions later (0x2BE01E34). (The neighbouring literal
`"mkstringf, out of memory error"` at 0x2BE1C7DE is `mkstringf`'s *return value*, not a halt message —
so the pointer's exact value also tells us *which* of the two formatter routines failed.) The dyld
buffer at 0x2BE49564 is in dyld's own `__DATA`, consistent with a shared-cache slide that leaves dyld
`__TEXT` at 0x2BE00000.

Mechanism of the branch: `dyld::throwf` (0x2BE01640) calls dyld's own `__simple_salloc`
(0x2BE18DAC), which is **`vm_allocate(mach_task_self(), &page, 4096, VM_FLAGS_ANYWHERE)`** and returns
NULL if the kernel refuses; on NULL (or if the following `strdup` returns NULL) it throws with that
literal. So "out of memory error" here means *a 4 KiB `vm_allocate` (or one `strdup`) failed*, not
necessarily real exhaustion. That failure and the commpage are independent: nothing in that path
touches 0xFFFF4xxx.

**Verdict on W1.** The memo's W1 was right about the mechanism (`-3` SWI returns r0=0/r1=stale: this
is still live and still corrupting every userspace `mach_absolute_time`), but the *stopping event* of
the 993 boot is not W1's silent class and not a fault at +0x90: dyld detected a fatal condition,
failed to format its own diagnostic, and called `dyld::halt` → `_dyld_fatal_error`. The udf is
therefore **not "downstream of a commpage read"** and not evidence against W1 either way — it is a
separate wall that the alias uncovered by letting the process run past 0xFFFF4090.

## 5. Verdict table — commpage fields × writer × dyld reader

| offset | iOS 7 (4570) name | written here? (by whom) | read by dyld? | consequence on D13 |
|---|---|---|---|---|
| +0x000 | SIGNATURE | no | no (dyld verified exhaustive) | none |
| +0x01E | VERSION | YES (`commpage.c:92`, u16 = 3 = `_COMM_PAGE_THIS_VERSION`) | no | satisfied |
| +0x020 | CPU_CAPABILITIES | YES (`:93`, 0x8000 = kUP) | no | satisfied |
| +0x022 | NCPUS | YES (`:61`, u8 = 1) | no | satisfied |
| +0x024/25 | USER_PAGE_SHIFT_32/64 | no | no | unread by dyld |
| +0x026 | CACHE_LINESIZE | YES (`:106`, 64) | no | satisfied |
| +0x02C | SPIN_COUNT | no (0) | no | libpthread spin tuning only |
| +0x034/35/36 | ACTIVE/PHYSICAL/LOGICAL_CPUS | YES (`:62-64`, u8 = 1) | no | libdispatch heap sizing satisfied |
| +0x038 | MEMORY_SIZE | YES (`:94`) | no | satisfied |
| +0x040 | TIMEOFDAY_DATA (legacy) | partial (`commpage.c:127-131`; sec/usec fields swapped vs the ABI struct, ticks_per_sec forced 0) | no | userspace `gettimeofday` fast path (W7) |
| +0x080 | CPUFAMILY | YES (`:90`, ARM_13) | no (dyld's own memcpy resolver via libsystem_platform) | satisfied |
| +0x088 | TIMEBASE_OFFSET | **no (0)** | **yes** — 0x2BE1B2E0/E4, only on the fast path | unreachable while +0x90 = 0 |
| +0x090 | USER_TIMEBASE | **no (0)** | **yes** — `ldrb r0,[r12,#8]` at 0x2BE1B2C8 | forces dyld's `-3`/`svc #0x80` fallback (W1) |
| +0x091 | CONT_HWCLOCK | no (0) | no | `mach_continuous_time` userspace stub only |
| +0x098 | CONT_TIMEBASE | no (0) | no | as above |
| +0x0A0 | BOOTTIME_USEC | no (0) | no | dyld has no reference |
| +0x0C0/C8 | APPROX_TIME[_SUPPORTED] | no (0) | no | `mach_approximate_time` userspace only |
| +0x120 | NEWTIMEOFDAY_DATA | no (0) | no | iOS 7 `gettimeofday` fast path (W7) |

Ranked candidates for "what the process needed next":

1. **[MEASURED] A 4 KiB allocation inside dyld** — the 993 stopper. Not commpage-related; the next
   question for the team is *why* `vm_allocate`/`strdup` failed at that moment (`throwf`'s first
   caller is also unidentified — see below). The `dyld::log` output that would have named it is
   written to stderr and does not appear in the capture (0 `dyld:` lines).
2. **[MEASURED] W1 standing** — every userspace `mach_absolute_time` still returns `(stale_r1 << 32)|0`
   on D13. Whether this corrupted anything before the halt is *not* established; it is a silent-wrong
   value, and it is the one commpage-shaped defect left in the tree.
3. **[INFERENCE, unverified] W7** — `+0x40/+0x120` TOD data: the D13 writers fill neither the ABI
   field order nor `+0x120`, so a userspace `gettimeofday` fast path would compute from zeros or a
   swapped struct. dyld does not read it; libsystem/libdispatch would.
4. **[OPEN] Which `throwf` call site threw.** The register set cannot name it (the halt frame is the
   deepest frame the kernel reports, and the 236 repetitions all show that same frame). Two candidate
   routes to identify it from a host: (a) find the caller by making `dyld::halt`'s message buffer
   readable, or (b) reconstruct which `throwf` messages are reachable before the halt — dyld's
   message strings are all in the `0x2BE1C7xx` region of the image already extracted.
5. **[OPEN] Why the kernel loops on the udf** (236 identical deliveries) — kernel-side; owned by the
   `udf-retry-mechanism` study, not by this one.

### What this changes about the task's premises

- "the `cmn r12,#3; beq swi_trap_tb` guard is compiled OUT" — **false by value**. `#if !(ARMPBA8 ||
  LEO)` with neither defined is TRUE; `out/xnu_asm_obj_d13/traps_lo.o` starts `fleh_swi` with
  `cmn ip,#3; beq swi_trap_tb`. What *is* compiled out is the arm that returns a real timebase
  (`traps_lo.s:162-168` and `:264-268`).
- "the udf at dyld+0x10C4" — **true**, and now resolved: dyld+0x10C4 = `_dyld_fatal_error`, called by
  `dyld::halt`, whose message is dyld's own `throwf` out-of-memory literal.
- The alias is doing exactly what 992 claimed: the page is the same physical page, mapped user
  read-only and XN, and 0xFFFF4090 now reads 0 *successfully* instead of faulting.