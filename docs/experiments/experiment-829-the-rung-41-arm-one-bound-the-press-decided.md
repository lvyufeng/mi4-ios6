# 829 — the rung-41 arm: one bound, decided by a press

**BUILT AND PARKED, NOT PRESSED, NOT ARMED.** No press has been made; none is owed by this record;
`frozen/` is a RECORD and not a queue. This arm re-runs rung 40's bounded `DATA_AVAILABLE` wait with
its one number raised from 20 ms to the 1.25 s rung 38 was measured to be sufficient behind — moving
**no command, no register, no megabyte, no device and no cell.**

| | |
| --- | --- |
| arm | `armed-storage-af522aca` — named after its own entry bin's sha256 prefix |
| switch | **VALUE 40 = ordinal rung 41**, `STAGE90_XNU_STORAGE_PROBE=40` |
| the act | `ST_EXT_DATA_TICK_BUDGET` 384000 → **24000000u** (1.25 s at 19,200,000 Hz); the second pass over the same 128 words of `BUFFER 0x20`, each gated on `PRESENT_STATE`'s `DATA_AVAILABLE` (`0x800`), is byte-for-byte rung 40's |
| entry bin | 5,569,148 B `af522aca…` |
| entry elf | 6,749,248 B `d339a648…` |
| payload | `stage90-qcdt.img` `bbd0ae4c…`, 8,589,312 B |
| readiness | `tools/verify_press_ready.sh` **5 of 5**, exit 0; `make check` clean |

---

## 1. What the rung-40 press decided, and what this arm does with it

828's press is the whole premise, and it was **decisive**: the mechanism worked and the number was
wrong. Every cell it was built to produce came back with the opposite sign:

- `_ext_data_wait_timeout = 1`, `_ext_data_gated = 0`, `_ext_data_ticks = 0x5ddf7` = **384,503 ticks
  = 20.03 ms** — the gate ran its whole budget and the block was still silent.
- `_ext_complete = 1`, `_ext_resp = 0x00000900` (CMD8's own R1, state 4 = TRAN),
  `_ext_doing_read_seen = 0x80` — the command completed cleanly and the block had entered DOING_READ.
- `_ext_sec_count = 0` — no data reached the buffer inside 20 ms.

**And the same ladder had already read these bytes.** The rung-38 press measured `_ext_words_gated =
0x80`, `_ext_sec_count = 0x01d5a000`, `_ext_rev = 7`, `_ext_structure = 2` — with `_ext_complete = 0`,
because rung 38 had no `SDHCI_INT_RESPONSE` enable, so its command poll waited its **full**
`ST_CMD_DONE_TICK_BUDGET` = 1.2 s before the read loop ran. **Rung 38's own defect was the reason its
read worked.** The card's data DOES arrive; it needs longer than 20 ms; 1.2 s is proven sufficient.

**This rung is the time change that follows from that, and nothing else.** The 20 ms figure came from
a comment's arithmetic ("a 512-byte block takes a little over 10 ms at 400 kHz") that the press
falsified — 20.03 ms of waiting and the block was still silent. `ST_EXT_DATA_TICK_BUDGET` becomes
**24000000u (1.25 s)** — rung 38's own measured-sufficient value, a little above its 1.2 s. **The
second pass's code is rung 40's, unchanged**: it re-reads `PRESENT_STATE` and `BUFFER`, the two
registers rung 38 already reached, plus the `.bss` array. **The first pass is untouched**, so
`_ext_words_gated` stays the first pass's reading and the four `_ext_data_*` cells are the second
pass's.

## 2. What the press would read

The same `_ext_*` cells with `_ext_data_wait_timeout = 0`, `_ext_data_gated` non-zero,
`_ext_words_gated` non-zero, and the card's fields back where rung 38 left them (`_ext_sec_count =
0x01d5a000`, `_ext_rev = 7`, `_ext_structure = 2`) **all beside `_ext_complete = 1`** — the three
readings rung 38 bungled together, at once. `_ext_data_gated = 0x80` moves the frontier to
**CMD16 `SET_BLOCKLEN`**. An `_ext_data_wait_timeout = 1` at 1.25 s would itself be a reading: the
data path would then need a different mechanism and not a longer wait.

## 3. The build clause, and the artifact it re-surfaced

`build_entry.sh`'s `xnu_entry_829` refuses **both directions**: the source must define the bound as
`24000000u` (asserted at the SOURCE, `grep -c 'ST_EXT_DATA_TICK_BUDGET    24000000u'`), and rung 40's
384000 body immediates (`mov #56320`, `movt #5`) must be ABSENT from the body. The assertion is
source-level rather than body-immediate because the first draft's immediates were wrong twice over —
the same [[mi4-a-claim-in-a-comment-is-not-a-check]] discipline, one rung on.

**A first draft added a diagnostic cell (`_ext_data_ps_first`) and REVERTED it.** The extra
instruction perturbed register allocation and re-surfaced 828's **m828** classifier artifact as
`NODECL-f982-0:str` — the `.bss` write resolved through a stale `movt`. The constant-only change
builds clean, which is itself the check that the artifact is allocation-sensitive and not content-
sensitive.

## 4. Where the ladder stands

The eMMC chain is CMD0 → CMD1 → CMD2 → CMD3 → CMD9 → CMD7 → CMD13 → CMD8 → **CMD16**. The card is in
TRAN (rung 37); the 512 bytes arrived (rung 38); the completion witness was restored (rung 39); the
wait that makes both true at once was added (rung 40) and its bound is fixed here (rung 41). The
recommended next rung is **CMD16 `SET_BLOCKLEN`**, then sector reads → filesystem → mount →
TWRP-to-storage.

**THE GOAL IS NOT MET.** No block read, no filesystem, no mount; TWRP-to-storage stays withheld.