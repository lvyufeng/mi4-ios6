# Experiment 368 — `YarrowCoreLib/src/prng.c`: the definer of 367's stop, and the first object with one allocated section

**Step:** link one object, `osfmk/prng/YarrowCoreLib/src/prng.c` (`osfmk_prng_YarrowCoreLib_src_prng.o`) —
the pool definer of 367's stop `prngInitialize`. It is inserted into the entry link between
`osfmk_prng_prng_yarrow.o` and `stages/stage90/xnu_platform/MSM8974PlatformExpert.o`. Nothing else changes.

**Prediction:** *4 resolved (4 function) / 11 added (11 function), 0 storage either way — 783 → **790**
undefined, 681 → **688** function, 102 → **102** storage*; object `.text` **0x8017BE98** (0x7C4); `.text` end
**0x801A6EC0**; everything below `.text` unmoved; and the stop at **`mmInit`** at key **`0x8017BEA8`**.

**Result:** all three counts exact, every `.text` placement exact, every section boundary below `.text`
exact, the **stop name and key exactly as predicted with no falsifier firing** — and one defect, in the
*same* form as 366's and 367's: a sum that was written down and never added.

## The object

Eight definitions, all `T`, in one `.text` of **0x7C4**: the four the pool stops on (`prngInitialize`
0x13C, `prngOutput` 0x164, `prngForceReseed` 0x1B8, `prngInput` 0x4C) plus `prngProcessSeedBuffer` (0xE4),
`prngStretch` (0xDC), `prngAllowReseed` (0x124) and `prngDestroy` (0x3C), which nothing references yet.

**It has no `.rodata`, no `.rodata.str1.1`, no `.bss`, no `.data`, no `.sysctl_set` and no `.init_array`** —
four sections in the object file, two of them (`comment`, `ARM.attributes`) not allocatable. It is the first
object in this walk with a single allocated section, and that is what makes the layout arithmetic short: the
`.rodata`-run term is the *name slots only*, and there is no `.bss` term at all, so `.bss`'s size
*cannot* change and the pad rule is not exercised (366's case, not 367's).

The three already-satisfied references of its fourteen are `mach_absolute_time` (0x8000DA48), `memcpy`
(0x800036A0) and `memset` (0x800039C8) — all real, all used by `prngInitialize` itself.

## The stop

`prngInitialize` is 0x13C bytes, and the first call in it is the frontier:

```
   0: push {r4-r9, fp, lr} ; 4: sub sp, #0x60   ; 8: mov r8, r0
   c: bl  mmInit          <- THE STOP           (YarrowCoreLib/port/smf.o)
  10: mov r0, #0xAC       ; 14: bl mmMalloc
  28: bl  mmGetPtr        ; 3c: bl memset (real)
  58: bl  YSHA1Init       ; 64: bl YSHA1Init   ; 74/88: bl YSHA1Update
  94: bl  YSHA1Final      ; a8: bl memcpy (real)
  b4: bl  YSHA1Init       ; c4: bl YSHA1Update ; d0: bl YSHA1Final
 e0/f4/104: bl comp_init ; 12c: bl mmFree
```

So the frontier is `mmInit`, one call past the prologue. Everything else in the body is either a stub this
step creates — the cascade `mmMalloc` 0x14, `mmGetPtr` 0x28, then the three `YSHA1*` names, then the three
`comp_*` names — or one of the three real ones.

## The key, composed

```
yarrow's .text 0x8017BA04 + 0x494            = 0x8017BE98   the object's .text
prngInitialize is at object +0x0             -> 0x8017BE98
the `bl mmInit` is at object +0xC            -> 0x8017BEA4
and the field holds the return address, +4   -> 0x8017BEA8
```

## What the run measured

```
 xnu_entry_checks=0x00000005              xnu_entry_failures=0x00000000
 xnu_entry_stub_caller_v=0x8017bea8       xnu_entry_abort_entries=0x00000000
 xnu_entry_stub_caller_digits=0x0000002a
 xnu_entry_stub_caller_w0=0x37313038 ("8017")   w1=0x38616562 ("bea8")
 xnu_entry_abort_first_dfar=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=mmInit
No errors detected
```

Name and key exact; the ASCII pair spells the key back. The falsifiers, checked one by one: **(a)** no
`mmMalloc` at 0x8017BEB0 and no `mmGetPtr` at 0x8017BEC4, so the *first* call in `prngInitialize` is the one
that stopped; **(b)** no `panic` line and the log ends in the kernel's own `No errors detected` —
`yarrow_init` panics only on a nonzero `prngInitialize`, which needs the stub to return; **(c)** no
`prngInitialize` at 0x8017BCEC, so the link took the object — and that is now a two-way fact, because 367's
own stop was that name and this run went *through* it; **(d)** no `YSHA1Init` at 0x8017BEF4; **(e)**
`abort_entries=0` and `abort_first_dfar=0`.

Between 367 and 368 the machine went through the yarrow factory, through `ccdrbg_init`'s `blx r6`, through
`yarrow_init`'s first call, and into a body that is **real instructions this time**.

## The layout

| | 367 measured | 368 predicted | 368 measured |
|---|---|---|---|
| counts | 783 / 681 / 102 | **790 / 688 / 102** | **790 / 688 / 102** |
| object `.text` | — | 0x8017BE98 (0x7C4) | **0x8017BE98 (0x7C4)** |
| `realstubs.o` `.text` | 0x8017C044 (0x3FD8) | 0x8017C808 (0x4080) | **0x8017C808 (0x4080)** |
| platform expert `.text` | 0x8017BE98 | 0x8017C65C | **0x8017C65C (0x150)** |
| object `.rodata` / `.bss` | — | none | **none** |
| `realstubs.o` `.rodata.str1.4` | 0x801A2190 (0x3A80) | 0x801A29FC (0x3AE0) | **0x801A29F8 (0x3AC8)** |
| `.text` end | 0x801A65E0 | 0x801A6EC0 | **0x801A6E80** |
| text size | 1730016 | 1732288 | **1732224** |
| `.data` | 0x801A8000 (0x19A58) | 0x801A8000 (0x19A58) | **0x801A8000** (0x19A58) |
| `.sysctl_set` | 0x801C1A58 (0x150) | 0x801C1A58 (0x150) | **0x801C1A58** (0x150) |
| `.init_array` | 0x801C1BA8 (0x84) | 0x801C1BA8 (0x84) | **0x801C1BA8** (0x84) |
| its end | 0x801C1C2C | 0x801C1C2C | **0x801C1C2C** |
| `.bss` | 0x801C1C40 (0x390D8) | 0x801C1C40 (0x390D8) | **0x801C1C40** (0x390D8) |
| the `0x04` fill before `realstubs.o` `.bss` | 0x801F85BC, 0x4 | 0x801F85BC, 0x4 | **0x801F85BC, 0x4** |
| `realstubs.o` `.bss` | 0x801F85C0 (0x2744) | 0x801F85C0 (0x2744) | **0x801F85C0** (0x2744) |
| `__bss_end` | 0x801FAD18 | 0x801FAD18 | **0x801FAD18** |
| image | 1842220 | 1842220 | **1842220** |
| headroom | 1069800 | 1069800 | **1069800** |

The `.data` argument held as written on both sides of the error: the room was `0x801A8000 − 0x801A65E0 =
0x1A20` in 367 and `0x801A8000 − 0x801A6EC0 = 0x1140` under this block's own end, against a step of 0x8CC.

## The miss: eleven addends, printed, never added

The block enumerated all eleven created name slots and then summed them wrong: it wrote `+0x98` where the
eleven terms it printed are `0x60` of eight `0xC`-names plus `comp_get_ratio`'s `0x10` plus `mmFree`'s and
`mmInit`'s `0x8` — **0x80**. The measured `realstubs.o` `.rodata.str1.4` size is `0x3AC8 = 0x3A80 − 0x38 +
0x48`, i.e. exactly `created − retired` with `created = 0x80`. The object-side arithmetic was right and only
its **sum** was not; the consequence is a `.text` end predicted 0x18 high on that term.

That is 357's rule a third time, in its strongest form yet. 366 wrote `676 × 0x18` and never multiplied; 367
wrote the run-order rule and never added it to a row; 368 wrote eleven addends and never added *them*. The
difference here is that the addends are all printed, in order, one per line — the sum is one `python3 -c`
away. So the rule for the next block is not "show the terms" but **print the sum of the terms you just
printed**.

The remaining 0x28 of the 0x40 error is the model's own limit, and it is worth decomposing because it is the
first time this walk has seen it in `.text` rather than `.rodata`:

* **0x4** of fill at the boundary between the `.text` run and the `.rodata` run: uuid's `.rodata` went
  0x801A1651 → **0x801A1EB9**, a shift of **0x868** against the `.text` run's `+0x86C`. So the rule for a
  `.rodata` row is right and its term is 0x4 finer than the model — the rows shift by the `.text` run's
  delta *as the linker consumed it*.
* **0x10** of tail pad that 367 needed and 368 does not. In 367 the last `.text` row (`.ARM.exidx`, 0x8 at
  0x801A65C8) ended at 0x801A65D0 and the section closed at 0x801A65E0 with 0x10 of alignment; in 368 the
  same row moved to **0x801A6E78** — a row shift of exactly **0x8B0** — and ends at 0x801A6E80, already
  32-aligned, so the section closes with no pad at all. **A section end is a row shift plus a pad, and the
  pad is not the same on both sides of a step.**

0x18 + 0x4 + 0x10 = 0x2C of the 0x40; the rest is the `ALIGN(32)` closing (the model rounds its own raw end
up from 0x801A6EAC to 0x801A6EC0, while the real raw end was already 32-aligned).

## The next object, named before its run

The frontier is `mmInit`, whose pool definer is **`osfmk_prng_YarrowCoreLib_port_smf.o`**
(`YarrowCoreLib/port/smf.c`) — the only object in the 695-object pool that defines `mmInit`, and the only one
that defines `mmMalloc`, `mmGetPtr` and `mmFree` either, so one step retires all four. Measured against this
image: 6 definitions, 2 references, **4 resolved (4 function) / 0 added**, both references already satisfied —
**790 → 786 undefined, 688 → 684 function, 102 → 102 storage**. It is 0x38 of `.text` and **0x18 of
`__DATA, __data`**, which makes 369 the first step since 364 whose `.data` can move.

**And 369's stop will not be in 369's object.** All four `mm*` names retire at once and the object adds
nothing, so the run continues inside `prngInitialize` past `mmMalloc` (0x14), `mmGetPtr` (0x28) and the real
`memset` (0x3C) to the next stub in address order: `YSHA1Init` at object +0x58, key **0x8017BEF4**, whose
definer is `osfmk_prng_YarrowCoreLib_src_sha1mod.o` (`.text` 0x14AC, `.bss` 0x40). That is 366's shape — a
step that retires names without being the step that stops.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img`; nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading of
either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`, `abort_entries=0`, no `exception:`
line. Log 301618 bytes, 3975 lines, last line `No errors detected`. The device came back to Android on its
own (`MI 4LTE`, release 10). Nothing in the step is new hardware access: the object reads no device, and its
first executed call is a reporting stub.
