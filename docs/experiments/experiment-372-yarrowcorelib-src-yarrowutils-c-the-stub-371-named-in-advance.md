# Experiment 372 — `YarrowCoreLib/src/yarrowUtils.c`: the stub 371 stopped on, and a step that adds a `panic` to the path ahead

**Step:** link one object, `osfmk/prng/YarrowCoreLib/src/yarrowUtils.c`
(`osfmk_prng_YarrowCoreLib_src_yarrowUtils.o`) — the pool definer of 371's stop `trashMemory`, and the object
371's block named in advance. It is inserted into the entry link between
`osfmk_prng_YarrowCoreLib_src_comp.o` and `stages/stage90/xnu_platform/MSM8974PlatformExpert.o`. Nothing else
changes.

**Prediction:** *1 resolved (1 function) / 0 added — 780 → **779** undefined, 678 → **677** function,
102 → **102** storage*; object `.text` **0x8017DB74** (0x44); **text size exact to the byte**; every row below
`.text` **unmoved**; and the stop at **`FIPS_SHA1Init`** at key **`0x8017BB0C`**.

**Result:** all three counts exact, the text size exact to the byte, every `.text` row exact, every row below
`.text` exact and unmoved, the stop's name and key exact with no falsifier firing — and **one miss, of 0x4, on
the `.rodata` row**, which is the boundary-fill caveat and nothing else.

## The object

```
resolved (1: 1 function, 0 storage)
    trashMemory
added (0: 0 function, 0 storage)
of the 1 references, 1 are already satisfied
```

`.text` is **0x44**, 4-byte aligned, a single section, and there is nothing else allocatable — no `.rodata`, no
`.bss`, no `.data`, no `.init_array` — so this is 368's / 371's shape: the `.rodata`-run term is the one
retired name slot, there is no `.bss` term, and the pad rule is not exercised. The body is real code, and it
is the only *debug* helper the walk has linked:

```
+0x00: cmp r1, #0 ; bxeq lr         - a zero length is a no-op
+0x08: push {r4, r5, fp, lr}
+0x1c: bl memset(r0, 0x00, r1)      - clear
+0x2c: bl memset(r0, 0xff, r1)      - fill with 0xff
+0x40: b  memset(r0, 0x00, r1)      - clear again, as a tail call
```

so `trashMemory(buf, len)` clears, fills and clears again — the kernel's "poison this buffer so a
use-after-free is obvious" helper. The step therefore runs real code over buffers whose lengths come from its
caller, which is the one thing to watch in its run: `prngForceReseed` calls it as `trashMemory(r6, 0x14)` and
`trashMemory(sp + 0x14, 0x40)` — both stack, both in range, and a fault there would show as `abort_entries != 0`
rather than as a wrong stop.

## The stop

```
obj+0x250: bl random_block      (fips_initialize, called from yarrow_init at +0x358)
obj+0x0d0: bl prngOutput        (random_block) - *skipped*, see below
obj+0x104: bl FIPS_SHA1Init     <- THE STOP (osfmk/prng/fips_sha1.c)
```

Reached like this: `trashMemory` returns, `prngForceReseed` takes its second call site (0x8017C2D8), falls out
of its tail block to `0x44C` (`mov r0, r5` with `r5 = 0`) and returns to `yarrow_init` at +0x350; `yarrow_init`
then reaches its **last** call, `fips_initialize` at +0x358, which `memset`s two buffers and calls
`random_block` at +0x250 with **`r2 = 0`** (`mov r2, #0` at +0x24C) — and `random_block`'s first branch is
`ldr r0, [sp, #8] ; cmp r0, #0 ; beq 0x100` (obj+0xB4..+0xBC), so the `bl prngOutput` at +0xD0 is **not
executed on this path** and the first call it does make is `bl FIPS_SHA1Init` at **obj+0x104**. The key:

```
prng_yarrow.o's .text 0x8017BA04    (= 370's, 371's and 372's: it sits before the insertion point)
random_block is at object +0x74
the `bl FIPS_SHA1Init` is at +0x104  -> 0x8017BB08
and the field holds the return address, +4 -> 0x8017BB0C
```

## What the run measured

```
 xnu_entry_checks=0x00000005              xnu_entry_failures=0x00000000
 xnu_entry_stub_caller_v=0x8017bb0c       xnu_entry_abort_entries=0x00000000
 xnu_entry_stub_caller_digits=0x0000002f
 xnu_entry_stub_caller_w0=0x37313038 ("8017")   w1=0x63306262 ("bb0c")
 xnu_entry_abort_first_dfar=0x00000000    xnu_entry_abort_first_pc=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=FIPS_SHA1Init
No errors detected
```

**Name and key exactly as predicted**, and the key is the one 371's block named a step early as its own
falsifier (b). `trashMemory` ran — real code, twice, over the 0x14-byte and 0x40-byte stack buffers
`prngForceReseed` passes it — and the walk then took `prngForceReseed`'s second call site, its return to
`yarrow_init` at +0x350, `yarrow_init`'s last call `fips_initialize` at +0x358, and inside it `random_block` at
+0x250 with `r2 = 0`.

The falsifiers, checked one by one: **(a)** no `trashMemory` at 0x8017C2D0 — the object took, and that key is
371's own stop, so it is a two-way fact; **(b)** no `trashMemory` at 0x8017C2DC either, so the second call site
was not the one that stopped; **(c)** no `FIPS_SHA1Update` at 0x8017BB48, which is what the run reads if
`FIPS_SHA1Init` turned out to be real — so the name really is a stand-in *and* the `beq 0x100` at obj+0xBC
really did skip the `bl prngOutput` at obj+0xD0, exactly as predicted; **(d)** no `panic` line and the log ends
in the kernel's own `No errors detected`, so `yarrow_init`'s guard at +0x2E8 saw a zero `prngInitialize`
return again; **(e)** `abort_entries=0` with `abort_first_pc=0` and `abort_first_dfar=0` — **and this is the
reading this prediction was about**: `trashMemory` writes 0x14 + 0x40 bytes to two stack buffers, three passes
each, and it did not fault.

## The layout

| | 371 measured | 372 predicted | 372 measured |
|---|---|---|---|
| counts | 780 / 678 / 102 | **779 / 677 / 102** | **779 / 677 / 102** |
| object `.text` | — | 0x8017DB74 (0x44) | **0x8017DB74 (0x44)** |
| platform expert `.text` | 0x8017DB74 (0x150) | 0x8017DBB8 (0x150) | **0x8017DBB8 (0x150)** |
| `realstubs.o` `.text` | 0x8017DD20 (0x3F90) | 0x8017DD64 (0x3F78) | **0x8017DD64 (0x3F78)** |
| its end | 0x80181CB0 | 0x80181CDC | **0x80181CDC** |
| `realstubs.o` `.rodata.str1.4` | 0x801A3E24 (0x3A54) | 0x801A3E50 (0x3A48) | **0x801A3E54 (0x3A48)** |
| `.text` end | 0x801A8240 | 0x801A8260 | **0x801A8260** |
| text size | 1737280 | 1737312 | **1737312** |
| `.data` | 0x801AC000 (0x19A70) | unmoved | **0x801AC000 (0x19A70)** |
| `.sysctl_set` | 0x801C5A70 (0x150) | unmoved | **0x801C5A70 (0x150)** |
| `.init_array` | 0x801C5BC0 (0x84) | unmoved | **0x801C5BC0 (0x84)** |
| its end | 0x801C5C44 | unmoved | **0x801C5C44** |
| `.bss` | 0x801C5C80 (0x39118) | unmoved | **0x801C5C80 (0x39118)** |
| `__bss_end` | 0x801FED98 | unmoved | **0x801FED98** |
| image | 1858628 | unmoved | **1858628 (= 0x1C5C44)** |
| args / topOfKernelData / tree / window / headroom | +2097152 / +4194304 / +6291456 / 8388608 / 2101864 | all unmoved | **all unmoved** |

**The two terms, stated as expressions rather than numbers**, because 371 got one of them wrong:
`resolved (1: 1 function, 0 storage)` → **one** body of 0x18 (the stub-body term, `678 - 677`) and **one** name
slot of `align4(11 + 1) = 0xC` (the `.rodata` term) — both from the same integer, which is the tool's first
printed number. The `.text` run's delta is `+0x44 − 0x18 = +0x2C`; the `.rodata` run's is `−0xC`; the section
grows `0x20` from 0x801A8240 and is already 32-aligned, so it closes exactly at **0x801A8260**.

**And the row that moves in two directions at once came out exact**: `realstubs.o`'s `.text` **start** moves
+0x44 (where the object went) while its **size** falls 0x18 (one fewer stub body), so its **end** moves +0x2C —
0x80181CB0 → 0x80181CDC. That is 371's table shape applied rather than discovered.

## The one miss: 0x4, and it is the model's

The `.rodata` row's *size* is exact (0x3A54 − 0xC = 0x3A48), and its *address* is 0x4 higher than the `.text`
run's delta alone says: 0x801A3E24 + 0x2C = 0x801A3E50 predicted, **0x801A3E54** measured. That 0x4 is fill the
linker inserts at the boundary where the `.text` run hands the cursor to the `.rodata` run — the same 0x4 that
368 measured (`uuid`'s `.rodata` +0x868 against the run's +0x86C) and that 371 measured as 0x0. **A `.rodata`
row is a row shift plus whatever the boundary absorbed, and the absorbed term is 0 to 0x10**, which is why the
band is written on the section end rather than on this row. Nothing else moved: the counts, both `.text`
placements for the object and the platform expert, the stub region in all three of its rows, the section end,
the text size and every row below `.text` are exact.

## The next object, named before its run

`FIPS_SHA1Init`'s pool definer is **`osfmk_prng_fips_sha1.o`** (`osfmk/prng/fips_sha1.c`) — the only object in
the pool that defines it, and the only one that defines `FIPS_SHA1Update` either, so one step retires both.
Measured against this image: 5 definitions, 2 references (both `memcpy`/`memset`, already satisfied),
**2 resolved (2 function) / 0 added** — **779 → 777 undefined, 677 → 675 function, 102 → 102 storage**. Unlike
368, 370, 371 and 372, **this object is not text-only**: `.text` is **0x1600** (four definitions —
`FIPS_SHA1Init` 0x40, `FIPS_SHA1Update` 0xE0, `FIPS_SHA1Final` 0x14C and a **local** `SHA1Transform` 0x1394, the
second SHA-1 compression function this walk links) *and* a `.data` of **0x40** (one object named `PADDING`,
`d`) — **the first `.data` input in eleven steps**, so 373 is where the `.data` section next moves.

**And 373 is the first step on this path with a real `panic` in front of the frontier.** `random_block` hashes
a fixed input with the FIPS SHA-1 and `fips_initialize` compares the result against `kKnownAnswer` (20 bytes of
`prng_yarrow.o`'s `.rodata`), calling the real `panic` at obj+0x278 if they differ. Because both sides now come
from the same source the expectation is that the test passes — but it is the first time this walk has linked
code whose own self-test runs, and it is the thing to read first in the next run's log.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img`; nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of
either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`, `abort_entries=0`, no `exception:`
line. Log 301625 bytes, 3975 lines, last line `No errors detected`. The device came back to Android on its own
(`MI 4LTE`, release 10).

The step's own code is a `memset`-only helper that ran twice over stack buffers; it reads no device, allocates
nothing and stops at a reporting stub before any of the PRNG's state is poisoned.
