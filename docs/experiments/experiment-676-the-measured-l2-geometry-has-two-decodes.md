# 676: the measured L2 geometry has two decodes in this tree — and the arm that follows has an in-tree reference

Host-side and read-only. 658 §2 designed the arm the L2 cell needs and named three things it would have to
publish. This step went looking for the numbers that arm would sweep with, and found two of them already
measured — and the same measured register value carrying **two different decodes** in this tree, the wrong one
in the frozen image's own source comment and in `cache_ops.c`, the right one in 544's doc. It also read the
reference implementation 658 §2 cites, which turns out to be a complete in-tree specification for the arm.

Nothing was built, nothing was sent, no device was addressed, and **no file in 660 §5's closure was edited** —
every reading below is `grep`/`awk`/`python3` over the tree and over three archived captures.

## 1. The measured triple, from three runs, and the decode the fields actually give

`entry_epilogue` publishes the cache-selection state at the top of every report (`entry_stubs.c:4202-4204`), so
it is in every capture that reached the report path. Read out of the three archived runs that carry it — 520
(2026-09-22), 533 (2026-09-23) and 650 (the 574 park, 2026-09-24) — **all three print the same three values**:

```
xnu_entry_csselr_before = 0x00000002
xnu_entry_ccsidr_before = 0xf0ffe03b
xnu_entry_ccsidr_l1     = 0xa007e01a
```

Decoded with the ARMv7-A `CCSIDR` layout (`LineSize` bits[2:0], `Associativity` bits[12:3], `NumSets`
bits[27:13], each field "minus one", line size = 2^(LineSize+4)):

| value | line | ways | sets | total |
| --- | --- | --- | --- | --- |
| `ccsidr_l1` | 64 B | 4 | 64 | **16 KiB** |
| `ccsidr_before` | 128 B | 8 | **2048** | **2 MiB** |

**The layout is fixed by the second row, not assumed from the first.** The code writes `CSSELR ← 0`
(`entry_stubs.c:3578`, labelled "level 1 data") and then reads `CCSIDR`; 64 sets / 4 ways / 64 B is a
16 KiB 4-way L1, which is a coherent L1 description where the *first* row's 2 MiB is not — so the field
boundaries that produce the second row are the right ones, and the same boundaries then give the first row.
(This is a fix on where the boundaries are and not a claim about the core's L1 size: 544 §2 measured the same
value and found the compile-time `ARMA7`/`MMU_CSIZE` says **32 KB** and sweeps 128 sets, so the tree's own
constant disagrees with the register here too. The point is only which bits belong to which field.)

**And the `CSSELR` value is a constant in the kernel, not an inference.** `cpuid_internal.h:43-45` is
`CSSELR_L1 = 0x0`, `CSSELR_L2 = 0x2`, `CSSELR_L3 = 0x4`, and `machine_write_csselr` writes `level | type`
(`machine_cpuid.c:87-88`, `CSSELR_DATA_UNIFIED = 0x0`). So the measured `0x2` **is** `CSSELR_L2`, and the
`Level` field is 0-based (`0x0` = L1) — which is what makes `entry_stubs.c:3578`'s `0u` deserve its "level 1
data" label and what makes 195's reading of the value correct.

## 2. Two decodes of one measured value, and only one of them is a decode

| carrier | what it says `0xf0ffe03b` is |
| --- | --- |
| `entry_stubs.c:3566` (in the image) | *"decodes to **4096 sets** of 8 ways of 128-byte lines, **four megabytes**"* |
| `cache_ops.c:51` | *"a **4096-set**, 8-way, 128-byte description (**4 MB**)"* |
| `docs/experiments/experiment-195-…md:159` | *"**4096 sets** x 8 ways x 128 B = **4 MB**"* — the source of the other two |
| `docs/experiments/experiment-544-…md:110-112` | *"decodes to **2048 sets** / 8 ways / 128 B (**2 MB**) where `__ARM_L2CACHE_SIZE_LOG__=21` with `L2_CLINE=6` implies **4096 sets of 64 B**"* |

544 has it right and names where the other number comes from: **4096 is the compile-time L2 geometry**
(`__ARM_L2CACHE_SIZE_LOG__=21`, `L2_CLINE=6` → 4096 sets of 64 B), *not* a decode of the register. The
wrong number is a real quantity from a different source, presented in `entry_stubs.c` as what *this* register
value "decodes to". That is [[mi4-one-value-two-definitions]] with the two definitions one file apart, and it is
worth naming exactly: **`NumSets` for `0xf0ffe03b` is 2047, so the set count is 2048.** No shift or mask of that
word yields 4096 (the nearest candidate, bits[27:12], gives 4095) — so it is not a boundary misread, it is a
number from the compile-time layout wearing the measured value's name.

Two independent corroborations that 2 MiB is the hardware and 4 MB is not:
* **XNU's own source says it.** `cpuid.c:275`, inside the very function that leaves `CSSELR` where it is:
  *"capri has a **2MB L2 cache** unlike every other SoC up to this point with a 1MB L2 cache"* — and this device
  is capri (MSM8974).
* **The arithmetic the kernel does with it is consistent.** `cpuid.c:289` computes
  `vm_cache_geometry_colors = (NumSets + 1) * c_linesz / PAGE_SIZE`, read under `CSSELR_L2`: 2048 × 128 / 4096 =
  **64**, an integer. 4096 sets would be 128.

**What this does and does not change.** It changes no behaviour: the geometry the code actually sweeps comes from
`cache_dcache_geometry` (`cache_ops.c:54-58`), which *writes `CSSELR` and reads `CCSIDR` at run time* — the
comment above it is wrong and the code is right. What it changes is what a reader takes away, and the reader who
matters is the next arm's builder: 544 §2's rule is *"walk `(NumSets, Assoc, LineSize)` from the register and not
from `proc_reg.h`"*, and an arm built from the comment's numbers would sweep 4096 sets of a 2048-set cache.

## 3. 534's claim, confirmed by measurement rather than by reading

534 found that the whole XNU tree has one `CSSELR` writer and it never selects L1 back. Both halves are now
measured:

* `cpuid.c:224` writes `CSSELR_L1`, reads `CCSIDR`, and `cpuid.c:265` writes `CSSELR_L2` for the second read —
  and `do_cacheid` ends at `:300` with **no third write**, so the register is left at the L2.
* And that is exactly what the device reports: `xnu_entry_csselr_before = 0x00000002 = CSSELR_L2`, in three runs,
  read by this project's own image before it writes the register itself.

So the ambient `CSSELR` the seam arm must save and restore is **predicted to be `0x2`** on this device, with the
writer named (`machine_write_csselr` at `0x80022eb0`, reached from `cpuid.c:265`). A restore that puts back
anything else is [[mi4-one-value-two-definitions]] in a register, which is what 658 §2 already said the
requirement was for — this gives the value it must put back.

## 4. The arm's reference is in-tree, and reading it adds two things 658 §2 did not say

658 §2 cites `external/android_kernel_xiaomi_cancro/arch/arm/mm/cache-v7.S`'s `v7_flush_dcache_all`. Read
verbatim this step (`:62-115`), and it is the whole design:

| line | what it does |
| --- | --- |
| `:64` | `mrc p15, 1, r0, c0, c0, 1` — read **CLIDR** |
| `:65-67` | extract **LoC** (bits[26:24], `0x7000000 >> 23`) → the walk's bound |
| `:73-74` | skip a level whose Ctype < 2 (no cache, or i-cache only) |
| `:78` | `mcr p15, 2, r10, c0, c0, 0` — **write CSSELR = this level** |
| `:79-80` | `isb`, then read **CCSIDR** for that level's geometry |
| `:84-90` | line size, and the two field masks — **`0x3ff` and `0x7fff`, with no `+1`** |
| `:100` | `mcr p15, 0, r11, c7, c14, 2` — `DCCISW` by set/way |
| `:110-111` | **switch back to level 0** (`CSSELR ← 0`) before returning |

Two additions to 658 §2, both decision-relevant:

1. **The level count is `LoC` out of CLIDR, not a constant.** 658 §2 said the arm needs "the level count `CLIDR`
   reported"; the reference shows the arm should read it for itself rather than trust a published copy, which is
   the same rule as 544's (read the register, not a layout). **And CLIDR is still genuinely absent from this
   arm** — measured with the path named: `CLIDR` occurs in `stages/stage90/xnu_arm_boot/` exactly once, as a
   *word in a comment* (`build_entry.sh:1127`), and nowhere as an instruction; the control (`CCSIDR`, which must
   be present) returns `entry_stubs.c` and `build_entry.sh`, so the extractor could see this directory.
2. **The reference does not over-iterate, and it restores the level.** It sweeps exactly the register's counts
   (the missing `+1` on both masks), where this tree's own loops sweep compile-time geometry — and its final
   `CSSELR ← 0` is the restore 658 §2 asked for, with a reference that actually does it.

So of the three cells 658 §2 called absent, **two are already in the log** (`CSSELR`, and the L2 geometry itself —
2048/8/128, which the ambient read *is* evidence for, since reading `CCSIDR` with an L2-selecting `CSSELR`
returned a valid L2 description), and only **CLIDR** is missing. That shrinks the arm; it does not change its
shape.

## 5. One claim of 544's this step did not confirm, and names as open

544 §2's parenthetical — *"(over-iterating sets is harmless)"* — is a statement about ARMv7's `DCCISW`, not about
this tree, and nothing measured here supports it. The evidence in hand points the other way: **the only
architecturally-sanctioned implementation in this tree is the one that does not over-iterate** (§4), and XNU's own
loops over-iterate only because they use compile-time geometry instead of the register. The survival record — the
over-iterating loop has run on this device in every run without an observable fault — is a record, not a
semantics. **Named open because an L2 arm must not inherit an unmeasured "harmless"**: if the arm copies
`cache-v7.S` it inherits the exact sweep and the question never arises; if it copies this tree's shape it does.

## 6. What waits, and on what

* **The two comments are inside the closure and are not edited here.** `entry_stubs.c` is under
  `stages/stage90/xnu_arm_boot/**`, which the gate hashes against the manifest, so an unbuilt edit makes the gate
  refuse the armed press (673 §3, re-measured in 675). `cache_ops.c` is not in that directory — and it is still
  recorded rather than edited, because a change to the payload's sources is a change to a payload **build**, and
  a build under an armed launcher replaces the bytes the press sends (660 §5).
* **So both are for the next build, in the same sitting as 674 §3's requirement and 675 §1's cross-lane key
  pair** — which is now a four-item list for one build, and that is the point of doing this while the press is
  owed rather than after it.
* **It does not re-open the press order** (674 §4's rule). Which arm that build produces is still decided by the
  press's own reading, which is still owed.

## 7. Safety, and what this does not do

Read-only throughout: `grep`, `awk`, `python3` and `Read` over the tree, and `grep` over three archived captures
under `out/stage90/captures/` (no write to `out/`). **No build, no byte written under `out/`, no edit to
`xnu_arm_boot/**`, `cache_ops.c` or `preflight_boot_check.sh`, no gate or runner invocation, no `fastboot`, no
boot, nothing written to storage, the neighbour `33e80afe` untouched.** The armed launcher (pid **426955**,
deadline **13:21:30 UTC**) was left alone: a `ps` of its pid and a `tail` of its log.

**It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** No boot and no new device reading: every number
above was already in captures taken on 2026-09-22/23/24. The frontier is where 652 left it — XNU reaches pid 1,
runs the userland phase, dies at the idle exit's `pop {fp, pc}` — and this step makes the arm that addresses that
`pop` smaller, safer and better specified without being the answer. **TWRP-to-storage stays withheld**, because
「如果os已经能进去了的话」 is unmet.
