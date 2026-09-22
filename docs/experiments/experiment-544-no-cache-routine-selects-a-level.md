# 544: no cache routine in this image selects a level, so one of the two loops is always on the wrong one

> **Superseded in part by [545](experiment-545-the-level-is-in-the-operand.md).** The cache level travels
> in the **set/way operand's own low bits** - `mov r0, #0` at the head of every routine, `mov r0, #2` at
> the head of every second loop, the `CSSELR` encoding of L1 and L2 - so §2's consequence ("after
> `do_cacheid` the level is L2 for the rest of the boot"), both branches of §3, and §4's claim that no
> sequence the project has run can invalidate an L1 line are **wrong**. What stands: §2's two `CSSELR`
> sites as a fact about `CSSELR`, §5's "a sweep must read `CCSIDR`, not `proc_reg.h`", and the choice of
> seam (the exit's `FlushPoU_Dcache`) - which 545 re-justifies on coverage rather than on geometry.

A host-side reading with no device and no build, on the critical path of the next arm. It settles what
534's `CSSELR` finding *means* for the flush the idle exit actually calls, and it does it in a form that
does not depend on how well I remember the ARM ARM.

**The reading in one line: the image contains two `CSSELR` writes in total, neither of them inside a
cache-maintenance routine, and Apple's routines are written in *pairs* of loops whose two geometries can
only matter if something selects a level between them.** So whichever way the architecture resolves the
level, one of every pair is maintaining the wrong cache - and for the one call the exit makes, that
means the L1 is not maintained by anything in this image.

## 1. The two loops, read off the linked image

`CleanPoC_Dcache` and `FlushPoC_Dcache` each have two loops; `CleanPoU_Dcache` and `FlushPoU_Dcache` have
one. From `out/stage90/xnu_arm_entry.elf` (`CONFIG` `ARMA7=1`, `__ARM_L2CACHE_SIZE_LOG__=21`, both read
out of `tools/build_xnu_arm_kernel.sh:362`):

| | loop A | loop B |
| --- | --- | --- |
| `FlushPoU_Dcache` @ `0x80045874` | set inc `#64`, overflow `#0x2000` → **128** sets, way inc `1<<30` → **4** ways, `mcr 15,0,r0,c7,c14,2` | *absent* |
| `CleanPoU_Dcache` @ `0x800457a8` | identical to the above with `c7,c10,2` | *absent* |
| `CleanPoC_Dcache` @ `0x8004575c` | identical to loop A | set inc `#64`, overflow `#0x40000` → **4096** sets, way inc `1<<29` → **8** ways |
| `FlushPoC_Dcache` @ `0x80045828` | identical to loop A | identical to loop B, with `c7,c14,2` |

Every one of those constants decodes to a `proc_reg.h` define from the **`ARMA7`** branch: `MMU_I7SET`
6, `MMU_NSET+I7SET` 13, `MMU_I7WAY` 30, and `L2_I7SET` 6, `L2_NSET+I7SET` = (21−3−6)+6 = 18,
`L2_I7WAY` 29. So loop A is the **L1's** geometry and loop B is the **L2's**, and Apple's own naming
(`clean_dcacheline` / `clean_l2dcacheline`, kept in the linked symbol table) says which is which.

**Two loops with two geometries do two different things only if a level is selected between them.** That
is what the pair is *for*; there is no other reading of the structure.

## 2. And nothing in the image selects one

`CSSELR` is written by `mcr p15, 2, Rt, c0, c0, 0` (`machine_cpuid.c:88`). Over the whole linked entry
image there are exactly **two** such instructions:

| site | in | value written | when |
| --- | --- | --- | --- |
| `0x80022eb0` | `machine_write_csselr` (`0x80022eac`) | `r0 = r1 \| r0`, the caller's `level\|type` | from `do_cacheid` only (`cpuid.c:224/262/265`) |
| `0x8000497c` | `entry_epilogue` (`0x80004948`), this project's terminal report path | `r3 = #0` → **L1** | at the *end* of a run, to read the geometry for the report |

Neither is a cache routine, and neither is on the path *into* one. `do_cacheid` writes L1 first, reads
`CCSIDR`, then writes **L2** (or L3) and reads `CCSIDR` again - **and never selects L1 back**. So after
`do_cacheid` the level is L2 for the rest of the boot, and the two loops of every later `CleanPoC_Dcache`
/ `FlushPoC_Dcache` run with `CSSELR = 2`.

Confirmed independently by the live capture, which is a third instrument and the only one that is a
reading of the hardware rather than of the code: 520's log carries `xnu_entry_csselr_before=0x00000002`,
read at the top of `entry_epilogue` - i.e. at the moment the run died, with nothing between `do_cacheid`
and that point writing `CSSELR`. **L2.**

## 3. The claim that holds either way

I am not going to settle from memory whether ARMv7's set/way D-cache operations take their level from
`CSSELR`. The reading does not need it:

- **If they do** (which is what Apple's paired loops say Apple believed, and what the `CSSELR` write
  sites in `cpuid.c` are for): both of `CleanPoC_Dcache`'s loops run on the **L2**, loop A sweeping it
  with the L1's geometry - 128 sets × 4 ways = **512 of the L2's 16384 (way, set) pairs**, 3.1% - and
  loop B covering it fully, because the L2's *compile-time* geometry over-iterates. **No set/way
  operation in this image can reach the L1 at all.**
- **If they do not**: loop B is dead code and the **L2** is never maintained, by anything, ever.

Either way a pair cannot be doing what it was written for, and the image contains no instruction that
could change that. Stated as the thing I can defend: **the two geometries are in the image and no
`CSSELR` write is between them.**

## 4. What that says about the flushes this phase has already run

This is where it stops being an architectural note. The routines the phase has actually relied on:

| the call | where | what it maintains |
| --- | --- | --- |
| `FlushPoU_Dcache` @ `0x800462d8` | `platform_cache_idle_exit`'s **first instruction pair after the `push`** - the one call inside the symmetric push/pop pair | **loop A only** → 512 L2 pairs, 3.1% of it, and no L1 line |
| 516's `CleanPoC_Dcache` | `__wrap_platform_cache_idle_enter`, before the window | loops A (on the L2, 3.1%) **and** B (the L2, fully) - **no L1** |
| 517/521's `FlushPoC_Dcache` | the same wrapper site, behind `STAGE90_XNU_EXIT_POC_FLUSH` | loops A and B - **no L1** |

So `FlushPoU_Dcache` is confirmed to be the weak one 534 named - it is the only routine here with a
single loop, and that loop is the L1's geometry pointed at the L2 - and 534's conclusion is *confirmed*
rather than corrected (I count 512 pairs where 534 counted 256; the two orders agree and the two
conclusions, "a fraction of the L2" and "no L1 line", are the same).

**But it also says something 534 did not: the L1 is not the gap in that one call, it is the gap in
every cache operation this image makes.** 516's write-back before the window and 521's PoC flush are
both *two-loop* routines - the strongest maintenance the image has - and under the first reading above
neither of them touches a single L1 line either. That matters for what "the stale line survives" means:
the push the fatal `pop` reads over is a **stack** line, the hottest kind of line in the L1, and no
sequence the project has run - including the two it built specifically to fix this - can invalidate it.

## 5. What this changes for the next arm

534's arm was "wrap `FlushPoU_Dcache`, because its L2 sweep is partial and it reaches no L1 line". Both
halves are right, and the second half is now larger than it was: **the seam has to be one from which a
level can be selected, and the sweep has to select each level and use that level's geometry** - i.e. for
each of L1 and L2: write `CSSELR`, read `CCSIDR`, walk `(NumSets, Assoc, LineSize)` **from the register
and not from `proc_reg.h`**. Two further consequences of §1, both decision-relevant:

1. **The compile-time geometry is not the hardware's, in two of three dimensions on the L1 and two of
   three on the L2.** `ccsidr_l1=0xa007e01a` decodes to **64 sets / 4 ways / 64 B** (16 KB) where
   `ARMA7`'s `MMU_CSIZE` says 32 KB and the loop sweeps 128 sets. `ccsidr_before=0xf0ffe03b` decodes to
   **2048 sets / 8 ways / 128 B** (2 MB) where `__ARM_L2CACHE_SIZE_LOG__=21` with `L2_CLINE=6` implies
   4096 sets of 64 B. The two agree on the total and on nothing else, which is exactly why loop B covers
   the L2 anyway (over-iterating sets is harmless) and why a correct sweep has to read the register.
   **An arm that sweeps "the same shape, more of it" would be reading a layout instead of the
   expression** - measurement defect 405's tell.
2. **The seam 534 picked is still the right one** on the merits: it is inside the symmetric pair, before
   both state changes (the `ACTLR` write at `0x8004630c` and `SCTLR.C` at `0x8004631c`), it is reached in
   every pass, and it is a `bl` - so it is wrappable, per [[mi4-hooks-live-at-calls]].

## 6. What this does not decide

- **Which level actually holds the stale line.** Both can; the reading says neither is maintained
  correctly, not which one the `pop` hit. A sweep that selects both levels is right either way, which is
  why it does not have to be decided first.
- **Whether the L1 line is the mechanism at all.** 534's mechanism remains a hypothesis with strong
  indirect support (the popped value is a counter reading and the fault's `pc` is that value `- 1`). This
  step narrows what a fix must do; it does not prove the fix will work.
- **The arm to build.** 533 is frozen and unrun and decides it - so nothing here is built, and 535 is
  still a design rather than an artifact.
- **The storage line**, unchanged from 530-532, and TWRP, still withheld.

## 7. Safety

No device action in this step: `objdump` and `grep` over `out/stage90/xnu_arm_entry.elf`, the XNU
source, and the build flags. Nothing written outside `docs/`, no build run, no file under `out/`
touched, and the frozen pair is untouched - `out/stage90/stage90-qcdt.img` =
`1daaf44e624563694e…`, `out/stage90/xnu_arm_entry.bin` = `f202f2465886aba6…`. The device is off the bus
and owes a power press before 533 can run.
