# Experiment 377 — `bsd/kern/kern_malloc.c` and `iokit/Tests/Tests.cpp`: one stop that is two things, two objects, one for the symbol and one for the value

**Step:** link two objects, `bsd/kern/kern_malloc.c` (`bsd_kern_kern_malloc.o`) and
`iokit/Tests/Tests.cpp` (`iokit_Tests_Tests.o`), into the entry link between
`iokit_Kernel_IOPowerConnection.o` and `stages/stage90/xnu_platform/MSM8974PlatformExpert.o`, in that
order. Nothing else changes.

**Prediction:** *6 resolved (5 function, 1 storage) / 0 added — 756 → **750** undefined, 657 → **652**
function, 99 → **98** storage*; `bsd_kern_kern_malloc.o` `.text` **0x8017F820** (0x49C) and
`iokit_Tests_Tests.o` `.text` **0x8017FCBC** (0x38); `.data` moved for the first time since 369 —
0x801AC000 (**0x1A328**); `.sysctl_set` 0x801C6328 (**0x158**); `.init_array` 0x801C6480 (0x90);
`.bss` **0x801C6540 (0x39098)**; text size **1746848**; image **1860880**; args **0x80201000**;
headroom **2099752**; and the stop at **`throttle_init`** at key **`0x8003A9FC`**.

**Result:** all three counts exact and both stub-name facts exact (`__MALLOC` and
`iokit_Tests_Tests.o`'s oid gone; `stubnames.txt` reads 652 function, 98 data), `realstubs.o`'s
`.rodata.str1.4` row exact at **0x36DA**, every `.text` row, `.bss` row, `bss_start`, `bss_end`,
`args_pa`, `top_of_kernel_data`, headroom and the image to the byte — and **the stop is not the one
predicted**: the run stopped at `work_interval_thread_terminate`, in a **thread teardown** frame, with
`throttle_init` still record 552 of the stub list, so `bsd_init` was never entered. The falsifier that
fired is (f), the one the block wrote for exactly this. Two layout misses, both explained and both
worth keeping: the `.rodata` saving is not local (five *earlier* objects' string chunks shrank), and
`.data` is placed in **two groups**, not one.

## The two objects

```
== bsd_kern_kern_malloc.o
   23 definitions, 16 references
    resolved (5: 5 function, 0 storage)
       _FREE                                                    object T, stand-in was func T
       _FREE_ZONE                                               object T, stand-in was func T
       __MALLOC                                                 object T, stand-in was func T
       __MALLOC_ZONE                                            object T, stand-in was func T
       kmeminit                                                 object T, stand-in was func T
    added (0: 0 function, 0 storage)
    of the 16 references, 16 are already satisfied
== iokit_Tests_Tests.o
    3 definitions, 2 references
    resolved (1: 0 function, 1 storage)
       sysctl__kern_iokittest                                   object D, stand-in was data D 0x30
    added (0: 0 function, 0 storage)
    of the 2 references, 2 are already satisfied
```

Six names retired, none created: **both objects' whole closures are already real**, so the only thing
that can stop this run on this step's account is one of the six names they retire. Five are function
records — `_FREE` 85, `_FREE_ZONE` 86, `kmeminit` 175, `__MALLOC` 280, `__MALLOC_ZONE` 281 — and the
sixth is `data sysctl__kern_iokittest D 0x30`, record 523 of the 99 data records.

**And it is the second object that makes this step a step rather than a trap.** 376's stop was
`__MALLOC`, hit at `0x80106760` (the `bl` at `0x8010675C` = `sysctl_register_oid + 0x2C`, the
*fourth* of `IOPMrootDomain::start`'s eight oid registrations). The first three take the `+0x4C` leg
because their oids are real `.data` carrying `CTLFLAG_OID2`; the fourth is `sysctl__kern_iokittest`,
whose storage is a stand-in, so its `oid_kind` reads 0, the flag bit is clear, and the guard falls
into the leg that calls `__MALLOC`. **The stop is two defects in one site — a missing symbol and an
invented zero — and each alone leaves the site broken in a different way.** Linking only
`bsd_kern_kern_malloc.o` makes `__MALLOC` real and *converts the stub stop into a data abort*: the leg
reads `ldr r6,[r5]` (the oid's `oid_parent`) at `+0x0C` and `ldr r0,[r6]` at `+0x70` before any guard
that could bail, so a still-zeroed stand-in gives `abort_first_dfar = 0` at **0x801067A0** — 342's
shape exactly. `iokit_Tests_Tests.o` is the other half: its `.data` is 0x30 bytes carrying
`oid_kind = 0xC3C00002` at offset 12 (little-endian `02 00 c0 c3`), so `CTLFLAG_OID2`
(0x40000000) *is* set and call #4 takes the same real leg the first three took. **One step, two
objects, one for the symbol and one for the value.**

Both objects also carry `.data` for the first time in a long while — `bsd_kern_kern_malloc.o` 0x848,
`iokit_Tests_Tests.o` 0x30, plus 4 bytes of `__DATA,__sysctl_set` each — so 377 is the step that
moves the `.data` bucket, which is `align_up(__entry_text_end, 0x4000)`.

## The stop the block predicted: the site is clean, the frontier one phase downstream

`sysctl_register_oid` has three direct calls, the first stub call is `+0x2C` (`__MALLOC`) and there is
**no stub call anywhere on its straight line from `+0x74`**. So after this step the function that
stopped 376 runs to completion: the fourth oid takes the `+0x4C` leg, reads `oid_refcnt`, locks
exclusively and links the oid into the tree, and then calls #5 to #8 do the same over the four real
`.data` oids `debug_iokit`, `hw_targettype`, `consoleoptions` and `progressoptions`.

`IOPMrootDomain::start`'s tail is a *virtual* call — `ldr r0,[r4]` / `ldr r2,[r0,#352]` / `blx r2`,
the vptr-adjusted `_ZTV14IOPMrootDomain + 0x160` slot holding 0x8012B80C =
`IOService::registerService` — and from there the walk measured every frame in this phase it can name
and found no stub on any of their straight lines (`registerService`, `startMatching`, `_IOServiceJob`,
`doServiceMatch`, `probeCandidates`, `_IOConfigThread::main`, `startCandidate`, `IOService::start`,
both platform experts). The image-wide scan agrees: none of those frames is among the 622 functions
that call a stub. **So the frontier was aimed past the whole IOKit bring-up, at `bsd_init`'s second
line**, `+0x8 bl 8003a9f8 <throttle_init>`, key `0x8003A9FC` — and that prediction was written as
conditional, because the route between the last stop and `bsd_init` is the part of the image the call
walk cannot follow (it lists 218 indirect sites under `kernel_bootstrap`, 12 under `registerService`,
20 under `doServiceMatch`).

## What the run measured

```
 xnu_entry_checks=0x00000005              xnu_entry_failures=0x00000000
 xnu_entry_stub_caller_v=0x80009544       xnu_entry_abort_entries=0x00000000
 xnu_entry_stub_caller_digits=0x00000042
 xnu_entry_stub_caller_w0=0x30303038 ("8000")   w1=0x34343539 ("9544")
 xnu_entry_abort_first_dfar=0x00000000    xnu_entry_abort_first_pc=0x00000000
 xnu_entry_image_bytes=0x001c6518         xnu_entry_bss_start=0x801c6540
 xnu_entry_bss_end=0x801ff5d8             xnu_entry_args_pa=0x80201000
 xnu_entry_top_of_kernel_data=0x80400000  xnu_entry_why_byte=0x00000061
 xnu_entry_checksum=0x907fe5c5
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=work_interval_thread_terminate
No errors detected
```

**The stop is `work_interval_thread_terminate`, and it is not the one predicted.** The key
`0x80009544` is `thread_terminate_self + 0xE8`, the return address of the `bl` at `0x80009540`
(`eb05e8ba bl 80183830 <work_interval_thread_terminate>`, disassembled out of the image the device
ran). Both falsifiers that would have caught a wrong *step* are clean: `abort_entries=0`,
`abort_first_pc=0`, `abort_first_dfar=0`, `checks=5`, `failures=0`, no `panic` line, and the log ends
in the kernel's own `No errors detected`. The coupling the block predicted is retired: nothing aborted
at `0x801067A0` and `__MALLOC` is gone from the stub list.

**`throttle_init` is still record 552 of the stub list**, which is the sharpest single fact of this
run: `bsd_init` was *not* entered, so the boot never left the IOKit phase — and yet it travelled all
the way to a **thread teardown**. That frame belongs to `thread_apc_ast` (0x800EB1C8), the *only*
caller of `thread_terminate_self` in this image, and the call sits behind a guard read out of the
thread's own storage:

```
800eb210:  ldrb  r0, [r5, #708]   ; thread->active, +0x2C4
800eb214:  tst   r0, #1
800eb218:  bne   800eb228         ; still active: skip the teardown
800eb21c:  mov   r0, r4
800eb220:  bl    80014430 <lck_mtx_unlock>
800eb224:  bl    8000945c <thread_terminate_self>   ; <- the frame the stub is called from
```

so this is `if (!thread->active) { … thread_terminate_self(); }` of `osfmk/kern/thread_act.c:934`: the
variant taken is the *inactive* one, at 0xE4 into `thread_terminate_self`'s body, in a frame nowhere
near `IOPMrootDomain`, `registerService` or `bsd_init`. **This is the walk's first stop in a teardown
path rather than a bring-up one, and its site is chosen by a byte of a thread's state.** The falsifier
that fired is (f): the run advanced into machinery whose frames are virtual dispatches and stopped in
a frame the model had no way to place — not in a frame the model placed wrongly.

**The other four falsifiers, one by one:** (a) `throttle_init` at `0x8003A9FC` — *no*, and the key it
did report is a return address as the block's own arithmetic said it would be. (b) no stop at
`__MALLOC` — yes, gone from the image's stub list entirely. (c) no abort at `0x801067A0` — yes,
`abort_entries=0` and all three `abort_first_*` zero, so the second object did its job and the oid was
real when the registrar read it. (d) the counts — 750 / 652 / 98, `xnu_entry_stubnames.txt` reading 652
function and 98 data records, exact. (e) the layout — mostly exact, with the two misses below.

## The layout

| | 376 measured | 377 predicted | 377 measured |
|---|---|---|---|
| counts | 756 / 657 / 99 | **750 / 652 / 98** | **750 / 652 / 98** |
| IOCommand `.text` | 0x8017F4DC (0x15C) | unmoved | **0x8017F4DC (0x15C)** |
| IOPowerConnection `.text` | 0x8017F63C (0x1E0) | unmoved | **0x8017F63C (0x1E0)** |
| `kern_malloc` `.text` | — | 0x8017F820 (0x49C) | **0x8017F820 (0x49C)** |
| `Tests` `.text` | — | 0x8017FCBC (0x38) | **0x8017FCBC (0x38)** |
| platform expert `.text` | 0x8017F820 (0x150) | 0x8017FCF4 (0x150) | **0x8017FCF4 (0x150)** |
| last kernel ctor `.text.startup` | 0x8017F974 (0x4) | 0x8017FE48 (0x4) | **0x8017FE48 (0x4)** |
| rtabi `.text.eabi` | 0x8017F978 (0x54) | 0x8017FE4C (0x54) | **0x8017FE4C (0x54)** |
| `realstubs.o` `.text` | 0x8017F9CC (0x3D98 = 657 × 0x18) | 0x8017FEA0 (0x3D20 = 652 × 0x18) | **0x8017FEA0 (0x3D20)** |
| `.rodata` run start | 0x80183A10 | 0x80183E6C | **0x80183E6C** |
| IOCommand `.rodata` | 0x801A5400 (0x84) | unmoved | **0x801A5838 (0x84)** |
| IOPowerConnection `.rodata` / `.str1.1` | 0x801A5490 (0x38C) / 0x801A581C (0x12) | unmoved | **0x801A58C8 / 0x801A5C54** |
| `kern_malloc` `.rodata.str1.1` | — | 0x801A5C8A (0x4BD) | **0x801A5C66 (0x4CF)** |
| `Tests` `.rodata.str1.1` | — | 0x801A6147 (0x0A) | **0x801A6135 (0x0A)** |
| platform expert `.rodata` / `.str1.1` | 0x801A5830 (0x440) / 0x801A5C70 (0x16) | 0x801A6154 (0x440) / 0x801A6594 | **0x801A6140 / 0x801A6580** |
| `.rodata.macho` | 0x801A5C88 (0x14C) | 0x801A65AC (0x14C) | **0x801A6598 (0x14C)** |
| `realstubs.o` `.rodata.str1.4` | 0x801A5DD4 (0x3716) | 0x801A66F8 (0x36DA) | **0x801A66E4 (0x36DA)** |
| `.text` raw end / end | — / 0x801A9EA0 | 0x801AA790 / 0x801AA7A0 | **0x801AA780 / 0x801AA780** |
| text size | 1744544 | 1746848 | **1746816** |
| `.data` | 0x801AC000 (0x19AB0) | 0x801AC000 (0x1A328) | **0x801AC000 (0x1A330)** |
| `.sysctl_set` | 0x801C5AB0 (0x150) | 0x801C6328 (0x158) | **0x801C6330 (0x158)** |
| `.init_array` | 0x801C5C00 (0x90) | 0x801C6480 (0x90) | **0x801C6488 (0x90)** |
| `.bss` | 0x801C5CC0 (0x390D8) | 0x801C6540 (0x39098) | **0x801C6540 (0x39098)** |
| `realstubs.o` `.bss` | 0x801FC700 (0x2684) | 0x801FCF80 (0x2644) | **0x801FCF80 (0x2644)** |
| `__bss_end` | 0x801FED98 | 0x801FF5D8 | **0x801FF5D8** |
| image | 1858704 (0x1C5C90) | 1860880 (0x1C6510) | **1860888 (0x1C6518)** |
| headroom | 2101864 | 2099752 | **2099752** |
| args / topOfKernelData / tree / window | +2097152 / +4194304 / +6291456 / 8388608 | args **+2101248**, rest unmoved | **args 0x80201000, top 0x80400000** |

### What was right, and why it is the whole of the `.text` bucket

The `.text` run's delta is `+0x49C + 0x38 − 5 × 0x18` = **+0x45C** — two objects, five stub bodies
retired, none created — and every row of it landed to the byte, including both new objects' own
`.text` and `realstubs.o`'s 0x3D20 = 652 × 0x18. `realstubs.o`'s `.rodata.str1.4` came out **0x36DA**,
which is the second confirmation of the name-slot rule 376 corrected and the first time its
retired-name term was `Σ align4(len+1)` over five names (0x3C) rather than the two-name case that
found the rule. **`.bss` and everything derived below it is exact as well** — `bss_start=0x801c6540`,
`bss_end=0x801ff5d8`, `args_pa=0x80201000` (the args page moving, as predicted),
`top_of_kernel_data=0x80400000`, headroom 2099752 — and the args page moved for the reason the block
gave: the `.bss` end crossed 0x1FF000 to 0x1FF5D8, so `align_up(bss_end − ENTRY_BASE, 0x1000)` went
from 0x1FF000 to 0x200000. The 0x40 the retired data record frees is *not* what decided that; it is
the +0x880 the `.data` bucket took that moved it.

### Miss 1 — the `.rodata` saving is not local either, and this time it was measured rather than argued

Every row from `iokit_Kernel_IOMapper.o` onward sits at **+0x438**, not +0x45C, so 0x24 of the run's
content disappeared upstream of the insertion point. The block's first account of this said "every
input between the two is byte-identical in size to 376", and **that is false** — an attribution, not a
measurement, and the wrong one. The correction came from rebuilding 376: the same build script with
the two objects taken out of the link list and run into a scratch `out/`, which reproduces 376's
published numbers exactly (text 1744544, image 1858704, `.bss` 0x801C5CC0..0x801FED98, headroom
2101864) and so is a 376 map to diff against. Five inputs had shrunk, and no others had changed:

```
osfmk_arm_pmap.o        .rodata.str1.1   0xBBF -> 0xBBA   (-0x5)
bsd_kern_bsd_init.o     .rodata.str1.1   0x184 -> 0x17F   (-0x5)
bsd_kern_kdebug.o       .rodata.str1.1   0x125 -> 0x123   (-0x2)
osfmk_kern_task.o       .rodata.str1.1   0x3F9 -> 0x3F3   (-0x6)
bsd_kern_kern_event.o   .rodata.str1.1   0x712 -> 0x70C   (-0x6)
                                         total            -0x18
```

and the alignment fills over the same window fell from 89 bytes to 77, **−0xC** — the term a sum of
section *contents* cannot see, which is 375's lesson in a new place. −0x18 + −0xC = **−0x24**, and the
window between the run's first row and `iokit_Kernel_IOMapper.o`'s `.rodata` measures 0x20EE0 in 376
against 0x20EBC here: the arithmetic closes exactly. The shift steps down as the run is walked —
+0x45C to +0x458 at `osfmk_arm_pmap.o`'s `.rodata.str1.1`, +0x453 at `osfmk_kern_printf.o`'s, +0x450
at `osfmk_kern_debug.o`'s `.rodata`, +0x44C at `bsd_kern_bsd_init.o`'s `.rodata.cst32`, +0x448 at
`osfmk_vm_vm_init.o`'s `.rodata`, +0x440 at `osfmk_kern_task_policy.o`'s, +0x43A at
`bsd_kern_kern_event.o`'s `.rodata.cst8`, and +0x438 from `osfmk_kern_priority.o`'s `.rodata` to the
end of the run.

**And the five objects that lost bytes are nowhere near the insertion point**: `pmap.c`,
`bsd_init.c`, `kdebug.c`, `task.c`, `kern_event.c`. That is 301's rule read forward as its sharper
form — an object's size for a mergeable section is an upper bound, *and the saving is not local
either*: a string inserted at a late position is a new tail that earlier strings can be suffixes of,
so the duplicates collapse into the new copy and the bytes come out of every earlier chunk that shared
a string with it. **A prediction that books the saving where the object was inserted gets both the
object's own row and every row before it wrong**, and no single object's size can tell you which.
The object's own chunk came out **0x4CF** (block: 0x4BD, band [0x4BD, 0x501]; the object's raw 0x501
is the upper bound and the map's number is the answer) and `iokit_Tests_Tests.o`'s **0x0A** exactly as
predicted.

### Miss 2 — the `.data` output section has two groups, and that is a structural fact about `entry.ld`

The block assumed the two new `.data` inputs append after the *last* `.data` input in link order, and
they do not. `entry.ld`'s `.data` output section is:

```
*(.data .data.*)                      <- group 1, in link order
*("__DATA,*__data" "__DATA,*__const") <- group 2, placed after all of group 1
```

so a `.data`-named input can only append inside **group 1**, and the last one before the insertion
point is `osfmk_prng_fips_sha1.o`, ending **0x801C4874**. The two new sections land there.
`bsd_kern_kern_malloc.o`'s `.data` is 8-aligned and the cursor is 0x801C4874, so the linker inserts a
**0x4 fill** first and `kern_malloc`'s 0x848 goes at **0x801C4878**; `iokit_Tests_Tests.o`'s 0x30
follows at **0x801C50C0** and group 1 then ends at 0x801C50F0. Group 2, which upstream began at
0x801C4874, therefore begins **0x8 later**, and one of *its* internal alignment fills changes by
another **0x4**. Group 1 grew 0x18874 → 0x190F0 (+0x87C: 0x848 + 0x30 + the 0x4 fill) and group 2
0x123C → 0x1240 (+0x4), so `.data` grew by **+0x880** to 0x1A330.

Everything downstream inherits it: `.sysctl_set` **0x801C6330** (0x158 exact), `.init_array`
**0x801C6488**, image **0x1C6518** — 0x8 more than predicted. **And the 0x8 did not reach anything
below it**: `.bss` starts at `align64(.init_array end)` and 0x801C6518 rounds up to 0x801C6540
exactly as 0x801C6510 does, so `__bss_end`, the args page, `topOfKernelData` and the headroom all
came out as written. That is the second time in three steps that an arithmetic change in `.data` has
been absorbed by a rounding step and left the derived numbers alone.

### The text size, and the 0x20 it missed by

1746816 (0x1AA780) against 1746848 (0x1AA7A0). Route 1, position: the `.rodata` run's own shift is
+0x438 rather than +0x45C, so `realstubs.o`'s `.rodata.str1.4` sits at 0x801A66E4 where the block wrote
0x801A66F8 — **−0x14** — then the tail: `*fill* 0x2`, `__TEXT,__const` 0x4, `*fill* 0xC` (the block
predicted 0x8, and this works in the other direction, **+0x4**), `__TEXT, initcode` 0x64C, the three
`__TEXT,__os_log` rows 0x251 / 0x3 / 0x108, `.ARM.exidx` 0x8 — a raw end of **0x801AA780**, which is
already 32-aligned, so `ALIGN(0x20)` contributes **0x0** here and cost the block the remaining
**0x10** because its predicted raw end 0x801AA790 was 16 bytes short of a line. −0x14 + 0x4 − 0x10
= −0x20. **The closing alignment being the thing that moves a number is 377's inversion of 376**, where
the boundary fill absorbed a 4-byte shift and left the text size unmoved.

**One transcription slip, of the class 370 documented**: the prediction line printed the image as
1859856, and its own hex `0x1C6510` is 1860880. The arithmetic of the step was right and the decimal
was not; the measured image 0x1C6518 is that number plus the 0x8 of Miss 2.

## The next object, named before its run

`work_interval_thread_terminate`'s pool definer is **`osfmk_kern_work_interval.o`** —
`osfmk/kern/work_interval.c` — and measured against this image it is a clean single-object step: 16
definitions, 21 references, **2 resolved (2 function, 0 storage) / 0 added** —
`work_interval_thread_terminate` and `work_interval_port_notify` — so 378 takes the counts to
**748 undefined, 650 function, 98 storage**, retiring two stub bodies and two name slots and creating
nothing. It is the smallest kind of step this walk has, which is the point: what 377 bought is a
*place* to stand, and the frame it stands in is a thread teardown.

**And the frontier this block predicted is still ahead, unvisited.** `throttle_init` is record 552 of
the stub list (function index 477, body address 0x80182B58 in this image), and
`bsd_miscfs_specfs_spec_vnops.o` — the pool's only definer of it, and of `rethrottle_thread`,
`throttle_lowpri_io` and the `spec_filtops` R 0x28 stand-in — measures 132 definitions, 170
references, **4 resolved (3 function, 1 storage)** and **91 added (55 function, 36 storage)**, from
`buf_getblk` (pool T 0x92C) and `buf_brelse` (0x768) down to `VNOP_IOCTL` and `VNOP_SELECT`: the
largest incoming step the walk has faced, and the buffer-cache and vnode-operation face of VFS. It is
not this step's object, but it is still the next *named* one on the boot path.

**What the teardown changes is the question, not the answer.** A stop in `thread_apc_ast` is a stop in
a frame chosen by a thread's state — the `tst r0,#1` on `thread->active` above — so the block after
this one has to say which thread was terminating before it can say what comes next, and
`thread_apc_ast` runs on the thread being torn down, which need not be the boot thread at all. The two
readings that distinguish them are already in the run's own vocabulary: the caller key 0x80009544
places the frame, and the kernel's per-thread state would say whose it is.

## Safety

A non-persistent `fastboot boot` of `stage90-qcdt.img` (4880384 bytes, sha256
`509faf051da25de77b8a61a3eed5c2ecde71e78be4eae25edf0be75b60ed56fb`); nothing flashed. 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`, with no non-zero reading
of either. `xnu_entry_checks=0x00000005` / `xnu_entry_failures=0x00000000`, `checksum=0x907fe5c5`,
`abort_entries=0` with `abort_first_pc=0` and `abort_first_dfar=0`, no `exception:` line. Log 301642
bytes, 3975 lines, last line `No errors detected`. The device came back to Android on its own
(`MI 4LTE`, release 10).

The step's own code is a kernel allocator and 0x30 bytes of oid table: `__MALLOC` and the four
functions beside it are reached by a registrar that was already running three of its eight oids, and
the new code allocates and frees from zones the kernel has already initialised — `kmeminit` is what
`bsd_init` calls, and it is *not* on this path. Both objects' references are all satisfied and neither
adds a name, so nothing new can fault at load; the one way this step could have gone wrong is the path
being *longer* than the block said, and the recovery nets (`sleepGate` returning under the hardware
watchdog, and the software dead-man firing on a silent boot) are what would have caught a stop that is
not a stub — a data abort in the allocator, which falsifier (c) was written to detect. It did not
fire.
