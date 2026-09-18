# Experiment 297 — `__bss_start` is the start of `.bss`, and the 3252 bytes every run was erasing

**Step:** move `__bss_start = .;` from between the `.data` and `.bss` output sections to the first
statement *inside* `.bss` — the defect the 296 build found in the instrument. One line moves; nothing
else in `entry.ld` changes.
**Prediction:** that every address stays where it is except `__bss_start`, which becomes the `.bss`
output section's first byte (0x80130940, +0xCC8); that the image is byte-identical; and that the stop
stays `kpc_thread_create` at `xnu_entry_stub_caller=0x8000b034`, because no code has moved.
**Result:** every line of it, and the run confirms the two ranges are disjoint — the copy ends at
0x80130930 and the memset now starts 16 bytes later.

## The defect, in one sentence

`__bss_start` was assigned between two output sections, which names the *end of `.data`*; it names the
start of `.bss` only if the linker places nothing in between, and the linker placed the two Mach-O
orphans there — so the payload's `memset(BSS_START, 0, BSS_END - BSS_START)`, which runs *after* the
image is copied in, zeroed 3252 bytes of initialized data on every run this project has ever made.

## What was being erased, measured

The 296 image's two orphan sections, in full:

```
__DATA, __const   0x8012fc78   0x144    `const_boot_args` (0x140) + `BootArgs` (4)
__DATA, __data    0x8012fdc0   0xb70    122 `vm_allocation_site`s x 24 bytes = 2928
```

`__DATA, __const` is **all zero in the file** — `arm_init.c:154` writes both `const_boot_args = *args`
and `BootArgs = &const_boot_args` at boot, before anything reads them — so zeroing those 324 bytes
cost nothing. Everything that mattered is in `__DATA, __data`, and all **129 non-zero bytes** of the
3252 are in the three fields `VM_ALLOC_SITE_STATIC` initializes:

| initializer | sites | non-zero bytes each | total |
|---|---|---|---|
| `refcount=2  tag=0    flags=0` | 117 | 1 | 117 |
| `refcount=2  tag!=0   flags=0` | 3 | 2 | 6 |
| `refcount=2  tag!=0   flags=0x0080` | 2 | 3 | 6 |
| | **122** | | **129** |

The 117 are `kalloc`/`kalloc_noblock`/`kallocp` sites (`VM_ALLOC_SITE_STATIC(0, 0)`), the 3 are the
`kalloc_tag` family (`(0, itag)`) and the 2 are the `kalloc_tag_bt` family
(`(VM_TAG_BT, itag)`, `VM_TAG_BT` = 0x0080) — `osfmk/kern/kalloc.h:90-139`. The stride is 24 bytes
because `struct vm_allocation_site` is `total` (8), `mapped` (8), `refcount` (`int16`), `tag`
(`uint16`), `flags` (`uint16`), `subtotalscount` (`uint16`) with no `peak` field, i.e. this is a
RELEASE build with `DEBUG`/`DEVELOPMENT` off.

**This corrects 296's own note, which said the zeroed `flags` were "zero either way".** They are not:
five of the 122 sites carry a real `tag`, and two of those carry `VM_TAG_BT` in `flags`. The field
that matters most is `tag`, because `vm_tag_alloc_locked` opens with

```c
    if (site->tag) return;
```

(`osfmk/vm/vm_resident.c:8212`). So until this step, those five sites — and no others — went through
the tag allocator on first use instead of keeping the tag they were built with: taking a tag out of
`free_tag_bits` and calling `OSAddAtomic16(1, &site->refcount)` on a count that had been zeroed from
2 (`vm_resident.c:8251`).

## The fix is the project's own answer, from the other linker script

`stages/stage90/linker.ld:19` has always written

```ld
    .bss : {
        __bss_start = .;
        *(.bss*)
```

with the symbol *inside* the output section, which is the placement that makes the claim true however
the linker orders anything around it. **The two scripts have disagreed about this for as long as both
have existed, and the entry script was the wrong one.** So the fix is not a new idea; it is that idea
applied where it was missing. It is a step of its own because a change to the linker script is the
kind of change that moves addresses.

## The build

```
                   predicted        measured
undefined          772              772
function           682              682
storage             90               90
.data              0x80118000       0x80118000
__DATA, __const    0x8012fc78       0x8012fc78
__DATA, __data     0x8012fdc0       0x8012fdc0
.bss               0x80130940       0x80130940
__bss_start        0x80130940       0x80130940     <- was 0x8012fc78
__bss_end          0x80167718       0x80167718
image              1247536          1247536
text               1139704          1139704
__entry_data_filesize   +0xCC8       0x1a2d8 -> 0x1afa0
```

and two new build lines, from the two new checks:

```
  __bss_start 0x80130940 is the .bss output section's first byte (224728 bytes to 0x80167718)
  the copied image ends at 0x80130930, 16 bytes below __bss_start, so the memset touches nothing that was copied
```

**The image itself did not change at all.** The section table is identical to the 296 image, address
for address, offset for offset and size for size — `.text` at 0x80000000, the five Mach-O orphans
where the linker had put them, `.data` at 0x80118000, `.bss` at 0x80130940 — so `objcopy`'s binary is
byte-identical and the only difference in what the device is handed is the one constant the payload
compiles in from `xnu_arm_entry.h`. That is the shape this kind of fix should have: the layout is
untouched, and 3252 bytes that were in the memset range are not.

## The two checks, and why there are two

**This is a defect about a placement, so the fix needs a check that is not one.** They say different
things:

* a **layout invariant**, stated over the two numbers the payload is compiled from:
  `bss_start - ENTRY_BASE >= bin_size`. This is the property that matters — the copy and the memset
  do not overlap — and it holds however the linker chooses to order, merge or place anything.
  **It is what would have failed the 296 build**, 0xCC8 short, and it is the check whose absence let
  nine experiments run with the overlap.
* **`verify_bss`**, structural: `__bss_start` *is* the `.bss` output section's first byte. The
  arithmetic invariant would still pass if the orphans moved below `.bss`: the memset would then start
  at the end of `.data` and zero the alignment fill between the two, which costs nothing. `verify_bss`
  is what makes `__entry_data_filesize` mean what the Mach-O header says it means, and it fails the
  moment anything is placed between `.data` and `.bss` again.

Nothing else had to change: the pin (`ResetHandlerData - ExceptionLowVectorsBase = 0x167704`, writes
at 0x80167708 and 0x8016770c) followed on its own, because both of its symbols are derived from
`__entry_reset_handler_data`, which is inside `.bss` and did not move.

## The run

```
stub_hit=kpc_thread_create    xnu_entry_stub_caller=0x8000b034     (unchanged, as predicted)
xnu_entry_bss_start=0x80130940                                     (was 0x8012fc78)
xnu_entry_bss_bytes=0x00036dd8                                     (was 0x00037aa0)
xnu_entry_copied_bytes=0x00130930                                  (unchanged)
```

`0x80130930` is where the copy ends; `0x80130940` is where the memset now begins. **The two are
disjoint, 16 bytes apart**, which is the arithmetic the new check states and the run confirms against
the payload's own numbers. The 122 allocation sites keep their `refcount = 2` and their five real
`tag`s.

**The stop did not move, and that is the negative half of the result rather than a surprise.** Five
sites no longer allocate a tag on first use, no site's `refcount` starts at 0, and nothing between
`arm_init` and `thread_create_internal` notices — which is what the code predicts: `refcount` has one
other reader, the reclaim loop `vm_tag_alloc_locked` reaches only when `free_tag_bits` is exhausted
(`vm_resident.c:8234`, `1 != prev->refcount`), and nothing on this path has exhausted 8192 tags. The
state that was wrong is now right; the boot has not yet reached the place where being wrong would have
shown. Where that place is: the tag-slot accounting in `vm_allocation_sites[]` and
`vm_allocation_zone_totals[]`, and the release path at `vm_resident.c:8520-8532`, which asserts
`refcount > 0` and acts on the transition through 1.

This is the same family as the pad defect 288 and 291 fixed — **a value with two definitions, one of
them the link's and the other the instrument's assumption about the link** — and it is the
twenty-first instance of it this project has recorded. Like the first of the two pad instances, the
interesting part is that it was found by a *number* being wrong rather than by anything failing: nine
experiments ran with 3252 bytes of initialized data erased and every report was clean.

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`), log
301118 bytes, no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10).

**Next:** experiment 298 — the orphan sections themselves, on their own, because naming them in
`entry.ld` moves `__entry_text_end` and `__entry_data_start` and so changes the `__TEXT`/`__DATA`
split the Mach-O header describes. The complete set of Mach-O-style names over all 695 objects is
`__TEXT, initcode` (1 object), `__TEXT,__const` (1), `__TEXT,__os_log` (5), `__DATA, __const` (1),
`__DATA, __data` (207) and `__DATA,__sysctl_set` (105) — and they are **not** all in one place: the
linker put the two `__DATA, __*` ones after `.data`, and `__DATA,__sysctl_set` up with the read-only
group after `.text`. That is the clearest evidence available that ld's orphan placement is not a
policy anyone can rely on, and it also decides the treatment: `__DATA,__sysctl_set` is the one of the
six whose *name* is an interface — `LINKER_SET_BEGIN(__sysctl_set)` looks it up by name through the
Mach-O header (`bsd/sys/linker_set.h:193`, `getsectdatafromheader(_header, "__DATA", _set, &_size)`) —
so it wants an output section of its own and an entry in `entry_macho.s`, not to be merged into
`.data`; and `__TEXT, initcode` holds `memorystatus_init`, so it belongs in `__TEXT` rather than in
the writable `__DATA` region it is in today.

Then 299 — `osfmk/kern/kpc_thread.c` for `kpc_thread_create`, and
`sched_set_thread_base_priority`/`sched_thread_mode_demote` (`osfmk/kern/priority.c`), which closes
`thread_create_internal`; after that `kernel_thread_create` returns a real thread to
`kernel_bootstrap`, which calls `thread_deallocate` and branches to `load_context` — the first time
this walk crosses into a context switch rather than a function call.
