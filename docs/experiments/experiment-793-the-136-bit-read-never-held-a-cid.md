# 793: the 136-bit read has been taken nine times and never once held a CID — and the row-31 narration's "the CID is taken" is an overclaim, corrected before the press

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER, NOTHING
BUILT.** `out/` was **not** touched: the armed rung-31 arm `armed-storage-f9613649` is byte-identical
before and after this step, its eleven-member park is intact, and no entry or payload source was
edited. One press spent: **none**. **No press is authorized.**

Every number below is read out of a capture already on disk under `out/stage90/captures/`, and every
source claim is an execution of `grep`/`sed` against the working tree rather than a recollection.

## 1. Why this step exists, and it is the press's own narration that sent me here

792 §7 pre-registered rung 31's four rows and gave the first one as **"`_cid_complete = 1` — THE CID IS
TAKEN"**, and that sentence is also carried into `tools/verify_press_ready.sh`, the file whose rung-30
branch and `rung_para 30` **are the text the next press prints**. The claim rests on an assumption that
was never checked: that the four `RESPONSE` words read after CMD2 would be *a CID if the command
completed*. **That assumption is false, and the archive says so nine times over.**

This step checks the assumption with readings that already exist. **It costs nothing and it lands before
the press, which is the only moment at which a narration error of this kind can still be corrected for
free** — after the press, a reader would take the same log and read its first row as the CID.

## 2. The four words are read unconditionally — so they are read on every arm, completed or not

`st_all_send_cid`'s tail (`src/entry/entry_storage.c:3341-3366`, the block introduced by the comment
citing `sdhci.c:1163-1172`, the driver's own 136-bit branch, word 3 first):

```c
raw0 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
raw1 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 8u);
raw2 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
raw3 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
... ST_LIVE cid_raw0..3, then cid_resp0..3 from (rawN << 8) | the byte below ...
```

**There is no `if` in that block** — `grep -c 'if ('` over lines 3341–3362 is **0**, and the comment on
the following line says of the exit *"unconditional, on the line after the publishes"*. So the four
words are read on **every** arm that has this body, whatever the command did. `_cid_raw0..3` and
`_cid_resp0..3` are therefore **not** a cell that only fires on success — they are a cell that always
fires, and their *content* is a separate question from `_cid_complete`.

## 3. Nine arms carry them, and all nine are byte-for-byte identical

| capture | ordinal / value | `INT_ENABLE` at CMD2 | `_cid_complete` | `_cid_status_any` | `_cid_raw0` | `_cid_raw1` | `_cid_raw2` | `_cid_raw3` | `_cid_resp0` | `_cid_resp1..3` |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `rung17-cid` | 17 / 16 | `0x00000001` | 0 | `0x00000000` | `0x0040ff80` | `0x80000000` | **0** | **0** | `0x40ff8080` | **0** |
| `rung18-rb` | 18 / 17 | `0x00000001` | 0 | `0x00000000` | `0x0040ff80` | `0x80000000` | **0** | **0** | `0x40ff8080` | **0** |
| `rung19-rca` | 19 / 18 | `0x00000001` | 0 | `0x00000000` | `0x0040ff80` | `0x80000000` | **0** | **0** | `0x40ff8080` | **0** |
| `rung20-nrsp` | 20 / 19 | `0x00000001` | 0 | `0x00000000` | `0x0040ff80` | `0x80000000` | **0** | **0** | `0x40ff8080` | **0** |
| `rung21-noidx` | 21 / 20 | `0x00000001` | 0 | `0x00000000` | `0x0040ff80` | `0x80000000` | **0** | **0** | `0x40ff8080` | **0** |
| `rung22-dllcensus` | 22 / 21 | `0x00000001` | 0 | `0x00000000` | `0x0040ff80` | `0x80000000` | **0** | **0** | `0x40ff8080` | **0** |
| `rung24-cmdline` | 24 / 23 | `0x00000001` | 0 | `0x00000000` | `0x0040ff80` | `0x80000000` | **0** | **0** | `0x40ff8080` | **0** |
| `rung29-2win` | 29 / 28 | **`0x000f0001`** | 0 | **`0x00028000`** | `0x0040ff80` | `0x80000000` | **0** | **0** | `0x40ff8080` | **0** |
| `rung30-c1win` | 30 / 29 | **`0x000f0001`** | 0 | **`0x00028000`** | `0x0040ff80` | `0x80000000` | **0** | **0** | `0x40ff8080` | **0** |

**Nine arms. Seven of them narrow, two of them wide enough to report an error, two of them having
latched `ERR | CRC` at poll 1 — and the eight published words are identical on all nine.** The census is
`grep -l xnu_live_storage_cid_raw0 out/stage90/captures/*.txt`, and it returns exactly those nine files
(the ladder has 32 archived `*-last_kmsg.txt` captures).

**A CID is 128 bits.** Three of the four words being zero, on every reading this ladder has ever taken,
is not a CID. **So the ladder has never read a CID, and — this is the point — it was never going to read
one merely by completing the command, because these cells were read on nine arms that did not complete
either and read the same thing.**

## 4. And the word is not a static default — the register block MOVES, and the log's own line order proves it

The obvious escape — *"`0x40ff8080` is the block's power-on default, so of course it is the same
everywhere"* — is **closed by readings already in the archive**, and closed by the project's own rule
that a claim about *when* a cell was read is part of the cell (`m732`).

`st_resp_before` (`src/entry/entry_storage.c:3446`, called once at `:5095`) reads **the same four offsets
with the same `<< 8` arithmetic** — deliberately, because rung 18's question was *"is the word the same
before and after CMD2"* — and it runs immediately **before** `st_cmd_path()`. So it is the register
block's state on entry to the command sequence. On the four arms that carry both bodies:

| arm | `_rb_raw0` (+12, **before** the command path) | `_cmd0_resp` (+0, after CMD0) | `_cmd1_resp` (+0, after CMD1) | `_cid_resp_short` (+0, after CMD2) | `_cid_raw0` (+12, after CMD2) |
| --- | --- | --- | --- | --- | --- |
| 17 | *(key did not exist)* | **0** | **`0x40ff8080`** | **0** | **`0x0040ff80`** |
| 18 | **0** | **0** | **`0x40ff8080`** | **0** | **`0x0040ff80`** |
| 29 | **0** | **0** | **`0x40ff8080`** | **0** | **`0x0040ff80`** |
| 30 | **0** | **0** | **`0x40ff8080`** | **0** | **`0x0040ff80`** |

**And the log's own line order, which is a reading and not an inference.** Rung 30's capture:

```
8674: xnu_live_storage_rb_raw0=0x00000000
8777: xnu_live_storage_cmd0_resp=0x00000000
8827: xnu_live_storage_cmd1_resp=0x40ff8080
8855: xnu_live_storage_cid_status_any=0x00028000
8875: xnu_live_storage_cid_resp_short=0x00000000
8876: xnu_live_storage_cid_raw0=0x0040ff80
```

Rung 18's is the same ordering (8674, 8746, 8793, 8815, 8825, 8826); rung 29's is the same (8676, 8779,
8827, 8850, 8853, 8873, 8874). **So in three separate boots the register block is `0` before the command
path, `0` after CMD0, `0x40ff8080` at `+0` after CMD1, and after CMD2 it is `0` at `+0` and `0x0040ff80`
at `+12`.** The value is written by the commands and it moves between offsets. **It is not a default, and
the 136-bit read is reading a real 32-bit word the block put there** — one word, at twelve zero bytes'
distance from three other words that are zero.

**What this does not say.** It does not say CMD2 wrote it, and it does not say CMD1 did. Two commands'
worth of movement are in one interval and the archive does not separate them. It says only that the
register was **zero when the ladder arrived**, so everything the block later holds it put there itself.

## 5. The correction: row 1 must be split, and the second half is the stronger row

**792 §7's row 1 — "`_cid_complete = 1` — THE CID IS TAKEN" — is an overclaim and is superseded here.**
`_cid_complete` is `st_send_command`'s completion latch. The four response words are read
unconditionally (§2) and have been the same eight values on nine arms (§3). **A completed command does
not by construction fill them**, and the row must be read as two:

| row | reading |
| --- | --- |
| **1a — `_cid_complete = 1` AND the register block CHANGED** (any of `_cid_resp0` off `0x40ff8080`, `_cid_raw2`, `_cid_raw3`, or `_cid_resp1..3` off zero) | **THE CID IS TAKEN.** This is 792 §7's row 1 as it should have been written, and it is the row that moves the ladder: the driver order then opens — CMD9 (CSD), CMD7 (select), CMD16 (block length), then data. |
| **1b — `_cid_complete = 1` and the block is UNCHANGED** (the nine-arm pattern, byte for byte) | **A FIFTH ROW, AND IT IS STRONGER THAN 1a.** The block **completed** a 136-bit command **and its response register file never filled**. That separates the completion latch from the response path, and it puts every `_cid_complete` reading the ladder has taken under suspicion in the direction that matters — the bit would be reporting the *command's* completion and not the *response's* arrival. It would also put 790 §1's CMD1 row and its 2.094 ratio back on the table with a new subject. |

Rows 2 (`ERR | TIMEOUT`), 3 (`ERR | CRC` anyway) and 4 (nothing over the full bound) are unchanged, and
**row 3's reading is sharpened rather than weakened**: `_cid_raw2`/`_cid_raw3` being zero on the two wide
arms (§3) is a second, enable-independent line of evidence that the CRC latch fires with no 136-bit
response present.

## 6. Where the correction has to land, and why it is a file and not a paragraph

The sentence is in `tools/verify_press_ready.sh`, in two places that are the text the press prints:
`entry_conseq`'s rung-30 branch (1-based line 1104) and `rung_para 30` (line 1215). **Both are corrected
in this step** by appending the split and the `_cid_raw2`/`_cid_raw3` discriminator, **not** by deleting
the original sentence — a reader who saw the old text elsewhere must be able to find the correction next
to it. The step also adds a `# 793:` block to `records/revert-set.txt` and a row to
`docs/experiments/README.md`.

**Lane, stated plainly rather than left to be inferred.** `tools/verify_press_ready.sh` is **not**
`preflight_boot_check.sh`, which is the one file this session (`run-experiment-526`, the gate owner)
owns. `ListAgents` today names this session and two peers (`zl1-bb10-10`, `xing4-decode-launch-profile`),
and **neither is `mi4-ios6-1a`** — the session the tree's lane agreement assigns the rest of the press
path to. **So this edit is a LANE-ownerless case and not the COST case**: the file has no live owner to
report it to, and the alternative to making the edit is leaving a wrong row in the one text the operator
reads before spending a press. Recorded here so a later reader does not have to reconstruct which of the
two reasons applied.

## 7. What this does not do

- **It builds nothing, edits no source under `src/`, and moves no byte of `out/` or of any park.** The
  armed arm is 792's, unchanged and unspent.
- **It spends no press and authorizes none.**
- **It does not identify which command moved the register** (§4). It does not say `0x40ff8080` is or is
  not a real answer to CMD1; 790 §5's finding stands unchanged — the value is identical on three arms
  whose CMD1 windows differ, so it is not evidence the card answered.
- **It does not reduce rung 31's answer space.** It converts one ambiguous row into two sharp ones and
  adds `_cid_raw2`/`_cid_raw3` — two cells that have been **zero on every arm this ladder has ever run** —
  to the set of things the press reads. **The value of the armed press went up; its cost did not.**
- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made, so
  「让os可以正常启动并且挂载存储」 is not reached and **TWRP-to-storage stays withheld.**

## 8. Owed, and named rather than left to be inferred

- **Row 1b of §5 is now a live row and has no key that names it.** A reader has to detect "the block
  changed" by comparing five cells by eye. A `_cid_resp_changed` cell — one bit, computed in the body
  from the four raw words against their pre-command values, no new device access — would make it a
  reading. **COST-owed to a build**, and it is the cheapest addition the next arm can make at this
  window.
- **`_cid_raw2` / `_cid_raw3` are the two cells 792's row 1 needed and did not name.** They exist on the
  armed arm already (they are published at `:3349-3352`); what was missing was a sentence saying to read
  them. **Paid by this step, in `tools/verify_press_ready.sh`.**
- **Which command moved the register (§4)** — two commands, one interval, no cell that separates them.
  A pre-CMD1 read of the same four offsets in the same arithmetic would do it. **COST-owed.**
- Unchanged from 787–792: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head (COST); a
  `_cmd1_raw_pre*` / `_cmd1_raw_post*` pair around CMD1's own `RESPONSE` registers (COST, and §4 makes it
  more valuable); `c3.inhibit_timeout` unpublished for the `nidx` family (COST); the four `*_status_post`
  masked cells (791 §5, COST); the four `5,088,000`s and the mis-citation at `entry_storage.c:302-303`
  (COST); a check that counts `ST_LIVE` sites per key (789 §4); the `_Static_assert` message's bit map at
  `entry_storage.c:2360` (789 §5); `_cid_ps_after`'s second producer (789 §2, COST); the set-comparison
  pad repair (779 §7); the `rung_para` correction for values 12..23; the seam-address class;
  `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell (782 §6);
  784's `rail_name` cell; and 783's window-scope check — landed at rung 30's window and not at the
  ladder's other windows.
