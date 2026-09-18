# Experiment 229 — the Cheapest Object in the Sequence, Ten Stubs for None, and the Frontier Moved Two Levels In

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: real arm_init reached a symbol this image does not provide
 xnu_entry_kv_written=0x00000024
 xnu_entry_kv_in_dram=0x00000024
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=memorystatus_pages_update

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` in every contract that reports
one, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x24 = 36 = strlen("memorystatus_pages_update") + 11`.
No `exception:` line, and one `undef` breadcrumb, at line 3454, before the jump.

**The prediction was `zone_bootstrap` and it was wrong** - the third consecutive miss, and the third
different reason. It is also the first one whose cause is visible in the write-up of the step before
it: the walk answered `zone_bootstrap` because `vm_page_bootstrap`'s subtree contained no stub it
could reach, and it could not reach the one that mattered because it is behind a branch.

## Where the run went

```
 kernel_bootstrap
   vm_mem_bootstrap
     vm_page_bootstrap
       ...
       pmap_startup                   real, 0x408 bytes, entered
         ...                          the page-release loop
         memorystatus_pages_update    STUB at pmap_startup+0x3b4 - the stop
         panic                        never reached
```

Two things about this stop are new.

**The first is that `pmap_startup` is not in `pmap.c`.** It is defined in
`osfmk/vm/vm_resident.c` at line 1123, which is why `grep memorystatus_pages_update
osfmk/arm/pmap.c` finds nothing and why the symbol's owner looks like the wrong file. The call is

```c
	for (i = pages_initialized; i > 0; i--) {
		if (fill) fillPage(...);
		vm_page_release_startup(&vm_pages[i - 1]);
	}

	VM_CHECK_MEMORYSTATUS;
```

and `VM_CHECK_MEMORYSTATUS` is the `osfmk/vm/vm_page.h` macro that expands to
`memorystatus_pages_update(vm_page_pageable_external_count + vm_page_free_count + ...)` under
`CONFIG_JETSAM`. It is straight-line source, unconditionally executed after the loop - and it is
behind the loop's exit branch, which is why the tool called it guarded.

**The second is that this is the same defect class as experiment 226, one level in.** Experiment 226
read the caller one level deep and the path was two; this one asked the walk where it goes and the
walk could not leave `vm_page_bootstrap`'s entry region, so the answer came from
`vm_mem_bootstrap`'s next call instead. Three experiments, three misses, three mechanisms:

| | prediction | device | why the prediction failed |
| --- | --- | --- | --- |
| 226 | `zone_bootstrap` | `vm_compressor_init_locks` | read one level deep; path two |
| 228 | `zone_bootstrap` | `OSCompareAndSwap16` | the guard rule could not tell a loop from an assertion |
| 229 | `zone_bootstrap` | `memorystatus_pages_update` | the walk cannot leave a 4 KB `-O2` function's entry block |

The last row is not a bug and is not going to be fixed by another rule. It is the tool's model
running out, and the practice that follows from it is in the tool's own header: **root the walk at
the symbol the last run named**, because that symbol is where the run stopped and the function behind
it is one level *down*, not one level up. That is experiment 230's prediction.

## Cost

`out/xnu_kernel_obj/libkern_gen_OSAtomicOperations.o` (`libkern/gen/OSAtomicOperations.c`) -
**1104 bytes of text, no data, no `.bss`, 27 definitions, and zero undefined references**. The first
object in the sequence with nothing undefined at all.

```
resolved (10): OSAddAtomic    OSAddAtomic16     OSAddAtomic64    OSBitAndAtomic16
               OSBitOrAtomic16  OSCompareAndSwap  OSCompareAndSwap16
               OSCompareAndSwap64  OSCompareAndSwapPtr  OSIncrementAtomic

added  (0):    -
```

**The prediction written into `build_entry.sh` before the build said "one stub resolved and
twenty-six added", and the measurement is 10 and 0.** The image had heard of ten of the object's 27
symbols, and because the object has no undefined reference of its own, the other seventeen can add
nothing: they arrive as new *definitions* the linker has no obligation to make, the `vm_kernel_ready`
case from experiment 226 again. All ten resolved were 12-byte function stubs - `nm` reports each as
`T ... c` in the image before and a real size after - so the image had been linking stubs for ten
names XNU 4570 references in full. Only `OSCompareAndSwap16` is on the path the run took; the other
nine came along because the image's undefined set is a set, not a path.

`.text` grows 800 bytes and `.bss` does not move at all, and the entry image's file size is identical
before and after - 519744 in both directions. This is the first step in the sequence with no new
storage stub.

| | exp-228 | now |
| --- | --- | --- |
| entry objects linked | 68 | 69 (`libkern_gen_OSAtomicOperations.o`) |
| entry text | 423012 B | **423812 B** |
| entry image | 519744 B | **519744 B** |
| entry `.bss` | 0x0027e8c0–0x0029cc48 (123784 B) | unchanged |
| undefined | 606 | **596** |
| stubs | 510 functions, 96 storage | **500 functions, 96 storage** |
| boot_args offset | +647168 | **+647168** |
| headroom below `topOfKernelData` | 1455032 B | **1455032 B** |
| payload text | 1011930 B | **1011930 B** |

## The three-term decomposition, fifth test

```
symbol region    387924 -> 388908            +984
.text            423012 -> 423812            +800
                                    gaps + tail   -184
```

The tail term is **negative**, which is a first, and it is checkable: the generator emits
`void f(void) { entry_stub_hit("f"); }` per stub, and the aligned tail of `.text` holds the name
strings. Extracting the `.text` section of each image as a raw blob and diffing its printable strings
gives exactly the ten names that were resolved and nothing else:

```
$ for f in A B; do arm-none-eabi-objcopy -O binary --only-section=.text /tmp/$f.elf /tmp/$f.text.bin; done
$ comm -23 <(strings -n 4 /tmp/A.text.bin | sort -u) <(strings -n 4 /tmp/B.text.bin | sort -u)
OSAddAtomic  OSAddAtomic16  OSAddAtomic64  OSBitAndAtomic16  OSBitOrAtomic16
OSCompareAndSwap  OSCompareAndSwap16  OSCompareAndSwap64  OSCompareAndSwapPtr  OSIncrementAtomic

$ comm -23 ... | awk '{n += length($0) + 1} END {print n}'      # 166
```

166 bytes of names plus 18 of alignment is the whole of the −184, and the other term is the object's
own text: 1104 bytes added, 10 x 12 = 120 bytes of stub bodies removed, 984 net - which is the
symbol-region figure exactly. This is the first decomposition in the sequence whose residual is
identified to the byte in both directions.

## What is next: `memorystatus_pages_update`, and the prediction is `zone_bootstrap`

The frontier is `memorystatus_pages_update`, defined by `bsd/kern/kern_memorystatus.c` →
`out/xnu_kernel_obj/bsd_kern_kern_memorystatus.o` - **31388 bytes of text plus a 1612-byte `initcode`
section, 620 of data, 136 of `.rodata`, 712 of `.bss`, 1254 of `.rodata.str1.1`, 209 definitions,
110 references**. The largest object since `vm_map.o`, and the first with a section this build has
not seen (`initcode`).

**The prediction is `stub_hit=zone_bootstrap`**, and the argument is not a walk this time. The run is
inside `pmap_startup`; `memorystatus_pages_update` is the only stub left in it and it is the
function's last call before the trailing `panic`; so `pmap_startup` completes, `vm_page_bootstrap`
completes after `arm_usimple_lock_init` (already real), and `vm_mem_bootstrap` calls
`kernel_debug_string_early` (real) and then, at `+0x2c`, `zone_bootstrap`, which is a stub. The
call list is `vm_mem_bootstrap`'s own, read directly:

```
+0x010 kernel_debug_string_early   real
+0x01c vm_page_bootstrap           real
+0x028 kernel_debug_string_early   real
+0x02c zone_bootstrap              STUB   <- the prediction
+0x03c vm_object_bootstrap         STUB
```

`zone_bootstrap` is defined by `osfmk/kern/zalloc.c`, so the step after this one is
`osfmk_kern_zalloc.o`.

## Reproduce

```bash
# the step: 10 resolved, 0 added, 606 -> 596, stubs 510fn/96st -> 500fn/96st
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_LIBKERN_GEN_OSATOMICOPERATIONS_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(LC_ALL=C sort -u /tmp/A.txt) <(LC_ALL=C sort -u /tmp/B.txt)   # 10 resolved
comm -13 <(LC_ALL=C sort -u /tmp/A.txt) <(LC_ALL=C sort -u /tmp/B.txt)   # nothing
wc -l /tmp/A.txt /tmp/B.txt                                             # 606 and 596

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro_last_kmsg.txt | head -14  # ... stub_hit=memorystatus_pages_update

# the tail term, identified to the byte
for f in A B; do arm-none-eabi-objcopy -O binary --only-section=.text /tmp/$f.elf /tmp/$f.text.bin; done
comm -23 <(strings -n 4 /tmp/A.text.bin | LC_ALL=C sort -u) \
         <(strings -n 4 /tmp/B.text.bin | LC_ALL=C sort -u)          # the ten names
arm-none-eabi-nm -P out/xnu_kernel_obj/libkern_gen_OSAtomicOperations.o | awk '$2=="U"' | wc -l   # 0

# the object that defines the frontier, and the stop's real home
arm-none-eabi-nm -P out/xnu_kernel_obj/bsd_kern_kern_memorystatus.o | grep -w memorystatus_pages_update
awk 'NR<=1262 && /^[a-zA-Z_][a-zA-Z0-9_ \t\*]*\(/ {n=NR; l=$0} END{print n": "l}' \
  external/xnu-4570.1.46/osfmk/vm/vm_resident.c     # 1123: pmap_startup( - not in pmap.c
grep -n "VM_CHECK_MEMORYSTATUS" external/xnu-4570.1.46/osfmk/vm/vm_page.h
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
