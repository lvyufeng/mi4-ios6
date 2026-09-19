# Experiment 349 — `iokit_rpc.c`: fifteen names retired in one step, and a premise corrected before the build

**Step:** link one object, `osfmk/device/iokit_rpc.c` (`osfmk_device_iokit_rpc.o`, 6140 bytes) — the object
defining 348's stop `IOGetLastPageNumber`, and the largest single-object retirement of the walk so far.
Nothing else changes.

**Prediction, as corrected before the build:** *15 resolved / 0 added — 846 → **831** undefined, 743 → **728**
function, 103 → **103** storage; `.text` a band at +0x590..+0x5A4, every derived row unmoved;* and **the stop at
`_ZN12IORootParent10initializeEv` on `iokit_post_constructor_init+0x28`, key `0x8011b114`.**

**Result:** all three counts exact, every derived address exact, `realstubs.o` shrinking on two of its three
sections for the first time in the walk — and the stop landed **name and key exactly where the correction said
it would**, one call *earlier* than the prediction this step originally carried. The correction is the substance
of the experiment: the earlier prediction had rested on a sentence that confused *the pool defines this name*
with *this link makes it real*.

## The object

| `osfmk_device_iokit_rpc.o` | |
|---|---|
| `.text` | **2096** (0x830), 23 definitions |
| `.rodata` | **24** (0x18) |
| `.rodata.str1.1` | **20** (0x14) |
| `.bss` | **4** |
| `.data`, `.init_array`, COMDAT | **none of them** |
| definitions / references | 23 / 39, **all thirty-nine references already satisfied** |

**15 resolved / 0 added** — every one a function, and every one a name the image carries as a stub:
`IOGetLastPageNumber` (348's stop), `IODefaultCacheBits`, `IOProtectCacheMode`, `IOMapPages`, `IOUnmapPages`,
`iokit_notify`, `iokit_alloc_object_port`, `iokit_destroy_object_port`,
`iokit_lookup_connect_ref_current_task`, `iokit_make_send_right`, `iokit_mod_send_right`,
`iokit_release_port`, `iokit_release_port_send`, `iokit_retain_port`, `iokit_switch_object_port` — for
846 + 0 − 15 = **831**, **743 → 728 function, 103 → 103 storage**, 728 + 103 = 831.

**It creates nothing**, which makes it the first step in six whose retirement side is the whole of its
movement: **no created stub bodies, no created name slots, no added storage stand-ins.** The other eight of its
23 definitions (`iokit_lookup_object_port`, `iokit_lock_port`, `iokit_unlock_port`, `iokit_lookup_connect_port`,
`iokit_lookup_connect_ref`, `iokit_make_object_port`, `iokit_make_connect_port`, `IOGetTime`) are names the
image neither defines nor references — they simply become real with this link and appear in no count, because
the walk's arithmetic is over stub records and undefined references, not over definitions.

## The correction: "the object defines it" is not "this link defines it"

348's run stopped inside `_ZN18IOMemoryDescriptor10initializeEv`, at the call to `IOGetLastPageNumber`. Both
348's write-up and this step's first prediction then agreed that once `IOGetLastPageNumber` is real,
`initialize` returns, `_ZN12IORootParent10initializeEv` returns, and the constructor reaches the call at
`+0x28`. The middle clause was false, and the way it was false had not appeared in this walk before:

* `_ZN12IORootParent10initializeEv` is **4 bytes in the object that defines it** — `iokit_Kernel_IOPMrootDomain.o`,
  `T e3d0 4`, and the whole of it is `bx lr`. That sentence was the prediction's evidence.
* But `iokit_Kernel_IOPMrootDomain.o` is the **only** definer of that name in the pool, **and 348 does not link
  it**. So 348 could not have retired it, and did not: 348's resolved column is seven `IOMemoryDescriptor`
  methods and nothing else.
* `nm -S` on the 348 image reports the symbol as **`T 0x18` at 0x80150cbc** — the stub body — both before and
  after 348's link. The 349 image reports the same thing, and the run confirms it.

So the constructor advances **one** call, not two, and the name it lands on is still a stub. The rule, which is
348's rule read in the other direction: **read a body from the object that defines it, and read a stub's
*identity* from the image** — the pool tells you who *could* define a name, and only the image tells you whether
anything has.

The correction was made in 348's prediction block, 348's measured section and 349's own block **before this
step's build**, and the correction is recorded in place rather than deleted, as 345's withdrawn "constant" was.

## The stop: name and key as corrected

```
stub_hit=_ZN12IORootParent10initializeEv
xnu_entry_stub_caller_v=0x8011b114  (also _a and _e, all three agreeing)
caller-4 = 0x8011b110: bl 80151384 <_ZN12IORootParent10initializeEv>
abort_entries=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x8011b114` → **`iokit_post_constructor_init+0x28`**, and 0x80151384 is the
0x18 stub. The call one instruction further on — `0x8011b114: bl 8015142c
<_ZN16IOPMinformeeList22getSharedRecursiveLockEv>` — is the one the step's *original* prediction named, and it
is now 350's. `digits=0x43` with `w0=0x31313038` (`"8011"`) and `w1=0x34313162` (`"b114"`) renders the key as
its own ASCII digits again.

**`abort_entries=0` is what makes the step a measurement.** Between 348's stop and this one the run had to reach
`IOGetLastPageNumber`'s real body and return from it (the image now carries `T 0x8` at 0x8014d608 where the 348
image had `T 0x18` at 0x8014d884 — the entire effect of this step on the executed path), store that 0 into
`gIOLastPage`, return from `_ZN18IOMemoryDescriptor10initializeEv` in full, and let the constructor advance one
call. All three of the falsifiers the corrected prediction named — a stop inside `IOGetLastPageNumber`, a stop at
key 0x8011b118, a stop from one of the object's other stub-facing sites — did not fire.

`IOGetLastPageNumber`'s body was read **from the object** this time, which is 348's whole lesson applied one step
later, and it is why this step's prediction could be a point rather than a hope:

```
 7fc: mov r0, #0
 800: bx  lr
```

**Eight bytes, no calls** — so nothing else in the object's 0x830 bytes of text can be on the path, and the
`bl`-sweep of its remaining stub-facing sites (`iokit_lookup_connect_ref` → `ipc_object_translate`, `io_free`;
`iokit_make_send_right` → `ipc_object_copyout`) is off the executed path **by construction** rather than by
inspection of the run.

## The layout: every row exact, and the fill closes the identity for the first time in six steps

| | 348 | 349 measured | 349 predicted |
|---|---|---|---|
| `.text` | 0x80172DE0 (0x172DE0) | **0x80173380** (0x173380) | 0x80173380..0x801733A0 ✓ (the low end) |
| `.data` | 0x80174000 (**0x194A8**) | **0x80174000** (0x194A8, unmoved) | **0x80174000** ✓ |
| `.sysctl_set` | 0x8018D4A8 (0x10C) | **0x8018D4A8** (0x10C, unmoved) | **0x8018D4A8** ✓ |
| `.init_array` | 0x8018D5B4 (0x58, 22) | **0x8018D5B4** (0x58, twenty-two, unmoved) | **0x8018D5B4** ✓ |
| `.bss` | 0x8018D640 (size 0x37D58) | **0x8018D640** (size **0x37D58**, unmoved) | 0x8018D640, size as a band |
| `__bss_end` | 0x801C5398 | **0x801C5398** (unmoved) | ~0x801C539C (banded) |
| image | 1627660 | **1627660** (unmoved) | **1627660** ✓ |
| headroom | 1289320 | **1289320** (unmoved) | ~1289316 (banded) |

`.text` closed **+0x5A0** (0x172DE0 → **0x173380**), and every term of that closure is read from this map's own
lines rather than carried from the previous step: **+0x830** (the object's `.text` at 0x8014ce0c), **+0x18** (its
`.rodata`), **+0x14** (its `.rodata.str1.1`, placed whole at 0x8016e684), **−0x168** (the fifteen retired stub
bodies, `realstubs.o`'s `.text` 0x45A8 → **0x4440** at 0x8014d694) and **−0x150** (the fifteen retired name
slots, its `.rodata.str1.4` 0x431F → **0x41cf** at 0x8016e7e8). Placed term **+0x5A4**, so the fill delta is
**−0x4**.

**The fill row is worth one honest line.** The map prints 66 fill rows inside `.text` summing 0xD3A, 0x10 of
which is the trailing pad at 0x80173370..0x80173380; 348 recorded 0xD2A over 65 rows. 0xD3A against 0xD2A would
say +0x10, which contradicts a closure built from five map lines that are all visible — and the trailing pad is
*inside* `.text`, because it ends exactly at `.text`'s end. One of the two running totals was therefore summed
under a convention that was never restated, and the row this ledger carries is **the delta from the closure
(−0x4), not a difference of two totals**: a fill is read, and read in the frame it is used in (343's rule, from a
seventh direction — this time the frame is *which lines the sum was taken over*).

**And the −0x4 did not move the predicted address.** The placed term alone gives `align32(0x80173384)` =
0x801733A0, the top of the predicted band; the measured end is 0x80173380, one 32-byte quantum lower. That is
345's quantiser absorbing a fill error again, and the prediction survived only because it was written as a band
whose width came from the string term's three outcomes, not from the arithmetic.

**`.bss`'s size is unmoved at 0x37D58** with this object's 4 bytes placed at 0x801c38fc: placed +4, fill **−4**,
size unchanged. That is 343's `.data` row reproduced on `.bss` nine steps later, and it is why `__bss_end`, the
headroom and the image all three come out exact for the first time since 346 — not because the fill's value was
known, but because the fill *absorbed* the placed term. The band the prediction wrote for that row was honest
and unnecessary.

`realstubs.o` **shrank on two of its three sections for the first time in the walk**, exactly as predicted:
`.text` 0x45A8 → **0x4440** (−0x168 = fifteen bodies), `.rodata.str1.4` 0x431F → **0x41cf** (−0x150 = fifteen
slots), `.bss` **0x1A84** unchanged.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000070   xnu_entry_kv_in_dram=0x00000094   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x801522bc          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x8011b114   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=_ZN12IORootParent10initializeEv
```

`xnu_entry_why=0x801522bc` disassembles to `61 20 73 79` = `"a sy"`, the tail of the message pool, where a stub
stop's `why` always points.

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301643 bytes, and the device back on Android on its own
(`MI 4LTE`, release 10).

## Frontier: 350 — `iokit/Kernel/IOPMrootDomain.cpp`

The object that defines `_ZN12IORootParent10initializeEv`, and **nothing else in the pool defines it**, so it is
the only candidate for the next step: **4 resolved / 30 added** — that function,
`_ZN14IOPMrootDomain10tracePointEh`, `_ZN14IOPMrootDomain13startSpinDumpEj` and `hibernate_should_abort` out;
**twenty-three functions and seven storage stand-ins in** — for 831 + 30 − 4 = **857** undefined,
**728 → 747 function, 103 → 110 storage** (747 + 110 = 857).

It is the largest object this walk has yet considered: 144744 bytes, `.text` **0xE8E8** of 565 definitions,
`.bss` 0x250, `.rodata` 0xA7C, `.rodata.str1.1` 0xB5C, `.data` 0x338, `__DATA,__sysctl_set` 0x2C and an
`.init_array`. 0x173380 + 0xE8E8 plus its COMDAT, minus four bodies and four name slots, plus 0xA7C of `.rodata`
and up to 0xB5C of strings is well past the 0x80178000 boundary, so **`.data` should step to 0x80184000 at
least** — the first `__DATA` move since 347 — with `.sysctl_set` moving with it, since this object brings 0x2C of
`__DATA,__sysctl_set` of its own.

**Predicted stop: `_ZN16IOPMinformeeList22getSharedRecursiveLockEv` at `iokit_post_constructor_init+0x2C`, key
`0x8011b118`** — the call 349's first prediction named one step early, and the call 349's *run* stopped one
instruction short of. `_ZN12IORootParent10initializeEv` has been read from its object and is 4 bytes, `bx lr`,
no calls, so once it is real the constructor advances one call. The two objects are adjacent in the tree, which
makes 351 small: `iokit_Kernel_IOPMinformeeList.o` (6092 bytes) defines
`_ZN16IOPMinformeeList22getSharedRecursiveLockEv` (`T 0xf8 24`) with a 4-byte `.bss` for its `sharedListLock`.
