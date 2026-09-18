# Experiment 253 — `vm_mem_bootstrap` Returns, and the Stop Leaves It

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

`osfmk_vm_device_vm.o` — **1536 bytes of text**, 28 references, 22 definitions: `device_pager_init`,
`_setup`, `_lookup`, `_map`, `_reference`, `_deallocate`, `_terminate`, the `_data_*` family,
`_populate_object`, `_synchronize`, `device_pager_ops` and the three lock attributes.

Resolved 2, added 2 — the first step where the two are equal:

```
resolved   device_pager_bootstrap  is_device_pager_ops
added      device_close  device_data_action
```

So 605 undefined before and after (523 function and 82 storage stubs on both sides), and text
680369 → 682353 (+1984). The two new names are `device_vm.o`'s own door out to the device layer.

## The prediction: the last call in `vm_mem_bootstrap`

`device_pager_bootstrap` is five statements and none of them calls a stub:

```
80091570  bl zinit                    ; (40, MAX_DNODE*40 = 0x61a80, PAGE_SIZE, "device node pager structures")
80091588  bl zone_change              ; (zone, Z_CALLERACCT, FALSE)
80091598  bl lck_grp_attr_setdefault
800915b0  bl lck_grp_init             ; (&device_pager_lck_grp, "device_pager", &…lck_grp_attr)
800915c0  b  lck_attr_setdefault      ; a tail call again
```

`xnu_entry_callwalk.py` confirmed no stub on its straight-line closure. The reason this step is
different is what comes **after** it. The tail of `vm_mem_bootstrap` is:

```
80040560  bl device_pager_bootstrap    <- this step's function
80040564  bl vm_paging_map_init        <- real
80040570  bl kernel_debug_string_early
80040578  pop {r4, r5, r6, r7, fp, pc}  <- vm_mem_bootstrap RETURNS
```

`vm_paging_map_init` is already real and `vm_mem_bootstrap` has nothing left after it but its
epilogue, so this run should be the first since experiment 246 that leaves the function the frontier
has been inside since 247. The caller is `kernel_bootstrap`, which does:

```
8000db60  bl vm_mem_bootstrap
8000db70  bl cs_init                   <- STUB
8000db80  bl vm_mem_init               <- real
8000dbc8  bl oslog_init                <- real
8000dbd8  bl telemetry_init            <- STUB
```

**The prediction: `stub_hit=cs_init`, caller `kernel_bootstrap+0x134`** — with `vm_mem_init` named as
the outcome if `cs_init` turned out to be real, and `telemetry_init` the one after that.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000034
 xnu_entry_kv_in_dram=0x00000034
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=cs_init
 xnu_entry_stub_caller=0x8000db74

No errors detected
```

`failure_mask=0x00000000` in all 87 contracts that report one, `persistent_write_attempted=0x00000000`
in all 25, and the device returned to Android on its own. `kv_written == kv_in_dram == 0x34` (52 bytes
= 18 + 34), and `0x8000db74` resolves to `kernel_bootstrap+0x134`, whose `caller - 4` is
`8000db70: bl 80091f04 <cs_init>`.

**`vm_mem_bootstrap` returned.** That is the measurement this step was for, and it is the first time
the stop has been outside that function since experiment 247 began it — six steps ago:

| step | stop | reached from |
| --- | --- | --- |
| 247 | `vm_allocate_kernel` | `vm_mem_bootstrap+0xf8` |
| 248 | `kext_alloc_init` | `vm_mem_bootstrap+0x1a8` |
| 249 | `kalloc_init` | `vm_mem_bootstrap+0x228` |
| 250 | `vm_fault_init` | `vm_mem_bootstrap+0x238` |
| 251 | `memory_manager_default_init` | `vm_mem_bootstrap+0x248` |
| 252 | `device_pager_bootstrap` | `vm_mem_bootstrap+0x268` |
| 253 | **`cs_init`** | **`kernel_bootstrap+0x134`** |

Everything that function does has now either run or been stepped over, `device_pager_bootstrap` and
`vm_paging_map_init` included. Measured this run, read from the source and the instruction stream:

- **`device_pager_zone = zinit(sizeof(struct device_pager), MAX_DNODE * sizeof, PAGE_SIZE, "device node
  pager structures")`** — the immediate `40` in the first argument register is the struct's size and
  `0x61a80` in the second is `MAX_DNODE * 40`, i.e. 10000 pagers' worth; the result was stored to
  `0x800ecdc0`.
- **`zone_change(device_pager_zone, Z_CALLERACCT, FALSE)`** ran (immediate 5 with value 0).
- **`lck_grp_attr_setdefault(&device_pager_lck_grp_attr)`**, **`lck_grp_init(&device_pager_lck_grp,
  "device_pager", &device_pager_lck_grp_attr)`** and the tail-called **`lck_attr_setdefault`** ran —
  a third lock group initialised on this boot, after `kalloc_init`'s, and `zone_init`'s before it.
- **`vm_paging_map_init()`** ran, the function that was already real and had been waiting behind four
  stubs since 246.

Then `kernel_bootstrap`'s `bl vm_mem_bootstrap` returned, its `kernel_debug_string_early` printed, and
the run stopped at **`cs_init`** — the code-signing subsystem's initialisation, reached from
`bsd/kern/kern_cs.c`. That is the first **BSD-layer** function this image has ever been stopped at:
every previous stop, from the very first stub in this sequence, has been in `osfmk`.

## Cost

| | exp-252 | now |
| --- | --- | --- |
| undefined | 605 | **605** (2 resolved, 2 added) |
| function stubs | 523 | **523** |
| storage stubs | 82 | **82** |
| entry text | 680369 B | **682353 B** (+1984) |
| entry image | 785456 B | **785456 B**, unchanged |
| entry `.bss` | 0x800bf500–0x800ee548 | **0x800bf500–0x800ee688** |
| layout | args +983040, headroom 1120952 B | **args +983040, headroom 1120632 B** |
| payload text | 1277674 B | **1277674 B**, unchanged |

This is the first step in five that did **not** move the image: 1984 bytes of text fitted inside the
linker script's alignment padding, so `__entry_image_end`, `end_kern`, the derived `boot_args` offset
and the payload's own size are all exactly where 252 left them. The pattern is the one 247 first
recorded — small objects fit, large ones move the image by a 16 KB block — and at these object sizes
the small ones are now the exception.

## What is next

`cs_init` is in `out/xnu_kernel_obj/bsd_kern_kern_cs.o` — **2120 bytes of text, 21 references**. It is
the code-signing trust-cache initialisation, and it is where the fifteen boundaries experiment 251's
fault path introduced (`cs_enforcement`, `cs_invalid_page`, `cs_validate_range`,
`vnode_pager_cs_check_validation_bitmap`, `panic_on_cs_killed`) are finally wanted from. Whether it
completes on this image is a question about its own closure, which has not been walked yet.

Beyond it in `kernel_bootstrap`, already read off the disassembly: `vm_mem_init` (real),
`oslog_init` (real), `telemetry_init` (**stub**), then `PE_i_can_has_debugger` and the boot-argument
parsing that follows it. The two real functions between here and `telemetry_init` mean another step or
two of the same kind.

## Reproduce

```bash
grep -n 'OSFMK_VM_DEVICE_VM_OBJ' stages/stage90/xnu_arm_boot/build_entry.sh
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
grep -E 'undefined|stubs:|text size|image bytes|bss |layout|headroom'
comm -23 <(sort /tmp/undef_252.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the resolved 2

# the prediction: the function, and the tail of its caller
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<device_pager_bootstrap>:/{f=1} f{print} f&&/^$/{exit}'
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<vm_mem_bootstrap>:/{f=1} f{print} f&&/^$/{exit}' | tail -12
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/<kernel_bootstrap>:/{f=1} f{print} f&&/^$/{exit}' \
  | sed -n '/8000db60:/,$p' | head -12
./tools/xnu_entry_callwalk.py --root device_pager_bootstrap

# the source the immediates come from
sed -n '/device_pager_bootstrap(void)/,/^}/p' external/xnu-4570.1.46/osfmk/vm/device_vm.c

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x8000db74   # -> kernel_bootstrap+0x134
grep -c 'persistent_write_attempted=0x00000000' /tmp/cancro-last_kmsg.txt
```
