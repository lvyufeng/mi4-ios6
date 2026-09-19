# Experiment 348 — `IOMemoryDescriptor.cpp`: the stop a step creates and then reaches, and a body read from the wrong image

**Step:** link one object, `iokit/Kernel/IOMemoryDescriptor.cpp` (`iokit_Kernel_IOMemoryDescriptor.o`, 59796
bytes) — the only object defining 347's stop. Nothing else changes.

**Prediction:** *8 resolved / 17 added — 837 → **846** undefined, 736 → **743** function, 101 → **103** storage;
`.text` a band, `.data` stepping 0x8000 to 0x80174000;* and **the stop at
`_ZN16IOPMinformeeList22getSharedRecursiveLockEv` on `iokit_post_constructor_init+0x28`, key `0x8011b118`.**

**Result:** all three counts exact, every derived address exact, `realstubs.o` exact on all three of its
sections again — and **the stop was not the predicted one**. It landed on `IOGetLastPageNumber`, a name *this
step created*, reached from *inside* the object this step linked. The prediction's own falsifier fired, and the
reason is a defect worth more than the row it moved: **the body was read out of the image, where the symbol was
still a stub.**

## The object

| `iokit_Kernel_IOMemoryDescriptor.o` | |
|---|---|
| `.text` | **32364** (0x7E6C), 198 definitions |
| `.group` / COMDAT | 0x24 / **0x0C** |
| `.bss` | **88** (0x58) |
| `.rodata` | **728** (0x2D8) |
| `.rodata.str1.1` | **788** (0x314) |
| `.data` | **8** (`_ZL18gIOMDPreparationID`) — the first step of the walk whose object brings `.data` |
| `.init_array` | 4 (`_GLOBAL__sub_I_IOMemoryDescriptor.cpp`) |
| definitions / references | **198 / 134**, of which 117 already satisfied |

**8 resolved / 17 added** — seven functions (`IOMemoryDescriptor::initialize`, `withAddressRange`,
`getPhysicalAddress`, `createMappingInTask`, `IOMemoryMap::userClientUnmap`, `device_close`,
`device_data_action`) plus the `R 0x4` stand-in `_ZN11IOMemoryMap9metaClassE` out; fourteen functions and three
storage stand-ins in — for 837 + 17 − 8 = **846**, **736 → 743 function, 101 → 103 storage**, 743 + 103 = 846.

**Three of the created names are defined nowhere in the 695-object pool** — `upl_get_internal_vectorupl`,
`upl_get_internal_pagelist_offset`, `upl_get_internal_vectorupl_pagelist` — and the tool prints that in its own
words (`nowhere in the pool`) rather than as a pool size. That is 329's `__cxa_atexit` and 330's `__dso_handle`
shape: a name no link can resolve *from the pool*, whose body the generator supplies, so its behaviour if
reached is a **zero** rather than an error. They are declared in `osfmk/mach/memory_object_types.h` and
referenced in `bsd/vfs/vfs_cluster.c` and `osfmk/vm/vm_pageout.c`.

## The stop: created by this step, reached inside this step

```
stub_hit=IOGetLastPageNumber
xnu_entry_stub_caller_v=0x8014c050 = _ZN18IOMemoryDescriptor10initializeEv+0x24
caller-4 = 0x8014c04c: bl 8014d884 <IOGetLastPageNumber>
abort_entries=0x00000000
```

`IOGetLastPageNumber` is in this step's **`added`** column — the link created it as a stub — and the call is
inside `_ZN18IOMemoryDescriptor10initializeEv`, i.e. inside the object the step just linked. That is 343's
shape (a stop inside the linked object) and 342's (on a name the link created) at once, and it is exactly the
falsifier the prediction named: *"a stop naming one of the fourteen created functions, which would mean an
`IOMemoryDescriptor` method was reached through an edge the `bl`-classification cannot see."*

**The classification did not fail. The body was read from the wrong image.** The prediction said
`_ZN18IOMemoryDescriptor10initializeEv` is "six instructions with zero direct calls", because that is what
`first_stub_call.py` reports for a symbol that is still a **stub** in the 347 image. A stub's body is the
reporting tail. The object says the function is **0x34 bytes** with two calls:

```
7098: ldr r0, [r4]          ; the .bss word at 0x801c38d8 — the descriptor's recursive lock
70ac: bne 70b8              ; skip the alloc if it is already there
70b0: bl IORecursiveLockAlloc
70b8: bl IOGetLastPageNumber      ; +0x20 in the object, +0x24 in the image: the stop
      str r0, [r1]          ; r1 = 0x801c38a4 = gIOLastPage
      pop {r4, pc}
```

and the image agrees instruction for instruction (0x8014c02c..0x8014c05c) — with the lock word at 0x801c38d8
holding zero, so `IORecursiveLockAlloc` **was** called, at 0x8014c044.

**The rule this buys:** to predict a stop inside a function a step is about to make real, read the function's
body from the **object** that defines it, never from the image where the symbol is still a stub.
`first_stub_call.py` answers a true question about the stub and the prediction read it as an answer about the
function. `nm -S` on the object is the cheap cross-check that would have caught it — it reports 0x34 for a
function the image said was 0x18 — and it is the check IOUserClient's 347 prediction happened to use (0x28
bytes) precisely because that function really is small.

**The consequences are small.** `IOGetLastPageNumber` is the *last* call in `initialize`, so once it is real
the function returns, `_ZN12IORootParent10initializeEv` (4 bytes, real from this link) returns, and the
constructor reaches the call it was already going to reach — one that 348 retired without ever calling, since
the stop came earlier. So the frontier is one call *below* where it was predicted, not somewhere new.

## The layout

| | 347 | 348 measured | 348 predicted |
|---|---|---|---|
| `.text` | 0x8016A820 (0x16A820) | **0x80172DE0** (0x172DE0) | 0x80172B00..0x80172E00 ✓ |
| `.data` | 0x8016C000 (0x19498) | **0x80174000** (**0x194A8**) | 0x80174000, 0x194A0 (8 low) |
| `.sysctl_set` | 0x80185498 (0x10C) | **0x8018D4A8** (0x10C) | 0x8018D4A0 (8 low) |
| `.init_array` | 0x801855A4 (0x54, 21) | **0x8018D5B4** (**0x58**, twenty-two entries) | same, 8 low |
| `.bss` | 0x80185600 | **0x8018D640** | **0x8018D640** ✓ |
| `.bss` fill / size | 0x124 / 0x37C98 | **0x10C** / **0x37D58** | placed 0x37CB0 modelled |
| `__bss_end` | 0x801BD298 | **0x801C5398** | ~0x801C5318 (0x80 low) |
| image | 1594872 | **1627660** | 1627140 (0x208 low) |
| headroom | 1322344 | **1289320** | ~1309888 |

`.text` closed **+0x85C0** (0x16A820 → 0x172DE0) with fill 0xD2E → **0xD2A** (−4), so the placed term is
**+0x85C4** — inside the band 0x82C4..0x85D8, implying 0x2D0 of the object's 0x314 string bytes were placed
(0x44 merged away, 346's third mechanism again).

**The `.data` row is the one to keep, and it is off by exactly 8.** This is the first step whose object brings
`.data`, and the prediction reasoned about it correctly and then left out the fill: the object's 8 bytes land
at 0x8018C2A8 in the realstubs `.data` region, where nothing follows them but the section's `ALIGN(0x8)` — which
the map prints as *already satisfied* — so **placed +8**, and then a **second** 8 bytes of fill appears behind
them, because an input further along needed 8-byte alignment. `size = placed + fill = 8 + 8 = 0x10`. That is
343's row in both directions at once: there placed +4 and fill −4 cancelled; here placed +8 and fill +8 add,
and the prediction used only the placed half. Every derived row below `.data` inherits the 8.

`.bss`'s start is `align64(0x8018D5B4 + 0x58)` = **0x8018D640**, unmoved relative to `.init_array` for the
ninth step running, and exact. Its placed term is where the model is weakest: the section's size is 0x37D58
with a fill of 0x10C, so the implied input sum is 0x37C4C against a modelled 0x37CB0 — and the `.bss` output
section contains a direct `. += 16` reserved-slot advance (`entry.ld:201`) plus its own `ALIGN`s, which the
placed-term model does not carry. That gap is worth one line and no more: the start was exact, and every row
that depends on the *fill* has been the wrong one for five steps running regardless.

`realstubs.o` was exact on all three sections for the second step running, and again with no tail artifact:
`.text` 0x4500 → **0x45A8** (**+0xA8** = 14 created − 7 retired bodies), `.rodata.str1.4` 0x4253 → **0x431F**
(**+0xCC** = 0x1D8 created slots − 0x10C retired), `.bss` 0x1A04 → **0x1A84** (**+0x80** = 3 × 0x40 created −
1 × 0x40 retired).

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301631 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## Frontier: 349 — `osfmk/device/iokit_rpc.c`

`osfmk_device_iokit_rpc.o` is the only object defining this step's stop, and it is the largest single-object
retirement of the walk: **15 resolved / 0 added** — `IOGetLastPageNumber`, `IODefaultCacheBits`,
`IOProtectCacheMode`, `IOMapPages`, `IOUnmapPages`, `iokit_notify`, `iokit_alloc_object_port`,
`iokit_destroy_object_port`, `iokit_lookup_connect_ref_current_task`, `iokit_make_send_right`,
`iokit_mod_send_right`, `iokit_release_port`, `iokit_release_port_send`, `iokit_retain_port`,
`iokit_switch_object_port` — for 846 → **831** undefined, **743 → 728 function, 103 → 103 storage**. Its
`.text` is 0x830, `.rodata` 0x18, `.rodata.str1.1` 0x14, `.bss` 4, and it creates **nothing**, so
`realstubs.o` should give back 15 bodies and 15 name slots and create none — the first step in a while where
its `.text` and `.rodata.str1.4` *shrink*.

`IOGetLastPageNumber`'s body must be read from the object this time, not the image, and that is the whole
lesson of 348 applied one step later. **Predicted stop: `_ZN16IOPMinformeeList22getSharedRecursiveLockEv` at
`iokit_post_constructor_init+0x28`, key `0x8011b118`** — the call 348 retired without reaching.
