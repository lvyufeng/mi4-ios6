# Phase 3: The MSM8974 Platform Shim — Specification

What an MSM8974 replacement for XNU's ARM platform bring-up has to provide, why it has to
replace rather than configure, and which hardware facts it must encode. Grounded in three
sources that can all be read in this repository:

1. **What XNU calls and expects** — `external/xnu-4570.1.46/pexpert/arm/`, `osfmk/arm/`
2. **What the payload already has working on this device** — `stages/stage90/gic.c`,
   `timebase.c`, and the hardware logs in `docs/experiments/`
3. **The vendor's own hardware reference** — `external/android_kernel_xiaomi_cancro/`

This is a specification, not an implementation. Its purpose is to bound the work and to make
the hardware-specific values explicit *before* code is written, because the failure mode in
this area is a plausible-looking constant that is architecturally correct and wrong on the
silicon.

## 1. Why a shim, and why it replaces rather than configures

`pe_arm_init_interrupts` (`pexpert/arm/pe_identify_machine.c:558`) ends in
`pe_arm_init_timer`, which is a chain of `#if defined(ARM_BOARD_CLASS_*)` tests on
`gPESoCDeviceType`, with `return 0` as the fallthrough. The 32-bit ARM
`pexpert/pexpert/arm/board_config.h` defines exactly three classes — `S7002`, `T8002`,
`T8004` — all Apple.

**There is no configuration in which the stock function succeeds on MSM8974.** No
device-tree value can change that; it would require an `ARM_BOARD_CLASS_*` that does not
exist. So Phase 3 writes a replacement for `pe_arm_init_interrupts` (and whatever else in
that chain is Apple-specific), rather than trying to satisfy Apple's platform code from the
device tree.

One consequence worth stating because it *removes* work: the `reg`-offset-vs-absolute
question is not on the critical path. The stock function returns before its computed address
drives anything, and the shim will compute addresses its own way. See
[`../reference/xnu-handoff-contract.md`](../reference/xnu-handoff-contract.md) Blocker 2.

## 2. The interface the shim must satisfy

### 2.1 Globals XNU's ARM code reads

| Global | Declared | Written by | Consumed by |
| --- | --- | --- | --- |
| `gSocPhys` | `pe_identify_machine.c:305` | `pe_arm_get_soc_base_phys()` | the mapping helper |
| `gPicBase` | `pe_identify_machine.c:305` | the mapping helper | `pe_arm_init_timer` (`:593`) |
| `gTimerBase` | `pe_identify_machine.c` | the mapping helper | `pe_arm_init_timer` |
| `gPESoCDeviceType` | `pe_identify_machine.c` | from the `arm-io` node's `device_type` | the board-class dispatch |

`gPicBase` is consumed **only** inside `pe_identify_machine.c` in this tree — nothing else in
`osfmk/arm` or `pexpert/arm` reads it. That bounds the blast radius of replacing the
function: the shim owns these globals and its own consumers, and does not have to satisfy a
wide interface.

### 2.2 The timer interface: `tbd_ops` and `ml_init_timebase`

`osfmk/arm/machine_routines.h:242`:

```c
struct tbd_ops {
        void     (*tbd_fiq_handler)(void);
        uint32_t (*tbd_get_decrementer)(void);
        void     (*tbd_set_decrementer)(uint32_t dec_value);
};
typedef struct tbd_ops *tbd_ops_t;

void ml_init_timebase(void *args, tbd_ops_t tbd_funcs,
                      vm_offset_t int_address, vm_offset_t int_value);
```

And `ml_init_timebase` itself (`osfmk/arm/machine_routines.c:436`) is a **pure registration
function** — it computes nothing and touches no timer:

```c
void ml_init_timebase(void *args, tbd_ops_t tbd_funcs,
                      vm_offset_t int_address, vm_offset_t int_value)
{
        cpu_data_t *cpu_data_ptr = (cpu_data_t *)args;

        if ((cpu_data_ptr == &BootCpuData)
            && (rtclock_timebase_func.tbd_fiq_handler == (void *)NULL)) {
                rtclock_timebase_func = *tbd_funcs;
                rtclock_timebase_addr = int_address;
                rtclock_timebase_val = int_value;
        }
}
```

That is good news and one trap, and the trap is the important part.

**Good news:** there is no Apple timer-behaviour assumption to satisfy here. The ops are
copied verbatim into globals that the rest of the kernel reads. Whatever the shim puts in
`tbd_ops` is what XNU will call.

**The trap:** registration is guarded by `cpu_data_ptr == &BootCpuData`. Passing any other
pointer — including a perfectly valid one — makes the whole registration a **silent no-op**:
no error, no log, and `rtclock_timebase_func` stays zeroed, so the timebase ops are absent
and whatever fails later will look like a clock or scheduling bug rather than a call
convention mistake. The shim must pass `&BootCpuData` exactly, and should assert that the
registration took (read `rtclock_timebase_func.tbd_fiq_handler` back and check it is now
non-NULL) rather than trusting the call.

Note also that the second condition makes a `tbd_ops` whose `tbd_fiq_handler` is NULL
re-registerable, which is a useful property for a probe but means "registered" cannot be
inferred from the call having happened.

So the shim must supply three things, and they map onto hardware the payload has already
driven:

| `tbd_ops` entry | What it is on MSM8974 |
| --- | --- |
| `tbd_get_decrementer` | Read `CNTP_TVAL` (`mrc p15, 0, r, c14, c2, 0`) |
| `tbd_set_decrementer` | Write `CNTP_TVAL` (`mcr p15, 0, r, c14, c2, 0`) |
| `tbd_fiq_handler` | The FIQ entry for the timer; the shim's own vectors, not Apple's |

`int_address`/`int_value` are the EOI address and value. The payload's validated GIC code
(`stages/stage90/gic.c`) acknowledges with a write to `GICC_EOIR` (`cpu_base + 0x010`) using
the IAR value it read, so the natural pairing is `int_address = GICC_EOIR`,
`int_value = <the IAR read in the handler>` — which is why `tbd_fiq_handler` and the EOI
value are coupled and must be designed together rather than separately.

### 2.3 FIQ is not optional on this build — established, not assumed

The first draft of this spec listed "whether the FIQ path is usable at all" as unsettleable
from the host. Reading further resolves the *architectural* half of it, and the answer is
that the shim must provide a working FIQ handler.

The FIQ vector slot in `osfmk/arm/locore.s:147`:

```asm
        adr     pc, Lexc_irq_vector
#if __ARM_TIME__
        adr     pc, Lexc_decirq_vector
#else /* ! __ARM_TIME__ */
        mov     pc, r9                              /* -> cpu_get_fiq_handler */
#endif /* __ARM_TIME__ */
```

`__ARM_TIME__` is used in 16 places in the ARM tree and **defined in none of them** (only
`__ARM_TIME_TIMEBASE_ONLY__` is defined, at `proc_reg.h:98`). So the `#else` branch is live
and the FIQ vector **branches to `r9`**, which `cpu_serialize_timebase` loaded from
`CPU_GET_FIQ_HANDLER` — the shim's `tbd_fiq_handler`.

Two consequences:

1. **The shim must supply a real FIQ handler.** There is no build in which XNU takes the
   timer as an IRQ; the FIQ path is the one wired up. This also means the payload's own
   approach — every interrupt it has ever driven on this device is IRQ — does *not* transfer,
   and the FIQ path is genuinely new work rather than a variation.
2. **The handler's contract is readable.** XNU's own generic one
   (`fleh_fiq_generic`, `locore.s:1650`) shows exactly what it must do:

```asm
LEXT(fleh_fiq_generic)
        str     r11, [r10]                  /* clear the FIQ source: int_value -> int_address */
        ldr     r13, [r8, CPU_TIMEBASE_LOW]
        adds    r13, r13, #1                /* maintain a software TBL ... */
        str     r13, [r8, CPU_TIMEBASE_LOW]
        ...
        subs    r12, r12, #1                /* ... and a software decrementer */
        str     r12, [r8, CPU_DECREMENTER]
        subspl  pc, lr, #4                  /* return unless DEC < 0 */
        b       EXT(fleh_dec)
```

   The first instruction confirms the `int_address`/`int_value` pairing above: the EOI *is* a
   write of `int_value` to `int_address`, and it is the handler's first act.

   The rest of the generic handler is a **software-maintained timebase** — TBL incremented by
   one per tick — which is the older ARM scheme. That looked like a tension with
   `__ARM_TIME_TIMEBASE_ONLY__`, under which `ml_get_timebase` reads the real `CNTPCT`
   (`machine_routines_asm.s:979`); reading `rtclock.c` resolves it, and the resolution is
   reassuring.

   **It is not a tension, because with the real counter available the software TBL is dead
   code.** Everything that wants a timestamp goes through `ml_get_timebase`, whose
   `__ARM_TIME__ || __ARM_TIME_TIMEBASE_ONLY__` branch reads `CNTPCT` directly. The
   software-TBL branch exists for SoCs *without* the hardware counter; on this build the
   handler's TBL/decrementer bookkeeping maintains values nothing reads.

   That matters in three ways:

   1. **The shim can use XNU's generic handler as-is.** It needs `fleh_fiq_generic` to exist
      and be installed, not to be reimplemented per-platform.
   2. **The timer does not have to tick at a fixed rate for the timebase to be correct.** A
      free-running `CNTPCT` tolerates an irregular interrupt; only an incremented TBL would
      require periodicity. So the shim can program the timer for whatever interval suits it.
      That removes a constraint that would otherwise have shaped the timer code.
   3. **`tbd_get_decrementer`/`tbd_set_decrementer` still matter**, because they are what
      `fleh_fiq_generic` uses to re-arm — `cpu_timebase_init` copies all three funcs
      unconditionally. The shim supplies real implementations, not the generic NULLs.

`pe_arm_init_timer`'s default is `struct tbd_ops generic_funcs = {&fleh_fiq_generic, NULL,
NULL}` (`pe_identify_machine.c:589`) — note that both decrementer operations are NULL there,
while `cpu_timebase_init` copies all three unconditionally. Whatever the shim supplies must
account for XNU calling through a NULL in the generic case, or not relying on those two at
all.

## 3. MSM8974 facts the shim must encode

These are the values where "architecturally correct" and "correct on this silicon" differ.
Each is cited to its evidence.

### 3.1 GIC

| Item | Value | Evidence |
| --- | --- | --- |
| Distributor base | `0xf9000000` | cancro device tree; `gic_validate_snapshot()` asserts it and passes on hardware |
| CPU interface base | `0xf9002000` | same |
| `GICC_IAR` | `+0x00c` | `stages/stage90/gic.c:16` |
| `GICC_EOIR` | `+0x010` | `stages/stage90/gic.c:17` |
| `GICC_PMR` | `+0x004` | `stages/stage90/gic.c:14` |
| Spurious intid | `0x3ff` | `stages/stage90/gic.c:21` |

### 3.2 The timer interrupt number — and why it matters

**The ARM generic timer's physical timer (`CNTP`) is delivered on intid 19 on this device,
not the architectural 30.**

Evidence, from a hardware log (`docs/experiments/experiment-12-stage9-timer-irq.md`):

```text
MI4IOS6_STAGE9_XNU gic_timer_ppi0_id=0x00000012     /* 18 */
MI4IOS6_STAGE9_XNU gic_timer_ppi1_id=0x00000013     /* 19 */
MI4IOS6_STAGE9 irq handler iar=0x00000013 id=0x00000013 count=1 timer_count=1
MI4IOS6_STAGE9_XNU gic_timer_last_timer_id=0x00000013
MI4IOS6_STAGE9_XNU gic_timer_cntp_ctl_armed=0x00000001
MI4IOS6_STAGE9_XNU gic timer selftest ok
```

The payload enabled both 18 and 19 (`gic_timer_ppi_mask=0x000c0000`), armed `CNTP`
(`cntp_ctl_armed=0x1`), and the interrupt that actually arrived carried **id 19**. The
handler observed `CNTP_CTL = 0x5` — enable plus ISTATUS — confirming the interrupt came from
the generic timer's CNTP and not from something else that happened to fire.

This is the kind of value that costs a bring-up: a shim written from the ARM ARM would use
30, the timer would never fire, and the failure would look like a broken GIC. The payload
knows the real number because it has driven it.

Two open questions the shim must settle at runtime rather than assume:

- **What intid 18 is.** The payload enables it alongside 19 but never observed it fire. It
  may be a second timer (MSM's own debug timer?) or unused. The shim should not require it.
- **Whether 19 is fixed or per-SoC-variant.** Everything here is from one cancro. The shim
  should log the intid it actually receives rather than hard-coding an assumption about it.

### 3.3 Timer frequency

`19,200,000 Hz`, validated by `ml timebase ok` in every stage since Stage4 — the payload
measures a delta over a known delay and compares. The shim's `tbd_get_decrementer` /
`tbd_set_decrementer` work in these units.

### 3.4 Watchdog (relevant to Phase 3 only as a dependency)

`0xf9017000`, bark/bite, `WDT_HZ = 32765`. Already implemented in
`stages/stage90/hw_watchdog.c`; a real kernel would want the same, and the vendor driver
(`arch/arm/mach-msm/msm_watchdog_v2.c`) is the reference. Note the vendor binding's
qualification: the bite resets via the *secure* watchdog, so the dependency is on TrustZone,
not on pure hardware.

## 4. What the shim must *not* do

- **Do not assume the stock `pe_arm_init_interrupts` can be made to succeed.** It cannot; the
  board-class set is closed.
- **Do not take the interrupt numbers from the ARM ARM.** Use the observed ones (§3.2).
- **Do not map `reg` as offsets to satisfy the stock function.** The shim computes addresses
  directly; the Apple convention stops binding once the function is replaced.
- **Do not enable the timer interrupt before the vector/base state is ready.** The payload's
  ordering (`gic.c`: distributor → CPU interface → arm → enable delivery last) is
  hardware-validated; keep it.

## 5. What cannot be settled from the host

Stated plainly, because this document is a specification and not evidence. Two entries that
were here in the first draft have been *removed* by reading further — §2.2's
`ml_init_timebase` question and §2.3's timebase question — which is the intended direction
for this section:

- **Resolved since the first draft:** `ml_init_timebase` was read (see §2.2). It is a pure
  registration function with no Apple timer assumptions — the literal risk of "the function
  will not accept our `tbd_ops`" does not exist. It was replaced by a sharper, concrete one:
  the `&BootCpuData` guard, which fails silently.
- **Whether FIQ is *usable* on MSM8974** — narrowed considerably in §6, which is the most
  useful result in this document: the vendor tree says FIQ is a secure-world privilege here,
  and XNU already contains a complete IRQ-based timer path selected by a single macro. What
  remains open is whether that path builds and runs, and which interrupt its timer uses.
- ~~Which timebase is authoritative.~~ **Resolved in §2.3:** the software TBL is dead code
  on this build, `CNTPCT` is what everything reads, and the timer therefore need not be
  periodic.
- **Anything about SMP.** `ml_processor_register`, IPIs and the CPU startup path are all
  Apple-shaped and untouched here.

These are where Phase 3's real risk sits, and each is a reading task before it is a coding
task.

## 6. FIQ on MSM8974: evidence, and a concrete alternative

This is the largest risk in Phase 3 (§2.3 establishes that XNU's ARM timer path requires a
platform FIQ handler). Reading the vendor tree narrows it considerably — not to an answer,
but from "unknown" to "probably blocked, and here is a path already in XNU's source".

### 6.1 The evidence that FIQ is a secure-world privilege here

Three independent pieces, and it is worth being precise about the strength of each:

1. **The vendor's own comment.** `arch/arm/mach-msm/msm_watchdog.h:28`, attached to
   `bool use_kernel_fiq`:

   ```c
   /* You have to be running in secure mode to use FIQ */
   bool use_kernel_fiq;
   ```

   Precisely: this is a statement about *the watchdog's bark* being delivered to the kernel
   as FIQ — see `msm_watchdog.c:437-439`, where `appsbark_fiq = pdata->use_kernel_fiq` is
   gated on `!pdata->has_secure`. It is the closest vendor statement available, and it is
   about FIQ on this family, but it is about one hardware block's use of it.

2. **The device does not use it.** `msm8974.dtsi`'s watchdog node carries
   `qcom,bark-time`, `qcom,pet-time` and `qcom,ipi-ping` — no `qcom,use-kernel-fiq`, and the
   binding doc does not list the property at all. And `has_secure`/`use_kernel_fiq` appear
   only in the pre-DT driver (`msm_watchdog.c`, used on 8960/8064); the DT driver this device
   uses (`msm_watchdog_v2.c`) references neither.

3. **The payload's experience.** Every interrupt it has driven on this device is IRQ —
   GIC SGI, timer PPI, the dead-man, the watchdog bark all go through IRQ. It has never taken
   an FIQ, and the project's SGI/timer selftests do not exercise that path.

None of these is an architectural datasheet statement. Together they make "FIQ from the
non-secure world works on MSM8974" a claim that would need evidence rather than one we can
assume.

### 6.2 The alternative is already in XNU's source

XNU's ARM tree contains a **complete IRQ-based decrementer path**, selected by the same macro
that selects the FIQ one — `locore.s:147`:

```asm
        adr     pc, Lexc_irq_vector
#if __ARM_TIME__
        adr     pc, Lexc_decirq_vector          /* <-- IRQ-based decrementer */
#else
        mov     pc, r9                          /* <-- FIQ handler, what we get today */
#endif
```

and `Lexc_decirq_vector` (`locore.s:189`) leads to `fleh_decirq` (`locore.s:1491`), which is
a full user/kernel-entry handler, not a stub. `start.s:135` already patches the vector table
entry for it, and `start.s:430` generates its address definition — so the plumbing exists on
both sides.

**So `__ARM_TIME__` is the switch between "the timer arrives as FIQ" and "the timer arrives
as IRQ", and it is currently undefined, which is why the FIQ path is the live one.**

That reframes the risk. Rather than "MSM8974 may not permit what XNU requires", the
candidate resolution is: **define `__ARM_TIME__`, and the timer moves to an IRQ path that the
hardware demonstrably supports** — the payload takes timer IRQs today.

Reading the remaining guarded sites strengthens this, because the two paths differ in a way
that is not merely about which vector fires. In `ml_get_decrementer` /
`ml_set_decrementer` (`machine_routines_asm.s:1004`, `:1029`):

```asm
#if __ARM_TIME__
        mrc     p15, 0, r0, c14, c3, 0      /* read CNTV_TVAL - a real hardware timer */
#else
        ldr     r0, [r3, CPU_DECREMENTER]   /* read the software counter */
#endif
```

and the `#else` arm of `ml_set_decrementer` switches mode to reach a FIQ-banked register:

```asm
        msr     cpsr_c, #(PSR_FIQ_MODE|PSR_FIQF|PSR_IRQF)   /* enter FIQ mode ... */
        mov     r12, r0
        str     r12, [r8, CPU_DECREMENTER]                  /* ... to touch r8 */
```

So the FIQ coupling is **structural, not incidental**: the non-`__ARM_TIME__` path keeps the
decrementer in a register banked to FIQ mode, which is exactly why it needs a FIQ handler at
all. The `__ARM_TIME__` path uses the architectural timer and touches no FIQ-banked register.
That is the shape of a path designed for platforms whose timer is a real hardware timer — the
situation here — rather than for the legacy software-decrementer scheme.

### 6.2.1 And it moves the timer from CNTP to CNTV — for a reason that does not apply here

Worth flagging before anyone programs it: `__ARM_TIME__` reads and writes **`CNTV_TVAL`**
(`c14, c3, 0` — the *virtual* timer), where the payload programs **`CNTP`** (`c14, c2, 0` —
the *physical* timer) and observes its interrupt on intid 19 (`§3.2`).

XNU states why, in a comment on `fiq_context_init` (`machine_routines_asm.s:924-933`) — and
quoting it is the point, because it is a statement about Apple's hardware, not about the
architecture:

```c
/* Despite the fact that we use the physical timebase
 * register as the basis for time on our platforms, we
 * end up using the virtual timer in order to manage
 * deadlines.  This is due to the fact that for our
 * current platforms, the interrupt generated by the
 * physical timer is not hooked up to anything, and is
 * therefore dropped on the floor.  Therefore, for
 * timers to function they MUST be based on the virtual
 * timer.
 */
```

**"For our current platforms, the interrupt generated by the physical timer is not hooked up
to anything."** On MSM8974 that is measurably false: the payload armed `CNTP`, and its
interrupt arrived on intid 19 — recorded in `experiment-12` and cited in §3.2. The physical
timer's interrupt *is* wired here.

So the `__ARM_TIME__` path's choice of CNTV is a workaround for an Apple-specific wiring
decision, not an architectural requirement, and this device does not share the limitation.
Three consequences:

1. **The route still works** — CNTV is available on MSM8974 and the path avoids FIQ, which is
   its value. But it is now clear it buys that at the cost of using the *less* convenient of
   two timers that both function here.
2. **A third option exists that neither source discusses:** keep `__ARM_TIME__` for the IRQ
   vector, but point the shim's decrementer callbacks at `CNTP` rather than `CNTV`. The
   `tbd_ops` functions are the shim's own code, so the choice of counter inside them is the
   shim's; `ml_get_decrementer` calls a non-NULL callback in preference to its own default
   (`machine_routines_asm.s:1016`), so supplying `CNTP` there would override the CNTV default.
   That would use the already-measured intid 19 and the payload's existing, validated timer
   code. **This is a hypothesis from reading, not a tested design**, and it should be checked
   against what `fiq_context_init` writes to `CNTV_CTL` under `__ARM_TIME__` — it enables
   CNTV unconditionally, which may matter if both timers end up armed.
3. **CNTV's interrupt id remains unknown** either way, for the option where it is used. The
   ARM architecture assigns CNTP and CNTV different PPIs; the payload has only ever observed
   CNTP's. Presumably-mapped, not measured.

### 6.3 What is not yet established, and the specific next checks

Stated so the next session does not over-read §6.2:

1. **`__ARM_TIME__` is defined nowhere in the tree.** That may mean it is legacy for
   platforms XNU no longer builds, and that enabling it leaves a path that does not compile or
   was never finished. It must be *built*, not assumed. This is a host-side check and can be
   done without the device.
2. **`__ARM_TIME__` does more than move the vector** — now read, and the list is short and
   tractable: the vector (`locore.s:147`), the exception-vector table slot (`locore.s:188`),
   `fiq_context_init` (`machine_routines_asm.s:924`, which enables CNTV instead of loading the
   FIQ bank), the two decrementer accessors (`:1004`, `:1029`), `ml_get_timebase` (`:979`,
   where the `|| __ARM_TIME_TIMEBASE_ONLY__` means this arm is already live), and
   `user_timebase_allowed` (`machine_routines.c:1118`, returns TRUE instead of FALSE). Nothing
   obviously legacy or half-finished, which is evidence for rather than against the route —
   but it is still only reading, and the path must be *built* to confirm it compiles.
   The two `tbd` decrementer callbacks remain meaningful under this path (§6.2.1 point 2).
3. **Whether FIQ is *actually* blocked** — worth testing directly and cheaply if the device
   comes back: the payload could enable a timer as FIQ and see whether it arrives. That is a
   small, non-persistent experiment, and it would settle by observation what the vendor
   comments only suggest.
5. **Which interrupt CNTV arrives on (§6.2.1).** Only measurable on the device, and the
   payload's GIC code could do it in a few lines — program `CNTV_TVAL`, enable `CNTV_CTL`,
   and log the IAR value. Small, non-persistent, and it removes the last unknown from the
   candidate resolution.
4. **A third possibility neither source rules out:** that the payload runs in a
   TrustZone-configured state where FIQ *is* routed to the non-secure world, since aboot
   loads us directly. The only way to know is to try.

Check 1 is the highest-value next action, and it needs no hardware.
