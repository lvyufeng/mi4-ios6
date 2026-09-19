# Experiment 312 — `kern_kpc.c`, and the first step to move `.data` and `__sysctl_set`

**Step:** link **`bsd/kern/kern_kpc.c`** (`out/xnu_kernel_obj/bsd_kern_kern_kpc.o`, manifest:39) — the
object that defines `kpc_init`, the name 311 stopped on.

**Prediction:** *`kpc_init`'s three lock calls are real, so the stop is its fourth call,
`kpc_arch_init`, at caller key `0x8010279C`* — with `kpc_common_init` at `0x801027A0` as the named
alternative. Counts: 1 resolved / **15 added**, 733 → 747 undefined and 644 → 658 function stubs.

**Result:** the run lands on the key exactly — `stub_hit=kpc_arch_init` at
`xnu_entry_stub_caller=0x8010279c` = `kpc_init + 0x48`. The counts are exact. **But the prediction
before the build said 19 added, not 15, and the four that were wrong are the step's real lesson.**

## The object

`bsd_kern_kern_kpc.o` is the largest step since 288, and the first one whose footprint is not just
`.text` and `.bss`:

| section | object size | linked |
|---|---|---|
| `.text` | 1796 | 1796 |
| `.bss` | 24 | 24 |
| `.rodata.str1.1` | 428 | **385** (0x181; 0x1AC before relaxing) |
| `.data` | 672 | 672 |
| `__DATA,__sysctl_set` | 56 | 56 |

with 33 definitions and 28 references. The `__sysctl_set` contributions are the 14
`__set___sysctl_set_sym_sysctl__kpc*` entries — the mechanism by which this object registers its
sysctl tree — and the `.data` is the 15 `sysctl__kpc*` nodes themselves.

## The build: counts exact, and four resolutions that were never in the undefined list

The baseline was built in this session with an **empty stand-in object** in this slot, reproducing 311
exactly (733 / 644 / 89, `.text` 0x11DA40, image 0x138AAC, bss 0x80138AC0..0x8016F918, headroom
1640168).

|  | predicted | measured |
|---|---|---|
| undefined / function / storage | 747 / 658 / 89 | 747 / 658 / 89 |
| `.text` | 0x11DA40 + 0x704 + 0x181 + 0x150 + 0x100 + fill | **0x11E500** (+0xAC0) |
| `.data` | 0x18A08 + 0x2A0 | **0x18CA8** (+0x2A0) |
| `.sysctl_set` | 0xA4 + 0x38 | **0xDC** (+0x38) |
| image | 0x138AAC + 0x2D8 | **0x138D84** |
| `__bss_start` | — | **0x80138DC0** (+0x300) |
| `.bss` size | — | **0x36E58 (unchanged)** |
| `__bss_end` | — | **0x8016FC18** (+0x300) |
| headroom | — | **1639400** (−0x300) |

**The count prediction before the build was 19 added, and the build said 15.** All four of the
difference are names this object references, that are defined by `osfmk_kern_kpc_thread.o` — an object
that has been in `LINK_OBJS` since long before this walk reached `kpc_init`:

```
kpc_thread_init              kpc_get_curthread_counters
kpc_get_thread_counting      kpc_set_thread_counting
```

I had read "absent from the undefined list" as "this image has never heard of it, so linking this
object will make it a stub". Absence means one of **two** things — already defined by something
already linked, or referenced by nothing yet — and only `nm` on the image, or the `LINK_OBJS` array,
tells them apart. 311 made the same inference in miniature (`panic_spin_forever`, predicted +1,
measured 0) and got the same correction from the build. Twice in two steps, from one cause.

### `.text`: five terms and a 0xC residual

```
  this object's .text                                  +0x704
  this object's .rodata.str1.1, linked                 +0x181   (0x1AC in the object)
  the stub object's .text, net                         +0x150   (1 body retired, 15 created)
  the stub object's .rodata.str1.4, net                +0x100   (1 name retired, 15 created)
  .text-region alignment fill                          -0x021
  residual, unattributed                               +0x00C
                                                      -------
                                                       +0xAC0
```

The residual is stated rather than papered over: the four named terms and the fill come from the map
and the two maps' input lists, and they do not close by 12 bytes. The `.rodata.str1.1` line is worth
its own sentence because the map prints both numbers:

```
 .rodata.str1.1  0x000000008011a57f  0x181  bsd_kern_kern_kpc.o
                                    0x1ac (size before relaxing)
```

— 43 bytes of the object's 428 are strings that already exist in the image, and the linker drops them.
That is the 301 lesson (`arm-none-eabi-size -A` on a *mergeable* section is an upper bound) arriving a
second time, and this time the map says so in the link itself.

### `.bss` did not grow at all, and it is the same alignment rule

This is the third case, and the three together are the whole rule:

| step | object `.bss` | fill in front of the stand-ins | `__bss_end` |
|---|---|---|---|
| 308 | 0x18 | 0x2C, of which 0x10 was the gap | **unmoved** |
| 310 | 0x1C | 0x10 | **+0x40** |
| 312 | 0x18 | 0x34 | **0** (section size unchanged) |

The gap in front of the 64-byte-aligned stub object is exactly `align64(end) - end`; an input that fits
consumes that much of the fill and moves nothing, and an input that does not pushes the whole 0x1704 of
stand-ins to the next boundary. The map line for this step, in one place:

```
 .bss  0x8016e498  0x18  osfmk_kern_kern_monotonic.o
 .bss  0x8016e4b0  0x1c  osfmk_device_device_init.o
 .bss  0x8016e4cc  0x18  bsd_kern_kern_kpc.o        <- this step's 24 bytes
 .bss  0x8016e4e4  0x0   xnu_arm_entry_rtabi.o
 *fill* 0x8016e4e4  0x1c
 .bss  0x8016e500  0x1704 xnu_arm_entry_realstubs.o
```

The fill that was `0x34` is now `0x1c` — exactly 0x18 less, which is this object's whole `.bss` — and
the stand-ins sit at the same offset from the section start (0x8016e500 − 0x80138dc0 = 0x8016e200 −
0x80138ac0 = 0x35740). So `__bss_start` and `__bss_end` both move by the **same** 0x300, which is
entirely `.data` + `.sysctl_set` growth plus the 64-byte alignment turn after them
(0xAC → 0xC0 is 0x14, 0x84 → 0xC0 is 0x3C; 0x2D8 + 0x28 = 0x300). Headroom is
`topOfKernelData − __bss_end`, so it loses the same 0x300.

## The run

```
MI4IOS6_STAGE90_XNU disarm_hw_watchdog_en=0x00000001
MI4IOS6_STAGE90_XNU xnu_entry_entering_at=0x80000074
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_stub_caller_v=0x8010279c   xnu_entry_stub_caller_e=0x8010279c
 xnu_entry_abort_entries=0x00000000   xnu_entry_failures=0x00000000
 xnu_entry_bss_start=0x80138dc0       xnu_entry_bss_end=0x8016fc18
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=kpc_arch_init
```

`tools/host_resolve_entry_addr.sh 0x8010279c` → `kpc_init+0x48`, the `bl` at `0x80102798`. The linked
body, and it is the first frontier in a long time that is *inside* the object the step links:

```
80102758  bl lck_grp_attr_alloc_init
80102774  bl lck_grp_alloc_init
80102794  bl lck_mtx_init
80102798  bl kpc_arch_init        <- THE STOP (lr 0x8010279c)
8010279c  bl kpc_common_init      <- the named alternative
801027a0  bl kpc_thread_init      <- real: kpc_thread.o is in LINK_OBJS
```

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED, no storage symbols in the
payload), log 301625 bytes, one `stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`, and the device returned to
Android on its own (`ro.build.version.release` = 10).

## What it measures: three locks, a lock group, and a sysctl tree

`kpc_init`'s first three statements all ran and returned:

```c
	sysctl_lckgrp_attr = lck_grp_attr_alloc_init();
	sysctl_lckgrp = lck_grp_alloc_init("kpc", sysctl_lckgrp_attr);
	lck_mtx_init(&sysctl_lock, sysctl_lckgrp, LCK_ATTR_NULL);
```

So a lock group attribute, a lock group named `"kpc"` and a mutex over it now exist in the kernel,
allocated out of the real zone machinery, with the group name coming from the new object's
`.rodata.str1.1` rather than from a zeroed stand-in. And the object's `__sysctl_set` contributions are
the registration entries for the 15 `sysctl__kpc*` nodes — the KPC subsystem's sysctl tree is now part
of the kernel's set, which is a *structural* change no earlier step has made: every previous step
added code or a lock, this one adds a set of entries the sysctl walker will visit.

## What it does not measure

* **That the sysctl tree is walkable.** The entries are registered; nothing has enumerated them. The
  first `sysctl_register_oid`-style traversal is further down the bootstrap.
* **Which thread ran.** 307's question is still open, and 309–312 have not touched it.
* **That `kpc_arch_init` is harmless.** Its body is two instructions, which the next step's build
  shows — but "two instructions" was read from the *object*, and only the linked image confirms it.

## Next

**`osfmk/arm/kpc_arm.c`** (`osfmk_arm_kpc_arm.o`, manifest:442) — the object that defines
`kpc_arch_init`: `.text` **3916**, `.rodata.str1.1` 42, `.bss` **184**, 33 definitions and 19
references.

It resolves **6** — `kpc_arch_init` (this stop), `kpc_get_classes`, `kpc_get_pmu_version`, `kpc_idle`,
`kpc_idle_exit`, `kpc_set_sw_inc` — and adds **7** (`kpc_actionid`, `kpc_controls_fixed_counters`,
`kpc_get_curcpu_counters`, `kpc_popcount`, `kpc_sample_kperf`, `PE_cpu_perfmon_interrupt_enable`,
`PE_cpu_perfmon_interrupt_install_handler`), so 747 → **748** undefined and 658 → **659** function
stubs, storage unchanged. Of the 19 references, 11 are already real (`_consume_kprintf_args`,
`cpu_broadcast_xcall`, `cpu_datap`, `cpu_number`, `current_processor`, `hw_atomic_add`,
`hw_atomic_sub`, `ml_set_interrupts_enabled`, `panic`, `real_ncpus`, `thread_wakeup_prim`) and one,
`kpc_get_counter_count`, is already a stub.

**And the stop is predicted one call past the object's own entry point**, because in the object
`kpc_arch_init` is:

```
00000150 <kpc_arch_init>:
 150:	ee190f1c 	mrc	15, 0, r0, cr9, cr12, {0}
 154:	e12fff1e 	bx	lr
```

— a read of PMCR (performance monitor control register, `p15, 0, c9, c12, 0`) and a return, with the
value discarded. So the next stop is **`kpc_common_init` at caller key `0x801027A0`**
(`bl kpc_common_init` at `0x8010279c`, the very instruction after the one this run stopped on), and the
step is predicted to be a *small* one despite the object being 3916 bytes of `.text`: the frontier
moves by one call, not into the ARM PMU code the object mostly is.
