# Experiment 382 — the checkpoint reports, and the caller it names is a thread teardown, not the task teardown

**Step:** no link change. A *measurement* build: the entry link with the checkpoint instrument pointed at
one symbol — `STAGE90_ENTRY_CHECKPOINT=ipc_port_dealloc_special` — and the payload rebuilt around it. It
is the run experiment 381's silence calls for: the payload's ladder completes, jumps to XNU's `_start`,
and nothing comes back, so the question is not *why a frame blocked* but *whether the run reached a
frame at all*.

**Prediction:** the instrument's own rule, from `entry_checkpoint.c`: a report means the image's
reporting path works in this build and the boot reached that point — so 381's silence is about code
*after* it; a silence means the reporting path is what failed. And 381's block narrows the caller to one
of two frames: `ipc_task_terminate` (caller `0x800c92e8`) or `semaphore_dereference` (caller
`0x800b390c`).

**Measured: it reported, and the caller is neither of those.**

    stub_hit=ipc_port_dealloc_special
    xnu_entry_stub_caller=0x800c9a0c
    xnu_entry_stub_caller_a=0x800c9a0c
    xnu_entry_stub_caller_e=0x800c9a0c
    cp_arg0=0xc056f300  cp_arg1=0xc056c840  cp_arg2=0xc056f304  cp_arg3=0x00000000

`0x800c9a0c` is **`ipc_thread_terminate + 0x18c`** — the `bl ipc_port_dealloc_special` at
`0x800c9a08`. The frame above the port teardown is a **thread** teardown, not the task teardown 381's
block argued for by elimination.

## The two conclusions

**1. The reporting path works in this build.** A wrapper reached `entry_stub_hit` and the payload wrote
its keys, in the same image family that was silent in 381. So 381's ending is not the instrument's
defect (which is what experiment 280's silence turned out to be) — it is the walk. The walk reaches
this call, and after it nothing reports. That is the strong form of the finding: the silence lives at
or after `ipc_thread_terminate + 0x18c`.

**2. The caller is named by measurement, and it corrects a reading defect.** 381's block enumerated
`ipc_port_dealloc_special`'s callers from `ipc_port.c` and concluded that "what is left is
`ipc_task_terminate`". **The linked image has fifteen call sites, in eleven functions**, and they are
not all in that file:

    iv_dealloc + 0x9c                            0x800ad89c
    convert_voucher_to_port + 0x68               0x800add78
    ivac_dealloc + 0xb4                          0x800adfb4
    convert_voucher_attr_control_to_port + 0x80  0x800ae1dc
    semaphore_dereference + 0x44                 0x800b390c
    ipc_task_terminate + 0x284 / +0x290 / +0x2a4 0x800c92e8 / 0x800c92f4 / 0x800c9308
    ipc_task_reset + 0x29c                       0x800c95c8
    ipc_thread_terminate + 0x18c / +0x1b8        0x800c9a0c / 0x800c9a38
    ipc_thread_reset + 0x70 / +0x1d8             0x800c9b1c / 0x800c9c84
    iokit_destroy_object_port + 0x40             0x8014d170
    work_interval_port_notify + 0xf4             0x8017fdec

— taken from the image (`bl 800d9638` over the disassembly), not from one source file. The walk took
the first of the two `ipc_thread_terminate` sites. **The tell: a caller list is a property of the link,
so read it out of the linked image; a file's callers are a subset and read as if they were the set.**

## The forward reading, before the next run

`io_free` is the function this walk made real in 381, and the whole subtree under it is now readable
from the image (stub slots = `realstubs.o`'s `.text`, `0x80185180 .. 0x80188DE0`, 644 × 0x18):

    io_free (0x801845bc, otype in r0, object in r1)
      ├─ if otype == IOT_PORT: ipc_port_finalize (0x800d9668)
      │    ├─ ipc_table_free → kfree  (real; no stub in the subtree)
      │    └─ b ipc_mqueue_deinit (0x800d9a04)
      │         ├─ b waitq_set_deinit → waitq_lock, ltable_get_elem, lt_elem_invalidate,
      │         │     waitq_clear_prepost_locked, walk_waitq_links, delay, ltable_put_elem … (all real)
      │         └─ b waitq_deinit → waitq_lock, waitq_clear_prepost_locked, walk_waitq_links … (all real)
      ├─ lck_spin_destroy (object+8, grp 0x801f9d20) → lck_grp_lckcnt_decr, lck_grp_deallocate (real)
      └─ b zfree (0x800700a0)  — the only stubs anywhere in this subtree, all of them guarded:
           0x800701b8  bl trace_backtrace                (DO_LOGGING(zone) && corruption_debug_flag)
           0x800705e4  bl btlog_add_entry                (DO_LOGGING(zone), corruption path)
           0x800705f4  bl OSBacktrace                    (same guard)
           0x80070698  bl btlog_remove_entries_for_element (DO_LOGGING(zone), leak path)
         plus panics at 0x80070368 / 0x80070454 / 0x80070540 and free_to_zone / re_queue_tail /
         lck_mtx_lock_spin_always (all real).

So the subtree has **no reportable stub on an untaken logging branch** — which is exactly what a silent
run looks like from the inside if it is in there. The one storage the step retired,
`ipc_object_zones[IOT_PORT]`, is *not* a suspect for a null zone: it is written by `ipc_init`
(`ipc_init.c:167`), which is called from `kernel_bootstrap + 0x254` and ran long before this point.

## What the next run measures

The plain 381 run's call site for `io_free` is known (`ipc_port_destroy + 0x35C`, key `0x800d8380` —
380's stop), and io_free is entered there. So the sharpest single question the instrument can answer is
whether **`io_free` returns**, and that is the checkpoint's `_AFTER` variant, whose silence is a
finding rather than an absence because the call site has already been proved reached:

    STAGE90_ENTRY_CHECKPOINT=io_free STAGE90_ENTRY_CHECKPOINT_AFTER=1

  * **a report with `cp_ret`** ⇒ `io_free` returned ⇒ the whole port free completes ⇒ the silence is
    after it, in `ipc_thread_terminate`'s own continuation;
  * **silence** ⇒ `io_free` does not return ⇒ the silence is inside the one function this step made
    real, and the next probe splits its body (`ipc_port_finalize`/`ipc_mqueue_deinit` chain vs `zfree`).

## Safety

A non-persistent `fastboot boot` of `out/stage90/stage90-qcdt.img` (4896768 bytes, sha256
`957107d7ac5c345acb8b67934c18b0796f836512bc45e5f6844a3d0592393c92`); nothing flashed. The checkpoint
build's own numbers: text size `1768704` (381's `1768512` plus the wrapper's 192 bytes), image
`1877400` (unchanged), `.bss` `0x801CA5C0 .. 0x802035D8` (0x39018), headroom `2083368` — the instrument
costs 192 bytes of `.text` and moves nothing else.

The run ended at the checkpoint as designed (the wrapper is terminal). No `exception:`, no `panic`; 25
records of `persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000`; log 301716
bytes, 3979 lines. The device came back to Android on its own, and was confirmed there afterwards
(`adb devices` shows `4a2fe00b`, uptime consistent with a hardware-watchdog reset followed by an
Android boot).
