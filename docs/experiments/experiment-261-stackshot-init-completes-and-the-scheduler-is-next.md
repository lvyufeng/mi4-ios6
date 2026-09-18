# Experiment 261 — `stackshot_init` Completes, and the Scheduler Is the Frontier

Date: 2026-09-18
Build switches: `STAGE90_ENTRY_REAL_ARM_INIT=1` (entry image), `STAGE90_XNU_ENTRY=1` (payload);
gate flag `--allow-xnu-entry`. All other switches at their defaults, which are exp-159's:
`HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`, `STAGE90_XNU_REAL_DT = 0`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/cancro-last_kmsg.txt`

## The change

260's stop was `stackshot_init`. **The name is in two files** — `bsd/kern/stackshot.c` (manifest line 79,
the syscall half) and `osfmk/kern/kern_stackshot.c` (manifest line 559) — and only the second defines
`stackshot_init`. Both are in the manifest and both compile, so the object is
`out/xnu_kernel_obj/osfmk_kern_kern_stackshot.o`. **`OSFMK_KERN_KERN_STACKSHOT_OBJ` is the change.**

This is the first step in a while that is not cheap. The object is 17737 bytes of text, 136 of bss,
with **116 references** — 76 already satisfied and **40 new**, the largest block this frontier has
added since 254. They cluster, and the clusters say what the stackshot is made of:

```
kcdata   kcdata_memory_alloc_init   kcdata_memory_static_init   kcdata_memory_destroy
         kcdata_memory_get_used_bytes   kcdata_add_container_marker
         kcdata_add_uint32_with_description   kcdata_add_uint64_with_description
         kcdata_get_memory_addr_for_array   kcdata_undo_add_container_begin
         kcdata_write_buffer_end
coalition  coalition_id   coalition_type   coalition_is_privileged   coalition_is_reaped
         coalition_is_terminated   coalition_iterate_stackshot   coalition_term_requested
         kdp_coalition_get_leader
kdp      kdp_vtophys   kdp_mqueue_recv_find_owner   kdp_mqueue_send_find_owner
         kdp_pthread_find_owner   kdp_pthread_get_thread_kwq   kdp_sema_find_owner
         kdp_ulock_find_owner   kdp_workloop_sync_wait_find_owner
         proc_name_kdp   proc_starttime_kdp   proc_threadname_kdp
trace    machine_trace_thread   machine_trace_thread64   mt_core_supported
         mt_stackshot_thread   mt_stackshot_task
misc     count_busy_buffers   get_task_uniqueid   task_importance_list_pids
         workqueue_get_pwq_state_kdp   gLoadedKextSummaries   gLoadedKextSummariesTimestamp
```

Three of the object's definitions exist in this image as stubs and become real: `stackshot_init`,
`do_stackshot` and `stack_snapshot_from_kernel` — and the last of those is **a name 259 introduced**
when telemetry was linked, so this step is the frontier closing on a boundary it created two steps ago.

## The prediction

`stackshot_init` is 120 bytes and calls six things:

```
lck_grp_attr_alloc_init  0x80010e44  real (push {r4,lr})
lck_grp_alloc_init       0x80010ec8  real (push {r4,r5,r6,lr})
lck_attr_alloc_init      0x80011274  real (push {r4,lr})
lck_mtx_init             0x80013604  real
clock_timebase_info      0x8000d44c  real - five instructions: `ldrd r2,[r1,#8]` out of 0x800b0a30, `stm r0,{r2,r3}`, `bx lr`
__aeabi_uldivmod         0x8009b66c  the EABI runtime
```

**all six real** — so it completes, and what it does is allocate a lock group and its attribute, take a
mutex, read the timebase and compute `fault_stats.sfs_system_max_fault_time`. It returns to
`kernel_bootstrap+0x1fc`, where the straight line is `kernel_debug_string_early` (real) and then

```
8000dc44  bl sched_init    ; STUB at 0x8009ea78
```

**The prediction: `stub_hit=sched_init`, `xnu_entry_stub_caller=0x8000dc48`** — `caller - 4` =
`0x8000dc44` = `kernel_bootstrap+0x204`.

## The result

```
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start

MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000037
 xnu_entry_kv_in_dram=0x00000037
 xnu_entry_csselr_before=0x00000002
 xnu_entry_ccsidr_before=0xf0ffe03b
 xnu_entry_ccsidr_l1=0xa007e01a
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=sched_init
 xnu_entry_stub_caller=0x8000dc48

No errors detected
```

`0x8000dc48` resolves to `kernel_bootstrap+0x208`, whose `caller - 4` is
`8000dc44: bl 8009ea78 <sched_init>` — the prediction, address for address, for the **fourth step in a
row**. `failure_mask=0x00000000` in all 87 contracts that report one,
`persistent_write_attempted=0x00000000` in all 25, and the device returned to Android on its own.

`kv_written == kv_in_dram == 0x37`, four below 260's `0x3b`: `sched_init` is four characters shorter
than `stackshot_init`, and the KV buffer records the stub's name verbatim.

## Cost

| | exp-260 | now |
| --- | --- | --- |
| undefined | 596 | **633** (3 resolved, 40 added) |
| function stubs | 520 | **554** |
| storage stubs | 76 | **79** |
| entry text | 711044 B | **730436 B** (+19392) |
| entry image | 818696 B | **835080 B** (+16384) |
| entry `.bss` end | 0x800f6b88 | **0x800facc8** |
| derived `args` offset | +1015808 | **+1032192** |
| headroom | 1086584 B | **1069880 B** |
| payload text | 1310914 B | **1327298 B** (+16384) |

The image moved by exactly one 16 KB alignment block again, and the payload followed it byte for byte,
because its embedded `.bin` grew. The 40 new boundaries are the largest single addition since 254's 13
and 251's 15 put together — the frontier has reached the code that walks *other* subsystems' data
structures, and each of these will be resolved the same way: one object, from the manifest, when its
turn comes.

## What is next

`sched_init` — `osfmk/kern/sched_prim.c` or `osfmk/kern/sched_*.c`, and the interesting one: a
scheduler is not an allocator, a lock group or a console. It is the first target in this line whose
dependencies may reach outside its own object and into thread and AST machinery that the boot path has
not touched yet. Behind it the same measured line, every target still a stub:

```
8000dc54  bl ltable_bootstrap   <- stub   0x8009df98
8000dc64  bl waitq_bootstrap    <- stub
8000dc74  bl ipc_bootstrap      <- stub
```

and then `kernel_bootstrap` is past its initialisation block. That run-in ends at `bsd_init`, which is
where "XNU loads, enters the OS and runs its basic drivers" stops being a forecast.

## Reproduce

```bash
# the name is in two files, and only one defines it
grep -n 'stackshot' out/xnu_arm_manifest.txt
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_kern_kern_stackshot.o | grep ' T stackshot_init'
arm-none-eabi-nm --defined-only out/xnu_kernel_obj/osfmk_kern_kern_stackshot.o | grep -c ' T \| B \| D '

# its dependencies, and the ones this image does not have
arm-none-eabi-nm -u out/xnu_kernel_obj/osfmk_kern_kern_stackshot.o | awk '{print $2}' | sort > /tmp/ss.txt
arm-none-eabi-nm --defined-only out/stage90/xnu_arm_entry.elf | awk '{print $3}' | sort -u > /tmp/img.txt
comm -23 /tmp/ss.txt /tmp/img.txt        # the 40 new

# stackshot_init's own calls, all six real
arm-none-eabi-objdump -dr out/xnu_kernel_obj/osfmk_kern_kern_stackshot.o \
  | awk '/<stackshot_init>:/{f=1} f{print} f&&/^$/{exit}' | grep -o 'R_ARM_CALL\s*\S*'

# the link, and the prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
arm-none-eabi-objdump -d out/stage90/xnu_arm_entry.elf | awk '/^8000dc44:/{print; exit}'

# ... and it ran
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
sed -n '/jumping to XNU/,$p' /tmp/cancro-last_kmsg.txt | head -12
./tools/host_resolve_entry_addr.sh 0x8000dc48     # -> kernel_bootstrap+0x208
```
