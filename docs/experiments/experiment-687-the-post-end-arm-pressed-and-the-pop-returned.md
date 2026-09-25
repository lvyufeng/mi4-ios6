# 687: the press — the post-end arm came back, and the `pop {fp, pc}` it lets run RETURNED

The press 686 armed was fired. **The device returned and the log came back**, and the key the arm was built
for is present: **`xnu_live_slot_post_calls=0x00000001`** — the idle exit's own `push {fp, lr}` / `pop {fp, pc}`
pair, the instruction every capture since 520 has died on, **retired**. On this arm the operation's
clean-and-invalidate and its two restore stores carried a `pop` that the measure arm dies on.

**This is 662 §4's first explanation measured rather than assumed**, and it moves the frontier past the `pop`:
what ends a boot on this image is no longer the exit. The run was ended deliberately at the arm's own ending on
the far side of the `pop`, so this log says the `pop` returned and says nothing about how far the boot goes
next — that is the next step's question, and it is named in §7.

**One press spent: one readiness chain (5/5), one gate (exit 0, 559 stdout lines), exactly one runner, one
non-persistent `fastboot boot`. Nothing flashed, nothing written to storage, no device state changed but the
boot.** The firer was the armed launcher `press-on-clear.v5.sh` (sha `7f23cae5…`), armed for
`armed-post-endrun-ee7aca87` with the flags it derived from the arm's own switches.

## 1. The press

| time (UTC) | event |
| --- | --- |
| 13:45:58 | the launcher armed: `armed-post-endrun-ee7aca87`, flags `--allow-xnu-entry`, sha `7f23cae5…` |
| 13:46:32 | the phone is on the bus (fastboot `[]`, adb `[4a2fe00b device]`), neighbour `33e80afe` absent |
| 13:46:33 | **readiness exit=0** — 5 of 5, including `the press would be caught` |
| 13:46:33 | `preflight_boot_check.sh --allow-xnu-entry` |
| 13:46:58 | **`GATE EXIT=0`, 559 stdout lines** |
| 13:46:58 | the one run: `run_and_capture.sh --allow-xnu-entry --expect-arm=armed-post-endrun-ee7aca87` |
| 13:47:56 | **`RUNNER EXIT=0`** — returned and captured, **20 s** after the send |

The bytes sent were checked against the gate's reading and were unchanged across the send:
`1e2abec44e50ffd7…`. The capture is **598,302 bytes**, sha256
`98b00b282e946222879385107b8c8177ba7b7597e643a413ae81668626ca2654`, archived as
`out/stage90/captures/687-post-endrun-pressed-2026-09-25-last_kmsg.txt`.

The launcher's own start block names the three log paths and its own sha, so this log says which program
fired the press. It refused nothing, fired once, and exited.

## 2. The key, and it is the arm's whole question

```
xnu_live_slot_post_calls=0x00000001      <-- THE ANSWER
xnu_live_slot_pre_calls=0x00000001
xnu_live_slot_rtcpre_calls=0x00000001
xnu_live_slot_cwe_win=0x30c57879         C clear - the window really opened where the image says
xnu_live_seam_calls=0x00000001   xnu_live_seam_op=0x00000001
xnu_live_seam_lr=0x800462dc      xnu_live_seam_sp=0x8054fec8   xnu_live_seam_sctlr=0x30c57879
xnu_live_seam_b0=0x800b2648      xnu_live_seam_b1=0x8047c9c0
xnu_live_seam_a0=0x800b2648      xnu_live_seam_a1=0x8047c9c0
xnu_live_seam_end_run=0x00000000       xnu_live_seam_post_end_run=0x00000001
xnu_live_poll_seq=0x00000001
```

* **`slot_post_calls=1`** is published by `entry_slot_null_note(&g_slot_post)` from the wrapper *after*
  `__real_platform_cache_idle_exit()` returns, so it is the project's own name for "the exit completed". It
  did. **The `pop` at `0x8004633c` returned.**
* `seam_post_end_run=1` is the arm's identity key — this is 686's arm, not 653's and not 678's — so the
  reading above is not a pass that happened to survive: the arm *is* the one that lets the `pop` run and ends
  afterwards.
* The pair came back **unchanged** (`0x800b2648 / 0x8047c9c0`), read with `SCTLR.C` clear
  (`0x30c57879`), which is 686 §3's point: it is a DRAM reading and it is not this press's answer.
* The live channel is **not full** — 4454 records of 0x2000, and no `xnu_live_capped` — so an absent key in
  this log is an event that did not happen, not a truncated channel.

## 3. The death this log also reports is the ending's own store, and the fault names its call site

The log carries the abort 684 measured, and it is the ending's first store:

```
panic(cpu 0 caller 0x80454668): kernel abort type 4: fault_type=0x3, fault_addr=0xfa0065c
this log's abort: xnu_live_sleh_lr=0x8047c9ec pc=0x8047b488  storm=0x00000009
```

Resolved against this arm's own ELF: `entry_seam_end_run` is at `0x8047b47c` and the faulting instruction is
its fourth, `8047b488: str r3, [r2, #1628]` with `r2 = 0x0fa00000` — the address `0x0fa0065c`, the
restart-reason word. **And the recorded `lr` is `0x8047c9ec`, the instruction after
`8047c9e8: bl 8047b47c <entry_seam_end_run>`** — the last call in `__wrap_platform_cache_idle_exit`'s body,
which is exactly where this arm's ending is placed. The stub's own record of its call site therefore says the
ending ran at the wrapper's far end and **not** at the seam, which is this arm's identity read a second way.

So no cell of this press needed the boot to survive: the `pop` returned, the ending ran, its store faulted
because XNU's pmap owns TTBR0 by then (684's finding, unchanged), and the entry image's own abort path and
`entry_epilogue` are what brought the phone back.

## 4. Why this is 662 §4's first explanation, and how strong the claim is

The measure arm (the 574 park, and every capture since 520) intercepts at the seam with **no** operation and
dies at the `pop`. This arm runs **the same operation as 653's arm** and its `pop` returned.

* **Measured here:** with the PoC clean-and-invalidate and the two restore stores in place, the `pop` returns.
* **Inferred, and stated as an inference:** 653's operation arm was pressed once (03:53:50, exit 2, no log),
  and this arm differs from it by **one call site placed downstream of the `pop`** — nothing the `pop` reads
  changes. So the `pop` returned on 653's arm too and its non-return is a hang *after* the exit. n = 1 on each
  side, and a hang destroys its own reading, which is why 662 §4 refused to claim this; the difference now is
  that one side has a log.
* **Not claimed:** that the boot stays up. See §7.

## 5. The runner read it with 686's rules

The `run_and_capture.sh` prose added in 686 printed the arm's own cell, and it is worth quoting because it is
the sentence the next step builds on:

> **AND ON THIS ARM THAT IS THE PRESS'S ANSWER (686).** `xnu_live_seam_post_end_run=0x00000001` says the ending
> is the one on the FAR side of the pop … the operation's clean-and-invalidate and its two restore stores
> carried a pop that every earlier run died on. … **the death this log also reports is the ending's own store
> and NOT the pop** … the frontier is not the pop on this arm.

The readiness row 4 correction made in the same step printed too (the equal pair as *this image's shape* rather
than *the operation was inert*, with 684's `b1 == a1 == 0x8047c9c0` named). Both files behaved as the step
that wrote them said they would.

## 6. What was already true and is re-read here

The userland phase is in this log and is unchanged: the fixture's own syscalls (`open` 2 calls with the first
answered by a driver, `read` returning the fixture's `0xfeedface`, `getpid` 1, `exit` 1, `wait` 2, and two AST
records). It is the **floor** — 520 and 533 carry all of it, because the whole phase runs before the exit — and
having it says only that the OS still boots to pid 1's syscalls. The hardware watchdog was armed and counting
and the dead-man armed, and `platform_reboot entered` is 0: neither net fired, and neither was needed.

## 7. What this does not do, and the next question

**It does not enter the OS in the sense the goal needs, and it does not move the goal's own criterion.** The
boot is *ended on purpose* on the far side of the `pop`; whether it would have continued, and for how long, is
not in this log. `xnu_live_poll_seq=0x00000001` and `slot_pre_calls=1` say this was the first idle pass, so
"how far past the exit does the first pass get" is unanswered.

**And the storage condition is still unmet**: 「如果os已经能进去了的话」 asks for a boot that is *observed*
entering the OS and staying there, and this run is one that was told to stop. **TWRP-to-storage stays
withheld.**

**The next arm is the same ending moved one step further, and the site already exists.** `__wrap_cpu_idle_wfi`
(`0x8047c8b4`) is already wrapped and already calls `entry_note_wfi`, so an ending placed there — after the
`WFI` of the *next* idle pass, on a counted pass — says whether the boot reaches the WFI that follows the exit,
which is the first thing after the `pop` this project has an instrument on. That keeps 686's property (the
ending is downstream of the thing being asked about, and both cells come back with a log) and it asks the
question this press made askable.

## 8. State

One press spent; the arm in `out/` is unchanged (`armed-post-endrun-ee7aca87`, 11 files, recorded, gated
green) and remains revertible. The launcher exited at 13:47:57 (`done. gate=0 runner=0`), so **no firer is
armed** and 660's closure has released. No surface other than the boot was touched: nothing flashed, nothing
written to storage, no persistent state.
