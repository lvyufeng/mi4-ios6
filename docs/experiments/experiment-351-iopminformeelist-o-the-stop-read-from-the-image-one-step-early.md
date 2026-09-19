# Experiment 351 — `IOPMinformeeList.cpp`: the stop read from the image one step early, and `.bss` absorbed

**Step:** link one object, `iokit/Kernel/IOPMinformeeList.cpp` (`iokit_Kernel_IOPMinformeeList.o`, 6092 bytes) —
the only object in the pool defining 350's stop `_ZN16IOPMinformeeList22getSharedRecursiveLockEv`. Nothing else
changes.

**Prediction:** *1 resolved / 1 added — 857 → **857** undefined, 747 → **747** function, 110 → **110** storage;
`.text` a band at +0x490..+0x4A1 with every derived row unmoved;* and **the stop at
`_ZN16IOKitDiagnostics11diagnosticsEv` on `iokit_post_constructor_init+0x74`, key `0x8011b160`.**

**Result:** all three counts exact, the stop **name and key exactly as predicted**, and the prediction had been
read *out of the image* a step early — the method 349's correction bought. Every derived row is exact except
`.bss`, which is the fill again, in the direction that made two more rows exact than the banded prediction
claimed.

## The object

| `iokit_Kernel_IOPMinformeeList.o` | |
|---|---|
| `.text` | **1036** (0x40C), 30 definitions |
| `.group` / COMDAT | 0xC / **4** |
| `.bss` | **28** (0x1C) |
| `.rodata` | **132** (0x84) |
| `.rodata.str1.1` | **17** (0x11) |
| `.init_array` | 4 |
| definitions / references | **30 / 34**, of which 33 already satisfied |

**1 resolved / 1 added** — `_ZN16IOPMinformeeList22getSharedRecursiveLockEv` out,
`_ZN12IOPMinformee10withObjectEP9IOService` in — for **857 → 857**, one of each, net zero: the counts do not
move at all, which makes this the first step of the walk whose arithmetic is an identity.

## The stop: predicted from the image one step early

```
stub_hit=_ZN16IOKitDiagnostics11diagnosticsEv
xnu_entry_stub_caller_v=0x8011b160  (also _a and _e, all three agreeing)
caller-4 = 0x8011b15c: bl 8015fd38 <_ZN16IOKitDiagnostics11diagnosticsEv>
abort_entries=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x8011b160` → **`iokit_post_constructor_init+0x74`**, and the `bl` is at
`+0x70`.

**This is the first stop of the walk predicted by reading *the image* past the callee the step retires**, which
is what 349's correction is for. The constructor's list of calls was walked in the 350 image, the call the step
retires was skipped, `OSString::withCString` at `+0x34` was read as real (0x74 bytes at 0x80122430), and the
first 0x18-byte body after it — `+0x70` — was named as the stop **before the build**. It landed there.

And the complementary reading — the callee's **body**, from its object — said the same thing from the other
side: `getSharedRecursiveLock` is 0x24 bytes, `push {r4, lr}`, `ldr r0, [sharedListLock]`, `cmp r0, #0`,
`popne {r4, pc}` on the warm path, `bl IORecursiveLockAlloc` (real since 328), `str r0, [r4]`, `pop` on the cold
one: **no stub on either path**. `abort_entries=0` is the hardware agreeing with both readings.

**The object's call graph faces nothing stubbed at all.** The `bl`-and-relocation sweep over its 30 definitions
and 34 references returns **zero** stub-facing sites apart from the name it retires — the strongest form this
check has taken in the walk, and the reason there was nothing for the run to surprise the prediction with.

## The layout: the band's top, and a fill that subtracts

| | 350 | 351 measured | 351 predicted |
|---|---|---|---|
| `.text` | 0x801836E0 (0x1836E0) | **0x80183B80** (0x183B80) | 0x80183B80..0x80183BA0 ✓ (the low end) |
| `.data` | 0x80184000 (0x197E0) | **0x80184000** (0x197E0, unmoved) | **0x80184000** ✓ |
| `.sysctl_set` | 0x8019D7E0 (0x138) | **0x8019D7E0** (0x138, unmoved) | **0x8019D7E0** ✓ |
| `.init_array` | 0x8019D918 (0x5C, 23) | **0x8019D918** (**0x60**, twenty-four) | 0x8019D918, 0x60 ✓ |
| `.bss` | 0x8019D980 (0x38198) | **0x8019D980** (size **0x38198**, unmoved) | 0x8019D980, 0x381B4 (0x1C low) |
| `__bss_end` | 0x801D5B18 | **0x801D5B18** (unmoved) | ~0x801D5B34 |
| image | 1694068 | **1694072** | **1694072** ✓ |
| headroom | 1221864 | **1221864** (unmoved) | ~1221836 |

`.text` closed **+0x4A0** (0x1836E0 → 0x183B80) with a fill delta of **−0x1** (68 rows summing 0xD43 against
350's 67 rows summing 0xD44, same convention), so the placed term is **+0x4A1** — the *top* of the predicted
band 0x490..0x4A1, the arithmetic in which **all 0x11 string bytes were placed** and the body terms cancelled
exactly (one retired, one created). The band was written wide precisely because the string row could be anything
in 0..0x11; the outcome was its top end, and the fill's −1 then pulled the end address back one 32-byte quantum
to 0x80183B80 — **the quantiser again, in the direction that made the band's low end the answer.**

**`.bss`'s size is unmoved for the second step running**: the object's 0x1C bytes were placed, the section did
not grow, so a fill of −0x1C absorbed them — 343's rule on `.bss` for the second time in three steps, and with
the opposite sign from 350's +0x30. That is why `__bss_end` and the headroom are unmoved **and exact**, which
the banded prediction could not claim: the band was honest, and the fill's absorption made it unnecessary.

`.init_array` grew by 4 **at the same start** (0x8019D918) — the shape a new object's `.init_array` input has,
since the start is fixed by everything linked before it and only the size grows. The row was drafted the other
way round (start +4, size 0x60) and corrected before the build; the end address and the image size come out the
same either way, but the shape is what a later step's row depends on. It is the same class as 350's base slip:
a row that is a *shape* rather than a value, written from the wrong end.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000075   xnu_entry_kv_in_dram=0x00000099   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x801611a0          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8011b160   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN16IOKitDiagnostics11diagnosticsEv
```

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301648 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## Frontier: 352 — `IOKitDebug.cpp`, and the step that ends the constructor

`iokit_Kernel_IOKitDebug.o` is the only pool definer of this step's stop: **7 resolved / 0 added** — the function
plus **six storage stand-ins** (`gIOKitDebug`, `gIOKitTrace`, `sysctl__debug_iokit`, `debug_iomalloc_size`,
`debug_iomallocpageable_size`, `debug_container_malloc_size`) — for 857 → **850** undefined, **747 → 746
function, 110 → 104 storage** (746 + 104 = 850). Six retired storage stand-ins is the largest storage
retirement of the walk, worth −0x180 of `.bss` before the object's own 0x50 is added. Its sections: `.text`
0x864, `.group` 0x18 / COMDAT 0x8, `.bss` 0x50, `.rodata` 0x45C, `.rodata.str1.1` 0x17A, `.data` 0x60,
`__DATA,__sysctl_set` 8, `.init_array` 4.

**And this is the step that ends `iokit_post_constructor_init`.** The constructor's list of calls was walked to
its end in the 351 image: after the diagnostics call at `+0x70` there is **no further call in the function** —
it is 0xB4 bytes and its last `bl` is that one — so the constructor *returns*, and for the first time in this
walk's history a step's stop is also the last call of the function that contains the walk. What follows is an
unwind, and reading it *is* this step's prediction:

```
iokit_post_constructor_init  pop {r4, r5, fp, pc}
  -> OSRuntimeInitializeCPP+0x184   the `blx r0` at +0x180 called `last_kernel_constructor`, whose four-byte
                                    `b` tail-branched here; `subs r9, r9, #1` now reads zero, because that
                                    was the table's last entry
  -> its own tail                   checkModLoad (0x30), nextsegfromheader (returns 0), postModLoad (0x474),
                                    OSRuntimeFinalizeCPP (0x180) - every direct call real
  -> OSlibkernInit+0x1C             `cmp r0, #0; beq`; nonzero would `bl panic` with
                                    "OSRuntime: C++ runtime failed to..." at 0x8017849f
  -> StartIOKit+0xC4's return       `bl OSlibkernInit` at 0x8011b264 returns to 0x8011b268
8011b268: bl devsw_init            STUB  <- 352's stop, key 0x8011b26c
```

**Predicted stop: `devsw_init` at `StartIOKit+0xCC`, key `0x8011b26c`** (`caller-4` = `0x8011b268`,
`StartIOKit+0xC8`). `StartIOKit` is `iokit/Kernel/IOStartIOKit.cpp`'s entry point, 0x184 bytes at 0x8011b1a0,
and its list of calls reads the same way: `PE_parse_boot_argn` three times (real), `IOLibInit` (real),
`OSlibkernInit` (real), **`devsw_init` 0x18 STUB**, `OSSymbol::withCStringNoCopy` (real),
`OSSet::withObjects` (real), **`interruptAccountingInit` 0x18 STUB**, `OSObject::operator new` (real),
**`IOPlatformExpertDevice::IOPlatformExpertDevice()` 0x18 STUB**, then four `blx` dispatches. `devsw_init` is
defined by `bsd_kern_bsd_stubs.o` in the pool.

**[CORRECTED BY 352, BEFORE 352'S BUILD.]** This paragraph first went on to say that
`IOPlatformExpertDevice`'s constructor "is defined **nowhere in the 695-object pool**". It is defined there:
`iokit_Kernel_IOPlatformExpert.o`, `T 0x38` at 0x2764, and the 352 image still carries the symbol as a stub
(`T 0x18` at 0x80160b34) only because that object is not linked. What the sweep actually looked for was the
**C name** — `nm --defined-only *.o | grep -w IOPlatformExpertDevice` matches nothing, because the pool holds
`_ZN22IOPlatformExpertDeviceC1Ev`. So this is 348's rule one turn further out: a body is an object fact, a
stub is an image fact, and **a definition is a mangled-name fact**. The frontier tool itself never had the
bug — it compares the image's undefined list against the pool's defined list, both mangled — the ad-hoc sweep
around it did, which is where 352 found it and why it is written down rather than deleted.

**The falsifiers are the interesting part**: (a) a stub reached through one of the `blx` dispatches inside
`postModLoad`'s metaclass walk — the one class of call this walk's tooling has never been able to see, and
`postModLoad` is 0x474 bytes of it; (b) **`panic`** — if `OSRuntimeInitializeCPP` propagates a nonzero
`postModLoad` return, `OSlibkernInit` calls the real 0x48-byte `panic` and the run ends in the walk's *kind 6*
rather than on a stub; (c) a stop inside `OSRuntimeFinalizeCPP`. The first is the one to expect if `devsw_init`
is not reached, and all three leave `abort_entries=0`.
