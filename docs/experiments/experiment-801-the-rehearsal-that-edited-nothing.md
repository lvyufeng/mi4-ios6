# 801: the rehearsal that edited nothing — two `dd` writes set `0xff` over `0xff`, and the armed arm's own record named two of its five manifest members

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER, NOTHING
BUILT.** No `sudo`, no `adb`, no `fastboot`; the whole step is two shell files and one field of the
record. **`out/` was not touched** — every byte of `out/stage90/` and of the park
`out/stage90/frozen/armed-storage-ce2f589c/` is identical before and after, and the arm is still
**ARMED AND NOT PRESSED**. One press spent: **none**. **No press is authorized — the press is the
operator's.**

This step was found by accident and it is worth saying so: it began as a check of the press path and
turned into a defect in **the instrument that checks the press path**, plus a false field in the
**armed arm's own record**.

## 1. The rehearsal was red, and its own contract says it cannot be

`tools/rehearse_revert_set.sh` drives `tools/verify_revert_set.sh` — the tool that decides whether a
park is the recorded set — against real directories, with no device and no build. Its header says every
cell invokes the real tool and asserts the exit code **and** a phrase of the output, "so a refusal that
has never been shown to fire is a claim".

Run on this tree it returned **`35 ok, 3 failed`**, exit 1. Its own last line is
`Every refusal fires, names the member or the host it is about, and the honest case passes`, and the
whole point of the battery is that this line is earned.

| cell | expected | got |
| --- | --- | --- |
| `one-byte-edited` | exit 1, `stage90-qcdt.img hashes to` | **exit 0** |
| `self-consistent-manifest` | exit 1, `stage90-qcdt.img hashes to` | exit 1, but refused on `SHA256SUMS.txt is 847 bytes, the record says 560` |
| `stale-member-turns-it-red` | exit 1, `stage90.img: FAILED` | **exit 0** |

## 2. The cause, measured, and it is in the test and not in the tool

Both of the first and third cells edit a file with the same line:

```sh
printf '\xff' | dd of="$DIR/<member>" bs=1 seek=1000000 conv=notrunc status=none
```

**On this build the byte at offset 1,000,000 of both members is already `0xff`:**

| file | byte @ 1000000 | size |
| --- | --- | --- |
| `out/stage90/stage90.img` | **`0xff`** | 6,051,840 |
| `out/stage90/stage90-qcdt.img` | **`0xff`** | 8,572,928 |

Measured with `od -An -tx1 -j 1000000 -N 1`. **So both writes were no-ops.** The "edited" directory
was byte-identical to the good one, the verifier correctly verified it, and the two cells that exist to
show it *refusing an edited member* went red **while the verifier was right**.

**This is the project's own defect class, one direction over from the usual one.** The rule these cells
were written under is "a check that can never print a POSITIVE is not a check"; this is a check that can
never produce its own INPUT — a test whose edit edits nothing is a test that asserts nothing, and it
fails rather than passes, which is why it was visible at all. **The specific cause is that a filler byte
was chosen without reading the file it overwrites.** `0xff` is the obvious filler for an image full of
`0xff`, which is exactly why it was the wrong one.

## 3. The verifier is sound, and that distinction is the whole safety value of this step

The alternative reading — *"`verify_revert_set.sh` has stopped refusing edited bytes"* — is a finding
about the parked arm the next press depends on. It is **false**, and it was measured rather than argued:

```
cp the eleven recorded files from out/stage90/ to a scratch directory
flip ONE byte of stage90-qcdt.img in place        (0xff -> 0x00 at offset 1000000)
tools/verify_revert_set.sh <scratch> --set=armed-storage-ce2f589c
```

| | |
| --- | --- |
| before the edit | **exit 0**, `VERIFIED` — the eleven files are the recorded set |
| after one byte | **exit 1**, `FAIL  stage90-qcdt.img hashes to 3e51e057345fc1d675b7dcbafa22ce556f98d9277ce90e90a25b561c0ced9430` |

**The tool refuses a one-byte edit, names the member and prints the hash it found.** The park verifier
is intact; what was broken was the instrument that demonstrates it.

## 4. The third cell needed more than the same fix, and why is a fact about the fixture

`self-consistent-manifest` copies the edited directory, regenerates its manifest so that the manifest
**agrees with the edited bytes**, and asserts the tool refuses anyway — because it hashes from the
record and never from a manifest it finds in the target. The property is right. Its needle could not
appear, and the no-op edit was only half the reason.

The other half is that the record's `SHA256SUMS.txt` line pins the **live** manifest — five entries with
absolute paths, 560 bytes — while the cell's regenerated manifest is **ten entries with bare names, 847
bytes**. The verifier checks sizes before hashes, so it refused the **manifest** and never reached the
payload. **That is the same class this file already records at 724** for the
`live-tree-vs-the-other-set` needle: *an assertion about WHICH check refused first, when the cell is
about the directory being refused at all.*

**So the cell's record is rebuilt, not its needle.** It is now built from the good fixture and only its
manifest line is moved onto this directory's own manifest:

```sh
mkrekord "$GOOD" "$W/selfcons-record.txt"
sed -i "s|^set=fixture sha256=... file=SHA256SUMS.txt |... $<this manifest>...|" "$W/selfcons-record.txt"
```

**That removes the size disagreement and leaves exactly one: `stage90-qcdt.img`'s bytes.** The cell now
fails for the reason its name says it measures and for no other — which is a stronger cell than the one
that was green before this step, and not merely a repaired one.

## 5. The repair to the edits, and it refuses to be a no-op again

A function replaces both `dd` lines. It reads the byte it is about to replace and writes its
**complement**, so the edit is an edit for every input, and it compares the file's hash before and after
and **fails the run if the file did not change**:

```sh
flipbyte() {  # flipbyte FILE OFFSET
  b=$(od -An -tx1 -j "$off" -N 1 "$f" | tr -d ' \n')
  printf "\\x$(printf '%02x' $(( 16#$b ^ 255 )))" | dd of="$f" bs=1 seek="$off" conv=notrunc status=none
  [[ $before != "$after" ]] || { echo "flipbyte: the edit at offset $off of $f did not change the file" >&2; return 1; }
}
```

**The size is unchanged and only one byte moves**, which is deliberate: `wrong-size` and `empty-member`
are separate cells about other refusals, and an edit that also changed the size would make this cell
pass on the wrong one.

**Result: `38 ok, 0 failed`, exit 0** — the count the battery's own contract names, now earned.

## 6. The second finding, and it is about the armed arm's own record: `manifest_members=` named two of five

The rehearsal derives the manifest's member list from the **record**, from the union of every set's
`manifest_members=` field, precisely so that a member the record fails to cover is caught. That
derivation printed `the manifest it verifies names 5` — and it printed 5 **while the armed set's own
field named 2**, because thirteen other sets supply the other three. **A union hides a member set that
is short.**

A census of every set carrying the field:

| | field | names |
| --- | --- | --- |
| thirteen consecutive sets, `armed-storage-pwrwait-aa2b051d` … `armed-storage-c1f89600` | **5** | `stage90_fixture.macho,stage90.elf,stage90.bin,stage90.img,stage90-qcdt.img` |
| **`armed-storage-ce2f589c`** — the armed arm | **2** | `stage90_fixture.macho,stage90.img` |

**The two are the first and the last of the canonical five**, which is the signature of a list computed
and then truncated, not of a manifest with a different member set. And the manifest is not in doubt: the
live `out/stage90/SHA256SUMS.txt` **and** the park's both name exactly those five files.

**So the armed arm's record carried a false claim about its own manifest, and nothing refused.** Two
reasons, both structural:

- the derivation that would catch it is a **union over all sets**, so one short set is invisible;
- `verify_revert_set.sh`'s check 6a tests only **one direction** — every name in the field is a `file=`
  line of the set — and says nothing about names the manifest carries and the field omits.

**Corrected to the measured five.** The `sha256=` and `bytes=` lines were **not** touched, so every
verification the park depends on is unchanged; the field is a name list, and it now agrees with the file
it describes. Re-measured afterwards: check 6a holds for all five names, and the park still verifies
**11 ok / 0 failed**.

## 7. What this does not do

- **It spends no press, authorizes none, and changes no byte of `out/` or of any park.** The arm
  `armed-storage-ce2f589c` is still armed and unspent.
- **It builds nothing.** No entry image, no payload, no rebuild.
- **It does not touch the gate, the runner, or the arm's own bytes.**
- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made, so
  「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld**.

## 8. Owed, and named rather than left to be inferred

- **A check that compares `manifest_members=` against the manifest it describes, in BOTH directions.**
  §6's defect is exactly the direction 6a lacks, and the repair here is a corrected field rather than a
  guard — which means the next build can reintroduce it silently. **This is the one item this step adds
  to the owed list, and it is the one that would have caught §6 before a human read the line.**
- **The rung-33 press** — `armed-storage-ce2f589c`, parked, readiness 5/5, and the operator's.
- **`entry_storage.c`'s inverted CMD2 gate** — the driver proceeds when bit 31 SETS. Source, and it
  belongs to the build the press makes possible.
- **`entry_storage.c`'s `>= 30` guard** (798 §4) — it should be `== 30`, or the record must state that
  value 31 is a strict containment of value 30. COST-owed to a build.
- **796 §1/§3/§8's sentences about `0x0209`** (798 §4) — false; COST-owed to a build.
- **The six live-prose corrections of 797 §1** — the 1.200 s attribution, COST-owed to a build.
- **`mmc_select_voltage`'s window** (799 §2, 800 §9) — deliberately not ported, and the reason is
  written where the arm is.
- **`TIMEOUT_CONTROL`'s real scope** (798 §2) — unmeasured on this ladder.
- **The effective bit rate, 171.5 kHz against rung 6's configured 400 kHz** (795 §10) — untouched.
- **A `_cmd1_raw_pre*` / `_cmd1_raw_post*` pair** around CMD1's own `RESPONSE` registers — owed since
  787 §6.
- Unchanged from 787–800: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (COST);
  `c3.inhibit_timeout` for the `nidx` family (COST); the `*_status_post` class (791 §5); the four
  `5,088,000`s and the mis-citation at `entry_storage.c:302-303` (COST); a check that counts `ST_LIVE`
  sites per key (789 §4); the `_Static_assert` message's bit map at `entry_storage.c:2360` (789 §5);
  `_cid_ps_after`'s second producer (789 §2, COST); the set-comparison pad repair (779 §7); the
  `rung_para` correction for values 12..23; the seam-address class; `run_and_capture.sh`'s
  `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell (782 §6); 784's `rail_name` cell;
  and 783's window-scope check.
