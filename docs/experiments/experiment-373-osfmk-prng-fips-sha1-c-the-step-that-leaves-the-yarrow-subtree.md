# Experiment 373 — `osfmk/prng/fips_sha1.c`: the step whose self-test runs, and the first stop outside `osfmk/prng/`

**Step:** link one object, `osfmk/prng/fips_sha1.c` (`osfmk_prng_fips_sha1.o`) — the pool definer of 372's
stop `FIPS_SHA1Init`, and therefore of `FIPS_SHA1Update` as well. It is inserted into the entry link between
`osfmk_prng_YarrowCoreLib_src_yarrowUtils.o` and `stages/stage90/xnu_platform/MSM8974PlatformExpert.o`.
Nothing else changes.

**Prediction:** *2 resolved (2 function) / 0 added — 779 → **777** undefined, 677 → **675** function,
102 → **102** storage*; object `.text` **0x8017DBC0** (0x1600, after an 0x8 alignment fill); `.data`
**0x801AC000 (0x19AB0)** — the address unmoved, the size grown by 0x40; text size **1742880**; and the stop
at **`IOPMPowerStateQueue::PMPowerStateQueue`** at key **0x8014E6D0** — a C++ constructor, and the first
stop in this walk that is not a `osfmk/prng/` name.

**Result:** all three counts exact, every `.text` placement exact, the `.rodata` row exact **in both its
parts**, the whole `.data` derivation exact, the text size exact to the byte, every derived layout number
exact, the stop's name and key exact with **no falsifier firing — and no `panic`**, which is the reading
this step was actually about — and **one miss, of 0x5C**, on a row that was located relative to a
neighbouring object instead of derived.

## The object

```
resolved (2: 2 function, 0 storage)
    FIPS_SHA1Init
    FIPS_SHA1Update
added (0: 0 function, 0 storage)
of the 2 references, 2 are already satisfied
```

`.text` is **0x1600** — the largest single step since 370 — with four definitions and a **16-byte**
alignment:

| | | |
|---|---|---|
| `FIPS_SHA1Init` | 0x40 | no relocation at all: `vld1.64 {d16-d17}, [r2 :128]` loads the SHA-1 IV from object+0x30, stores it, returns — a leaf |
| `FIPS_SHA1Update` | 0xE0 | `memcpy` at +0xA0, the local `SHA1Transform` at +0xB4 and +0xDC, tail `b memcpy` at +0x11C |
| `SHA1Transform` | 0x1394 | local; the second SHA-1 compression function this walk links |
| `FIPS_SHA1Final` | 0x14C | defined, referenced by nothing yet |

plus a `.data` of **0x40** (one object, `PADDING`, alignment 1) — **the first `.data` input in eleven
steps** — and nothing else allocatable.

**Both of its references are `memcpy` and `memset`, both already real**, so this is 370's shape: an object
whose entire closure is already satisfied, which cannot stop the run inside itself. And the `vld1.64`
with the `:128` alignment hint is a **functional** row, not bookkeeping: the IV is at object+0x30, so it is
16-byte aligned only because the object's own 16-byte `.text` alignment is honoured. A 4-aligned placement
would abort, which is exactly the trap `predict_layout.py`'s relative-path default hid at 370 — and why the
0x8 of fill this step's placement needs is a row worth predicting rather than rounding away.

## The stop: out of the PRNG, into a constructor

```
random_block+0x104   bl FIPS_SHA1Init     real now, and a leaf: it returns
random_block+0x114   bl FIPS_SHA1Update   real now
random_block+0x128   bl FIPS_SHA1Update   real now
random_block+0x19C   beq 1f8              TAKEN - `state->0x30` is 0 on the first call, so the CRC
                                          comparison is skipped, the flag is set, and the function
                                          returns to `fips_initialize` at +0x254
fips_initialize+0x264 bl memcmp           real; on agreement `beq 0x27C` falls through to the **tail
                                          call** `b random_block(state, state+0x18, 1)`
random_block+0x0D0   bl prngOutput        **the call 372's block said was skipped - now executed**,
                                          because this second call passes `r2 = 1`
random_block+0x1A0   cmp r0, r1 / bne     the second CRC differs from the first, so it returns instead
                                          of taking the `panic` at +0x1EC
yarrow_init+0x35C                         returns r0 = `prngForceReseed`'s 0
read_random+0x12C    bl cc_clear          real; read_random has waited at +0x128 since 367
read_random+0x238    blx r7               the ccdrbg record's **generate** slot = `yarrow_generate`
yarrow_generate+0x3D8 bl memmove          real; 16 bytes out of state+0x18, then it returns 0
read_random+0x260    bl lck_mtx_unlock    real; read_random returns
uuid_generate+0x13C                       version/variant bits, then it returns
IOPMrootDomain::start+0x5F4 .. +0x77C     uuid_unparse_upper, memcpy, OSArray::withCapacity,
                                          OSSymbol::withCString x5, IOLockAlloc,
                                          OSDictionary::withCapacity, OSSet::withCapacity x2,
                                          OSArray::withObjects x2, IOService::getIOPMWorkloop
                                          - all real, and the walker agrees: no stub above any of them
IOPMrootDomain::start+0x780 bl IOPMPowerStateQueue::PMPowerStateQueue   <- THE STOP
```

The key: `IOPMrootDomain::start` is at `0x8014DF4C`, the `bl` at +0x780 is `0x8014E6CC`, and the field
holds the return address, **+4 → `0x8014E6D0`**.

**The check that made this safe to predict** is `tools/first_stub_call.py`, which reads one function's own
body from an offset and takes the first `bl` whose target name is a stub. On
`_ZN14IOPMrootDomain5startEP9IOService` from `+0x5F4` it reports `+0x780` — and between `+0x5F4` and
`+0x77C` there is **no return and no conditional branch at all** (the only branches are four `blx` vtable
calls), so the straight line is the executed line. The three names that could have stopped it earlier —
`uuid_generate` (365), `read_random` (366) and `yarrow_init`'s whole subtree (367..373) — are the walk's own
recent history.

**This is the step where the walk leaves the subtree it has been inside for seven steps.** Every prediction
since 367 was a stop inside `osfmk/prng/`; this one has `yarrow_init` returning for the first time,
`read_random` running its whole tail including the generate call, `uuid_generate` finishing, and
`IOPMrootDomain::start` — stopped at its `bl uuid_generate` since 365 — resuming at `+0x5F4` and running
100 instructions of real C++ before it needs a name the image does not define.

## What the run measured

```
 xnu_entry_checks=0x00000005              xnu_entry_failures=0x00000000
 xnu_entry_stub_caller_v=0x8014e6d0       xnu_entry_abort_entries=0x00000000
 xnu_entry_stub_caller_digits=0x00000062
 xnu_entry_stub_caller_w0=0x34313038 ("8014")   w1=0x30643665 ("e6d0")
 xnu_entry_abort_first_dfar=0x00000000    xnu_entry_abort_first_pc=0x00000000
 xnu_entry_image_bytes=0x001c5c84         xnu_entry_bss_start=0x801c5cc0
 xnu_entry_bss_end=0x801fedd8             xnu_entry_args_pa=0x80200000
 xnu_entry_top_of_kernel_data=0x80400000  xnu_entry_why_byte=0x00000061
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN19IOPMPowerStateQueue17PMPowerStateQueueEP8OSObjectPFvS1_zE
No errors detected
```

**Name and key exactly as predicted, and no `panic`** — which is the first thing this step had to read,
because it is the first step on this path that links code whose own self-test runs.

The falsifiers, checked one by one: **(a)** no `panic` carrying `FIPS random self test failed` (`.L.str.1`,
**without** the hyphen) — `fips_initialize`'s known-answer `memcmp` at +0x264 **agreed**, so the FIPS SHA-1
this step links reproduces `kKnownAnswer`; **(b)** no `panic` carrying `FIPS random self-test failed.`
(`.L.str`, **with** the hyphen) — `random_block`'s repeated-CRC test at +0x1EC did not fire, i.e.
`prngOutput` really did change the block that is hashed twice; **(c)** no `panic` carrying
`Couldn't initialize Yarrow, ...` or `Couldn't seed Yarrow.` — `yarrow_init`'s guards at +0x2E8 and +0x31C
saw zeros; **(d)** no `FIPS_SHA1Init` at `0x8017BB0C` and no `FIPS_SHA1Update` at `0x8017BB48`, so both
names took and the stop is past them; **(e)** no stop at any of the four vtable `blx` sites in the tail of
`IOPMrootDomain::start`, so no stub is hiding behind a vtable slot (365's kind 9); **(f)** `abort_entries=0`
with `abort_first_pc=0` and `abort_first_dfar=0` — the 0x1600 bytes of SHA-1 ran over the 0xAC-byte
context, and **the `vld1.64 {d16-d17}, [r2 :128]` did not fault**, which is the reading that certifies the
16-byte alignment was honoured.

## The layout

| | 372 measured | 373 predicted | 373 measured |
|---|---|---|---|
| counts | 779 / 677 / 102 | **777 / 675 / 102** | **777 / 675 / 102** |
| object `.text` | — | 0x8017DBC0 (0x1600, after an 0x8 fill) | **0x8017DBC0 (0x1600)** |
| platform expert `.text` | 0x8017DBB8 (0x150) | 0x8017F1C0 (0x150) | **0x8017F1C0 (0x150)** |
| `realstubs.o` `.text` | 0x8017DD64 (0x3F78) | 0x8017F310 (0x3F48 = 675 × 0x18) | **0x8017F36C (0x3F48)** |
| its end | 0x80181CDC | 0x80183258 | **0x801832B4** |
| `realstubs.o` `.rodata.str1.4` | 0x801A3E54 (0x3A48) | 0x801A542C (0x3A28) | **0x801A542C (0x3A28)** |
| `.text` end | 0x801A8260 | 0x801A9820 | **0x801A9820** |
| text size | 1737312 | 1742880 | **1742880 (= 0x1A9820)** |
| `.data` | 0x801AC000 (0x19A70) | 0x801AC000 (0x19AB0) | **0x801AC000 (0x19AB0)** |
| `.sysctl_set` | 0x801C5A70 (0x150) | 0x801C5AB0 (0x150) | **0x801C5AB0 (0x150)** |
| `.init_array` | 0x801C5BC0 (0x84) | 0x801C5C00 (0x84) | **0x801C5C00 (0x84)** |
| its end | 0x801C5C44 | 0x801C5C84 | **0x801C5C84** |
| `.bss` | 0x801C5C80 (0x39118) | 0x801C5CC0 (0x39118) | **0x801C5CC0 (0x39118)** |
| `__bss_end` | 0x801FED98 | 0x801FEDD8 | **0x801FEDD8** |
| image | 1858628 | 1858692 | **1858692 (= 0x1C5C84)** |
| headroom | 2101864 | 2101800 | **2101800 (= 0x201228)** |
| args / topOfKernelData / tree / window | +2097152 / +4194304 / +6291456 / 8388608 | all unmoved | **all unmoved** |

**The `.data` derivation, because four earlier steps each cost one to this section.** The map says the
section's last input is `osfmk_prng_YarrowCoreLib_port_smf.o`'s `__DATA, __data` at 0x801C5A58 (0x18),
ending on 0x801C5A70, and the `. = ALIGN(0x8)` after it is already satisfied. The new object sits **after**
every other `.data` producer in `LINK_OBJS`, and its `.data` alignment is 1, so its 0x40 lands exactly at
0x801C5A70 and pushes `.sysctl_set` and everything below it by +0x40 — a *size* change with no *address*
change, because `align_up(0x801A9820, 0x4000)` is still 0x801AC000.

**The `.bss` pad rule is not exercised**: no `.bss` input, and `.bss`'s start moves by +0x40 — a multiple of
64 — so every pad inside it is unchanged and the section is a copy in size (0x39118) and a move in place.

**And the derived numbers do not move even though the image grows**: `args` is
`align_up(bss_end − ENTRY_BASE, 0x1000) + 0x1000`, and align_up(0x1FEDD8, 0x1000) is still 0x1FF000, so
`args`, `topOfKernelData`, the tree, the window and the payload's whole layout are 372's — only `headroom`
changes, by exactly the 0x40 the image grew. The build's five invariants all passed, and the copied image
ends 60 bytes below `__bss_start`, so the payload's `memset` touches nothing that was copied.

**The boundary fill measured 0x0 again.** The `.rodata` row is exact in both parts this time
(0x801A542C, size 0x3A28 = 0x3A48 − 0x20), which makes the fourth reading of the term 368 first named:
368 absorbed 0x4, 371 0x0, 372 0x4, **373 0x0**.

## The one miss: 0x5C, a row located rather than derived

`realstubs.o`'s `.text` does not begin where the platform expert's `.text` row ends. Three more inputs sit
between them, all constants of this image:

```
0x8017F310  0x4   MSM8974PlatformExpert.o's .text._ZN21MSM8974PlatformExpert9MetaClassD0Ev
                  (a separate input of the same object, matched by the `.text.*` wildcard)
0x8017F314  0x4   xnu_arm_entry_last_kernel_constructor.o's .text.startup
0x8017F318  0x54  xnu_arm_entry_rtabi.o's .text.eabi (the __aeabi_mem* family)
0x8017F36C        -> realstubs.o's .text
```

The 372 map has the same gap and the same size (0x8017DD08 + 0x5C = 0x8017DD64), so this is a constant of
the image that has to be carried into the row, not a property of this step. **Tell: the derived rows in a
prediction have to be derived, and a row that is instead located relative to a neighbouring object has to
name the objects in between** — 366's `.rodata` row was the same shape and was also wrong until it was
turned into arithmetic. Nothing derived from this row moved: the text size, the section end and every
number below them are sums, and the sums were right.

## The next object, named before its run

The frontier is defined by **`iokit_Kernel_IOPMPowerStateQueue.o`** (`iokit/Kernel/IOPMPowerStateQueue.cpp`)
— the only object in the pool that defines
`_ZN19IOPMPowerStateQueue17PMPowerStateQueueEP8OSObjectPFvS1_zE`, and the only one that defines
`_ZN19IOPMPowerStateQueue16submitPowerEventEjPvy` either, the *other* stub this object's header names and
the one `IOPMrootDomain::start` reaches later. Measured against this image: 23 definitions, 47 references
(all satisfied), **2 resolved (2 function) / 0 added** — **777 → 775 undefined, 675 → 673 function,
102 → 102 storage**. Unlike the last five objects it is not text-only: `.text` **0x318**, `.rodata`
**0xB4**, `.rodata.str1.1` **0x14**, `.bss` **0x18**, and **`.init_array` 0x4** — a static constructor, so
374 is the step where `.init_array` next grows, and the first step since 364 with a `.bss` input.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img`; nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of
either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`, `checksum=0x907fedd9`,
`abort_entries=0`, no `exception:` line, no `panic`. Log 301674 bytes, 3975 lines, last line
`No errors detected`. The device came back to Android on its own (`MI 4LTE`, release 10).

The step's new code is a SHA-1 over a 0xAC-byte context the previous step allocated, plus 16 bytes of
`memmove` out of the PRNG state — CPU work over memory, no device read, no allocation, and the stop is a
reporting stub, so the constructor the frontier names never runs.
