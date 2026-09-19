# Experiment 353 — `IOInterruptAccounting.cpp`: the step that did not move the frontier

**Step:** link one object, `iokit/Kernel/IOInterruptAccounting.cpp`
(`iokit_Kernel_IOInterruptAccounting.o`, 6808 bytes) — the only pool definer of
`_Z23interruptAccountingInitv`, which the frontier block named as 353's *predicted* stop. Nothing else
changes.

**Prediction:** *4 resolved / 2 added — 850 → **848** undefined, 746 → **745** function, 104 → **103**
storage;* `.text` a band at +0x19C..+0x1B1 with every `__DATA` row unmoved; and **the stop at
`interruptAccountingInit` on `StartIOKit+0x104`, key `0x8011b2a4`.**

**Result:** all three counts exact, the text band exact at its low end, two alignment mechanisms read out of
the map — and **the stop did not move**: the run stopped on `devsw_init` at key `0x8011b26c`, byte for byte
where 352 stopped. The block contradicted itself — it said the linked object defines *this step's* stop and
that the run's stop would be that same name — and the walk's oldest rule, followed by every step since 332 and
never written down on its own line, is that **the step's object is the definer of the PREVIOUS step's stop**.
Linking the object that defines a stub is the thing that retires it, so the run cannot stop on the name the
step linked. The measurement is a negative one, and a clean one.

## The object

| `iokit_Kernel_IOInterruptAccounting.o` | |
|---|---|
| `.text` | **580** (0x244), 9 definitions |
| `.data` | **4** (`gInterruptAccountingStatisticBitmask`) |
| `.bss` | **16** (0x10) |
| `.rodata.str1.1` | **21** (0x15) |
| `.rodata`, COMDAT, `.init_array`, `__sysctl_set` | **none of them** |
| definitions / references | 9 / 7, five already satisfied |

**4 resolved / 2 added** — three functions (`_Z23interruptAccountingInitv`,
`_Z37interruptAccountingDataUpdateChannelsP25IOInterruptAccountingDataP16IOSimpleReporter`,
`_Z38interruptAccountingDataInheritChannelsP25IOInterruptAccountingDataP16IOSimpleReporter`) and the
`D 0x4` stand-in `gInterruptAccountingStatisticBitmask` out; `_ZN16IOSimpleReporter8getValueEy` and
`_ZN16IOSimpleReporter8setValueEyx` in (both `T` in `iokit_Kernel_IOSimpleReporter.o`, which is not linked) —
for 850 + 2 − 4 = **848**, **746 → 745 function, 104 → 103 storage**, 745 + 103 = 848.

**The two created names are one level below the stop**: they are called from
`interruptAccountingDataUpdateChannels` and `...InheritChannels`, the object's other two functions, not from
anything on the path to `StartIOKit`'s next stub — which is why this step's arithmetic was exact and its stop
was not where it was aimed.

## The rule this step pinned: the object retires the *previous* stop

Every block back to 332 has followed it. None has stated it as a sentence:

> **The step links the object that defines the stop the previous run reached.** The run then advances one
> call, to the next stub on the path, and *that* is the stop the block predicts.

Check it against the last four: 350 retired 349's stop `_ZN12IORootParent10initializeEv` and stopped on
`_ZN16IOPMinformeeList22getSharedRecursiveLockEv`; 351 retired that and stopped on
`_ZN16IOKitDiagnostics11diagnosticsEv`; 352 retired that and stopped on `devsw_init`; and 353's block named
`_Z23interruptAccountingInitv` in **both** roles — as what it retires and as what it would stop on. The first
is right, the second is impossible, and the run says so.

The mechanism was carried in every document as a *shape* ("the object defining this step's stop…"), and a shape
cannot be checked against itself. What made this step useful is that writing the name twice turned a shape into
a claim, and a claim can be falsified by a run.

## The layout: two alignments absorb the whole step

| | 352 | 353 measured | 353 predicted |
|---|---|---|---|
| `.text` | 0x80184960 (0x184960) | **0x80184B00** (0x184B00) | 0x80184B00..0x80184B20 ✓ (**the low end, exactly**) |
| `.data` | 0x80188000 (0x19840) | **0x80188000** (0x19840, **unmoved**) | 0x80188000, 0x19844 (4 high) |
| `.sysctl_set` | 0x801A1840 (0x140) | **0x801A1840** (0x140, unmoved) | 0x801A1844 |
| `.init_array` | 0x801A1980 (0x64, 25) | **0x801A1980** (0x64, twenty-five, unmoved) | 0x801A1984 |
| `.bss` | 0x801A1A00 (0x38058) | **0x801A1A00** (0x38058, **unmoved**) | 0x801A1A00, 0x38028 (0x30 low) |
| `__bss_end` | 0x801D9A58 | **0x801D9A58** (unmoved) | ~0x801D9A28 |
| image | 1710564 | **1710564** (unmoved) | 1710568 |
| headroom | 1205672 | **1205672** (unmoved) | ~1205720 |

`.text`'s terms are +0x244 object text, +0x000 COMDAT, +0x000 `.rodata`, +0x000..0x015 strings, −0x048 (three
retired bodies), −0x0D8 (three retired name slots, `align4(len + 1)` = 0x20 + 0x5C + 0x5C), +0x030 (two
created bodies), +0x048 (two created name slots) — sum **+0x19C..+0x1B1**, and the measured end 0x80184B00 is
the band's low end exactly.

**The placed terms were absorbed by two alignments, and the map prints both.** The first is the section's own
trailing one:

```
 .data  ...  the object's 4 bytes at 0x801A0648
                0x00000000801a1840                . = ALIGN (0x4)
```

The object's 4 bytes push the last `.data` input's end from 0x801A1838 to 0x801A183C, and the `ALIGN(8)` that
closes `.data` sends **both** to 0x801A1840 — so the section's size, every row below it, the image and the
whole `__DATA` segment are unmoved by a placed term of +4.

The second is a 64-byte alignment the map names outright:

```
 .bss           0x00000000801d7f80     0x10  iokit_Kernel_IOInterruptAccounting.o
 *fill*         0x00000000801d7f90     0x30
 .bss           0x00000000801d7fc0   0x1a84  xnu_arm_entry_realstubs.o
```

The object's 0x10 lands on a 64-byte-aligned slot, the next input needs 64-byte alignment, and a **0x30 pad
appears exactly where the retired 0x40 stand-in slot used to be** — the retirement and the pad cancel to the
byte. So the `.bss` row's `−0x30` placed term came out as zero, and the row's band was honest and unnecessary.

Two absorption mechanisms in one step, with opposite signs, on the two sections this walk has been least able
to predict — and one of them is printed in the map as `*fill*`, which is the first time that word has been the
*answer* rather than the residual.

`realstubs.o` is exact on all three sections, as the closed forms from 352 now allow: `.text` **0x45D8** =
745 × 0x18, `.rodata.str1.4` **0x4440** = 0x44D0 − 0xD8 + 0x48, `.bss` **0x1A84** = 0x1AC4 − 0x40.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000005b   xnu_entry_kv_in_dram=0x0000007f   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x80161c20          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller=0x8011b26c   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=devsw_init
```

**`devsw_init` at `0x8011b26c`, byte for byte where 352 stopped** — the walk's position is unchanged, which is
what "the step's object retires the *previous* stop" predicts and what makes this run a measurement rather than
a repeat: the three name sets moved exactly as the tool said, nothing on the path did, and `abort_entries=0`
says the path itself is exactly the path 352 walked.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301622 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## Frontier: 354 — `bsd/kern/bsd_stubs.c`, the definer of `devsw_init`

`bsd_kern_bsd_stubs.o` is where this step's object should have been: **2 resolved / 10 added** — `current_proc`
and `devsw_init` out (both already stubs in the image); **five functions and five storage stand-ins in**
(`chrtoblk_set`, `enodev`, `enodev_strat`, `msleep`, `seltrue`; `bdevsw`, `cdevsw`, `cdevsw_flags`, `nblkdev`,
`nchrdev`) — for 848 → **856** undefined, **745 → 748 function, 103 → 108 storage**. It brings `.text` **0x980**
of 30 definitions, `.rodata.str1.1` 0xBF, `.data` 0x58, `.bss` 0x20 and 24 bytes of `__DATA, __data` — the
last of which `entry.ld` places *inside* `.data`, so it is +0x18 there and not a section of its own.

`devsw_init`'s body, read from the object as 348's rule requires, is **0x4C bytes** and calls `lck_grp_alloc_init`
and `lck_mtx_init` — real, and already on this walk's measured path, since `IORegistryEntry::initialize` calls
the same pair. `current_proc` (0x94) is retired but is not on the path.

**Predicted stop: `interruptAccountingInit` at `StartIOKit+0x104`, key `0x8011b2a4`** — the stop 353 named, and
this time the object is the one that lets the walk reach it. It would be the first stop of the walk inside a
function it has *already entered once*.
