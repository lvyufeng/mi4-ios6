# 599: the window is unreachable on the arm that is parked, and that is a fixed point rather than an intention

594 built the arm that skips the idle window, 595 gated it and parked it as the image the next power
press sends, and both said why it should get past the death: the switch compiles out 514's one-shot
repair, so `SIGPdisabled` stays set, so `cpu_idle` leaves by its first door on every pass and never
enters the window. That is a story about a control flow, verified only by the link-time count the build
makes (`sigicalls_want = 1 - IDLE_NO_SLEEP`, `build_entry.sh:28198`).

This step turns it into a property of the image, and the property is stronger than the story: **on the
sleepless image the window has no route in at all**, because `SIGPdisabled` is a *closed fixed point* of
this port and the gate the window sits behind can therefore never open. One press is the scarcest thing
this project has; what follows is the check that the arm is worth it.

Host-side only: one new tool under `tools/`, no build, no device, no `fastboot`, no `adb`, nothing
written to storage. **TWRP stays withheld** — 「如果os已经能进去了的话」 is unmet.

## 1. The gate, and the test in front of it, read out of Apple's own source

`osfmk/arm/cpu.c:118` is the whole control flow, and the image matches it instruction for instruction:

```c
void __attribute__((noreturn))
cpu_idle(void)
{
        ...
        if ((!idle_enable) || (cpu_data_ptr->cpu_signal & SIGPdisabled))
                Idle_load_context();            /* :123  -> 0x8000d97c, a TAIL branch */
        if (!SetIdlePop())
                Idle_load_context();            /* :125  -> the same door   */
        ...
        platform_cache_idle_enter();
        cpu_idle_wfi((boolean_t) wfi_fast);
        platform_cache_idle_exit();             /* <- the pop that dies is inside this */
        ClearIdlePop(TRUE);
        cpu_idle_exit();
}
```

The three compiled facts, all read out of `out/stage90/xnu_arm_entry.elf`:

```
cpu_idle @ 0x8000d960   ldr r0, [r5, #0x28]              <- cpu_data->cpu_signal
cpu_idle @ 0x8000d964   cmn r0, #1                       <- == (cpu_signal & 0x80000000)
                        ble     -> 0x8000d978            <- SIGPdisabled set: the door
cpu_idle @ 0x8000d96c   bl  __wrap_SetIdlePop            <- THE GATE
cpu_idle @ 0x8000d974   bne     -> 0x8000d980            <- SetIdlePop() != 0: the window
cpu_idle @ 0x8000d97c   b   __wrap_Idle_load_context     <- otherwise, the door (fall-through)
```

`cmn r0, #1` / `ble` on a value loaded from `[r5, #0x28]` **is** `cpu_signal & SIGPdisabled`:
`SIGPdisabled` is `0x80000000U` (`cpu_internal.h:68`) and `cmn r0, #1` sets the flags for `r0 + 1`, so
`ble` means `r0 <= -1`, i.e. bit 31 set. The offset `0x28` is `cpu_data->cpu_signal`, and the tool
**asserts** it rather than assuming it, so a struct change moves the check into a refusal instead of
reading the wrong word.

## 2. `cpu_idle` is `noreturn`, so "door 1" is the healthy idle and not a spin

The two exits are **tail branches**, not calls — `0x8000d97c` in `cpu_idle` and `0x8000dad4` in
`cpu_idle_exit` — for the two `Idle_load_context()` calls at `cpu.c:125` and `cpu.c:169`. Both are
followed by the `mov lr, pc` idiom and neither returns, which is what `__attribute__((noreturn))`
means on this compiler.

That is worth stating because it corrects the natural reading of "the bypass at `0x8000d978` that runs
no WFI and no cache window" (594 §1): the door is not a degraded fallback that degrades into a spin.
**`Idle_load_context` is `Idle_load_context`** — the routine that saves the idle thread's context and
dispatches another one. Leaving by the door *is* XNU's idle working. The window path is the exception,
and it exists only for the case where the CPU is about to actually stop.

## 3. The window's single entry, and the gate that dominates it

The three wrappers the window runs through each have **exactly one `bl` site in the whole image**, and
all three are in `cpu_idle`, 188/204/208 bytes after the gate:

```
__wrap_platform_cache_idle_enter   0x8000da28   in cpu_idle
__wrap_cpu_idle_wfi                0x8000da38   in cpu_idle
__wrap_platform_cache_idle_exit    0x8000da3c   in cpu_idle
```

and the region they live in, `[0x8000d980, 0x8000da50)`, has **no branch into it from outside**: every
branch in `cpu_idle` whose target lands inside the region starts inside it, except the gate's own
`bne`. So the gate *dominates* the whole window — the three calls, the WFI and the exit — and shutting
the gate makes `platform_cache_idle_exit`'s `pop {fp, pc}` unreachable.

`__wrap_SetIdlePop` has a second call site at `0x8000da20`, and it is **inside** that region, not a
second way in: it is `if (cpu_data_ptr->rtcPop != lastPop) SetIdlePop();` (`cpu.c:150`), reachable only
by a pass that is already through the gate. The tool's first draft assumed one site and refused on the
real image; the second draft assumed the door was the very next instruction after the gate's branch and
refused too. **Both refusals are the tool doing its job** — the shape was written down from the source
and the compiler's output differed twice, which is exactly the class of thing this project keeps paying
for, caught before it reached a conclusion instead of after.

## 4. Why the gate can never open: `SIGPdisabled` is a closed fixed point

The bit is **set** at init (`cpu.c:365`, in `cpu_data_init`). The only writer that **clears** it is the
`disable_signal == FALSE` arm of `cpu_signal_handler_internal` (`cpu_common.c:401-402`), which is the

```
80012ed0   mvn r1, #-2147483648    ; 0x80000000     <- ~SIGPdisabled
80012ed4   bl  hw_atomic_and
```

pair inside that function. And that function has exactly one caller:

```c
osfmk/arm/machine_routines.c:605   *ipi_handler = cpu_signal_handler;   /* inside ml_processor_register, :526 */
```

— it is registered as **the IPI handler**, and `cpu_signal_handler()` (`cpu_common.c:382`) is nothing
but `cpu_signal_handler_internal(FALSE)`.

So clearing the bit requires an IPI. And sending one is guarded by the same bit:

```c
cpu_common.c:343   if (!(target_proc->cpu_signal & SIGPdisabled)) { ... PE_cpu_signal(...); }
cpu_common.c:374   if (!(target_proc->cpu_signal & SIGPdisabled)) { ... }
```

**With the bit set, no IPI is sent** — and with `real_ncpus == 1` (measured for this image in 549)
there is no other CPU to send one anyway. So the state is its own fixed point, closed on itself: set ⇒
no IPI ⇒ no handler ⇒ never cleared.

And note what the argument does *not* depend on: it does not matter whether this port can deliver an
IPI to itself, or whether the GIC route is wired. **Nothing sends one**, so nothing arrives.

That makes 514's repair — `cpu_signal_handler_internal(FALSE)` called in software, from the park's own
poll — not a convenience but *the IPI*, simulated. And 593's observation ("uniprocessor port ⇒ no IPI ⇒
`SIGPdisabled` stays set ⇒ 32768+ passes leave `cpu_idle` by door 1, then the repair clears the bit once
and the very next pass enters the window and dies") is now derived rather than inferred.

## 5. The measurement, and the differential

`tools/check_idle_window_unreachable.py` asserts §1–§3 and then counts the clearers, which is the one
number that separates the two arms:

```
== (D) the clearer: every `bl cpu_signal_handler_internal` in the image ==
  0x8047be64  in __wrap_poll            <- the frozen 574/535 arm:  1 site, in the park's poll
  (none)                                <- the sleepless arm:       0 sites
```

| image | clearers | verdict |
| --- | --- | --- |
| `/tmp/r594/frozen-payload/xnu_arm_entry.elf` (535/574) | **1**, at `0x8047be64` in `__wrap_poll` | **reachable EXACTLY ONCE** — the gate opens on the pass after 514's repair, which is the baseline's measured shape (520 and 533 each publish one `sip_seq`/`pce_seq`/`wfi_seq` record against 16 door records) |
| `out/stage90/xnu_arm_entry.elf` (the sleepless arm) | **0** | **UNREACHABLE** — nothing in the image clears the bit, so the gate never opens and the `pop` is never executed |

Both exit 0, each with the verdict its regime names. So 594/595's claim is not just consistent with the
image: it is the *only* thing the image can do.

## 6. And the ceiling is not "time stops"

The fixed point has an obvious cost worth checking before the press: the IPI handler also calls
`rtclock_intr` (`cpu_common.c:407`, servicing `SIGPdec`), so if that were the tick's only route, the
sleepless arm would have no timebase, the park's timed poll could never expire, and rung 1 of the
pre-registered ladder would be unreachable — the arm could only ever produce rung 0.

It is not the only route. The image's four `bl rtclock_intr` sites:

```
0x8000c698  in entry_irq_handler        <- not the IPI handler
0x80012f48  in cpu_signal_handler_internal
0x8001acf4  in L_cond_extern_1617_shim  <- the locore.s:1617 shim (fleh_decirq_handler's call)
0x8001af50  in L_cond_extern_1858_shim  <- locore.s:1858
```

Three of the four are outside the IPI handler, and the two shims are the `COND_EXTERN_BLNE` forms of
`locore.s:1621` / `:1862` — `bl rtclock_intr` straight out of the exception vectors, the first of them
inside **`fleh_decirq_handler`**. So the tick arrives on the decrementer's own vector and does not need
the IPI. The tool refuses if that ever stops being true, because then the arm's readings would mean
something different.

That also explains a fact the phase already had and had not connected: the baseline's own ≥32768
door-1 passes coexisted with the userland phase's *timed* calls (533's `xnu_live_poll_timeout_ms` at
5 ms and 40 ms, both of which expired) — which is only possible because time advanced with
`SIGPdisabled` set.

## 7. Verified on six states, and how the tool was shown to fail

The two real images are artifacts; the four controls are doctored **copies** under `/tmp`, patched by
absolute virtual address, never in place:

| state | what was changed | verdict |
| --- | --- | --- |
| the frozen 574/535 arm | — | **EXIT=0**, *reachable EXACTLY ONCE* |
| the sleepless arm in `out/` | — | **EXIT=0**, *UNREACHABLE* |
| `/tmp/r599-d1.elf` | the frozen arm's `bl cpu_signal_handler_internal` planted at `0x8047be64` | **EXIT=0**, and the verdict **flips to *reachable EXACTLY ONCE*** — so the differential is what drives it |
| `/tmp/r599-d2.elf` | `0x8000d964`: `cmn r0, #1` → `cmp r0, #1` | **EXIT=1**: "expected exactly one `cmn r0, #1` before the gate; found 0" |
| `/tmp/r599-d3.elf` | `0x8000d934` → `b 0x8000da28`, an intruder branch into the region | **EXIT=1**: "branch(es) into the window region from outside it … the region is not dominated by the gate" |
| `/tmp/r599-d4.elf` | `0x8000d97c`: the door's `b` → `bl` | **EXIT=1**: "`cpu_idle` is declared noreturn, so a returning call here means the compiled shape is not the one this file's note describes" |

`d1` is the one worth keeping: it is the *same bytes as the frozen arm* in the one place that decides
the verdict, so it proves the whole chain is driven by the clearer count and not by anything
incidental. `d3` and `d4` are the two structural assertions the verdict rests on, each shown to fire on
its own predicate.

## 8. What this does not do

* **It does not boot anything, and it changes no arm, payload, gate or prediction.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…`, the gate still exits 0, and the press is still
  the user's.
* **It does not say the boot will survive.** It says the death at the `pop` cannot be what stops the
  sleepless arm, because that arm cannot reach the instruction. Whether the machine gets *further* than
  the idle is the run's question, and rung 1 is the reading that decides it.
* **It does not prove the port is otherwise healthy.** A closed `SIGPdisabled` means the IPI path never
  runs on this port at all, which is a fact about the port and not something this step fixes; the
  tick's independence (§6) is what keeps it from being fatal, and that is a reading of *one* route.
* **It does not settle 535's non-return** (572 §5, 574's `b1`) or the two candidate `entry_trace.c`
  repairs 597 §7 named. Those still need an arm, and this step does not build one.
* **It does not replace 547 §4.** The frozen arm's pre-registered death is still the reading for *that*
  arm; this step is about the one that is actually parked.

## 9. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One new
host-side tool under `tools/`; reads of the two entry ELFs, of Apple's ARM sources (`cpu.c`,
`cpu_common.c`, `cpu_internal.h`, `machine_routines.c`, `locore.s`), and four doctored *copies* under
`/tmp`. The parked frozen payload and the sleepless payload are unmodified, byte for byte — verified by
re-running the gate after the step, not by assumption. `fastboot boot` only — never `flash` — so no
outcome of any of this can write to storage.
