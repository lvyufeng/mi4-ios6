# Experiment 259 — `telemetry_init` Completes, and the Stop Is `console_init`

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

258's stop was `telemetry_init`, the first stub in `kernel_bootstrap`'s straight line after
`oslog_init` returned. It is an ordinary tree symbol with an ordinary answer: `osfmk/kern/telemetry.c:120`,
one object, already in the manifest (`out/xnu_arm_manifest.txt:595`) and already built at
`out/xnu_kernel_obj/osfmk_kern_telemetry.o`. **`OSFMK_KERN_TELEMETRY_OBJ` is the change.**

The object, measured before the run: 5482 bytes of text, 28 of data, 744 of bss, 30 definitions,
57 references. 40 of the references were already satisfied; the **17 new ones** are

```
get_task_dispatchqueue_serialno_offset   host_get_special_port      kperf_ucallstack_sample
proc_did_throttle   proc_get_darwinbgstate   proc_get_effective_task_policy   proc_pid
proc_uniqueid   proc_was_throttled   stack_snapshot_from_kernel   task_did_exec
task_grab_latency_qos   telemetry_notification
vm_shared_region_deallocate   vm_shared_region_get
vm_shared_region_get_slide    vm_shared_region_start_address
```

— almost all of them the BSD layer, and none of them on this call's path. Four of its definitions exist
in the image today as stubs (`telemetry_init`, `telemetry_task_ctl`, `bootprofile_init`,
`bootprofile_wake_from_sleep`) and the link replaces them.

## The prediction

`telemetry_init`'s body calls exactly six things, read from the object's relocations:

```
lck_grp_init   lck_mtx_init   PE_parse_boot_argn   kmem_alloc   bzero   _consume_kprintf_args
```

**all six already real in this image** — so it completes, and what it does on the way is a real
16384-byte allocation. The `telemetry_buffer_size` boot arg is absent, so the size is
`TELEMETRY_DEFAULT_BUFFER_SIZE` (`telemetry.c:83`, `16 * 1024`), capped at `TELEMETRY_MAX_BUFFER_SIZE`
(64 KB), passed to `kmem_alloc(kernel_map, &telemetry_buffer.buffer, size, VM_KERN_MEMORY_DIAG)`. That
is the second real kernel allocation this frontier has made, after 255's 73728-byte guarded one.

It returns to `kernel_bootstrap+0x19c`, and the straight line from there is

```
8000dbe0  bl PE_i_can_has_debugger      ; real
8000dbfc  bl PE_parse_boot_argn         ; real, and only reached if the debugger is present
8000dc20  bl kernel_debug_string_early  ; real
8000dc24  bl console_init               ; STUB at 0x800980c8
```

**The prediction: `stub_hit=console_init`, `xnu_entry_stub_caller=0x8000dc28`** — `caller - 4` =
`0x8000dc24` = `kernel_bootstrap+0x1e4`.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000039
 xnu_entry_kv_in_dram=0x00000039
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=console_init
 xnu_entry_stub_caller=0x8000dc28

No errors detected
```

`0x8000dc28` resolves to `kernel_bootstrap+0x1e8`, whose `caller - 4` is
`8000dc24: bl 800980c8 <console_init>` — the prediction, address for address, for the second step in a
row. `failure_mask=0x00000000` in all 87 contracts that report one,
`persistent_write_attempted=0x00000000` in all 25, and the device returned to Android on its own.

`kv_written == kv_in_dram == 0x39`, two bytes shorter than 258's `0x3b`: the KV buffer records the
stub's name verbatim, and `telemetry_init` (14) is two characters longer than `console_init` (12).

## Cost

| | exp-258 | now |
| --- | --- | --- |
| undefined | 592 | **605** (4 resolved, 17 added) |
| function stubs | 514 | **527** |
| storage stubs | 78 | **78** |
| entry text | 702564 B | **708644 B** (+6080) |
| entry image | 802248 B | **818656 B** (+16408) |
| entry `.bss` end | 0x800f2888 | **0x800f6b88** |
| headroom | 1103736 B | **1086584 B** |
| payload text | 1294466 B | **1310874 B** (+16408) |
| derived `args` offset | +999424 | **+1015808** |

The image moved for the first time since 255, and by the shape this project has seen before: +16408 =
one 16 KB alignment block plus 24, and the payload moved by exactly the same 16408 because its
embedded `.bin` grew. The derived `boot_args` offset moved with `.bss`, as the layout block intends.

## What is next

`console_init` — and the straight line behind it is already measured, every target still a stub:

```
8000dc24  bl console_init       <- stub   0x800980c8
8000dc34  bl stackshot_init     <- stub
8000dc44  bl sched_init         <- stub
8000dc54  bl ltable_bootstrap   <- stub
8000dc64  bl waitq_bootstrap    <- stub
8000dc74  bl ipc_bootstrap      <- stub
```

with `kernel_debug_string_early` (real, `strlen`/`strncpy` only) between each pair. `console_init` is
`osfmk/kern/console.c`, and it is where the kernel starts talking to the serial console — the first
step in this list that is not merely an allocator or a lock group, and the one where "XNU loads and
enters the OS" gets its first visible output.

The 17 new boundaries the telemetry link added are worth remembering as a block: they are the BSD
`proc_*`/`task_*`/`vm_shared_region_*` surface, and any of them may appear as a stop once execution
reaches code that samples a task. They are cheap to resolve by the same method — the manifest already
compiles most of the BSD layer.

## Reproduce

```bash
# the object, and that it is the one the manifest builds
grep -n 'osfmk/kern/telemetry.c' out/xnu_arm_manifest.txt
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_kern_telemetry.o | grep ' T telemetry_init'
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_kern_telemetry.o | wc -l         # 57

# its calls, which are all real
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_kern_telemetry.o \
  | awk '/<telemetry_init>:/{f=1} f{print} f&&/^$/{exit}' | grep -o 'R_ARM_CALL\s*\S*'

# the link, and the prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
comm -13 <(sort /tmp/undef_258.txt) <(sort out/stage90/xnu_arm_entry_undef.txt)   # the added 17
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/^8000dc24:/{print; exit}'

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x8000dc28     # -> kernel_bootstrap+0x1e8
```
