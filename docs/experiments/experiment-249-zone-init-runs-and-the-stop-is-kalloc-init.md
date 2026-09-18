# Experiment 249 — `zone_init` Runs, and the Stop Is `kalloc_init`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change, and what the object turned out to be

`osfmk_kern_kext_alloc.o` — **344 bytes of text**, four definitions (`kext_alloc_init`,
`kext_alloc`, `kext_free`, `g_kext_map`) and four references (`_consume_printf_args`, `kernel_map`,
`mach_vm_allocate_kernel`, `mach_vm_deallocate`). Build: **resolved 1, added 0** — `kext_alloc_init`
and nothing else, since the second of its four references is answered by 248's own object.

Reading the built function is the interesting part, because it is not what the source looks like:

```
80086370 <kext_alloc_init>:
80086370:	movw	r0, #2552	; 0x9f8      <- two .bss flags, set to 1
...
8008639c:	movt	r0, #32782	; 0x800e
800863a0:	str	r2, [r0]                  <- and one word copied
800863a4:	bx	lr
```

**Fifteen instructions and not one call.** `osfmk/kern/kext_alloc.c:64-130` is a long function that
snags 2 GB of kernel VA for kext text, computes `kext_alloc_base`/`kext_alloc_max` and panics if
`kmem_suballoc` fails — but all of it is inside `#if CONFIG_KEXT_BASEMENT`, which is x86-only, and the
ARM build is left with the flag stores at the end of the function. So the object `vm_mem_bootstrap`
calls `kext_alloc_init` for is, on this platform, three stores: the paragraph that would have
described a 2 GB reservation describes nothing, and the measurement is what says so.

## The prediction: two functions in a row, not one

With `kext_alloc_init` having no calls at all, the stop had to move again — and the next three things
`vm_mem_bootstrap` does are all real:

```
+0x204  bl zone_init               <- real, osfmk_kern_zalloc.o
+0x214  bl vm_page_module_init     <- real
+0x224  bl kalloc_init             <- STUB
```

`zone_init` was walked too, and has no stub on its straight-line closure. **The prediction:
`stub_hit=kalloc_init`, caller `vm_mem_bootstrap+0x228`** — which means this run would be the first
since 243 to pass *two* whole functions in a single step.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000038
 xnu_entry_kv_in_dram=0x00000038
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=kalloc_init
 xnu_entry_stub_caller=0x80040524

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25, and the device returned to Android on its own. `kv_written == kv_in_dram == 0x38` (56 bytes
= 22 + 34), and `0x80040524` resolves to `vm_mem_bootstrap+0x228`, whose `caller - 4` is
`80040520: bl 800873d4 <kalloc_init>`.

**`zone_init` ran.** That is the largest thing this sequence has executed so far: it is the function
that creates the zone map and every zone in it, and it is what experiment 239 was measuring when it
read `zone_map_min_address` and `zone_map_max_address` as zero — the two 4-byte globals at
`0x800c36a8` and `0x800c36ac` are the only things `zone_init` writes them with
(`zalloc.c:2958-2959`), and the reasoning then was explicitly "`zone_init` has never executed in this
image". It executed here. Its first call, `kmem_suballoc`, is answered by 244's object; the zone map
came from the pmap and the map code that 247's and 248's steps had just created and exercised.

`vm_page_module_init` ran as well (`0x18c` bytes, real), and the run stopped one call later, at
`kalloc_init` (`0x18` bytes as a stub body).

## Cost

| | exp-248 | now |
| --- | --- | --- |
| undefined | 623 | **622** |
| function stubs | 539 | **538** |
| storage stubs | 84 | **84** |
| entry text | 634033 B | **634321 B** (+288) |
| entry image | 736192 B | **736192 B**, unchanged |
| entry `.bss` end | 0x800e2208 | **0x800e2248** (+0x40) |
| layout / `__entry_image_end` / `end_kern` | args +933888, 0x800e2208, 0x800e3000 | **unchanged** |
| headroom below `topOfKernelData` | 1170936 B | **1170872 B** |
| payload text | 1228410 B | **1228410 B**, unchanged |

+288 bytes is the object's 344 less an 88-byte stub and the alignment; the image did not move, so
`__entry_image_end`, `end_kern` and the derived layout are all where 248 left them, and the payload
was rebuilt only to carry a new copy of a same-sized `.bin`.

## What is next

`kalloc_init` is in `osfmk_kern_kalloc.o` — 4472 bytes of text, 33 references, defines `kalloc_init`
and the whole `kalloc`/`kfree`/`kalloc_canblock` family. It is six times this step's object, and its
references include several that are still stubs, so the next stop is more likely to be one of those
than to be another whole function. The candidates after it in `vm_mem_bootstrap` are
`vm_fault_init` (`+0x234`), `memory_manager_default_init` (`+0x244`),
`memory_object_control_bootstrap` (`+0x254`) and `device_pager_bootstrap` (`+0x264`), all still stubs.

## Reproduce

```bash
grep -n 'OSFMK_KERN_KEXT_ALLOC_OBJ' stages/stage90/xnu_arm_boot/build_entry.sh
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'undefined|stubs:|text size|image bytes|bss |layout|headroom'

# the fifteen instructions, and why they are not the source's function
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<kext_alloc_init>:/{f=1} f{print} f&&/^$/{exit}'
sed -n '60,75p' external/xnu-4570.1.46/osfmk/kern/kext_alloc.c   # #if CONFIG_KEXT_BASEMENT

# the prediction
./tools/xnu_entry_callwalk.py --root kext_alloc_init
./tools/xnu_entry_callwalk.py --root zone_init

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x80040524   # -> vm_mem_bootstrap+0x228

# the two globals zone_init is the only writer of (experiment 239's finding, now answered)
arm-none-eabi-nm -S -P --defined-only out/stage90/xnu_arm_entry.elf | grep zone_map_m
grep -n 'zone_map_min_address' external/xnu-4570.1.46/osfmk/kern/zalloc.c
```
