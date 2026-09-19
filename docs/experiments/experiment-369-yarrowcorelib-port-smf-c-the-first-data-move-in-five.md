# Experiment 369 — `YarrowCoreLib/port/smf.c`: the first `.data` move in five steps, and a stop three frames away

**Step:** link one object, `osfmk/prng/YarrowCoreLib/port/smf.c` (`osfmk_prng_YarrowCoreLib_port_smf.o`) —
the pool definer of 368's stop `mmInit`. It is inserted into the entry link between
`osfmk_prng_YarrowCoreLib_src_prng.o` and `stages/stage90/xnu_platform/MSM8974PlatformExpert.o`. Nothing
else changes.

**Prediction:** *4 resolved (4 function) / 0 added — 790 → **786** undefined, 688 → **684** function,
102 → **102** storage*; object `.text` **0x8017C65C** (0x38) and `__DATA, __data` **0x801C1A58** (0x18);
`.data` grows to **0x19A70** with everything below it +0x18; and the stop at **`YSHA1Init`** at key
**`0x8017BEF4`**.

**Result:** **every row exact** — counts, all three `.text` placements, the `.data` derivation and the four
rows it moves, the `.rodata` row in both its parts, the stop's name and its composed key — and no falsifier
fired. The first step in a long while with no miss anywhere.

## The object

Six definitions in **0x38 of `.text`** plus **0x18 of `__DATA, __data`**, and nothing else allocatable. The
four the pool stops on are trivial, and reading them says what the step buys:

```
mmInit     0x4   bx lr                                   - a no-op
mmMalloc  0x28   kalloc_canblock(size, 1, &mmMalloc.site)
mmFree     0x4   b kfree_addr                            - tail call
mmGetPtr   0x4   bx lr                                   - returns its argument
mmReturnPtr 0x4  bx lr                                   - defined, nothing references it
```

and `mmMalloc.site` is a 0x18 `VM_ALLOC_SITE_STATIC` — the whole of the object's `.data`. So
`prngInitialize`'s `mmMalloc(0xAC)` becomes a real `kalloc`, `mmGetPtr` hands the same pointer straight
back, and the `memset(ptr, 0, 0xAC)` that follows is over the allocation the allocator returned.
`kalloc_canblock` was already proven at this point in the boot: 366's run executed it at 0x8003824C for
`read_random`'s `ccdrbg_info` and went past it.

## The stop is three frames away, and that is the shape to write down

All four `mm*` names retire at once and the object creates nothing, so nothing in it can stop the run. The
frontier is the **next stub in `prngInitialize`'s own instruction order**, three frames past the object just
linked — `prngInitialize` (real since 368), `mmMalloc` (real now), `mmGetPtr` (real now), the real `memset`
at +0x3C, then:

```
58: bl YSHA1Init   <- object +0x58, key 0x8017BEF4     (YarrowCoreLib/src/sha1mod.o)
```

This is 366's shape — a step that retires names without being the step that stops — and it is the second
appearance, so the tool is written down: **disassemble the function being resumed, list its stub calls in
address order, and take the first one whose definer is not in the step.**

## What the run measured

```
 xnu_entry_checks=0x00000005              xnu_entry_failures=0x00000000
 xnu_entry_stub_caller_v=0x8017bef4       xnu_entry_abort_entries=0x00000000
 xnu_entry_stub_caller_digits=0x0000002d
 xnu_entry_stub_caller_w0=0x37313038 ("8017")   w1=0x34666562 ("bef4")
 xnu_entry_abort_first_dfar=0x00000000    xnu_entry_abort_first_pc=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=YSHA1Init
No errors detected
```

The falsifiers, checked one by one: **(a)** no `mmInit` at 0x8017BEA8, so the link took the object and the
four names really are retired; **(b)** `abort_entries=0` with `abort_first_pc=0` — the **NEON sequence at
object +0x40..+0x50 executed without a coprocessor fault**, which is the argument the block made in advance
(`start.s:408-410` sets `FPEXC_EN` and the image's `_start` has executed `vmsr fpexc, r2` at 0x800003A8 in
every run, and a `VMSR` is a CP10 access) confirmed by the run; **(c)** no `panic` line, so `mmMalloc`'s
`kalloc(0xAC)` did **not** return NULL and the `beq 0x124` branch was not taken; **(d)** no `YSHA1Update` at
0x8017BF10; **(e)** `abort_first_dfar=0`.

What actually ran, in order: `prngInitialize` (entered from `yarrow_init`), `mmInit` (`bx lr`), `mmMalloc` →
`kalloc_canblock(0xAC, 1, &mmMalloc.site)`, `mmGetPtr` (`bx lr`, same pointer back), the real `memset` over
0xAC bytes, the two NEON stores, then the stop. A 0xAC-byte kernel allocation was taken and zeroed on the
way, and the `VM_ALLOC_SITE_STATIC` the allocator recorded lives in the 0x18 of `.data` this step placed.

## The layout

| | 368 measured | 369 predicted | 369 measured |
|---|---|---|---|
| counts | 790 / 688 / 102 | **786 / 684 / 102** | **786 / 684 / 102** |
| object `.text` | — | 0x8017C65C (0x38) | **0x8017C65C (0x38)** |
| `realstubs.o` `.text` | 0x8017C808 (0x4080) | 0x8017C840 (0x4020) | **0x8017C840 (0x4020)** |
| platform expert `.text` | 0x8017C65C | 0x8017C694 (0x150) | **0x8017C694 (0x150)** |
| object `__DATA, __data` | — | 0x801C1A58 (0x18) | **0x801C1A58 (0x18)** |
| `realstubs.o` `.rodata.str1.4` | 0x801A29F8 (0x3AC8) | 0x801A29D0 (0x3AA0) | **0x801A29D0 (0x3AA0)** |
| `.text` end | 0x801A6E80 | 0x801A6E40 | **0x801A6E40** |
| text size | 1732224 | 1732160 | **1732160** |
| `.data` | 0x801A8000 (0x19A58) | 0x801A8000 (0x19A70) | **0x801A8000 (0x19A70)** |
| `.sysctl_set` | 0x801C1A58 (0x150) | 0x801C1A70 (0x150) | **0x801C1A70 (0x150)** |
| `.init_array` | 0x801C1BA8 (0x84) | 0x801C1BC0 (0x84) | **0x801C1BC0 (0x84)** |
| its end | 0x801C1C2C | 0x801C1C44 | **0x801C1C44** |
| `.bss` | 0x801C1C40 (0x390D8) | 0x801C1C80 (0x390D8) | **0x801C1C80 (0x390D8)** |
| `__bss_end` | 0x801FAD18 | 0x801FAD58 | **0x801FAD58** |
| image | 1842220 | 1842244 | **1842244** |
| headroom | 1069800 | 1069736 | **1069736** |
| `args` / `topOfKernelData` | +2080768 / +3145728 | unmoved | **+2080768 / +3145728** |

**The `.data` move was derived, not asserted, and the derivation was the whole point.** 368's `.data` ended
exactly on `.sysctl_set`'s start — the section's last input is `osfmk_vm_vm_shared_region.o`'s
`__DATA, __data` at 0x801C1A40 (0x18), ending on 0x801C1A58, with the `. = ALIGN(0x8)` after it already
satisfied — so there was no fill to absorb the new input and the 0x18 had to land at the section's end and
push everything below it by +0x18. That is 364's and 365's error case run correctly: **a `.data` row is a
sum only when the tail is exactly aligned, and here the map said so before the build.** All four derived
rows landed, and so did the two derived counts (image +0x18 = 1842244, headroom −0x40 = 1069736).

**`.text` shrank while the object added bytes** — the first step in a while with opposite signs:
`+0x38` (the object) `− 0x60` (four fewer stub bodies, `684 × 0x18 = 0x4020` against 688's `0x4080`) =
`−0x28`, and the `.rodata` run's own delta was the name slots alone, `−0x28` (`mmFree` `align4(6+1)` = 0x8,
`mmGetPtr` 0xC, `mmInit` 0x8, `mmMalloc` 0xC), so the section closed `0x50` earlier at 0x801A6E40.

**`.bss` is a copy in size and a move in place**: no `.bss` input changed, so 0x390D8 is 368's number, and
its *start* is `align64(0x801C1C44)` = 0x801C1C80 — so the section, `__bss_end`, the image and the headroom
all moved by +0x40 while `.bss` itself did not change by a byte. **And `args` did not move**, because
`align_up(0x1FAD58, 0x1000)` is still 0x1FB000.

**The `.rodata` row, in both parts** — the row 366 got 0x3F0 wrong, 367 got 0x510 wrong and 368 got 0x18
wrong, now exact: address `0x801A29F8 + (−0x28) = 0x801A29D0`, where the `−0x28` is the `.text` run's delta
*as the linker consumed it* (this time the run boundary absorbed nothing, so the model's sum and the
linker's cursor agreed to the byte), and size `0x3AC8 − 0x28 = 0x3AA0`. It came out because the rule was
applied as **arithmetic** rather than restated as a sentence, and because the name slots were summed with a
calculator.

## The next object, named before its run

The frontier is `YSHA1Init`, whose pool definer is **`osfmk_prng_YarrowCoreLib_src_sha1mod.o`**
(`YarrowCoreLib/src/sha1mod.c`) — the only object in the pool that defines `YSHA1Init`, and the only one
that defines `YSHA1Update` and `YSHA1Final` either, so one step retires all three. Measured against this
image: 7 definitions, 2 references, **3 resolved (3 function) / 0 added**, both references already satisfied
— **786 → 783 undefined, 684 → 681 function, 102 → 102 storage**. It is `.text` **0x14AC** — 5292 bytes, much
the largest single step of the last twenty — a `.bss` of **0x40**, and a 2-byte `.rodata` with a 2-byte
`.rodata.str1.1`.

**Its stop is in its own object this time** — `YSHA1Init` is the first call in it and the step retires it —
so 370 reverts to the ordinary shape: disassemble `YSHA1Init`, take its first non-real call, and check
whether that name is in 370's own `added` column.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img`; nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of
either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`, `abort_entries=0`, no `exception:`
line. Log 301621 bytes, 3975 lines, last line `No errors detected`. The device came back to Android on its
own (`MI 4LTE`, release 10). The one new hardware-adjacent thing this step does is take a 0xAC-byte kernel
allocation, which the boot had already done at 366 and which is memory, not a device.
