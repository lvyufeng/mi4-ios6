# 761: 759 §5's "only the opcode separates them" does not survive — the class it was drawn from is two of the archive's own four shapes, and the field that does separate them is 746's

**Host-side only. No device action, no press, no gate, no runner, no build, `out/` untouched.** The
measurements are `tools/read_storage_commands.py` over every command-carrying capture, the vendored kernel tree
read at line numbers, and the DT node. **No arm is moved.**

**Why this exists.** 759 §5 (mine, landed yesterday) ends: *"the split is **in the command word** … among all
six words the only field of the `COMMAND` register that separates those two sets is the **opcode**"*. The two
sets are `{0x0000, 0x0300, 0x030A, 0x031A}` and `{0x0102, 0x0209}`. **This project's own tool, run over this
project's own archive, names FOUR shapes over those six words — and the second set contains one word of each
of two different shapes.** A class whose two members a published cell separates is not a class, and the field
claim drawn from it is not supported. §2–§4 are that; §5 is the other landed claim this step read back, which
**passed**.

## 1. The tool already computes the classification, and it has four names, not two

`tools/read_storage_commands.py` prints, for every command body, a shape "from the measured columns only" and
deliberately prints **no verdict** for an ambiguous row. Run over **every** archived capture — 39 in
`out/stage90/captures/` — the 9 that carry a command body produce exactly four shapes, reproducibly:

| shape | words | on which captures |
| --- | --- | --- |
| `TAKEN -> COMPLETED` — the inhibit bit CLEAR at the last sample and the completion latched | `0x0000` (CMD0), `0x0300` (rung 20) | rung 15–21 for `0x0000` and rung 20–21 for `0x0300`; **rung 13–14 read the same `0x0000` as `TAKEN -> RELEASED, NO COMPLETION`** — the pre-740 shape of that row, where the completion was latched into a register the arm had masked |
| `LINE MOVED INSIDE THE WINDOW` — the CMD line is LOW in at least one reading | `0x0102` (CMD1) | rung 15, 17, 18, 19, 20, 21 |
| `LINE NEVER MOVED` — the CMD line is HIGH at every published moment and the word was in the register | `0x0209` (CMD2) | rung 17, 18, 19, 20, 21 |
| `TAKEN -> STUCK AT THE BOUND` — the inhibit bit was SET at the last sample | `0x031A` (rung 19), `0x030A` (rung 21) | their own captures and later |

**The tool says of `0x0102` word-for-word**: *"the CMD line is LOW in at least one reading; 749 records that
the archive holds TWO candidate readings of this shape and that they disagree."* It does not say *never
started*, because the cell 759 used as the criterion does not support that word.

## 2. The two rows 759 merged, and the one cell that separates them

749 §3 is the whole argument and it is already landed: CMD1 and CMD2 are the like-for-like pair — both
`inhibit_seen = 0`, neither ever latched `INT_STATUS`, both ran the full 1.2 s bound and therefore took the
**same** 1024-sample window at the same position (~484 µs after their own store) with the same sampler.

| | CMD1 `0x0102` | CMD2 `0x0209` |
| --- | --- | --- |
| `inhibit_seen`, first 1024 samples | 0 | 0 |
| `inhibit_last` | **`0x00f80000`** | **`0x01f80000`** |
| **bit 24, the CMD line** | **LOW** | **HIGH** |
| `RESPONSE` in the window | `0x00000000` → **`0x40ff8080`** | `0x40ff8080` → **unmoved** |

**One bit apart in `inhibit_last`, and a whole register apart in `RESPONSE`.** 749's conclusion is that
`inhibit_seen = 0` is *a key with two producers and one name*, and that CMD1's outcome is **unresolved** —
the archive holds two candidate readings and they disagree. 759 §5 took that same key as its criterion and
read it as one producer.

## 3. The opcode claim fails under either reading of "started"

759 §5 never defines its criterion, and that is the defect: *started* is a word, and the archive supports two
different meanings of it, and **neither yields an opcode split**.

- **If "started" means `CMD_INHIBIT` rose** (the reading the tool's shapes use), the class `{0x0102, 0x0209}`
  is *two different shapes*: `LINE MOVED INSIDE THE WINDOW` and `LINE NEVER MOVED`. **The class is a merged
  label, and its two members are separated by a published cell** — so the split it produces is a split of the
  label, not of the hardware.
- **If "started" means the block did something on the bus** (the line moved, or the response register gained a
  word), then `0x0102` belongs with the started rows — 749 §3 measured the line moving and `RESPONSE` gaining
  `0x40ff8080` — and the not-started class is **`{0x0209}` alone**. **A class of one row separates nothing:
  every field of the `COMMAND` register "separates" a set of one.**

**And the merge is not a harmless bookkeeping choice — it changed the answer.** Under the split that *is*
supported by a measured shape (the two `TAKEN -> COMPLETED` words against the four that did not complete),
the fields of the `COMMAND` register sort like this:

| field | `0x0000` `0x0300` | `0x0102` `0x0209` `0x030A` `0x031A` | separates? |
| --- | --- | --- | --- |
| **bit 1, `RESP_PRESENT`** | **0, 0** | **1, 1, 1, 1** | **yes** |
| bit 0, `RESP_TYPE_SELECT` low | 0, 0 | 0, 1, 0, 0 | no |
| bit 3, `CRC_CHECK` | 0, 0 | 0, 1, 1, 1 | no |
| bit 4, `CMD_INDEX` | 0, 0 | 0, 0, 1, 1 | no |
| bits 7:6, `CMD_TYPE` | 00, 00 | 00, 00, 00, 00 | separates nothing |
| bits 13:8, opcode | 0, 3 | 1, 2, 3, 3 | **no** — `3` is in both |

**The unique separating field is the response demand, which is 746's headline.** 759 §5 listed
`RESP_TYPE_SELECT`, `CRC_CHECK`, `CMD_INDEX`, `CMD_TYPE` and the argument, found each failing, and **omitted
`RESP_PRESENT`** — because under its merged split, `0x0102` and `0x030A` are one class and both carry
`RESP_TYPE = 10`. **It dismissed the variable that tracks the outcome by using the label that had already
merged two shapes.**

## 4. What survives, stated as narrowly as it was measured

- **746's headline survives and is strengthened.** *The response demand is the stall* is not one of several
  candidate stories: **bit 1 of the `COMMAND` register is the only bit of the word that separates the two
  words that completed from the four that did not**, over all six words this ladder has ever put on this bus.
  759 §5 offered the opcode as a rival to it; that rival is removed.
- **758's refutation is untouched.** `0x030A` and `0x031A` are one bit apart (`INDEX`) and agree in every
  stopping cell. Nothing here weakens that.
- **What 759 §5 got right and keeps**: the enable window is not the discriminator; `st_send_command` has no
  per-opcode branch; order alone does not explain it; the absences are readings because the instrument ran.
  Those four are independent of the split and none of them is affected.
- **What is left is a shape set, not a field.** Six words, four behaviours: two completed, one moved the line
  and gained a response word, one left the bus untouched, two were taken and never finished. **`RESP_PRESENT`
  tracks completion; it does not yet explain the other three shapes**, and no document here claims it does.

## 5. The other landed claim this step read back: 756's gap list, enumerated rather than cited

756 §1 named `CORE_DLL_CONFIG 0x100` and `HOST_CONTROL2 0x3E` as the two registers the vendor's bring-up
programs at the ladder's clock and the ladder never touches. **This step tested that by enumerating every
register the vendor's power-up path writes, one by one, and putting the ladder's coverage beside each.** It
passed — and the census is stronger than the claim:

| vendor write on the power-up path, at the ladder's clock | where | ladder |
| --- | --- | --- |
| `CORE_VENDOR_SPEC 0x10C` ← MCLK select `DFLT`, `HC_SELECT_IN` cleared (`sdhci-msm.c:2496-2513`, the **else** arm of `curr_ios.timing == MMC_TIMING_MMC_HS400`) | `set_clock` | **implemented** — rung 6's own record says *two `CORE_VENDOR_SPEC 0x10C` read-modify-writes (MCLK select ← DFLT, `HC_SELECT_IN` cleared)* |
| `CORE_VENDOR_SPEC 0x10C` ← `CLK_PWRSAVE` (`:2415-2440`), and the standard `CLOCK_CONTROL` halfwords + stability poll | `set_clock` | **implemented** — rung 6 |
| `CORE_DLL_CONFIG 0x100` ← `\|CORE_DLL_RST` (`:2578-2580`), then `\|CORE_DLL_PDN` (`:2583-2585`) | `set_uhs_signaling`, inside `if (host->clock <= CORE_FREQ_100MHZ)` (`:2567`, and `CORE_FREQ_100MHZ` is `100*1000*1000` at `:164`) | **NOT touched** — and the census lands it: `grep` finds no `CORE_DLL_CONFIG` in `src/entry/entry_storage.c` |
| `HOST_CONTROL2 0x3E` ← `ctrl_2` with `UHS` mask cleared (`:2597`) | `set_uhs_signaling` | **NOT touched** |

**And the reachability question, which 756 asserted and this step checked.** `sdhci.c:1784-1785` calls
`set_uhs_signaling` inside `if (host->version >= SDHCI_SPEC_300) {` (`:1729`) with **no timing test** — so it
runs on every `sdhci_set_ios`, including the power-up one, where `ios->timing` is `MMC_TIMING_LEGACY` and
`host->clock` is 0 or 400 kHz — **both `<= 100 MHz`, so the DLL write is unconditional on our path.**

**`CORE_DLL_CONFIG` has five writers in the vendor tree, and exactly one of them is reachable before the
first command** — which is what makes the gap unique rather than merely present:

| writer | line | reachable before a command? |
| --- | --- | --- |
| `msm_config_cm_dll_phase` | `:392-423` | no — tuning |
| `msm_init_cm_dll` | `:571-687`, and it **enables** the DLL (`\|CORE_DLL_EN` `:622`, `\|CORE_CK_OUT_EN` `:626`) | no — `sdhci_msm_execute_tuning` (`:792`) only |
| `sdhci_msm_toggle_cdr` | `:2216`, `:2220` | no — registered as `.toggle_cdr` (`:2657`) and called only from `sdhci.c:1007-1011`, the **data** path |
| **`sdhci_msm_set_uhs_signaling`** | **`:2578-2585`** | **yes — every `set_ios`, and it DISABLES the DLL** |

**So the candidate set for "a write the vendor makes that the ladder does not" is exactly two writes, and both
of them are writes that turn something OFF** (the DLL, and the UHS-mode override that `HC_SELECT_IN` would
otherwise win over). **That is a negative result and it is the useful kind**: it says the search space for
"something the vendor disabled and the ladder left on" is closed at two, so the next arm's two reads are
sufficient to settle it — or to move the question somewhere else entirely.

## 6. And the controller's identity, settled from the DT rather than from the comment

`entry_storage.c:604` states that *"the DT describes a soldered eMMC (`qcom,bus-width = <8>`)"* — and since
`xnu_live_storage_reg_card_present` reads **`0x00000000`** in every capture (bit 16 of `PRESENT_STATE` clear),
whether that comment is right decides whether "no card" is a verdict about the bus or an artifact of broken
detection. **Checked, and it is right:**

    msm8974.dtsi:24     sdhc1 = &sdhc_1; /* SDC1 eMMC slot */
    msm8974.dtsi:500    sdhc_1: sdhci@f9824900 { qcom,bus-width = <8>; ... }
    msm8974pro.dtsi:1765  &sdhc_1 { reg = <0xf9824900 0x1a0>, <0xf9824000 0x800>; }

**`0xf9824900` is `ST_HC_MEM_BASE`**, the node is the eMMC slot by the vendor's own alias comment, and it is
8-bit wide — so `CARD_PRESENT` clear is the quirk `sdhci-msm.c:2896` names
(`SDHCI_QUIRK_BROKEN_CARD_DETECTION`), not an empty socket. **The ladder is on the right controller**, and the
alternative reading — that the whole `0x030A`/`0x031A` investigation has been probing a slot with nothing in
it — is refuted. The Pro override `0x1a0` also matches the window 756 and 767 used.

## 7. What this document does not say

- **It does not re-open 749, 758 or 746.** It reads 759 §5 against 749 §3's table and the tool that 749 was
  built from.
- **It does not name a mechanism for the stall.** §3 says `RESP_PRESENT` is the only separating field of the
  word, which is a statement about six rows, not a cause. **No rung is designed from it.**
- **It does not edit the documents it corrects.** 749 stays as it is; **759 §5 gains a marked correction
  note** pointing here, and its own text is left standing, which is this project's rule about superseding.
- **It does not arm, build, or press anything.** `out/` still holds `armed-storage-46fe6737`
  (`STAGE90_XNU_STORAGE_PROBE=20`, entry ordinal rung 21), the arm 758's press spent; no firer is armed; any
  further press is the operator's decision.
- **It does not advance 「挂载存储」.** No storage, no filesystem, no mount. What it buys is that **the next
  reader does not spend a press on a question whose premise was a merged label.**

## 8. New instance — **m771: a class built by merging two rows that a published cell separates**

759 §5 grouped `{0x0102, 0x0209}` under the word *never started* and then read the group as one behaviour; the
archive separates its two members by one bit of `inhibit_last` and by the whole `RESPONSE` register, and this
project's own tool gives them **two different shape names**. The group was not a measurement, it was a
spelling — and the field claim drawn from it is the field claim that had to be withdrawn.

**Shape to suspect first: a set defined by a WORD rather than by a CELL.** *Started* is not a column of any
capture; `inhibit_seen`, `inhibit_after`, `bit 24` and `RESPONSE` are, and they disagree. **The test is the
one 749 wrote for this exact key — ask how many producers a value has before letting it name a class** — and
the second test is cheaper still: **if a class has two members, print the cell that would separate them and
look at it.** Related: `mi4-one-value-two-definitions` (one value, two readings), `mi4-silence-is-a-reading-only-if-success-is-silent` (`inhibit_seen = 0` has three producers, m720), and m737 (a claim about a
register that was really about a time).

**The goal is still NOT met.** 「起码要能进入操作系统，把基础驱动跑起来」 is partially met (user mode and the
`memdev.c` fixture, per 745); the SDHCI storage driver does not exist — the block starts a response-demanding
command and never completes one — and 「让os可以正常启动并且挂载存储」 is not reached, so **TWRP-to-storage
stays withheld.**
