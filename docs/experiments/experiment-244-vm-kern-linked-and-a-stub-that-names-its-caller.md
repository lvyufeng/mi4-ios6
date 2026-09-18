# Experiment 244 — `osfmk_vm_vm_kern.o`, and a Stub That Names Its Caller

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change: one object, and one argument

Experiment 243 ended with the run stopping at `stub_hit=kmem_init` — the first stop in this sequence
that was neither an exception nor a missing symbol *address*, and the first since 211 that the
one-object-per-run method can answer, because 243's frontier is a function. `osfmk_vm_vm_kern.o`
defines it, and three of the four stops ahead of it:

```
kmem_init          0x1ac bytes at +0x1e54    the symbol 243 named
kmem_alloc_kobject 0x24 bytes                vm_object_bootstrap's second statement (234's prediction)
kmem_suballoc      0x1bc bytes               zone_init's first call
kmem_alloc         0x60 bytes
```

The object is 9132 bytes of text, 674 of `.rodata.str1.1`, 8 of `.bss`, no `.data`, 26 global
definitions and 79 references. Nothing else in this project's object pool defines any of the 26, and
`entry_stubs.c` has no hand-written definition of any `kmem_*`, `kernel_memory_*`, `kernel_map` or
`vm_kernel_addr*` name, so no collision is possible — the build would have said so.

The second change is the reporting, and it is not a nicety: **from here every stop is a stub
somewhere far from `arm_init`**, and the name alone stopped being enough. `entry_stub_hit` gains the
`lr` the stub was entered with, written as `xnu_entry_stub_caller`, and the generated stubs read it
with `__builtin_return_address(0)`:

```c
void kmem_init(void) { entry_stub_hit("kmem_init", (uint32_t)(uintptr_t)__builtin_return_address(0)); }
```

A generated stub is a one-liner, so that is a read of `lr` before the function has done anything —
and that was checked rather than assumed, by compiling both shapes with this build's own flags and
reading the disassembly:

```
00000000 <one_liner>:
   4:	push	{lr}
   8:	mov	r1, lr          <- the argument, from the register `bl` wrote
  14:	b	<entry_stub_hit>

00000018 <after_calls>:          <- a function that makes calls first, like pmap_bootstrap's probe
  18:	strd	r4, [sp, #-16]!
  30:	mov	r5, lr          <- spilled in the prologue, used at the call
  70:	mov	r1, r5
  88:	b	<entry_stub_hit>
```

The value is the return address of a `bl`, so the call site is `caller - 4`. `tools/host_resolve_entry_addr.sh`
(new, and the reason the key is worth having) turns it back into `function+0xNN` against the image
the device ran, printing both readings:

```
$ ./tools/host_resolve_entry_addr.sh 0x8004651c
0x8004651c  vm_map_create+0x5c
          caller-4 = 0x80046518  vm_map_create+0x58   <- the `bl`, if the call was one
```

Tested against 243's hand-derived chain before the run, and it reproduces it exactly: `0x80040374`
is `vm_mem_bootstrap+0x78` whose `caller-4` is `vm_mem_bootstrap+0x74` — the `bl kmem_init` that
243's write-up quotes.

The stop line's own text changes with it, from `real arm_init reached a symbol this image does not
provide` to `a symbol this image does not provide was called`. That sentence was written when the
only interesting stub was inside `arm_init`, and by 243 it was wrong about where; with the call site
reported separately it becomes what it can be exact about.

## The build: 12 resolved, 3 added, and what the reporting alone cost

```
  636 symbol(s) undefined
  stubs: 551 function(s), 85 storage
```

| | exp-243 | now |
| --- | --- | --- |
| undefined | 645 | **636** |
| function stubs | 559 | **551** |
| storage stubs | 86 | **85** |
| entry text | 594065 B | **610225 B** |
| entry image | 703352 B | **719736 B** |
| entry `.bss` | 0x800ab4f0–0x800da248 | **0x800af4f0–0x800de208** |
| `__entry_image_end` / `end_kern` | 0x800da248 / 0x800db000 | **0x800de208 / 0x800df000** |
| layout | args +901120, topOfKernelData +2097152, tree +4194304, window 8388608 | args **+917504**, rest unchanged |
| headroom below `topOfKernelData` | 1203640 B | **1187320 B** |
| payload text | 1195570 B | **1211954 B** (+16384, exactly the entry image's growth) |

Resolved 12, added 3, which is the net −9:

```
resolved   copyinmap kernel_map kernel_memory_allocate kernel_memory_depopulate
           kernel_memory_populate kmem_alloc kmem_alloc_flags kmem_alloc_kobject
           kmem_alloc_pageable kmem_free kmem_init kmem_suballoc      (12: 11 functions, kernel_map storage)
added      SHA256_Init SHA256_Update SHA256_Final                    (3, from kernel_memory_populate)
```

**The reporting change was measured on its own**, by building with the new object replaced by an
empty one: 645 undefined, 559 function and 86 storage stubs — the exp-243 stub set exactly — and
**text 600785 against 243's 594065**. So the caller argument costs **6720 bytes**, of which 6708 is
the stubs themselves: `movw/movt` + `b` is 12 bytes, and the version that has to carry `lr` across a
tail call is 24, because gcc pushes and pops `lr` so the callee still returns to the original caller.
That is 12 bytes × 559 stubs, and it is the price of being able to name a call site at all.

## The prediction, from the disassembly rather than from the source

`tools/xnu_entry_callwalk.py --root kmem_init` walks the transitive closure of `kmem_init`'s calls
and reports the first stub on the straight-line path — and the answer was read off the linked image
by hand afterwards, which is the rule this project has had to learn twice (the source's program order
is where a reading starts; the disassembly is where the claim is made):

```
kmem_init+0x2c  800801bc: bl 800464c0 <vm_map_create>
  vm_map_create+0x20  800464e0: bl 8006f284 <zalloc>              <- defined, real
  vm_map_create+0x58  80046518: bl 80083604 <vm_map_store_init>   <- a 24-byte stub
```

**The prediction: `stub_hit=vm_map_store_init`, caller `vm_map_create+0x5c`.** It is the
`#if defined(__arm__)` path of `kmem_init` — `kernel_map = vm_map_create(pmap_kernel(),
VM_MIN_KERNEL_AND_KEXT_ADDRESS, VM_MAX_KERNEL_ADDRESS, FALSE)` — and it needs `vm_map_create`'s own
body, not `kmem_init`'s. The walker also listed what happens if one of `zalloc`'s conditional
branches goes the other way (`lck_mtx_lock_spin_always`, `thread_wakeup_prim`, `assert_wait_timeout`,
`thread_block`, `panic`, all behind guards in `zalloc_internal`); the device took the straight path.

## The result: the prediction, and a caller the device named itself

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000003e
 xnu_entry_kv_in_dram=0x0000003e
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vm_map_store_init
 xnu_entry_stub_caller=0x8004651c

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25 contracts that report it, and the device returned to Android on its own. No exception ran:
`kv_written == kv_in_dram == 0x3e` = 62 bytes, and the two lines are 28 + 34 exactly — ` stub_hit=vm_map_store_init\n`
and ` xnu_entry_stub_caller=0x8004651c\n`. The address resolves to `vm_map_create+0x5c`, whose
`caller - 4` is `80046518: bl 80083604 <vm_map_store_init>` — the instruction the prediction was made
from, the device's own reading of it.

**`kmem_init` ran for real, and so did the allocator underneath it.** The stop is inside
`vm_map_create`, and the instruction before it is `bl zalloc` followed by `cmp r0, #0` / `bne` and a
`panic` for the NULL case — the panic at `800464f8` was not taken, so `zalloc` returned non-NULL: a
real zone allocation out of `vm_map_zone` happened on this hardware. `zone_init` is still ahead of
the run (`vm_mem_bootstrap+0x204`, after `kmem_init`), so the two zone-map bounds experiment 239
reads as zero are unchanged — the path to them is shorter by one stub now, and `kmem_suballoc`, its
own first call, is answered by this same object.

## What is next

`vm_map_store_init` is defined by `out/xnu_kernel_obj/osfmk_vm_vm_map_store.o` (908 bytes of text, 12
definitions, 14 references) — already built, so the frontier method continues unchanged. Note what
that step's own next stop will be, because the object is a dispatcher rather than an implementation:
it calls `vm_map_store_init_ll` and `vm_map_store_init_rb`, which live in
`osfmk_vm_vm_map_store_ll.o` (776 bytes) and `osfmk_vm_vm_map_store_rb.o` (5808 bytes). Both are
already built too.

## Reproduce

```bash
# the change: the object, and the caller argument
grep -n 'OSFMK_VM_VM_KERN_OBJ' stages/stage90/xnu_arm_boot/build_entry.sh
grep -n 'builtin_return_address' stages/stage90/xnu_arm_boot/build_entry.sh

(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'undefined|stubs:' # -> 636 undefined, 551 function(s), 85 storage
grep -E 'text size|image bytes|bss |layout|headroom'   # -> 610225 B, 719736 B, args +917504

# what the reporting change cost, on its own: the same build with an empty stand-in for the object
arm-none-eabi-gcc -c /dev/null -x c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
    STAGE90_ENTRY_OSFMK_VM_VM_KERN_OBJ=/tmp/empty.o ./build_entry.sh | grep 'text size')
# -> 600785 B against 243's 594065, with 243's 645-symbol stub set

# the header, which the object moved but did not break
./tools/host_entry_macho_check.sh | tail -6
# -> __PRELINK_TEXT 0x800de208 size 0, RWNX(0x800de208, 0xdf8)

# the prediction, then the hand check of it in the image the device will run
./tools/xnu_entry_callwalk.py --root kmem_init
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<vm_map_create>:/{f=1} f{print} f&&/^$/{exit}' | grep -E 'bl.' 

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12

# the address the device reported, resolved against the image it ran
./tools/host_resolve_entry_addr.sh 0x8004651c
# -> vm_map_create+0x5c, caller-4 = vm_map_create+0x58, which is `bl vm_map_store_init`

# where the next object is
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_vm_vm_map_store*.o | grep vm_map_store_init
```
