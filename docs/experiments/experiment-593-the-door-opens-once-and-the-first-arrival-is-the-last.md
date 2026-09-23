# 593: the window opens once because 514's repair opens the door once, and the first arrival is the last

`why does the cache window open exactly once per boot` was left open as the one host-side question that
is aimed at the *goal* rather than at a reader, and it is answered here from the two archived captures and
the instruments' own partition — no device, no build, no arm. The answer changes which repair is worth
building: **the `pop` is reachable only through this project's own repair, so there is a lever that does
not have to win it.**

The frozen 574 arm is untouched (`914f45ac…` / `151425c4…` / `3bc72605…`) and unrun. TWRP stays
withheld: 「如果os已经能进去了的话」 is unmet.

## 1. The question, stated so it can be answered

520's log and 533's log both carry these, and they are the whole reading:

| key | 520 | 533 | what it counts |
| --- | --- | --- | --- |
| `xnu_live_door_seq` | 16 records, largest `0x00008000` | 16 records, largest `0x00008000` | entries into `__wrap_Idle_load_context`, published at powers of two ⇒ **≥ 32768** |
| `xnu_live_door_en` | `0x00000001` | `0x00000001` | `idle_enable` at each door record |
| `xnu_live_sip_seq` | `0x00000001` | `0x00000001` | calls to `SetIdlePop` ⇒ **exactly 1** |
| `xnu_live_sip_ret` | `0x00000001` | `0x00000001` | what that one call returned ⇒ **TRUE** |
| `xnu_live_pce_seq` | `0x00000001` | `0x00000001` | `platform_cache_idle_enter` ⇒ 1 |
| `xnu_live_wfi_seq` | `0x00000001` | `0x00000001` | the `wfi` ⇒ 1 |

So the window — enter, WFI, exit — ran **once**, and everything else left `cpu_idle` by the other door.
That is not a hypothesis about the counter's schedule: the schedule is "powers of two", so one record
*is* the count one, and sixteen records to `0x8000` is "at least 32768".

## 2. The answer is already in the instruments' own partition

`entry_stubs.c`'s 513 block states the partition, and this is it applied:

```
door 1 = machine_idle entries - SetIdlePop entries
door 2 = SetIdlePop entries whose answer was FALSE
door 3 = SetIdlePop entries whose answer was TRUE
```

`SetIdlePop` was entered once and answered TRUE, so **door 3 = 1 — that is the one window pass** — and
door 1 = `door - sip` ≥ **32767**. `door_en = 1`, and 513 says so in its own words: with `idle_enable`
true, door 1's disjunction "can only be true because of the `SIGPdisabled` bit" (the disassembly agrees:
`0x8000d95c: beq` is the `!idle_enable` arm, `0x8000d968: ble` is the `cpu_signal` arm, and both land on
one `mov lr, pc; b __wrap_Idle_load_context`).

Then 514 supplies the missing link, and it is one sentence of its own comment: `SIGPdisabled` "is never
cleared because the only thing that clears it is the platform's delivery of an interprocessor interrupt —
and this is a uniprocessor port". 514's repair is `cpu_signal_handler_internal(FALSE)`, the kernel's own
clearing, called **once** — `entry_trace.c:1221` guards it on `park_printed == 0u` and `:1288` sets that
flag, so it fires on pid 1's first long `poll` and never again — and, checked rather than assumed, **no
`#if` wraps it**: there is no build switch to revert, only code.

**So the causal chain is closed, and "exactly once" is a property of the repair, not of the window:**

1. Uniprocessor port ⇒ no IPI ⇒ `SIGPdisabled` stays set ⇒ `cpu_idle`'s first test is true on every pass
   ⇒ 32767+ passes leave by door 1 ⇒ no WFI, no cache window. That is exactly 512's "the idle does not
   sleep" (33,554,432 `machine_idle` entries, ≤ 127 decrementer writes) and it is also *stable*.
2. 514's repair clears the bit, once, on pid 1's first long `poll`.
3. **The very next pass** of `cpu_idle` falls past the first test, reaches `SetIdlePop` for the first time
   in the boot, gets TRUE, and enters the window for the first time in the boot.
4. That first window pass dies at `platform_cache_idle_exit`'s `pop {fp, pc}`.

The window does not "open once per boot" for any reason of its own. **It opens when the door is opened,
and the first arrival is the last.** 514 was designed to let the idle sleep; what it also did was make the
`pop` reachable for the first time.

## 3. A correction to a flagged discrepancy: 520's log must be resolved against 520's image

The phase record has carried an open item — 520's `xnu_live_sip_site = 0x8000d810` "resolves inside
`L_64ormorealigned` (a bzero loop), not `cpu_idle`", which "contradicts 538's documented site filter".

It does not, and the three keys agree on why. Resolved against **533's own ELF**, 533's own log's three
site keys are exact:

| key | 533's log | 533's ELF | the site |
| --- | --- | --- | --- |
| `door_lr` | `0x8000d980` | `0x8000d978: mov lr, pc` + 8 | the bypass's continuation |
| `sip_site` | `0x8000d970` | `0x8000d96c: bl __wrap_SetIdlePop` + 4 | `cpu_idle+0x3c`, the `SetIdlePop` call |
| `pce_caller` | `0x8000da2c` | `0x8000da28: bl __wrap_platform_cache_idle_enter` + 4 | the enter wrapper's call |

And 520's three are each **exactly `0x160` lower** — `0x8000d820` / `0x8000d810` / `0x8000d8cc`. One
key off by an arbitrary amount would be a stale stack word passing a plausibility test; **three keys off
by the same amount, whose true sites are known independently, is a version offset.** 520's image is not
archived, so its `cpu_idle` sat `0x160` bytes below 533's and there is nothing to resolve its keys
against today. That is the whole of the discrepancy: an address from one build read against another
build's ELF — this project's oldest defect class, in the form a *reader of archived captures* is most
exposed to. The rule it earns: **a recorded site is only resolvable against the image that produced it,
and the check that it is the right image is that the independently-known sites move together.**

## 4. What this buys: an arm that does not have to win the `pop`

The death is reachable **only through 514's one-shot repair.** Before it, the machine spun in door 1 and
stayed alive for at least 33 million passes. So there is a lever that does not touch the `pop` at all:

> **Do not clear `SIGPdisabled`** (or make `SetIdlePop` refuse). Then `cpu_idle` leaves by door 1 on every
> pass, `__wrap_platform_cache_idle_enter` / `_wfi` / `_exit` are never called, and the `pop {fp, pc}` at
> `0x8004633c` **is never executed** — not survived, not repaired, not reached.

That is the shape the goal actually wants: XNU reaches userland *before* the idle exit, so a port whose
idle never sleeps is a port that has reached the OS and stayed there.

**Pre-registered, as one arm, not built** (the phase runs one arm at a time and 574 is frozen):

| item | value |
| --- | --- |
| the change | a build switch that skips 514's `cpu_signal_handler_internal(0)` call — a switch, because the call is unconditional code and the alternative would be deleting a repair |
| what it proves if it works | the OS comes up and stays up with the idle spinning: 「起码要能进入操作系统」 reached, and the `pop`'s death is confirmed as reachable only through the sleep path |
| falsifier | the death moves *earlier* than the idle exit, or the machine stops returning at all: then the death is not in the sleep path and this axis is wrong |
| what it costs | the idle never sleeps — the port defect 512/513 measured and 514 repaired — so the CPU spins; a bring-up stopgap traded for a boot, and reversible with the same switch |
| what it does *not* do | it does not explain the `pop`'s death, and it does not make the idle correct; if the OS comes up this way, 574's `b1` and the 535/590 axis are still owed |

**Honest limit, stated rather than implied:** the pre-514 spin is known to have been *alive*, from 512's
33-million-entry count, but that count was taken at an earlier phase — the full path into userland has
never been observed with the idle not sleeping. So this is a candidate with a falsifier, not a fix.

## 5. Safety

No device action, no `fastboot`, no `adb`, nothing written to storage, no build input, no arm: reads of
two archived captures (`out/stage90/captures/520-2026-09-22-last_kmsg.txt`,
`533-2026-09-23-last_kmsg.txt`), disassembly of two archived ELFs and of the frozen one, and reads of the
instruments' own sources. **The frozen 574 arm is unchanged and unrun; the gate on disk is HEAD (`6b228b7`)
and green.** A boot is still one power press away, and `fastboot boot` only — never `flash` — so no outcome
of any of this can write to storage.
