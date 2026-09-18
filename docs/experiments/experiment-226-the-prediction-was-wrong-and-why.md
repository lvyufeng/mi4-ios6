# Experiment 226 — the Prediction Was Wrong, `vm_compressor_init_locks` for `zone_bootstrap`, and Why

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
 xnu_entry_kv_written=0x00000023
 xnu_entry_kv_in_dram=0x00000023
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=vm_compressor_init_locks

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` in every contract that reports
one, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x23 = 35 = strlen("vm_compressor_init_locks") + 11`.
No `exception:` line, and one `undef` breadcrumb, at line 3454, before the jump.

**The prediction was `zone_bootstrap` and it was wrong.** This is the first failed prediction in
twelve - the run before it was the twelfth consecutive one to hold - and the run is nonetheless a
clean measurement: a new symbol was named, the safety boundary held, and the device came back. What
follows is what the failure was, because a prediction that is wrong is the only thing in this method
that produces new information about the method.

## What actually ran, and the two levels the reading stopped at

The stop is one call *inside* `vm_page_bootstrap`, not one call after it:

```
 kernel_bootstrap
   vm_mem_bootstrap
     vm_page_bootstrap                 ; 0x00218b50, real since experiment 195
       vm_page_init_lck_grp            ; 0x002189b4, real, 220 bytes
         vm_compressor_init_locks      ; STUB at 0x00241330 - the stop
```

`vm_page_init_lck_grp` ends with a tail call, and that is the whole of it:

```
     104:	bl	lck_mtx_init_ext
     10c:	R_ARM_JUMP24	vm_compressor_init_locks
```

The reasoning that produced `zone_bootstrap` was: `vm_mem_bootstrap`'s first call is
`vm_page_bootstrap`; `vm_page_bootstrap` is already real; its calls are `__bzero`,
`vm_page_init_lck_grp` and `lck_mtx_init_ext` three times, and all of them are real; therefore the
run passes through it and stops at the next call in `vm_mem_bootstrap`, which is `zone_bootstrap`.
Two things are wrong with that, and they are different kinds of wrong.

**The first is arithmetic, and it is the reason the second was invisible.** The reading of
`vm_page_bootstrap` was taken from a disassembly of `0x00218b50` to `0x00218ec8` - 0x378 bytes -
because `arm-none-eabi-nm -S -P` reports the symbol's size as `888` and that was read as 888. It is
hexadecimal: the function is **0x888 = 2184 bytes**, and `vm_map_steal_memory`, four `__bzero` calls,
`PE_parse_boot_argn`, `pmap_free_pages`, two `pmap_steal_memory`, three `lck_spin_init`, `pmap_startup`
and `arm_usimple_lock_init` all live past the truncation point. The function has **27 direct calls,
not five**. A wrong number of the same symbol that was being read, in the same table, one column over
from the value that was right - the same defect class as the two `arm_vm_init` addresses that were
wrong in experiment 218 and the `.text` rounding theory that experiment 216 got from four good
numbers.

**The second is depth, and it survives the arithmetic being right.** Even with all 27 calls in hand,
"the callee is real" is not the same as "the callee has nothing missing", and the reading treated
them as the same. `vm_page_init_lck_grp` is real; what it *calls* is not. The prediction was made one
level down from the function it started in, and the stop was two levels down.

## `tools/xnu_entry_callwalk.py`, and how it is checked

The fix is not "read more carefully", because the failure mode is not carelessness - it is that the
thing being computed by hand is a transitive closure, and a transitive closure is not something to
compute by hand over a 1744-function image.

`tools/xnu_entry_callwalk.py` builds the call graph of the linked entry ELF and walks it in call
order from a given root, reporting the first stub it reaches. Two properties make it usable rather
than merely plausible:

- **Stubs are recognised structurally, not by name.** The generator emits
  `void f(void) { entry_stub_hit("f"); }`, so every stub is a 12-byte body that ends in a branch to
  `entry_stub_hit`. 439 of the image's 1744 functions are identified that way, with no list of names
  anywhere.
- **Calls inside a conditional block are not followed.** An initialisation path does not take its
  assertion branches: `lck_mtx_lock`'s `bl panic` is an argument check, and following it is how the
  first version of this tool reported `PEHaltRestart` for a boot that never panics. A call is skipped
  if it is unreachable from the function's entry without taking a branch, or if a forward conditional
  branch spans it. Both rules are needed - the contended path of a mutex is entered the first way,
  an assertion the second - and the 126 calls the walk steps over on this path are listed rather than
  hidden.

**It is checked against two answers the device produced, not against itself.** Run on this
experiment's image with `osfmk_vm_vm_init.o` linked, it names `vm_compressor_init_locks` - which is
what the device printed. Run on this experiment's *empty-object* image, it names `vm_mem_bootstrap` -
which is what experiment 225 printed. Two known answers, one either side of the step, and it gets
both.

What it cannot do is stated in its own header rather than left to be discovered: it does not follow
indirect calls, and it reports where it was blind when it finds no stub, so "no stub on the path" is
never read as "everything on the path is real".

## Cost

`out/xnu_kernel_obj/osfmk_vm_vm_init.o` (`osfmk/vm/vm_init.c`) - **994 bytes of text, no data, 24 of
`.bss`, 29 definitions, 24 references**:

```
resolved (2):  vm_mem_bootstrap   vm_mem_init      (function stubs)

added  (15):   device_pager_bootstrap  kalloc_init  kext_alloc_init  kmem_init
               log_executable_mem_entry  memory_manager_default_init
               memory_object_control_bootstrap  vm_allocate_kernel  vm_fault_init  vm_map_init
               vm_object_bootstrap  vm_object_init  vm_paging_map_init  zone_bootstrap  zone_init

506 -> 519 undefined
```

`506 + 15 - 2 = 519`, and the stub counters move `426 -> 438` functions and `80 -> 81` storage -
`426 - 2 + 14 = 438` and `80 + 1 = 81`. So 14 of the 15 are function stubs and one is storage:
`log_executable_mem_entry`, `B` with size 4. The object also defines `vm_kernel_ready`, `kmem_ready`,
`kmem_alloc_ready`, `kmapoff_pgcnt`, `kmapoff_kaddr`, `zlog_ready`, `vm_min_kernel_address` and
`vm_max_kernel_address`, which appear in neither list: they were absent from the empty-object image
entirely, so they are new *definitions* rather than resolved obligations.

| | exp-225 | now |
| --- | --- | --- |
| entry objects linked | 65 | 66 (`osfmk_vm_vm_init.o`) |
| entry text | 319332 B | **320708 B** |
| entry image | 421240 B | **421240 B** |
| entry `.bss` | 0x00266888–0x00280648 (105920 B) | 0x00266888–0x00280688 (**105984 B**) |
| undefined | 506 | **519** |
| stubs | 426 functions, 80 storage | **438 functions, 81 storage** |
| boot_args offset | +532480 | +532480 |
| headroom below `topOfKernelData` | 1571256 B | **1571192 B** |
| payload text | 913426 B | **913426 B** |

The entry image's file size does not move, and the payload's text does not move with it - the first
time that has happened since experiment 222. `.bss` grew by 64 and the text by 1376, both inside the
window the image had already reserved.

## What is next: `vm_compressor_init_locks`, and the prediction is `vm_map_steal_memory`

The frontier is `vm_compressor_init_locks`, defined by `out/xnu_kernel_obj/osfmk_vm_vm_compressor.o`
(`osfmk/vm/vm_compressor.c`) - **23260 bytes of text, 144 of data, 16248 of `.bss`, 206 definitions,
100 references**, the largest object this link has taken on. The function itself is 88 bytes and its
calls are `lck_grp_attr_setdefault`, `lck_grp_init`, `lck_attr_setdefault` and a tail `lck_rw_init`,
every one of them already real.

**The prediction is `stub_hit=vm_map_steal_memory`**, and this time it is the tool's answer rather
than a hand reading. What follows it, in `vm_page_bootstrap`'s straight-line order, is the part of
that function which had not been read before this experiment:

```
2190c0: bl vm_map_steal_memory        <-- STUB at 0x002468bc, the predicted stop
2190dc: bl pmap_free_pages            (inside a conditional block)
219188: bl _consume_printf_args       (inside a conditional block)
2191a0: bl pmap_steal_memory
2191c4: bl pmap_steal_memory
219230: bl lck_spin_init
219268: bl lck_spin_init
21927c: bl lck_spin_init
219308: bl pmap_startup
219324: bl panic                       (inside a conditional block)
2193cc: bl arm_usimple_lock_init
```

`vm_page_bootstrap`'s body past 0x2190c0 also contains the NEON that experiment 225 predicted would
run "next" and experiment 226 stopped before reaching - `vld1.64`, `vdup.32`, `vshl.s32` and the
looping `vst2.32 {d24-d27}, [r1 :64]!`. If this prediction holds, that NEON executes, and the log
should have no `exception:` line for the same reason the last one did not.

## Reproduce

```bash
# the step: 2 resolved, 15 added, 506 -> 519, stubs 426fn/80st -> 438fn/81st
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_OSFMK_VM_VM_INIT_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 2 resolved
comm -13 <(sort /tmp/A.txt) <(sort /tmp/B.txt)   # 15 added
wc -l /tmp/A.txt /tmp/B.txt                      # 506 and 519

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=vm_compressor_init_locks

# the defect: `nm -S -P` sizes are hex, and the truncated window was 0x378 of 0x888
arm-none-eabi-nm -S -P out/stage90/xnu_arm_entry.elf | grep -w vm_page_bootstrap   # T 218b50 888
awk 'BEGIN{printf "0x%s = %d bytes, not 888\n", "888", 0x888}'
arm-none-eabi-objdump -d --start-address=0x00218b50 --stop-address=0x002193d8 out/stage90/xnu_arm_entry.elf \
  | grep -cE '\bbl\b'                               # 27 calls, not 5
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_vm_vm_resident.o \
  | sed -n '/<vm_page_init_lck_grp>:/,/^$/p' | tail -3   # ... R_ARM_JUMP24 vm_compressor_init_locks

# the tool, checked against two answers the device produced
python3 tools/xnu_entry_callwalk.py --elf /tmp/A.elf      # before: vm_mem_bootstrap (exp-225 printed it)
python3 tools/xnu_entry_callwalk.py                       # after:  vm_compressor_init_locks (this run printed it)
python3 tools/xnu_entry_callwalk.py --list-stubs | tail -1
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
