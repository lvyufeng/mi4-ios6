# Experiment 367 — `prng_yarrow.c`: the step whose counts go *up*, and a stop two indirect dispatches deep

**Step:** link one object, `osfmk/prng/prng_yarrow.c` (`osfmk_prng_prng_yarrow.o`) — the pool definer of
366's stop `ccdrbg_factory_yarrow`. It is inserted into the entry link between `libkern_uuid_uuid.o` and
`stages/stage90/xnu_platform/MSM8974PlatformExpert.o`. Nothing else changes.

**Prediction:** *1 resolved (1 function) / 6 added (6 function), 0 storage either way — 778 → **783**
undefined, 676 → **681** function, 102 → **102** storage* — i.e. the counts go **up** for the first time
since 342; object `.text` **0x8017BA04** (0x494); `.text` end **0x801A65E0**; every section below `.text`
unmoved; and the stop at **`prngInitialize`** at key **`0x8017BCEC`**.

**Result:** all three counts exact, every row predicted from a *boundary* argument exact except one, and
**the stop name and the key both exactly as predicted with no falsifier firing**. The one miss is 366's
miss 2 class again, with 366's own check written down beside it and not run.

## Why this object, and why the counts go up

366 stopped at `ccdrbg_factory_yarrow` from `read_random`. The effect tool against the 366 image:

```
resolved (1: 1 function, 0 storage)
    ccdrbg_factory_yarrow
added (6: 6 function, 0 storage)
    FIPS_SHA1Init  FIPS_SHA1Update  prngForceReseed  prngInitialize  prngInput  prngOutput
of the 10 references, 4 are already satisfied
```

so this step retires one name and creates six — five more stubs than it clears. That is not a regression:
it retires **the name the run stopped on**, which is the whole criterion for a step, and the six it creates
are the next six frontiers. The object is a leaf whose references point into a library nothing else in the
image touches: the six names are defined by `osfmk_prng_fips_sha1.o` and
`osfmk_prng_YarrowCoreLib_src_prng.o`, and neither is linked. (`osfmk_prng_fips_sha1.o` measures 0
resolved / 0 added against this image, so it is not the frontier and there was no reason to prefer it.)

The object itself: `.text` **0x494** — **nine** definitions, five `T` (`add_blocks`, `CalculateCRC`,
`random_block`, `fips_initialize`, `ccdrbg_factory_yarrow`) and four local `t` (`yarrow_init`,
`yarrow_generate`, `yarrow_reseed`, `yarrow_destroy`, the four the factory stores); `.rodata` **0x414**
(`g_crc_table` 0x400 and `kKnownAnswer` 0x14, the FIPS known-answer test vector); `.rodata.str1.1`
**0x92**; `.bss` **0x2C** (`zeros`); no `.data`, no `.sysctl_set`, no `.init_array`.

## The stop, and why it is two dispatches in

The factory is a leaf — 0x34 bytes and **not one call**:

```
290: push {r4, lr}
294-2b4: movw/movt  yarrow_generate, yarrow_init, yarrow_destroy, yarrow_reseed
2a4: mov  r2, #64                     ; infop->size = sizeof(yarrow context)
2b8: stm  r0, {r2, r3, r4, ip, lr}    ; {size, init, reseed, generate, destroy}
2bc: str  r1, [r0, #20]               ; infop->options
2c0: pop  {r4, pc}
```

So after this step `prng_ccdrbg_factory(pp->infop, NULL)` returns, `read_random` allocates
`pp->statep = kalloc(infop->size)` = `kalloc(64)`, reads 64 bytes through the real `entropy_buffer_read`,
and reaches `ccdrbg_init`, which is `blx r6` with `r6 = infop->init` — the pointer the factory just wrote,
i.e. **`yarrow_init`, in the object being linked**. That dispatch is at 0x800382FC and is *not* the stop,
because its target is real. `yarrow_init` (object +0x2C4, 0xAC) then makes five calls and the first is the
frontier:

```
2e4: bl  prngInitialize      <- THE STOP          (YarrowCoreLib_src_prng.o)
2f8: bl  panic               ; only if prngInitialize returned nonzero
314: bl  prngInput
338: bl  prngOutput
34c: bl  prngForceReseed
358: bl  fips_initialize     ; object-local, real
```

`prngInitialize` is a *created* stub — it is in this step's own `added` column — so the run stops one call
into `yarrow_init`, before any of the FIPS or SHA-1 work. The stop is therefore reached through **two**
indirect dispatches: the factory call in 366, then `infop->init` here. That is the shape 365 named (a stop
two virtual dispatches further on, inside a plane the step makes reachable), and 367 is the first step to
exercise it end to end — and the second dispatch's target *had* to be real for the first call inside it to
be the frontier at all.

## The key, composed rather than looked up

`0x8017BCEC` exists in no earlier image, so it was composed from three facts, each of them checkable
against the 366 map without a run:

```
uuid's .text 0x8017B5CC + 0x438                  = 0x8017BA04   the object's .text
yarrow_init is at object +0x2C4                  -> 0x8017BCC8
the `bl prngInitialize` is at object +0x2E4      -> 0x8017BCE8
and the field holds the return address, call +4  -> 0x8017BCEC
```

The last line is the convention 366 got wrong, and it is written out here because of that.

## What the run measured

```
 xnu_entry_checks=0x00000005              xnu_entry_failures=0x00000000
 xnu_entry_stub_caller_v=0x8017bcec       xnu_entry_abort_entries=0x00000000
 xnu_entry_stub_caller_digits=0x00000032
 xnu_entry_stub_caller_w0=0x37313038  ("8017")   w1=0x63656362  ("bcec")
 xnu_entry_abort_first_dfar=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=prngInitialize
No errors detected
```

**Name and key exactly as predicted.** The falsifiers, checked one by one: **(a)** `prngInput` at
`0x8017BD1C` did not fire — it is what stops the run if `prngInitialize` turned out to be real after all;
**(b)** there is no `panic` line and the log ends in the kernel's own `No errors detected`, and
`yarrow_init` only panics on a *nonzero* `prngInitialize`, which needs the stub to return and a stub never
returns; **(c)** no `ccdrbg_factory_yarrow` at `0x8003827C`, so the link took the object; **(d)** no
`PMPowerStateQueue` at `0x8014E6CC`, so `read_random` was entered again; **(e)** `abort_entries=0` and
`abort_first_dfar=0`, so the `blx r6` through `infop->init` did not fault — which is the measurement that
says the factory really did write a code pointer, not a zero or a storage stand-in.

## The layout

| | 366 measured | 367 predicted | 367 measured |
|---|---|---|---|
| counts | 778 / 676 / 102 | **783 / 681 / 102** | **783 / 681 / 102** |
| object `.text` | — | 0x8017BA04 (0x494) | **0x8017BA04 (0x494)** |
| `realstubs.o` `.text` | 0x8017BBB0 (0x3F60) | 0x8017C044 (0x3FD8) | **0x8017C044 (0x3FD8)** |
| object `.rodata` | — | 0x801A1740 (0x414) | **0x801A1744** (0x414) |
| object `.rodata.str1.1` | — | 0x801A1B54 (0x92) | **0x801A1B58** (0x92) |
| `realstubs.o` `.rodata.str1.4` | 0x801A17D8 (0x3A40) | 0x801A1C80 (0x3A80) | **0x801A2190 (0x3A80)** |
| `.text` end | 0x801A5BE0 | 0x801A65E0 | **0x801A65E0** |
| `.data` | 0x801A8000 (0x19A58) | 0x801A8000 (0x19A58) | **0x801A8000** (0x19A58) |
| `.sysctl_set` | 0x801C1A58 (0x150) | 0x801C1A58 (0x150) | **0x801C1A58** (0x150) |
| `.init_array` | 0x801C1BA8 (0x84) | 0x801C1BA8 (0x84) | **0x801C1BA8** (0x84) |
| its end | 0x801C1C2C | 0x801C1C2C | **0x801C1C2C** |
| `.bss` | 0x801C1C40 (0x390D8) | 0x801C1C40 (0x390D8) | **0x801C1C40** (0x390D8) |
| object `.bss` | — | 0x801F8578 (0x2C) | **0x801F8578 (0x2C)** |
| fill before `realstubs.o` `.bss` | 0x30 | **0x04** | **0x04** |
| `realstubs.o` `.bss` | 0x801F85C0 (0x2744) | 0x801F85C0 (0x2744) | **0x801F85C0** (0x2744) |
| `__bss_end` | 0x801FAD18 | 0x801FAD18 | **0x801FAD18** |
| image | 1842220 | 1842220 | **1842220** |
| headroom | 1069800 | 1069800 | **1069800** |

**The `.data` argument, shown and not asserted** (365's rule, and this time the terms are evaluated): the
`.text` run's own delta is `+0x494` (the object's `.text`) `+ 0x78` (five more stub bodies, `681 × 0x18 =
0x3FD8` against 676's `0x3F60`) = `+0x50C`, and the `.rodata` run's is `+0x414 + 0x92 + 0x40 = +0x4E6`
(the `0x40` being the net name slots: `−0x18` for the retired `ccdrbg_factory_yarrow`, `+0x58` for the six
created). The section grows `0x9F2` from `0x801A5BE0` to `0x801A65D2`, which `ALIGN(32)` closes at
**0x801A65E0** — measured exactly. The room under `.data` is `0x801A8000 − 0x801A65E0 = 0x1A20`, and
`0x9F2 < 0x1A20`, so `.data` cannot move; it did not.

`.bss` is the interesting row: **the pad rule's fourteenth confirmation, in the cancellation form** that
366 could not exercise because its object had no `.bss` at all:

```
IORangeAllocator .bss  0x801F855C (0x1C)  -> ends 0x801F8578
yarrow           .bss  0x801F8578 (0x2C)  -> ends 0x801F85A4     NEW
platform expert  .bss  0x801F85A4 (0x18)  -> ends 0x801F85BC
realstubs.o      .bss  align64 -> 0x801F85C0 (0x2744)            was 0x801F85C0
the fill in front of it: 0x30 -> 0x04 = (0x30 - 0x2C) mod 64
```

so `+0x2C` of new object and `−0x2C` of pad cancel **exactly**, and `.bss`'s size, `__bss_end`, the image
and the headroom are all 366's rows unchanged. `0x801F8578` is where the platform expert's `.bss` was in
366 — the object took the slot the previous image left the pad occupying.

## The one miss

**The `.rodata`-run row is 366's miss 2 again — and 366 wrote down the check that catches it.** The block
claimed

```
realstubs.o .rodata.str1.4   0x801A17D8  ->  0x801A1C80
```

and the map says **0x801A2190**. The *size* was right to the byte — `0x3A80 = 0x3A40 − 0x18 + 0x58`, the
retired name `ccdrbg_factory_yarrow` (`align4(21 + 1) = 0x18`) out and six created names in
(`prngInitialize` 0x10, `prngInput` 0xC, `prngOutput` 0xC, `prngForceReseed` 0x10, `FIPS_SHA1Init` 0x10,
`FIPS_SHA1Update` 0x10, for 0x58) — and the offset is wrong by **+0x510**, which is a whole term left out:
`0x801A1C80 = 0x801A17D8 + 0x4A6 + 0x2`, i.e. **only the `.rodata` run's own new inputs.** The row was
carried forward as if the `.rodata` run began where it began in 366 — but the **`.text` run** is what sits
in front of it, and this step's grew by `+0x50C`. Adding that one term gives

```
0x801A17D8 + 0x50C + 0x4A6 = 0x801A218A
```

against a measured 0x801A2190, the remaining 0x6 being alignment the model does not carry. The rule —
320's, measured directly at 366 — is:

> the `.rodata` run begins where the whole `.text` run ends, so every `.rodata` row shifts by the
> `.text` run's net delta, and *then* by whatever `.rodata` inputs were inserted ahead of it in the run.

366 ended its miss-2 paragraph by calling that "a check that would have caught miss 2 before the build."
This step had the check in hand, in the same file, thirty lines up, and did not run it. **A rule written
down in a defect note is not a check until it is arithmetic in the prediction.** The object's own two
`.rodata` rows were 0x4 low for a smaller version of the same thing — placed at 0x801A1740 and 0x801A1B54
where the map has 0x801A1744 and 0x801A1B58, the 0x4 being the `*fill*` of 0x2 the linker inserts at
0x801A1742 to align the incoming `.rodata`.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img`; nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of
either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`, `abort_entries=0`, no
`exception:` line. Log 301626 bytes, 3975 lines, last line `No errors detected`. The device came back to
Android on its own (`MI 4LTE`, release 10). Nothing in the step is new hardware access: the object reads no
device and its only new code path runs after `read_random` has already returned, and it reaches a reporting
stub before any FIPS or SHA-1 state is touched.
