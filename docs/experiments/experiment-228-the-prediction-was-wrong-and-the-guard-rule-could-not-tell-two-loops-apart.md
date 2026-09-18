# Experiment 228 — the Prediction Was Wrong, `OSCompareAndSwap16` for `zone_bootstrap`, and the Loop-Exit Branch That Was Not a Condition

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
 xnu_entry_kv_written=0x0000001d
 xnu_entry_kv_in_dram=0x0000001d
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=OSCompareAndSwap16

No errors detected
```

`stage90_xnu_entry_stub_status=0x90000001`, `failure_mask=0x00000000` in every contract that reports
one, `safety_boundary_preserved=0x00000001`, `mmu_unchanged=0x00000001`,
`persistent_write_attempted=0x00000000` in all 25 contracts that report it, and the device returned
to Android on its own. `kv_written == kv_in_dram == 0x1d = 29 = strlen("OSCompareAndSwap16") + 11`.
No `exception:` line, and one `undef` breadcrumb, at line 3454, before the jump.

**The prediction was `zone_bootstrap` and it was wrong** - the second failure in three experiments,
after experiment 227 held. The prediction came from `tools/xnu_entry_callwalk.py`, and this document
is mostly about what was wrong with the tool, because that turned out to be findable and is now
fixed.

## Where the run went

```
 kernel_bootstrap
   vm_mem_bootstrap
     vm_page_bootstrap                    the walk came through here
       vm_map_steal_memory                exp-227's stop, real since this step
       pmap_steal_memory                  unguarded, and 0x1c0 bytes long
         pmap_enter                       the walk said GUARDED; it is not
           pmap_enter_options             unguarded, and 0x1090 bytes long
             OSCompareAndSwap16           the stop, at pmap_enter_options+0x1054
       zone_bootstrap                     STUB - what the walk answered, and the run never got there
```

`OSCompareAndSwap16` sits in one of `pmap_enter_options`'s compare-and-swap retry loops:

```
  223fc0:	ldr	r2, [r8]
  223fc4:	ldrh	r0, [r2, r7]!
  223fc8:	orr	r1, r0, #1024	; 0x400
  223fcc:	bl	257a90 <OSCompareAndSwap16>     <-- +0x1054, the stop
  223fd0:	cmp	r0, #0
  223fd4:	beq	223fc0                          <-- retry, backwards
```

It is referenced 43 times in `osfmk_arm_pmap.o`, across twelve functions; the tool had every one of
those call sites in its guard list and none of them on its path.

## The tool was wrong, and the reason is a rule that could not tell two loops apart

The walk calls a call site *guarded* by two rules, and the second rule is the one that failed:

1. **not reachable from the function's entry without taking a branch.** Follow fall-through and
   local `b`, stop at `bx`, `pop {..pc}` and tail calls. The contended path of a mutex is entered
   this way.
2. **inside a forward conditional branch's span.** The rule exists for assertions: `lck_mtx_lock`'s
   argument check is

   ```
     213320:	cmp	r0, #34
     213324:	beq	213338          <-- spans the next four instructions
     213334:	bl	22d294 <panic>  <-- inside the span, and never executed
   ```

   and a `bl panic` walked into is how the first version of this tool came to answer `PEHaltRestart`
   for a boot that never panics.

**A loop's exit test is also a forward conditional branch that spans its own body**, and rule 2
therefore marked the whole loop body guarded. `pmap_steal_memory`'s page-allocation loop is the case
that mattered:

```
  21947c:	bcs	21952c          <-- the exit test, spanning 0x1c4 bytes of body
  2194a4:	b	2194cc          <-- an unambiguous branch TO the loop head
  2194cc:	mov	r0, r7
  2194d0:	bl	2215a0 <pmap_next_page_hi>   <-- inside the span, and always executed
```

Rule 2 saw `pmap_next_page_hi` inside a span and called it cold; `pmap_enter` at `+0x130`, later in
the same span, was called cold for the same reason. The rule's premise - "code inside a conditional
branch's span runs only when the branch is not taken" - is false for a loop body, because the loop
head is also reached by an explicit branch.

**The fix is to record *how* an address became reachable.** The traversal now carries a flag: an
address reached by an actual branch is not conditional, whatever span it sits in. With that, rule 2
applies only to addresses reachable by falling into a span. The two cases separate cleanly and both
come out right:

| | before | after |
| --- | --- | --- |
| `lck_mtx_lock`'s `bl panic` (inside a span, fallen into) | guarded | **guarded** |
| `pmap_steal_memory`'s `bl pmap_next_page_hi` (inside a span, branched to) | guarded | **not guarded** |
| `pmap_steal_memory`'s `bl pmap_enter` (inside a span, branched to) | guarded | **not guarded** |

The first line is the assertion the rule was written for and it still holds; the other two are the
loop body and they no longer do.

## What the fix did not do, and where the wall actually is

**The fix did not make the prediction right, and it is worth being exact about why**, because the
first draft of this document blamed a data-dependent condition and that was not the story.

With the fix in, the walk enters `pmap_steal_memory`, enters `pmap_next_page_hi`, enters
`pmap_enter`, enters `pmap_enter_options` - and then stops, because `OSCompareAndSwap16` at
`+0x1054` is **cold in the first sense**: `pmap_enter_options` is 0x1090 bytes and its entry block
ends at `+0x114`. Everything after that is behind a conditional branch, which is what `-O2` code
looks like at this size. There is no data fact to read here and no `if` to blame; the walk's model
of "an initialisation path is straight lines and its assertions" simply does not survive contact
with a function this shape. The device's answer is inside a retry loop in the tail of a 4 KB
function.

So the tool's straight-line answer is a **lower bound** on how far the run gets, and the correct
reading of it is "the run reaches at least this far". That was already true and already stated in
its header; what experiment 228 adds is a case where the gap between the bound and the answer is
large, and the mechanism of the gap.

`tools/xnu_entry_callwalk.py` gained `--assume-taken CALLER+0xNNN` for exactly that gap: walk into
one guarded call site as if its branch were taken. It is repeatable, the offsets are the ones the
tool's own list prints, and the walk stays mechanical while the data or the source reading is
supplied by hand. With the two sites this run passed through supplied, the tool reproduces the
experiment exactly:

```
$ python3 tools/xnu_entry_callwalk.py --elf /tmp/B228.elf \
    --assume-taken 'pmap_steal_memory+0x130' --assume-taken 'pmap_enter_options+0x1054'
walk from kernel_bootstrap:
  kernel_bootstrap
    vm_mem_bootstrap
      vm_page_bootstrap
        vm_map_steal_memory
          pmap_steal_memory
            pmap_enter
              pmap_enter_options
                OSCompareAndSwap16   STUB
first stub on the straight-line path: OSCompareAndSwap16
```

That is the tool's **third** check against an answer the device produced - `vm_mem_bootstrap` for
experiment 225, `vm_compressor_init_locks` for 226, `OSCompareAndSwap16` for this one - and the first
that required input. The first two checked the walk; this one checks the walk *and* the seam, and
both were re-run after the rule change - `vm_mem_bootstrap` and `vm_compressor_init_locks`, still -
which is what makes the change a fix rather than a tuning.

The tool also gained an annotation on every guard it prints, `guards_to_stub(callee)`: `None` if a
taken branch there reaches no symbol this image lacks - so it cannot be the stop - and a number if it
does, which names the stop that many branches further. **The ranking was tried and is worse, and the
number is what says so.** Sorted by graph distance, this experiment's stop came 73rd of the 90
reachable guards, behind the whole of `thread_deallocate`'s teardown, whose `kfree`-family stubs sit
zero or one branch from guards in a function that never executes on this path. Distance in the graph
is not likelihood on the path, so the list stays in execution order and only the per-entry annotation
is printed.

## Cost

`out/xnu_kernel_obj/osfmk_vm_vm_map.o` (`osfmk/vm/vm_map.c`) - **76439 bytes of text, 52 of data,
416 of `.bss`, 202 definitions, 162 references**, nearly three times the largest object linked before
it:

```
resolved (8):  current_map  not_in_kdp  vm_kernel_reserved_entry_init  vm_map_copyin_common
               vm_map_init  vm_map_sizes  vm_map_steal_memory  vm_submap_object

added  (61):   _vm_map_store_entry_link  _vm_map_store_entry_unlink  apple_protect_pager_setup
               convert_port_to_map  copyinmap  find_vnode_object  is_device_pager_ops
               log_stack_execution_failure  log_unnest_badness  mach_destroy_memory_entry
               memory_object_control_to_vm_object  memory_object_data_request
               memory_object_deallocate  memory_object_map  memory_object_to_vm_object
               msg_ool_size_small  pid_from_task  random  shared_region_trace_level
               thread_interrupt_level  vm_commpage_enter  vm_compressor_pager_get_count
               vm_compressor_pager_state_get  vm_counters  vm_deallocate  vm_fault_copy
               vm_fault_enter  vm_fault_unwire  vm_fault_wire  vm_global_no_user_wire_amount
               vm_global_user_wire_limit  vm_map_store_copy_insert  vm_map_store_copy_reset
               vm_map_store_entry_link  vm_map_store_entry_unlink  vm_map_store_init
               vm_map_store_lookup_entry  vm_map_store_update
               vm_map_store_update_first_free  vm_object_allocate
               vm_object_change_wimg_mode  vm_object_coalesce  vm_object_copy_delayed
               vm_object_copy_quickly  vm_object_copy_slowly  vm_object_copy_strategically
               vm_object_deactivate_pages  vm_object_deallocate  vm_object_lock_shared
               vm_object_lock_yield_shared  vm_object_pmap_protect
               vm_object_pmap_protect_options  vm_object_purgable_control
               vm_object_shadow  vm_object_sync  vm_purgeable_nonvolatile_enqueue
               vm_purgeable_object_purge_all  vm_shared_region_enter  vm_user_wire_limit
               zalloc_canblock  zone_prio_refill_configure

553 -> 606 undefined          (553 + 61 - 8)
```

`553 + 61 - 8 = 606` exactly. Of the eight resolved, six are function stubs - `current_map`,
`vm_kernel_reserved_entry_init`, `vm_map_copyin_common`, `vm_map_init`, `vm_map_sizes`,
`vm_map_steal_memory` - and two are storage, `not_in_kdp` (4 bytes) and `vm_submap_object` (4 bytes).
So the counters move `461 - 6 + 55 = 510` functions and `92 - 2 + 6 = 96` storage, and 55 + 6 = 61.
The `added` list is the shape the last several steps have had: the image's undefined set is a set, not
a path, and almost none of these 61 is anywhere the run will go - the whole `vm_map_store` layer, the
`vm_object` copy family, and `ledger_debit`/`ledger_credit`, which are stubs inside a function the
run *does* enter (see below).

| | exp-227 | now |
| --- | --- | --- |
| entry objects linked | 67 | 68 (`osfmk_vm_vm_map.o`) |
| entry text | 344804 B | **423012 B** |
| entry image | 454152 B | **519744 B** |
| entry `.bss` | 0x0026e8b8–0x0028c988 (123088 B) | 0x0027e8c0–0x0029cc48 (**123784 B**) |
| undefined | 553 | **606** |
| stubs | 461 functions, 92 storage | **510 functions, 96 storage** |
| boot_args offset | +581632 | **+647168** |
| headroom below `topOfKernelData` | 1521272 B | **1455032 B** |
| payload text | 946338 B | **1011930 B** |

This is the largest single step in the sequence so far: 78208 bytes of text and 65592 of image, and
the payload's text crosses a megabyte. The headroom is still 1.39 MB, so nothing is near a limit, but
the boot_args offset has now moved 114688 bytes down in two steps.

## The three-term decomposition, fourth test

```
symbol region    312536 -> 387924          +75388
.text            344804 -> 423012          +78208
                                    gaps + tail  +2820
```

The +2820 is consistent with 55 new function stubs' worth of 12-byte bodies (55 * 12 = 660) plus
their `entry_stub_hit("name")` strings in the aligned tail - the 61 added names average about 24
bytes with their terminating NUL - plus the layout slack a 76 KB object introduces. The second term
was measured directly in the step that follows this one, where ten stub names leaving the tail
accounted for all but 18 bytes of the tail's change.

## What is next: `OSCompareAndSwap16`, and the prediction is `zone_bootstrap`

The frontier is `OSCompareAndSwap16`, defined by `libkern/gen/OSAtomicOperations.c` ->
`out/xnu_kernel_obj/libkern_gen_OSAtomicOperations.o`. It is the cheapest object this link has taken
on: **1104 bytes of text, no data, no `.bss`, 27 definitions, and zero undefined references** - the
first object in the sequence with nothing undefined at all, so it can add no requirement of its own.
`OSCompareAndSwap16` is referenced by two objects: `osfmk_arm_pmap.o`, which is linked, and
`bsd_kern_uipc_mbuf.o`, which is not - which is why the symbol was undefined when only the first
mattered.

**The prediction is `stub_hit=zone_bootstrap`** - the same answer the walk gave for this experiment,
now on the image with `OSAtomicOperations` linked, and unchanged whether or not the two sites this
run passed through are forced. The reasoning is that `pmap_enter_options` completes once the CAS
returns (its retry loop's other exits are `mov fp, #1` / `cmp` / `b`), `pmap_steal_memory` completes,
and `vm_page_bootstrap` runs on to the next call in `vm_mem_bootstrap`.

**The named alternative, and the reason to expect it:** `pmap_enter_options` also contains
`ledger_debit` and `ledger_credit`, which are stubs, at five call sites inside the same cold region
(`+0x230` and `+0x25c` for the debit, `+0xf00`, `+0xf24` and `+0xf44` for the credit). This run passed
without stopping at any of them, so none of those five is taken on its way to `+0x1054`; if one is
taken later, the stop is
`ledger_debit` or `ledger_credit` instead. `zone_bootstrap` is the prediction and the ledger is the
alternative, and the run will say which.

`zone_bootstrap` is defined by `osfmk/kern/zalloc.c`, so the step after this one is
`osfmk_kern_zalloc.o`.

## Reproduce

```bash
# the step: 8 resolved, 61 added, 553 -> 606, stubs 461fn/92st -> 510fn/96st
printf '' > /tmp/empty.c && arm-none-eabi-gcc -mcpu=cortex-a15 -marm -c /tmp/empty.c -o /tmp/empty.o
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 \
   STAGE90_ENTRY_OSFMK_VM_VM_MAP_OBJ=/tmp/empty.o ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/A.txt
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
cp out/stage90/xnu_arm_entry_undef.txt /tmp/B.txt
comm -23 <(LC_ALL=C sort -u /tmp/A.txt) <(LC_ALL=C sort -u /tmp/B.txt)   # 8 resolved
comm -13 <(LC_ALL=C sort -u /tmp/A.txt) <(LC_ALL=C sort -u /tmp/B.txt)   # 61 added
wc -l /tmp/A.txt /tmp/B.txt                                             # 553 and 606

# ... and it ran
./tools/host_entry_macho_check.sh
./tools/host_dt_check.sh | grep -E "tree built|RESULT"
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -14   # ... stub_hit=OSCompareAndSwap16

# the two loops the guard rule could not tell apart, and the fix
arm-none-eabi-objdump -d --no-show-raw-insn out/stage90/xnu_arm_entry.elf \
  | sed -n '/<lck_mtx_lock>:/,/lck_mtx_lock_contended/p' | head -12    # beq over a bl panic
arm-none-eabi-objdump -d --no-show-raw-insn out/stage90/xnu_arm_entry.elf \
  | sed -n '/<pmap_steal_memory>:/,/^$/p' | grep -B2 -A2 'pmap_next_page_hi'

# the walk, and the same walk told about the two sites this run passed through
python3 tools/xnu_entry_callwalk.py --elf /tmp/B228.elf                         # zone_bootstrap
python3 tools/xnu_entry_callwalk.py --elf /tmp/B228.elf \
  --assume-taken 'pmap_steal_memory+0x130' --assume-taken 'pmap_enter_options+0x1054'   # OSCompareAndSwap16
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_arm_pmap.o | grep -c OSCompareAndSwap16   # 43
arm-none-eabi-nm -P out/xnu_kernel_obj/libkern_gen_OSAtomicOperations.o | awk '$2=="U"' | wc -l   # 0
```

Nothing was flashed: `persistent_write_attempted=0x00000000` in all 25 contracts that report it,
and the device returned to Android on its own.
