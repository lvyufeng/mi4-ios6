# Experiment 248 — `vm_allocate_kernel` Runs, `pmap_init` Runs, and the Image Moves Again

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change: the largest object in the sequence so far

`osfmk_vm_vm_user.o` — **16196 bytes of text, 91 references** — the object `vm_allocate_kernel` is in.
Every step since 244 had a closure that fitted in a few dozen symbols; this one does not, and the
build says so: **resolved 9, added 6**.

```
resolved   log_executable_mem_entry  mach_destroy_memory_entry  mach_make_memory_entry_64
           mach_vm_deallocate  mach_vm_map_kernel  upl_offset_to_pagelist
           vm_allocate_kernel  vm_deallocate  vm_protect
added      ipc_kobject_set  ipc_port_alloc_special  ipc_port_copy_send  ipc_port_nsrequest
           ipc_space_kernel  vm_fault
```

(the second list is what the free-standing `mach_vm_*`/`upl_*` entry points pull in: a Mach IPC port
for the memory-entry path, and the page-fault entry.)

## The prediction

`vm_allocate_kernel` is small and its call list is one line long:

```
80082700 <vm_allocate_kernel>:
  +0x0d0  bl  vm_map_enter        <- the only call, and real
```

so the prediction was that **it completes**, and the question moved again to `vm_mem_bootstrap`. The
calls between the stop 247 named (`+0xf4`) and the next stub in that function are all real:

```
+0x110  bl PE_parse_boot_argn("log_executable_mem_entry", ...)
+0x134  bl pmap_init                        <- the ARM platform pmap
+0x158  bl PE_parse_boot_argn("zsize", ...)
+0x1a4  bl kext_alloc_init                  <- STUB
```

`pmap_init` was walked too, and has no stub on its straight-line closure. **The prediction:
`stub_hit=kext_alloc_init`, caller `vm_mem_bootstrap+0x1a8`** — one call past a platform pmap
initialisation, which is the most that could be claimed without running it.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000003c
 xnu_entry_kv_in_dram=0x0000003c
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=kext_alloc_init
 xnu_entry_stub_caller=0x800404a4

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25, and the device returned to Android on its own. `kv_written == kv_in_dram == 0x3c` (60 bytes
= 26 + 34), and `0x800404a4` resolves to `vm_mem_bootstrap+0x1a8`, whose `caller - 4` is
`800404a0: bl 800873e4 <kext_alloc_init>` — the call site, this call being a `bl`.

So the run measured, in order, by *completing* them:

- **`vm_allocate_kernel` returned KERN_SUCCESS** — its argument checks passed and its one call,
  `vm_map_enter`, entered the `kmapoff_pgcnt * 4096` bytes into `kernel_map`. That is a real kernel
  virtual allocation, through the map code experiment 247 created.
- **`PE_parse_boot_argn("log_executable_mem_entry", …)`** ran.
- **`pmap_init()` ran.** The ARM pmap's own initialization, after the two `pmap_bootstrap`-era
  experiments (195, 197) that measured the *bootstrap* half of the same file.
- **`kmem_alloc_ready = TRUE`** was stored — the flag experiment 234 found was the reason
  `zalloc`'s name-buffer path had not run yet.
- **`PE_parse_boot_argn("zsize", …)` returned FALSE** (no such boot arg), so `zsize = sane_size >> 2`
  and the clamps that follow it ran.

Then `kext_alloc_init` — the next symbol this image does not provide, and the last stub standing
between the run and `zone_init` (`vm_mem_bootstrap+0x204`), which is **real**.

## Cost, and the first address movement since 244

| | exp-247 | now |
| --- | --- | --- |
| undefined | 626 | **623** |
| function stubs | 541 | **539** |
| storage stubs | 85 | **84** (`log_executable_mem_entry`) |
| entry text | 617809 B | **634033 B** (+16224) |
| entry image | 719736 B | **736192 B** (+16456) |
| entry `.bss` | 0x800af4f0–0x800de208 | **0x800b34f0–0x800e2208** |
| `__entry_image_end` / `end_kern` | 0x800de208 / 0x800df000 | **0x800e2208 / 0x800e3000** |
| layout | args +917504, topOfKernelData +2097152, tree +4194304, window 8388608 | args **+933888**, rest unchanged |
| headroom below `topOfKernelData` | 1187320 B | **1170936 B** |
| payload text | 1211954 B | **1228410 B** |

The `+16224` of text is the object's 16196 plus 28 of net stub churn, and this time it did not fit:
**the image grew by one 16 KB block, `.bss` moved up by 0x4000, `__entry_image_end` and `end_kern`
with it, and the derived `boot_args` offset moved 917504 → 933888.** The three preceding steps (245,
246, 247) had all fitted inside the linker script's alignment padding; 16 KB is more than three times
what those three took between them, so this is the step where the "unchanged" rows end. Nothing about
it is a hazard — every number above is computed from the image by `build_entry.sh` and written into
`xnu_arm_entry.h`, and the payload was rebuilt from the regenerated header — but it is the reason the
layout block exists, and it is worth seeing it work.

## What is next

`kext_alloc_init` is in `out/xnu_kernel_obj/osfmk_kern_kext_alloc.o` — **344 bytes of text**, four
definitions (`kext_alloc_init`, `kext_alloc`, `kext_free`, `g_kext_map`) and four references
(`_consume_printf_args`, `kernel_map`, `mach_vm_allocate_kernel`, `mach_vm_deallocate`) of which the
last two are still stubs — including `mach_vm_allocate_kernel`, which is the entry point experiment
248's own object just linked, so this object's *first* call may well be one this image already has.

The step after it in `vm_mem_bootstrap` is `zone_init` (`+0x204`), which is **already real** (linked
since 239): its first call is `kmem_suballoc`, answered by experiment 244's object. So linking this
344-byte object is likely to open a run of several real functions rather than one, and the prediction
for 249 is a prediction about its own closure — which is short enough to read in full from the
disassembly before the run, the way 247's `kmem_init` was.

## Reproduce

```bash
# the change
grep -n 'OSFMK_VM_VM_USER_OBJ' stages/stage90/xnu_arm_boot/build_entry.sh

(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'undefined|stubs:'            # -> 623 undefined, 539 function(s), 84 storage
grep -E 'text size|image bytes|bss |layout|headroom'   # -> 634033 B, 736192 B, args +933888

# the prediction, both levels
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<vm_allocate_kernel>:/{f=1} f{print} f&&/^$/{exit}'
./tools/xnu_entry_callwalk.py --root vm_allocate_kernel
./tools/xnu_entry_callwalk.py --root pmap_init

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x800404a4   # -> vm_mem_bootstrap+0x1a8

# what ran before the stop, from the source
sed -n '146,232p' external/xnu-4570.1.46/osfmk/vm/vm_init.c

# where the next object is
arm-none-eabi-nm --defined-only --extern-only out/xnu_kernel_obj/osfmk_kern_kext_alloc.o
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_kern_kext_alloc.o
```
