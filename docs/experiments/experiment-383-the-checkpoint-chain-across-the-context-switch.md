# Experiment 383 — the checkpoint chain: the boot thread's own teardown, the context switch, and the ladder's top (383–393)

**Step:** no link change. Eleven *measurement* builds on the 381 link — the entry image with
`STAGE90_ENTRY_CHECKPOINT` pointed at one symbol per run — and eleven device runs. The question the
series answers is the one 381's silence and 382's report left open: **381's run produced no report at
all, while a checkpoint on the same image reported from inside `ipc_thread_terminate`.** Either the
silence is about code after that call, or the walk was never where 382's caller key said.

**Measured: every teardown frame returns, the walk crosses the context switch, and it climbs the new
thread's ladder to `ipc_thread_call_init`.** The instrument for a frame that has one call site is the
`_AFTER` variant — it calls the real function, records `cp_ret`, and reports *after* it returns — so a
report is "the call site was reached and the callee returned", and a silence, with the call site
already proved reached, is "the callee does not return".

## The eleven runs

| # | checkpoint | variant | measured |
| --- | --- | --- | --- |
| 383 | `io_free` | `_AFTER` | `cp_calls=1` `cp_ret=0x60000013`, caller `0x800d8380` = `ipc_port_destroy + 0x35C` — **`io_free` returns**, so 381's object works and `ipc_port_destroy`'s frame empties |
| 384 | `ipc_thread_terminate` | `_AFTER` | `cp_calls=1` `cp_ret=0x00000000`, caller `0x80009818` = `thread_deallocate + 0x64` — **the frame above it is `thread_deallocate`'s `bl` at `+0x60`**, not `task_deallocate`'s |
| 385 | `thread_block` | plain | silent |
| 386 | `thread_block` | `_SKIP=1` + `_AFTER` | `cp_calls=0x00000008` `cp_ret=0x00000000`, caller `0x80014048` = `lck_mtx_lock_contended + 0xB8` |
| 387 | `task_deallocate` | `_AFTER` | `cp_calls=1` `cp_ret=0xc044b678`, caller `0x800098AC` = **`thread_deallocate + 0xF8`**, the `bl task_deallocate` at `+0xF4` |
| 388 | `thread_deallocate` | `_AFTER` | `cp_calls=1` `cp_ret=0x801CF7EC`, caller `0x8000E4EC` = **`kernel_bootstrap + 0x36C`**, the `bl thread_deallocate` at `0x8000E4E8` |
| 389 | `machine_load_context` | plain | `stub_hit=machine_load_context`, caller `0x8000E884` = **`load_context + 0xE8`**, the `bl` at `0x8000E880` |
| 390 | `machine_load_context` | `_AFTER` | **silent** (294553 bytes) — it does not return, which is what a context switch looks like |
| 391 | `sched_startup` | plain | `stub_hit=sched_startup`, caller `0x8000E5B0` = **`kernel_bootstrap_thread + 0x30`**, the `bl` at `0x8000E5AC` |
| 392 | `sched_startup` | `_AFTER` | `cp_calls=1` `cp_ret=0xFFFFFFFF`, caller `0x8000E5B0` — it returned, so the boot thread's `thread_block(THREAD_CONTINUE_NULL)` was woken by the scheduler |
| 393 | `ipc_thread_call_init` | plain | `stub_hit=ipc_thread_call_init`, caller `0x8000E5F8` = **`kernel_bootstrap_thread + 0x78`**, the `bl` at `0x8000E5F4` |

## What the chain says, and the reading it corrects

**1. The 383–388 chain is one call, and it is the boot thread's own exit.** `thread_deallocate`'s
sole caller on this path is `kernel_bootstrap + 0x368`; `thread_deallocate`'s own body calls
`ipc_thread_terminate` at `+0x60` and `task_deallocate` at `+0xF4`; `ipc_thread_terminate` reaches
`ipc_port_dealloc_special` at `+0x18C`, which is 382's stop; and `io_free` is reached from
`ipc_port_destroy + 0x35C`, which is 380's stop. So 380, 382, 383, 384, 387 and 388 are **one straight
line inside a single `bl thread_deallocate` near the end of `kernel_bootstrap`**, and every frame on it
returns. The teardown is not a dying system's symptom: it is `kernel_bootstrap` handing the
boot thread's reference back before it hands the processor to the new thread.

**2. 382's by-elimination reading was right about the frame and wrong about where it is.** 382
concluded that 381's silence "lives at or after `ipc_thread_terminate + 0x18C`". The frame is right;
the address is not a frontier. `load_context` is reached *after* that call returns (389), the context
switch happens at `load_context + 0xE8` (390), and the walk continues in `kernel_bootstrap_thread` on
the same CPU — so everything 381 was silent about is **after** the teardown, not inside it.

**3. The one reading the series could not explain from its own numbers.** Run 386 was built with
`SKIP=1` (the build banner reads *"running call 2 for real and reporting its return value"*), and the
wrapper's arithmetic is `if (calls < SKIP) { calls++; return real(...); } calls++; report cp_calls=calls`
— so the report must carry `cp_calls=2`. It carries `cp_calls=0x00000008`. **The instrument's own
number cannot be produced by the instrument's own source**, and the same run's log also shows
`xnu_entry_kv_written=0x000000D7` against `xnu_entry_kv_in_dram=0x000000FB` — 215 records written,
251 found in DRAM, i.e. **36 records in the buffer that this run did not write**. Both readings point
the same way (the counter and the tail of the buffer are being read out of `.bss` that a previous boot
left behind, not out of this run's writes) and neither is explained here. Recorded rather than
explained away: `cp_calls` is the one key in the report whose value has not been seen to match the
build that produced it.

**4. What the chain establishes for the runs after it.** `machine_load_context`'s caller is
`load_context + 0xE8` and its body has no callee at all (`mrc`/`ldr`/`mcr`/`ldr`/`bx lr`), so the
context switch is the only thing that can be happening when 390 is silent. `sched_startup` reporting
from `kernel_bootstrap_thread + 0x30` and returning means the walk is on the *new* thread's stack and
the scheduler can wake a blocked boot thread. From there the ladder is a straight line of real calls
(`thread_daemon_init`, `vm_kernel_reserved_entry_init`, `thread_call_initialize`, `thread_bind`,
`ipc_thread_call_init`) — all confirmed stub-free by name against `xnu_arm_entry_realstubs.o`, not by
address.

## And the thing the series did not notice until 404

Since 381 the link has not changed, and since 381 **not one report has come from a stub**. Every one
of 382–393's reports came from a terminal checkpoint wrapper, and every silent run was the *same*
silent run: 294553 bytes, byte-identical, no `stub_hit`, no `exception:`, no epilogue. 381's silence
is not a second phenomenon to be explained by the teardown chain — it is the same event as 398's,
399's, 400's and 404's, and it is a **block**, not a missing symbol: the walk never reaches the first
stub the image still lacks, because it stops inside a real function that waits. Experiments 394–404
name it.

## Safety

Nothing was linked in this series: every run is the 381 image plus one wrapper, booted
non-persistently (`fastboot boot`, never flash) through the two gated scripts. No run reported an
`exception:` line or a `panic`; every run's log carries `persistent_write_attempted=0x00000000` and
`failure_mask=0x00000000` with no non-zero reading, `abort_entries=0`, and the device came back to
Android on its own each time and was confirmed there (`adb devices` shows `4a2fe00b`).
