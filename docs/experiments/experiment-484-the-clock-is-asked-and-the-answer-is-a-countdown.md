# Experiment 484 — the clock is asked, and the answer is a countdown

Date: 2026-09-20
Hardware: Xiaomi Mi 4 (cancro), non-persistent `fastboot boot`, `/proc/last_kmsg` captured
Artifacts: `stages/stage90/xnu_arm_boot/entry_timebase.c`, `entry_trace.c`, `entry_gic.c`,
`entry_stubs.c`, `build_entry.sh`, `tools/check_timer_sources.py` (new),
`tools/check_gic_routing.py`

**Result: the two readings 483 could not tell apart are told apart, and the answer is the opposite of
both.** `xnu_live_tmr_src3 = 2034` of the first 2048 arming entry-point calls says the deadline the
decrementer is armed for comes from **the scheduler's quantum timer** — `timer_call_quantum_timer_enter`
on `BootProcessor+64`, whose callback is `thread_quantum_expire` — and `xnu_live_tmr_src1 = 2`,
`_src2 = 12`, `_src4 = 0` say nothing else arms this clock at scale. `xnu_live_dec_new_value` names
**nine distinct values in the first eleven armings** (the eighth at call 10, the ninth at call 11), so
"every arming was `0x4ab8`" is false — but the 64 arming records show the *deadlines* repeating
**thirteen distinct values across 64 armings, one of them held for thirty consecutive ones**, while the
*values* differ in 63 of 64, and `delta == deadline - now` in **64 of 64**. The thing 483's one-sided
`dec_min` was being asked to decide is therefore decided by the pair: **the deadline is stable and the
number written is a countdown to it**, which is why the old census printed the same log for "never
moved" and "moved within `0x4ab6`–`0x4ab9`".

**And the first run of this step did not get to say any of it.** It stopped in a panic this step's own
instrument caused — `sleh_abort at interrupt context`, `pc = gicd_read+4`, `far = 0xf9000000`, 17
`MACH Reboot` lines — because the live channel's device mapper installed its section into a page-table
base it had latched at the console's *first* live write, and 484's wrappers moved that first write
earlier than 483's. The fix is one function (`entry_mmio_section` now re-reads `TTBR0`/`TTBR1` for every
install) plus the reading that proves it: `xnu_live_l1 = 0x80700000` beside
`xnu_live_gic_l1 = 0x80704000` with `xnu_live_gic_l1_moved = 0x1`.

The boot is unperturbed and the device came back on its own, so the never-brick half holds. **The
minimum bar is still not met**: the OS console text is byte-identical to 483's (`ostext_chars =
ostext_total = 0x3ee`, `ostext_block = 0x20000`, `heals = 1`), `xnu_live_sleh_seen` reaches `0x20`
(483 C: `0x20`), and **`xnu_live_getpid_count` reaches `0x00800000` — the same 8 388 608 calls 483's run
counted**. The clock is now known to be asked; the fixture still never asks to wait.

## What 483 left, and the experiment that separates the two readings

483's `xnu_live_dec_min` slides from `0x7fffffff` (the first arming, by construction) to `0x2edb5`,
`0x2eda2`, `0x2ec25` and then `0x4aae`, and 483's write-up read the flat tail as "every arming was the
same value". Two things are wrong with that reading and one of them is the measurement:

  - `dec_min` only ever *falls*. "every arming was `0x4ab8`" and "every arming was *at least* `0x4ab8`"
    print the same log, so the range sentence is unsupported by the instrument that produced it.
  - A maximum does not repair it: arming call 1 is handed `DECREMENTER_MAX` (`0x7fffffff`) by
    construction, so a maximum over all calls is `0x7fffffff` whatever the clock does afterwards.

What separates "a clock nothing waits on" from "a fixture that never asks to wait" is not a bound but
**repetition**: is the value written the same one every time, and if not, is it the same *deadline* seen
from a moving clock? 483's instrument could not ask either question, and the second one is the question
the first one's answer depends on — `setPop` is handed `deadline - now`, so a fixed deadline re-armed
from a running counter produces a *different number every time*.

## The instrument

Three things, all of them readings rather than arguments.

**The arm-er names itself.** `setPop` (`osfmk/arm/rtclock.c:344`) receives an absolute time and not its
provenance, and the three `cpu_data` deadline fields (`rtclock_timer.deadline`, `idle_timer_deadline`,
`quantum_timer_deadline`) are not addressable from this image's `assym.s`. So the entry points are
wrapped instead of the fields — `timer_call_enter` (1), `timer_call_enter_with_leeway` (2),
`timer_call_quantum_timer_enter` (3) — and `timer_call_setup` is wrapped so the `call` pointers can be
resolved to the callbacks they were set up with. `thread_quantum_expire` is wrapped as well, which is
how the callback that re-arms is counted rather than inferred.

**The census replaces the minimum.** Call 2 is the reference (`g_dec_ref_value`, call 1 excluded
because it saturates), later calls are `dec_same` or `dec_other`, the first eight *distinct* values are
published with the call they first appeared on (`dec_new_value`/`dec_new_call`/`dec_distinct`), further
distinct values are counted (`dec_other_more`, the first one published), and values already in the table
are counted per row (`dec_seen_value`/`dec_seen_count`). Every bound is published as a count rather than
dropped, and each is written on the powers of two of the counter it belongs to — so every total in this
document is a **lower bound**, which is stated here once and not repeated per number.

**`tools/check_timer_sources.py` (new, seven claims, 42 mutations refused).** It checks the wrappers'
source numbers against their own calls, both spellings of each prototype against Apple's headers
(argument count, every `uint64_t` position — the AAPCS pairs hazard — and the 64-bit return), that all
five names and their wrappers are in the linked image at different addresses, that
`__wrap_thread_quantum_expire`'s address is materialised in the image (as a `movw`/`movt` pair, which is
the form that reference takes) and the unwrapped one is not, that every bound the census imposes is
present and counted, that all five wraps are in `TRACE_LDFLAGS` and none in `PASS1_LDFLAGS`, and that
`timer_call_enter1` — the family's fourth member — has **no caller in this image**.

That last claim is a finding of the build, not a choice of this step: `timer_call_enter1` is called only
from `osfmk/kern/sfi.c` (not compiled here) and `bsd/dev/dtrace/dtrace_glue.c` (not in this
configuration), `nm` over the 708 objects of `out/xnu_kernel_obj` finds the name in
`osfmk_kern_timer_call.o` alone, and the linked image has **no branch to the wrapper** — so the first
build refused it ("these `--wrap`'d symbols have no branch to their wrapper anywhere in the linked
image, so the wrapper can never run: `timer_call_enter1 thread_quantum_expire`") and the wrapper was
removed rather than kept as a dead record. The source numbers stay 1/2/3 with 2 = the leeway form and
3 = the quantum one, because the numbers are what a log says.

## The defect the first run found, and the fix

**Run A: 438 482 bytes, 4 399 lines, 17 `MACH Reboot`, no census.** The panic is XNU's own and it names
itself:

```
Attempting system restart...MACH Reboot
panic(cpu 0 caller 0x8044cc8c): sleh_abort at interrupt context (saved state:0x80507d48)
r0: 0xf9000000  r1: 0x0000000a ... r7: 0x8050a000  lr: 0x80008870 pc: 0x80008d70
cpsr: 0x80000093 fsr: 0x00000005 far: 0xf9000000
```

`0x80008d70` is `gicd_read+4` (`80008d6c: add r0, r0, #0xf9000000 / 80008d70: ldr r0, [r0]`) and
`lr = 0x80008870` is `entry_gic_probe+264`, the read of `GICD_CTLR` — the first GIC register the probe
touches. `r0 = 0xf9000000` is the *computed* address, so the offset passed was 0 and the section the
probe had just installed was not there. The probe says so itself two lines earlier: it had published
`xnu_live_gic_map = 0x1` — the install "succeeded" — and the very next load faulted.

The reason is a value **latched and then used after the machine replaced it**. The live channel reads
`TTBR0`/`TTBR1` once, at its first live write, and keeps the base as `g_live_l1`;
`entry_mmio_section` installed every device section into that latch. That is sound for the console,
whose sections go in *before* `arm_vm_init` copies the boot table into the system table four pages
higher (`osfmk/arm/arm_vm_init.c:370-380`: `boot_ttep = args->topOfKernelData`, `cpu_ttep = boot_ttep +
ARM_PGBYTES * 4`, `bcopy(boot_tte, cpu_tte, ARM_PGBYTES * 4)`) — the copy is what carries them across.
For a caller that runs *after* that copy it is a table the MMU has stopped walking.

**And what moved the latch is this step.** In 483's run the first live write came from
`ml_init_timebase` (`xnu_live_timebase_registered`, log line 3946) — after the copy, so the latch
happened to name the live table. In 484's runs it comes from `__wrap_timer_call_setup`
(`xnu_live_tmr_setup_seq = 1`, the same line), which is earlier in the boot, and the latch names
`0x80700000` = `topOfKernelData` = `boot_ttep`. 483's run was not right; it was lucky.

The fix, and both of its halves are structural:

  - `entry_mmio_section` reads the table **at the moment it installs** — `entry_live_ttb_base()`, one
    definition of "which table answers for an address above the `TTBCR` boundary", used by the
    console's init and by the mapper (a second spelling would be "one value, two definitions" with a
    faulting load as the failure). The same window and alignment test the init makes is applied, and
    the four readings are kept in `g_live_mmio_*`.
  - `tools/check_gic_routing.py` gains a seventh claim (43 mutations refused, was 36): the mapper
    reads the table and does not install into the latch, the rule is spelled **once** in the image,
    the probe publishes the table it used *before* the return a refused mapping takes, and Apple's
    `bcopy` — the reason the console's latch is sound and a later install is not — is read out of
    `arm_vm_init.c` rather than assumed.

**Run B: the same build with that one fix. 499 465 bytes, 6 132 lines, zero `MACH Reboot`, device back
on Android by itself.** The mapping's own keys now separate the two tables:
`xnu_live_gic_l1 = 0x80704000` (the system table, `boot_ttep + 0x4000`),
`xnu_live_gic_ttbr0 = xnu_live_gic_ttbr1 = 0x8070404a`, `xnu_live_gic_l1_moved = 0x1` against
`xnu_live_l1 = 0x80700000` (the console's latch), and the read that faulted in run A answers:
`xnu_live_gic_dist_ctlr = 0x00000001`, `gic_dist_typer = 0x468`, `gic_isenabler0 = 0x7fff` — 482's
numbers, read this time through a table the MMU is walking.

## The measurement

**The arm-er.** At the 2048-call sample of the arming entry points: `_src1 = 2`, `_src2 = 12`,
`_src3 = 2034`, `_src4 = 0`. The first 64 records say which *call structures* those were:
`BootProcessor+64` (the quantum timer, whose callback is `__wrap_thread_quantum_expire`) 60 times,
`thread_call_groups+40` (`thread_call_delayed_timer`, `thread_call.c:618`'s
`timer_call_enter_with_leeway(&group->delayed_timers[flavor], ...)`) 3 times, and `init_thread+488`
(`thread_timer_expire`) once. The quantum path is source 3 with `flags = 0` — not "no flags" but "no
flags argument": `timer_call_quantum_timer_enter` hard-codes `TIMER_CALL_SYS_CRITICAL | TIMER_CALL_LOCAL`
(`= 0x01 | 0x40`, `timer_call.c:715`), and the entry point's own record is the absence of the argument.
The three leeway armings carry `flags = 0x21` = `TIMEOUT_URGENCY_SYS_CRITICAL | TIMEOUT_URGENCY_LEEWAY`
(`osfmk/kern/kern_types.h:159-169`), which is `thread_call`'s own "system-critical with an explicit
leeway" encoding.

**The value, and the deadline under it.** In the first 64 arming records: **13 distinct deadlines, 63
distinct deltas, and `delta == deadline - now` in all 64.** One deadline (`0x6f6c936`) is held across
**thirty consecutive records** while `now` walks from `0x6f3dc24` to `0x6f46ca0` and the delta falls from
`0x2ed12` to `0x25a51`. The census agrees from the other side: nine distinct values in the first eleven
armings is a *rising* count of new values, while `dec_seen_value = 0x2ed6a` appears 8 times and
`0x2ed67` 4 times — the values do repeat, in a small set, which is what re-arming one deadline from a
moving clock looks like.

**The census's own totals** (lower bounds, per the sampling above): `dec_ref_value = 0x2edb5` at call 2;
the eight published distinct values in order `0x2eda2, 0x2ec25, 0x2ed67, 0x2ed34, 0x2ed55, 0x2ed7a,
0x2ed6a, 0x2ed63` at calls 3..10; the ninth `0x2ed71` at call 11; `dec_min` `0x7fffffff → 0x2edb5 →
0x2eda2 → 0x2ec25 → 0x4aae`; `dec_same` past 2048 while `dec_other = 0xdd8` (3544) at the same sample,
so **at least 5 592 armings** reached `ml_set_decrementer` (483's run reached 4 096). The clock is
asked more often than it interrupts: `xnu_live_irq_timer_count` past 2048, `xnu_live_setpop_count` past
4096, `xnu_live_tmr_enter_over` past 2048 records beyond the 64 shown, `xnu_live_tmr_qexp_over` past
2048 quantum expiries.

**The registered callouts, named.** `timer_call_setup` is published in full (24 records, then a count:
`xnu_live_tmr_setup_over = 1`, so ≥25), and every `func` resolves against this image:

| call | func | what it is |
| --- | --- | --- |
| `BootProcessor+64` | `__wrap_thread_quantum_expire` | the quantum timer (`processor.c:163`) |
| `alarm_expire_timer` | `alarm_expire` | BSD `timeout`/hardclock |
| `ntp_loop_update` | `ntp_loop_update_call` | NTP's own callout |
| `init_thread+488`, `init_thread+152` | `thread_timer_expire`, `thread_depress_expire` | the boot thread's timers |
| six `0xc05b6xxx` pairs | `thread_timer_expire`, `thread_depress_expire` | six more threads |
| `thread_call_groups+40` … `+368` | `thread_call_delayed_timer`, `thread_call_dealloc_timer` | the `thread_call` groups |

So the kernel does have a callout population — 25 registrations across seven subsystems — and the one
that arms the hardware clock is the scheduler's quantum timer, on `BootProcessor`'s own
`struct timer_call`. Two of the three `sched_prim.c` sites that arm it are the idle and dispatch paths
(`thread_select_idle` at `:2104`, `thread_dispatch` at `:2859`), which is the shape a re-arm-while-
switching scheduler produces.

## What moved, and what did not

| | 483 C | 484 B |
| --- | --- | --- |
| `xnu_live_setpop_count` | 4096 | **past 4096** |
| `xnu_live_dec_min` | flat at `0x4ab8` after call 22 | `0x7fffffff`, `0x2edb5`, `0x2eda2`, `0x2ec25`, `0x4aae` |
| `xnu_live_irq_timer_count` | past 2048 | past 2048 |
| `xnu_live_getpid_count` | `0x800000` | **`0x800000`** (8 388 608) |
| `xnu_live_sleh_seen` | `0x20` | **`0x20`** |
| OS console text | `0x3ee`, block `0x20000` | **identical** |

The clock now has a measured owner and a measured shape, and the fixture is untouched: the last three
rows are what "the minimum bar is not met" means in numbers — the boot does the same 8.4 million
`getpid` calls and reaches the same console text with the same abort population. The abort window is
the same 16 records in both runs (**14 kernel-mode, 2 user-mode**), the two user-mode entries carry the
same two pcs (`0x00001118`, `0x00001124`, inside the fixture's own thread state) and every kernel-mode
pc moved by exactly `+0x6E0` (`0x80013234 → 0x80013914`, `0x80013320 → 0x80013a00`), which is the
instrument's own growth and nothing else.

## Defects this step's own session found

Three, in `mi4-measurement-defects.md` (234–236):

  - **234 — the latch used after the machine replaced what it named.** The console reads
    `TTBR0`/`TTBR1` once and shares a device mapper that assumed the answer stays true. It does not: a
    table base is a value the kernel replaces in the middle of the boot, and the two answers here are
    one `ARM_PGBYTES * 4` apart. The instrument's own new call site moved the latch, which is how a
    latent defect became a panic instead of a silent zero. **The tell: a value read once and used later
    is a value whose *replacement* nobody checked for** — the guard that would have caught it is the one
    this fix adds, a reading at the point of use.
  - **235 — a substring test answering a "was it published" question.** `tools/check_timer_sources.py`'s
    census claim searched the file for `xnu_live_dec_min`, `g_dec_other_more` and `xnu_live_dec_ref`,
    and each needle also matched a *longer* key or a neighbouring line
    (`xnu_live_dec_min_count`, the publish of `dec_more`, `xnu_live_dec_ref_call`), so three mutations
    that removed the publication were **accepted**. Two more were stale in the same session: the
    `STAGE90_TMR_SRC_MAX` mutation set the limit to 4 after the largest source became 3, and a mutation
    that *added* a value to the set the claim unions was testing a state the design accepts. All five
    are refused now; the claims search for the publish call, not the name.
  - **236 — a claim about a name where the property was about an install.** The new GIC claim's first
    version said the mapper must not *mention* `g_live_l1`; the fix's own `g_live_mmio_l1_moved = (l1 !=
    g_live_l1)` mentions it by necessity, and the check's own baseline run said so before any build. The
    claim now tests `entry_section_install(va, pa, g_live_l1,` — the install — which is the property.

## Build, layout, and artifacts

| | 483 C | 484 A | 484 B |
| --- | --- | --- | --- |
| `.text` | 5238976 | 5240672 | **5240896** |
| image bytes | 5454452 | 5454452 | **5454452** |
| `.bss` | `0x80533a80..0x8058b608` | `..0x8058b6c8` | **`..0x8058b6c8`** (359496) |
| headroom | 1526264 | 1526072 | 1526072 |
| undefined / wraps | 26 / 49 | 26 / 54 | **26 / 54** |

The image file's length is unchanged for the third step running (the added code fits inside the
sections' aligned-fill term), and the `.bss` growth is exactly the census's state: +192 bytes on a
`0x80533a80`-aligned start, which is what `xnu_entry_bss_bytes 0x00057b88 → 0x00057c48` and
`xnu_entry_checksum 0x907066b9 → 0x90706679` report from the payload's side. The 54 wraps are 48
reached by a branch, 1 same-object (`IOService::matchPassive`), 1 never called here (`sleep`) and 4 by
address only — `vcputc getpid mmap thread_quantum_expire`, and the last of those is this step's: the
reference is the `movw`/`movt` pair four instructions before `bl __wrap_timer_call_setup` at
`800113d4`.

Entry image (`xnu_arm_entry.bin`) sha256 run A `2bf7c4da…`, **run B `734a6fed36884c2930d5079ff68d18534a13472127da9c9598ed0b2839cba95e`**
(`.elf` `bfcb114062358be78ba912804f2b5d3cc61da6fc8d3335e13fecd0145303bb6a`); payload
`stage90-qcdt.img` 8 474 624 bytes, run A `fba94970…`, **run B
`a65b8cddd0f1b5d1f0443ea0a7d13d6b81071fe0a0be1807d9efb3ab323846bd`**, `kernel_size = 5949716`,
`dt_size = 2521088`, page size 2048. Build lines, in order, all green: `check_undef_handler.py --split`
(61 mutations refused), `check_timebase_registration.py` (12), `check_gic_routing.py` (**43**),
`check_irq_routing.py` (36), `check_timer_sources.py` (**42**). Both runs went through
`preflight_boot_check.sh --allow-xnu-entry` and `run_and_capture.sh --allow-xnu-entry`, exit 0, and the
device returned to Android on its own both times.

## What 485 has to do

The clock is answered, so the remaining half of 484's question is the one the numbers point at: **the
fixture never asks to wait.** 8 388 608 `getpid` calls with `_last = 1` on every one, 32 user-mode
aborts, the same console text — the boot is not stopped by a missing timer, and it is not stopped by a
driver: it is stopped by the program in the RAM disk calling a syscall that returns immediately. The
next step is therefore about *that* program and not about the kernel: the RAM disk's thread state
(`entry_ramdisk.s`: `sp = 0`, so the kernel picks `USRSTACK` and allocates the stack itself; `pc =
entry_pc_value` = `TEXT_VMADDR + (entry_code - g_stage90_ramdisk)`, an expression rather than a
literal; `cpsr = 0x10`, from which `machine_thread_set_state` keeps only the flags) runs `getpid` in a
loop — 475's `xnu_live_undef_pc = 0x000010e0` is that program's first `udf` — and the loop is the
fixture's, so 485's instrument belongs in the fixture. What it has to decide, in the order the machine
decides it: whether the loop is what the fixture *says* (an armv7 program whose `main` calls `getpid`
and matches on the result) or what its *thread state* does (a `pc` that never advances past the
syscall), and then give the fixture its first real wait — a syscall whose effect is on the machine and
which *cannot* return immediately: `mmap` of a page (480's `mmap` already returns `0x00102000`), a
store into it, and a read back, which also makes this image's first *serviced* demand fault (467's
handler, never yet exercised by a user store) part of the reading.

Still owed, and unchanged: which of `arm_fast_fault`/`vm_fault` serviced 480's write fault; why the
`VM_FLAGS_FIXED` stack allocation at `0x26E00000` is refused; 474's `thread->map = 0` moment; 448's
`_bad` slots as a named pair; the pthread table's other ~34 slots; `osfmk/kperf/kperfbsd.c`; the
untraced build's `entry_stubs.c` compile errors; `thread_bootstrap_return`; and the fixture's timer node
(`interrupts = <1 2 0 1 3 0>` → INTID 18/19), which 482 falsified for `CNTV` and which now matters to
the quantum timer's own chapter.
