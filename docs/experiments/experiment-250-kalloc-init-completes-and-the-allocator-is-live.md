# Experiment 250 — `kalloc_init` Completes, and the Allocator Is Live

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

`osfmk_kern_kalloc.o` — **4472 bytes of text, 33 references, 35 global definitions** (12992-byte file):
`kalloc_init`, `kalloc_canblock`, `kfree`, `kfree_addr`, `kalloc_size`, `kalloc_bucket_size`,
`kalloc_external`, the `OSMalloc_*` family, and the storage those functions keep their state in.

Resolved 6, added 0 — and the six are exactly the ones something in the image had already referenced:

```
resolved (functions)  kalloc_canblock  kalloc_init  kfree
resolved (storage)    kalloc_large_total  kalloc_map  kfree_nop_count
added                 (none)
```

Measured by building the step twice, once with the object and once with an empty stand-in in its
place (the stand-in build reproduces exp-249's 622 / 538 / 84 exactly, which is how the two sets were
separated rather than guessed). Only 6 of the object's 35 definitions had been stubs: the rest were
never referenced by anything in the image, so they were never in the undefined list — the difference
between "what an object defines" and "what a link needs" being six symbols.

## The prediction: read off the disassembly, and it was the whole function

`kalloc_init` is not small any more — 210 bytes, and its calls are all real:

```
8008652c  bl kmem_suballoc          ; 244's object
80086540  bl panic                  ; the failure path, not taken
800865d0  bl zinit                  ; loop, up to 30 iterations
800865e0  bl zone_change            ; twice per iteration
800865f0  bl zone_change
80086678  bl lck_grp_init
8008668c  bl lck_mtx_init
800866ac  bl lck_grp_alloc_init
800866cc  bl lck_mtx_init
```

`./tools/xnu_entry_callwalk.py --root kalloc_init` agreed: **no stub on the straight-line path**, with
the only conditional escapes being ones behind guards inside `kmem_suballoc`, `vm_map_enter` and
`panic`'s own debugger path. So the prediction was that `kalloc_init` **completes**, and the stop moves
to the next stub in `vm_mem_bootstrap`, four calls further on:

```
+0x234  bl vm_fault_init                 <- STUB
+0x244  bl memory_manager_default_init   <- STUB
+0x254  bl memory_object_control_bootstrap <- STUB
+0x264  bl device_pager_bootstrap        <- STUB
+0x274  bl vm_paging_map_init            <- real
```

**The prediction: `stub_hit=vm_fault_init`, caller `vm_mem_bootstrap+0x238`** (the return address of
the `bl` at `+0x234`).

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x0000003a
 xnu_entry_kv_in_dram=0x0000003a
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vm_fault_init
 xnu_entry_stub_caller=0x80040534

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25, and the device returned to Android on its own. `kv_written == kv_in_dram == 0x3a` (58 bytes
= 24 + 34), and `0x80040534` resolves to `vm_mem_bootstrap+0x238`, whose `caller - 4` is
`80040530: bl 8008a3f4 <vm_fault_init>`.

**`kalloc_init` completed, and it is the first thing this sequence has run that a later kernel will
call in anger.** Everything below is read from the address the run stopped at — the stop is *after*
the function's `pop {…, pc}`, and the only exits before that are the `panic` at `+0x78`, whose
`fleh_undef`/`panic` path would have shown as an exception and did not:

- **The kalloc map exists.** `kmem_suballoc(kernel_map, &min, kalloc_map_size, FALSE, VM_FLAGS_ANYWHERE,
  …, VM_KERN_MEMORY_KALLOC, &kalloc_map)` returned `KERN_SUCCESS` — its `retval != KERN_SUCCESS` panic
  at `+0x78` was not taken — so a submap of `kernel_map` was created and `kalloc_map`, `kalloc_map_min`
  and `kalloc_map_max` were stored. The size is `sane_size >> 5` clamped into
  `[KALLOC_MAP_SIZE_MIN, KALLOC_MAP_SIZE_MAX]`; the compiled clamp is visible as
  `cmp r4, #0x8000000 / movcs` then `cmp r4, #0x1000000 / movls`, and the argument registers that
  reach `kmem_suballoc` are the clamped ones, so the value used is on the record even though the
  argument itself is not logged.
- **The size thresholds are set.** `kalloc_max = 16384` (`movw r0, #0x4000` → `kalloc_max`),
  `kalloc_max_prerounded = 8193`, `kalloc_kernmap_size = kalloc_largest_allocated = 0x40001`
  (`16384 * 16 + 1`) — three stores of constants, all of which the run passed.
- **Every kalloc zone was created.** The `zinit` loop is one call per size class below `kalloc_max`;
  the ARM size table (`kalloc.c:192-205`, `KALLOC_MINSIZE == 8 && KALLOC_LOG2_MINALIGN == 3`) is
  `8, 16, 24, 32, 40, 48, 64, 72, 88, 112, 128, 192, 256, 288, 384, 440, 512, 576, 768, 1024, 1152,
  1536, 2048, 2128, 3072, 4096, 6144, 8192, 16384, 32768`, so the condition `size < kalloc_max` stops
  it at **28 zones**, sizes 8 through 8192. The compiler's own constants confirm the wiring: the first
  `zinit` gets size 8 as an immediate (the folded `k_zone_size[0]`), each later iteration reloads size
  from `k_zone_size` at the loop bottom, and the name comes from `k_zone_name` (`0x80098e4c`).
  Two `zone_change` calls per zone — `Z_CALLERACCT` (5) and `Z_KASAN_QUARANTINE` (10), both `FALSE`;
  the `Z_TAGS_ENABLED` call is inside `if (zone_tagging_on)` and is not in the compiled loop. So 28
  `zinit` and 56 `zone_change` calls, each of which allocates or mutates a real zone — on top of the
  zone subsystem 249's `zone_init` had just created.
- **The direct lookup table is built.** 256 `strb`-per-entry stores into `k_zone_dlut` from the
  `k_zone_size` table, ending with `k_zindex_start = zindex`.
- **The allocator's locks are initialised**, including one function that is in the same file and was
  **inlined rather than called**: after `lck_grp_init(&kalloc_lck_grp, "kalloc.large", NULL)` and
  `lck_mtx_init(&kalloc_lock, &kalloc_lck_grp, NULL)` the code self-links a queue head
  (`str r0, [r0]` / `str r0, [r0, #4]`), calls `lck_grp_alloc_init`, stores the result into the
  symbol `OSMalloc_tag_lck_grp` and initialises the lock beside it. That is `OSMalloc_init()`'s body
  (`kalloc.c`) executed inside `kalloc_init` — `OSMalloc_init` exists as its own 0x48-byte function
  too, but this call site did not use it.

`vm_paging_map_init` was already real and is not reached; the run stops one call before it.

## Cost, and the second consecutive move

| | exp-249 | now |
| --- | --- | --- |
| undefined | 622 | **616** |
| function stubs | 538 | **535** |
| storage stubs | 84 | **81** |
| entry text | 634321 B | **639953 B** (+5632) |
| entry image | 736192 B | **752648 B** (+16456) |
| entry `.bss` | 0x800b34f0–0x800e2248 | **0x800b74f0–0x800e64c8** |
| `__entry_image_end` / `end_kern` | 0x800e2208 / 0x800e3000 | **0x800e64c8 / 0x800e7000** |
| layout | args +933888 | **args +950272**, rest unchanged |
| headroom below `topOfKernelData` | 1170872 B | **1153848 B** |
| payload text | 1228410 B | **1244866 B** |

The object's 4472 bytes plus stub and name churn is +5632, and it did not fit: the image grew by
another 16 KB block, `.bss` moved up by 0x4000, `__entry_image_end` and `end_kern` with it, and the
derived `boot_args` offset moved 933888 → 950272. Two steps in a row now (248 and 250) have moved the
image; every number is recomputed from the link by `build_entry.sh` and written into
`xnu_arm_entry.h`, and the payload was rebuilt from the regenerated header.

## What is next

`vm_fault_init` is in `out/xnu_kernel_obj/osfmk_vm_vm_fault.o` — **30392 bytes of text and 156
references**, the largest object in this sequence by a factor of two over 248's `vm_user.o`. It brings
`vm_fault`, `vm_fault_page`, `vm_fault_enter`, `vm_fault_wire`/`_unwire`, `vm_fault_copy`,
`vm_pre_fault`, the code-signing validation path (`vm_page_validate_cs`, `vm_cs_*`) and
`kdp_lightweight_fault`. It cannot be read in full from the disassembly the way 244–250 were; the
prediction for 251 is a prediction about `vm_fault_init`'s own closure, and `vm_fault_init` is a
bootstrap function rather than the fault path, so the step may resolve far more than it adds.

The candidates after it in `vm_mem_bootstrap` are `memory_manager_default_init` (`+0x244`),
`memory_object_control_bootstrap` (`+0x254`) and `device_pager_bootstrap` (`+0x264`), all still stubs.

## Reproduce

```bash
grep -n 'OSFMK_KERN_KALLOC_OBJ' stages/stage90/xnu_arm_boot/build_entry.sh
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'undefined|stubs:|text size|image bytes|bss |layout|headroom'

# the resolved/added split, measured against an empty stand-in for the object
printf 'int x;\n' > /tmp/empty.c
arm-none-eabi-gcc -c -mcpu=cortex-a15 -marm -ffreestanding -O2 -o /tmp/empty_kalloc.o /tmp/empty.c
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_OSFMK_KERN_KALLOC_OBJ=/tmp/empty_kalloc.o ./build_entry.sh)  # -> 622 / 538 / 84
comm -23 <(sort out/stage90/xnu_arm_entry_undef.txt) <(sort /tmp/undef_new.txt)

# the prediction
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<kalloc_init>:/{f=1} f{print} f&&/^$/{exit}'
./tools/xnu_entry_callwalk.py --root kalloc_init
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<vm_mem_bootstrap>:/{f=1} f{print} f&&/^$/{exit}' \
  | grep -E 'bl\s+8008'

# the zone table that decides how many zinit calls run
sed -n '185,205p' external/xnu-4570.1.46/osfmk/kern/kalloc.c
sed -n '357,368p' external/xnu-4570.1.46/osfmk/kern/kalloc.c   # the loop itself

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x80040534   # -> vm_mem_bootstrap+0x238
```
