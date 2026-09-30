# 826 — the rung-39 arm: one immediate in an OR restores the completion witness rung 38 switched off

**BUILT AND PARKED, NOT PRESSED, NOT ARMED.** No press has been made; none is owed by this record;
`frozen/` is a RECORD and not a queue. This arm carries the same CMD8 the rung-38 press ran, with the
one enable bit that press proved was missing — so unlike rung 38 it is *not* an untested data path:
the 512 bytes have already been seen to arrive.

| | |
| --- | --- |
| arm | `armed-storage-6a2e94ae` — named after its own entry bin's sha256 prefix |
| switch | **VALUE 38 = ordinal rung 39**, `STAGE90_XNU_STORAGE_PROBE=38` |
| the act | `st_send_ext_csd`'s `INT_ENABLE 0x34` window ORs `ST_SDHCI_INT_ENABLE_CMD` and clears the two DMA enables |
| entry bin | 5,569,148 B `6a2e94ae…` — **same size** as the spent rung-38 arm, 565,819 bytes differ |
| entry elf | 6,749,248 B `b667093d…` — **same size** as the spent rung-38 arm (6,749,248 B) |
| payload | `stage90-qcdt.img` `bd5bb0a2…`, 8,589,312 B |
| readiness | `tools/verify_press_ready.sh` **5 of 5**, exit 0; `make check` clean |

---

## 1. What the press taught, and what this arm does with it

825's press is the whole premise. It measured `_ext_complete = 0` / `_ext_timeout = 1` **beside** a
512-byte transfer that plainly arrived (four consistent non-zero card fields in a `.bss`-zeroed buffer)
and CMD8's own R1 in the register (`_ext_resp = 0x00000900`, state 4 = TRAN). The cause was the rung's
own window: **`_ext_ena_wrote = 0x30`** — the vendor's PIO pair `DATA_AVAIL | SPACE_AVAIL` alone, with
`SDHCI_INT_RESPONSE` (bit 0) **clear**. `INT_STATUS`'s latch honours `INT_ENABLE`, so the bit every
`st_send_command` poll waits on (`ST_SDHCI_INT_CMD_MASK`) was forbidden to set, and the poll ran its
full 1.2 s. **The witness was switched off; the command was not slow.** This is
`[[mi4-silence-is-a-reading-only-if-success-is-silent]]` turned on the arm's own instrumentation — the
shape `[[mi4-one-value-two-definitions]]` names as m825.

**The repair is ONE OR.** The rung-39 window is

```c
word = int_enable | ST_SDHCI_INT_PIO_IRQS;
#if STAGE90_XNU_STORAGE_PROBE >= 38
    word |=  (uint32_t)ST_SDHCI_INT_ENABLE_CMD;                 /* 0x000F0001, bit 0 = RESPONSE */
    word &= ~((uint32_t)ST_SDHCI_INT_DMA_END | (uint32_t)ST_SDHCI_INT_ADMA_ERROR);
#endif
```

which is also what the vendor does: `sdhci_set_transfer_irqs` (`sdhci.c:806-815`) clears
`DMA_END | ADMA_ERROR` and sets `DATA_AVAIL | SPACE_AVAIL` for its PIO arm, and `sdhci_init`
(`sdhci.c:291-296`) leaves `SDHCI_INT_RESPONSE` enabled for the host's whole life. Rung 37's restore
of `INT_ENABLE` to 0 is what left rung 38 a zero base to add to.

## 2. The assertion is the final OR's immediate, and it is double-sided

The window word is built in **one register** across interleaved `entry_live_write` cells — the arm's own
`_ext_*` readings sit in the gaps — so gcc **folds the pair of ORs into one `orr`**. Read out of the
two linked images:

| image | the four-instruction window, in order |
| --- | --- |
| `armed-storage-92b6c552` (pressed) | `bic #0x2000000` → `bic #8` → `orr r5, r5, #983040` → **`orr r5, r5, #48`** (= 0x30) → `str r5, [r4, #2356]` |
| **`armed-storage-6a2e94ae`** (this arm) | `bic #0x2000000` → `bic #8` → `orr r5, r5, #983040` → **`orr r5, r5, #49`** (= 0x31) → `str r5, [r4, #2356]` |

**Same function, same store site, the same four-instruction window, one different immediate.** The
build clause `xnu_entry_825` refuses unless the **last `orr` on the register the `str ..., #2356`
(`INT_ENABLE 0x34`) publishes has BIT 0 SET**. The assertion is *measured*, not described:

- it **PASSES** on this image (the clause printed its own line into the build log: `orr r5, r?, #0x31`,
  `BIT 0 SDHCI_INT_RESPONSE is SET [1]`), and
- it **REFUSES** `armed-storage-92b6c552` at the bit-0 test — run this step against the parked ELF:
  the refusal names the pressed arm's final OR `#0x30`, bit 0 clear.

That second direction is what makes the clause more than a claim in a comment
(`[[mi4-a-claim-in-a-comment-is-not-a-check]]`): a build with the defect and the build without it are
told apart by the assertion, and the pressed arm is the counterexample it was proved against.

**A note on the extraction, because it cost two build aborts with no message.** The clause reads the
window with a single `awk` over the full disassembly — **no pipe, no `head`** — because this file runs
under `set -euo pipefail`, and a reader that exits after one line sends the writer SIGPIPE (141), which
pipefail reports as the pipeline's status and `set -e` turns into an abort. The first attempt used
`grep ... | head -1` and a `[ \t]` character class that silently matched nothing against the tab the
`objdump` line actually carries; both were replaced.

## 3. What the arm adds to the image, and what it cannot break

**No new command, no new register, no new megabyte, no new device.** The body is the same
`st_send_ext_csd`, gated `#if STAGE90_XNU_STORAGE_PROBE >= 38`, called once, immediately after rung
37's CMD13, at the same place in the driver's own order. The device surface is the one rung 38
already used, and rung 14's already mapped. The entry `.bin` is the **same size** as the spent arm's
despite the new statements — the rung-37 page move's alignment padding absorbs them — so a size
comparison could not tell the two apart and 565,819 bytes do.

## 4. What the press would read

The same `_ext_*` cells as the rung-38 press, with **one expected change**:

| cell | pressed arm (825) | this arm, if the repair holds |
| --- | --- | --- |
| **`_ext_complete`** | `0` | **`1`** — the witness restored |
| **`_ext_timeout`** | `1` | **`0`** |
| `_ext_ena_wrote` | `0x00000030` | a word **carrying bit 0** |
| `_ext_command_enable` | (absent) | the OR this rung adds, masked to `ST_SDHCI_INT_ENABLE_CMD` |
| `_ext_dma_cleared` | (absent) | `0` — the two DMA enables cleared |
| `_ext_resp` | `0x00000900` | unchanged (TRAN) |
| `_ext_words_read` / `_ext_sec_count` | `0x80` / `0x01d5a000` | unchanged (the 512 bytes, the capacity) |
| `_ext_gated` / `_ext_done` | `0` / `1` | unchanged — a refused phase is `1` / `0` |

**`_ext_complete = 1` is the one cell that answers this arm.** If it reads 1 with the data unchanged,
the rung-38 press is confirmed from its other side and the frontier moves to **CMD16 `SET_BLOCKLEN`**
(`mmc_set_blocklen`, the driver's own next statement before any sector read).

## 5. What this arm does not settle

It asserts the **window word**. It does **not** assert that a press will complete: the enable is now
correct but the block's own completion still has to be *measured*, which is why `_ext_complete` is the
cell and its absence (a refused gate) is `_ext_gated = 1` with `_ext_done = 0`. **No block read has
been made, no filesystem is reached, no mount is made.**

## 6. The goal

**THE GOAL IS NOT MET.** 「起码要能进入操作系统」 remains met and is unchanged. 「把基础驱动跑起来」 now
stands one measurement from a *working* first data phase with its completion made a reading rather than
inferred. But 「让os可以正常启动并且挂载存储」 is not reached and **TWRP-TO-STORAGE STAYS WITHHELD** for
818 §5's reason: the storage it would be written to is the one thing this ladder has not got working
end to end.

**AND NOTHING IS ARMED.** This arm is parked; the press is the operator's.