# Experiment 252 — Two Stops in One Object, and the Stop Is `device_pager_bootstrap`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

`osfmk_vm_memory_object.o` — **9508 bytes of text**, 58 references, 58 definitions. It defines the
memory-object and memory-object-control layer (`memory_object_init`, `_terminate`, `_deallocate`,
`_map`, `_data_request`/`_return`/`_initialize`, `memory_object_control_allocate`, `_collapse`,
`_disable`, `_to_vm_object`, `vm_object_sync`, `vm_object_update`) — and, because the frontier asked
for it, **both of the next two stops in `vm_mem_bootstrap`**: `memory_manager_default_init` (`+0x244`)
and `memory_object_control_bootstrap` (`+0x254`).

Resolved **18**, added **1** — the reverse of 251's shape, and all 18 are names this image had been
stubbing since it started:

```
resolved   memory_manager_default_init  memory_object_control_allocate
           memory_object_control_bootstrap  memory_object_control_collapse
           memory_object_control_disable  memory_object_control_to_vm_object
           memory_object_data_initialize  memory_object_data_request
           memory_object_data_return  memory_object_deallocate  memory_object_init
           memory_object_last_unmap  memory_object_map  memory_object_reference
           memory_object_terminate  memory_object_to_vm_object  vm_object_sync
           vm_object_update
added      ipc_port_make_send
```

## The prediction: two functions, both short enough to read completely

Neither of the two functions the step is *for* has a call that is a stub, so both should complete:

```
<memory_manager_default_init>
80090d0c  str r1, [r0]        ; memory_manager_default = MEMORY_OBJECT_DEFAULT_NULL
80090d24  b   lck_mtx_init    ; a TAIL call, with r0/r1/r2 already set up

<memory_object_control_bootstrap>
80090f8c  bl  zinit           ; (8, 65536, 4096, "mem_obj_control")
80090fa4  bl  zone_change     ; (zone, Z_CALLERACCT, FALSE)
80090fb8  b   zone_change     ; (zone, Z_NOENCRYPT, TRUE) - also a tail call
```

`zinit` and `zone_change` are the two functions 250's `kalloc_init` called 84 times between them, and
`lck_mtx_init` is the one 250's `kalloc_init` and `zone_init` both called; `xnu_entry_callwalk.py`
found no stub on either closure. So the prediction was that the run passes **two** stops in one step —
the third time this has happened — and lands on the next stub in `vm_mem_bootstrap`:

```
+0x264  bl device_pager_bootstrap   <- STUB
+0x274  bl vm_paging_map_init       <- real
```

**The prediction: `stub_hit=device_pager_bootstrap`, caller `vm_mem_bootstrap+0x268`.**

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000043
 xnu_entry_kv_in_dram=0x00000043
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=device_pager_bootstrap
 xnu_entry_stub_caller=0x80040564

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25, and the device returned to Android on its own. `kv_written == kv_in_dram == 0x43` (67 bytes
= 33 + 34), and `0x80040564` resolves to `vm_mem_bootstrap+0x268`, whose `caller - 4` is
`80040560: bl 80091a08 <device_pager_bootstrap>`.

**Both functions completed**, and the run measured:

- **`memory_manager_default = MEMORY_OBJECT_DEFAULT_NULL`** was stored (`str r1, [r0]` with `r1 = 0`
  into `0x800ecdac`, the symbol `memory_manager_default`) and **`lck_mtx_init(&memory_manager_default_lock,
  &vm_object_lck_grp, &vm_object_lck_attr)`** was called — the argument registers in the tail call are
  the three addresses the source names.
- **`mem_obj_control_zone = zinit(sizeof(struct memory_object_control), 8192 * sizeof, 4096,
  "mem_obj_control")`** was called and its result stored to `0x800ecdbc` — the immediate 8 in the
  first argument register is `sizeof(struct memory_object_control)`, which makes the second argument
  `0x10000`, exactly the `8192*i` the source asks for.
- **`zone_change(mem_obj_control_zone, Z_CALLERACCT, FALSE)`** and
  **`zone_change(mem_obj_control_zone, Z_NOENCRYPT, TRUE)`** ran — the immediates 5 and 6 with values
  0 and 1 are the two item numbers (`zalloc.h:261`, `zalloc.h:262`), and unlike `kalloc_init`'s zones
  the second flag is `TRUE`, because this zone is never written to disk during hibernation.

A detail worth recording, because it is the shape experiment 246 identified: **both of these functions
end in a tail call** (`b lck_mtx_init`, `b zone_change`) rather than a `bl`. Had either of those two
callees been a stub, the caller key would have named *`vm_mem_bootstrap`* rather than the function the
stub was reached from, and `caller - 4` would have pointed at `bl memory_manager_default_init` — a
different symbol. That is the trap 246 found from hardware data, and it appears here twice in the same
step, in functions that happen to be real. The reading rule stays the same as it has been since: resolve
`caller - 4` against the image the run used and compare its target against the stub that was hit.

## Cost, and the fourth move

| | exp-251 | now |
| --- | --- | --- |
| undefined | 622 | **605** (18 resolved, 1 added) |
| function stubs | 540 | **523** |
| storage stubs | 82 | **82**, unchanged |
| entry text | 671505 B | **680369 B** (+8864) |
| entry image | 769072 B | **785456 B** (+16384) |
| entry `.bss` | 0x800bb500–0x800ea548 | **0x800bf500–0x800ee548** |
| layout | args +966656, headroom 1137336 B | **args +983040, headroom 1120952 B** |
| payload text | 1261290 B | **1277674 B** |

The object's 9508 bytes of text plus churn is +8864, and it did not fit either — the image grew by
another 16 KB block and the derived `boot_args` offset moved 966656 → 983040. Four steps in a row now
(248, 250, 251, 252) have moved the image, and the entry image has grown from 736192 to 785456 bytes in
those four steps. The headroom below `topOfKernelData` is **1120952 bytes** — still over a megabyte,
and the number that decides how many more of these objects fit.

## What is next

`device_pager_bootstrap` is in `out/xnu_kernel_obj/osfmk_vm_device_vm.o` — **1536 bytes of text**, 28
references, 22 definitions: `device_pager_init`, `_setup`, `_lookup`, `_map`, `_reference`,
`_deallocate`, `_terminate`, `_data_request`/`_return`/`_initialize`/`_unlock`, `_populate_object`,
`_synchronize`, `device_pager_ops` and the three lock attributes. Small enough to read in full, as
`vm_fault_init` and `memory_manager_default_init` were.

`vm_paging_map_init` (`+0x274`) is already real, so if `device_pager_bootstrap` completes the run keeps
going past it; what comes after in `vm_mem_bootstrap` has not been walked yet.

## Reproduce

```bash
grep -n 'OSFMK_VM_MEMORY_OBJECT_OBJ' stages/stage90/xnu_arm_boot/build_entry.sh
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'undefined|stubs:|text size|image bytes|bss |layout|headroom'
comm -23 <(sort /tmp/undef_251.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the resolved 18

# the prediction: both functions, and their tail calls
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<memory_manager_default_init>:/{f=1} f{print} f&&/^$/{exit}'
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<memory_object_control_bootstrap>:/{f=1} f{print} f&&/^$/{exit}'
./tools/xnu_entry_callwalk.py --root memory_manager_default_init
./tools/xnu_entry_callwalk.py --root memory_object_control_bootstrap

# the source of both, and the zone_change item numbers
sed -n '/^memory_manager_default_init/,/^}/p'          external/xnu-4570.1.46/osfmk/vm/memory_object.c
sed -n '/^memory_object_control_bootstrap/,/^}/p'      external/xnu-4570.1.46/osfmk/vm/memory_object.c
sed -n '257,265p'                                       external/xnu-4570.1.46/osfmk/kern/zalloc.h

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x80040564   # -> vm_mem_bootstrap+0x268
```
