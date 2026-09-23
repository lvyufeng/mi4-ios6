# 605: the write path exists now, and it refuses

The user's condition has two halves, and only the first has had any machinery:
「起码要能进入操作系统，把基础驱动跑起来」, and then 「如果os已经能进去了的话，可以twrp写入到存储里了」.
Every device touch in this tree so far has been `fastboot boot`, which writes nothing to storage —
that is the whole reason a brick has been impossible by construction — and the second half is the step
where that stops being true. It had a **procedure** (`docs/reference/recovery-and-rollback.md`, 311
lines of it) and no **gate**: a document cannot refuse.

`stages/stage90/preflight_storage_write.sh` is that gate. It refuses by default, runs no `fastboot`,
touches no device, and clears exactly one form of the write.

Host-side only: one new script, one section added to an existing reference, no build, no device, no
`fastboot`, no `adb`, nothing written to storage. **TWRP stays withheld** — the OS has still not been
observed staying up, and the gate now says so mechanically rather than in prose.

## 1. The condition, made checkable

「如果os已经能进去了的话」 read by feel is not a precondition. The gate defines it as the **runner's own
two clauses in one capture**:

* the goal block's `PASS` — user mode reached and a driver answering (596); and
* the arm's own clause `PASS` — the machine stayed up past the park (594).

The second is what makes it a criterion instead of a formality. **The first alone is a floor that 520
and 533 also meet**: both got to pid 1, both ran the whole userland fixture (the driver's `open`, the
`read` that left `MH_MAGIC`, `getpid`, the forked child reaped by `wait`, two user ASTs), and both died
at the idle exit's `pop {fp, pc}`. A gate that accepted the floor would clear a persistent write on the
strength of a boot that died. The runner already names this in its own text — *"the run that matters is
the one whose log has both"* — so the gate is reading a ceiling the runner wrote, not one invented here.

**And the phrases are checked against the runner before they are used.** A gate that greps the
summariser's output for a sentence is making a claim about what that file prints, and this project has
paid for that class repeatedly (602: a rule written down in a comment and enforced by nothing). So the
first thing the gate does after the target check is assert that both phrases are still *in*
`run_and_capture.sh`; if either is gone, it refuses rather than clearing a write on a phrase nothing
prints any more.

## 2. The four preconditions, and the one that is the point

| # | precondition | what it protects |
| --- | --- | --- |
| 1 | the criterion above, from a capture log | no write cleared by a boot that reached userland and died |
| 2 | the whole backup manifest verifies **and** the target's stock image is present | a rollback whose hashes have moved is a promise, not a path |
| 3 | the target is `boot` or `recovery`, nothing else | the partition list the reference forbids, refused by name |
| 4 | **TWRP is booted, never flashed** | one persistent change instead of two |

Precondition 4 is the finding. The tempting reading of 「可以twrp写入到存储里了」 is *flash TWRP*. It is
not needed. TWRP is a **tool**: `fastboot boot twrp.img` writes nothing, and then the target is written
from inside it —

```bash
sudo fastboot boot twrp.img                 # NOT a flash
sudo adb -s 4a2fe00b shell 'cat /proc/partitions; ls -l /dev/block/by-name/boot'
sudo adb -s 4a2fe00b push payload.img /tmp/write.img
sudo adb -s 4a2fe00b shell 'dd if=/tmp/write.img of=/dev/block/by-name/boot bs=4096'
sudo adb -s 4a2fe00b shell 'sync'
```

— which leaves **exactly one** persistent change instead of two, and keeps `fastboot flash` out of the
picture entirely for the tool. The only `fastboot flash` in the gate's output is the **rollback**, and
it is labelled as a recovery action.

## 3. A contradiction the first draft had, and the fix

The first draft printed the from-inside-TWRP form when `--boot-image` was given **and** printed the
direct `fastboot flash` form as a fallback when it was not — then said `CLEARED` either way. So one
verdict covered two actions with different blast radii, and the fallback line said *"NOT cleared here"*
four lines above a `CLEARED`:

```
  (no --boot-image given, so the from-inside-TWRP form cannot be printed. The alternative is
   a bootloader write, which this project has never done and which is NOT cleared here:)
       sudo fastboot flash boot ...        # <- one persistent change, no tool booted
...
CLEARED: every precondition verified.
```

That is one value with two definitions, in the file whose subject is refusing. The fix is to require
`--boot-image`: the gate clears exactly one form, and a bootloader flash is not printed anywhere in it.
A form of the write that the gate will not clear belongs in the reference document, where it already is.

## 4. Every refusal shown to fire

Nine states, each a real invocation, each refusing **by name** with exit 1:

| what was passed | what it said |
| --- | --- |
| nothing | `the flag that says so is not set` — and the paragraph on why this step is different |
| `--target=modem` | the allowed list, and the forbidden partition list by name |
| no `--evidence` | `a write cleared without one is not gated at all` |
| `--evidence` at a path with no file | `An absent file is not a capture that failed to say the criterion` (601's class) |
| **`--evidence=/tmp/533-A.log`** | **`shows the goal's clause but NOT the arm's`** — the floor state, refused |
| an empty file as evidence | `An empty log is a file that exists (596a) and it carries no reading` |
| `--image` absent | precondition 5 of the reference, quoted |
| `--boot-image` = `--image` | `a gate that clears booting the payload in order to write the payload has cleared the wrong thing` |
| `--boot-image=/etc/hostname` | `does not parse as an Android boot image` (`tools/parse_android_bootimg.py` refused it) |

The 533 row is the one worth having: it is a **real archived capture**, from a boot that reached pid 1
and ran the fixture, and the gate refuses it on the ceiling rather than on the floor. Both clauses are
in the runner and the row exercises the difference between them.

**And the structural check fires too**: one word of the arm clause doctored in `run_and_capture.sh`
(`did what it was built to do` → `behaved as designed`) made the gate refuse at the phrase check, before
looking at any evidence. The runner was restored from a copy taken first and verified by **sha256**
(`2a7aca1a…`) — not by `git checkout`, which would have reverted nothing here but is the habit that
lost 603's own edit.

The cleared path was then exercised end to end: exit 0, with the payload's sha256 (`60063c47…`) and the
rollback's (`b2119252…`) printed as the record the reference's own checklist asks for.

**The evidence used for that end-to-end run was a synthetic log**, built to the shape the coming boot
should produce. That is a test of the *gate* and it is not evidence that the criterion has ever been
met — no capture in this tree meets it, which is exactly why the gate has never cleared on a real one.

## 5. What this does not do

* **No TWRP image exists in this tree.** `xiaomi4-cancro-backup-20260604-112053/recovery.img` is the
  **stock** recovery — verified against the manifest, and it is the rollback target — not TWRP. A
  `cancro` TWRP build has to be obtained and its provenance recorded before `--boot-image` means
  anything. The stock `recovery.img` was used as a stand-in for the mechanics test only.
* **The gate cannot tell you what an image is.** It verifies that the boot image parses as an Android
  boot image and that it is not the same file as the payload; it cannot verify it is TWRP, unsigned, or
  built for this device. That is the operator's check.
* **It does not make the OS boot.** The criterion it enforces has not been met by any run. It makes the
  second half of the condition *refusable* until the first half is true — which is the only thing that
  could have been built while the phone is off the bus.
* **The backup is not re-taken.** The manifest verified on this step (`22` entries, all `OK`), which is
  what the gate checks; a stale backup whose hashes still verify is still a stale backup.
* **No EDL path is documented for this phone**, so the rollback assumes fastboot still works. That is
  stated in the reference and in the gate's own output; it is not a gap this step closed.

## 6. Safety

No device action, no `fastboot`, no `adb`, **nothing written to storage**, no boot, no build. One new
host-side script (`stages/stage90/preflight_storage_write.sh`, mode 100755) and one section added to
`docs/reference/recovery-and-rollback.md`. The script runs no `fastboot` and opens no device: it reads
files, runs the runner's `--summarise` on a log, and prints commands. Its only writes are to a `mktemp`
file it removes on exit. One single-word doctoring of `run_and_capture.sh` for the phrase check,
restored from a copy and verified by sha256; the boot gate re-run afterwards → **EXIT=0 / 518 lines /
0 stderr**, census still 3 exit sites / codes `2 3`. The payload, the parked frozen pair at
`/tmp/r594/frozen-payload/` and the arm the next press sends are unmodified. `fastboot boot` only —
never `flash` — so no outcome of any of this can write to storage.
