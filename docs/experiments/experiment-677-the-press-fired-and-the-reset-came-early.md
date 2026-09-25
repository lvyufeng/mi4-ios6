# 677: the press fired and the device came back — the net's question is answered by the clock, and the reset landed earlier than the arm's own number

The owed press was given, the armed launcher fired, and the run returned. **This is the first
hardware-watchdog self-test the project has ever got a reading from**, and the reading is not the one the
pre-registration contained: the device came back, but **it came back too early for either of the arm's own two
candidate times**, so the arm's question is answered in one direction and re-opened in the other.

One press spent, exactly one gate, exactly one runner, one non-persistent `fastboot boot`, nothing flashed,
nothing written to storage. The device is up and in adb.

## 1. The fire, and what came back

From the launcher's own log (all UTC) and the runner's:

| time | event |
| --- | --- |
| 08:03:36 | the phone is on the bus after 2470 s (the operator's power press had landed) — `adb: [4a2fe00b device]`, `fastboot: []` |
| 08:04:08 | readiness `exit=0`; the arm is still `armed-selftest-wdog-ef0361a2` |
| 08:04:33 | `GATE EXIT=0` — **546 stdout lines**, the number the recorded set prints |
| 08:04:33 | the one run: `run_and_capture.sh --allow-xnu-entry --allow-hw-watchdog-selftest --expect-arm=armed-selftest-wdog-ef0361a2` |
| 08:05:17 | `Sending 'boot.img' (8340 KB) OKAY [0.263s]` / `Booting OKAY [0.011s]` |
| 08:05:53 | **`RUNNER EXIT=0`** — returned and captured |

The bytes sent were checked against the gate's own reading of them and were unchanged across the send:
`ef0361a2bd761eb70b5d951b73f6618137991221742226e52a9ac9493a6753af`. The capture is **2062 bytes, 33 payload
lines**, sha256 `542edb59dd71881d86ef1a456baf2cb915a072fc415dbd46e41a1c13d0142d53`, archived as
`out/stage90/captures/677-selftest-wdog-2026-09-25-last_kmsg.txt`. It is short because this arm never enters
the entry image — it arms the net, builds the DT, prints one line and spins.

## 2. The log's own reading: the net was armed, and nothing in the payload ended the run

Complete and clean, every key the arm publishes:

```
hw_watchdog_enabled=0x00000001          hw_watchdog_bark_ticks_written=0x000c7fb5
hw_watchdog_timeout_s=0x00000019        hw_watchdog_bite_ticks_written=0x000dffac
hw_watchdog_en_after=0x00000001         hw_watchdog_bark_after=0x000c7fb5
hw_watchdog_countdown_first=0x00000000  hw_watchdog_bite_after=0x000dffac
hw_watchdog_countdown_second=0x00000c26 hw_watchdog_bite_truncated=0x00000000
hw_watchdog_counter_running=0x00000001  hw_watchdog_max_ticks=0x000fffff
hw_watchdog_countdown_plausible=0x1     hw_watchdog_readback_ok=0x00000001
hw_watchdog_checksum=0xf90e83c1         hw_watchdog_sts_before / _en_before = 0
```

and the marker table the runner printed:

```
hw_watchdog SELFTEST: spinning        1     exception                             0
hw_watchdog SELFTEST: deadline reached 0    pc-sampling watchdog: dump+reboot     0
deadman: armed                         0    platform_reboot entered               0
```

**The log's last line is the "spinning" line and there is nothing after it.** The deadline path is not merely
un-reached in prose: it prints two more keys (`selftest_deadline_us`, `selftest_elapsed_us`,
`stage90_main.c:1175-1180`) and **neither is in the log**. The dead-man was not armed. There is no `exception:`
line. And the file ends with the new kernel's own ramoops marker, **`No errors detected`** — i.e. the previous
boot ended with **no software-recorded error**, which is what a boot ended by a hardware reset looks like and
not what a panic looks like.

## 3. The clock — and it excludes *both* of the arm's candidate times

This is the decisive measurement, and it is two independent clocks, each read device-side or host-side:

**(a) The host's own USB log on port `3-10`, matched by serial `4a2fe00b`** (the rule the file has carried since
550/551):

```
08:05:11  disconnect, device 76          <- `adb reboot bootloader`
08:05:17  device 77  18d1:d00d           <- the phone in FASTBOOT
08:05:19  disconnect, device 77          <- THE JUMP (the payload starts)
08:06:03  device 78  2717:0368           <- Android bring-up begins
08:06:21  disconnect, device 78
08:06:22  device 80  18d1:4ee7           <- Android up, and it stays
```

**(b) The phone's kernel start, as `host wall − /proc/uptime`**, three samples four seconds apart:

```
wall 08:07:17  phone_uptime  96.12  -> kernel start 08:05:40
wall 08:07:22  phone_uptime 100.28  -> kernel start 08:05:41
wall 08:07:26  phone_uptime 104.44  -> kernel start 08:05:41
```

So the payload's whole life is **(kernel start 08:05:41) − (the jump 08:05:19) = 22 s, minus whatever the
bootloader spends before the kernel starts.** 22 s is an *upper* bound on the payload session.

The arm's own two candidates are **28 s** (the SoC countdown, `STAGE90_HW_WATCHDOG_TIMEOUT_S + _BITE_GAP_S`) and
**90 s** (the bounded spin's `STAGE90_SELFTEST_DEADLINE_US`), and the runner measured its own answer beside them
(**47 s** from the send, which includes the transfer and Android's bring-up — its own text says so).

So:

* **the bounded spin did not reboot the device — excluded by the clock, not by the missing sentence.** 90 s
  cannot fit inside a 22 s session, and the deadline path's own two keys are absent as well. That half is
  settled.
* **and the countdown's 28 s is *also* above the measurement, by at least 6 s.** The measurement's error is
  ~1 s (three samples agree to a second; the host's dmesg timestamps are ±0.2 s), so this is outside it.

**That is a third outcome, and the pre-registration did not contain it.** The owed question was *"does the SoC's
own countdown reset the device"* with two cells (back at ~28 s = it fired; back at ~90 s = it did not). The
device came back inside a session shorter than the countdown, so the honest statement is: **the device was reset,
by something, earlier than the net's own stated interval** — and neither cell of 663 §3.1's table is the one
this run landed in.

## 4. Two candidate explanations, both unmeasured — and one of them is weak

**(a) The countdown fired and its interval is shorter than 28 s, because the tick rate is not 32,765 Hz.** This
is the reading that keeps the arm's answer positive, and it is **weakened, not supported, by the tree**: the
device's own driver is in this repository, and

```
external/android_kernel_xiaomi_cancro/arch/arm/mach-msm/msm_watchdog_v2.c:82
    static long WDT_HZ = 32765;        /* "By default it is set to 32765" */
:437    __raw_writel(timeout + 3*WDT_HZ, wdog_dd->base + WDT0_BITE_TIME);
```

gives the same constant this project uses, on this SoC, **and gives the `+3 s` this project's `_BITE_GAP_S`
came from**. If the real rate were ~46 kHz, every one of Android's own watchdog conversions on this device —
including the panic-timeout path at `:129-131` — would be wrong by the same factor, and that would not have gone
unnoticed for a decade. So (a) requires the driver's constant to be wrong *and* its effect to be invisible,
which is possible but not cheap. **Named, not adopted.**

**(b) Something outside the payload reset it inside 22 s.** All the payload-side paths are excluded by §2, so
this means a reset agent that is not the payload's own: the bootloader's own net, the PMIC/PON path, or a
hardware guard armed before the payload started. **Which one is not established**, and it is named here rather
than guessed at. The one thing §5 does say is that it was a **warm** reset with no software reason recorded.

## 5. What the device itself recorded, decoded out of this tree

The phone exposes its powerup reason, and — because the tree contains the driver that prints it — the string can
be read rather than inferred:

```
/sys/bootinfo/powerup_reason          = unknown reboot
/sys/bootinfo/powerup_reason_details  = 0x191
/proc/sys/kernel/boot_reason          = 0
```

`0x191 & BIT(8) = 0x100`, i.e. **`PU_REASON_EVENT_WARMRST` is set** (`arch/arm/include/asm/bootinfo.h:27`), so
the driver takes the warm-reset branch (`arch/arm/kernel/bootinfo.c:83`) and reads the restart-reason field as
`pu_reason >> 16` — which is **zero**. `find_first_bit(0, 32)` returns **32**, which is `>= RS_REASON_MAX`, so
the driver falls through to its last line and prints **`unknown reboot`** (`:117`). And
`is_abnormal_powerup()` — `pu_reason & (RESTART_EVENT_KPD | … )` tested against `WDOG | KPANIC | OTHER`
(`bootinfo.h:40-43`) — returns **0**: **no watchdog and no panic reason was recorded for this reset.**

Three things follow, and one of them is a limit:

* **It was a warm reset** (the WARMRST bit is the only one that matters here, and it is set).
* **No software reason was recorded** — consistent with a hardware countdown, which leaves none, and
  inconsistent with a kernel panic, which would set `KPANIC`.
* **It is not readable as a watchdog reset.** The field that would say `wdog` is zero, so the device's own
  record does **not** confirm the bite — it only fails to contradict it. And there is a **confound**: `adb
  reboot bootloader` at 08:05:11 is itself a warm reset eight seconds before the jump, so the PON latches behind
  this reading are not cleanly attributable to our run alone.

## 6. The instrument this arm lacked — and it is printer-sized

§3's question is *"how long did the payload live"*, and the payload **already has the exact quantity**: it
carries a 19.2 MHz timebase (`timebase_ticks()` / `timebase_elapsed_us()`, `cntfrq=0x0124f800`) and it already
prints `selftest_elapsed_us` — **on the deadline path, which is the path that means the net did *not* fire**
(`stage90_main.c:1179`). So the one number that settles §3 is published only when it is not the interesting one.

The fix is a periodic print inside the spin: `selftest_elapsed_us` every second, into the RAM console, which
**survives the warm reset** — so the last line the device wrote comes back with the log, and the device states
its own session length in its own words. **That is the cheap decisive instrument**, it needs no new mapping and
no new primitive, and it belongs in the same build as 674 §3's `for(;;) wfe`, 675 §1's cross-lane key pair and
676's two comment corrections — a **five-item list** that was being assembled precisely so that this build could
be one sitting.

## 7. What it changes, and what it does not

* **674 §2's order looks *more* right, not less.** Step 4 was *PS_HOLD first, then the bite* precisely because
  "either one alone has an unknown failure mode". **The bite's interval is now in question while PS_HOLD is
  proven on every returning run** — so the frontier arm rides the measured net first, which is what 674 §2
  already prescribed.
* **The window is closed and the build is unblocked.** The launcher fired exactly one runner and exited
  (`pid 426955` gone, its own log ending `done. gate=0 runner=0`), so 660 §5's closure has released itself and
  673 §3's derived order applies word for word: **edit → build → park → record → gate → re-arm.**
* **The device needs no keypress for the next run.** `sudo adb devices` lists `4a2fe00b device` and
  `/proc/uptime` climbs `83.72 → 88.87` and `96.12 → 100.28 → 104.44` over its samples — a running system, not a
  crash loop. So `adb reboot bootloader` lands on a stable `18d1:d00d`, which is 568's lesson applied.
* **It does not re-open the frontier.** The frontier is where 652 left it: XNU reaches pid 1, runs the userland
  phase, dies at the idle exit's `pop {fp, pc}`. This run produced no XNU reading at all — the arm never enters
  the entry image, and the runner's goal block says so in its own words (`open`/`read`/`getpid` 0 calls,
  `UNREAD`).
* **And the storage write stays withheld.** 605's criterion is the runner's two clauses in **one** capture — the
  goal block's `PASS` (user mode reached, a driver answering) *and* the arm's own clause. This capture has
  neither: it is a self-test that spins before the entry. **「如果os已经能进去了的话」 is unmet**, and Android
  running on the phone is a fact about the stock OS and not about ours.

## 8. Safety

**One press spent, and the device is not hung, altered or bricked.** One `fastboot boot` and nothing flashed;
`preflight_boot_check.sh` ran once (`EXIT=0`, 546 lines); `run_and_capture.sh` ran exactly once (`EXIT=0`),
both under the arm's own derived flags and with `--expect-arm` declared, and the launcher exited on its own
after the one run. Nothing this project sent was ever written to storage, so the brick half of the standing
constraint holds by construction for this run as for every other. The device-side readings in §5 are sysfs
reads through `adb`; no `dd`, no partition, no `flash`, nothing persistent. The neighbour `33e80afe` was off the
bus throughout and untouched.

**And the projection record this run adds, which is worth having:** the payload is now **measured** to return the
device from inside the payload's own context, with the log intact and with ramoops reporting no error. Every
later arm's "a hang still returns the log" rests on a property that was, before today, only a design claim.
