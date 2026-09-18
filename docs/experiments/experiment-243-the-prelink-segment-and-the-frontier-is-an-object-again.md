# Experiment 243 — the `__PRELINK_TEXT` Segment, and the Frontier Is an Object Again

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change: one segment, and a host check that makes the property structural

Experiment 242 named the fault and the cause: `arm_vm_prot_init`'s call

```c
	arm_vm_page_granular_RWNX(segPRELINKTEXTB + segSizePRELINKTEXT,
	                             end_kern - (segPRELINKTEXTB + segSizePRELINKTEXT), force_coarse_physmap);
```

was `RWNX(0, end_kern)` because this image's Mach-O has no `__PRELINK_TEXT`, and `getsegdatafromheader`
returns NULL and sets the size to zero for a name that is not present
(`libkern/kernel_mach_header.c`). 1024 iterations of the 4 MB alignment loop, one page-table page
taken from `avail_start` each time, and the first page the protection pass had already written
read-only is where it stopped.

So `entry_macho.s` grows a third `LC_SEGMENT` — empty, at the first free address after `__DATA`:

```
    .long 0x1                            /* cmd = LC_SEGMENT */
    .long 56                             /* cmdsize, no sections */
    .ascii "__PRELINK_TEXT"
    .zero 2
    .long __entry_image_end              /* vmaddr */
    .long 0                              /* vmsize */
```

`vmsize = 0` is the honest size for a kernel with no kexts, and it is also what makes the call
correct: `segPRELINKTEXTB + segSizePRELINKTEXT` becomes a real address, so the call protects exactly
the round-up slop of the image's last page instead of the whole address space below the kernel. Both
the address and the size come from the linker script, so nothing is hard-coded, and `getlastaddr()`
is a maximum over segments — `vmaddr + vmsize` for this one is the same address it already was, so
`end_kern` does not move.

**And `tools/host_entry_macho_check.sh` now checks the *range*, not the segment.** That is the part
worth keeping: the fact that the previous header was "internally consistent, every field matching the
linker's own symbols" is exactly why it passed for fifty experiments while being wrong. The new check
computes `end_kern - (segPRELINKTEXTB + segSizePRELINKTEXT)` from the decoded commands and fails
unless it is smaller than a page and non-negative:

```
  getlastaddr()                  -> 0x800da248  (end_kern = round_page of this)
  PreLinkInfoDictionary range    -> RWNX(0x800da248, 0x00000db8)  [0x800da248, 0x800db000)
```

The second half of that assertion matters as much as the first: a `__PRELINK_TEXT` ending past
`end_kern` makes the subtraction negative and the `unsigned long size` enormous, which is a worse
failure than the one being fixed. **It was tested by breaking it** — with the segment's `vmaddr`
temporarily set to `__entry_text_start`, the check exits 1 and prints two failures:

```
FAIL: __PRELINK_TEXT vmaddr: got 2147483648, expected 2148377160
FAIL: PreLinkInfoDictionary range is 0xdb000 bytes, not the slop of one page
```

`fleh_dataabt` also reports the three lives that call is computed from — `end_kern`,
`segPRELINKTEXTB`, `segSizePRELINKTEXT` — so that a run which *does* abort again still shows whether
the header change reached the code. The prediction is that they are never printed, and the absence is
the result.

## The result: the fault is gone, and the run reaches `kmem_init`

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x00000014
 xnu_entry_kv_in_dram=0x00000014
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=kmem_init

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25 contracts that report it, and the device returned to Android on its own. `kv_written ==
kv_in_dram == 0x14` = 20 bytes: no exception handler ran, and no abort key was written — the three
new ones included.

**There is no data abort at `0x80300000` any more**, and `stub_hit=kmem_init` is far past where the
fault was. The run has no exception at all: it ends at a function this image does not provide, which
is the *other* kind of stop, and the first one of those since experiment 211.

## Where `kmem_init` is: the whole boot path in front of it ran

`kmem_init` has exactly one caller in the image, and one chain reaches it:

```
  arm_init+0x340      80002e00: bl 80007054 <machine_startup>
  machine_startup+0xe8 8000713c: bl 8000da40 <kernel_bootstrap>
  kernel_bootstrap+0x120 8000db60: bl 800402fc <vm_mem_bootstrap>
  vm_mem_bootstrap+0x074 80040370: bl 8007ec0c <kmem_init>
```

and `vm_mem_bootstrap` is straight-line from its first instruction to that call:

```
80040304: bl <kernel_debug_string_early>      "..."     80040318: bl <vm_page_bootstrap>
80040324: bl <kernel_debug_string_early>                80040328: bl <zone_bootstrap>
80040334: bl <kernel_debug_string_early>                80040338: bl <vm_object_bootstrap>
80040354: bl <kernel_debug_string_early>                80040358: bl <vm_map_init>
80040364: bl <kernel_debug_string_early>                80040370: bl <kmem_init>
```

**There is no conditional branch anywhere in that range**, so reaching `kmem_init` is a statement
about every call before it: `vm_page_bootstrap`, `zone_bootstrap`, `vm_object_bootstrap` and
`vm_map_init` all returned. In particular the free that panicked four runs in a row — experiment
239's `vm_map_init+0x260`, `zcram(vm_map_zone, map_data, map_data_size)` with its element 7968 bytes
into the chunk `vm_map_steal_memory` had taken from `virtual_space_start` — passed, which is what
experiment 241's base move was for and what experiments 236 to 240 could not see past.

`zone_init` is still ahead: it is at `vm_mem_bootstrap+0x204`, after `kmem_init`, so
`zone_map_min_address` and `zone_map_max_address` are still zero and experiment 239's second finding
is unchanged — it just is not the thing in the way.

## The prediction, and the one thing it asked for that turned out to be small

Experiment 242 predicted that 243 "gets past `arm_vm_prot_init`". It did, and by a wide margin:
`arm_vm_prot_init`'s return, the EVB special case, `arm_vm_init`'s pre-initialization loop for
`virtual_space_start = 0xC0000000`, `patch_low_glo_static_region`, the rest of `arm_init`
(`printf_init`, `panic_init`, `PE_consistent_debug_inherit`, `PE_init_kprintf`), `machine_startup`,
`kernel_bootstrap` and the first five calls of `vm_mem_bootstrap`. The prediction's *shape* was
right — a stop that is not `pmap_init_pte_static_page` — and its *scale* was not something the two
registers could have said.

One thing the run did not show is worth recording because it looks like a miss. The chain above is
the *only* caller chain, and it was derived from the disassembly after the run. The device's line
says ``real arm_init reached a symbol this image does not provide`` — a label written when the only
interesting stub was inside `arm_init`, and one that cannot be more specific: `entry_stub_hit` is
handed a name and nothing else. Experiment 206's lesson is exactly this ("the device's
`stub_hit=<symbol>` names a symbol, never a caller"), and with the stop this far from `arm_init` the
label is now misleading. The fix is one argument — the generated stubs can pass
`__builtin_return_address(0)`, which is in `lr` for the instruction after the `bl` — and it belongs
in the next experiment rather than in a rewrite of this one.

## Cost

| | exp-242 | now |
| --- | --- | --- |
| `ncmds` / `sizeofcmds` | 2 / 180 | **3 / 236** |
| `__TEXT` vmsize | 0x906c0 | **0x90700** |
| `__DATA` vmaddr / vmsize | 0x800906c0 / 0x49b88 | **0x80090700 / 0x49b48** |
| `__entry_image_end` | 0x800da248 | **0x800da248**, unchanged |
| entry text | 593841 B | **594065 B** (+224) |
| `fleh_dataabt` | 368 B | **440 B** (+72, three keys) |
| entry image | 703352 B | **703352 B** |
| entry `.bss` | 0x800ab4f0–0x800da248 (191832 B) | **unchanged** |
| layout | args +901120, topOfKernelData +2097152, tree +4194304, window 8388608 | **unchanged** |
| headroom below `topOfKernelData` | 1203640 B | **1203640 B** |
| undefined | 645 | **645** |
| stubs | 559 functions, 86 storage | **unchanged** |

64 bytes of the 224 are the load command itself and 160 are the three keys and their names. The
header grew, so `__entry_text_end` moved up by 64 and with it `__DATA`'s front: the writable region
as the Mach-O describes it starts 64 bytes later and is 64 bytes shorter, while its *end* is where it
was — which is why `getlastaddr()` and `end_kern` are unmoved and the payload needed no rebuild
beyond reading the new `.bin`.

**No object was linked.** Nothing was flashed:
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own.

## What is next: the frontier is an object again

**The one-object-per-run method is back, and this time the object is the whole point.** Experiment
239 ended it because the address it was handed was rejected by a compile-time constant; here the
address is fine and what is missing is a function. `osfmk_vm_vm_kern.o` is already built (17.7 KB,
79 undefined symbols) and provides `kmem_init`, `kmem_alloc`, `kmem_free` and `kmem_suballoc`, so
experiment 244 links it and reads the new undefined count:

```bash
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_vm_vm_kern.o | grep -wE 'kmem_(init|alloc|free|suballoc)'
```

Two things to carry into it. `vm_mem_bootstrap`'s next statement after `kmem_init` is
`kext_alloc_init` at `+0x1a4` and then `zone_init` at `+0x204`, whose own first call is
`kmem_suballoc` — so the same object answers the next two stubs as well, and the run's next stop
should be *inside* `kmem_init` or `zone_init` rather than at a stub, unless an object those call is
missing. And the reporting fix above is cheap and belongs in the same run, because from here on every
stop is a stub somewhere far from `arm_init` and the label will keep lying about where.

## Reproduce

```bash
# the change: a third LC_SEGMENT, empty, at __entry_image_end
grep -n 'PRELINK_TEXT' -A 12 stages/stage90/xnu_arm_boot/entry_macho.s | head -20
grep -n 'PRELINK_TEXT' stages/stage90/xnu_arm_boot/entry.ld

(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'text size|image bytes|bss|layout' # -> 594065 B, 703352 B, layout unchanged

# ... and the header now says the call is bounded, which is the check that would have caught 242
./tools/host_entry_macho_check.sh | tail -8

# the negative test: break the segment and the check must refuse
sed -i 's|\.long __entry_image_end              /\* vmaddr \*/|.long __entry_text_start             /* vmaddr */|' \
    stages/stage90/xnu_arm_boot/entry_macho.s
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh >/dev/null)
./tools/host_entry_macho_check.sh ; echo "exit=$?"        # FAIL, exit 1
git checkout stages/stage90/xnu_arm_boot/entry_macho.s    # and rebuild

# ... and it ran
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12

# where the stop is, in the image the device ran
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf > /tmp/e243.dis
grep -n 'bl\t[0-9a-f]* <kmem_init>' /tmp/e243.dis
awk '/<vm_mem_bootstrap>:/{f=1} f{print} f&&/^$/{exit}' /tmp/e243.dis | head -30
grep -n 'bl\t[0-9a-f]* <vm_mem_bootstrap>\|bl\t[0-9a-f]* <kernel_bootstrap>\|bl\t[0-9a-f]* <machine_startup>' /tmp/e243.dis

# where the next object is
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_vm_vm_kern.o | grep -wE 'kmem_(init|alloc|free|suballoc)'
```
