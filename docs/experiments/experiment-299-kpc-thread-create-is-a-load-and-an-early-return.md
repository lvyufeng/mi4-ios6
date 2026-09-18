# Experiment 299 — `kpc_thread_create` is a load and an early return, and the stop moves one call down

**Step:** link `osfmk/kern/kpc_thread.c` (`osfmk_kern_kpc_thread.o`), the one object that defines
`kpc_thread_create`.
**Prediction:** that the function returns immediately — its first instruction is a load of a variable
the same object defines — and that the stop therefore moves to the next stub in
`thread_create_internal`, `sched_set_thread_base_priority`, at `xnu_entry_stub_caller=0x8000b090`.
**Result:** exactly that, which also measures that the one real indirect call between the two stubs
completed.

## The object is the whole prediction

The 298 run stopped at `stub_hit=kpc_thread_create`, `thread_create_internal + 0x348`. The object
that defines it has been built by this project all along — `osfmk/kern/kpc_thread.c`, manifest:563,
`out/xnu_kernel_obj/osfmk_kern_kpc_thread.o` — and its function is eight instructions:

```
368: push  {r4, lr}
36c: mov   r4, r0
370: movw/movt r0, kpc_threads_counting      <- `B`, 4 bytes, defined in this same object
378: ldr   r0, [r0]
37c: cmp   r0, #0
380: popeq {r4, pc}                           <- the early return
384: bl    kpc_counterbuf_alloc
388: str   r0, [r4, #760]                     <- thread->kpc_buf
38c: pop   {r4, pc}
```

`kpc_threads_counting` is `int kpc_threads_counting = 0;` (`kpc_thread.c:51`), so linking the object
does not *stand in* for a zero — **it defines the variable**, in `.bss`, and the branch at 0x380 is
taken. The only writer is `kpc_set_thread_counting`, which is reached from the sysctl tree and from
nowhere else in this image. So this step's function is answered by a function that calls nothing, and
the interesting content of the step is what it proves about its neighbours.

## Two silent things the object brings

* `kpc_counterbuf_alloc`, `kpc_counterbuf_free`, `kpc_get_counter_count`, `kpc_get_cpu_counters` and
  `act_set_kperf` are all **not** in this object — they are in `kpc_common.c`/`kpc_arm.c` — so they
  arrive as five new stubs, none of which is reached. The step is 4 resolved and 5 added, and the
  imbalance is the 250/270 shape: the object also *defines* ten names the link never asked for.
* `kpc_thread_init` is in the object and nothing calls it, so `kpc_thread_lock` (8 bytes of the
  object's 0x20 of `.bss`) is a lock that is never initialised. That is safe here for a reason worth
  stating rather than assuming: the function that does run, `kpc_thread_create`, does not take it.
  It is [[mi4-stand-in-size-is-not-value]] in a milder form — real storage whose initializer has not
  run — and it stays harmless for exactly as long as nothing calls `kpc_get_thread_counting` or
  `kpc_set_thread_counting`.

## The prediction is a table of what is *between* the two stubs

`thread_create_internal` is in `osfmk_kern_thread.o`, which links before this step's object, so its
base is `0x8000acec` before and after. In address order after 298's stop:

| offset | call | |
|---|---|---|
| +0x348 | return site of `bl kpc_thread_create` | 298's stop |
| +0x350 | `SCHED(initial_thread_sched_mode)` via `blx r1`, `r1 = *(sched_multiq_dispatch + 0x4c)` | **real since 262** |
| +0x3a4 | `sched_set_thread_base_priority` | **stub, unguarded** — the stop |
| +0x3dc | `sched_thread_mode_demote` | stub, behind `cmp r1, #4 / bgt` |

The prediction is therefore not "the next stub in address order". There is one real indirect call in
between, through the `sched_multiq_dispatch` data table 262 linked, and the run's evidence that it
completed is that the stop is *past* it. The guard on the third stub is the same one 296 read before
it ran — `ldr r1, [sl, #0x50]` is `parent_task->max_priority`, 95 for the kernel task, and the `bgt`
was taken.

## The build

| | predicted | measured |
|---|---|---|
| undefined / function / storage | 773 / 684 / 89 | 773 / 684 / 89 |
| `.data` | 0x80118000 | 0x80118000 |
| `__bss_start` | 0x80130a00 | 0x80130a00 |
| `__bss_end` | 0x801677b8 | **0x801677d8** |
| `.text` | 0x116360 → ~0x11678c | **0x116780** (+0x420) |
| image | ~1248768 | **1247700** (unchanged) |
| stop | `sched_set_thread_base_priority` at 0x8000b090 | same |

**The image did not move**, because the 0x420 of new `.text` fitted into the padding in front of the
16-KB-aligned `.data` — `__entry_data_start` is 0x80118000 and `.text` now ends at 0x80116780.

### The one miss, and it is a rule about slots rather than sizes

`__bss_end` was predicted to drop 0x20 and did not move at all. `.bss` here is not a sum of sizes, it
is a sum of *aligned slots*, and the map file shows both halves of the trade being exactly 0x40:

* **retired:** `kpc_off_cpu_active`'s stand-in is `uint8_t [0x4] __attribute__((aligned(64)))` —
  the generator in `build_entry.sh`, and the 64 is why *every* storage name costs a 64-byte slot.
  Removing it shrinks `realstubs.o`'s `.bss` from **0x1744 to 0x1704**: 0x40 freed, for a 4-byte array.
* **added:** `osfmk_kern_kpc_thread.o`'s `.bss` is 0x20 (four 4-byte words and the 8-byte
  `kpc_thread_lock`), but `realstubs.o`'s arrays are 64-byte aligned, so the object costs 0x20 of its
  own plus 0x20 of extra fill in front of them: **0x40 spent**.

0x40 freed and 0x40 spent, so `__bss_end` is 0x801677d8 before and after — the same address from two
independent 0x40s. The prediction subtracted sizes; **the rule is that a `.bss` change is a slot
change.** This is the same family as 293's "function stubs have no `.bss` slot at all": the layout is
made of alignments, and an object's cost in it is not its size.

## The run

```
stub_hit=sched_set_thread_base_priority    xnu_entry_stub_caller=0x8000b090
xnu_entry_bss_start=0x80130a00             (unchanged)
xnu_entry_bss_bytes=0x00036dd8             (unchanged)
xnu_entry_copied_bytes=0x001309d4          (unchanged)
```

`0x8000b090` is `thread_create_internal + 0x3a4`, the return address of the `bl` at 0x8000b08c —
the predicted address, on the predicted stub. **Three things that are not stubs were therefore
measured by completing:** the `bl kpc_thread_create` returned; `kpc_thread_create`'s `cmp`/`popeq` on
`kpc_threads_counting` took the early return, so the variable the object defines is zero at boot; and
the `blx r1` through `sched_multiq_dispatch + 0x4c` entered and returned from the real
`sched_multiq_initial_thread_sched_mode`. The third of 296's three consecutive stubs in this function
is answered by a function that calls nothing.

Preflight clean (`loader_xnu_entry_stub_status=0x90000001`, `high_va_data_verified=0x00000001`), log
301131 bytes, one `stub_hit=` line and no `exception:` line.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`getprop ro.build.version.release` = 10).

**Next:** experiment 300 — `osfmk/kern/priority.c` for `sched_set_thread_base_priority` and
`sched_thread_mode_demote`, which closes `thread_create_internal`'s own body; then
`thread_policy_create` (real since 294) and the rest of the success path runs to `return new_thread`,
and `kernel_thread_create` hands a real thread back to `kernel_bootstrap`, which calls
`thread_deallocate` and branches to `load_context` at +0x380 — the first time this walk crosses into
a context switch rather than a function call. When the step that links `bsd/kern/kern_sysctl.c`
arrives, `entry_macho.s` gets the `__DATA,__sysctl_set` section entry described in 298.
