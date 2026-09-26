# 739: the rung-18 pre-registration — the response register read before any command, and the ladder's own evidence put on trial

Host-side. **No device action of any kind**: no `fastboot`, no `adb`, no `sudo`, no press. The arm it
describes is **ARMED, NOT PRESSED**. The last press is 738's (2026-09-26 14:41:56–14:43:13 UTC, exit 0,
capture `rung17-cid-20260926-144313-last_kmsg.txt`, 628,500 B).

**The build WAS run for this arm** — the entry image twice and the payload once, each with the pressed
arm's own switch set read out of the record, with the two configs compared afterwards. That is a
disclosure and not a routine step (`./scripts/build.sh` does not reproduce, 408).

## 0. Two spellings of every rung number, and why both are in this file

The ladder in `src/entry/entry_storage.c` counts a rung by the **value** of
`STAGE90_XNU_STORAGE_PROBE`, so this arm's clauses read `17 = 16 plus …`. The commit subjects and the
pre-registrations count **ordinal arms**, and the two part company at the ladder's value 9 (which has
two arms), so **this arm's ladder value is 17 and its ordinal is rung 18**. The record spells both.

## 1. What 738 measured, and the doubt it left

738's press took the ladder's **first 136-bit response read** and got

    _cid_resp0 = 0x40ff8080        )  the driver's own read: (raw0 << 8) | byte at +0x0B
    _cid_raw0  = 0x0040ff80        )  the four raw words, word 0 first
    _cid_raw1  = 0x80000000        )
    _cid_raw2  = 0x00000000        )
    _cid_raw3  = 0x00000000        )
    _cid_complete   = 0             over 5,088,256 polls and 23,040,593 ticks = 1.200 s
    _cid_status_any = 0             over the WHOLE register, with the enable standing
    _cid_inhibit_seen = 0           where CMD0's own inhibit was seen 537 times in the same run

**`_cid_resp0` is bit-for-bit `_cmd1_resp` in the same log** — which 733 read from `RESPONSE + 0x10`
(word 0) at CMD1's own moment — and read as one 128-bit register the four raw words are that same
32-bit value **one byte further along**. So the four words are not a CID.

The register did move in that run (after CMD0 `_cmd0_resp = 0x0` with `_cmd0_resp_read = 1`; after CMD1
`0x40ff8080`), so it is not a constant. But **"it changed at some point during CMD1" does not separate
*the card wrote it* from *the block's own default for a command the sequencer never took*** — and since
733 that value has been read as the card's own OCR: 733's index row calls it "exactly the reference
word", 736 §5 built the push plan from it, and **rung 17's own gate** (`c1.sent != 0 && (c1.resp &
MMC_CARD_BUSY) == 0`) is read off it. The value has an OCR's *shape* (bit 30 set, voltage window
`0x00ff8000`), which is why it convinced. **Shape is not provenance.**

**The ladder's push from CMD1 therefore rests on evidence this press put in doubt**, and that is a
cheaper thing to fix than to keep building on.

## 2. What the rung is

**The whole of rung 17, plus one read-only body called at a moment no rung has read this register at.**

    #if STAGE90_XNU_STORAGE_PROBE >= 17
        if (g_storage_mode_complete != 0u)
            st_resp_before();
    #endif

placed in `entry_storage_probe` **immediately before `st_cmd_path()`** and after rung 9's wait — the
position rung 15 used, and for the same reason: the block is in SDHCI mode, powered, clocked and
quiesced (rungs 2–9 measured all three), and **no command has ever been put on its bus in this image's
life**. Anywhere above that point the block is not yet quiesced; anywhere below it a command has run and
the register is answering a command instead of describing a block.

**`RESPONSE 0x10` is read here and nowhere earlier.** The only two bodies in the image that touch it are
`st_send_command` (rung 11's poll) and `st_all_send_cid` (rung 17), both of them **after** a command,
and rung 3's census of ten registers does not read it at all. So this is the register's first reading on
a block that has never carried a command.

| step | reading | cell |
| --- | --- | --- |
| 1 | `RESPONSE + 0x1C`, `+0x18`, `+0x14`, `+0x10` — the driver's own order, word 3 first | `_rb_raw0`..`_rb_raw3` |
| 2 | the same four through `readl(...) << 8 \| readb(... - 1)`, and no byte for `resp[3]` | `_rb_resp0`..`_rb_resp3` |
| 3 | the aggregate of the four raw words | **`_rb_resp_zero` — this rung's answer** |
| 4 | `PRESENT_STATE 0x24` whole, and its `SDHCI_CMD_INHIBIT` bit | `_rb_present`, `_rb_inhibit` |
| 5 | `COMMAND 0x0E` as a **halfword** | `_rb_cmd_word` |
| 6 | `INT_STATUS 0x30`, `INT_ENABLE 0x34`, `SIGNAL_ENABLE 0x38` | `_rb_int_status`, `_rb_int_enable`, `_rb_sig_enable` |

**No store of any width, no new command, no data phase, no `POWER_CONTROL 0x29`, no GCC word, no
`core_mem` word and no byte of the medium.** `SIGNAL_ENABLE` is read and never written, at every rung.

**Step 2 is the same arithmetic rung 17 uses after CMD2, deliberately.** The comparison this rung exists
for is *"is the word the same before and after CMD2"*, and a comparison between two different
derivations would answer a different question — so the pre-command four words and the post-CMD2 four
words are one derivation taken at two times, and a reader compares them **one for one in a single log**.

## 3. The two outcomes, and why a null reading still answers

    _rb_resp_zero = 1  ->  RESPONSE was EMPTY before any command.
                           CMD1's 0x40ff8080 was put there by CMD1; 733's reading survives;
                           the next rung continues the driver from a fact rather than a hope.
    _rb_resp_zero = 0  ->  the register already held a value with no command in the block's past.
                           The four shifted pre-command words then say WHICH value, and if they
                           match the post-CMD2 ones, the ladder's one piece of evidence that a card
                           exists is RETIRED and the push from CMD1 must be re-derived from a signal
                           other than RESPONSE.

738 left the candidate for that other signal and it is **`CMD_INHIBIT`**: `_cmd0_inhibit_seen = 0x219`
says the block *started* CMD0, while `_cmd1_inhibit_seen = 0` and `_cid_inhibit_seen = 0` say it never
showed a start for the two commands whose response words are the ones this ladder has been trusting.
`_rb_inhibit` is that same bit read at the pre-command moment, where it must read zero, and the three
cells together are the discriminator nobody has used as one.

**This is a rung whose null reading still answers**, which is why it is worth a press. Rung 15's quiet
block could only *remove* a mechanism — a zero there refuted (A) without confirming (B) — and that
asymmetry was named in 736 §1. Here either value names the next act: one buys the ladder's evidence
back, the other buys the truth.

## 4. The safety contract, and why this body may sit above the gate where rung 15's could not

**736's press is the measurement of what a store above `st_cmd_path`'s gate costs**: rung 15's write left
bit 15 set in `INT_ENABLE 0x34` (a write to that register sets it and no write clears it), the gate read
`0x00008000`, and **no command went on the bus**. A body that stores nothing cannot move a register the
gate reads.

**And that is a property of the bytes rather than a promise about this source**, because the build clause
`xnu_entry_739` holds the body's store set to EMPTY and prints it. The clause is placed so that the store
assertion is **first** — see §6, where the first draft made it unreachable.

**The failure mode is a diagnosis, not a lost device.** This rung reads only, so it cannot raise the
line; and the run's ending is unchanged from 730/733/736/738 (the kernel's own `No errors detected`,
with `_seam_post_end_ticks` and `_sleh_storm` unmoved). The pre-registered hazard the rung above carries
— a delivery on SPI 123 → intid 155 — did not occur on 733, 736 or 738, and this arm adds no store that
could cause it.

**And rung 15's body is excluded, asserted twice.** `st_quiet_enable_probe` is compiled for the value 15
and no other; the rung-17 clause refuses its presence in the image and at the probe's call sites. **A log
from this arm carrying any `_quiet_*` key is a log from an arm that is not this one.**

## 5. What the arm's own build holds it to

`src/entry/build_entry.sh` gains a clause for `STORAGE_PROBE -ge 17`. For `st_resp_before` it asserts:

- the symbol is present and sized (an unreferenced static is dropped, and a build without it publishes
  every `_rb_*` cell as absent while the record claims the rung — m720's shape);
- its device access **set** is exactly the twelve, sorted:
  `f982490e:ldrh f9824910:ldr f9824913:ldrb f9824914:ldr f9824917:ldrb f9824918:ldr f982491b:ldrb
  f982491c:ldr f9824924:ldr f9824930:ldr f9824934:ldr f9824938:ldr`;
- its **counts** are each exactly 1 — every address once and none of them written;
- its **store set is EMPTY**;
- its device accesses **in program order, distinct** are the four words descending, each followed by the
  byte the driver takes below it, then `PRESENT_STATE`, then `COMMAND` as a halfword, then the three
  interrupt registers — the half neither the set nor the counts can carry, and the one that makes the
  pre-command words comparable with the post-CMD2 ones;
- its **image side is EMPTY**;
- `entry_storage_probe` calls it **exactly once**;
- and **that call is ordered BEFORE `st_cmd_path`'s own call** in the probe's disassembly — the clause
  that refuses the arm placed after the command path, where a non-zero register is a fact about a block
  that has just answered three commands and `_rb_resp_zero` could not mean what its name says.

**Nine falsifications, each refused by its own clause, each nameable:**

| # | perturbation | verdict |
| --- | --- | --- |
| 1 | a byte read made 32-bit (the `0x1b` fill replaced by a word read at `0x18`) | FAIL, the **SET** clause |
| 2 | a store of a read value added to the body | FAIL, the **READ-ONLY** clause |
| 3 | a write-1-to-clear into `INT_STATUS 0x30` | FAIL, the **READ-ONLY** clause |
| 4 | a second read of a declared register (`INT_STATUS` twice) | FAIL, the **COUNTS** clause |
| 5 | a read of an undeclared device offset (`0xFE` as a halfword) | FAIL, the **SET** clause, reporting `NODECL-` |
| 6 | the call site moved **below** `st_cmd_path` | FAIL, the **ORDER** clause, naming both lines |
| 7 | the call site duplicated | FAIL, the **CALL-COUNT** clause |
| 8 | the body renamed | FAIL, the **SYMBOL** clause |
| 9 | the call site removed | the **compiler** refuses first — `-Werror=unused-function` |

**Cell 9 is the honest note rather than a claim that the clause fired.** With the call site gone the body
is an unreferenced static, and `-Werror=unused-function` stops the build before the symbol clause is
reached. The symbol clause remains reachable by a rename (cell 8) and by inlining, so the clause is not
dead — but a reader comparing the table to the mechanism should know which of the two fired.

## 6. Two clauses repaired before they shipped, because the first draft made them unreachable

**The store clause originally sat after the set and count clauses, and could never have fired there.**
Any device store appears in the classified set as a `:str` entry, so the set clause always refuses
first — and the set and count clauses' own refusal texts claimed stores as *their* subject. That is
`[[mi4-a-claim-in-a-comment-is-not-a-check]]` in its cheapest form: **a refusal that describes a
perturbed build it cannot be reached by.** Cell 2 above is what exposed it: the first attempt at that
perturbation was refused by the *set* clause, with a message about a store, from a clause whose subject
is widths and offsets.

The repair has two halves. The store assertion is now **first**, so a perturbed build with a store is
refused by the sentence that explains why a store here is the one thing this rung must not have; and the
set and count clauses' texts now point at it rather than claiming it. Both clauses keep a reachable
subject: the set clause refuses wrong widths and undeclared offsets, the count clause refuses a second
reading of one address.

**This is the same class as 734's own found defect**, one level down: there, four rungs printed a
narration with no reading of their own in it and row 4 said `ok`; here, a refusal would have printed a
sentence about itself that could not be reached.

## 7. The cell table

| cell | expected | read against |
| --- | --- | --- |
| `_rb_calls` | `1` | the body ran |
| `_rb_raw0`..`_rb_raw3` | **a reading** | the four words at `0x1c/0x18/0x14/0x10` |
| `_rb_resp0`..`_rb_resp3` | the same four, shifted | **compare one for one with `_cid_resp0`..`_cid_resp3` in the same log** |
| **`_rb_resp_zero`** | **`1`** or `0` | **this rung's answer** |
| `_rb_present` | `0x01f80000`-shaped, no command in flight | 730 and 733 both read it so |
| `_rb_inhibit` | `0` | CMD_INHIBIT at the pre-command moment |
| `_rb_cmd_word` | `0x0000` | the block's own copy of the last command word; nothing has carried |
| `_rb_int_status` | `0` | the status register before anything is enabled or cleared |
| `_rb_int_enable` | `0x00008000`, or `0` | **bit 15 is a durable side effect of any write to `0x34`**; nothing above the gate writes it, so `0` is expected and `0x8000` would be a fact about the block |
| `_rb_sig_enable` | `0` | never written at any rung |
| `_cmd0_inhibit_seen`, `_cmd1_inhibit_seen`, `_cid_inhibit_seen` | `0x219`, `0`, `0` | **738's discriminator, in this log** |
| `_cmd_gated`, `_cmd1_*`, `_cmd0_complete` | as 738's | the rung below is inherited unchanged |
| `_cid_word`, `_cid_word_read`, `_cid_resp0`, `_cid_complete` | `0x0209`, `0x0209`, `0x40ff8080`, `0` | the rung above is inherited unchanged |
| `_quiet_*` | **ABSENT** | rung 15's body is not in this image |

**A `_rb_resp_zero = 0` is not a failed press.** It is this rung answering, and the answer is that the
ladder's push from CMD1 has to be re-derived from a signal other than `RESPONSE`. **A `_rb_resp_zero = 1`
with a non-zero `_cid_resp0`** is the reading that retires the doubt: the register was empty before the
command and held a word after it.

**And the comparison is available in one place and only one**: `_rb_resp0` beside `_cid_resp0`, same
derivation, same offsets, two times, one log. A reader who takes only the aggregate has the answer; a
reader who takes the pair has the reason.

## 8. Host-side work, and what verified it

| command | reading |
| --- | --- |
| `tools/verify_press_ready.sh` | **5 of 5, exit 0** — flags `--allow-xnu-entry`, `--expect-arm=armed-storage-rb-05645d1a` |
| `tools/rehearse_revert_set.sh` | **38 ok / 0 failed** |
| `tools/verify_revert_set.sh out/stage90/frozen/armed-storage-rb-05645d1a --set=…` | VERIFIED, 11 files |
| the same against `/mnt/data/mi4-ios6-export/armed-storage-rb-05645d1a` | VERIFIED, 11 files — the park is exported |
| `tools/check_set_name_rule.sh` | exit 0 — the suffix is the entry bin's own sha256 prefix |
| `tools/check_arm_variant_pair.sh` | the pair agrees — `STAGE90_XNU_STORAGE_PROBE=17` against `STAGE90_XNU_ENTRY 1` |
| `make check` | exit 0 |
| `./scripts/preflight_boot_check.sh --allow-xnu-entry` | exit 0 — **the tree in front of the gate is the tree the image was built from**, because the entry sources are verified **by content** |
| two clean entry builds of the final source | `05645d1a…` byte-for-byte, `cmp`-measured |
| the payload's config against the pressed arm's | **byte-identical** (`6c2b6038…`, `cmp`-measured) |

**Measurements the record keeps.** Entry image `05645d1a…`, **5,552,764 B — the same size as the pressed
arm's**, so the seam constant did not move: `platform_cache_idle_exit` at `0x800492d4`, its
`bl <__wrap_FlushPoU_Dcache>` at `0x800492d8` returning to `0x800492dc` = `entry_trace.c`'s
`STAGE90_XNU_SEAM_LR`, re-derived read-only from the **fresh** `xnu_arm_entry_seam.dis`. `.text`
5,332,264 → 5,332,776 (+512). `st_resp_before` at `0x8000d7c8`, size `0x17c` (380 B). `st_all_send_cid`
at `0x8000dcb4`, size unchanged `0x29c`. The payload's own switch set is byte-identical to the pressed
arm's, so no payload switch moved and the config comparison the 732 block made owed holds for this arm
too.

## 9. What the rung is not

It is not a storage driver: the command path is still three commands (CMD0, CMD1, CMD2) with no data
phase, and the new body sends nothing at all. It is not a new rung of *act* — the store set of the whole
image does not move, and the new body adds no store anywhere. It does not *by itself* make the card
appear: the CID the push wants is still not in hand, and this rung decides whether the ladder has been
addressing a card at all. It does not meet the goal: **「把基础驱动跑起来」/「起码能进入操作系统」 is still
unmet** — no storage, no filesystem, the OS not observed reaching userland — so **TWRP-to-storage stays
withheld** (the clause is conditioned on 「如果os已经能进去了的话」).

**And it is not the last rung either way.** If `_rb_resp_zero = 1`, the driver's own continuation is
CMD3 `mmc_set_relative_addr`, CMD9 `SEND_CSD`, CMD7 `SELECT_CARD`, and then CMD17 at LBA 1, where 531
§9's self-verifying reading lives (the eight bytes `EFI PART`, or an MBR ending `55 aa`). If it is `0`,
the next rung is a different signal — `CMD_INHIBIT` is the candidate this press already carries three
cells for.

## 10. The arm, and the press

**ARMED, NOT PRESSED.** `out/` holds `armed-storage-rb-05645d1a` and nothing is owed. The two commands
carry the `scripts/` prefix from the repo root, and `33e80afe` must be absent from **both** device lists
before firing:

    sudo -n adb devices
    ./scripts/preflight_boot_check.sh --allow-xnu-entry
    ./scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-rb-05645d1a

**No firer is armed**, and the press path is those two commands run by hand once each — 730 §6's finding,
since the launcher's bytes are kept nowhere.
