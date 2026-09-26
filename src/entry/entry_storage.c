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
#if STAGE90_XNU_STORAGE_PROBE < 0 || STAGE90_XNU_STORAGE_PROBE > 16
#error "STAGE90_XNU_STORAGE_PROBE is a rung: 0 = inert, 1 = the read-only probe, 2 = the probe and the vendor's mode sequence (four stores to the controller), 3 = 2 plus the standard register file's census (ten reads, no store), 4 = 3 plus the driver's own SDHCI_RESET_ALL (ONE byte store to SOFTWARE_RESET 0x2F, plus a bounded poll of that same byte) - the rung that writes through hc_mem for the first time - 5 = 4 plus the CLOCK SURFACE read at its own widths (the GCC's four SDCC1 branches and the apps root's five RCG words, plus CORE_VENDOR_SPEC 0x10C), a rung of reads that stores NOTHING anywhere, and 6 = 5 plus THE DRIVER'S FIRST CLOCK SET (sdhci_msm_set_clock at 400 kHz): four CBCR read-modify-writes of BIT(0) on the GCC each followed by a bounded halt check, two CORE_VENDOR_SPEC 0x10C read-modify-writes (MCLK select <- DFLT, HC_SELECT_IN cleared), and the standard's two CLOCK_CONTROL halfwords with the stability poll between them - SIX stores and NO rate, because sup_clock == msm_host->clk_rate on the first call (experiment-706 section 2) - and it writes neither BCR 0x04C0 nor any RCG word, nor POWER_CONTROL 0x29, and 7 = 6 plus THE DRIVER'S OWN FIRST POWER BYTE (mmc_power_up's PASS A: sdhi_set_power at sdhci.c:1663 - ONE 8-bit store to POWER_CONTROL 0x29, the value derived from the CAPABILITIES register the way sdhci_add_host and mmc_power_up derive it), with the vendor's REQ_BUS_ON wait NOT taken (sdhci-msm.c:2179-2209 would wait_for_completion on an IRQ this image cannot deliver) - a rung of one store and five readings, and 8 = 7 plus THE DRIVER'S OWN POWER IRQ (the vendor's sdhci_msm_pwr_irq, sdhci-msm.c:1990-2099, as a CLIENT of this image's own dispatcher: intid 170 - SPI 138, the `pwr_irq` msm8974.dtsi:502 declares - registered and its line enabled BEFORE the power byte, with the three arms that may sleep absent because the vendor's own IRQF_ONESHOT+NULL-primary declares a threaded handler and this image has no thread to sleep in), clearing the latch the byte latched and answering the controller - a rung of three stores, in core_mem and in hc_mem, and the two addresses of VENDOR_SPEC 0x10C read side by side, and 9 = 8 plus THE DRIVER'S OWN COMPLETION (sdhci_set_power's own next statement, sdhci.c:1371-1372: check_power_status(host, REQ_BUS_ON), whose sdhci_msm_check_power_status at sdhci-msm.c:2179 compares the request against the TWO DRIVER-SIDE FIELDS the handler's tail writes - curr_pwr_state/curr_io_level, :2092-2096 - and then blocks; rung 9 ports the predicate and replaces the block with a BOUNDED tick poll whose end condition is the CONTROLLER's own CORE_PWRCTL_CTL bit BUS_SUCCESS, because the probe runs with SCTLR.C clear while the handler may run with the caches on, so an image-side flag written by one can be invisible to the other - the flag is still written and published beside the device ack, and the pair is this rung's new cell), with the budget STAGE90_XNU_PWR_WAIT_TICKS and the three sleeping arms still absent, and 10 = 9 plus THE SAME WAIT TAKEN WITH THE MASK OFF: rung 10 measured that the completion arrives within 20 ms of the byte and that the handler is delivered the moment the payload's own idle-exit code lifts `I` (experiment-717), so the mask - not the device and not any budget - is what the poll was measuring; this rung saves the CPSR, clears `I`, runs the SAME bounded poll, restores the saved value and publishes both the CPSR the poll ran under (`_wait_cpsr`, which must now read `0x80000013`) and the state it leaves behind (`_wait_cpsr_after`) - ONE new cell, NO new device access and NO new store, and the first interrupt this image takes inside the cache-off idle-exit window and the first time the handler's two driver-side fields are read back after the handler wrote them. and 11 = 10 plus THE DRIVER'S OWN FIRST COMMAND (sdhci_send_command, sdhci.c:1076-1155: the bounded wait for SDHCI_CMD_INHIBIT to clear, then ARGUMENT 0x08 and COMMAND 0x0E, with the completion taken as a BOUNDED poll of the CONTROLLER's own SDHCI_INT_RESPONSE bit because this image enables no SDHCI interrupt - the block's hc_irq is SPI 123 -> intid 155, a line nobody here owns, and a delivery would end the run at the dispatcher) for the driver's own first two commands, mmc_go_idle's CMD0 (opcode 0, argument 0, no response: the word 0x0000) and mmc_attach_mmc's CMD1 (opcode 1, argument 0, MMC_RSP_R3: the word 0x0102), with the response read out of RESPONSE 0x10 - the first act of this line that addresses the CARD rather than the controller, the first 32-bit write-1-to-clear to INT_STATUS 0x30, and NO data-path register, NO POWER_CONTROL, NO GCC word, NO core_mem word, and no byte of the medium, and 12 = 11 plus THE REGISTER STATE AT THE INSTANT OF THE COMMAND AND THE COMMAND'S OWN RETURN PATH (experiment-724): a new READ-ONLY census body re-takes - at the command's own moment rather than at rungs 3/4/5/6/11's - the POWER_CONTROL 0x29 byte (723 section 5 corrects 723 section 3: rung 7 writes it and it takes, `_pwr_before 0x00 -> _pwr_after 0x0b`, and what differs from the driver's 0x0F is the VOLTAGE field), CLOCK_CONTROL 0x2C's three bits, PRESENT_STATE 0x24, INT_ENABLE 0x34 and SIGNAL_ENABLE 0x38, and the GCC's SDCC1_APPS_RCG (CMD_RCGR decoded into root_en/root_status/update and CFG_RCGR into src/div/mnd_mode - rung 5's own 0x00000507 says SRC_SEL 5, DIV 7, root_status clear, so the root is ENABLED and the vendor's own pre-divider makes the SDCC1 apps clock 200 MHz, not the 384 MHz ST_SET_MAX_CLK names, and the arm's divider of 480 delivers 208 kHz rather than 400), SDCC1 apps/AHB CBCR and the BCR - and refuses the command if the bus-power bit is clear, which is the one condition rung 11's gate does not check; and the command body gains `_cmdN_word_read` (COMMAND 0x0E read back - the block's own copy of the word, which an absence of interrupt cannot supply), `_cmdN_inhibit_after`/`_inhibit_seen`/`_inhibit_last` (PRESENT_STATE's CMD_INHIBIT sampled immediately after the store and over the first 1024 poll iterations, which is what separates NEVER STARTED from RAN from IN FLIGHT), `_cmdN_status_any`/`_any_polls` (the FIRST non-zero INT_STATUS OF ANY KIND, beside rung 11's narrower poll, so that 'nothing latched' becomes 'exactly this bit latched' if the block said something other than RESPONSE), `_cmdN_resp_read` (1, the companion that makes a zero RESPONSE a reading rather than a silence) and an UNCONDITIONAL RESPONSE 0x10 read - so rung 12 makes NO store anywhere, moves no gate, and every clause of rung 11 stands over it unchanged, and 13 = 12 plus WHERE THIS CONTROLLER REPORTS A COMPLETION (experiment-729): a new body of its own - the first device store this ladder declares OUTSIDE `st_send_command`'s window since rung 7, and the first store to an interrupt-enable register anywhere in this image - which reads the registers a completion could be hiding in (`SLOT_INT_STATUS 0xFC` read as a halfword, `CORE_PWRCTL_STATUS 0xDC`, `COMMAND 0x0E` read back as a halfword, `PRESENT_STATE 0x24`), then writes ONE bit of `INT_ENABLE 0x34` (`SDHCI_INT_RESPONSE`, `0x00000001`) with `SIGNAL_ENABLE 0x38` left at ZERO, reads `INT_STATUS 0x30` immediately, reads `INT_ENABLE` back (the block's own copy of the enable, without which a zero status has two producers and a hardware answer cannot be told from a store that never took), writes `INT_ENABLE` back to zero and reads THAT back, and makes NO new command, no store to `POWER_CONTROL 0x29`, no GCC word, no core_mem write and no byte of the medium; and it runs at the one point in `st_cmd_path` this machine has ever reached, immediately after CMD0's publishes and BEFORE the gate, because rung 13's own log says `_cmd_gated = 1` and a block placed after the gate would sit on a path no press has taken. And 14 = 13 plus THE COMMAND PATH WITH THE ENABLE SET (experiment-732): the same two commands, the same gates and the same bodies, with **ONE 32-bit store to `INT_ENABLE 0x34` (`SDHCI_INT_RESPONSE`) taken immediately before CMD0 is put on the bus, and its restore taken on ONE unconditional line immediately after CMD0's publishes** - `SIGNAL_ENABLE 0x38` still ZERO, which is the one thing 730's press measured is safe, and the enable is therefore open for exactly CMD0's own send and its poll. The reason is 730's own answer: `_int_status_after = 0x00000001` against `_int_status_before = 0x00000000` with exactly one store between them, so **`_cmd_gated = 1` was the MASKED POLL's answer and not the block's** - rungs 11, 12 and 13 read a status register whose enable nothing in this image had ever set while a command was in flight. **This rung's FIRST BUILD put the same store after CMD0 instead of before it, and it was refuted without spending a press**: `st_send_command`'s completion poll is inside CMD0, so a window opened below the command leaves `c0.complete == 0` and the between-commands gate refuses - the arm's own promised cell (`_cmd_gated = 0`) was absent by construction, which is m720's shape, an absent key with one of its producers guaranteed. The window now has one entrance and one exit, both unconditional, and every branch of this body (the census's refusal and the register half of the gate above it, the between-commands gate below it) stands outside them. Gate 1 is NARROWED rather than removed: `SIGNAL_ENABLE` must still read zero and `INT_ENABLE`'s only permitted bit is `SDHCI_INT_RESPONSE`, both checked BEFORE the store, and the refusal publishes which of the two fired (`_cmd_gate_kind`: 1 = `SIGNAL_ENABLE` non-zero, 2 = a bit of `INT_ENABLE` other than `RESPONSE`), so a refusal is localised rather than reported as a boolean. The restore publishes its own sequence number, the value written back, the block's readback and `INT_STATUS` read AFTER the disable - and the readback is the cell that says the restore is a PARTIAL one: 730's press measured its own pair as `_int_enable_held = 0x00008001` for a store of `0x00000001` and `_int_enable_readback = 0x00008000` for a store of `0x00000000`, so bit 15 (`SDHCI_INT_ERROR`) is set by a write to 0x34 and is not cleared by one. Three reads 697 and 730 left owed come with it: `HOST_VERSION 0xFE` as a halfword, `INT_STATUS` with the enable set and BEFORE any command - where 0 says the completion bit is latched by the command rather than held by the block - and `INT_STATUS` after the restore. **The cell that says the rung worked is `_cmd_gated = 0` with `_cmd1_*` keys present, and the cell that says the enable was the mask is `_cmd0_complete = 1` beside `_cmd0_status_any != 0`** - the first command this line drives to a completion and the first CMD1 it ever issues. The window is bounded by the two stores and no new write class appears: no new command, no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word and no byte of the medium. **Two spellings of this rung's number are in use and this clause states both**: this ladder counts a rung by the VALUE of `STAGE90_XNU_STORAGE_PROBE`, so the clause above reads 14, while the commit subjects and the pre-registration count ORDINAL ARMS and call it **rung 15** - the two differ by one from value 9 onwards because value 9 has two arms (the second is the same wait with the mask off), and a reader holding both numbers holds one arm. And 15 = 14 plus THE QUIET-BLOCK READ (experiment-734): **the same ONE-bit store to `INT_ENABLE 0x34`, taken for the first time in this ladder at a point where NO COMMAND HAS EVER BEEN SENT** - a body of its own, `st_quiet_enable_probe`, called immediately BEFORE `st_cmd_path()` and after rung 9's wait, on a block that is already in SDHCI mode, powered and clocked, so the only thing that differs from rung 13's and rung 14's store is the absence of any command in the block's past. It reads `INT_STATUS 0x30`, `INT_ENABLE 0x34` and `SIGNAL_ENABLE 0x38` first and publishes all three, writes `INT_ENABLE <- (read | SDHCI_INT_RESPONSE)` and publishes the value it wrote, reads `INT_STATUS` IMMEDIATELY after that store (`_quiet_status_after`, which is this rung's answer), reads the enable back (`_quiet_held`), writes the enable back to its before-value and publishes that, then reads `INT_STATUS` again (`_quiet_status_post`) and the enable again (`_quiet_readback`). `SIGNAL_ENABLE` is READ and NEVER WRITTEN, at any rung. **The reason is 733's own failed prediction and it is a discrimination, not a new act**: 733 pressed rung 14 and measured the pair `_int_status_before = 0x00000000` -> `_int_status_after = 0x00000001` across exactly one store to `0x34`, with `_ena_status_post = 0` and `_int_status_before = 0` both reading the register CLEAR - the same 0 -> 1 transition that 730 read as *the enable revealed a completion*, on a register that had just been read as clear twice. So that pair does not separate (A) a write to 0x34 that makes 0x30's RESPONSE bit read 1 from (B) a completion the block was holding and re-latched when the enable returned - 726's sixth hypothesis, which 733 did not put down. **THE QUIET BLOCK IS WHAT SEPARATES THEM, because (B) requires a completion to have happened**: (B) predicts `_quiet_status_after = 0` here (no command has ever been issued, so there is nothing to re-latch) and (A) predicts `1`. Nothing else on this arm is new: two stores, both to `0x34`, the second restoring the first, on a block that has never carried a command, with no new command, no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word and no byte of the medium. And 16 = 15 *WITH 15'S OWN BODY LEFT OUT* plus THE FIRST 136-BIT RESPONSE (experiment-737): `st_quiet_enable_probe` is compiled for the VALUE 15 and for NO OTHER, because 736's press measured what its store costs the rung above it - a write to `INT_ENABLE 0x34` leaves bit 15 (`SDHCI_INT_ERROR`) set and no write clears it, so `st_cmd_path`'s own gate read `0x00008000` on that arm and REFUSED THE WHOLE COMMAND PATH (`_cmd_gate_kind = 2`, `_cmd_sent = 0`: no command went on the bus and every `_cmd*` key inside the command path was absent). A rung that needs the command path to run cannot carry a write to `0x34` above the gate, and no rung after 15 needs 15's answer, which is in the record. So this arm is the ladder THROUGH 15 WITHOUT 15's body: after rung 14's two commands, and gated on the DRIVER'S OWN condition rather than on a status bit, `st_all_send_cid` opens the SAME one-bit enable window (`INT_ENABLE 0x34 <- read | SDHCI_INT_RESPONSE`, `SIGNAL_ENABLE 0x38` still never written), issues `mmc_all_send_cid`'s CMD2 (opcode 2, argument 0, `MMC_RSP_R2` = PRESENT|136|CRC, which decodes to `SDHCI_CMD_RESP_LONG 0x01 | SDHCI_CMD_CRC 0x08` and the word 0x0209), and reads the 136-bit response THE DRIVER'S OWN WAY - `resp[i] = readl(RESPONSE + (3-i)*4) << 8 | readb(RESPONSE + (3-i)*4 - 1)`, the shift sdhci.c:1164-1172's comment calls *CRC is stripped*, published both as the four shifted words and as the four raw words so a reader can see whether the shift changed anything - then closes the window on ONE unconditional line and publishes its restore. **The enable has to be standing for CMD2, and that is 733's measurement rather than a preference**: on rung 14's press CMD0 ran inside the enabled window and completed (`_cmd0_complete = 1`, `_cmd0_status_any = 1`) while CMD1 ran with the enable closed, was ANSWERED BY THE CARD (`_cmd1_resp = 0x40ff8080`, a valid OCR) and never latched a completion at all (`_cmd1_complete = 0`, `_cmd1_status_any = 0` over 5,088,000 polls, `_cmd1_timeout = 1`) - so on this controller a command's status bit is latched only while its enable stands, and a CMD2 sent the way CMD1 was would answer the same way and teach nothing. **The gate is the driver's own and it is read off CMD1's RESPONSE and not off a status bit** (`c1.sent != 0 && (c1.resp & MMC_CARD_BUSY) == 0`), because a status-bit gate would refuse on every arm of this ladder. It transcribes two constants this tree's vendor header orders the other way from upstream Linux (`sdhci.h:53` has `RESP_LONG 0x01` and `:54` `RESP_SHORT 0x02`), and it adds NO new command class beyond CMD2, no data path, no `POWER_CONTROL 0x29`, no GCC word, no `core_mem` word and no byte of the medium. A value above the ladder is refused here rather than shaping an image whose switches claim something else."
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
#define ST_SDHCI_SOFTWARE_RESET 0x2Fu   /* sdhci.h:113 - a BYTE */
#define ST_SDHCI_RESET_ALL      0x01u   /* sdhci.h:114 - the mask the driver resets with, and it
                                         * is also the bit the poll below tests: the standard says
                                         * the bit is self-clearing */
#define ST_SDHCI_SLOT_INT_STAT  0xFCu   /* sdhci.h:239 - 16-bit */
#define ST_SDHCI_HOST_VERSION   0xFEu   /* sdhci.h:241 - 16-bit, and the register 697 left owed */
#define ST_SDHCI_CARD_PRESENT   0x00010000u /* sdhci.h:71 - must NOT be read as "no card" here, see below */

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
#define ST_MMC_CARD_BUSY           0x80000000u  /* mmc.h:227 - mmc_ops.c:158's loop condition */
#define ST_CMD_OP_ALL_SEND_CID     2u           /* mmc.h:31 - CMD2 bcr R2, mmc_ops.c:173 */
#define ST_CMD_OP_GO_IDLE_STATE    0u           /* mmc.h:29 - CMD0, mmc_ops.c:107 */
#define ST_CMD_OP_SEND_OP_COND     1u           /* mmc.h:30 - CMD1, mmc.c:1923 */
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
    uint32_t rsp_present;       /* the driver's own guard, sdhci.c:1169 */
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

    word = ST_SDHCI_CMD_WORD(opcode, mmc_flags);
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

    /* sdhci.c:1169-1175 - the response is read only when the command asked for one. */
    r->rsp_present = ((mmc_flags & ST_MMC_RSP_PRESENT) != 0u) ? 1u : 0u;
#if STAGE90_XNU_STORAGE_PROBE >= 12
    /*
     * **724 section 3: read unconditionally, and `rsp_present` stays the driver's own condition.**
     * `sdhci_finish_command` (`sdhci.c:1169-1175`) reads RESPONSE only when the command asked for a
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
 * `_cmd1_timeout = 1` bound). A CMD2 sent the way CMD1 was would answer the same way - the response
 * would arrive and nothing would latch - and this arm would learn nothing it does not already have.
 * So the SAME one-bit window rung 14 opens for CMD0 is opened again here, for CMD2's send and its poll,
 * and closed on ONE unconditional line immediately after the publishes below.
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
static __attribute__((noinline, noclone)) void st_all_send_cid(uint32_t int_enable)
{
    struct st_cmd_result c2;
    uint32_t raw0, raw1, raw2, raw3, held, readback, status_post;

    ST_LIVE("xnu_live_storage_cid_calls", 1u);

    /* --- the window opens here, immediately before CMD2 is put on the bus ---------------------- */
    ST_LIVE("xnu_live_storage_cid_ena_wrote", int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
    held = st_read32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE);
    ST_LIVE("xnu_live_storage_cid_ena_held", held);
    ST_LIVE("xnu_live_storage_cid_sig_enable",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_SIGNAL_ENABLE));

    ST_LIVE("xnu_live_storage_cid_op", ST_CMD_OP_ALL_SEND_CID);
    ST_LIVE("xnu_live_storage_cid_flags", ST_MMC_RSP_R2);
    st_send_command(ST_CMD_OP_ALL_SEND_CID, 0u, ST_MMC_RSP_R2, &c2);

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
}
#endif /* STAGE90_XNU_STORAGE_PROBE >= 16 - one body, one caller, two stores and the ladder's first 136-bit read */
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
    ST_LIVE("xnu_live_storage_ena_wrote", int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE);
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
    ST_LIVE("xnu_live_storage_cmd_sent", sent);
    ST_LIVE("xnu_live_storage_cmd_ps_after",
            st_read32(ST_HC_MEM_BASE + ST_SDHCI_PRESENT_STATE));

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
    if (c1.sent != 0u && (c1.resp & ST_MMC_CARD_BUSY) == 0u) {
        ST_LIVE("xnu_live_storage_cid_gated", 0u);
        st_all_send_cid(int_enable);
    } else {
        ST_LIVE("xnu_live_storage_cid_gated", 1u);
        ST_LIVE("xnu_live_storage_cid_gated_reason", (c1.sent == 0u) ? 1u : 2u);
    }
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
