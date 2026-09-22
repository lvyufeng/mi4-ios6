# 542: the net has not fired in this arm, and the gate now says so

A host-side reading, no device and no build: **the hardware watchdog, which every gate run describes as
the net that makes a hang safe, has not brought the device back from any of the last three hangs** - and
nothing after the jump can have disarmed it. The step is one clause of gate text, because the gate is
where an operator forms the expectation that decides whether a hang is a repair or a brick.

## 1. What the gate said, and what is measured

`== recovery net ==` prints, on every run:

```
hardware watchdog: ARMED. The SoC resets itself if the payload stops making
                   progress, whatever the CPU is doing - and platform_reboot()
                   forces a bite so the reboot does not depend on PS_HOLD.
```

That is a claim about the *mechanism*, and it is a fair one - the watchdog is a hardware counter that
does not need the CPU, the GIC or the vector table. What it does not say is whether it has ever worked
in the configuration about to be booted, and for this arm the answer is that the last three hangs did
not come back:

| run | outcome |
| --- | --- |
| 517 (first run) | no log, no return - power press |
| 521 (exit-side `FlushPoC_Dcache`) | no log, no return - power press |
| 522 (enter-side `SCTLR.C` re-enable) | no log, no return - power press |
| 526 (the null instrument) | no log, no return - power press |

**526's is measured on this host now rather than taken from a ledger.** Its `usb 3-10` fastboot device
- `18d1:d00d`, serial `4a2fe00b` - disconnected at **16:35:47** and the port has carried no event
since. That is over two hours against a bite due 28 s after the arming. The net did not fire.

## 2. And it cannot have been switched off, which is the part that makes it a finding

The obvious explanation for a net that does not fire is that something disarmed it. Measured over
every `mov`/`movw`+`movt` immediate pair in `out/stage90/xnu_arm_entry.elf` - the image that contains
XNU's kernel objects, the project's stubs and Apple's platform code - **no address in
`[0xf9010000, 0xf901ffff]` is materialised anywhere**, so nothing that runs after the jump can reach
the watchdog's registers at `0xf9017000`. (`0xf9000000`/`0xf9002000` do appear, and they are the
**GIC** - distributor and CPU interface - which is also why the first pass of this search looked like a
hit: the first two bytes of `0xf9017000` are `0xf901`, and the region that matters is one nibble away
from the one that is there. Nil novi: a search that finds a near miss must be resolved to a full
address before it is read either way.)

So the arm is the payload's and it stays armed, and the three hangs are not a case of the net being
switched off. **A hang in this state does not come back from it.**

## 3. What the returns do tell us, and what they do not

The three runs that *did* return - 518, 519, 520 - have logs, and their logs carry XNU's own panic and
its own `Attempting system restart...MACH Reboot` (the archived 520 log puts them at lines 4002 and
4009, with the panic at `fault_addr=0x4b79074`). So XNU reset the machine itself, and a `MACH Reboot`
is a return path that does not need the net.

**Whether the net was also due around then is not settled here, and one measurement is deliberately not
claimed.** The payload's output carries no timestamps, so the logs cannot order the arming against the
return. The host's USB log can - the jump is the fastboot device's disconnect and the return is the
next enumeration - and one returning run's timeline is still in the ring buffer:

```
02:00:56  18d1:d00d appears            (fastboot)
02:00:59  18d1:d00d disconnects        (the jump into XNU)
02:01:16  2717:0368 appears            (Android, 17 s later)
```

17 s from jump to Android, against a bite due ~28 s after an arming that happens a second or two into
the payload - which would put the return *before* the net was due, and make this run a second witness
that `MACH Reboot` is what returns the device. But that arithmetic needs the arming's own time, and the
only way to place it is the run's log, which has no timestamps; the log captured at 02:55 is not
provably the log of the 02:00 run. So it is recorded as an observation with its condition and not as a
finding. **What does not depend on it:** XNU's own `MACH Reboot` is in the returning runs' logs, and
that is a return path independent of the net.

**Likewise the ledger claim for 506-515.** The entry block's safety paragraph says the net "recovered
every run from 506 to 515, including runs parked in the kernel's own idle WFI and runs in an abort
storm". Those logs are not in hand, and a recovery that came back on a panic-driven `MACH Reboot` is
indistinguishable from one that came back on the net when all that is kept is "it returned" - which is
the same conflation as the paragraph above, one step removed. So this step does not overturn 506-515 and
does not lean on it either.

## 4. The change

`preflight_boot_check.sh` prints one more paragraph inside `== recovery net ==`, **only when
`--allow-xnu-entry` is given** - the arm it is about:

```
MEASURED, this arm: the last three hangs (517's first run, 521, 522, 526) did NOT come back
         and each needed a power press - 526's port has been silent for hours against a bite
         due 28 s after the arming, and nothing in the entry image can reach the watchdog's
         registers, so the net was not disarmed: a hang here does not come back from it.
         Expect a power press, and expect the log to go with it.
```

It is printed for this arm only because the measurements are this arm's; a run that never jumps into
XNU parks in the payload's own loop, where the net's record is the good one. Verified both ways: with
`--allow-xnu-entry` the gate exits 0 and the lines print; without it, the run is refused before the
block and the lines are absent (`grep -c` = 0).

**What this does not do is add a net.** A second net would be a change to the image and therefore to
the arm, and the arm is frozen and unrun. What it changes is the expectation an operator brings to a
hang: three of the last four runs owed a power press, the device is never at risk of being bricked (no
`flash`, nothing written to storage), and the log of a hang is lost with the cold boot.

## 5. Safety

No device action in this step. One gate run with the flag and one without, both on the host. The frozen
pair is untouched: `out/stage90/stage90-qcdt.img` = `1daaf44e624563694e…`, `out/stage90/xnu_arm_entry.bin`
= `f202f2465886aba6…`, and `./build.sh` was not run.
