# Experiment 366 — `uuid.c`: the stop one frame deeper than the object being linked

**Step:** link one object, `libkern/uuid/uuid.c` (`libkern_uuid_uuid.o`) — the pool definer of 365's stop
`uuid_generate`. It is inserted into the entry link between `iokit_Kernel_IORangeAllocator.o` and
`stages/stage90/xnu_platform/MSM8974PlatformExpert.o`. Nothing else changes.

**Prediction:** *5 resolved (5 function) / 2 added (2 function), 0 storage either way — 781 → **778**
undefined, 679 → **676** function, 102 → **102** storage;* object `.text` **0x8017B5CC** (0x438);
`realstubs.o` `.text` 0x8017BBB0; `.text` end **0x801A5C20**; `.data`, `.sysctl_set`, `.init_array`,
`.bss`, `__bss_end`, image and headroom all **unmoved**; and the stop at
**`ccdrbg_factory_yarrow`** at key **`0x80038278`**.

**Result:** all three counts exact, every row the prediction derived from a *boundary* argument exact, the
**stop name exactly as predicted** with no falsifier firing — and **four misses in the block's own
arithmetic**, one of which was a closed form written out and never evaluated.

## Why the stop is one frame past the object

365 stopped at `uuid_generate` from `IOPMrootDomain::start`. The effect tool against the 365 image:

```
resolved (5: 5 function, 0 storage)
    uuid_compare   uuid_generate   uuid_parse   uuid_unparse   uuid_unparse_upper
added (2: 2 function, 0 storage)
    sscanf         uuid_get_ethernet
of the 9 references, 7 are already satisfied
```

`uuid_generate` is 0x34 bytes and calls exactly one thing:

```
12c: push {r4, lr}; mov r1, #16; mov r4, r0
138: bl   read_random                      <- real, 0x800381D4, 0x28C bytes
13c: ldrb r0,[r4,#6] ... bfi ... strb      ; the version nibble
154: ldrb r1,[r4,#8] ... bfi ... strb      ; the variant bits
15c: pop {r4, pc}
```

so the stop is inside `read_random` (`osfmk/prng/random.c:575`), and this is the **first time anything in
this kernel asks for random bytes**. Its first statement is `lck_mtx_lock`, then
`prng_infop(current_prng_context())` — inlined, and the whole thing is visible in the image:

```
80038210: ldr sl,[r0],#8          ; sl = pp->infop
8003821c: bne read_random+0x1ac   ; if (pp->infop) return it - false on the first call
80038220: movw/movt r6 = 0x801CD838     ; &prng_ccdrbg_factory
80038228: ldr r0,[r6]
80038230: beq read_random+0x140   ; if (factory == NULL) -> the 10-second wait loop
8003824c: bl  kalloc_canblock     ; pp->infop = kalloc(sizeof(ccdrbg_info))
8003826c: ldr r2,[r6]             ; r2 = prng_ccdrbg_factory
80038278: blx r2                  <- THE STOP
```

## Why `prng_ccdrbg_factory` is not NULL

`kernel_bootstrap_thread` calls `prng_cpu_init(master_cpu)` at **0x8000E690** — *before* its
`PE_init_iokit` at 0x8000E6B0, i.e. before the walk entered IOKit at all at step 360. And `prng_cpu_init`
(`osfmk/prng/random.c:497`) ends with the line the source marks *"XXX Temporary registration"*:

```c
	prng_factory_register(ccdrbg_factory_yarrow);
```

`prng_factory_register` is real and does `prng_ccdrbg_factory = factory; thread_wakeup(...)`, so the
global now holds **the address of the yarrow stub itself** — `ccdrbg_factory_yarrow` is `func ... T` in
this image, 0x18 bytes of reporting tail. The argument is literally `master_cpu`, so the
`if (cpu != master_cpu) return;` above it cannot be taken: the registration happens on every boot.

That makes this a stop with an unusual shape worth naming: **kind 1, but the object that retires it is not
the object being linked** — a five-step-old stub that only became *reachable* now, because `uuid.c` is what
made `read_random` run for the first time.

If the factory had been NULL the run would have entered `prng_infop`'s wait loop instead and read
`panic("prng_ccdrbg_factory registration timeout")` after ten seconds — which is falsifier (b), and it is
the reason the NULL case had to be settled by reading the caller order rather than assumed.

## What the run measured

```
 xnu_entry_why=0x801802f8          xnu_entry_why_byte=0x00000061   'a'
 xnu_entry_stub_caller_v=0x8003827c        xnu_entry_abort_entries=0x00000000
 xnu_entry_abort_first_dfar=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=ccdrbg_factory_yarrow
```

The name is exact, `abort_entries=0`, no `exception:` line, the log ends in the kernel's own
`No errors detected`. `uuid_generate` ran and returned into `read_random`, which locked its mutex, found
`pp->infop` NULL, found the factory non-NULL, allocated the `ccdrbg_info`, and called the yarrow factory.

**The key is off by one instruction, and it is a convention slip worth keeping.** The block wrote
`xnu_entry_stub_caller_v=0x80038278` and named `read_random + 0xa4` beside it — and 0x800381D4 + 0xA4 is
genuinely the `blx r2`. But the field holds the **return address**, call + 4: 365's 0x8014E540 for a `bl`
at 0x8014E53C, 364's 0x8015D98C for 0x8015D988, 354's 0x8011B2B4 for 0x8011B2B0. The device reported
**0x8003827C**. The site was right and the value was one instruction early; the same slip the other way
would point at the previous statement's tail and read as a different miss entirely.

## What the layout did

| | 365 measured | 366 predicted | 366 measured |
|---|---|---|---|
| counts | 781 / 679 / 102 | **778 / 676 / 102** | **778 / 676 / 102** |
| object `.text` | — | **0x8017B5CC** (0x438) | **0x8017B5CC** (0x438) |
| `realstubs.o` `.text` | 0x8017B778 (0x3FA8) | 0x8017BBB0 (**0x3F90**) | **0x8017BBB0 (0x3F60)** |
| object `.rodata` | — | 0x801A0D54 (0x10) | **0x801A1141** (0x10) |
| `realstubs.o` `.rodata.str1.4` | 0x801A12F8 (0x3A74) | 0x801A13EC (0x3A40) | **0x801A17D8 (0x3A40)** |
| `.text` end | 0x801A5720 | 0x801A5C20, band 0x801A5BF0..0x801A5C40 | **0x801A5BE0** |
| `.data` | 0x801A8000 (0x19A58) | 0x801A8000 (0x19A58) | **0x801A8000** (0x19A58) |
| `.sysctl_set` | 0x801C1A58 (0x150) | 0x801C1A58 (0x150) | **0x801C1A58** (0x150) |
| `.init_array` | 0x801C1BA8 (0x84) | 0x801C1BA8 (0x84) | **0x801C1BA8** (0x84) |
| its end | 0x801C1C2C | 0x801C1C2C | **0x801C1C2C** |
| `.bss` | 0x801C1C40 (0x390D8) | 0x801C1C40 (0x390D8) | **0x801C1C40** (0x390D8) |
| `__bss_end` | 0x801FAD18 | 0x801FAD18 | **0x801FAD18** |
| image | 1842220 | 1842220 | **1842220** |
| headroom | 1069800 | 1069800 | **1069800** |

**The whole bottom half is unmoved, and the reason is structural rather than lucky**: the object
contributes **no `.bss` at all** and no storage stand-in retires, so `realstubs.o`'s `.bss` keeps its
0x2744, the pad in front of it keeps its 0x30, and `.bss`'s size therefore *cannot* change. `.init_array`
is unmoved because `uuid.c` is C — there is no static constructor to add an entry.

This is also the first step in a while where **the `.data` argument was shown rather than asserted**, which
365's defect called for: 365's `.text` end is 0x801A5720 and `.data` starts at 0x801A8000, so the room is
`0x801A8000 - 0x801A5720 = 0x28E0`, against a step delta of well under 0x500. `.data` could not move.

## The four misses

**1. `676 × 0x18` was written as 0x3F90. It is 0x3F60.** The product appears in the table and again inside
the `.data` argument as the term `-0x18`, and neither had been evaluated: 0x3F90 = 16272 = **678** × 24, so
the shrink the argument needed was `-0x48`. That is 0x30 of the step's text delta and the whole of the
point-estimate error — with `-0x48` the arithmetic gives

```
0x801A5720 + 0x438 + 0x10 + 0xE1 - 0x48 - 0x34 = 0x801A5BCD
```

and the measured end is 0x801A5BE0, the remaining 0x13 being alignment the model does not carry. 365's rule
was *show the subtraction*; this step's is the second half of it — **evaluate the closed form, in the base
it will be compared in.** A block that prints `676 × 0x18 = 0x3F90` next to a measured `0x3F60` is
self-checking, which is exactly what 357 wrote down and this step did not do.

**2. The `.rodata`-run row came from the layout tool without asking whether the tool's own output was
self-consistent.** `predict_layout.py` prints, for this insertion,

```
0x801a5820 0x0438 .text  libkern_uuid_uuid.o   NEW
```

— a `.text` input placed at the *end* of the output section, after the `.rodata` run has begun. A `.text`
input cannot go there, so its `:ANCHOR` rule had failed to find a successor in the `.text` class and
appended instead, and **every `.rodata` row it printed was consequently 0x3F0 low**. The block copied
0x801A13EC from it and labelled the row "±0x40", which is 364's caveat magnitude and not this failure's.
**The tell is in the tool's own first rows, before any measurement: read them for a placement the linker
cannot produce.**

**3 and 4 are the measurement's own additions, and they are worth more than the rows.** The `.rodata` run
shifted by exactly **+0x3F0** — IORangeAllocator's `.rodata` went 0x801A0C94 → 0x801A1084 — and
`0x1084 - 0x0C94 = 0x3F0` is the `.text` run's own net delta, `+0x438 - 0x48`. That is the run-order rule
this walk has carried since 320 measured directly: **the `.rodata` run begins where the whole `.text` run
ends, so any change anywhere in `.text` moves every `.rodata` row by the same amount** — a check that would
have caught miss 2 before the build. And the name-slot arithmetic was **exact**: `realstubs.o`'s
`.rodata.str1.4` came out 0x3A40 against 0x3A74, which is `-0x50` for the five retired names
(`align4(len + 1)` = 0x10, 0x10, 0xC, 0x10, 0x14) `+0x1C` for the two created (`sscanf` 0x8,
`uuid_get_ethernet` 0x14).

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img`; nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of
either. `xnu_entry_checks=5` / `xnu_entry_failures=0`, `abort_entries=0`, no `exception:` line. Log 301633
bytes, last line `No errors detected`. The device came back to Android on its own (`MI 4LTE`, release 10).
Nothing in the step is new hardware access: the object it adds reads no device — the only new code path is
`read_random`, which the boot had already armed at `prng_cpu_init` and which reaches a reporting stub
before it touches any entropy source.
