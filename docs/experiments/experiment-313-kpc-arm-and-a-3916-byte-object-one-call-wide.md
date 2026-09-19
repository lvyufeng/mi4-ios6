# Experiment 313 — `kpc_arm.c`, and a 3916-byte object whose step is one call wide

**Step:** link **`osfmk/arm/kpc_arm.c`** (`out/xnu_kernel_obj/osfmk_arm_kpc_arm.o`, manifest:442) — the
object that defines `kpc_arch_init`, the name 312 stopped on.

**Prediction:** *`kpc_arch_init` is two instructions in the object (`mrc p15, 0, r0, cr9, cr12, {0}`
then `bx lr`), so it returns and the stop is the next call — `kpc_common_init` at caller key
`0x801027A0`.* Plus counts: 6 resolved / 7 added, 747 → 748 undefined and 658 → 659 function stubs.

**Result:** the key lands exactly — `stub_hit=kpc_common_init` at
`xnu_entry_stub_caller=0x801027a0` = `kpc_init + 0x4c`. The resolutions are exact. **The added
count is right and its split is wrong**: 6 functions and **1 storage**, so the function-stub count
does not move at all.

## The object, and why its size is almost irrelevant

`osfmk_arm_kpc_arm.o` is `.text` **3916** / `.rodata.str1.1` 42 / `.bss` **184**, with 45 definitions
and 38 references. Nearly all of that text is the ARM PMU half of KPC — `kpc_pmi_handler`,
`kpc_get_fixed_counters`, `kpc_get_configurable_counters`, the four `kpc_*_xcall` cross-call stubs,
`kpc_set_config_arch`, `kpc_set_period_arch`, `kpc_set_running_arch` — none of which this run reaches,
because the one function `kpc_init` calls is:

```
00000150 <kpc_arch_init>:
 150:	ee190f1c 	mrc	15, 0, r0, cr9, cr12, {0}
 154:	e12fff1e 	bx	lr
```

`mrc p15, 0, r0, c9, c12, 0` reads PMCR — the performance monitor control register — into `r0`, and
then the function returns with the value **discarded**. So the barest possible reading of this step is
that 3916 bytes of object buy two instructions of execution.

## The build: counts exact, split not

The baseline was built in this session with an **empty stand-in object** in this slot, reproducing 312
exactly (747 / 658 / 89, `.text` 0x11E500, image 0x138D84, bss 0x80138DC0..0x8016FC18, headroom
1639400).

|  | predicted | measured |
|---|---|---|
| undefined | 748 | 748 |
| function stubs | **659** | **658** |
| storage stubs | 89 | **90** |
| `__bss_end` | — | 0x8016FD18 (+0x100) |
| headroom | — | 1639144 (−0x100) |

Resolved **6** — `kpc_arch_init` (this stop), `kpc_get_classes`, `kpc_get_pmu_version`, `kpc_idle`,
`kpc_idle_exit`, `kpc_set_sw_inc` — and added **7**, and *which* seven is where the prediction was
wrong. `kpc_actionid` is a **variable** in `kpc_common.c`, and the generator sizes a storage stand-in
from the real definition:

```c
uint8_t kpc_actionid[0x18] __attribute__((aligned(64)));
```

so one of the seven arrives as 24 bytes of `.bss` rather than as a 24-byte stub body. The other six are
functions. That is why the function count does not move: **−6 retired + 6 created = 0**, while two
counters move — 7 new names, 6 of one kind and 1 of the other. The generated stub object says it in one
line each:

```c
uint8_t kpc_actionid[0x18] __attribute__((aligned(64)));
void kpc_controls_fixed_counters(void) { entry_stub_hit("kpc_controls_fixed_counters", ...); }
```

### `.text` closes exactly, with no residual

```
  this object's .text                                  +0xF4C   (3916)
  this object's .rodata.str1.1                         +0x02A   (42, no relaxing needed)
  the stub object's .text, net                         +0x000   (6 bodies retired, 6 created)
  the stub object's .rodata.str1.4, net                +0x044   (6 names retired, 6 created)
  .text-region alignment fill                          +0x006   (53 fills -> 54)
                                                      -------
                                                       +0xFC0
```

which is unlike 312, where 0xC of the sum went unattributed — here the map's terms account for every
byte. The name-string term is worth its own line: the six retired names (`kpc_arch_init` 16,
`kpc_get_classes` 16, `kpc_get_pmu_version` 20, `kpc_idle` 12, `kpc_idle_exit` 16, `kpc_set_sw_inc` 16 =
0x60) and the six created ones (`kpc_controls_fixed_counters` 28, `kpc_get_curcpu_counters` 24,
`kpc_popcount` 16, `kpc_sample_kperf` 20, `PE_cpu_perfmon_interrupt_enable` 32,
`PE_cpu_perfmon_interrupt_install_handler` 40 = 0xA0) differ by 0x40, and the measured 0x44 carries 4
bytes of padding.

### `.bss` grew by exactly its inputs, and the rule predicts it without help

```
 .bss   0x8016e4cc  0x18  bsd_kern_kern_kpc.o          <- 312's
 .bss   0x8016e4e8  0xb8  osfmk_arm_kpc_arm.o         <- this step's 184 bytes
 *fill* 0x8016e5a0  0x20
 .bss   0x8016e5c0  0x1744 xnu_arm_entry_realstubs.o  <- 0x1704 + 0x40: one more stand-in
```

0xB8 (this object) + 0x8 (alignment, 0x8016e4e4 → 0x8016e4e8) + 0x40 (the new 64-byte-aligned
`kpc_actionid` slot) = **0x100**, which is the measured section growth. It is the same three-case rule
308 / 310 / 312 established — 308 and 312 are the cases where the input fitted in the fill in front of
the stand-ins; this is a 310-shaped case where it did not, so the block moved to the next 64-byte
boundary *and* gained a slot of its own. `__bss_start` does not move (nothing above it changed), so
`__bss_end` and the headroom move by exactly the 0x100.

## The run

```
MI4IOS6_STAGE90_XNU disarm_hw_watchdog_en=0x00000001
MI4IOS6_STAGE90_XNU xnu_entry_entering_at=0x80000074
MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
MI4IOS6_STAGE90_XNU real XNU entry: a symbol this image does not provide was called
 xnu_entry_stub_caller_v=0x801027a0   xnu_entry_stub_caller_e=0x801027a0
 xnu_entry_abort_entries=0x00000000   xnu_entry_failures=0x00000000
 xnu_entry_bss_start=0x80138dc0       xnu_entry_bss_end=0x8016fd18
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=kpc_common_init
```

`tools/host_resolve_entry_addr.sh 0x801027a0` → `kpc_init+0x4c`, the `bl` at `0x8010279c` — the
instruction immediately after the one 312 stopped on.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED, no storage symbols in the
payload), log 301627 bytes, one `stub_hit=` line, **no `exception:` line**.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, `xnu_entry_abort_entries=0x00000000`, and the device returned to
Android on its own (`ro.build.version.release` = 10).

## What it measures, and it is mostly what it makes possible

The stop is one call after the object's entry point, so the six names this step resolves are six names
the boot has *acquired*, not six it has exercised: `kpc_idle`, `kpc_idle_exit` and `kpc_set_sw_inc` in
particular will not be called until KPC is configured and a CPU idles, which is a long way off. The
honest statement of this step's measured content is its storage: **184 bytes of real PMU state** now
exist as correctly sized variables instead of nothing at all —

```
saved_PMOVSR   saved_PMCNTENSET   saved_PMXEVTYPER   saved_counter
kpc_running_classes   kpc_running_cfg_pmc_mask   kpc_enabled_counters
kpc_xcall_sync   kpc_xread_sync   kpc_reload_sync   kpc_config_sync
kpc_configured   first_time
```

— and the difference matters for the same reason `mi4-stand-in-size-is-not-value` exists: a stand-in is
a zeroed array of the right size, and *these* are the variables `kpc_pmi_handler` and the xcall
machinery will synchronise through. Whether their initial values are right is a question no run has
asked yet.

## What it does not measure

* **That the PMU exists, or that `mrc` returned anything meaningful.** The value is discarded in the
  same function, so the run cannot report it. A future step that stores it would make the `kpc_pmu_version`
  path measurable; this one does not.
* **Whether 3916 bytes of untested PMU code is safe.** It is linked and unreached. That is fine while
  the walk is on the bootstrap thread, and it becomes a live question the moment anything calls
  `kpc_get_config` or installs `kpc_pmi_handler`.

## Next

**`osfmk/kern/kpc_common.c`** (`osfmk_kern_kpc_common.o`, manifest:562) — the object that defines
`kpc_common_init`: `.text` **8072** / `.bss` 64 / `.rodata.str1.1` 4 / `__DATA,__data` **120**, with 45
definitions and 38 references.

Its body is three real lock calls, one of which is a tail branch:

```c
kpc_common_init(void)
{
	kpc_config_lckgrp_attr = lck_grp_attr_alloc_init();
	kpc_config_lckgrp = lck_grp_alloc_init("kpc", kpc_config_lckgrp_attr);
	lck_mtx_init(&kpc_config_lock, kpc_config_lckgrp, LCK_ATTR_NULL);
}
```

so like 313 it is predicted **not** to stop inside the object. It resolves **23** names — the whole
`kpc_*` API the last three steps have been adding stubs for — and is predicted to add **1**
(`kperf_sample`; of its 38 references every other one is either already real in this image, including
`kdebug_enable` and `machine_info`, which are `B` in the entry `.elf` and are *not* stand-ins, or
already a stub).

If that holds, `kpc_init`'s body is then complete — its last call, `kpc_thread_init`, has been real
since `osfmk_kern_kpc_thread.o` entered `LINK_OBJS` — so the step should end with **`kpc_init`
returning** and `kernel_bootstrap_thread` moving on to **`ktrace_init`, caller key `0x8000E660`**
(`bl ktrace_init` at `0x8000e65c`, the first stub after `kpc_init` in that function's straight line —
read off the image's own disassembly, not inferred from the source).
