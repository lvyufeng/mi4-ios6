# 545: the level is in the operand, so the L1 loop is the L1

**544 is wrong where it matters, and the disproof is two instructions before the second loop of each
routine it was reading.** 544 concluded that the cache level for a set/way maintenance operation comes
from `CSSELR`, that nothing in this image writes `CSSELR` on the way into a cache routine, and therefore
that "the L1 is not the gap in that one call, it is the gap in every cache operation this image makes".
The level travels **in the set/way operand itself**, encoded exactly as `CSSELR` encodes it, and Apple
writes it at the head of every second loop: `mov r0, #2`.

This is a reading of the same linked image, plus the vendor kernel in the same repository; no device, no
build, nothing in `out/` touched. §4 lists what it overturns in 544, §5 what the corrected picture says
about the exit's flush, and §6 records one observation on that same seam which the correction makes
visible and which no level-correct sweep can repair.

## 1. The instruction, in the frozen image

The four routines 544 measured, disassembled from `out/stage90/xnu_arm_entry.elf` (`f202f2465886aba…`).
Every one of them begins by putting a **level** into the register that is then used as the set/way
operand for the whole loop, incrementing only geometry fields afterwards:

| routine | entry | the level init | the loop's first `mcr` | second loop? |
| --- | --- | --- | --- | --- |
| `CleanPoC_Dcache` | `0x8004575c` | `mov r0, #0` @ `0x8004575c` | `0x80045760` | **yes** - `mov r0, #2` @ `0x80045780`, loop @ `0x80045784` |
| `CleanPoU_Dcache` | `0x800457a8` | `mov r0, #0` @ `0x800457a8` | `0x800457ac` | no |
| `FlushPoC_Dcache` | `0x80045828` | `mov r0, #0` @ `0x80045828` | `0x8004582c` | **yes** - `mov r0, #2` @ `0x8004584c`, loop @ `0x80045850` |
| `FlushPoU_Dcache` | `0x80045874` | `mov r0, #0` @ `0x80045874` | `0x80045878` | no |

`0` is the L1 and `2` is the L2, and 2 is not an arbitrary tag: it is the `CSSELR` encoding of L2
(`CSSELR`'s level field is bits [3:1], 0 = L1, 1 = L2), which is the same value the same encoding
appears as in the second witness below. The two loops of a PoC routine differ in exactly three ways -
the way-increment bit (`1<<30` vs `1<<29`), the set-count overflow (`0x2000` vs `0x40000`), and this
init - and the first two are geometry for a 4-way L1 and an 8-way L2. **Only the init can be naming the
cache.**

## 2. And the same in the source, across all four routines

`external/xnu-4570.1.46/osfmk/arm/caches_asm.s` - the file the image's bytes were assembled from, so it
explains the instruction rather than standing in for it:

| routine | source | `mov r0, #0` | `mov r0, #2` |
| --- | --- | --- | --- |
| `CleanPoC_Dcache` | `:129` | `:132` | `:145`, after a `dsb` at `:144` |
| `CleanPoU_Dcache` | `:167` | `:169` | - |
| `FlushPoC_Dcache` | `:241` | `:242` | `:254`, after a `dsb` at `:253` |
| `FlushPoU_Dcache` | `:276` | `:277` | - |

The split is by *name*, and it is the one the names predict: **every `PoC` routine (point of coherency -
main memory, past the L2) has the `#2` and every `PoU` routine (point of unification - the L1) does
not.** That is what "to the point of unification" means as an instruction count, and it is a second,
independent statement of the same fact: the operand's low bits select the cache the loop walks.

## 3. Why it cannot be anything else, and the third witness

**It cannot be a set number.** If `2` were a starting set (the set field begins at bit 6 here, and the
increment is 64), the second loop would sweep sets 2…4095 of each way and **never sets 0 and 1** - a gap
no author would leave, and one with nothing to recommend it, since the L1 loop starts at set 0. As a
level it is exact.

**And a second implementation in this repository encodes the same thing the same way.** The vendor
kernel's `v7_flush_dcache_all`
(`external/android_kernel_xiaomi_cancro/arch/arm/mm/cache-v7.S:61-113`) walks the levels of `CLIDR` and,
per level, does both of these in the same iteration:

```
 78:	mcr	p15, 2, r10, c0, c0, 0		@ select current cache level in cssr
 80:	mrc	p15, 1, r1, c0, c0, 0		@ read the new csidr
 94:	orr	r11, r10, r9, lsl r5		@ factor way and cache number into r11
...
111:	mcr	p15, 2, r10, c0, c0, 0		@ select current cache level in cssr   (back to level 0)
```

`r10` is the only level register in that routine, it starts at 0 and steps by 2 (so 0, 2, 4 = L1, L2,
L3), and it is used in **both** places: written to `CSSELR` for the `CCSIDR` read *and* ORed into the
set/way operand. Two independent implementations - Apple's and the ARM reference code - put the level
in the operand's low bits with the same encoding, and Apple's has no `CSSELR` write anywhere near it.
The architectural half that stays open is only whether *Krait* honours those bits (§7).

## 4. What this overturns in 544

| 544's claim | now |
| --- | --- |
| §2/§3: after `do_cacheid` the level is L2 "for the rest of the boot", so every later PoC routine runs both loops on the L2 | true of `CSSELR` and **irrelevant to these routines**, which never read it; the routine's own loop head is the selector |
| §3 branch 1: "both of `CleanPoC_Dcache`'s loops run on the L2 … no set/way operation in this image can reach the L1 at all" | **false**: loop A is level 0 = the L1, with the L1's geometry |
| §3 branch 2: "loop B is dead code and the L2 is never maintained" | **false**: loop B is level 2 = the L2 |
| §4: "the L1 is not the gap in that one call, it is the gap in every cache operation this image makes … no sequence the project has run can invalidate it" | **false**: every PoC routine sweeps the L1 and the PoU routines sweep the L1, and nothing in the image sweeps it incorrectly |

**What survives from 544 is the part 544 itself called the useful part.** The geometries are still the
compile-time ones and still differ from the hardware's: the captured `ccsidr_l1` is 64 sets / 4 ways /
64 B where `ARMA7`'s loops sweep 128 sets / 4 ways / 64 B, and `ccsidr_before` is 2048 sets / 8 ways /
128 B where the L2 loop sweeps 4096 sets / 8 ways / 64 B. Both mismatches are *over*-iteration in the
dimension that does not matter - and the associativity is right in both cases (4 and 8 ways), which is
the field the way bit has to match and which the hardware confirms. So the corrected statement of 544
§5.1 is still the one to keep: **a sweep must read `CCSIDR`, not `proc_reg.h`, and a "same shape, more
of it" arm would be reading a layout instead of the expression.** And 534's choice of seam still stands
on §5's grounds below.

## 5. What the corrected picture says about the exit's flush

`platform_cache_idle_exit` (`0x800462d4`) is `push {fp, lr}` / `bl FlushPoU_Dcache`, and that routine is
an **L1 only** clean+invalidate - not because it is broken, but because a PoU flush is defined to stop at
the L1. The two strong routines the phase has already run:

| the call | level 0 (L1) | level 2 (L2) |
| --- | --- | --- |
| the exit's `FlushPoU_Dcache` | swept, 128×4 | **not reached, by design** |
| 516's `CleanPoC_Dcache` | swept | swept |
| 517/521's `FlushPoC_Dcache` | swept | swept |

So 534's conclusion ("the PoU call is the weak seam") survives, and its *reason* is now the opposite of
the one it gave: not "it reaches no L1 line", but "it is L1-only, because that is what PoU is". An arm
that wraps this seam does not need to repair a sweep - it needs to put a **PoC** flush where Apple put a
PoU one, which is a statement about coverage and not about geometry.

## 6. One thing the correction makes visible, and it is not a level problem

The image has **no barrier between the push and the sweep**:

```
800462d4 <platform_cache_idle_exit>:
800462d4:	e92d4800 	push	{fp, lr}
800462d8:	ebfffd65 	bl	80045874 <FlushPoU_Dcache>
```
and the routine being called puts its barrier at the **end** (`dsb sy`, then `bx lr`), as do all four -
its first instruction is the `mov r0, #0` above. `caches_asm.s` contains **no `dmb` at all** (grep count
0).

The vendor kernel's flusher opens with one, with the reason in the comment:

```
 63:	dmb					@ ensure ordering with previous memory accesses
```

If prior stores have to be made visible to a set/way sweep for the sweep to cover them, then this
sequence lets the push's two stores sit in the store buffer while the L1 sweep walks past the line they
belong to: the line is then left **dirty in the L1** by the `dsb` at the end of the routine (which
drains the buffer but does not clean anything), and a dirty line in a cache that the idle window is
about to turn off is exactly the data that never reaches memory. The `pop` at the window's end would
then read a memory line the push never wrote.

**This is a hypothesis and it is labelled as one.** What is a reading is the asymmetry between the two
implementations in this repository and the absence of any barrier in the window between the push and the
first `mcr`. What is not settled by anything here is whether the architecture requires that ordering,
and whether Krait's store buffer makes it bite for a push four instructions before the sweep. It is
named now rather than after 535 is designed for one reason: **no level-correct sweep and no amount of
coverage can repair it**, so an arm that fixes the level question and nothing else could come back with
the same death and be read as "the flush was never the problem".

## 7. What this does not decide

- **Which cache holds the stale line the `pop` reads over**, and whether the death is a cache matter at
  all. Both levels are now known to be swept by the routines that were supposed to sweep them; that
  removes one *hypothesis* (a broken sweep) and adds one (§6), and it does not identify the mechanism.
- **The Krait-specific half.** That the operand's spare bits name a level is established for the two
  implementations above; that *this* core honours them for a set/way operation is not - the run's own
  captures prove only that `CSSELR` selects the cache whose `CCSIDR` is read. So a sweep for the next arm
  should still select each level explicitly and take its geometry from `CCSIDR`.
- **533's arm.** Nothing here is built: the frozen pair is `1daaf44e624563694e…` (boot image,
  `./build.sh` not run) and `f202f2465886aba6…` (entry image), and the run is still owed a power press.
  533's null instrument is unaffected by anything in this document - which is the point of running it
  first.
- **535**, still a design and not an artifact, and now with a second dimension in it (§5's coverage,
  §6's ordering) that 534 did not have.
- **The storage line and TWRP**, unchanged: still withheld.

## 8. Safety

No device action in this step. `objdump` and `grep` over `out/stage90/xnu_arm_entry.elf`, the XNU
sources in `external/`, and the vendor kernel tree; two `grep`s over the memory directory. Nothing
written outside `docs/` and the memory files, no build run, no file under `out/` touched, `flash` not
used, and the frozen pair is untouched (`1daaf44e624563694e…` / `f202f2465886aba6…`). The device is off
the bus and owes a power press before 533 can run.
