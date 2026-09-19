# Experiment 361 — `vm_shared_region.c`: the first stop the scheduler hands the walk, and three slips in one closure band

**Step:** link one object, `osfmk/vm/vm_shared_region.c` (`osfmk_vm_vm_shared_region.o`) — the pool definer of
360's stop `vm_shared_region_init`. Nothing else changes.

**Prediction:** *13 resolved / 0 added — 803 → **790** undefined, 694 → **682** function, 109 → **108** storage;*
`.text` ends 0x801A31E0, **crossing `0x801A0000`, so `.data` steps a whole `ALIGN(0x4000)` to `0x801A4000`**;
`.sysctl_set` **0x801BD9B0**; `.init_array` **0x801BDB00** (0x78) ending 0x801BDB78; `.bss` **0x801BDB80**
(0x391D8); `__bss_end` **0x801F6D58**; image **1825656**; headroom **1086120**; `args` **+2064384**;
`topOfKernelData` **+3145728**; and the stop on the next stub the boot thread would meet — `ccdrbg_factory_yarrow`
through `read_random`'s factory pointer at key `0x8003827C`, with `throttle_init` at `bsd_init+0x8`, key
`0x8003A9FC`, named as the fallback.

**Result:** all three counts exact, every row exact but `.text` (0x801A3280 against a predicted 0x801A31E0, a
0xA0 miss with four separately-identified causes), the pad rule's **ninth** confirmation exact — and **the stop
is neither of the two the block named.** It is `stub_hit=compute_averages` at `xnu_entry_stub_caller_v=0x800a31f8`,
i.e. the `bl` at 0x800A31F4 = `sched_timeshare_maintenance_continue+0x108`, `abort_entries=0`.

## The stop: a thread the bootstrap created, entered by a context switch

`sched_timeshare_maintenance_continue` (`osfmk/kern/sched_prim.c:4755`) is the scheduler maintenance thread's
body: it is entered by `thread_block((thread_continue_t)sched_timeshare_maintenance_continue)` at
`sched_prim.c:4855`, and the thread itself was created by `sched_startup`'s
`kernel_thread_start_priority(sched_init_thread, …)` — which `kernel_bootstrap_thread` called at **0x8000E5AC**,
*before* `PE_init_iokit`. So the stop has **no calling frame above it at all**. The instrument's `caller − 4`
idiom still names the `bl` (`compute_averages` is called directly by that function), but the frame was entered
by a context switch, not by a call, and the code that ran before it is the `kernel_bootstrap_thread` line the
walk has been following since 355.

**This is a seventh frontier kind: a stop the scheduler hands you.** The preemption point is `spllo()`
(0x8000E6C4, `ml_set_interrupts_enabled`, real and inlined): before it the walk ran with interrupts off; after
it the boot is preemptible, and the maintenance thread the bootstrap had already created ran. The frontier kinds
therefore now include an event that is not on the walk's own call chain at all — and 362 is the step that shows
the same hand-off can deliver a *panic* instead of a stub.

Both of the block's predictions are refuted, and *why* is the useful part: `ccdrbg_factory_yarrow` was not
reached because the boot thread never got there before the switch, and `throttle_init` was not reached because
the boot thread had not got as far as `bsd_init` either. The block's error was structural rather than
arithmetic: **it walked the boot thread's call chain as if nothing else could run**, at exactly the step where
`spllo()` makes that assumption false. The question it should have asked is not "what does this code call next"
but "what runs next".

## The layout: every row but `.text` exact

| | 360 measured | 361 predicted | 361 measured |
|---|---|---|---|
| `.text` | 0x8019F460 | end 0x801A31E0 | **0x801A3280** (+0xA0) |
| `.data` | 0x801A0000 (0x19980) | **0x801A4000** | **0x801A4000** (0x199B0) |
| `.sysctl_set` | 0x801B9980 (0x150) | **0x801BD9B0** | **0x801BD9B0** (0x150) |
| `.init_array` | 0x801B9AD0 (0x78, 30) | **0x801BDB00** (0x78) | **0x801BDB00** (0x78, thirty) |
| its end | 0x801B9B48 | **0x801BDB78** | **0x801BDB78** |
| `.bss` | 0x801B9B80 | **0x801BDB80** | **0x801BDB80** |
| `.bss` size | 0x39198 | **0x391D8** | **0x391D8** (233944) |
| `__bss_end` | 0x801F2D18 | **0x801F6D58** | **0x801F6D58** |
| image | 1809224 | **1825656** | **1825656** |
| headroom | 1102568 | **1086120** | **1086120** |
| `args` | +2048000 | **+2064384** | **+2064384** |
| `topOfKernelData` | +3145728 | +3145728 (unmoved) | **+3145728** |

Everything structural was right: `.text` ended above 0x801A0000 and below 0x801A4000, so `.data` stepped by the
same mechanism as 357's step, and every row below it came out exactly as predicted. The 0xA0 miss is four
separate slips, each of which is now a measured rule:

| term | predicted | measured | why |
|---|---|---|---|
| 12 retired stub bodies | −0x1B0 | **−0x120** | arithmetic: 12 × 0x18 = 0x120, not 0x1B0 (which is 18 bodies) |
| 13 retired name slots | −0x148 | **−0x12C** | **a storage stand-in has no name slot**: the run is `sum(align4(len + 1))` over the 682 *function* names, and `realstubs.o`'s `.rodata.str1.4` came out 0x3AE4 = 0x3C10 − 0x12C |
| the object's strings | +0x088 | **+0x084** | its 0x85 *replaces* the 0x2 pad that used to sit between `IOEventSource.o`'s strings and `macho.o`'s piece, plus a 0x1 pad of its own — measured as `*fill* 0x8019EC97 0x1` |
| the tail's own fill | (not modelled) | **−0x008** | `__TEXT,__const`'s 0x4 bytes are followed by a fill that was 0xC at 360 and is 0x4 here, because the whole tail moved |

With those four, the closure is exactly the measured **+0x3E30** from 360's placed end 0x8019F450, and the section
ends at 0x801A3280 with `ALIGN(0x20)` adding nothing — the placed end lands on the boundary this time, where
360's did not and 361's prediction assumed it would. The block's own note that "the sum lands on the 32-byte
boundary exactly" was true of its arithmetic and false of the image.

`realstubs.o` closed on two of its three forms (`.text` **0x3FF0** = 682 × 0x18 at 0x80179BD4, `.bss` **0x28C4**
at 0x801F4480) and missed the third by exactly the name-slot slip (`.rodata.str1.4` **0x3AE4**, not 0x3AC8).

## The pad rule's ninth confirmation, from a pad of 0x28

The object's `.bss` is 0x70 and lands at 0x801F43D8, where `IOEventSource.o`'s 0x18 has just ended
(0x801F43C0 + 0x18, no fill). The run's tail then reads:

```
IOCommandGate.o 0x801F43A8 (0x18) / IOEventSource.o 0x801F43C0 (0x18) / this object 0x801F43D8 (0x70)
  / the three zero-size shims at 0x801F4448 / *fill* 0x801F4448 0x38 / realstubs.o 0x801F4480 (0x28C4)
```

The fill is exactly `align64(0x801F4448) − 0x801F4448`, so `new_pad = (0x28 − 0x70) mod 64 = 0x38` on the nose,
and the row closes to the byte:
`0x39198 − 0x40 (the retired storage stand-in) + 0x70 (the object) + 0x10 (the pad, 0x28 → 0x38) = 0x391D8`.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_kv_written=0x00000061   xnu_entry_kv_in_dram=0x00000085   xnu_entry_kv_dropped=0x00000000
 xnu_entry_why=0x8017e3ac          xnu_entry_why_byte=0x00000061     'a'
 xnu_entry_stub_caller_v=0x800a31f8   (also _a and _e, all three agreeing)
 xnu_entry_abort_entries=0x00000000   <- nothing faulted on the way
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=compute_averages
```

Safety, as every run: non-persistent `fastboot boot` of `stage90-qcdt.img`, nothing flashed, 25 records of
`persistent_write_attempted=0x00000000` and 87 of `failure_mask=0x00000000` with no non-zero reading of either,
`xnu_entry_checks=5` / `xnu_entry_failures=0`, log 301628 bytes whose last line is the kernel's own
`No errors detected`, and the device back on Android on its own (`MI 4LTE`, release 10).

## Frontier: 362 — `osfmk/kern/sched_average.c`

`osfmk_kern_sched_average.o` is the pool definer of this step's stop: **5 resolved (1 function, 4 storage) /
1 added** — `compute_averages` out, four storage stand-ins out (`avenrun` `B 0xC`, `mach_factor` `B 0xC`,
`sched_load_average` `B 0x4`, `sched_mach_factor` `B 0x4`) and `compute_averunnable` in — for 790 → **786**
undefined, 682 → **682 function, 108 → 104 storage**. The object is `.text` 0x48C, `.data` 0xA8, `.bss` 0x38, and
nothing else. Its `.bss` lands exactly where 361's `*fill* 0x38` was, so `new_pad = (0x38 − 0x38) mod 64 = 0` and
the tenth confirmation would be a third zero-fill. The predicted stop was `ccdrbg_factory_yarrow` again at key
`0x8003827C` — the boot thread's next stub once the maintenance thread blocks — with `throttle_init` at key
`0x8003A9FC` as the fallback. Both were refuted by an outcome of a third kind: the run panicked inside real
code (experiment 362).
