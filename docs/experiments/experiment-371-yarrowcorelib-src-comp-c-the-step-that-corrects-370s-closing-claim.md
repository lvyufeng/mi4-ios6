# Experiment 371 — `YarrowCoreLib/src/comp.c`: the step that completes the yarrow call graph, and whose stop is not where 370 said it would be

**Step:** link one object, `osfmk/prng/YarrowCoreLib/src/comp.c` (`osfmk_prng_YarrowCoreLib_src_comp.o`) — the
pool definer of 370's stop `comp_init`. It is inserted into the entry link between
`osfmk_prng_YarrowCoreLib_src_sha1mod.o` and `stages/stage90/xnu_platform/MSM8974PlatformExpert.o`. Nothing
else changes.

**Prediction:** *3 resolved (3 function) / 0 added — 783 → **780** undefined, 681 → **678** function,
102 → **102** storage*; object `.text` **0x8017DB4C** (0x28); `.data` and every row below `.text`
**unmoved**; and the stop at **`trashMemory`** at key **`0x8017C2D0`** — a stop *inside* `osfmk/prng/`, which
required overruling 370's own closing claim before the run.

**Result:** all three counts exact, every row below `.text` exact, the stop's name and key exact with no
falsifier firing — and **one miss**, in the stub-body term: the block retired two stub bodies where the
object retires three, which is exactly the class 370's miss 2 was (one quantity, two values, inside one
block).

## The object, and why it is four instructions that unwedge 0x300 bytes

```
resolved (3: 3 function, 0 storage)
    comp_end   comp_get_ratio   comp_init
added (0: 0 function, 0 storage)
of the 0 references, 0 are already satisfied
```

**The object has zero references** — the first in this walk with none at all. `.text` is **0x28** for four
definitions and there is nothing else allocatable; `comp_add_data` is `T` in the object but is not a stand-in
in the image, because nothing references it:

```
comp_init       0x8   mov r0, #0 ; bx lr
comp_add_data   0x8   mov r0, #0 ; bx lr
comp_get_ratio 0x10   mov r0, #0x3F800000 ; str r0, [r1] ; mov r0, #0 ; bx lr
comp_end        0x8   mov r0, #0 ; bx lr
```

so `comp_get_ratio` writes **1.0f** through its out-parameter and returns success, and the other three return
zero. What those four instructions buy is a *flag*, and that is this step's whole story.

## The stop: 370's closing claim was wrong, and the block that corrects it

370's block ended by saying *"371 completes the yarrow PRNG subtree, and its stop will not be in
`osfmk/prng/` at all … the walk resumes eleven steps' worth of frames back into `IOPMrootDomain::start`"*. That
is wrong, and this block says so **before** its own run, because it misread the four calls `yarrow_init` makes
after `prngInitialize`:

* It read them as *cut off*. They are not — 370's stop sits **inside** `prngInitialize`, which `yarrow_init`
  calls at +0x2E4. As soon as `prngInitialize` can *return*, `yarrow_init` continues straight into
  `prngInput` (+0x314), `prngOutput` (+0x338), `prngForceReseed` (+0x34C) and `fips_initialize` (+0x358).
* It read "real" as a property of the *call sites*. All four are real calls. Their closures are not all real:
  `fips_initialize` → `random_block` → `FIPS_SHA1Init` is a stub, and `prngForceReseed` has one stub of its
  own left in its tail.

`prngForceReseed` is the one that decides the step, and it turns on a flag **this step's object is what
sets**:

```
obj+0x2b0:  beq 0x44c            <- early return 3 if the state pointer is NULL
obj+0x2b8:  ldr r0, [r0, #0xa8]
obj+0x2bc:  cmp r0, #33
obj+0x2c0:  bne 0x44c            <- early return 3 if state->0xa8 != 33
...  the 50 ms generation loop: real prngOutput xN, real YSHA1Update, real mach_absolute_time
obj+0x3a8 / 0x3c4 / 0x404:  YSHA1Final, YSHA1Final, YSHA1Final      real
obj+0x408:  zero the state's tail (vmov.i32 q8, #0 ; vst1.32 {d16-d17}, [r0], r1)
obj+0x434:  bl trashMemory       <- THE STOP (YarrowCoreLib/src/yarrowUtils.c)
obj+0x440:  bl trashMemory       (reached only if the first ever returned, which a stub never does)
```

and `state->0xa8` is written **by `prngInitialize` at +0x11C**, on the path where all three `comp_init` calls
returned 0:

```
prngInitialize obj+0xe0 / +0xf4 / +0x104:  bl comp_init
               obj+0xe8 / +0xf8 / +0x108:  cmp r0, #0 ; bne 0x128   (bail out to mmFree)
               obj+0x114: mov r0, #33
               obj+0x11c: str r0, [r6, #168]      <- state->0xa8 = 33
```

**So 371 is the step that makes `prngForceReseed` take its long path**, and the frontier is `trashMemory` —
not `FIPS_SHA1Init` (which `fips_initialize` would reach only *after* `prngForceReseed` returned) and not
`IOPMrootDomain::start` (eleven frames further up). This is a new shape for the walk: **a step whose own
return value is what makes the next function's guard fall through.** The general lesson is the one 370 forgot:
a call site being real says nothing about the call's closure, and *a function that returns is a function whose
caller's next line executes*.

## The key, composed

```
prng.o's .text 0x8017BE98          (= 370's: prng.o sits before the hole, so unmoved)
prngForceReseed is at prng.o +0x2A0 -> 0x8017C138
the `bl trashMemory` is at +0x434   -> 0x8017C2CC
and the field holds the return address, +4 -> 0x8017C2D0
```

## What the run measured

```
 xnu_entry_checks=0x00000005              xnu_entry_failures=0x00000000
 xnu_entry_stub_caller_v=0x8017c2d0       xnu_entry_abort_entries=0x00000000
 xnu_entry_stub_caller_digits=0x0000002f
 xnu_entry_stub_caller_w0=0x37313038 ("8017")   w1=0x30643263 ("c2d0" reversed)
 xnu_entry_abort_first_dfar=0x00000000    xnu_entry_abort_first_pc=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=trashMemory
No errors detected
```

**Name and key exactly as predicted.** What ran, in order: `prngInitialize` **returned 0** with all three
`comp_init` calls taking the `mov r0, #0 ; bx lr` path, so the three `cmp r0, #0 ; bne 0x128` guards were not
taken and `state->0xa8 = 33` was written at +0x11C; then `yarrow_init`'s `prngInput` (one real `YSHA1Update`),
`prngOutput` (twelve real `YSHA1*`/`memcpy` calls) and `prngForceReseed`, which took its **long** path
*because* of that 33 — the 50 ms generation loop, the three `YSHA1Final` calls, the tail block's NEON zeroing,
and the stop.

The falsifiers, checked one by one: **(a)** no `comp_init` at 0x8017BF7C — the object took, and that key is
370's own stop, so it is now a two-way fact; **(b) no `FIPS_SHA1Init` at 0x8017BB0C** — the informative one:
`prngForceReseed` did not take either early return, i.e. `state->0xa8` really is 33, i.e. `comp_init`
returning 0 really is what let `prngInitialize` set the flag; **(c)** no `trashMemory` at 0x8017C490, so the
stop is `prngForceReseed`'s call site and not `prngStretch`'s; **(d)** `abort_entries=0` with
`abort_first_pc=0` and `abort_first_dfar=0` — the NEON zeroing and the whole 50 ms loop ran without a fault;
**(e)** no `panic` line and the log ends in the kernel's own `No errors detected` — `fips_initialize`'s
known-answer `memcmp`/`panic` at +0x264/+0x278 is *behind* the stop.

## The layout

| | 370 measured | 371 predicted | 371 measured |
|---|---|---|---|
| counts | 783 / 681 / 102 | **780 / 678 / 102** | **780 / 678 / 102** |
| object `.text` | — | 0x8017DB4C (0x28) | **0x8017DB4C (0x28)** |
| platform expert `.text` | 0x8017DB4C (0x150) | 0x8017DB74 (0x150) | **0x8017DB74 (0x150)** |
| `realstubs.o` `.text` | 0x8017DCF8 (0x3FD8) | 0x8017DD20 (0x3FA8) | **0x8017DD20 (0x3F90 = 678 × 0x18)** |
| `realstubs.o` `.rodata.str1.4` | 0x801A3E44 (0x3A7C) | 0x801A3E3C (0x3A54) | **0x801A3E24 (0x3A54)** |
| `.text` end | 0x801A8280 | 0x801A8260 | **0x801A8240** |
| text size | 1737344 | 1737312 | **1737280** |
| `.data` | 0x801AC000 (0x19A70) | unmoved | **0x801AC000 (0x19A70)** |
| `.sysctl_set` | 0x801C5A70 (0x150) | unmoved | **0x801C5A70 (0x150)** |
| `.init_array` | 0x801C5BC0 (0x84) | unmoved | **0x801C5BC0 (0x84)** |
| its end | 0x801C5C44 | unmoved | **0x801C5C44** |
| `.bss` | 0x801C5C80 (0x39118) | unmoved | **0x801C5C80 (0x39118)** |
| `__bss_end` | 0x801FED98 | unmoved | **0x801FED98** |
| image | 1858628 | unmoved | **1858628 (= 0x1C5C44)** |
| args / topOfKernelData / tree / window | +2097152 / +4194304 / +6291456 / 8388608 | unmoved | **all unmoved** |
| headroom | 2101864 | unmoved | **2101864 (= 0x201268)** |

**The `.text` output section shrank while the object added bytes**, and the `.data` bucket was not re-entered:
`.data` starts at `align_up(text_end, 0x4000)`, and even at the predicted 0x801A8260 that is still above
0x801A8000, so 0x801AC000 held with 0x3DA0 of room under it. Nothing below `.text` changed, so the three
derived layout numbers and the window could not.

**The `.bss` pad rule is not exercised** — the object has no `.bss` at all, so `.bss`'s size and start are
370's numbers (368's case, not 367's).

## The one miss: two stub bodies in one term, three in another

The block's `.text`-run term retired **two** stub bodies where the object retires **three**:

```
block:    .text run delta = +0x28 - 0x30 = -0x8       (0x30 = 2 x 0x18)
correct:  .text run delta = +0x28 - 0x48 = -0x20      (0x48 = 3 x 0x18)
```

`comp.o` resolves three function names — `comp_init`, `comp_get_ratio` and `comp_end` — and the block's own
quotation of the tool, twenty lines above the term, says so: `resolved (3: 3 function, 0 storage)`. The same
block used **three** correctly for the `.rodata` term (`comp_init` 0xC + `comp_get_ratio` 0x10 + `comp_end`
0xC = 0x28, and that row's size came out exact at 0x3A54) and **two** for the body term. **One quantity, two
values, inside one block** — 370's miss 2 in a new place: not a derived delta re-derived inline in a later
row, but a *count*, one line of arithmetic away from the term that used it correctly.

The 0x18 appears in exactly three rows, and those are the three that missed: `realstubs.o`'s `.text` **size**
(0x3FA8 written, 0x3F90 measured), the `.rodata` row's **address** (0x801A3E3C written, 0x801A3E24 measured),
and the `.text` end and text size (0x801A8260 / 1737312 written, 0x801A8240 / 1737280 measured). With the term
right, all three are exact with no other change:

```
.text end = 0x801A8280 - 0x28 (3 retired name slots) - 0x48 (3 retired stub bodies) = 0x801A8238
ALIGN(32) -> 0x801A8240                                                    = measured exactly
.rodata row = 0x801A3E44 - 0x20 = 0x801A3E24, size 0x3A7C - 0x28 = 0x3A54   both exact
```

**Tell: a step retires one name *and* one body per resolved function, so both terms must be built from the
same integer — and that integer is the first number the effect tool prints.** The two terms are multiplied by
different constants (a name slot's `align4(len + 1)`, a body's 0x18), which is exactly why the count has to be
taken once and carried rather than re-read per row. 370 missed the same way one step earlier, in the other
direction: there the *derived* value was re-derived; here the *count* was.

## The next object, named before its run

`trashMemory`'s pool definer is **`osfmk_prng_YarrowCoreLib_src_yarrowUtils.o`**
(`YarrowCoreLib/src/yarrowUtils.c`) — the only object in the pool that defines it. Measured against this
image: 1 definition, 1 reference (`memset`, already satisfied), **1 resolved (1 function) / 0 added** —
**780 → 779 undefined, 678 → 677 function, 102 → 102 storage**. `.text` is **0x44**, 4-byte aligned, a single
section, nothing else allocatable, and the body is real:

```
+0x00: cmp r1, #0 ; bxeq lr        - zero length is a no-op
+0x08: push {r4, r5, fp, lr}
+0x1c: bl memset(r0, 0x00, r1)     - clear
+0x2c: bl memset(r0, 0xff, r1)     - fill
+0x40: b  memset(r0, 0x00, r1)     - clear, tail call - so the only name it adds is `memset`
```

**And 372's stop is named here, before 372's block exists**: the same object takes the walk through
`prngForceReseed`'s *second* call site (0x8017C2D8) and the function's return to `yarrow_init`, then
`fips_initialize` at +0x358 → `random_block` at prng_yarrow.o +0x250 → **`FIPS_SHA1Init`, the `bl` at
prng_yarrow.o +0x104, key `0x8017BB0C`** — this block's falsifier (b), now expected to fire as the stop. Its
definer is **`osfmk_prng_fips_sha1.o`** (`.text` 0x1600, `.data` 0x40 — the first `.data` input in eleven
steps — 2 resolved / 0 added, and `FIPS_SHA1Final` defined but unreferenced). So **the yarrow subtree ends at
372, not at 371**, and the walk returns toward `IOPMrootDomain::start` one step later than 370 thought.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img`; nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of
either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`, `checksum=0x907fed19`,
`abort_entries=0`, no `exception:` line. Log 301623 bytes, 3975 lines, last line `No errors detected`. The
device came back to Android on its own (`MI 4LTE`, release 10).

The step's own code is register arithmetic; what it *unlocks* is the 50 ms generation loop, which is CPU work
over memory already allocated by 369's `kalloc` and 370's SHA-1 — no device is read, and the stop is a
reporting stub, so the zeroing the tail block performs never happens.
