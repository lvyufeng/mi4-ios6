/*
 * 692: the storage line's first on-device act - one section, one clock gate read before it, and six
 * register loads behind that gate.
 *
 * **Why this is the next step and not a new line.** 529, 530, 531 and 532 are all host-side readings of
 * this same controller: 529 showed that this tree has no HFS to mount with, 530 showed what a block
 * device must register, 531 read the vendor driver's programming surface - two windows, named
 * `hc_mem` at `0xf9824900` and `core_mem` at `0xf9824000`, and the mode bit in `CORE_HC_MODE 0x78` that
 * has to say SDHCI before a single standard register means anything - and 532 read the prerequisite
 * under all of it: neither window is in any table this image builds, and **both are inside the same
 * 1 MB section**, `0xf9824000 >> 20 == 0xf9824900 >> 20 == 0xF98` (3992). So 532 section 6's step 2 is
 * one `entry_mmio_section(0xf9824000, 0xf9824000, ...)` call and not two, and 532 section 6's step 3 is
 * three words read out of `core_mem` before anything is written to it. This probe is those two steps.
 *
 * **It reads, and it does not write - and that is the arm's design rather than a first cut.** The
 * vendor's own probe (`sdhci-msm.c:2841-2868`) opens with `writel_relaxed(0, core_mem + CORE_HC_MODE)`,
 * then sets `CORE_SW_RST` (bit 7 of `CORE_POWER`, offset 0), polls it clear, then sets `HC_MODE_EN`, and
 * 531 section 8 adds the third one from the other side: `POWER_CONTROL 0x29` is avoided by the driver
 * itself because on this SoC **writing 0 to it *is* a bus-off request**. Every one of those is a write
 * that changes the state of the medium's controller, and none of them is needed to answer the question
 * this arm asks, which is whether the block is there, whether it is clocked and what it already says.
 * `xnu_live_storage_writes` is published as `0` for exactly this reason: the arm declares the count of
 * writes it made to the controller, so "nothing was written" is a reading in the log rather than a
 * property a reader has to take from this comment.
 *
 * **The gate, why it comes first, and 692's measurement of what it costs.** A load from a block whose
 * clock is off is not a fault on this SoC's fabrics - it is a busy-wait that nothing ends, which is
 * the one failure this project cannot read a log out of (there is no return, so `ram_console` is never
 * recovered). 531 section 8 read SDCC1's two gates out of the vendor clock driver as `clk_ops_branch`
 * clocks, i.e. `BIT(0)` into their own CBCR, and `SDCC1_BCR 0x04C0` (`clock-8974.c:244`) gives the pair
 * at `0xFC4004C0` (BCR) and `0xFC4004C4` (CBCR) - the `+4` being the project's own measurement rather
 * than the file's, since 528 section 8 read `BLSP1_UART2_BCR 0x0700`'s gate at `0xFC400704`. So the
 * gate read is taken **before** the storage block is installed: a gate of 0 skips the six loads and
 * publishes that it did, which turns a probable press-losing hang into a reading. The interlock can
 * only ever fire in the safe direction - if the offset is wrong the word reads as something else and
 * the arm proceeds exactly as it would have without it.
 *
 * **And 692 pressed that, and the gate read is what died.** The published claim was that `0xFC4` is
 * already mapped "proved by `entry_epilogue`'s own PS_HOLD store at `0xfc4ab000` on every returning
 * run" - a *store inside a code path* read as evidence that an *address is mapped*. It is false, and
 * the arm's own log is the disproof: `xnu_live_sleh_far_frame = 0xfc4004c0`,
 * `fsr_frame = 0x00000005` (a section translation fault, on a read), `pc = 0x8000d0c4` =
 * `entry_storage_probe+0xcc` = `ldr r1, [r3, #1216]` with `r3 = 0xfc400000`, and the panic's own
 * `r0 = 0x80487fc8` is the pointer to the string `xnu_live_storage_bcr`. **So this arm's first act is
 * one more `entry_mmio_section`, for the GATE's megabyte, and the storage block's install follows it
 * only once the gate has said the branch is on.** The two installs cannot collide: `0xFC4` and `0xF98`
 * are different L1 indices in the same table, so 532 section 3.2's occupied-slot refusal cannot fire.
 *
 * **And that megabyte is the reset path's as well.** `entry_epilogue`'s two stores are at `0x0fa0065c`
 * and `0xfc4ab000` - **two different unmapped megabytes** - and 684 measured the first faulting on
 * 678's arm while 692's own ninth abort shows the payload's deliberate ending faulting at `0x0fa0065c`
 * too. `0xfc4ab000` shares this arm's `0xFC4` index, so `xnu_live_storage_gcc_map = 1` beside a
 * readable `_bcr` is also the measurement that the PS_HOLD half of the reset path becomes reachable -
 * the repair 684 named, taken here as a side effect of the gate's own mapping and not as a second
 * experiment.
 *
 * **The four refusals are the mapper's, and they are not one reading.** `entry_mmio_section` returns 0
 * for `g_live_state != 1` (no live channel), for a table outside the kernel's window, for an index past
 * the table, and for an **occupied** slot - the last being a skip and not a clobber, which is the one
 * 532 section 3.2 says a two-call arm trips. **This arm makes two calls, at two different indices, so
 * that refusal cannot be how the second one ends** - and each call publishes its own four numbers
 * (`xnu_live_storage_gcc_map`/`_slot_before`/`_desc` for the GATE's megabyte,
 * `xnu_live_storage_map`/`_slot_before`/`_desc` for the storage block's, plus `_l1`/`_l1_moved` read at
 * the second), because 532 section 6's step 2 is that the arm must be able to tell them apart. And when
 * an install refuses, this probe reads **no** register through it - the GIC probe's own rule, and the
 * reason is the same: a load through a translation this arm cannot vouch for is a fault, not a
 * measurement. 692 measured that such a fault does come back with a log; the rule is about not
 * spending the arm's reading on an address whose descriptor this run did not write.
 *
 * **696: and then the block answered, and the answer made the next act a WRITE.** 694 pressed the
 * read-only probe and all twenty-eight keys came back, none through a fault: both windows are reachable
 * through a descriptor this arm writes, the branch clock is enabled (`_cbcr = 0x00004ff1`), the block
 * answers (`_mci_version = 0x10000011`), and **`HC_MODE_EN` is CLEAR** (`_hc_mode = 0x00002000`). So
 * 531 section 6's "prerequisite or re-do" is answered in the direction that costs a write, and this
 * file gains the vendor's own bring-up - four stores, one bounded poll, and a readback after each store
 * - as **rung 2** of the switch above it. It is the first act in this project that writes to a device
 * block rather than reading one, and everything about its shape is a consequence of that:
 *
 *   * the **gate now guards stores**, where it was written to guard loads, and a load from an unclocked
 *     block is still the failure no bound can end (see the poll's comment);
 *   * the sequence is **pre-registered** as a write, with its cells and its failure path, in
 *     `docs/experiments/experiment-696-…`;
 *   * the count of stores is **published** (`xnu_live_storage_writes`) and it is a number about this
 *     arm's behaviour rather than about what the file contains - `0` on every path that refuses;
 *   * and `POWER_CONTROL 0x29` is still not touched, because writing 0 to it *is* a bus-off request.
 */
#include <stdint.h>

#include "entry_storage.h"
#include "entry_timebase.h"   /* 696: stage90_cntvct_read, for the reset poll's time bound */
/* 710: `entry_irq_register_client` and `entry_irq_enable_line`. The header and not a second
 * `extern` here, because a signature spelled in two files is two definitions with nothing
 * comparing them - which is what `entry_gic.h` says about itself where it declares them. */
#include "entry_gic.h"

/*
 * **696: the switch is a rung, and the `#error` says so where a `-D` would otherwise reach the
 * preprocessor as a value nobody defined.** `STORAGE_PROBE` was a flag - 0 the object links and
 * compiles to nothing, 1 the read-only probe - and the mode sequence is a *different kind of act*
 * (four stores to the controller rather than loads from it), so it gets its own value and the three
 * values are one ladder: how far up the storage line this image goes. The record is the only place a
 * reader learns which rung was built, which is why a value above the ladder is refused here rather
 * than shaping an image whose switches claim something else.
 *
 * **698: the fourth value, and it is a return to the read-only kind.** Rung 2 is the first arm in this
 * project that writes to a device block; rung 3 adds **the standard register file's census** and no
 * store at all, because the next act on this path - the driver's own `sdhci_reset(SDHCI_RESET_ALL)` -
 * changes four of the registers this census reads, and a before-value can only be taken before. The
 * ladder is therefore not "how much does this arm do" but "how far up the line", and a rung that adds
 * reads after a rung that added writes is the shape the ladder was built to allow.
 */
#ifndef STAGE90_XNU_STORAGE_PROBE
#define STAGE90_XNU_STORAGE_PROBE 0
#endif
/*
 * **812: the ladder's bound, raised one rung - and this line's own shape is a measured defect that
 * is NOT repaired here, recorded instead so the next reader does not rediscover it.** The `#error`
 * below is 49,503 characters on one line and only the first 11 KB of it sits inside string
 * literals: the LAST literal closes at the words "an image whose switches claim something else.",
 * and every rung-24-onward clause after it is BARE PREPROCESSING TOKENS - which is why the
 * rungs-34 text appended below is written back inside a literal, in the shape the rungs 0..23
 * clauses use, rather than as more bare prose. The bare tail is left alone because escaping 13.6 KB
 * of prose is a different change from the one this rung makes; it is latent rather than live,
 * because `build_entry.sh` refuses a value above this bound before the compiler ever sees the
 * line. **The one thing it costs is that the message has never been lexed**: a rung that trips this
 * guard would get the intended text AND whatever the stray apostrophes lex to. Naming it is the
 * whole of the repair here, and it is the same discipline as the rest of this file - a claim about
 * text that is never compiled is a claim nobody has tested.
 */
#if STAGE90_XNU_STORAGE_PROBE < 0 || STAGE90_XNU_STORAGE_PROBE > 39
#error "STAGE90_XNU_STORAGE_PROBE is a rung: 0 = inert, 1 = the read-only probe, 2 = the probe and the vendor's mode sequence (four stores to the controller), 3 = 2 plus the standard register file's census (ten reads, no store), 4 = 3 plus the driver's own SDHCI_RESET_ALL (ONE byte store to SOFTWARE_RESET 0x2F, plus a bounded poll of that same byte) - the rung that writes through hc_mem for the first time - 5 = 4 plus the CLOCK SURFACE read at its own widths (the GCC's four SDCC1 branches and the apps root's five RCG words, plus CORE_VENDOR_SPEC 0x10C), a rung of reads that stores NOTHING anywhere, and 6 = 5 plus THE DRIVER'S FIRST CLOCK SET (sdhci_msm_set_clock at 400 kHz): four CBCR read-modify-writes of BIT(0) on the GCC each followed by a bounded halt check, two CORE_VENDOR_SPEC 0x10C read-modify-writes (MCLK select <- DFLT, HC_SELECT_IN cleared), and the standard's two CLOCK_CONTROL halfwords with the stability poll between them - SIX stores and NO rate, because sup_clock == msm_host->clk_rate on the first call (experiment-706 section 2) - and it writes neither BCR 0x04C0 nor any RCG word, nor POWER_CONTROL 0x29, and 7 = 6 plus THE DRIVER'S OWN FIRST POWER BYTE (mmc_power_up's PASS A: sdhi_set_power at sdhci.c:1663 - ONE 8-bit store to POWER_CONTROL 0x29, the value derived from the CAPABILITIES register the way sdhci_add_host and mmc_power_up derive it), with the vendor's REQ_BUS_ON wait NOT taken (sdhci-msm.c:2179-2209 would wait_for_completion on an IRQ this image cannot deliver) - a rung of one store and five readings, and 8 = 7 plus THE DRIVER'S OWN POWER IRQ (the vendor's sdhci_msm_pwr_irq, sdhci-msm.c:1990-2099, as a CLIENT of this image's own dispatcher: intid 170 - SPI 138, the `pwr_irq` msm8974.dtsi:502 declares - registered and its line enabled BEFORE the power byte, with the three arms that may sleep absent because the vendor's own IRQF_ONESHOT+NULL-primary declares a threaded handler and this image has no thread to sleep in), clearing the latch the byte latched and answering the controller - a rung of three stores, in core_mem and in hc_mem, and the two addresses of VENDOR_SPEC 0x10C read side by side, and 9 = 8 plus THE DRIVER'S OWN COMPLETION (sdhci_set_power's own next statement, sdhci.c:1371-1372: check_power_status(host, REQ_BUS_ON), whose sdhci_msm_check_power_status at sdhci-msm.c:2179 compares the request against the TWO DRIVER-SIDE FIELDS the handler's tail writes - curr_pwr_state/curr_io_level, :2092-2096 - and then blocks; rung 9 ports the predicate and replaces the block with a BOUNDED tick poll whose end condition is the CONTROLLER's own CORE_PWRCTL_CTL bit BUS_SUCCESS, because the probe runs with SCTLR.C clear while the handler may run with the caches on, so an image-side flag written by one can be invisible to the other - the flag is still written and published beside the device ack, and the pair is this rung's new cell), with the budget STAGE90_XNU_PWR_WAIT_TICKS and the three sleeping arms still absent, and 10 = 9 plus THE SAME WAIT TAKEN WITH THE MASK OFF: rung 10 measured that the completion arrives within 20 ms of the byte and that the handler is delivered the moment the payload's own idle-exit code lifts `I` (experiment-717), so the mask - not the device and not any budget - is what the poll was measuring; this rung saves the CPSR, clears `I`, runs the SAME bounded poll, restores the saved value and publishes both the CPSR the poll ran under (`_wait_cpsr`, which must now read `0x80000013`) and the state it leaves behind (`_wait_cpsr_after`) - ONE new cell, NO new device access and NO new store, and the first interrupt this image takes inside the cache-off idle-exit window and the first time the handler's two driver-side fields are read back after the handler wrote them. and 11 = 10 plus THE DRIVER'S OWN FIRST COMMAND (sdhci_send_command, sdhci.c:1076-1155: the bounded wait for SDHCI_CMD_INHIBIT to clear, then ARGUMENT 0x08 and COMMAND 0x0E, with the completion taken as a BOUNDED poll of the CONTROLLER's own SDHCI_INT_RESPONSE bit because this image enables no SDHCI interrupt - the block's hc_irq is SPI 123 -> intid 155, a line nobody here owns, and a delivery would end the run at the dispatcher) for the driver's own first two commands, mmc_go_idle's CMD0 (opcode 0, argument 0, no response: the word 0x0000) and mmc_attach_mmc's CMD1 (opcode 1, argument 0, MMC_RSP_R3: the word 0x0102), with the response read out of RESPONSE 0x10 - the first act of this line that addresses the CARD rather than the controller, the first 32-bit write-1-to-clear to INT_STATUS 0x30, and NO data-path register, NO POWER_CONTROL, NO GCC word, NO core_mem word, and no byte of the medium, and 12 = 11 plus THE REGISTER STATE AT THE INSTANT OF THE COMMAND AND THE COMMAND'S OWN RETURN PATH (experiment-724): a new READ-ONLY census body re-takes - at the command's own moment rather than at rungs 3/4/5/6/11's - the POWER_CONTROL 0x29 byte (723 section 5 corrects 723 section 3: rung 7 writes it and it takes, `_pwr_before 0x00 -> _pwr_after 0x0b`, and what differs from the driver's 0x0F is the VOLTAGE field), CLOCK_CONTROL 0x2C's three bits, PRESENT_STATE 0x24, INT_ENABLE 0x34 and SIGNAL_ENABLE 0x38, and the GCC's SDCC1_APPS_RCG (CMD_RCGR decoded into root_en/root_status/update and CFG_RCGR into src/div/mnd_mode - rung 5's own 0x00000507 says SRC_SEL 5, DIV 7, root_status clear, so the root is ENABLED and the vendor's own pre-divider makes the SDCC1 apps clock 200 MHz, not the 384 MHz ST_SET_MAX_CLK names, and the arm's divider of 480 delivers 208 kHz rather than 400), SDCC1 apps/AHB CBCR and the BCR - and refuses the command if the bus-power bit is clear, which is the one condition rung 11's gate does not check; and the command body gains `_cmdN_word_read` (COMMAND 0x0E read back - the block's own copy of the word, which an absence of interrupt cannot supply), `_cmdN_inhibit_after`/`_inhibit_seen`/`_inhibit_last` (PRESENT_STATE's CMD_INHIBIT sampled immediately after the store and over the first 1024 poll iterations, which is what separates NEVER STARTED from RAN from IN FLIGHT), `_cmdN_status_any`/`_any_polls` (the FIRST non-zero INT_STATUS OF ANY KIND, beside rung 11's narrower poll, so that 'nothing latched' becomes 'exactly this bit latched' if the block said something other than RESPONSE), `_cmdN_resp_read` (1, the companion that makes a zero RESPONSE a reading rather than a silence) and an UNCONDITIONAL RESPONSE 0x10 read - so rung 12 makes NO store anywhere, moves no gate, and every clause of rung 11 stands over it unchanged, and 13 = 12 plus WHERE THIS CONTROLLER REPORTS A COMPLETION (experiment-729): a new body of its own - the first device store this ladder declares OUTSIDE `st_send_command`'s window since rung 7, and the first store to an interrupt-enable register anywhere in this image - which reads the registers a completion could be hiding in (`SLOT_INT_STATUS 0xFC` read as a halfword, `CORE_PWRCTL_STATUS 0xDC`, `COMMAND 0x0E` read back as a halfword, `PRESENT_STATE 0x24`), then writes ONE bit of `INT_ENABLE 0x34` (`SDHCI_INT_RESPONSE`, `0x00000001`) with `SIGNAL_ENABLE 0x38` left at ZERO, reads `INT_STATUS 0x30` immediately, reads `INT_ENABLE` back (the block's own copy of the enable, without which a zero status has two producers and a hardware answer cannot be told from a store that never took), writes `INT_ENABLE` back to zero and reads THAT back, and makes NO new command, no store to `POWER_CONTROL 0x29`, no GCC word, no core_mem write and no byte of the medium; and it runs at the one point in `st_cmd_path` this machine has ever reached, immediately after CMD0's publishes and BEFORE the gate, because rung 13's own log says `_cmd_gated = 1` and a block placed after the gate would sit on a path no press has taken. And 14 = 13 plus THE COMMAND PATH WITH THE ENABLE SET (experiment-732): the same two commands, the same gates and the same bodies, with **ONE 32-bit store to `INT_ENABLE 0x34` (`SDHCI_INT_RESPONSE`) taken immediately before CMD0 is put on the bus, and its restore taken on ONE unconditional line immediately after CMD0's publishes** - `SIGNAL_ENABLE 0x38` still ZERO, which is the one thing 730's press measured is safe, and the enable is therefore open for exactly CMD0's own send and its poll. The reason is 730's own answer: `_int_status_after = 0x00000001` against `_int_status_before = 0x00000000` with exactly one store between them, so **`_cmd_gated = 1` was the MASKED POLL's answer and not the block's** - rungs 11, 12 and 13 read a status register whose enable nothing in this image had ever set while a command was in flight. **This rung's FIRST BUILD put the same store after CMD0 instead of before it, and it was refuted without spending a press**: `st_send_command`'s completion poll is inside CMD0, so a window opened below the command leaves `c0.complete == 0` and the between-commands gate refuses - the arm's own promised cell (`_cmd_gated = 0`) was absent by construction, which is m720's shape, an absent key with one of its producers guaranteed. The window now has one entrance and one exit, both unconditional, and every branch of this body (the census's refusal and the register half of the gate above it, the between-commands gate below it) stands outside them. Gate 1 is NARROWED rather than removed: `SIGNAL_ENABLE` must still read zero and `INT_ENABLE`'s only permitted bit is `SDHCI_INT_RESPONSE`, both checked BEFORE the store, and the refusal publishes which of the two fired (`_cmd_gate_kind`: 1 = `SIGNAL_ENABLE` non-zero, 2 = a bit of `INT_ENABLE` other than `RESPONSE`), so a refusal is localised rather than reported as a boolean. The restore publishes its own sequence number, the value written back, the block's readback and `INT_STATUS` read AFTER the disable - and the readback is the cell that says the restore is a PARTIAL one: 730's press measured its own pair as `_int_enable_held = 0x00008001` for a store of `0x00000001` and `_int_enable_readback = 0x00008000` for a store of `0x00000000`, so bit 15 (`SDHCI_INT_ERROR`) is set by a write to 0x34 and is not cleared by one. Three reads 697 and 730 left owed come with it: `HOST_VERSION 0xFE` as a halfword, `INT_STATUS` with the enable set and BEFORE any command - where 0 says the completion bit is latched by the command rather than held by the block - and `INT_STATUS` after the restore. **The cell that says the rung worked is `_cmd_gated = 0` with `_cmd1_*` keys present, and the cell that says the enable was the mask is `_cmd0_complete = 1` beside `_cmd0_status_any != 0`** - the first command this line drives to a completion and the first CMD1 it ever issues. The window is bounded by the two stores and no new write class appears: no new command, no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word and no byte of the medium. **Two spellings of this rung's number are in use and this clause states both**: this ladder counts a rung by the VALUE of `STAGE90_XNU_STORAGE_PROBE`, so the clause above reads 14, while the commit subjects and the pre-registration count ORDINAL ARMS and call it **rung 15** - the two differ by one from value 9 onwards because value 9 has two arms (the second is the same wait with the mask off), and a reader holding both numbers holds one arm. And 15 = 14 plus THE QUIET-BLOCK READ (experiment-734): **the same ONE-bit store to `INT_ENABLE 0x34`, taken for the first time in this ladder at a point where NO COMMAND HAS EVER BEEN SENT** - a body of its own, `st_quiet_enable_probe`, called immediately BEFORE `st_cmd_path()` and after rung 9's wait, on a block that is already in SDHCI mode, powered and clocked, so the only thing that differs from rung 13's and rung 14's store is the absence of any command in the block's past. It reads `INT_STATUS 0x30`, `INT_ENABLE 0x34` and `SIGNAL_ENABLE 0x38` first and publishes all three, writes `INT_ENABLE <- (read | SDHCI_INT_RESPONSE)` and publishes the value it wrote, reads `INT_STATUS` IMMEDIATELY after that store (`_quiet_status_after`, which is this rung's answer), reads the enable back (`_quiet_held`), writes the enable back to its before-value and publishes that, then reads `INT_STATUS` again (`_quiet_status_post`) and the enable again (`_quiet_readback`). `SIGNAL_ENABLE` is READ and NEVER WRITTEN, at any rung. **The reason is 733's own failed prediction and it is a discrimination, not a new act**: 733 pressed rung 14 and measured the pair `_int_status_before = 0x00000000` -> `_int_status_after = 0x00000001` across exactly one store to `0x34`, with `_ena_status_post = 0` and `_int_status_before = 0` both reading the register CLEAR - the same 0 -> 1 transition that 730 read as *the enable revealed a completion*, on a register that had just been read as clear twice. So that pair does not separate (A) a write to 0x34 that makes 0x30's RESPONSE bit read 1 from (B) a completion the block was holding and re-latched when the enable returned - 726's sixth hypothesis, which 733 did not put down. **THE QUIET BLOCK IS WHAT SEPARATES THEM, because (B) requires a completion to have happened**: (B) predicts `_quiet_status_after = 0` here (no command has ever been issued, so there is nothing to re-latch) and (A) predicts `1`. Nothing else on this arm is new: two stores, both to `0x34`, the second restoring the first, on a block that has never carried a command, with no new command, no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word and no byte of the medium. And 16 = 15 *WITH 15'S OWN BODY LEFT OUT* plus THE FIRST 136-BIT RESPONSE (experiment-737): `st_quiet_enable_probe` is compiled for the VALUE 15 and for NO OTHER, because 736's press measured what its store costs the rung above it - a write to `INT_ENABLE 0x34` leaves bit 15 (`SDHCI_INT_ERROR`) set and no write clears it, so `st_cmd_path`'s own gate read `0x00008000` on that arm and REFUSED THE WHOLE COMMAND PATH (`_cmd_gate_kind = 2`, `_cmd_sent = 0`: no command went on the bus and every `_cmd*` key inside the command path was absent). A rung that needs the command path to run cannot carry a write to `0x34` above the gate, and no rung after 15 needs 15's answer, which is in the record. So this arm is the ladder THROUGH 15 WITHOUT 15's body: after rung 14's two commands, and gated on the DRIVER'S OWN condition rather than on a status bit, `st_all_send_cid` opens the SAME one-bit enable window (`INT_ENABLE 0x34 <- read | SDHCI_INT_RESPONSE`, `SIGNAL_ENABLE 0x38` still never written), issues `mmc_all_send_cid`'s CMD2 (opcode 2, argument 0, `MMC_RSP_R2` = PRESENT|136|CRC, which decodes to `SDHCI_CMD_RESP_LONG 0x01 | SDHCI_CMD_CRC 0x08` and the word 0x0209), and reads the 136-bit response THE DRIVER'S OWN WAY - `resp[i] = readl(RESPONSE + (3-i)*4) << 8 | readb(RESPONSE + (3-i)*4 - 1)`, the shift sdhci.c:1164-1172's comment calls *CRC is stripped*, published both as the four shifted words and as the four raw words so a reader can see whether the shift changed anything - then closes the window on ONE unconditional line and publishes its restore. **The enable has to be standing for CMD2, and that is 733's measurement rather than a preference**: on rung 14's press CMD0 ran inside the enabled window and completed (`_cmd0_complete = 1`, `_cmd0_status_any = 1`) while CMD1 ran with the enable closed, was ANSWERED BY THE CARD (`_cmd1_resp = 0x40ff8080`, a valid OCR) and never latched a completion at all (`_cmd1_complete = 0`, `_cmd1_status_any = 0` over 5,088,000 polls, `_cmd1_timeout = 1`) - so on this controller a command's status bit is latched only while its enable stands, and a CMD2 sent the way CMD1 was would answer the same way and teach nothing. **The gate is the driver's own and it is read off CMD1's RESPONSE and not off a status bit** (`c1.sent != 0 && (c1.resp & MMC_CARD_BUSY) == 0`), because a status-bit gate would refuse on every arm of this ladder. It transcribes two constants this tree's vendor header orders the other way from upstream Linux (`sdhci.h:53` has `RESP_LONG 0x01` and `:54` `RESP_SHORT 0x02`), and it adds NO new command class beyond CMD2, no data path, no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word and no byte of the medium. And 17 = 16 plus THE RESPONSE REGISTER READ BEFORE ANY COMMAND (experiment-739): a body of its own, `st_resp_before`, which is **READ-ONLY - it stores NOTHING anywhere**, the class of rung 3's census, rung 5's clock census and rung 12's register census - and which runs at the ONE position this ladder has never read this register at: immediately BEFORE `st_cmd_path()`, after rung 9's wait, where the block is in SDHCI mode, powered, clocked and quiesced AND NO COMMAND HAS EVER BEEN PUT ON ITS BUS IN THIS IMAGE'S LIFE. It reads `RESPONSE 0x10`'s four words at their own offsets (+0x1C, +0x18, +0x14, +0x10), the three one-byte fills the driver's 136-bit branch takes BELOW them (+0x1B, +0x17, +0x13, and none for `resp[3]`), and publishes both the four raw words and the four shifted ones - so the driver's own `resp[i] = readl(RESPONSE + (3-i)*4) << 8 | readb(RESPONSE + (3-i)*4 - 1)` is available at the pre-command moment and is directly comparable, in ONE log and through ONE derivation, with the four shifted words rung 17's `st_all_send_cid` publishes after CMD2. It also reads `PRESENT_STATE 0x24` and publishes its `SDHCI_CMD_INHIBIT` bit (the before-value for 738's discriminator: CMD0's own inhibit was seen 537 times while CMD1's and CMD2's were never seen at all), `COMMAND 0x0E` as a halfword (the block's own copy of the last command word, which is 0 if no command has ever carried), and `INT_STATUS 0x30`, `INT_ENABLE 0x34` and `SIGNAL_ENABLE 0x38` - and `SIGNAL_ENABLE` is READ AND NEVER WRITTEN at any rung. **The arm's answer is `_rb_resp_zero`**, the aggregate of the four raw words: `1` says the register was EMPTY before any command, so CMD1's `0x40ff8080` was put there by CMD1 and 733's reading survives; `0` says the register already held a value with no command in the block's past, and then the four shifted pre-command words say WHICH value - and if they match the post-CMD2 ones, then **the ladder's one piece of evidence that a card exists is retired** and the push from CMD1 must be re-derived from a signal other than RESPONSE. **The reason is 738's own press and it is a doubt, not a new act**: the first 136-bit read this ladder ever made returned `_cid_resp0 = 0x40ff8080`, BIT-FOR-BIT `_cmd1_resp` in the same log, with the four raw words holding that same 32-bit value ONE BYTE FURTHER ALONG - so the four words are not a CID, and since 733 that value has been read as the card's own OCR by 733's index row, by 736 section 5's push plan and by **rung 17's own gate** (`c1.sent != 0 && (c1.resp & MMC_CARD_BUSY) == 0`). The gate's LOGIC still holds - a status-bit gate refuses on every arm of this ladder - but the EVIDENCE it consumes is weaker than every rung since 733 has treated it as: `0x40ff8080` has an OCR's SHAPE, which is why it convinced, and shape is not provenance. Rung 17's whole command path stands over this rung unchanged (CMD0, CMD1, CMD2 with the enable standing for CMD2, `SIGNAL_ENABLE` still zero), and rung 15's body is still compiled for the value 15 and NO other, because a store above `st_cmd_path`'s gate refuses the whole command path (736's measurement). **This rung adds NO new command class, no data path, and NO STORE ANYWHERE**: no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word, no `INT_STATUS` write and no byte of the medium. And 18 = 17 plus THE DRIVER'S OWN NEXT COMMAND, CMD3 `SET_RELATIVE_ADDR` (experiment-741): `mmc.c:1409`'s `mmc_set_relative_addr(card)`, the statement immediately after `mmc_all_send_cid` in `mmc_attach_mmc`, transcribed from `mmc_ops.c:194-210` - opcode `MMC_SET_RELATIVE_ADDR` 3 (`mmc.h:32`, `ac [31:16] RCA R1`), argument `card->rca << 16` with `card->rca = 1` assigned eight lines earlier at `mmc.c:1400`, so the argument is **0x00010000 - THE FIRST NON-ZERO ARGUMENT THIS LADDER HAS EVER PUT ON THE BUS** - and flags `MMC_RSP_R1 | MMC_CMD_AC` = `PRESENT|CRC|OPCODE` (`core.h:51`, with `MMC_CMD_AC` = 0 at `core.h:35`), which decodes under THIS TREE'S VENDOR ORDERING to `RESP_SHORT 0x02 | CRC 0x08 | INDEX 0x10` and the word **0x031A** - the ladder's FIRST `INDEX` BIT, and it is R1's own property rather than decoration: an R1 response carries the opcode back, which is why `MMC_RSP_OPCODE` is in the flags and why this is the first word here that is not `opcode << 8 | small_flags`. It is a body of its own, `st_set_relative_addr`, called from `st_cmd_path` after rung 17's CMD2 publishes and entered only when CMD2 was SENT - the value rung 17's body now RETURNS, so the gate and the cell `_cid_sent` in the same log are one reading with two consumers rather than two readings of one thing. It opens the SAME one-bit enable window rung 17 opens around CMD2 (`INT_ENABLE 0x34 <- read | SDHCI_INT_RESPONSE`, because 733's press measured that a command's status bit is latched only while its enable stands: CMD0 completed inside rung 14's window and CMD1, sent with the window closed, was answered and never latched), and closes it on ONE unconditional line immediately after the publishes, with `SIGNAL_ENABLE 0x38` still READ AND NEVER WRITTEN at any rung. **AND ITS OWN NEW CELL IS A PAIR THAT RUNG 18'S METHOD MAKES POSSIBLE**: `RESPONSE 0x10` is read through the DRIVER'S OWN WORD-0 DERIVATION - `(readl(RESPONSE + 0x1C) << 8) | readb(RESPONSE + 0x1B)`, the same arithmetic rung 17 uses after CMD2 and rung 18 uses before any command - immediately BEFORE the window opens and again immediately AFTER the command's publishes, so `_rca_resp_pre` and `_rca_resp_post` are ONE DERIVATION TAKEN AT TWO TIMES and `_rca_resp_moved` is this rung's answer: **1 says CMD3 MOVED the register, 0 says it did not.** `_rca_resp_pre` is directly comparable, in one log and through one derivation, with `_cid_resp0` (rung 17, after CMD2) and with `_rb_resp0` (rung 18, before any command) - and `_rca_resp_is_arg`, 1 iff the word after CMD3 equals the ARGUMENT 0x00010000, names THE ALTERNATIVE PRODUCER explicitly, so a register that echoes what was written to `ARGUMENT 0x08` is a reading this rung publishes rather than one a reader has to think of. **The shape of the expected answer is why this rung is worth a press**: CMD1's answer `0x40ff8080` has an OCR's shape and an R1 CARD STATUS does not (its bits 12:9 are `CURRENT_STATE`, 8 is `READY_FOR_DATA` and 22 is `ILLEGAL_COMMAND`), so a word that comes back with those fields is a card's and a word that comes back as `0x40ff8080` again is the register not answering CMD3 at all - and the three decoded fields are published beside the raw word as `_rca_state` `(resp >> 9) & 0xF`, `_rca_ready` `(resp >> 8) & 1` and `_rca_illegal` `(resp >> 22) & 1`, with the raw word `_rca_resp` beside them so no reader has to take a decode on trust. **THE INHIBIT TRIPLET IS PUBLISHED FOR CMD3 AS IT IS FOR CMD0, CMD1 AND CMD2** (`_rca_inhibit_after`, `_rca_inhibit_seen`, `_rca_inhibit_last`, the same three cells rung 12 added and rungs 17 and 18 read), so ONE log holds four commands' own inhibit readings side by side - which is 740 section 6's next question made answerable rather than asked: CMD0's `_cmd0_inhibit_seen` was 0x219 and CMD1's and CMD2's were both 0, and a CMD3 whose inhibit is seen beside a CMD3 whose register moved is a pair of readings that separates "the sequencer took it" from "the register file answered". **AND IT ADDS NO NEW REGISTER CLASS**: the same `INT_ENABLE 0x34` window rung 17 opens and restores, `RESPONSE 0x10` read through the words at 0x1C and 0x18 and the byte at 0x1B, `SIGNAL_ENABLE 0x38` read and NEVER written, no data-path register, no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word and no byte of the medium - and the four commands are the driver's own CMD0, CMD1, CMD2 and CMD3 in `mmc_attach_mmc`'s order. And 20 = 19 plus THE SAME COMMAND WITH ITS RESPONSE DEMAND KEPT AND ITS INDEX BIT TAKEN OUT (experiment-746): rung 19 put `0x031A` on the bus and this record's own re-reading of its three logs has since found that the block took it and never finished it, that rung 20's `0x0300` was taken and COMPLETED in 0.255 ms, and that **CMD1 (`0x0102`) and CMD2 (`0x0209`) were NEVER STARTED AT ALL** - `inhibit_seen = 0` over 5,088,256 and 5,088,000 samples while their words were read back out of `COMMAND 0x0E`, identically in all three of the last three presses. Five words are now on the record - `0x0000` started and completed, `0x0300` started and completed, `0x031A` started and stuck, `0x0102` and `0x0209` never started - and the ONE rule that fits all five is that **a word that asks for a response and carries no `INDEX` bit is DECLINED**; `0x0300` is the only word here that asks for nothing back. So this rung sends the SAME opcode and the SAME argument through the SAME one-bit window with `MMC_RSP_R1` minus `MMC_RSP_OPCODE` - `ST_MMC_RSP_R1_NOIDX` = `PRESENT|CRC`, giving the word **`0x030A`**, one bit from rung 19's `0x031A` in one direction and one bit from rung 20's `0x0300` in the other. **Its three outcomes separate the two surviving hypotheses**: NEVER STARTED confirms the rule and explains CMD1 and CMD2 in the same stroke; STARTED AND NEVER FINISHED kills the rule's INDEX half and puts the stall back on the response itself; STARTED AND COMPLETED kills the rule outright. It also repairs m756: the freshness pair is taken through `readl(RESPONSE 0x10)` - the very expression `st_send_command` reads for its own `resp` cell - so the three moments are ONE arithmetic at last instead of two, and the four raw words at the two moments the body owns are published beside it. And 19 = 18 plus THE SAME COMMAND WITH ITS RESPONSE DEMAND REMOVED (experiment-743): the ladder's first ONE-VARIABLE rung since it began addressing the card - `st_cmd3_noresp`, a body of its own that is `st_set_relative_addr` with ONE constant changed, the same opcode `MMC_SET_RELATIVE_ADDR` 3 and the same argument `0x00010000` sent through the same one-bit `INT_ENABLE 0x34` window with the same restore, and flags `MMC_RSP_NONE` (`core.h:50`, the value 0) in place of `MMC_RSP_R1` - so the command word changes from **0x031A** to **0x0300** and the `INDEX` bit goes with the response, because a card that is not asked to answer cannot echo an opcode back. **THE REASON IS 742'S ANSWER AND IT IS A CONTRAST THE LADDER ALREADY HOLDS**: rung 19 put CMD3 on the bus with R1 and measured it TAKEN (word and argument read back out of `COMMAND 0x0e` and `ARGUMENT 0x08`), STARTED (`CMD_INHIBIT` set on the read after the store and still set 1.2 s later, all 1024 samples), and NEVER FINISHED (`_rca_complete = 0`, `_rca_err = 0`, `_rca_status_any = 0` over 5,088,256 samples) with the response registers EMPTIED (`_rca_resp_pre = 0x40ff8080` -> `_rca_resp_post = 0x00000000`) - while the ONE command this ladder has ever driven to a completion is CMD0, whose flags are zero and which asks for no response at all, and it is also the only command whose inhibit had cleared by the last sample. The variable to move is therefore the response demand and nothing else, and every other cell this rung publishes is rung 19's. **THE THREE OUTCOMES IT CAN DISTINGUISH AND THE NEXT ACT EACH ONE NAMES**: `_nrsp_complete = 1` with `_nrsp_status_any != 0` says the response DEMAND is what stalled CMD3, and the subject becomes the response path (`RESPONSE 0x10`, `SDHCI_INT_RESPONSE` and the `INT_ENABLE` bit this ladder has carried since rung 13); `_nrsp_inhibit_seen > 0` with `_nrsp_complete = 0` says a no-response command stalls the same way, so the demand is exonerated and the subject is the opcode or the block's own state, and the census moves to `SLOT_INT_STATUS 0xFC` and `CORE_PWRCTL_STATUS 0xDC` - the two registers 726 section 3 named and no rung has yet read at a moment when there was a completion to report; `_nrsp_inhibit_seen = 0` with `_nrsp_word_read = 0x0300` says the block never started it at all, which is 724 section 2's hypothesis 1 arriving on a command whose flag word cannot be blamed on a malformed response request. **THE RESPONSE REGISTER IS READ THREE TIMES AND THAT IS THIS RUNG'S SECOND READING**: with the demand removed nothing should clear `RESPONSE 0x10` during the command, so `_nrsp_resp_post` should still hold whatever CMD2 left there (`0x40ff8080`, CMD1's word, which rungs 17 and 18 both measured) - and if the R1 command emptied the register while the no-response command does not, the CLEARING is tied to the response demand and 742 section 1's reading is confirmed rather than inferred. The three readings are one arithmetic at three times, the driver's own `(readl(RESPONSE + 0x1C) << 8) | readb(RESPONSE + 0x1B)`: immediately BEFORE the window opens, inside the command object's own `resp` (read unconditionally since rung 12, with `resp_read` beside it so a zero is a reading rather than a silence), and again immediately AFTER the publishes. **AND THE R1 DECODE IS DELIBERATELY ABSENT**: rung 19 published `_rca_state`, `_rca_ready` and `_rca_illegal` because an R1 CARD STATUS has those fields - and this command asks for no status, so the register holds whatever the last response-expecting command left there, and decoding THAT word as this command's answer is 738's trap (a stale word read as a fresh one) in its cheapest form; the three fields are not published and this sentence is why. **NO NEW REGISTER CLASS AND THE SAME TWO STORES, BOTH TO `INT_ENABLE 0x34`** - the window before the command and its restore on ONE unconditional line after the publishes, writing back the value the gate read (`SIGNAL_ENABLE 0x38` READ AND NEVER WRITTEN at every rung, because its conjunction with `INT_ENABLE` is what raises this block's SPI 123 -> intid 155, a line nobody here owns); no data-path register, no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word and no byte of the medium. Rung 19's own body `st_set_relative_addr` is compiled for the VALUE 18 and for NO other, the way rung 16 left rung 15's out, because a value-19 build that carried both would put TWO response-demand variants of one command on the same bus in one boot and neither reading would be attributable to its own flag word. **21 = 20 plus THE DLL AND HOST_CONTROL2 CENSUS** (756 section 6, the two-read discriminator that document pre-registered): THREE reads and NO store at any path - HOST_CONTROL2 0x3E as a HALFWORD (`ST_SDHCI_HOST_CONTROL2`, sdhci.h:79, the register `sdhci_msm_set_uhs_signaling` writes unconditionally at sdhci-msm.c:2597 and the only thing in this image that could have cleared its UHS field is rung 4's RESET_ALL), CORE_DLL_CONFIG 0x100 and CORE_DLL_STATUS 0x108 as WORDS (`sdhci-msm.c:80`, `:89`) - **and the window is `hc_mem`, not `core_mem`**, because the vendor reaches them as `host->ioaddr + CORE_DLL_CONFIG` and `host->ioaddr` is `sdhci_pltfm_init`'s first memory resource (`msm8974.dtsi:500`, reg-names "hc_mem","core_mem"); the DT settles it, since `msm8974pro.dtsi:1765` widens `&sdhc_1` from the base node's 0x11c to 0x1a0 and 0x100/0x108 are beyond 0x11c - the window was widened to cover exactly this pair. The rung publishes the whole word of each beside the named bits (`_dll_rst` bit 30, `_dll_pdn` bit 29, `_dll_en` bit 16, `_dll_ckout` bit 18, `_dll_lock` bit 7 of STATUS, `_dll_ctrl2_uhs` bits[2:0]), because 756 section 6 has three outcomes per register and each names the next act - bits 30 AND 29 both set kills the DLL hypothesis and nothing further is owed; either bit clear makes doing what the vendor does the cheapest untried thing on this path; `_dll_ctrl2_uhs = 0` closes the HOST_CONTROL2 half in one cell. **An image at this rung sends the rung-21 command as well**, because rung 21's call site is guarded `>= 20` and this rung does not change a guard that is already pressed - so the log carries the DLL cells taken BEFORE the first command (the position rung 18 uses) beside a second sample of the 0x030A stall. **No new store class and no new window**: all three addresses are in the megabyte 0xf98 this image already maps and reads (`0xf982493E`, `0xf9824A00`, `0xf9824A08`, all >= 0xf9824900 and < 0xf9824A1C, i.e. inside the 0x1a0 window the Pro override declares), so 692's interlock is satisfied before the arm is written, and 22 = 21 with THE WINDOW'S ENABLE SET REPLACED BY THE VENDOR'S OWN - one constant, the value written to INT_ENABLE 0x34 at the same call site and in the same position, so that the command, the argument, the flag word and the place in the driver's order are all rung 21's and the only variable between the two arms is WHICH STATUS BITS ARE ENABLED. rung 21's window is `int_enable | SDHCI_INT_RESPONSE` (one bit, sdhci.h:121) and rung 23's is `int_enable | SDHCI_INT_ENABLE_CMD` = `0x000F0001` - **FIVE bits: the completion plus the four command-level error bits the driver's own R1 path can read** (TIMEOUT `0x00010000`, CRC `0x00020000`, END_BIT `0x00040000`, INDEX `0x00080000`), which is rung 21's bit with the block's whole command-error half beside it. **765 section 4 pre-registered exactly these five and the exclusions are the point**: the vendor's own set is eleven bits (`0x01FF0003`, transcribed from sdhci.c:290-296) and FIVE of the other six are the DATA_* errors, which are about a transfer this rung never starts, while the sixth is AUTO_CMD_ERR `0x01000000`, a failure of the block's own auto-command mechanism rather than a fact about the card - so an arm that enabled all eleven would widen the answer space with bits that cannot distinguish the four pre-registered outcomes. What the five buy is that every `_status_any = 0` this ladder has ever read is a statement about a MASK as much as about the block (730 measured on this very block that a status bit is latched only while its enable stands), and a single press now separates *the card did not answer* (`TIMEOUT`) from *the card answered wrongly* (`CRC`/`INDEX`/`END_BIT`) from *nothing happened*. SIGNAL_ENABLE 0x38 is still NEVER WRITTEN, and rungs 17/21/22 have already measured what that buys: INT_ENABLE written repeatedly with SIGNAL_ENABLE at zero and no delivery ever taken. The arm ALSO READS TWO REGISTERS NO RUNG HAS READ: TIMEOUT_CONTROL 0x2E (a byte, which the vendor writes only for a command with data or MMC_RSP_BUSY at sdhci.c:827-828, so a non-zero value on a data-less command would be a finding) and INT_STATUS 0x30 immediately AFTER the enable write and BEFORE the command, which is the cell that separates 765 section 2's two open readings of the sticky bit 15, and 23 = 22 plus THE CMD LINE'S OWN LEVEL, COUNTED (a rung of ONE new cell, ONE new store and NO new device access): the poll that a command's whole 1.2 s budget runs in ALREADY reads PRESENT_STATE 0x24 once per iteration into `inhibit_last`, so rung 24 counts the samples of the first ST_CMD_INHIBIT_SAMPLES in which bit 24 - the CMD line's signal level - is LOW. That count is 765 section 4's deferred cell and it is the one reading that can separate 764 section 4's candidate 3 (the block never put anything on the bus: the line is pulled HIGH and nothing ever pulls it down, the count is ZERO) from candidate 1 (the block issued and the card stayed silent: the line moved, so SOMETHING drove it). A COUNT and not a sample, because `_nidx_inhibit_last` bit 24 is the LAST sample of all and reads HIGH in every arm on record - HIGH is what an idle line and a finished transmission BOTH look like, so one sample at the end cannot tell them apart. Two caveats the record carries: the count's ZERO is only a reading if the sampler outran the bus, so it is published beside `_nidx_polls`/`_nidx_ticks` (which give the sampling period), and bit 24 is the line's INPUT level, so the block reading back its own drive counts as movement just as a card's answer does. A value above the ladder is refused here rather than shaping an image whose switches claim something else.", and 24 = 23 plus THE SDC1 PADS READ: ONE 32-bit read of `TLMM + 0x2044` (`0xfd512044`, the dedicated-pad control register `sdhci_msm_setup_pad` writes and no rung has ever read), after an `entry_mmio_section` install of the `0xFD5` megabyte - ZERO stores, and the expected value is DERIVED from the Mi 4 board file arrays (`qcom,pad-pull-on` / `qcom,pad-drv-on`) rather than typed. The pad register has NO function-select field at all (every bit is drive strength or pull), which is what retires 768 section 5 half about the function mux by a register layout instead of by a reading; the cell that matters is `PULL SDC1_CMD`, because the MMC CMD line is OPEN-DRAIN during identification and returns HIGH only through it. 769. And note for TIMEOUT_CONTROL 0x2E: value 0 is the SHORTEST setting and not the longest (m776), and the register is never written on this ladder because sdhci.c:826 writes it only under `data || MMC_RSP_BUSY` - the 665.2 us the rung-24 press measured is that reset value, and 25 = 24 plus FOUR CELLS THAT ARE ALREADY MEASURED AND WERE BEING THROWN AWAY - NO NEW DEVICE ACCESS, NO NEW REGISTER, NO NEW WINDOW, NO NEW MEGABYTE AND NO NEW STORE. `cmdlow_seen` is filled by the SAME first-1024 sampler for EVERY command this ladder issues, and it was published for `nidx` ALONE - so value 25 prints it for CMD0 and CMD1 (`xnu_live_storage_cmd0_cmdlow_seen`, `xnu_live_storage_cmd1_cmdlow_seen`) and for CMD2 (`xnu_live_storage_cid_cmdlow_seen`), and it prints the one field of the CMD2 result that has been published for NO command at all - `xnu_live_storage_cid_inhibit_after`, the fourth row of 771 and the only row of that table the log does not carry. 771. The arm exists because 771 read the rung-24 log as ONE FOUR-ROW TABLE and found that the four rows are NOT the same experiment: `_cmd0_inhibit_seen = 0x218` beside a command that completed and latched `INT_RESPONSE`, `_cmd1_inhibit_seen = 0` and `_cid_inhibit_seen = 0` beside 5.09 M polls each with NOTHING latched at all, and `_nidx_inhibit_seen = 0x400` beside a block that armed AND FIRED its own response timeout at 665.2 us. AND THE ONLY TRACE OF WHAT THE CMD LINE WAS DOING DURING CMD1 OR CMD2 IS ONE SAMPLE: `inhibit_last` bit 24, which reads LOW for CMD1 and HIGH for CMD2 on every capture in the archive (10 cmd0, 8 cmd1, 7 cid) - and a single sample taken at the end of a window is exactly the reading rung 24 replaced with a COUNT for CMD3. This rung finishes that repair for the other three, and it is why a press of value 25 answers everything value 24 answers and these four cells besides - the build is byte-identical at value 24., and 26 = 25 plus THE REST OF THE CMD2 RESULT THAT WAS BEING THROWN AWAY - eleven fields `st_send_command` fills for every command and the `st_all_send_cid` publish block has never printed, because that block was written at rung 16, before rungs 12 and 24 added them to the struct. **NO NEW DEVICE ACCESS, NO NEW REGISTER, NO NEW WINDOW, NO NEW MEGABYTE AND NO STORE**: every one of the eleven is already in the struct when the block runs. The one that decides something is `xnu_live_storage_cid_stale` - `INT_STATUS 0x30` as found at the ENTRANCE of CMD2, which is THE FIRST READ OF THAT REGISTER SINCE the POLL of CMD1 GAVE UP 1.2 SECONDS EARLIER, and which no capture has ever carried for CMD2. **771 saw the anomaly and could not read it**: a block that acknowledges CMD0 (inhibit rises, `INT_RESPONSE` latches) and CMD3 (inhibit held for all 1024 samples, `INT_TIMEOUT` latches at 665.2 us) but raises NOTHING for CMD1 and CMD2 - and the press DOES show `_nidx_status_pre = 0x00018000`, i.e. `ERROR | TIMEOUT` latched after the poll of CMD2 gave up, while the only write in that interval is a store to `0x34` that 736 measured sets bit 15 by itself. `_cid_stale` asks the same question one command earlier, in an interval that contains no write to `0x34` at all. The other ten are the entrance and exit state (`ps_before`, `inhibit_before`, `ps_after`), the whole inhibit gate (`inhibit_polls`/`_ticks`/`_timeout`), the clear (`clear_wrote`), the argument, and the two companions that make a zero response a reading rather than a silence (`rsp_present`, `resp_read`). The build is byte-identical at value 25. , and 27 = 26 plus THE ONE STORE THIS LADDER HAS EVER MADE TO A SECOND DEVICE: the TLMM SDC1 pad-control register at PHYS 0xfd512044, the register rung 25 already reads. It is ONE masked read-modify-write covering all seven drive and pull fields at once - hdrive clk/cmd/data at bits 6/3/0 and pull clk/cmd/data/rclk at 13/11/9/15, the same seven constants the six _Static_asserts of rung 25 fix - and the value it writes is the one this board DT arrays produce through the kernel own shift and mask: 0x00009F24, which is exactly what the vendor own sdhci_msm_setup_pad walks those seven fields to. **AND IT IS GUARDED, WHICH IS WHAT MAKES THE ARM A FORK RATHER THAN A GUESS**: the store fires only when the 32-bit read did not already equal that value, so a board whose pads are already configured publishes _pad_write_skipped = 1 with _pad_writes = 0 and NO STORE IS MADE AT ALL, and anything else publishes the count 1 with _pad_wrote, _pad_after and _pad_after_match beside it. The read, the seven field decodes, the raw value and the expected value are unchanged from rung 25. **THE STORE IS A PLAIN READ-MODIFY-WRITE WITH A MASK**, field for field the one the vendor pad path reaches through msm_tlmm_set_field (gpio-msm-common.c:481-496): no unlock sequence, no write-one-to-clear, no FIFO, no shadow register - and a single masked RMW over the union of the seven fields makes the vendor six-call ORDER irrelevant, which is the property that makes one store safe where six interleaved ones would need an argument. The bits OUTSIDE the seven are carried through from the read, so this is a field write and not a whole-word write. It cannot reach another register (one word, no indexed or block access), another pad bank (0x2044 is not the SDC2 register at 0x2048, and the seven masks are disjoint so their union is their sum), a FUNCTION (every field of this register is drive strength or pull - 769 section 2 established by layout that there is no function-select field to write), the PMIC, the rails or any clock - acts 1 and 3 of the vendor BUS_ON branch are the two 773 says are unreachable, and this rung does not reach them. The build is byte-identical at value 26. 776, 775 and 773 for what 27 is, and 772 and 771 for what they answer. And 28 = 27 plus THE SAME WINDOW ONE CONSTANT WIDE AT THE TWO WINDOWS THE WIDENING HAD NOT REACHED, and nothing else: st_cmd_path's window - WHICH IS CMD0's AND NOT CMD0-and-CMD1's, A CLAIM 785 MADE AND ITS OWN PRESS REFUTED (786) - and st_all_send_cid's CMD2 window now write ST_SDHCI_INT_ENABLE_CMD = 0x000F0001 in place of the lone SDHCI_INT_RESPONSE, so every window this ladder opens before CMD2 carries the five bits - the same commands, the same arguments, the same flag words and the same two stores at INT_ENABLE 0x34 per window, SIGNAL_ENABLE 0x38 still READ AND NEVER WRITTEN, and NO NEW REGISTER, NO NEW ACCESS, NO NEW KEY AND NO NEW STORE. It is 783's finding acted on: on the rung-28 arm _cid_status_any was a reading about the MASK. A value below 28 compiles byte for byte to rung 28's arm, which is how strict containment is measured rather than argued. And 29 = 28 plus THE THIRD ENABLE WINDOW, THE ONE 785 THOUGHT IT HAD ALREADY WIDENED: st_cmd_path's rung-14 window CLOSES on the line after CMD0's publishes, and its restore stores int_enable - ZERO - into INT_ENABLE 0x34, so CMD1 IS SEND_OP_COND and has run on EVERY ARM FROM RUNG 14 ON WITH THE COMPLETION BIT NOT ENABLED, its 1.2 s bound being what a poll sees when the bit it polls for is gated off (786 read that out of the rung-29 capture: _ena_wrote_back = 0x00000000 beside _cmd1_status_any = 0 over _cmd1_polls = 0x004da000 and _cmd1_timeout = 1). Rung 30 opens a THIRD window immediately after the between-commands gate and closes it immediately after CMD1's publishes, through TWO BODIES OF ITS OWN (st_cmd1_enable_open and st_cmd1_enable_restore, one caller each) so that rung 14's one-caller assertion stands unchanged: TWO MORE STORES TO THE SAME INT_ENABLE 0x34, one store and one readback per body and NOTHING ELSE - neither body touches INT_STATUS 0x30 - so no new address, no new width, no new megabyte, no new device and NO NEW STATUS CELL, and CMD1 finally runs under an enable that can see a completion, and 30 = 29 plus ONE BIT OUT OF THE CMD2 FLAG WORD: st_all_send_cid sends ALL_SEND_CID with MMC_RSP_PRESENT | MMC_RSP_136 and the CRC flag dropped - the word 0x0209 becomes 0x0201 - with the SAME opcode, the SAME argument, the SAME window, the SAME position in the driver order and the SAME five-bit INT_ENABLE 0x34, so it adds NO new address, NO new width, NO new megabyte, NO new device and NO new key, and the reading it buys is whether the immediate ERR | CRC that 791 found at CMD2 is a property of a CRC check the command carries or of its 136-BIT RESPONSE REQUEST, the first row being _cid_complete = 1 and the CID taken., and 31 = 30 plus ONE BYTE STORE TO TIMEOUT_CONTROL 0x2E - THE REGISTER THIS LADDER HAS NEVER WRITTEN, which sits at its reset value, the SHORTEST bound the register can express, and which the driver leaves alone because sdhci_prepare_data writes it only for a data command or MMC_RSP_BUSY, and every command here is data-less - so the 665.2 us that CMD2 and CMD3 both hit, and that CMD1 completes 130 us inside, is the block default and not a property of the card. The arm raises it to 0x03 - 5.32 ms at the measured 665.2 us scaled by 2 to the value - for the CMD2 window alone and restores it after, which is 225 times under the driver own 1.2 s poll bound,  so it can neither expire before a correct 136-bit response arrives nor hide the poll bound - AND THAT ARM WAS PRESSED AND REFUTED ITS OWN MECHANISM (798): `_tout_held = 3` with `_cid_ticks` 12,772 = 665.3 us against rung 31's 12,770 = 665.2 us, so `TIMEOUT_CONTROL 0x2E` DOES NOT GOVERN THE COMMAND RESPONSE TIMEOUT and the card is simply not answering, and 32 = 31 plus THE DRIVER'S OWN CONTROL FLOW, PORTED: `mmc_send_op_cond`'s OCR argument derived from CMD1's first response (`mmc.c:1943`'s `ocr &= ~0x7F` and `mmc.c:1359`'s `(1 << 30)`, with the `host->ocr_avail` intersection deliberately NOT ported because this image has no representation of it) and its LOOP - up to 100 sends of CMD1 with `mmc_delay(10)` between them, exiting when `resp[0] & MMC_CARD_BUSY` SETS, which is `mmc_ops.c:157`'s own test, is the direction this ladder's gate reads INVERTED at `:4785`, and which the image already publishes as `_cmd1_resp_busy` - a key that reads 0 on ALL TWELVE occurrences in the archive - so the ladder has been putting CMD2 on the bus since rung 16 for a card that has never reported power-up complete; and 33 will gate CMD2 on the loop's answer, which is the driver's semantics and is a SEPARATE rung for the one-change-per-rung reason, and 33 = 32 plus THE CMD2 GATE GIVEN THE WORD THE DRIVER ACTUALLY TESTS (experiment 804): the SAME two bodies, the SAME two commands, the SAME argument and the SAME single `op_busy` value that `st_op_cond_loop` already returns and the rung below already computes and throws away, with ONE CONDITION CHANGED and NOTHING ELSE MOVED - the gate that decides whether CMD2 is sent tests `op_busy != 0` (the LOOP result, which is `mmc_ops.c:157` own test) in place of `(c1.resp & MMC_CARD_BUSY) == 0` (the PROBE response). The probe is a SINGLE PASS by the driver own design (`mmc_ops.c:148-150`), so on a card that has not finished power-up its word is the OCR with bit 31 CLEAR and the OLD condition is TRUE - which is why `_cid_gated` read 0 on every arm from rung 16 through 32 and why all of them put CMD2 and CMD3 on the bus at a card that had not finished power-up. **THE RUNG-33 PRESS MEASURED BOTH HALVES OF THIS**: `_opcond_sends = 2` with `_opcond_busy_seen = 1` and `_opcond_last_resp = 0xc0ff8080` against `_cmd1_resp = 0x40ff8080` - the SAME OCR differing by EXACTLY THE BUSY BIT - and then `_cid_complete = 1` with a real SanDisk `SDW16G` CID that NINE ARMS had never been able to read. So the two words are not two readings of one thing: they are the answer to two different questions, and the gate was asking the one the driver does not. TWO NEW CELLS make the repair readable and are published on BOTH paths - `xnu_live_storage_cid_gate_word` (the word the gate tested, read out of the LOOP result struct) and `xnu_live_storage_cid_gate_busy` (the test own result, printed beside its source the way `_cmd1_resp_busy` already is) - so on a row where the card became ready they differ from `_cmd1_resp` by exactly the busy bit, and on a row where the loop never saw the card out of reset they are EQUAL and `_cid_gated` is 1, which is the row the old gate could not produce at all. `c1.sent != 0` is KEPT so that `_cid_gated_reason` still separates 1 (CMD1 never reached the bus) from 2 (it did, and the loop never saw the card out of reset) - reason 2 MEANING is what moves with this rung. **NO NEW STORE, NO NEW ADDRESS, NO NEW WIDTH, NO NEW MEGABYTE, NO NEW DEVICE AND NO NEW KEY OF ANY DEVICE REGISTER**: the two arm call the same bodies they called before, in the same order, at the same place in the driver sequence, and what changes is WHICH WORD DECIDES WHETHER THE SECOND OF THEM RUNS. The value-32 arm is left BY VALUE - the `#if >= 33` split sits INSIDE rung 16 guard, so a value below 33 compiles the old condition byte for byte and the spent rung-33 park containment stands", and 34 = 33 plus THE DRIVER'S FIRST COMMAND ABOVE CMD3 - CMD9 (`SEND_CSD`, mmc.h:38, `ac [31:16] RCA R2`), which is mmc.c:1420's `mmc_send_csd(card, card->raw_csd)` - the driver's own statement immediately after CMD3: ONE new command body, `st_send_csd`, that puts the driver's own word 0x0909 on the bus with the argument the DRIVER assigns ITSELF - mmc.c:1400's `card->rca = 1` through mmc_ops.c:301's `card->rca << 16`, so `ST_MMC_RCA_1` and NOT any word the card returned, because CMD3's response is R1 (mmc.h:32), which is 32 bits of card status with no RCA field at any offset - assembles the 136-bit response in the driver's word-3-first order (sdhci.c:1163-1172: the sixth copy of that one arithmetic, and the copy `tools/check_response_word_order.py` now reads), publishes the card's own status word taken BETWEEN CMD3 and CMD9 so that this command's precondition is a reading rather than an assumption, and decodes the four assembled words with the vendor's own CSD offsets (mmc.c:147-196) so that a real CSD is checkable from outside this image - TWO stores, eight response reads, and the same window, gate and driver position as the rungs below it, and 35 = 34 plus THE DRIVER'S NEXT COMMAND (`mmc_select_card`, mmc.c:1436): CMD7 `SELECT_CARD` (opcode 7, mmc.h:36 \"ac [31:16] RCA R1\") with the driver's own words - argument `card->rca << 16` (mmc_ops.c:36) and flags `MMC_RSP_R1 | MMC_CMD_AC` (mmc_ops.c:37) - so this rung's command word is 0x071A, whose FLAG BYTE 0x1A IS THE BYTE 817 MEASURED THIS LADDER REMOVING A BIT FROM ON CMD3 AT RUNG 21 (0x1A -> 0x0A) and has not sent since - the word and the flag byte are two different values and 819's first draft of this clause wrote the wrong one - which makes SDHCI_CMD_INDEX reachable and `_sel_err = 0` beside `_sel_complete = 1` a statement that the frame IS CMD7's response; the body decodes the R1 fields off its own response (mmc.h:141-144) and DECODES NOTHING out of its precondition, because RESPONSE 0x10 holds the CSD's last 32 bits and not an R1 - one store to the enable window, the same window and the same gat, and 36 = 35 plus THE DRIVER'S OWN "IS THE CARD ALIVE" CHECK (`mmc_send_status`, `mmc_ops.c:468`): CMD13 `SEND_STATUS` (opcode 13, `mmc.h:42` `ac [31:16] RCA R1`) with `cmd.arg = card->rca << 16` (`mmc_ops.c:478`) and **the DRIVER'S OWN FLAG WORD, SPI BITS AND ALL** - `cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC` (`mmc_ops.c:479`) = 0x0195, which is NOT the 0x15 this ladder calls \"the driver's flags\" - so this rung's command word is 0x0D1A, CMD7's flag byte 0x1A with a different opcode, and the two SPI bits fall off `sdhci_cmd_to_flags` (five bits read, bits 7 and 8 not among them) so they never reach the bus; the body publishes the driver's word, the ladder's word and the mapping's answer as THREE cells because that is one value with two definitions and only one of them is a command. It is a command the driver uses as its alive check (`mmc.c:1724-1727`), it changes no state, and its `R1_CURRENT_STATE` is the discriminator rung 36 left open: 4 = TRAN says CMD7 landed, 3 = STBY says it did not. Its call is UNGATED and `_sta_pre_state` is published as a reading rather than gated on, because filtering on the answer is the one thing this rung must not do. Same window, same gate, one frame, and 37 = 36 plus THE DRIVER'S OWN NEXT STATEMENT AND THIS LADDER'S FIRST DATA PHASE - CMD8 `SEND_EXT_CSD` (`mmc.h:37` `adtc R1`, `mmc_ops.c:335`), reached by `mmc.c:1446`'s `mmc_get_ext_csd` (`mmc.c:201`), which is the line IMMEDIATELY AFTER `mmc.c:1436`'s `mmc_select_card`. IT RETIRES CMD16: `MMC_SET_BLOCKLEN` (`mmc.h:50`) has ONE caller in the whole vendor tree - `core.c:2588`'s `mmc_set_blocklen` is called only from `card/mmc_test.c:182` - so it is not on this chain at all, and rung 37's record (value 36) recommended it by reading a function's position in a FILE as its position on a PATH. Its flags are `MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC` (`mmc_ops.c:263`) = 0x00B5 with `cmd.arg = 0`, so its word is 0x081A - three commands, three DIFFERENT driver flag words, ONE flag byte 0x1A. THE MECHANISM IS `sdhci_prepare_data`'s PIO half in the vendor's own order: `TIMEOUT_CONTROL` 0x2E first (`sdhci.c:828`, before `sdhci.c:831`'s `if (!data) return;`), then `BLOCK_SIZE` 0x04 = `SDHCI_MAKE_BLKSZ(SDHCI_DEFAULT_BOUNDARY_ARG, 512)` = 0x7200 (`sdhci.c:975`), `BLOCK_COUNT` 0x06 = 1 (`sdhci.c:976`), `sdhci_set_transfer_irqs`'s PIO pair `DATA_AVAIL | SPACE_AVAIL` (`sdhci.c:806`) added to the enable window, `TRANSFER_MODE` 0x0C = `SDHCI_TRNS_BLK_CNT_EN | SDHCI_TRNS_READ` = 0x0012 (`sdhci.c:1015`), and 128 reads of `SDHCI_BUFFER` 0x20 into a 512-byte buffer, each driven by `INT_STATUS`'s `DATA_AVAIL` and bounded. THE COUNT IN TIMEOUT_CONTROL IS COMPUTED, NOT CHOSEN, AND IT IS LOAD-BEARING: 512 bytes at this ladder's 400 kHz clock is 10.24 ms on the wire, so the reset value's bound is not one. `sdhci_calc_timeout` (`sdhci.c:740`) takes its `ALWAYS_USE_BASE_CLOCK` arm because `sdhci-msm.c:2899` sets that quirk and `:2906` sets `DIVIDE_TOUT_BY_4` with it, so `host->timeout_clk` from CAPABILITIES is NEVER READ there and the step-0 bound is 2^13 * 1000 / (host->clock / 4000) - which rung 31's own `ST_SDHCI_TIMEOUT_CMD2` comment does not assume, and this rung publishes both. The target is `mmc_set_data_timeout` (`core.c:1252`) for THIS card, decoded from the CSD the ladder already holds. It answers with the card's own EXT_CSD: `EXT_CSD_REV` byte 192, `EXT_CSD_STRUCTURE` 194, `EXT_CSD_CARD_TYPE` 196 and `EXT_CSD_SEC_COUNT` 212-215 - the density this ladder has never had. NOTHING IS WRITTEN TO THE CARD: `data.flags = MMC_DATA_READ` (`mmc_ops.c:267`) is card-to-host, and every store in the body is a host register this file already names, and 38 = 37 plus THE RUNG-38 PRESS'S OWN REPAIR - the SAME CMD8 body, whose window now ORs ST_SDHCI_INT_ENABLE_CMD (0x000F0001, bit 0 = SDHCI_INT_RESPONSE) and clears the two DMA enables exactly as sdhci_set_transfer_irqs's PIO arm does (sdhci.c:806-815), because the press measured _ext_complete = 0 while the 512 bytes arrived: rung 38's window enabled only the PIO pair (0x30) and INT_STATUS latches a bit only for the enables set in INT_ENABLE, so the completion bit never latched and st_send_command's poll ran its full budget; the arm re-sends the SAME CMD8 and the only new cell is _ext_complete = 1 - no new megabyte, no new device, the same body]"
#endif

/*
 * **712: rung 9's own budget, and it is a switch because the number is the arm's judgement rather than
 * the vendor's.** `sdhci_msm_check_power_status` (`sdhci-msm.c:2205`) blocks on
 * `wait_for_completion` with **no bound at all** - the bound it is compared against is the driver's
 * own reset poll, 100 ms (`sdhci.c:251`, 'Wait max 100 ms', `mdelay(1)` per decrement at `:267`), and
 * rung 4's reset poll already carries that same 1,920,000 ticks. So the default is that number at the
 * 19,200,000 Hz 699's ending read out of `cntfrq`, and the range is what a press may spend:
 *
 * * **1 is the floor, and 0 is refused rather than read as "no wait".** Rung 8 *is* the no-wait arm -
 *   it takes the predicate, the registration and the arming and stops - so a budget of 0 would make
 *   this rung's cells indistinguishable from the rung below it while the record said otherwise.
 * * **19,200,000 (1 s) is the ceiling**: this arm's run ends on 690's 6,000 ms clock, and a budget past
 *   a sixth of it turns the press into a reading about the ending rather than about the wait.
 *
 * A budget that is a *time* and not a count is the same choice rung 4 made, and for the same measured
 * reason: the rate is read out of the hardware (`xnu_live_post_cntfrq` = 19,200,000 on 691's, 694's
 * and 711's runs), so 19200 ticks is a millisecond on this device and not an assumption.
 */
#ifndef STAGE90_XNU_PWR_WAIT_TICKS
#define STAGE90_XNU_PWR_WAIT_TICKS 1920000
#endif
#if STAGE90_XNU_PWR_WAIT_TICKS < 1 || STAGE90_XNU_PWR_WAIT_TICKS > 19200000
#error "STAGE90_XNU_PWR_WAIT_TICKS is rung 9's bounded replacement for sdhci_msm_check_power_status's UNBOUNDED wait_for_completion (sdhci-msm.c:2205), in ticks of this device's own 19,200,000 Hz counter. 1920000 (the default) is 100 ms - the bound sdhci.c:251 states for the driver's own reset poll and the number rung 4's poll already carries. 1 is the floor and 0 is REFUSED rather than read as 'no wait', because rung 8 is the no-wait arm and a budget of 0 would make this rung's cells indistinguishable from the rung below it while the record said otherwise; 19200000 (1 s) is the ceiling, because this arm's run ends on a 6,000 ms clock and a budget past a sixth of it makes the press a reading about the ending rather than about the wait."
#endif

/*
 * **The declarations are outside the `#if`, and 692 measured why they have to be.** The no-op arm of
 * `ST_LIVE` below still evaluates its arguments, so an `extern` inside the `#if` is a name the untraced
 * build cannot see - and that is not hypothetical: `entry_gic.c` carried its five in the guarded arm
 * since 484 and would not compile at `STAGE90_ENTRY_GIC_TRACED=0`, which is exactly what
 * `STAGE90_ENTRY_TRACE=0` sets. That is repaired in the same step, and this file is written the way the
 * repair leaves its sibling. A declaration is not code, so this costs no byte of any traced image.
 */
extern void entry_live_write(const char *key, uint32_t value);
extern uint32_t g_live_state;
/* 484's four numbers, read by `entry_mmio_section` at the install - see `entry_gic.c`. */
extern uint32_t g_live_mmio_l1;
extern uint32_t g_live_mmio_l1_moved;
extern uint32_t g_live_mmio_ttbr0;
extern uint32_t g_live_mmio_ttbr1;
#if STAGE90_XNU_STORAGE_PROBE
#define ST_LIVE(key, value) entry_live_write((key), (uint32_t)(value))
#else
#define ST_LIVE(key, value) do { (void)(key); (void)(value); } while (0)
#endif

extern uint32_t entry_mmio_section(uint32_t va, uint32_t pa, uint32_t *slot_before_out,
                                   uint32_t *desc_out);

/* --------------------------------------------------------------------------------------------- */
/* The two windows, and the clock gate that decides whether they may be read                       */
/* --------------------------------------------------------------------------------------------- */

/*
 * **531 section 1's table, and every address in it comes from `msm8974.dtsi:500-528` - the node that
 * declares both windows by name** (`reg = <0xf9824900 0x11c>, <0xf9824000 0x800>`;
 * `reg-names = "hc_mem", "core_mem"`). The four offsets are the vendor driver's own `#define`s, quoted
 * with the line each one is read from rather than retyped: `CORE_POWER 0x0` and `CORE_SW_RST (1 << 7)`
 * (`sdhci-msm.c:59-60`), `CORE_MCI_DATA_CTRL 0x2C` (`:132`), `CORE_MCI_VERSION 0x050` (`:139`),
 * `CORE_HC_MODE 0x78` and `HC_MODE_EN 0x1` (`:55-56`). The two at the end are the SDHCI standard's own
 * - `HCI_VERSION 0x00` and `CAPABILITIES 0x40` - and they are read out of the *other* window precisely
 * because they are standard: a word at `hc_mem + 0x00` that carries a plausible spec version is
 * evidence the second window answers, which `core_mem`'s words cannot give on their own.
 *
 * **698: the name was wrong, and the two words were never the standard register file.** The paragraph
 * above says `HCI_VERSION 0x00`; the vendor's own header says `sdhci.h:27` `SDHCI_DMA_ADDRESS 0x00` and
 * `:241` `SDHCI_HOST_VERSION 0xFE`, and `sdhci-msm.c:2909` reads the version **at `0xFE`**. 697 is what
 * made that visible: the pre/post pair is `0x10 -> 0x00`, which is a *soft* register the core reset
 * clears (a DMA address) and not a version word, while `0x40`'s strapping constant did not move. So the
 * define is renamed to what it reads, the real version register is added, and the misnamed keys are
 * renamed beside it. **The readers were enumerated first** ([[mi4-one-value-two-definitions]], m699:
 * one rename, two readers - and a missed reader fails *silent*): two sites in this file and one line of
 * 694's document, and nothing in `src`'s shell scripts or in `tools/` names either key.
 */
#define ST_CORE_MEM_BASE        0xf9824000u
#define ST_HC_MEM_BASE          0xf9824900u
#define ST_CORE_POWER           0x00u
#define ST_CORE_MCI_DATA_CTRL   0x2Cu
#define ST_CORE_MCI_VERSION     0x050u
#define ST_CORE_HC_MODE         0x78u
#define ST_SDHCI_DMA_ADDRESS    0x00u   /* sdhci.h:27 - the name this file carried as HCI_VERSION until 698 */
#define ST_SDHCI_CAPABILITIES   0x40u   /* sdhci.h:181 */
#define ST_SDHCI_CAPABILITIES_1 0x44u   /* sdhci.h:215 */
#define ST_SDHCI_MAX_CURRENT    0x48u   /* sdhci.h:217 */
#define ST_SDHCI_PRESENT_STATE  0x24u   /* sdhci.h:64  */
#define ST_SDHCI_HOST_CONTROL   0x28u   /* sdhci.h:76  - a BYTE, and at an offset no 32-bit access reaches */
#define ST_SDHCI_POWER_CONTROL  0x29u   /* sdhci.h:87  - a BYTE. **READ ONLY UNTIL RUNG 7, WHICH WRITES
                                         * IT**: rung 3 reads it, and rung 7 makes the one 8-bit store
                                         * to it that `sdhci_set_power` makes (the value derived from
                                         * CAPABILITIES 0x40 below). The census clause moved it from the
                                         * asserted-absent set `47 44 44` to the asserted-present one
                                         * `47 44 44 41` in the same build */
#define ST_SDHCI_CLOCK_CONTROL  0x2Cu   /* sdhci.h:100 - 16-bit */
#define ST_SDHCI_TIMEOUT_CONTROL 0x2Eu  /* sdhci.h:111 - a BYTE, and **READ ONLY UNTIL RUNG 23, WHICH
                                         * READS IT AND STILL DOES NOT WRITE IT**. The vendor writes it
                                         * only from `sdhci_prepare_data` (sdhci.c:827-828), inside
                                         * `if (data || (cmd->flags & MMC_RSP_BUSY))` - so a data-less
                                         * command with no `MMC_RSP_BUSY` leaves it at its reset value on
                                         * the vendor's own path too, and this ladder's six commands are
                                         * all data-less and none carries `MMC_RSP_BUSY` (765 section 1).
                                         * It is read at rung 23 because 765 section 4 pre-registered it:
                                         * a NON-ZERO value here would mean something armed a response
                                         * timeout for a command the vendor's rule says never does - and
                                         * `0x00` is the field's LONGEST setting rather than "no timeout",
                                         * so the reading is about the arm's own premise and not a claim
                                         * that the block cannot time out */
#define ST_SDHCI_SOFTWARE_RESET 0x2Fu   /* sdhci.h:113 - a BYTE */
#define ST_SDHCI_RESET_ALL      0x01u   /* sdhci.h:114 - the mask the driver resets with, and it
                                         * is also the bit the poll below tests: the standard says
                                         * the bit is self-clearing */
#define ST_SDHCI_SLOT_INT_STAT  0xFCu   /* sdhci.h:239 - 16-bit */
#define ST_SDHCI_HOST_VERSION   0xFEu   /* sdhci.h:241 - 16-bit, and the register 697 left owed */
#define ST_SDHCI_CARD_PRESENT   0x00010000u /* sdhci.h:71 - must NOT be read as "no card" here, see below */

/*
 * **The two registers 756 section 1 named as the ones the vendor's own bring-up programs at this
 * ladder's clock and no rung has ever touched, plus the status register that says whether the DLL is
 * locked.** Read out of `sdhci-msm.c` and `sdhci.h` rather than retyped:
 *
 *   * `HOST_CONTROL2 0x3E` (`sdhci.h:162`, 16-bit, 2-aligned) is written unconditionally by
 *     `sdhci_msm_set_uhs_signaling` at `:2597` after the DLL branch, and rung 4's `SDHCI_RESET_ALL` is
 *     the only thing in this image that could have cleared its `UHS` field. Bits[2:0] are the test.
 *   * `CORE_DLL_CONFIG 0x100` (`sdhci-msm.c:80`) and `CORE_DLL_STATUS 0x108` (`:89`) are reached by the
 *     vendor as `host->ioaddr + CORE_DLL_CONFIG`, and `host->ioaddr` is `sdhci_pltfm_init`'s FIRST
 *     memory resource - **`hc_mem`** (`msm8974.dtsi:500`, `reg-names = "hc_mem", "core_mem"`). It is the
 *     one place in this ladder where a `0x1xx` offset is in `hc_mem` and not in `core_mem`, and the DT
 *     is what settles it: `msm8974pro.dtsi:1765` widens `&sdhc_1` from the base node's `0x11c` to
 *     **`0x1a0`**, and `0x100`/`0x108` are beyond `0x11c` - the window was widened to cover exactly
 *     this register pair. Getting the window wrong would read `core_mem + 0x100` (a different register
 *     file) and report it under these names, which is why the constants below carry the window in the
 *     name and not only the offset.
 */
#define ST_SDHCI_HOST_CONTROL2  0x3Eu   /* sdhci.h:162 - 16-bit, and the vendor reads it with sdhci_readw */
#define ST_HC_DLL_CONFIG        0x100u  /* sdhci-msm.c:80 - hc_mem's tail (0x1a0 on this part) */
#define ST_HC_DLL_STATUS        0x108u  /* sdhci-msm.c:89 - hc_mem's tail */
#define ST_HC_DLL_EN            (1u << 16)  /* sdhci-msm.c:82 */
#define ST_HC_CDR_EN            (1u << 17)  /* sdhci-msm.c:83 */
#define ST_HC_CK_OUT_EN         (1u << 18)  /* sdhci-msm.c:84 */
#define ST_HC_DLL_PDN           (1u << 29)  /* sdhci-msm.c:86 */
#define ST_HC_DLL_RST           (1u << 30)  /* sdhci-msm.c:87 */
#define ST_HC_DLL_LOCK          (1u << 7)   /* sdhci-msm.c:90, in CORE_DLL_STATUS 0x108 */
#define ST_SDHCI_CTRL_UHS_MASK  0x0007u     /* sdhci.h - SDHCI_CTRL_UHS_MASK, the field 2597 sets */

/*
 * **The widths are not a style choice, and the reason is an ARMv7 rule rather than a preference**: an
 * *unaligned* access to Device memory **faults**, and this arm's mappings are Strongly-ordered
 * shareable device sections (694 measured both descriptors' attribute). So `POWER_CONTROL 0x29`,
 * `HOST_CONTROL 0x28` and `SOFTWARE_RESET 0x2F` are byte registers at offsets no 4-byte access can
 * reach, `CLOCK_CONTROL 0x2C`/`SLOT_INT_STATUS 0xFC`/`HOST_VERSION 0xFE` are 16-bit with `0xFE` not
 * 4-aligned either, and a 32-bit load at any of those four addresses would fault exactly where 692
 * faulted - by *alignment* this time rather than by translation. The widths below are the vendor's own
 * accessors, quoted from `sdhci.c:98-128`'s register dump (`readw` for the version and the two 16-bit
 * registers, `readb` for the three bytes, `readl` for the rest) and from `:246`/`:259` for
 * `SOFTWARE_RESET`. `build_entry.sh` refuses the build if any access in this body breaks the rule.
 */
#define ST_CORE_PWRCTL_MASK     0xE0u   /* sdhci-msm.c:63, readl at :2946 */
#define ST_CORE_PWRCTL_CTL      0xE8u   /* sdhci-msm.c:65 - the vendor reads it 32-bit (:2879) and writes it
                                         * 8-bit at one site (:2069); 0xE8 is 4-aligned, so a 32-bit read is
                                         * legal and is what the read at :2879 does */

/* 531 section 8's SDCC1 pair, out of `clock-8974.c:244` and the `+4` the UART pair measured. */
#define ST_GCC_BASE             0xfc400000u
#define ST_SDCC1_BCR            0x04C0u
#define ST_SDCC1_CBCR           0x04C4u
#define ST_SDCC1_CLK_ENABLE     0x1u

/*
 * **769: the SDC1 pads, and the register is `TLMM + 0x2044` - i.e. PHYS `0xfd512044`.**
 *
 * `sdhci_msm_setup_pad` (`sdhci-msm.c:964-988`) is where the vendor writes them, and it calls exactly
 * two helpers - `msm_tlmm_set_hdrive` and `msm_tlmm_set_pull` - which land in `msm_tlmm_set_field`
 * (`gpio-msm-common.c:481-496`) on the register `SDC1_HDRV_PULL_CTL = 0x2044` (`:37-42`). The register
 * is 4-aligned, so a 32-bit read is the legal width and is what this rung takes.
 *
 * **EVERY FIELD OF THIS REGISTER IS DRIVE STRENGTH OR PULL** (`gpio-msm-common.c:59-88`): the 3-bit
 * hdrive fields at 6/3/0 for CLK/CMD/DATA and the 2-bit pull fields at 13/11/9 for CLK/CMD/DATA plus
 * 15 for RCLK. There is **no function-select field**, which is why 768 section 5's "function mux"
 * candidate is retired by a register layout and not by a reading - SDC1 is one of this SoC's
 * **dedicated-pad** controllers (`sdhci-msm.c:267-270`), not a GPIO-routed one.
 *
 * The expected value is DERIVED below rather than typed, from the Mi 4's own board file
 * (`msm8974pro-ac-pm8941-mtp-v5.dts:25-29`: `qcom,pad-pull-on = <0x0 0x3 0x3 0x1>`,
 * `qcom,pad-drv-on = <0x4 0x4 0x4>`), through the same shift-and-mask the kernel performs.
 */
#define ST_TLMM_BASE            0xfd510000u  /* mach/msm_iomap-8974.h:34 - MSM8974_TLMM_PHYS, 16 KB */
#define ST_TLMM_SDC1_PAD        0x2044u      /* gpio-msm-common.c:37 - SDC1_HDRV_PULL_CTL */
#define ST_TLMM_SDC1_PAD_ADDR   (ST_TLMM_BASE + ST_TLMM_SDC1_PAD)  /* 0xfd512044 */
#define ST_TLMM_SECTION         0xfd500000u  /* the 1 MB section the install covers, and
                                              * the address `entry_mmio_section` is given:
                                              * `entry_section_install` masks the PA to
                                              * the section and indexes on `va >> 20` */

#define ST_TLMM_SDC1_DATA_HDRV_SHIFT  0u     /* gpio-msm-common.c:59-88, in the enum's own order */
#define ST_TLMM_SDC1_CMD_HDRV_SHIFT   3u
#define ST_TLMM_SDC1_CLK_HDRV_SHIFT   6u
#define ST_TLMM_SDC1_DATA_PULL_SHIFT  9u
#define ST_TLMM_SDC1_CMD_PULL_SHIFT   11u
#define ST_TLMM_SDC1_CLK_PULL_SHIFT   13u
#define ST_TLMM_SDC1_RCLK_PULL_SHIFT  15u
#define ST_TLMM_HDRV_WIDTH      3u           /* msm_tlmm_set_hdrive -> msm_tlmm_set_field(..., 3, ...) */
#define ST_TLMM_PULL_WIDTH      2u           /* msm_tlmm_set_pull  -> msm_tlmm_set_field(..., 2, ...) */

/* The board's own declaration, field by field. `pull-on` has four entries (clk, cmd, data, rclk) and
 * `drv-on` three (clk, cmd, data) - `sdhci-msm.c:1146` sets `pull_data->size = 4` and `:1189` the drv
 * size, which is the array shape the DT is parsed against. */
#define ST_TLMM_DT_PULL_ON_CLK  0u
#define ST_TLMM_DT_PULL_ON_CMD  3u
#define ST_TLMM_DT_PULL_ON_DATA 3u
#define ST_TLMM_DT_PULL_ON_RCLK 1u
#define ST_TLMM_DT_DRV_ON_CLK   4u
#define ST_TLMM_DT_DRV_ON_CMD   4u
#define ST_TLMM_DT_DRV_ON_DATA  4u

/* The same arithmetic the kernel performs, evaluated here so the comparison is published rather than
 * left in a reader's head: `reg |= (val & (2^w - 1)) << off`. */
#define ST_TLMM_FIELD(val, width, shift) \
    (((uint32_t)(val) & (((uint32_t)1u << (width)) - 1u)) << (shift))
#define ST_TLMM_SDC1_EXPECT \
    (ST_TLMM_FIELD(ST_TLMM_DT_DRV_ON_DATA,  ST_TLMM_HDRV_WIDTH, ST_TLMM_SDC1_DATA_HDRV_SHIFT) | \
     ST_TLMM_FIELD(ST_TLMM_DT_DRV_ON_CMD,   ST_TLMM_HDRV_WIDTH, ST_TLMM_SDC1_CMD_HDRV_SHIFT)  | \
     ST_TLMM_FIELD(ST_TLMM_DT_DRV_ON_CLK,   ST_TLMM_HDRV_WIDTH, ST_TLMM_SDC1_CLK_HDRV_SHIFT)  | \
     ST_TLMM_FIELD(ST_TLMM_DT_PULL_ON_DATA, ST_TLMM_PULL_WIDTH, ST_TLMM_SDC1_DATA_PULL_SHIFT) | \
     ST_TLMM_FIELD(ST_TLMM_DT_PULL_ON_CMD,  ST_TLMM_PULL_WIDTH, ST_TLMM_SDC1_CMD_PULL_SHIFT)  | \
     ST_TLMM_FIELD(ST_TLMM_DT_PULL_ON_CLK,  ST_TLMM_PULL_WIDTH, ST_TLMM_SDC1_CLK_PULL_SHIFT)  | \
     ST_TLMM_FIELD(ST_TLMM_DT_PULL_ON_RCLK, ST_TLMM_PULL_WIDTH, ST_TLMM_SDC1_RCLK_PULL_SHIFT))

/* The four field masks, each already shifted, so a decode is `(raw & MASK) >> SHIFT` and no reader has
 * to reconstruct a width. */
#define ST_TLMM_SDC1_DATA_HDRV_MASK  ST_TLMM_FIELD(((1u << ST_TLMM_HDRV_WIDTH) - 1u), ST_TLMM_HDRV_WIDTH, ST_TLMM_SDC1_DATA_HDRV_SHIFT)
#define ST_TLMM_SDC1_CMD_HDRV_MASK   ST_TLMM_FIELD(((1u << ST_TLMM_HDRV_WIDTH) - 1u), ST_TLMM_HDRV_WIDTH, ST_TLMM_SDC1_CMD_HDRV_SHIFT)
#define ST_TLMM_SDC1_CLK_HDRV_MASK   ST_TLMM_FIELD(((1u << ST_TLMM_HDRV_WIDTH) - 1u), ST_TLMM_HDRV_WIDTH, ST_TLMM_SDC1_CLK_HDRV_SHIFT)
#define ST_TLMM_SDC1_DATA_PULL_MASK  ST_TLMM_FIELD(((1u << ST_TLMM_PULL_WIDTH) - 1u), ST_TLMM_PULL_WIDTH, ST_TLMM_SDC1_DATA_PULL_SHIFT)
#define ST_TLMM_SDC1_CMD_PULL_MASK   ST_TLMM_FIELD(((1u << ST_TLMM_PULL_WIDTH) - 1u), ST_TLMM_PULL_WIDTH, ST_TLMM_SDC1_CMD_PULL_SHIFT)
#define ST_TLMM_SDC1_CLK_PULL_MASK   ST_TLMM_FIELD(((1u << ST_TLMM_PULL_WIDTH) - 1u), ST_TLMM_PULL_WIDTH, ST_TLMM_SDC1_CLK_PULL_SHIFT)
#define ST_TLMM_SDC1_RCLK_PULL_MASK  ST_TLMM_FIELD(((1u << ST_TLMM_PULL_WIDTH) - 1u), ST_TLMM_PULL_WIDTH, ST_TLMM_SDC1_RCLK_PULL_SHIFT)

/* **776: the seven masks OR-ed, which is every bit of the register a field owns.** Rung 28's store is
 * ONE read-modify-write over exactly this mask, so the bits outside it are the bits the READ handed
 * back and are written back unchanged - which is the whole difference between a field write and a
 * whole-word write to a register a bootloader may have owned. The `_Static_assert`s below are what
 * make that a property of the build rather than of this sentence: the seven are disjoint, so this OR
 * is also their arithmetic sum. */
#define ST_TLMM_SDC1_ALL_MASK \
    (ST_TLMM_SDC1_DATA_HDRV_MASK | ST_TLMM_SDC1_CMD_HDRV_MASK | ST_TLMM_SDC1_CLK_HDRV_MASK | \
     ST_TLMM_SDC1_DATA_PULL_MASK | ST_TLMM_SDC1_CMD_PULL_MASK | ST_TLMM_SDC1_CLK_PULL_MASK | \
     ST_TLMM_SDC1_RCLK_PULL_MASK)

/*
 * **704: the third block's surface, and the four names below are `clock-8974.c`'s own.** The file
 * already carried the BCR and one CBCR (`ST_SDCC1_BCR`/`ST_SDCC1_CBCR`, the pair the gate comes out of);
 * what rung 5 adds is the other three branches of the same controller and the **root clock generator**
 * their parent is, plus the one register on this path that is not a clock branch at all - all of it read
 * and none of it written, because the writer is `sdhci_msm_set_clock` and that is the next step
 * (`docs/experiments/experiment-704-...`, the pre-registration this rung is built from).
 *
 * **Why the AHB branch is the rung's open question.** `sdhci_msm_prepare_clocks` (`sdhci-msm.c:2315`)
 * enables `pclk` *and* `clk`, and `msm8974.dtsi`'s SDCC1 node binds `pclk` to this branch
 * (`SDCC1_AHB_CBCR`, `clock-8974.c:340`, `gcc_sdcc1_ahb_clk` at `:2339-2341`). **No run in this project
 * has ever read this register**, and the controller answers its register file today - which *suggests*
 * the branch is on, and is not the same statement. Its `branch_clk` is also the one that carries
 * `has_sibling = 1`, which the framework's own rule turns into `-EPERM` for `round_rate` and
 * `list_rate` (`clock-local2.c:444-446`, `:460-462`) - so enabling it is a different act from enabling
 * the apps branch and rung 6 will have to say so.
 */
#define ST_GCC_SDCC1_APPS_CBCR          0x04C4u   /* clock-8974.c:339 - the clk branch, and it is the
                                                  * SAME WORD the gate comes out of (`ST_SDCC1_CBCR`
                                                  * above): the gate is this branch's `BIT(0)`. The
                                                  * second name is 704's, kept because every rung-1..5
                                                  * reading is published under `_gate`/`_cbcr` and
                                                  * renaming those keys would move a record a press
                                                  * already answered */
#define ST_GCC_SDCC1_AHB_CBCR           0x04C8u   /* clock-8974.c:340 - the pclk branch */
#define ST_GCC_SDCC1_APPS_RCG           0x04D0u   /* clock-8974.c:140, :1584 - sdcc1_apps_clk_src's CMD_RCGR */
#define ST_GCC_SDCC1_CDCCAL_SLEEP_CBCR  0x04E4u   /* clock-8974.c:341 */
#define ST_GCC_SDCC1_CDCCAL_FF_CBCR     0x04E8u   /* clock-8974.c:342 */

/*
 * **The RCG's five words are four bytes apart and they are `clock-local2.c`'s own arithmetic**
 * (`:49-53`: `CMD_RCGR_REG(x) (*(x)->base + (x)->cmd_rcgr_reg)`, `CFG_RCGR_REG` `+0x4`, `M_REG` `+0x8`,
 * `N_REG` `+0xC`, `D_REG` `+0x10`), not an address anyone found in a table. All five are 4-aligned, so
 * they are 32-bit reads and 698's width census checks them for free.
 */
#define ST_RCG_CMD              0x00u
#define ST_RCG_CFG              0x04u
#define ST_RCG_M                0x08u
#define ST_RCG_N                0x0Cu
#define ST_RCG_D                0x10u

/*
 * **The bits, each quoted from the file that defines it.** The three CBCR bits are
 * `clock-local2.c:62-63` and `:67`; the BCR's one bit is `:66`; the RCG's four are `:61-65` and
 * `:68-71`. They are named here rather than shifted inline because §2 of the rung-5 pre-registration is
 * about exactly this: `CBCR_BRANCH_ENABLE_BIT` and `CBCR_BRANCH_OFF_BIT` are **two** bits about one
 * quantity, and this file carried only the first of them as "the gate" from 692 until 704.
 */
#define ST_CBCR_ENABLE_BIT      (1u << 0)     /* CBCR_BRANCH_ENABLE_BIT - what the driver WRITES */
#define ST_CBCR_OFF_BIT         (1u << 31)    /* CBCR_BRANCH_OFF_BIT    - what "running" means */
#define ST_CBCR_HW_CTL_BIT      (1u << 1)     /* CBCR_HW_CTL_BIT - set => the framework skips its halt check */
#define ST_BCR_ARES_BIT         (1u << 0)     /* BCR_BLK_ARES_BIT - the block reset, not on this path */
#define ST_RCG_ROOT_EN_BIT      (1u << 1)     /* CMD_RCGR_ROOT_ENABLE_BIT */
#define ST_RCG_UPDATE_BIT       (1u << 0)     /* CMD_RCGR_CONFIG_UPDATE_BIT - the bit rcg_update_config polls */
#define ST_RCG_ROOT_STATUS_BIT  (1u << 31)    /* CMD_RCGR_ROOT_STATUS_BIT */
#define ST_RCG_CFG_DIV_MASK     0x0000001Fu   /* CFG_RCGR_DIV_MASK, BM(4,0) */
#define ST_RCG_CFG_SRC_MASK     0x00000700u   /* CFG_RCGR_SRC_SEL_MASK, BM(10,8) */
#define ST_RCG_CFG_SRC_SHIFT    8u
#define ST_RCG_CFG_MND_MASK     0x00003000u   /* MND_MODE_MASK, BM(13,12) - 0x2 is the dual-edge value */

/*
 * **`CORE_VENDOR_SPEC 0x10C`, and the address is the part a reader has to be told about**
 * (`sdhci-msm.c:92`). It is a 4-aligned 32-bit register read with `st_read32`; the two fields below
 * are the only two the driver's own code touches (`:93-96`), and the MCLK select is the one it
 * writes on the non-HS400 path (`:2466-2494`). Rung 5 reads both and writes neither, which is what
 * makes them rung 6's before-values.
 *
 * **710 CORRECTED THE WINDOW THIS MACRO IS APPLIED TO, and the correction is a finding about the
 * record and not about the device.** Every one of the vendor's **23** uses of this offset goes
 * through `host->ioaddr` (`:587`, `:597`, `:646`, `:653`, `:2079-2085`, `:2416-2433`, `:2490-2512`)
 * and **none** through `msm_host->core_mem` - so the register `sdhci_msm_set_clock` writes is
 * `hc_mem (0xf9824900) + 0x10C` = **`0xF9824A0C`**, inside the standard file's own vendor-specific
 * area, and the `0xF982410C` this file has read since 705 is the *other* window of the same
 * controller. 707 section 2's "the field did not take" is therefore an address error: the two
 * stores landed, and 707's readings are about a register the driver never addresses. (Rung 8 reads
 * both addresses in one run and settles whether they are two registers or one; until that reading,
 * no rung writes the MCLK field at the vendor's address.)
 *
 * **The macro's own NAME was the false claim, so the name changed rather than the comment alone.**
 * It was `ST_CORE_VENDOR_SPEC`, and "CORE" asserted the window in the identifier - the same class of
 * error as a comment, in the one place a reader trusts without reading (`mi4-a-claim-in-a-comment-is
 * -not-a-check`). It is `ST_VENDOR_SPEC` now: the offset is the vendor's, the *window* is the call
 * site's, and each of the six sites below says which one it means.
 */
#define ST_VENDOR_SPEC     0x10Cu        /* sdhci-msm.c:92 - `VENDOR_SPEC_FUNC`, the offset; hc_mem's 0x10C is the driver's */
#define ST_VENDOR_PWRSAVE_BIT   (1u << 1)     /* CORE_CLK_PWRSAVE, :93 */
#define ST_VENDOR_MCLK_MASK     0x00000300u   /* CORE_HC_MCLK_SEL_MASK, :96 - BM(9,8) */
#define ST_VENDOR_MCLK_SHIFT    8u

/*
 * **696: the vendor's sequence, and every offset below is a `#define` in the same file it is read out
 * of (`sdhci-msm.c:55-60`) rather than retyped.** `ST_HC_MODE_EN` and `ST_SDCC1_CLK_ENABLE` are the
 * same number and it is the same *bit position* - bit 0 - of two different registers: one selects
 * SDHCI mode in `CORE_HC_MODE` and the other enables the branch's clock in the CBCR. They are kept as
 * two names because the registers are two and neither bit's meaning follows from the other's, and the
 * arm's own pair of readings says the two are independent: the gate reads `0x00004ff1` (bit 0 set)
 * while `_hc_mode` reads bit 0 clear.
 */
#define ST_HC_MODE_EN           0x1u
#define ST_CORE_SW_RST          (1u << 7)
#define ST_FF_CLK_SW_RST_DIS    (1u << 13)
#define ST_CORE_PWRCTL_STATUS   0xDCu

/*
 * **The poll's bounds, and the sentence that must be read beside them: a bound cannot bound a read that
 * never returns.** A load from a block whose clock is off is a bus wait nothing ends on this SoC's
 * fabrics, so if the gate reading were wrong the *first* `CORE_POWER` read inside the poll would hang
 * and no number here would end it - which is why the sequence runs only with `gate == 1`, on a pair of
 * gate words 694 measured on hardware rather than on the offset's arithmetic. What the bounds do
 * address is the case they can: a reset that does not complete. The vendor polls to a 1 ms ceiling
 * (`readl_poll_timeout(..., 10, 1000)`, against a reset its own comment sizes at ~40 us); this ceiling
 * is 100x that, 1/20 of the fixture's 2000 ms park and 1/60 of this arm's 6 s ending, and it is a
 * *time* rather than a count because the rate is measured - `xnu_live_post_cntfrq` read 19,200,000 Hz
 * on 691's and 694's runs, which is 19200 ticks/ms.
 */
#define ST_MODE_RST_TICK_BUDGET 1920000u  /* 100 ms at 19.2 MHz */
#define ST_MODE_RST_STEPS_MAX   4096u     /* the backstop: ~126 ms of reads, so it cannot fire first */
#define ST_MODE_RST_INNER       1024u     /* device reads between two samples of the clock */
/*
 * **701: the driver's own poll bound, and it is the vendor's number rather than a new one.**
 * `sdhci.c:251-252` is the comment 'Wait max 100 ms' over `timeout = 100`, and the loop
 * `mdelay(1)`s per decrement (`:267`), so the driver's bound is **100 ms of wall clock** - the same
 * quantity `ST_MODE_RST_TICK_BUDGET` is, at the same 19,200,000 Hz 699's ending read out of `cntfrq`.
 * The arm's bound and the driver's bound being the same number is the point: a run that times out here
 * is a run in which the driver's own `Reset 0x%x never completed` path (`:260-264`) would have fired.
 */
#define ST_RST_TICK_BUDGET 1920000u  /* 100 ms at 19.2 MHz */
#define ST_RST_STEPS_MAX   4096u     /* the backstop: ~126 ms of byte reads, so it cannot fire first */
#define ST_RST_INNER       1024u     /* byte reads between two samples of the clock */

static uint32_t st_read32(uint32_t addr)
{
    return *(volatile uint32_t *)(uintptr_t)addr;
}

/*
 * **698: the two narrower widths, and they exist because the register file is not uniform.** The four
 * offsets they reach (`0x28`, `0x29`, `0x2F` byte; `0x2C`, `0xFC`, `0xFE` halfword) are read that way by
 * the vendor's own accessors (`sdhci.c:98-128`), and the reason is not style: an unaligned access to a
 * Strongly-ordered device section **faults on ARMv7**, so a 32-bit read of `0x29` or of `0xFE` would die
 * on the bench exactly where 692 died - by alignment this time rather than by translation. The `ldrb`/
 * `ldrh` these compile to are the whole of the arm's protection here, and `build_entry.sh`'s alignment
 * census refuses the build if any access in this body breaks the rule.
 */
static uint8_t st_read8(uint32_t addr)
{
    return *(volatile uint8_t *)(uintptr_t)addr;
}

static uint16_t st_read16(uint32_t addr)
{
    return *(volatile uint16_t *)(uintptr_t)addr;
}

/*
 * **The store, and the `dsb sy` is what makes "the readback was taken after the store" a property of
 * this code rather than of the bus.** The vendor uses `writel_relaxed`/`readl_relaxed` and relies on
 * the interconnect; this image has one barrier already written down in the same shape (`entry_irq.c`'s
 * write helper) and the project's own reset path is specified as a store followed by `dsb sy`, so the
 * device write here is that shape too. Ten of them per run at most - the vendor's mode sequence's four,
 * then rung 6's four branch enables and two CORE_VENDOR_SPEC read-modify-writes - beside the byte store
 * of the reset and the two CLOCK_CONTROL halfwords, and every one is a read-modify-write (or an
 * already-set bit's own value) of a field the vendor's own driver writes, against a block whose clock
 * this arm has already read as enabled.
 */
static void st_write32(uint32_t addr, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)addr = value;
    __asm__ volatile ("dsb sy" ::: "memory");
}

#if STAGE90_XNU_STORAGE_PROBE >= 4
/*
 * **701: the byte store, and rung 4 is the reason it exists.** `SOFTWARE_RESET 0x2F` is a byte register
 * at an offset no 4-byte access reaches, so the driver's reset cannot be written with `st_write32` - a
 * 32-bit store there is the unaligned Device access the alignment clause refuses the build over. The
 * `dsb sy` is in this helper for the same reason it is in the one above: the poll that follows reads
 * the byte just written, and the barrier is what makes "the poll was taken after the store" a property
 * of this code rather than of the bus.
 */
static void st_write8(uint32_t addr, uint8_t value)
{
    *(volatile uint8_t *)(uintptr_t)addr = value;
    __asm__ volatile ("dsb sy" ::: "memory");
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 4 - the helper has one caller, and -Werror is on */

static uint32_t g_storage_probed;

#if STAGE90_XNU_STORAGE_PROBE >= 2
/*
 * **698: the rung-3 census's interlock, and it is the sequence's own outcome rather than a second
 * decision.** The census may only read a register file that is in SDHCI mode, and `_mode_stage == 8`
 * is what says the block reached that state. A flag rather than a re-read of `CORE_HC_MODE` because a
 * re-read could observe a word the *sequence* did not write, and the census's premise is the sequence's
 * result and not the register's history.
 */
static uint32_t g_storage_mode_complete;
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 2
/*
 * **696: rung 2 - the vendor's mode sequence, and it is this project's first write to a device block.**
 * `sdhci-msm.c:2841-2868` is the whole of it, and the four stores below are that text in order: write 0
 * to `CORE_HC_MODE`, set `CORE_SW_RST` in `CORE_POWER`, poll the bit clear, write `HC_MODE_EN`, then
 * set `FF_CLK_SW_RST_DIS`. It reads the register back after every store, so the log carries what the
 * block answered rather than what this code asked for, and the count it publishes
 * (`xnu_live_storage_writes`) is the count of stores it actually made.
 *
 * **The state this arm starts from says the sequence's ends matter.** The handed-over `_hc_mode` reads
 * `0x00002000` = `FF_CLK_SW_RST_DIS` with `HC_MODE_EN` clear, i.e. the vendor's own destination one bit
 * short. So store 1 (which writes 0) *clears* bit 13, and store 4 is what puts it back: an arm that
 * skipped the ends would leave the block worse than it found it.
 *
 * **The one guard this adds to the gate's.** A block that answered `CORE_MCI_VERSION` with 0 or with a
 * saturated word is a block that is not answering, and the sequence's second store is to `CORE_POWER`.
 * So the version word is the condition of the whole sequence: `_mode_refused = 1` with `_writes = 0` is
 * reached without a single store, and it is the only cell in which this arm changes nothing.
 *
 * **What it deliberately does not touch.** `POWER_CONTROL 0x29` (the SDHCI standard's own power
 * register, in `hc_mem`) - writing 0 to it is a bus-off request on this SoC, and the bus is already
 * powered. And `CORE_PWRCTL_CLEAR 0xE4`: the vendor acknowledges the power-IRQ status there because it
 * is about to register a handler, and this arm registers none, so the acknowledge would be a write with
 * no purpose. The status itself is *read* (`_mode_pwrctl_status`) and left latched - it is the reading
 * that says whether the reset latched anything, which the vendor's own comment predicts when the
 * previous power state was BUS_ON, as `_core_power = 0x441` says it was.
 */
static void st_mode_sequence(uint32_t core_power, uint32_t mci_version)
{
    uint32_t w0, w1, w2, pwr, pwr_wr, polls = 0u, steps = 0u, cleared = 0u, i;
    uint32_t t0, ticks;

    ST_LIVE("xnu_live_storage_mode_calls", 1u);

    if (mci_version == 0u || mci_version == 0xffffffffu) {
        ST_LIVE("xnu_live_storage_mode_refused", 1u);
        ST_LIVE("xnu_live_storage_mode_stage", 0u);
        ST_LIVE("xnu_live_storage_writes", 0u);
        return;
    }
    ST_LIVE("xnu_live_storage_mode_refused", 0u);

    /* Store 1: the vendor's `writel_relaxed(0, core_mem + CORE_HC_MODE)`. */
    ST_LIVE("xnu_live_storage_mode_stage", 1u);
    st_write32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE, 0u);
    ST_LIVE("xnu_live_storage_writes", 1u);
    w0 = st_read32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE);
    ST_LIVE("xnu_live_storage_mode_w0_read", w0);

    /* Store 2: `CORE_POWER |= CORE_SW_RST`. */
    ST_LIVE("xnu_live_storage_mode_stage", 2u);
    pwr_wr = core_power | ST_CORE_SW_RST;
    st_write32(ST_CORE_MEM_BASE + ST_CORE_POWER, pwr_wr);
    ST_LIVE("xnu_live_storage_writes", 2u);
    ST_LIVE("xnu_live_storage_mode_power_wr", pwr_wr);

    /*
     * The poll, and both of its bounds are published with the result. The clock is sampled once per
     * `ST_MODE_RST_INNER` device reads, which is what keeps the sampler from being the thing being
     * measured; `steps` reaching its ceiling and `ticks` reaching its budget are two different
     * statements and the log carries both.
     */
    ST_LIVE("xnu_live_storage_mode_stage", 3u);
    t0 = (uint32_t)stage90_cntvct_read();
    for (steps = 0u; steps < ST_MODE_RST_STEPS_MAX; steps++) {
        for (i = 0u; i < ST_MODE_RST_INNER; i++) {
            pwr = st_read32(ST_CORE_MEM_BASE + ST_CORE_POWER);
            polls++;
            if ((pwr & ST_CORE_SW_RST) == 0u) {
                cleared = 1u;
                break;
            }
        }
        if (cleared != 0u)
            break;
        if ((uint32_t)stage90_cntvct_read() - t0 >= ST_MODE_RST_TICK_BUDGET)
            break;
    }
    ticks = (uint32_t)stage90_cntvct_read() - t0;

    ST_LIVE("xnu_live_storage_mode_rst_polls", polls);
    ST_LIVE("xnu_live_storage_mode_rst_steps", steps);
    ST_LIVE("xnu_live_storage_mode_rst_ticks", ticks);
    ST_LIVE("xnu_live_storage_mode_rst_cleared", cleared);
    ST_LIVE("xnu_live_storage_mode_timeout", (cleared == 0u) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_mode_pwrctl_status",
            st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_STATUS));

    if (cleared == 0u) {
        /*
         * The failure path, and its shape is "no further store": the block is left exactly as stores 1
         * and 2 left it, and `_mode_w0_read` and `_mode_power_wr` above say what that is.
         */
        ST_LIVE("xnu_live_storage_mode_stage", 4u);
        ST_LIVE("xnu_live_storage_writes", 2u);
        return;
    }

    /* Store 3: the vendor's `writel_relaxed(HC_MODE_EN, core_mem + CORE_HC_MODE)`. */
    ST_LIVE("xnu_live_storage_mode_stage", 5u);
    st_write32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE, ST_HC_MODE_EN);
    ST_LIVE("xnu_live_storage_writes", 3u);
    w1 = st_read32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE);
    ST_LIVE("xnu_live_storage_mode_w1_read", w1);

    /* Store 4: the vendor's read-modify-write that ends on `FF_CLK_SW_RST_DIS`. */
    ST_LIVE("xnu_live_storage_mode_stage", 6u);
    st_write32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE, w1 | ST_FF_CLK_SW_RST_DIS);
    ST_LIVE("xnu_live_storage_writes", 4u);
    w2 = st_read32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE);
    ST_LIVE("xnu_live_storage_mode_w2_read", w2);
    ST_LIVE("xnu_live_storage_mode_bit_after", w2 & ST_HC_MODE_EN);

    /*
     * **And the standard words again, now through a block in SDHCI mode.** 694's pair
     * (`0x10`, `0x742dc8b2`) was taken through a block that was *not*, and 694 section 3 says why that
     * makes it a pre-mode pair rather than a spec-valid one. Read here, one store-group later, the two
     * pairs become the reading the arm exists for.
     *
     * **698 corrected the first of the two names and added the third read.** `hc_mem + 0x00` is
     * `SDHCI_DMA_ADDRESS` (`sdhci.h:27`) and not a version register, which 697 measured rather than
     * argued: the word went `0x10 -> 0x00` across the sequence - a *soft* register the core reset clears
     * - while `0x40`'s strapping constant did not move. So the key is renamed to what it reads and
     * `HOST_VERSION 0xFE` (`sdhci.h:241`, read by `sdhci-msm.c:2909`) is read beside it at its own
     * width: **that is the cell 697's pre-registration left owed**, and it is the first spec-valid
     * statement about this register file this project can make. `SDHCI_VENDOR_VER_MASK 0xFF00` /
     * `SDHCI_SPEC_VER_MASK 0x00FF` (`sdhci.h:242-245`) split the word.
     */
    ST_LIVE("xnu_live_storage_mode_stage", 7u);
    ST_LIVE("xnu_live_storage_mode_dma_address", st_read32(ST_HC_MEM_BASE + ST_SDHCI_DMA_ADDRESS));
    ST_LIVE("xnu_live_storage_mode_capabilities", st_read32(ST_HC_MEM_BASE + ST_SDHCI_CAPABILITIES));
    ST_LIVE("xnu_live_storage_mode_host_version", st_read16(ST_HC_MEM_BASE + ST_SDHCI_HOST_VERSION));
    ST_LIVE("xnu_live_storage_mode_stage", 8u);
    /* The whole sequence ran: this is what the rung-3 census is conditional on. */
    g_storage_mode_complete = 1u;
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 2 */

#if STAGE90_XNU_STORAGE_PROBE >= 3
/*
 * **698: rung 3 - the standard register file's census, and it is the before-value arm.**
 *
 * Every key this project has published about `hc_mem` until now is one of two *words* (`0x00` and
 * `0x40`). The registers a driver's bring-up actually reads and writes - `PRESENT_STATE`,
 * `HOST_CONTROL`, `POWER_CONTROL`, `CLOCK_CONTROL`, `SOFTWARE_RESET`, `SLOT_INT_STATUS` - have never
 * been read, and the next act on this path is `sdhci_reset(SDHCI_RESET_ALL)`, which touches four of
 * them (`sdhci.c:246` writes `SOFTWARE_RESET`, `:250` clears the driver's own `clock`, and the restore
 * path reads and re-writes `HOST_CONTROL`). **A before-value can only be taken before**, so this rung
 * exists between the mode sequence and the reset, and it adds no store: the store census in
 * `build_entry.sh` asserts exactly the four the vendor's sequence makes and passes unchanged here,
 * which is itself the proof that a read-only rung wrote nothing new.
 *
 * **The two registers this rung is most careful about.**
 *
 *   * `POWER_CONTROL 0x29` is **read and never written**, and 696 section 2's reason for not reading it
 *     ("a load from an address whose meaning this arm is not going to act on is a load that can only
 *     fault") no longer holds: the window is proven twice over and the value is now decision-relevant,
 *     because `sdhci.c:1342`/`:1353` are `sdhci_writeb(host, 0, SDHCI_POWER_CONTROL)` - the generic
 *     SDHCI core turns the bus off by writing 0 to this byte, which on this SoC **is** a bus-off
 *     request (531 section 8). Whether `SDHCI_RESET_ALL` clears it is the one hazard the next step
 *     carries, and this key is where the answer starts.
 *   * `CARD_PRESENT` (bit 16 of `PRESENT_STATE`, `sdhci.h:71`) **must not be read as "no card"**:
 *     `sdhci-msm.c:2896` sets `SDHCI_QUIRK_BROKEN_CARD_DETECTION`, the DT describes a soldered eMMC
 *     (`qcom,bus-width = <8>`), and detection on this board is a GPIO. The key is published with the
 *     bit named so that a reader cannot make that mistake quietly, and the census publishes the whole
 *     word so the other bits are readable without a second press.
 *
 * **The count is bottom-up.** `_reg_loads` is incremented at each read rather than written down, so the
 * record's "ten reads" and the log's number are two derivations of one fact (m688's rule: a table of
 * things-to-count is a scope claim unless the counter is the table's own).
 */
static void st_standard_census(void)
{
    uint32_t loads = 0u;
    uint8_t host_control, power_control, software_reset;
    uint16_t clock_control, slot_int_status;
    uint32_t present_state, capabilities_1, max_current, pwrctl_mask, pwrctl_ctl;

    /*
     * `SOFTWARE_RESET 0x2F` first among the bytes: it is self-clearing, so a non-zero read would mean a
     * reset is stuck in progress - and that would falsify 696's and 697's "no reset ran" rather than
     * anything this arm assumes. Read first so that the reading is in the log before the rest.
     */
    software_reset = st_read8(ST_HC_MEM_BASE + ST_SDHCI_SOFTWARE_RESET);
    ST_LIVE("xnu_live_storage_reg_software_reset", (uint32_t)software_reset);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    power_control = st_read8(ST_HC_MEM_BASE + ST_SDHCI_POWER_CONTROL);
    ST_LIVE("xnu_live_storage_reg_power_control", (uint32_t)power_control);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    host_control = st_read8(ST_HC_MEM_BASE + ST_SDHCI_HOST_CONTROL);
    ST_LIVE("xnu_live_storage_reg_host_control", (uint32_t)host_control);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    clock_control = st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL);
    ST_LIVE("xnu_live_storage_reg_clock_control", (uint32_t)clock_control);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    slot_int_status = st_read16(ST_HC_MEM_BASE + ST_SDHCI_SLOT_INT_STAT);
    ST_LIVE("xnu_live_storage_reg_slot_int_status", (uint32_t)slot_int_status);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    present_state = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
    ST_LIVE("xnu_live_storage_reg_present_state", present_state);
    ST_LIVE("xnu_live_storage_reg_card_present", present_state & ST_SDHCI_CARD_PRESENT);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    capabilities_1 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_CAPABILITIES_1);
    ST_LIVE("xnu_live_storage_reg_capabilities_1", capabilities_1);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    max_current = st_read32(ST_HC_MEM_BASE + ST_SDHCI_MAX_CURRENT);
    ST_LIVE("xnu_live_storage_reg_max_current", max_current);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    /*
     * **The other half of 696 section 3's hazard, and the before-value for the vendor's own
     * acknowledge.** 697 measured the power-IRQ *status* (0, nothing latched); the mask says whether a
     * power IRQ would have been *routed* at all, which a status register cannot answer. `CTL` is what
     * the vendor reads before it writes the success bits back (`:2879` reads, `:2884` writes, `:2069`
     * writes a byte at one site) - so it is the register the next step's acknowledge would change.
     */
    pwrctl_mask = st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_MASK);
    ST_LIVE("xnu_live_storage_reg_pwrctl_mask", pwrctl_mask);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    pwrctl_ctl = st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_CTL);
    ST_LIVE("xnu_live_storage_reg_pwrctl_ctl", pwrctl_ctl);
    ST_LIVE("xnu_live_storage_reg_loads", ++loads);

    ST_LIVE("xnu_live_storage_regs_done", loads);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 3 */

#if STAGE90_XNU_STORAGE_PROBE >= 4
/*
 * **701: rung 4 - the driver's own reset, and it is this project's first store through `hc_mem`.**
 *
 * Every store this project has made to a device so far is on `core_mem` and is one of the vendor's four
 * mode-sequence stores. The driver's own bring-up begins one block over, with
 * `sdhci_reset(host, SDHCI_RESET_ALL)` - and read out of the vendor's source (`sdhci.c:229-279`) that
 * function is **one byte write and a bounded poll**, because everything else in it is guarded off by
 * state a freshly initialized host does not have:
 *
 *   * `sdhci_writeb(host, mask, SDHCI_SOFTWARE_RESET)` (`:246`) - `0x01` to `hc_mem + 0x2F`. **This is
 *     the rung's whole store.**
 *   * `host->clock = 0` (`:249`) - the driver's own variable, no register.
 *   * `check_power_status(host, REQ_BUS_OFF)` (`:254-256`) - skipped because `host->pwr` is 0, and it is
 *     the guard that matters most: `sdhci_msm_check_power_status` (`sdhci-msm.c:2179-2209`) writes no
 *     register at all and ends in `wait_for_completion(&msm_host->pwr_irq_completion)` - **an unbounded
 *     wait on a power IRQ**. A payload that "finished the driver's init" by emulating it would be waiting
 *     for an interrupt nothing in this image raises. Rung 4 therefore stops short of it *deliberately*.
 *   * `platform_reset_enter`/`_exit` (`:243`, `:271`) - `sdhci_msm_ops` (`sdhci-msm.c:2653-2666`) has no
 *     such member, so the MSM does not hook this reset beyond the power-status check it cannot reach.
 *   * the poll (`:259-268`) - `sdhci_readb(SDHCI_SOFTWARE_RESET) & mask`, up to 100 x `mdelay(1)`, with
 *     the driver's own `Reset 0x%x never completed` path (`:260-264`) if the bound expires. **This rung
 *     polls the same byte with the same 100 ms bound**, published as ticks on this machine's own counter.
 *
 * **And the clocks are not on this path either** (701 section 2): `sdhci_msm_ops` replaces `.set_clock`,
 * and `sdhci_msm_set_clock` (`:2402-2535`) writes `CORE_VENDOR_SPEC 0x10C` - a fifth `core_mem` offset -
 * and calls `clk_set_rate` (`:2520`) on the GCC's SDCC clocks, i.e. the `0xfc400000` megabyte 692 pressed
 * and measured as **not mapped**. So this rung sets no clock, and `CLOCK_CONTROL` is read before and
 * after only as a before/after pair of a register this rung does not write.
 *
 * **The before-values are 699's, which is what makes the reset's effect a measurement**: `_reg_software_reset`
 * and `_reg_power_control` both read 0, `_reg_present_state`'s inhibit bits are clear, and
 * `_reg_pwrctl_mask` reads `0x0000000f` - four bits of power IRQ routed, which is what makes
 * `_reg_pwrctl_status_after` below a reading and not a formality (696 section 3 warned the reset may latch
 * a power-IRQ status when the previous state was `BUS_ON`, and 699 measured `_core_power=0x00000441`).
 *
 * **The guard is the register's own contract.** `SOFTWARE_RESET` is self-clearing, so a byte that reads
 * non-zero at entry is a reset already in progress - the one state in which writing this byte would be
 * acting on a block that is mid-reset. 699 measured 0, so that branch is the refusal path and not the
 * expected one, and it is published rather than silent.
 */
static void st_driver_reset(void)
{
    uint32_t t0, ticks, polls = 0u, steps = 0u, stores = 0u, i, cleared = 0u, done;
    uint8_t reset_before;

    ST_LIVE("xnu_live_storage_rst_calls", 1u);

    reset_before = st_read8(ST_HC_MEM_BASE + ST_SDHCI_SOFTWARE_RESET);
    ST_LIVE("xnu_live_storage_rst_before", reset_before);
    if ((reset_before & ST_SDHCI_RESET_ALL) != 0u) {
        ST_LIVE("xnu_live_storage_rst_refused", 1u);
        ST_LIVE("xnu_live_storage_rst_wrote", 0u);
        ST_LIVE("xnu_live_storage_rst_stores", 0u);
        ST_LIVE("xnu_live_storage_rst_stage", 0u);
        return;
    }
    ST_LIVE("xnu_live_storage_rst_refused", 0u);

    /* The store: the driver's own `sdhci_writeb(host, SDHCI_RESET_ALL, SDHCI_SOFTWARE_RESET)`. */
    ST_LIVE("xnu_live_storage_rst_stage", 1u);
    st_write8(ST_HC_MEM_BASE + ST_SDHCI_SOFTWARE_RESET, (uint8_t)ST_SDHCI_RESET_ALL);
    stores++;
    ST_LIVE("xnu_live_storage_rst_wrote", 1u);
    ST_LIVE("xnu_live_storage_rst_stores", stores);

    /*
     * The poll, and both of its bounds are published with the result - the same shape as the mode
     * sequence's poll, and for the same reason: the clock is sampled once per `ST_RST_INNER` byte reads,
     * which keeps the sampler from being the thing being measured, and `steps` reaching its ceiling,
     * `ticks` reaching its budget and the bit clearing are three different statements.
     */
    ST_LIVE("xnu_live_storage_rst_stage", 2u);
    t0 = (uint32_t)stage90_cntvct_read();
    for (steps = 0u; steps < ST_RST_STEPS_MAX; steps++) {
        for (i = 0u; i < ST_RST_INNER; i++) {
            polls++;
            if ((st_read8(ST_HC_MEM_BASE + ST_SDHCI_SOFTWARE_RESET) & ST_SDHCI_RESET_ALL) == 0u) {
                cleared = 1u;
                break;
            }
        }
        if (cleared != 0u)
            break;
        if ((uint32_t)stage90_cntvct_read() - t0 >= ST_RST_TICK_BUDGET)
            break;
    }
    ticks = (uint32_t)stage90_cntvct_read() - t0;

    ST_LIVE("xnu_live_storage_rst_polls", polls);
    ST_LIVE("xnu_live_storage_rst_steps", steps);
    ST_LIVE("xnu_live_storage_rst_ticks", ticks);
    ST_LIVE("xnu_live_storage_rst_cleared", cleared);
    ST_LIVE("xnu_live_storage_rst_timeout", (cleared == 0u) ? 1u : 0u);

    /*
     * **The after-values, and the first of them is the cell 698 section 2 owed.** `POWER_CONTROL 0x29`
     * is read here and still never written: a value with bit 0 set after the reset would mean the
     * standard reset *changed the block's belief about the bus*, and the driver would then have to
     * reconcile that with `CORE_PWRCTL`; a 0 says the reset left it exactly as 699 found it. The other
     * three are the registers the *generic* core's reset path can touch on other platforms
     * (`HOST_CONTROL` is restored under a quirk this SoC does not set; the inhibit bits say whether a
     * transfer was aborted), so reading them makes "this reset moved one register" a measurement.
     */
    ST_LIVE("xnu_live_storage_reg_software_reset_after", st_read8(ST_HC_MEM_BASE + ST_SDHCI_SOFTWARE_RESET));
    ST_LIVE("xnu_live_storage_reg_power_control_after", st_read8(ST_HC_MEM_BASE + ST_SDHCI_POWER_CONTROL));
    ST_LIVE("xnu_live_storage_reg_pwrctl_status_after", st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_STATUS));
    ST_LIVE("xnu_live_storage_reg_host_control_after", st_read8(ST_HC_MEM_BASE + ST_SDHCI_HOST_CONTROL));
    ST_LIVE("xnu_live_storage_reg_present_state_after", st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));

    done = 1u;
    ST_LIVE("xnu_live_storage_rst_done", done);
    ST_LIVE("xnu_live_storage_rst_stage", 3u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 4 */

#if STAGE90_XNU_STORAGE_PROBE >= 5
/*
 * **704: rung 5 - the clock surface, read and never written.** The rung-5 pre-registration
 * (`docs/experiments/experiment-704-...`) is the design and §2 of it is the reason this function
 * decomposes the gate into three bits instead of reusing the one the probe already has.
 *
 * **The defect this rung exists to correct, in one line.** `entry_storage_probe` has guarded this whole
 * line since 692 with
 *
 *     gate = cbcr & ST_SDCC1_CLK_ENABLE;              // ST_SDCC1_CLK_ENABLE is BIT(0)
 *
 * and `BIT(0)` of a CBCR is `CBCR_BRANCH_ENABLE_BIT` - **the bit the driver writes**, i.e. the *request*
 * (`clock-local2.c:62`, written at `:380-382`). What "the clock is running" means is a second bit two
 * lines away, `CBCR_BRANCH_OFF_BIT` = `BIT(31)` (`:63`), which the framework then *polls* to a bound
 * before it believes the clock is on (`:386-387` -> `branch_clk_halt_check`, `:333-360`) and which its
 * own handoff test reads **alone**, without looking at `BIT(0)` at all (`branch_clk_handoff`, `:490-497`:
 * `BIT(31)` set is `HANDOFF_DISABLED_CLK`). **The two can disagree**, and the ordinary state in which
 * they do is a branch whose enable is requested while the root above it is off - the clock has not
 * propagated, and a read through it is the bus wait nothing ends. 694's `_cbcr = 0x00004ff1` satisfies
 * both halves, so nothing measured so far is overturned; what is corrected is that the guard was reading
 * **half of itself**, and this rung publishes both halves for all four branches.
 *
 * **Why this rung writes nothing, stated as a reading and not as a comment.** The rung's whole claim is
 * that these registers can be read at all and what they say; the writer is `sdhci_msm_set_clock`, whose
 * first write would be a read-modify-write of a field on a branch whose halt state this run has never
 * read. `_clk_writes` is published as 0 on every path, and the store census in `build_entry.sh` refuses
 * a build in which this window holds any store at any rung - so the sentence "this rung does not write
 * the clock" is a property of the linked image rather than of this paragraph.
 *
 * **What it does not do**: no rate. The MND and source-select *fields* are published, and converting
 * them to Hz needs the parent's rate (`gpll0`/`gpll4`/`cxo`, `clock-8974.c:1564-1574`), which is a table
 * this image does not carry. Publishing a computed frequency beside the framework's own cached
 * `msm_host->clk_rate` would be the same one-value-two-definitions defect one level up.
 */
static void st_clock_census(void)
{
    uint32_t loads = 0u;
    uint32_t bcr, apps, ahb, cd_sleep, cd_ff;
    uint32_t rcg_cmd, rcg_cfg, rcg_m, rcg_n, rcg_d;
    uint32_t vendor;

    ST_LIVE("xnu_live_storage_clk_calls", 1u);
    ST_LIVE("xnu_live_storage_clk_writes", 0u);

    /*
     * **The block reset word, and the bit 694 published as a number.** `_bcr` has been in the log since
     * 694 without a key saying what a bit of it means; `BCR_BLK_ARES_BIT` (`clock-local2.c:66`) is the
     * only one that matters here, and `ares = 0` is what makes the branch readings below mean "the clock
     * is off" rather than "the block is held in reset and nothing it says is about anything".
     */
    bcr = st_read32(ST_GCC_BASE + ST_SDCC1_BCR);
    loads++;
    ST_LIVE("xnu_live_storage_clk_bcr", bcr);
    ST_LIVE("xnu_live_storage_clk_bcr_ares", (bcr & ST_BCR_ARES_BIT) ? 1u : 0u);

    /*
     * **The four branches, each as a word and as the three bits the framework itself distinguishes.**
     * `en` is the request, `off` is the halt state, and `hw` is `CBCR_HW_CTL_BIT` (`:67`) - set means the
     * branch is under hardware gating, and the framework skips its own halt check there (`:346-347`), so
     * an `off` bit is not consulted in that mode and a reader has to see it to know that.
     */
    apps = st_read32(ST_GCC_BASE + ST_SDCC1_CBCR);
    loads++;
    ST_LIVE("xnu_live_storage_clk_apps_cbcr", apps);
    ST_LIVE("xnu_live_storage_clk_apps_en", (apps & ST_CBCR_ENABLE_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_apps_off", (apps & ST_CBCR_OFF_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_apps_hw", (apps & ST_CBCR_HW_CTL_BIT) ? 1u : 0u);

    ahb = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_AHB_CBCR);
    loads++;
    ST_LIVE("xnu_live_storage_clk_ahb_cbcr", ahb);
    ST_LIVE("xnu_live_storage_clk_ahb_en", (ahb & ST_CBCR_ENABLE_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_ahb_off", (ahb & ST_CBCR_OFF_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_ahb_hw", (ahb & ST_CBCR_HW_CTL_BIT) ? 1u : 0u);

    cd_sleep = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_CDCCAL_SLEEP_CBCR);
    loads++;
    ST_LIVE("xnu_live_storage_clk_cdccal_sleep_cbcr", cd_sleep);
    ST_LIVE("xnu_live_storage_clk_cdccal_sleep_en", (cd_sleep & ST_CBCR_ENABLE_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_cdccal_sleep_off", (cd_sleep & ST_CBCR_OFF_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_cdccal_sleep_hw", (cd_sleep & ST_CBCR_HW_CTL_BIT) ? 1u : 0u);

    cd_ff = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_CDCCAL_FF_CBCR);
    loads++;
    ST_LIVE("xnu_live_storage_clk_cdccal_ff_cbcr", cd_ff);
    ST_LIVE("xnu_live_storage_clk_cdccal_ff_en", (cd_ff & ST_CBCR_ENABLE_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_cdccal_ff_off", (cd_ff & ST_CBCR_OFF_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_cdccal_ff_hw", (cd_ff & ST_CBCR_HW_CTL_BIT) ? 1u : 0u);

    /*
     * **The root clock generator the apps branch hangs off, and `_rcg_update` is the premise of the
     * poll the next rung will run.** `rcg_update_config` (`clock-local2.c:90-108`) sets
     * `CMD_RCGR_CONFIG_UPDATE_BIT` and then polls *that same bit* clear to a bound of
     * `UPDATE_CHECK_MAX_LOOPS 500` (`:44`) - so a `_rcg_update = 0` before anything writes is what makes
     * "a bit that does not clear afterwards is a failed update" a reading rather than an assumption.
     * Read in the same order the poll would find them: command, then configuration, then the three MND
     * words, which are `set_rate_mnd`'s own inputs (`:126-146`) written **before** the config word.
     */
    rcg_cmd = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_APPS_RCG + ST_RCG_CMD);
    loads++;
    ST_LIVE("xnu_live_storage_clk_rcg_cmd", rcg_cmd);
    ST_LIVE("xnu_live_storage_clk_rcg_root_en", (rcg_cmd & ST_RCG_ROOT_EN_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_rcg_update", (rcg_cmd & ST_RCG_UPDATE_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_rcg_root_status", (rcg_cmd & ST_RCG_ROOT_STATUS_BIT) ? 1u : 0u);

    rcg_cfg = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_APPS_RCG + ST_RCG_CFG);
    loads++;
    ST_LIVE("xnu_live_storage_clk_rcg_cfg", rcg_cfg);
    ST_LIVE("xnu_live_storage_clk_rcg_src", (rcg_cfg & ST_RCG_CFG_SRC_MASK) >> ST_RCG_CFG_SRC_SHIFT);
    ST_LIVE("xnu_live_storage_clk_rcg_div", rcg_cfg & ST_RCG_CFG_DIV_MASK);
    ST_LIVE("xnu_live_storage_clk_rcg_mnd_mode", (rcg_cfg & ST_RCG_CFG_MND_MASK) >> 12);

    rcg_m = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_APPS_RCG + ST_RCG_M);
    loads++;
    ST_LIVE("xnu_live_storage_clk_rcg_m", rcg_m);
    rcg_n = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_APPS_RCG + ST_RCG_N);
    loads++;
    ST_LIVE("xnu_live_storage_clk_rcg_n", rcg_n);
    rcg_d = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_APPS_RCG + ST_RCG_D);
    loads++;
    ST_LIVE("xnu_live_storage_clk_rcg_d", rcg_d);

    /*
     * **The one register on this path that is not a clock branch**, and the fifth offset in a window the
     * census has bounded to four since 698. `sdhci_msm_set_clock` reads it at five sites and writes a
     * field of it at five more; rung 5 reads both fields and writes neither, which is what makes them the
     * write rung's before-values.
     */
    vendor = st_read32(ST_CORE_MEM_BASE + ST_VENDOR_SPEC);
    loads++;
    ST_LIVE("xnu_live_storage_clk_vendor_spec", vendor);
    ST_LIVE("xnu_live_storage_clk_vendor_pwrsave", (vendor & ST_VENDOR_PWRSAVE_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_clk_vendor_mclk_sel",
            (vendor & ST_VENDOR_MCLK_MASK) >> ST_VENDOR_MCLK_SHIFT);

    /*
     * **The after-value, and it is the same register 703 read after the reset.** `CLOCK_CONTROL 0x2C`
     * read last means "the census moved nothing" is a measurement: bit 2 (`SD clock enable`) still clear
     * says this rung did not set a clock, and bits 0/1 still set say it did not clear one either.
     */
    ST_LIVE("xnu_live_storage_clk_clock_control_after",
            (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL));
    loads++;

    ST_LIVE("xnu_live_storage_clk_loads", loads);
    ST_LIVE("xnu_live_storage_clk_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 5 */

#if STAGE90_XNU_STORAGE_PROBE >= 6
/*
 * **706: rung 6 - the driver's first clock set, and the rate write that is not on this path.**
 * `docs/experiments/experiment-706-...md` is the pre-registration and the whole of this block is its
 * section 1 read out of the vendor's own source, in the vendor's own order:
 *
 *   `sdhci_do_set_ios` (`sdhci.c:1635-1636`) -> `sdhci_set_clock(host, 400000)` -> the vendor hook
 *   (`sdhci.c:1211-1214`) -> `sdhci_msm_set_clock` (`sdhci-msm.c:2402-2535`) -> then the standard's own
 *   divisor arithmetic and its two `CLOCK_CONTROL` halfwords (`sdhci.c:1254-1267`, `:1285-1306`).
 *
 * **`clock = 400000` is a reading, not a choice.** `mmc_rescan_try_freq(host, host->f_min)` sets
 * `host->f_init = freq` (`drivers/mmc/core/core.c:3077-3079`, called at `:3251`) and `host->f_min` is
 * `sup_clk_table[0]` (`sdhci-msm.c:2233`) = the first entry of the DT's `qcom,clk-rates`
 * (`msm8974pro.dtsi:1768`), i.e. 400000.
 *
 * **And the rate write is NOT on this path, which is section 2's finding and the reason this rung is
 * small.** `sdhci_msm_get_sup_clk_rate(host, 400000)` returns 400000 and `msm_host->clk_rate` was
 * initialised to `get_min_clock(host)` = the same 400000 (`:2795`), so `sup_clock != msm_host->clk_rate`
 * is false and `clk_set_rate` - the only thing here that writes the RCG - is skipped. The driver believes
 * the clock is 400 kHz while 705 measured the register file at `gpll4`/div 4 (192 MHz): two readings of
 * one quantity that disagree by 480x, and nothing reconciles them until the first *speed change*. So the
 * RCG's five words are read here (rung 5 did that) and **written nowhere**, `_clk_set_rate_writes = 0`.
 *
 * **Why the writes below cannot gate a clock, and what the build refuses instead.** The four CBCR stores
 * are read-modify-writes of `BIT(0)` - a bit 705 measured *set* on all four branches - so they cannot
 * disable one; the two `CORE_VENDOR_SPEC` stores touch only `MCLK_SEL` (`2 << 8`) and the `HC_SELECT_IN`
 * pair, neither of which gates anything. **`BCR 0x04C0` (block reset) and the whole RCG (`0x04D0`-`0x04E0`)
 * are refused by `build_entry.sh` rather than avoided here** - the GCC window's store set is asserted to
 * be exactly the four branch offsets in order - and `POWER_CONTROL 0x29` stays unreachable in the
 * `hc_mem` window, because the driver's power path writes **0** there (a bus-off request) and then waits
 * unbounded (`sdhci.c:1352-1355`, `sdhci-msm.c:2179-2209`): that act is the next rung's subject.
 * **708: the next rung is rung 7, and it does not write that zero.** `SDHCI_QUIRK_SINGLE_POWER_WRITE` is
 * set for this host (`sdhci-msm.c:2897`), so `sdhci.c:1352`'s zero/bus-off write is *not* on the path -
 * what `mmc_power_up`'s pass A takes is the one store `sdhci.c:1317-1334` derives from `CAPABILITIES`,
 * and the unbounded wait is NOT taken (this image cannot deliver the threaded handler's IRQ, measured
 * silent in 708 section 1.3). So the sentence above is rung 6's reading of this register and not the
 * ladder's; the `hc_mem` window's store set is `47 44 44 41` from rung 7 on.
 *
 * **And the AHB branch's `has_sibling` does not make its enable a different act, which the rung-5 census's
 * own comment asked this rung to say.** `gcc_sdcc1_ahb_clk` carries `has_sibling = 1`, and
 * `clock-local2.c:444-446`/`:460-462` turns that into `-EPERM` for `round_rate` and `list_rate` - both of
 * which are *queries about a rate*, reached from `clk_set_rate` and `clk_round_rate`. The path this rung
 * takes is `clk_prepare_enable`, and that is `:373-390`: a CBCR read-modify-write of `BIT(0)` followed by
 * the halt check, with no branch on `has_sibling` anywhere. 705 measured that bit already set
 * (`_clk_ahb_cbcr = 0x2000cff1`), so this store writes back the word it read - the same shape as the other
 * three. What `has_sibling` *does* mean here is the one thing the halt check has to handle and this image
 * already handles: the AHB branch is the one whose ON value is `BRANCH_NOC_FSM_ON_VAL` and not
 * `BRANCH_ON_VAL` (`clock-local2.c:325-328`, `:357-358`), which is why `st_branch_enable` accepts both.
 */
#define ST_BRANCH_CHECK_MASK      0xF0000000u    /* BM(31, 28), clock-local2.c:325 */
#define ST_BRANCH_ON_VAL          0x00000000u    /* BRANCH_ON_VAL,      :326 */
#define ST_BRANCH_NOC_FSM_ON_VAL  0x20000000u    /* BRANCH_NOC_FSM_ON_VAL, :328 - the AHB branch's own
                                                  * value, and 705 measured it: `_clk_ahb_cbcr = 0x2000cff1`
                                                  * reads 0x2 in bits 31:28, which `:357-358` accepts as ON */
#define ST_HALT_CHECK_MAX_LOOPS   500u           /* :36 - with a 1 us delay between failed reads */
#define ST_HALT_TICK_BUDGET       9600u          /* 500 us at this machine's 19,200,000 Hz */
#define ST_VENDOR_MCLK_DFLT       0x00000200u    /* CORE_HC_MCLK_SEL_DFLT = (2 << 8), sdhci-msm.c:94 */
#define ST_VENDOR_SELECT_IN_EN    (1u << 18)     /* CORE_HC_SELECT_IN_EN,   :98 */
#define ST_VENDOR_SELECT_IN_MASK  (7u << 19)     /* CORE_HC_SELECT_IN_MASK, :100 */
#define ST_SDHCI_CLOCK_INT_EN     0x0001u        /* sdhci.h:109 */
#define ST_SDHCI_CLOCK_INT_STABLE 0x0002u        /* sdhci.h:108 - read-only */
#define ST_SDHCI_CLOCK_CARD_EN    0x0004u        /* sdhci.h:107 - THE SD CLOCK ENABLE BIT */
#define ST_SET_INIT_CLOCK         400000u        /* host->f_init = host->f_min = sup_clk_table[0] */
#define ST_SET_MAX_CLK            384000000u     /* msm8974pro.dtsi:1768's last entry, via
                                                  * sdhci-msm.c:2240 get_max_clock - the divisor
                                                  * arithmetic's one non-register input */
#define ST_SET_MAX_CLK_STD        200000000u     /* msm8974.dtsi:339 - the ALTERNATIVE table, and the cell
                                                  * that tells the two apart is `_clk_set_divisor_alt` */
#define ST_SET_DIV_MAX            2046u          /* SDHCI_MAX_DIV_SPEC_300, sdhci.h:255 */
#define ST_CC_TICK_BUDGET         384000u        /* the driver's `timeout = 20` ms (sdhci.c:1292) */
#define ST_CC_POLL_STEPS          4096u          /* the backstop: 4096 halfword reads cannot outlast
                                                  * 20 ms, so the clock bound is the one that fires */

/*
 * **The halfword store, and rung 6 is the reason it exists.** `CLOCK_CONTROL 0x2C` is a 16-bit register
 * (`sdhci.h:100`) written with `sdhci_writew` (`sdhci.c:1289`, `:1306`), so a 32-bit store there would be
 * a *wider* access than the vendor's own contract - and `0x2C` is 4-aligned, so the alignment census
 * would let it through: the width here is a property of the register, not of the address, which is why
 * `build_entry.sh`'s `hc_mem` clause checks the mnemonic and not only the offset.
 */
static void st_write16(uint32_t addr, uint16_t value)
{
    *(volatile uint16_t *)(uintptr_t)addr = value;
    __asm__ volatile ("dsb sy" ::: "memory");
}

/*
 * **One branch enable, as `branch_clk_enable` writes it** (`clock-local2.c:373-390`): read the CBCR, set
 * `BIT(0)`, write it back, then run the halt check (`:333-371`) - which for every branch on this path is
 * the `HALT` arm, because `gcc_sdcc1_ahb_clk` and `gcc_sdcc1_cdccal_sleep_clk` declare `has_sibling = 1`
 * and none of the four declares a `halt_check` (`clock-8974.c:2339-2381`), so the field's value is 0 =
 * `HALT` (`clock-local2.h`). The poll's terminal state is published rather than inferred from the count,
 * and the count is published rather than inferred from the state: a first-read pass (expected) and a
 * 500th-read pass (a bound that was spent) are different readings of the same bit.
 */
struct st_branch_poll {
    uint32_t before;
    uint32_t after;
    uint32_t polls;
    uint32_t ticks;
    uint32_t halted;
};

/*
 * **`always_inline`, and the reason is a clause's subject rather than speed.** `build_entry.sh`'s
 * store census reads `entry_storage_probe`'s OWN body and classifies every store in it, which is what
 * makes "the offsets are the property" true of the artifact the gate boots - the four branch enables
 * must therefore be *this body's* stores. GCC does not inline a helper with four call sites (it does
 * inline its single-call siblings: `st_clock_census`, `st_clock_set`) and the first build of this rung
 * measured exactly that: `st_branch_enable` came out as a symbol at `0x8000cff8` with the probe pushed
 * to `0x8000d09c`, the census reported `core_mem [120 0 120 120 ]` (the two `0x10C` stores were in the
 * inlined `st_clock_set` and did land), the GCC window came out EMPTY, and the arm's own record was
 * refused by two clauses at once. A helper the arm's safety claim has to name is not a helper this
 * image wants outlined, so the attribute makes the compiler's choice the arm's choice and the stores
 * land where the record says they are.
 */
static inline __attribute__((always_inline)) void
st_branch_enable(uint32_t addr, struct st_branch_poll *r)
{
    uint32_t value, t0, polls = 0u, halted = 0u;

    r->before = st_read32(addr);
    value = r->before | ST_CBCR_ENABLE_BIT;      /* clock-local2.c:381 */
    st_write32(addr, value);                     /* clock-local2.c:382 - the read-modify-write */
    r->after = st_read32(addr);

    t0 = (uint32_t)stage90_cntvct_read();
    for (polls = 1u; polls <= ST_HALT_CHECK_MAX_LOOPS; polls++) {
        uint32_t v = st_read32(addr) & ST_BRANCH_CHECK_MASK;

        if (v == ST_BRANCH_ON_VAL || v == ST_BRANCH_NOC_FSM_ON_VAL) {   /* :357-358 */
            halted = 1u;
            break;
        }
        if ((uint32_t)stage90_cntvct_read() - t0 >= ST_HALT_TICK_BUDGET)
            break;
    }
    r->ticks = (uint32_t)stage90_cntvct_read() - t0;
    r->polls = polls;
    r->halted = halted;
}

static void st_clock_set(void)
{
    struct st_branch_poll b;
    uint32_t writes = 0u, gcc_writes = 0u, rate_writes = 0u;
    uint32_t vendor, v1, v2, vendor_after;
    uint32_t cc_before, div, real_div = 0u, word_int, word_card, cc_after;
    uint32_t stable = 0u, cc_polls = 0u, cc_steps = 0u, t0;
    uint32_t alt = 0u;

    ST_LIVE("xnu_live_storage_clk_set_calls", 1u);

    /*
     * **1. `sdhci_msm_prepare_clocks(host, true)` (`sdhci-msm.c:2315-2374`), and the order is the
     * vendor's**: `pclk` (= `gcc_sdcc1_ahb_clk`, `clock-8974.c:2339`), `clk` (= `gcc_sdcc1_apps_clk`,
     * `:2350`), then `bus_clk`, `ff_clk` (= `gcc_sdcc1_cdccal_ff_clk`, `:2361`) and `sleep_clk`
     * (= `gcc_sdcc1_cdccal_sleep_clk`, `:2372`). `bus_clk` is `devm_clk_get(&pdev->dev, "bus_clk")`
     * (`:2758`) and this board's SDCC1 node has no such clock, so `IS_ERR_OR_NULL` skips it - which is
     * why four branches are enabled and not five.
     */
    st_branch_enable(ST_GCC_BASE + ST_GCC_SDCC1_AHB_CBCR, &b);
    writes++;
    gcc_writes++;
    ST_LIVE("xnu_live_storage_clk_set_ahb_before", b.before);
    ST_LIVE("xnu_live_storage_clk_set_ahb_after", b.after);
    ST_LIVE("xnu_live_storage_clk_set_ahb_polls", b.polls);
    ST_LIVE("xnu_live_storage_clk_set_ahb_ticks", b.ticks);
    ST_LIVE("xnu_live_storage_clk_set_ahb_halted", b.halted);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);
    ST_LIVE("xnu_live_storage_clk_set_gcc_writes", gcc_writes);

    st_branch_enable(ST_GCC_BASE + ST_GCC_SDCC1_APPS_CBCR, &b);
    writes++;
    gcc_writes++;
    ST_LIVE("xnu_live_storage_clk_set_apps_before", b.before);
    ST_LIVE("xnu_live_storage_clk_set_apps_after", b.after);
    ST_LIVE("xnu_live_storage_clk_set_apps_polls", b.polls);
    ST_LIVE("xnu_live_storage_clk_set_apps_ticks", b.ticks);
    ST_LIVE("xnu_live_storage_clk_set_apps_halted", b.halted);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);
    ST_LIVE("xnu_live_storage_clk_set_gcc_writes", gcc_writes);

    st_branch_enable(ST_GCC_BASE + ST_GCC_SDCC1_CDCCAL_FF_CBCR, &b);
    writes++;
    gcc_writes++;
    ST_LIVE("xnu_live_storage_clk_set_ff_before", b.before);
    ST_LIVE("xnu_live_storage_clk_set_ff_after", b.after);
    ST_LIVE("xnu_live_storage_clk_set_ff_polls", b.polls);
    ST_LIVE("xnu_live_storage_clk_set_ff_ticks", b.ticks);
    ST_LIVE("xnu_live_storage_clk_set_ff_halted", b.halted);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);
    ST_LIVE("xnu_live_storage_clk_set_gcc_writes", gcc_writes);

    st_branch_enable(ST_GCC_BASE + ST_GCC_SDCC1_CDCCAL_SLEEP_CBCR, &b);
    writes++;
    gcc_writes++;
    ST_LIVE("xnu_live_storage_clk_set_sleep_before", b.before);
    ST_LIVE("xnu_live_storage_clk_set_sleep_after", b.after);
    ST_LIVE("xnu_live_storage_clk_set_sleep_polls", b.polls);
    ST_LIVE("xnu_live_storage_clk_set_sleep_ticks", b.ticks);
    ST_LIVE("xnu_live_storage_clk_set_sleep_halted", b.halted);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);
    ST_LIVE("xnu_live_storage_clk_set_gcc_writes", gcc_writes);

    /*
     * **2. The vendor spec's two read-modify-writes (`sdhci-msm.c:2495-2513`), the non-HS400 arm**, and
     * the `curr_pwrsave` pair above them (`:2427-2441`) is **not** taken at this clock: both of its arms
     * need `clock > 400000` or `curr_pwrsave`, and rung 5 measured `_clk_vendor_pwrsave = 0`. So the two
     * stores here are the MCLK select and the HC_SELECT_IN clears, and nothing else on 0x10C.
     */
    vendor = st_read32(ST_CORE_MEM_BASE + ST_VENDOR_SPEC);
    v1 = (vendor & ~ST_VENDOR_MCLK_MASK) | ST_VENDOR_MCLK_DFLT;            /* :2497-2499 */
    st_write32(ST_CORE_MEM_BASE + ST_VENDOR_SPEC, v1);
    writes++;
    ST_LIVE("xnu_live_storage_clk_set_vendor_before", vendor);
    ST_LIVE("xnu_live_storage_clk_set_vendor_w1", v1);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);

    v2 = st_read32(ST_CORE_MEM_BASE + ST_VENDOR_SPEC)
         & ~ST_VENDOR_SELECT_IN_EN & ~ST_VENDOR_SELECT_IN_MASK;           /* :2510-2512 */
    st_write32(ST_CORE_MEM_BASE + ST_VENDOR_SPEC, v2);
    writes++;
    vendor_after = st_read32(ST_CORE_MEM_BASE + ST_VENDOR_SPEC);
    ST_LIVE("xnu_live_storage_clk_set_vendor_w2", v2);
    ST_LIVE("xnu_live_storage_clk_set_vendor_after", vendor_after);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);

    /*
     * **3. The rate write, and it is not reached.** `sup_clock = sdhci_msm_get_sup_clk_rate(host, 400000)
     * = 400000` (the table's first entry equals the request) and `msm_host->clk_rate = 400000` from
     * `:2795`, so `:2517`'s condition is false and the RCG - whose before-values rung 5 published - is
     * not written. This key is the rung's central claim, published rather than argued.
     */
    ST_LIVE("xnu_live_storage_clk_set_rate_writes", rate_writes);

    /*
     * **4. The standard's own two halfwords and the poll between them** (`sdhci.c:1254-1306`). The
     * divisor is the count's, computed with the driver's own loop and the one input that is not a
     * register: `max_clk = get_max_clock(host) = sup_clk_table[sup_clk_cnt-1]` = the DT's last entry.
     * `_clk_set_divisor_alt` is what tells the two candidate tables apart in the log.
     */
    ST_LIVE("xnu_live_storage_clk_set_max_clk", ST_SET_MAX_CLK);

    if (ST_SET_MAX_CLK <= ST_SET_INIT_CLOCK) {
        div = 1u;                                                        /* :1257-1258 */
    } else {
        for (div = 2u; div < ST_SET_DIV_MAX; div += 2u)                  /* :1260-1261 */
            if ((ST_SET_MAX_CLK / div) <= ST_SET_INIT_CLOCK)
                break;
    }
    real_div = div;
    div >>= 1u;                                                          /* :1266 */
    if (real_div == (ST_SET_MAX_CLK_STD / ST_SET_INIT_CLOCK))
        alt = 1u;

    ST_LIVE("xnu_live_storage_clk_set_real_div", real_div);
    ST_LIVE("xnu_live_storage_clk_set_div", div);
    ST_LIVE("xnu_live_storage_clk_set_divisor_alt", alt);

    cc_before = (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL);
    word_int = (uint32_t)(((div & 0xFFu) << 8)
                          | (((div & 0x300u) >> 8) << 6)             /* :1285-1286 */
                          | ST_SDHCI_CLOCK_INT_EN);                  /* :1288 */
    st_write16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL, (uint16_t)word_int);
    writes++;
    ST_LIVE("xnu_live_storage_clk_set_cc_before", cc_before);
    ST_LIVE("xnu_live_storage_clk_set_cc_int", word_int);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);

    t0 = (uint32_t)stage90_cntvct_read();
    for (cc_steps = 1u; cc_steps <= ST_CC_POLL_STEPS; cc_steps++) {
        cc_polls = (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL);
        if ((cc_polls & ST_SDHCI_CLOCK_INT_STABLE) != 0u) {           /* sdhci.c:1293-1294 */
            stable = 1u;
            break;
        }
        if ((uint32_t)stage90_cntvct_read() - t0 >= ST_CC_TICK_BUDGET)
            break;
    }
    ST_LIVE("xnu_live_storage_clk_set_cc_stable", stable);
    ST_LIVE("xnu_live_storage_clk_set_cc_polls", cc_steps);
    ST_LIVE("xnu_live_storage_clk_set_cc_ticks", (uint32_t)stage90_cntvct_read() - t0);

    word_card = word_int | ST_SDHCI_CLOCK_CARD_EN;                    /* :1305 */
    st_write16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL, (uint16_t)word_card);
    writes++;
    cc_after = (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL);
    ST_LIVE("xnu_live_storage_clk_set_cc_card", word_card);
    ST_LIVE("xnu_live_storage_clk_set_cc_after", cc_after);
    ST_LIVE("xnu_live_storage_clk_set_writes", writes);
    ST_LIVE("xnu_live_storage_clk_set_gcc_writes", gcc_writes);
    ST_LIVE("xnu_live_storage_clk_set_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 6 - st_branch_enable is called four times (and is
        * always_inline so its stores are the probe's own) and st_clock_set once, and -Werror is on */

#if STAGE90_XNU_STORAGE_PROBE >= 7
/*
 * **708: rung 7 - the driver's own first power byte, and the wait that is not taken.**
 * `docs/experiments/experiment-708-the-rung-7-pre-registration-the-card-power-byte-and-the-wait-that-is-not-taken.md`
 * is the pre-registration and this block is its sections 1.2-1.4 read out of the vendor's own source:
 *
 *   `mmc_power_up`'s PASS A (`core.c:1916-1924` - `ios.clock` is still 0 and `ios.power_mode =
 *   MMC_POWER_UP`) -> `sdhci_do_set_ios` -> `if (ios->clock)` false, so **no clock set on this pass** ->
 *   `if (ios->power_mode & MMC_POWER_UP)` (`sdhci.c:1658`, true only for `UP` because `ON` is 2 and the
 *   test is `& 1`) -> `enable_controller_clock` -> **`sdhci_set_power(host, ios->vdd)` (`:1663`)**.
 *   Rung 6 was PASS B's act (the clock), this is PASS A's (the power), and the ladder appends rather than
 *   reorders - `_pwr_cc_before` is the cell that says so.
 *
 * **`SDHCI_QUIRK_SINGLE_POWER_WRITE` is SET on this host (`sdhci-msm.c:2897`), so the `0` write and its
 * `REQ_BUS_OFF` are NOT on this path**: `sdhci.c:1352`'s block is skipped and the whole act is ONE store
 * - `pwr |= SDHCI_POWER_ON` then `sdhci_writeb` (`:1368-1370`) - followed by ONE
 * `sdhci_msm_check_power_status(host, REQ_BUS_ON)` (`:1371-1372`). 706 section 4 and 707 section 7 called
 * the act "both hazards in three lines"; the bus-off write is not one of them, and both documents carry
 * the correction. **It writes `0x0B` and never `0x00`.**
 *
 * **And the wait is NOT taken here, which is this rung's boundary.** With `curr_pwr_state` and
 * `curr_io_level` both 0 - they are written ONLY by the threaded `sdhci_msm_pwr_irq` (`sdhci-msm.c:2092-2094`)
 * and every run has measured the power surface silent (`_reg_pwrctl_mask = 0x0f`, `_reg_pwrctl_status = 0`)
 * - `check_power_status(REQ_BUS_ON)` takes `wait_for_completion(&msm_host->pwr_irq_completion)` and waits
 * for an IRQ this image cannot deliver: **the driver's power-up cannot be performed the driver's way in
 * this image.** So this rung does the register act and takes the reading the handler would have taken
 * (`CORE_PWRCTL_STATUS 0xDC`), left latched - no write to `CORE_PWRCTL_CLEAR 0xE4`, `CORE_PWRCTL_CTL 0xE8`
 * or `CORE_PWRCTL_MASK 0xE0`, and no call to the vendor's check.
 *
 * **The byte is a derivation, and its input is a reading this project already published.** `host->ocr_avail`
 * comes from `caps[0]` = `SDHCI_CAPABILITIES 0x40` (`sdhci.c:3185`, `:3421-3481`), and `mmc_power_up` takes
 * `fls(ocr_avail) - 1` (`core.c:1912-1914`); 705's `_mode_capabilities = 0x742dc8b2` has VDD_330 **clear**,
 * VDD_300 **clear** and VDD_180 **set**, so `ocr_avail = MMC_VDD_165_195` and the byte is
 * `SDHCI_POWER_180 | SDHCI_POWER_ON = 0x0B` - a **1.8 V request**, not the `0x0F` a 3.3 V-reporting host
 * gives. The default arm of the driver's own `switch (1 << power)` is `BUG()` (`sdhci.c:1333-1334`) and
 * `_pwr_refused` is this arm's mirror of it: refuse and publish rather than write a byte the driver never
 * would.
 */
#define ST_SDHCI_POWER_ON     0x01u       /* sdhci.h:88  */
#define ST_SDHCI_POWER_180    0x0Au       /* sdhci.h:89  */
#define ST_SDHCI_POWER_300    0x0Cu       /* sdhci.h:90  */
#define ST_SDHCI_POWER_330    0x0Eu       /* sdhci.h:91  */
#define ST_SDHCI_CAN_VDD_330  0x01000000u /* sdhci.h:195 - the bit sdhci_add_host tests for 3.3 V */
#define ST_SDHCI_CAN_VDD_300  0x02000000u /* sdhci.h:196 */
#define ST_SDHCI_CAN_VDD_180  0x04000000u /* sdhci.h:197 */
#define ST_MMC_VDD_165_195    0x00000080u /* host.h:220 - `1 << power` for power = 7 */
#define ST_MMC_VDD_29_30      0x00020000u /* host.h:230 */
#define ST_MMC_VDD_30_31      0x00040000u /* host.h:231 */
#define ST_MMC_VDD_32_33      0x00100000u /* host.h:233 */
#define ST_MMC_VDD_33_34      0x00200000u /* host.h:234 */

/*
 * `fls`/`ffs` written out rather than taken from a builtin, because this arm's subject is the driver's own
 * definition of the bit and not a compiler's choice of instruction: a loop is the definition, and it is
 * the same for every build of this image.
 */
static uint32_t st_fls32(uint32_t v)   /* 1-based position of the highest set bit; 0 for v == 0 */
{
    uint32_t r = 0u;
    while (v != 0u) { r++; v >>= 1; }
    return r;
}

static uint32_t st_ffs32(uint32_t v)   /* 1-based position of the lowest set bit; 0 for v == 0 */
{
    uint32_t r = 0u;
    if (v == 0u)
        return 0u;
    while ((v & 1u) == 0u) { r++; v >>= 1; }
    return r + 1u;
}

static void st_power_set(void)
{
    uint32_t cap, avail, vdd, vdd_ocr, pwr, refused;
    uint8_t before, after;

    ST_LIVE("xnu_live_storage_pwr_calls", 1u);

    cap = st_read32(ST_HC_MEM_BASE + ST_SDHCI_CAPABILITIES);
    ST_LIVE("xnu_live_storage_pwr_cap", cap);

    /* sdhci.c:3421-3465 - the driver's own ocr_avail, from `caps[0]` and its three bits. */
    avail = 0u;
    if ((cap & ST_SDHCI_CAN_VDD_330) != 0u)
        avail |= ST_MMC_VDD_32_33 | ST_MMC_VDD_33_34;
    if ((cap & ST_SDHCI_CAN_VDD_300) != 0u)
        avail |= ST_MMC_VDD_29_30 | ST_MMC_VDD_30_31;
    if ((cap & ST_SDHCI_CAN_VDD_180) != 0u)
        avail |= ST_MMC_VDD_165_195;
    ST_LIVE("xnu_live_storage_pwr_avail", avail);

    refused = 0u;
    if (avail == 0u) {
        /* `fls(0) - 1` is not a bit index, so the driver's own path cannot reach a power byte from this
         * register state; the arm refuses for the same reason the switch below does. */
        refused = 1u;
        vdd = 0u;
        vdd_ocr = 0u;
    } else {
        vdd = st_fls32(avail) - 1u;                                            /* core.c:1914 */
        vdd_ocr = st_ffs32(1u << (st_fls32(avail) - 1u)) - 1u;                  /* core.c:1912 */
    }
    ST_LIVE("xnu_live_storage_pwr_vdd", vdd);
    ST_LIVE("xnu_live_storage_pwr_vdd_ocr", vdd_ocr);

    pwr = 0u;
    switch (1u << vdd) {                       /* sdhci.c:1317-1334, `switch (1 << power)` */
    case ST_MMC_VDD_165_195:
        pwr = ST_SDHCI_POWER_180;
        break;
    case ST_MMC_VDD_29_30:
    case ST_MMC_VDD_30_31:
        pwr = ST_SDHCI_POWER_300;
        break;
    case ST_MMC_VDD_32_33:
    case ST_MMC_VDD_33_34:
        pwr = ST_SDHCI_POWER_330;
        break;
    default:
        refused = 1u;                          /* the driver's own arm here is BUG() */
        break;
    }
    ST_LIVE("xnu_live_storage_pwr_voltage_bits", pwr);
    ST_LIVE("xnu_live_storage_pwr_refused", refused);
    if (refused != 0u) {
        ST_LIVE("xnu_live_storage_pwr_done", 0u);
        return;
    }

    /* The readings the rung takes before the store - including the ones the handler that cannot run
     * would have taken, and the clock and card state this pass is defined against. */
    before = st_read8(ST_HC_MEM_BASE + ST_SDHCI_POWER_CONTROL);
    ST_LIVE("xnu_live_storage_pwr_before", (uint32_t)before);
    ST_LIVE("xnu_live_storage_pwr_status_before",
            st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_STATUS));
    ST_LIVE("xnu_live_storage_pwr_mask", st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_MASK));
    ST_LIVE("xnu_live_storage_pwr_ctl", st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_CTL));
    ST_LIVE("xnu_live_storage_pwr_ps_before", st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
    ST_LIVE("xnu_live_storage_pwr_cc_before",
            (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL));

    /* THE STORE - one byte, and the value is the derivation above with SDHCI_POWER_ON. */
    ST_LIVE("xnu_live_storage_pwr_wrote", pwr | ST_SDHCI_POWER_ON);
    st_write8(ST_HC_MEM_BASE + ST_SDHCI_POWER_CONTROL, (uint8_t)(pwr | ST_SDHCI_POWER_ON));

    after = st_read8(ST_HC_MEM_BASE + ST_SDHCI_POWER_CONTROL);
    ST_LIVE("xnu_live_storage_pwr_after", (uint32_t)after);
    ST_LIVE("xnu_live_storage_pwr_status_after",
            st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_STATUS));
    ST_LIVE("xnu_live_storage_pwr_ps_after", st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
    ST_LIVE("xnu_live_storage_pwr_cc_after",
            (uint16_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL));

    ST_LIVE("xnu_live_storage_pwr_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 7 - two small helpers and one caller */

#if STAGE90_XNU_STORAGE_PROBE >= 8
/*
 * **710: rung 8 - the driver's own power IRQ, and the first rung of this ladder that OWNS a line.**
 * 709 pressed rung 7 and the press answered a question the pre-registration had not asked: the power
 * byte took and read back, the controller answered `CORE_PWRCTL_STATUS = 0x02` (a bus-on request), and
 * that request **arrived as an interrupt on intid 170** - the one line this image hands to nobody,
 * whose arm in `entry_irq.c:546-557` ends the run. 709 section 3 left the next rung as a *decision*:
 * mask the power events (`CORE_PWRCTL_MASK <- 0`) or own the line. **This rung owns it**, and the
 * reason is structural rather than taste: `entry_irq_register_client` and `entry_irq_enable_line`
 * already exist, were built and measured at 496/498/500 on intid 40, and **this is their first caller
 * in the ladder** - while masking would leave the driver's own power handshake permanently incomplete,
 * because `sdhci_msm_check_power_status`' completion is written by the handler this rung installs
 * (`sdhci-msm.c:2092-2096`).
 *
 * **The handler is the vendor's own `sdhci_msm_pwr_irq` (`:1990-2099`) with the three arms that can
 * sleep absent, and the vendor's own code is why they are absent.**
 * `devm_request_threaded_irq(..., NULL, sdhci_msm_pwr_irq, IRQF_ONESHOT, ...)` (`:2937-2939`) - a NULL
 * primary over `IRQF_ONESHOT` - *declares a threaded handler*, because `sdhci_msm_setup_vreg`,
 * `sdhci_msm_setup_pins` and `sdhci_msm_set_vdd_io_vol` may sleep and none of them may run in an
 * exception. This image's client is called from `entry_irq_handler`, i.e. from `fleh_irq_kernel`'s
 * frame, with interrupts masked and no thread to sleep in. What remains of the handler is the register
 * handshake - acts 1, 2, the decode's ack bits, 5 and 6 - which is exactly the part the controller
 * needs and the part 709 measured it asking for.
 *
 * **The ack value is the vendor's own on a path with no regulators at all.** The probe's own
 * pre-acknowledge (`:2872-2889`) reads `CORE_PWRCTL_STATUS`, writes it to `CORE_PWRCTL_CLEAR`, ORs
 * `CORE_PWRCTL_BUS_SUCCESS` into `CORE_PWRCTL_CTL` **because the status said `BUS_ON`** - with no vreg
 * call and no card present - and its comment says why (`CORE_SW_RST` may trigger a power IRQ if the
 * previous status was `BUS_ON`). So this handler is that preamble moved from probe time to the moment
 * the line is raised, plus the decode that names which event it was.
 *
 * **One consequence of the decode is load-bearing for the build's store census.** For
 * `irq_status = BUS_ON` the vendor sets **both** `pwr_state = REQ_BUS_ON` *and* `io_level = REQ_IO_HIGH`
 * (`:2028-2029`, outside the `ret` test), so act 6 runs - a read-modify-write of
 * `host->ioaddr + CORE_VENDOR_SPEC` (`:2079-2085`) - and the handler is a **three-store** handler,
 * not the two-store one a reader would predict from the `readb`/`writeb` pair at the top. That store
 * lands in `hc_mem`, so this sentence read "rung 8's census moves from `47 44 44 41` to
 * `47 44 44 41 268`" - **and 711 section 5's correction is that the build says `47 44 44 41`**: the
 * handler is a separate symbol, so its store is outside `entry_storage_probe`'s window, and the one
 * value had two readings of which the pre-registration wrote the wrong one. The store is asserted by
 * this handler's own clause, against `0xf9824a0c`.
 *
 * **§1.5's two addresses are read here, in one run, before the byte.** `ST_VENDOR_SPEC 0x10C` is used
 * 23 times in the vendor's file, every one through `host->ioaddr`, and zero times through
 * `msm_host->core_mem` - so the register `sdhci_msm_set_clock`'s MCLK select lives in is
 * `hc_mem + 0x10C` and this ladder has read `core_mem + 0x10C` since 705. The handler's act 6 writes
 * the vendor's address; the arming function reads both and publishes them side by side, which retires
 * the question two-registers-or-alias whichever way it reads.
 *
 * **What this rung does NOT do**: it does not mask the power events (709's first option, rejected with
 * its reason above); it does not run the three arms that sleep; it does not emulate the completion or
 * touch `curr_pwr_state`/`curr_io_level` as a cache (a field of a `struct sdhci_msm_host` this image
 * does not have - a cell computed here would be the driver's name on a second definition); it registers
 * intid 170 and nothing else, so `hc_irq` (SPI 123 = intid 155) stays unowned and a delivery on it is
 * still a stop, which is the property that keeps a driver's mistake from being a storm.
 */
#define ST_CORE_PWRCTL_CLEAR        0xE4u    /* sdhci-msm.c:64 - the latch's acknowledge           */
#define ST_CORE_PWRCTL_BUS_OFF      0x01u    /* :67 */
#define ST_CORE_PWRCTL_BUS_ON       (1u << 1) /* :68 - the bit 709's press read as 0x02             */
#define ST_CORE_PWRCTL_IO_LOW       (1u << 2) /* :69 */
#define ST_CORE_PWRCTL_IO_HIGH      (1u << 3) /* :70 */
#define ST_CORE_PWRCTL_BUS_SUCCESS  0x01u    /* :72 - the ack bit a BUS_ON status earns            */
#define ST_CORE_PWRCTL_BUS_FAIL     (1u << 1) /* :73 */
#define ST_CORE_PWRCTL_IO_SUCCESS   (1u << 2) /* :74 */
#define ST_CORE_PWRCTL_IO_FAIL      (1u << 3) /* :75 */
#define ST_CORE_IO_PAD_PWR_SWITCH   (1u << 16) /* :97 - CORE_IO_PAD_PWR_SWITCH, act 6's field       */
#define ST_REQ_BUS_OFF              (1u << 0) /* sdhci.h:290 - the vendor's own request bits, the    */
#define ST_REQ_BUS_ON               (1u << 1) /* :291 - first of which is what rung 9 asks about     */
#define ST_REQ_IO_LOW               (1u << 2) /* :292 */
#define ST_REQ_IO_HIGH              (1u << 3) /* :293 */
#define ST_PWR_WAIT_INNER           1024u     /* ack reads between two samples of the clock - the    */
                                              /* same sampler spacing rung 4's poll uses, so the
                                               * sampler is not the thing being measured          */
#define ST_PWR_IRQ_INTID            170u      /* msm8974.dtsi:502 - `<0 138 0>` + 32, `pwr_irq`   */
#define ST_PWR_IRQ_TARGET           0x01u     /* GICD_ITARGETSR's byte for CPU 0, the same target
                                               * 500's intid-40 arm was given (`_line_target_want`) */

static uint32_t g_pwr_irq_calls;   /* the client's own count of being called, published every call */
static uint32_t g_pwr_irq_arms;    /* the arming ran exactly once, whatever the probe's guards do  */
/*
 * **712: rung 9's three image-side words, and they are the vendor's own two fields plus its
 * completion.** `msm_host->curr_pwr_state` / `curr_io_level` are the fields
 * `sdhci_msm_check_power_status` compares `req_type` against (`sdhci-msm.c:2201`) and the handler's
 * tail fills (`:2092-2094`); `g_pwr_irq_done` stands where `complete(&msm_host->pwr_irq_completion)`
 * stands (`:2095`). They are **volatile** because two contexts reach them - the handler that fills
 * them and the probe that spins on the flag - and because the compiler must not hoist the flag's read
 * out of the polling loop. They carry two names of the driver's on purpose: what rung 8 refused was
 * publishing a *cell* under the driver's name, and what these are is the driver's own *transition*,
 * performed where the driver performs it.
 */
static volatile uint32_t g_pwr_curr_state;   /* `msm_host->curr_pwr_state` - REQ_* bits, the handler */
static volatile uint32_t g_pwr_curr_io;      /* `msm_host->curr_io_level`  - REQ_* bits, the handler */
static volatile uint32_t g_pwr_irq_done;     /* the `complete()` stand-in: set at the handler's tail */

/*
 * **The client, and every device access is written here rather than through the `st_*` helpers.** The
 * build classifies *this function's own body* - the probe's window ends at the next global symbol and
 * the handler is one, so a store in it is outside the probe's clause and needs its own - and a helper
 * call would put the access in another function and leave this window empty. That is rung 6's lesson
 * arriving one level over: `st_branch_enable` was not inlined, and the GCC window came out empty while
 * the record said the stores were the probe's. So the four `volatile` accesses below are raw, the
 * `dsb sy` after each store is the same barrier the helpers carry, and the clause in `build_entry.sh`
 * names each offset it expects to find.
 *
 * **The two widths of `CORE_PWRCTL_STATUS` are deliberate and are the rung's one self-check.** The
 * vendor reads it with `readb_relaxed`, the probe's own preamble with `readl_relaxed`, and the pair is
 * published from *one* moment: **a disagreement between them is a reading about the register's width
 * behaviour**, and agreement is what makes the byte and the word the same quantity - the defect class
 * this project keeps meeting (`mi4-one-value-two-definitions`), measured rather than assumed.
 */
void st_pwr_irq(void *refCon, uint32_t intid)
{
    const uint32_t status_addr = ST_CORE_MEM_BASE + ST_CORE_PWRCTL_STATUS;
    const uint32_t ctl_addr    = ST_CORE_MEM_BASE + ST_CORE_PWRCTL_CTL;
    const uint32_t pad_addr    = ST_HC_MEM_BASE + ST_VENDOR_SPEC;
    uint32_t status32, ctl_before, ctl_after, pad_before, pad_after;
    uint32_t ack = 0u, io_level = 0u, pwr_state = 0u, at;
    uint8_t status;

    (void)refCon;
    g_pwr_irq_calls++;

    /*
     * **712: the handler's own timestamp, taken before anything else.** Rung 8's press left the
     * delivery *time* unmeasured: `_pwr_irq_calls = 0x1` beside `_pwr_irq_calls_probe_end = 0x0` says
     * the client ran after the probe's tail but not when, and the probe's own clock (`_wait_t0`) is
     * the base this stamp is read against. The difference is the interrupt's latency from the byte -
     * a number rather than an assumption, and the cell that tells a budget-expired press what budget
     * to use next.
     */
    at = (uint32_t)stage90_cntvct_read();

    /* Act 1 - the status, at both widths, from one moment. */
    status   = *(volatile uint8_t *)(uintptr_t)status_addr;   /* the vendor's `readb_relaxed`   */
    status32 = *(volatile uint32_t *)(uintptr_t)status_addr;  /* the probe's `readl_relaxed`    */

    ST_LIVE("xnu_live_storage_pwr_irq_at", at);
    ST_LIVE("xnu_live_storage_pwr_irq_calls", g_pwr_irq_calls);
    ST_LIVE("xnu_live_storage_pwr_irq_intid", intid);
    ST_LIVE("xnu_live_storage_pwr_irq_refcon", (uint32_t)(uintptr_t)refCon);
    ST_LIVE("xnu_live_storage_pwr_irq_status", (uint32_t)status);
    ST_LIVE("xnu_live_storage_pwr_irq_status32", status32);

    /* Act 2 - the latch's acknowledge, with the byte the vendor writes (`:2006`), and the readback
     * that says whether the latch let go. A clear that does not take is the rung's own falsifier: the
     * level line re-asserts, the dispatcher calls this client again, and `STAGE90_IRQ_CLIENT_CAP` (64,
     * `entry_gic.h`) stops the run with `_irq_cli_storm` instead of the 690 ending. */
    *(volatile uint8_t *)(uintptr_t)(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_CLEAR) = status;
    __asm__ volatile ("dsb sy" ::: "memory");
    ST_LIVE("xnu_live_storage_pwr_irq_status_after",
            (uint32_t)*(volatile uint8_t *)(uintptr_t)status_addr);

    /* Act 4 - the decode. The vendor's own arms, in its own order, with the two `ret` branches of the
     * regulator calls folded to the success direction: there is no regulator call here to fail, which
     * is the same reason the probe's preamble ORs `BUS_SUCCESS` unconditionally (`:2880-2883`). */
    if ((status & ST_CORE_PWRCTL_BUS_ON) != 0u) {
        io_level = ST_CORE_PWRCTL_IO_HIGH;     /* `:2029` - the line that makes act 6 run */
        pwr_state = ST_REQ_BUS_ON;             /* `:2023` - 712: the field rung 9's check reads */
        ack |= ST_CORE_PWRCTL_BUS_SUCCESS;
    }
    if ((status & ST_CORE_PWRCTL_BUS_OFF) != 0u) {
        io_level = ST_CORE_PWRCTL_IO_LOW;      /* `:2050` - the other arm sets the other pair */
        pwr_state = ST_REQ_BUS_OFF;
        ack |= ST_CORE_PWRCTL_BUS_SUCCESS;
    }
    if ((status & ST_CORE_PWRCTL_IO_LOW) != 0u) {
        io_level = ST_CORE_PWRCTL_IO_LOW;
        ack |= ST_CORE_PWRCTL_IO_SUCCESS;
    }
    if ((status & ST_CORE_PWRCTL_IO_HIGH) != 0u) {
        io_level = ST_CORE_PWRCTL_IO_HIGH;
        ack |= ST_CORE_PWRCTL_IO_SUCCESS;
    }

    /* Act 5 - the answer to the controller, read either side of the store. */
    ctl_before = (uint32_t)*(volatile uint8_t *)(uintptr_t)ctl_addr;
    *(volatile uint8_t *)(uintptr_t)ctl_addr = (uint8_t)ack;
    __asm__ volatile ("dsb sy" ::: "memory");
    ctl_after = (uint32_t)*(volatile uint8_t *)(uintptr_t)ctl_addr;

    /* Act 6 - `host->ioaddr + CORE_VENDOR_SPEC`, the read-modify-write the vendor's `IO_HIGH`/`IO_LOW`
     * arms perform (`:2079-2085`). This is the register §1.5 measured this ladder reading in the other
     * window; the handler writes the vendor's own address, and the readback after it is 706's lesson
     * applied to the one register whose store this project has already seen not hold at the other. */
    pad_before = *(volatile uint32_t *)(uintptr_t)pad_addr;
    pad_after  = pad_before;
    if ((io_level & ST_CORE_PWRCTL_IO_HIGH) != 0u)
        pad_after = pad_before & ~ST_CORE_IO_PAD_PWR_SWITCH;
    else if ((io_level & ST_CORE_PWRCTL_IO_LOW) != 0u)
        pad_after = pad_before | ST_CORE_IO_PAD_PWR_SWITCH;
    *(volatile uint32_t *)(uintptr_t)pad_addr = pad_after;
    __asm__ volatile ("dsb sy" ::: "memory");
    pad_after = *(volatile uint32_t *)(uintptr_t)pad_addr;

    /*
     * **712: the vendor's own tail (`:2092-2096`), and it is the whole of what rung 9 depends on.**
     * The two fields are written exactly where the driver writes them - guarded by the same
     * `if (pwr_state)` / `if (io_level)` tests, because a status that carried neither leaves the
     * fields alone - and the completion is raised after them, which is what makes `_pwr_irq_calls`
     * (this function's first statement) and `_wait_done` (the flag) two different readings of one
     * event: an entry with no flag is an event that did not finish.
     *
     * **The barrier is the vendor's `spin_lock_irqsave`'s, taken without the lock.** The vendor
     * publishes under `host->lock` and `complete()` carries its own barriers; this image is
     * single-CPU with interrupts masked here, so what matters is that the *device* accesses above
     * this line have reached the controller before the flag is visible - and `dsb sy` is the same
     * barrier the raw accesses above carry, for the same reason.
     */
    if (pwr_state != 0u)
        g_pwr_curr_state = pwr_state;
    if (io_level != 0u)
        g_pwr_curr_io = io_level;
    __asm__ volatile ("dsb sy" ::: "memory");
    g_pwr_irq_done = 1u;

    ST_LIVE("xnu_live_storage_pwr_irq_state", pwr_state);
    ST_LIVE("xnu_live_storage_pwr_irq_io", io_level);
    ST_LIVE("xnu_live_storage_pwr_irq_ctl_before", ctl_before);
    ST_LIVE("xnu_live_storage_pwr_irq_ack", ack);
    ST_LIVE("xnu_live_storage_pwr_irq_ctl_after", ctl_after);
    ST_LIVE("xnu_live_storage_pwr_irq_io_level", io_level);
    ST_LIVE("xnu_live_storage_pwr_irq_pad_ctl_before", pad_before);
    ST_LIVE("xnu_live_storage_pwr_irq_pad_ctl_after", pad_after);
}

/*
 * **The arming, and it runs BEFORE rung 7's power byte - the first rung of this ladder that does not
 * simply append.** The driver's own order is registration-then-power and it is not close:
 * `devm_request_threaded_irq` is at `:2937` and `mmc_power_up` runs from `mmc_rescan` a call graph
 * later, so an arm that armed *after* the byte would send the byte into a line owned by nobody - which
 * is rung 7's press, i.e. the rung would be unrunnable. What the append rule protects is the *cells*,
 * and none of them moves: the registration writes this image's own table and no device register, and
 * the arming writes four GICC distributor words no earlier rung reads (`xnu_live_irq_line_*` has no
 * other producer, which the build asserts). The byte still runs last among the device acts, and
 * `_pwr_irq_calls_once_before_byte` is the cell that states it - at the moment the byte is written,
 * this client has never been called.
 *
 * **`always_inline`, and the reason is rung 6's.** The build asserts that the two client-API calls are
 * made from `entry_storage_probe`'s own body (`bl` sites counted in that body's disassembly), because a
 * helper the build cannot see into is a helper whose calls are in another function while the record says
 * they are this rung's - exactly the way `st_branch_enable`'s four stores came out of the GCC window and
 * the census read an empty set as a clean one. `static` before the attribute because GCC refuses
 * `always_inline` on a function with external linkage, and it is `static` for the same reason it is one
 * caller.
 */
__attribute__((always_inline)) static inline void st_pwr_irq_arm(void)
{
    uint32_t rc;
    const uint32_t refCon = 0u;

    if (g_pwr_irq_arms != 0u)
        return;
    g_pwr_irq_arms = 1u;

    /*
     * §1.5's two addresses, in one run, before the byte, through the same helper rung 5 read them
     * with. `core_mem`'s is 705's own before-value and 706's own after-value re-read
     * (`_clk_vendor_*`, `_clk_set_vendor_after = 0`); `hc_mem`'s has never been read by this project.
     */
    ST_LIVE("xnu_live_storage_pwr_irq_vendor_core", st_read32(ST_CORE_MEM_BASE + ST_VENDOR_SPEC));
    ST_LIVE("xnu_live_storage_pwr_irq_vendor_hc", st_read32(ST_HC_MEM_BASE + ST_VENDOR_SPEC));

    rc = entry_irq_register_client(ST_PWR_IRQ_INTID, (uint32_t)(uintptr_t)st_pwr_irq, refCon);
    ST_LIVE("xnu_live_storage_pwr_irq_reg_rc", rc);
    ST_LIVE("xnu_live_storage_pwr_irq_reg_intid", ST_PWR_IRQ_INTID);
    ST_LIVE("xnu_live_storage_pwr_irq_reg_handler", (uint32_t)(uintptr_t)st_pwr_irq);
    ST_LIVE("xnu_live_storage_pwr_irq_reg_refcon", refCon);

    /*
     * The line's own distributor state, written once and read back. 500's four writes are the
     * architecture's requirement (a line in the wrong group is enabled, pending and never delivered)
     * and its before/after pairs are what tell "the machine's predecessor configured this line" from
     * "this arm had to" - which for a line Android's own kernel takes on this frame is the former.
     */
    ST_LIVE("xnu_live_storage_pwr_irq_arm_rc",
            entry_irq_enable_line(ST_PWR_IRQ_INTID, ST_PWR_IRQ_TARGET));

    ST_LIVE("xnu_live_storage_pwr_irq_calls_once_before_byte", g_pwr_irq_calls);
}

/*
 * **The probe's own end, and it is here because the rung's central timing question cannot be asked
 * from inside the handler.** 709's press ended on this interrupt, so whether the line is delivered
 * *during* the probe (interrupts unmasked, the byte store latching the status and the GIC raising it
 * straight away) or *after* it (masked through the probe and delivered in the idle loop) is
 * unmeasured - and the two make different claims about when the vendor's handshake can complete.
 * A count read at the probe's own end, compared with the same count read by the handler, separates
 * them, and it is a read and not a store, so the census does not move.
 */
static void st_pwr_irq_after(void)
{
    if (g_pwr_irq_arms == 0u)
        return;
    ST_LIVE("xnu_live_storage_pwr_irq_calls_probe_end", g_pwr_irq_calls);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 8 - two small helpers, one client, and one caller each */

#if STAGE90_XNU_STORAGE_PROBE >= 9
/*
 * **712: rung 9 - the driver's own completion, and the first rung whose act is a WAIT.**
 *
 * `sdhci_set_power` writes the byte rung 7 writes and then makes one more statement
 * (`sdhci.c:1371-1372`):
 *
 *     if (host->ops->check_power_status)
 *             host->ops->check_power_status(host, REQ_BUS_ON);
 *
 * and `sdhci_msm_check_power_status` (`sdhci-msm.c:2179-2210`) is *not* a device handshake. It
 * compares `req_type` against **two driver-side fields** (`msm_host->curr_pwr_state`,
 * `curr_io_level`) which the **handler's tail** fills (`:2092-2094`), and if the request is not
 * already satisfied it blocks on `wait_for_completion` (`:2205`) with **no bound at all**. So the
 * function is a cache read plus a block, its completion arrives only from the interrupt rung 8 owns,
 * and on the first call the predicate is false - which is why 708 could not take it and why 709's
 * press is the measurement of what it would have blocked on.
 *
 * **What this rung ports, and where it departs.** The predicate is the vendor's own, evaluated
 * first, and the `init_completion` branch is *taken* rather than skipped: on that branch the vendor
 * resets the completion and does **not** wait (`:2203`), so an arm that waited anyway would be
 * measuring its own code. The block is replaced by a bounded tick poll - and **the poll's end
 * condition is the CONTROLLER's own `CORE_PWRCTL_CTL` bit `BUS_SUCCESS`, not the image-side flag.**
 *
 * That choice is a defect this project names, met in advance. The probe runs inside Apple's
 * cache-off idle-exit window (`xnu_live_seam_sctlr = 0x30c57879`, `C` clear); the handler may run
 * with the caches **on**. A `g_pwr_irq_done` written by the handler can therefore sit in L1/L2 while
 * the probe's direct read of the same address is answered by DRAM - **one flag, two definitions**
 * ([[mi4-one-value-two-definitions]]) - and a poll armed with it would time out on a machine whose
 * handler had already run. `CTL` is the same event *through the device*: it is Strongly-ordered,
 * read as `0` by the probe on rung 7's press (`_pwr_ctl`), read as `0` and written as `0x01` by the
 * handler on rung 8's (`_pwr_irq_ctl_before` / `_pwr_irq_ctl_after`), and written by no other arm of
 * this ladder. **The image-side flag is still written and still published** (`_wait_done`), because
 * the disagreement between the two is the reading: a device ack with the flag clear is a
 * driver-side completion that did not cross the cache boundary - the shape 686's correction of 652
 * describes, one context over.
 *
 * **`noinline`, and it is the mirror of rung 6's `always_inline`.** The helpers that write device
 * registers are inlined into the probe so the store census can see them; this function writes no
 * device register at all, and what the build must check about it is its own bounded *shape* - the
 * budget constant, the ack it spins on, and the probe's single call to it. A body whose shape a
 * clause must read has to be a body.
 *
 * **What it does NOT do**: it does not run the three arms that may sleep (the vendor's own
 * `IRQF_ONESHOT` + NULL primary declares a threaded handler for exactly that reason, and this client
 * runs in `fleh_irq_kernel`'s frame); it does not mask the power events, which would take away the
 * interrupt the completion arrives on; it does not write `CORE_PWRCTL_CTL` (the request is the
 * byte's consequence and the ack is the handler's); it does not touch `hc_irq`; and it makes no store
 * to any device. Its whole device-facing shape is two reads of `CLOCK_CONTROL 0x2C` either side of
 * the wait and the polled `CORE_PWRCTL_CTL` byte.
 *
 * **710 adds the one mechanism this wait was missing and changes nothing above.** Rung 9 replaced the
 * block with a spin and kept the context the block was made in; rung 10 measured that the context, and
 * not the device, is what the spin was measuring (the completion is there within 20 ms of the byte and
 * the handler arrives the moment the payload's idle-exit code lifts `I` - experiment-717). So the spin
 * now runs with `I` clear, bounded by the same switch, with the saved CPSR restored afterwards, and
 * with one new cell (`_wait_cpsr_after`) beside the one whose *value* is the arm (`_wait_cpsr`). **The
 * `cpsie` is a mask coming off for a bounded spin and NOT a sleep**: the two statements above about
 * the arms that may sleep stand unchanged, and the vendor's `wait_for_completion` remains unported in
 * the one respect that matters here - this poll cannot yield to another thread, because this image has
 * no thread to yield to. What it can do is let the interrupt in, which is what the block needed.
 */
__attribute__((noinline)) static void st_pwr_wait(void)
{
    const uint32_t req = ST_REQ_BUS_ON;   /* sdhci.c:1372 - PASS A's own request, and the only one */
    uint32_t state_before, io_before, done_before, ctl_before, ctl_after, ctl_now;
    uint32_t t0, now, polls = 0u, inner, timeout = 0u, cpsr = 0u;
#if STAGE90_XNU_STORAGE_PROBE >= 10
    /* rung 10's two, declared under the guard so that the arm below this rung compiles without them:
     * an unused declaration is a -Werror diagnostic and a `cpsr_saved` written and never read would be
     * a second definition of "the mask was restored" with no reader. */
    uint32_t cpsr_saved = 0u, cpsr_after = 0u;
#endif /* STAGE90_XNU_STORAGE_PROBE >= 10 - the mask that comes off, and the state it is put back in */
    uint32_t cc_before, cc_after;

    /*
     * The two readings 711 section 3 left open: the same register either side of the wait, which is
     * a hundred milliseconds or a bounded fraction of one - long enough to say whether the bit two
     * consecutive boots disagreed about is a race or the machine's state.
     */
    cc_before = (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL);

    ctl_before   = (uint32_t)*(volatile uint8_t *)(uintptr_t)(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_CTL);
    state_before = g_pwr_curr_state;
    io_before    = g_pwr_curr_io;
    done_before  = (((req & state_before) | (req & io_before)) != 0u) ? 1u : 0u;

    ST_LIVE("xnu_live_storage_pwr_wait_req", req);
    ST_LIVE("xnu_live_storage_pwr_wait_bound", (uint32_t)STAGE90_XNU_PWR_WAIT_TICKS);
    ST_LIVE("xnu_live_storage_pwr_wait_ctl_before", ctl_before);
    ST_LIVE("xnu_live_storage_pwr_wait_state_before", state_before);
    ST_LIVE("xnu_live_storage_pwr_wait_io_before", io_before);
    ST_LIVE("xnu_live_storage_pwr_wait_done_before", done_before);

    t0  = (uint32_t)stage90_cntvct_read();
    now = t0;
    ST_LIVE("xnu_live_storage_pwr_wait_t0", t0);

    if (done_before != 0u) {
        /*
         * The vendor's own branch, and it does NOT wait (`:2202-2203`): the request is already
         * satisfied by what the handler has published, so the completion is *reset* - and the
         * vendor's comment says why, in the same words this reason needs: a completion raised before
         * this call would otherwise let the next wait return immediately. `_wait_reset` and the two
         * branch cells are published from both paths so the log always names which one ran.
         */
        g_pwr_irq_done = 0u;
        ST_LIVE("xnu_live_storage_pwr_wait_reset", 1u);
        ST_LIVE("xnu_live_storage_pwr_wait_cpsr", cpsr);
        /* This path waits for nothing, so no mask is taken off and none is put back: both CPSR cells
         * read 0 here, which is `this path did not read a CPSR` and not a value (the same reading
         * rung 9 gave `_wait_cpsr` on this branch). */
#if STAGE90_XNU_STORAGE_PROBE >= 10
        ST_LIVE("xnu_live_storage_pwr_wait_cpsr_after", cpsr_after);
#endif
    } else {
        ST_LIVE("xnu_live_storage_pwr_wait_reset", 0u);
#if STAGE90_XNU_STORAGE_PROBE >= 10
        /*
         * **710: the mask comes off for the poll, and this is the only shape of this wait that can
         * ever be satisfied.** Rung 10 measured the completion `20.172 ms` after the byte and the
         * handler `60.78 us` after the spin gave up, on a `20 ms` bound, against `100.122 ms` and
         * `60.05 us` on a `100 ms` one - so the poll has been measuring its own mask and no budget
         * can change that: the mask ends where the code that owns it returns and this spin is inside
         * that code (experiment-717 section 2). The vendor's `wait_for_completion` works because it
         * SLEEPS, i.e. because its context can be interrupted; the port keeps the predicate, the
         * request and the acknowledge, and to keep the wait satisfiable it has to give the poll the
         * one thing the block gave it. **Three properties of the shape below are deliberate.**
         *
         * 1. **The saved value is read FIRST and restored LAST, and the restore writes that register
         *    rather than `cpsid i`.** A constant would be a second definition of "as it found it"
         *    (this arm's own `_wait_cpsr` is the first), and if the caller's `I` had been clear an
         *    unconditional `cpsid i` would mask an interrupt the payload had left open - the probe
         *    would have changed the machine while appearing to clean up after itself.
         * 2. **`cpsie i` and not a CPSR write, so only `I` moves.** `F` is already clear in this
         *    context, the mode is untouched, and no other bit is rewritten.
         * 3. **The premise cell keeps its definition.** `_wait_cpsr` was and is *the CPSR the poll
         *    runs under* - rungs 9 and 10 read `0x80000093` there, this arm must read `0x80000013` -
         *    and `_wait_cpsr_after` is the new quantity, *the CPSR the probe gives back*, which must
         *    read `0x80000093`: the value rungs 9 and 10 read as their premise, which is what says
         *    the restore restored the SAVED state and not some other state that happens to look tidy.
         *
         * **What is unchanged is as much of the reading as what is new.** The poll's end condition is
         * still the CONTROLLER's own `CORE_PWRCTL_CTL` byte and not the image-side flag, the batch is
         * still 1024 reads between two samples of the clock, the bound is still the same switch, and
         * **no device store is added**: the ack the poll waits for is still the handler's store, and
         * a waiter that wrote it would be answering its own completion. This arm's write set is
         * rung 10's, measured by the same build clause, with one more image-side cell.
         *
         * **And this is the first interrupt this image takes inside the cache-off idle-exit window**
         * (`xnu_live_seam_sctlr` reads `C` clear on every run of this ladder) - the state the payload
         * masks `I` to avoid. Every earlier delivery happened at the end of the probe, on the way out
         * of the window. It is also the first arm on which the waiter reads the handler's two
         * driver-side fields AFTER the handler wrote them: on rungs 9 and 10 the flag reads happened
         * before the handler had run, so the cache-boundary question this rung's sibling comment
         * argues about has never had a comparison - and `_wait_done = 1` beside `_wait_ctl_after =
         * 0x01` is that comparison agreeing through DRAM.
         */
        __asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr_saved));
        __asm__ volatile ("cpsie i" ::: "memory");
#endif /* STAGE90_XNU_STORAGE_PROBE >= 10 - above this line the poll is masked, below it the same poll
        * runs with `I` clear and the saved value is still held for the restore at the loop's exit */
        __asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr));
        ST_LIVE("xnu_live_storage_pwr_wait_cpsr", cpsr);
        for (;;) {
            for (inner = 0u; inner < ST_PWR_WAIT_INNER; inner++) {
                polls++;
                ctl_now = (uint32_t)*(volatile uint8_t *)(uintptr_t)
                              (ST_CORE_MEM_BASE + ST_CORE_PWRCTL_CTL);
                if (ctl_now != 0u)
                    break;
            }
            ctl_now = (uint32_t)*(volatile uint8_t *)(uintptr_t)
                          (ST_CORE_MEM_BASE + ST_CORE_PWRCTL_CTL);
            if (ctl_now != 0u)
                break;
            now = (uint32_t)stage90_cntvct_read();
            if (now - t0 >= (uint32_t)STAGE90_XNU_PWR_WAIT_TICKS) {
                timeout = 1u;
                break;
            }
        }
        now = (uint32_t)stage90_cntvct_read();
        /* The mask goes back on from the SAVED register (property 1 above), and the reading that
         * says so is taken immediately afterwards, adjacent to the instruction it is about rather
         * than at the end of the function where anything else could have moved it. */
#if STAGE90_XNU_STORAGE_PROBE >= 10
        __asm__ volatile ("msr cpsr_c, %0" :: "r"(cpsr_saved) : "memory");
        __asm__ volatile ("mrs %0, cpsr" : "=r"(cpsr_after));
        ST_LIVE("xnu_live_storage_pwr_wait_cpsr_after", cpsr_after);
#endif /* STAGE90_XNU_STORAGE_PROBE >= 10 - the restore and its own reading, inside the branch that
        * took the mask off: the arm below this rung has nothing to restore and publishes no cell */
    }

    ctl_after = (uint32_t)*(volatile uint8_t *)(uintptr_t)(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_CTL);
    cc_after  = (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL);

    ST_LIVE("xnu_live_storage_pwr_wait_polls", polls);
    ST_LIVE("xnu_live_storage_pwr_wait_ticks", now - t0);
    ST_LIVE("xnu_live_storage_pwr_wait_timeout", timeout);
    ST_LIVE("xnu_live_storage_pwr_wait_ctl_after", ctl_after);
    ST_LIVE("xnu_live_storage_pwr_wait_done", g_pwr_irq_done);
    ST_LIVE("xnu_live_storage_pwr_wait_state_after", g_pwr_curr_state);
    ST_LIVE("xnu_live_storage_pwr_wait_io_after", g_pwr_curr_io);
    ST_LIVE("xnu_live_storage_pwr_wait_calls", g_pwr_irq_calls);
    ST_LIVE("xnu_live_storage_pwr_wait_cc_before", cc_before);
    ST_LIVE("xnu_live_storage_pwr_wait_cc_after", cc_after);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 9 - one body, one caller, and no device store in either */

#if STAGE90_XNU_STORAGE_PROBE >= 11
/*
 * **721: rung 11 - the driver's own first command, and the first act of this line that addresses the
 * card rather than the controller.**
 * `docs/experiments/experiment-721-the-rung-12-pre-registration-the-drivers-first-command-and-the-cards-first-answer.md`
 * is the pre-registration and this block is its sections 2, 3 and 4 read out of the vendor's source.
 *
 *   `sdhci_send_command` (`sdhci.c:1076-1155`) for a command with no data is: a bounded wait for the
 *   controller to release `SDHCI_CMD_INHIBIT` (`:1096`, `timeout = 10` ms at `:1084`), then
 *   `sdhci_writel(host, cmd->arg, SDHCI_ARGUMENT)` (`:1119`), then `sdhci_set_transfer_mode` - which
 *   RETURNS on its first line for a data-less command (`:985-986`, so `TRANSFER_MODE 0x0C` is NOT
 *   written by this rung) - then the flags decode (`:1130-1140`) and
 *   `sdhci_writew(host, SDHCI_MAKE_CMD(cmd->opcode, flags), SDHCI_COMMAND)` (`:1153`). The completion
 *   is the driver's IRQ; `sdhci_finish_command` (`:1158-1181`) reads `SDHCI_RESPONSE` only when the
 *   command asked for a response (`:1174`).
 *
 * **The two commands are the driver's own, and so are their arguments and their order.**
 * `mmc_go_idle` (`mmc_ops.c:96-127`) is `MMC_GO_IDLE_STATE` with argument 0 and `MMC_RSP_NONE`, and
 * `mmc_attach_mmc` (`mmc.c:1913-1923`) opens with `mmc_send_op_cond(host, 0, &ocr)` - argument 0, the
 * driver's own single-pass probe form (`mmc_ops.c:139-141`) - with `MMC_RSP_R3` = `MMC_RSP_PRESENT`
 * (`core.h:54`), a 48-bit response with no CRC and no opcode check. `mmc.c:1356-1359` is one function
 * doing them back to back ("`mmc_go_idle` is needed for eMMC that are asleep"), and that pair is what
 * this rung takes. The words that reach `SDHCI_COMMAND` are **`0x0000`** and **`0x0102`** -
 * `sdhci.h:57`'s `MAKE_CMD(c, f)` over `:1130-1140`'s decode. What is NOT ported is
 * `mmc_rescan_try_freq`'s sequence: between its `mmc_go_idle` and the MMC attach are CMD8 and the SDIO
 * and SD attach attempts (`core.c:3104-3111`), three probes an eMMC answers with timeouts by design.
 *
 * **THE ONE SUBSTITUTION IS THE ONE RUNG 9 MADE, for the same reason.** The driver's completion
 * arrives through `sdhci_irq`; this image enables no SDHCI interrupt, so the block becomes a BOUNDED
 * poll of `SDHCI_INT_STATUS`'s `SDHCI_INT_RESPONSE` bit - the register the handler would have read.
 * It is not merely a preference: the block's `hc_irq` is SPI 123 -> intid 155, a line this image hands
 * to nobody, and a delivery would arrive at the dispatcher as `_irq_other_count` and **END THE RUN**
 * (`entry_irq.c:549-556`).
 *
 * **The three gates, each of which REFUSES rather than proceeds:**
 *
 *   1. `INT_ENABLE 0x34` and `SIGNAL_ENABLE 0x38` must both read zero, read once before the first
 *      command. They are the only thing that can let the block raise the line nobody owns, so they
 *      are a gate and not a comment (`_cmd_enabled_out`, `_cmd_refused`).
 *   2. `PRESENT_STATE`'s `SDHCI_CMD_INHIBIT` must clear inside the driver's own 10 ms. Issuing a
 *      command into a controller still holding one is the one act here that could put a second
 *      transaction on the bus (`_cmdN_inhibit_timeout`).
 *   3. `INT_STATUS`'s command bits are read, written straight back (write-1-to-clear) and read again.
 *      **This is what makes the poll a reading**: the bit latches, so without the clear CMD1's poll
 *      would be satisfied by CMD0's own completion and the arm would report an answer nobody read
 *      (`_cmdN_stale`, `_cmdN_clear_after`).
 *
 * **What it does not write, and every one of these is a clause in `build_entry.sh` rather than a
 * promise here: no data-path register at all** (`BLOCK_SIZE`, `BLOCK_COUNT`, `TRANSFER_MODE` and
 * `BUFFER` are in no access of this function), **no `POWER_CONTROL 0x29`** (the probe's own window is
 * unchanged at `47 44 44 41`), **no GCC word, no `core_mem` word, no `BCR 0x04C0`**, and **no byte of
 * the medium**: CMD0 and CMD1 are `bc`/`bcr` commands with no data phase, so the controller has no
 * transfer to run and the card's stored content cannot change.
 */
#define ST_SDHCI_ARGUMENT          0x08u        /* sdhci.h:35  - 32-bit, sdhci.c:1119 */
#define ST_SDHCI_COMMAND           0x0Eu        /* sdhci.h:45  - 16-bit, sdhci.c:1153 */
#define ST_SDHCI_RESPONSE          0x10u        /* sdhci.h:60  - 32-bit, sdhci.c:1174 */
#define ST_SDHCI_INT_STATUS        0x30u        /* sdhci.h:118 - 32-bit write-1-to-clear */
/* **`INT_ENABLE` WAS `READ ONLY in this image` UNTIL 729, AND RUNG 13 IS WHAT MADE THAT FALSE.**
 * Rung 11's gate reads it and rung 13's body writes it twice (the one-bit probe and its restore),
 * so the old comment described the image as it stood one rung earlier - a claim in a comment that
 * no build could contradict, which is this project's oldest defect wearing a register's name
 * ([[mi4-a-claim-in-a-comment-is-not-a-check]]). What is still true, and is the safety clause and
 * not a note: **every write to this register in this image is one bit (`SDHCI_INT_RESPONSE`), and
 * `SIGNAL_ENABLE 0x38` is never written at all** - `build_entry.sh` asserts both sets exactly, and
 * the conjunction of the two registers is what would raise intid 155. */
#define ST_SDHCI_INT_ENABLE        0x34u        /* sdhci.h:119 - read by rungs 11/12/13/14, written
                                                 * by 13 and 14: the one-bit enable and its restore */
#define ST_SDHCI_SIGNAL_ENABLE     0x38u        /* sdhci.h:120 - READ ONLY in this image, and that
                                                 * is the half of the pair every rung above 10 keeps
                                                 * at zero on purpose */
#define ST_SDHCI_CMD_INHIBIT       0x00000001u  /* sdhci.h:65  - the inhibit gate's bit */
#define ST_SDHCI_CMD_LINE_LEVEL    0x01000000u  /* `PRESENT_STATE 0x24` bit 24, the CMD line's own
                                                 * signal level - and **IT IS THE LINE'S INPUT
                                                 * LEVEL**: a card answering pulls it LOW, and the
                                                 * block reading back its OWN drive over the same
                                                 * pad does too, so a count of LOW samples is a
                                                 * reading about the PAD and not about the card.
                                                 * That is the whole point of rung 24: a line that
                                                 * never goes LOW was driven by nobody, and a line
                                                 * that moves was driven by something. The bit is
                                                 * NOT `ST_SDHCI_CMD_INHIBIT` (bit 0): inhibit is
                                                 * the block's own state machine and this is the
                                                 * pin, and the project has an arm (rung 19, 742)
                                                 * in which the two DISAGREE - bit 0 set at the
                                                 * last sample with three `0x24` reads in the same
                                                 * capture showing it clear. **READ ONLY UNTIL RUNG
                                                 * 24, WHICH COUNTS IT AND STILL WRITES NOTHING.** */
#define ST_SDHCI_CMD_RESP_NONE     0x00u        /* sdhci.h:52  */
#define ST_SDHCI_CMD_RESP_SHORT    0x02u        /* sdhci.h:54  */
#define ST_SDHCI_INT_RESPONSE      0x00000001u  /* sdhci.h:121 - the poll's own end condition */
#define ST_SDHCI_INT_CMD_MASK      0x010F0001u  /* sdhci.h:144 - the driver's own command set:
                                                  * RESPONSE | TIMEOUT | CRC | END_BIT | INDEX |
                                                  * AUTO_CMD_ERR. NOT `ERROR_MASK` (sdhci.h:142),
                                                  * which carries BUS_POWER (0x00800000) and the
                                                  * DATA_* bits: a poll that ended on one of those
                                                  * would be measuring the power block or a transfer
                                                  * this rung does not start. */
#define ST_SDHCI_INT_CMD_ERR       0x010F0000u  /* the same set without the completion bit */
#define ST_SDHCI_INT_TIMEOUT       0x00010000u  /* sdhci.h:130 - "the command went out and the card
                                                  * did not answer", and the one bit that separates
                                                  * that finding from every controller-side error */
#define ST_SDHCI_INT_ENABLE_CMD    0x000F0001u  /* rung 23's window: SDHCI_INT_RESPONSE (0x1) with the
                                                  * FOUR command-level error bits the driver's own
                                                  * `sdhci_send_command` waits on - TIMEOUT
                                                  * (0x00010000), CRC (0x00020000), END_BIT
                                                  * (0x00040000) and INDEX (0x00080000). **It is
                                                  * `ST_SDHCI_INT_CMD_MASK` MINUS `AUTO_CMD_ERR`
                                                  * (0x01000000)** and the omission is deliberate:
                                                  * AUTO_CMD_ERR is the block's own auto-command
                                                  * mechanism failing, not a fact about the card, so it
                                                  * could only widen the pre-registered answer space with
                                                  * a bit that names none of the four outcomes. The
                                                  * DATA_* half of the vendor's own eleven-bit set is
                                                  * omitted for the same reason one register over: this
                                                  * rung starts no transfer */
#define ST_MMC_RSP_PRESENT         0x00000001u  /* core.h:28 - MMC_RSP_PRESENT */
#define ST_MMC_RSP_136             0x00000002u  /* core.h:29 - MMC_RSP_136, the 136-bit response */
#define ST_MMC_RSP_CRC             0x00000004u  /* core.h:30 - MMC_RSP_CRC */
#define ST_MMC_RSP_BUSY            0x00000008u  /* core.h:31 - MMC_RSP_BUSY */
#define ST_MMC_RSP_OPCODE          0x00000010u  /* core.h:32 - MMC_RSP_OPCODE */
/*
 * **737: the four response flags of `sdhci.c:1131-1143`'s ladder, and the vendor's ordering is NOT
 * upstream Linux's.** This is the file the ladder is transcribed from, and in it
 *
 *     sdhci.h:52  SDHCI_CMD_RESP_NONE        0x00
 *     sdhci.h:53  SDHCI_CMD_RESP_LONG        0x01
 *     sdhci.h:54  SDHCI_CMD_RESP_SHORT       0x02
 *     sdhci.h:55  SDHCI_CMD_RESP_SHORT_BUSY  0x03
 *
 * while upstream Linux has LONG at 0x02 and SHORT at 0x00. **Read the vendor's file and not the
 * memory of the upstream one** - a rung that took the upstream numbers would write a 136-bit command
 * with the SHORT encoding and read a four-word response that was never sent, and nothing in this image
 * could tell that from a card that answered zero. `SDHCI_CMD_CRC 0x08` / `SDHCI_CMD_INDEX 0x10` are
 * `sdhci.h:47-48` and `SDHCI_MAKE_CMD` is `sdhci.h:57`; `MMC_RSP_R2` is `core.h:53`'s
 * `PRESENT|136|CRC`, and `MMC_CARD_BUSY` is `mmc.h:227`'s `0x80000000`, the bit `mmc_send_op_cond`'s
 * loop tests (`mmc_ops.c:158`) and therefore the driver's own "the card is out of reset" condition.
 */
#define ST_SDHCI_CMD_RESP_LONG     0x01u        /* sdhci.h:53  */
#define ST_SDHCI_CMD_RESP_SHORT_BUSY 0x03u      /* sdhci.h:55  */
#define ST_SDHCI_CMD_CRC           0x08u        /* sdhci.h:47  */
#define ST_SDHCI_CMD_INDEX         0x10u        /* sdhci.h:48  */
#define ST_MMC_RSP_R2  (ST_MMC_RSP_PRESENT | ST_MMC_RSP_136 | ST_MMC_RSP_CRC)  /* core.h:53 */
#if STAGE90_XNU_STORAGE_PROBE >= 30
/*
 * **791: RUNG 31 IS ONE BIT OUT OF THIS COMMAND FLAG WORD, AND THE CONSTANT IS WRITTEN AS AN
 * EXPRESSION SO THAT THE ONE BIT IS WHAT A READER SEES.** The rung sends `ALL_SEND_CID` - the same
 * opcode, the same argument, the same window, the same position in the driver order and the same
 * five-bit `INT_ENABLE 0x34` - with `MMC_RSP_CRC` (`core.h:30`) taken out and nothing else moved.
 *
 * **Why that one bit.** 791 censused every CMD2 this ladder has ever sent: nine arms carry its cells
 * and the immediate `ERR | CRC` (`0x00028000` at the FIRST poll) appears on exactly the two arms whose
 * CMD2 window carried the CRC enable. The command word is a property of the COMMAND and not of the
 * enable, and the ladder has now given three commands a wide enable - CMD1 (`0x0102`, 48-bit, CRC
 * check OFF) which COMPLETED, CMD3 (`0x030a`, 48-bit, CRC check ON) which latched `ERR | TIMEOUT`
 * with bit 17 NEVER set, and CMD2 (`0x0209`, **136-bit**, CRC check ON) which latched `ERR | CRC` at
 * poll 1 - so the correlate is the 136-BIT RESPONSE REQUEST and not the CRC check. **The missing
 * corner of that table is 136-bit with the CRC check off, and it is this constant.**
 *
 * **It is legal on the wire.** The card returns the CID either way; the block simply does not check
 * the CRC of the response it receives. The card is not asked for anything different, so an arm that
 * completes here has taken the CID and not merely removed a check.
 *
 * **And the arm adds no key and no address**: `_cid_flags` publishes this value and `_cid_word` /
 * `_cid_word_read` publish the folded word, so the whole change is readable out of keys the ladder
 * already carries. That is rung 23 and rung 30 shape one command over - ONE CONSTANT.
 */
#define ST_MMC_RSP_R2_NOCRC  (ST_MMC_RSP_R2 & ~ST_MMC_RSP_CRC)
#endif /* STAGE90_XNU_STORAGE_PROBE >= 30 - one bit out of the 136-bit branch flag word */
#define ST_MMC_CARD_BUSY           0x80000000u  /* mmc.h:227 - mmc_ops.c:158's loop condition */
#define ST_CMD_OP_ALL_SEND_CID     2u           /* mmc.h:31 - CMD2 bcr R2, mmc_ops.c:173 */
#define ST_CMD_OP_GO_IDLE_STATE    0u           /* mmc.h:29 - CMD0, mmc_ops.c:107 */
#define ST_CMD_OP_SEND_OP_COND     1u           /* mmc.h:30 - CMD1, mmc.c:1923 */
#if STAGE90_XNU_STORAGE_PROBE >= 18
#define ST_CMD_OP_SET_RELATIVE_ADDR 3u          /* mmc.h:32 - CMD3 ac [31:16] RCA R1,
                                                 * mmc_ops.c:194 mmc_set_relative_addr, called from
                                                 * mmc.c:1409 immediately after mmc_all_send_cid */
#define ST_MMC_RSP_R1  (ST_MMC_RSP_PRESENT | ST_MMC_RSP_CRC | ST_MMC_RSP_OPCODE)  /* core.h:51 */
/*
 * **822: the driver's flag word for CMD13, and it is NOT `ST_MMC_RSP_R1`.** `mmc_ops.c:479` sets
 * `cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC` - two SPI-mode bits that this host, being
 * non-SPI, never acts on, and which `sdhci_cmd_to_flags` does not read either. It is spelled as the
 * driver spells it rather than folded into `ST_MMC_RSP_R1`, because the point of rung 37's first two
 * assertions is that **the flag word and the command word are two different quantities and only one
 * of them reaches the bus.** `MMC_CMD_AC` is 0 (`core.h:47`), so it contributes nothing.
 */
#ifndef ST_MMC_RSP_SPI_S1
#define ST_MMC_RSP_SPI_S1  (1u << 7)                                  /* core.h:40 */
#define ST_MMC_RSP_SPI_S2  (1u << 8)                                  /* core.h:41 */
#define ST_MMC_RSP_SPI_R2  (ST_MMC_RSP_SPI_S1 | ST_MMC_RSP_SPI_S2)    /* core.h:70 */
#endif
#define ST_MMC_RSP_R1_SPI  ((uint32_t)(ST_MMC_RSP_R1 | ST_MMC_RSP_SPI_R2))
#define ST_MMC_RCA_1               0x00010000u  /* mmc.c:1400's `card->rca = 1` through mmc_ops.c:200's
                                                 * `cmd.arg = card->rca << 16` - the ladder's first
                                                 * NON-ZERO argument */
#endif /* STAGE90_XNU_STORAGE_PROBE >= 18 - three constants, declared only for the rung that uses them */
#if STAGE90_XNU_STORAGE_PROBE >= 34
#define ST_CMD_OP_SEND_CSD         9u           /* mmc.h:38 - CMD9 ac [31:16] RCA R2, and the same three
                                                 * things in the same shape as CMD3's line above:
                                                 * mmc_ops.c:296 mmc_send_csd calls
                                                 * mmc_send_cxd_native(card->host, card->rca << 16, csd,
                                                 * MMC_SEND_CSD), that function takes the opcode as an
                                                 * argument and sets `cmd.flags = MMC_RSP_R2 | MMC_CMD_AC`
                                                 * (mmc_ops.c:224), and mmc.c:1420 is the caller - the
                                                 * driver's own statement immediately after
                                                 * mmc_set_relative_addr. NOTE what the argument is:
                                                 * `card->rca << 16` with `card->rca = 1` assigned BY THE
                                                 * DRIVER at mmc.c:1400, so CMD9's argument is
                                                 * `ST_MMC_RCA_1` above and NOT any word the card returned -
                                                 * CMD3's response is R1 (mmc.h:32), which is card status
                                                 * and has no RCA field in it */
#endif /* STAGE90_XNU_STORAGE_PROBE >= 34 - one opcode, declared where it is used */
#if STAGE90_XNU_STORAGE_PROBE >= 35
#define ST_CMD_OP_SELECT_CARD      7u           /* mmc.h:36 - CMD7 ac [31:16] RCA R1, mmc_ops.c:33.
                                                 * It is the driver's statement IMMEDIATELY after
                                                 * `mmc_send_csd` - `mmc_select_card(card)` at
                                                 * mmc.c:1436 - and its response format is R1, the
                                                 * same as CMD3's and NOT R1b: mmc_ops.c:37 sets
                                                 * `MMC_RSP_R1 | MMC_CMD_AC`, so there is no busy
                                                 * window on DAT0 for this body to handle. (mmc.h:34
                                                 * and :35 give CMD5 and CMD6 R1b; read the column
                                                 * before assuming the neighbour's format.) */
#endif /* STAGE90_XNU_STORAGE_PROBE >= 35 - one opcode, declared where it is used */
#if STAGE90_XNU_STORAGE_PROBE >= 36
#define ST_CMD_OP_SEND_STATUS      13u          /* mmc.h:42 - CMD13 ac [31:16] RCA R1, mmc_ops.c:476.
                                                 * **It is NOT the driver's next statement in the
                                                 * init path** - that is CMD8 at mmc.c:1446 - and it is
                                                 * chosen here as a MEASUREMENT: it reads the card's
                                                 * status without changing it, which is what makes its
                                                 * `R1_CURRENT_STATE` the discriminator rung 36 left
                                                 * open. The driver's own use of it is mmc.c:1724-1727's
                                                 * `mmc_alive` - `return mmc_send_status(host->card,
                                                 * NULL);` - so the cell this rung publishes is the
                                                 * driver's own "is the card alive" verb. */
#endif /* STAGE90_XNU_STORAGE_PROBE >= 36 - one opcode, declared where it is used */
#if STAGE90_XNU_STORAGE_PROBE >= 37
#define ST_CMD_OP_SEND_EXT_CSD     8u           /* mmc.h:37 - CMD8 `adtc R1`, mmc_ops.c:335. **It is the
                                                 * driver's own next statement after CMD7** - that is
                                                 * `mmc.c:1447`'s `err = mmc_get_ext_csd(card, &ext_csd);`
                                                 * immediately below `mmc.c:1436`'s `mmc_select_card` that
                                                 * rung 36 sent - and it is the FIRST command in this
                                                 * ladder whose `cmd->data` is non-NULL. **`adtc` is the
                                                 * whole rung**: `mmc_ops.c:263` sets `MMC_CMD_ADTC` and
                                                 * `core.c:338` turns that into `cmd->data = mrq->data`,
                                                 * the pointer `sdhci.c:1146-1149` tests before it ORs
                                                 * `SDHCI_CMD_DATA 0x20` into the command word - so the word
                                                 * this rung puts on the register is `0x083A` and NOT the
                                                 * `0x081A` the five-bit mapping gives
                                                 * (see `ST_SDHCI_CMD_WORD_DATA`). Its argument is **0**:
                                                 * `mmc_ops.c:256`'s `cmd.arg = 0`, the same for every
                                                 * `mmc_send_cxd_data` caller - an RCA here would be CMD9's
                                                 * argument worn by a command that addresses the card as a
                                                 * whole. **NOTHING IS WRITTEN TO THE CARD**: `mmc_ops.c:267`
                                                 * sets `data.flags = MMC_DATA_READ`, so the 512 bytes move
                                                 * card-tohost and the only stores this rung makes are host
                                                 * registers this file already names */
#endif /* STAGE90_XNU_STORAGE_PROBE >= 37 - one opcode, declared where it is used */
#if STAGE90_XNU_STORAGE_PROBE == 19
#define ST_MMC_RSP_NONE 0u                      /* core.h:50 - MMC_RSP_NONE, and it is ALSO the value
                                                 * CMD0 carries: `sdhci_cmd_to_flags`
                                                 * (sdhci.c:1131-1143) tests `MMC_RSP_PRESENT` FIRST and
                                                 * falls to `SDHCI_CMD_RESP_NONE` when it is clear, then
                                                 * only ORs CRC and INDEX in - so CMD0's word is 0x0000
                                                 * and this rung's word is 0x0300, the ONE command this
                                                 * ladder has ever driven to a completion and a command
                                                 * told to expect nothing back */
#endif /* STAGE90_XNU_STORAGE_PROBE == 19 - the demand the rung above removes, named where it is used */
#if STAGE90_XNU_STORAGE_PROBE >= 20
#define ST_MMC_RSP_R1_NOIDX (ST_MMC_RSP_PRESENT | ST_MMC_RSP_CRC)  /* core.h:51's `MMC_RSP_R1` with
                                                 * `MMC_RSP_OPCODE` (`core.h:32`) taken out. It is NOT a
                                                 * name from `core.h` - this tree has no R-response
                                                 * without an opcode check - and it is written as an
                                                 * expression rather than as a number so that its one
                                                 * difference from `ST_MMC_RSP_R1` is readable here rather
                                                 * than reconstructed from a log. `PRESENT` keeps the
                                                 * 48-bit response demand, `CRC` keeps the check, and what
                                                 * goes is the `INDEX` bit the card would echo the opcode
                                                 * back in. */
#endif /* STAGE90_XNU_STORAGE_PROBE >= 20 - one constant, and it is rung 19's with one term removed */
#define ST_CMD_INHIBIT_TICK_BUDGET 192000u      /* the driver's `timeout = 10` ms (sdhci.c:1084),
                                                  * 10 ms x 19,200 ticks/ms */
#define ST_CMD_INHIBIT_INNER       256u         /* the clock is sampled once per batch, so the
                                                  * sampler is not the thing being measured */
#define ST_CMD_DONE_TICK_BUDGET    23040000u    /* 1,200 ms at 19,200,000 Hz - just above the SDHCI
                                                  * specification's fixed ~1 s command timeout, so
                                                  * that the block's OWN timeout is the event this
                                                  * bound can see. If it is not, `_cmdN_timeout`
                                                  * firing with no error bit is a reading about the
                                                  * controller's timeout being longer than 1.2 s. */
#define ST_CMD_DONE_INNER          1024u
/* **RUNG 40's OWN BOUND, AND IT IS A TRANSFER BOUND AND NOT A COMMAND ONE.** At the clock this
 * ladder has set (400 kHz, rung 6) a 512-byte block takes a little over 10 ms to shift out, and the
 * rung-39 press measured that the 128-iteration read loop can finish BEFORE the card's own data
 * arrives (`_ext_words_gated = 0`, `_ext_data_avail_seen = 0`, 128 zero words) - so each word must be
 * waited for, and the wait must be bounded so a card that never delivers does not hang the boot. This
 * bound covers the WHOLE 128-word transfer at 400 kHz with generous headroom: 20 ms at 19,200,000 Hz.
 * It is the read-side sibling of `ST_CMD_DONE_TICK_BUDGET` and it bounds a WAIT, not the command. */
#define ST_EXT_DATA_TICK_BUDGET    384000u       /* 20 ms at 19,200,000 Hz */
#define ST_EXT_DATA_INNER          256u          /* the clock is sampled once per batch */
#define ST_CMD_INHIBIT_SAMPLES     1024u        /* 724's sampling window: the first 1024 poll
                                                 * iterations (~240 us at the measured 234 ns per
                                                 * read) - CMD_INHIBIT's rise and fall are at the
                                                 * START of the transmission, and sampling every
                                                 * iteration would double the poll's device traffic
                                                 * and halve the run's coverage for no reading */
/*
 * **737: THE THREE WORDS THIS LADDER PUTS ON `SDHCI_COMMAND 0x0E`, ASSERTED HERE AT COMPILE TIME.**
 *
 * The decode below is `sdhci.c:1131-1143`'s ladder, and until this rung no check anywhere read its
 * output: the build's `st_send_command` clause asserts the ACCESSES of the body (which registers, how
 * many, in what order) and never the VALUE it stores, so a reordered arm - the 136-bit test after the
 * busy test, say - would have put a different word on the wire while every set, count and cell the
 * record names still read the way the record says. That is `[[mi4-one-value-two-definitions]]` one
 * level down: the same command described by a source comment and by an immediate.
 *
 * **The two words below the rung are not decoration.** `0x0000` and `0x0102` are the words rungs 11,
 * 12, 13, 14 and 15 put on the bus and read back (`_cmd0_word`, `_cmd1_word` in four archived logs),
 * so an edit that moved one of them would move a pressed arm's own evidence - and the assertion makes
 * that a build failure instead of a discrepancy nobody looks for.
 *
 * They are macros and not an `inline` function because a function call is not an integer constant
 * expression and `_Static_assert` needs one. One definition, used by the body and by the assertions,
 * so a body that stopped agreeing with them cannot exist.
 */
#define ST_SDHCI_CMD_FLAGS(fl)                                                    \
    (((((uint32_t)(fl)) & ST_MMC_RSP_PRESENT) == 0u)                              \
         ? ST_SDHCI_CMD_RESP_NONE                                                 \
         : (((((uint32_t)(fl)) & ST_MMC_RSP_136) != 0u)                           \
                ? ST_SDHCI_CMD_RESP_LONG                                          \
                : (((((uint32_t)(fl)) & ST_MMC_RSP_BUSY) != 0u)                   \
                       ? ST_SDHCI_CMD_RESP_SHORT_BUSY                             \
                       : ST_SDHCI_CMD_RESP_SHORT))                            \
     | (((((uint32_t)(fl)) & ST_MMC_RSP_CRC) != 0u) ? ST_SDHCI_CMD_CRC : 0u)      \
     | (((((uint32_t)(fl)) & ST_MMC_RSP_OPCODE) != 0u) ? ST_SDHCI_CMD_INDEX : 0u))

#define ST_SDHCI_CMD_WORD(op, fl)                                                 \
    ((((uint32_t)(op) & 0xFFu) << 8) | (ST_SDHCI_CMD_FLAGS(fl) & 0xFFu))

_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_GO_IDLE_STATE, 0u) == 0x0000u,
               "CMD0's word is 0x0000: opcode 0, no response, no CRC, no index");
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_SEND_OP_COND, ST_MMC_RSP_PRESENT) == 0x0102u,
               "CMD1's word is 0x0102: opcode 1, RESP_SHORT (the vendor's 0x02 and not upstream's 0x00)");
#if STAGE90_XNU_STORAGE_PROBE >= 16
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_ALL_SEND_CID, ST_MMC_RSP_R2) == 0x0209u,
               "CMD2's word is 0x0209: opcode 2, RESP_LONG 0x01 | CRC 0x08 - the 136-bit arm of the "
               "driver's ladder, which is tested BEFORE the busy arm and carries no INDEX");
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 30
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_ALL_SEND_CID, ST_MMC_RSP_R2_NOCRC) == 0x0201u,
               "rung 31's word is 0x0201: opcode 2, RESP_LONG 0x01 and NO CRC - the 136-bit word with "
               "its 0x08 taken out, which is the ONE BIT this rung moves. The word is pinned HERE "
               "because the build clause that reads this rung out of the LINKED image can see the "
               "flags ARGUMENT at the call site and cannot see the word the callee folds from it: one "
               "assertion on each side of that call, and neither is a sentence in a comment");
_Static_assert(ST_MMC_RSP_R2_NOCRC == (ST_MMC_RSP_R2 & ~ST_MMC_RSP_CRC),
               "the constant is rung 16's word with `MMC_RSP_CRC` removed and not a new name for "
               "something else, so the distance between this arm and the one below it is readable "
               "here without resolving a number out of a log");
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 34
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_SEND_CSD, ST_MMC_RSP_R2) == 0x0909u,
               "CMD9's word is 0x0909: opcode 9, RESP_LONG 0x01 | CRC 0x08 - the 136-bit arm of the "
               "driver's ladder, the same arm CMD2 takes, and it carries no INDEX because "
               "`MMC_RSP_R2` (core.h:53) is PRESENT|136|CRC with no OPCODE bit. The word is pinned "
               "HERE for the reason rung 31's twin names: the build clause on the other side reads "
               "the flags ARGUMENT out of the LINKED image at the call site and cannot see the word "
               "the callee folds from it, so one assertion sits on each side of that call");
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 35
/*
 * **RUNG 36'S CONSTANTS, ASSERTED HERE FOR THE REASON THE CSD'S WORD IS ASSERTED ONE BLOCK UP - AND
 * THE FIRST DRAFT OF THIS BLOCK WAS WRONG, WHICH IS WHY IT IS ASSERTED RATHER THAN NARRATED.**
 *
 * 819 wrote `_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_SELECT_CARD, ST_MMC_RSP_R1) == 0x031Au, ...)`
 * and the compiler refused it, correctly. `ST_SDHCI_CMD_WORD` is `(opcode << 8) | flags`, and
 * `0x031A` is **opcode 3's** word: the CSD body above it is `0x0909` because CMD9 is opcode 9 and the
 * CMD3 arm asserts `0x031A` because CMD3 is opcode 3. **CMD7 is opcode 7, so its word is `0x071A`.**
 * The mistake was reading `0x031A` as *"the driver's `MMC_RSP_R1` word"* when it is *"CMD3's word, one
 * of whose two halves is `MMC_RSP_R1`"* - **one value, two definitions, in the rung whose prose is a
 * correction of that exact class** (`[[mi4-one-value-two-definitions]]`), and it was caught by the
 * assertion and not by the paragraph three hundred lines above that got the same thing right.
 * **This is the argument for the assertion, made by the assertion.**
 *
 * `MMC_RSP_R1` is `PRESENT | CRC | OPCODE` and its `ST_SDHCI_CMD_FLAGS` byte is **`0x1A`** - which is
 * exactly the byte the ladder removed the INDEX bit from on CMD3 at rung 21, where `0x1A` became
 * `0x0A` (`ST_MMC_RSP_R1_NOIDX`, live word `0x030A`). **So the bit 817 is about and the byte this
 * rung sends are separated from CMD3's live word by exactly the opcode and that one bit**, and both
 * halves of that statement are asserted below.
 */
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_SELECT_CARD, ST_MMC_RSP_R1) == 0x071Au,
               "CMD7's word is 0x071A: opcode 7, RESP_SHORT 0x02 | CRC 0x08 | INDEX 0x10 = 0x1A. The "
               "opcode is what makes it CMD7's and not CMD3's - the flag byte is the driver's own and "
               "is byte-identical to the one rung 21 removed a bit from");
_Static_assert((ST_SDHCI_CMD_WORD(ST_CMD_OP_SELECT_CARD, ST_MMC_RSP_R1) & 0xFFu) ==
               (ST_SDHCI_CMD_WORD(ST_CMD_OP_SET_RELATIVE_ADDR, ST_MMC_RSP_R1) & 0xFFu),
               "and this is 817's finding as arithmetic rather than as prose: the two commands' FLAG "
               "BYTES are the same 0x1A, so this rung sends the byte CMD3 does not - the INDEX bit "
               "included - and the two words differ only by the opcode in bits 15:8");
_Static_assert(ST_MMC_RSP_R1 == (ST_MMC_RSP_R1_NOIDX | ST_MMC_RSP_OPCODE),
               "the FLAGS this rung sends are the ladder's live CMD3 flags with ONE term back, so the "
               "distance between the two arms of this pair is readable in this file and does not have "
               "to be reconstructed from two logs - which is 817's finding, made a build refusal");
/*
 * **AND THE SECOND IS 819'S DEFECT MADE A PREDICATE RATHER THAN A SENTENCE.** Rung 35's outcome table
 * classified `CSD_STRUCTURE` against `{1, 2}` - the SD family's reading of a field this card reads as
 * an eMMC - and the press answered a genuine CSD into the row that calls it a phantom. The vendor's
 * predicate is `mmc.c:159`'s `if (csd->structure == 0)` and nothing else, so it is written here as
 * the vendor's own test and ASSERTED over the values that have to pass: a rung that narrows it again
 * fails the build instead of misreading a press.
 */
#define ST_CSD_STRUCTURE_REJECTED 0u
#define ST_CSD_STRUCTURE_IS_CSD(s) ((s) != ST_CSD_STRUCTURE_REJECTED)
_Static_assert(!ST_CSD_STRUCTURE_IS_CSD(ST_CSD_STRUCTURE_REJECTED),
               "0 is the ONE structure the vendor rejects (mmc.c:159), so the predicate must be false "
               "there and this line is the half of it that says the name means what it says");
_Static_assert(ST_CSD_STRUCTURE_IS_CSD(1u) && ST_CSD_STRUCTURE_IS_CSD(2u) &&
               ST_CSD_STRUCTURE_IS_CSD(3u),
               "1 and 2 are the MMC CSD revisions and 3 is what an eMMC v4.4/4.41 card reports - the "
               "value the rung-35 press MEASURED. A predicate that rejected 3 would call this card's "
               "own CSD a phantom, which is exactly what rung 35's table did");
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 36
/*
 * **RUNG 37'S CONSTANTS, AND THE FIRST ASSERTION IN THIS FILE WHOSE SUBJECT IS A BIT THAT IS
 * DECLARED AND NEVER SENT.**
 *
 * `mmc_ops.c:479` sets `cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC` for CMD13 - **not the
 * same value as CMD7's flags**, and the difference is two SPI-mode bits. This rung sends the driver's
 * word verbatim, so the first half here is that the word is what `mmc_ops.c:479` says it is; the
 * second half is that those two bits do not reach the COMMAND register, which is a property of
 * `sdhci_cmd_to_flags` and of nothing else.
 */
_Static_assert(ST_MMC_RSP_R1_SPI == 0x00000195u,
               "the driver's flag word for CMD13 is 0x195 = MMC_RSP_R1 (0x15) | MMC_RSP_SPI_R2 "
               "(MMC_RSP_SPI_S1 0x80 | MMC_RSP_SPI_S2 0x100, core.h:40-41 and :70) - and it is NOT the "
               "0x15 every arm from rung 21 up calls 'the driver's flags'. Two numbers, one of which "
               "is CMD13's and one of which is CMD3's and CMD7's");
_Static_assert(ST_SDHCI_CMD_FLAGS(ST_MMC_RSP_R1_SPI) == ST_SDHCI_CMD_FLAGS(ST_MMC_RSP_R1),
               "**AND THIS IS THE RUNG.** sdhci.c:1131-1143 reads exactly five bits of cmd->flags - "
               "PRESENT (0), 136 (1), BUSY (3), CRC (2) and OPCODE (4) - so the two SPI bits fall off "
               "the mapping and the driver's 0x195 and the ladder's 0x15 produce THE SAME command "
               "byte 0x1A. **A reader who saw _sta_flags_driver = 0x195 beside a command word of "
               "0x0D1A and concluded the mapping was broken would be reading two quantities with one "
               "name** - and this assertion is what stops the next arm from 'fixing' a mapping that is "
               "not wrong");
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_SEND_STATUS, ST_MMC_RSP_R1_SPI) == 0x0D1Au,
               "CMD13's word is 0x0D1A: opcode 13 in bits 15:8, CMD7's own flag byte 0x1A in bits "
               "7:0. It differs from CMD7's 0x071A in the opcode and in NOTHING ELSE, which is the "
               "sentence a reader can check against _sel_word and _sta_word in one log");
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 37
/*
 * **RUNG 38'S CONSTANTS, AND THE FIRST TIME THIS FILE NAMES A REGISTER IT INTENDS TO PUT DATA
 * THROUGH.**
 *
 * Every constant above this block belongs to a command with no data phase. These belong to
 * `sdhci_prepare_data` (`sdhci.c:817`), which the driver calls between its inhibit wait and its
 * ARGUMENT write, and to the PIO half of it - the half this image can use, because a DMA engine
 * needs a PHYSICAL address and this image runs with its MMU on and has never translated one.
 * `sdhci_prepare_data`'s DMA arm has to be handed one, so **PIO is not a preference here, it is the
 * only arm that can be transcribed at all.**
 *
 * Three of these are the vendor's own lines and the rest are the vendor's own arithmetic written out
 * so that the build can assert a VALUE rather than a sentence. `sdhci.c:975` writes
 * `SDHCI_MAKE_BLKSZ(SDHCI_DEFAULT_BOUNDARY_ARG, data->blksz)` and `sdhci.h:261` makes that argument
 * `ilog2(512 * 1024) - 12` = **7**, so a 512-byte block is `0x7200` and NOT `0x0200`. **A reader who
 * wrote the block size alone would set the block's SDMA boundary to 0**, which the vendor's own
 * default never does - and the boundary is the top three bits of the same halfword, which is exactly
 * the shape of one value with two definitions that this project keeps meeting.
 *
 * **`ST_SDHCI_INT_DATA_AVAIL` is the one bit this rung adds to an enable window**, and the vendor's
 * own `sdhci_set_transfer_irqs` (`sdhci.c:806`) is where the pair comes from: its PIO arm is
 * `sdhci_clear_set_irqs(host, dma_irqs, pio_irqs)`, so the DATA availability bits go UP and the DMA
 * bits go DOWN. The ladder's window `0x000F0001` has never carried a DMA bit, so for this image that
 * call is the pair `DATA_AVAIL | SPACE_AVAIL` added and nothing removed.
 *
 * **AND CMD8'S FLAG WORD IS A THIRD ONE.** `mmc_ops.c:263` sets
 * `MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC` = `0x00B5`, where CMD7's is `0x15` and CMD13's is
 * `0x195` - three commands, three different driver flag words. `MMC_RSP_SPI_R1` is `MMC_RSP_SPI_S1`
 * **alone** (`core.h:39`), one bit, where CMD13's `MMC_RSP_SPI_R2` is two; and the third bit,
 * `MMC_CMD_ADTC` (`core.h:36`), is one `sdhci_cmd_to_flags` does not read at all - so the FIVE-BIT
 * MAPPING gives all three the flag byte `0x1A` and would put `0x081A` on this command's register.
 *
 * **AND `0x081A` IS NOT THE WORD CMD8 GETS, WHICH IS THE ONE NUMBER THIS RUNG EXISTS FOR.**
 * `sdhci.c:1146-1149` reads a SIXTH thing the mapping above does not - **the presence of a data
 * structure** - and ORs `SDHCI_CMD_DATA 0x20` (`sdhci.h:49`, "Data Present Select") into the flags
 * byte when `cmd->data != NULL`:
 *
 *     if (cmd->data || cmd->opcode == MMC_SEND_TUNING_BLOCK || ...)
 *             flags |= SDHCI_CMD_DATA;
 *
 * and `core.c:338` sets `mrq->cmd->data = mrq->data` for every request `mmc_send_cxd_data` builds,
 * so for CMD8 that condition is TRUE. **CMD8's command word is `0x083A`.** `0x081A` is what a reader
 * gets by transcribing the five-bit mapping and stopping, and a block told `0x081A` has been told
 * there is no data phase while `BLOCK_SIZE`, `BLOCK_COUNT` and `TRANSFER_MODE` are armed - so the
 * transfer never starts, the buffer never fills, and the arm's own poll reports a card that did not
 * send what it was never asked for. **That is `one value, two definitions` with the TWO VALUES IN
 * ONE WORD** - `0x081A` and `0x083A` differ by the one bit that means "this command has data" - and
 * it is met here from the SAFE side: every step of the chain is a `_Static_assert` below, and the
 * body's own word is published as `_ext_word` so the register's copy is a reading and not an
 * inference.
 */
#define ST_SDHCI_BLOCK_SIZE        0x04u        /* sdhci.h:30 - 16-bit, written at sdhci.c:974 */
#define ST_SDHCI_BLOCK_COUNT       0x06u        /* sdhci.h:33 - 16-bit, written at sdhci.c:976 */
#define ST_SDHCI_TRANSFER_MODE     0x0Cu        /* sdhci.h:37 - 16-bit, written at sdhci.c:1015 */
#define ST_SDHCI_BUFFER            0x20u        /* sdhci.h:62 - the PIO data port, and the one address
                                                 * in this file whose READ returns the card's data
                                                 * rather than a register's state */
#define ST_SDHCI_DATA_INHIBIT      0x00000002u  /* sdhci.h:66 - the second bit of the mask
                                                 * `sdhci_send_command` builds when `cmd->data != NULL`
                                                 * (sdhci.c:1093-1095). **This ladder's inhibit gate has
                                                 * never tested it**, because no rung above has sent a
                                                 * command with a data phase */
#define ST_SDHCI_DOING_READ        0x00000200u  /* sdhci.h:68 - the block's own statement that a read
                                                 * is in flight; published, never waited on */
#define ST_SDHCI_DATA_AVAILABLE    0x00000800u  /* sdhci.h:70 - PRESENT_STATE bit 11, the bit a PIO read
                                                 * waits on, and a bit no rung has named */
#define ST_SDHCI_INT_DATA_END      0x00000002u  /* sdhci.h:122 - the transfer's own completion */
#define ST_SDHCI_INT_DMA_END       0x00000008u  /* sdhci.h:123 - never set by this image, named only
                                                 * so that the mask below is built the way sdhci.h:148
                                                 * builds its own */
#define ST_SDHCI_INT_SPACE_AVAIL   0x00000010u  /* sdhci.h:124 */
#define ST_SDHCI_INT_DATA_AVAIL    0x00000020u  /* sdhci.h:125 - THIS rung's poll condition */
#define ST_SDHCI_INT_DATA_TIMEOUT  0x00100000u  /* sdhci.h:134 - the one failure a data transfer adds
                                                 * that a command cannot have, and the bit the count in
                                                 * TIMEOUT_CONTROL exists to bound */
#define ST_SDHCI_INT_DATA_CRC      0x00200000u  /* sdhci.h:135 */
#define ST_SDHCI_INT_DATA_END_BIT  0x00400000u  /* sdhci.h:136 */
#define ST_SDHCI_INT_ADMA_ERROR    0x02000000u  /* sdhci.h:139 */
#define ST_SDHCI_TRNS_BLK_CNT_EN   0x0002u      /* sdhci.h:39 - sdhci_set_transfer_mode's first term */
#define ST_SDHCI_TRNS_READ         0x0010u      /* sdhci.h:42 - the read direction bit */
#define ST_SDHCI_DEFAULT_BOUNDARY_ARG 7u        /* sdhci.h:260-261 - ilog2(512 * 1024) - 12 */
#define ST_SDHCI_MAKE_BLKSZ(dma, blksz) \
        ((((dma) & 0x7u) << 12) | ((blksz) & 0xFFFu))   /* sdhci.h:31 */
#define ST_SDHCI_BLOCK_SIZE_512 \
        ((uint32_t)ST_SDHCI_MAKE_BLKSZ(ST_SDHCI_DEFAULT_BOUNDARY_ARG, 512u))
#define ST_SDHCI_TRNS_READ_1BLK ((uint32_t)(ST_SDHCI_TRNS_BLK_CNT_EN | ST_SDHCI_TRNS_READ))
#define ST_SDHCI_INT_PIO_IRQS   ((uint32_t)(ST_SDHCI_INT_DATA_AVAIL | ST_SDHCI_INT_SPACE_AVAIL))
#define ST_SDHCI_INT_DATA_BITS  ((uint32_t)(ST_SDHCI_INT_DATA_END | ST_SDHCI_INT_DMA_END |        \
                                            ST_SDHCI_INT_SPACE_AVAIL | ST_SDHCI_INT_DATA_AVAIL | \
                                            ST_SDHCI_INT_DATA_TIMEOUT | ST_SDHCI_INT_DATA_CRC |  \
                                            ST_SDHCI_INT_DATA_END_BIT | ST_SDHCI_INT_ADMA_ERROR))

#define ST_MMC_CMD_ADTC            0x00000020u  /* core.h:36 - `MMC_CMD_ADTC` (1 << 5): the bit that
                                                 * makes mmc_send_cxd_data's command a data command
                                                 * AND the bit `sdhci_cmd_to_flags` does not read.
                                                 * `mmc_ops.c:263` is the only place this ladder's
                                                 * chain sets it, and `core.c:338` is where the
                                                 * driver turns the same fact into `cmd->data` */
#define ST_SDHCI_CMD_DATA          0x20u        /* sdhci.h:49 - `SDHCI_CMD_DATA`, the Data Present
                                                 * Select in the COMMAND register's flag byte. **The
                                                 * same NUMBER as `ST_MMC_CMD_ADTC` above and a
                                                 * DIFFERENT QUANTITY**: one is a bit the driver sets
                                                 * in its own flag word and the mapping reads nothing
                                                 * of, the other is the bit the block reads to learn
                                                 * that a transfer follows. They coincide because the
                                                 * vendor's own `sdhci.c:1146-1149` ORs this one in
                                                 * exactly when the driver has set that one - and
                                                 * `ST_SDHCI_CMD_WORD_DATA` below is where the two are
                                                 * joined, once, with a `_Static_assert` on each side */
#define ST_MMC_RSP_SPI_R1          ST_MMC_RSP_SPI_S1  /* core.h:39 - `MMC_RSP_SPI_R1`, which is
                                                 * `MMC_RSP_SPI_S1` ALONE - one bit, where CMD13's
                                                 * `MMC_RSP_SPI_R2` is two */
#define ST_MMC_RSP_R1_ADTC ((uint32_t)(ST_MMC_RSP_SPI_R1 | ST_MMC_RSP_R1 | ST_MMC_CMD_ADTC))
/*
 * **THE SIXTH TERM, AS A MACRO AND NOT AS A TYPED NUMBER, so that neither of the two command words
 * can be written down without the other being one line away.** `sdhci_send_command` builds the word
 * with `sdhci_cmd_to_flags`'s five bits and then ORs `SDHCI_CMD_DATA` in on a condition that is not
 * one of those bits; a ladder that transcribed only the first half would put `0x081A` on CMD8's
 * register, and **the block would then run a command with `BLOCK_COUNT` armed and no data phase** -
 * an arm whose every cell reads plausibly and whose transfer never happens. The safe form is the
 * pair: the five-bit word is `ST_SDHCI_CMD_WORD`, the driver's own word for a data command is this,
 * and the difference is asserted as a difference.
 */
#define ST_SDHCI_CMD_WORD_DATA(op, fl) \
        (ST_SDHCI_CMD_WORD((op), (fl)) | ((uint32_t)ST_SDHCI_CMD_DATA))
#define ST_CMD_OP_SEND_EXT_CSD     8u           /* this rung's own opcode is declared with the other
                                                 * four, in the `declared where it is used` block
                                                 * beside ST_CMD_OP_SEND_STATUS */

/* --- the timeout's own four values, all read at CAPABILITIES 0x40 ------------------------------ */
#define ST_SDHCI_TIMEOUT_CLK_MASK  0x0000003Fu  /* sdhci.h:182 */
#define ST_SDHCI_TIMEOUT_CLK_SHIFT 0u           /* sdhci.h:183 - ZERO, so the field is CAPABILITIES
                                                 * bits 5:0 and NOT bits 23:16. A reader who assumed
                                                 * the SDHCI spec's own layout would read this
                                                 * device's timeout clock out of the wrong nibbles */
#define ST_SDHCI_TIMEOUT_CLK_UNIT  0x00000080u  /* sdhci.h:184 - bit 7, and it is SET on this part */
#define ST_SDHCI_TOUT_BASE_DIVISOR 4000u        /* sdhci.c:781's `host->clock / 1000` composed with
                                                 * sdhci.c:783's `/ 4`, which sdhci-msm.c:2906 turns
                                                 * on whenever ALWAYS_USE_BASE_CLOCK
                                                 * (sdhci-msm.c:2899) is set. **Both are set on this
                                                 * host, so the CAPABILITIES timeout clock above is
                                                 * NEVER READ by sdhci_calc_timeout on this device** -
                                                 * and the two formulas disagree, which this rung
                                                 * publishes rather than chooses between */
#define ST_SDHCI_TOUT_STEP0_NUMER ((uint32_t)((1u << 13) * 1000u))  /* sdhci.c:784's `(1 << 13) * 1000`
                                                 * - **the NUMERATOR and not a time**: the step-0 bound
                                                 * is this divided by the timeout clock, and the
                                                 * divisor here is `host->clock / 4000` (the base-clock
                                                 * arm above) and NOT the CAPABILITIES field three lines
                                                 * up. `ST_SDHCI_TOUT_BASE_DIVISOR` is where 4000 comes
                                                 * from, and at this ladder's 400 kHz it is 100, so
                                                 * step-0 is 81,920 us = 81.92 ms - **which is why a
                                                 * 512-byte read at 400 kHz (10.24 ms) fits inside the
                                                 * reset value's own bound and the count still has to be
                                                 * COMPUTED for the target rather than for the step size** */

/* --- the card's own CSD, which is the other half of the timeout target -------------------------- */
#define ST_TACC_EXP_COUNT          8u           /* mmc.c:38-40 */
#define ST_TACC_MANT_COUNT         16u          /* mmc.c:42-45 */
#define ST_EXT_CSD_LEN             512u         /* mmc_send_cxd_data's len (mmc_ops.c:265, :335) */
#define ST_EXT_CSD_WORDS           (ST_EXT_CSD_LEN / 4u)
#define ST_EXT_MMC_MULT            10u          /* core.c:1268 - `mmc_card_sd(card) ? 100 : 10`, and
                                                 * this card is not SD, so 10. core.c:1274's
                                                 * `mult <<= card->csd.r2w_factor` is the WRITE arm and
                                                 * CMD8 reads, so it is not taken */
#define ST_EXT_CSD_OFF_REV         192u         /* mmc.h:303 - `RO` */
#define ST_EXT_CSD_OFF_STRUCTURE   194u         /* mmc.h:304 - `RO` */
#define ST_EXT_CSD_OFF_CARD_TYPE   196u         /* mmc.h:305 - `RO` */
#define ST_EXT_CSD_OFF_SEC_CNT     212u         /* mmc.h:312 - `RO, 4 bytes`, little-endian, and the
                                                 * density this ladder has never had. `mmc.c:333-336`
                                                 * assembles it the same way `ST_EXT_CSD_WORD` does,
                                                 * byte 0 into bits 7:0 - the two agree or one of them
                                                 * is wrong, and the source is where that is decided */
/*
 * **THE FOUR BYTES OF `SEC_COUNT` AS `mmc.c:333-336` ASSEMBLES THEM, and it is a macro because the
 * alternative is four hand-written shifts.**
 *
 * `card->ext_csd.sectors` is
 *
 *     ext_csd[EXT_CSD_SEC_CNT + 0] << 0  |  ext_csd[EXT_CSD_SEC_CNT + 1] << 8  |
 *     ext_csd[EXT_CSD_SEC_CNT + 2] << 16 |  ext_csd[EXT_CSD_SEC_CNT + 3] << 24
 *
 * - a LITTLE-ENDIAN assembly out of four separately indexed bytes, which is the same value as the
 * 32-bit word they sit in **only because `ST_EXT_CSD_BYTE` reads byte `off % 4` of word `off / 4` at
 * shift `8 * (off % 4)`**, i.e. the same little-endian order. Both spellings are in this file and the
 * `_Static_assert` below is what holds them together: a reader who later changed one - to big-endian,
 * or to a word read at a different offset - would otherwise change a capacity without changing any
 * cell's name. Sector count is a quantity this ladder's whole purpose is denied without, so the two
 * readings agreeing is worth one assertion.
 */
#define ST_EXT_CSD_WORD(off)                                                    \
        ((uint32_t)(ST_EXT_CSD_BYTE(off)        | (ST_EXT_CSD_BYTE((off) + 1u) << 8)  | \
                    (ST_EXT_CSD_BYTE((off) + 2u) << 16) | (ST_EXT_CSD_BYTE((off) + 3u) << 24)))

/*
 * **THE 512 BYTES, AND THEY ARE `.bss` RATHER THAN A STACK FRAME ON PURPOSE.** The driver `kmalloc`s
 * them (`mmc.c:214-218`) and never puts 512 bytes on a stack; this image has one stack with no
 * watermark measured, so the buffer is a file-scope object and the body's frame stays the size every
 * rung above it has. **It is filled by `SDHCI_BUFFER` reads and by nothing else** - no DMA, no
 * engine, and therefore no physical address anywhere in this rung.
 */
static uint32_t st_ext_csd[ST_EXT_CSD_WORDS];

/* the vendor's own `ext_csd[off]` (`mmc.c:332`), over the word array above: byte `off` is byte
 * `off % 4` of word `off / 4`, little-endian, which is how a little-endian FIFO fills it */
#define ST_EXT_CSD_BYTE(off) \
        ((uint32_t)((st_ext_csd[(off) / 4u] >> (8u * ((off) % 4u))) & 0xFFu))

/*
 * **mmc.c:38-45's TWO TABLES, TRANSCRIBED, BECAUSE THE TIMEOUT TARGET IS A LOOKUP AND NOT AN
 * ARITHMETIC IDENTITY.** `csd->tacc_ns` is `(tacc_exp[e] * tacc_mant[m] + 9) / 10` (`mmc.c:168`)
 * where `m` is the CSD's TAAC time value and `e` its time unit - and the mantissa table is
 * `{0,10,12,13,15,20,25,30,35,40,45,50,55,60,70,80}`, which is NOT 10 per step and NOT a power of
 * two. **A reader who approximated it would compute a timeout target that is wrong in a direction
 * they cannot see**, which is the whole reason the driver carries a table. Every value here is
 * `mmc.c:38-40` and `mmc.c:42-45` in order.
 */
static const uint32_t st_tacc_exp[ST_TACC_EXP_COUNT] = {
    1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u
};
static const uint32_t st_tacc_mant[ST_TACC_MANT_COUNT] = {
    0u, 10u, 12u, 13u, 15u, 20u, 25u, 30u, 35u, 40u, 45u, 50u, 55u, 60u, 70u, 80u
};

/*
 * **THE LADDER'S FIRST PIECE OF CARD STATE THAT SURVIVES A COMMAND, and it is here because the
 * timeout target needs it and nothing else can supply it.**
 *
 * `mmc_set_data_timeout` (`core.c:1252`) computes a data transfer's timeout from `card->csd.tacc_ns`
 * and `card->csd.tacc_clks` - the card's own TAAC and NSAC - and the ladder's only source of those is
 * the CSD, which rung 34 read. **The CSD is not still in `RESPONSE` by the time CMD8 runs**: two
 * commands later (CMD7, CMD13) `RESPONSE + 0` holds CMD13's R1 and the four words rung 34 assembled
 * are gone. **So the ladder has to carry them**, exactly as the driver carries `card->csd` across
 * `mmc_select_card` and `mmc_send_status`, and this pair of objects is that carry: `st_csd_words` is
 * the assembled CSD and `st_csd_words_valid` says whether a CSD was ever assembled in this boot.
 *
 * **`valid` is published and is NOT a gate**, the same discipline as `_sel_pre_cid_sent` and
 * `_sta_pre_state`: a rung that refused to compute a timeout without a CSD would be a rung that
 * silently did nothing on the press where the CSD did not arrive. It computes anyway and says so in
 * the log, and the transfer's own outcome cells are what the reader weighs.
 */
static uint32_t st_csd_words[4];
static uint32_t st_csd_words_valid;

_Static_assert(ST_MMC_RSP_R1_ADTC == 0x000000B5u,
               "CMD8's driver flag word is 0xB5 = MMC_RSP_SPI_R1 (0x80, core.h:39) | MMC_RSP_R1 "
               "(0x15) | MMC_CMD_ADTC (0x20, core.h:36) - mmc_ops.c:263. It is NEITHER CMD7's 0x15 "
               "NOR CMD13's 0x195, and all three produce the same flag byte");
_Static_assert(ST_SDHCI_CMD_FLAGS(ST_MMC_RSP_R1_ADTC) == ST_SDHCI_CMD_FLAGS(ST_MMC_RSP_R1),
               "sdhci.c:1131-1143 reads five bits of cmd->flags - PRESENT (0), 136 (1), BUSY (3), "
               "CRC (2) and OPCODE (4) - so the SPI bit AND the ADTC bit both fall off the mapping "
               "and CMD8's command byte from THAT function alone is 0x1A, the same as CMD7's and "
               "CMD13's. **This assertion is TRUE and it is HALF THE STORY, which is why the two "
               "below it exist**: the five-bit mapping really does give 0x1A, and the register really "
               "does receive 0x3A. A reader who took this line as the last word would write 0x081A");
_Static_assert(ST_SDHCI_CMD_DATA == (uint32_t)ST_MMC_CMD_ADTC,
               "**ONE NUMBER, TWO NAMES, AND THE COINCIDENCE IS LOAD-BEARING.** SDHCI_CMD_DATA "
               "(sdhci.h:49) and MMC_CMD_ADTC (core.h:36) are both 0x20 - the block's Data Present "
               "Select and the driver's 'this command has a data phase'. They are NOT the same "
               "quantity and the vendor never equates them: sdhci.c:1146-1149 tests `cmd->data`, a "
               "POINTER, and core.c:338 is what makes that pointer non-NULL for exactly the commands "
               "mmc_send_cxd_data marks ADTC. This assertion pins the coincidence so that the "
               "condition below can be written once; if a future vendor moved either constant it "
               "would refuse rather than silently key the data phase off a bit the block reads "
               "differently");
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_SEND_EXT_CSD, ST_MMC_RSP_R1_ADTC) == 0x081Au,
               "CMD8's word WITHOUT the data bit is 0x081A: opcode 8 in bits 15:8, 0x1A in bits "
               "7:0. **This is the number the five-bit mapping alone produces and it is asserted "
               "here so that the number the register receives, one line below, is a DIFFERENCE and "
               "not a mystery**: a reader who wrote 0x081A into the body would arm BLOCK_SIZE, "
               "BLOCK_COUNT and TRANSFER_MODE and then tell the block there is no data phase");
_Static_assert(ST_SDHCI_CMD_WORD_DATA(ST_CMD_OP_SEND_EXT_CSD, ST_MMC_RSP_R1_ADTC) == 0x083Au,
               "**AND CMD8'S WORD IS 0x083A, which is what sdhci.c:1146-1149's sixth term makes "
               "of 0x081A**: opcode 8 in bits 15:8 and 0x3A = 0x1A | 0x20 in bits 7:0, where the "
               "0x20 is SDHCI_CMD_DATA - the Data Present Select - ORed in because cmd->data is "
               "non-NULL (core.c:338) for every request mmc_send_cxd_data builds. Three commands, "
               "three DRIVER flag words (0x15, 0x195, 0xB5), ONE flag byte 0x1A from the mapping, "
               "and TWO words that differ by the one DATA bit: **0x081A is the word for a command "
               "with no data phase and 0x083A is the word for this one.** The pair is the arm");
_Static_assert(ST_SDHCI_BLOCK_SIZE_512 == 0x7200u,
               "`SDHCI_MAKE_BLKSZ(SDHCI_DEFAULT_BOUNDARY_ARG, 512)` (sdhci.c:974, sdhci.h:31) is "
               "0x7200: SDMA boundary 7 in bits 14:12 and block size 512 in bits 11:0. **0x0200 is "
               "what a reader gets by writing the block size and forgetting the boundary**, and it "
               "is the reason this is asserted rather than commented");
_Static_assert(ST_SDHCI_TRNS_READ_1BLK == 0x0012u,
               "`sdhci_set_transfer_mode` (sdhci.c:979) starts from SDHCI_TRNS_BLK_CNT_EN and adds "
               "READ for a read. It does NOT add MULTI - sdhci.c:989-991 tests "
               "`mmc_op_multi(cmd->opcode) || data->blocks > 1` and CMD8 is neither, one block - and "
               "it does NOT add DMA, because this image takes the PIO arm");
_Static_assert(ST_SDHCI_INT_DATA_BITS == 0x0270003Au,
               "sdhci.h:148's SDHCI_INT_DATA_MASK as eight bits: DATA_END 0x2, DMA_END 0x8, "
               "SPACE_AVAIL 0x10, DATA_AVAIL 0x20, DATA_TIMEOUT 0x00100000, DATA_CRC 0x00200000, "
               "DATA_END_BIT 0x00400000, ADMA_ERROR 0x02000000. **Four of the eight are the "
               "transfer succeeding and four are it failing**; the rung publishes the two groups "
               "separately so a reader never has to split them by eye");
_Static_assert((ST_SDHCI_INT_DATA_BITS & ST_SDHCI_INT_CMD_MASK) == 0u,
               "the data bits and the command bits sdhci.h:144 names are DISJOINT - so a reader of "
               "this rung's INT_STATUS cells tells a command failure from a data failure by which "
               "mask the word falls in, with no denylist");
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 31
/*
 * **795 SECTION 7's REPAIR, AS ONE CONSTANT - and it is the first rung in this ladder whose act is a
 * REPAIR rather than a measurement.**
 *
 * `TIMEOUT_CONTROL 0x2E` is the register `sdhci_calc_timeout` feeds and `sdhci_prepare_data` stores
 * (`sdhci.c:827-828`), inside `if (data || (cmd->flags & MMC_RSP_BUSY))`. Every command this ladder
 * sends is data-less and none asks for `MMC_RSP_BUSY`, so the register has never been written by
 * anything - and rung 31's own capture says so: `_nidx_tout_ctl = 0x00000000`, the reset value.
 *
 * **The bound that value expresses is 665.2 us, and rung 31 MEASURED it twice in one boot**: CMD2 at
 * `_cid_ticks = 0x31e2` = 12,770 ticks and CMD3 at `_nidx_ticks = 0x31e4` = 12,772, both at 19,200,000
 * Hz - and CMD1 completes at 10,276 ticks = 535.2 us, 130 us inside it. So the ladder has never had a
 * timeout problem: it has had a bound 63 per cent of what a 136-bit response needs (795 section 5, with
 * its extrapolation named).
 *
 * **The value is 0x03 and the choice is arithmetic, not taste.** The register scales the bound by two
 * per step - `0x00` is 2^13 TMCLK cycles and the measurement is the calibration - so `0x01` = 1.33 ms,
 * `0x02` = 2.66 ms and `0x03` = **5.32 ms**. The larger of 795 section 5's two estimates for a 136-bit
 * response is 1,048.2 us, so `0x03` is a factor of **5.08** above it; and the driver's own poll bound is
 * `ST_CMD_DONE_TICK_BUDGET` = 23,040,000 ticks = **1.200 s**, so `0x03` is **225 times under** the
 * bound that would hide it. **The value is chosen to be wrong in neither direction**: too small and the
 * arm answers nothing new, too large and the poll's own bound becomes the event and the device timeout
 * stops being observable at all.
 *
 * **IT IS A WINDOW AND NOT A STATE CHANGE.** The byte is read before it is written and written back
 * after CMD2's window closes, by two bodies of their own (`st_tout_open`, `st_tout_restore`), so
 * `st_cmd_path`'s own device surface - which the rung-14 clause group asserts at the digit - does not
 * move, and the register is left exactly as this arm found it. CMD3 therefore runs at the RESET-VALUE
 * bound on this arm, which is deliberate and is 795 section 6's own test: a 48-bit response needs
 * 535.2 us and fits inside 665.2, so **if CMD2 completing is what lets CMD3 be answered, CMD3 answers
 * on this arm without any help from the raised bound** - and if CMD3 times out again, the protocol
 * account in 795 section 6 is wrong and the raised bound is not the reason.
 */
#define ST_SDHCI_TIMEOUT_CMD2 0x03u
_Static_assert(ST_SDHCI_TIMEOUT_CMD2 >= 0x01u,
               "the value must be ABOVE the reset value 0x00 or the store writes the byte this arm "
               "found and the whole repair is a no-op wearing a window");
_Static_assert(ST_SDHCI_TIMEOUT_CMD2 <= 0x0Eu,
               "the field is four bits (`SDHCI_TIMEOUT_CONTROL` bits [3:0]); a value above 0x0E would "
               "carry into the reserved half of the byte and is not a longer timeout");
_Static_assert((ST_SDHCI_TIMEOUT_CMD2 & 0xF0u) == 0u,
               "the value is the register's whole content, not a field of it: the byte is written "
               "outright and nothing is read-modified-written, so a value with high bits set would "
               "write them rather than preserve them");
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 18
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_SET_RELATIVE_ADDR, ST_MMC_RSP_R1) == 0x031Au,
               "CMD3's word is 0x031A: opcode 3, RESP_SHORT 0x02 | CRC 0x08 | INDEX 0x10. It is the "
               "ladder's FIRST INDEX - R1 (core.h:51) is PRESENT|CRC|OPCODE and MMC_RSP_OPCODE is "
               "the card echoing the opcode back, which is why this word is not `opcode << 8 | flags` "
               "in the shape the three below it have");
_Static_assert(ST_MMC_RCA_1 == (1u << 16),
               "this rung's argument is mmc.c:1400's `card->rca = 1` shifted by mmc_ops.c:200's "
               "`card->rca << 16`; a reader who finds 0x00010000 in a log has the derivation here");
#endif

#if STAGE90_XNU_STORAGE_PROBE == 19
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_SET_RELATIVE_ADDR, ST_MMC_RSP_NONE) == 0x0300u,
               "this rung's word is 0x0300: opcode 3 with NO response bits at all. It is rung 19's "
               "0x031A with the `INDEX` bit removed along with `RESP_SHORT` and `CRC`, because "
               "`MMC_RSP_NONE` is the absence of `MMC_RSP_PRESENT` and every other term in "
               "`sdhci_cmd_to_flags` is conditioned on it - so the two words differ by exactly the "
               "demand this rung is about, and the ladder's first `INDEX` bit does NOT survive it");
_Static_assert(ST_MMC_RSP_NONE == 0u,
               "the demand this rung removes is a literal zero, which is what makes it removable: the "
               "arm's one changed constant can be compared with `0u` in the log and in this file "
               "without a reader having to resolve a name");
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 20
_Static_assert(ST_SDHCI_CMD_WORD(ST_CMD_OP_SET_RELATIVE_ADDR, ST_MMC_RSP_R1_NOIDX) == 0x030Au,
               "this rung's word is 0x030A: opcode 3, RESP_SHORT 0x02 | CRC 0x08 and NO INDEX. It is "
               "rung 19's 0x031A minus the INDEX bit and NOTHING ELSE - the response is still demanded "
               "and the CRC is still checked - so the one bit this rung moves is the one bit the "
               "five-word rule is about");
_Static_assert(ST_MMC_RSP_R1_NOIDX == (ST_MMC_RSP_R1 & ~ST_MMC_RSP_OPCODE),
               "the constant is rung 19's with `MMC_RSP_OPCODE` removed and not a new name for "
               "something else: a reader who wants the distance between the two arms of this pair can "
               "read it here, in the same file, without resolving a number out of a log");
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 22
/*
 * **Rung 23's ONE CONSTANT, ASSERTED HERE BECAUSE NO BUILD CLAUSE CAN READ A VALUE.** The clause that
 * guards this body asserts its ACCESSES - the addresses, the widths, the counts, the order and the store
 * set - and its own text says so: a body's memory accesses and its `bl` sites carry no immediate. What
 * the window ENABLES is therefore carried by these six lines or by nothing, and 765 section 4 pinned
 * the value before the arm existed.
 */
_Static_assert(ST_SDHCI_INT_ENABLE_CMD == 0x000F0001u,
               "rung 23's enable word is 0x000F0001: the completion (0x1) with TIMEOUT (0x00010000), "
               "CRC (0x00020000), END_BIT (0x00040000) and INDEX (0x00080000). Five bits, and the "
               "number is written out because the whole arm is the distance between this word and rung "
               "21's lone 0x00000001");
_Static_assert((ST_SDHCI_INT_ENABLE_CMD & ST_SDHCI_INT_RESPONSE) != 0u,
               "the completion must still be enabled: `st_send_command`'s poll breaks on "
               "`ST_SDHCI_INT_CMD_MASK` and publishes `_nidx_complete` from `SDHCI_INT_RESPONSE`, so a "
               "window without bit 0 would make the rung-21 comparison this arm rides on unreadable");
_Static_assert((ST_SDHCI_INT_ENABLE_CMD & ST_SDHCI_INT_TIMEOUT) != 0u,
               "TIMEOUT is the bit the arm exists for: 765 section 4's first branch is `0x00010000` "
               "latched, which reads as 'the card did not answer and the controller knew it' and "
               "refutes the whole 'the block never sets its status' reading of rungs 13-22 in one bit");
_Static_assert((ST_SDHCI_INT_ENABLE_CMD & 0x000F0000u) == 0x000F0000u,
               "all FOUR command-level error bits must be in - TIMEOUT, CRC, END_BIT and INDEX. The "
               "mask is written as a literal and NOT as `ST_SDHCI_INT_CMD_ERR`, and the first draft of "
               "this line got that wrong and the compiler refused it: `ST_SDHCI_INT_CMD_ERR` is "
               "`0x010F0000`, which CARRIES `AUTO_CMD_ERR` (0x01000000), while this window does not - so "
               "`(ENABLE_CMD & CMD_ERR) == CMD_ERR` is false by construction and the assertion failed at "
               "build time. That is the assertion doing its job one level up: it is the constant's own "
               "definition that had the extra bit, and the sentence above names it");
_Static_assert((ST_SDHCI_INT_ENABLE_CMD & 0x01000000u) == 0u,
               "AUTO_CMD_ERR must NOT be in this window. It is the sixth bit of "
               "`ST_SDHCI_INT_CMD_MASK` and it is a failure of the block's own auto-command mechanism "
               "rather than a fact about the card, so enabling it would widen the pre-registered "
               "answer space with a bit that names none of the four outcomes (765 section 4)");
_Static_assert((ST_SDHCI_INT_ENABLE_CMD & 0x00F00000u) == 0u,
               "`BUS_POWER` and the DATA_* half of the vendor's own eleven-bit set must NOT be in this "
               "window: this "
               "rung starts no transfer, so BUS_POWER, DATA_CRC, DATA_END_BIT and DATA_TIMEOUT can "
               "only be set by something the ladder never did - which is why rung 23 takes FIVE of the "
               "vendor's eleven bits and not all of them");
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 23
/*
 * **Rung 24's one constant, and the four things the ladder has to agree with itself about before a
 * count of this bit means anything.**
 */
_Static_assert(ST_SDHCI_CMD_LINE_LEVEL == 0x01000000u,
               "the CMD line's signal level is bit 24 of PRESENT_STATE 0x24 and nothing else - the "
               "SDHCI specification's own bit map, and the arm the census is read out of records the "
               "whole word, so a wrong bit would be counted against a different pin");
_Static_assert((ST_SDHCI_CMD_LINE_LEVEL & ST_SDHCI_CMD_INHIBIT) == 0u,
               "the line's level and the block's inhibit must NOT share a bit: `inhibit_seen` counts "
               "the second over the same window this rung counts the first, and a shared bit would "
               "make the two cells two spellings of one measurement (742's press is the arm in which "
               "they DISAGREE, which is what makes them two readings)");
_Static_assert((ST_SDHCI_CMD_LINE_LEVEL & ~0xFF000000u) == 0u,
               "the bit must live in the byte the specification puts the line levels in - 23 is "
               "DAT[0], 24 is CMD, 25-27 are DAT[3:1] - so a constant in the low three bytes would "
               "be a status bit or a card-detect level read as a pin");
_Static_assert(ST_CMD_INHIBIT_SAMPLES == 1024u,
               "the window the census is taken over is 724's, and this rung reuses it rather than "
               "choosing a second one: `inhibit_seen`'s 1024 samples are the samples this count is "
               "over, so the two cells describe the SAME window and can be read against each other");
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 24
/*
 * **Rung 25's constants, and the four arithmetic facts the comparison rests on.** These are here
 * rather than in prose because a body's memory accesses and its `bl` sites carry no immediate - no
 * clause in `build_entry.sh` can read a *constant*, which is why this ladder's encoded values are
 * asserted by the compiler and its access SETS by the build (765 section 4, and the reason
 * `ST_SDHCI_INT_ENABLE_CMD` has six assertions of its own).
 */
_Static_assert((ST_TLMM_BASE + ST_TLMM_SDC1_PAD) == 0xfd512044u,
               "the pad register's own address: TLMM's physical base plus SDC1_HDRV_PULL_CTL. A "
               "transposed nibble here reads a different pad bank's register under this rung's key "
               "names, which no cell could tell from a board whose pads really are unconfigured");
_Static_assert(((ST_TLMM_BASE + ST_TLMM_SDC1_PAD) >> 20) == 0xFD5u,
               "the megabyte the arm has to INSTALL before it dereferences anything: 692's press is "
               "the measurement that a load through a translation this image did not write faults "
               "(`fsr_frame = 0x5`), so the install is not optional and this is the index it must use");
_Static_assert((ST_TLMM_BASE + ST_TLMM_SDC1_PAD) % 4u == 0u,
               "the read is 32-bit, so the address must be 4-aligned: an unaligned access to "
               "Strongly-ordered Device memory FAULTS on ARMv7, which is 692's abort class reached by "
               "alignment instead of by translation");
/* The seven masks must not overlap, or a decode reports one field's bits under another's name. */
_Static_assert((ST_TLMM_SDC1_DATA_HDRV_MASK | ST_TLMM_SDC1_CMD_HDRV_MASK |
                ST_TLMM_SDC1_CLK_HDRV_MASK  | ST_TLMM_SDC1_DATA_PULL_MASK |
                ST_TLMM_SDC1_CMD_PULL_MASK  | ST_TLMM_SDC1_CLK_PULL_MASK |
                ST_TLMM_SDC1_RCLK_PULL_MASK) ==
               (ST_TLMM_SDC1_DATA_HDRV_MASK + ST_TLMM_SDC1_CMD_HDRV_MASK +
                ST_TLMM_SDC1_CLK_HDRV_MASK  + ST_TLMM_SDC1_DATA_PULL_MASK +
                ST_TLMM_SDC1_CMD_PULL_MASK  + ST_TLMM_SDC1_CLK_PULL_MASK +
                ST_TLMM_SDC1_RCLK_PULL_MASK),
               "the seven field masks are disjoint - the sum equals the OR only then. Two overlapping "
               "masks would make the field decodes read against each other's bits, and the decode is "
               "the only thing in this rung a reader is asked to trust");
_Static_assert(ST_TLMM_SDC1_EXPECT == 0x00009F24u,
               "the expected value, DERIVED from the board's own arrays by the kernel's own shift and "
               "mask, is 0x00009F24: hdrive 4/4/4 at bits 6/3/0 = 0x124, pull 3/3/0/1 at 9/11/13/15 = "
               "0x9E00. This assertion is what makes the arithmetic checkable rather than reported - "
               "if a field's shift or width is wrong the EXPECT itself moves and the compiler refuses "
               "the build, instead of the arm reporting a mismatch that is really a decode bug");
_Static_assert((ST_TLMM_SDC1_CMD_PULL_MASK & ST_TLMM_SDC1_EXPECT) == ST_TLMM_SDC1_CMD_PULL_MASK,
               "and the ONE field this rung's mechanism rests on is non-zero: PULL SDC1_CMD is a "
               "pull-UP. MMC's CMD line is open-drain during identification, so the high level the "
               "block must see comes from this pull and from nothing else - a board whose pads were "
               "left at reset reads CMD's pull as 0 and the line floats");
_Static_assert((ST_TLMM_SDC1_EXPECT & ST_TLMM_SDC1_ALL_MASK) == ST_TLMM_SDC1_EXPECT,
               "**776, and it is the store's own precondition.** The value this rung writes is "
               "entirely INSIDE the seven fields - EXPECT has no bit the mask does not cover - so "
               "`(raw & ~MASK) | EXPECT` really is the masked read-modify-write the prose describes, "
               "and not a whole-word write with a field region that happened to be right. If a shift "
               "or a width above is wrong in a way that moves a bit out of the region, the compiler "
               "refuses this build instead of the arm quietly clobbering a bit the read handed back");
_Static_assert(ST_TLMM_SDC1_ALL_MASK == 0x0001FFFFu,
               "**and the region itself is checkable as a number rather than as seven names.** The "
               "fields of SDC1_HDRV_PULL_CTL cover bits 0..16 and nothing above, so the union of the "
               "seven masks is 0x0001FFFF: a width or a shift that widened the region would move this "
               "constant and the compiler would refuse the build. What this cannot say is whether "
               "0x0001FFFF is EVERY field the register has - a field this table does not name is one "
               "rung 28 neither writes nor protects, and 769 section 2 is the register layout that "
               "says there is no such field to have missed");
#endif /* STAGE90_XNU_STORAGE_PROBE >= 24 */

/*
 * **One command's whole result, in one struct, filled by the function and PUBLISHED by the caller.**
 * The keys are string literals, so the function cannot name them - and that is the arrangement the
 * rung wants anyway: the caller's two calls are where `_cmd0_` and `_cmd1_` come from, and the
 * pre-registration's cell table is written in the caller's own words. Rung 6's `struct st_branch_poll`
 * is the same shape.
 */
struct st_cmd_result {
    uint32_t ps_before;         /* PRESENT_STATE before the inhibit gate */
    uint32_t inhibit_before;    /* ps_before & SDHCI_CMD_INHIBIT - the FIRST refusal */
    uint32_t inhibit_polls;     /* the driver's own 10 ms bounded wait (sdhci.c:1096) */
    uint32_t inhibit_ticks;
    uint32_t inhibit_timeout;
    uint32_t stale;             /* INT_STATUS as found - the latch's baseline */
    uint32_t clear_wrote;       /* the W1C value written back, == stale by construction */
    uint32_t clear_after;       /* INT_STATUS read back - a command bit here REFUSES */
    uint32_t arg_wrote;
    uint32_t word_wrote;
    uint32_t sent;
    uint32_t polls;
    uint32_t ticks;
    uint32_t status_after;
    uint32_t complete;          /* status_after & SDHCI_INT_RESPONSE */
    uint32_t err;               /* status_after & SDHCI_INT_CMD_ERR */
    uint32_t timed_out;
    uint32_t ps_after;
    uint32_t rsp_present;       /* the driver's own guard, sdhci.c:1162 */
    uint32_t resp;              /* SDHCI_RESPONSE 0x10, read iff rsp_present on rung 11 and
                                 * ALWAYS on rung 12 (`_cmdN_resp_read` beside it says which) */

    /*
     * **724: rung 12's seven, and they are unconditional so that this struct has ONE definition.** An
     * `#if` around four fields would make the record and the image disagree about a struct the moment
     * someone built the rung below and read the rung above's document - and the fields cost nothing on
     * rung 11, because this whole struct lives on `st_cmd_path`'s stack and `.data`/`.bss` do not see
     * it. What rung 11 does NOT do is publish them, which is what keeps its own record byte-shaped the
     * way it was pressed.
     */
    uint32_t word_read;         /* COMMAND 0x0E read back, immediately after the store */
    uint32_t inhibit_after;     /* PRESENT_STATE & CMD_INHIBIT, immediately after the store */
    uint32_t inhibit_seen;      /* poll iterations (of the first ST_CMD_INHIBIT_SAMPLES) in which
                                 * CMD_INHIBIT was set - 0 means the block NEVER started it */
    uint32_t inhibit_last;      /* the last sampled PRESENT_STATE, so "seen > 0 and still set" is a
                                 * reading rather than an inference from the run's total */
#if STAGE90_XNU_STORAGE_PROBE >= 23
    uint32_t cmdlow_seen;       /* rung 24: poll iterations (of the SAME first ST_CMD_INHIBIT_SAMPLES
                                 * window `inhibit_seen` counts) in which PRESENT_STATE's bit 24 -
                                 * the CMD line's own level - was LOW. **0 means the line never moved
                                 * over that window**, which is the one reading that separates "the
                                 * block never drove anything" from "something drove the line and the
                                 * card stayed silent"; a sample at the END of the run cannot, because
                                 * an idle line and a finished transmission both read HIGH. **The
                                 * guard is what keeps rung 23's own build byte-identical**: this field
                                 * is a stack object's member, so adding it shifts no rung below 24. */
#endif
    uint32_t status_any;        /* the FIRST non-zero INT_STATUS of ANY kind, 0 if none */
    uint32_t status_any_polls;  /* the poll index at which it appeared */
    uint32_t resp_read;         /* 1 iff RESPONSE 0x10 was read - the companion that makes
                                 * `_resp = 0` a reading instead of a silence */
};

/*
 * **`noinline`, and it is the same reason as `st_pwr_wait`'s.** This body IS the rung's device act,
 * and `build_entry.sh`'s clauses read a *window* - a function's own extent, from `nm -S`. A command
 * path inlined into the probe would leave the probe's own `hc_mem` store set carrying three offsets
 * this ladder's safety argument has always asserted it does not have, and would put the command
 * registers inside a clause written about the reset and the power byte.
 *
 * **`noclone` is the second half, and the first build of this rung was refused to find it out.**
 * `noinline` alone is not enough: this function is called twice with constant arguments, and GCC's
 * interprocedural constant propagation rewrote the whole body into a specialised clone and dropped the
 * original - `nm` showed `st_send_command.constprop.0` and no `st_send_command` at all, so the clause's
 * `sym_addr` refused the build. That refusal is the clause working: a clone is a *second* body with its
 * own extent, and a clause that read the original's window while the image's only command path lived in
 * the clone would be describing a function no call reaches. `noclone` keeps the symbol one function and
 * the window the rung's record is written about.
 */
static __attribute__((noinline, noclone)) void
st_send_command(uint32_t opcode, uint32_t arg, uint32_t mmc_flags, struct st_cmd_result *r)
{
    uint32_t t0, i, steps, cleared, word;

    r->polls = 0u;
    r->sent = 0u;
    r->timed_out = 0u;
    r->rsp_present = 0u;
    r->resp = 0u;
    r->arg_wrote = 0u;
    r->word_wrote = 0u;
    r->complete = 0u;
    r->err = 0u;
    r->clear_wrote = 0u;
    r->clear_after = 0u;
    r->word_read = 0u;
    r->inhibit_after = 0u;
    r->inhibit_seen = 0u;
    r->inhibit_last = 0u;
#if STAGE90_XNU_STORAGE_PROBE >= 23
    r->cmdlow_seen = 0u;
#endif
    r->status_any = 0u;
    r->status_any_polls = 0u;
    r->resp_read = 0u;

    r->ps_before = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
    r->inhibit_before = r->ps_before & ST_SDHCI_CMD_INHIBIT;

    /* sdhci.c:1096's loop, with the driver's own bound (`timeout = 10`, :1084) as a tick budget. */
    t0 = (uint32_t)stage90_cntvct_read();
    cleared = 0u;
    r->inhibit_polls = 0u;
    for (steps = 0u; steps < (ST_CMD_INHIBIT_TICK_BUDGET / ST_CMD_INHIBIT_INNER); steps++) {
        for (i = 0u; i < ST_CMD_INHIBIT_INNER; i++) {
            r->inhibit_polls++;
            if ((st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE) & ST_SDHCI_CMD_INHIBIT) == 0u) {
                cleared = 1u;
                break;
            }
        }
        if (cleared != 0u)
            break;
        if ((uint32_t)stage90_cntvct_read() - t0 >= ST_CMD_INHIBIT_TICK_BUDGET)
            break;
    }
    r->inhibit_ticks = (uint32_t)stage90_cntvct_read() - t0;
    r->inhibit_timeout = (cleared == 0u) ? 1u : 0u;
    if (r->inhibit_timeout != 0u) {
        r->ps_after = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
        return;                                  /* sent stays 0: no command was issued */
    }

    /* The latch: read, written straight back (write-1-to-clear), read again. */
    r->stale = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS, r->stale);
    r->clear_wrote = r->stale;
    r->clear_after = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    if ((r->clear_after & ST_SDHCI_INT_CMD_MASK) != 0u) {
        r->ps_after = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
        return;                                  /* sent stays 0: the poll would be unreadable */
    }

    /* sdhci.c:1119. */
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_ARGUMENT, arg);
    r->arg_wrote = arg;

    /*
     * **sdhci.c:1131-1143's decode, transcribed in FULL, and it is the 737 change to this body.**
     * Rungs 11 through 15 sent only two commands - CMD0 with no response and CMD1 with `MMC_RSP_R3` -
     * so the decode could be written as the two arms those commands need. CMD2 asks for `MMC_RSP_R2`,
     * which is `PRESENT|136|CRC`, and the driver's ladder branches on it in an order that matters: the
     * 136-bit arm is tested BEFORE busy, so a 136-bit-busy combination would take the LONG branch (and
     * `sdhci.c:1123-1129` refuses that combination outright, which this image never forms because no
     * transcribed command is both). The CRC and INDEX terms are the driver's own `|=` lines and they are why CMD2's
     * word carries `CRC` and not `INDEX`: `MMC_RSP_R2` has no `MMC_RSP_OPCODE`.
     *
     * **The two arms the ladder already used are unchanged by this**: CMD0's flags are 0 so the first
     * arm still gives `RESP_NONE` (the word 0x0000, which rung 12 read back), and CMD1's `MMC_RSP_R3`
     * is `PRESENT` alone so it falls to the last `else` and still gives `RESP_SHORT` with no CRC (the
     * word 0x0102, which rung 15's press read back as `_cmd1_word`). A transcription that moved either
     * of those words would move a pressed arm's evidence, so **both are asserted at COMPILE TIME by
     * the three `_Static_assert`s above this body** - which is where the claim belongs, because the
     * build's `st_send_command` clause asserts this body's ACCESSES and never the value it stores.
     */

    /*
     * **824: `SDHCI_CMD_DATA` IS ADDED HERE, AND ITS ABSENCE WAS A LATENT DEFECT THIS RUNG WOULD
     * HAVE PRESSED.**
     *
     * The vendor does not put this bit in the flag word the driver hands down. `sdhci_send_command`
     * (`sdhci.c:1153`) writes `sdhci_cmd_to_flags(cmd)` - `sdhci.c:1131-1143`, five bits - and then
     * ORs `SDHCI_CMD_DATA` at `sdhci.c:1146-1149` **because `cmd->data != NULL`**, a condition that
     * is a property of the REQUEST and not of any bit in `cmd->flags`. `MMC_CMD_ADTC` is how
     * `core.c:338` sets that pointer (`mmc_ops.c:263`'s CMD8 flag word carries it, and CMD9's does
     * not), so on this ladder the ADTC bit in `mmc_flags` is the faithful stand-in for the vendor's
     * own condition and is the only thing that distinguishes a data command here.
     *
     * **Every rung below this one passes flags whose ADTC bit is clear, so this branch is the
     * identity for all of them** - CMD0's 0, CMD1's `MMC_RSP_R3`, CMD2's and CMD9's `MMC_RSP_R2`,
     * CMD3's and CMD7's `MMC_RSP_R1`, CMD13's `MMC_RSP_R1_SPI` - which is why rung 15's and rung
     * 18's pressed words (`_cmd1_word` 0x0102, `_all_word`) are unmoved by it.
     *
     * **What it costs to be wrong is not a failed command, it is a silent one.** Without this bit
     * CMD8's register receives `0x081A`, the block sees no data phase while `BLOCK_SIZE`,
     * `BLOCK_COUNT` and `TRANSFER_MODE` are all armed, and `st_send_command`'s completion poll
     * (`_ext_complete`) can still return - so every `_ext_*` cell would read plausibly and the
     * 512-byte transfer this rung exists for would never happen. This file's own header comment
     * (the `ST_SDHCI_CMD_WORD_DATA` block) names that failure; what it did not have until now was
     * the store, and 824's clause asserts the stored word is `0x083A` rather than `0x081A` so the
     * claim is refused by the build rather than by a reader.
     */
    word = ST_SDHCI_CMD_WORD(opcode, mmc_flags);
#if STAGE90_XNU_STORAGE_PROBE >= 37
    /*
     * **AND IT IS GUARDED, BECAUSE THE TWO NAMES IT NEEDS ARE DECLARED ONLY AT THIS RUNG AND
     * ABOVE** (`ST_MMC_CMD_ADTC` and `ST_SDHCI_CMD_DATA` are inside the `>= 37` block that also
     * carries the command-word pair's four `_Static_assert`s). Below this rung the body compiles to
     * exactly the byte sequence it did before 824, which is what makes the two arms already pressed
     * at values 35 and 36 reproducible from this tree - the property every spent park's recheck
     * depends on. The guard is not an optimisation: without it the whole ladder below 37 fails to
     * compile, which is how this was found.
     */
    if ((mmc_flags & ST_MMC_CMD_ADTC) != 0u)
        word |= (uint32_t)ST_SDHCI_CMD_DATA;
#endif
    r->word_wrote = word;
    st_write16(ST_HC_MEM_BASE + ST_SDHCI_COMMAND, (uint16_t)word);   /* sdhci.c:1153 */
    r->sent = 1u;

#if STAGE90_XNU_STORAGE_PROBE >= 12
    /*
     * **724 section 3: the two readings the driver never takes, taken here because the arm's whole
     * question is whether the COMMAND store reached the block's transmitter.**
     *
     * `word_read` is the block's own copy of the word. The driver does not read COMMAND back - it does
     * not have to, because it has an interrupt to tell it the command ran. This arm has only the
     * absence of that interrupt, and an absence is a reading about two things at once: the block may
     * have refused the word, or it may hold the word and never have transmitted it. `word_read` splits
     * them: `0x0000` says the block holds the driver's own CMD0 word.
     *
     * `inhibit_after` is `CMD_INHIBIT` one read after the store. `PRESENT_STATE`'s inhibit bit is the
     * block's own statement that a command is in progress, and the driver's `sdhci_send_command`
     * *waits for it to clear* before writing COMMAND (`sdhci.c:1096`) - so the bit rising here, on the
     * read after the store, is the block's acknowledgement that it took the command. It is one read
     * and it is the only evidence available before the poll starts; the poll's own sampling below is
     * what turns it into a count rather than a snapshot.
     */
    r->word_read = (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_COMMAND);
    r->inhibit_after = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE) & ST_SDHCI_CMD_INHIBIT;
#endif

    /* THE SUBSTITUTION: the register the handler would have read, polled under a bound. */
    t0 = (uint32_t)stage90_cntvct_read();
    r->status_after = 0u;
    for (steps = 0u; steps < (ST_CMD_DONE_TICK_BUDGET / ST_CMD_DONE_INNER); steps++) {
        for (i = 0u; i < ST_CMD_DONE_INNER; i++) {
            r->polls++;
            r->status_after = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
#if STAGE90_XNU_STORAGE_PROBE >= 12
            /*
             * **The first non-zero reading of ANY kind, and it is rung 12's second reason for being.**
             * Rung 11 polled `SDHCI_INT_RESPONSE` alone, so a block that latched some *other* bit would
             * have run the arm's whole 1.2 s budget with a one-bit answer sitting in the register -
             * and that is exactly what "nothing latched" cannot rule out. This cell is the wider
             * question asked beside the narrower one: the break condition stays the driver's, and what
             * the block actually said is published whether or not it satisfies it.
             */
            if (r->status_after != 0u && r->status_any == 0u) {
                r->status_any = r->status_after;
                r->status_any_polls = r->polls;
            }
            /*
             * **`CMD_INHIBIT`, sampled for the first `ST_CMD_INHIBIT_SAMPLES` iterations and only
             * those.** The bit's rise and fall are at the start of the transmission; sampling every
             * iteration would double the poll's device traffic and halve the run's coverage for no
             * reading at all. `seen` counts the samples in which it was set, and `last` is the last
             * sample taken - so "seen = 0" (the block never started the command) and "seen > 0 with
             * `last` still set" (the command is in flight and stuck) are two different records.
             */
            if (r->polls <= ST_CMD_INHIBIT_SAMPLES) {
                r->inhibit_last = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
                if ((r->inhibit_last & ST_SDHCI_CMD_INHIBIT) != 0u)
                    r->inhibit_seen++;
#if STAGE90_XNU_STORAGE_PROBE >= 23
                /*
                 * **Rung 24's whole act, and it is a mask on a value this body had already put in a
                 * register.** The register read above is 724's and is not touched; what is new is the
                 * second question asked of the same word. **No new device access, no new register, no
                 * new store to the block** - which is the strongest safety property an arm in this
                 * ladder can have, and it is why this rung needs no new window and no new megabyte.
                 */
                if ((r->inhibit_last & ST_SDHCI_CMD_LINE_LEVEL) == 0u)
                    r->cmdlow_seen++;
#endif
            }
#endif
            if ((r->status_after & ST_SDHCI_INT_CMD_MASK) != 0u)
                goto cmd_done;
        }
        if ((uint32_t)stage90_cntvct_read() - t0 >= ST_CMD_DONE_TICK_BUDGET) {
            r->timed_out = 1u;
            break;
        }
    }
cmd_done:
    r->ticks = (uint32_t)stage90_cntvct_read() - t0;
    r->complete = ((r->status_after & ST_SDHCI_INT_RESPONSE) != 0u) ? 1u : 0u;
    r->err = r->status_after & ST_SDHCI_INT_CMD_ERR;

    /* sdhci.c:1162-1175 - the response is read only when the command asked for one. */
    r->rsp_present = ((mmc_flags & ST_MMC_RSP_PRESENT) != 0u) ? 1u : 0u;
#if STAGE90_XNU_STORAGE_PROBE >= 12
    /*
     * **724 section 3: read unconditionally, and `rsp_present` stays the driver's own condition.**
     * `sdhci_finish_command` (`sdhci.c:1162-1175`) reads RESPONSE only when the command asked for a
     * response, which CMD0 never does - so on rung 11 the register was not read at all for CMD0, and
     * `_cmd0_resp = 0` was the absence of a read wearing the shape of a value. This is the
     * `mi4-silence-is-a-reading-only-if-success-is-silent` repair: one unconditional read, the
     * driver's condition published beside it as a cell of its own, and `resp_read` saying the read
     * happened so that a zero RESPONSE is a reading rather than a silence.
     */
    r->resp = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    r->resp_read = 1u;
#else
    if (r->rsp_present != 0u)
        r->resp = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
#endif
    r->ps_after = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
}

#if STAGE90_XNU_STORAGE_PROBE >= 12
/*
 * **724: rung 12 - the register state at the INSTANT of the command, and the command's own return
 * path.** The pre-registration is
 * `docs/experiments/experiment-724-the-rung-13-pre-registration-the-instant-of-the-command-and-the-return-path.md`;
 * this function is its section 3's first bullet and `st_send_command`'s new cells are the second.
 *
 * **Why a census and not a comment.** Rung 11 read every one of these registers - at other rungs'
 * moments. `POWER_CONTROL` was read by rung 3's census and rung 4's reset tail, `CLOCK_CONTROL` and
 * the GCC words by rungs 5 and 6, and both interrupt-enable registers by rung 11's own gate, once,
 * before the command. The rung-12 press then read a log in which those cells described a machine
 * that no longer existed: `_reg_power_control = 0x00` was rung 3's reading of a byte rung 7 had since
 * written `0x0B`, and 723 section 3 concluded from it that the ladder had never written the byte at
 * all (see 723 section 5, the dated correction, and m732). **Every one of those quantities is a
 * function of time in a run whose rungs run in order, and the command is the moment they all have to
 * be true at.** This body takes them again, one register each, immediately before the command.
 *
 * **It makes NO store, and that is the clause rather than the comment.** `build_entry.sh` asserts this
 * body's device set exactly and its store set EMPTY - the reverse of the usual emphasis, because a
 * census is exactly the kind of body in which a stray store would look like instrumentation.
 *
 * **What it returns is the one condition that can stop the command, and it is 531 section 8's hazard
 * read in the safe direction.** `POWER_CONTROL`'s bus-power bit clear means the block is not driving
 * the bus; the arm's byte is `0x0B` (`SDHCI_POWER_ON | SDHCI_POWER_180`, rung 7) and if it is not
 * still set the command is refused rather than issued. A caller that read `_cmd2_power_control` and
 * ignored it would be spending a press on a transaction the driver itself would not make.
 *
 * **The four readings the pre-registration names as "the alternative it never considered"** - the
 * clock tree's own enable and rate, and `PRESENT_STATE`'s inhibit bits - are here for the same reason
 * and in the same shape: rung 5's `_clk_rcg_*` cells are a decode of `0x00000507` (`SRC_SEL = 5`,
 * `DIV = 7`, `ROOT_STATUS` bit 31 **clear**, so the root is enabled) and the vendor's own pre-divider
 * `(div + 1) >> 1 = 4` makes the SDCC1 apps clock **200 MHz** - so `ST_SET_MAX_CLK = 384000000`, read
 * out of the *pro* device tree, names a table this hardware does not use, and the arm's `_clk_set_div`
 * of 480 delivers 208 kHz rather than the 400 it intends. Both are defects of the record and not
 * causes (208 kHz is a legal identification clock), and this rung is where a reader can see them
 * rather than take them on trust.
 */
static __attribute__((noinline, noclone)) uint32_t st_cmd_census(void)
{
    uint32_t pc, cc, ps, ie, se, rcg_cmd, rcg_cfg, apps, ahb, bcr, ok;

    ST_LIVE("xnu_live_storage_cmd2_calls", 1u);

    /* Rung 7's byte, at THIS moment instead of rung 3's (723 section 5, m732). */
    pc = (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_POWER_CONTROL);
    ST_LIVE("xnu_live_storage_cmd2_power_control", pc);
    ST_LIVE("xnu_live_storage_cmd2_power_bus", pc & ST_SDHCI_POWER_ON);

    /* Rung 6's two halfwords' register, and the three bits that make a command transmissible. */
    cc = (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_CLOCK_CONTROL);
    ST_LIVE("xnu_live_storage_cmd2_clock_control", cc);
    ST_LIVE("xnu_live_storage_cmd2_cc_int_en", (cc & ST_SDHCI_CLOCK_INT_EN) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_cmd2_cc_stable", (cc & ST_SDHCI_CLOCK_INT_STABLE) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_cmd2_cc_card_en", (cc & ST_SDHCI_CLOCK_CARD_EN) ? 1u : 0u);

    ps = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
    ST_LIVE("xnu_live_storage_cmd2_present_state", ps);
    ST_LIVE("xnu_live_storage_cmd2_ps_inhibit", ps & ST_SDHCI_CMD_INHIBIT);

    /*
     * **The run-protecting gate, re-taken at the command's own moment.** Rung 11 read these once and
     * its gate is unchanged below; these two cells are the same question asked at the instant that
     * matters, and they are EVIDENCE rather than a second gate - a rung above does not get to weaken
     * the one below by moving it.
     */
    ie = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_cmd2_int_enable", ie);
    se = st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE);
    ST_LIVE("xnu_live_storage_cmd2_sig_enable", se);

    /* Rung 5's clock surface, at this moment: the root's own state and the table it names. */
    rcg_cmd = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_APPS_RCG + ST_RCG_CMD);
    ST_LIVE("xnu_live_storage_cmd2_rcg_cmd", rcg_cmd);
    ST_LIVE("xnu_live_storage_cmd2_rcg_root_en", (rcg_cmd & ST_RCG_ROOT_EN_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_cmd2_rcg_root_status", (rcg_cmd & ST_RCG_ROOT_STATUS_BIT) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_cmd2_rcg_update", (rcg_cmd & ST_RCG_UPDATE_BIT) ? 1u : 0u);
    rcg_cfg = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_APPS_RCG + ST_RCG_CFG);
    ST_LIVE("xnu_live_storage_cmd2_rcg_cfg", rcg_cfg);
    ST_LIVE("xnu_live_storage_cmd2_rcg_src",
            (rcg_cfg & ST_RCG_CFG_SRC_MASK) >> ST_RCG_CFG_SRC_SHIFT);
    ST_LIVE("xnu_live_storage_cmd2_rcg_div", rcg_cfg & ST_RCG_CFG_DIV_MASK);
    ST_LIVE("xnu_live_storage_cmd2_rcg_mnd_mode", (rcg_cfg & ST_RCG_CFG_MND_MASK) >> 12u);

    apps = st_read32(ST_GCC_BASE + ST_SDCC1_CBCR);
    ST_LIVE("xnu_live_storage_cmd2_apps_cbcr", apps);
    ahb = st_read32(ST_GCC_BASE + ST_GCC_SDCC1_AHB_CBCR);
    ST_LIVE("xnu_live_storage_cmd2_ahb_cbcr", ahb);
    bcr = st_read32(ST_GCC_BASE + ST_SDCC1_BCR);
    ST_LIVE("xnu_live_storage_cmd2_bcr", bcr);
    ST_LIVE("xnu_live_storage_cmd2_bcr_ares", (bcr & ST_BCR_ARES_BIT) ? 1u : 0u);

    ok = ((pc & ST_SDHCI_POWER_ON) != 0u) ? 1u : 0u;
    ST_LIVE("xnu_live_storage_cmd2_ok", ok);
    return ok;
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 12 - the census is one body with no store, and
        * build_entry.sh asserts exactly that */

#if STAGE90_XNU_STORAGE_PROBE >= 13
/*
 * **729: rung 14 - WHERE THIS CONTROLLER REPORTS A COMPLETION, and the one place it can be asked
 * without putting the line in play.** The pre-registration is
 * `docs/experiments/experiment-728-the-rung-14-pre-registration-where-this-controller-reports-a-completion.md`;
 * this function is its section 2, and its section 1's placement constraint is the call site in
 * `st_cmd_path` below.
 *
 * **Why a body of its own, with a store in it.** Rung 12's clause records that `st_cmd_path` READS the
 * two interrupt-enable registers and stores to neither, and names the reason: the block's `hc_irq` is
 * SPI 123 -> intid 155, a line this image hands to nobody, and a store to an enable is the act that can
 * let it rise. This rung makes that store deliberately. `noinline` and `noclone` - for `st_send_command`'s
 * two measured reasons, one of them a build refusal - are what keep it out of `st_cmd_path`'s window, so
 * that rung 11's "no store at all" assertion stays true of the function it was written about instead of
 * being widened by this rung. The store lands in a window this rung declares and no other.
 *
 * **The hypothesis this tests.** Rung 13 (726) refuted all five of 724 section 2's alternatives and left
 * one: **this controller runs a command to completion and does not set `INT_STATUS 0x30`.** The command
 * reached the CMD line, the block held `CMD_INHIBIT` while it was in flight and released it when it
 * finished (`_cmd0_inhibit_after = 1`, `_cmd0_inhibit_seen = 0x21b`, `_cmd0_inhibit_last` bit 0 clear),
 * and `_cmd0_status_any` was `0x00000000` over 5,088,256 reads taken ACROSS that window. The SDHCI
 * normal-interrupt path is `INT_STATUS -> INT_ENABLE -> SIGNAL_ENABLE -> the line`, so a block built as
 * *the status bit latches only if its enable is set* produces exactly that log. Three parts:
 *
 * 1. **The elsewhere-reads.** A completion recorded in a register rung 11 was not polling is a completion
 *    the arm read past. `SLOT_INT_STATUS 0xFC` and `CORE_PWRCTL_STATUS 0xDC` are the two other status
 *    registers this block has.
 *
 *    **Both have been read before, and both 726 and 728 say they have not.** Rung 3's `st_standard_census`
 *    reads `SLOT_INT_STATUS` (`_reg_slot_int_status`, `0x00000000` on 726's own log) and rung 2's mode
 *    sequence and rung 8's handler read `CORE_PWRCTL_STATUS` (`_pwr_irq_status32 = 0x02`). So "this ladder
 *    has never read at all" is FALSE as written, and the true statement is both stronger and the one this
 *    rung needs: **neither has ever been read at a moment when there was a completion to report**, which
 *    is this call site and no other, and the rung-3 and rung-8 readings are in the same log as the new
 *    cells, so the pair is comparable rather than a claim ([[mi4-one-value-two-definitions]], m732's shape:
 *    a cell that names a register was read for a TIME).
 * 2. **The one-bit probe.** Write ONE bit of `INT_ENABLE 0x34` - `SDHCI_INT_RESPONSE 0x00000001` - with
 *    `SIGNAL_ENABLE 0x38` left at ZERO, then read `INT_STATUS 0x30` immediately. If the block ANDs the two
 *    enables (which is what the vendor's own `sdhci_enable_irq`/`sdhci_disable_irq` pair composes) the
 *    line never rises and the status bit latches - and this run gets both the answer and the log. If it
 *    does not AND them, the line rises and the run ends at the dispatcher as `_irq_other_count = 1` with
 *    `_irq_other_iar = 155`: **an ending this image already reads and survives** (709's press ended
 *    exactly that way on intid 170 and the device came back, and the payload carries its own armed
 *    watchdog). The failure mode is a diagnosis, not a lost device.
 * 3. **The restore, published.** `INT_ENABLE` goes back to zero and is read back, so "the gate is where
 *    it was" is a cell and not a claim.
 *
 * **`_int_status_after` is the rung's answer, and the pair beside it is what keeps the answer honest.**
 * Non-zero means the block latches a status bit only when its enable is set. Zero means the enable is not
 * the mask. But a zero has two producers - the hardware's answer, or a store that never took - and only a
 * readback separates them: `_int_enable_wrote` is the value this body intends (the shape of `_cmdN_word`)
 * and `_int_enable_held` is the block's own copy of it (the shape of `_cmdN_word_read`, which rung 12
 * added for exactly this reason: an absence of interrupt cannot supply the block's own value). Without
 * that pair this rung's headline cell would be ambiguous in the one direction that matters, because the
 * two readings it separates are a hardware hypothesis and a software defect.
 *
 * **The store order is the record's and not the compiler's convenience.** `build_entry.sh` asserts this
 * body's device accesses exactly, stores included, in the order this function writes them: `INT_ENABLE <- 1`,
 * `INT_STATUS` read, `INT_ENABLE` read back, `INT_ENABLE <- 0`, `INT_ENABLE` read back. A reordering would
 * make the status read's meaning a property of the compiler's block layout rather than of this rung.
 */
static __attribute__((noinline, noclone)) void st_int_report(void)
{
    uint32_t slot, pwrctl, word, present, status_before, enable_before, sig_enable,
             status_after, enable_held, readback;

    ST_LIVE("xnu_live_storage_int_calls", 1u);

    /* (1) The elsewhere-reads: the two other status registers, the word, and the block's own state. */
    slot = (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_SLOT_INT_STAT);
    ST_LIVE("xnu_live_storage_int_slot_status", slot);

    pwrctl = st_read32(ST_CORE_MEM_BASE + ST_CORE_PWRCTL_STATUS);
    ST_LIVE("xnu_live_storage_int_pwrctl_status", pwrctl);

    word = (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_COMMAND);
    ST_LIVE("xnu_live_storage_int_cmd_word", word);

    present = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
    ST_LIVE("xnu_live_storage_int_present", present);
    ST_LIVE("xnu_live_storage_int_present_inhibit", present & ST_SDHCI_CMD_INHIBIT);

    status_before = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    ST_LIVE("xnu_live_storage_int_status_before", status_before);

    /*
     * The state of the gate this rung writes through, read BEFORE it writes it - so that "the enable was
     * clear when this rung set it" is `_int_enable_before` and not an assumption inherited from the two
     * rungs below. Rung 11's gate itself is unchanged: a rung above does not get to weaken the rung below
     * by moving it.
     */
    enable_before = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_int_enable_before", enable_before);
    sig_enable = st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE);
    ST_LIVE("xnu_live_storage_int_sig_enable", sig_enable);

    /* (2) The one-bit probe. ONE bit of INT_ENABLE; SIGNAL_ENABLE stays at zero throughout. */
    ST_LIVE("xnu_live_storage_int_enable_wrote", ST_SDHCI_INT_RESPONSE);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, ST_SDHCI_INT_RESPONSE);

    status_after = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    ST_LIVE("xnu_live_storage_int_status_after", status_after);

    enable_held = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_int_enable_held", enable_held);

    /* (3) The restore, and its own reading. */
    ST_LIVE("xnu_live_storage_int_enable_restored", 0u);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, 0u);
    readback = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_int_enable_readback", readback);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 13 - one body, one caller, and the only store this rung makes:
        * two 32-bit writes to INT_ENABLE 0x34, the second of them the restore */

#if STAGE90_XNU_STORAGE_PROBE >= 14
/*
 * **732: the enable's restore, in a body of its own because it is the window's one closing act and
 * the build counts its callers.** The rung-14 window is `INT_ENABLE 0x34 <- SDHCI_INT_RESPONSE`
 * ... the restore, and **it has exactly ONE caller, taken unconditionally on the single line that
 * follows CMD0's publishes.** The first build of this arm had two callers - the between-commands
 * gate's early `return` and the end of the body - and it was REFUTED WITHOUT A PRESS: CMD0's own
 * completion poll is inside the window only if the window opens above the command, and that build
 * opened it below, so `c0.complete` could not be 1 and the gate the restore was guarding was
 * unreachable. The two-exit shape described a window that could not be entered.
 *
 * Written once rather than twice for the reason the first build gave and which still holds: two
 * copies of one act leave the copy a later edit forgets on the path the press happens to take.
 * Written once, the caller is a `bl` the build counts, and `build_entry.sh` asserts this body's
 * device accesses and its single store exactly.
 *
 * **`seq` is a reading and not decoration.** A log carries `_ena_restore_seq` = 1 for the one exit
 * this window has, so "the restore ran" and "which exit it ran on" are one cell rather than an
 * inference from the cells around it - and the cell stays even though there is one exit today,
 * because the next rung that adds an exit would otherwise have to add the cell with it.
 *
 * **`INT_STATUS` is read AFTER the disable on purpose.** 730 section 5 left this owed: whether the
 * bit rung 13 saw was a status the enable had GATED (so clearing the enable takes the reading away)
 * or a status the enable had merely made VISIBLE (so the bit is still there). `_ena_status_pre` is
 * that read on the other side of the store, taken before CMD0, and neither reading is a verdict on
 * its own.
 */
static __attribute__((noinline, noclone)) void st_cmd_enable_restore(uint32_t was, uint32_t seq)
{
    ST_LIVE("xnu_live_storage_ena_restore_seq", seq);
    ST_LIVE("xnu_live_storage_ena_wrote_back", was);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, was);
    ST_LIVE("xnu_live_storage_ena_readback",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE));
    ST_LIVE("xnu_live_storage_ena_status_post",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS));
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 14 - one body, one caller, one store and four readings */

#if STAGE90_XNU_STORAGE_PROBE >= 29
/*
 * **RUNG 30's TWO BODIES, AND THEY ARE BODIES AT ALL FOR THE REASON rung 14's IS: the build counts
 * callers and asserts what each one touches.** 732 wrote the enable's close once and made the single
 * caller a clause. Rung 30's window is a THIRD interval, so it needs an open and a close of its own -
 * and both are written as separate functions rather than inline in `st_cmd_path`, because
 * `st_cmd_path`'s own clause group asserts that the half of it after CMD0's send touches
 * `PRESENT_STATE` and NOTHING ELSE of its own device surface. A store added inline there would have
 * been a store that clause exists to refuse, and relaxing that clause for this rung would have been
 * exactly the wrong repair: the property it states ("everything else on that path is inside a called
 * body") is what makes rung 14's window readable as an interval. Two bodies keep it true.
 *
 * **WHY THE WINDOW EXISTS AT ALL IS 786's READING OF ITS OWN ARM.** Rung 29's press carries
 * `_ena_wrote_back = 0x00000000` - rung 14's restore stores ZERO - and then `_cmd1_status_any = 0`
 * over `_cmd1_polls = 0x004da000` (5,087,232) with `_cmd1_timeout = 1`. CMD1 is `SEND_OP_COND`, the
 * command an eMMC must answer for this ladder to move at all, and it has run on every arm from rung
 * 14 on with the completion bit NOT enabled: a poll that cannot see the bit it polls for reports
 * exactly the arm's own bound and nothing else. 785 believed it had already widened this interval;
 * 786 refuted that out of the capture's own line order, by the restore's log line standing BEFORE
 * `_cmd1_status_any` in the same capture.
 *
 * **THE OPEN RETURNS THE STATE THE CLOSE PUTS BACK, so the two are one value with two consumers.**
 * That is rung 17's shape (`st_all_send_cid` returns the sent bit its gate then reads) and it is why
 * the pair is written as two bodies taking and returning one word rather than as two stores that
 * happen to use the same expression. **AND EACH BODY'S DEVICE SURFACE IS ONE REGISTER**: a store to
 * `INT_ENABLE 0x34` and its readback, and no access to `INT_STATUS 0x30` at either end - which is
 * what lets `build_entry.sh`'s `xnu_entry_787` assert the set EXACTLY, and what keeps the arm from
 * needing a new status cell at all.
 */
static __attribute__((noinline, noclone)) uint32_t st_cmd1_enable_open(uint32_t int_enable)
{
    uint32_t ena;

    ena = int_enable | (uint32_t)ST_SDHCI_INT_ENABLE_CMD;
    ST_LIVE("xnu_live_storage_c1_ena_wrote", ena);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, ena);
    ST_LIVE("xnu_live_storage_c1_ena_held",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE));
    return ena;
}

/*
 * **AND THE CLOSE, whose interval is the point.** It is called on the line that follows CMD1's own
 * publishes and BEFORE the rung-16 CMD2 block, so the interval contains exactly CMD1's send and its
 * poll: CMD2 has a window of its own and opens it with its own store, so an interval left open
 * across that call would be closed by CMD2's restore rather than by this one. Between the two calls
 * in `st_cmd_path` there is no `return` and no branch - the function reaches `_cmd_done` on one path
 * - so this window has exactly one entrance and one exit.
 *
 * **AND NEITHER BODY TOUCHES `INT_STATUS 0x30`, WHICH IS THE SAME PROPERTY RUNG 14's RESTORE HAS.**
 * The window's device surface is `INT_ENABLE 0x34` and NOTHING ELSE - one store and one readback per
 * body - so `build_entry.sh`'s `xnu_entry_787` can assert each body's device set EXACTLY, and there
 * is no status read here that could be confused with the poll's own. The arm needs no new status
 * cell: what it changes is what the cells CMD1 ALREADY CARRIES can mean, exactly as rung 23's
 * widening changed CMD3's without adding a key. The old reading is not lost either - `_cmd1_stale`
 * and `_cmd1_status_after` are `st_send_command`'s own entrance and exit reads of `0x30` and are
 * published for CMD1 like every other command.
 */
static __attribute__((noinline, noclone)) void st_cmd1_enable_restore(uint32_t was)
{
    ST_LIVE("xnu_live_storage_c1_ena_wrote_back", was);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, was);
    ST_LIVE("xnu_live_storage_c1_ena_readback",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE));
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 29 - two bodies, one caller each, and the interval between them */

#if STAGE90_XNU_STORAGE_PROBE >= 31
/*
 * **796: rung 32's two bodies, and the whole of the arm is in them.**
 *
 * **Why a body of its own, and it is the same reason 785 gave for rung 30's pair.** `st_cmd_path`'s
 * device surface is asserted at the digit by the rung-14 clause group - its device set, its counts, its
 * program order and its empty image side - and 736's press measured what a store ABOVE that gate costs:
 * `INT_ENABLE 0x34`'s bit 15 stands after any write to it and no write clears it, so the gate refuses
 * the whole path. So the store lives in a body reached by a `bl`, `st_cmd_path` gains two `bl`s and NO
 * device access, and every rung-14 clause stays TRUE rather than relaxed.
 *
 * **And the two ends are a WINDOW, not a state change.** The open RETURNS the byte it found and the
 * close writes that byte back, so `_tout_was == _tout_wrote_back` is a READING - the same construction
 * `st_cmd1_enable_open`/`st_cmd1_enable_restore` use one rung down - and the register is left exactly
 * as this arm found it whatever the run does afterwards.
 *
 * **The read-back at each end is what makes the store a reading rather than an intent.** `_tout_held`
 * and `_tout_readback` are the register's own answers; a `strb` to a byte register this block does not
 * implement would leave `_tout_wrote` non-zero with `_tout_held` at the old value, and the pair tells
 * that row apart from the row where the store landed.
 */
static __attribute__((noinline, noclone)) uint32_t st_tout_open(void)
{
    uint32_t was;

    ST_LIVE("xnu_live_storage_tout_calls", 1u);
    was = (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL);
    ST_LIVE("xnu_live_storage_tout_was", was);
    ST_LIVE("xnu_live_storage_tout_wrote", (uint32_t)ST_SDHCI_TIMEOUT_CMD2);
    st_write8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL, (uint8_t)ST_SDHCI_TIMEOUT_CMD2);
    ST_LIVE("xnu_live_storage_tout_held",
            (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL));
    return was;
}

static __attribute__((noinline, noclone)) void st_tout_restore(uint32_t was)
{
    ST_LIVE("xnu_live_storage_tout_wrote_back", was);
    st_write8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL, (uint8_t)was);
    ST_LIVE("xnu_live_storage_tout_readback",
            (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL));
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 31 - two bodies, one caller each, and the interval between them */

#if STAGE90_XNU_STORAGE_PROBE >= 32
/*
 * **799: RUNG 33 PORTS THE DRIVER'S OWN CONTROL FLOW - THE `mmc_send_op_cond` LOOP - AND THESE ARE
 * ITS TWO BODIES.** The driver calls CMD1 TWICE and this image has only ever called it once:
 *
 *   mmc.c:1923   mmc_send_op_cond(host, 0, &ocr)              <- the image has this one
 *   mmc.c:1951   host->ocr = mmc_select_voltage(host, ocr)   <- absent
 *   mmc.c:1359   mmc_send_op_cond(host, ocr | (1 << 30), ...) <- absent, and THIS is the one that loops
 *
 * and the ARGUMENT is what decides whether the loop runs at all - `mmc_ops.c:148-150` carries, in
 * the driver's own words, the comment "if we're just probing, do a single pass", and then:
 *
 *     if (ocr == 0)
 *             break;
 *
 * So the image has been running THE PROBE and then treating the probe's answer as the final one. By
 * the driver's own code that answer is an INPUT to a later, looped call - it is not a gate. And the
 * ladder already publishes the driver's own test and has never passed it: `_cmd1_resp_busy` reads 0
 * on ALL TWELVE occurrences across every capture in the archive.
 *
 * **THE TWO BODIES ARE BODIES OF THEIR OWN**, the shape 787 and 796 used, so that `st_cmd_path`'s own
 * device surface does not move: every device access in this arm is inside `st_send_command`, which
 * `st_cmd_path` already calls.
 */
#define ST_OP_COND_SEND_MAX     100u         /* mmc_ops.c:145's `for (i = 100; i; i--)` */
#define ST_OP_COND_DELAY_TICKS  192000u      /* mmc_ops.c:164's `mmc_delay(10)` = 10 ms at 19.2 MHz */
#define ST_OP_COND_OCR_CLR      0x0000007Fu  /* mmc.c:1943-1948's `ocr &= ~0x7F` - the sub-range bits */
#define ST_OP_COND_BIT30        0x40000000u  /* mmc.c:1359's `(1 << 30)` - sector-mode / high-capacity */
_Static_assert(ST_OP_COND_SEND_MAX >= 1u, "a loop of zero sends would be the rung below this one");
_Static_assert(ST_OP_COND_SEND_MAX <= 100u, "mmc_ops.c:145 bounds the driver's loop at 100 sends");
_Static_assert((ST_OP_COND_OCR_CLR & ~0x7Fu) == 0u, "the clear mask must be the low seven bits");

/*
 * **THE ARGUMENT, DERIVED RATHER THAN NAMED, AND THE TWO STEPS NAMED TOGETHER.** `mmc.c:1943-1948`
 * clears the card's sub-range voltage claims before anything uses them, and `mmc.c:1359` sets bit 30
 * on whatever `mmc_select_voltage` returned. **THE INTERSECTION WITH `host->ocr_avail` IS DELIBERATELY
 * NOT PORTED** and that is 799 section 7's named design decision rather than an omission: this image
 * has no representation of the host's available OCR windows at all (`grep` finds `select_voltage`
 * once, in a comment), so the arm passes the CARD's own window through and says so in its cells,
 * rather than inventing a host window that would be this ladder's own number wearing the driver's
 * name. The raw response is published beside the derived argument so a reader can redo the mask.
 */
static __attribute__((noinline, noclone)) uint32_t st_op_cond_arg(uint32_t resp)
{
    uint32_t arg = (resp & ~ST_OP_COND_OCR_CLR) | ST_OP_COND_BIT30;

    ST_LIVE("xnu_live_storage_opcond_calls", 1u);
    ST_LIVE("xnu_live_storage_opcond_from", resp);
    ST_LIVE("xnu_live_storage_opcond_arg", arg);
    return arg;
}

/*
 * **THE LOOP, AND EVERY FIELD IT PUBLISHES IS ONE THE LADDER DID NOT HAVE.** `sends` is the cell 799
 * section 3a named as the ONE thing needed: on the row where the card becomes ready it is the first
 * send at which it did. `busy_seen` is `mmc_ops.c:157`'s own test as the loop evaluates it, and
 * `last_resp` is the word the last send left in the RESULT STRUCT - read out of the same struct
 * `st_send_command` fills, never re-derived. `ticks` is the wall time the loop took, so the driver's
 * 10 ms can be checked against the counter rather than trusted.
 *
 * **THE DELAY IS THE DRIVER'S AND THE WINDOW IS BOUNDED BY THIS ARM'S OWN CONSTRUCTION**: the loop
 * runs at most `ST_OP_COND_SEND_MAX` sends, each of which is itself bounded by
 * `st_send_command`'s own `ST_CMD_DONE_TICK_BUDGET`, so the whole body is bounded by
 * 100 x (1.2 s + 10 ms) and by 100 x (665.2 us + 10 ms) when the card is silent - which is what the
 * ladder measures. The delay sits at the END of the iteration, where `mmc_ops.c:164` puts it, so the
 * FIRST send is not delayed.
 */
static __attribute__((noinline, noclone)) uint32_t st_op_cond_loop(uint32_t arg,
                                                                  struct st_cmd_result *r)
{
    uint32_t n, t0, t1, busy = 0u, last = 0u;

    t0 = (uint32_t)stage90_cntvct_read();
    for (n = 1u; n <= ST_OP_COND_SEND_MAX; n++) {
        st_send_command(ST_CMD_OP_SEND_OP_COND, arg, ST_MMC_RSP_PRESENT, r);
        last = r->resp;
        /* mmc_ops.c:157's test, evaluated exactly as the driver evaluates it. */
        if ((last & ST_MMC_CARD_BUSY) != 0u) {
            busy = 1u;
            break;
        }
        /* mmc_ops.c:164's `mmc_delay(10)`, in this device's own counter ticks. */
        t1 = (uint32_t)stage90_cntvct_read();
        while ((uint32_t)stage90_cntvct_read() - t1 < ST_OP_COND_DELAY_TICKS) {
            /* bounded by the counter, not by an instruction count */
        }
    }
    ST_LIVE("xnu_live_storage_opcond_loop_calls", 1u);
    ST_LIVE("xnu_live_storage_opcond_sends", n > ST_OP_COND_SEND_MAX ? ST_OP_COND_SEND_MAX : n);
    ST_LIVE("xnu_live_storage_opcond_busy_seen", busy);
    ST_LIVE("xnu_live_storage_opcond_last_resp", last);
    ST_LIVE("xnu_live_storage_opcond_ticks", (uint32_t)stage90_cntvct_read() - t0);
    return busy;
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 32 - two bodies, one caller each, and the driver's own loop */

#if STAGE90_XNU_STORAGE_PROBE == 15
/* **AND THE GUARD IS `== 15` RATHER THAN `>= 15`, WHICH IS 736'S MEASUREMENT AND NOT A TIDY-UP.**
 * This body's store leaves bit 15 (`SDHCI_INT_ERROR`) set in `INT_ENABLE 0x34`, and no write clears
 * it, so any arm that compiles this body in AND then runs `st_cmd_path` has its gate refuse the whole
 * command path - 736 pressed exactly that and measured `_cmd_gate_kind = 2` with `_cmd_sent = 0`.
 * Every rung above 15 needs the command path, so this body belongs to the value 15 and to nothing
 * above it. See the ladder clause's own spelling of 16, and the call site's guard.
 */
/*
 * **734: rung 15's own body - THE QUIET-BLOCK READ, and it exists because 733's press refuted one of the
 * fifteen's predicted cells in a way that re-read the arm beneath it.**
 *
 * 733 pressed rung 14 and measured `_int_status_before = 0x00000000` -> `_int_status_after = 0x00000001`
 * across exactly one store, to `INT_ENABLE 0x34`, of `SDHCI_INT_RESPONSE` - and the two readings that
 * bracket that store FROM THE SAME BODY both say the register was clear (`_ena_status_post = 0` at the
 * window's close, `_int_status_before = 0` in `st_int_report`), while the only write between the pair is
 * `st_int_report`'s own absolute `0x00000001`. So the transition is real and its producer is ambiguous:
 *
 *   (A) a write to `0x34` makes `0x30`'s RESPONSE bit READ 1, or
 *   (B) the block was holding a completion and re-latched it into `0x30` when the enable returned -
 *       726's sixth hypothesis, which 730 adopted and which 733 did NOT put down.
 *
 * **`(B)` needs a completion to have happened. This body runs where none ever has.** It is called from
 * `entry_storage_probe` immediately BEFORE `st_cmd_path()`, i.e. after the mode sequence, the reset, the
 * clock set, the power byte and rung 9's wait - so the block is in SDHCI mode, powered and clocked, and
 * **no command has been put on its bus in this image's life**. The one thing that differs from rungs 13
 * and 14 is exactly that absence.
 *
 * It is therefore a DISCRIMINATION and not a new act: the same one-bit store, read the same way, with the
 * block's history as the only new variable. **The predicted cells are opposite** - `(A)` says
 * `_quiet_status_after = 1`, `(B)` says `0` - and either reading is an answer, which is what makes this a
 * cheap rung: one store, its restore, and no new command class.
 *
 * **THE COUNTER-EVIDENCE THAT KEEPS `(A)` FROM BEING ASSUMED IS IN 733'S OWN LOG**, and it is why this
 * rung is a rung rather than a paragraph: with the enable standing from before CMD0's send, CMD0's poll
 * read `INT_STATUS = 0` for its first 538 samples (`_cmd0_any_polls = 0x21b` against
 * `_cmd0_inhibit_seen = 0x219`), so a naive form of `(A)` - "writing `0x34` always sets `0x30`'s bit 0" -
 * does not describe the poll. `_quiet_status_after` beside `_cmd0_any_polls` is the pair that settles it.
 *
 * **The safety argument is the same as rung 13's and it is a reading here rather than a claim.**
 * `SIGNAL_ENABLE 0x38` is READ and published (`_quiet_sig_before`) and is NEVER WRITTEN, because the
 * line rises only on the conjunction of the two enables - and the store is restored on its own
 * unconditional line, so the enable cannot outlive the body. Two stores, both to `0x34`; no new command,
 * no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word and no byte of the medium.
 */
static __attribute__((noinline, noclone)) void st_quiet_enable_probe(void)
{
    uint32_t status_before, enable_before, sig_before, wrote, status_after, held, status_post, readback;

    ST_LIVE("xnu_live_storage_quiet_calls", 1u);

    /*
     * The three reads come first, and the enable's own read is the one the store is derived from - so
     * `_quiet_wrote` is a value this body read rather than a constant, and a block that already had a
     * bit set in `0x34` is visible before it is written back.
     */
    status_before = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    ST_LIVE("xnu_live_storage_quiet_status_before", status_before);

    enable_before = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_quiet_enable_before", enable_before);

    sig_before = st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE);
    ST_LIVE("xnu_live_storage_quiet_sig_before", sig_before);

    /* (1) The one-bit enable, and the reading 733's press could not take. */
    wrote = enable_before | (uint32_t)ST_SDHCI_INT_RESPONSE;
    ST_LIVE("xnu_live_storage_quiet_wrote", wrote);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, wrote);

    status_after = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    ST_LIVE("xnu_live_storage_quiet_status_after", status_after);

    held = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_quiet_held", held);

    /* (2) The restore, on its own unconditional line, and both readings after it. */
    ST_LIVE("xnu_live_storage_quiet_wrote_back", enable_before);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, enable_before);

    status_post = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    ST_LIVE("xnu_live_storage_quiet_status_post", status_post);

    readback = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_quiet_readback", readback);
}
#endif /* STAGE90_XNU_STORAGE_PROBE == 15 - one body, one caller, two stores and eight readings */




#if STAGE90_XNU_STORAGE_PROBE >= 16
/*
 * **737: rung 16's own body - THE FIRST 136-BIT RESPONSE, and the enable has to be standing for it.**
 *
 * The driver's next act after `mmc_send_op_cond` returns is `mmc_all_send_cid` (`mmc.c:1375-1380`,
 * `mmc_ops.c:173`): CMD2, argument 0, `MMC_RSP_R2 | MMC_CMD_BCR`. It is the first command on this
 * ladder whose response is 136 bits wide, and it is the first that asks the CARD for a fact about
 * ITSELF - the CID - rather than for its operating conditions.
 *
 * **WHY THE WINDOW IS HERE, AND IT IS 733's PRESS RATHER THAN A PREFERENCE.** On rung 14's press CMD0
 * ran INSIDE the enabled window and completed (`_cmd0_complete = 1`, `_cmd0_status_any = 1`); CMD1 ran
 * with the enable closed, WAS ANSWERED BY THE CARD (`_cmd1_resp = 0x40ff8080`, a valid OCR with the
 * voltage window in bits 23:15) and **never latched a completion at all** (`_cmd1_complete = 0`,
 * `_cmd1_status_any = 0` across `_cmd1_polls = 0x004db000` = 5,088,000 polls and the whole
 * `_cmd1_timeout = 1` bound).
 *
 * **AND 771 REFUTED THE PREDICTION THAT USED TO FOLLOW FROM IT, WHICH IS WHY THIS PARAGRAPH CARRIES A
 * CORRECTION RATHER THAN A REASON (776, and the correction is owed to THIS build because a comment
 * that changes forces one).** The sentence here used to read *a CMD2 sent the way CMD1 was would
 * answer the same way - the response would arrive and nothing would latch - and this arm would learn
 * nothing it does not already have*, and the rung-24 press measured the opposite of its consequence:
 * **CMD2 was sent INSIDE this window and gained nothing by it, because CMD2 never put a command on the
 * bus at all** - `_cid_inhibit_seen = 0` over all 1024 samples, i.e. the block never raised
 * `CMD_INHIBIT`, so for CMD2 this window was open above a command that was never issued. The window's
 * rationale as a PREDICTION is therefore refuted, and 771 withdrew it from the press path.
 *
 * **The window itself stays, and the distinction is 771's rather than a preference.** Its cost is one
 * read-modify-write pair around one command; it sits BELOW `st_cmd_path`'s own gate, so 736's lesson -
 * that a `0x34` write above the gate leaves `SDHCI_INT_ERROR` set and refuses the whole command path -
 * is untouched by it; and what it buys is not a claim about CMD2's outcome but the removal of a
 * competing explanation, because a run whose enables are up and which latches nothing is a stronger
 * reading than one whose enables are down. **And the window is re-opened here rather than left open**
 * for the reason the next paragraph gives.
 *
 * **AND IT IS RE-OPENED HERE RATHER THAN LEFT OPEN, WHICH IS THE 736 LESSON.** Rung 15's own quiet body
 * writes `0x34` BEFORE `st_cmd_path` runs, and that press measured the consequence: bit 15
 * (`SDHCI_INT_ERROR`) is set by any write to that register and is NOT cleared by one, so `st_cmd_path`'s
 * gate read `0x00008000` and refused the whole command path - `_cmd_gate_kind = 2`, `_cmd_sent = 0`, no
 * command on the bus. **This body's two stores are the only writes to `0x34` ABOVE `st_cmd_path`'s gate in this image**, because rung 15's quiet body is compiled for the value 15 and no other (see the ladder clause and that body's call site): the value-16 arm carries no store above the gate. Rung 14's window is still here and still BELOW the gate - its two stores are inside `st_cmd_path`'s own body - so the register reads `0x00008000` at this body's entrance, left there by that window's PARTIAL restore, and that is why `int_enable` (the value `st_cmd_path` read at its own top, 0) is ORed rather than a constant written.
 *
 * **AND THE ORDER IS THE WHOLE DESIGN OF THIS ARM, WHICH IS WHY THE BUILD REFUSES IT RATHER THAN THE PROSE DESCRIBING IT**: a body placed above the gate - or a rung-15 body left in - re-measures the rung below while publishing this rung's names, and 732's first build and 736's press are the two instances of that class. The build clauses that hold this arm's order are this body's call line's position in `st_cmd_path`'s disassembly (after the between-commands gate's `c1` publishes) and rung 15's own exclusion above it.
 *
 * **The reads are the driver's own and they are published twice on purpose.** `sdhci_finish_command`'s
 * 136-bit branch (`sdhci.c:1163-1172`) is
 *
 *     resp[i] = readl(RESPONSE + (3-i)*4) << 8;
 *     if (i != 3) resp[i] |= readb(RESPONSE + (3-i)*4 - 1);
 *
 * - the shift the driver's own comment calls *CRC is stripped*, one byte wide and taken from the byte
 * BELOW each word. It is transcribed exactly, and the four raw words are published beside the four
 * shifted ones because the shift is the ONE thing here that could be wrong in a way no cell would show:
 * a byte read through this block's 32-bit register window may or may not do what the driver expects,
 * and with both readings in the log a reader can see whether the low byte of each word moved at all.
 * `_cid_resp3` is the driver's `resp[3]` - `readl(RESPONSE) << 8` with no byte fill - and it is the one
 * of the four that has no byte read in it.
 *
 * **THE SIGNAL ENABLE IS READ AND NEVER WRITTEN, and it is read INSIDE the window** rather than
 * before it: the register cannot change between the two moments (nothing in this image writes it), and
 * reading it here makes the cell describe the same instant the enable does.
 */
static __attribute__((noinline, noclone)) uint32_t st_all_send_cid(uint32_t int_enable)
{
    struct st_cmd_result c2;
    uint32_t raw0, raw1, raw2, raw3, held, readback, status_post;

    ST_LIVE("xnu_live_storage_cid_calls", 1u);

    /* --- the window opens here, immediately before CMD2 is put on the bus ---------------------- */
#if STAGE90_XNU_STORAGE_PROBE >= 28
    /*
     * **RUNG 29 (value 28) AT CMD2'S WINDOW - 783 section 2's second of the two windows the
     * widening had not reached.** Same reasoning, same constant, same whole-`#if` shape as the
     * CMD0/CMD1 window above, so a value below 28 compiles to rung 16's two lines byte for byte.
     * The reading this buys is `_cid_status_any` and `_cid_status_after` as readings about the BLOCK
     * rather than about the mask - and it is the last window in the run whose enable was still one
     * bit wide once `st_cmd3_noidx` was widened.
     */
    {
        uint32_t ena;
        ena = int_enable | (uint32_t)ST_SDHCI_INT_ENABLE_CMD;
        ST_LIVE("xnu_live_storage_cid_ena_wrote", ena);
        st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, ena);
    }
#else
    ST_LIVE("xnu_live_storage_cid_ena_wrote", int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
#endif
    held = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_cid_ena_held", held);
    ST_LIVE("xnu_live_storage_cid_sig_enable",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE));

#if STAGE90_XNU_STORAGE_PROBE >= 30
    /*
     * **791: RUNG 31 IS THIS ONE CONSTANT AND NOTHING ELSE IN THIS BODY - AND IT IS WRITTEN AS A
     * WHOLE ALTERNATIVE RATHER THAN AS A VARIABLE ASSIGNED IN TWO ARMS, so that at every value below
     * 30 the two lines below are the ORIGINAL text character for character and the containment
     * property is STRUCTURAL rather than argued.** Every other line of `st_all_send_cid` - the
     * window's two stores, the four `RESPONSE` words and their three byte reads, the restore, the
     * cell set it publishes - is rung 16's, unchanged, and the clause group that reads this body out
     * of the linked image (its device set, its counts, its program order, its empty image side) is
     * unchanged with it: **a body device surface does not move when a constant moves.**
     *
     * **What the rung changes is the flags ARGUMENT, and two assertions carry it from the two
     * sides.** The build clause reads the immediate in the flags register at the CALL SITE in the
     * LINKED image - `#7` below this rung, `#3` at and above it - and the source `_Static_assert`
     * above pins the command WORD that constant folds to (`0x0209` / `0x0201`). The clause can see
     * the argument and cannot see the word the callee folds from it, and the assert can see the word
     * and not the call; **neither side is a sentence in a comment, and together they are the whole
     * change.**
     */
    ST_LIVE("xnu_live_storage_cid_op", ST_CMD_OP_ALL_SEND_CID);
    ST_LIVE("xnu_live_storage_cid_flags", ST_MMC_RSP_R2_NOCRC);
    st_send_command(ST_CMD_OP_ALL_SEND_CID, 0u, ST_MMC_RSP_R2_NOCRC, &c2);
#else
    ST_LIVE("xnu_live_storage_cid_op", ST_CMD_OP_ALL_SEND_CID);
    ST_LIVE("xnu_live_storage_cid_flags", ST_MMC_RSP_R2);
    st_send_command(ST_CMD_OP_ALL_SEND_CID, 0u, ST_MMC_RSP_R2, &c2);
#endif

    ST_LIVE("xnu_live_storage_cid_sent", c2.sent);
    ST_LIVE("xnu_live_storage_cid_word", c2.word_wrote);
    ST_LIVE("xnu_live_storage_cid_word_read", c2.word_read);
    ST_LIVE("xnu_live_storage_cid_complete", c2.complete);
    ST_LIVE("xnu_live_storage_cid_err", c2.err);
    ST_LIVE("xnu_live_storage_cid_timeout", c2.timed_out);
    ST_LIVE("xnu_live_storage_cid_status_any", c2.status_any);
    ST_LIVE("xnu_live_storage_cid_any_polls", c2.status_any_polls);
    ST_LIVE("xnu_live_storage_cid_inhibit_seen", c2.inhibit_seen);
    ST_LIVE("xnu_live_storage_cid_inhibit_last", c2.inhibit_last);
#if STAGE90_XNU_STORAGE_PROBE >= 25
    /*
     * **The two cells the fourth row of 771 table is missing.** `inhibit_after` is filled by
     * `st_send_command` for every command and printed by NO publish block, so it is absent from
     * every capture in the archive while CMD0 and CMD3 both carry theirs (`1` and `1`) and CMD1
     * carries `0`. `cmdlow_seen` is the same story one field over: the sampler counts it for all
     * four commands and rung 24 published it for `nidx` alone.
     *
     * **And these two cells carry 771 second refutation**: the window opened above for CMD2 was
     * justified by the prediction that a CMD2 sent the way CMD1 was would "answer the same way -
     * the response would arrive and nothing would latch". The press refuted it as a prediction
     * (`_cid_status_any = 0`, `_cid_complete = 0`), and `_cid_inhibit_seen = 0` says why: the
     * block never raised `CMD_INHIBIT`, so there was no completion for any enable to deliver.
     * `cid_cmdlow_seen` is what says whether it drove the line anyway.
     *
     * BOTH ARE FREE: no new device access, no new register, no new window, no store.
     */
    ST_LIVE("xnu_live_storage_cid_inhibit_after", c2.inhibit_after);
    ST_LIVE("xnu_live_storage_cid_cmdlow_seen", c2.cmdlow_seen);
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 26
    /*
     * **772: THE REST OF THE CMD2 RESULT, AND IT IS THE SAME CLASS OF CELL AS THE TWO ABOVE.**
     * `st_all_send_cid` was written at rung 16, before rungs 12 and 24 added the entrance state and
     * the inhibit gate to `struct st_cmd_result` - so this block publishes ten of the struct's
     * twenty-seven fields and the fifteen it drops include every one of them. **Nothing here is
     * measured for the first time**: `st_send_command` fills all eleven for every command, and this
     * arm prints them. **No new device access, no new register, no new window, no store of any kind.**
     *
     * **AND ONE OF THE ELEVEN DECIDES SOMETHING NO OTHER CELL IN THE ARCHIVE CAN.** `_cid_stale` is
     * `INT_STATUS 0x30` as FOUND at CMD2's entrance - which is the FIRST read of that register since
     * CMD1's poll gave up 1.2 seconds earlier (`_cmd1_any_polls = 0` over `_cmd1_polls` = 5,090,304
     * reads that were ALL ZERO). The rung-24 press DOES carry `_nidx_status_pre = 0x00018000`, so
     * `ERROR | TIMEOUT` was latched somewhere between CMD2's poll and CMD3's entrance, and the only
     * write in that interval is a store to `0x34` - which 736 measured sets bit 15 by itself. Whether
     * bit 16 (`INT_TIMEOUT`) was a real CMD2 timeout that arrived after the poll gave up, or the
     * enable store's own effect, is therefore UNSEPARATED by every capture on record. **`_cid_stale`
     * is the same question asked one command earlier**, in an interval that contains NO write to
     * `0x34` at all: a non-zero here is CMD1 doing something after its poll ended, and a zero is CMD1
     * doing nothing - which is the reading 771 needed and could not take.
     *
     * The other ten are the entrance and exit state (`ps_before`, `inhibit_before`, `ps_after`), the
     * whole inhibit gate (`inhibit_polls`/`_ticks`/`_timeout` - `inhibit_timeout = 1` would make the
     * window a refusal at the gate rather than a command that ran and stalled), the clear
     * (`clear_wrote`), the argument, and the two companions that make a zero response a READING
     * rather than a silence (`rsp_present`, the driver's own condition at `sdhci.c:1162-1175`, and
     * `resp_read`, which says the register was read at all).
     */
    ST_LIVE("xnu_live_storage_cid_ps_before", c2.ps_before);
    ST_LIVE("xnu_live_storage_cid_inhibit_before", c2.inhibit_before);
    ST_LIVE("xnu_live_storage_cid_inhibit_polls", c2.inhibit_polls);
    ST_LIVE("xnu_live_storage_cid_inhibit_ticks", c2.inhibit_ticks);
    ST_LIVE("xnu_live_storage_cid_inhibit_timeout", c2.inhibit_timeout);
    ST_LIVE("xnu_live_storage_cid_stale", c2.stale);
    ST_LIVE("xnu_live_storage_cid_clear_wrote", c2.clear_wrote);
    ST_LIVE("xnu_live_storage_cid_arg", c2.arg_wrote);
    ST_LIVE("xnu_live_storage_cid_ps_after", c2.ps_after);
    ST_LIVE("xnu_live_storage_cid_rsp_present", c2.rsp_present);
    ST_LIVE("xnu_live_storage_cid_resp_read", c2.resp_read);
#endif
    ST_LIVE("xnu_live_storage_cid_polls", c2.polls);
    ST_LIVE("xnu_live_storage_cid_ticks", c2.ticks);
    ST_LIVE("xnu_live_storage_cid_clear_after", c2.clear_after);
    ST_LIVE("xnu_live_storage_cid_resp_short", c2.resp);

    /*
     * sdhci.c:1163-1172, the 136-bit branch, in the driver's own order - word 3 first.
     * `<< 8` on each, and the byte below each word except the last.
     */
    raw0 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    raw1 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 8u);
    raw2 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
    raw3 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("xnu_live_storage_cid_raw0", raw0);
    ST_LIVE("xnu_live_storage_cid_raw1", raw1);
    ST_LIVE("xnu_live_storage_cid_raw2", raw2);
    ST_LIVE("xnu_live_storage_cid_raw3", raw3);
    ST_LIVE("xnu_live_storage_cid_resp0",
            (raw0 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 11u));
    ST_LIVE("xnu_live_storage_cid_resp1",
            (raw1 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 7u));
    ST_LIVE("xnu_live_storage_cid_resp2",
            (raw2 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 3u));
    ST_LIVE("xnu_live_storage_cid_resp3", raw3 << 8);

    /* --- the window's ONE exit, unconditional, on the line after the publishes ------------------ */
    ST_LIVE("xnu_live_storage_cid_wrote_back", int_enable);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable);
    status_post = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    ST_LIVE("xnu_live_storage_cid_status_post", status_post);
    readback = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_cid_readback", readback);
    ST_LIVE("xnu_live_storage_cid_ps_after",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
    ST_LIVE("xnu_live_storage_cid_done", 1u);

    /*
     * **741: the body RETURNS the one value rung 19's gate has to read, and that is what keeps the
     * gate and the cell from being two readings of one thing.** Rung 19 issues CMD3 only if CMD2 was
     * SENT, and a caller that re-derived that condition from `c1` - as it could, since the expression
     * is the same one above - would put the condition in the source twice; the two would agree today
     * and an edit to one of them would not touch the other. Returning `c2.sent` - the same field the
     * publishes above report as `_cid_sent` - makes the gate's condition and the log's cell ONE
     * reading with two consumers, which is what `[[mi4-one-value-two-definitions]]` asks for.
     */
    return c2.sent;
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 16 - one body, one caller, two stores and the ladder's first 136-bit read */

#if STAGE90_XNU_STORAGE_PROBE >= 17
/*
 * **739: rung 18's own body - the FIRST rung of this ladder whose new act is a READ at a moment no rung
 * has read at, and the first whose question is about the ladder's own evidence rather than about the
 * block.**
 *
 * **The question, and it is 738's, not a hypothetical.** Rung 17 (738) made the ladder's first 136-bit
 * response read and got `_cid_resp0 = 0x40ff8080` - **bit-for-bit `_cmd1_resp` from the same log**, with
 * the four raw words holding that same 32-bit value one byte further along. So the four words are not a
 * CID. Since 733, `_cmd1_resp = 0x40ff8080` has been read as *the card answered CMD1*: 733's index row
 * calls it "exactly the reference word", 736 section 5 built the push plan from it, and **rung 17's own
 * gate** is read off it. The value has an OCR's shape - bit 30 set, voltage window `0x00ff8000` - which
 * is why it convinced, and **shape is not provenance**. The register was not a constant (after CMD0 in
 * 738's own run `_cmd0_resp = 0x00000000` with `_cmd0_resp_read = 1`, and after CMD1 it was
 * `0x40ff8080`), but "it changed at some point during CMD1" does not separate *the card wrote it* from
 * *the block's own default for a command that was never taken* - and 738's press is the first that makes
 * the question askable, because it showed that value sitting in the register with **no completion
 * latched** (`_cid_complete = 0`, `_cid_status_any = 0` over 5,088,256 polls and 1.200 s with the enable
 * standing) and **no `CMD_INHIBIT` ever seen** (`_cid_inhibit_seen = 0`, where CMD0's own was seen 537
 * times in the same run).
 *
 * **One read decides it, and it is the cheapest act this ladder has.** RESPONSE is read HERE and nowhere
 * earlier: `st_standard_census` (rung 3) reads ten registers of the block and NOT this one, and the only
 * two bodies in this image that touch `RESPONSE 0x10` are `st_send_command` (rung 11's poll) and
 * `st_all_send_cid` (rung 17) - both of them AFTER a command. So this body is the register's FIRST
 * reading on a block that has never carried a command, and the two outcomes are exhaustive:
 *
 *   - **`_rb_resp_zero = 1`** - the register was EMPTY. Then CMD1's `0x40ff8080` was put there by CMD1,
 *     733's reading survives, and the next rung continues the driver from a fact rather than a hope.
 *   - **`_rb_resp_zero = 0`** - the register already held a value with no command in the block's past.
 *     The four shifted pre-command words beside it then say WHICH value, and if they match rung 17's
 *     post-CMD2 ones the word is the block's own and **the ladder's one piece of evidence that a card
 *     exists is retired**: the push from CMD1 must be re-derived from a signal other than RESPONSE
 *     (738's own second cell - `CMD_INHIBIT` - is the candidate, because CMD0's was observed 537 times
 *     and CMD1's and CMD2's were never observed at all).
 *
 * **This is a rung whose null reading still answers**, which is why it is worth a press: unlike rung 15's
 * quiet block - where the (B) branch could only be *removed*, never confirmed - here either value names
 * the next act. A press that returns `_rb_resp_zero = 1` has bought the ladder's evidence back; one that
 * returns `0` has bought the truth.
 *
 * **Why the position is the experiment.** It runs immediately BEFORE `st_cmd_path()`, after rung 9's
 * wait: the block is in SDHCI mode, powered, clocked and quiesced (rungs 2-9 measured all three) and no
 * command has been put on its bus. Anywhere above this point the block is not yet quiesced; anywhere
 * below it a command has run and the register is answering a command rather than describing a block.
 * **It is READ-ONLY**: no store of any width, no new command, no data phase, no `POWER_CONTROL 0x29`,
 * no GCC word, no `core_mem` word and no byte of the medium - so the safety property is the one rung 3,
 * rung 5 and rung 12 already established, and the build's own clause holds the body's store set to
 * EMPTY rather than describing it in prose.
 *
 * **The two readings are published through ONE derivation.** `sdhci_finish_command`'s 136-bit branch
 * (`sdhci.c:1163-1172`) is
 *
 *     resp[i] = readl(RESPONSE + (3-i)*4) << 8;
 *     if (i != 3) resp[i] |= readb(RESPONSE + (3-i)*4 - 1);
 *
 * - transcribed here exactly as `st_all_send_cid` transcribes it, so the pre-command four words and the
 * post-CMD2 four words are the same arithmetic over the same offsets taken at two times. That is
 * deliberate: the comparison 738 needs is *"is the word the same before and after CMD2"*, and a
 * comparison between two different derivations would answer a different question.
 */
static __attribute__((noinline, noclone)) void st_resp_before(void)
{
    uint32_t raw0, raw1, raw2, raw3, present;

    ST_LIVE("xnu_live_storage_rb_calls", 1u);

    /* --- the four words, in the driver's own order (word 3 first) ------------------------------ */
    raw0 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    raw1 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 8u);
    raw2 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
    raw3 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("xnu_live_storage_rb_raw0", raw0);
    ST_LIVE("xnu_live_storage_rb_raw1", raw1);
    ST_LIVE("xnu_live_storage_rb_raw2", raw2);
    ST_LIVE("xnu_live_storage_rb_raw3", raw3);

    /* --- and the driver's four shifted words, the same arithmetic rung 17 uses after CMD2 ----- */
    ST_LIVE("xnu_live_storage_rb_resp0",
            (raw0 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 11u));
    ST_LIVE("xnu_live_storage_rb_resp1",
            (raw1 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 7u));
    ST_LIVE("xnu_live_storage_rb_resp2",
            (raw2 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 3u));
    ST_LIVE("xnu_live_storage_rb_resp3", raw3 << 8);

    /*
     * **THIS RUNG'S ANSWER, and it is derived in the image rather than left to a reader's arithmetic.**
     * The four raw words are ANDed into one bit: 1 says the register was empty at a moment when no
     * command had ever been issued, so anything in it later was put there by a command. Both the
     * aggregate and its four operands are in the log, so the aggregate can be checked rather than
     * believed - the shape rung 3's `_reg_loads` counter uses one rung up.
     */
    ST_LIVE("xnu_live_storage_rb_resp_zero",
            ((raw0 | raw1 | raw2 | raw3) == 0u) ? 1u : 0u);

    /*
     * **738's discriminator, taken as a before-value.** `PRESENT_STATE 0x24`'s bit 0 is `SDHCI_CMD_INHIBIT`
     * and it is published both as the whole register and as the bit, because "the word is in the register
     * file" and "the sequencer took the command" are the two readings 738 could not separate: CMD0's own
     * inhibit was observed 537 times (`_cmd0_inhibit_seen = 0x219`), CMD1's and CMD2's were never observed
     * at all. Here it is the before-value for all three.
     */
    present = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
    ST_LIVE("xnu_live_storage_rb_present", present);
    ST_LIVE("xnu_live_storage_rb_inhibit", present & ST_SDHCI_CMD_INHIBIT);
    ST_LIVE("xnu_live_storage_rb_cmd_word",
            (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_COMMAND));

    /*
     * **The three interrupt registers at the same instant**, read and not written: the pre-command
     * enable state is the before-value for rung 14's window and for rung 17's own gate, and
     * `SIGNAL_ENABLE` is READ AND NEVER WRITTEN at any rung of this ladder.
     */
    ST_LIVE("xnu_live_storage_rb_int_status", st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS));
    ST_LIVE("xnu_live_storage_rb_int_enable", st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE));
    ST_LIVE("xnu_live_storage_rb_sig_enable", st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE));

    ST_LIVE("xnu_live_storage_rb_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 17 - one body, one caller, no store anywhere and RESPONSE's first reading */

#if STAGE90_XNU_STORAGE_PROBE >= 21
/*
 * **756 section 6: the DLL and HOST_CONTROL2 census, and the ladder's second body whose subject is a
 * read and nothing else.** The pre-registration is
 * `docs/experiments/experiment-756-the-vendors-bring-up-disables-the-dll-and-the-ladder-never-has.md`;
 * this function is its section 6's two-read discriminator, taken one register wider (the DLL's STATUS
 * word as well, because `CORE_DLL_LOCK` is the cell that says whether the block is sampling on the DLL
 * or on something else).
 *
 * **What it is FOR, in the vendor's own words.** `sdhci_msm_set_uhs_signaling` (`sdhci-msm.c:2537-2600`)
 * is called on the power-up `set_ios` - `sdhci.c:1784-1785`, inside the `host->version >= SDHCI_SPEC_300`
 * block at `:1729` with NO timing test - and at `host->clock <= CORE_FREQ_100MHZ` (`:2567`; the ladder is
 * at 400 kHz) it sets `CORE_DLL_RST` (`:2578`) and `CORE_DLL_PDN` (`:2583`) in `CORE_DLL_CONFIG`, with
 * the vendor's own comment: *"the feedback clock must be provided and DLL must not be used so that tuning
 * can be skipped"*. That is a statement about HOW THE CONTROLLER SAMPLES, and the ladder's measured stall
 * is confined to receiving: CMD0 and the no-response CMD3 complete in 0.255-0.30 ms, while every word
 * that demands a response is taken and never completes with `_status_any = 0` over 5,088,256 polls
 * (746, 749, 758). **`CORE_DLL_CONFIG` has five writers in the vendor tree and exactly ONE of them is
 * reachable before the first command** - the other four are the tuning path (`:392-423`, `:571-687`) and
 * the data path (`sdhci_msm_toggle_cdr`, `:2216`, called only from `sdhci.c:1007-1011`) - so this is the
 * one un-made low-clock setup write on the sampling path.
 *
 * **Read and never written, and that is the arm's whole safety argument.** `build_entry.sh` holds this
 * body's store set to EMPTY and its call site above `st_cmd_path`'s gate, for the reason 739 gave for
 * rung 18: a body that stores nothing cannot move a register that gate reads, and 736's press measured
 * what a store there costs. The three addresses are in the megabyte this image already maps and reads
 * (`0xf98`), all inside `hc_mem`'s 0x1a0 declared window, so `entry_mmio_section`'s install is not
 * touched and 692's interlock is satisfied by construction rather than by argument.
 *
 * **The window is `hc_mem`, and getting that wrong is the one error that would make this rung's readings
 * mean something else.** The vendor reaches `CORE_DLL_CONFIG` as `host->ioaddr + 0x100`, and `host->ioaddr`
 * is the FIRST memory resource - `hc_mem` (`msm8974.dtsi:500`). `msm8974pro.dtsi:1765` widens `&sdhc_1`
 * from the base node's `0x11c` to `0x1a0` and `0x100`/`0x108` are beyond `0x11c`: the window was widened
 * to cover exactly this pair. `core_mem + 0x100` is a different register file, and a reading taken there
 * under these names would be a defect of the class this project records as a key that names a register
 * the run did not read.
 *
 * **The count is bottom-up**, like `_reg_loads` and for the same reason (m688): the record's "three
 * reads" and the log's number are two derivations of one fact.
 */
static __attribute__((noinline, noclone)) void st_dll_census(void)
{
    uint32_t loads = 0u;
    uint16_t ctrl2;
    uint32_t dll_config, dll_status;

    /*
     * `HOST_CONTROL2 0x3E` as a HALFWORD, which is both the vendor's accessor (`sdhci_readw`,
     * `sdhci-msm.c:2544`) and the only width this address admits: 0x3E is 2-aligned and not 4-aligned,
     * and an unaligned access to Device memory faults on ARMv7 - 692's abort class, reached by alignment
     * instead of by translation.
     */
    ctrl2 = st_read16(ST_HC_MEM_BASE + ST_SDHCI_HOST_CONTROL2);
    ST_LIVE("xnu_live_storage_dll_ctrl2", (uint32_t)ctrl2);
    ST_LIVE("xnu_live_storage_dll_ctrl2_uhs", (uint32_t)ctrl2 & ST_SDHCI_CTRL_UHS_MASK);
    ST_LIVE("xnu_live_storage_dll_loads", ++loads);

    dll_config = st_read32(ST_HC_MEM_BASE + ST_HC_DLL_CONFIG);
    ST_LIVE("xnu_live_storage_dll_config", dll_config);
    ST_LIVE("xnu_live_storage_dll_rst", (dll_config & ST_HC_DLL_RST) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_dll_pdn", (dll_config & ST_HC_DLL_PDN) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_dll_en", (dll_config & ST_HC_DLL_EN) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_dll_ckout", (dll_config & ST_HC_CK_OUT_EN) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_dll_loads", ++loads);

    dll_status = st_read32(ST_HC_MEM_BASE + ST_HC_DLL_STATUS);
    ST_LIVE("xnu_live_storage_dll_status", dll_status);
    ST_LIVE("xnu_live_storage_dll_lock", (dll_status & ST_HC_DLL_LOCK) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_dll_loads", ++loads);

    ST_LIVE("xnu_live_storage_dll_done", loads);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 21 - one body, one caller, THREE READS and no store anywhere */

#if STAGE90_XNU_STORAGE_PROBE >= 24
/*
 * **769: rung 25's body - ONE READ OF THE SDC1 PADS, in a megabyte this image has never installed.**
 *
 * **What the reading is for.** 768 section 5 left the frontier at "the card does not answer, on a bus
 * the controller is demonstrably driving", and named the two things between the controller and the
 * pins: the function mux and the clock. 769 section 2 retires the mux **by a register layout** - the
 * SDC1 pads are one of this SoC's dedicated-pad controllers (`sdhci-msm.c:267-270`), `sdhci_msm_setup_pad`
 * (`:964-988`) reaches only `msm_tlmm_set_hdrive`/`msm_tlmm_set_pull`, and every field of
 * `TLMM + 0x2044` is drive strength or pull with **no function field to write**. What is NOT retired
 * is whether those fields are set at all: the vendor applies them from inside `sdhci_msm_pwr_irq`'s
 * `CORE_PWRCTL_BUS_ON` branch (`sdhci-msm.c:2015-2032`), and the ladder's own handler acks the latch
 * rather than doing that work - so no rung has ever attempted it. This body reads the register and
 * publishes it raw beside the value the board's own DT says it should hold.
 *
 * **Why the field that matters is `PULL SDC1_CMD`.** MMC's CMD line is **open-drain during
 * identification**: the host and the card both pull it LOW, and it returns HIGH only through the
 * pull-up - which on this SoC is this register and not a discrete resistor the arm can assume. A board
 * whose pads sit at their reset value has CMD's pull field at 0, and a block that drives a command and
 * then waits for a high level has nothing to see. **That is a mechanism and it is stated as a
 * candidate**: the CMD line reads HIGH on the idle samples of every arm on record
 * (`_nidx_inhibit_last = 0x01f80001`, bit 24 set), which is what a pull-up produces and **equally**
 * what a floating line that happens to sit high produces - the cell cannot tell them apart and this
 * register can.
 *
 * **The install comes first and the reason is 692's press, not caution.** That run's third line was a
 * load from `0xfc4004c0`, a megabyte no table this image built covered, and it came back with
 * `fsr_frame = 0x5` - a section translation fault. So `_pad_map` is published before the read and a
 * refusal publishes `_pad_read = 0` rather than a value: a load through a translation this run did not
 * write is a fault, not a measurement. `0xfd512044 >> 20 = 0xFD5`, and no other device this image
 * touches lives there, so 692's `addr >> 20` interlock is satisfied by construction rather than by an
 * argument.
 *
 * **ZERO STORES AT VALUES 24, 25 AND 26, AND ONE GUARDED STORE AT 27 (776).** The read is a read:
 * `msm_tlmm_set_field` itself opens with `__raw_readl` on this exact address
 * (`gpio-msm-common.c:487`), the register is not W1C and not a FIFO, and up to and including value 26
 * `_pad_writes` is published as a count that never leaves zero. Value 27 adds the write the register
 * exists for, and it adds it **guarded**: `_pad_write_skipped` is 1 and `_pad_writes` is 0 when the
 * read already matched, so **a store is made only on the branch where the read did not match**, and
 * the count is published as what happened rather than as a constant. **Which of the two this body
 * compiles to is a property of the build and not of this paragraph**: `build_entry.sh` holds the
 * store set to EMPTY at values 24..26 and to exactly ONE predicated or branch-skipped `str` to
 * `fd512044` at 27, refusing the build on anything else - and the guard's own sufficient argument is
 * the `if` below, which is what the store's clause names.
 *
 * **And the comparison is published rather than performed for the reader.** `_pad_expect` is the DT's
 * own value through the kernel's own shift-and-mask, and `_pad_raw` is beside it with the seven field
 * decodes, so a mismatch can be read as WHICH field differs instead of as a boolean nobody can check.
 * `_pad_match` is a convenience on top of that and is never the only cell to read - a derived boolean
 * whose two operands print identically is m736's defect, and the two operands here are published.
 */
static __attribute__((noinline, noclone)) void st_pad_census(void)
{
    uint32_t slot_before = 0u, desc = 0u, mapped, raw;
#if STAGE90_XNU_STORAGE_PROBE >= 27
    uint32_t want = 0u, after = 0u;
#endif

    ST_LIVE("xnu_live_storage_pad_calls", 1u);
    ST_LIVE("xnu_live_storage_pad_section", ST_TLMM_SECTION >> 20);

    /*
     * The install, and its four numbers under `_pad_*` so they are never read as the storage block's
     * or the gate's - 693's rule, applied a third time because this image now installs three
     * megabytes and the keys are what keep them apart.
     */
    mapped = entry_mmio_section(ST_TLMM_SECTION, ST_TLMM_SECTION, &slot_before, &desc);
    ST_LIVE("xnu_live_storage_pad_map", mapped);
    ST_LIVE("xnu_live_storage_pad_slot_before", slot_before);
    ST_LIVE("xnu_live_storage_pad_desc", desc);

    if (mapped == 0u) {
        ST_LIVE("xnu_live_storage_pad_read", 0u);
        ST_LIVE("xnu_live_storage_pad_writes", 0u);
        return;
    }

    /*
     * ONE read, 32-bit, and the address is 4-aligned - asserted by the compiler rather than argued
     * here, because an unaligned access to Strongly-ordered Device memory faults on ARMv7.
     */
    raw = st_read32(ST_TLMM_SDC1_PAD_ADDR);
    ST_LIVE("xnu_live_storage_pad_raw", raw);
    ST_LIVE("xnu_live_storage_pad_read", 1u);

    ST_LIVE("xnu_live_storage_pad_clk_hdrv",
            (raw & ST_TLMM_SDC1_CLK_HDRV_MASK) >> ST_TLMM_SDC1_CLK_HDRV_SHIFT);
    ST_LIVE("xnu_live_storage_pad_cmd_hdrv",
            (raw & ST_TLMM_SDC1_CMD_HDRV_MASK) >> ST_TLMM_SDC1_CMD_HDRV_SHIFT);
    ST_LIVE("xnu_live_storage_pad_data_hdrv",
            (raw & ST_TLMM_SDC1_DATA_HDRV_MASK) >> ST_TLMM_SDC1_DATA_HDRV_SHIFT);
    ST_LIVE("xnu_live_storage_pad_clk_pull",
            (raw & ST_TLMM_SDC1_CLK_PULL_MASK) >> ST_TLMM_SDC1_CLK_PULL_SHIFT);
    ST_LIVE("xnu_live_storage_pad_cmd_pull",
            (raw & ST_TLMM_SDC1_CMD_PULL_MASK) >> ST_TLMM_SDC1_CMD_PULL_SHIFT);
    ST_LIVE("xnu_live_storage_pad_data_pull",
            (raw & ST_TLMM_SDC1_DATA_PULL_MASK) >> ST_TLMM_SDC1_DATA_PULL_SHIFT);
    ST_LIVE("xnu_live_storage_pad_rclk_pull",
            (raw & ST_TLMM_SDC1_RCLK_PULL_MASK) >> ST_TLMM_SDC1_RCLK_PULL_SHIFT);

    ST_LIVE("xnu_live_storage_pad_expect", ST_TLMM_SDC1_EXPECT);
    ST_LIVE("xnu_live_storage_pad_match", (raw == ST_TLMM_SDC1_EXPECT) ? 1u : 0u);
#if STAGE90_XNU_STORAGE_PROBE >= 27
    /*
     * **776: THE ONE STORE, AND THE GUARD IS THE ARM.**
     *
     * **Why this is a fork and not a fix.** 773 section 5 pre-registered exactly this shape: a board
     * that boots from this eMMC to `fastboot` has had SOMETHING drive that bus successfully, so
     * `_pad_raw = 0x00009F24` is the MORE LIKELY row - and on that row the vendor's own pad work has
     * already been done by whatever configured them, so the arm stores NOTHING and says so. The other
     * row, any word whose fields do not match the value this board's DT arrays produce, says the
     * vendor act was never applied on this path, and there the store is the vendor's act.
     *
     * **What is written is what the vendor writes on this board.** `sdhci_msm_setup_pad`
     * (`sdhci-msm.c:964`) walks the SAME seven fields to the SAME two arrays
     * (`msm8974pro-ac-pm8941-mtp-v5.dts:25-29`), so the worst case of an unguarded fire would still be
     * a word Linux itself writes at every `CORE_PWRCTL_BUS_ON` - and the bits OUTSIDE the seven are
     * the bits the read handed back, carried through unchanged, which is what makes this a field
     * write rather than a whole-word write to a register a bootloader may have owned.
     *
     * **Why the read-back is published only on the branch that wrote.** The skip branch publishes
     * neither `_pad_after` nor `_pad_after_match`, because in that branch a second read of a register
     * nothing wrote is a second sample of one value - the argument this body's own count clause
     * already makes. So the three keys of the skip case are `_pad_write_skipped = 1`,
     * `_pad_writes = 0` and `_pad_raw`, and the five of the store case are the same two counts with
     * `_pad_write_skipped = 0`, plus `_pad_wrote`, `_pad_after` and `_pad_after_match`. An absent
     * `_pad_write_skipped` is neither of those: it is the refusal path above, where `_pad_read = 0`
     * is already the discriminator (m720's rule, and the one key that separates a branch that did not
     * run from a value that was not published).
     */
    if (raw == ST_TLMM_SDC1_EXPECT) {
        ST_LIVE("xnu_live_storage_pad_write_skipped", 1u);
        ST_LIVE("xnu_live_storage_pad_writes", 0u);
    } else {
        want = (raw & ~ST_TLMM_SDC1_ALL_MASK) |
               (ST_TLMM_SDC1_EXPECT & ST_TLMM_SDC1_ALL_MASK);
        st_write32(ST_TLMM_SDC1_PAD_ADDR, want);
        ST_LIVE("xnu_live_storage_pad_write_skipped", 0u);
        ST_LIVE("xnu_live_storage_pad_writes", 1u);
        ST_LIVE("xnu_live_storage_pad_wrote", want);
        after = st_read32(ST_TLMM_SDC1_PAD_ADDR);
        ST_LIVE("xnu_live_storage_pad_after", after);
        ST_LIVE("xnu_live_storage_pad_after_match",
                (after == ST_TLMM_SDC1_EXPECT) ? 1u : 0u);
    }
#else
    ST_LIVE("xnu_live_storage_pad_writes", 0u);
#endif /* STAGE90_XNU_STORAGE_PROBE >= 27 */
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 24 - one body, one caller, ONE READ at 24..26 and ONE GUARDED
        * STORE at 27 - the read, the seven field decodes and the raw value are the same code at every
        * value from 24 up */

#if STAGE90_XNU_STORAGE_PROBE == 18
/*
 * **741: rung 19's own body - THE DRIVER'S OWN NEXT COMMAND, and the first word on this bus with an
 * INDEX bit and a non-zero argument.**
 *
 * `mmc_attach_mmc`'s statement after `mmc_all_send_cid` is `mmc_set_relative_addr(card)`
 * (`mmc.c:1409`), which is `mmc_ops.c:194-210`: opcode 3, `cmd.arg = card->rca << 16` with
 * `card->rca = 1` assigned nine lines above it (`mmc.c:1400`), and `cmd.flags = MMC_RSP_R1 |
 * MMC_CMD_AC`. Three firsts and all three are in the word: the word is **0x031A** (opcode 3,
 * `RESP_SHORT 0x02 | CRC 0x08 | INDEX 0x10`), the ladder's **first `INDEX`** - R1 carries the opcode
 * back, which is what `MMC_RSP_OPCODE` is for - and the argument is **0x00010000**, the first
 * non-zero argument this image has ever written to `ARGUMENT 0x08`.
 *
 * **WHY THIS BODY IS THE RIGHT NEXT ACT, AND IT IS 740's OWN FINDING RATHER THAN A PLAN.** Rung 18
 * measured `_rb_resp_zero = 1`: `RESPONSE 0x10..0x1C` was EMPTY before any command had ever been on
 * the bus. So the register IS a witness, and CMD1's `0x40ff8080` was put there by a command. What
 * that leaves open is *which command writes it and with what*: CMD2 (a 136-bit read) re-presented
 * CMD1's own word rather than a CID, and CMD1's answer has an **OCR's shape** while an R1 CARD STATUS
 * does not - its bits 12:9 are `CURRENT_STATE`, bit 8 is `READY_FOR_DATA` and bit 22 is
 * `ILLEGAL_COMMAND`. So a CMD3 that the block answers with a status-shaped word and a CMD3 that
 * leaves the register where CMD2 left it are two different worlds, and this rung tells them apart
 * from inside the driver's own sequence rather than from a synthetic test.
 *
 * **THE NEW CELL IS A PAIR, AND IT IS RUNG 18'S METHOD APPLIED TO A COMMAND.** `RESPONSE` is read
 * through the DRIVER'S OWN WORD-0 DERIVATION - `(readl(RESPONSE + 0x1C) << 8) | readb(RESPONSE +
 * 0x1B)`, the same arithmetic rung 17 uses after CMD2 and rung 18 uses before any command -
 * immediately BEFORE this rung's window opens and again immediately AFTER the command's publishes.
 * The two are one derivation at two times, so `_rca_resp_moved` is a measurement and not a
 * comparison of two things that happen to be spelled alike, and `_rca_resp_pre` is directly
 * comparable with `_cid_resp0` and `_rb_resp0` in the same log. `_rca_resp_is_arg` (1 iff the word
 * after the command equals `0x00010000`) names THE ALTERNATIVE PRODUCER: a register that echoed what
 * was written to `ARGUMENT 0x08` would look like a fresh reading to every other cell here.
 *
 * **The R1 decode is published beside the raw word, and the raw word is the one to read.** `_rca_state`
 * is bits 12:9, `_rca_ready` bit 8, `_rca_illegal` bit 22 - the three fields that separate a card
 * status from an OCR - and they are derived in the image so that no reader has to re-derive them from
 * a decimal, with `_rca_resp` beside them for a reader who wants the word itself. A decode is a
 * reading of a reading; the project's rule is to publish both.
 *
 * **THE INHIBIT TRIPLET COMES WITH IT, AND THAT IS 740's OPEN QUESTION MADE ANSWERABLE.** Rungs 12
 * and 17 publish `inhibit_after`/`inhibit_seen`/`inhibit_last` for CMD0, CMD1 and CMD2, and this
 * body publishes the same three for CMD3. One log then holds FOUR commands' own inhibit readings
 * side by side: CMD0's `_cmd0_inhibit_seen` was 0x219 (537 samples of the first 1024) and CMD1's and
 * CMD2's were both 0 in both of the last two presses. That asymmetry is unexplained, and a CMD3
 * whose inhibit is seen beside a CMD3 whose response moved is the pair that separates "the sequencer
 * took it" from "the register file answered" - a question this rung does not have to spend anything
 * to ask, because `st_send_command` already samples it.
 *
 * **The window, and why it is the same one.** Rung 17 opens the one-bit enable around CMD2 because
 * 733's press measured that a command's status bit is latched only while its enable stands: CMD0
 * completed inside rung 14's window (`_cmd0_complete = 1`) and CMD1, sent with the window closed, was
 * ANSWERED (`_cmd1_resp = 0x40ff8080`) and never latched (`_cmd1_complete = 0`). CMD3 is a
 * response-carrying command and would answer the same way, so the same window is opened here for
 * CMD3's send and its poll and closed on ONE unconditional line after the publishes. `SIGNAL_ENABLE
 * 0x38` is READ and NEVER WRITTEN, at any rung.
 *
 * **`int_enable` is the value the gate read and ORed rather than a constant, for rung 17's reason**:
 * the register this body's own restore finds is not zero - bit 15 (`SDHCI_INT_ERROR`) rides along
 * with any write to 0x34 and no write clears it, so by this point in the run the register reads
 * 0x00008000 - and the cell that says so is `_rca_ena_held` (`0x00008001` for a store of 1), the same
 * pair 730, 733, 736, 738 and 740 all measured. The restore writes back the SAME value the gate
 * authorised and no other.
 */
static __attribute__((noinline, noclone)) void st_set_relative_addr(uint32_t int_enable)
{
    struct st_cmd_result c3;
    uint32_t before, after, held, readback, status_post;

    ST_LIVE("xnu_live_storage_rca_calls", 1u);

    /*
     * The freshness baseline, BEFORE the window opens - the driver's own word-0 derivation, so that
     * this reading, rung 17's `_cid_resp0` and rung 18's `_rb_resp0` are one arithmetic at three
     * times rather than three derivations that agree.
     */
    before = (st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u) << 8)
           | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 11u);
    ST_LIVE("xnu_live_storage_rca_resp_pre", before);

    /* --- the window opens here, immediately before CMD3 is put on the bus ----------------------- */
    ST_LIVE("xnu_live_storage_rca_ena_wrote", int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
    held = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_rca_ena_held", held);
    ST_LIVE("xnu_live_storage_rca_sig_enable",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE));

    ST_LIVE("xnu_live_storage_rca_op", ST_CMD_OP_SET_RELATIVE_ADDR);
    ST_LIVE("xnu_live_storage_rca_flags", ST_MMC_RSP_R1);
    ST_LIVE("xnu_live_storage_rca_arg", ST_MMC_RCA_1);
    st_send_command(ST_CMD_OP_SET_RELATIVE_ADDR, ST_MMC_RCA_1, ST_MMC_RSP_R1, &c3);

    ST_LIVE("xnu_live_storage_rca_sent", c3.sent);
    ST_LIVE("xnu_live_storage_rca_word", c3.word_wrote);
    ST_LIVE("xnu_live_storage_rca_word_read", c3.word_read);
    ST_LIVE("xnu_live_storage_rca_arg_wrote", c3.arg_wrote);
    ST_LIVE("xnu_live_storage_rca_complete", c3.complete);
    ST_LIVE("xnu_live_storage_rca_err", c3.err);
    ST_LIVE("xnu_live_storage_rca_timeout", c3.timed_out);
    ST_LIVE("xnu_live_storage_rca_status_any", c3.status_any);
    ST_LIVE("xnu_live_storage_rca_any_polls", c3.status_any_polls);
    ST_LIVE("xnu_live_storage_rca_inhibit_after", c3.inhibit_after);
    ST_LIVE("xnu_live_storage_rca_inhibit_seen", c3.inhibit_seen);
    ST_LIVE("xnu_live_storage_rca_inhibit_last", c3.inhibit_last);
    ST_LIVE("xnu_live_storage_rca_polls", c3.polls);
    ST_LIVE("xnu_live_storage_rca_ticks", c3.ticks);
    ST_LIVE("xnu_live_storage_rca_clear_after", c3.clear_after);
    ST_LIVE("xnu_live_storage_rca_resp", c3.resp);
    ST_LIVE("xnu_live_storage_rca_resp_read", c3.resp_read);

    /*
     * The R1 decode, the three fields that separate a card status from an OCR. `CURRENT_STATE` is
     * bits 12:9, `READY_FOR_DATA` is bit 8 and `ILLEGAL_COMMAND` is bit 22 of the card's own status
     * word; a card that took CMD3 has 22 clear, and a card in identification answers with state 2 or
     * 3. These are published as whole bit fields rather than as booleans so that a reader compares
     * them with `_rca_resp` and not with this comment.
     */
    ST_LIVE("xnu_live_storage_rca_state", (c3.resp >> 9) & 0xFu);
    ST_LIVE("xnu_live_storage_rca_ready", (c3.resp >> 8) & 1u);
    ST_LIVE("xnu_live_storage_rca_illegal", (c3.resp >> 22) & 1u);

    /* --- the freshness pair's second half: the SAME derivation, after the command ---------------- */
    after = (st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u) << 8)
          | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 11u);
    ST_LIVE("xnu_live_storage_rca_resp_post", after);
    ST_LIVE("xnu_live_storage_rca_resp_moved", (after != before) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_rca_resp_is_arg", (after == ST_MMC_RCA_1) ? 1u : 0u);

    /* --- the window's ONE exit, unconditional, on the line after the publishes ------------------ */
    ST_LIVE("xnu_live_storage_rca_wrote_back", int_enable);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable);
    status_post = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    ST_LIVE("xnu_live_storage_rca_status_post", status_post);
    readback = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_rca_readback", readback);
    ST_LIVE("xnu_live_storage_rca_ps_after",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
    ST_LIVE("xnu_live_storage_rca_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE == 18 - one body, one caller, two stores and the driver's own CMD3.
        * **743: compiled for THIS VALUE AND NO OTHER**, because rung 20 sends the SAME opcode and the SAME
        * argument with the response demand removed, and a build carrying both would put two variants of one
        * command on the same bus in one boot with neither reading attributable to its own flag word. The
        * pattern is rung 16's (`st_quiet_enable_probe` is compiled for 15 and no other), and the reason is
        * different: there it was a store that poisoned the rung above, here it is two readings that would
        * answer each other's question. **Rung 21 is the third of the pattern and the second for THIS
        * body's own reason**: `st_cmd3_noidx` sends the same opcode and the same argument with a
        * different flag word again, so a value-20 build carries one of the two and no other, and the
        * pair's cells stay attributable to their own flag words. */

#if STAGE90_XNU_STORAGE_PROBE == 19
/*
 * **743: rung 20's own body - THE SAME COMMAND WITH ITS RESPONSE DEMAND REMOVED, and the ladder's
 * first ONE-VARIABLE rung since it began addressing the card.**
 *
 * This body is `st_set_relative_addr` with ONE constant changed. Rung 19 measured that CMD3 with
 * `MMC_RSP_R1` is TAKEN (its word and its argument read back out of `COMMAND 0x0e` and
 * `ARGUMENT 0x08`), STARTED (`CMD_INHIBIT` set on the read after the store and still set 1.2 s
 * later) and NEVER FINISHED (`_rca_complete = 0`, `_rca_err = 0`, `_rca_status_any = 0` over
 * 5,088,256 samples) with the response registers EMPTIED (`0x40ff8080` -> `0x00000000`). Against that
 * stands the ONE command this ladder has ever driven to a completion: **CMD0, whose flags are zero
 * and which asks for no response at all** - and it is also the only command in the whole ladder whose
 * `CMD_INHIBIT` had cleared by the last sample. The variable to move is the response demand.
 *
 * **What changes**: the flags, from `MMC_RSP_R1` (`PRESENT|CRC|OPCODE`, `0x15`) to `MMC_RSP_NONE`
 * (`core.h:50`, the value 0), and with them the command word - `0x0300` instead of `0x031A`, because
 * `sdhci_cmd_to_flags` conditions every other bit on `MMC_RSP_PRESENT` and the `INDEX` bit that R1
 * carries goes with the demand. **What does not change**: the opcode, the non-zero argument, the
 * one-bit `INT_ENABLE 0x34` window opened immediately before the store and restored on ONE
 * unconditional line after the publishes, the two per-command guards inside `st_send_command`, the
 * inhibit triplet, and the three readings of `RESPONSE 0x10`.
 *
 * **THE R1 DECODE IS DELIBERATELY ABSENT, AND THAT IS A DESIGN DECISION RATHER THAN AN OVERSIGHT.**
 * Rung 19 published `_rca_state`, `_rca_ready` and `_rca_illegal` beside its raw word because an R1
 * CARD STATUS has those fields. This command asks for no status, so the register holds whatever the
 * last response-expecting command left there - and decoding THAT word as this command's answer is
 * 738's trap (a stale word read as a fresh one) in its cheapest form. A reader wanting the R1 fields
 * on an R1 command has rung 19's log; a reader wanting them here would be reading CMD1's OCR.
 *
 * **NO NEW REGISTER CLASS AND NO NEW STORE CLASS.** Two stores, both to `INT_ENABLE 0x34`;
 * `SIGNAL_ENABLE 0x38` read and NEVER written; no data path, no `POWER_CONTROL 0x29`, no GCC word,
 * no `core_mem` word, no `INT_STATUS` write and no byte of the medium. The failure mode is a
 * diagnosis and not a lost device: the 1.2 s bound is inside `st_send_command`, the ending is
 * unmoved, and a delivery on this block's shared SPI 123 line ends the run at the dispatcher -
 * `_irq_other_count = 1` / `iat = 155`, an ending this image already reads and survives.
 */
static __attribute__((noinline, noclone)) void st_cmd3_noresp(uint32_t int_enable)
{
    struct st_cmd_result c3;
    uint32_t before, after, held, readback, status_post;

    ST_LIVE("xnu_live_storage_nrsp_calls", 1u);

    /*
     * The freshness baseline, BEFORE the window opens - the same driver derivation rung 19's,
     * rung 17's and rung 18's readings are taken through, so the three logs' words are comparable
     * one for one.
     */
    before = (st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u) << 8)
           | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 11u);
    ST_LIVE("xnu_live_storage_nrsp_resp_pre", before);

    /* --- the window opens here, immediately before the command is put on the bus ---------------- */
    ST_LIVE("xnu_live_storage_nrsp_ena_wrote", int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
    held = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_nrsp_ena_held", held);
    ST_LIVE("xnu_live_storage_nrsp_sig_enable",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE));

    ST_LIVE("xnu_live_storage_nrsp_op", ST_CMD_OP_SET_RELATIVE_ADDR);
    ST_LIVE("xnu_live_storage_nrsp_flags", ST_MMC_RSP_NONE);
    ST_LIVE("xnu_live_storage_nrsp_arg", ST_MMC_RCA_1);
    st_send_command(ST_CMD_OP_SET_RELATIVE_ADDR, ST_MMC_RCA_1, ST_MMC_RSP_NONE, &c3);

    ST_LIVE("xnu_live_storage_nrsp_sent", c3.sent);
    ST_LIVE("xnu_live_storage_nrsp_word", c3.word_wrote);
    ST_LIVE("xnu_live_storage_nrsp_word_read", c3.word_read);
    ST_LIVE("xnu_live_storage_nrsp_arg_wrote", c3.arg_wrote);
    ST_LIVE("xnu_live_storage_nrsp_complete", c3.complete);
    ST_LIVE("xnu_live_storage_nrsp_err", c3.err);
    ST_LIVE("xnu_live_storage_nrsp_timeout", c3.timed_out);
    ST_LIVE("xnu_live_storage_nrsp_status_any", c3.status_any);
    ST_LIVE("xnu_live_storage_nrsp_any_polls", c3.status_any_polls);
    ST_LIVE("xnu_live_storage_nrsp_inhibit_after", c3.inhibit_after);
    ST_LIVE("xnu_live_storage_nrsp_inhibit_seen", c3.inhibit_seen);
    ST_LIVE("xnu_live_storage_nrsp_inhibit_last", c3.inhibit_last);
    ST_LIVE("xnu_live_storage_nrsp_polls", c3.polls);
    ST_LIVE("xnu_live_storage_nrsp_ticks", c3.ticks);
    ST_LIVE("xnu_live_storage_nrsp_clear_after", c3.clear_after);

    /*
     * The register read INSIDE the command. `rsp_present` is the driver's own condition and it is
     * CLEAR on this arm - the command asked for no response - so this word is read unconditionally
     * (rung 12's repair) and `resp_read` says the read happened, while the word itself is whatever
     * was in the register: a reading of the register's state and NOT this command's answer. That is
     * the whole reason the R1 decode is absent above.
     */
    ST_LIVE("xnu_live_storage_nrsp_resp", c3.resp);
    ST_LIVE("xnu_live_storage_nrsp_resp_read", c3.resp_read);

    /* --- the freshness pair's second half: the SAME derivation, after the command ---------------- */
    after = (st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u) << 8)
          | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 11u);
    ST_LIVE("xnu_live_storage_nrsp_resp_post", after);
    ST_LIVE("xnu_live_storage_nrsp_resp_moved", (after != before) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_nrsp_resp_is_arg", (after == ST_MMC_RCA_1) ? 1u : 0u);

    /* --- the window's ONE exit, unconditional, on the line after the publishes ------------------ */
    ST_LIVE("xnu_live_storage_nrsp_wrote_back", int_enable);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable);
    status_post = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    ST_LIVE("xnu_live_storage_nrsp_status_post", status_post);
    readback = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_nrsp_readback", readback);
    ST_LIVE("xnu_live_storage_nrsp_ps_after",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
    ST_LIVE("xnu_live_storage_nrsp_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE == 19 - one body, one caller, one changed constant and the driver's own CMD3
        * with nothing asked back. **The three cells rung 19 publishes and this body does NOT are `_state`,
        * `_ready` and `_illegal`** - the R1 decode - because a command that asks for no status leaves the
        * register holding the last response-expecting command's word, and decoding that word as this
        * command's answer is the stale-word trap in its cheapest form. */

#if STAGE90_XNU_STORAGE_PROBE >= 20
/*
 * **746: rung 21's own body - THE SAME COMMAND WITH ITS RESPONSE DEMAND KEPT AND ITS `INDEX` BIT TAKEN
 * OUT, and the ladder's first test of a rule that fits every word it has ever put on this bus.**
 *
 * **The rule, and where it came from.** Rung 20's press answered its own question (`_nrsp_complete = 1`,
 * the demand was the stall) and the same capture, read again, answered a question nobody had asked:
 * **CMD1 (`0x0102`) and CMD2 (`0x0209`) were NEVER STARTED** - `_cmd1_inhibit_after = 0` with
 * `_cmd1_inhibit_seen = 0` over 5,088,256 poll samples, `_cid_inhibit_seen = 0` over 5,088,000, in both
 * cases with the word read back out of `COMMAND 0x0E` - and identically in all three of the last three
 * presses. Five words are on the record:
 *
 *   `0x0000`  started, completed     the ladder's first completion
 *   `0x0300`  started, completed     0.255 ms
 *   `0x031A`  started, never finished
 *   `0x0102`  NEVER STARTED
 *   `0x0209`  NEVER STARTED
 *
 * and the one rule that fits all five is that **a word that asks for a response (`RESP_PRESENT`) and
 * carries no `INDEX` bit is DECLINED**. `0x0300` is the only word here that asks for nothing. This rung
 * moves ONE bit to test it: `ST_MMC_RSP_R1_NOIDX` is `ST_MMC_RSP_R1` with `MMC_RSP_OPCODE` removed, the
 * word is `0x030A`, and it sits one bit from each arm of the pair that is already measured - `0x031A`
 * with the `INDEX` gone, `0x0300` with the demand put back.
 *
 * **What changes from rung 20**: the flags, `MMC_RSP_NONE` -> `ST_MMC_RSP_R1_NOIDX`, and with them the
 * word, `0x0300` -> `0x030A`. **What does not change**: the opcode, the non-zero argument, the one-bit
 * `INT_ENABLE 0x34` window opened immediately before the store and restored on ONE unconditional line
 * after the publishes, the two per-command guards inside `st_send_command`, and the inhibit triplet.
 *
 * **AND IT CARRIES m756'S REPAIR, WHICH IS READ-ONLY AND IS DECLARED RATHER THAN SMUGGLED.** 743
 * section 7 wrote this pair of readings as *one arithmetic at three times - the driver's own
 * `(readl(RESPONSE + 0x1C) << 8) | readb(RESPONSE + 0x1B)`* and it was not one: `st_send_command` reads
 * `readl(RESPONSE 0x10)` - **word 0**, the word a 48-bit response actually lives in - while rung 19's
 * and rung 20's bodies took their pair through the **136-bit** formula, whose top word is `+0x1C`. So
 * `0x40ff8080 -> 0x00000000` compared word 0 against words 2 and 3, and the two agreed at the first
 * moment only because CMD1's word was still in word 0 while CMD2's leftover was still in words 2/3 (the
 * instance is m756). **This body's pair is `readl(RESPONSE 0x10)` at both moments** - the same
 * expression `st_send_command` reads for `resp` - so the three cells are ONE arithmetic at last, and
 * **the four raw words are published at the two moments this body owns** so that a reader can check the
 * claim rather than trust it. **What is NOT done**: the middle moment's four raw words are not
 * published, because that reading lives in `st_send_command` and putting four new offsets into the one
 * body every command in this ladder shares is a change this arm does not need to make its own point.
 *
 * **THE R1 DECODE IS BACK, AND RUNG 20'S REASON FOR LEAVING IT OUT IS WHY.** Rung 20 asks for no
 * response, so `rsp_present` is clear and the register holds the last response-expecting command's word;
 * decoding that as its own answer would be 738's stale-word trap. **This command DOES ask for a 48-bit
 * response** (`PRESENT` is set), so `_nidx_resp` is read for this command by the driver's own condition
 * - and the three fields are published beside the raw word, as rung 19 published them, with the same
 * caveat its record carried: a field is a decode of whatever the register holds, and whether the card
 * answered is a separate question this arm also asks (`_nidx_complete`, `_nidx_inhibit_last`).
 *
 * **NO NEW REGISTER CLASS AND NO NEW STORE CLASS.** Two stores, both to `INT_ENABLE 0x34`;
 * `SIGNAL_ENABLE 0x38` read and NEVER written; no data path, no `POWER_CONTROL 0x29`, no GCC word, no
 * `core_mem` word, no `INT_STATUS` write and no byte of the medium. The failure mode is a diagnosis and
 * not a lost device: the 1.2 s bound is inside `st_send_command`, the ending is unmoved, and a delivery
 * on this block's shared SPI 123 line ends the run at the dispatcher - `_irq_other_count = 1` /
 * `iat = 155`, an ending this image already reads and survives (709 ended that way on intid 170).
 *
 * **RUNG 23: THE SAME COMMAND UNDER THE VENDOR'S OWN ENABLE MASK - one constant, two new reads, and
 * the same two stores.** 765 section 1 measured that this ladder has enabled exactly ONE status bit in
 * its whole history while the vendor's `sdhci_init` enables ELEVEN, and 765 section 2 turned that into
 * the narrowest true statement of the ladder's headline negative: *no bit was ever seen in
 * `INT_STATUS`* is also *`INT_STATUS`'s error half has had its enable at zero for every one of those
 * samples*, and the two cannot be told apart from a log with one bit enabled. Rung 23 is the arm that
 * tells them apart, and it changes exactly three things, all guarded `>= 22`:
 *
 *   * **the enable word** - `ST_SDHCI_INT_ENABLE_CMD` (`0x000F0001`: the completion with TIMEOUT, CRC,
 *     END_BIT and INDEX) in place of the lone `SDHCI_INT_RESPONSE`. Five bits and not the vendor's
 *     eleven; the two omissions are AUTO_CMD_ERR and the DATA_* half, neither of which is a fact about
 *     the card (765 section 4's pre-registration);
 *   * **`_nidx_status_pre`** - `INT_STATUS 0x30` read between the enable store and its readback, with
 *     the wider mask in place and no command of this arm's on the bus;
 *   * **`_nidx_tout_ctl`** - `TIMEOUT_CONTROL 0x2E` read as a BYTE after the restore, the register no
 *     rung has ever read.
 *
 * **Why this is safe, and it is 730's own measurement rather than an argument.** The line rises on the
 * CONJUNCTION of `INT_ENABLE` and `SIGNAL_ENABLE`; `SIGNAL_ENABLE 0x38` is not written at this rung
 * either, and 730's press wrote `INT_ENABLE` on this very block, took no command and took no interrupt.
 * Rungs 17, 19, 20, 21 and 22 have each written this register with the signal enable at zero and none
 * took a delivery. **The primary answer needs no new cell at all**: `st_send_command`'s poll already
 * publishes the RAW, unmasked `INT_STATUS` word it first saw non-zero in (`_nidx_status_any`) and reads
 * it 5.09 million times, so the same poll that has always read zero is the poll that will read
 * `0x00010000` if the card is silent and the block knows it. This body's four outcomes are 765
 * section 4's table, and each one names the next act.
 */
static __attribute__((noinline, noclone)) void st_cmd3_noidx(uint32_t int_enable)
{
    struct st_cmd_result c3;
    uint32_t pre, post, held, readback, status_post, raw, ena;

    ST_LIVE("xnu_live_storage_nidx_calls", 1u);

    /*
     * The freshness baseline, BEFORE the window opens - the four raw words word-3-first in rung 18's own
     * order, and then the ONE word this body's three cells are all taken through. `pre` is the last
     * `raw` rather than a fifth read: two readings of one address at one moment would be a number no
     * cell could tell from the other, which is the defect this rung repairs, stated the other way round.
     */
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    ST_LIVE("xnu_live_storage_nidx_raw_pre0", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 8u);
    ST_LIVE("xnu_live_storage_nidx_raw_pre1", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
    ST_LIVE("xnu_live_storage_nidx_raw_pre2", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("xnu_live_storage_nidx_raw_pre3", raw);
    pre = raw;
    ST_LIVE("xnu_live_storage_nidx_resp_pre", pre);

    /* --- the window opens here, immediately before the command is put on the bus ---------------- */
    /*
     * **THE ENABLE WORD, AND RUNG 23 IS ONE CONSTANT WIDE.** Rung 21 opens this window on
     * `int_enable | SDHCI_INT_RESPONSE` - ONE bit - and 765 section 1 measured that the vendor's own
     * `sdhci_init` writes ELEVEN (`0x01FF0003`) into this register, so every `_status_any = 0` this
     * ladder has ever read was a reading about a MASK as much as about the block. Rung 23's window is
     * `ST_SDHCI_INT_ENABLE_CMD` = `0x000F0001`: the completion with the four command-level error bits
     * beside it, the five 765 section 4 pre-registered, and NOT the vendor's eleven (five of the other
     * six are the DATA_* errors, and the sixth is AUTO_CMD_ERR - neither is a fact about the card).
     * **Everything else about this window is rung 21's**: the same position, the same two stores, the
     * same `SIGNAL_ENABLE 0x38` left at zero, and the same restore. A value below 22 takes the rung-21
     * word, so the two arms differ in exactly one constant.
     */
#if STAGE90_XNU_STORAGE_PROBE >= 22
    ena = int_enable | (uint32_t)ST_SDHCI_INT_ENABLE_CMD;
#else
    ena = int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE;
#endif
    ST_LIVE("xnu_live_storage_nidx_ena_wrote", ena);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, ena);
#if STAGE90_XNU_STORAGE_PROBE >= 22
    /*
     * **`_nidx_status_pre`, and it is rung 23's third read - the one taken with the WIDER MASK IN
     * PLACE AND THE LADDER'S OWN COMMAND NOT YET ON THE BUS.** It is read HERE, between the store and
     * the readback, and not one line later, because every rung below has cleared the latch inside
     * `st_send_command` and the only moment that answers 765 section 2's open question about the
     * sticky bit 15 is this one: what the block holds, with the error enables up, before this arm has
     * asked it for anything. What it reads is NOT guaranteed zero - CMD0, CMD1 and CMD2 have already
     * been on this bus in this boot - and `_nidx_clear_after` below is the second half of the pair:
     * the same register read after `st_send_command`'s write-1-to-clear, which is what says whether
     * this arm's own command starts from a clean latch.
     */
    ST_LIVE("xnu_live_storage_nidx_status_pre",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS));
#endif
    held = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_nidx_ena_held", held);
    ST_LIVE("xnu_live_storage_nidx_sig_enable",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE));

    ST_LIVE("xnu_live_storage_nidx_op", ST_CMD_OP_SET_RELATIVE_ADDR);
    ST_LIVE("xnu_live_storage_nidx_flags", ST_MMC_RSP_R1_NOIDX);
    ST_LIVE("xnu_live_storage_nidx_arg", ST_MMC_RCA_1);
    st_send_command(ST_CMD_OP_SET_RELATIVE_ADDR, ST_MMC_RCA_1, ST_MMC_RSP_R1_NOIDX, &c3);

    ST_LIVE("xnu_live_storage_nidx_sent", c3.sent);
    ST_LIVE("xnu_live_storage_nidx_word", c3.word_wrote);
    ST_LIVE("xnu_live_storage_nidx_word_read", c3.word_read);
    ST_LIVE("xnu_live_storage_nidx_arg_wrote", c3.arg_wrote);
    ST_LIVE("xnu_live_storage_nidx_complete", c3.complete);
    ST_LIVE("xnu_live_storage_nidx_err", c3.err);
    ST_LIVE("xnu_live_storage_nidx_timeout", c3.timed_out);
    ST_LIVE("xnu_live_storage_nidx_status_any", c3.status_any);
    ST_LIVE("xnu_live_storage_nidx_any_polls", c3.status_any_polls);
    ST_LIVE("xnu_live_storage_nidx_inhibit_after", c3.inhibit_after);
    ST_LIVE("xnu_live_storage_nidx_inhibit_seen", c3.inhibit_seen);
    ST_LIVE("xnu_live_storage_nidx_inhibit_last", c3.inhibit_last);
#if STAGE90_XNU_STORAGE_PROBE >= 23
    /*
     * **Rung 24's one cell, published beside the window it shares.** `_nidx_cmdlow_seen` is over the
     * SAME first `ST_CMD_INHIBIT_SAMPLES` iterations as `_nidx_inhibit_seen`, so the two are read
     * against each other: `_nidx_inhibit_seen = 0x400` with `_nidx_cmdlow_seen = 0` is a block that
     * held its inhibit for all 1024 samples while the line never moved, and either one alone would
     * be a weaker statement. **Its ZERO is a reading only if the sampler outran the bus**, so it is
     * read against `_nidx_polls` and `_nidx_ticks`, which give the sampling period over exactly the
     * window the count is over.
     */
    ST_LIVE("xnu_live_storage_nidx_cmdlow_seen", c3.cmdlow_seen);
#endif
    ST_LIVE("xnu_live_storage_nidx_polls", c3.polls);
    ST_LIVE("xnu_live_storage_nidx_ticks", c3.ticks);
    ST_LIVE("xnu_live_storage_nidx_clear_after", c3.clear_after);

    /*
     * The register read INSIDE the command, and the middle moment of the three the freshness pair
     * brackets. `rsp_present` is SET on this arm - the command demands a 48-bit response - so this read
     * is the driver's own condition as well as the rung's, and `resp_read` beside it says the read
     * happened whatever the word turns out to be.
     */
    ST_LIVE("xnu_live_storage_nidx_resp", c3.resp);
    ST_LIVE("xnu_live_storage_nidx_resp_read", c3.resp_read);

    /*
     * Rung 19's three R1 fields, back for the reason its own record gave and this one repeats: the
     * command asks for a card status, so the fields are this command's to read - and the raw word is
     * published BESIDE them so no reader has to take a decode on trust. `_illegal` is bit 22 of the word
     * in the register, and on an arm whose register still holds the previous command's answer it is a
     * fact about THAT word, which is exactly what 738's trap is about.
     */
    ST_LIVE("xnu_live_storage_nidx_state", (c3.resp >> 9) & 0xFu);
    ST_LIVE("xnu_live_storage_nidx_ready", (c3.resp >> 8) & 0x1u);
    ST_LIVE("xnu_live_storage_nidx_illegal", (c3.resp >> 22) & 0x1u);

    /* --- the freshness pair's second half: the SAME reading as `pre` and as `c3.resp` ------------- */
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    ST_LIVE("xnu_live_storage_nidx_raw_post0", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 8u);
    ST_LIVE("xnu_live_storage_nidx_raw_post1", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
    ST_LIVE("xnu_live_storage_nidx_raw_post2", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("xnu_live_storage_nidx_raw_post3", raw);
    post = raw;
    ST_LIVE("xnu_live_storage_nidx_resp_post", post);
    ST_LIVE("xnu_live_storage_nidx_resp_moved", (post != pre) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_nidx_resp_is_arg", (post == ST_MMC_RCA_1) ? 1u : 0u);

    /* --- the window's ONE exit, unconditional, on the line after the publishes ------------------ */
    ST_LIVE("xnu_live_storage_nidx_wrote_back", int_enable);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable);
    status_post = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    ST_LIVE("xnu_live_storage_nidx_status_post", status_post);
#if STAGE90_XNU_STORAGE_PROBE >= 22
    /*
     * **`TIMEOUT_CONTROL 0x2E`, BYTE, AND IT IS THE REGISTER NO RUNG HAS EVER READ.** The vendor
     * writes it only from `sdhci_prepare_data` (sdhci.c:827-828) under `if (data || (cmd->flags &
     * MMC_RSP_BUSY))`, so on the vendor's own path a data-less command leaves it at its reset value -
     * which is why 765 section 1 called it "not a vendor act the ladder skipped" and why a non-zero
     * value here would be a finding rather than a confirmation. It is read HERE, after the restore, so
     * the reading is of a block this arm has stopped configuring, and it is a BYTE because `0x2E` is
     * 2-aligned and not 4-aligned and this register is 8-bit in the standard. `0x00` is the field's
     * LONGEST setting, not "no timeout" - the cell is about the arm's premise and not about whether
     * the block can time out.
     */
    ST_LIVE("xnu_live_storage_nidx_tout_ctl",
            (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL));
#endif
    readback = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_nidx_readback", readback);
    ST_LIVE("xnu_live_storage_nidx_ps_after",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
    ST_LIVE("xnu_live_storage_nidx_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 20 - one body, one caller, ONE FLAG BIT MOVED, and the ladder's
        * first command that both asks for a response and is a word the five-word rule says the block
        * should decline. **Three outcomes and each one names the next act**: `_nidx_inhibit_seen = 0`
        * with `_nidx_word_read = 0x030A` CONFIRMS the rule and explains CMD1 and CMD2 in the same
        * stroke; `_nidx_inhibit_seen > 0` with `_nidx_complete = 0` kills the rule's INDEX half and puts
        * the stall back on the response itself; `_nidx_complete = 1` kills the rule outright. */

#if STAGE90_XNU_STORAGE_PROBE >= 34
/*
 * **812: rung 35's own body - THE DRIVER'S FIRST COMMAND ABOVE CMD3, and the ladder's second 136-bit
 * response and its first with a NON-ZERO argument.**
 *
 * `mmc_attach_mmc`'s statement after `mmc_set_relative_addr` is `mmc_send_csd(card, card->raw_csd)`
 * (`mmc.c:1420`), which is `mmc_ops.c:296-302` - `mmc_send_cxd_native(card->host, card->rca << 16,
 * csd, MMC_SEND_CSD)` - with `cmd.flags = MMC_RSP_R2 | MMC_CMD_AC` (`mmc_ops.c:224`). `mmc.h:38`
 * says the same three things, and its own words for them are "ac [31:16] RCA R2" - read that
 * line before trusting a CSD argument taken from anywhere but `ST_MMC_RCA_1`.
 *
 * **THE ARGUMENT IS THE HOST'S OWN CONSTANT, AND SETTLING THAT IS THE FIRST THING THIS RUNG DID.**
 * `card->rca` is assigned BY THE DRIVER at `mmc.c:1400` (``card->rca = 1``), nine lines above the
 * CMD3 that carries it, and `mmc_set_relative_addr` sends it as `card->rca << 16` (`mmc_ops.c:203`).
 * **An MMC card does not return an RCA to the host.** `mmc.h:32` gives CMD3 the response `R1`, which
 * is the 32-bit CARD STATUS - `R1_CURRENT_STATE` is its bits 12:9 (`mmc.h:141`), `R1_READY_FOR_DATA`
 * its bit 8 (`mmc.h:142`) - and there is no RCA field in it at any offset. (An SD card returns one in
 * R6, and even there the field is bits 31:16.) So this body's argument is `ST_MMC_RCA_1`, the same
 * constant CMD3 was sent with, and **the three cells the archive read as "RCA 0x0500" out of
 * `_nidx_resp = 0x00000500` were reading a field that exists in neither format**: that word under the
 * vendor's own two macros is `R1_READY_FOR_DATA` set with `R1_CURRENT_STATE = 2`, and 2 is
 * `R1_STATE_IDENT` (`mmc.h:149`) - the state a card is in AFTER CMD2 and BEFORE a CMD3 it has
 * accepted. The correction is written out in `docs/experiments/experiment-812-...md`; it changes
 * nothing in this body, and it is exactly why the next cell below exists.
 *
 * **THE PRECONDITION IS PUBLISHED AS A CELL RATHER THAN ASSUMED, AND THE READING IS RUNG 18'S OWN
 * SHAPE.** CMD9 is legal in STBY and TRAN; a card still in IDENT is not listening for it. This ladder
 * has never had a completed CMD3 (`_nidx_complete = 1` at rung 21 with the card's own answer saying
 * IDENT), so a body that assumed the precondition would be publishing a number about a bus it had
 * not checked. `RESPONSE 0x10` is therefore read BETWEEN CMD3 and CMD9 - a moment no rung has read at
 * - and published raw and decoded: `_csd_pre_state` 3 is `R1_STATE_STBY` and 4 is `R1_STATE_TRAN`,
 * which is where CMD9 belongs, and any other value is the reading that says this command went out too
 * early. That is `st_resp_before`'s precedent (rung 18, the first rung whose whole act was a read at a
 * moment no rung had read at) and not a new idea.
 *
 * **THE 136-BIT ASSEMBLY IS THE SIXTH COPY OF ONE ARITHMETIC, WRITTEN IN THE DRIVER'S OWN ORDER.**
 * `sdhci.c:1163-1172` reads word 3 first (`+ 12`), shifts each word left by a byte and ORs in the byte
 * ONE BELOW that word, and gives the last word no byte at all. There is no assembler function in this
 * file to call - the five definitions are five definitions, which is what 810 section 8 says - and
 * `tools/check_response_word_order.py` is in `make check` and reads THIS file: an ascending order, a
 * repeated offset inside this one command segment, or a byte taken from a word other than the one
 * below it is a **build refusal**, so this copy cannot silently disagree with the five above it.
 *
 * **AND THE FOUR WORDS ARE DECODED WITH THE VENDOR'S OWN OFFSETS, WHICH IS WHAT MAKES A CSD ANSWER
 * CHECKABLE FROM OUTSIDE THIS IMAGE - AND `resp` IN THAT CALL IS THE ARRAY THE ASSEMBLER PRODUCES,
 * NOT THE FOUR REGISTERS.** 814's correction, and the reason the two must not be conflated: `resp[]`
 * holds `(raw << 8) | (the next word's top byte)`, so a field read off a raw word is one byte too
 * high. `mmc_decode_csd` (`mmc.c:147`) reads `csd->structure =
 * UNSTUFF_BITS(resp, 126, 2)` at **`mmc.c:158`** FIRST and returns `-EINVAL` from `mmc.c:162`
 * unless it is non-zero, then `csd->mmca_vsn = UNSTUFF_BITS(resp, 122, 4)` (`mmc.c:165`).
 *
 * **819 CORRECTED THE TWO VALUES THIS PARAGRAPH USED TO DEMAND, AND THE SENTENCE THAT DERIVED THEM.**
 * It read **"`_csd_structure` must read 1"**, and it reached that from `mmc.c:110` - which is inside
 * **`mmc_decode_cid`** and switches on `cid->mmca_vsn`, the **CID's** spec version, which is why
 * `case 2/3/4` there is the one carrying the 32-bit serial. That is not the switch `mmc_decode_csd`
 * makes: **`mmc.c:158-163` switches on `structure`**, and "the CSD is v1.2" does not give `1` even in
 * this vendor's own comment, where v1.2 is `2`. **Two different switch variables in two different
 * decoders, read as one - `[[mi4-one-value-two-definitions]]`.** The vendor's predicate is
 * `mmc.c:159`'s `if (csd->structure == 0)` and nothing else: `{1, 2}` is the **SD** family's reading
 * of a shared field name, and an eMMC v4.4/4.41 card reads **3** - which is what the rung-35 press
 * MEASURED (`_csd_structure = 3` with `_csd_mmca_vsn = 4` = `CSD_SPEC_VER_4`, `mmc.h:272`). **The
 * two values this ladder did not supply and cannot have chosen are `3` and `4`**, which is the same
 * free cross-check the CID's `serial` gave against `ro.serialno` (811 section 4), read the way the
 * card's own family reads it. `ST_CSD_STRUCTURE_IS_CSD` above is that predicate made a build
 * refusal, so a later rung cannot narrow it back to the SD reading and misread a press again.
 * The rest of the decode is the vendor's `mmc.c:165-196` verbatim:
 * `cmdclass` at bits 95:84, `read_blkbits` at 83:80, `C_SIZE` at 73:62 and `C_SIZE_MULT` at 49:47.
 *
 * **`_csd_capacity_blocks` IS PUBLISHED WITH ITS OWN LIMIT STATED**: `mmc.c:178`'s
 * `csd->capacity = (1 + C_SIZE) << (C_SIZE_MULT + 2)` is a 12-bit C_SIZE wide and cannot express a
 * 16 GB part, which is why `mmc_get_ext_csd` (`mmc.c:201`) says in the vendor's own words that *"high
 * capacity cards should have this 'magic' size stored in their CSD"* and takes the real size from the
 * EXT_CSD. So a placeholder here is the EXPECTED reading on this card and is not a defect in the
 * decode - and it is 809's wall, CMD8 `SEND_EXT_CSD`, named one rung early by arithmetic.
 */
static __attribute__((noinline, noclone)) void st_send_csd(uint32_t int_enable)
{
    struct st_cmd_result c9;
    uint32_t pre, held, readback, status_post;
    uint32_t w0, w1, w2, w3, a0, a1, a2, a3, c_size;

    ST_LIVE("xnu_live_storage_csd_calls", 1u);

    /*
     * The precondition, taken at the only moment it matters. Three decodes of one word, and they are
     * the vendor's fields and not new names: `R1_CURRENT_STATE` (`mmc.h:141`), `R1_READY_FOR_DATA`
     * (`mmc.h:142`) and `R1_ILLEGAL_COMMAND` (bit 22) - the bit `mmc_send_op_cond`'s own caller reads
     * to mean the card did not understand the command at all.
     */
    pre = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("xnu_live_storage_csd_pre_resp", pre);
    ST_LIVE("xnu_live_storage_csd_pre_state", (pre >> 9) & 0xFu);
    ST_LIVE("xnu_live_storage_csd_pre_ready", (pre >> 8) & 0x1u);
    ST_LIVE("xnu_live_storage_csd_pre_illegal", (pre >> 22) & 0x1u);

    /* --- the window opens here, immediately before CMD9 is put on the bus ---------------------- */
    {
        uint32_t ena;
        ena = int_enable | (uint32_t)ST_SDHCI_INT_ENABLE_CMD;
        ST_LIVE("xnu_live_storage_csd_ena_wrote", ena);
        st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, ena);
    }
    held = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_csd_ena_held", held);
    ST_LIVE("xnu_live_storage_csd_sig_enable",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE));

    ST_LIVE("xnu_live_storage_csd_op", ST_CMD_OP_SEND_CSD);
    ST_LIVE("xnu_live_storage_csd_arg", ST_MMC_RCA_1);
    ST_LIVE("xnu_live_storage_csd_flags", ST_MMC_RSP_R2);
    st_send_command(ST_CMD_OP_SEND_CSD, ST_MMC_RCA_1, ST_MMC_RSP_R2, &c9);

    ST_LIVE("xnu_live_storage_csd_sent", c9.sent);
    ST_LIVE("xnu_live_storage_csd_arg_wrote", c9.arg_wrote);
    ST_LIVE("xnu_live_storage_csd_word", c9.word_wrote);
    ST_LIVE("xnu_live_storage_csd_word_read", c9.word_read);
    ST_LIVE("xnu_live_storage_csd_complete", c9.complete);
    ST_LIVE("xnu_live_storage_csd_err", c9.err);
    ST_LIVE("xnu_live_storage_csd_timeout", c9.timed_out);
    ST_LIVE("xnu_live_storage_csd_status_any", c9.status_any);
    ST_LIVE("xnu_live_storage_csd_any_polls", c9.status_any_polls);
    ST_LIVE("xnu_live_storage_csd_inhibit_seen", c9.inhibit_seen);
    ST_LIVE("xnu_live_storage_csd_inhibit_last", c9.inhibit_last);
    ST_LIVE("xnu_live_storage_csd_inhibit_after", c9.inhibit_after);
    ST_LIVE("xnu_live_storage_csd_cmdlow_seen", c9.cmdlow_seen);
    ST_LIVE("xnu_live_storage_csd_ps_before", c9.ps_before);
    ST_LIVE("xnu_live_storage_csd_inhibit_before", c9.inhibit_before);
    ST_LIVE("xnu_live_storage_csd_inhibit_polls", c9.inhibit_polls);
    ST_LIVE("xnu_live_storage_csd_inhibit_ticks", c9.inhibit_ticks);
    ST_LIVE("xnu_live_storage_csd_inhibit_timeout", c9.inhibit_timeout);
    ST_LIVE("xnu_live_storage_csd_stale", c9.stale);
    ST_LIVE("xnu_live_storage_csd_clear_wrote", c9.clear_wrote);
    ST_LIVE("xnu_live_storage_csd_ps_after", c9.ps_after);
    ST_LIVE("xnu_live_storage_csd_rsp_present", c9.rsp_present);
    ST_LIVE("xnu_live_storage_csd_resp_read", c9.resp_read);
    ST_LIVE("xnu_live_storage_csd_polls", c9.polls);
    ST_LIVE("xnu_live_storage_csd_ticks", c9.ticks);
    ST_LIVE("xnu_live_storage_csd_clear_after", c9.clear_after);
    ST_LIVE("xnu_live_storage_csd_resp_short", c9.resp);

    /*
     * sdhci.c:1163-1172, the 136-bit branch, in the driver's own order - word 3 first.
     * `<< 8` on each, and the byte below each word except the last. ONE command segment, four 32-bit
     * offsets descending and three byte offsets at word-1: that is the whole of what
     * `tools/check_response_word_order.py` reads in this body.
     *
     * **THE FOUR RAW WORDS AND THE FOUR ASSEMBLED WORDS ARE TWO DIFFERENT VALUES, AND 814 IS THE
     * STEP THAT FOUND OUT THE HARD WAY.** `w0..w3` hold the REGISTERS; `a0..a3` hold what
     * `sdhci_finish_command` makes of them, and the vendor's decoders are written against the
     * second: `a_i = (w_i << 8) | (w_{i+1} >> 24)`, so `a_i` is NOT `w_i` - it is `w_i` moved up one
     * byte with the next word's TOP byte shifted in below. Rung 18's own comment already says it in
     * one line ("the four raw words holding that same 32-bit value one byte further along"). The
     * first version of this body decoded `w0..w3` with the vendor's shifts anyway, which reads every
     * field one byte too high - and the two most visible fields, `structure` and `mmca_vsn`, off the
     * top byte the assembler SHIFTS OUT.
     */
    w0 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    w1 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 8u);
    w2 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
    w3 = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("xnu_live_storage_csd_raw0", w0);
    ST_LIVE("xnu_live_storage_csd_raw1", w1);
    ST_LIVE("xnu_live_storage_csd_raw2", w2);
    ST_LIVE("xnu_live_storage_csd_raw3", w3);
    a0 = (w0 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 11u);
    a1 = (w1 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 7u);
    a2 = (w2 << 8) | (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 3u);
    a3 = w3 << 8;
    ST_LIVE("xnu_live_storage_csd_resp0", a0);
    ST_LIVE("xnu_live_storage_csd_resp1", a1);
    ST_LIVE("xnu_live_storage_csd_resp2", a2);
    ST_LIVE("xnu_live_storage_csd_resp3", a3);
#if STAGE90_XNU_STORAGE_PROBE >= 37
    /*
     * **Rung 38's carry, guarded so that rungs 34 through 36 build the body they were pressed with.**
     * Four 32-bit stores into `.bss` and one flag: no device access, no register, and nothing any
     * clause about this body's device surface has ever asserted. The hand-off exists because
     * `RESPONSE` does not hold a CSD two commands later, and `st_send_ext_csd` needs this card's TAAC
     * to compute a data timeout the way `mmc_set_data_timeout` does. **The alternative was to send
     * CMD9 a second time, which would put a command on the bus whose only purpose is to re-read
     * something this ladder already read** - so the carry is both cheaper and closer to the driver,
     * which keeps `card->csd` across every command between the two.
     */
    st_csd_words[0] = a0;
    st_csd_words[1] = a1;
    st_csd_words[2] = a2;
    st_csd_words[3] = a3;
    st_csd_words_valid = 1u;
    ST_LIVE("xnu_live_storage_csd_carried", 1u);
#endif

    /*
     * **The vendor's own field offsets, over the four ASSEMBLED words above.** `UNSTUFF_BITS(resp,
     * start, size)` indexes `resp[3 - start/32]` with `resp[0]` holding bits 127:96, and `resp[]` is
     * `a0..a3` - the array `mmc_send_cxd_native` copies out of `cmd.resp[]`, which
     * `sdhci_finish_command` filled with the assembler above. 811 section 4 checked the mapping
     * arithmetically against the CID's serial, whose bits 47:16 come out of `a2`/`a3` as the phone's
     * own `ro.serialno`; the same two-piece call on `w2`/`w3` gives `0x014a2fe0`, which is not a
     * serial number. Each field below is the vendor's call written out with its shift and mask, so a
     * reader can check the arithmetic instead of trusting a helper:
     *
     *   mmc.c:158  structure    = UNSTUFF_BITS(resp, 126, 2)  -> (a0 >> 30) & 0x3
     *   mmc.c:165  mmca_vsn     = UNSTUFF_BITS(resp, 122, 4)  -> (a0 >> 26) & 0xF
     *   mmc.c:174  cmdclass     = UNSTUFF_BITS(resp,  84, 12) -> (a1 >> 20) & 0xFFF
     *   mmc.c:180  read_blkbits = UNSTUFF_BITS(resp,  80, 4)  -> (a1 >> 16) & 0xF
     *   mmc.c:177  C_SIZE       = UNSTUFF_BITS(resp,  62, 12) -> spans the a2/a1 boundary, shift 30
     *   mmc.c:176  C_SIZE_MULT  = UNSTUFF_BITS(resp,  47, 3)  -> (a2 >> 15) & 0x7
     *
     * **819 RE-MEASURED EVERY CITATION IN THIS TABLE AGAINST THE VENDOR'S FILE, AND FOUR OF THE SIX
     * WERE WRONG** - `173` (`max_dtr`) for cmdclass, `185` (`write_blkbits`, **a different field**)
     * for read_blkbits, `175` for C_SIZE and `174` (`cmdclass`) for C_SIZE_MULT. **A previous pass
     * had audited this area and declared it clean**, against a list someone had written down; these
     * four were not on the list, which is m810's shape - a verdict about the cells that were read,
     * published as a verdict about the file. `mmc.c:147` (the definition), `:162`, `:110`, `:201`,
     * `:165` and `:165-196` re-measure correct and are unchanged. **A citation table is not checked
     * by anything**, so read it against the file rather than against this paragraph.
     *
     * C_SIZE spans the boundary and is the one field where a reader can be wrong quietly, so it is
     * written as the vendor's own two-piece expression rather than as an approximation of it. **A
     * right shift of a raw word is now a build refusal** - `tools/check_response_word_order.py`'s
     * rule 4, added by 814 for exactly this defect - so the next copy of this arithmetic cannot be
     * written the way this one first was.
     */
    ST_LIVE("xnu_live_storage_csd_structure", (a0 >> 30) & 0x3u);
    ST_LIVE("xnu_live_storage_csd_mmca_vsn", (a0 >> 26) & 0xFu);
    ST_LIVE("xnu_live_storage_csd_cmdclass", (a1 >> 20) & 0xFFFu);
    ST_LIVE("xnu_live_storage_csd_read_blkbits", (a1 >> 16) & 0xFu);
    ST_LIVE("xnu_live_storage_csd_c_size_mult", (a2 >> 15) & 0x7u);
    c_size = ((a2 >> 30) | (a1 << 2)) & 0xFFFu;
    ST_LIVE("xnu_live_storage_csd_c_size", c_size);
    ST_LIVE("xnu_live_storage_csd_capacity_blocks",
            (1u + c_size) << (((a2 >> 15) & 0x7u) + 2u));

    /* --- the window's ONE exit, unconditional, on the line after the publishes ------------------ */
    ST_LIVE("xnu_live_storage_csd_wrote_back", int_enable);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable);
    status_post = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    ST_LIVE("xnu_live_storage_csd_status_post", status_post);
    readback = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_csd_readback", readback);
    ST_LIVE("xnu_live_storage_csd_ps_after",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
    ST_LIVE("xnu_live_storage_csd_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 34 - one body, one caller, TWO stores and the ladder's second
        * 136-bit read - and the first one whose argument is a constant rather than zero.
        *
        * **819: THIS TABLE WAS PRESSED AND IT WAS WRONG, AND THE MEASUREMENT IS NOW THE ROW.** As
        * written it said `_csd_complete = 1` with `_csd_structure = 1` is a CSD and that **"a
        * structure that is neither 1 nor 2 is a 136-bit register read that is NOT a response to this
        * command"** - so when the press answered `_csd_structure = 3`, the table's SECOND row fired
        * on a genuine eMMC CSD. The predicate it should have carried is the vendor's own,
        * `mmc.c:159`'s `structure == 0`, which is `ST_CSD_STRUCTURE_IS_CSD` above and is a build
        * refusal rather than a sentence. **The corrected rows, in the order the press measured
        * them**: `_csd_complete = 1`, `_csd_err = 0`, `_csd_mmca_vsn = 4` and
        * `ST_CSD_STRUCTURE_IS_CSD(_csd_structure)` - MEASURED, and it is a real eMMC v4 CSD, so CMD7
        * is next; `_csd_complete = 1` with a structure the vendor rejects (`0`) is 738's stale-word
        * trap one rung up; `_csd_complete = 0` with `_csd_pre_state = 2` puts the wall where the
        * measurement already points - CMD3 has never been accepted, so the card is in IDENT and CMD9
        * is a command it is not obliged to answer. **An outcome table is a claim in a comment with a
        * table drawn round it, and nothing checks it** - `[[mi4-a-claim-in-a-comment-is-not-a-check]]`,
        * which is why the surviving predicate is an assertion and not a row. */

#if STAGE90_XNU_STORAGE_PROBE >= 35
/*
 * **819: rung 36's own body - THE DRIVER'S NEXT COMMAND, AND THE FIRST WORD IN THIS LADDER THAT ASKS
 * THE BLOCK TO CHECK AN OPCODE ECHO.**
 *
 * `mmc_attach_mmc`'s statement after `mmc_send_csd` is `mmc_select_card(card)` (`mmc.c:1436`), which
 * is `mmc_ops.c:26`'s `_mmc_select_card` with `card` non-NULL: `cmd.opcode = MMC_SELECT_CARD`
 * (`mmc_ops.c:33`), `cmd.arg = card->rca << 16` (`mmc_ops.c:36`) and
 * `cmd.flags = MMC_RSP_R1 | MMC_CMD_AC` (`mmc_ops.c:37`). `mmc.h:36` states the same three things in
 * the driver's own shorthand - "ac [31:16] RCA R1" - so the opcode, the argument and the response
 * format are all read off the vendor's two files and not chosen here.
 *
 * **THE WORD IS THE DRIVER'S OWN, AND THAT IS THIS RUNG'S SECOND READING.** `MMC_RSP_R1` is
 * `PRESENT | CRC | OPCODE`, so this arm's command word is **`0x071A`** (opcode 7 << 8 | the flag byte
 * `0x1A`) - and **the flag byte is the byte 817 showed this ladder removing a bit from on CMD3 at
 * rung 21 and has not sent since**: the live CMD3 sends `0x030A`
 * (`ST_MMC_RSP_R1_NOIDX`, flag byte `0x0A`), with `SDHCI_CMD_INDEX` clear. (**`0x031A` is CMD3's
 * WORD, and reading it as "the `MMC_RSP_R1` word" is the mistake 819 made and the assertion below
 * caught** - the word and the flag byte are two different values and only one of them is this
 * rung's.) Rung 21 took that bit out on the evidence of
 * the rung-19 press, and 817 read the same press and the whole archive and measured that CMD1 had
 * never completed on any press before rung 30 - so the hang that justified the removal was the block
 * still inhibited by a CMD2 that never finished, and the deviation was inherited from an artifact.
 * **With the bit clear, a completion with no error means only that a CRC-valid 48-bit frame arrived;
 * with it set, `SDHCI_CMD_INDEX` has the block compare the response's index field against 7 and raise
 * `SDHCI_INT_INDEX` (`0x00080000`, one of the four bits `_sel_err` reads) when it differs.** So
 * `_sel_err = 0` beside `_sel_complete = 1` says the frame IS CMD7's response - the attribution 813
 * section 5 could not make and this ladder has never been able to make about any command.
 *
 * **WHY THE BIT IS ON THIS RUNG AND NOT PUT BACK ON CMD3.** One rung moves one thing, and CMD3's word
 * stays at `0x030A` in the same boot as this body's `0x071A`: the two commands run on the same chain
 * ten milliseconds apart, and `_nidx_*` and `_sel_*` are then a comparison of the two words on one
 * bus - same flag byte except for the one bit, and an opcode apart. **If this body hangs, the rung has bought 817's answer and not CMD7's** - the NEXT arm drops
 * the bit and re-arms - and that risk is stated here rather than discovered by the press.
 *
 * **WHAT THIS BODY DOES NOT DO: DECODE A PRECONDITION OUT OF `RESPONSE`.** CMD7 needs the card in
 * STBY, and the register read before it holds **the last 32 bits of the CSD** (`_csd_resp_short`),
 * which is not an R1 - there is no `R1_CURRENT_STATE` in it. A `_sel_pre_state` taken from that word
 * would be m812's defect (a field read out of a format the word does not have) and m819's (a
 * classification drawn from the wrong family), committed on purpose. So the pre-read is published
 * RAW (`_sel_pre_resp` and the four `_sel_raw_pre*` rows) and **nothing is decoded from it**; the
 * state this command needs is the one CMD3 measured, published as `_nidx_state`, and the state this
 * command PRODUCES is `_sel_state` from its own response.
 *
 * **The four raw words are read on both sides, in rung 18's order.** A 48-bit response writes
 * `RESPONSE + 0` alone, and the 136-bit CMD9 above it wrote all four - so the two rows are also the
 * cheapest statement of which format the block actually read. `tools/check_response_word_order.py`
 * reads this file, so the four offsets descending and the three bytes at word-1 are a build refusal
 * if this copy disagrees with the six above it; this body reads no bytes and shifts nothing, which is
 * the driver's own shape for a short response (`sdhci.c:1177`'s `resp[0]` straight out of the
 * register).
 */
static __attribute__((noinline, noclone)) void st_select_card(uint32_t int_enable)
{
    struct st_cmd_result c7;
    uint32_t pre, post, held, readback, status_post, raw, ena;

    ST_LIVE("xnu_live_storage_sel_calls", 1u);

    /* --- the freshness baseline, BEFORE the window opens: one word read four times ------------- */
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    ST_LIVE("xnu_live_storage_sel_raw_pre0", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 8u);
    ST_LIVE("xnu_live_storage_sel_raw_pre1", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
    ST_LIVE("xnu_live_storage_sel_raw_pre2", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("xnu_live_storage_sel_raw_pre3", raw);
    pre = raw;
    ST_LIVE("xnu_live_storage_sel_resp_pre", pre);

    /* --- the window opens here, immediately before the command is put on the bus ---------------- */
    ena = int_enable | (uint32_t)ST_SDHCI_INT_ENABLE_CMD;
    ST_LIVE("xnu_live_storage_sel_ena_wrote", ena);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, ena);
    ST_LIVE("xnu_live_storage_sel_status_pre",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS));
    held = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_sel_ena_held", held);
    ST_LIVE("xnu_live_storage_sel_sig_enable",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE));

    ST_LIVE("xnu_live_storage_sel_op", ST_CMD_OP_SELECT_CARD);
    ST_LIVE("xnu_live_storage_sel_flags", ST_MMC_RSP_R1);
    ST_LIVE("xnu_live_storage_sel_arg", ST_MMC_RCA_1);
    st_send_command(ST_CMD_OP_SELECT_CARD, ST_MMC_RCA_1, ST_MMC_RSP_R1, &c7);

    ST_LIVE("xnu_live_storage_sel_sent", c7.sent);
    ST_LIVE("xnu_live_storage_sel_word", c7.word_wrote);
    ST_LIVE("xnu_live_storage_sel_word_read", c7.word_read);
    ST_LIVE("xnu_live_storage_sel_arg_wrote", c7.arg_wrote);
    ST_LIVE("xnu_live_storage_sel_complete", c7.complete);
    ST_LIVE("xnu_live_storage_sel_err", c7.err);
    ST_LIVE("xnu_live_storage_sel_timeout", c7.timed_out);
    ST_LIVE("xnu_live_storage_sel_status_any", c7.status_any);
    ST_LIVE("xnu_live_storage_sel_any_polls", c7.status_any_polls);
    ST_LIVE("xnu_live_storage_sel_inhibit_after", c7.inhibit_after);
    ST_LIVE("xnu_live_storage_sel_inhibit_seen", c7.inhibit_seen);
    ST_LIVE("xnu_live_storage_sel_inhibit_last", c7.inhibit_last);
    ST_LIVE("xnu_live_storage_sel_inhibit_before", c7.inhibit_before);
    ST_LIVE("xnu_live_storage_sel_inhibit_polls", c7.inhibit_polls);
    ST_LIVE("xnu_live_storage_sel_inhibit_ticks", c7.inhibit_ticks);
    ST_LIVE("xnu_live_storage_sel_inhibit_timeout", c7.inhibit_timeout);
    ST_LIVE("xnu_live_storage_sel_cmdlow_seen", c7.cmdlow_seen);
    ST_LIVE("xnu_live_storage_sel_stale", c7.stale);
    ST_LIVE("xnu_live_storage_sel_clear_wrote", c7.clear_wrote);
    ST_LIVE("xnu_live_storage_sel_polls", c7.polls);
    ST_LIVE("xnu_live_storage_sel_ticks", c7.ticks);
    ST_LIVE("xnu_live_storage_sel_clear_after", c7.clear_after);

    /* --- the response, and the four R1 fields the vendor's own macros name ---------------------- */
    /*
     * `mmc.h:141` `R1_CURRENT_STATE(x)` is `(x & 0x00001E00) >> 9`, `mmc.h:142` `R1_READY_FOR_DATA`
     * is bit 8, `mmc.h:143` `R1_SWITCH_ERROR` is bit 7, `mmc.h:144` `R1_EXCEPTION_EVENT` is bit 6 -
     * and `ILLEGAL_COMMAND` is bit 22, which the vendor's header does not name as a macro and
     * `mmc_send_op_cond`'s own caller reads anyway (the ladder's rung-19 comment says where).
     * **`R1_CURRENT_STATE` is read as `mmc.h:141`'s own expression**, shifted and masked the way the
     * vendor writes it, so a reader checks the arithmetic instead of trusting a helper.
     */
    ST_LIVE("xnu_live_storage_sel_resp", c7.resp);
    ST_LIVE("xnu_live_storage_sel_resp_read", c7.resp_read);
    ST_LIVE("xnu_live_storage_sel_rsp_present", c7.rsp_present);
    ST_LIVE("xnu_live_storage_sel_state", (c7.resp & 0x00001E00u) >> 9);
    ST_LIVE("xnu_live_storage_sel_ready", (c7.resp >> 8) & 0x1u);
    ST_LIVE("xnu_live_storage_sel_switch_err", (c7.resp >> 7) & 0x1u);
    ST_LIVE("xnu_live_storage_sel_illegal", (c7.resp >> 22) & 0x1u);

    /* --- the freshness pair's second half: the SAME reading as `pre` and as `c7.resp` ------------ */
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    ST_LIVE("xnu_live_storage_sel_raw_post0", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 8u);
    ST_LIVE("xnu_live_storage_sel_raw_post1", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
    ST_LIVE("xnu_live_storage_sel_raw_post2", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("xnu_live_storage_sel_raw_post3", raw);
    post = raw;
    ST_LIVE("xnu_live_storage_sel_resp_post", post);
    ST_LIVE("xnu_live_storage_sel_resp_moved", (post != pre) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_sel_resp_is_arg", (post == ST_MMC_RCA_1) ? 1u : 0u);

    /* --- the window's ONE exit, unconditional, on the line after the publishes ------------------ */
    ST_LIVE("xnu_live_storage_sel_wrote_back", int_enable);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable);
    status_post = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    ST_LIVE("xnu_live_storage_sel_status_post", status_post);
    ST_LIVE("xnu_live_storage_sel_tout_ctl",
            (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL));
    readback = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_sel_readback", readback);
    ST_LIVE("xnu_live_storage_sel_ps_after",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
    ST_LIVE("xnu_live_storage_sel_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 35 - one body, one caller, the driver's own word, and FOUR
        * outcomes each of which names the next act: `_sel_complete = 1` with `_sel_err = 0` and
        * `_sel_state = 4` is the card SELECTED and in TRAN - the data path opens and CMD8 is next;
        * `_sel_complete = 1` with `_sel_err` carrying `SDHCI_INT_INDEX` (`0x00080000`) is a 48-bit
        * frame whose opcode field is NOT 7 - the first direct statement this ladder can make about
        * what is on the CMD line, and 817's hypothesis answered against it; `_sel_complete = 1` with
        * `_sel_illegal = 1` is the card itself refusing - IT IS IN IDENT AND CMD3 WAS NOT ACCEPTED,
        * whatever its `_nidx_*` cells say; `_sel_complete = 0` with `CMD_INHIBIT` asserted at the end
        * is the word `0x071A` hanging a live block, which is rung 21's reason measured on a chain
        * that works, and the successor drops the bit. */

#if STAGE90_XNU_STORAGE_PROBE >= 36
/*
 * **822: rung 37's own body - THE DRIVER'S OWN "IS THE CARD ALIVE" CHECK, AND THE FIRST WORD IN THIS
 * LADDER WHOSE FLAG WORD CARRIES BITS THIS CONTROLLER DOES NOT READ.**
 *
 * **WHAT THE COMMAND IS.** CMD13 `SEND_STATUS` (`mmc.h:42`, `ac [31:16] RCA R1`), whose body in the
 * driver is `mmc_ops.c:468`'s `mmc_send_status`: `cmd.opcode = MMC_SEND_STATUS` (`mmc_ops.c:476`),
 * `cmd.arg = card->rca << 16` (`mmc_ops.c:478`, under `if (!mmc_host_is_spi(card->host))` - this host
 * is not SPI, so the argument IS taken) and
 *
 *     cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC;      // mmc_ops.c:479
 *
 * **AND THAT FLAG WORD IS NOT CMD7's.** `MMC_RSP_SPI_R2` is `MMC_RSP_SPI_S1 | MMC_RSP_SPI_S2` =
 * `(1 << 7) | (1 << 8)` (`core.h:40-41`, `:70`), so the driver's flag word for this command is
 * **`0x0195`** where CMD7's is `0x0015`: two extra bits, and they are SPI-mode bits. This rung sends
 * **the driver's word verbatim** and publishes the mapping's answer beside it
 * (`_sta_flags_driver`, `_sta_flags_mapped`), because whether those two bits reach the COMMAND
 * register is a property of `sdhci_cmd_to_flags` and not of the driver's declaration - and the build
 * clause below asserts the answer out of the linked image.
 *
 * **THE ANSWER IS THAT THEY DO NOT, AND THIS IS THE ONE THING THIS RUNG'S PROSE COULD GET WRONG.**
 * `sdhci.c:1131-1143` reads exactly five bits of `cmd->flags` - `MMC_RSP_PRESENT` (0), `MMC_RSP_136`
 * (1), `MMC_RSP_BUSY` (3), `MMC_RSP_CRC` (2) and `MMC_RSP_OPCODE` (4) - and no others, so bits 7 and
 * 8 fall off the mapping. **CMD13's command word is therefore `0x0D1A`: opcode 13 in bits 15:8 with
 * CMD7's own flag byte `0x1A` in bits 7:0.** `ST_MMC_RSP_R1_SPI` below is asserted to differ from
 * `ST_MMC_RSP_R1` and to map to the SAME byte, so the two readings cannot be quietly conflated: a
 * reader who saw `_sta_flags_driver = 0x195` next to a command word of `0x0D1A` and concluded the
 * mapping was broken would be reading two quantities with one name (`[[mi4-one-value-two-definitions]]`)
 * - and a future rung that asserted "the flags are 21" for this command while passing the driver's
 * word would fail the build rather than send the wrong frame.
 *
 * **WHY THIS COMMAND AND NOT CMD8.** In the driver's own init order the statement after
 * `mmc.c:1436`'s CMD7 is `mmc.c:1446`'s `mmc_get_ext_csd`, which is CMD8 `SEND_EXT_CSD` - a
 * **512-byte DATA read** that this image has no mechanism for: no `sdhci_prepare_data`, no block size,
 * no DMA or PIO path, and `TIMEOUT_CONTROL 0x2E` never raised (`_sta_tout_ctl` publishes that it is
 * still `0x00`). **So this rung is NOT the driver's own next statement and does not pretend to be.**
 * It is chosen as a MEASUREMENT, and the measurement it makes is the one 821 left open: `R1_CURRENT_STATE`
 * read by a command that changes nothing. The driver uses CMD13 for exactly this - `mmc.c:1724-1727`'s
 * `mmc_alive`, whose whole body is `return mmc_send_status(host->card, NULL);`, and `mmc_ops.c:423`'s
 * busy poll - so the cell this rung publishes is the driver's own "is the card alive" verb, run one
 * command after CMD7 and reported with the state field attached.
 *
 * **WHAT IT DECIDES.** The card walked IDENT (2) -> STBY (3) across CMD3 and CMD7 in the same boot
 * (821 section 5). CMD7 must select the card out of STBY into TRAN (4). **This command's own R1 is
 * therefore the discriminator 821 said one reading of one field could not be**: `_sta_state = 4` says
 * CMD7 landed and the card is in the transfer state; `_sta_state = 3` says it is still in STBY and
 * CMD7 did not take effect - and both are read by a command whose own presence cannot change the
 * state, which is what makes the reading attributable in a way CMD7's was not. `_sta_illegal = 1`
 * (bit 22) would say the card refused CMD13 itself, which is a state the standard does not have for a
 * selected card and would put the wall somewhere else entirely.
 *
 * **THE PRECONDITION IS NOT DECODED, AND THAT IS DELIBERATE.** The word read before this command is
 * CMD7's R1 - a genuine `R1_CURRENT_STATE` this time, unlike CMD7's own precondition - and it is
 * published RAW (`_sta_pre_state` decodes it as the vendor's macro does). **It is published and it is
 * NOT a gate**, for the reason rung 36's caller gives at length: a gate turns "the card is not where
 * this command wants it" into "the command failed", and this rung exists precisely to read the state
 * rather than to act on it. A card that answered CMD7 with STBY and CMD13 with STBY is the reading
 * this rung is for, and gating would suppress it.
 *
 * **THE DEVICE SURFACE IS rung 36's, and the build clause asserts it address for address.** Four
 * `RESPONSE` words descending on both sides, the `INT_ENABLE 0x34` window read and written, `INT_STATUS`,
 * `SIGNAL_ENABLE`, the `TIMEOUT_CONTROL` byte and `PRESENT_STATE` - no new register, no store to any
 * register the arms below do not already write, and no byte below any word because this response is 48
 * bits. `tools/check_response_word_order.py` reads this file and refuses an ascending order, a byte
 * below a word, or a right shift of a raw word, so this copy cannot silently disagree with the seven
 * above it.
 *
 * **AND THE FOUR OUTCOMES, EACH NAMING THE NEXT ACT** - the closing comment at the end of this body
 * writes them out.
 */
static __attribute__((noinline, noclone)) void st_send_status(uint32_t int_enable)
{
    struct st_cmd_result c13;
    uint32_t pre, post, held, readback, status_post, raw, ena;

    ST_LIVE("xnu_live_storage_sta_calls", 1u);

    /* --- the freshness baseline, BEFORE the window opens: one word read four times --------------- */
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    ST_LIVE("xnu_live_storage_sta_raw_pre0", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 8u);
    ST_LIVE("xnu_live_storage_sta_raw_pre1", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
    ST_LIVE("xnu_live_storage_sta_raw_pre2", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("xnu_live_storage_sta_raw_pre3", raw);
    pre = raw;
    ST_LIVE("xnu_live_storage_sta_resp_pre", pre);
    /*
     * **AND THE ONE CELL THIS LADDER HAS NEVER HAD THE OPPORTUNITY TO PUBLISH.** For every command
     * above this one the pre-read was either a 136-bit frame's last word, a CSD word or the previous
     * command's own R1 read too early to mean anything. CMD7 was the first command whose RESPONSE+0
     * held an R1, and THIS command's pre-read is therefore the **first precondition in this ladder
     * that is genuinely the format it is being decoded as**: `_sta_pre_state` is `R1_CURRENT_STATE`
     * of CMD7's response, sampled a few microseconds after CMD7 completed. It is decoded and
     * published rather than gated (see the header comment); comparing it with `_sta_state` is then
     * two readings of the same field by two commands, and the pair is the rung's whole answer.
     */
    ST_LIVE("xnu_live_storage_sta_pre_state", (pre & 0x00001E00u) >> 9);
    ST_LIVE("xnu_live_storage_sta_pre_ready", (pre >> 8) & 0x1u);
    ST_LIVE("xnu_live_storage_sta_pre_illegal", (pre >> 22) & 0x1u);

    /* --- the window opens here, immediately before the command is put on the bus ---------------- */
    ena = int_enable | (uint32_t)ST_SDHCI_INT_ENABLE_CMD;
    ST_LIVE("xnu_live_storage_sta_ena_wrote", ena);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, ena);
    ST_LIVE("xnu_live_storage_sta_status_pre",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS));
    held = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_sta_ena_held", held);
    ST_LIVE("xnu_live_storage_sta_sig_enable",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE));

    ST_LIVE("xnu_live_storage_sta_op", ST_CMD_OP_SEND_STATUS);
    /*
     * **THREE FLAG CELLS, AND THE THIRD IS THE MAPPING'S OWN ANSWER.** `_sta_flags` is the ladder's
     * `ST_MMC_RSP_R1` (what every arm from rung 21 up calls "the driver's flags"); `_sta_flags_driver`
     * is what `mmc_ops.c:479` actually sets for THIS command, SPI bits and all; `_sta_flags_mapped` is
     * what `sdhci_cmd_to_flags` makes of that. Publishing all three makes the claim in the header
     * comment a reading: the driver's 0x195 and the ladder's 0x15 are two different numbers that
     * produce ONE command word.
     */
    ST_LIVE("xnu_live_storage_sta_flags", ST_MMC_RSP_R1);
    ST_LIVE("xnu_live_storage_sta_flags_driver", ST_MMC_RSP_R1_SPI);
    ST_LIVE("xnu_live_storage_sta_flags_mapped", (uint32_t)ST_SDHCI_CMD_FLAGS(ST_MMC_RSP_R1_SPI));
    ST_LIVE("xnu_live_storage_sta_arg", ST_MMC_RCA_1);
    st_send_command(ST_CMD_OP_SEND_STATUS, ST_MMC_RCA_1, ST_MMC_RSP_R1_SPI, &c13);

    ST_LIVE("xnu_live_storage_sta_sent", c13.sent);
    ST_LIVE("xnu_live_storage_sta_word", c13.word_wrote);
    ST_LIVE("xnu_live_storage_sta_word_read", c13.word_read);
    ST_LIVE("xnu_live_storage_sta_arg_wrote", c13.arg_wrote);
    ST_LIVE("xnu_live_storage_sta_complete", c13.complete);
    ST_LIVE("xnu_live_storage_sta_err", c13.err);
    ST_LIVE("xnu_live_storage_sta_timeout", c13.timed_out);
    ST_LIVE("xnu_live_storage_sta_status_any", c13.status_any);
    ST_LIVE("xnu_live_storage_sta_any_polls", c13.status_any_polls);
    ST_LIVE("xnu_live_storage_sta_inhibit_after", c13.inhibit_after);
    ST_LIVE("xnu_live_storage_sta_inhibit_seen", c13.inhibit_seen);
    ST_LIVE("xnu_live_storage_sta_inhibit_last", c13.inhibit_last);
    ST_LIVE("xnu_live_storage_sta_inhibit_before", c13.inhibit_before);
    ST_LIVE("xnu_live_storage_sta_inhibit_polls", c13.inhibit_polls);
    ST_LIVE("xnu_live_storage_sta_inhibit_ticks", c13.inhibit_ticks);
    ST_LIVE("xnu_live_storage_sta_inhibit_timeout", c13.inhibit_timeout);
    ST_LIVE("xnu_live_storage_sta_cmdlow_seen", c13.cmdlow_seen);
    ST_LIVE("xnu_live_storage_sta_stale", c13.stale);
    ST_LIVE("xnu_live_storage_sta_clear_wrote", c13.clear_wrote);
    ST_LIVE("xnu_live_storage_sta_polls", c13.polls);
    ST_LIVE("xnu_live_storage_sta_ticks", c13.ticks);
    ST_LIVE("xnu_live_storage_sta_clear_after", c13.clear_after);

    /* --- the response, and the four R1 fields the vendor's own macros name ---------------------- */
    /*
     * `mmc.h:141` `R1_CURRENT_STATE(x)` is `(x & 0x00001E00) >> 9`, `mmc.h:142` `R1_READY_FOR_DATA`
     * is bit 8, `mmc.h:143` `R1_SWITCH_ERROR` is bit 7, `mmc.h:144` `R1_EXCEPTION_EVENT` is bit 6 -
     * and `ILLEGAL_COMMAND` is bit 22, which the vendor's header does not name as a macro and
     * `mmc_send_op_cond`'s own caller reads anyway. **`R1_CURRENT_STATE` is read as `mmc.h:141`'s own
     * expression**, so a reader checks the arithmetic instead of trusting a helper.
     */
    ST_LIVE("xnu_live_storage_sta_resp", c13.resp);
    ST_LIVE("xnu_live_storage_sta_resp_read", c13.resp_read);
    ST_LIVE("xnu_live_storage_sta_rsp_present", c13.rsp_present);
    ST_LIVE("xnu_live_storage_sta_state", (c13.resp & 0x00001E00u) >> 9);
    ST_LIVE("xnu_live_storage_sta_ready", (c13.resp >> 8) & 0x1u);
    ST_LIVE("xnu_live_storage_sta_switch_err", (c13.resp >> 7) & 0x1u);
    ST_LIVE("xnu_live_storage_sta_exception", (c13.resp >> 6) & 0x1u);
    ST_LIVE("xnu_live_storage_sta_illegal", (c13.resp >> 22) & 0x1u);

    /* --- the freshness pair's second half: the SAME reading as `pre` and as `c13.resp` ----------- */
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 12u);
    ST_LIVE("xnu_live_storage_sta_raw_post0", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 8u);
    ST_LIVE("xnu_live_storage_sta_raw_post1", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE + 4u);
    ST_LIVE("xnu_live_storage_sta_raw_post2", raw);
    raw = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("xnu_live_storage_sta_raw_post3", raw);
    post = raw;
    ST_LIVE("xnu_live_storage_sta_resp_post", post);
    ST_LIVE("xnu_live_storage_sta_resp_moved", (post != pre) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_sta_resp_is_arg", (post == ST_MMC_RCA_1) ? 1u : 0u);
    /*
     * **AND THE ONE COMPARISON THAT MAKES THIS RUNG'S ANSWER ATTRIBUTABLE RATHER THAN PLAUSIBLE.**
     * CMD13 reads the card's status WITHOUT changing it, so a press in which `_sta_state` equals
     * `_sta_pre_state` is one where the card reported the same state twice, a few microseconds apart,
     * across a command that cannot move it - and a press in which they differ is one where something
     * moved the card between CMD7 and CMD13. `1` is the expected reading and is NOT a defect: it is
     * what makes `_sta_state` a state rather than a snapshot of a transition. This cell is published
     * for the same reason `_sel_resp_moved` was - a comparison of two readings, not of a value with
     * itself - and it is the reason `_sta_illegal` alone would have been too weak a refusal row.
     */
    ST_LIVE("xnu_live_storage_sta_state_held",
            (((pre & 0x00001E00u) >> 9) == ((c13.resp & 0x00001E00u) >> 9)) ? 1u : 0u);

    /* --- the window's ONE exit, unconditional, on the line after the publishes ------------------ */
    ST_LIVE("xnu_live_storage_sta_wrote_back", int_enable);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable);
    status_post = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    ST_LIVE("xnu_live_storage_sta_status_post", status_post);
    ST_LIVE("xnu_live_storage_sta_tout_ctl",
            (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL));
    readback = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_sta_readback", readback);
    ST_LIVE("xnu_live_storage_sta_ps_after",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
    ST_LIVE("xnu_live_storage_sta_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 36 - one body, one caller, no data phase and FOUR outcomes each
        * of which names the next act:
        *   `_sta_complete = 1` with `_sta_err = 0` and `_sta_state = 4` (TRAN) is CMD7 LANDED: the
        *     card is in the transfer state, the block's own opcode check passed on CMD13 too, and the
        *     frontier is the DATA PATH - CMD8's 512-byte read - and nothing else.
        *   `_sta_complete = 1` with `_sta_err = 0` and `_sta_state = 3` (STBY) is CMD7 DID NOT LAND:
        *     the card answered the command addressed to it and did not move, which makes CMD7's own
        *     `3` the state it was in and not the state it produced. The next arm is CMD7 again with
        *     SOMETHING ELSE moved - and the candidate that is measured rather than guessed is the
        *     block's `TIMEOUT_CONTROL 0x2E`, still `0x00` on every press this ladder has made.
        *   `_sta_complete = 1` with `_sta_illegal = 1` (bit 22) is THE CARD REFUSING CMD13 ITSELF:
        *     a state the standard does not have for an addressed card, which puts the wall below
        *     CMD13 entirely and says the RCA this ladder sends is not the address the card answers to.
        *   `_sta_complete = 0` with `CMD_INHIBIT` asserted is a hang, which on a command with no data
        *     phase and the same five-bit window as CMD7 would be a property of the block and not of
        *     the word - and the successor reads `_sta_inhibit_last` before anything else. */

#if STAGE90_XNU_STORAGE_PROBE >= 37
/*
 * ================================================================================================
 * RUNG 38 - `st_send_ext_csd`: THE LADDER'S FIRST DATA PHASE, AND ITS ONLY PATH TO THE MEDIUM'S
 * OWN CONTENT.
 * ================================================================================================
 *
 * **WHAT IT IS.** `mmc.c:1447`'s `err = mmc_get_ext_csd(card, &ext_csd)` is the driver's own
 * statement immediately after the `mmc_select_card` rung 36 sent, and `mmc_get_ext_csd`
 * (`mmc.c:201-208`) is `mmc_send_ext_csd` -> `mmc_send_cxd_data(card, host, MMC_SEND_EXT_CSD,
 * ext_csd, 512)`. A 512-byte read out of the card's EXT_CSD area - the register the card uses to
 * describe itself in eMMC v4 and later, including the sector count this ladder has never had.
 *
 * **THE FIVE REGISTERS AND THE ORDER THEY ARE WRITTEN IN ARE `sdhci_send_command`'s OWN.** The
 * vendor's function is one body, and this rung transcribes its data half in program order:
 *
 *     sdhci.c:1084-1107   wait for `CMD_INHIBIT | DATA_INHIBIT` to clear (10 ms, `mdelay(1)`)
 *     sdhci.c:1117        sdhci_prepare_data
 *     sdhci.c:828-829         TIMEOUT_CONTROL 0x2E <- sdhci_calc_timeout(...)   [BYTE, first]
 *     sdhci.c:831             if (!data) return;                                [not taken]
 *     sdhci.c:974-975         BLOCK_SIZE 0x04 <- SDHCI_MAKE_BLKSZ(7, 512) = 0x7200
 *     sdhci.c:976             BLOCK_COUNT 0x06 <- 1
 *     sdhci.c:806             INT_ENABLE <- (INT_ENABLE & ~DMA) | DATA_AVAIL | SPACE_AVAIL
 *     sdhci.c:1119        ARGUMENT 0x08 <- cmd->arg                        [inside st_send_command]
 *     sdhci.c:1121        sdhci_set_transfer_mode -> TRANSFER_MODE 0x0C <- 0x0012
 *     sdhci.c:1153        COMMAND 0x0E <- 0x083A                           [inside st_send_command]
 *     sdhci.c:2754        PIO: PRESENT_STATE & DATA_AVAILABLE -> read BLOCK
 *
 * **THREE THINGS IN THAT LIST ARE NOT THE VENDOR'S AND EACH IS A DECISION, NAMED SO A READER CAN
 * OVERRULE IT.**
 *
 *    TAKEN WITHOUT SEARCHING FOR SDHCI_RESPONSE AFTER THE CMD INDEX*"* - the card's own status word,
 *    re-read at the moment the decision is made rather than remembered from rung 37's decode. And it
 *    is checked by name: reading `SDHCI_RESPONSE` *directly* is the arm; reading
 *    `SDHCI_RESPONSE | SDHCI_RESPONSE_CMD` - which sdhci.c:2344 also has - returns the same value with
 *    a different 16-bit field in mind and would be the m812 class in the one place this rung cannot
 *    afford it, because the gate would then be a reading of a quantity nobody has named.
 *
 * 0. **IT IS GATED, AND IT IS THE LADDER'S FIRST GATED COMMAND IN FOUR RUNGS.** CMD7 went unconditionally
 *    because this file had no cell that measured its precondition (820); CMD13 went unconditionally
 *    because its precondition WAS the measurement (822). **CMD8's precondition is neither of those**:
 *    it is a value this ladder MEASURES in the same boot three commands earlier - `R1_CURRENT_STATE`
 *    out of CMD13's own R1, `mmc.h:141`'s `(x & 0x00001E00) >> 9` - and it is a value the standard
 *    does not require a card to interpret a data command by. **The gate re-reads `RESPONSE + 0` at
 *    the top of the body rather than trusting the ladder's own earlier decode of it**, publishes the
 *    word and the field on both paths, and refuses when the state is not `4` (`R1_STATE_TRAN`). One
 *    outcome row, and the repair it names is rung 37 again rather than this rung.
 * 1. **`HOST_CONTROL 0x28`'s DMA field is NOT written.** `sdhci.c:951-961` clears `SDHCI_CTRL_DMA_MASK`
 *    and selects SDMA for every data command in the `host->version >= SDHCI_SPEC_200` branch - the
 *    branch this part takes - whether or not the request uses DMA. **This rung does not**, because the
 *    register is measured `0x00` on every press of this ladder: the DMA field reads ZERO, and
 *    `SDHCI_CTRL_SDMA` (`sdhci.h:81`) **is also 0x00**. The vendor's store would therefore write back
 *    the value the register already holds, and the one thing it could do is CLEAR a bit this image
 *    never set - the measured value already is the vendor's target value. **The reading is published
 *    as `_ext_host_control_pre` so the claim is a cell and not a sentence**; a press whose value is
 *    not `0x00` falsifies this paragraph and the repair is the vendor's line, not a new register.
 *    (`HOST_CONTROL` is a BYTE register at an offset no 32-bit access reaches - the width rule that
 *    governs this whole file.)
 * 2. **`SIGNAL_ENABLE 0x38` is NOT written, and this is the ONE PLACE THIS RUNG DEPARTS FROM THE
 *    VENDOR ON PURPOSE.** `sdhci_set_transfer_irqs` (`sdhci.c:806`) calls `sdhci_clear_set_irqs`,
 *    which (`sdhci.c:803-804`) writes `INT_ENABLE` **and `SIGNAL_ENABLE`** with the same word. On the
 *    real driver that is correct and it is also unreachable in this image's terms: the conjunction of
 *    those two registers is what raises the block's SPI - **intid 155, a line this image hands to
 *    nobody**, whose delivery arrives at the dispatcher as `_irq_other_count` and ENDS THE RUN. The
 *    whole ladder has kept `SIGNAL_ENABLE` at zero for thirty-seven rungs and every clause that reads
 *    a body's device surface asserts it is READ and never written. **The PIO mechanism does not need
 *    it**: the polls here read `INT_STATUS` and `PRESENT_STATE` directly, which is what rungs 11
 *    through 37 already do for every command - so the enable's role is to UNMASK A LATCH, not to
 *    deliver an exception, and the ladder's own poll is the substitute for the handler the mask would
 *    have called. `_ext_sig_enable` publishes the register so the departure is a reading.
 * 3. **The PIO loop is bounded and does not depend on the interrupt.** `sdhci_transfer_pio`
 *    (`sdhci.c:447-479`) loops `while (PRESENT_STATE & DATA_AVAILABLE)` and calls
 *    `sdhci_read_block_pio` once per BLOCK, and that function reads the block as 32-bit words:
 *    `scratch = sdhci_readl(host, SDHCI_BUFFER)` with `chunk = 4` and four byte extracts
 *    (`sdhci.c:527-537`). **The word count is therefore `512 / 4 = 128` and the width is 32 bits** -
 *    a 16-bit or byte read of `0x20` would take one FIFO beat per access and need 256 or 512 of them,
 *    a different number against the same buffer. **This body reads exactly
 *    `ST_EXT_CSD_LEN / 4` words and no more**, publishing the DATA_AVAILABLE/DOING_READ readings at
 *    every iteration and the count of iterations in which the state said a word was ready - so a
 *    transfer that produced four words instead of 128 is a reading out of `_ext_words_gated`, not a
 *    hang. The vendor's own PIO read is also where the WIDTH is settled for a reader who wants to
 *    check it: `sdhci_transfer_pio` passes NO width to `sdhci_read_block_pio` and that function uses
 *    `sdhci_readl`. This rung takes three readings per word (the state's two bits and the word
 *    itself) - **a count of 128 is a floor on how much the loop may run, never a command it issues**.
 */
static __attribute__((noinline, noclone)) void st_send_ext_csd(uint32_t int_enable)
{
    struct st_cmd_result c8;
    uint32_t was, count, count_max, step0, target_us, clks, tacc, m, e, i;
    uint32_t hc_pre, pre, ps, word, gated, saw_do, saw_da, first_ps, last_ps;
#if STAGE90_XNU_STORAGE_PROBE >= 39
    uint32_t gated2, data_timeout, waited, steps2, t0_data;
#endif
    uint32_t host_use_reserved_max;

    ST_LIVE("xnu_live_storage_ext_calls", 1u);

    /* --- THE PRECONDITION, READ FIRST: the card's own R1, still in the register ------------- */
    /*
     * **A PRECONDITION IS READ BEFORE ANYTHING IS TOUCHED, AND THIS ONE IS READ FROM THE REGISTER
     * ITSELF RATHER THAN FROM THE LADDER'S DECODE OF IT.** `RESPONSE + 0` holds CMD13's R1 - the
     * register's own copy, put there by the block when CMD13 completed, and read by nothing since
     * except reads - and `R1_CURRENT_STATE` out of it is `mmc.h:141`'s `(x & 0x00001E00) >> 9`, the
     * same expression rung 37 decoded three commands ago. **So this is not the ladder remembering an
     * answer; it is the card's own status word, re-read at the moment the decision is made** - the
     * shape 804 insisted on for CMD2's gate, one rung up from CMD13's own `_sta_pre_state`.
     *
     * **AND IT IS PUBLISHED AS A READING, ON BOTH PATHS.** `_ext_gate_resp` is the raw word, so the
     * decode below is checkable rather than trusted, and `_ext_gate_state` is the field. A reader
     * comparing it with `_sta_state` in the same log has the whole gate: the two cells are one
     * register read twice, on either side of one unconditional command, and they can only disagree
     * if something between them moved the card - which nothing can, because CMD13 cannot move a card
     * and the ladder issues nothing between them.
     *
     * **AND THE GATE IS NOT ON `st_csd_words_valid`, DELIBERATELY, THOUGH THE CARRY IS READ BELOW.**
     * The carry's own comment says what it is: *"`valid` is published and is NOT a gate"*. Making it
     * one here would be the second definition of a value this rung already has one of - and the
     * timeout computation has a defined answer without a CSD (a zero target, and a count of 0, which
     * is the register's own reset value). **The one thing this rung refuses to do is send a DATA
     * command into a card that is not in the transfer state**; everything else it does with what it
     * has and says what it had.
     */
    pre = st_read32(ST_HC_MEM_BASE + ST_SDHCI_RESPONSE);
    ST_LIVE("xnu_live_storage_ext_gate_resp", pre);
    ST_LIVE("xnu_live_storage_ext_gate_state", (pre & 0x00001E00u) >> 9);
    if (((pre & 0x00001E00u) >> 9) != 4u) {
        /*
         * **THE REFUSAL IS A ROW, AND IT IS THE ONLY ROW IN THIS LADDER THAT ANSWERS A QUESTION THE
         * RUNG BELOW IT ALREADY ANSWERED** - which is exactly why it is worth a cell. A press in which
         * `_sta_state` said 4 and this says anything else is a press in which the card moved between
         * two reads of one register across a command that cannot move it, and the repair is not this
         * rung: it is rung 37 again. `_ext_done = 0` beside `_ext_calls = 1` says the body ran and the
         * phase was refused, which is a state no absent key can express.
         */
        ST_LIVE("xnu_live_storage_ext_gated", 1u);
        ST_LIVE("xnu_live_storage_ext_done", 0u);
        return;
    }
    ST_LIVE("xnu_live_storage_ext_gated", 0u);

    /* --- HOST_CONTROL 0x28: the register the vendor's own prepare_data would have written -------- */
    /*
     * `sdhci.c:951-961`'s `HOST_CONTROL` read-modify-write is the one line of `sdhci_prepare_data`
     * this body does not transcribe, and this read is why. See the header: the DMA field is measured
     * zero and `SDHCI_CTRL_SDMA` is zero, so the vendor's store is a no-op on this part - and the
     * value is published so that is a reading rather than a claim.
     */
    hc_pre = (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_HOST_CONTROL);
    ST_LIVE("xnu_live_storage_ext_host_control_pre", hc_pre);

    /* --- the window opens: rung 14's enable PLUS the PIO pair, and nothing taken away --------------- */
    /*
     * `sdhci_set_transfer_irqs`'s PIO arm is `sdhci_clear_set_irqs(host, dma_irqs, pio_irqs)` - the
     * four DMA bits DOWN and `DATA_AVAIL | SPACE_AVAIL` UP. **This ladder's window has never carried
     * a DMA bit**, so for this image the clear is the identity and the set is the whole change. It is
     * published as a reading (`_ext_ena_wrote`) beside what the register actually took
     * (`_ext_ena_held`), which is the same pair every rung above 13 uses.
     */
    ST_LIVE("xnu_live_storage_ext_ena_base", int_enable);
    ST_LIVE("xnu_live_storage_ext_ena_add", (uint32_t)ST_SDHCI_INT_PIO_IRQS);
    word = int_enable | (uint32_t)ST_SDHCI_INT_PIO_IRQS;
#if STAGE90_XNU_STORAGE_PROBE >= 38
    /*
     * **RUNG 39: THE WINDOW MUST KEEP THE COMMAND-COMPLETION ENABLE, AND THIS IS THE RUNG-38
     * PRESS'S OWN REPAIR.** The press measured `_ext_complete = 0` BESIDE data that plainly
     * arrived (`_ext_sec_count = 0x01d5a000`), and the cause was not the card: `_ext_ena_base = 0`
     * (every rung below 37 restores `INT_ENABLE` to 0 at its exit) and `_ext_ena_wrote = 0x30` - the
     * window enabled only `ST_SDHCI_INT_PIO_IRQS` and NEVER `SDHCI_INT_RESPONSE` (bit 0). **An
     * `INT_STATUS` bit latches only for the enables set in `INT_ENABLE`**, so the enabled data bit
     * latched (`_ext_status_after = 0x20`) while the disabled RESPONSE bit never did, and
     * `st_send_command`'s poll - watching `ST_SDHCI_INT_CMD_MASK`, of which bit 0 is the only one a
     * `SEND_EXT_CSD` can produce - ran its full 1.2 s. **`_ext_complete = 0` was a switched-off
     * witness, not a failed command.**
     *
     * So the window ORs `ST_SDHCI_INT_ENABLE_CMD` (`0x000F0001`, bit 0 = RESPONSE with the four
     * command-error bits) - exactly what every command rung from 23 enables, and what the vendor
     * leaves set for the life of the host (`sdhci_init`, `sdhci.c:291-296`).
     *
     * **AND IT CLEARS THE TWO DMA ENABLES, WHICH IS `sdhci_set_transfer_irqs`'s PIO ARM, TAKEN
     * LITERALLY** (`sdhci.c:806-815`): `sdhci_clear_set_irqs(host, SDHCI_INT_DMA_END |
     * SDHCI_INT_ADMA_ERROR, DATA_AVAIL | SPACE_AVAIL)`. On THIS image the clear is the identity in
     * VALUE (no DMA bit is ever set) but it is kept as the vendor's own statement, and its real
     * content is that the completion enable is ADDED and never REMOVED - the instruction the
     * `INT_ENABLE_CMD` name stands for and 824 dropped.
     */
    word |= (uint32_t)ST_SDHCI_INT_ENABLE_CMD;
    word &= ~((uint32_t)ST_SDHCI_INT_DMA_END | (uint32_t)ST_SDHCI_INT_ADMA_ERROR);
    ST_LIVE("xnu_live_storage_ext_command_enable",
            (uint32_t)(word & (uint32_t)ST_SDHCI_INT_ENABLE_CMD));
    ST_LIVE("xnu_live_storage_ext_dma_cleared",
            (uint32_t)(word & ((uint32_t)ST_SDHCI_INT_DMA_END |
                               (uint32_t)ST_SDHCI_INT_ADMA_ERROR)));
#endif
    ST_LIVE("xnu_live_storage_ext_ena_wrote", word);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, word);
    ST_LIVE("xnu_live_storage_ext_ena_held",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE));
    ST_LIVE("xnu_live_storage_ext_sig_enable",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE));

    /* --- TIMEOUT_CONTROL 0x2E: sdhci_calc_timeout, transcribed, and the COUNT is computed ---------- */
    /*
     * **The vendor's own order is why this is before `BLOCK_SIZE` and not after it**: `sdhci.c:827-828`
     * writes it inside `if (data || (cmd->flags & MMC_RSP_BUSY))`, ABOVE `sdhci.c:831`'s
     * `if (!data) return;`, so on a data command it is written first of all.
     *
     * **The target is `mmc_set_data_timeout(core.c:1252)`'s, and it comes from THIS CARD.** The ladder
     * carried the four assembled CSD words across CMD7 and CMD13 for exactly this computation
     * (`st_csd_words`); `tacc_ns` is `(tacc_exp[e] * tacc_mant[m] + 9) / 10` (`mmc.c:168`) with `m`
     * from CSD bits 115:112 and `e` from 114:112's own field at 112:3, `tacc_clks` is bits 104:96
     * times 100 (`mmc.c:169`), and `mult` is 10 because this card is not SD (`core.c:1268`).
     *
     * **And the divisor is the BASE-CLOCK arm.** `sdhci_calc_timeout` (`sdhci.c:779-788`) takes
     * `curr_clk = host->clock / 1000` and then `/ 4` when `SDHCI_QUIRK2_ALWAYS_USE_BASE_CLOCK` is set
     * - which `sdhci-msm.c:2899` sets unconditionally and `:2906` pairs with `DIVIDE_TOUT_BY_4` - so
     * the `timeout_clk` field in `CAPABILITIES` **is never read on this device**. The step-0 bound is
     * `(1 << 13) * 1000 / curr_clk` microseconds and the count is the number of doublings that reach
     * the target. **A reader who used the CAPABILITIES field would compute a different count and the
     * constant would be wrong in a direction no cell can see**, which is why the count is computed
     * here from the clock the ladder actually set - `ST_SET_INIT_CLOCK` = 400,000 Hz, rung 6 - and
     * published as a number.
     */
    tacc = 0u;
    clks = 0u;
    host_use_reserved_max = 1u;               /* sdhci-msm.c:2904, a property of THIS HOST and of no
                                               * CSD - so it is TRUE on the press where no CSD arrived
                                               * and `count_max` is 0xF there too */
    ST_LIVE("xnu_live_storage_ext_csd_carried", st_csd_words_valid);
    if (st_csd_words_valid != 0u) {
        m = (st_csd_words[0] >> 16) & 0xFu;   /* mmc.c:166's UNSTUFF_BITS(resp, 115, 4) */
        e = (st_csd_words[0] >> 8) & 0x7u;    /* mmc.c:167's UNSTUFF_BITS(resp, 112, 3) */
        tacc = (st_tacc_exp[e] * st_tacc_mant[m] + 9u) / 10u;
        clks = ((st_csd_words[0] >> 8) & 0xFFu) * 100u;   /* mmc.c:169's UNSTUFF_BITS(resp, 104, 8) */
    }
    ST_LIVE("xnu_live_storage_ext_csd_tacc_ns", tacc);
    ST_LIVE("xnu_live_storage_ext_csd_tacc_clks", clks);
    ST_LIVE("xnu_live_storage_ext_timeout_ns", tacc * (uint32_t)ST_EXT_MMC_MULT);
    /*
     * **THE CLOCK TERM IS ADDED IN CYCLES AND THE SUM IS CEILED, and both halves are the vendor's.**
     * `sdhci.c:769-772` is `target_timeout = data->timeout_ns / 1000;` and then, with a clock,
     * `target_timeout += data->timeout_clks / host->clock;` - **a division of CYCLES by HERTZ, which is
     * SECONDS, added to a value in MICROseconds**, so what the vendor's own expression computes for a
     * nonzero `timeout_clks` is a DIMENSION ERROR rather than a duration, and it is truncated to zero
     * for any cycle count below the clock. `data->timeout_clks` is `card->csd.tacc_clks * mult`
     * (`core.c:1278`), **so a card with a nonzero NSAC takes that arm on the real driver too.**
     * `sdhci.c:1282-1283` - `mmc_set_data_timeout`'s own next reader - says what it was meant to be:
     * *"Divide to get the number of cycles, then compute in microseconds"*, `timeout_clks / (clock /
     * 1000000)`. **THIS BODY TAKES THE INTENDED ARITHMETIC AND SAYS SO**: the clock term is
     * `clks * 1000000 / host->clock` microseconds, one integer division, no truncation to zero. Where
     * `clks` is 0 - which this card's measured CSD gives - the two agree exactly; where it is not, this
     * is the driver's documented intent rather than its transcription, and `_ext_csd_tacc_clks` beside
     * `_ext_tout_count` is what makes the difference a reading.
     *
     * **AND THE SUM IS ROUNDED UP**, `(a + b + 999) / 1000` rather than `(a + b) / 1000`, because
     * `sdhci_calc_timeout` decides whether the step-0 bound is already enough - and a target truncated
     * DOWN can only make a count SMALLER than the card's own answer requires. 130 ns divided by 1000 is
     * 0 either way; the ceiling is for the card whose TAAC does not divide.
     */
    target_us = (tacc * (uint32_t)ST_EXT_MMC_MULT + clks + 999u) / 1000u;
    step0 = (uint32_t)ST_SDHCI_TOUT_STEP0_NUMER /
            ((uint32_t)ST_SET_INIT_CLOCK / (uint32_t)ST_SDHCI_TOUT_BASE_DIVISOR);
    ST_LIVE("xnu_live_storage_ext_timeout_us", target_us);
    ST_LIVE("xnu_live_storage_ext_timeout_step0", step0);
    count = 0u;
    while (step0 < target_us) {
        count++;
        step0 <<= 1;
        if (count >= 0xFu)
            break;
    }
    /*
     * **AND THE CAP IS SKIPPED ON THIS HOST, WHICH IS THE THIRD PLACE THIS RUNG READS A QUIRK RATHER
     * THAN ASSUMING IT.** `sdhci.c:795-800` refuses `count >= 0xF` and writes `0xE` - **inside
     * `if (!(host->quirks2 & SDHCI_QUIRK2_USE_RESERVED_MAX_TIMEOUT))`, and `sdhci-msm.c:2904` SETS that
     * quirk unconditionally.** So on this device the vendor's own value for a very large target is
     * `0xF`, the reserved encoding the specification's `0xE` sentence does not describe, and a body
     * that copied the cap would be writing the value the vendor declines to write. **The loop's own
     * `count >= 0xF` break above is kept**: `sdhci.c:786-791`'s `if (count >= 0xF) break;` is inside
     * the loop on every arm. `_ext_tout_max` publishes which of the two values is reachable here.
     * **On THIS card neither branch fires** - the target is 1 us and step 0 is 81,920 us - which is
     * exactly why the branch has to be a cell: a press cannot distinguish a count of 0 computed from a
     * target of 1 us from one computed from a target of 0.
     */
    count_max = 0xEu;
    if (host_use_reserved_max != 0u)
        count_max = 0xFu;
    ST_LIVE("xnu_live_storage_ext_tout_max", count_max);
    if (count >= 0xFu)
        count = count_max;
    ST_LIVE("xnu_live_storage_ext_tout_count", count);

    was = (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL);
    ST_LIVE("xnu_live_storage_ext_tout_was", was);
    st_write8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL, (uint8_t)count);
    ST_LIVE("xnu_live_storage_ext_tout_held",
            (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL));

    /* --- BLOCK_SIZE 0x7200, then BLOCK_COUNT 1: sdhci.c:974-976, in that order ---------------------- */
    ST_LIVE("xnu_live_storage_ext_blksz", (uint32_t)ST_SDHCI_BLOCK_SIZE_512);
    st_write16(ST_HC_MEM_BASE + ST_SDHCI_BLOCK_SIZE, (uint16_t)ST_SDHCI_BLOCK_SIZE_512);
    ST_LIVE("xnu_live_storage_ext_blksz_held",
            (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_BLOCK_SIZE));
    ST_LIVE("xnu_live_storage_ext_blkcnt", 1u);
    st_write16(ST_HC_MEM_BASE + ST_SDHCI_BLOCK_COUNT, 1u);
    ST_LIVE("xnu_live_storage_ext_blkcnt_held",
            (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_BLOCK_COUNT));

    /* --- TRANSFER_MODE, then the command: sdhci.c:1121 then :1153 -------------------------------- */
    ST_LIVE("xnu_live_storage_ext_trns", (uint32_t)ST_SDHCI_TRNS_READ_1BLK);
    st_write16(ST_HC_MEM_BASE + ST_SDHCI_TRANSFER_MODE, (uint16_t)ST_SDHCI_TRNS_READ_1BLK);
    ST_LIVE("xnu_live_storage_ext_trns_held",
            (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_TRANSFER_MODE));

    ST_LIVE("xnu_live_storage_ext_op", ST_CMD_OP_SEND_EXT_CSD);
    ST_LIVE("xnu_live_storage_ext_arg", 0u);
    ST_LIVE("xnu_live_storage_ext_flags", ST_MMC_RSP_R1_ADTC);
    ST_LIVE("xnu_live_storage_ext_flags_mapped",
            (uint32_t)ST_SDHCI_CMD_FLAGS(ST_MMC_RSP_R1_ADTC));
    /*
     * **THE WORD, AND IT IS `st_send_command`'s `ST_SDHCI_CMD_WORD` PLUS ONE BIT - which is the
     * whole reason this rung could not reuse the body below it.** `st_send_command` folds the word
     * from the opcode and the five-bit mapping, and for a command with no data phase that is
     * complete. CMD8 has a data phase and the block learns it from `SDHCI_CMD_DATA`, a bit that is
     * NOT in the flag word the driver hands down. **This cell is the one a reader checks against the
     * register's own copy (`_ext_word_read`) and against the two clauses' immediates**: 0x081A is
     * the word a five-bit transcription writes, 0x083A is the word `sdhci.c:1146-1149` writes, and
     * they differ by exactly the bit that means "this command has data".
     */
    ST_LIVE("xnu_live_storage_ext_word",
            (uint32_t)ST_SDHCI_CMD_WORD_DATA(ST_CMD_OP_SEND_EXT_CSD, ST_MMC_RSP_R1_ADTC));
    st_send_command(ST_CMD_OP_SEND_EXT_CSD, 0u, ST_MMC_RSP_R1_ADTC, &c8);
    ST_LIVE("xnu_live_storage_ext_sent", c8.sent);
    ST_LIVE("xnu_live_storage_ext_word_read", c8.word_read);
    ST_LIVE("xnu_live_storage_ext_arg_wrote", c8.arg_wrote);
    ST_LIVE("xnu_live_storage_ext_complete", c8.complete);
    ST_LIVE("xnu_live_storage_ext_err", c8.err);
    ST_LIVE("xnu_live_storage_ext_status_after", c8.status_after);
    ST_LIVE("xnu_live_storage_ext_timeout", c8.timed_out);
    ST_LIVE("xnu_live_storage_ext_inhibit_timeout", c8.inhibit_timeout);
    ST_LIVE("xnu_live_storage_ext_resp", c8.resp);
    ST_LIVE("xnu_live_storage_ext_rsp_present", c8.rsp_present);
    ST_LIVE("xnu_live_storage_ext_state", (c8.resp & 0x00001E00u) >> 9);
    ST_LIVE("xnu_live_storage_ext_illegal", (c8.resp >> 22) & 0x1u);
    ST_LIVE("xnu_live_storage_ext_ready", (c8.resp >> 8) & 0x1u);

    /*
     * **AND THE ONE PLACE `st_send_command`'s POLL CANNOT BE USED, WHICH IS WHY THIS IS A SECOND
     * POLL AND NOT A SECOND CALL.** The body below breaks its loop as soon as `INT_STATUS` shows any
     * command bit - and for CMD8 the command's own completion arrives while the DATA is still moving.
     * `sdhci_irq` (`sdhci.c:2870-2885`) dispatches the two masks SEPARATELY: `SDHCI_INT_CMD_MASK` to
     * `sdhci_cmd_irq` and `SDHCI_INT_DATA_MASK` to `sdhci_data_irq`. This body uses
     * `st_send_command`'s completion as the GREEN LIGHT TO START READING and then polls the block's
     * own state for the words themselves - which is the order the vendor's interrupt-driven path
     * takes for the same reason.
     */
    ST_LIVE("xnu_live_storage_ext_irq_cmd", (uint32_t)ST_SDHCI_INT_CMD_MASK);
    ST_LIVE("xnu_live_storage_ext_irq_data", (uint32_t)ST_SDHCI_INT_DATA_BITS);

    /* --- the PIO read: 128 words, each gated on the block's own state, each published --------------- */
    gated = 0u;
    saw_do = 0u;
    saw_da = 0u;
    first_ps = 0u;
    last_ps = 0u;
    for (i = 0u; i < (uint32_t)ST_EXT_CSD_WORDS; i++) {
        ps = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
        if (i == 0u)
            first_ps = ps;
        last_ps = ps;
        if ((ps & (uint32_t)ST_SDHCI_DOING_READ) != 0u)
            saw_do++;
        if ((ps & (uint32_t)ST_SDHCI_DATA_AVAILABLE) != 0u)
            saw_da++;
        /*
         * **The one thing this loop does that the vendor's does not: it reads the word even when
         * the state bit says the FIFO is empty.** `sdhci_transfer_pio` reads only inside
         * `while (PRESENT_STATE & DATA_AVAILABLE)`; this body reads the buffer unconditionally and
         * counts the gated iterations, so a transfer in which the bit never came up produces 128
         * published words of whatever the port holds instead of an empty buffer and a silent zero.
         * **That is a reading and it is also a risk, and the risk is named**: an early read could
         * take a partial beat. It is taken because the alternative - a loop that can exit having
         * read nothing - is the `mi4-silence-is-a-reading-only-if-success-is-silent` shape, and
         * because this port is read-only and a read cannot change what the card is doing.
         */
        st_ext_csd[i] = st_read32(ST_HC_MEM_BASE + ST_SDHCI_BUFFER);
        if ((ps & (uint32_t)ST_SDHCI_DATA_AVAILABLE) != 0u)
            gated++;
    }
#if STAGE90_XNU_STORAGE_PROBE >= 39
    /*
     * **RUNG 40: THE WAIT THE RUNG-39 PRESS PROVED WAS MISSING.** The rung-39 press measured
     * `_ext_complete = 1` (its one-OR repair worked) AND `_ext_words_gated = 0` with 128 zero words:
     * with `SDHCI_INT_RESPONSE` enabled the command poll returns in MICROSECONDS, so the loop above -
     * which reads `BUFFER` unconditionally - runs BEFORE the card's data arrives. rung 38's missing
     * enable made the poll wait its full 1.2 s and the data arrived during that wait, so its read
     * succeeded BY the timeout; the defect was hiding this one.
     *
     * **THE FIX IS THE VENDOR'S OWN SHAPE.** `sdhci_transfer_pio` reads only inside
     * `while (PRESENT_STATE & SDHCI_DATA_AVAILABLE)`. This re-enters the 128-word loop and gates EACH
     * word on `DATA_AVAILABLE`, sampling the clock between batches and giving up (publishing
     * `data_wait_timeout = 1` and whatever arrived) once `ST_EXT_DATA_TICK_BUDGET` = 20 ms at 400 kHz
     * is spent - a bound, so a card that never delivers is a reading and not a hang.
     *
     * **IT ADDS NO DEVICE ACCESS.** The registers are `PRESENT_STATE` and `BUFFER`, both already in
     * this body's surface since rung 38; the loop runs a SECOND time over the SAME 128 words and the
     * second pass is the one whose values the decoded fields are read from. The first pass stays as
     * rung 38 left it, so `_ext_words_gated` is still the 0-or-128 reading of THAT pass and the new
     * cells (`_ext_data_waited`, `_ext_data_wait_timeout`, `_ext_data_ticks`) are the second
     * pass's - and a press on this arm carries both, which is the reading the arm exists for.
     */
    gated2 = 0u;
    data_timeout = 0u;
    waited = 0u;
    t0_data = (uint32_t)stage90_cntvct_read();
    for (i = 0u; i < (uint32_t)ST_EXT_CSD_WORDS; i++) {
        waited = 0u;
        steps2 = 0u;
        for (;;) {
            ps = st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE);
            if ((ps & (uint32_t)ST_SDHCI_DATA_AVAILABLE) != 0u)
                break;
            if (++steps2 >= ST_EXT_DATA_INNER) {
                steps2 = 0u;
                if ((uint32_t)stage90_cntvct_read() - t0_data >= ST_EXT_DATA_TICK_BUDGET) {
                    data_timeout = 1u;
                    break;
                }
            }
            waited++;
        }
        if (data_timeout != 0u)
            break;
        st_ext_csd[i] = st_read32(ST_HC_MEM_BASE + ST_SDHCI_BUFFER);
        gated2++;
    }
    ST_LIVE("xnu_live_storage_ext_data_waited", waited);
    ST_LIVE("xnu_live_storage_ext_data_wait_timeout", data_timeout);
    ST_LIVE("xnu_live_storage_ext_data_gated", gated2);
    ST_LIVE("xnu_live_storage_ext_data_ticks",
            (uint32_t)stage90_cntvct_read() - t0_data);
#endif
    ST_LIVE("xnu_live_storage_ext_words_read", (uint32_t)ST_EXT_CSD_WORDS);
    ST_LIVE("xnu_live_storage_ext_words_gated", gated);
    ST_LIVE("xnu_live_storage_ext_doing_read_seen", saw_do);
    ST_LIVE("xnu_live_storage_ext_data_avail_seen", saw_da);
    ST_LIVE("xnu_live_storage_ext_ps_first", first_ps);
    ST_LIVE("xnu_live_storage_ext_ps_last", last_ps);

    /* --- the transfer's own end: INT_STATUS at the END, which is when the DATA bits live -------------- */
    word = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS);
    ST_LIVE("xnu_live_storage_ext_int_status_end", word);
    ST_LIVE("xnu_live_storage_ext_int_data_end",
            (word & (uint32_t)ST_SDHCI_INT_DATA_END) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_ext_int_data_avail",
            (word & (uint32_t)ST_SDHCI_INT_DATA_AVAIL) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_ext_int_space_avail",
            (word & (uint32_t)ST_SDHCI_INT_SPACE_AVAIL) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_ext_int_data_err",
            word & ((uint32_t)ST_SDHCI_INT_DATA_TIMEOUT | (uint32_t)ST_SDHCI_INT_DATA_CRC |
                    (uint32_t)ST_SDHCI_INT_DATA_END_BIT));
    ST_LIVE("xnu_live_storage_ext_int_leftover",
            word & ~((uint32_t)ST_SDHCI_INT_CMD_MASK | (uint32_t)ST_SDHCI_INT_DATA_BITS));

    /* --- and the EXT_CSD's own four fields, at the offsets mmc.h names ------------------------------ */
    /*
     * `mmc.c:310` reads `ext_csd[EXT_CSD_STRUCTURE]`, `:321` reads `ext_csd[EXT_CSD_REV]`, `:333-336`
     * assembles `EXT_CSD_SEC_CNT` little-endian, and `:348` reads `ext_csd[EXT_CSD_CARD_TYPE]` -
     * four fields, four offsets, and every one is a BYTE of the 512 except `SEC_CNT`. **`_ext_rev`
     * is the one that says whether the transfer worked at all**: a card that answered CMD8 with a
     * command response but sent no data leaves all 512 bytes as they were, and the buffer is `.bss`
     * - zeroed - so `_ext_rev = 0` with `_ext_words_gated = 0` is "the command landed and the data
     * did not", a state no command-only rung could reach. `mmc.c:322` rejects a revision above 7.
     */
    ST_LIVE("xnu_live_storage_ext_rev", ST_EXT_CSD_BYTE(ST_EXT_CSD_OFF_REV));
    ST_LIVE("xnu_live_storage_ext_structure", ST_EXT_CSD_BYTE(ST_EXT_CSD_OFF_STRUCTURE));
    ST_LIVE("xnu_live_storage_ext_card_type", ST_EXT_CSD_BYTE(ST_EXT_CSD_OFF_CARD_TYPE));
    ST_LIVE("xnu_live_storage_ext_sec_count",
            ST_EXT_CSD_WORD(ST_EXT_CSD_OFF_SEC_CNT));
    ST_LIVE("xnu_live_storage_ext_byte0", ST_EXT_CSD_BYTE(0u));
    /*
     * **AND ONE WORD THAT IS NOT A FIELD, READ AS A FRESHNESS WITNESS.** `EXT_CSD_REV` is 0 on a
     * buffer that was never filled, and so is a great deal else - so the four readings above cannot
     * separate "the card sent 512 zeroes" from "nothing was sent". `_ext_w0` and `_ext_w127` are the
     * FIRST and LAST words of the transfer, published raw: a card's EXT_CSD is not uniform, so two
     * words that differ from each other and from zero are the buffer's own statement that it holds a
     * transfer. This is rung 12's `_resp_read` and rung 34's `_csd_raw_*` shape, one transfer down.
     */
    ST_LIVE("xnu_live_storage_ext_w0", st_ext_csd[0]);
    ST_LIVE("xnu_live_storage_ext_w127", st_ext_csd[ST_EXT_CSD_WORDS - 1u]);

    /* --- the window's ONE exit, the timeout restored, and the state the block is left in ------------- */
    ST_LIVE("xnu_live_storage_ext_wrote_back", int_enable);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable);
    ST_LIVE("xnu_live_storage_ext_ps_end",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
    ST_LIVE("xnu_live_storage_ext_int_status_post",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS));
    ST_LIVE("xnu_live_storage_ext_tout_restore", was);
    st_write8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL, (uint8_t)was);
    ST_LIVE("xnu_live_storage_ext_tout_readback",
            (uint32_t)st_read8(ST_HC_MEM_BASE + ST_SDHCI_TIMEOUT_CONTROL));
    ST_LIVE("xnu_live_storage_ext_readback",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE));
    ST_LIVE("xnu_live_storage_ext_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 37 - one body, one caller, and the ladder's first data phase:
        *   `_ext_gated = 1` with `_ext_done = 0` is **THE PRECONDITION REFUSING**, and it is the row
        *     that costs the least and says the most: `_ext_gate_state` is `R1_CURRENT_STATE` re-read
        *     out of `RESPONSE + 0` at the top of this body, and a value that is not `4` there is the
        *     card answering a question this rung did not ask - the repair is rung 37 again, not this
        *     rung, and no register below this line was touched.
        *   `_ext_complete = 1` with `_ext_err = 0` and `_ext_rev` in 1..7 is **CMD8 LANDED AND THE
        *     CARD IS AN eMMC v4 PART**: `_ext_structure` (mmc.h:304) reads 0..2 and `_ext_sec_count`
        *     is the density - and `_ext_words_gated = 128` is the transfer itself having run.
        *   `_ext_complete = 1`, `_ext_err = 0`, **`_ext_rev = 0` and `_ext_w0 = _ext_w127 = 0`** is
        *     THE COMMAND LANDING AND THE TRANSFER NOT RUNNING: the response came back, the 512 bytes
        *     did not. The next arm is the DATA phase alone - `_ext_ps_first`/`_ext_ps_last` say
        *     whether the block ever entered DOING_READ, and `_ext_words_gated = 0` says the FIFO
        *     never offered a word. **This is the row the whole design of the loop exists for**: a
        *     poll that exited on an empty read would publish the same four zeros as an EXT_CSD that
        *     is genuinely zeroed.
        *   `_ext_complete = 1` with `_ext_err` carrying `SDHCI_INT_DATA_TIMEOUT` or `_DATA_CRC` or
        *     `_DATA_END_BIT` (`_ext_int_data_err` nonzero) is THE CARD'S OWN DATA FAILURE, which is
        *     what `TIMEOUT_CONTROL` and the CRC check exist to catch - the first rung on which a
        *     data-path bit can be set at all.
        *   `_ext_complete = 0` with `_ext_inhibit_timeout = 1` is the block refusing a command whose
        *     inhibit window now includes `DATA_INHIBIT` (sdhci.h:66) - a rung-38-only precondition,
        *     since no rung above sent a command with a data phase. */

/*
 * **The per-command key block, written once and used twice.** A macro and not a function, because
 * every key is a string literal and the two calls differ only in the tag. Every cell the
 * pre-registration names is here, in one place, so that the record and the image cannot drift: a
 * reader comparing the two reads the same list twice.
 */
#define ST_CMD_PUBLISH(tag, r)                                                  \
    do {                                                                         \
        ST_LIVE("xnu_live_storage_cmd" tag "_ps_before",     (r).ps_before);     \
        ST_LIVE("xnu_live_storage_cmd" tag "_inhibit_before", (r).inhibit_before);\
        ST_LIVE("xnu_live_storage_cmd" tag "_inhibit_polls", (r).inhibit_polls); \
        ST_LIVE("xnu_live_storage_cmd" tag "_inhibit_ticks", (r).inhibit_ticks); \
        ST_LIVE("xnu_live_storage_cmd" tag "_inhibit_timeout", (r).inhibit_timeout);\
        ST_LIVE("xnu_live_storage_cmd" tag "_stale",         (r).stale);         \
        ST_LIVE("xnu_live_storage_cmd" tag "_clear_wrote",   (r).clear_wrote);   \
        ST_LIVE("xnu_live_storage_cmd" tag "_clear_after",   (r).clear_after);   \
        ST_LIVE("xnu_live_storage_cmd" tag "_arg",           (r).arg_wrote);     \
        ST_LIVE("xnu_live_storage_cmd" tag "_word",          (r).word_wrote);    \
        ST_LIVE("xnu_live_storage_cmd" tag "_sent",          (r).sent);          \
        ST_LIVE("xnu_live_storage_cmd" tag "_polls",         (r).polls);         \
        ST_LIVE("xnu_live_storage_cmd" tag "_ticks",         (r).ticks);         \
        ST_LIVE("xnu_live_storage_cmd" tag "_status_after",  (r).status_after);  \
        ST_LIVE("xnu_live_storage_cmd" tag "_complete",      (r).complete);      \
        ST_LIVE("xnu_live_storage_cmd" tag "_err",           (r).err);           \
        ST_LIVE("xnu_live_storage_cmd" tag "_timeout",       (r).timed_out);     \
        ST_LIVE("xnu_live_storage_cmd" tag "_ps_after",      (r).ps_after);      \
        ST_LIVE("xnu_live_storage_cmd" tag "_rsp_present",   (r).rsp_present);   \
        ST_LIVE("xnu_live_storage_cmd" tag "_resp",          (r).resp);          \
    } while (0)

#if STAGE90_XNU_STORAGE_PROBE >= 12
/*
 * **724's seven cells, in a macro of their own so that rung 11's `ST_CMD_PUBLISH` is untouched.**
 * A preprocessor directive cannot live inside a macro body, so an `#if` inside `ST_CMD_PUBLISH` would
 * expand to literal `#if` text and fail the build; a second macro called from an `#if` at the call
 * site is the same thing said in a form the language allows - and it has the property the record
 * wants, which is that a reader comparing rung 11's key list with rung 12's reads two lists that
 * differ by exactly this block.
 *
 * `_resp_read` is the odd one and it is deliberate: a zero `_resp` is a reading only if the register
 * was read, and on rung 11 it was not read at all for a no-response command.
 */
#define ST_CMD_PUBLISH_12(tag, r)                                                    \
    do {                                                                             \
        ST_LIVE("xnu_live_storage_cmd" tag "_word_read",     (r).word_read);          \
        ST_LIVE("xnu_live_storage_cmd" tag "_inhibit_after", (r).inhibit_after);      \
        ST_LIVE("xnu_live_storage_cmd" tag "_inhibit_seen",  (r).inhibit_seen);       \
        ST_LIVE("xnu_live_storage_cmd" tag "_inhibit_last",  (r).inhibit_last);       \
        ST_LIVE("xnu_live_storage_cmd" tag "_status_any",    (r).status_any);         \
        ST_LIVE("xnu_live_storage_cmd" tag "_any_polls",     (r).status_any_polls);   \
        ST_LIVE("xnu_live_storage_cmd" tag "_resp_read",     (r).resp_read);          \
    } while (0)
#endif /* STAGE90_XNU_STORAGE_PROBE >= 12 */

/*
 * **The arm, and the one thing it does that no rung below it does: it puts a command register.** The
 * gate order is the pre-registration's section 3 and the gate is what makes the two calls a sequence
 * rather than a pair: CMD1 is issued only if CMD0 completed with no command error, because a CMD1
 * issued after a CMD0 that did not complete would be a second transaction on a bus whose first one is
 * still open.
 *
 * **`noinline`, for a clause's reason and not for speed.** `build_entry.sh` asserts this body's device
 * accesses are the two enable reads and `PRESENT_STATE` and **no store at all** - so that every device
 * WRITE this arm makes is inside `st_send_command`'s own window, where its set is asserted exactly.
 * A caller inlined into the probe would leave its three reads unclassified in the probe's window and
 * would make "the arm's writes are in one function" a claim about the compiler instead of the image.
 *
 * `noclone` for `st_send_command`'s reason, one function over: called once with no constant worth
 * propagating, but the attribute is written here too so that the pair reads as one decision rather
 * than as one function that happens to have escaped cloning.
 */
static __attribute__((noinline, noclone)) void st_cmd_path(void)
{
    struct st_cmd_result c0, c1;
    uint32_t int_enable, sig_enable, gate_kind, sent = 0u, gated = 0u;
#if STAGE90_XNU_STORAGE_PROBE >= 18
    /*
     * **741 (and 743): CMD2's own sent bit, carried from rung 17's body to the gates that stand on
     * it - rung 19's `st_set_relative_addr` and rung 20's `st_cmd3_noresp`, which send the same
     * command with different response demands and are entered on the same condition.** The body
     * RETURNS the field it publishes as `_cid_sent`, so the gate below reads a value the log names
     * rather than re-deriving the same condition from `c1` a second time. It is zero unless the
     * rung-17 gate let the body run AND the command path inside it reached the `COMMAND 0x0e` store
     * - which is what the cell in the same log says, so a `_cid_sent = 0` beside `_rca_gated = 1` is
     * one fact written twice and not two facts that agree by luck.
     */
    uint32_t cid_sent = 0u;
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 29
    /*
     * **Rung 30's window word, held across CMD1.** Declared under the guard so that a value below 29
     * has the identifier nowhere in the translation unit - the same reason `cid_sent` above is
     * guarded, and the reason a rung's absence is checkable rather than a zero.
     */
    uint32_t c1_ena;
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 31
    /*
     * **Rung 32's window byte, held across CMD2.** Guarded like the two above it so that a value
     * below 31 has the identifier nowhere in the translation unit - "the absence of a rung is a
     * reading" applied to this file, one rung on.
     */
    uint32_t c2_tout;
#if STAGE90_XNU_STORAGE_PROBE >= 32
    /*
     * **796: rung 32's window handle; 799: rung 33's loop.** `op_arg` is the OCR window derived from
     * CMD1's first response, `op_busy` is whether the driver's own loop ever saw `MMC_CARD_BUSY` set,
     * and `c1b` is a SECOND result struct - not a reuse of `c1` - because the loop's last send must be
     * readable without destroying the probe's own four words, which the rows above still publish.
     */
    uint32_t op_arg, op_busy;
    struct st_cmd_result c1b;
#endif
#endif

    ST_LIVE("xnu_live_storage_cmd_calls", 1u);

#if STAGE90_XNU_STORAGE_PROBE >= 12
    /*
     * **THE CENSUS FIRST, and the one refusal it adds.** Every register the command depends on is read
     * at this moment and published; the only one of them that can stop the act is `POWER_CONTROL`'s
     * bus-power bit, because a block that is not driving the bus must not be sent a command. Gate 1
     * below is unchanged and its two registers are read again - which costs two reads and keeps this
     * rung inside the rung below it, rather than moving that rung's gate a rung up.
     */
    if (st_cmd_census() == 0u) {
        ST_LIVE("xnu_live_storage_cmd2_refused_power", 1u);
        ST_LIVE("xnu_live_storage_cmd_refused", 1u);
        ST_LIVE("xnu_live_storage_cmd_sent", 0u);
        ST_LIVE("xnu_live_storage_cmd_gated", 0u);
        ST_LIVE("xnu_live_storage_cmd_ps_before",
                st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
        ST_LIVE("xnu_live_storage_cmd_ps_after",
                st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
        ST_LIVE("xnu_live_storage_cmd_done", 0u);
        return;
    }
    ST_LIVE("xnu_live_storage_cmd2_refused_power", 0u);
#endif

    /*
     * **GATE 1, and it is read before anything is written.** The SDHCI `hc_irq` is SPI 123 -> intid
     * 155, which this image hands to nobody: a command completion that raised it would reach the
     * dispatcher as `_irq_other_count` and end the run. These two registers are the only thing that
     * can let that happen, so a non-zero value refuses the whole act and publishes why.
     */
    int_enable = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    sig_enable = st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE);
    ST_LIVE("xnu_live_storage_cmd_int_enable", int_enable);
    ST_LIVE("xnu_live_storage_cmd_sig_enable", sig_enable);

#if STAGE90_XNU_STORAGE_PROBE >= 14
    /*
     * **732: the gate is NARROWED, and the narrowing is the rung's whole premise.** Rungs 11 and 12
     * refused a non-zero `INT_ENABLE` at all, because their arms enable nothing; rung 14's arm is the
     * enable, so the register may carry exactly one bit and the line may still not be armed. The two
     * conditions that remain are the two halves of the conjunction that would raise intid 155:
     * `SIGNAL_ENABLE 0x38` must read ZERO (which is 730's measured-safe half), and `INT_ENABLE` may
     * carry `SDHCI_INT_RESPONSE` and nothing else. Both are read above and neither is written here.
     *
     * **`_cmd_gate_kind` is a kind and not a boolean, on purpose.** `_cmd_enabled_out` below stays
     * the rung-11 0/1 - it means "something was enabled out" at every rung - and the new cell says
     * WHICH, so a refusal is localised instead of being one bit that two different registers
     * produced. One name for two readings is this project's oldest defect.
     */
    gate_kind = (sig_enable != 0u) ? 1u
              : (((int_enable & ~(uint32_t)ST_SDHCI_INT_RESPONSE) != 0u) ? 2u : 0u);
    ST_LIVE("xnu_live_storage_cmd_gate_kind", gate_kind);
#else
    gate_kind = (int_enable != 0u || sig_enable != 0u) ? 1u : 0u;
#endif

    if (gate_kind != 0u) {
        ST_LIVE("xnu_live_storage_cmd_enabled_out", 1u);
        ST_LIVE("xnu_live_storage_cmd_refused", 1u);
        ST_LIVE("xnu_live_storage_cmd_sent", 0u);
        ST_LIVE("xnu_live_storage_cmd_gated", 0u);
        ST_LIVE("xnu_live_storage_cmd_ps_before",
                st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
        ST_LIVE("xnu_live_storage_cmd_ps_after",
                st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
        ST_LIVE("xnu_live_storage_cmd_done", 0u);
        return;
    }
    ST_LIVE("xnu_live_storage_cmd_enabled_out", 0u);
    ST_LIVE("xnu_live_storage_cmd_refused", 0u);
    ST_LIVE("xnu_live_storage_cmd_ps_before",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));

#if STAGE90_XNU_STORAGE_PROBE >= 14
    /*
     * **732: THE ENABLE, AND WHERE IT STANDS IS THE EXPERIMENT RATHER THAN A PREFERENCE.**
     *
     * The window opens HERE - immediately before CMD0 is put on the bus - and not anywhere below it,
     * because the question this rung asks is *can a command's own completion poll see the completion
     * with the enable set*. `st_send_command`'s poll is inside CMD0, so the enable has to be standing
     * before CMD0 is sent; the first build of this arm opened the window after CMD0, in the body that
     * runs just before the between-commands gate, and the arm could not answer its own question by
     * construction - CMD0's poll ran masked, `c0.complete` stayed 0, and the gate this rung is meant
     * to pass would have refused. It was refuted by 730's own log without spending a press, and the
     * cell that says so is `_cmd0_complete` beside `_cmd_gated`.
     *
     * **The window closes on ONE line, unconditionally, and that is a safety property rather than a
     * simplification.** The closing call stands immediately after CMD0's publishes, before the gate
     * and before `st_int_report()` below, so there is no path out of this interval that skips it -
     * not the census's refusal (above the window), not the register half of the gate (above it too),
     * and not the between-commands gate's early `return` (below it, with the enable already closed).
     * The two-exit shape the first build had described a window whose exits were unreachable.
     *
     * **It closes BEFORE `st_int_report()` on purpose.** Rung 13's `_int_status_before` is DEFINED as
     * the baseline taken with both enables clear, and its `_int_status_after` as the same register
     * read immediately after that body's own single-bit store. Leaving this window open across that
     * body would silently redefine a cell of the rung underneath while its name and its reader stayed
     * the same, which is `[[mi4-one-value-two-definitions]]` inside one function.
     *
     * The value written is `int_enable | SDHCI_INT_RESPONSE`, which is `SDHCI_INT_RESPONSE` alone
     * because the gate above proved the rest of the register is zero. **The OR is written out rather
     * than the constant** so that the store's value is derived from the read that authorised it, and
     * the cells below are the pair that makes it a store rather than an intention - the same reason
     * rung 12 added `_cmdN_word_read`.
     *
     * **`_ena_held` is the block's own copy of the enable and it is not the value written.** 730's
     * press measured its own readback as `0x00008001` for a store of `0x00000001`, and `0x00008000`
     * for a store of `0x00000000`, so bit 15 (`SDHCI_INT_ERROR`) is set by a write to this register
     * and is not cleared by one. That is the reason the cell exists: a rung whose restore is a
     * *partial* restore says so in a cell rather than in a reader's assumption.
     *
     * **`_ena_status_pre` is the third of 730's owed reads and it is taken with NO COMMAND IN
     * FLIGHT.** Rung 13 measured `INT_STATUS 0x30 = 1` immediately after setting the enable, one
     * instruction after a command that had already completed and been cleared, so there was a
     * latched RESPONSE bit to see. Read here, before CMD0, the same register answers the other half
     * of that question: 0 says the bit is *latched by the completion*, and 1 would say the block is
     * holding a bit no command in this run has produced.
     *
     * **`_ena_host_version` is `HOST_VERSION 0xFE` as a HALFWORD** and it is the read experiment 697
     * left owed and 730 did not take: the SDHCI specification gives this register's low byte the
     * specification version, and a 3.00-or-later block is the reading under which `INT_STATUS`'s
     * latching behaviour is described at all. It is one read at an offset no 32-bit access may reach,
     * and it changes nothing.
     */
#if STAGE90_XNU_STORAGE_PROBE >= 28
    /*
     * **RUNG 29 (value 28): THE SAME WINDOW, ONE CONSTANT WIDE.** 765 section 1 measured that this
     * block's `INT_STATUS` is a *gated view* - a status bit is visible only while its enable stands -
     * and 766 acted on it in `st_cmd3_noidx`, the LAST command this ladder sends. 783 then read the
     * scope and found the widening had reached ONE of THREE windows: this one, which carries CMD0
     * and CMD1, and `st_all_send_cid`'s, which carries CMD2, still wrote the lone `RESPONSE` bit. So
     * on the rung-28 arm `_cmd1_status_any = 0` and `_cid_status_any = 0` over their full 1.2 s
     * windows were still readings about the MASK - and CMD1 is `SEND_OP_COND`, the command an eMMC
     * must answer for this ladder to move at all.
     *
     * **A value below 28 keeps rung 14's word**, so the two arms differ in exactly one constant per
     * window, and this block is written as a whole `#if`/`#else` pair rather than as a conditional
     * expression so that the value-27 source is byte-identical and its build can be compared against
     * the parked arm - which is how "this arm strictly contains rung 28" is measured and not argued.
     */
    {
        uint32_t ena;
        ena = int_enable | (uint32_t)ST_SDHCI_INT_ENABLE_CMD;
        ST_LIVE("xnu_live_storage_ena_wrote", ena);
        st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, ena);
    }
#else
    ST_LIVE("xnu_live_storage_ena_wrote", int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
#endif
    ST_LIVE("xnu_live_storage_ena_held",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE));
    ST_LIVE("xnu_live_storage_ena_status_pre",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_STATUS));
    ST_LIVE("xnu_live_storage_ena_host_version",
            (uint32_t)st_read16(ST_HC_MEM_BASE + ST_SDHCI_HOST_VERSION));
#endif

    /* CMD0 - mmc_go_idle (`mmc_ops.c:107`): argument 0, MMC_RSP_NONE, so no response is read. */
    ST_LIVE("xnu_live_storage_cmd0_op", ST_CMD_OP_GO_IDLE_STATE);
    st_send_command(ST_CMD_OP_GO_IDLE_STATE, 0u, 0u, &c0);
    ST_CMD_PUBLISH("0", c0);
#if STAGE90_XNU_STORAGE_PROBE >= 12
    ST_CMD_PUBLISH_12("0", c0);
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 25
    /*
     * **771: the cell that was already being measured and thrown away.** `cmdlow_seen` is
     * incremented by the same first-1024 sampler that fills `inhibit_seen`, for EVERY command,
     * and rung 24 published it for `nidx` alone - so the only trace of what the CMD line was
     * doing during CMD0 exists at all is `inhibit_last` bit 24, ONE sample, taken at the break.
     * It is published here and costs NOTHING: no new device access, no new register, no new
     * window, no new megabyte, and no store of any kind.
     */
    ST_LIVE("xnu_live_storage_cmd0_cmdlow_seen", c0.cmdlow_seen);
#endif
    sent += c0.sent;

#if STAGE90_XNU_STORAGE_PROBE >= 14
    /*
     * **THE WINDOW'S ONE EXIT, AND IT IS TAKEN BEFORE ANYTHING CAN BRANCH.** The enable is closed
     * here - before the gate and before `st_int_report()`, and on the only line that runs after
     * CMD0's publishes - so the interval it was open for is exactly CMD0's own send and poll, and
     * there is no path out of `st_cmd_path` that reaches the gate, CMD1, or a `return` with the
     * enable still set. `seq = 1` is this exit's name; it is a constant here rather than a branch's
     * label, and it stays a parameter because the cell it publishes is how a reader tells "the
     * restore ran" from "the enable was never opened".
     *
     * **It is called even when CMD0 was not sent at all** - when the register half of the gate
     * refused, this line is never reached because the window is opened below it, so the ordering
     * itself is the guard. That asymmetry is the design: the window has one entrance and one exit,
     * both unconditional, and the gate that can branch is outside both.
     */
    st_cmd_enable_restore(int_enable, 1u);
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 13
    /*
     * **729: THE RUNG-14 BLOCK, AND ITS POSITION IS A READING AND NOT A PLACE ON THE PAGE.** It stands
     * immediately after CMD0's publishes and BEFORE the gate, and the reason is rung 13's own log: 726
     * measured `_cmd_gated = 1`, which is `st_cmd_path` returning before CMD1 because `c0.complete == 0`.
     * A block placed after the commands - which is where 726 section 3's prose reads as if it goes -
     * would sit on a path this machine has never taken and has no reason to take, and the press would
     * buy a log with no rung-14 key in it at all. That is m720's shape, an absent key with one of its
     * three producers guaranteed by construction.
     *
     * It is called UNCONDITIONALLY, and not only when `c0.sent` is set. The cell that carries the
     * information is `_int_status_after`, and a reader qualifies it against `_cmd0_sent` in the same
     * log; what an `if` here would buy instead is a second way for the rung's own keys to be absent from
     * a log whose reader cannot tell that from a device that said nothing.
     */
    st_int_report();
#endif


    /*
     * **THE GATE BETWEEN THE TWO COMMANDS.** CMD1 is `mmc_attach_mmc`'s next act and it is issued
     * only if CMD0 completed with no command error - a CMD1 after a CMD0 that did not complete would
     * be a second command on a bus whose first one is still open.
     */
    if (c0.sent == 0u || c0.complete == 0u || c0.err != 0u) {
        gated = 1u;
        ST_LIVE("xnu_live_storage_cmd_gated", gated);
        ST_LIVE("xnu_live_storage_cmd_sent", sent);
        ST_LIVE("xnu_live_storage_cmd_ps_after",
                st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));
        ST_LIVE("xnu_live_storage_cmd_done", 0u);
        return;
    }
    ST_LIVE("xnu_live_storage_cmd_gated", gated);

#if STAGE90_XNU_STORAGE_PROBE >= 29
    /*
     * **RUNG 30 (value 29): THE THIRD WINDOW, AND THE FIRST ONE CMD1 HAS EVER RUN INSIDE.** 785
     * widened this function's window believing it covered CMD0 and CMD1; 786 refuted that from the
     * capture's own line order, because rung 14's restore - `_ena_wrote_back = 0x00000000` - runs on
     * the line after CMD0's publishes and BEFORE this gate. So CMD1's interval has carried an
     * `INT_ENABLE` that enables neither the completion nor any error detail on every arm from rung 14
     * on, and `_cmd1_status_any = 0` over 5,087,232 polls with `_cmd1_timeout = 1` is the arm's own
     * bound and nothing else. **This window is the same two stores to the same register, with the
     * same constant, inside an interval the ladder has been running CMD1 in all along**: no new
     * address, no new width, no new megabyte, no new device, and `SIGNAL_ENABLE 0x38` still read and
     * never written, so nothing here can raise a line this image cannot service.
     *
     * **It is opened by a CALL, so that this body's own device surface is unchanged**: the store and
     * its two readings live in `st_cmd1_enable_open`, and this function gains two `bl`s and no access
     * of its own. The word the open returns is the word the close puts back - one value with two
     * consumers rather than one expression written twice.
     */
    c1_ena = st_cmd1_enable_open(int_enable);
#endif

    /*
     * CMD1 - `mmc_attach_mmc` (`mmc.c:1923`) with the driver's own single-pass probe argument 0 and
     * `MMC_RSP_R3` (= `MMC_RSP_PRESENT`), so `sdhci_finish_command`'s guard reads RESPONSE 0x10.
     */
    ST_LIVE("xnu_live_storage_cmd1_op", ST_CMD_OP_SEND_OP_COND);
    st_send_command(ST_CMD_OP_SEND_OP_COND, 0u, ST_MMC_RSP_PRESENT, &c1);
    ST_CMD_PUBLISH("1", c1);
#if STAGE90_XNU_STORAGE_PROBE >= 12
    ST_CMD_PUBLISH_12("1", c1);
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 25
    /*
     * **The cell 771 needs most, and the reason is a single sample.** `_cmd1_inhibit_last` reads
     * `0x00F80000` on every capture in the archive - bit 24, the CMD line own level, LOW - while
     * `_cmd1_inhibit_seen` reads 0 over all 1024 samples. So on this block the line can sit LOW
     * with `CMD_INHIBIT` never rising, and "the block never started this command" and "the block
     * drove the line and the inhibit bit is not a reliable witness" are INDISTINGUISHABLE from
     * one sample. The count separates them: 0 says the line never moved, and anything else says
     * it did. No new device access - the sampler already reads this word.
     */
    ST_LIVE("xnu_live_storage_cmd1_cmdlow_seen", c1.cmdlow_seen);
#endif
    sent += c1.sent;

    /*
     * **The two faces of the response that the DRIVER's own code uses**, published raw-adjacent on
     * purpose: `mmc_send_op_cond`'s loop tests `cmd.resp[0] & MMC_CARD_BUSY` (`mmc_ops.c:156`,
     * `mmc.h:227`) and `mmc_select_voltage` masks the window against `host->ocr_avail`. The bit's
     * *meaning* is the driver's convention and not this image's, so what is published is the raw word
     * above plus these two readings of it, and no translation.
     */
    ST_LIVE("xnu_live_storage_cmd1_resp_busy", (c1.resp >> 31) & 1u);
    ST_LIVE("xnu_live_storage_cmd1_resp_voltage", c1.resp & 0x00FF8000u);
#if STAGE90_XNU_STORAGE_PROBE >= 32
    /*
     * **799: THE SECOND CMD1 - THE ONE THE DRIVER LOOPS, AND THE ONE THIS IMAGE HAS NEVER SENT.**
     *
     * It stands HERE, after CMD1's own publishes and before the CMD2 gate, for a reason that is a
     * position and not a convenience: the probe's four words and its two derived readings must survive
     * into the log beside the loop's, so a reader can see WHICH RESPONSE the derived argument came
     * from. `c1b` is a separate struct for exactly that - the loop overwrites `resp` on every send and
     * `c1` is still being published above it.
     *
     * **AND IT IS ONE VARIABLE AND NOT TWO.** This arm ADDS the loop and leaves the CMD2 gate exactly
     * where rung 16 put it, so CMD2 still fires on the same condition it always has and the two arms
     * are comparable cell for cell. Gating CMD2 on the loop's answer is the driver's semantics and it
     * is a SEPARATE rung - one change per rung is this ladder's own discipline, and 785's defect was
     * a window and a claim moving in one step.
     *
     * **AND THAT SEPARATE RUNG IS 804, WHICH IS THE ONE ABOVE THIS ONE.** It consumes this return
     * value as the CMD2 gate's own condition, so from value 33 the `(void)` below is a no-op cast on a
     * value that IS read and at value 32 it is the cast that says the loop exists and decides nothing.
     * The two arms are one `#if` apart and both are in the tree, which is what keeps this sentence a
     * property of the source rather than the record of a moment.
     */
    op_arg = st_op_cond_arg(c1.resp);
    op_busy = st_op_cond_loop(op_arg, &c1b);
    (void)op_busy;
#endif
    ST_LIVE("xnu_live_storage_cmd_sent", sent);
    ST_LIVE("xnu_live_storage_cmd_ps_after",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));

#if STAGE90_XNU_STORAGE_PROBE >= 29
    /*
     * **THE THIRD WINDOW'S ONE EXIT, AND IT IS ON THE LINE THAT FOLLOWS CMD1's OWN PUBLISHES.**
     * It stands BEFORE the rung-16 CMD2 block on purpose: CMD2 has a window of its own and opens it
     * with its own store, so an interval left open across that call would be closed by CMD2's
     * restore rather than by this one, and `_c1_status_post` would then be a reading of a moment
     * CMD2 had already written. Between the open above and this line there is no `return` and no
     * branch - `st_cmd_path` reaches `_cmd_done` on one path - so this window has exactly one
     * entrance and one exit and 732's rule holds for it exactly as it holds for rung 14's.
     *
     * **AND IT PUTS BACK WHAT THE OPEN WROTE, not the expression the open was built from.** The
     * argument is the word `st_cmd1_enable_open` returned, so the log carries
     * `_c1_ena_wrote == _c1_ena_wrote_back` as a READING rather than as two evaluations of one
     * expression that happen to agree - which is 741's shape one window over, and the reason the open
     * returns a value at all.
     */
    st_cmd1_enable_restore(c1_ena);
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 16
    /*
     * **737: THE DRIVER'S OWN NEXT STATEMENT, AND ITS GATE IS THE DRIVER'S OWN CONDITION.** In
     * `mmc.c:1375-1380` the call to `mmc_all_send_cid` follows the op-cond call unconditionally - the
     * driver does not gate CMD2 on a completion, because on hardware whose status latch works there is
     * nothing to gate: `mmc_send_op_cond` either returned the OCR or it returned an error.
     *
     * **This gate is read off CMD1's RESPONSE and NOT off its status bit, and 733's press is why.** That
     * press measured `_cmd1_complete = 0` with `_cmd1_resp = 0x40ff8080` - the card ANSWERED and no
     * completion latched - so a status-bit gate here would refuse on every arm this ladder can build,
     * and the refusal would be an artifact of the same masked latch this whole line of rungs is
     * about. `MMC_CARD_BUSY` is `mmc.h:227`'s `0x80000000`, the bit `mmc_send_op_cond`'s own loop tests
     * (`mmc_ops.c:158`) to decide the card is out of reset - so the condition is the DRIVER's, read in
     * the direction the driver reads it, and `_cid_gated_reason` says which half failed when it does:
     * 1 = CMD1 was never sent (the between-commands gate refused above), 2 = it was sent and the card
     * was still busy.
     *
     * **A refusal here costs nothing and is not a silence**: `_cid_gated` is published on BOTH paths,
     * so an arm whose CMD1 did not answer produces `_cid_gated = 1` with a reason and no `_cid_*` keys
     * beside it, which is a reading rather than an absent block.
     */
#if STAGE90_XNU_STORAGE_PROBE >= 31
    /*
     * **796: rung 32's window opens HERE - on the line above the CMD2 gate and not inside a body,
     * because the interval the raised bound must cover is the CMD2 command itself, and the two
     * bodies it calls touch `TIMEOUT_CONTROL 0x2E` and NOTHING ELSE.** The store is unconditional
     * with respect to the gate: an arm whose `c1` refuses CMD2 still opens and closes this window,
     * so `_tout_calls = 1` with `_cid_calls` absent is a READING of that refusal rather than a
     * window that never ran.
     */
    c2_tout = st_tout_open();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 33
    /*
     * **804: RUNG 34 - THE GATE TESTS THE WORD THE DRIVER TESTS, AND THIS IS THE RUNG 800 NAMED AS
     * THE SEPARATE ONE.** 799's own comment above `op_busy` says it in the source: "Gating CMD2 on the
     * loop's answer is the driver's semantics and it is a SEPARATE rung - one change per rung is this
     * ladder's own discipline". The rung-33 press (803) is what made it a measurement rather than a
     * tidy-up, and it did so by showing that the old condition is not merely inverted - it reads the
     * WRONG WORD.
     *
     * **THE OLD CONDITION TESTS `c1.resp`, THE PROBE'S RESPONSE, AND THE PROBE IS A SINGLE PASS BY THE
     * DRIVER'S OWN DESIGN.** `mmc_send_op_cond` returns after one CMD1 (`mmc_ops.c:148-150`'s
     * `if (ocr == 0) break;`), so on a card that has not finished power-up the probe's word says
     * exactly what 803 measured it says - the OCR with bit 31 CLEAR - and `(c1.resp & MMC_CARD_BUSY)
     * == 0` is TRUE, so the old gate PASSES. **The old gate therefore refuses only when the card
     * happens to be busy on the probe's single pass, i.e. it fires on the one reading that means "come
     * back later" and passes on the reading that means "not ready yet".** That is why nine arms
     * (rung 16 through 32) put CMD2 and CMD3 on the bus at a card that had not finished power-up, and
     * why `_cid_gated` was 0 on every one of them.
     *
     * **THE DRIVER'S CONDITION IS `mmc_ops.c:157`'s, AND IT IS THE LOOP'S.** `if (cmd.resp[0] &
     * MMC_CARD_BUSY) break;` - the loop leaves when bit 31 SETS, and the OCR it returns is the word the
     * driver then acts on. `op_busy` is that condition's own result, set inside `st_op_cond_loop` at
     * the exact place the driver evaluates it and returned here, so the gate and the loop cannot
     * disagree about what the card said - which is the same one-reading-two-consumers property
     * `st_all_send_cid`'s return already has in this function.
     *
     * **ONE VARIABLE, AND IT IS A CONDITION RATHER THAN AN ACT.** No store, no new address, no new
     * width and no new device register key: the gate's two arms call the same bodies they called
     * before, in the same order, at the same place in the driver's sequence. What changes is which
     * word decides whether the second of them runs.
     *
     * **THE TWO CELLS BELOW ARE THE REPAIR MADE READABLE, AND THEY ARE PUBLISHED ON BOTH PATHS.** The
     * arm's own reading is `_cid_gate_word` beside the ladder's long-standing `_cmd1_resp`: on a row
     * where the card became ready they differ by EXACTLY the busy bit (`0x40ff8080` against
     * `0xc0ff8080`, 803's own numbers) and that difference IS this rung. On a row where the loop never
     * saw the card out of reset the two cells are EQUAL and `_cid_gated` is 1 - which is the row the
     * old gate could not produce. `_cid_gate_busy` is the test's result printed beside its source, the
     * shape `_cmd1_resp_busy` already has four lines above.
     *
     * **AND THE GATE IS STILL STRICTER THAN THE DRIVER'S, DELIBERATELY.** The driver has no gate at
     * all (737's comment above): `c1.sent != 0u` is kept so that `_cid_gated_reason` still separates
     * 1 (CMD1 never reached the bus) from 2 (it did, and the loop never saw the card out of reset).
     * Reason 2's MEANING is what moved with this rung - it is now the loop's answer and not the
     * probe's, and 803 is what says the two are different questions.
     */
    ST_LIVE("xnu_live_storage_cid_gate_word", c1b.resp);
    ST_LIVE("xnu_live_storage_cid_gate_busy", op_busy);
    if (c1.sent != 0u && op_busy != 0u) {
        ST_LIVE("xnu_live_storage_cid_gated", 0u);
#if STAGE90_XNU_STORAGE_PROBE >= 18
        cid_sent = st_all_send_cid(int_enable);
#else
        st_all_send_cid(int_enable);
#endif
    } else {
        ST_LIVE("xnu_live_storage_cid_gated", 1u);
        ST_LIVE("xnu_live_storage_cid_gated_reason", (c1.sent == 0u) ? 1u : 2u);
    }
#else
    if (c1.sent != 0u && (c1.resp & ST_MMC_CARD_BUSY) == 0u) {
        ST_LIVE("xnu_live_storage_cid_gated", 0u);
#if STAGE90_XNU_STORAGE_PROBE >= 18
        cid_sent = st_all_send_cid(int_enable);
#else
        st_all_send_cid(int_enable);
#endif
    } else {
        ST_LIVE("xnu_live_storage_cid_gated", 1u);
        ST_LIVE("xnu_live_storage_cid_gated_reason", (c1.sent == 0u) ? 1u : 2u);
    }
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 31
    /*
     * **And it closes HERE, before CMD3 - and that is a deliberate part of this arm and not a
     * tidy-up.** CMD3 is a 48-bit command and its response needs 535.2 us, which FITS inside the
     * reset-value bound of 665.2 us - so CMD3 runs on this arm at the bound this ladder has always
     * had, and whether it completes is 795 section 6's protocol claim tested on its own terms.
     * A window that covered CMD3 too would answer that question by giving CMD3 the same help CMD2
     * is getting, and the two rows would stop being separable.
     */
    st_tout_restore(c2_tout);
#endif
#endif

#if STAGE90_XNU_STORAGE_PROBE == 18
    /*
     * **741: rung 19's own statement, and its gate is the ONE reading rung 17's body returned.**
     *
     * `mmc.c:1409` puts `mmc_set_relative_addr` immediately after `mmc_all_send_cid` with no
     * condition beyond `!mmc_host_is_spi(host)`, so the driver itself does not gate CMD3 on a
     * completion - on hardware whose status latch works, `mmc_send_op_cond` has already returned an
     * error or an OCR. This arm cannot read that error (the latch is the thing this whole line of
     * rungs is about), so the gate is the weakest one that is still a reading: **CMD2 was SENT**.
     * That is `cid_sent`, the value `st_all_send_cid` returns and the same field it publishes as
     * `_cid_sent`, so the gate and the cell cannot disagree - and it is deliberately NOT the CMD2
     * response, because CMD2's response is the very reading in doubt (740 section 6).
     *
     * **Why a gate at all, when the driver has none.** `st_send_command` already carries the
     * driver's own two per-command guards - the bounded wait for `SDHCI_CMD_INHIBIT` to clear
     * (`sdhci.c:1096`) and the refusal to issue when the write-1-to-clear left a command bit latched
     * - so nothing here can put a command on a bus another command still holds. What the gate buys
     * is that an arm whose CMD2 never reached `COMMAND 0x0e` does not spend its CMD3 on a bus whose
     * second command never went out, and that the refusal is a published cell (`_rca_gated` with a
     * reason) rather than an absent block a reader has to interpret.
     */
    if (cid_sent != 0u) {
        ST_LIVE("xnu_live_storage_rca_gated", 0u);
        st_set_relative_addr(int_enable);
    } else {
        ST_LIVE("xnu_live_storage_rca_gated", 1u);
        ST_LIVE("xnu_live_storage_rca_gated_reason", 1u);
    }
#endif

#if STAGE90_XNU_STORAGE_PROBE == 19
    /*
     * **743: rung 20's call, and it is the SAME statement with ONE constant different.**
     *
     * `mmc.c:1409`'s `mmc_set_relative_addr(card)` is the driver's own next statement after
     * `mmc_all_send_cid`, and rung 19 sent it with `MMC_RSP_R1` and measured the block start it and
     * never finish. This rung sends the same opcode with the same non-zero argument and **no response
     * demand**, which is the one flag word in this ladder whose completion is already known: CMD0's,
     * whose flags are zero and which is the only command here that ever latched a status.
     *
     * **The gate is rung 19's and it is deliberately UNCHANGED** - CMD2 was SENT - so that the two
     * rungs' logs are comparable at the same place in the same driver order, and a `_nrsp_gated = 1`
     * is the same complete answer `_rca_gated = 1` is: the value `st_all_send_cid` RETURNS and the
     * value it publishes as `_cid_sent` are one reading with two consumers, so the gate and the cell
     * in the same log cannot disagree.
     *
     * **Why not send rung 19's R1 command first and then this one**: rung 19's own press measured
     * that its CMD3 left `CMD_INHIBIT` set at the end of a 1.2 s window, so a second command in the
     * same boot would meet the driver's own inhibit guard inside `st_send_command` and either wait
     * out its 10 ms bound or be refused - reading rung 19's in-flight state rather than this rung's
     * flag word. One command, one flag word, one answer.
     */
    if (cid_sent != 0u) {
        ST_LIVE("xnu_live_storage_nrsp_gated", 0u);
        st_cmd3_noresp(int_enable);
    } else {
        ST_LIVE("xnu_live_storage_nrsp_gated", 1u);
        ST_LIVE("xnu_live_storage_nrsp_gated_reason", 1u);
    }
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 20
    /*
     * **746: rung 21's call, and it is rung 20's statement with the demand PUT BACK and the `INDEX` bit
     * taken out instead - ONE BIT IN THE OTHER DIRECTION, on the same opcode and the same argument.**
     *
     * Rung 20 measured that this command without a response demand COMPLETES in 0.255 ms; rung 19
     * measured that the same command WITH `RESP_SHORT|CRC|INDEX` starts and never finishes; and the same
     * three logs say CMD1 and CMD2 - the ladder's other two response-demanding words - were never
     * started at all. The one rule that fits all five words is that a response demand without `INDEX` is
     * declined, and the word this call sends is the test of it.
     *
     * **The gate is rung 19's and rung 20's, and it is deliberately UNCHANGED** - CMD2 was SENT - so the
     * three rungs' logs are comparable at the same place in the same driver order, and a
     * `_nidx_gated = 1` is the same complete answer `_rca_gated = 1` and `_nrsp_gated = 1` are: the value
     * `st_all_send_cid` RETURNS and the value it publishes as `_cid_sent` are one reading with two
     * consumers, so this gate and that cell cannot disagree.
     *
     * **Why neither of the two variants runs first in the same boot.** Rung 19's own press measured that
     * its CMD3 left `CMD_INHIBIT` set at the end of a 1.2 s window, so a second command in the same boot
     * would meet the driver's own inhibit guard inside `st_send_command` and either wait out its 10 ms
     * bound or be refused - reading the first command's in-flight state rather than the second's flag
     * word. One command, one flag word, one answer. The source compiles the two bodies for one value
     * each, and `build_entry.sh` refuses an image that carries both.
     */
    if (cid_sent != 0u) {
        ST_LIVE("xnu_live_storage_nidx_gated", 0u);
        st_cmd3_noidx(int_enable);
    } else {
        ST_LIVE("xnu_live_storage_nidx_gated", 1u);
        ST_LIVE("xnu_live_storage_nidx_gated_reason", 1u);
    }
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 34
    /*
     * **812: rung 35's call, and it is placed where the driver places it.**
     *
     * `mmc.c:1420`'s `mmc_send_csd(card, card->raw_csd)` is the statement IMMEDIATELY after
     * `mmc.c:1409`'s `mmc_set_relative_addr(card)`, so this call sits directly below rung 21's CMD3
     * and above nothing else: the ladder's own order and the driver's are the same order here, which
     * is what makes one press read both commands as a sequence rather than as two experiments.
     *
     * **The gate is rung 19's, rung 20's and rung 21's, and it is deliberately UNCHANGED** - CMD2 was
     * SENT. The driver has no gate at all, and this ladder keeps the weakest one that is still a
     * reading for the reason rung 19's own comment gives: an arm whose CMD2 never reached
     * `COMMAND 0x0e` should not spend its CMD9 on a bus whose first command never went out, and the
     * refusal is a published cell rather than an absent block.
     *
     * **AND THE PRECONDITION THIS CALL CANNOT SEE IS TAKEN INSIDE THE BODY.** Whether CMD3 was
     * ACCEPTED - whether the card left IDENT for STBY - is not gateable from here, because
     * `st_cmd3_noidx` returns nothing and no arm of this ladder has ever had a completed CMD3. Rather
     * than assume it, `st_send_csd` reads the card's own status word between the two commands and
     * publishes it (`_csd_pre_state`, `_csd_pre_ready`, `_csd_pre_illegal`), so a press that finds
     * the card in IDENT says so in the same log that reports what CMD9 did with it. **A gate that
     * could not be honest about its own precondition would be worse than no gate**: it would turn
     * "the card is not ready for this command" into "the command failed", which is exactly the
     * conflation 804's gate repair was about one rung down.
     */
    if (cid_sent != 0u) {
        ST_LIVE("xnu_live_storage_csd_gated", 0u);
        st_send_csd(int_enable);
    } else {
        ST_LIVE("xnu_live_storage_csd_gated", 1u);
        ST_LIVE("xnu_live_storage_csd_gated_reason", 1u);
    }
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 35
    /*
     * **819: rung 36's caller, and it is the ladder's FIRST UNGATED COMMAND - deliberately.**
     *
     * Every command above this one carries a gate, and each gate is a statement about *the command
     * below it*: CMD2 on `cid_sent`, CMD9 on the same value, CMD3 on CMD2 having reached
     * `COMMAND 0x0e`. **CMD7's precondition is none of those.** It needs the card in STBY, which is
     * a fact about what the CARD made of CMD3 - and this ladder has no cell that measures it: the
     * register read before CMD7 holds the last 32 bits of the CSD (`_csd_resp_short = 0xef8a4040`),
     * which is not an R1 and carries no `R1_CURRENT_STATE`. **A gate keyed on anything this file
     * can read would therefore be a gate on the wrong quantity** - the m812/m819 defect class
     * (`[[mi4-one-value-two-definitions]]`) installed as a control-flow decision, which is strictly
     * worse than the same mistake in an outcome table. So the command goes out unconditionally and
     * the reading is its own R1: `_sel_state` from the card and `_sel_illegal` for the refusal, both
     * published whatever they say.
     *
     * **Is it safe to send ungated? Yes, and by what the command IS.** CMD7 is `ac [31:16] RCA R1`
     * (`mmc.h:36`): no data phase, no storage write, no register this image can reach - one 48-bit
     * command frame and one 48-bit response on CMD, the same shape as the CMD3 four lines up that
     * this ladder already sends on every press. It cannot write to the phone, and it cannot brick
     * it: the two risks this project's gate names are a bad write and a lost bootloader, and CMD7 is
     * neither. **The device-safety argument is not "the command is harmless because a rung says so";
     * it is that the whole command path this rung extends already runs, and this adds one frame to
     * it.**
     *
     * **`_sel_pre_cid_sent` is published anyway, and it is not a gate.** A reader comparing this
     * press against the ones above needs to know whether CMD2 went out in the same boot; publishing
     * the value makes that a reading instead of an inference from a rung number - and it is the
     * value the block below WOULD have gated on, which is why it is named rather than dropped.
     */
    ST_LIVE("xnu_live_storage_sel_pre_cid_sent", cid_sent);
    st_select_card(int_enable);
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 36
    /*
     * **822: rung 37's caller, and the second UNGATED one - for a reason that is the opposite of
     * CMD7's.** CMD7 went out ungated because this file had no cell that measured its precondition.
     * **This one goes out ungated because its PRECONDITION IS THE MEASUREMENT.** CMD13 reads the
     * card's status without changing it, and the whole point of the rung is the state field it
     * reports: a gate reading `_sta_pre_state` and refusing when the card is not in TRAN would
     * discard exactly the reading the arm exists for - the press in which CMD7 did not move the card.
     * `_sta_pre_state` is published from inside the body instead, as a READING, and the state the
     * command produces is `_sta_state` beside it. **A gate here would be a filter on the answer.**
     *
     * **AND IT IS SAFE for the same reason CMD7 was, one step stronger.** `ac [31:16] RCA R1`
     * (`mmc.h:42`): no data phase, no storage write, no register this image can reach - and unlike
     * CMD7 it is a command the card is required to answer in every state it can be addressed in, so
     * even a refusal comes back as an R1 with `ILLEGAL_COMMAND` set rather than as a hang. **One more
     * 48-bit frame on CMD, addressed to the same RCA as the two commands above it.** The block's
     * `TIMEOUT_CONTROL`, which is still `0x00` after this press, is read and not written.
     *
     * **It sits immediately below CMD7 and above nothing**, so one press reads CMD3, CMD9, CMD7 and
     * CMD13 as a sequence in one boot - which is what makes `_nidx_state`, `_csd_pre_state`,
     * `_sel_state` and `_sta_state` four readings of one field on one chain rather than four
     * experiments.
     */
    st_send_status(int_enable);
#endif

#if STAGE90_XNU_STORAGE_PROBE >= 37
    /*
     * **824: rung 38's caller - `mmc.c:1447`'s `mmc_get_ext_csd`, the driver's own next statement
     * after the `mmc_select_card` five lines above.**
     *
     * **IT IS ONE `bl`, AND THE GATE IS INSIDE.** Rung 33's pair of bodies (`st_set_op_cond`) is the
     * shape and this is the same reason one rung up: `st_cmd_path`'s own device surface is asserted
     * EXACTLY by its clause, and every register this rung adds - `TIMEOUT_CONTROL` written,
     * `BLOCK_SIZE`, `BLOCK_COUNT`, `TRANSFER_MODE`, the data port - would move it. **A caller that
     * adds one `bl` and no access leaves every clause above this one true rather than relaxed.**
     *
     * **AND THE GATE GOES WITH IT**, for the same reason: the precondition is a reading of
     * `RESPONSE 0x10` - CMD13's own R1, still in the register - and that register read belongs to the
     * body whose clause asserts its surface. The body publishes the refusal and its reason; the
     * caller does not decide.
     *
     * **A data command is the first command in this ladder that must NOT go out unconditionally.**
     * CMD7 went ungated because this file had no cell for its precondition; CMD13 went ungated
     * because its precondition WAS the measurement. CMD8's precondition - the card is in TRAN - is a
     * value this ladder MEASURES in the same boot, three commands earlier, and the standard does not
     * define a card's answer to a data command outside it. **The refusal is cheap and the wrong
     * question is not**: 512 bytes of an answer to a command the card was not required to understand
     * would be decoded into four fields and reported as a card type.
     *
     * **AND THE GATED CELL IS READ AS "THE PHASE DID NOT RUN", NOT AS "THE ARM FAILED".**
     * `_ext_gated = 1` beside `_ext_calls = 1` is a complete reading of this rung: the precondition
     * was sampled, it was not 4, and nothing below it was touched. The next arm in that case is rung
     * 37 again - and the two cells that say which arm that is are already in the same log.
     */
    st_send_ext_csd(int_enable);
#endif

    ST_LIVE("xnu_live_storage_cmd_done", 1u);
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 11 - one function, one caller, and the first command on the bus */


void entry_storage_probe(void)
{
    uint32_t slot_before = 0u, desc = 0u, mapped, section, hc_section;
    uint32_t gcc_mapped, gcc_slot_before = 0u, gcc_desc = 0u, gcc_section;
    uint32_t bcr, cbcr, gate;
    uint32_t core_power, mci_data_ctrl, mci_version, hc_mode, hci_version, capabilities;
    uint32_t loads = 0u;

    if (g_storage_probed != 0u)
        return;
    g_storage_probed = 1u;

    /*
     * **The addresses before they are dereferenced.** 532 section 3.1's arithmetic, published as
     * numbers: the index the one install covers, and the index the *other* window falls in. They are
     * equal on this device and they are two keys rather than one because a device whose two windows
     * straddled a 1 MB boundary would make them differ - and nothing in the installer would say so
     * (532 section 3.3). **`_gcc_section` is the third**, and it is the one 692's press made
     * load-bearing: it is the index of the GATE's megabyte, and `0xfc4ab000` - the reset path's
     * PS_HOLD store - is in it too.
     */
    section = ST_CORE_MEM_BASE >> 20;
    hc_section = ST_HC_MEM_BASE >> 20;
    gcc_section = ST_GCC_BASE >> 20;

    ST_LIVE("xnu_live_storage_calls", 1u);
    ST_LIVE("xnu_live_storage_live_state", g_live_state);
    ST_LIVE("xnu_live_storage_section", section);
    ST_LIVE("xnu_live_storage_hc_mem_section", hc_section);
    ST_LIVE("xnu_live_storage_windows_share_section", (section == hc_section) ? 1u : 0u);
    ST_LIVE("xnu_live_storage_gcc_section", gcc_section);
    ST_LIVE("xnu_live_storage_gcc_share_section", (gcc_section == section) ? 1u : 0u);

    /*
     * **The GATE's own megabyte, installed first, and it is 692's whole correction.** The read below
     * is the probe's third line and it is the load that faulted on 692's press: `0xFC4` is in no table
     * this image builds, and the claim that a store elsewhere in the same megabyte proved it mapped is
     * the defect this install removes. Its own four numbers are published under `_gcc_*` so the two
     * installs are never confused for one another - `_map`/`_slot_before`/`_desc` belong to the
     * storage block below and stay that way.
     *
     * And when THIS install refuses, the gate is not read either, by the same rule the storage block
     * gets: a load through a translation this arm cannot vouch for is a fault, not a measurement. 692
     * showed such a fault comes back with a log, so the rule is not about surviving it - it is about
     * not spending the arm's one reading on an address whose descriptor this run did not write.
     */
    gcc_mapped = entry_mmio_section(ST_GCC_BASE, ST_GCC_BASE, &gcc_slot_before, &gcc_desc);
    ST_LIVE("xnu_live_storage_gcc_map", gcc_mapped);
    ST_LIVE("xnu_live_storage_gcc_slot_before", gcc_slot_before);
    ST_LIVE("xnu_live_storage_gcc_desc", gcc_desc);

    if (gcc_mapped == 0u) {
        ST_LIVE("xnu_live_storage_gate_read", 0u);
        ST_LIVE("xnu_live_storage_gated_out", 0u);
        ST_LIVE("xnu_live_storage_loads", 0u);
        ST_LIVE("xnu_live_storage_writes", 0u);
        return;
    }

    /*
     * **The storage block's install, and it is one call.** 532 section 3.2: a second call for `hc_mem`
     * would find the slot already holding a block descriptor, so `entry_section_install` returns 0
     * without writing and the arm would report a mapping failure *after* mapping the controller
     * correctly. One call, both windows, and the mistake is loud rather than silent.
     *
     * **It is placed before the gate read and not after it**, which is the one ordering change 692's
     * measurement forced - and the safety property is untouched by it, because an install is a
     * *page-table* write and not an access to the medium's controller. The gate still guards every
     * load of the block below; what the reordering buys is that a run which ends up gated out still
     * publishes the four numbers that say whether the section was installed, so "the block was not
     * touched" and "the block could not be reached" stay two different cells.
     */
    mapped = entry_mmio_section(ST_CORE_MEM_BASE, ST_CORE_MEM_BASE, &slot_before, &desc);
    ST_LIVE("xnu_live_storage_map", mapped);
    ST_LIVE("xnu_live_storage_slot_before", slot_before);
    ST_LIVE("xnu_live_storage_desc", desc);
    ST_LIVE("xnu_live_storage_l1", g_live_mmio_l1);
    ST_LIVE("xnu_live_storage_l1_moved", g_live_mmio_l1_moved);
    ST_LIVE("xnu_live_storage_ttbr0", g_live_mmio_ttbr0);
    ST_LIVE("xnu_live_storage_ttbr1", g_live_mmio_ttbr1);

    /*
     * **The gate, out of the megabyte this arm installed first.** `BIT(0)` of the CBCR is the branch
     * enable; `BCR` itself is read beside it because the pair is what makes "this offset is the
     * register I think it is" checkable rather than assumed. `_gate_read` is published as 1 so that a
     * run whose gate could not be reached cannot be confused with one whose gate was read and found
     * closed - and `_gcc_map = 0` above is the only way `_gate_read` is 0.
     */
    bcr = st_read32(ST_GCC_BASE + ST_SDCC1_BCR);
    cbcr = st_read32(ST_GCC_BASE + ST_SDCC1_CBCR);
    gate = cbcr & ST_SDCC1_CLK_ENABLE;

    ST_LIVE("xnu_live_storage_gate_read", 1u);
    ST_LIVE("xnu_live_storage_bcr", bcr);
    ST_LIVE("xnu_live_storage_cbcr", cbcr);
    ST_LIVE("xnu_live_storage_gate", gate);

    /*
     * Two ways to reach the same cell - no section, or a section and a closed branch - and the two
     * keys that tell them apart are `_map` and `_gate`. Neither reads a register of the block.
     */
    if (mapped == 0u) {
        ST_LIVE("xnu_live_storage_gated_out", 0u);
        ST_LIVE("xnu_live_storage_loads", 0u);
        ST_LIVE("xnu_live_storage_writes", 0u);
        return;
    }

    /*
     * The interlock, and the arm's own statement of which side of it the run is on. `gated_out` is 1
     * only when the gate was readable and closed; a run with `gated_out = 1` and `loads = 0` says the
     * section is installed and the block was not touched, which is the whole of that cell.
     */
    if (gate == 0u) {
        ST_LIVE("xnu_live_storage_gated_out", 1u);
        ST_LIVE("xnu_live_storage_loads", 0u);
        ST_LIVE("xnu_live_storage_writes", 0u);
        return;
    }

    ST_LIVE("xnu_live_storage_gated_out", 0u);

    /*
     * **Six loads, in the order 532 section 6 states, and every one of them a read.** `CORE_POWER`'s
     * bit 7 is `CORE_SW_RST` and it is published as its own key because "the core is held in reset" and
     * "the core answers a version word" are two different machines and a reader should not have to
     * shift a word to tell them apart - the same reason the mode bit gets one.
     *
     * **698: and the six is counted rather than written down.** It was the literal `6u` until this step,
     * which is a claim about this block that only a reader could check; `loads` is incremented at each
     * read and published once, so the record's "six loads" and the log's number are two derivations of
     * one fact (m688: a table of things-to-count is a scope claim unless the counter is the table's own).
     * The later blocks publish their own counts for the same reason - `_reg_loads` at rung 3 - and the
     * three numbers are per-block on purpose: one number for the whole function would hide which block
     * grew.
     */
    loads = 0u;
    core_power = st_read32(ST_CORE_MEM_BASE + ST_CORE_POWER);
    loads++;
    mci_data_ctrl = st_read32(ST_CORE_MEM_BASE + ST_CORE_MCI_DATA_CTRL);
    loads++;
    mci_version = st_read32(ST_CORE_MEM_BASE + ST_CORE_MCI_VERSION);
    loads++;
    hc_mode = st_read32(ST_CORE_MEM_BASE + ST_CORE_HC_MODE);
    loads++;
    hci_version = st_read32(ST_HC_MEM_BASE + ST_SDHCI_DMA_ADDRESS);
    loads++;
    capabilities = st_read32(ST_HC_MEM_BASE + ST_SDHCI_CAPABILITIES);
    loads++;

    ST_LIVE("xnu_live_storage_loads", loads);
    ST_LIVE("xnu_live_storage_writes", 0u);
    ST_LIVE("xnu_live_storage_core_power", core_power);
    ST_LIVE("xnu_live_storage_mci_data_ctrl", mci_data_ctrl);
    ST_LIVE("xnu_live_storage_mci_version", mci_version);
    ST_LIVE("xnu_live_storage_hc_mode", hc_mode);
    ST_LIVE("xnu_live_storage_dma_address", hci_version);
    ST_LIVE("xnu_live_storage_capabilities", capabilities);
    /*
     * **The two bits this arm exists for.** 531 section 6 asked whether the mode sequence is a
     * *prerequisite* or a *re-do*: `xnu_live_storage_mode_bit` reads `CORE_HC_MODE & HC_MODE_EN`, so 1
     * says the block the payload inherited is already in SDHCI mode and 0 says the vendor's first two
     * writes are still ahead of it. And `xnu_live_storage_sw_rst` reads `CORE_POWER & CORE_SW_RST`, so 1
     * says the core is being held in software reset by whatever ran before - which would make the
     * version word's answer meaningless rather than absent.
     */
    ST_LIVE("xnu_live_storage_sw_rst", (core_power >> 7) & 1u);
    ST_LIVE("xnu_live_storage_mode_bit", hc_mode & ST_HC_MODE_EN);

#if STAGE90_XNU_STORAGE_PROBE >= 2
    /*
     * **696: rung 2, and it is placed here for the same reason the probe itself is placed before the
     * clock.** Everything above is a reading this sequence is conditional on - the gate that decides
     * whether the block may be touched at all, the version word the sequence refuses itself on, and the
     * two words the sequence's own stores are read against - so if the sequence is reached at all, every
     * one of those is already published and a run that dies inside the sequence still carries the state
     * it was entered in. `_writes` is republished by the sequence with its true count, which is why the
     * `0` above is a statement about the read-only path and not about this one.
     */
    st_mode_sequence(core_power, mci_version);
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 3
    /*
     * **698: rung 3, and it is placed last because it is the only block whose subject is the state the
     * rungs above left.** The mode sequence's four stores are what makes the standard register file mean
     * anything (694 section 3), so a census taken before them would be a census of a block that is not
     * in SDHCI mode - which is exactly the reading 694 could already give. Taken here, it is the state a
     * driver would find on this boot, and it is the before-value for the reset the next step performs.
     *
     * It is guarded by the sequence having run: `_mode_stage == 8` and `_writes == 4` are the two keys
     * that say so, and a run that refused itself (`_mode_refused = 1`, the block not answering) must
     * **not** have its register file read, because a block that did not answer a version word is a block
     * whose census would be a census of nothing. That is the same interlock the sequence uses, applied
     * one rung up, and it is the only condition under which this rung does not run.
     */
    if (g_storage_mode_complete != 0u)
        st_standard_census();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 4
    /*
     * **701: rung 4, and it is placed after the census because the census is its before-values.** The
     * reset's effect on `SOFTWARE_RESET`, `POWER_CONTROL`, `PRESENT_STATE` and `HOST_CONTROL` can only be
     * read as a *change* if those four were read first, which is why 698 was a rung of reads before a
     * rung of writes - and it is guarded by the same interlock one rung up, `g_storage_mode_complete`,
     * because a block that did not answer a version word is a block whose reset must not be written: the
     * census would not have run, so the before-values would be absent and the reset would be an act on a
     * register file this image has never read.
     */
    if (g_storage_mode_complete != 0u)
        st_driver_reset();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 5
    /*
     * **704: rung 5, and it is placed after the reset for the same reason the reset is placed after the
     * census.** The clock surface is the *next* act's before-values (`sdhci_msm_set_clock` is what writes
     * these registers), and a before-value can only be taken before - so a run that dies in the write rung
     * still carries what the block's clock tree looked like on a boot nobody had touched. It is guarded by
     * the same `g_storage_mode_complete` interlock every rung above 2 uses: a block that did not answer a
     * version word is a block whose clock tree must not be read either, because the same branch gates
     * both. The rung writes nothing at any path, so the guard is about *meaning* and not about safety.
     */
    if (g_storage_mode_complete != 0u)
        st_clock_census();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 6
    /*
     * **706: rung 6, the first rung that writes the clock controller, and it is placed last for the
     * reason every rung above 2 is guarded**: `g_storage_mode_complete` says the block answered a version
     * word, so the four branch enables and the MMC clock are written only into a controller this image has
     * already read. The three things it does NOT write are the ones that matter - the RCG (a rate is not
     * on this path, section 2), `BCR 0x04C0` (block reset) and `POWER_CONTROL 0x29` (where the driver's
     * own power path writes 0, a bus-off request) - and the build's clauses are what keep all three out
     * of the linked image rather than this comment.
     */
    if (g_storage_mode_complete != 0u)
        st_clock_set();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 8
    /*
     * **710: rung 8's own half, and it runs BEFORE rung 7's call - the first rung that does not simply
     * append.** The registration and the arming are this image's own table and four distributor words,
     * so no cell any earlier rung reads moves; what the order buys is that the power byte cannot be
     * sent into a line owned by nobody, which is rung 7's press. See `st_pwr_irq_arm`'s comment and
     * experiment-710 section 3 for the argument in full.
     */
    if (g_storage_mode_complete != 0u)
        st_pwr_irq_arm();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 7
    /*
     * **708: rung 7 - PASS A's act, appended after rung 6's PASS B act.** The same interlock, and the
     * one register this rung writes is `POWER_CONTROL 0x29`: the byte is derived from the CAPABILITIES
     * register (`_pwr_cap`), the value is `SDHCI_POWER_180 | SDHCI_POWER_ON`, and the arm refuses rather
     * than writing one whose voltage bits have no entry in the driver's own map (`_pwr_refused`). What
     * it deliberately does NOT do is wait: `sdhci_msm_check_power_status(REQ_BUS_ON)` would block on a
     * completion only this SoC's power IRQ completes, and this image delivers no IRQ - section 1.3 of
     * experiment-708. The status register is read and left latched instead.
     */
    if (g_storage_mode_complete != 0u)
        st_power_set();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 9
    /*
     * **712: rung 9's own statement, and the first block of this ladder that is not the last one.**
     * `sdhci.c:1370-1372` is the byte and the check, adjacent statements of one function: the wait
     * runs immediately after `st_power_set()` and **before** rung 8's tail count, because an arm
     * that ran it anywhere else would be measuring its own order rather than the driver's. The
     * consequence for the rung above is deliberate and small: `_pwr_irq_calls_probe_end` still means
     * "at the probe's own tail", and the tail is now after the wait - so that pair reads the same way
     * it did and gains a second reading beside it (`_wait_calls`, at the wait's own end). See
     * experiment-712 section 3.
     */
    if (g_storage_mode_complete != 0u)
        st_pwr_wait();
#endif
#if STAGE90_XNU_STORAGE_PROBE == 15
    /*
     * **AND IT IS COMPILED FOR THE VALUE 15 AND NO OTHER**, because what it leaves in `0x34` refuses
     * `st_cmd_path`'s gate on every rung above it (736 measured it; see the body's own comment).
     * **734: rung 15's own statement, and it takes the ONE position this ladder has never used.** It runs
     * immediately BEFORE `st_cmd_path()`, which is the last moment at which the block is in SDHCI mode,
     * powered and clocked AND has never carried a command - the three conditions this rung's question
     * needs, and the reason it is not inside `st_cmd_path` beside rung 13's and rung 14's stores (those
     * two run at a moment when a command has already completed, which is exactly the state that cannot
     * tell mechanism (A) from mechanism (B); see the body's own comment). **It runs after rung 9's wait
     * and not before it**: the wait is what leaves the block quiescent, and a store taken while the
     * power-up sequence was still in flight would be a different experiment with the same key names.
     */
    if (g_storage_mode_complete != 0u)
        st_quiet_enable_probe();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 17
    /*
     * **739: rung 18's own statement, and it takes the SAME position rung 15 used - immediately BEFORE
     * `st_cmd_path()`, after rung 9's wait.** The position is the experiment and it is the only one that
     * answers this rung's question: above this point the block is not yet quiesced (rungs 6-9 are still
     * running, and rung 8's arming is immediately above), and below it a command has been put on the bus
     * and `RESPONSE 0x10` is answering a command instead of describing a block. Only rung 3's census,
     * rung 5's clock census and rung 12's register census are read-only bodies of comparable cost, and
     * none of the three reads this register.
     *
     * **It is READ-ONLY, so 736's ordering rule does not apply to it and the reason is worth stating
     * rather than leaving to be re-derived**: 736's press cost a press because rung 15's store to
     * `INT_ENABLE 0x34` above `st_cmd_path`'s gate left bit 15 set and the gate then refused the whole
     * command path. This body stores nothing anywhere, so it cannot move any register the gate reads, and
     * the build's own clause holds its store set to EMPTY rather than this comment promising it.
     */
    if (g_storage_mode_complete != 0u)
        st_resp_before();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 21
    /*
     * **756 section 6's census, and it takes the SAME position rung 18's does - immediately BEFORE
     * `st_cmd_path()`, after rung 9's wait.** The position is the experiment for the reason 739 gave:
     * above this point the block is not yet quiesced (rungs 6-9 are still running), and below it a
     * command has been put on the bus - and `HOST_CONTROL2` and the DLL words are what the vendor's own
     * bring-up sets during power-up, so they must be read after that and before anything this ladder
     * sends. **The rung-21 command still runs below**, because rung 21's call site is guarded `>= 20`
     * and this rung changes no guard that is already pressed: the log therefore carries the DLL cells
     * taken before the first command beside a second sample of the `0x030A` stall, which costs no
     * reading and is the reason this rung is purely additive.
     *
     * It is READ-ONLY, so 736's ordering rule does not apply to it and the reason is the one 739 wrote
     * for rung 18: a body that stores nothing cannot move a register `st_cmd_path`'s gate reads. The
     * build's own clause holds this body's store set to EMPTY rather than this comment promising it.
     */
    if (g_storage_mode_complete != 0u)
        st_dll_census();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 24
    /*
     * **769: rung 25's call site, and it takes the position rung 21's and rung 18's do - immediately
     * BEFORE `st_cmd_path()`, after rung 9's wait.** The position is well-defined for this rung but it
     * is not the experiment (as it was for rung 18): this body reads a **different device** - the TLMM,
     * not the controller - so no command this ladder sends can change it, and no register
     * `st_cmd_path`'s gate reads can be moved by it. That is also why it does not matter which side of
     * the other pre-command censuses it sits on: it shares no address with them.
     *
     * It is READ-ONLY, so 736's ordering rule does not apply and the reason is 739's: a body that
     * stores nothing cannot move a register the gate reads. The build's own clause holds this body's
     * store set to EMPTY rather than this comment promising it.
     */
    if (g_storage_mode_complete != 0u)
        st_pad_census();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 11
    /*
     * **721: rung 11's own statement, and the SECOND block of this ladder that is not the last one.**
     * The driver's own order is what places it here: `mmc_power_up`'s byte and its `check_power_status`
     * happen in `sdhci_do_set_ios` (`sdhci.c:1658-1672`) and the commands happen later, in the rescan
     * - so the command path runs after rung 9's wait and before rung 8's tail count, exactly as the
     * wait does. The consequence for the cell above is the one rung 9 already documented: 
     * `_pwr_irq_calls_probe_end` still means "at the probe's own tail", and the tail is now after the
     * command path as well - so on this arm that cell is read after up to 1.2 s of command polling
     * rather than after 73 us of wait, and the pair it forms with `_pwr_irq_calls` is unchanged.
     */
    if (g_storage_mode_complete != 0u)
        st_cmd_path();
#endif
#if STAGE90_XNU_STORAGE_PROBE >= 8
    /*
     * **710: the count at the probe's own end.** Rung 7's device act is the last one; this is a read
     * of this rung's own counter, and the pair (`_pwr_irq_calls_probe_end` here beside
     * `_pwr_irq_calls` in the handler) is what says whether the line arrives during the probe or after
     * it - a question 709's press left unmeasured because it ended on the interrupt.
     */
    if (g_storage_mode_complete != 0u)
        st_pwr_irq_after();
#endif
}
