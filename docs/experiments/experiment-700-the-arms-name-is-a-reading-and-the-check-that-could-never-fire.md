# 700: the arm's name is a reading — readiness narrated the census arm as the clock arm, and the check that would have caught it could not fire

698 made `STAGE90_XNU_STORAGE_PROBE` a **ladder** (0 inert, 1 the read-only probe, 2 that plus the
vendor's mode sequence, 3 that plus the register-file census) and 699 pressed rung 3. This step repairs
two defects the press's own readiness output carries, both of them in **the narration** — the paragraph
readiness prints to name the arm the press will send — and it turns the second one into a check.

**No arm changed, nothing was pressed, and nothing about the device is claimed here.** Measured before
any edit: the arm's 11-file set does not contain `tools/verify_press_ready.sh` or
`preflight_boot_check.sh` (`stages/stage90/revert-set.txt`, rung-3 block), so a repair to either leaves
`out/`'s identity and its park untouched, and no re-park or re-record was needed. The device was on the
bus (`adb` lists `4a2fe00b` as `device`) and was not touched.

## 1. The finding: the rung-3 arm was named as 690's clock arm, by a fall-through, and the row said `ok`

The primary evidence is the press's own output, not a reconstruction. `guard`-side `/tmp/r654/press.log`
is the launcher's log, and the 698 press begins at its line 670 (`armed for
'armed-storage-census-4d66f871'`); its readiness header is line 689:

```
  ok    the arm is named by a reading      the ACTING arm (653: the seam WITH its operation, SEAM_POC=1
        and SEAM_MEASURE=0) - **and this is NOT that arm either: it is 690's CLOCK arm**
        (STAGE90_XNU_POST_END_TICKS=115200000 ticks - 6000 ms at the payload's own 19200 ticks/ms ...)
```

**The census is not mentioned anywhere in it**, and the word `census` does not appear in the whole
readiness output for that press. The narration was produced by the `wpet > 0` branch — the arm 690's
switch selects — because the storage branch's own condition was

```bash
  if [[ $wst == 1 || $wst == 2 ]]; then
```

and a rung-**3** value is neither. The branch that asks about the storage key was skipped, the chain fell
through to the tick arm, and **that branch produced a perfectly good name for a different arm.**

**The row still printed `ok`, and that is the part worth keeping.** `the arm is named by a reading` asks
whether a name was produced — non-empty `entry_arm`, a verdict sentence it knows, a consistent
`IDLE_NO_SLEEP` — and a fall-through satisfies all three. So the row's verdict is about the *shape* of
the naming and not about the *subject* of it: **a narration can be wrong and still non-empty**, and this
row could not tell those apart. The press was not mis-fired (the set name row was right, the flags were
derived from the arm's own switches, the gate refused nothing), but the one reading a person takes from
readiness before spending a press was describing the previous rung's arm.

## 2. The second defect: the narration was a **fourth** reader of a name 698 renamed

698 renamed the offset-0 word's key from `hci_version` to `dma_address` because `sdhci.h:27` says
`SDHCI_DMA_ADDRESS 0x00`, and 698 §3 enumerated the readers of that name before the edit and found
**exactly three**: the two sites in `entry_storage.c` and one line in 694's document. Measured now, that
census is **short by one**:

```
stages/stage90/xnu_arm_boot/entry_storage.c:170   (the #define, with the old name in its comment)
stages/stage90/xnu_arm_boot/entry_storage.c:678   (the ST_LIVE)
docs/experiments/experiment-694-...md             (the record's row)
tools/verify_press_ready.sh:681                   (the narration: `_mode_hci_version`)   <-- the fourth
```

**The census could not have seen it, and the reason is the extractor's scope rather than a mistake in the
edit**: the key is *published* as `xnu_live_storage_mode_hci_version`, and the narration writes the
**abbreviated** form `_mode_hci_version` — every other key in that sentence is abbreviated the same way
(`_mode_bit`, `_mode_w2_read`). A census that greps the full name is exact about what it greps and silent
about everything else, which is [[mi4-one-value-two-definitions]]'s **m699** read one reader further out:
the half that is not grepped is the half that goes stale, and it fails *silent*.

So on the 698 press the narration told its reader that `_mode_hci_version` would be "the first spec-valid
look at that register file" — **a key that run could not print**, because that arm publishes
`xnu_live_storage_mode_dma_address`. 699 §1 read the value off the right key; a reader following the
readiness paragraph would have grepped for one that is not in the capture.

## 3. The repair: the arm is selected by the rung, and the rung it quotes is checked

**One change to the selection and one new paragraph.** The branch is entered by any non-zero rung —

```bash
  if [[ $wst =~ ^[1-9][0-9]*$ ]]; then
```

— and the arm's name is then selected by the rung's **own value** (`2` → 696's paragraph, **`3` → 698's**,
`1` → the two-arm set branch, exactly as before). A rung this file has no paragraph for is deliberately
*not* given one by falling through: it gets no storage sentence at all, and the row below refuses it, so
the failure is a refusal and not another arm's prose.

**The rung-3 paragraph** names the arm by its act and its rung
(`STAGE90_XNU_STORAGE_PROBE=3, resolved set \`armed-storage-census-4d66f871\``), lists the ten registers
with the width each is read at and the vendor accessor that fixes that width, records the rename and what
it means for reading a 696/697 capture, and its consequence paragraph carries 699's three decisive
readings (`_reg_power_control` = the before-value that makes the driver's own `SDHCI_RESET_ALL` harmless
by construction, `_reg_pwrctl_mask` = the half a status register cannot answer, `_mode_host_version` =
the vendor's `SDHCI_VER_100` branch not taken), the alignment census that makes the widths a property of
the artifact, and what is **not** new (no store, no command, no sector, nothing mounted).

**And the rung-2 correction is extended to rung 3**, because the sentence it corrects is the *shared*
rung-1 one: `A \`_loads=6\` with \`_writes=0\` is the run this arm exists for` is a sentence a rung-3 arm
carries too, and while that guard read `$wst == 2` alone the rung-3 arm got the rung-1 sentence with no
correction beside it. That is the same defect one paragraph over — **a correction that is not carried to
every arm it is true of is a sentence the reader is left with** — and it is why the guard is now
`$wst == 2 || $wst == 3`.

## 4. The two checks, and what each one's scope is

Both live in the naming row (`the arm is named by a reading`), computed just above it, and both are
guarded to the arm shapes that carry a narration (`the window is reachable EXACTLY ONCE`, no self-test
switch on):

| check | the property | fails when |
| --- | --- | --- |
| **(1) the rung** | the narration **quotes the rung the entry record carries** | a rung with no paragraph here, or a narration naming a different rung — one quantity with two readings |
| **(2) the names** | every `` `_name` `` the narration writes is a suffix of some `xnu_live_` string **this image publishes** | a name the image cannot print, which is what a rename makes of prose |

**Check (2)'s scope is stated in its own refusal, because a scope claim is where this project's defects
live** (m693, m702): the match is a **suffix** and not an exact key — the narration abbreviates
(`_loads` for `xnu_live_storage_loads`) — and a **glob** (`_gcc_*`, `_reg_*`) is a *scope* claim and not a
name, so it is counted and listed but not checked. What it therefore catches is a name the image cannot
print; it cannot catch a name that happens to be the suffix of the wrong key, and nothing in the file
says it does.

**And the success path prints its reading**, because a check that passes by printing nothing cannot be
told from one that never ran ([[mi4-silence-is-a-reading-only-if-success-is-silent]]). The naming row's
`ok` line ends with:

```
 **The narration's own names are a reading too**: the 42 name(s) and 2 glob(s) it writes were read
 against /mnt/data/mi4-ios6/out/stage90/xnu_arm_entry.elf's own key strings, and every name is a suffix
 of an `xnu_live_` string this image publishes.
```

That sentence is what found the next defect, one step later.

## 5. The check that could never fire — and that printed `0 name(s)` only because §4 had just made it print

The first draft of check (2) extracted the names with

```bash
grep -oE '\\`_[a-z][a-z0-9_]*'
```

**Two backslashes, and in ERE `\\` is a literal backslash.** So the pattern required a backslash before
every backtick, no narration contains one, `nar_tok` was empty, `nar_miss` was empty **by
construction**, and the check passed — silently, on every arm, forever. The **single**-backslash spelling
is no better: to GNU grep `` \` `` is the *beginning-of-buffer* anchor, so it would match a name only at
the very start of the text. **Both spellings fail and both fail closed-looking**: the check's output is
identical to a check that examined everything and found nothing wrong.

What made it visible was that §4 had just been written: the `ok` line printed **`0 name(s) and 0
glob(s)`** beside a narration with forty-odd names in it, and a number that cannot be right is a reading
about the instrument. That is [[mi4-measurement-defects]]'s first-ranked shape — *a uniform verdict is a
fact about the instrument first* — and its corollary, *before concluding a value is absent, establish that
the extractor could have seen it*. The working spelling is the literal backtick inside **single** quotes,
where the shell does not touch it:

```bash
grep -oE '`_[a-z][a-z0-9_]*'      # the name
grep -oE '`_[a-z][a-z0-9_]*\*'    # the globs, whose star is escaped for the ERE
```

verified in a live shell before it was trusted (`sample='a `_mode_bit=0` and `_reg_loads` and a glob
`_gcc_*` and `*hci_version`'` → `TOK:_gcc_ _mode_bit _reg_loads` / `GLOB:_gcc_`, so the glob is classified
as a glob and the `*hci_version` form is not a name at all).

**One more property of the loop, checked rather than assumed:** the script is `set -uo pipefail` — **no
`-e`** — and the miss-finding loop's body is unconditional, so a non-matching `grep` cannot truncate it.
Under `set -e` the same loop would have aborted on its first non-match and reported an empty miss list
for a different reason, which is the same silence through a different door.

## 6. The measurements, in the order they were taken

| output | tree | result |
| --- | --- | --- |
| `/tmp/r654/press.log:689` (the press) | pre-repair | row `ok`, arm = **690's CLOCK arm**, no census anywhere |
| `/tmp/g700/ready.out` | checks added, **selection not yet fixed** (a splice typo) | **exit 1, 1 of 5** — row 4 `FAIL`, check (1) firing on the real arm: *"does not quote the rung the entry record carries: STAGE90_XNU_STORAGE_PROBE=3"* |
| `/tmp/g700/ready2.out` | selection fixed | **exit 0, 5/5**, arm = **698's STORAGE arm**, rung quoted, no `CLOCK arm` anywhere |
| `/tmp/g700/ready3.out` | + the success note | 5/5, and the note reads **`0 name(s) and 0 glob(s)`** — the extractor's defect surfacing |
| `/tmp/g700/ready4.out` | extractors fixed | 5/5, `32 name(s) and 1 glob(s)` — the rung-2 correction not yet carried to rung 3 |
| `/tmp/g700/ready5.out` | correction extended | **5/5, `42 name(s) and 2 glob(s)`**, every name resolving |

**Both directions on the pre-repair narration** (`/tmp/g700/verify_press_ready.sh.bak`, the file as 699's
press ran it): check (1) fails it (no `STAGE90_XNU_STORAGE_PROBE=3` anywhere in its text) and check (2)
fails it with **`_mode_hci_version`** as the miss — the one name in the pre-repair narration that no key
in the live image ends in. The controls are the reason to believe the checks fire at all, since the
repaired tree passes them by printing a count and nothing else.

`ready.out`'s refusal is worth its own sentence: it is the *same* failure the press's readiness had, now
**refused by a row instead of narrated by a fall-through** — a control in the unsafe direction that
happened to be produced by a typo in this step's own splice, and that is why the typo is recorded rather
than quietly fixed.

## 7. What this does not do

* **It does not touch the arm, the park, or any recorded byte.** Readiness and the gate are not members
  of the 11-file set (measured, §opening), so `out/`'s identity, the park and the record are unchanged
  and no re-park was needed.
* **It does not press and it does not build.** Five readiness runs, each read-only and each invoking the
  gate once (row 3); the gate accepted the tree on every one of them (`exit 0` under
  `--allow-xnu-entry`), and the device was only listed by `adb`.
* **It changes no reading of the device.** Every sentence repaired here is prose about which arm the
  bytes are; the storage frontier is where 699 left it.
* **TWRP-to-storage stays withheld** — the user's condition is that the OS can already be entered and
  stays, and the OS is not observed doing that.

## 8. Owed

* **The gate's narration for `STAGE90_XNU_STORAGE_PROBE` is two rungs short** (`preflight_boot_check.sh`
  :588-597 reads the key as a two-valued switch: "at 1 … at 0 …"). That file is the peer lane's, so this
  is **reported by message and not edited** (the reported finding, 2026-09-25) — the same defect shape as
  §1, one file over, and the reason the message carries the measurement rather than the advice.
* **Check (2) is a suffix check and says so.** A stronger form — resolve each name to the *one* key it
  abbreviates — is possible only if the narration stops abbreviating, which is a rewrite of prose that no
  press depends on; it is named here so the next reader does not mistake the current check for more than
  it is.
* **Carried, unchanged**: the driver's own `sdhci_reset(SDHCI_RESET_ALL)` and the clock, with
  `POWER_CONTROL` read back afterwards (698 §7, 699 §6); what actually returns a run (8/17/24/27/24 s);
  the ending's first store still faulting into a panic (`RESTART_REASON 0x0fa0065c`); the 691 §5
  `entry_note_wfi` readback; `entry_reset.h`'s false IMEM claim; 676 §6 / 677 §6; the 684-owed runner
  clause for the 678 arm; `tools/xnu_dt_requirements.py` and the `"master"` value; the two peer-lane
  tripwire repairs; and the seam address pinned in two files (`entry_trace.c`'s `STAGE90_XNU_SEAM_LR`,
  `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`).
* **698 §3's "exactly three readers" is corrected to four** by §2 above; the record keeps the sentence
  and this step's row is the correction, in keeping with the rule that a superseded claim is annotated
  where it stands rather than rewritten.
