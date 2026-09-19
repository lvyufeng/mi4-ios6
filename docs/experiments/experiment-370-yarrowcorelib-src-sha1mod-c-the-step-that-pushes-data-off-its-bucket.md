# Experiment 370 — `YarrowCoreLib/src/sha1mod.c`: the SHA-1 the FIPS layer waits for, and the step that pushes `.data` off its bucket

**Step:** link one object, `osfmk/prng/YarrowCoreLib/src/sha1mod.c`
(`osfmk_prng_YarrowCoreLib_src_sha1mod.o`) — the pool definer of 369's stop `YSHA1Init`. It is inserted
between `osfmk_prng_YarrowCoreLib_port_smf.o` and `stages/stage90/xnu_platform/MSM8974PlatformExpert.o`.
Nothing else changes.

**Prediction:** *3 resolved (3 function) / 0 added — 786 → **783** undefined, 684 → **681** function,
102 → **102** storage*; object `.text` **0x8017C6A0** (16-aligned, a 0xC fill); `.data` moved a whole
`ALIGN(0x4000)` to **0x801AC000**; and the stop at **`comp_init`** at key **`0x8017BF7C`**.

**Result:** counts exact, every `.text` placement exact, **the `.data` move and all four rows it drags
exact**, the stop's name and key exact with no falsifier firing — and four misses, three of them in the
block's own arithmetic.

## The object, and why nothing in it can stop the run

```
resolved (3: 3 function, 0 storage)
    YSHA1Final   YSHA1Init   YSHA1Update
added (0: 0 function, 0 storage)
of the 2 references, 2 are already satisfied
```

The two references are `memcpy` and `memset`, both real. **This is the first object in the walk whose
entire closure is already satisfied** — once it is linked, every call it makes is real, so nothing inside
it can stop the run, and the frontier moves eight calls further on.

It is also the largest single step of the last twenty: `.text` **0x14AC** for four definitions —
`YSHA1Transform` 0x12A4, `YSHA1Init` 0x40, `YSHA1Update` 0xAC, `YSHA1Final` 0x110 — plus a `.bss` of
**0x40** (`YSHA1Transform.workspace`, a 2×16 aligned array) and a 2-byte `.rodata` with a 2-byte
`.rodata.str1.1`.

`YSHA1Init` is the one that matters for safety, because the run enters it and it contains a *faulting*
instruction if its operand is not aligned:

```
12b0: add r2, pc, #40
12b8: vld1.64 {d16-d17}, [r2 :128]   <- a 128-BIT ALIGNED load of the SHA-1 IV
12c8: vst1.32 {d16-d17}, [r0]!
12d4: bx lr                          <- no call at all
12e0: .word 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476
```

The constant is at object `.text + 0x12E0`, so whether this step runs at all depends on where the input
is placed — which is what makes the alignment row below a functional row and not bookkeeping.

## The stop, eight calls further on

All three `YSHA1*` names retire at once and this object creates nothing, so the run walks through the
SHA-1 code and stops at the next stub on `prngInitialize`'s path. The keys inside `prngInitialize` are
**identical to 369's**, because `prng.o` sits before the inserted object and does not move:

```
+0x58 bl YSHA1Init    key 0x8017BEF4   now REAL (369's stop, passed through)
+0x64 bl YSHA1Init    key 0x8017BF00   real
+0x74 bl YSHA1Update  key 0x8017BF10   real
+0x88 bl YSHA1Update  key 0x8017BF24   real
+0x94 bl YSHA1Final   key 0x8017BF30   real
+0xa8 bl memcpy       -                real
+0xb4 bl YSHA1Init    key 0x8017BF50   real
+0xc4 bl YSHA1Update  key 0x8017BF60   real
+0xd0 bl YSHA1Final   key 0x8017BF6C   real
+0xe0 bl comp_init    key 0x8017BF7C   <- THE STOP (YarrowCoreLib/src/comp.c)
```

## The layout, and the tool defect this step is the first to expose

The object's `.text` is **16-byte aligned** in the object file (`2**4`); every object this walk has
linked so far has been 4-aligned (`2**2`). That exposed a defect in `predict_layout.py`:

```
python3 tools/predict_layout.py --insert out/.../sha1mod.o:out/.../smf.o
  -> 0x8017c694      WRONG - the object is 16-aligned, and 0x8017C694 is 4 mod 16
python3 tools/predict_layout.py --insert $PWD/out/.../sha1mod.o:$PWD/out/.../smf.o
  -> 0x8017c6a0      right
```

`section_align()` starts `if not path.startswith("/"): return 4` — a **relative** path is not "not found",
it is "not a path I recognise", and the tool answers 4 without saying so. Every earlier invocation in this
walk passed relative paths and every earlier object was 4-aligned, so the default was right by
coincidence for twenty steps. With the object 16-aligned the placement is `align_up(0x8017C694, 16)` =
**0x8017C6A0**, i.e. 0xC of fill — which the build then confirmed.

The `.text` run's delta is `+0x14AC + 0xC − 0x48 = +0x1470` (the `−0x48` being three fewer stub bodies:
`681 × 0x18 = 0x3FD8` against `684 × 0x18 = 0x4020`), and the `.rodata` run's is `−0x24 + 0x4 = −0x20`
(three retired names at 0xC each, two 2-byte inputs added, both alignment 1). So the output section grows
`0x1450` from `0x801A6E40`, closing at **0x801A82A0** under `ALIGN(32)`.

**And that is past `.data`.** `.data` starts at `align_up(text_end, 0x4000)`, so 0x801A82A0 cannot fit
under 0x801A8000 and the section moves a whole `ALIGN(0x4000)`. This is the **second `.data` move in this
walk** (364 was the first) and the first driven by an *overflow* rather than a margin: `0x801A82A0 >
0x801A8000` by 0x2A0, which no rounding can absorb.

## What the build measured

| | 369 measured | 370 predicted | 370 measured |
|---|---|---|---|
| counts | 786 / 684 / 102 | **783 / 681 / 102** | **783 / 681 / 102** |
| object `.text` | — | 0x8017C6A0 (0x14AC) | **0x8017C6A0 (0x14AC)** |
| `realstubs.o` `.text` | 0x8017C840 (0x4020) | 0x8017DCF8 (0x3FD8) | **0x8017DCF8 (0x3FD8)** |
| platform expert `.text` | 0x8017C694 | 0x8017DB4C (0x150) | **0x8017DB4C (0x150)** |
| object `.bss` | — | 0x801F85E4 (0x40) | **0x801FC5E4 (0x40)** |
| `realstubs.o` `.rodata.str1.4` | 0x801A29D0 (0x3AA0) | 0x801A3E38 (0x3A7C) | **0x801A3E44 (0x3A7C)** |
| `.text` end | 0x801A6E40 | 0x801A82A0 | **0x801A8280** |
| text size | 1732160 | 1737376 | **1737344** |
| `.data` | 0x801A8000 (0x19A70) | **0x801AC000 (0x19A70)** | **0x801AC000 (0x19A70)** |
| `.sysctl_set` | 0x801C1A70 (0x150) | 0x801C5A70 (0x150) | **0x801C5A70** (0x150) |
| `.init_array` | 0x801C1BC0 (0x84) | 0x801C5BC0 (0x84) | **0x801C5BC0 (0x84)** |
| its end | 0x801C1C44 | 0x801C5C44 | **0x801C5C44** |
| `.bss` | 0x801C1C80 (0x390D8) | 0x801C5C80 (0x39118) | **0x801C5C80 (0x39118)** |
| `__bss_end` | 0x801FAD58 | 0x801FED98 | **0x801FED98** |
| image | 1842244 | 0x1C5C44 | **1858628 (= 0x1C5C44)** |
| args | +2080768 | +2097152 | **+2097152** |
| topOfKernelData | +3145728 | +4194304 | **+4194304** |
| tree | +5242880 | +6291456 | **+6291456** |
| window | 8388608 | 8388608 | **8388608** |
| headroom | 1069736 | 0x201268 | **2101864 (= 0x201268)** |

The `.bss` pad rule was the ordinary case this time: the object's 0x40 goes in front of the platform
expert's 0x18 and the pad in front of `realstubs.o` goes `(0x04 − 0x40) mod 64 = 0x04` — **unchanged,
because 0x40 is a multiple of 64** — so `realstubs.o`'s `.bss` moves by exactly +0x40 and `__bss_end`
with it (the fifteenth confirmation, in the form where the pad does *not* move).

## The four misses

**1. The object's own `.bss` row was left in the pre-move frame.** The block derived the `.data` move and
then predicted the object's `.bss` at **0x801F85E4** — which is where the platform expert's `.bss` was in
369, i.e. the *old* frame. Measured **0x801FC5E4**, exactly +0x4000. The same block shifted `.bss`'s base
and `__bss_end` correctly; it just did not shift this one. **Tell: when a prediction contains a section
move, every row derived before the move has to be re-derived after it.**

**2. The `.rodata` row used two different values for one quantity, inside the one block.** The block
derived the `.text` run's delta as `+0x14AC + 0xC − 0x48 = +0x1470` — the `0xC` being the alignment fill
this very step introduced — and then wrote the row as `0x801A29D0 + 0x1464 + 0x4`, with the fill left
out:

```
0x801A29D0 + 0x1470 + 0x4 = 0x801A3E44     = measured
0x801A29D0 + 0x1464 + 0x4 = 0x801A3E38     = written, 0xC low
```

The *size* was exact (0x3A7C = 0x3AA0 − 0x24), so the row was right in one column and wrong in the other.
**Tell: a quantity the block has derived must be used as derived; re-deriving it inline in a later row is
how the two copies drift.**

**3. Three `hex ↔ decimal` conversions by hand, two of them wrong.** `image` was written `1860676` where
0x1C5C44 is **1858628**, and `headroom` was written `4199016` where `0x400000 − 0x1FED98` is **2101864**
(0x201268). Both rows' *hex* values were right, and both are rows that have no hex column in the table.
**Tell: these tables mix bases; state a row in the base it will be compared in, or compute the conversion
rather than doing it in the head.**

**4. The model's own, and it is the documented one.** The `.text` end came out 0x20 high (0x801A8280
against 0x801A82A0). The `.rodata` run's own delta was **−0x30** rather than the modeled −0x20, the extra
0x10 being fill the run redistributes when two 2-byte inputs are inserted into it, plus the tail pad
differing between the two ends. That is the ±0x10..0x40 caveat on the `.rodata` run this walk has carried
since 320 — and the reason the prediction carries a `band`.

## What the run measured

```
 xnu_entry_checks=0x00000005              xnu_entry_failures=0x00000000
 xnu_entry_stub_caller_v=0x8017bf7c       xnu_entry_abort_entries=0x00000000
 xnu_entry_stub_caller_digits=0x0000002d
 xnu_entry_stub_caller_w0=0x37313038 ("8017")   w1=0x63376662 ("bf7c")
 xnu_entry_abort_first_dfar=0x00000000    xnu_entry_abort_first_pc=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=comp_init
No errors detected
```

**Name and key exactly as predicted, no falsifier fired.** The stop is eight calls past the one 369
reported, and every one of the eight executed real code: the two `YSHA1Init` calls (+0x58, +0x64) with
their `VLD1.64 [r2 :128]`, `YSHA1Update` at +0x74 and +0x88, `YSHA1Final` at +0x94, the real `memcpy` at
+0xa8, then `YSHA1Init` at +0xb4, `YSHA1Update` at +0xc4 and `YSHA1Final` at +0xd0. So **0x12A4 bytes of
`YSHA1Transform` — the actual SHA-1 compression function — ran on this step's path**, twice.

The falsifiers, checked one by one: **(a)** no `YSHA1Update` at 0x8017BF10 and **(b)** no `YSHA1Final` at
0x8017BF30, so the object took and the chain is real; **(c)** `abort_entries=0` with `abort_first_pc=0`
and `abort_first_dfar=0`, so `YSHA1Transform` did not fault over the 0xAC-byte context or the 0x40-byte
workspace; **(d)** no fault at the aligned NEON load — the constant is at 0x8017D980 and is 16-aligned
only because the input's own alignment was honoured, so the tool's default would have placed it 0xC
earlier and *this falsifier would have fired*; **(e)** no `mmInit` at 0x8017BEA8 and no `YSHA1Init` at
0x8017BEF4, so neither of the previous two stops recurred.

## The next object, named before its run — and the last one on this path

The frontier is `comp_init`, whose pool definer is **`osfmk_prng_YarrowCoreLib_src_comp.o`**
(`YarrowCoreLib/src/comp.c`) — the only object in the pool that defines `comp_init`, `comp_end` or
`comp_get_ratio`. Measured against this image: 4 definitions, **0 references**, **3 resolved (3 function) /
0 added** — **783 → 780 undefined, 681 → 678 function, 102 → 102 storage**. `.text` is **0x28** and there
is nothing else allocatable:

```
comp_init       0x8   mov r0, #0 ; bx lr
comp_add_data   0x8   mov r0, #0 ; bx lr
comp_get_ratio 0x10   mov r0, #0x3F800000 ; str r0, [r1] ; mov r0, #0 ; bx lr
comp_end        0x8   (returns zero)
```

so `comp_get_ratio` writes **1.0f** through its out-parameter and returns success, and the other three are
no-ops. **An object with no references at all is the shape to notice**: after 371 every call
`prngInitialize` makes is real, and every call `yarrow_init` makes after it — `prngInput` (+0x314),
`prngOutput` (+0x338), `prngForceReseed` (+0x34C), `fips_initialize` (+0x358) — is real too, because all
four came in with `prng.o` at 368 and `prng_yarrow.o` at 367.

**So 371 completes the yarrow PRNG subtree, and its stop will not be in `osfmk/prng/` at all.** The return
path is `prngInitialize` → `yarrow_init` → `ccdrbg_init`'s `blx r6` → `read_random` → `uuid_generate` →
`IOPMrootDomain::start`, which is where 365's stop was at +0x5F0. The walk therefore resumes **eleven
steps' worth of frames back, at the instruction after the call it stopped on at 365** — and 371's block has
to work out where that is.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img`; nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of
either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`, `abort_entries=0`, no
`exception:` line. Log 301621 bytes, 3975 lines, last line `No errors detected`. The device came back to
Android on its own (`MI 4LTE`, release 10).

The layout moved further than in any step since 364 — `topOfKernelData` a whole megabyte, the tree another
— and the image still ends 60 bytes below `__bss_start`, so the payload's copy and its `memset` do not
overlap; the build's five layout invariants all passed. The step reads no device: the new code is pure
arithmetic over the 0xAC-byte context the previous step allocated.
