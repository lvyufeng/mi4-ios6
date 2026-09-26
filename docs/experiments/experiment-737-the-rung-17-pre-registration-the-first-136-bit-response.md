# 737: the rung-17 pre-registration — the first 136-bit response, and the rung below it that had to be left out

Host-side. **No device action of any kind** was taken for anything in this document: no `fastboot`, no
`adb`, no `sudo`, nothing written to the device, no press. The arm it describes is **ARMED, NOT
PRESSED**: the last press is still 736's (2026-09-26 11:45:21–11:46:32 UTC, exit 0, 71 s). The set it
names is `armed-storage-cid-dc8d7002` — those eight hex characters are `xnu_arm_entry.bin`'s own sha256
prefix, the naming rule every storage arm in the record follows (`tools/check_set_name_rule.sh` now
enforces it: 26 sets, 25 named by a hash of one of their own members).

## 0. Two spellings of every rung number, and why both are in this file

The ladder in `src/entry/entry_storage.c` counts a rung by the **value** of `STAGE90_XNU_STORAGE_PROBE`
— its clauses read `16 = 15 …`. The commit subjects and the pre-registrations count **ordinal arms**, and
the two part company from value 9 (which has two arms), so **this arm's ladder value is 16 and its
ordinal is rung 17**. The record spells both; this document uses the ordinal in its title and says "the
ladder's value 16" when it means the switch.

## 1. The one finding that decides this arm's shape: 736's cost is *this* rung's constraint

736 pressed rung 15 (`armed-storage-quiet-c9738417`) and it answered its own question — but it paid for
it. Its body writes `INT_ENABLE 0x34` **before** `st_cmd_path` runs, and a write to that register leaves
bit 15 (`SDHCI_INT_ERROR`) set with no write to clear it. `st_cmd_path`'s gate — whose whole job is
*"`INT_ENABLE` must hold nothing but RESPONSE"* — then read `0x00008000` and refused:

    _cmd_int_enable = 0x00008000      set by THAT RUN'S OWN quiet store
    _cmd_gate_kind  = 0x00000002      a bit other than RESPONSE was set
    _cmd_sent       = 0x00000000      nothing went on the bus

**So the first build of this arm would have been dead on arrival.** Rung 15's body is guarded
`>= 15` — cumulative, like every rung below it — so at value 16 it is still compiled in, still called
before `st_cmd_path`, and it still leaves `0x8000` where the gate reads. The command path would have
been refused exactly as it was on 736's press, `c0.sent` and `c1.sent` would both be 0, this rung's own
gate would have refused for reason 1, and **every `_cid_*` cell would have been absent by
construction** while the arm spent a press. That is `[[mi4-silence-is-a-reading-only-if-success-is-silent]]`'s
m720 shape — an absent key one of whose producers is guaranteed — and it is the same class 732's first
build and 736's press both belong to.

**The repair is an exclusion, and it is written in four places rather than in a sentence:**

| place | what it says |
| --- | --- |
| the body's own `#if` | `STAGE90_XNU_STORAGE_PROBE == 15` (was `>= 15`), with the reason above it |
| its call site's `#if` | the same, with a note that what it leaves in `0x34` refuses the gate on every rung above |
| the ladder's `#error` clause | value 16 reads *"15 **with 15's own body left out**"*, and says why |
| `build_entry.sh` | the rung-15 clause is guarded **`-eq 15`** instead of `-ge 15`, and the rung-16 clause **refuses the symbol's presence** and the call site's presence — the check, not the comment |

**And the exclusion is not a loss.** No rung above 15 needs 15's answer: `_quiet_status_after = 0` was
read on 736's press, (A) is refuted, and the reading is in the record.

## 2. What the rung is

**The same two commands, the same three gates, the same census and the same bodies — plus one body of
its own inside `st_cmd_path`, BELOW its between-commands gate**, which opens the same one-bit enable
window rung 14 opens and puts `mmc_all_send_cid`'s CMD2 on the bus under it:

    #if STAGE90_XNU_STORAGE_PROBE >= 16
        if (c1.sent != 0u && (c1.resp & ST_MMC_CARD_BUSY) == 0u) {
            ST_LIVE("xnu_live_storage_cid_gated", 0u);
            st_all_send_cid(int_enable);
        } else {
            ST_LIVE("xnu_live_storage_cid_gated", 1u);
            ST_LIVE("xnu_live_storage_cid_gated_reason", (c1.sent == 0u) ? 1u : 2u);
        }
    #endif

| step | action |
| --- | --- |
| 1 | write `INT_ENABLE 0x34 <- (read \| SDHCI_INT_RESPONSE)` — **the window opens before the command** |
| 2 | read `INT_ENABLE` back → `_cid_ena_held` |
| 3 | read `SIGNAL_ENABLE 0x38` → `_cid_sig_enable` (**read inside the window, never written**) |
| 4 | `st_send_command(2, 0, MMC_RSP_R2, &c2)` — CMD2, the word `0x0209` |
| 5 | publish `c2`'s fourteen cells (`_cid_sent`, `_cid_word`, `_cid_word_read`, `_cid_complete`, `_cid_err`, `_cid_timeout`, `_cid_status_any`, `_cid_any_polls`, `_cid_inhibit_seen`, `_cid_inhibit_last`, `_cid_polls`, `_cid_ticks`, `_cid_clear_after`, `_cid_resp_short`) |
| 6 | read `RESPONSE 0x10`'s four words at `0x1c`, `0x18`, `0x14`, `0x10` → `_cid_raw0`..`_cid_raw3` |
| 7 | publish the driver's own 136-bit form: `(rawN << 8) \| readb(RESPONSE + (3-N)*4 - 1)` → `_cid_resp0`..`_cid_resp2`, and `_cid_resp3 = raw3 << 8` with no byte fill |
| 8 | write `INT_ENABLE <- int_enable` — **the window's ONE exit, unconditional** |
| 9 | read `INT_STATUS` → `_cid_status_post`, read `INT_ENABLE` back → `_cid_readback`, read `PRESENT_STATE` → `_cid_ps_after` |

**Why the window stands before the command is 732's first build and 733's press, not a preference.**
`st_send_command`'s completion poll is *inside* the command; an enable written after it measures a
masked block. And **why the enable has to be standing at all is 733's measurement**: on that press CMD0
ran inside the enabled window and completed (`_cmd0_complete = 1`, `_cmd0_status_any = 1`), while CMD1
ran with the window closed, **was answered by the card** (`_cmd1_resp = 0x40ff8080`, a valid OCR) and
never latched anything (`_cmd1_complete = 0`, `_cmd1_status_any = 0` over 5,088,000 polls). On this
controller a command's status bit is latched only while its enable stands — so a CMD2 sent the way CMD1
was would answer the same way and teach nothing.

**The gate is the driver's own condition and it is read off CMD1's RESPONSE, not off a status bit**:
`c1.sent != 0 && (c1.resp & MMC_CARD_BUSY) == 0`. A status-bit gate would refuse on every arm this
ladder can build, because 733's press measured `_cmd1_complete = 0` with the card answering.
`_cid_gated` is published on **both** paths with `_cid_gated_reason` (1 = CMD1 was never sent, 2 = it was
sent and the card was still busy), so a refusal appears as this rung's own cell with a cause beside it
rather than as a set of keys that are merely absent.

**The 136-bit read is the driver's own, transcribed exactly** (`sdhci.c:1163-1172`, word 3 first):

    resp[i] = readl(RESPONSE + (3-i)*4) << 8;
    if (i != 3) resp[i] |= readb(RESPONSE + (3-i)*4 - 1);

and **both readings are published** — the four raw words beside the four shifted ones — because the
shift is the one thing here that could be wrong in a way no cell would show: a byte read through this
block's register window may or may not do what the driver expects, and `_cid_resp3` is the one word with
no byte read in it.

**The flag decode is the driver's full ladder, and this is the rung that needed it.** Rungs 11–15 sent
two commands, so the decode could be the two arms those commands needed. CMD2 asks for `MMC_RSP_R2`
(`PRESENT|136|CRC`), and `sdhci.c:1131-1143` branches in an order that matters — the 136-bit arm is
tested **before** busy. `ST_MMC_RSP_R2` = `0x07` decodes to `SDHCI_CMD_RESP_LONG 0x01 | SDHCI_CMD_CRC
0x08` = `0x09`, and the word is `0x0209`. **This tree's vendor `sdhci.h` orders those constants the other
way from upstream Linux** (`:52` NONE `0x00`, `:53` LONG `0x01`, `:54` SHORT `0x02`, `:55` SHORT_BUSY
`0x03`); a rung that took the upstream numbers would write a 136-bit command with the SHORT encoding.

## 3. What the arm's own build holds it to

`src/entry/build_entry.sh` gains a clause for `STORAGE_PROBE -ge 16`, `xnu_entry_737`. `st_all_send_cid`
is a `noinline` static, so `nm` places it before `entry_storage_probe` and **neither** the probe's store
census nor the width-vs-offset census reads a line of it: this clause is the only place its widths, its
two stores and its access set are asserted at all. It holds:

- the **symbol** is present and sized;
- its device accesses, sorted, are exactly
  `f9824910:ldr f9824913:ldrb f9824914:ldr f9824917:ldrb f9824918:ldr f982491b:ldrb f982491c:ldr
  f9824924:ldr f9824930:ldr f9824934:ldr f9824934:str f9824938:ldr` — **twelve accesses and no more**;
- its **counts** are exactly `…f9824934:ldr=2 f9824934:str=2…` and `1` for every other address: the
  window opens once and closes once, each read back;
- its **store set** is exactly `f9824934:str` — `SIGNAL_ENABLE 0x38` is never written;
- its accesses **in program order, distinct**, are the store first, then the enable read back, then the
  signal enable, then the four words descending, each followed by its byte, then the restore — the half
  neither the sorted set nor the counts can carry;
- its **image side is EMPTY** (the body's `struct st_cmd_result` and its four locals are `sp`-relative,
  so the classifier does not see them);
- it has **exactly one call site**, on a disassembly line **after `st_cmd_path`'s last
  `bl <st_send_command>`** — the clause that refuses the arm placed above the gate, which is 732's first
  build and 736's press;
- and, in the other direction, **`st_quiet_enable_probe` is NOT in the linked image and is NOT called
  from `entry_storage_probe`** — §1's exclusion, as a refusal rather than a promise.

**Six falsifications, every one measured on the real source and on a real build:**

| # | perturbation | expected | measured |
| --- | --- | --- | --- |
| 1 | rung 15's body put back (`== 15` → `>= 15`) | refuse, naming the symbol | **FAIL**: "`st_quiet_enable_probe` is IN THE LINKED IMAGE while …=16, and rung 16 is the arm that must not carry it" |
| 2 | one of the three byte reads made a 32-bit read | refuse, naming the set | **FAIL**: the set loses `f982491b:ldrb` and `f9824918:ldr`'s count goes to 2 |
| 3 | the CMD2 call site **moved above the between-commands gate** | refuse, naming both lines | **FAIL**: "calls `st_all_send_cid` on disassembly line 322 and its last `st_send_command` on line 336" |
| 4 | the window's restore store deleted | refuse, naming the counts | **FAIL**: "`f9824934:str=1`" |
| 5 | the body renamed | refuse, naming the symbol | **FAIL**: "`st_all_send_cid` is not in the linked image" |
| 6 | the call duplicated | refuse, naming the count | **FAIL**: "makes 2 call(s) to `st_all_send_cid`" |

The clean build passes all of it and prints the twelve accesses, the counts, the store set and the two
call lines. **Nothing is rebuilt by any refusal.**

## 4. The defect this step closed on the way: three words that nothing checked

The build's `st_send_command` clause asserts that body's **accesses** — which registers, how many, in
what order — and **never the value it stores**. So the decode this rung extended was the one thing in
the arm that a wrong edit could move with every set, count and cell still reading the way the record
says: swap the 136-bit and busy arms and the word becomes `0x020b`, the bus carries a command nobody
described, and no check in this repository would notice.

**The decode is now a macro used twice — by the body and by three `_Static_assert`s:**

    ST_SDHCI_CMD_WORD(CMD0, 0)              == 0x0000u     /* opcode 0, no response */
    ST_SDHCI_CMD_WORD(CMD1, MMC_RSP_PRESENT)== 0x0102u     /* opcode 1, RESP_SHORT, no CRC */
    ST_SDHCI_CMD_WORD(CMD2, MMC_RSP_R2)     == 0x0209u     /* opcode 2, RESP_LONG|CRC, no INDEX */

The first two are not decoration: `0x0000` and `0x0102` are the words **four archived press logs** read
back out of the block (`_cmd0_word`, `_cmd1_word`), so an edit that moved one of them would move a
pressed arm's own evidence — and now it fails the build instead. This closes a claim that was in a
comment in the source (*"both are asserted by the build clause"*) and **was false**: the clause asserts
accesses, and the assertion that claim named did not exist. `[[mi4-a-claim-in-a-comment-is-not-a-check]]`.

**The refactor is measured, not assumed**: it moved `st_send_command` from `0x2ec` to `0x314` (+4 bytes)
and moved every symbol after it by the same 4, it **left `_Static_assert` and access sets identical**
(the clause above is green), and **the seam constant did not move** — `bl <__wrap_FlushPoU_Dcache>` is
still at `0x800492d8` returning to `0x800492dc`, read out of the **live** ELF (`_ttbr1`'s neighbours
moved, this did not).

## 5. And a narration defect the 736 step itself introduced, found here

The 736 addendum inside `tools/verify_press_ready.sh`'s rung-14 paragraph was written with
**unescaped backticks inside a double-quoted string**, so five of its key names were executed as command
substitutions and the sentence printed with holes where they belong:

    verify_press_ready.sh: line 829: _cmd_gate_kind: command not found
    verify_press_ready.sh: line 829: _cmd_int_enable: command not found
    verify_press_ready.sh: line 829: _cmd_sent: command not found

That is `[[mi4-measurement-defects]]`'s **m730** exactly — *a backtick inside a double-quoted refusal
message is a command substitution: the refusal fires and its explanation has a hole* — reintroduced by
the very step that wrote the sentence about a refusal. It was invisible for the same reason as the
other narration defects: the row it lives in printed `ok`, because the row asks whether the narration
quotes the right names and the *unescaped* names are the ones that survive. **Fixed by escaping (70
backtick pairs on that line), and the fix is a check rather than a promise**: row 4 of
`tools/verify_press_ready.sh` now scans the lines that BUILD the narration — `rung_para N "…"`,
`entry_arm="$entry_arm …"` and `entry_conseq+="…"` — and refuses any backtick on one of them that is not
preceded by a backslash, naming the line numbers. The scan is restricted to those three line shapes on
purpose: a backtick inside a SINGLE-quoted string (this tool's own extractor patterns) is not a
substitution, and a file-wide scan would be a check with false positives.

**And the falsification measured a property worth naming.** With **one** backtick unescaped the file no
longer parses at all — the substitution swallows to the next backtick in the file and `bash` reports a
syntax error, so that half is loud. With a **pair** unescaped on each of two narration lines (the shape
736's defect actually had — 5 pairs on one line), the file parses and the names are silently executed:
the row then refuses, naming `[786 832]`, and it is the only thing that does. Restored byte-for-byte
afterwards, with the row green again.

## 6. The safety contract

**The write set is two stores, both to `INT_ENABLE 0x34`, both inside `st_cmd_path`'s own body and
BELOW the gate that reads the register.** `SIGNAL_ENABLE 0x38` is **read and never written**, at any
rung, and it is the conjunction of the two enables that raises this block's SPI 123 → intid 155, a line
nobody here owns. The new command is a `bcr` with a 136-bit response and **no data phase**, so the
controller has no transfer to run: **no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word, no
`BLOCK_SIZE`/`BLOCK_COUNT`/`TRANSFER_MODE`/`BUFFER`, and no byte of the medium can change.** The
`stage90-build-config.txt` of this arm is **byte-identical to the pressed arm's** (`6c2b6038…`,
measured), so no payload switch moved either.

**The failure mode is a diagnosis, not a lost device.** The command bound is inside `st_send_command`
(1.2 s, just above the specification's own ~1 s) and the run's ending is unchanged from 730's, 733's and
736's. If the block did raise the line, the delivery reaches the dispatcher as an unknown intid — an
ending this ladder already reads and survives (709 ended that way on intid 170). A `_cid_gated = 1` is a
complete answer rather than a lost press, and it is published with its reason.

## 7. The cell table

| cell | expected | read against |
| --- | --- | --- |
| `_cid_gated` | `0` | the driver's own gate did not refuse |
| `_cid_gated_reason` | *absent* when `_cid_gated = 0`; `1` or `2` when it is 1 | which half of the gate refused |
| `_cid_calls` | `1` | the body ran |
| `_cid_ena_wrote` | `0x00000001` | `read \| RESPONSE`, with the read at 0 |
| `_cid_ena_held` | `0x00008001` | 730's and 733's readbacks: bit 15 rides along with any write to `0x34` |
| `_cid_sig_enable` | `0` | the signal enable, read inside the window and never written |
| `_cid_op` / `_cid_flags` | `2` / `0x00000007` | the opcode and `MMC_RSP_R2`, as passed |
| `_cid_sent` | `1` | the block took the word |
| `_cid_word` / `_cid_word_read` | `0x0209` / `0x0209` | **the driver's word, and the block's own copy of it** |
| `_cid_complete` | `1`, or `0` | **the cell 733 could not get from CMD1** |
| `_cid_status_any` / `_cid_any_polls` | ≠ 0 / a poll index | *when* the block said anything |
| `_cid_inhibit_seen` / `_cid_inhibit_last` | > 0 / any | NEVER STARTED vs RAN vs IN FLIGHT |
| `_cid_polls`, `_cid_ticks` | the bound, or less | the poll's own extent |
| `_cid_err` | `0`, or `0x00010000` | `SDHCI_INT_TIMEOUT` is the **card** not answering; any other bit is the controller |
| `_cid_timeout` | `0` | the bound expiring |
| `_cid_clear_after` | `0` | the write-1-to-clear took |
| `_cid_resp_short` | *any* | `c2.resp`, the 32-bit read taken unconditionally beside the four |
| `_cid_raw0`..`_cid_raw3` | *any* | **the four words, word 3 first — the reading this rung exists for** |
| `_cid_resp0`..`_cid_resp3` | `raw << 8 \| byte`, `resp3 = raw3 << 8` | the driver's own shift, for comparison |
| `_cid_wrote_back` | equals the gate's own read (`0`) | this body's value, not a constant |
| `_cid_status_post` | *any* | `INT_STATUS` after the restore |
| `_cid_readback` | `0x00008000`, or `0` | **the restore is a partial one** (bit 15 stays) |
| `_cid_ps_after` | `0x01f80000` | the controller idle again |
| `_cid_done` | `1` | the body reached its last line |
| `_cmd_gated`, `_cmd0_*`, `_cmd1_*`, `_ena_*`, `_int_status_*` | as 733's press | **the rungs below are inherited unchanged** |
| `_quiet_*` | **ABSENT, and that is this arm's own reading** | 15's body is not in this image |

**A `_cid_complete = 0` with `_cid_resp0` non-zero is a reading and not a failed run**: it says the
response arrived and the status bit did not latch — 726's sixth hypothesis a second time, on a command
whose answer is eight times as wide. **A `_quiet_*` key in this arm's log would mean the log is from an
arm that is not this one.**

## 8. Host-side work, the builds, and what verified it

**Three builds were run and the operator should know it** — the entry image (three times: the first for
the measurement, then after the word-assertion refactor, then once for reproducibility) and the payload
(once). The pressed arm was parked and exported before any of them, and `./scripts/build.sh` does not
reproduce the payload (408), so this is a disclosure and not a routine step.

| reading | value |
| --- | --- |
| the entry image | `dc8d7002…`, **5,552,764 B** — the same size as the pressed arm's |
| `.text` | 5,331,560 → **5,332,264** (+704 = `st_all_send_cid` 668 + `st_send_command` +36 + `st_cmd_path` +104 − `entry_storage_probe` 16 − `st_quiet_enable_probe` 220, the rest alignment) |
| `st_all_send_cid` | `0x8000db38`, size `0x29c` (668 B) |
| its call line | 472, against `st_cmd_path`'s last `bl <st_send_command>` at 315 |
| the seam constant | `bl <__wrap_FlushPoU_Dcache>` at `0x800492d8`, returning to `0x800492dc` — **did not move** (read from the live ELF) |
| the payload config | `6c2b6038…` — **byte-identical to the pressed arm's** |
| reproducibility | three builds of the same switch set gave the same bytes, measured (`cmp`), including after the refactor |

| command | reading |
| --- | --- |
| `tools/verify_press_ready.sh` | **5 of 5, exit 0**; flags `--allow-xnu-entry`, `--expect-arm=armed-storage-cid-dc8d7002` |
| `tools/verify_revert_set.sh out/stage90/frozen/armed-storage-cid-dc8d7002 --set=…` | VERIFIED, 11 files, 6 manifest members |
| `tools/verify_revert_set.sh /mnt/data/mi4-ios6-export/armed-storage-cid-dc8d7002 --set=…` | VERIFIED — **the park is exported** |
| `tools/check_set_name_rule.sh` | exit 0; 26 sets, 25 hash-named |
| `tools/rehearse_revert_set.sh` | **38 ok / 0 failed** |
| `make check` | exit 0 |
| `tools/verify_press_ready.sh` row 3 (the gate) | exit 0 under `--allow-xnu-entry` — **the entry sources match the manifest by content**, so the tree in front of the gate is the tree the image was built from |

The arm is **ARMED, NOT PRESSED**: `out/` holds `armed-storage-cid-dc8d7002` and nothing is owed. **The
press is the operator's**, and it is spent only against a green readiness print whose flags and
`--expect-arm=` come from that print's own header:

    sudo -n adb devices            # 4a2fe00b; the neighbour 33e80afe must be ABSENT
    ./scripts/preflight_boot_check.sh --allow-xnu-entry
    ./scripts/run_and_capture.sh --allow-xnu-entry --expect-arm=armed-storage-cid-dc8d7002

## 9. What the rung is not

It is not a storage driver: `st_cmd_path` is three commands with **no data phase**, and no byte of the
medium can change. It is not a new rung of *act* — the store set is the same two stores rung 14 uses,
moved below the gate, and the new surface is a read. It does not meet the goal: **「把基础驱动跑起来」/
「起码要能进入操作系统」 is still unmet** — no storage, no filesystem, and the OS is not observed to reach
userland — so **TWRP-to-storage stays withheld** (the clause is conditioned on 「如果os已经能进去了的话」).
It is not the last rung: whatever CMD2 answers, the driver's next statements are CMD3
(`mmc_set_relative_addr`), CMD9 (`SEND_CSD`) and CMD7 (`SELECT_CARD`), and then CMD17 at LBA 1 — where
531 §9's self-verifying reading lives (the eight bytes `EFI PART`, or an MBR ending `55 aa`).
