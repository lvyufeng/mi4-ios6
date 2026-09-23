# 606: the run has a ceiling, and it is the net — 28 s, on a successful run too

594's arm exists to answer one question: does the machine get past the `pop {fp, pc}` at the idle exit?
604 §7/603 §7 left the sweep owed. Neither asked the prior question: **how long can a run of this arm
last at all?**

It is not open-ended. The payload arms the MSM8974 hardware watchdog (`stage90_main.c:1205`) before the
jump, nothing in the image that runs afterwards carries that page, and so **the SoC resets the machine
on its own clock** — and that is true of a run that goes *well* as much as of a hung one. The gate has
measured the middle of that argument since 584 (the entry image carries zero addresses in
`[0xf9010000, 0xf901ffff]`); what it did not do is draw the consequence, and an operator reading a green
gate would reasonably assume a good run simply keeps running.

Host-side only: narration added to `preflight_boot_check.sh`'s XNU-entry section, reads of two archived
captures and of the gate's own measurement. No build, no device, no `fastboot`, no `adb`, nothing
written to storage, no boot. **TWRP stays withheld.**

## 1. The ceiling, as a number read out of the tree

The clause does not write 28 in prose — it reads the two constants out of `stage90.h`, which is rung 1b's
rule (a threshold is quoted from the file that defines it) and refuses if either cannot be read:

```
    STAGE90_HW_WATCHDOG_TIMEOUT_S    25 s   (the bark)
    STAGE90_HW_WATCHDOG_BITE_GAP_S   3 s   (bark -> bite)
    so the bite is at most 28 s after the payload arms it, and the payload
    spends about 1 s of that before the jump (measured: 533's payload timebase sample to the
    idle's first read is 1.1805 s on one 19.2 MHz counter).
```

The 1 s is 603's measurement, reused rather than re-derived: the same one-clock arithmetic that excludes
the net as the cause of a failed park also says how much of the net's budget the boot consumes before the
park starts.

## 2. The three legs, and which of them the gate measures

| leg | how it is established |
| --- | --- |
| the net is armed before the jump and stays armed | `stage90_main.c:1205`; and the payload's own `disarm_hw_watchdog_en=0x00000001` readback at the jump — in 533's capture *and* in 513's |
| nothing that runs after the jump carries its page | **measured by the gate**, on every run: pool word 0, `movt` 0, `mov`/`mvn` immediate 0 in `[0xf9010000, 0xf901ffff]`, with the payload as a positive control (it arms the net, so it must carry the page, and it does — 10 `movt`) |
| nothing pets it through the GIC section either | **not** established, and the clause says so: the watchdog shares its 1 MB section with the GIC (`hw_watchdog.c:53`) and `entry_gic.c:377` maps exactly that section, so an image can reach the registers without materialising an address in the page |

So the claim is a **ceiling**, not a proof of the reset path — the same narrower claim the net-reach
paragraph already makes, drawn to its consequence.

## 3. What the ceiling looks like, measured

513's two captures (2026-09-21) are runs of an image with **no repair** — the same regime the sleeper
arm's switch restores — and they are the hardware reading of this:

| in 513's captures | count |
| --- | --- |
| `panic` | **0** |
| `Attempting system restart` / `MACH Reboot` | **0** / **0** |
| `pc_sample_watchdog_fired` (the payload's software dead-man) | **0** |
| `platform_reboot entered` | **0** |
| `xnu_live_poll_seq` | 4, with `xnu_live_poll_over` published |
| the last payload record | `xnu_live_poll_over` — the fifth poll |

**No software path ended either run.** Not XNU's panic-restart path, which 533's capture *does* carry
(`MACH Reboot` 1, `panic` 4 — 533 died at the `pop` and XNU restarted the machine itself); and not the
payload's own 60 s software dead-man. The captures simply stop after the work, and the phone was back on
Android (they were read out of `/proc/last_kmsg`, which only Android can hand over).

So the shape a good run of this arm makes is: **`poll_seq=3`, `poll_over`, and then the records stop —
with no fault text.** "The log ends without a fault" is the ceiling, not a missing reading, and that is
now printed where the operator reads the green gate.

## 4. What this does and does not mean for the goal

**It does not make 「起码要能进入操作系统，把基础驱动跑起来」 unreachable.** That condition is about
*entering* the OS and having a driver answer, and 596 measured exactly that in the archived baselines:
the driver's `open` returning 0, the `read` that left `MH_MAGIC` in a user page, `getpid` → 1, a forked
child reaped by `wait`, two user ASTs. All of it happens in the first seconds, inside the window. A
28 s ceiling does not stand between this project and that condition.

**What it does mean is that "stays up" is not a state this arm can be in, and no arm can be, until the
net is addressed.** Two consequences:

* **The coming press is still worth spending, and its expected ending is now stated**: the park's poll
  returns (the witness), the fifth poll publishes, the records stop with no fault, and the phone comes
  back to Android on its own — *not* needing a power press. That is a better prediction than the gate's
  previous one, which warned about a power press without saying a successful run also returns.
* **The next arm, if the phase wants to demonstrate the OS going on for longer than 28 s, has to change
  the net** — either not arm it, or pet it from XNU. Both are changes with a cost that is the user's
  call and not this step's: not arming it removes the only net, and the reference's own record
  (`docs/reference/recovery-and-rollback.md`) is that a run whose net is gone and which hangs costs a
  power press *and* its log, because a power press reinitialises DRAM. Petting it from XNU is a new
  driver in the entry image, which is a new build — and a new build replaces the parked arm.

This step takes neither. It states the ceiling where the image is described, so that an arm built next
is built knowing what its clock is.

## 5. What this does not do

* **It does not boot anything, and it changes no arm, payload, prediction or gate verdict.**
  `out/stage90/stage90-qcdt.img` is still `60063c47…`, the parked arm is still the sleeper
  (`STAGE90_XNU_IDLE_NO_SLEEP=1`, entry `696a0f39…`), and the press is still the user's.
* **It does not establish the reset path**, and it says so in the text it prints: the GIC-section route
  is real and unmeasured, so this is a ceiling and not a proof that the net is what fires.
* **It does not change 594's witness or its falsifier.** `xnu_live_poll_seq=3` with
  `poll_timeout_ms >= 1000` is still the prediction, and `poll_seq` still 2 is still the falsifier.
* **It does not sweep the `FAIL` branches** (603 §7, 604 §5) — still owed.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One
host-side file edited (`stages/stage90/preflight_boot_check.sh`, not in `xnu_arm_entry-sources.txt`; the
gate's freshness scan deliberately does not match `*.sh`), in its `--allow-xnu-entry` narration only — no
new `exit`, no new `die`, no new `fail`. Gate re-run on this tree: **EXIT=0 / 531 stdout lines / 0
stderr** (518 before; +13, all `echo`), exit census unchanged. Reads of 533's and 513's archived
captures and of the net-reach clause's own measurement. The payload, the parked frozen pair at
`/tmp/r594/frozen-payload/` and the arm the next press sends are unmodified. `fastboot boot` only —
never `flash` — so no outcome of any of this can write to storage.
