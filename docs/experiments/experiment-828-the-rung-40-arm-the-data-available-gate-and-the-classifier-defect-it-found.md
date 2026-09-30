# 828 — the rung-40 arm: the data-available gate, and the classifier defect that had been reading a `.bss` write as a device store

**BUILT AND PARKED, NOT PRESSED, NOT ARMED.** No press has been made; none is owed by this record;
`frozen/` is a RECORD and not a queue. This arm re-runs the same CMD8 the rung-38 and rung-39 presses
ran, with the read moved **inside** the vendor's own `DATA_AVAILABLE` gate — the wait 827 proved was
missing.

| | |
| --- | --- |
| arm | `armed-storage-f34cf0fb` — named after its own entry bin's sha256 prefix |
| switch | **VALUE 39 = ordinal rung 40**, `STAGE90_XNU_STORAGE_PROBE=39` |
| the act | `st_send_ext_csd` gains a **second** pass over the same 128 words of `BUFFER 0x20`, each word gated on `PRESENT_STATE`'s `DATA_AVAILABLE` (`0x800`), bounded by `ST_EXT_DATA_TICK_BUDGET` = 384000 ticks (20 ms at 19.2 MHz) |
| entry bin | 5,569,148 B `f34cf0fb…` — **same size** as the spent rung-39 arm, 578,368 bytes differ |
| entry elf | 6,749,248 B `4e271989…` — **same size** as the spent rung-39 arm |
| payload | `stage90-qcdt.img` `98756a6a…`, 8,589,312 B |
| readiness | `tools/verify_press_ready.sh` **5 of 5**, exit 0; `make check` clean |

---

## 1. What the rung-39 press taught, and what this arm does with it

827's press is the whole premise. Its one-OR repair worked — `_ext_complete = 1`, `_ext_timeout = 0`,
`_ext_status_after = 0x1` = `SDHCI_INT_RESPONSE` — **and the data was gone with it**: `_ext_words_gated`
fell from `0x80` to `0`, `_ext_ps_first` dropped `0x800` (`DATA_AVAILABLE`), and all 128 words read
zero. The mechanism is one wait where there should be two: **`st_send_command`'s poll returns on the
CMD_IRQ; the DATA IRQ comes later.** With `SDHCI_INT_RESPONSE` enabled the poll returns in
microseconds, so the unconditional read loop runs before the card's data arrives. Rung 38's missing
enable made the poll wait its full 1.2 s — **so rung 38's own defect was the only reason its read
worked.**

**The repair is the vendor's shape.** `sdhci_transfer_pio` reads only inside
`while (PRESENT_STATE & SDHCI_DATA_AVAILABLE)`. This arm adds a second pass over the *same 128 words*
in that shape — `PRESENT_STATE 0x24` bit 11 (`0x800`, `ST_SDHCI_DATA_AVAILABLE`) tested before each
`BUFFER 0x20` read — and bounds the whole wait with `ST_EXT_DATA_TICK_BUDGET` = 384000 CNTVCT ticks
(20 ms at the 19,200,000 Hz this device reported) so that a card which never delivers is a **reading**
(`_ext_data_wait_timeout = 1`) and not a hang.

**The first pass is untouched.** `_ext_words_gated` stays the first pass's reading; the four new cells
are the second pass's:

- `_ext_data_waited` — the inner-loop iterations the wait actually took
- `_ext_data_wait_timeout` — 1 if the tick budget expired before a word arrived
- `_ext_data_gated` — how many of the 128 words the wait handed over
- `_ext_data_ticks` — the total ticks the second pass spent

## 2. What the press would read

The same `_ext_*` cells the rung-39 press produced, with one expected change: **`_ext_words_gated`
non-zero AND `_ext_data_gated` non-zero beside `_ext_complete = 1` at the same time** — the goal rung 38
and rung 39 each got exactly half of. `_ext_data_wait_timeout` should be 0; `_ext_data_ticks` should be
the small count the wait actually took; the capacity word `_ext_sec_count` should return to its rung-38
value `0x01d5a000`. An `_ext_data_gated` of `0x80` (128 of 128) says the data path works end to end and
moves the frontier to **CMD16 `SET_BLOCKLEN`**.

**And what it does not settle.** It asserts the wait EXISTS and is BOUNDED. It does not assert that a
press will complete the transfer: a card that delivers no data is `_ext_data_wait_timeout = 1` with
whatever arrived — a reading, not a success.

## 3. The classifier defect this arm found, which is the larger finding

The rung-38 build clause asserted `st_send_ext_csd`'s device surface as an EXACT SET that included
`f9824904:str` — a word store to `BLOCK_SIZE 0x04`, which the clause's own text says would clear the
sibling `BLOCK_COUNT`. It was not a device store. It is the 128-word loop's `str r3, [r2, #4]!` writing
`.bss` (`st_ext_csd`), whose base `r2` is loaded by `ldr r2, [pc, #784]` from the literal pool. The
classifier tracked only the `movt`/`mov` halves, so it resolved that base through a **stale**
`movt rX, #0xf982` left by a different register.

- **value 38's arm** resolved it as `f9824904:str` — the high half `0xf982` from the stale `movt`, and
  the low half `0x4904` coincidentally in the register's low set. **A false PASS by coincidence.**
- **value 39's arm** allocates registers differently, so the same artifact surfaced as
  `NODECL-f982-0:str` — a false refusal.

The two together exposed the class, which is `[[mi4-measurement-defects]]`. **The fix is
classifier-wide** (it is `classify_body`, used by every device clause in the file): a `[pc, #N]` base is
resolved **exactly** out of the literal pool — the one base class whose address the disassembly states
outright — and a register loaded from the pool carries its value forward through `mov` copies and
`add`/`sub`-immediate; any other load clears its destination.

The fix changed the value-38 body's own resolution to the corrected one, so this arm's clause was
reconciled to the fixed classifier. The two bodies — old and new — now classify **identically**: the
second pass moved the allocator, not the surface. Rung 38's image-side EXACT-SET assertion, which
depended on the allocator, is retired here to two sound forms: no foreign symbol, and `st_ext_csd` (the
512-byte buffer the transfer fills) among the accessed image symbols.

**And the classifier fix is itself the proof the arm adds no device access**: it resolved the `.bss`
fills to image-side rather than a fabricated device address, so the whole second pass is `PRESENT_STATE`
and `BUFFER` — the two registers rung 38 already reached — plus the `.bss` array.

## 4. Where the ladder stands

The eMMC chain is CMD0 → CMD1 → CMD2 → CMD3 → CMD9 → CMD7 → CMD13 → CMD8 → **CMD16**. Rung 40 is a
**time change**: no new command, no new register, no new megabyte, no new device. The card is in TRAN
(rung 37); the 512 bytes arrived (rung 38); the completion witness was restored (rung 39); this rung is
the wait that makes both true at once. The recommended next rung is **CMD16 `SET_BLOCKLEN`**, then
sector reads → filesystem → mount → TWRP-to-storage.

**THE GOAL IS NOT MET.** No block read, no filesystem, no mount; TWRP-to-storage stays withheld.