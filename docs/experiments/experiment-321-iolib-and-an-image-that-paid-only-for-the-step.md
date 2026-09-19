# Experiment 321 — `iokit/IOLib.cpp`, and an image that paid only for the step

**Step:** link **`iokit/Kernel/IOLib.cpp`** (`out/xnu_kernel_obj/iokit_Kernel_IOLib.o`, manifest:322) —
the object that defines `IOLibInit`, 320's stop, and IOKit's whole allocator and thread-wrapper layer.

**Prediction:** *counts **4 resolved / 7 added** (5 functions and 2 storage), so 832 → **835** undefined,
724 → **725** function stubs, 108 → **110** storage; `.text` **+0x1B8F plus a band**; the 16 KB boundary
crossed so `.data` steps +0x4000 and the image grows by ~0x5BD7; predicted stop **`IOLockAlloc` at
`IOLibInit+0x10C`**.*

**Result:** **the counts are exact in every column, the stop is `IOLockAlloc` at `IOLibInit+0x10C`, and
`.text` closes with no residual.** The prediction's `.text` missed by 0x10F — all three reasons measured,
one of them the ledger's own arithmetic. And the image grew by **0x4048** while `.text` grew 0x1A80: the
step's text cost the image nothing at all.

## The object, and a baseline that reproduces 320

`iokit/Kernel/IOLib.cpp` is the largest step in `.text` since 316's `kern_newsysctl.o` and the largest
closure since then.

| | size |
|---|---|
| `.text` | **6476** (0x194C), 38 functions |
| `.bss` | **384** (0x180), 14 symbols |
| `.rodata` | **56** (0x38), 11 read-only symbols |
| `.rodata.str1.1` | **299** (0x12B), ten strings |
| `__DATA, __data` | **72** (0x48) — three 24-byte `VM_ALLOC_SITE_STATIC` sites |
| references | **52** |

What it defines is IOKit's allocator and thread wrappers: `IOCreateThread`, `IOExitThread`, `IOFree`,
`IOFreeAligned`, `IOFreeContiguous`, `IOFreePageable`, `IOMalloc`, `IOMallocAligned`,
`IOMallocContiguous`, `IOMallocPageable`, `IOKernelAllocateWithPhysicalRestrict`,
`IOKernelFreePhysical`, `IOPageableMapForAddress`, `IOFlushProcessorCache`, `IOSetProcessorCacheMode`,
`IOSizeToAlignment`, `IOAlignmentToSize`, `IODelay`, `IOPause`, `IOSleep`, `IOSleepWithLeeway`, `IOLog`,
`IOLogv`, `IOPanic`, `OSKernelStackRemaining`, the `iopa_*` page allocator and the three
`IOFindNameForValue`/`IOFindValueForName`/`IOCopyLogNameForPID` helpers.

The baseline was built in this session with an **empty stand-in object** in this slot, and it reproduced
320's image to the byte (`.text` 0x13B960, `.data` 0x8013C000/0x191C8, `.sysctl_set` 0x801551C8/0x10C,
`.init_array` 0x801552D4/0x4, `.bss` 0x80155300/0x37658, `__bss_end` 0x8018C958, image 1397464, headroom
1521320).

|  | predicted | measured |
|---|---|---|
| undefined | 835 | **835** |
| function stubs | 725 | **725** |
| storage stubs | 110 | **110** |
| resolved / added | 4 / 7 | **4 / 7** |
| of the added: functions / storage | 5 / 2 | **5 / 2** |

Resolved are `IOLibInit`, `IOMalloc`, `IOFree`, `IOSleep`; the other 62 of this object's 66 definitions
resolve nothing yet, which by 250/270's rule is not a count change at all — the reading 311 and 312 cost
two steps to.

## `.text` closes with no residual

`.text` 0x13B960 → **0x13D3E0** is +0x1A80:

```
  this object's .text                                  +0x194C   (6476, exact)
  this object's .rodata.str1.1                         +0x06F   (111 placed; the object's own
                                                                section is 0x12B = 299, see below)
  this object's .rodata                                +0x038   (56, exact)
  the stub object's .text                              +0x018   (4 bodies retired, 5 created)
  the stub object's name strings                       +0x070   (0x3C87 -> 0x3CF7; see the correction at
                                                                the end of this document — the base was
                                                                first written as 0x3C77, 320's pool *span*)
  .text-region alignment fill                          +0x005   (0xD28 -> 0xD2D; 56 fills in both)
                                                      -------
                                                       +0x1A80   against a measured +0x1A80
```

### The prediction missed by 0x10F, and all three reasons are measurable

| | bytes |
|---|---|
| the string term was taken as the object's own section size (0x12B) instead of what the linker placed (0x6F) | **−0xBC** |
| the name term was estimated at 0xC8 against 0x70 placed | **−0x58** |
| fill (inside the quoted band, and the only one the prediction allowed for) | **+0x05** |
| | **−0x10F** |

The point estimate also carried an arithmetic slip of its own, and it is the same slip this project has
recorded twice before: the name term was **written as 0xC8 while its own parenthetical gave created 0xC8
and retired 0x28** — a net of 0xA0. The created-minus-retired subtraction was left out of the sum, so
0x28 of the 0x10F is the ledger's arithmetic and not the linker's.

## 188 of the 299 string bytes went away, and the four dropped strings can be pointed at

The object's own `.rodata.str1.1` holds ten strings; the linker placed six, at 0x80138B7B, ending exactly
where the next input starts:

| placed (111 bytes) | dropped (188 bytes) | already in the image at |
|---|---|---|
| `IOKit` | | |
| `"failed to allocate iokit pageable map\n"` | | |
| `"IOPageableMapForAddress: null"` | | |
| `"%s"` | | |
| `0x%x (UNDEFINED)` | | |
| `pid %d, ` | | |
| | `"overflow detected"` | **0x8012A592** |
| | `"Invalid queue element %p"` | **0x80129F8E** |
| | `"Invalid queue element pointers for %p: next %p prev %p"` | **0x80129FA9** |
| | `"Invalid queue element linkage for %p: next %p …"` | **0x80129FE2** |

Each survives **exactly once** in the image, and each address is *below* the object's placed range — so
the copies this object brought were dropped. The four are `queue.h`'s `print_queue` panic strings and
`"overflow detected"`, already present from an earlier object.

This is the **fifth sighting** of the rule 301, 312, 314, 319 and 320 established, the third *shown*
rather than inferred, and by far the largest: **63% of the section**.

*(The paragraph that stood here read the name term as the same rule at pool scale — 832 names summing
0x4458 by `align4(len+1)` against a pool of 0x3C77, hence "the pool merges shared tails as well as whole
duplicates, and a name term is an upper bound whichever way it is computed". **Experiment 322 withdraws
that**: see the correction below. The 832 names include the 108 storage stand-ins, whose own `align4` sum
is 0x810 — which is the whole of the gap — and the pool never holds a storage name at all.)*

## Correction, from experiment 322: the name term, and where the 0x58 went

322 measured the name pool directly on the three images the device ran, and the term is exact and has a
closed form. **The pool holds one padded slot per *function* stub and nothing else**, so

```
name term = Σ align4(len+1) over the added function-stub names
          − Σ align4(len+1) over the retired function-stub names
```

| image | function stubs | pool extent (first name → next input) |
|---|---|---|
| 320 | 724 | 0x3C87 |
| 321 | 725 | **0x3CF7** (+0x70) |
| 322 | 719 | **0x3C77** (−0x80) |

and both deltas are byte-exact under that model. 321's **+0x70** is 0x98 (the five names it added:
`IOMapPages` 12, `IOUnmapPages` 16, `getPhysicalAddress` 48, `inTaskWithPhysicalMask` 64, `proc_name` 12)
minus 0x28 (the four it resolved: `IOFree` and `IOSleep` 8, `IOLibInit` and `IOMalloc` 12). `align4(len+1)`
over *all* of an image's function names reproduces its pool's **span** to within one byte (0x3CE8 against
0x3CE7 here), so no tail merging is happening.

**So this experiment's 0x58 is not tail merging.** The prediction summed all seven added names —
0xC8 = 0x98 of function names **+ 0x30 of storage names** — and left the retired 0x28 out of the sum, while
the linker placed 0x98 − 0x28 = **0x70**. The 0x58 is exactly 0x30 + 0x28:

* **0x30** = `debug_iomalloc_size` (20) + `debug_iomallocpageable_size` (28). Both are *storage* stand-ins,
  so neither name enters the pool — neither is present anywhere in the image.
* **0x28** = the four resolved names, already subtracted in the prediction's parenthetical and omitted from
  its sum — the arithmetic slip recorded (307, 317) as this ledger's recurring one.

One number in this document's closure table is corrected with it: the base of the name term was quoted as
**0x3C77**, which is 320's pool *span*; 320's pool *extent*, the definition its end 0x3CF7 uses, is
**0x3C87**. Same value, two definitions, and the +0x070 delta was right either way.

## The 16 KB boundary was crossed, and the image grew by 0x4048

`.text` now ends at 0x8013D3E0, past 0x8013C000, so the whole data block stepped **+0x4000** and `.data`
begins at 0x80140000. The image is the span [base, `.init_array`'s end]:

```
base:  0x8013C000 + 0x191C8 + 0x10C + 4 = 0x1552D8   (1397464)
real:  0x80140000 + 0x19210 + 0x10C + 4 = 0x159320   (1413920)
                                          delta = +0x4048 = the step + the object's 0x48 of data
```

**The 0x1A80 of text cost the image nothing at all**, because it sat entirely inside the gap between the
text's end and the next 16 KB boundary — a gap that was 0x6A0 wide before the step and 0x2C20 after it.
That is 304's rule stated for the third time: 318 moved the block +0x14000 for +0x16C80 of text, 319 moved
nothing for +0x140, and this one pays a boundary for text that fitted under it. It is also why the image
prediction of 0x15AFB7 — which *added* the text growth to the step — was 0x1CF7 too high.

| | base (320) | measured (321) | delta |
|---|---|---|---|
| `.text` | 0x13B960 | **0x13D3E0** | +0x1A80 |
| `.data` | 0x8013C000 (0x191C8) | **0x80140000** (0x19210) | +0x4000 start, +0x48 size |
| `.sysctl_set` | 0x801551C8 (0x10C) | 0x80159210 (0x10C) | +0x4048 |
| `.init_array` | 0x801552D4 (0x4) | 0x8015931C (0x4) | +0x4048 |
| `.bss` | 0x80155300 (0x37658) | **0x80159340** (0x37858) | +0x4040 start, +0x200 size |
| `__bss_end` | 0x8018C958 | **0x80190B98** | +0x4240 |
| image | 1397464 (0x1552D8) | **1413920 (0x1592C0)** | +0x4048 |
| headroom | 1521320 | **1504360** | −0x4240 |

`.bss` closes the plain way: the object's **0x180** plus **two** 64-byte stand-in slots is **0x200**, and
the section grew exactly 0x200 because the fill in front of the stand-ins did not move at all (0xFA /
38 fills in both maps). Nothing is retired this step — all four resolved names are functions — so nothing
cancels and no alignment term appears.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=IOLockAlloc
 xnu_entry_stub_caller=0x8011b528
 xnu_entry_stub_caller_a=0x8011b528
 xnu_entry_stub_caller_e=0x8011b528
```

`tools/host_resolve_entry_addr.sh 0x8011b528` → `IOLibInit+0x10c`, `caller-4` = `0x8011b524: bl
8011d86c <IOLockAlloc>` — the predicted call at the predicted offset, and the first prediction in this
walk whose offset base needed no conversion: `IOLibInit` is the *first* function in its object's `.text`,
so the relocation-stream offsets are the function's own (320's, which needed the 0xF0 correction, is the
case that taught the difference).

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, `STAGE90_HW_WATCHDOG ARMED`, software dead-man
armed, no storage symbols in the payload), log **301623** bytes, one `stub_hit=` line, **no `exception:`
line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`, and the device returned to Android
on its own (`ro.build.version.release` = 10).

## What it measures: `IOLibInit` ran its whole body

* The guard took the **run** branch: the object's own one-byte static `libInitialized` was zero. It lives
  at 0x8018EDFC, inside the image's `.bss`, and the `ldrb`/`cmp`/`bne` at the top of `IOLibInit` did not
  return early.
* `lck_grp_alloc_init("IOKit", NULL)` allocated a lock group and stored it into `IOLockGroup`.
* `kmem_suballoc` carved the **96 MB** pageable map (0x6000000) out of `kernel_map`, and its failure
  branch was **not** taken — so `panic` was not called and the string at 0x80138B81 was not printed. This
  is the first `kmem_suballoc` this boot has ever made.
* Both page allocators were initialised: `gIOBMDPageAllocator` and the private pageable one, each
  `bzero`'d and given a real `IOLockAlloc`-shaped lock, and `gIOPageAllocChunkBytes` was set to 64.
* The second `lck_mtx_alloc_init` result went into the pageable allocator's lock slot, and the pageable
  space was given its 96 MB base.

One detail of the disassembly worth keeping: the object's relocation for **`bzero` resolved to
`__bzero`** (0x800039D8) — gcc's builtin, not the libkern `bzero`. A name that is "already real" in the
ledger can still be an alias at link time.

## What it does not measure

* **`IOLockAlloc`.** It is defined by `iokit/Kernel/IOLocks.cpp`, the next step.
* **The 38 definitions nothing references yet** — `IOFreeAligned`, `IOMallocContiguous`, `IOCreateThread`,
  `IOLog`, `IOPanic` and the rest are defined, correct, linked and unreached. Their added-count of zero is
  the 250/270 rule, not a statement about the run.
* **The pageable map's 96 MB.** `iopa_alloc` and the pageable path are real from this step and nothing has
  called them.
* **Whether the four deduplicated strings' addresses matter to anything.** They are read by the same
  `panic` paths either way; that the linker put this object's copies away is a statement about the link.

## Next

**`iokit/Kernel/IOLocks.cpp`** (`iokit_Kernel_IOLocks.o`) — the object that defines `IOLockAlloc`, and the
opposite shape to this one: `.text` **820** for **26 functions** and **no `.bss`, no `.data` and no
`.rodata` at all**, with **19 references**, every one of which is already defined. Predicted **6 resolved /
0 added** (835 → **829** undefined, 725 → **719** function stubs, storage unchanged at 110), the six being
`IOLockAlloc` plus the five `IORecursiveLock*` names something already stubs.

**And the stop should leave `IOLibInit` entirely.** `IOLockAlloc` is twelve bytes — load `IOLockGroup`,
`ldr`, then **`b lck_mtx_alloc_init`**, a tail branch to a real function — so the first `IOLockAlloc`
returns, the second one at `IOLibInit+0x12C` is real too, `IOLibInit` runs off its epilogue, and
`StartIOKit` continues to the call 320 created as a stub: predicted stop **`OSlibkernInit`, caller key
`0x8011B268` = `StartIOKit+0xC8`**.
