# Experiment 309 — no object: the instrument's own interrupt source, and the boot leaves the scheduler

**Step:** link **nothing.** The change is to the instrument — the payload and the entry image — and it
is the step that moved the frontier furthest in this whole walk.

**Prediction:** *the interrupt that ended 308 was this project's own dead-man timer, and with it
disarmed the boot continues past the context switch.* Named falsifier: any other interrupt source, in
which case the run ends on `exception: irq` again — and, because the entry image was changed in the
same step to name the interrupt, the log would say which one.

**Result:** the prediction holds on all three counts. No `exception:` line, no `xnu_entry_irq_iar`
record (the new diagnostic never ran), and the run stopped on `stub_hit=device_service_create` at
caller key `0x8000e61c` = **`kernel_bootstrap_thread + 0x9c`**.

## The interrupt was the instrument's, and the payload's own docs said it would be

`stage90_arm_deadman_reset` arms the dead-man by calling `stage90_arm_pc_sampling_watchdog`, and that
function does three things the *payload's* vector table is what services:

```c
    /* Enable the timer PPIs at the distributor. */
    mmio_write32(dist_base + GICD_ISENABLER0, GIC_TIMER_PPI_MASK);
    ...
    write_cntp_tval(interval_ticks);
    write_cntp_ctl(CNTP_CTL_ENABLE);
    ...
    /* Enable IRQ delivery last so XNU runs interruptible. */
    enable_irq_delivery();
```

Nothing disarmed any of it before the jump. And `xnu_entry_jump.c`'s own header, and the entry
image's, both already say the dead-man cannot exist on the far side:

> It does not arm the software dead-man across the jump. The dead-man needs the payload's GIC and
> vector state, and both are gone the moment `_start` switches tables; leaving it armed would be a
> claim that cannot be honoured.

308 showed what that costs, and the cost is worse than an unhonourable claim: the tick lands on the
entry image's `fleh_irq`, which reports one line and stops the machine. So the net was never a net
across that window, and all it did was end every run at the first interrupt — three statements after
the kernel's first real context switch.

## The disarm, and the three levels

`stage90_disarm_deadman_timer()` (`stages/stage90/gic.c`) is called from `xnu_entry_jump.c` on the
last line the payload executes. It turns the source off at three levels, because each alone leaves
the tick a way in:

| level | what it stops |
|---|---|
| `CNTP_CTL` — `ENABLE` cleared, `IMASK` set | the timer asserting at all (`generic_timer_shutdown`) |
| `GICD_ICENABLER0` | a tick already asserted being delivered |
| `GICD_ICPENDR0` | a tick latched before the timer stopped being delivered later |

The fourth level — CPU-level IRQ masking, which `disable_irq_delivery()` does — is the one that
**cannot** hold across the jump, and saying so is part of the change: the entry image's exception
return restores the CPSR of the interrupted context, so the I bit comes back from XNU's own state
rather than from anything set here.

The measured values, and they are the evidence rather than a description of it:

```
 disarm_ppi_mask=0x000c0000
 disarm_isenabler0_before=0x000c7fff   disarm_isenabler0_after=0x00007fff
 disarm_ispendr0_before=0x20480000     disarm_ispendr0_after=0x20400000
 disarm_cntp_ctl_before=0x00000005     disarm_cntp_ctl_after=0x00000002
 disarm_hw_watchdog_en=0x00000001
```

`isenabler0` goes from `0x000c7fff` to `0x00007fff`: bits 18 and 19 — the timer PPIs — were the only
thing this boot had enabled beyond what aboot left. `ispendr0` loses bit 19: one had **already**
asserted and was waiting. And `cntp_ctl` was `0x5` = `ENABLE|ISTATUS`, an armed timer that had
**already expired**, becoming `0x2` = `IMASK` with `ENABLE` clear.

**The net that remains is read back from the SoC.** `disarm_hw_watchdog_en=0x00000001` is a live read
of `WDT0_EN` through a new `stage90_hw_watchdog_enabled_readback()`, not the cached result struct —
because the step that removes one recovery net has to show the other is still armed, and a shadow
copy would say what this project wrote rather than what the watchdog is. The hardware watchdog is
armed earlier in the same boot and its own log line already states the property that matters here:
"independent of GIC, timer and IRQ state".

## The entry image: name the interrupt, from where it can be named

The same step adds the diagnostic that would have answered the question if the disarm had not. It is
small, and the interesting part is *where* it had to go:

```c
void fleh_irq(void)
{
    g_irq_report_pending = 1u;
    entry_epilogue("exception: irq");
}
```

The GIC is at `0xf9002000`, and `fleh_irq` runs with **XNU's page tables live**, which map
`[physBase, physBase + memSize)` and nothing at `0xf9002000` — so the vector cannot read it. The
MMU does not come off until `entry_epilogue` is inside, so the read belongs there, after the
teardown, and the flag is what carries the request across it:

```c
    if (g_irq_report_pending != 0u) {
        entry_kv("xnu_entry_irq_iar", *(volatile uint32_t *)(uintptr_t)0xf900200cu);
        entry_kv("xnu_entry_irq_ispendr0", *(volatile uint32_t *)(uintptr_t)0xf9000200u);
        entry_kv("xnu_entry_irq_isenabler0", *(volatile uint32_t *)(uintptr_t)0xf9000100u);
    }
```

Reading `GICC_IAR` acknowledges the interrupt as a side effect, which is deliberate: the machine is
about to be stopped and reported anyway, and acknowledging is what turns "an interrupt arrived" into
"interrupt N arrived".

**It did not run.** `xnu_entry_irq_iar` appears **zero** times in the log. That is the disarm's own
confirmation arriving from a different direction — the entry image would have said so if anything
had been delivered — rather than a second claim about it.

Its only build cost is `.text` **0x11D8C0 → 0x11D980** (+0xC0). Image `0x138AAC`, `.data`
0x80120000, bss 0x80138AC0..0x8016F8D8 and headroom 1640232 are all unchanged, and the stub counts
stay 736 / 647 / 89 because no object moved.

## The run

```
MI4IOS6_STAGE90_XNU stage90 disarm: the instrument's timer PPI is off before the jump
 ...
MI4IOS6_STAGE90_XNU real XNU entry stub_hit=device_service_create
 xnu_entry_stub_caller=0x8000e61c        xnu_entry_stub_caller_a=0x8000e61c
 xnu_entry_stub_caller_e=0x8000e61c      xnu_entry_kv_written=0x00000066
 xnu_entry_bss_start=0x80138ac0          xnu_entry_bss_end=0x8016f8d8
 xnu_entry_copied_bytes=0x00138aac       xnu_entry_entering_at=0x80000074
 xnu_entry_failures=0x00000000
```

`tools/host_resolve_entry_addr.sh 0x8000e61c` → `kernel_bootstrap_thread+0x9c`, and the instruction
at `0x8000e618` is `bl 80102b3c <device_service_create>`, immediately after
`bl 8000bb698 <clock_service_create>` at `0x8000e614` — which *returned*.

Preflight clean (`STAGE90_XNU_ENTRY 1`, `HARD_SKIP`, watchdog ARMED, no storage symbols in the
payload), log 301633 bytes, one `stub_hit=` line, **no `exception:` line**, `xnu_entry_abort_entries`
0.

**Safety:** non-persistent `fastboot boot` only, nothing flashed,
`persistent_write_attempted=0x00000000` ×25, `failure_mask=0x00000000` ×87,
`xnu_entry_failures=0x00000000`, and the device returned to Android on its own
(`ro.build.version.release` = 10).

## What it measures, and it is the largest single advance in this walk

The stop is in `kernel_bootstrap_thread` itself, twelve calls into its straight line. Every one
before it ran and returned:

```
8000e584  current_processor
8000e594  kernel_debug_string_early
8000e59c  idle_thread_create          <- 305's predicted stop, reached and passed
8000e5ac  sched_startup               <- *** returned ***
8000e5b8  kernel_debug_string_early
8000e5bc  thread_daemon_init
8000e5c0  vm_kernel_reserved_entry_init
8000e5d0  thread_call_initialize
8000e5e4  thread_bind
8000e5f4  ipc_thread_call_init
8000e604  mapping_adjust
8000e614  clock_service_create
8000e618  device_service_create       <- THE STOP
```

**`sched_startup()` returned.** In 306 the run died three statements *inside* it, at `thread_block`'s
`ast_off`. For it to return, the bootstrap thread has to be blocked, the scheduler has to choose a
thread, `thread_select` and `thread_invoke` have to reach `machine_switch_context`, a **different**
thread has to run, and the bootstrap thread has to be selected and switched back to. That is the
first time this kernel has taken a full round trip through its own context switch on hardware — and
it means the enqueue that 305 and 306 watched go by, and that 307 measured `thread_select` accepting,
ended in an actual dispatch.

Seven more entries of `kernel_bootstrap_thread` that had never executed now have:
`idle_thread_create`, `thread_daemon_init`, `vm_kernel_reserved_entry_init`,
`thread_call_initialize`, `thread_bind`, `ipc_thread_call_init`, `mapping_adjust` — plus
`clock_service_create`, the clock subsystem.

## What it does not measure

* **Which thread ran.** 307's open question — the thread `sched_startup` created, or
  `processor->idle_thread` — is still open. Nothing in this run's log distinguishes them, and the
  interrupt that might have carried a sample never arrived.
* **Whether the dead-man is now useless everywhere.** It is disarmed only on the XNU-entry path, on
  the last line before the jump; the payload's other ladders (`xnu_handoff.c`, `xnu_kernel.c`) keep
  it armed, and they run with the payload's own vectors, where it works.
* **Whether `device_service_create` will succeed.** That is the next run's question.

## A note on 305's prediction

305 predicted `device_service_create` at `0x8000e59c`. This stop is *the same call* at `0x8000e618`
— the difference is the image having grown 0x80 across four steps, not a different frontier. So
305's prediction was right about the call and four experiments early about the step: it described
where the walk would arrive once the scheduler, the AST layer, `sfi.c`, `ast.c` and the monotonic
clock were all real, and none of those were the step it was written for.

## Next

**`osfmk/device/device_init.c`** (`osfmk_device_device_init.o`, manifest:506) — the object that
defines `device_service_create`. It is **188 bytes of `.text`**, 28 of `.bss` and 43 of
`.rodata.str1.1`, with 9 definitions and 11 references. It resolves **1** name (`device_service_create`
itself) and adds **0**: every one of its eleven references — `ipc_port_alloc_special` (behind the
`ipc_port_alloc_kernel` macro), `panic`, `ipc_kobject_set`, `host_priv_self`, `ipc_port_make_send`,
`kernel_set_special_port`, `lck_grp_attr_alloc_init`, `lck_grp_alloc_init`, `lck_attr_alloc_init`,
`lck_mtx_init`, `ipc_space_kernel` — is already real in this image, and
`tools/stub_calls_in_function.py` reports **no stub call inside any of their bodies**.

So the prediction is that `device_service_create` **runs to its end**, and the stop is the next call
`kernel_bootstrap_thread` makes, which the image puts at **`kdp_init`, caller key `0x8000e63c`**.
Its named falsifier is the body's own second call, `bl panic` at +0x30:

```c
	master_device_port = ipc_port_alloc_kernel();
	if (master_device_port == IP_NULL)
	    panic("can't allocate master device port");
```

— taken when `ipc_port_alloc_special` returns `IP_NULL`. That would be the first candidate in this
walk that is a **panic** rather than a stub, and the log distinguishes them: a panic prints its
string, a stub prints `stub_hit=`.
