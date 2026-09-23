# 578: the operation's two halves, and the routine whose name lies

A **pre-registration for the arm after this one**, written while the device is off the bus and the arm
built for it (574) waits on a power press. Its purpose is to remove the design round from the critical
path: if 574's run comes back and says the operation cost 535's return, the next arm's shape should
already be decided and its one hazard already measured. Nothing is built here, nothing runs, no arm is
changed, and the frozen 574 artifacts are untouched.

Two things are established below, both read out of the linked image and from logs already parked in
`out/stage90/captures/`: **the operation 535 used is a single instruction pair whose clean half is a
whole line** (572 section 5's first candidate), and **there is no invalidate-only-by-MVA routine in this
image to reach for** - including one whose name says there is.

## 1. The census: what this image can and cannot do to a line, by opcode

Every data-cache maintenance site in `out/stage90/xnu_arm_entry.elf`, counted by opcode (measured; the
counts are sites, not names):

| opcode | meaning | sites | where |
| --- | --- | --- | --- |
| `mcr 15,0,rX,cr7,cr10,{1}` | DCCMVAC - clean by MVA **to PoC** | 5 | `CleanPoC_DcacheRegion` (`0x800457fc`, also spelled `_Force`) among them |
| `mcr 15,0,rX,cr7,cr14,{1}` | DCCIMVAC - **clean and** invalidate by MVA | **2** | `FlushPoC_DcacheRegion` (`0x800458b0`) **and `invalidate_mmu_dcache_region` (`0x80045710`)** |
| `mcr 15,0,rX,cr7,cr11,{1}` | DCCMVAU - clean by MVA to PoU | 1 | `CleanPoU_DcacheRegion` (`0x800457e4`) |
| `mcr 15,0,r0,cr7,cr6,{0}` | whole-cache invalidate | 1 | `invalidate_mmu_dcache` (`0x800456f4`) |
| **`mcr 15,0,rX,cr7,cr6,{1}`** | **DCIMVAC - invalidate by MVA, no clean** | **0** | **absent from this image** |

**The finding is the second and the last row together.** Apple's function named
`invalidate_mmu_dcache_region` is a **clean-and-invalidate**: its loop body is the same
`cr7, cr14, {1}` as `FlushPoC_DcacheRegion`'s, over the same `and r2,r0,#63` / `bic` / `lsr r1,r1,#6`
64-byte walk. An arm that reached for it *because of its name* - the obvious way to say "make the pop
miss without writing anything back" - would get exactly the clean half it is trying to avoid, and every
surface (the call's name, the file's name, the reader's narration) would say "invalidate".

This is this project's name-versus-body defect class, and here it is measured rather than suspected:
**the only invalidate-by-MVA the image contains is a whole-cache one, and the two routines that walk a
region both clean first.**

## 2. Why one DCCIMVAC is the wrong shape, in the arm's own arithmetic

`FlushPoC_DcacheRegion(slot, 8)` walks **whole 64-byte lines** - that is 572 section 5's first candidate,
and 574's design reasoned about "the slot's eight bytes" while the call is defined over a line. The
consequences, in the direction that matters:

- **The clean half writes the cache's copy of all sixteen words to memory.** For the two words the arm
  read and restores, that is repaired by the restore. For the other fourteen - the idle thread's own live
  frame on 546 section 1's reading - whatever the window wrote to them is overwritten by the stale cache
  copy. 546 section 3's whole premise is that this line is the idle thread's frame.
- **The invalidate half is what the arm actually needs**, because the `pop`'s load *is* cacheable: Apple
  re-enables `SCTLR.C` at `0x8004631c-24`, twenty-four bytes before the `pop` at `0x8004633c`. So the
  pop's load must *miss* to read memory, and a line left valid after a clean will hit with the stale
  contents.

So the operation is one instruction where the arm needs half of it, at two different times.

## 3. The shape the next arm would take, if 574's run names the operation

**Split the two halves and put each where it is safe:**

1. **Clean before the push** - at the exit wrapper's top, next to the position 517's flag occupies, i.e.
   *before* `__real_platform_cache_idle_exit()` and therefore before the real exit's `push {fp, lr}`. At
   that instant the line's cache copy is the pre-window authority and memory may hold something older
   (the line can be dirty from earlier passes through the same frame), so a clean is exactly what makes
   memory agree for all sixteen words - **and after it, nothing on the line is dirty**, which is what
   makes step 2 safe.
2. **Invalidate-only at the seam** - inside the exit, at the hook 574 already has (the call returning to
   `0x800462dc`), with the clean removed. The push has by then written the two words to memory (the
   window's stores are DRAM-only), the line is clean because step 1 cleaned it, and dropping it makes the
   pop miss and read the push's own values. The neighbours lose nothing, because step 1 already wrote
   their cache copy out.

**And step 2 needs an instruction this image does not contain.** There is no DCIMVAC site to call
(section 1), so that arm would open-code `mcr p15,0,rX,cr7,cr6,{1}` over the line - one instruction, with
the loop's other three lines being the walk that already exists in two routines. That is not a "second
definition" of an existing operation; it is the first definition of one this image lacks, and the
project's rule about second definitions does not apply, but **the rule about names does**: the build
clause would have to assert the *opcode's* absence in every routine it did **not** call, or an arm that
called `invalidate_mmu_dcache_region` by name would look identical from every surface while quietly
cleaning.

## 4. What would falsify this design before it is built

- **574 returns and the pair is unchanged** (`_a0 == _b0`, `_a1 == _b1`): nothing was written back, so
  the line was not dirty and a clean has nothing to protect - the split's step 1 is then unnecessary and
  the candidate moves to the L2-domain questions (572 section 5, candidate (b)).
- **574 does not return**: the interception itself is implicated (572 section 6) and neither this design
  nor any other operation-shaped arm is reachable until that is settled.
- **574 returns and the death is at the `pop` with the pair unchanged *and* `op` absent**: 574 section 3's
  third branch - a log that does not say which arm it was - and this design waits for a run that does.

## 5. Safety

No device action, no build, no edit to any arm: one disassembly census of an image already parked and one
reading of logs already parked. The device is off the bus waiting for the power press, the gate is green
on the frozen arm (`EXIT=0`, 0 UNREAD), and nothing here changes either. TWRP stays withheld:
「如果os已经能进去了的话」 is unmet (572 section 8, 574 section 6).
