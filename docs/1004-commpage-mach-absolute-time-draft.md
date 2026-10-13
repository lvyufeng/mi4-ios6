# 1004 design — the commpage `mach_absolute_time` structure is NOT published

Status: DRAFT (READ-ONLY analysis; no source or build file modified). The ONE artifact of this pass is
this file.
Sources read by value: `records/1003-stall-forensics-preliminary.txt`, `docs/1003-dyld-stall-design-draft.md`,
`external/xnu-hd2-darwin13/xnu/osfmk/arm/commpage/commpage.{c,h}`, `.../osfmk/arm/cpu_capabilities.h`,
`.../osfmk/arm/rtclock.c`, `.../osfmk/arm/traps_lo.s`, `.../osfmk/arm/pmap.c`,
`.../osfmk/arm/arm_vm_init.c`, `.../osfmk/kern/clock.c`, `.../osfmk/vm/vm_shared_region.c`,
`src/platform/darwin13/pe_msm8974.c`, `src/entry/entry_timebase.h`, `src/entry/entry_stubs.c`,
`tools/stage_d13_commpage_alias.sh`, `tools/build_xnu_arm_kernel.sh`, and a local disassembly of the real
`/usr/lib/dyld` (dyld-324.1, 196,576 B) plus its symbol table.

---

## 0. VERDICT (plain)

**The commpage `mach_absolute_time` structure at `_COMM_PAGE + 0x88` is NOT published in this build. The
kernel writes ZERO bytes of it — in any build, on any address.** There is no `commpage_set_mach_absolute_time`
and no equivalent ANYWHERE in the D13 tree: the tree's commpage time publisher is `commpage_set_timestamp`
(`osfmk/arm/commpage/commpage.c:122`), and it writes a DIFFERENT struct at a DIFFERENT offset. The whole
`_COMM_PAGE + 0x88..0x97` region is never touched by this kernel.

This is therefore **NOT a 990-993 alias defect.** The alias question is settled SEPARATELY and unfavorably:
the alias at `0xFFFF4000` maps `IOS7LAB_COMMPAGE_ALIAS` to the SAME physical page as `0x40000000`
(`pmap.c:2869-2870` re-enters `commpage->phys_page`), so it *is* "the same page, user read-only, XN"
(`pmap.c:2878`). But because the kernel writes NOTHING at page offset 0x88, pointing the alias at that page
is irrelevant: **the byte at 0xFFFF4090 AND the byte at 0x40000090 are both zero.**

The three `-3` SVCs in the 1002 capture are therefore **CORRECT kernel behaviour**, not a defect. And the
dyld `_mach_absolute_time` shim's COMMPAGE FALLBACK is **its own designed slow path**, not a stall. The
1002 halt is downstream of the trio; 1004 must not "fix" this by writing the byte — see §4.

**Answer to the task's escape-hatch clause** ("if the publisher IS already called and the byte SHOULD be
set, say so"): the byte SHOULD be 1, but no publisher is called — and cannot be, because none exists. The
design task reduces to writing the struct (or accepting the fallback); §4 argues for the latter.

---

## 1. THE KERNEL PUBLISH PATH (by value)

### 1a. What the tree's commpage publisher writes

`osfmk/arm/commpage/commpage.c:122-134`:

```c
void commpage_set_timestamp(uint64_t tbr, uint64_t secs, uint32_t ticks_per_sec)
{
    assert(common_page_ptr);
    commpage_timeofday_data_t* tofd = (commpage_timeofday_data_t*)_COMM_PAGE_TIMEOFDAY_DATA;
    tofd->TimeBase = tbr;
    tofd->TimeStamp_sec = secs;
    tofd->TimeBaseTicks_per_sec = 0;
    return;
}
```

- `_COMM_PAGE_TIMEOFDAY_DATA` = **0x40000040** (`cpu_capabilities.h:48`) → the alias mirror is **0xFFFF4040**.
- The struct `commpage_timeofday_data_t` (`cpu_capabilities.h:66-74`) has `TimeBase` at offset 0,
  `TimeStamp_usec` at 8, `TimeStamp_sec` at 12, `TimeBaseTicks_per_sec` at 16, `TimeBase_magic` at 24,
  `TimeBase_add` at 32, `TimeBase_shift` at 36.

**This is the gtod/nanotime struct, NOT the mach_absolute_time struct.** `commpage_set_timestamp` writes
offsets `+0, +12, +16` of 0x40000040 = page offsets **0x40, 0x4C, 0x50** — nowehere near 0x88.

### 1b. The mach_absolute_time struct the real dyld reads

Disassembling the real dyld (`/tmp/dyld_ios7`, symbol table: `2be1b2c0 l F __TEXT,__text _mach_absolute_time`)
at link vmaddr 0x2be1b2c0:

```
2be1b2c0: movw ip,#0x4088 ; movt ip,#0xffff    ; ip = 0xffff4088 = _COMM_PAGE + 0x88
2be1b2c8: ldrb r0,[ip,#8]                       ; validity byte @ _COMM_PAGE + 0x90
2be1b2cc: cmp r0,#0 ; beq 0x2be1b320
2be1b2d4: isb sy
2be1b2d8: push {r4,r5,r7,lr}
2be1b2e0: ldr r4,[ip] ; ldr r5,[ip,#4]          ; read pair at +0x88/+0x8C  (seqlock)
2be1b2e8: mrrc p15,0,r3,r1,c14                  ; CNTVCT hi lo   (op0=0 → virtual PL0)
2be1b2ec: mrrc p15,0,r0,r3,c14                  ; CNTVCT lo hi
2be1b2f0: mrrc p15,0,r3,r2,c14                  ; CNTVCT hi lo again
2be1b2f4: cmp r1,r2 ; bne 0x2be1b2e0            ; retry until stable  <-- SEQLOCK LOOP
2be1b2fc: ldr r2,[ip] ; ldr r3,[ip,#4]          ; re-read the pair
2be1b304: eor r4,r4,r2 ; eor r5,r5,r3 ; orrs r5,r5,r4
2be1b310: bne 0x2be1b2e0                        ; retry until the pair is stable
2be1b314: adds r0,r0,r2 ; adcs r1,r1,r3         ; 64-bit result = CNTVCT + (base pair)
2be1b31c: pop {r4,r5,r7,pc}                     ; FAST RETURN
2be1b320: mvn ip,#2                             ; ip = -3
2be1b324: svc #0x80                             ; SLOW PATH: svc with r12 = -3
2be1b328: bx lr                                 ; the 1002 recorded return pc = 0x2be97328
```

So the structure at `_COMM_PAGE + 0x88` is a **64-bit seqlock base pair** (`[+0x88]` = timebase base lo,
`[+0x8C]` = timebase base hi) with a **validity byte at `+0x90`**. The C struct in `cpu_capabilities.h`
names its last two fields `TimeBase_add` / `TimeBase_shift`, but the asm the shim runs treats the pair at
`+0x88` as a 64-bit additive base added to CNTVCT (`adds r0,r0,r2 ; adcs`). Either way, the operative fact
is: **`+0x90` is the flag, and the tree never writes it.**

### 1c. What sets 0x90? — NOTHING in this tree

Exhaustive grep, by value, across the entire D13 tree:

- `_COMM_PAGE_MACH_ABSOLUTE_TIME` is **not defined** anywhere in `osfmk/arm/` (the only `_COMM_PAGE_*`
  defines in `cpu_capabilities.h` are CPUFAMILY 0x40000080, TIMEOFDAY_DATA 0x40000040, MEMORY_SIZE
  0x40000038, NCPUS 0x40000022, CPU_CAPABILITIES 0x40000020, MYSTERY_VALUE 0x4000001E).
- No numeric write to `0x40000090`, `0xffff4090`, or page offset `0x88`/`0x90` exists in `osfmk/arm/`
  or `src/`.
- `pmap_create_sharedpage` (`pmap.c:2847`) `memset`s the commpage to `0x00` and the alias comment at
  `pmap.c:2850-2853` says so explicitly:

  > *"Offset 0x90 remains zero: Cortex-A8 must take mach_absolute_time's syscall fallback, not attempt an
  > ARM generic-timer userspace register read. This alias does not claim full iOS 7 commpage ABI
  > compatibility."*

- `commpage_populate` (`commpage.c:73-120`) writes `_COMM_PAGE_CPUFAMILY`, `_COMM_PAGE_MYSTERY_VALUE`,
  `_COMM_PAGE_CPU_CAPABILITIES`, `_COMM_PAGE_MEMORY_SIZE`, the CPU count, the count bytes at +0x34..0x36,
  and the cache-line byte at +0x26. **It does not write +0x88 or +0x90.** It reaches no
  mach_absolute_time publisher.

**Conclusion (by value): the byte at `_COMM_PAGE + 0x90` is 0 because no code in this build — or in the
4570 sibling — ever sets it.** It is zero-initialized by the commpage memset and stays zero.

### 1d. Is `commpage_set_timestamp` called at all?

`commpage_set_timestamp` has exactly ONE caller: `clock_gettimeofday_set_commpage`
(`osfmk/arm/rtclock.c:350`), itself called only by the generic `clock_gettimeofday` (`kern/clock.c:349`,
`i386/rtclock.c`). That is the gettimeofday path, and it writes the **wrong (gtod) struct at 0x40**, not the
mach_absolute_time struct. So even if it were called, it would not set 0x90.

### 1e. The `-3` handler IS present and correct

`osfmk/arm/traps_lo.s`, gated `#if ... || (defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS)`:

```
186: cmn r11,#3 ; beq ios7lab_timebase     ; catches num = -3
...
284: #if ... STAGE90_XNU_IOS7_TRAPS ...
285: ios7lab_timebase:
286:     blx _mach_absolute_time
287:     b   swi_exit64
```

`STAGE90_XNU_IOS7_TRAPS=1` is in the 1003 set (`scripts/build_1003.sh:124,191,265`), so this branch is
compiled in. The three 1002 `-3` traps resolve to the kernel's own `mach_absolute_time`
(`osfmk/arm/rtclock.c:188`), which returns a real value from CNTVCT (`msm8974_get_timebase` → `mrrc p15,0`,
`pe_msm8974.c:644,324`). **The trap returns a valid clock; it is not a defect.**

### 1f. The timebase IS configured on this board

`msm8974_timebase_init` (`pe_msm8974.c:605`) calls `rtc_configure(msm8974_cntfrq)` unconditionally
(`:624`), setting `rtclock_sec_divisor` (`rtclock.c:283`), and publishes `gPEClockFrequencyInfo.
timebase_frequency_hz`. `rtclock_init` (`rtclock.c:317`) → `rtclock_bus_configure` + `clock_timebase_init`.
`rtclock_sec_divisor != 0`, so the `-3` path returns a **non-zero** value. (This is what 1002 forensics §6
"assuming the -3 handler returns correctly" — it does.)

---

## 2. THE 990–993 HISTORY (what the commpage rungs did, and why 991 faulted at +0x90)

Read from the memory files and the staging tools:

- **990** fixed an SP-alignment defect and drove the boot into `vm_page_lookup` (memq_hint==1 frontier).
- **991** (`armed-d13-13355bf0`, `af099f4`) = `STAGE90_XNU_VMHINT_GUARD`. The guard fired 2× and the boot
  reached userspace — `launchd` EXECUTED — then aborted **231×** at `0xffff4090`: `usermode data abort,
  EXC_BAD_ACCESS at 0xffff4090 in map 0xc00057b8 (pmap 0xc0ba3cc0) (Translation fault on page)`.
  **0xffff4090 was UNMAPPED** — the iOS 7 commpage address — because this tree's control ABI sits at
  `_COMM_PAGE32_BASE_ADDRESS = 0x40000000` (`cpu_capabilities.h:13`) and only THAT va was mapped. So the
  991 fault at +0x90 was a **missing mapping** (translation fault), NOT a false validity byte.
- **992** (`armed-d13-fce6e8dd`, `d5facfc`) = `STAGE90_XNU_COMMPAGE_ALIAS` (first attempt). REGRESSED: the
  alias `pmap_enter` into L2 slot 0xf4 read raw DRAM left over from the stolen high-vector table and died
  in `pmap_enter_options` (pai 0x7FD5A). Retired by 993.
- **993** (`armed-d13-1d5cf20e`, `160650b`) = alias + high-vector L2 zeroing. CONFIRMED: the 231×
  `0xffff4090` burst did NOT recur (**0 hits**), `L2 slot post = 0x8173a3bb`, both alias markers printed
  (`IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN`). The frontier moved to dyld's
  `_dyld_fatal_error` (a trap-wire OOM).
- **994** (`armed-d13-8b4c9a3e`, `a4a2532`) = `STAGE90_XNU_IOS7_TRAPS=1`, the trap-10/12/14 wire-width
  fix. The OOM is gone; dyld halts on its REAL error. It also compiled IN the `-3` `ios7lab_timebase`
  handler (§1e).

**So 990–993 MAP the page (and stopped a translation fault). They never PUBLISHED the structure.** After
993/994 the page is mapped at 0xFFFF4000/0xFFFF4090 — consistently confirmed by 1002 (which reaches
dyld's link path with no `0xffff4090` abort: `0 usermode data abort at 0x4`, §3 of forensics). But because
+0x90 is unwritten, the byte reads 0, and dyld takes the fallback.

The **two questions in the task's §3 are separate, and one is already answered**:
1. *Is the byte set?* NO (§1).
2. *Does the alias point at a different physical page than the kernel writes?* NO — it is the SAME page
   (`pmap.c:2869-2870`, confirmed by 993's `same page` marker). The alias is correct; there is simply
   nothing at the offset it exposes.

---

## 3. WHY THIS IS NOT (by itself) THE STALL — the honest reading

The 1002 USVC recorder has a **bounded full-field window of 64** (`entry_stubs.c:5222`
`#define USVC_FULL_N 64u`; past it only `_n`/`_num` are written, `:5258-5260`). At n=22 we are well inside
64, so the trio n=20/21/22 IS the literal tail of the recorded SVC stream — there are no more SVCs to
miss. But the trio is **exactly three calls with an advancing `lr` march** (0x2be85a9f → 0x2be85af1 →
0x2be85b11, i.e. `dyld::ImageLoader::link` +0x37/+0x89/+0xa9), which is the signature of **`link()` polling
the clock at three fixed points**, not of a tight per-call SVC retry loop.

**Reading:** dyld made three `mach_absolute_time` calls while linking one image — each took the `-3`
fallback (each trap returned a valid clock, §1f) — and then **stopped making syscalls entirely** (forensics
§3: no storage reads after `@:28906`; §4: kernel alive, 97 watchdog feeds). The three `-3` traps are a
**symptom** (the commpage fast path is unavailable so dyld takes the SVC fallback) but the **halt itself is
downstream of the trio**, most likely user-space: either dyld SPINNING in `ImageLoader::link` (waiting on
something that never satisfies) or ASLEEP. **Determining which is 1003's job (the `xnu_live_stall_*` sampler),
and 1004 must not pre-empt that.**

This matters for the fix choice: if dyld's `link` code loops until a deadline, and it is SPINNING because
each `mach_absolute_time` returns a valid non-zero value (so the loop should exit) — then the byte does not
explain the stall. If it is ASLEEP in the kernel — also unexplained by the byte. **Either way, publishing
the byte is at best a contributory fix, at worst a red herring** ([[mi4-measurement-defects]]: a fix that
treats a symptom as the frontier). 1004 should be designed but **gated on 1003's verdict**.

---

## 4. THE FIX CANDIDATE (rung 1004)

Two candidate edits exist. **The recommended rung is CANDIDATE B** (publish the structure); CANDIDATE A is a
non-fix recorded for completeness.

### CANDIDATE A — do nothing to the byte (the fallback is by design)

The pmap.c comment already declares the fallback intentional (`pmap.c:2850-2853`). If 1003 finds dyld
SPINNING on something OTHER than the clock, the correct 1004 is a **different** rung (in dyld's
`ImageLoader::link` frontier), and the commpage structure is a red herring. **Do NOT write the byte merely
because it is zero** — a zero byte that yields a correct clock is not a bug.

### CANDIDATE B (RECOMMENDED if 1003 says the clock path matters) — publish the structure

**The ONE edit:** extend the tree's `commpage_set_timestamp` (`osfmk/arm/commpage/commpage.c:122-134`) to
also populate the mach_absolute_time seqlock base pair + validity byte, and CALL it once from the timebase
path after the tick scale is live. Concretely, add (gated by a NEW switch `STAGE90_XNU_COMMPAGE_TS`):

```c
/* osfmk/arm/commpage/commpage.c, appended to commpage_set_timestamp (or a sibling) */
#if defined(STAGE90_XNU_COMMPAGE_TS) && STAGE90_XNU_COMMPAGE_TS
    /* Publish the mach_absolute_time seqlock pair at _COMM_PAGE + 0x88 with the
     * validity byte at +0x90, so the iOS 7 dyld shim takes its FAST path instead of
     * the -3 SVC fallback. Base pair = 0 (CNTVCT is already the absolute clock);
     * validity = 1. Seqlock order: write the pair, DSB, then the flag. */
    *(volatile uint32_t *)(_COMM_PAGE_BASE_ADDRESS + 0x88) = 0u;  /* base lo */
    *(volatile uint32_t *)(_COMM_PAGE_BASE_ADDRESS + 0x8C) = 0u;  /* base hi */
    OSMemoryBarrier();
    *(volatile uint8_t  *)(_COMM_PAGE_BASE_ADDRESS + 0x90) = 1u;  /* VALID */
    printf("IOS7LAB commpage ts: +0x90=1 (mach_absolute_time published)\n");
#endif
```

**The CALL SITE (the layer question).** `commpage_set_timestamp` is only reached from the gettimeofday
path (§1d) and that path is rarely (or never) taken during early boot. So a rung that only edits the
publisher would still never run into the commpage before launchd execs. The CALL must be reachable ON THIS
D13 BOOT. Two options, in order of safety:

1. **Entry-layer call (RECOMMENDED).** The entry image already owns `entry_timebase` and knows exactly
   when the tick scale is live (`rtclock_init` / `msm8974_timebase_init` publish the frequency; the
   entry's own `gPEClockFrequencyInfo` write is on the boot line). Add, in the ENTRY image (POOL layer:
   `src/entry/entry_stubs.c` or `src/entry/entry_timebase.c`), a gated call `entry_publish_commpage_ts()`
   at the point where the tick has been observed to run (the same site whose `xnu_live_tick_*` keys are
   already live). This layer is where 990-993's own additions live and is the low-risk choice: it writes
   the commpage page the kernel already maps at 0x40000000 (kernel VA, no alias needed) AFTER first
   verifying the tick runs. The alias at 0xFFFF4000 then mirrors it for free (same physical page).

2. **Pool-layer call inside `commpage_populate`.** Write the pair + flag directly in
   `commpage_populate` (`commpage.c:73-120`), gated by the switch. This runs once from
   `vm_shared_region` startup (`vm_shared_region.c:1781`) — BEFORE launchd execs, so it is definitely
   reachable. It is one file (pool object `osfmk_arm_commpage_commpage.o`), but it publishes the pair
   before the tick is proven live, so the CNTVCT it names could read 0 during a brief window.

**Recommended: option 1 (POOL, gated call in the entry image after tick-live), with the writer added to
`commpage.c` gated by `STAGE90_XNU_COMMPAGE_TS`.** Layer **POOL** (a C edit to `commpage.c` + a gated
call in the entry image). No assembly edit is needed.

**The switch — default OFF, byte-identical when off** ([[mi4-off-option-two-spellings]]): the new `#if` must
lead with `defined(STAGE90_XNU_COMMPAGE_TS)`. Note the existing commpage gates use the `||`-suffix form
(`BOARD_CONFIG_x || (defined(S) && S)`) because they OPEN a dormant gate; a NEW block uses
`#if defined(S) && S` directly (the 995 dyld-witness precedent, memory 994/995).

---

## 5. THE VERIFICATION

### 5a. By-value checks that the fix is IN the image

1. **The object references the writer.** `out/xnu_kernel_obj_d13/osfmk_arm_commpage_commpage.o` defines the
   new store sequence: a reference to the string `IOS7LAB commpage ts: +0x90=1`, and the immediates
   `#0x88`/`#0x8C`/`#0x90` in the disassembly of `commpage_set_timestamp` (or its new sibling).
2. **The call site is in the image.** The entry half (`src/entry/entry_stubs.c` / `entry_timebase.c`)
   references the writer symbol (an `U` in the object, a `T`/`bl` in the linked image) gated by
   `#if STAGE90_XNU_COMMPAGE_TS` (grep the guard literal, the 1003 build-checker pattern,
   `build_1003.sh:617`).
3. **The arm record carries `STAGE90_XNU_COMMPAGE_TS=1`** (the fresh record key, so a park cannot freeze an
   arm whose switch the record omits).
4. **The negative control:** the frozen 1003/1002 elf must NOT contain the new string or the `+0x88` store
   sequence (the discriminator).

### 5b. What the 1004 capture should show

- The marker `IOS7LAB commpage ts: +0x90=1` printed once at the publish site.
- **`_COMM_PAGE + 0x90` reads 1.** If the entry records it (a cheap `entry_write_kv("xnu_live_commpage_ts",
  *(uint8_t*)(_COMM_PAGE_BASE_ADDRESS + 0x90))` and the same at `0xffff4090`), the two must be equal
  (=1), proving the alias mirrors the same physical page.
- **`mach_absolute_time` takes the fast path: NO `-3` SVCs.** The USVC stream's tail changes: the
  `num=0xfffffffd` records (1002 n=20/21/22) are **gone**; the SVC stream either ends at n=19 or continues
  with non-(-3) numbers. This is the pre-registered by-value reading.
- **If the stall PERSISTS after the trio is gone**, that is the DECISIVE finding: the byte was a symptom,
  and the real frontier is downstream (in `ImageLoader::link` or the kernel), to be read by 1003's sampler.

### 5c. The falsifier

- **No `IOS7LAB commpage ts` marker and no `+0x90` change**: the switch did not reach the pool object
  (`build_xnu_arm_kernel.sh` only LINKS `osfmk_arm_commpage_commpage.o`, per the 933 lesson, memory
  `mi4-d13-whole-kernel-series`), or the call site did not reach the entry image. Rebuild.
- **Marker printed AND `+0x90`==1 AND the `-3` trio RECURS**: the shim is reading a DIFFERENT physical
  page than the kernel wrote → the alias/`vmsplice` identity is wrong after all, and the 990-993 "same
  page" claim (correct as of 993) has regressed. That would be a genuine alias defect.
- **Marker printed, `-3` trio gone, stall persists**: **the byte was not the frontier** — the expected
  outcome if the trio is a symptom, in which case 1004's commpage fix is a cleanliness fix and the real
  1005 is in dyld's link loop.

### 5d. Keeping the fast path honest (the seqlock)

The shim's fast path loops on `cmp r1,r2 ; bne` (CNTVCT stability) and on the pair's stability
(`bne 0x1b2e0`). With a static pair (base lo=hi=0) and a valid CNTVCT, the loop exits on the first
iteration. There is NO `clrex`/atomic in the shim and no risk of a livelock from a non-atomic 2-word
publish IF the publish writes the pair BEFORE the flag (which the recommended order does). The pair must
be re-published only on a timebase change; on D13 CNTVCT is monotonic and the base stays 0, so a one-shot
publish is sufficient.

---

## 6. RISK

**This is a low-risk, measurement-adjacent edit.** Assessment:

- **Base/writes:** it writes ONE bool (`+0x90`) and two words (`+0x88`/`+0x8C`) of the commpage page —
  a page the kernel already allocates, maps, and memsets to 0. No new allocation, no pmap change, no new
  mapping. The alias at 0xFFFF4000 already points at this same page (993-confirmed).
- **Blast radius:** the byte at +0x90 is read ONLY by the libsystem `_mach_absolute_time` shim on the
  fast-path decision. Setting it to 1 makes that shim compute the time from CNTVCT+base instead of
  issuing `svc #0x80`. Since the base pair is 0 and CNTVCT is the same counter the `-3` path returns
  (`msm8974_get_timebase` = `mrrc p15,0`), the values should AGREE. **The one thing to verify (5b) is
  that the shim's CNTVCT read does not trap** — the shim's `mrrc p15,0` (CNTVCT, op0=0) is a PL0 read
  that requires `CNTKCTL.PL0VCTEN` on Cortex-A8; if it is not enabled, the fast path would fault at the
  shim's `mrrc` instead of taking the fallback. **This must be checked by value BEFORE 1004 is armed:**
  search for a `CNTKCTL` write (`mcr p15,0,r,c14,c1,0`) on the D13 boot path. **By value this pass found
  NONE** (no `cntk_ctl_write` caller; `entry_timebase.h:94-98` explicitly says CNTKCTL bit 0 is
  "deliberately not touched"). **That makes CANDIDATE B a RISK, not a clean fix** — see §6a.
- **It "does not change behaviour beyond the commpage":** correct only IF the shim's fast path is
  fault-free. That is currently UNPROVEN and is the crux.
- **Brick risk:** none on this device model — the base is never written ([[mi4-hardware-run-safety-gate]]),
  `fastboot boot` only. A fault in the shim would surface as a user-mode abort (recoverable via the
  watchdog/`recover_last_kmsg.sh`), not a brick.

### 6a. The CNTKCTL caveat — the reason to be cautious

`CNTKCTL.PL0VCTEN` (bit 1) gates the PL0 (user) read of CNTVCT. The `_mach_absolute_time` shim's fast path
does `mrrc p15,0,...,c14` (CNTVCT) at PL0. **A comment is not a check**
([[mi4-a-claim-in-a-comment-is-not-a-check]]): if the entry image does not set `CNTKCTL` bit 1, then
setting +0x90 = 1 would make the shim attempt a non-readable register and fault. This would REPLACE the
benign `-3` fallback with a user abort — a REGRESSION. **1004 must first prove `CNTKCTL[PL0VCTEN]` is set**
(a by-value read of the register in a probe, or a grep proving a writer) before publishing +0x90. If no
CNTKCTL writer exists, CANDIDATE B is REFUSED and CANDIDATE A stands: the `-3` fallback is the only safe
fast-path and it is already correct.

---

## 7. RECOMMENDATION

1. **Do not arm 1004 blind.** The byte being 0 is CORRECT given the tree has no publisher; the three `-3`
   SVCs are a correct fallback, not the stall.
2. **Wait for 1003's `xnu_live_stall_*` reading** (SPIN vs ASLEEP, and where). That reading decides whether
   the commpage path is even on the frontier.
3. **If, and only if, 1003 shows dyld spinning on the clock**, arm CANDIDATE B gated by
   `STAGE90_XNU_COMMPAGE_TS` (POOL layer; writer in `commpage.c`, gated call in the entry image after
   tick-live; marker `IOS7LAB commpage ts`), **after** proving `CNTKCTL[PL0VCTEN]` is set. Pre-register:
   `-3` trio GONE, `+0x90`==1, stall persists-or-not.
4. If CNTKCTL is not set, CANDIDATE B is refused and the sinking of the commpage byte is closed as
   "by design" — the front moves to a different rung.

---

## 8. EXACT CITATIONS

| Fact | File:line |
|---|---|
| Alias va = 0xFFFF4000 | `osfmk/arm/pmap.c:205-208` (`IOS7LAB_COMMPAGE_ALIAS`) |
| Alias maps `commpage->phys_page` (SAME page) | `osfmk/arm/pmap.c:2869-2870` |
| Alias is "same page, user read-only, XN" | `osfmk/arm/pmap.c:2876-2878` |
| "Offset 0x90 remains zero … syscall fallback … by design" | `osfmk/arm/pmap.c:2848-2853` |
| commpage memset to 0 | `osfmk/arm/pmap.c:2847` |
| Control ABI base = 0x40000000 | `osfmk/arm/cpu_capabilities.h:13` |
| `_COMM_PAGE_TIMEOFDAY_DATA` = 0x40000040 | `osfmk/arm/cpu_capabilities.h:48` |
| gtod struct layout (TimeBase @0 …) | `osfmk/arm/cpu_capabilities.h:66-74` |
| `commpage_set_timestamp` writes 0x40000040 (+0,+12,+16) | `osfmk/arm/commpage/commpage.c:122-134` |
| `commpage_populate` — no +0x88/+0x90 write | `osfmk/arm/commpage/commpage.c:73-120` |
| `commpage_parent` … `commpage_set_timestamp` sole caller | `osfmk/arm/rtclock.c:341-351` |
| `-3` handler compiled in (IOS7_TRAPS) | `osfmk/arm/traps_lo.s:182-188,284-288` |
| kernel `mach_absolute_time` | `osfmk/arm/rtclock.c:188-193` |
| `rtclock_sec_divisor` set (timebase live) | `osfmk/arm/rtclock.c:283` via `msm8974_timebase_init` `pe_msm8974.c:624` |
| `commpage_populate` call site (pre-exec) | `osfmk/vm/vm_shared_region.c:1781` |
| high-vector L2 zeroing (993) | `osfmk/arm/arm_vm_init.c:416` |
| USVC full-field window = 64 | `src/entry/entry_stubs.c:5222,5258-5260` |
| CNTKCTL bit 0/1 "deliberately not touched" | `src/entry/entry_timebase.h:94-98` |
| No `_COMM_PAGE_MACH_ABSOLUTE_TIME` define | (grep: absent from `osfmk/arm/`) |
| dyld shim `_mach_absolute_time` @ link 0x2be1b2c0 | `/tmp/dyld_ios7` (dyld-324.1), disasm §1b |
| 1002 `-3` trio (n=20/21/22, pc 0x2be97328) | `records/1003-stall-forensics-preliminary.txt` §1, §6 |
| 991 faulted at 0xffff4090 (UNMAPPED) | memory `mi4-991-vmhint-guard-fired-commpage-frontier` |
| 993 alias confirmed, no 0xffff4090 burst | memory `mi4-993-vecl2-zeroing-armed` |