# 1004 deepening — the commpage `mach_absolute_time` byte, by value (READ-ONLY)

Status: DEEPENING (READ-ONLY analysis; no source, build or out/stage90 file touched). One artifact:
this file. Supersedes nothing; it answers the five questions the 1004-draft left open with by-value
evidence from the real trees, the real dyld disassembly, and the real vendor kernel.

Sources read BY VALUE this pass:
- `docs/1004-commpage-mach-absolute-time-draft.md` (the prior agent's draft)
- `records/1003-stall-forensics-preliminary.txt`
- `external/xnu-hd2-darwin13/xnu/osfmk/arm/cpu_capabilities.h` (D13's commpage defines)
- `external/xnu-hd2-darwin13/xnu/osfmk/arm/commpage/commpage.c` (D13's publisher)
- `external/xnu-hd2-darwin13/xnu/osfmk/arm/pmap.c` (mapping + alias)
- `external/xnu-hd2-darwin13/xnu/osfmk/arm/proc_reg.h` (cntk_ctl accessor), `machine_routines_asm.s`
- `external/xnu-hd2-darwin13/xnu/osfmk/arm/rtclock.c`, `.../traps_lo.s`
- `src/platform/darwin13/pe_msm8974.c`, `src/entry/entry_timebase.{c,h}`
- `external/xnu-4570.1.46/osfmk/arm/cpu_capabilities.h` (the REAL iOS-timebase defines)
- `external/xnu-4570.1.46/osfmk/arm/{commpage/commpage.c,machine_routines.c,machine_routines_asm.s}`
- `external/xnu-4570.1.46/libsyscall/wrappers/mach_absolute_time.s`
- `external/android_kernel_xiaomi_cancro/arch/arm/include/asm/arch_timer.h` (vendor CNTKCTL usage)
- `/tmp/ios7bin/dyld.dis` — the real `/usr/lib/dyld` (dyld-324.1, 196,576 B) disassembled

---

## 0. VERDICT (plain, and it REFUSES the byte)

**Do NOT publish `0xffff4090 = 1`.** Publishing the validity byte is unsafe on **three independent,
each-sufficient** counts, and it is *also* not the frontier:

1. **The PL0 counter read is unproven and unwritten in this tree.** The shim's fast path reads the
   counter with a PL0 `mrrc p15,0` (CNTVCT / the ARMv7 generic-timer counter), which is gated by
   `CNTKCTL`. The D13 tree contains **no CNTKCTL writer at all** (`cntk_ctl_write` is defined at
   `proc_reg.h:595` and has **zero callers**; `machine_routines_asm.s` is 380 lines with **no**
   `fiq_context_init`), and `src/entry/entry_timebase.h:94-96` says in words that CNTKCTL's bit is
   "deliberately not touched here". Setting the byte would make the shim attempt a register read that
   may trap → a **new userspace Undef**, replacing today's benign fallback (a regression of exactly
   the 991-abort shape).

2. **The base pair is never published either, so the fast-path VALUE would be wrong.** The fast path
   computes `CNTVCT + [commpage+0x88..0x8F]`. No code in this tree writes that pair (only
   `commpage_set_timestamp` exists, and it writes the *gettimeofday* struct at `+0x40`). With the pair
   at 0 the result is **raw 19.2 MHz CNTVCT ticks**, whereas the `-3` kernel path returns the
   **`tscFreq`-scaled nanosecond** value (`rtclock_timer_scale_convert`, `rtclock.c:82-93`) — a ~52×
   domain mismatch. A deadline built on the fast path would not agree with any kernel deadline.

3. **The byte is 0 for a reason and the shim's fallback is CORRECT.** `pmap.c:2848-2853` states the
   intent verbatim ("Offset 0x90 remains zero … must take mach_absolute_time's syscall fallback … does
   not claim full iOS 7 commpage ABI compatibility"), and the `-3` handler (`traps_lo.s:284-288`) is
   present and returns a real clock.

**And the frontier is not here.** The stall pc `0x2be97328` is the `bx lr` **after** the shim's
`svc #0x80` — i.e. the recorded pc proves the *fallback was taken and returned*. The byte's zero-ness
is a *symptom* of an unpublishable commpage, not the cause of the 1003 stall. 1003's sampler
(SPIN vs ASLEEP) is the rung that decides the frontier; 1004 must not arm a commpage write behind it.

---

## 1. How the commpage is set up in THIS tree (by value)

### 1a. There IS a commpage, and it is at `0x40000000` (not `0xffff4000`)

The control ABI base is **`_COMM_PAGE32_BASE_ADDRESS = 0x40000000`**
(`external/xnu-hd2-darwin13/xnu/osfmk/arm/cpu_capabilities.h:13`); `_COMM_PAGE_BASE_ADDRESS` and
`_COMM_PAGE_START_ADDRESS` are aliases of it (`:46-48`). All the D13 `_COMM_PAGE_*` data defines are
spelled as `0x4000xxxx` (`:52-58`).

### 1b. The publisher is `commpage_populate`, and it writes a fixed, small set

`commpage_populate` (`commpage/commpage.c:73-120`), called exactly once from
`vm_shared_region.c:1781` (pre-exec — the same call site `vm_commpage_init` reaches), does:

```
pmap_create_sharedpage()                 // maps it, memsets it to 0
common_page_ptr = _COMM_PAGE_BASE_ADDRESS
*_COMM_PAGE_CPUFAMILY      = CPUFAMILY_ARM_13         // 0x40000080
*_COMM_PAGE_MYSTERY_VALUE  = 3                        // 0x4000001E
*_COMM_PAGE_CPU_CAPABILITIES = kUP                    // 0x40000020
*_COMM_PAGE_MEMORY_SIZE    = ram_size                 // 0x40000038
commpage_update_active_cpus()   // NCPUS 0x40000022 + bytes 0x34/0x35/0x36 (IOS7LAB branch)
*(uint16_t*)(_COMM_PAGE_BASE_ADDRESS + 0x26) = 64     // cache-line size
```

**It never touches +0x88 or +0x90.** The only other writer in the D13 file is
`commpage_set_timestamp` (`:122-134`), which writes `commpage_timeofday_data_t` at
`_COMM_PAGE_TIMEOFDAY_DATA = 0x40000040` (`cpu_capabilities.h:54`) — offsets **+0, +12, +16 of
0x40000040** = page offsets `0x40/0x4C/0x50`, the *gettimeofday* struct, **not** the
mach_absolute_time struct. So there is **no mach_absolute_time publisher anywhere in the D13 tree**.

### 1c. The mapping, and the 993 read-only XN mirror

`pmap_create_sharedpage` (`pmap.c:2837-2883`) maps the page at `_COMM_PAGE_BASE_ADDRESS` **RW**,
`memset`s it to 0 (`:2847`), then (with `STAGE90_XNU_COMMPAGE_ALIAS`) maps the **same physical page**
a second time at `IOS7LAB_COMMPAGE_ALIAS = 0xFFFF4000` (`pmap.c:206-208`, alias `pmap_enter` at
`:2869-2870`) with `VM_PROT_READ` only. `leo_pte_build`/the enter template force the alias slot to
`L2_ACCESS_USER | L2_ACCESS_APX | L2_NX_BIT` (`pmap.c:1644`, `:3258-3259`, `:3450-3451`), and
`pmap.c:2871-2878` **asserts** that slot equals that triple and prints "same page, user read-only, XN".

**Two consequences for 1004:** (i) the user-visible iOS 7 commpage va `0xffff4000` *is* reachable and
readable (the 1002 read shows 0 `0xffff4090` aborts — the alias is live), and (ii) the alias page is
mapped **XN**, so it is data-only — the "publish a byte" story does not involve executing commpage code.

### 1d. What the REAL iOS 7 defines are (from 4570, the closest tree that has them)

D13's `cpu_capabilities.h` does not define them; 4570 does (`external/xnu-4570.1.46/osfmk/arm/cpu_capabilities.h:191-192`):

```c
#define _COMM_PAGE_TIMEBASE_OFFSET  (_COMM_PAGE_START_ADDRESS+0x088) // uint64_t timebase offset for constructing mach_absolute_time()
#define _COMM_PAGE_USER_TIMEBASE    (_COMM_PAGE_START_ADDRESS+0x090) // uint8_t is userspace mach_absolute_time supported (can read the timebase)
```

So the struct the shim reads is `_COMM_PAGE + 0x88` (a `uint64_t` offset) with the flag at `+0x90`.
4570 publishes the offset via `commpage_update_timebase()` (`4570 commpage.c:322-328`, writes
`rtclock_base_abstime`) and the flag via `user_timebase_allowed()` (`4570 commpage.c:105`;
`4570 arm/machine_routines.c:1112-1118`) which returns `TRUE` iff `__ARM_TIME__`. **D13 has neither of
these functions wired** — no `commpage_update_timebase`, no `user_timebase_allowed`, no
`rtclock_base_abstime` symbol (grep for `rtclock_base_abstime` in the D13 arm tree: 0 hits).

---

## 2. The exact byte, its meaning, and the value

Disassembling the **real** `/usr/lib/dyld` (`/tmp/ios7bin/dyld.dis`, dyld-324.1) at `_mach_absolute_time`
(link `0x2be1b2c0`), the bytes are:

```
_mach_absolute_time:
2be1b2c0: movw  r12, #0x4088            ; r12 = 0xffff4088  (_COMM_PAGE + 0x88, runtime/alias va)
2be1b2c4: movt  r12, #0xffff
2be1b2c8: ldrb  r0,  [r12, #8]          ; <-- r0 = *(uint8_t*)0xffff4090  = THE VALIDITY BYTE
2be1b2cc: cmp   r0,  #0
2be1b2d0: beq   0x2be1b320              ; byte==0 -> SLOW PATH
2be1b2d4: isb   sy
2be1b2d8: push  {r4, r5, r7, lr}
2be1b2e0: ldr   r4,  [r12]              ; offset lo
2be1b2e4: ldr   r5,  [r12, #4]          ; offset hi
2be1b2e8: mrrc  p15, 0, r3, r1, c14     ; CNTVCT hi  <-- PL0 register read
2be1b2ec: mrrc  p15, 0, r0, r3, c14     ; CNTVCT lo
2be1b2f0: mrrc  p15, 0, r3, r2, c14     ; CNTVCT hi again
2be1b2f4: cmp   r1,  r2
2be1b2f8: bne   0x2be1b2e0              ; retry until high stable
...       re-read offset, compare (seqlock)
2be1b314: adds  r0, r0, r2              ; result = CNTVCT + offset  (64-bit)
2be1b318: adcs  r1, r1, r3
2be1b31c: pop   {r4, r5, r7, pc}        ; FAST RETURN
2be1b320: mvn   r12, #2                 ; r12 = -3
2be1b324: svc   #0x80                   ; SLOW PATH
2be1b328: bx    lr                      ; <-- the 1002 recorded pc 0x2be97328
```

**The byte's meaning** (§1d, 4570 wording): "is userspace mach_absolute_time supported (can read the
timebase)". The shim's test is `cmp r0,#0; beq slow`, so **any non-zero value takes the fast path**;
the D13/4570 kernels write `1` (a boolean). **The value to publish is 1.**

**The slow path reads NO register** — it is `mvn r12,#2; svc #0x80; bx lr`. So the answer to "does the
fallback read CNTVCT or just the page?" is: **the fallback reads neither — it traps**; the *kernel's*
`-3` handler reads the counter (`msm8974_get_timebase` → `mrrc p15,0`). Only the **fast path** (byte =
1) reads CNTVCT at PL0. **This is the crux of §3.**

**Byte-identity to upstream.** The shim's instructions are byte-for-byte the ARM slice of
`external/xnu-4570.1.46/libsyscall/wrappers/mach_absolute_time.s:176-202` (`movw/movt ip;
ldrb; cmp; beq; isb; push; ldr/ldr; mrrc×3; cmp/bne; ldr/ldr; eor/eor/orrs; bne; adds/adcs; pop`).
The one comment-level difference: upstream's lead comment calls the register **"CNTPCT"**, the D13
side reads **CNTVCT** (`msm8974_get_timebase` name + `entry_timebase.c:217` `mrrc p15,1`). On this
board the two counters are equal because **nothing writes `CNTVOFF`** (`pe_msm8974.c:329-331` says
so), so the two names denote one number here. **The opcode is `p15,0` either way — opc1=0.**

---

## 3. The PL0VCTEN / PL0PCTEN risk, settled by value

### 3a. ARMv7 names the opc1=0 counter read `CNTVCT` and gates it on `CNTKCTL[1]`

`external/android_kernel_xiaomi_cancro/arch/arm/include/asm/arch_timer.h` — the vendor kernel that
*boots on this exact SoC* — encodes the ARMv7 truth:

```c
#define ARCH_TIMER_USR_PCT_ACCESS_EN  (1 << 0) /* physical counter */
#define ARCH_TIMER_USR_VCT_ACCESS_EN  (1 << 1) /* virtual counter */
...
asm volatile("mrc p15, 0, %0, c14, c1, 0" : "=r"(cntkctl));   // CNTKCTL, opc1=1 -> bit1 = virtual counter
```

So on ARMv7: **bit 0 = physical-counter PL0 access (`PL0PCTEN`), bit 1 = virtual-counter PL0 access
(`PL0VCTEN`)**, and the shim's `mrrc p15,0,...,c14` needs **bit 1**. (On arm64 the opc1=0 register is
`CNTPCT` and the bits are swapped — 4570/arm64 `proc_reg.h:1275` calls bit1 `PL0VCTEN` for the
*physical* counter — which is why the naming drifts; the D13 A32 boot ABI is the ARMv7 one.)

### 3b. The D13 tree sets NEITHER bit — there is no CNTKCTL writer

By-value greps over `external/xnu-hd2-darwin13/` and `src/`:

- `proc_reg.h:594-595` define `cntk_ctl_read`/`cntk_ctl_write` (`p15,0,%0,c14,c1,0`). **No caller of
  `cntk_ctl_write` exists** (grep in D13 + src: 0 hits outside the inline definition).
- `machine_routines_asm.s` (D13) is **380 lines**; it has **no `fiq_context_init`** and **no `c14,c1`**
  mcr/mrc at all. Its `fiq_context_init` (the 4570 function that would set CNTKCTL, `4570
  machine_routines_asm.s:947`) **does not exist in D13** (grep `fiq_context_init` in D13: 0 hits).
- No `__ARM_TIME__` is defined anywhere in the D13 build (grep: the only hits are unrelated
  `alarm_time` variables). So even the guard that would *compile in* a CNTKCTL write is absent.
- `src/entry/entry_timebase.h:94-96`: *"CNTKCTL's bit 0 is deliberately not touched here …
  with `__ARM_TIME__` off (this build) nothing does, and this step does not."* — a **documented
  decision**, not an oversight.
- The entry image writes `CNTV_TVAL`/`CNTV_CTL` (`c14,c3,{0,1}`) and reads `CNTFRQ` (`c14,c0,0`) and
  `CNTVCT`/`CNTPCT` (`mrrc …,c14`) — but **never `c14,c1` (CNTKCTL)** (grep `c14, *c1` in `src/`: 0
  hits; only `c14,c0` for CNTFRQ).

**Therefore `CNTKCTL` is whatever it was left at — the kernel never writes it.** The ARMv7-A reset
value disables all PL0 timer/counter access, and this tree adds nothing on top; but whether MSM8974's
secure firmware / bootloader pre-sets bit 1 is **not established by anything in this repo** (grep for a
`mcr …,c14,c1,0` on any boot path: 0 hits). Reading it at PL1 is always permitted, so the only way to
settle it is to read it — which is exactly what a live `xnu_live_cntkctl` key would do (§5). Until that
read exists, the fast path's `mrrc` is **unproven** and the byte must not be set.

### 3c. What publishing the byte would do, concretely

1. First `mach_absolute_time` call after the publish: `ldrb r0,[r12,#8]` → `1` ≠ 0 → **fast path**.
2. `mrrc p15,0,r3,r1,c14` at PL0. If `CNTKCTL[1]==0` → **Undefined Instruction exception** in user
   mode → the shim has **no fault handler** → `SIGILL`/kernel `sleh_undef`. This is a **new** userspace
   fault that the 1002 capture does **not** contain today. It would replace a *correct benign* fallback
   with a crash. (This is precisely the failure mode `entry_timebase.h:94-96` and `pmap.c:2850-2853`
   were written to avoid.)
3. Even if the register IS readable (bit already set by firmware): the fast path returns
   `CNTVCT + 0` (**raw 19.2 MHz ticks**), while the kernel's `mach_absolute_time` returns
   `rtclock_timer_scale_convert()` = `CNTVCT * 1e9 / 19.2e6` (**ns**). ~52× apart. Any dyld/libdispatch
   deadline that mixes the two domains (e.g. compares a user-computed absolute deadline against a
   kernel-returned time) becomes wrong by ~52× — a "wait until" loop that could spin for ~52× its
   intended duration, or an immediate timeout.

So the byte alone is **doubly broken**: it can fault, and if it does not fault it returns the wrong
number in the wrong unit.

---

## 4. Is the byte even the frontier? No (this pass agrees with the 1003 read)

- The recorded stall pc `0x2be97328` = link `0x2be1b328` = the `bx lr` **after** `svc #0x80`
  (dyld.dis, §2). A pc *after* the svc means the fallback **was taken and returned** — the kernel's
  `-3` path ran (`traps_lo.s:284-288`) and gave control back. The three `-3` SVCs (`records/… §1`,
  n=20/21/22) are three such calls from one `ImageLoader::link` body (`lr` march
  `0x2be85a9f→…f1→…11`, = link `+0x37/+0x89/+0xa9`). They are the *last recorded* user activity, then
  **no syscall for ~23 min** (forensics §3-4). A zero byte that yields a *correct, returned* clock is
  not the thing that stops the thread.
- So 1004 is **gated on 1003**. If 1003's per-tick frame sampler says pid 1 is **User and looping**,
  the frontier is dyld's `link` code, not the byte. If it says **kernel/asleep**, the frontier is a
  wait/page-in/lock. Either way, the commpage byte is at most a *cleanliness* item.

---

## 5. RECOMMENDATION (minimal, lowest-risk, with a self-check)

**Rung 1004 as a commpage write is REFUSED.** Leave `+0x90` at 0. The `-3` fallback is correct and
already returns the same clock the kernel uses. Writing `+0x90=1` is a *regression risk* (§3c) with no
demonstrated frontier behind it (§4).

**If a future rung PROVES (via 1003) that pid 1 spends its stall re-entering the `-3` path in a tight
loop, and the frontier is the fallback's cost**, then — and only then — the minimal correct change is
**THREE coupled edits, never the byte alone**:

1. **Enable the PL0 counter read** with a single `CNTKCTL` write on the boot path:
   `mcr p15,0,r0,c14,c1,0` with `r0 = (CNTKCTL| (1<<1))` — read-modify-write, **bit 1 only**, so no
   other PL0 timer/timer-register access is opened. (One instruction; the accessor already exists,
   `proc_reg.h:595`.) Place it where the tick is already live (`msm8974_timebase_init`, after
   `rtc_configure`) so it cannot precede the counter being usable.
2. **Publish the pair AND the flag together, pair first**: `*(u32*)(_COMM_PAGE+0x88)=off_lo;
   *(u32*)(_COMM_PAGE+0x8C)=off_hi; dsb; *(u8*)(_COMM_PAGE+0x90)=1;` — with the offset chosen so that
   `CNTVCT + offset == the -3 path's value` (i.e. reconcile with `_absolutetime…`/`rtclock` domain;
   with the current scale the pair cannot be a constant, *which is itself a reason not to pursue this*
   — see the 4570 model, where the kernel's abstime is `base + counter`, not a runtime multiply).
   Writing the flag **before** the pair would let the shim read a half-published pair (seqlock hazard).
3. **Address the domain mismatch**: do not publish raw CNTVCT as if it were the kernel's abstime. If the
   tree cannot express `base + counter == scaled-abstime`, the fast path cannot be made correct and the
   fallback remains the right answer.

**Self-check (a build clause) that must REFUSE a half-done image** — the by-value guard for the rung:
a `tools/check_commpage_timebase.py` (modelled on `tools/check_commpage_alias.py`) that walks the linked
`out/stage90/xnu_arm_entry.elf` and **fails the build unless ALL of**: (a) a `c14,c1,0` **write**
(`mcr`) is present in the image's text, (b) the `+0x90` store is present, and (c) the `+0x88`/`+0x8C`
stores precede it. An image that sets `+0x90` without writing `CNTKCTL` (the dangerous half-fix) is
**refused**. This is the check that makes "a comment is not a check" (`mi4-a-claim-in-a-comment-is-not-a-check`)
into a build failure.

**Live key (the reading that would prove it, if it is ever armed):**
`xnu_live_commpage_ts = *(uint8_t*)0x40000090` **and** `xnu_live_commpage_ts_alias =
*(uint8_t*)0xffff4090` must both read **1** (equal — proving the alias mirrors the same physical page),
plus `xnu_live_cntkctl = <mrc c14,c1,0>` whose **bit 1 must be set**. The pre-registered outcome:
the `-3` trio (n=20/21/22) **gone**, and — the decisive reading — **whether the stall persists**. If
the stall persists with the trio gone, the byte was a symptom and the frontier is elsewhere (§4).

**Falsifier / regression tripwire:** if the trio is gone but a **new** userspace abort appears (Undef
at the shim, EXC_BAD_INSTRUCTION), `CNTKCTL[1]` was not set / not effective and the read faulted —
confirming §3c and that the write must be refused. If the marker prints, `+0x90==1`, and the `-3` trio
**recurs**, the alias points at a different physical page than the kernel wrote (a genuine alias
regression).

**The honest lowest-risk change is: none.** 1004's job is to record that the byte is zero by design,
that the fallback is correct, and to hand the frontier to 1003's SPIN/ASLEEP reading.

---

## 6. EXACT CITATIONS (file:line, by value)

| Fact | File:line |
|---|---|
| Control ABI base = `0x40000000` | `external/xnu-hd2-darwin13/xnu/osfmk/arm/cpu_capabilities.h:13` |
| D13 commpage defines (no +0x88/+0x90) | `.../cpu_capabilities.h:52-58` |
| `_COMM_PAGE_TIMEOFDAY_DATA` = 0x40000040 | `.../cpu_capabilities.h:54` |
| D13 gtod struct (guessed) | `.../cpu_capabilities.h:66-74` |
| `commpage_populate` — writes CPUFAMILY/MYSTERY/CAP/MEMSIZE/NCPUS/cache-line, never +0x88/+0x90 | `.../commpage/commpage.c:73-120` |
| `commpage_set_timestamp` writes 0x40000040 (+0/+12/+16) | `.../commpage/commpage.c:122-134` |
| `commpage_populate` called once, pre-exec | `.../vm/vm_shared_region.c:1781` |
| REAL defines: `_COMM_PAGE_TIMEBASE_OFFSET=+0x88`, `_COMM_PAGE_USER_TIMEBASE=+0x90` | `external/xnu-4570.1.46/osfmk/arm/cpu_capabilities.h:191-192` |
| 4570 offset publisher `commpage_update_timebase` | `external/xnu-4570.1.46/osfmk/arm/commpage/commpage.c:322-328` |
| 4570 flag publisher `user_timebase_allowed` (==`__ARM_TIME__`) | `.../4570 .../commpage/commpage.c:105`; `.../arm/machine_routines.c:1112-1118` |
| D13 has no `rtclock_base_abstime` symbol | grep `external/xnu-hd2-darwin13/xnu/osfmk/arm/`: 0 hits |
| Commpage mapped RW at 0x40000000, memset 0 | `.../arm/pmap.c:2842-2847` |
| Alias va 0xFFFF4000 = `IOS7LAB_COMMPAGE_ALIAS` | `.../arm/pmap.c:206-208` |
| Alias = same page, READ-only, XN (forced USER\|APX\|NX) | `.../arm/pmap.c:2869-2878`, `:1644`, `:3258-3259`, `:3450-3451` |
| "Offset 0x90 remains zero … syscall fallback … by design" | `.../arm/pmap.c:2848-2853` |
| `cntk_ctl` accessor defined, no caller | `.../arm/proc_reg.h:594-595` |
| D13 `machine_routines_asm.s`: 380 lines, no `fiq_context_init`, no `c14,c1` | `.../arm/machine_routines_asm.s` (whole file) |
| 4570 CNTKCTL writer sets **bit 0** (`PL0PCTEN`) | `external/xnu-4570.1.46/osfmk/arm/machine_routines_asm.s:939-947` |
| ARMv7 CNTKCTL bit1 = **virtual** counter PL0 enable | `external/android_kernel_xiaomi_cancro/arch/arm/include/asm/arch_timer.h:8-9,32-34,49` |
| `CNTKCTL` "deliberately not touched" on D13 | `src/entry/entry_timebase.h:94-96` |
| D13 `mach_absolute_time` returns `tscFreq`-scaled value | `.../arm/rtclock.c:82-93,188-193,299-304` |
| D13 counter read = CNTVCT `mrrc p15,0` | `src/platform/darwin13/pe_msm8974.c:322-326,628` |
| CNTVOFF never written (CNTVCT==CNTPCT here) | `src/platform/darwin13/pe_msm8974.c:329-331` |
| `-3` handler compiled in, returns real clock | `.../arm/traps_lo.s:182-188,284-288` |
| Real dyld shim disasm (validity @ +0x90, CNTVCT read, fallback svc #0x80) | `/tmp/ios7bin/dyld.dis`, `_mach_absolute_time` @ 0x2be1b2c0 |
| Upstream byte-identical ARM shim | `external/xnu-4570.1.46/libsyscall/wrappers/mach_absolute_time.s:176-202` |
| 1002 stall pc 0x2be97328 = the `bx lr` after `svc #0x80` | `records/1003-stall-forensics-preliminary.txt:212-239` |