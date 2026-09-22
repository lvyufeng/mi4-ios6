# 551: exit 2 measured adb alone, so a return the SoC made and adb missed was read as a hang

The peer session read `run_and_capture.sh` and found that its bounded wait polls `sudo adb devices`
and nothing else. I verified it at `:356-364`, and it is the one reading this phase cannot afford to
get wrong: **exit 2 is the non-return verdict, and the arm about to run is worth nothing if that
verdict can be produced by a host-side transport failure rather than by the SoC.** Host-side only -
no device action, no build, image untouched.

## 1. Why it matters today specifically

| time | the port (`usb 3-10`) |
| --- | --- |
| 16:35:45 -> 16:35:47 | `18d1:d00d` appears and disconnects - 526's run |
| 19:13:45 -> 19:24:35 | the serial-less `05c6:f006` occupant |
| 19:22:45 -> 19:23:03 | **`2717:0368`, serial `4a2fe00b`, 18 s, then dropped without reaching `18d1:4ee7`** |

That is a phone whose Android bring-up is **intermittent**, and the ledger already carries two earlier
`2717:0368` windows that ended in `f006` rather than `4ee7`. A run that returns the SoC correctly and
then fails to bring Android up leaves `adb devices` empty - and under the old loop that is exit 2,
"the payload ran and the device did NOT come back". The gate's closing text and the runner's own
message both assert that sentence.

## 2. The criterion is now two readings, and the second one is keyed on the serial

`adb devices` still says **where to capture the log from**. It no longer decides whether the device
came back. The independent reading is the host's own USB log, and it is a reading of the port *and the
serial*:

```
SerialNumber: 4a2fe00b
```

is printed only for this phone. **A port-only test would be wrong**: `usb 3-10` also carries the
serial-less `05c6:f006` occupant, which today appeared on its own after **2 h 38 m** of an empty port -
so "something enumerated on 3-10 after the boot" is not evidence the phone did. (This is the same
near-miss family the project has paid for twice: a search that reports a hit one nibble or one device
away from the thing it is looking for.)

## 3. The new contract, and the fourth state that found a defect in my first version

| the two counts | exit | what it claims |
| --- | --- | --- |
| adb empty, host log **unchanged** | **2** | did not come back - power press owed |
| adb empty, host log **increased** | **3** | **REFUSING to call this a non-return**: it came back into a state adb cannot reach, which is a capture failure; re-read later with `adb exec-out cat /proc/last_kmsg` |
| either count **unreadable** | **2** | UNREAD paragraph: says what it does *not* claim, and how to check by hand |
| host log **fell** | **2** | UNREAD: the `dmesg` ring buffer rotated, the two counts are not comparable, so it is not evidence of absence |

The last row was **a defect in my own first version, found by running the new clause rather than
reading it.** The version I wrote first treated any non-increase as "no new enumeration", so a rotated
buffer (`9 -> 4`) printed *"The device did not come back"* with full confidence. A count whose *shape*
was not checked before it was compared is this project's oldest defect class
([[mi4-measurement-defects]]), and I had just written one while quoting the rule about it in the same
paragraph. A fall is now an unreadable comparison, not an absence.

**And the fail-safe direction is the conservative one**: if `serial_enum_count` is missing or fails,
the count is empty, which takes the UNREAD path - I confirmed that by accident, when the first harness
did not define the stub and every state printed UNREAD rather than a verdict.

Verified in four states with the decision block extracted verbatim from the script and
`serial_enum_count` stubbed: dead port -> exit 2; one new enumeration -> exit 3; unreadable -> exit 2
UNREAD; rotation -> exit 2 UNREAD.

## 4. What this does not change, and the contract drift it creates

- **The arm is untouched.** `run_and_capture.sh` is host-side: no image, no build, nothing under `out/`.
  Frozen pair `1daaf44e624563694e…` / `f202f2465886aba6…` unchanged, `./build.sh` not run.
- **The gate still exits 0** after the change - re-run read-only - so the run is still armed.
- **But it leaves the gate's new clause stating something that is no longer true of the runner.**
  550's clause reads the wait loop out of this file and prints *"So its exit 2 is adb's reading alone"*.
  After this change that sentence is false: exit 2 now requires adb *and* the host log to be empty, and
  the new exit 3 is not in the gate's prose at all. That is a cross-file contract, so it is reported to
  the file's owner with the clause text rather than patched from here - the same discipline as 539-541
  in the other direction.

## 5. Safety

No device action. `bash -n`, one `--dry-run --allow-xnu-entry` (exits 0, touches nothing), one read-only
gate run (exits 0), and four stubbed harness runs of the decision block. `flash` not used, nothing
written to storage, no build run. The frozen pair is untouched, and the device is off the bus awaiting
a power press.
