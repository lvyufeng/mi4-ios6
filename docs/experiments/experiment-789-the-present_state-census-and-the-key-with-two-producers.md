# 789: 148 `PRESENT_STATE` reads, three values — and the one key that printed two of them in one run

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER, NOTHING
BUILT.** `out/` was **not** touched and no entry source was edited, so the armed rung-30 arm
(`armed-storage-dfc4ae78`, `STAGE90_XNU_STORAGE_PROBE=29`) is byte-identical before and after this
step. One press spent: **none**. **No press is authorized.**

Every number below is read out of a capture already on disk under `out/stage90/captures/`.

## 1. The census, and the selector that had to be fixed before it could be taken

Every `PRESENT_STATE 0x24` word the ladder has ever published — **148 reads across 20 captures** —
takes exactly **three** values:

| word | count | `CMD_INHIBIT` (bit 0) | CMD line level (bit 24) | which keys |
| --- | --- | --- | --- | --- |
| `0x01f80000` | **142** | 0 | **HIGH** | the steady state, every command's `_ps_before` and `_ps_after` |
| `0x01f80001` | **5** | **1** | **HIGH** | `_nidx_ps_after` (rungs 21, 22, 24, 29) and `_rca_ps_after` (rung 19) |
| `0x00f80000` | **1** | 0 | **LOW** | **`_cid_ps_after` on rung 29 — the second of its two lines** |

Bits 21–23 (write-protect, card-detect, card-stable) are **`111` in all three words**, and bits 17/18
are **`0` in all 148 reads** while bits 19/20 are **`1`** — so under the standard SDHCI map the block
reports DAT[3] and DAT[2] low and DAT[1]/DAT[0] high at every measurement it has ever taken. **That is
constant across the archive and therefore says nothing about this run**: it is either this IP's fixed
reading for those bits or a genuinely static low, and **nothing in the archive separates the two**.
Named, not concluded.

**The selector is the first thing that had to be corrected, and it is a defect class this project
names.** A first pass keyed on `\S*ps\S*` swept in fourteen keys that are not `PRESENT_STATE` at all —
`_cc_polls`, `_dll_*`, and above all **`_clk_set_apps_before`**, whose value `0x00004ff1` then appeared
in the table as if it were a bus reading. **`apps_before` contains `ps_before` as a substring**, which
is the m-class *a name that contains another name*; the census above is taken with the token anchored
at the end of the key. A census whose selector is looser than its claim produces a table that looks
like a measurement.

## 2. `_cid_ps_after` has two producers, and one run proves it

`src/entry/entry_storage.c` publishes `xnu_live_storage_cid_ps_after` from **two** sites in the same
function:

| line | what it publishes |
| --- | --- |
| `:3270` | `c2.ps_after` — the `PRESENT_STATE` word `st_send_command` recorded **at the end of CMD2's poll** |
| `:3306` | a **fresh read** of `PRESENT_STATE 0x24`, taken on the line after the window's exit, beside `_cid_readback` |

On rung 29's arm they produced **different values**: `0x01f80000` at capture line 8867 and
`0x00f80000` at line 8885. **So `_cid_ps_after` is one name over two moments** — the m739 class (*a
name with two producers*) and the m732 class (*a cell's meaning is partly WHEN it was read*) at once.
It is **the only key in the archive that carries two values in a single run**, and the archive-wide
census in §1 finds it by searching for exactly that property.

**A reader quoting `_cid_ps_after` must say which of the two lines they mean**, and the two lines are
not interchangeable: the first is the block's view at the end of a poll that gave up; the second is its
view after the window had already been restored.

## 3. The one reading, and what it is a reading of

**The second `_cid_ps_after` is the only `PRESENT_STATE` word in the archive's whole history with the
CMD line read LOW outside the poll's own samples** — one read in 148. It sits **after** the window's
one exit: `_cid_wrote_back` (`:3301`), the `INT_ENABLE` store, `_cid_status_post`, `_cid_readback`,
and then this read.

So at CMD2's exit the block still reported its own CMD line **low** — the line was still being driven
a whole window after the poll gave up. The companion in the same capture is `_cmd1_inhibit_last =
0x00f80000`, the last sample of CMD1's inhibit window, which 786 §6 already recorded.

**Two cells, two different windows, the same word**: the CMD line was low at the last sample of CMD1's
inhibit window and at CMD2's post-window read, and high at all 142 other measurements in the archive.
**That is a wedge signature and it is not a new observation so much as a new place to read it** — rung
19 found the inhibit held for all 1024 samples of CMD3's window, and this says the line itself is
still low one window later.

**What it is not**: it is not evidence that the card is holding the line. The block drives the CMD
line, and 788 §4's residual — whether an enable gates a condition's visibility or switches the check on
— is untouched here. A line low at a moment the block is not polling is consistent with a command
still in flight, and it is also consistent with a driver that never released the line.

## 4. Nothing in the tree catches a key with two producers

`tools/read_storage_commands.py` reasons about **producer *shapes* of branch conditions**, which is a
different question, and it is **not in `make check`**. A `grep` over `tools/` and `src/entry/` finds no
check that counts the `ST_LIVE` sites per key. **So the m739 class is named repeatedly in this
project's records and is enforced nowhere** — and §2 is the demonstration that the class is live, not
hypothetical. That is the owed item this step names; it is not built here.

## 5. One more piece of unverified prose, in a message nothing reads

`entry_storage.c:2360` carries, inside a `_Static_assert` **message**:

> *the bit must live in the byte the specification puts the line levels in — 23 is DAT[0], 24 is CMD,
> 25-27 are DAT[3:1]*

The assert it belongs to checks only `(ST_SDHCI_CMD_LINE_LEVEL & ~0xFF000000u) == 0u`. **The bit map
in the message is read by nothing** — it is the m-class *a claim in a comment no check reads* — and it
disagrees with the standard SDHCI map (bit 16 CMD signal level, 17–20 DAT[3]–DAT[0], 23 card stable, 24
CMD level). Under the message's map bits 25–27 are `0` in all 148 reads, i.e. DAT[3:1] low; under the
standard map bits 17/18 are `0` in all 148 reads, i.e. DAT[3]/DAT[2] low. **Both readings say some data
lines are low; neither can be confirmed, because the map itself is unchecked.** Named, and owed.

## 6. What this does not do

- **It spends no press and authorizes none, and it moves no byte of `out/` or of any park.**
- **It does not decide §1's DAT question or §3's wedge question.** Both need a reading the archive
  does not have: §1 needs a measurement where the DAT lines are known to move, and §3 needs a
  `PRESENT_STATE` read at a moment the ladder chooses rather than at a window boundary.
- **It does not change the next press.** The armed arm is the same `armed-storage-dfc4ae78`; §2 and §3
  are readings of an arm already spent, and their value is that a reader of rung 29's capture now knows
  which of two lines under one name they are looking at.
- **It does not reach the goal.** No transfer completes, no filesystem is reached, no mount is made, so
  **TWRP-to-storage stays withheld**.

## 7. Owed, and named rather than left to be inferred

- **A check that counts `ST_LIVE` sites per key** (§4) — the m739 class has a demonstrated instance and
  no enforcement.
- **The `_Static_assert` message's bit map at `entry_storage.c:2360`** (§5) — unverified prose in a
  message, contradicting the standard map, and the map it states cannot be confirmed from the archive.
- **`_cid_ps_after`'s second producer** (§2) — either the key should be renamed or the second read
  should be, so that one name is one moment. COST-owed to a build, because it is a source change.
- Unchanged from 787–788: the `0x40ff8080` "the card ANSWERED" comment at CMD1's head; the four
  `5,088,000`s and the mis-citation at `entry_storage.c:302-303`; the set-comparison pad repair (779
  §7); the `rung_para` correction for values 12..23; the seam-address class; `run_and_capture.sh`'s
  `EXIT_POP_LR_LITERAL`; `fdt_nodes`'s lack of a synthetic FDT cell (782 §6); 784's `rail_name` cell;
  and 783's window-scope check — landed at rung 30's window and not at the ladder's other windows.
