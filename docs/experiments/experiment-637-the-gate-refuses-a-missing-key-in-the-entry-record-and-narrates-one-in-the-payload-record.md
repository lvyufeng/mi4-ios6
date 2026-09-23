# 637: the gate refuses a missing key in the entry record and narrates one in the payload record

`stages/stage90/preflight_boot_check.sh` reads two generated records, and it treats a missing key in
them in **two opposite ways**. That is the whole finding, and it is a consistency gap inside one file
rather than a missing idea: the machinery for the right behaviour is already written there, for the
other record.

This step found it, measured it, and **did not fix it** - the file is another lane's
(`run-experiment-526` owns `preflight_boot_check.sh`), and it is the gate a press fires.

## 1. The contrast, read out of the one file

**The entry record (`out/stage90/xnu_arm_entry-config.txt`): a missing key is a refusal, both ways.**

```sh
ENTRY_CFG_KEYS=(STAGE90_XNU_ENTRY_SHA256 STAGE90_XNU_ENTRY_BYTES STAGE90_ENTRY_TRACE … )   # 15 names
for _k in "${ENTRY_CFG_KEYS[@]}"
do
  _v=$(awk -F= -v k="$_k" '$1 == k { print $2 }' "$ENTRY_CFG")
  [[ -n $_v ]] || fail "$ENTRY_CFG has no $_k line - … a record without that key would let a run go
                         out with a switch nobody recorded"
done
…
_unshown=$(… comm -23 …)     # and the converse: a key the list does not name
[[ -z $_unshown ]] || fail "$ENTRY_CFG carries key(s) this gate does not print: …"
```

**The payload record (`out/stage90/stage90-build-config.txt`): nothing.**

```sh
value_of() { sed -n "s/^#define $1 //p" "$CONFIG"; }

MODE=$(value_of STAGE90_HANDOFF_MODE)         # …and six more at 126-132, plus nine inline case reads
SELFTEST=$(value_of STAGE90_DEADMAN_SELFTEST)
…
[[ -n $MODE ]] || fail "STAGE90_HANDOFF_MODE missing from $CONFIG"
```

**Thirteen keys are read out of the payload record and one of them is asserted present.** There is no
`CONFIG_KEYS` array, no per-key `[[ -n ]]` over the rest, and no converse clause - `grep` finds exactly
three references to the file: its assignment, the `MODE` guard, and a comment. The two records are read
by the same gate, two hundred lines apart, with opposite disciplines.

## 2. What an unread payload key becomes: a value

Five `case` blocks group the empty string with the *off* values:

```sh
case "$FAULT_INJECT" in                       # 1474 - the only one with a consequence
  0|0u|"") echo "off: the handoff targets the Stage-owned high-VA function as usual." ;;
  *)       [[ $ALLOW_FAULT_INJECT -eq 1 ]] || fail "this build jumps at an intentionally unmapped
             VA ($FAULT_INJECT); needs --allow-fault-inject" …
esac
case "$(value_of STAGE90_XNU_ENTRY)" in       # 1490
  0|0u|"") echo "off: the handoff targets the Stage-owned high-VA function as usual." ;;
case "$(value_of STAGE90_XNU_BOOT_ARGS)" in   # 1746
  0|0u|"") echo "conforming boot_args (Phase 2): off - …" ;;
case "$(value_of STAGE90_XNU_MSM8974_SHIM)" in # 1752
  0|0u|"") echo "MSM8974 platform shim (Phase 3): off." ;;
case "$(value_of STAGE90_XNU_MSM8974_FIQ_PROBE)" in  # 1759
  0|0u|"") echo "FIQ availability probe: off." ;;
```

Measured by lifting the `BOOT_ARGS` block out of the gate **verbatim** and driving it with a
`value_of` that returns one value:

| `value_of` returns | the block prints |
| --- | --- |
| `0u` (what the real record says) | `conforming boot_args (Phase 2): off - only the ladder's own identity-based args exist.` |
| **empty** (the key is not in the record, or the pattern missed) | **the same sentence, character for character** |
| `1` | `conforming boot_args (Phase 2): ON, as a second object alongside the ladder's. …` |

**"The key could not be read" and "the key says off" are one state as far as the operator can tell.**
That is **632's class** - an empty value with more than one cause behind it, printed as one reading -
and **615's** (a statement naming a fact about a different thing) arriving in the gate, one record
over from where 632 repaired it in the runner.

**And for `FAULT_INJECT` the state is not prose.** The off-branch is the one that *skips* a refusal:
an unread key prints "off", the `--allow-fault-inject` check is never reached, and the gate exits 0 for
a build whose record did not say what the handoff targets.

## 3. And the extractor is brittle in both directions

`value_of` is `sed -n "s/^#define $1 //p"` - it consumes **exactly one** space after the name and
prints the rest. Against spellings of one line of the generated record:

| line the record carries | `value_of` returns | the `FAULT_INJECT` case |
| --- | --- | --- |
| `#define STAGE90_HANDOFF_FAULT_INJECT_VA 0u` (today) | `0u` | off, no refusal |
| `#define STAGE90_HANDOFF_FAULT_INJECT_VA  0u` (value column aligned) | **` 0u`** | **FAULT-INJECT mode → refuses without `--allow-fault-inject`** |
| `#define  STAGE90_HANDOFF_FAULT_INJECT_VA 0u` (two spaces after `#define`) | empty | off - indistinguishable from *not read* |
| `#define STAGE90_HANDOFF_FAULT_INJECT_VA<TAB>0u` | empty | off |
| `#define STAGE90_HANDOFF_FAULT_INJECT_VA` (no value) | empty | off |
| `#define STAGE90_HANDOFF_FAULT_INJECT_VA 0x8000` | `0x8000` | fault injection (correct) |

So a cosmetic alignment of that column does not merely go unread: it **flips an off switch into
fault-injection mode**, and the gate then refuses unless the operator passes a flag for a switch that
is off. That is 632's "four ordinary spellings defeated it" in a second pattern, with the difference
that here the padding direction is the **loud** one and the missing direction is the **quiet** one.

The same pattern is used for the two watchdog constants in `stage90.h` (lines 1322-1323,
`^#define STAGE90_HW_WATCHDOG_TIMEOUT_S \([0-9][0-9]*\)u`), and those are **guarded**: an empty value
calls `fail`. Recorded for completeness and **not a defect in effect** - a reformat of `stage90.h`
already refuses earlier, at the freshness clause, because that file is a payload source. The
per-record guard is what makes the difference, which is the point of § 1.

## 4. Why this is latent, and why it is still worth fixing

**All thirteen keys are present in the record on disk right now**, and `build.sh` emits exactly one
space, so nothing is mis-read today:

```
STAGE90_HANDOFF_MODE=STAGE90_HANDOFF_MODE_HARD_SKIP   STAGE90_DEADMAN_ENABLE=1u
STAGE90_XNU_ENTRY=1                                  STAGE90_HANDOFF_FAULT_INJECT_VA=0u
STAGE90_XNU_BOOT_ARGS=0u   STAGE90_XNU_MSM8974_SHIM=0u   STAGE90_XNU_MSM8974_FIQ_PROBE=0u   …
```

It is worth fixing because of *what the trigger is*. The record is generated, but the two ways it can
drift are ordinary: a key stops being emitted (the gate then narrates a switch it never read), or the
emitter's spacing changes (the gate then flips a mode and refuses). Both are silent in the first case
and loud-for-the-wrong-reason in the second, and **the fix is already in the file** - the
`ENTRY_CFG_KEYS` pattern, both directions, is exactly what the payload record lacks. That makes this a
consistency argument rather than a proposal: one gate, two records, one discipline.

**The one key that matters most is `STAGE90_XNU_ENTRY`.** Its off-branch is not a description, it is the
branch that carries the consequence 1489's own comment is about (a run that "never jumps", "paid for
with a power press"), so an unread value there reads to the operator as *the arm does not reach XNU*,
which is the sentence that decides whether the press is worth taking.

## 5. What this does not do

* **It does not fix the gate.** `preflight_boot_check.sh` is `run-experiment-526`'s lane and this
  session's rule is to message first rather than edit another lane's file; the finding is sent to them
  and the repair belongs in the same change as a re-read of the thirteen keys. Independently, the gate
  is a file the armed catcher fires, so changing it now would change what the press runs (the same
  reason 620 keeps the runner and 635 keeps the catcher unedited).
* **It does not change any verdict.** No edit was made to any file this step names; the measurements
  are reads plus isolated reproductions of three code fragments in `/tmp`.
* **It does not make the extractors total.** The repair is a refusal on an unread key, not a tolerant
  parser for a format that has one writer.
* **It does not touch the device, the arm, the payload or any parked byte.** `out/stage90/stage90-qcdt.img`
  is still `60063c47…`, still **UNRUN**, and `tools/verify_press_ready.sh` still reads 3 ok / 1 FAIL
  with the FAIL being the neighbour `33e80afe` in fastboot.
* **It does not advance 「起码要能进入操作系统，把基础驱动跑起来」.** The device is off the bus; the only
  event that can move the goal is the user's **unplug `33e80afe`, then Vol-Down + Power**.

## 6. The lane's owner reproduced it independently, and added two extensions

The finding was sent to `run-experiment-526` - whose lane `preflight_boot_check.sh` is, and who owns the
repair - rather than edited by this session. Their reply is worth recording for its *method*, because
this project's most-repeated defect is a claim that nobody re-derived:

* they re-derived rather than accepted every figure here: `value_of()`'s pattern at `:122-124`, the
  **13** keys enumerated **off the source and not off this document's count**, and `[[ -n $MODE ]] || fail`
  at `:134` as the only presence assertion;
* they drove their own extractor against four synthetic records and reproduced **both** directions
  (`0u` → OFF; **two spaces → `" 0u"` → the fault-injection branch**; no value → OFF; tab → OFF);
* they read the live `out/stage90/stage90-build-config.txt` through the gate's own extractor and
  confirmed all thirteen keys are present and read correctly - so § 4's "latent" is confirmed by the
  file's owner, not asserted by the reporter.

**Two extensions they added, both of which this step had not measured:**

1. **`#define K` with nothing after it is also swallowed.** The emptiness that § 2 shows for a
   *missing* key is the same emptiness for a key **emitted with no value** - and the compiler does not
   default that to 0, it is a syntax error `build.sh` would catch. So the gate would have printed "off"
   for a record that could not have been built. It is the same one-sentence-for-two-states, with a
   third cause behind the empty value.
2. **The converse gap is wider than a missing key.** A key that is *present in the record* and *absent
   from the gate's thirteen* is invisible here - the record carries it and nothing reads it. That is
   the half `ENTRY_CFG_KEYS`' `comm -23` clause already guards for the entry record and this record has
   no equivalent of, so § 1's "one discipline" argument covers one more case than § 1 listed.

**Ownership and timing, agreed by both sessions.** The repair does not land before the press: the
catcher fires this file, so an edit now changes what the run does - the same reason 620 keeps the
runner and 635 keeps the catcher unedited. The shape they intend is § 1's discipline applied to
`$CONFIG` - a key array, a per-key presence refusal, the converse - **plus** the extractor made
whitespace-tolerant (`s/^#define $1[[:space:]]*//p`) so an aligned value column cannot flip a switch.

## 7. Safety

No device action, no boot, no build, no `fastboot`, no `adb`, **nothing written to storage**. Every
measurement is a read of two generated records and one shell file, plus three code fragments lifted
verbatim into `/tmp/637` and driven with stub functions. Nothing in `stages/`, `tools/` or `out/` was
modified: `git status` is clean and the arm still hashes to `60063c47…`. Both catchers were confirmed
alive (pid 1344846 watcher, pid 4067419 relay) before and after, and the peer's own device reading
agrees with 635's: off the bus ~9 h, `4a2fe00b` absent from both lists, `33e80afe` alone in fastboot,
press not spent. `fastboot boot` only - never `flash` - so no outcome of this step can write to
storage.
