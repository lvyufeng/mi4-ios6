# Experiment 315 — `kern_ktrace.c`, and a prediction that named the caller key exactly

**Step:** link **`bsd/kern/kern_ktrace.c`** (`out/xnu_kernel_obj/bsd_kern_kern_ktrace.o`, manifest:40) —
the object that defines `ktrace_init`, the name 314 stopped on.

**Prediction:** *`ktrace_init` is four real lock calls plus an `assert`, so it returns — and the stop then
leaves `kernel_bootstrap_thread`'s straight line, because `kdebug_init` is a one-instruction trampoline
into `kdebug_trace_start`, which calls four of the eleven names this step resolves at six call sites.
Predicted stop: `sysctl_early_init`, caller key `0x8000E6A4`.* Plus counts: **11 resolved / 4 added**,
726 → 719 undefined and 637 → 630 function stubs.

**Result:** both halves land. `stub_hit=sysctl_early_init` at `xnu_entry_stub_caller=0x8000e6a4` =
`kernel_bootstrap_thread + 0x124` — **the predicted name and the predicted key**, and the key is the
interesting part, because it is not an address inside `bsd_early_init` at all.

## The object, and counts that were right twice running

`bsd_kern_kern_ktrace.o` is `.text` **2564** / `.bss` 82 / `.rodata.str1.1` 250 / `.data` 288 /
`__DATA,__sysctl_set` **24**, with 47 defined symbols and 25 references.

The baseline was built in this session with an **empty stand-in object** in this slot, reproducing 314
exactly (726 / 637 / 89, `.text` 0x1210A0, `.data` 0x80124000 size 0x18D20, `.sysctl_set` 0x8013CD20 size
0xDC, `.bss` 0x8013CE00 size 0x36F58, headroom 1622696).

|  | predicted | measured |
|---|---|---|
| undefined | 719 | **719** |
| function stubs | 630 | **630** |
| storage stubs | 89 | **89** |
| resolved / added | 11 / 4 | **11 / 4** |

The 11 resolved are `ktrace_init` (this stop) plus `ktrace_assert_lock_held`, `ktrace_configure`,
`ktrace_end_single_threaded`, `ktrace_get_owning_pid`, `ktrace_kernel_configure`, `ktrace_lock`,
`ktrace_read_check`, `ktrace_reset`, `ktrace_start_single_threaded`, `ktrace_unlock` — all functions. The
4 added are `kperf_reset`, `kperf_sampling_disable`, `ktrace_background_available_notify_user` and
`sysctl_handle_string`, and all four are functions because none has a defining object in this pool, so the
generator has no size to take from one (the 304 rule).

## `.text` closes exactly, and the fill term had to be read per region

`.text` 0x1210A0 → **0x121A60** is +0x9C0:

```
  this object's .text                                  +0xA04   (2564)
  this object's .rodata.str1.1, linked                 +0x0D7   (0xFA in the object: 35 bytes relaxed)
  the stub object's .text, net                         -0x0A8   (11 bodies retired, 4 created)
  the stub object's .rodata.str1.4, net                -0x080   (11 names retired = 0xE4 padded,
                                                                 4 created = 0x64)
  .text-region alignment fill                          +0x00D
                                                       -------
                                                        +0x9C0
```

The map's own numbers agree line for line: `.text` non-fill 0x1203AB → 0x120D5E (+0x9B3 = the first four
terms) and `.text` fill 0xD0D → 0xD1A (+0xD).

**The fill sign is worth its own sentence, because reading the wrong total gets it wrong.** The whole
map's fill total *fell* by 5 (0x88C8 → 0x88C3) while `.text`'s fill *rose* by 0xD — because `.bss`'s fill
fell by 0x12. A prediction that took the total as `.text`'s term would be off by 0x18 and would have to
absorb it as a residual. 314's ledger drew exactly the opposite lesson (there, a printed term was 4 bytes
too big); this one is the same family: **a fill term is a property of a region, and the map's total is not
the region's.**

## `.data`, `.sysctl_set`, and `.bss`

`.data` grows by exactly its section and nothing steps this time: 0x18D20 → **0x18E40** (+0x120 = 288) at
the same start 0x80124000. `.sysctl_set` moves 0x8013CD20 → **0x8013CE40** and grows 0xDC → **0xF4**
(+0x18 = 24), both following `.data`'s end.

`.bss` is the alignment rule again, and this input is *split across the gap*: the object's 0x52 lands
where 314's zero-sized sections and 0x20 of fill were (offset 0x35820 from the section start), its 0x52
overflows that, and the 64-byte-aligned stand-in block moves +0x40:

```
 .bss   0x80172760  0x52  bsd_kern_kern_ktrace.o      <- this step's 82 bytes
 .bss   0x801727b2  0x0   rtabi.o / macho.o
 *fill* 0x801727b2  0xe
 .bss   0x801727c0  0x1704 xnu_arm_entry_realstubs.o  <- 0x35880 from the section start,
                                                         against 0x35840 in 314's map
```

So the section grows by 0x52 − 0x12 = **0x40** (non-fill +0x52, fill −0x12), and the 0xE of fill is exactly
`align64(0x801727b2) − 0x801727b2`. Unlike 314 no storage stand-in retires — all eleven resolutions are
functions — so nothing cancels and the 0x40 is real growth.

| | base (314) | measured (315) | delta |
|---|---|---|---|
| `.text` | 0x1210A0 | **0x121A60** | +0x9C0 |
| `.data` | 0x80124000 (0x18D20) | 0x80124000 (**0x18E40**) | +0x120 |
| `.sysctl_set` | 0x8013CD20 (0xDC) | 0x8013CE40 (**0xF4**) | +0x18 |
| `.bss` | 0x8013CE00 (0x36F58) | **0x8013CF40** (**0x36F98**) | +0x140 / +0x40 |
| image | 0x13CDFC | **0x13CF34** | +0x138 |
| `__bss_end` | 0x80173D58 | **0x80173ED8** | +0x180 |
| headroom | 1622696 | **1622312** | −0x180 |

`__bss_start`'s +0x140 is 0x120 (`.data`) + 0x18 (`.sysctl_set`) + 0x8 (the alignment after
`.sysctl_set` grew from a 4-byte to a 12-byte residue); `__bss_end` adds the 0x40 of `.bss`.

## The run

```
MI4IOS6_STAGE90_XNU disarm_hw_watchdog_en=0x00000001
MI4IOS6_STAGE90_XNU xnu_entry_entering_at=0x80000074
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_stub_caller_v=0x8000e6a4   xnu_entry_stub_caller_e=0x8000e6a4
 xnu_entry_abort_entries=0x00000000   xnu_entry_failures=0x00000000
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=sysctl_early_init
```

`tools/host_resolve_entry_addr.sh 0x8000e6a4` → `kernel_bootstrap_thread+0x124`, `caller − 4` = 0x8000e6a0
= `bl bsd_early_init`.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED, no storage symbols in the payload),
log **301629** bytes, one `stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`, and the device returned to
Android on its own (`ro.build.version.release` = 10).

## Why the caller key is the bootstrap thread's own `bl`

`bsd_early_init` is **one instruction**:

```
8003a9e0 <bsd_early_init>:  b 80108870 <sysctl_early_init>
8003a9e4:                   nop
```

A `b` does not set `lr`, so when `sysctl_early_init`'s stub reports the register it was entered with, the
value is still the return address of the call the *bootstrap thread* made — 0x8000e6a4. The prediction
said 0x8000E6A4 and not 0x8003A9E4 for that reason, and the run distinguishes them: a `bl` there would
have reported 0x8003A9E4. This is 313's tail-branch note arriving as a measurable difference rather than a
caveat: **for a one-instruction trampoline the stub's caller key names the caller one level up**, and the
key is therefore evidence about which function actually called in.

The same shape is why the stop is where it is. The chain the boot took:

```
8000e65c  bl ktrace_init            <- 314's stop; now real, ran and returned
8000e674  bl kdebug_init            -> b kdebug_trace_start
                                       (six call sites into the eleven names this step resolves)
8000e690  bl prng_cpu_init          <- ran: kalloc_canblock, ml_get_timebase, cc_clear,
                                       lck_grp_attr_alloc_init, lck_grp_alloc_init,
                                       lck_attr_alloc_init, lck_mtx_alloc_init, thread_wakeup_prim
8000e6a0  bl bsd_early_init         -> b sysctl_early_init   <- THE STOP, lr 0x8000e6a4
```

## What it measures: real code, running, for the first time in a while

This step is not one call wide. Everything above ran and returned:

* **`ktrace_init`'s body** — `lck_grp_attr_alloc_init`, `lck_grp_alloc_init("ktrace", …)`,
  `lck_grp_attr_free`, `lck_mtx_alloc_init` — so a lock group named `"ktrace"` now exists in the kernel,
  its attribute was allocated and freed, and the tracing mutex was allocated and stored. The
  `assert(ktrace_mtx != NULL)` did not fire, which is a real check with a real panic behind it.
* **`kdebug_trace_start`** — reached through a one-instruction trampoline, and its six call sites into
  `ktrace_start_single_threaded`, `ktrace_kernel_configure`, `ktrace_assert_lock_held` (three times) and
  `ktrace_end_single_threaded` all resolved and returned. That is the largest body of *ktrace* code this
  walk has executed: a single-threaded-tracing state machine ran its start, configure, lock assertions and
  end. It is the first step whose evidence includes a whole subsystem's control flow rather than one
  function's straight line.
* **`prng_cpu_init`** — a per-CPU PRNG initialization with no stub in its body.

## What it does not measure

* **That any of that control flow did anything.** `kdebug_trace_start` ran with `kdebug_enable` at
  whatever value the boot has left it; whether tracing is on, off, or bounded is a question about values,
  not about the frontier, and this run reports only that the code returned.
* **That `ktrace_mtx` is usable.** It was allocated out of the real zone machinery and asserted non-NULL;
  nothing has locked it.
* **Anything about `kperf_reset`, `kperf_sampling_disable`,
  `ktrace_background_available_notify_user` or `sysctl_handle_string`.** They are the four names this step
  *added*: linked as reporting stubs, and reached by nothing so far. `sysctl_handle_string` is the
  interesting one — the next step's object defines it, so it lasts exactly one experiment.

## Next

**`bsd/kern/kern_newsysctl.c`** (`bsd_kern_kern_newsysctl.o`, manifest:46) — the object that defines
`sysctl_early_init`: `.text` **8132** / `.bss` 28 / `__DATA,__data` 96 / `.rodata.str1.1` 71 / `.data`
**240** / `__DATA,__sysctl_set` **20**, with 59 defined symbols and 35 references.

`sysctl_early_init`'s body is four calls and all four are already real:

```c
	sysctl_lock_group  = lck_grp_alloc_init("sysctl", NULL);
	sysctl_geometry_lock = lck_rw_alloc_init(sysctl_lock_group, NULL);
	sysctl_unlocked_node_lock = lck_mtx_alloc_init(sysctl_lock_group, NULL);
	sysctl_register_set("__sysctl_set");
```

— and `sysctl_register_set` is *defined by this object* (0x24C), so it is a definition the step gains
rather than a reference that becomes a stub.

Predicted **6 resolved** (`sysctl_early_init`, `sysctl_handle_int`, `sysctl_handle_quad`,
`sysctl_handle_string`, `sysctl_io_number`, and `sysctl__children` **as storage**) and **6 added**, of
which only four are functions: `fuulong`, `suulong`, `mac_system_check_sysctlbyname` and `proc_suser` are
`T` in this pool, while **`securelevel` and `sysctl__sysctl_children` are `B`** and arrive as 4-byte
storage stand-ins. That is 719 → **719** undefined (unchanged: −6 + 6), 630 → **629** function stubs and
89 → **90** storage.

Predicted stop: **`StartIOKit`, caller key `0x80004A20`** — `PE_init_iokit` at 0x8000e6b0 contains exactly
one stub call, a conditional `bl StartIOKit` at 0x80004a1c. The named alternative is that a conditional
branch decides otherwise: `tools/xnu_entry_callwalk.py --root PE_init_iokit` reports **no stub on its
straight-line path** and lists the indirect calls it could not follow (`getval`,
`panic_trap_to_debugger`, `__doprnt` — a kprintf path), so a run that stops earlier is possible and the
tool says so rather than guessing.
