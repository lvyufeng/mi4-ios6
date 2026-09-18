# Experiment 251 — `vm_fault_init` Completes, and the Stop Is `memory_manager_default_init`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change: the largest object so far

`osfmk_vm_vm_fault.o` — **30392 bytes of text** (31667-byte object), **156 references**, 58 global
definitions. Twice exp-248's `vm_user.o` and nearly seven times 250's `kalloc.o`. It is the fault
path: `vm_fault`, `vm_fault_page`, `vm_fault_enter`, `vm_fault_wire`/`_unwire`, `vm_fault_copy`,
`vm_fault_cleanup`, `vm_pre_fault`, `kdp_lightweight_fault`, the code-signing validation path
(`vm_page_validate_cs`, `vm_page_validate_cs_mapped`, `vm_cs_*`) — and the counters
(`vm_fault_collapse_total`, `vm_fault_external`, `vm_fault_collapse_skipped`, `vm_default_behind`, …).

Resolved 9, added 15 — for the first time since 248 the step brings more new frontiers than it closes:

```
resolved   vm_fault  vm_fault_cleanup  vm_fault_copy  vm_fault_enter  vm_fault_init
           vm_fault_page  vm_fault_unwire  vm_fault_wire  vm_page_validate_cs
added      cs_enforcement  cs_invalid_page  cs_validate_range  current_thread_aborted
           kcdata_estimate_required_buffer_size  kcdata_get_memory_addr
           os_reason_alloc_buffer_noblock  panic_on_cs_killed  set_thread_exit_reason
           task_update_logical_writes  throttle_lowpri_io  vm_compressor_pager_get
           vnode_pager_cs_check_validation_bitmap  vnode_pager_get_object_mtime
           vnode_pager_get_object_name
```

The added fifteen are the code-signing and vnode-pager boundaries the fault path reaches out to: the
first time this image has had stubs named by a function that is itself only reachable from a page
fault. They are stubs now, but nothing in this run reaches them — the stop is earlier, in
`vm_mem_bootstrap`.

## The prediction, and a function small enough to read in full

`vm_fault_init` is the *bootstrap* half of the file, and unlike the fault path it is short. All four
of its calls are real:

```
80087680  bl __aeabi_uldivmod
800876a0  bl PE_parse_boot_argn      ; "vm_compressor"
80087708  bl _consume_printf_args    ; the "Ignoring vm_compressor boot arg" path
80087720  bl PE_get_default          ; "kern.vm_compressor"
```

and `tools/xnu_entry_callwalk.py --root vm_fault_init` found **no stub on its straight-line path**.
So the prediction was that `vm_fault_init` completes and the stop moves to the next stub in
`vm_mem_bootstrap`:

```
+0x244  bl memory_manager_default_init    <- STUB
+0x254  bl memory_object_control_bootstrap <- STUB
+0x264  bl device_pager_bootstrap          <- STUB
```

**The prediction: `stub_hit=memory_manager_default_init`, caller `vm_mem_bootstrap+0x248`** — the
return address of the `bl` at `+0x244`.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000048
 xnu_entry_kv_in_dram=0x00000048
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=memory_manager_default_init
 xnu_entry_stub_caller=0x80040544

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25, and the device returned to Android on its own. `kv_written == kv_in_dram == 0x48` (72 bytes
= 38 + 34), and `0x80040544` resolves to `vm_mem_bootstrap+0x248`, whose `caller - 4` is
`80040540: bl 80088dbc <memory_manager_default_init>`.

**`vm_fault_init` completed.** It is small, and the disassembly says exactly what it did — every step
of it is on the record in the run's own control flow:

- **`vm_hard_throttle_threshold` was computed.** `sane_size * (35 - MIN(sane_size / 1 GB, 25)) / 100`
  compiled to `lsr r0, r2, #30 / orr … / cmp r0, #25 / rsblt r1, r0, #35` then the 64-bit
  `__aeabi_uldivmod` by 100, and the result was stored to `vm_hard_throttle_threshold`
  (`0x800e8d88`, 8 bytes, `strd r0, [r2]`). The `MIN` clamp and the multiply are both visible in the
  instruction stream, so the number this hardware booted with was produced by XNU's own formula.
- **`PE_parse_boot_argn("vm_compressor", …)` returned FALSE** — the `cmp r0, #0 / beq +0xcc` branch is
  taken, and the `+0xcc` block is the `need_default_val` path — so `need_default_val` stayed TRUE and
  the bit-test loop over `VM_PAGER_MAX_MODES` at `+0x74` was **not** entered. The one call to
  `_consume_printf_args` in this function sits on the "Ignoring \"vm_compressor\" boot arg" path at
  `+0xc8`, which is only reachable when the boot arg *is* present; it was not called.
- **`PE_get_default("kern.vm_compressor", &vm_compressor_mode, 4)` ran** — the device-tree fallback
  path, taken because the boot arg was absent.
- **`PE_parse_boot_argn("vm_compressor_threads", &vm_compressor_thread_count, 4)` ran** (the address it
  writes, `0x800bab44`, is the symbol of that name).

Then `memory_manager_default_init` — the next symbol this image does not provide, one call further on.
The fault path itself (`vm_fault`, `vm_fault_page`, `vm_page_validate_cs`) is now *defined* but was not
executed by this run: nothing has faulted yet, because `vm_mem_bootstrap` has not finished.

## Cost, and the third move in a row

| | exp-250 | now |
| --- | --- | --- |
| undefined | 616 | **622** (+6 net: 9 resolved, 15 added) |
| function stubs | 535 | **540** |
| storage stubs | 81 | **82** |
| entry text | 639953 B | **671505 B** (+31552) |
| entry image | 752648 B | **769072 B** (+16424) |
| entry `.bss` | 0x800b74f0–0x800e64c8 | **0x800bb500–0x800ea548** |
| layout | args +950272, headroom 1153848 B | **args +966656, headroom 1137336 B** |
| payload text | 1244866 B | **1261290 B** |

The object's 30392 bytes of text plus churn is +31552, and it did not fit again: the image grew by
another 16 KB block and the derived `boot_args` offset moved 950272 → 966656. 248, 250 and 251 have
now all moved it; the object sizes at this frontier have gone from hundreds of bytes to tens of
thousands, so the "did it move" question is now the default rather than the exception. The headroom
below `topOfKernelData` is 1137336 bytes — a little over a megabyte, which is the number to watch as
the remaining `vm_mem_bootstrap` objects are linked.

## What is next

`memory_manager_default_init` is in `out/xnu_kernel_obj/osfmk_vm_memory_object.o` — **9508 bytes of
text, 58 references, 58 definitions** — and, usefully, that same object also defines
**`memory_object_control_bootstrap`**, which is the *next* stub in `vm_mem_bootstrap` after it
(`+0x254`). So this one object may close two of the remaining stops the way 249's 344-byte object
closed one and moved a second, and the prediction for 252 is a prediction about
`memory_manager_default_init`'s own closure — which, like `vm_fault_init`'s, is short enough to read
in full from the disassembly before the run.

The candidates after it in `vm_mem_bootstrap` are `device_pager_bootstrap` (`+0x264`, in
`osfmk_vm_device_pager.o`) and, if all three complete, whatever `vm_page_module_init`-era code and
`vm_paging_map_init` do — `vm_paging_map_init` is already real.

## Reproduce

```bash
grep -n 'OSFMK_VM_VM_FAULT_OBJ' stages/stage90/xnu_arm_boot/build_entry.sh
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'undefined|stubs:|text size|image bytes|bss |layout|headroom'
comm -13 <(sort /tmp/undef_250.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the added 15

# the prediction
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<vm_fault_init>:/{f=1} f{print} f&&/^$/{exit}'
./tools/xnu_entry_callwalk.py --root vm_fault_init

# what vm_fault_init computed, from the source
sed -n '/^vm_fault_init/,/^}/p' external/xnu-4570.1.46/osfmk/vm/vm_fault.c

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x80040544   # -> vm_mem_bootstrap+0x248

# the next two stops live in one object
arm-none-eabi-size -A out/xnu_kernel_obj/osfmk_vm_memory_object.o
```
