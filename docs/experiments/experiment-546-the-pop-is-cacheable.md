# 546: the pop is cacheable, and nothing in the window invalidates the L2

A host-side reading of the bytes the run dies in, with no device and no build. It is 545's method applied
to the frontier object itself: the whole of `platform_cache_idle_enter` and `platform_cache_idle_exit`
read instruction by instruction out of the frozen image, for where the window opens and closes, what
runs between, and what state the fatal `pop` executes in.

**The reading in one line: the fatal `pop {fp, pc}` executes 24 bytes after Apple's own exit turns the
D-cache back on, and no cache operation anywhere inside the window invalidates an L2 line - so a stale
valid L2 copy of the pushed stack line is readable at the pop, and Apple's L1 flush is the wrong level to
stop it.** That also explains, without being built to, why 516's and 521's cache arms did not work.

## 1. The window, and where the pop sits in it

Six instructions carry the whole story, and the source they were compiled from says the same in Apple's
own words: `platform_cache_idle_exit` is `osfmk/arm/caches.c:451-497` (the `ARMA7` body), the enter's
cache-off is `platform_cache_disable()` (`:385-400`, `/* Disable dcache allocation. */`), the exit's
re-enable carries `/* Enable dcache allocation. */` at `:486`, and the caller is `cpu_idle()`
(`osfmk/arm/cpu.c:155-157`): `platform_cache_idle_enter(); cpu_idle_wfi(...); platform_cache_idle_exit();`
- so the `lr` this frame pushed names `cpu_idle`, and the `pop` is the return into it.
`mrc p15,0,Rt,c1,c0,{0}` is `SCTLR`; `{1}` is `ACTLR`.

| site | instruction | source | effect |
| --- | --- | --- | --- |
| `0x8004623c`-`0x80046244` | `mrc` SCTLR / `bic r0, r0, #4` / `mcr` | `caches.c:385-400`, in the enter | **`SCTLR.C` = 0** - `platform_cache_disable()`, second instruction of the enter |
| `0x800462b8`-`0x800462c0` | `mrc` ACTLR / `bic r0, r0, #0x40` / `mcr` | `caches.c:434` `/* Leave the coherency domain */` | ACTLR bit 6 written low |
| `0x800462d4` | `push {fp, lr}` | the frame the compiler gave the exit | **inside the window, cache off** - the stores go to memory |
| `0x800462d8` | `bl FlushPoU_Dcache` | `caches.c:459` `/* Flush L1 caches and TLB ... */` | L1 clean+invalidate only (545 §5) |
| `0x8004630c`-`0x80046314` | `mrc` ACTLR / `orr r0, r0, #0x40` / `mcr` | `caches.c:473` `/* Rejoin the coherency domain */` | ACTLR bit 6 written high |
| `0x8004631c`-`0x80046324` | `mrc` SCTLR / `orr r0, r0, #4` / `mcr` | `caches.c:486-490` `/* Enable dcache allocation. */` | **`SCTLR.C` = 1** - the D-cache comes back on |
| `0x8004633c` | `pop {fp, pc}` | `caches.c:495` follows | **cacheable - the cache is on 24 bytes earlier** |

So the window is bounded by Apple's own two writes (`0x80046240` opens, `0x80046324` closes), the push
and the pop are both inside it, and the pop is on the **cacheable** side of the re-enable, not the
uncached side. The push is on the uncached side. The four instructions between the re-enable and the pop
are a `isb`, the per-CPU read, and a `ldr`/`str` pair - **and not one of them is a `bl`**.

## 2. What the window does to the caches, and the level it never touches

Every cache operation called inside the window `[0x80046240, 0x8004633c)` - and, since **the branches that
choose them are not all taken**, which of them actually runs in this image (the branch analysis is
[549](experiment-549-which-arm-runs-is-a-boot-arg.md); the image's boot-args end in
`up_style_idle_exit=1` and `real_ncpus` is `1`):

| site | call | what it invalidates | runs in this image? |
| --- | --- | --- | --- |
| `0x80046274` | `CleanPoU_Dcache` | nothing - clean only, L1 | **yes** - the arm this image takes |
| `0x80046284` | `FlushPoU_Dcache` | L1 only | no (the other arm) |
| `0x800462b0` | `CleanPoC_DcacheRegion(r4, 0x340)` | nothing - a *clean by VA* of the per-CPU struct, not a set/way sweep | no (the other arm) |
| `0x800462d8` | `FlushPoU_Dcache` (the exit's) | **L1 only** | **yes** |
| `0x80046304` | `InvalidatePoU_Icache` | the L1 **I-cache** | no (`up_style_idle_exit == 1`, `real_ncpus < 2`) |
| `0x80046308` | `flush_core_tlb` | the TLB | no (same condition) |

So the window's entire cache work in this configuration is **two operations, both D-side and both L1**:
a clean at the enter and a clean-and-invalidate at the exit. (My first version of this table listed all six
as executed; the branch analysis corrected it, and the correction makes the point below *stronger*, not
weaker.) **The only lines invalidated anywhere in the window are L1 lines**, and an L2 invalidate would
have to come from a PoC *flush* (`c7,c14,2` at level 2), for which there is no call in the window at all -
taken or not.

**The source says this even more plainly than the bytes do, in Apple's own comment on the first of them**:
`/* Flush L1 caches and TLB before rejoining the coherency domain */` (`caches.c:459`). Apple wrote the
exit's flush *as* an L1 flush and labelled it one. The exit contains no PoC operation of any kind - its
only other memory-related calls are the I-cache invalidate and the TLB flush, and in this image's
configuration those two are **skipped** - so the L2 is not maintained by this path on purpose, not by
omission. The two globals that pick the branches (`up_style_idle_exit` at `0x805511a4`, `real_ncpus` at
`0x80520378`; `caches.c:414`/`:464`) change *which* L1 work runs, and on **neither** branch is the L2
touched: the arm this image takes is the cheapest one, a single clean.

## 3. The hypothesis, and why it explains the two failures it was not built to

Set/way maintenance is not gated by `SCTLR.C` - it acts on the cache - so the L1 really is invalidated by
`0x800462d8` even though the cache is off. That is exactly what makes the L2 the gap:

1. The stack lines the idle thread uses were populated **cacheable** earlier, so a valid copy of them
   exists in the L1 and has been victimised down to the L2. Those L2 copies hold the *old* stack
   contents.
2. The push at `0x800462d4` runs with **`C` = 0**, so its two stores go to memory and the cached copies
   are not updated. A copy that is not updated is a copy that is now **stale, valid, and in the L2**.
3. `0x800462d8` invalidates the **L1** copy. The L2 copy is untouched, and nothing later in the window
   touches it.
4. `0x80046324` turns the cache back on. The pop at `0x8004633c` loads the stack address, **misses the
   L1** (just invalidated), and hits the **stale valid L2 line** - returning the value the stack held
   before the push instead of the `{fp, lr}` the push wrote to memory.

One thing already measured is consistent with this and was read as a curiosity: the value the pop dies
on is **a counter reading, and the fault's `pc` is that value minus one**. Under this reading that is
exactly what a stale slot should hold - this frame sits on the idle thread's stack at the address every
deeper call has already used, so the *previous* contents of the slot are the leftovers of whatever last
ran there (a counter's value is what a timer read leaves in a register passed down), and on the first
pass through `cpu_idle`'s new frame there is no earlier `lr` of its own to find. The reading does not
prove the mechanism - a leftover of any kind would look like this - but it does say the popped value
should be *garbage that is not an address*, which is what was seen.

**The hypothesis is one sentence - the L2 holds a copy the push could not update, and it is readable
because the cache is on by the time the pop runs - and it accounts for the two arms that were built
against this seam and did not work, neither of which was designed with it in mind:**

| arm | what it did | why this reading says it could not help |
| --- | --- | --- |
| 516, `CleanPoC_Dcache` in the enter wrapper | cleaned both levels before the window | a **clean** writes the stale copy *out*; it does not remove it. The L2 copy is still there for the pop to hit |
| 521, `FlushPoC_Dcache` in the enter wrapper | cleaned **and invalidated** both levels | the right operation at the wrong **time**: it runs before the window opens, and every cacheable stack access after it - the wrapper's own note-taking, Apple's enter code, the payload's bracket publishers - can victimise a fresh copy down into the L2 before the push ever happens |

That is the sign worth taking seriously and not as proof: **a mechanism that explains the failures it did
not cause.** 516 and 521 were each built on the assumption that the L1 was the problem, and under this
reading each removed the wrong thing - one did not invalidate at all, the other invalidated too early.
Both were recorded as negative results with no explanation; this supplies one, and it is falsifiable.

## 4. What that does to the seam, which is 535's question

The fix has to invalidate **the L2**, **after the push** (`0x800462d4`) and **before the re-enable**
(`0x80046324`). Two consequences, one of them a correction to 534/544/545's plan:

1. **534's seam is still the right one, and for a third reason.** The exit's `bl FlushPoU_Dcache` at
   `0x800462d8` is the **only** cache operation the exit performs in this image's configuration - the
   other two `bl`s in the span (`InvalidatePoU_Icache` at `0x80046304`, `flush_core_tlb` at `0x80046308`)
   are skipped by the `up_style_idle_exit == 1` / `real_ncpus < 2` branch, and both are I-side or TLB
   anyway - it is reached in every pass, and it is a `bl`, so it is wrappable, per
   [[mi4-hooks-live-at-calls]]. What 535 puts behind it should be a **PoC flush**, so that what runs after
   the push is a clean+invalidate of both levels rather than an L1-only clean+invalidate.
2. **And it must be told apart from the other three callers of the same routine.** `FlushPoU_Dcache` is
   called from four sites - `0x80045d08` (`cache_xcall`), `0x80046284` (the enter's else path),
   `0x800462d8` (the exit), `0x800463bc` (`cache_xcall_handler`) - and wrapping the symbol alone would put
   the flush in all four. The wrapper has to test the return address it was entered with (`0x800462dc`
   for the exit), which is what the project's `xnu_entry_stub_caller` mechanism already exists to read.

**And the region between the re-enable and the pop is unwrappable**, which closes off the other obvious
place to put it: those four instructions contain no `bl` at all, and the `pop` itself is not a call. So
"flush after the cache comes back on and just before the pop" is not a plan that can be built here - the
L2 has to be cleaned out *while the cache is off*, which is exactly the window step 1 above names.

## 5. The prediction this makes for 533, stated before the run

533 changes the **enter-side** `SCTLR.C` re-enable (`STAGE90_XNU_IDLE_CACHE_ENABLE=0`) and does not touch
the flush. Under this reading the stale L2 line is produced by the push and the off-cache window, neither
of which 533 changes, so:

- the pass should reach the exit and **die at the same `pop`** - `pre_calls` and `rtcpre_calls` published
  with `post_calls` absent, the localization `run_and_capture.sh` now prints; and

> **Correction (559, before the run): the second key is `rtcpre`, not `rtcab`.** Written here as
> `rtcab_calls` and corrected above in place; the prediction itself - the same three sites, the same
> order, the death at the `pop` - is unchanged. The exit wrapper's second publisher is `g_slot_rtcpre`
> (`entry_trace.c:1973`, `entry_slot_rtc_note(&g_slot_rtcpre, entry_tpidrprw())`); `g_slot_rtcab` is the
> **abort** path's (`entry_stubs.c:1792`, inside `entry_note_sleh`), so it is a *storm* counter and not a
> per-pass one - 520's own log has 5 `rtcab` records (`1,2,3,4,8`) against exactly one `rtcpre`. A
> criterion written on `rtcab` is satisfied by any pass that took an abort, so it would have read this
> arm's death as "inside the exit" even when the notes say otherwise. The peer session found the same
> name in `run_and_capture.sh`'s clause (3) and in 533/540 (its 558); this one, 547 §4, 554 §1 and
> 557 §4c were outside that enumeration and are corrected here. See
> [560](experiment-560-the-same-wrong-key-in-four-more-files.md) (and, for the gate's own copy of
> the bracket, [559](experiment-559-the-brackets-middle-key-was-named-wrong-in-the-one-block-that-governs.md)).
- the `slot_cwe_win`/`slot_cwe_set` pair should both read `C` clear (533's arm's shape, 540 §3).

**One thing 533 cannot change either way, and it is worth knowing before reading its log:** the re-enable
this section is about is **Apple's own, unconditional, in the exit** (`caches.c:482-495`, `#if __ARM_SMP__`
inside `#if defined (ARMA7)`), not the wrapper's enter-side one that 533 switches off. So every arm the
phase has run - including 520's returning cell - executes a `SCTLR.C` set within 24 bytes of the pop. If
the reading here is right, that write is in the returning runs too, and what saved them was that the stale
line happened not to be resident; if 533 returns, it is not this write that is being switched and §3's
step 4 is the wrong mechanism.

**It is written down because it can be wrong, and its being wrong is worth more than its being right:**
if 533 *returns*, then removing the enter-side re-enable fixed the pop, the L2 story here is not the
mechanism (or not the only one), and 535's flush would be aimed at the wrong level - which is the one
outcome that should stop 535 from being built as designed.

> **[547](experiment-547-the-load-succeeded-and-the-value-is-wrong.md) sharpens this prediction and
> reconciles it with the ledger.** 520's register dump shows the abort is a **prefetch** abort at the
> popped value with `lr = 0x800462dc` - so both loads of the `pop` succeeded and only the *value* was
> wrong, which eliminates the address-arithmetic family without any cache hypothesis. And the two cells
> that carry the wrapper's `SCTLR.C` write (522, 526) are the two that produce **no log at all**, which is
> the cell in which this document's precondition - a store the cache did not see - is *absent*: their push
> is cacheable, so this mechanism should not fire in them. The prediction 547 states is therefore the
> sharper one: **533 should die at the pop, log it, and come back on `MACH Reboot`** - and 547 §4 gives the
> three outcomes as a falsifier table.

## 6. What this does not decide

- **Whether the stack region is mapped cacheable.** *Checked, and it is* -
  [548](experiment-548-the-kernel-memory-is-mapped-cacheable.md): XNU maps kernel memory with attribute
  index 0 (`arm_vm_init.c:175-176`, `pmap.c:2641`/`:5416`/`:6232`/`:6240`, `CACHE_ATTRINDX_WRITEBACK`
  `proc_reg.h:630`), Apple names index 0 *"cache enabled, buffer enabled"*, `NMRR_SETUP = 0x01210121` has
  `IR0 = NMRR_WRITEBACK`, and the `PRRR.TR0 = 0b10` that is actually installed is cacheable under either
  reading of that field - so the premise holds. The live `SCTLR` captured **inside the window**
  (`xnu_live_pce_after_sctlr = 0x30c57879`) has `TRE` set (bit 28), which is what makes the index mean
  anything; the payload-era reading (`0x00c5487b`) has `TRE` clear, so the attributes in force at the
  frontier are XNU's.
- **Whether the death is a cache matter at all**, and which level holds the line the pop actually hit.
  545 §6's missing-barrier hypothesis and §3's stale-L2 hypothesis are both consistent with the same
  failures; they are not exclusive, and nothing here ranks them.
- **533's arm**, which is unchanged, frozen and unrun: `1daaf44e624563694e…` (boot image, `./build.sh`
  not run) and `f202f2465886aba6…` (entry image). This document adds a prediction about its result and
  changes nothing about it.
- **535**, still a design and not an artifact, and the entry lane's to build - now with one more
  requirement than 534 gave it: a PoC flush, behind the exit's call site only, by return address.
- **The storage line and TWRP**, unchanged: still withheld.

## 7. Safety

No device action in this step. `objdump` and one `grep` over `out/stage90/xnu_arm_entry.elf`, and grep
over the memory directory. Nothing written outside `docs/` and the memory files, no build run, no file
under `out/` touched, `flash` not used, and the frozen pair is untouched (`1daaf44e624563694e…` /
`f202f2465886aba6…`). The device is off the bus and owes a power press before 533 can run.
