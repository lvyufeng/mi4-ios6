# Experiment 354 — `bsd_stubs.c`: the stand-in model, and the stop four calls on

**Step:** link one object, `bsd/kern/bsd_stubs.c` (`bsd_kern_bsd_stubs.o`) — the pool definer of 353's stop
`devsw_init`, and the object 353's block said belonged at 353. Nothing else changes.

**Prediction:** *2 resolved / 10 added — 848 → **856** undefined, 745 → **748** function, 103 → **108**
storage;* `.text` a band at +0x9E4..+0xAA3, every `__DATA` row unmoved; and **the stop at
`interruptAccountingInit` on `StartIOKit+0x104`, key `0x8011b2a4`.**

**Result:** all three counts exact, `.text` inside its band, every derived row exact, the whole `__DATA`
segment and the `.bss` start where 353 left them — and **the stop four calls past the prediction**, on a name
this step retires: `_ZN22IOPlatformExpertDeviceC1Ev` at key `0x8011b2b4` = `StartIOKit+0x114`. The prediction
was inherited from 353's block instead of read from the image 353 built, and one `nm` would have shown it:
`_Z23interruptAccountingInitv` is `T 0x60` in that image, not the `0x18` stub the name was when 353's block
was written. The one row that came out wrong in the layout is the `.bss` row, and it is wrong by **0xCE0** —
not a fill, but a model: a storage stand-in costs `align64(its own size)`, not the 0x40-byte slot this walk has
assumed for hundreds of steps, and `cdevsw`'s stand-in is 0x968 bytes.

## The object

| `bsd_kern_bsd_stubs.o` | |
|---|---|
| `.text` | **2432** (0x980), 30 definitions |
| `.rodata.str1.1` | **191** (0xBF) |
| `.data` | **88** (0x58) |
| `.bss` | **32** (0x20) |
| `__DATA, __data` | **24** (0x18) — placed *inside* `.data` by `entry.ld`, not a section of its own |
| `.rodata`, COMDAT, `.init_array`, `__sysctl_set` | **none of them** |
| definitions / references | 30 / 56, of which 44 already satisfied |

**2 resolved / 10 added** — `current_proc` and `devsw_init` out (both `T`, both already stubs in the image);
**five functions and five storage stand-ins in** (`chrtoblk_set`, `enodev`, `enodev_strat`, `msleep`,
`seltrue`; `bdevsw`, `cdevsw`, `cdevsw_flags`, `nblkdev`, `nchrdev`) — for 848 + 10 − 2 = **856**,
**745 → 748 function, 103 → 108 storage**, 748 + 108 = 856.

`devsw_init`'s body, read from the object as 348's rule requires, is **0x4C bytes** and calls
`lck_grp_alloc_init` and `lck_mtx_init`, both real and both already on this walk's measured path
(`IORegistryEntry::initialize` calls the same pair). `current_proc` (0x94, `current_thread` /
`get_bsdthread_info` / `panic`) is retired but is not on the path. Neither matters to the run: the step's
object retires the *previous* stop and the walk never reaches the rest of it.

## The stop: four calls, only the first of them this step's

```
stub_hit=_ZN22IOPlatformExpertDeviceC1Ev
xnu_entry_stub_caller_v=0x8011b2b4   (also _a and _e, all three agreeing)
caller-4 = 0x8011b2b0: bl 80161728 <_ZN22IOPlatformExpertDeviceC1Ev>   0x18 STUB
abort_entries=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x8011b2b4` → **`StartIOKit+0x114`** (0x8011b1a0 + 0x114).
`digits=0x43` with `w0=0x31313038` (`"8011"`) and `w1=0x34623262` (`"b2b4"`).

The block predicted `interruptAccountingInit` at `StartIOKit+0x104`. That name is **`T 0x60` at 0x8015cbc4
in the image this run used** — real since 353's link, because 353's object was
`iokit_Kernel_IOInterruptAccounting.o`, the object that defines it. So the step advanced four calls:

```
8011b268: bl devsw_init                              0x4C real  <- the stub at 352 and 353, retired here
8011b274: bl _ZN8OSSymbol17withCStringNoCopyEPKc     0xC8 real
8011b290: bl _ZN5OSSet11withObjectsEPPK8OSObjectjj   0x84 real
8011b2a0: bl _Z23interruptAccountingInitv            0x60 real  <- real since 353, and the predicted stop
8011b2a8: bl _ZN8OSObjectnwEm                        0x4C real
8011b2b0: bl _ZN22IOPlatformExpertDeviceC1Ev         0x18 STUB  <- the stop
```

`abort_entries=0` is what makes the four calls a measurement: the run entered four real functions it had never
entered, returned from each, and landed on the first stub past them.

**A frontier's predicted stop has to be read from the image the step will start from.** 353's block named this
stop while `interruptAccountingInit` was still a stub in the 352 image; 353 then retired it, and the 354
frontier repeated the name without re-reading. This is 353's rule for the other column: 353 said the step's
object is the definer of the *previous* stop; this says the step's stop is *the next stub the previous image
shows*, and neither column can be inherited from prose. The tooling already had the answer — `entry_frontier.py`
and one `nm -S` on `out/stage90/xnu_arm_entry.elf` both list every stub with its size — and the prediction was
written from a paragraph instead.

## The layout: every row exact but one, and that one is a model

| | 353 | 354 measured | 354 predicted |
|---|---|---|---|
| `.text` | 0x80184B00 (0x184B00) | **0x801855A0** (0x1855A0) | 0x80185500..0x801855C0 ✓ |
| `.data` | 0x80188000 (0x19840) | **0x80188000** (**0x198B8**) | 0x80188000, 0x198B0 (8 low) |
| `.sysctl_set` | 0x801A1840 (0x140) | **0x801A18B8** (0x140, follows `.data`) | 0x801A18B0 |
| `.init_array` | 0x801A1980 (0x64, 25) | **0x801A19F8** (0x64, twenty-five), end **0x801A1A5C** | 0x801A19F0 |
| `.bss` | 0x801A1A00 (0x38058) | **0x801A1A80** (**exact**), size **0x38E98** | 0x801A1A80 ✓, 0x381B8 (+0xCE0) |
| `__bss_end` | 0x801D9A58 | **0x801DA918** | ~0x801D9C38 (+0xCE0) |
| image | 1710564 | **1710684** | 1710676 (8 low) |
| headroom | 1205672 | **1201896** | ~1205192 (−0xCE0) |

**`.text` closed `+0xAA0` against a placed sum of `+0xAA3`, so the fill is −3** — and three of the six terms
are pinned by `realstubs.o`'s own sizes rather than assumed:

| term | bytes |
|---|---|
| this object's `.text` (0x980 at 0x8015CE08, ending 0x8015D788) | **+0x980** |
| its strings, placed **whole** (0xBF at 0x80180585; the next input is at 0x80180644 exactly) | **+0xBF** |
| the two retired stub bodies (`current_proc`, `devsw_init`) | **−0x30** |
| the two retired name slots (`align4(len + 1)`: 0x10 + 0xC) | **−0x1C** |
| the five created stub bodies | **+0x78** |
| the five created name slots (0x10 + 0x8 + 0x10 + 0x8 + 0x8) | **+0x38** |

`realstubs.o`'s `.text` is `748 × 0x18` and its `.rodata.str1.4` is the same `align4(len + 1)` sum, so the two
name-slot terms are the one pair of this row that is measured twice. The residual −3 is the fill, and it is the
same row that has been the ledger's weakest since 346.

**`.data` came in at `0x198B8` = 0x19840 + 0x58 + 0x18 + 0x8.** Both placements are read from this step's map:
the object's `.data` at **0x801A0650** (`nobdev` there, `nocdev` at 0x801A0670, the input ending at
0x801A06A8) and its `__DATA,__data` at **0x801A18A0** (0x18, the last `.data` input, the section ending at
0x801A18B8 with its closing `ALIGN(8)` neutral). The residual **+0x8 is a pad** — the first `.data` fill in
several steps — and this section is where pads are least predictable: the same map prints `*fill*` rows of
**0x1**, **0x2**, **0x4**, **0x50** and **0x3A28** inside it, so its mergeable runs are not all 4- or
8-aligned and a 0x58-byte insertion changes the pads behind it by a few bytes. Everything below follows:
`.sysctl_set` 0x140 unchanged in size, `.init_array` 0x64 with its twenty-five entries, its end 0x801A1A5C,
and `align64(0x801A1A5C)` = **0x801A1A80** as the `.bss` start, exact.

## The `.bss` row: the stand-in model, and the generator has said so all along

`.bss` is the step's one real miss and it is **not a fill**: `0x38058 + 0xE40 = 0x38E98` to the byte, where the
0xE40 is exactly the five created storage stand-ins at `align64(size)` each.

| stand-in | symbol size (`nm -S` in the pool) | slot | in the image |
|---|---|---|---|
| `nchrdev` | 4 | 0x40 | 0x801D9100 (4) |
| `nblkdev` | 4 | 0x40 | 0x801D9180 (4) |
| `cdevsw_flags` | 0x158 | 0x180 | 0x801D9A00 (0x158) |
| `cdevsw` | **0x968** | **0x980** | 0x801D9B80 (0x968) |
| `bdevsw` | 0x2A0 | 0x2C0 | 0x801DA580 (0x2A0) |

0x40 + 0x40 + 0x180 + 0x980 + 0x2C0 = **0xE40**, and the object's own 0x20 of `.bss` is placed at
0x801D8010 (`devsw_lock_list_mtx` 0x801D8010, `devsw_locks` 0x801D8018, `devsw_lock_grp` 0x801D8020, then
`dmmin`/`dmmax`/`dmtext` at 0x24/0x28/0x2C) and then **absorbed**: the `*fill*` between it and `realstubs.o`'s
`.bss` is **0x10** where 353's was 0x30, so the 0x20 cancels against it and the section's delta is the stand-in
growth alone — the third step in four where a fill was the whole of a `.bss` row's answer.

The prediction used `5 × 0x40 = 0x140` and was 0xCE0 low. The arithmetic behind it is a model this walk has
carried for hundreds of steps — *a storage stand-in costs a 0x40-byte 64-aligned slot* — and the generator has
never done that:

```sh
echo "uint8_t $sym[0x$sz] __attribute__((aligned(64)));"
```

sized from `nm -S` over the pool and placed on a 64-byte boundary, so the cost is `align64(size)` and the two
agree only while every stand-in is 0x40 or smaller. `cdevsw` is the first storage stand-in of the walk bigger
than a slot, and its 0x968 bytes make the difference visible in one row. The stale model is printed by
`tools/entry_object_effect.py` as a caveat on its own output — *"(the N created storage stand-in(s) cost
N x 0x40 of `.bss`, not their own sizes)"* — so the number came off a tool's sentence rather than off the
generator, which is the same failure as 352's pool sweep with a different source of prose.

`realstubs.o` is exact on all three sections, and 352's closed form gets a second independent check:

| | 354 measured | model |
|---|---|---|
| `.text` | **0x4620** | 748 × 0x18 = 0x45D8 − 0x30 + 0x78 ✓ |
| `.rodata.str1.4` | **0x445C** | 0x4440 − 0x1C + 0x38 ✓ |
| `.bss` | **0x28C4** | 0x1A84 + 0xE40 ✓ |

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000070   xnu_entry_kv_in_dram=0x00000094   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x801625e8          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8011b2b4   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN22IOPlatformExpertDeviceC1Ev
```

`xnu_entry_why=0x801625e8` is the message pool a stub stop always points at, and the run's own build line
agrees with the table in both directions: `__bss_start 0x801a1a80 ... (233112 bytes to 0x801da918)`,
`the copied image ends at 0x801a1a5c, 36 bytes below __bss_start`, `image bytes 1710684`,
`headroom 1201896 bytes below topOfKernelData`.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of
either, `xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301643 bytes, and the device back on Android on its
own (`MI 4LTE`, release 10).

## Frontier: 355 — `iokit/Kernel/IOPlatformExpert.cpp`, the definer of the constructor

`iokit_Kernel_IOPlatformExpert.o` is the only pool definer of this step's stop `_ZN22IOPlatformExpertDeviceC1Ev`
(under its mangled name, per 352's correction): **8 resolved / 16 added** — `PEGetUTCTimeOfDay`, `PEHaltRestart`,
`PEReadNVRAMProperty`, `PERemoveNVRAMProperty`, `PESetUTCTimeOfDay` and the constructor out (functions),
`_ZN16IOPlatformDevice9metaClassE` and `gPlatformInterruptControllerName` out (storage, `R 4` and `B 4`);
**eleven functions in** (`SHA1Init`, `SHA1Update`, `SHA1Final`, `_Z17IODeviceTreeAllocPv`,
`_Z18IODTCompareNubNamePK15IORegistryEntryP8OSStringPS3_`,
`_Z21IODTResolveAddressingP15IORegistryEntryPKcP14IODeviceMemory`,
`_Z23IODTFindMatchingEntriesP15IORegistryEntrymPKc`, `_ZN10IOWorkLoop8workLoopEv`,
`_ZN16IORangeAllocator9withRangeEmmmm`, `_ZN8IOMapper17setMapperRequiredEb`, `_ZN9IODTNVRAMC1Ev`) and **five
storage stand-ins** (`_ZN16IORangeAllocator9metaClassE`, `_ZN21IOInterruptController9metaClassE`,
`_ZN9IODTNVRAM9metaClassE`, `gIODTCompatibleKey`, `gIODTModelKey`) — for 856 → **864** undefined,
**748 → 753 function, 108 → 111 storage**. All five stand-ins are 4 bytes, so 355 is a step where the 0x40
model and `align64(size)` agree and this step's defect is not retested by it.

The object is the largest of the IOKit steps: `.text` **0x2E74** (11892) with five `.group` sections and five
4-byte `.text.*` COMDAT pieces (COMDAT 0x28), `.bss` 0x94, `.rodata` **0x13A4**, `.rodata.str1.1` **0x313**,
`.rodata.cst16` 0x10, `.init_array` 4.

**The path is read from the image and the object together.** `StartIOKit+0x114` is `ldr r0, [r0]` — the vptr
the constructor just stored — and then `mov r1, r7; mov r2, r9; mov r3, r8; str r6, [sp]; ldr r5, [r0, #0x340];
blx r5`, a call to **`_ZN22IOPlatformExpertDevice12initWithArgsEPvS0_S0_S0_`**: `_ZTV22IOPlatformExpertDevice`
sits at `.rodata` + 0x86C with size 0x34C, the relocation at `.rodata` + 0xBB4 is that symbol, and
0x86C + 8 + 0x340 = 0xBB4 exactly. The four arguments are `StartIOKit`'s own, and
`pexpert/arm/pe_init.c:279` names them:

```c
StartIOKit(PE_state.deviceTreeHead, PE_state.bootArgs, (void *) 0, (void *) 0);
```

`initWithArgs`'s body, read from the object as 348's rule requires, is **0x84 bytes**, and its first call is
`_Z17IODeviceTreeAllocPv` — **a name this step creates** — taken when p1 is non-zero; the p1 == 0 branch calls
`_ZN9IOService4initEP12OSDictionary` (real, `T 0x6C` at 0x8012AC5C) and both branches converge on
`_ZN10IOWorkLoop8workLoopEv` at the object's +0x68, **also created by this step**.

**Predicted stop: `_Z17IODeviceTreeAllocPv` at `IOPlatformExpertDevice::initWithArgs+0x18`** (the `bl` at
object offset +0x14), with a named falsifier: if `PE_state.deviceTreeHead` is zero the run takes the other
branch and stops on `_ZN10IOWorkLoop8workLoopEv` at the same depth. Both are kind 1 and both are names this
step creates. The key is the first of the walk's predicted stops that does not exist in the previous image:
`initWithArgs` is inside the object being linked, so its address comes from this block's layout arithmetic and
cannot be read off the 354 map the way `StartIOKit+0x114` could.
