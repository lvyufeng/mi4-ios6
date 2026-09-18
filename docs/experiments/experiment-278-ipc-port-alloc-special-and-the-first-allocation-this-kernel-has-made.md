# Experiment 278 — `ipc_port_alloc_special`, and the first allocation this kernel has ever made

**Step:** link the object that defines `ipc_port_alloc_special` — `osfmk/ipc/ipc_port.c`
(`manifest:517`), `osfmk_ipc_ipc_port.o`, the name 277's run stopped at.
**Prediction:** `stub_hit=ipc_mqueue_init` with the caller at **`ipc_port_alloc_special+0x94`** — the
object's allocation path is entirely real until its last call, which is `ipc_mqueue_init`.
**Result:** exactly that. **20 resolved, 19 added** — bigger than 277 on both sides, and 20 of the 48
definitions retire stubs. `stub_hit=ipc_mqueue_init`,
`xnu_entry_stub_caller_v=0x800ba300` = **`ipc_port_alloc_special+0x94`**, four roads agreeing, zero
aborts — and a **real 128-byte IPC port was allocated** on the device. **The twentieth prediction in a
row.**

## The step, and the object

`osfmk_ipc_ipc_port.o` is **8924 bytes of text, 16 of bss, 81 of `.rodata.str1.1`, 48 definitions and
53 references**. The 16 bytes of bss are `ipc_port_timestamp_data` (4), `ipc_port_multiple_lock_data`
(8) and `ipc_portbt` (4); the strings are `"ipc port"`, `"ipc ports"` and a format string.

The 20 retired are 18 functions and **two storage stand-ins**:

```
ipc_port_alloc_special          ipc_port_dealloc_special       ipc_port_destroy
ipc_port_make_send              ipc_port_make_send_locked      ipc_port_make_sonce_locked
ipc_port_copy_send              ipc_port_copyout_send          ipc_port_release_send
ipc_port_release_sonce          ipc_port_nsrequest             ipc_port_check_circularity
ipc_port_impcount_delta         ipc_port_importance_delta      ipc_port_importance_delta_internal
ipc_port_sync_qos_delta         kdp_mqueue_send_find_owner     kdp_mqueue_recv_find_owner
ipc_port_multiple_lock_data     ipc_port_timestamp_data        <- storage, 8 and 4 bytes
```

and the 19 added are all functions, each defined by an object this build has already compiled — the
rule since 244:

```
ipc_object_alloc          ipc_object_alloc_name     ipc_object_copyout
ipc_mqueue_init           ipc_mqueue_deinit         ipc_mqueue_destroy_locked
ipc_mqueue_changed        ipc_mqueue_override_send  ipc_entry_lookup
ipc_pset_remove_from_all  ipc_notify_send_possible  ipc_notify_port_destroyed
ipc_notify_send_once      ipc_notify_no_senders     ipc_notify_dead_name
io_free                   ipc_kmsg_reap_delayed     task_is_importance_donor
knote_adjust_sync_qos
```

The last two are the first names this walk has obliged out of `osfmk/kern/task_policy.o` and out of
`bsd/`'s `kern_event.c` — but they are on the sync-QoS path, not on the allocation path, so they are
stubs this step does not reach.

Two storage stand-ins retiring is worth a note of its own: `ipc_port_multiple_lock_data` (8 bytes) and
`ipc_port_timestamp_data` (4 bytes) were stand-ins because the image had no definition of them at all,
and they were written by real code long before this step — `ipc_bootstrap` does
`ipc_port_multiple_lock_init()` and `ipc_port_timestamp_data = 0`. Their real definitions supersede the
stand-ins here, and their sizes agree, so nothing was ever at risk; but it is the one direction of the
stand-in problem that repairs itself, and the opposite of the `task_max` shape.

Build: 860 → 859 undefined, 784 → 785 function stubs, storage 76 → 74, text 953220 → 962244, image
bytes **unchanged** at 1066104 (the growth fitted the alignment padding), `.bss` `0x80103a00` ..
`0x80139548` (219976 bytes), headroom 1862328.

## The built path, and where it stops

`ipc_host_init` calls this function first thing (`800b757c bl ipc_port_alloc_special`), so the step runs
it for real. The linked stream is the object's instruction for instruction:

```
800ba26c <ipc_port_alloc_special>:
800ba26c: push {r4, r5, fp, lr}
800ba278: ldr r0, [ipc_object_zones]        ; IOT_PORT is 0 (`ipc_object.h:149`)
800ba280: bl 8006f744 <zalloc>              ; REAL
800ba284: cmp r0, #0 / beq -> return IP_NULL ; the only exit that is not the full one
800ba294: bl 80003764 <__bzero>             ; REAL, `mov r1, #128` = sizeof(struct ipc_port)
800ba2ac: bl 800129f8 <lck_spin_init>       ; REAL, with ipc_lck_grp / ipc_lck_attr
800ba2f0: strd r2, [r4]                     ; io_bits 0x80000000, ip_references 1
800ba2fc: bl 800d0e94 <ipc_mqueue_init>     ; A STUB  <- the stop
800ba300: mov r0, r4 / pop {r4, r5, fp, pc}
```

`ipc_port_init` is real in this object at `+0x5c4` and is **inlined** here, which is why the source's
`ipc_port_init(port, space, 1)` shows up as the store groups rather than as a call.

## Why `zalloc` was predicted to succeed, and the measurement it rests on

The one thing the prediction depended on is that `zalloc` does not panic first — a zone that cannot grow
takes a real `panic` *before* the stub is reached, and a panic is a different experiment. The argument is
not a hope:

- `ipc_space_create_special` calls `zalloc` at `800ac520`, from inside `ipc_bootstrap`, and **274 is the
  run in which `ipc_bootstrap` returned**. So a `zalloc` from a zone `ipc_bootstrap` created with the
  same `scale_setup` numbers had already worked on this device, once, before this step was written.
- `ipc_object_zones[IOT_PORT]` is the *same storage stand-in array* that `ipc_bootstrap` filled with a
  real `zinit("ipc ports", 128, ipc_port_max * 128, 128)` in that run. The stand-in is zeroed at payload
  start and written by real code before `ipc_port_alloc_special` ever sees it — which is why reading a
  zeroed stand-in here is not a null-zone panic, and why the read is safe despite the stand-in's *value*
  being 0 in the image.

That second point is the general one, and it is the opposite of `task_max`'s shape: a stand-in whose
value is written by real code before it is read is not a defect, and the way to tell is to ask which
runs wrote it. See [[mi4-stand-in-size-is-not-value]].

## What the run measured, and what it did

| key | value | what it says |
| --- | --- | --- |
| `stub_hit` | `ipc_mqueue_init` | the prediction, named by the stub's own write |
| `xnu_entry_stub_caller` | `0x800ba300` | `ipc_port_alloc_special+0x94`, read out of `g_kv_buf` |
| `xnu_entry_stub_caller_a` | `0x800ba300` | the second call, one call later, same state |
| `xnu_entry_stub_caller_v` | `0x800ba300` | the value itself, through the ram-console path |
| `xnu_entry_stub_caller_e` | `0x800ba300` | the same digits, written by `entry_kv` from the epilogue |
| `xnu_entry_stub_caller_digits` | `0x33` | 51 = 26 + 25, a 15-character name |
| `xnu_entry_stub_caller_w0` | `0x62303038` | `800b`, the four bytes in `g_kv_buf` |
| `xnu_entry_stub_caller_w1` | `0x30303361` | `a300`, the next four |
| `xnu_entry_kv_written` | `0x60` | 96 = a 26-byte stub record plus 34- and 36-byte caller records |
| `xnu_entry_kv_in_dram` | `0x84` | 132 = 96 + 36, the faithful-read control |
| `xnu_entry_kv_dropped` | `0x0` | nothing truncated |
| `xnu_entry_abort_entries` | `0x0` | no abort was taken |

`tools/host_resolve_entry_addr.sh 0x800ba300` gives `ipc_port_alloc_special+0x94`, and
`caller-4 = 0x800ba2fc` is the `bl 800d0e94 <ipc_mqueue_init>` itself. 661 of 661 words of `entry_kv`
through `entry_stub_hit` match the linked ELF again — the seventh build running.

**What ran in this one run, by completing:** `zalloc` allocated 128 bytes from the `"ipc ports"` zone
that `ipc_bootstrap` created four experiments earlier; `bzero` cleared them; `lck_spin_init` initialized
the object's spin lock with the real `ipc_lck_grp`/`ipc_lck_attr` that `ipc_bootstrap` filled in; and the
two store groups gave the object `io_bits = 0x80000000` and `ip_references = 1`. That is **the first IPC
port this kernel has ever allocated on this device** — and the first time this walk has executed an
*allocation* path rather than an initialization one. Everything before it was a `zinit`, a lock
initializer, a table, or a map suballocation.

## Safety

One run, `fastboot boot` only, nothing flashed. `persistent_write_attempted=0x00000000` in all 25
places, `failure_mask=0x00000000` in all 87 contracts, zero aborts, and the device returned to Android
on its own (`sys.boot_completed=1`, Android 10).

## What is next

`ipc_mqueue_init` is `osfmk/ipc/ipc_mqueue.c` — the next step, and the object that defines the other
seven `ipc_mqueue_*` / `ipc_notify_*` names this step just obliged. It is called at the *end* of a port
allocation, so behind it is the rest of `ipc_host_init` (two more `ipc_port_alloc_kernel` calls and
three `kernel_set_special_port` calls — the three host special ports), and then the walk returns to
`ipc_init`, to `kernel_bootstrap`, and onward to `mapping_free_prime` (`8000e170`, real), `machine_init`,
`clock_init`, `ledger_init`, and after that stretch `bsd_init` — where "XNU loads, enters the operating
system and runs its basic drivers" stops being a forecast.

## Reproduce

```bash
# entry image: the comment block above OSFMK_IPC_IPC_PORT_OBJ records the step and its prediction
(cd stages/stage90/xnu_arm_boot && STAGE90_ENTRY_REAL_ARM_INIT=1 ./build_entry.sh)
(cd stages/stage90 && STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./build.sh)
(cd stages/stage90 && ./run_and_capture.sh --allow-xnu-entry)
```

The line that carries the result is `xnu_entry_stub_caller_v=0x800ba300`;
`tools/host_resolve_entry_addr.sh 0x800ba300` resolves it against `out/stage90/xnu_arm_entry.elf` to
`ipc_port_alloc_special+0x94`.
