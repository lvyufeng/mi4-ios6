# Experiment 308 — `kern_monotonic.c`, and the run ends on an interrupt

**Step:** link `osfmk/kern/kern_monotonic.c` (`osfmk_kern_kern_monotonic.o`, manifest:558) — the
object that defines `mt_sched_update`, the name 307 stopped on.

**Candidate:** a stop *outside* the object being linked. `mt_sched_update` is
`if (!mt_update_thread(thread)) return;` and `mt_update_thread` opens with
`if (!mt_core_supported) return false;` — and `mt_core_supported` is one of the 89 storage
stand-ins, which are never initialized, so it reads 0 and the new code returns at its first
statement. The named candidates were therefore the first dispatch of the thread `sched_startup`
created (`sched_init_thread` → `sched_timeshare_maintenance_continue` → **`compute_averages`** at
caller 0x800A3178, 305's old falsifier arriving by a different road), with `thread_select`'s `idle`
arm as the alternative.

**Result:** `MI4IOS6_STAGE90_XNU real XNU entry: exception: irq` — **and no `stub_hit` line at all.**
The object behaved exactly as predicted; what the run ended on is something this walk has not
produced before.

## 7 resolved, 2 added, and the object returns at its first statement

`osfmk_kern_kern_monotonic.o` is `.text` **2900 bytes** and `.bss` **24** (`size -A`), with **23
definitions and 12 references**. It resolves **7** — `mt_sched_update` (307's stop),
`mt_fixed_counts`, `mt_fixed_task_counts`, `mt_perfcontrol`, `mt_stackshot_task`,
`mt_stackshot_thread`, `mt_terminate_update` — and adds **2**, `mt_core_snap` and `mt_cur_cpu`, both
`R_ARM_CALL` so both function stubs. Counts: 741 → **736** undefined, 652 → **647** function stubs,
89 storage **unchanged**. The storage count does not move because the object's one storage
reference, `mt_core_supported`, is a name it *uses* and does not define (`nm` on the image before
this step: `8016ef80 B`, one of the 89 stand-ins), so it stays a stand-in and nothing retires it —
and that is the whole reason this step cannot stop the run:

```c
void
mt_sched_update(thread_t thread)
{
	bool updated = mt_update_thread(thread);
	if (!updated) {
		return;
	}
	...
}
```

```c
bool
mt_update_thread(thread_t thread)
{
	if (!mt_core_supported) {
		return false;
	}
	...
}
```

## The build: two predictions missed, and the padding explains both

The baseline was built in this session with an empty stand-in object in this slot (it reproduces 307
exactly: 741/652/89, `.text` 0x11CE40, image 0x138AAC, headroom 1640232).

|  | predicted | measured |
|---|---|---|
| undefined / function / storage | 736 / 647 / 89 | 736 / 647 / 89 |
| `.text` | 0x11D8B0 + fill | **0x11D8C0** (fill 0x10) |
| `.data` | 0x80120000 | 0x80120000 |
| `__bss_start` | 0x80138AC0 | 0x80138AC0 |
| `__bss_end` | 0x8016F8F0 | **0x8016F8D8** (unmoved) |
| image | 0x138AAC | 0x138AAC |
| headroom | 1640208 | **1640232** (unmoved) |

`.text` is the arithmetic landing again: +0xB54 for the object, −7 stub bodies (0xA8) and their 7
name strings (0x84), +2 stub bodies (0x30) and their 2 name strings (0x18), and 0x10 of fill.

`__bss_end` is where the prediction was wrong, and the map says why in one line:

```
 .bss   0x8016e188  0xc  osfmk_arm_commpage_commpage.o
 *fill* 0x8016e194  0x4
 .bss   0x8016e198 0x18  osfmk_kern_kern_monotonic.o     <- this step's 24 bytes
 .bss   0x8016e1b0  0x0  xnu_arm_entry_rtabi.o
 *fill* 0x8016e1b0 0x10
 .bss   0x8016e1c0 0x1704 xnu_arm_entry_realstubs.o      <- the 89 stand-ins, 16-byte aligned
```

The stub object's stand-ins are 16-byte aligned, so the link script already carried **0x2C** of
padding between `commpage.o`'s last byte and their start; the new 0x18 sits inside it and the
section's end does not move. **`__bss_end` is the end of the padded output section, not the sum of
its inputs** — the same mechanism 301 found when `machine.o`'s `.bss` grew and `__bss_end` stood
still (there the retired stand-in shrank by exactly as much; here nothing had to shrink, because
padding absorbed it). Headroom is `topOfKernelData − __bss_end`, so it did not move either.

## The run

```
MI4IOS6_STAGE90_XNU real XNU entry: exception: irq
 xnu_entry_kv_written=0x00000000          xnu_entry_kv_in_dram=0x00000024
 xnu_entry_why=0x80106e24                 xnu_entry_why_byte=0x00000065
 xnu_entry_abort_entries=0x00000000       xnu_entry_abort_first_pc=0x00000000
 xnu_entry_bss_start=0x80138ac0           xnu_entry_bss_end=0x8016f8d8
 xnu_entry_copied_bytes=0x00138aac        xnu_entry_entering_at=0x80000074
 xnu_entry_failures=0x00000000
```

There is **no `stub_hit=` line**, and `xnu_entry_kv_written=0` where 306 and 307 wrote 0x58 and 0x60:
no symbol this image lacks was called. `xnu_entry_why` (0x80106E24) is the string, read out of the
image's `.rodata`: `exception: irq`.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, log 300987 bytes, and the device returned to Android on its own
(`ro.build.version.release` = 10).

## What it measures, and what it does not

**It measures that the object did nothing, and that the path after it is real.** For the run to end
on an *interrupt* rather than on a stub, everything between 307's stop at 0x800A0C10 and the point
where IRQs are first unmasked had to be satisfiable: `mt_sched_update` (now real, returns
immediately), the rest of `thread_invoke` — `ast_context` (retired by 307), `lck_spin_unlock`,
`thread_timer_event`, `timer_switch`, `machine_switch_context` — and whatever ran after the switch.
The candidate this step named, `compute_averages`, was not reached before the interrupt.

**It measures where the interrupt could be taken, indirectly.** `cswitch.s`'s
`machine_switch_context` contains no `cpsie` and no `ml_set_interrupts_enabled` at all, and
everything from `splsched` in `thread_block_reason` to the switch runs with IRQs masked — which is
why 306 and 307, sitting inside that window, saw no interrupt. The first CPSR with the I bit clear
on this path is the one the context switch restores for the thread it dispatches, so **the timer
fired on the far side of the context switch, in the newly dispatched thread's context.**

**It does not say which thread that was.** An interrupt report carries no PC — the `abort_*` block
in the instrument is about data aborts, and every field in it is zero here — so the question 307
left open (the thread `sched_startup` created, or `processor->idle_thread`) is still open, and so is
whether the interrupt arrived before or after that thread's first instruction. That is a probe's
job — a checkpoint that records `current_thread`/`processor->active_thread` and the PC at the
vector — not another object's.

## The fourth kind of stop

Three kinds of thing have stopped this boot before, and only one of them is in the build's own
output (285, 286, 287): a missing symbol, a zero the build invents, and a boot-arg/DT string that
decides a branch. This is a fourth:

**A vector this image does not have a real implementation for.** `start.s` installs the exception
vectors by patching `ExceptionVectorsTable` with the addresses of the `fleh_*` symbols
(`LOAD_ADDR_GEN_DEF(fleh_irq)` at start.s:429) and setting `SCTLR.HIGHVEC`. In this image all eight
`fleh_*` resolve to `entry_stubs.c`'s reporting handlers — real code this project wrote, at
0x80003160 in `fleh_irq`'s case, so they appear in **no** undefined list and in no stand-in count.
The boot now runs far enough for an interrupt to arrive, which means the next thing required is not
a name but a *working vector*: an exit that XNU's own code can re-enter, rather than a page of
instrument that turns the machine off and writes one line.

That is a different shape of step from every previous one, and `entry_stubs.c`'s own header already
describes it: "whatever this file still defines that one of those objects also defines is a link
error, not a silent override: the definitions here shrink as the real objects join, and the link is
what says which ones had to go."

## Next

**`osfmk/arm/locore.s`** (`out/xnu_asm_obj/locore.o`, assembled by `tools/assemble_arm_layer.sh`) —
`.text` **0x30C8** and **99 definitions**, owning the real `fleh_irq`, `fleh_irq_kernel`,
`fleh_irq_handler`, `fleh_irq_user` and the rest of the vector set, plus `ExceptionVectorsBase`,
`ExceptionVectorsTable`, `ExceptionVectorsEnd` and `ExceptionLowVectorsBase`. Linking it is the
first step in this walk that is *also* an edit to the instrument: `locore.o` defines all eight
`fleh_*` that `entry_stubs.c` currently provides, so those definitions have to go, and the link
error — not a prediction — is what will say exactly which ones.

`fleh_irq_kernel` is worth reading before the build: it takes IRQs in `PSR_SVC_MODE`, builds an
`EXC_CTX_SIZE` frame, saves VFP through `vfp_save`, switches TTBR0 and CONTEXTIDR for
`__ARM_USER_PROTECT__`, moves to the per-CPU interrupt stack through `CpuDataEntries`'s
`cpu_istackptr`, and only then reaches `fleh_irq_handler`, which increments
`ACT_PREEMPT_CNT` and calls `interrupt_trace` under `!NO_KDEBUG`. Every one of those is a name the
image may or may not already have, which is what the next run's log will settle.
