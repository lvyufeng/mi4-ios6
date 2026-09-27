# 779: the pad expectation is MTP-V5's word, and the device's own device tree is on disk

**HOST-SIDE ONLY. NO DEVICE ACTION, NO PRESS, NO FIRER, NOTHING BUILT.** `out/` was not touched: this
step edits no entry source, so the armed rung-28 arm (`armed-storage-d5d98738`,
`STAGE90_XNU_STORAGE_PROBE=27`) and every parked arm are byte-identical before and after it. One press
spent by this step: **none**. The gate was run **once, host-side, with no device action**, because this
step edits the gate's own narration and a narration edit is verified by running the gate — readiness **5
of 5, exit 0**, under the arm's own flags.

## 1. What the source says its constant comes from, and what this device actually declares

`src/entry/entry_storage.c` compares the live TLMM SDC1 pad register at `0xFD512044` against
`ST_TLMM_SDC1_EXPECT`, and its comment says where the value came from:

> The expected value is DERIVED below rather than typed, from the Mi 4's own board file
> (`msm8974pro-ac-pm8941-mtp-v5.dts:25-29`: `qcom,pad-pull-on = <0x0 0x3 0x3 0x1>`,
> `qcom,pad-drv-on = <0x4 0x4 0x4>`), through the same shift-and-mask the kernel performs.
> — `entry_storage.c:302-303`

Both halves of that are wrong, and neither is visible from the source.

**It is not the Mi 4's board file.** The vendor checkout
(`external/android_kernel_xiaomi_cancro/arch/arm/boot/dts/`, 381 files) contains **no cancro, xiaomi or
Mi 4 device tree at all** — it holds Qualcomm's reference boards, MTP and CDP and FLUID and LIQUID.
`msm8974pro-ac-pm8941-mtp-v5.dts` is one of **four** MSM8974PRO-AC MTP variants.

**And it is not in the repository.** `.gitignore:12` excludes `external/`, under a comment that calls
those checkouts *large, reproducible*. So the citation names a file no clone can resolve.

## 2. The device's own device tree is on disk, and it declares TWO words

The tree the ladder's image actually carries is not a `.dts` at all: it is the `dt.img` from the device's
own partition backup, which `scripts/build.sh:130` packs into `stage90-qcdt.img` as `$QCDT_DT`. It is
2,521,088 bytes, sha256 `c8faf487…`, and it is a QCDT table — **a list of alternatives**, one of which the
bootloader selects on the device.

`tools/derive_sdc1_pads.py`, added by this step, parses it and re-derives the register word each tree
implies, through the kernel's own shift and mask (`msm_tlmm_set_field`, `gpio-msm-common.c:481-496`;
hdrive width 3 at shifts 6/3/0, pull width 2 at 13/11/9/15, DT arrays indexed from the enum base). Six
FDT trees, five carrying a `sdhci@f9824900` node:

| # | model | board-id | `pad-pull-on` | `pad-drv-on` | expect |
| --- | --- | --- | --- | --- | --- |
| 0 | `Qualcomm MSM 8974Pro-AC + PMA8084 MTP` | 8 1 | `[0, 3, 3, 1]` | `[7, 4, 4]` | **`0x00009FE4`** |
| 1 | `Qualcomm MSM 8974Pro-AC MTP` | 8 256 | `[0, 3, 3, 1]` | `[4, 4, 4]` | `0x00009F24` |
| 2 | `Qualcomm MSM 8974Pro-AC MTP V4` | 8 1024 | `[0, 3, 3, 1]` | `[4, 4, 4]` | `0x00009F24` |
| 3 | `Qualcomm MSM 8974Pro-AC MTP V5` | 8 1280 | `[0, 3, 3, 1]` | `[4, 4, 4]` | `0x00009F24` |
| 4 | `Qualcomm MSM SAMARIUM RUMI` | — | — | — | *(no SDC1 node)* |
| 5 | `Qualcomm MSM 8974Pro-AA/AB MTP` | 8 0 | `[0, 3, 3, 1]` | `[4, 4, 4]` | `0x00009F24` |

**`ST_TLMM_SDC1_EXPECT = 0x00009F24` is four of those five, and it is MTP V5's.** The other one is
`0x00009FE4` and differs in one field: CLK hdrive **7** instead of 4 — one masked field of one register.

**Which tree the bootloader selects is not established anywhere in this tree, and this step does not
establish it.** The tool prints every alternative and picks none.

**And every tree that carries the node declares `qcom,vdd-always-on` and `qcom,vdd-io-always-on`.** That
is worth having: the record's open caveat — *"not that the rails are up on the physical device (the Mi 4's
own board dts is not in this tree)"* (`records/revert-set.txt:5646`) — is now answered **at its
provenance**: the device's own device tree IS in this tree, in the boot backup. What it is **not** is a
measurement, and this step does not close the rail question: a device tree is a declaration.

## 3. The fork's second row is narrower than it reads, and 776 said it the wide way

776 built the arm as a **fork**: `_pad_raw == 0x9F24` with `_pad_write_skipped = 1` means the pads were
already configured and no store is made; anything else means, in the arm's own words, *"the vendor act was
never applied on this path and the write has been MADE"*.

**That second row is not what it says.** The guard's word is MTP V5's, and a register holding `0x9FE4` is
a **configured** pad — declared by another tree on this same device — that this arm would re-configure to
V5's word. On that run:

- `_pad_write_skipped = 0` says *the register did not hold `0x9F24`*, not *the pad was unconfigured*;
- `_pad_after = 0x9F24` with `_pad_after_match = 1` says the store landed, **not** that the fix landed;
- and the reading that matters is `_pad_raw`, which is taken unconditionally and is unaffected.

**So the fork is `raw == 0x9F24` versus `raw != 0x9F24`, and `_pad_raw` is the reading.** The discount is
now in the gate's own narration for this arm, in both places it is printed, so it is beside the cells
before the press and not in a document a reader has to find.

**And a second thing the arm's narration over-claimed, corrected in the same edit**: it says the constant
is *"the value this board's own `qcom,pad-pull-on`/`qcom,pad-drv-on` arrays produce"*. It is a value some
tree on this board produces. Those are different statements on a device with two of them.

## 4. The repair is a derivation and a tracked reading, because the input cannot be tracked

The input cannot go in the repository: `xiaomi4-cancro-backup-*/` is `.gitignore`d (`.gitignore:2`), the same way
`external/` is. So the repair is two files and a check:

| artifact | what it is |
| --- | --- |
| `tools/derive_sdc1_pads.py` | parses any QCDT blob, walks each tree, prints every SDC1 pad declaration and the word it implies, and **refuses nothing on ambiguity** — it names which trees match a candidate constant and does not pick |
| `records/sdc1-pad-candidates.txt` | **the tracked reading**: every candidate word, the model and board-id of each tree that declares it, and the **sha256 of the `dt.img` it was read from**. This is what converts a gitignored derivation into something a clone holds |
| `tools/check_sdc1_pad_expectation.py` | in `make check`: reads the **tracked record** and refuses an `ST_TLMM_SDC1_EXPECT` that matches **no** candidate. Carries a 9-cell `--selftest` |

**The check's scope is deliberately narrower than "is the constant right".** It can catch a constant that
no board on this device implies, and it **cannot** catch one the wrong board implies — because the record
holds two words and the choice is made on the device. Four falsification cells were run against scratch
records: a record holding only the other word refuses; a record with no candidate line refuses; a candidate
list with no `# input`/`# sha256` header refuses. A record holding both passes and **prints** the ambiguity,
so the fact lives in the output of every `make check` rather than in a comment.

**`derive_sdc1_pads.py` is honest about the missing input in the other direction too**: run on a machine
without the backup it exits 2 and says which two gitignored paths it would need, and names the tracked
record it falls back to.

## 5. And the check 778 landed refused its own self-test, so this step began by repairing it

`make check` was **red on `master` at `9a0291e`**, the commit before this one. The cause is 778's own
class, a third time: `tools/check_count_citations.py` scans `git ls-files`, so while that file was
untracked its ten `SELFTEST` cells were invisible to it — and **three of those cells ARE the defect they
test**, so committing the file made the check refuse itself with exit 2.

**The green 778 recorded was measured in a state the commit destroys.** Its record says *73 counts, 0
disagreeing in any scope*; the reading was taken before the tool and the document were staged, so the file
being verified was excluded from the verification and so was the document that quotes the defects. The
repair is 778's own repair a third time — the three cells are now one string in the source split across two
adjacent literals, so the cell still tests the real string at runtime and no line of the file contains the
bad pair. The honest count after it is **108 counts, 0 disagreeing in the refusing scope, and 11 in the
historical record**, and those 11 are this repository's two documents quoting the defects on purpose, in
the scope that reports and does not refuse.

That is landed as its own commit (`779a`) because a red `make check` on `master` deserves to have its own
line in the history rather than to be folded into a step about pads.

## 6. What this does not disturb

- **The arm still takes its reading.** `_pad_raw`, `_pad_write_skipped`, `_pad_writes`, `_pad_wrote` and
  `_pad_after` are unchanged; nothing here touches `out/` or the entry source.
- **The guard and the build clauses are unchanged**, including the two independent refusals that keep the
  store off the unconditional path.
- **The rail question is where the record left it.** Every tree on this device says the slot's supplies are
  always-on; a device tree is a declaration, and no capture in the archive contains a PMIC, SPMI or
  regulator reading of any kind. This step narrows that caveat's *provenance* and does not close it.
- **No press is spent and none is authorized.**

## 7. Owed, and named rather than left to be inferred

- **`src/entry/entry_storage.c:302-303` — the mis-citation itself, still standing.** It is an entry source:
  a member of the entry image's recorded source manifest, so editing a character of comment makes the gate
  refuse the arm a press is waiting on (763 §5's measured cost). Owed for the **COST** reason and not a lane
  reason, and the next build should be an arm that carries it — the fix is to name the tree the word came
  from and to cite `records/sdc1-pad-candidates.txt` for the candidates.
- **`ST_TLMM_SDC1_EXPECT` is one constant where the device declares two.** Making the arm compare against
  the **set** — and publish which member matched — is the substantive repair, and it is a build. It is
  **pre-registered here and not built**: the arm is armed and unspent, and moving `out/` is the operator's
  decision, not this step's.
- **`tools/derive_sdc1_pads.py` has no self-test**, unlike the two reference tools beside it in
  `make check`. It is not in `make check` itself, so the debt is smaller than `read_storage_key_order.py`'s
  was — but it is a reference tool whose output looks plausible when it is wrong, which is exactly the
  condition those two self-tests exist for.
- Unchanged from 775/776/777/778: the `rung_para` correction for values 12..23; the seam-address class;
  `run_and_capture.sh`'s `EXIT_POP_LR_LITERAL`; the 737 window paragraph; and the four `5,088,000`s in
  `entry_storage.c`.

## 8. What this document does not say

- **It does not say the constant is wrong.** Four of the device's five SDC1 trees declare it. It says the
  constant is **one board's**, that the source calls it the device's, and that the arm's else-branch reads
  as a verdict where it is only a comparison.
- **It does not pick a tree.** The bootloader selects one and this step does not establish which.
- **It does not spend a press and it does not authorize one.**

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block issues a
response-demanding command, drives the CMD line, and **times out because the card does not answer** — and
「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage stays withheld.**
