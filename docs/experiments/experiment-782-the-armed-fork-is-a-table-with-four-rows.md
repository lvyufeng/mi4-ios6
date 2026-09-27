# 782: the armed fork is a table with four rows — the same declarations name the register's OFF state, and a two-row fork merges it with *never applied*

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO GATE AGAINST A DEVICE, NO RUNNER, NO FIRER, NOTHING
BUILT.** `out/` was not touched and no entry source was edited, so the armed rung-28 arm
(`armed-storage-d5d98738`, `STAGE90_XNU_STORAGE_PROBE=27`), every park and every capture are
byte-identical before and after: `stage90-qcdt.img` `93026ca1…` and `xnu_arm_entry.bin` `d5d98738…`,
both equal to the set lines the record carries. One press spent by this step: **none**. The readiness
tool was run once, host-side, because this step edits the narration it prints — **5 of 5, exit 0**,
under the arm's own flags, with no device action. Every input below is the device's own `.gitignore`d
backup (`.gitignore:2`), read on this machine and landed as a **tracked** record, as in 779 and 781.

## 1. The gap this step closes

776 built rung 28 as a **fork**, and 779 narrowed its second row:

> `_pad_raw = 0x00009F24` with `_pad_write_skipped = 1` and `_pad_writes = 0` means **THE PADS WERE
> ALREADY CONFIGURED** … anything else with `_pad_write_skipped = 0` means the vendor's pad act was
> never applied on this path and the write HAS BEEN MADE.

779's narrowing was about *which board's* word row 1 is — the guard's word is MTP V5's, and the device
declares two ON words. **This step is about a value the fork still has only one name for: the register
the guard reads has *two* states, and the same device trees that declare the ON word declare the OFF
word too.**

## 2. The OFF word, derived from the same two arrays

`qcom,pad-pull-off` and `qcom,pad-drv-off` sit beside `qcom,pad-pull-on` and `qcom,pad-drv-on` in the
node, and the kernel applies them by the same `msm_tlmm_set_field` arithmetic. Through it, all **five**
trees that carry the node declare the same OFF word:

| | `pull-off` | `drv-off` | word |
| --- | --- | --- | --- |
| every tree that carries the node | `[0, 3, 3, 1]` | `[0, 0, 0]` | **`0x00009E00`** |

**And `0x00009E00` is the pull half of `0x00009F24` with the drive half zeroed** — the same four pull
fields (CLK 0, CMD 3 = pull-up, DATA 3 = pull-up, RCLK 1 = pull-down) at the same shifts, and the three
hdrive fields at 0 instead of 4. The two ON words and the OFF word differ **only** in the hdrive
fields:

| word | pull half | hdrive half | what it is |
| --- | --- | --- | --- |
| `0x00009F24` | `0x9E00` | `0x124` (4/4/4) | four trees' ON word — the ladder's constant |
| `0x00009FE4` | `0x9E00` | `0x1E4` (7/4/4) | the PMA8084 MTP tree's ON word |
| `0x00009E00` | `0x9E00` | `0x000` (0/0/0) | **every tree's OFF word** |

## 3. So the fork's second row is four rows, and three of them are distinguishable

`_pad_raw` is the reading, and it says which of these the live register holds:

| `_pad_raw` | what it says | what it closes | what stays the subject |
| --- | --- | --- | --- |
| `0x00009F24` | the pads are at the vendor's `CORE_PWRCTL_BUS_ON` state | 768 §5's other half, and any *missing pull-up on SDC1_CMD* mechanism | nothing about the pads |
| `0x00009FE4` | configured to **the other declared board's** word | the same, for `PULL SDC1_CMD` | the CLK hdrive field, 7 vs 4 |
| **`0x00009E00`** | **the pulls are PRESENT and the drive is OFF** | **the pull-up mechanism, on this row too** | the three hdrive fields |
| `0x00000000` or anything else | a state **no declaration in this archive produces** | — | who wrote it |

**Row 3 is the row the two-row fork could not name, and it is not a corner case.** A controller whose
pads were configured and then driven to their *off* state by a power-down lands there, and its pull
half is **identical** to row 1's. That matters for the reason 769 §2 named the pull-up at all: MMC's
CMD line is open-drain during identification and returns HIGH only through that pull, so a register at
`0x9E00` has the pull-up present — **the mechanism 769 §2 offered for *a card that cannot see the
host* is retired on row 3 as well, and a fork that calls it *the vendor act was never applied* merges
that with row 4.** On row 4, by contrast, the pull fields are zero and the mechanism is live.

## 4. Landed as three artifacts, because a sentence is not a constraint

| artifact | what changed |
| --- | --- |
| `tools/derive_sdc1_pads.py` | derives the OFF word from the same node (`derive_or_none`, which returns **None** for an array shorter than the field list rather than silently truncating it via `zip` — SDC2's three-entry array is the live case); prints the OFF words in a block of their own, saying why a two-row fork is not enough |
| `records/sdc1-pad-candidates.txt` | **the tracked reading** carries an `OFF word` column per tree and one machine-readable `# offword 0x00009e00  pull-off … drv-off …  on …` line |
| `tools/check_sdc1_pad_expectation.py` | **a third refusal**: a guard constant equal to an OFF word is refused, because the guard asks *does the register already hold the word I expect* and a constant set to the off word would report a **powered-down pad as already configured** |

**And `tools/derive_sdc1_pads.py` now has the self-test 779 owed it** — eleven cells, seven on the
arithmetic alone (both ON words, the OFF word, and the three short/absent array shapes that must answer
*no word* rather than a truncated one) and four against the device's own file (the four controller
addresses, the two enabled ones, the ladder's node at five trees and window `0x1a0`, and
`prop_summary` reporting the differing pad array **as differing**). **Where `dt.img` does not exist the
four file cells print `SKIPPED` by name** — a cell that cannot run must not read as one that ran and
agreed — and the tool is in `make check` now, `--selftest` first.

**The three refusals are falsified, not asserted.** `make check` exit 0; five OFF-word self-test cells
pin `offword_verdict` as a pure function; and a scratch record whose `# offword` line is the guard's own
constant refuses with the reading named:

```
REFUSED 0x00009f24 is an OFF word in the record -- the value these declarations imply with the
controller powered DOWN. The guard compares the live register against the ON word, so a constant
set to an off word would report a powered-down pad as already configured
```

## 5. What this does not do

- **It does not change the arm.** Not a cell, not a store, not a byte: `_pad_raw`,
  `_pad_write_skipped`, `_pad_writes`, `_pad_wrote`, `_pad_after` and `_pad_after_match` are what they
  were, and the guard still compares against `0x9F24` alone. This step makes the *answer* decidable, not
  the experiment different — and that is deliberate, because editing `src/entry/entry_storage.c` to
  compare against the **set** would invalidate the armed arm for a COST reason (763 §5).
- **It does not say the pads are the cause of anything.** Three of the four rows have `PULL SDC1_CMD`
  present, so on three of them the pull-up is *not* the reason the card is silent, and `PULL SDC1_CMD`
  is necessary and not sufficient on all four.
- **It does not establish the card is powered.** No capture in this archive holds a PMIC, SPMI,
  regulator or card reading of any kind; 780 weakened the reason the record gave for closing the power
  candidate and did not close it.
- **No press is spent and none is authorized.** The arm is armed, unspent, and the press is the
  operator's.

## 6. Owed, and named rather than left to be inferred

- **The set-comparison repair is still the substantive one and is still not built**: make the arm
  compare `_pad_raw` against the **set** of words this device declares and publish which member matched,
  rather than against one board's word. 779 pre-registered it; it is a build; it is the operator's call
  because it moves `out/`.
- Unchanged: the four `5,088,000`s and the mis-citation at `entry_storage.c:302-303` (COST-owed, to be
  carried by a build); the `rung_para` correction for values 12..23; the seam-address class;
  `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; the 737 window paragraph (paid by the rung-28 build).
- **`node_verdict` and `offword_verdict` are pure functions with cells; `fdt_nodes` is not.** The FDT
  walker is exercised by the four file cells and by nothing synthetic — a hand-built two-node FDT blob
  as a cell is the obvious next tightening and is not done.

## 7. What this document does not say

- **It does not say which of the four rows is more likely.** 776 and the record have argued that a board
  which boots from this eMMC to `fastboot` has had *something* drive that bus, which makes a configured
  register the more likely reading — and the arm is what asks.
- **It does not propose a new rung** and does not authorize one.
- **It does not spend a press.**

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block issues a
response-demanding command, drives the CMD line, and **times out because the card does not answer** —
and 「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage stays withheld.**
