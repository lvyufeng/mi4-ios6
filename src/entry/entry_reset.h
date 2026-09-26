/*
 * The two hardware writes that end a run, and the one value that says which run it was - one
 * definition, because two files now make those writes.
 *
 * **Why this is a header and not three numbers in `entry_stubs.c`.** Until 678 the only writer of
 * these two registers in this image was `entry_epilogue`, and the numbers were `#define`s in
 * `entry_stubs.c` beside it (their old spellings, `RESTART_REASON` / `RESTART_NORMAL` /
 * `MSM8974_PSHOLD`, are gone with this file). 678's arm adds a *second* writer: the seam's
 * deliberate ending (`entry_seam_end_run`, `entry_trace.c`), which is in a different translation
 * unit and cannot see a `#define` in `entry_stubs.c`. The three ways to make that work are a copy
 * (one value with two definitions - the defect class this project has paid for most often, and the
 * one the seam's own switch block names), a single definition reached by one file and duplicated in
 * the other, or this: one definition in a file both include. That is the same reason
 * `entry_saved_state.h` and `entry_slot_capture.h` exist one file over.
 *
 * **The addresses are the device's, not this project's choice.** `RESTART_REASON` is the IMEM word
 * Android's own `bootinfo.c` reads back as `powerup_reason_details` (0x0fa0065c), and
 * `MSM8974_PSHOLD` is the PMIC's power-hold line (0xfc4ab000). Both are written by `entry_epilogue`
 * on every report, i.e. on **every run that has ever returned a log** - which is what makes the
 * seam's store the same store on measured ground rather than a new claim (664, 674 section 1).
 *
 * **What the value buys.** `RESTART_NORMAL` is not a magic number the bootloader interprets for us:
 * it is what this project has always written, and writing it is what keeps the *reason* field
 * legible to `bootinfo.c`'s decoder. A run that ends without it leaves whatever the previous boot
 * left there, which is the confound experiment 677 recorded - so a writer that ends a run writes
 * this first, and the ordering (reason, `dsb sy`, PS_HOLD, `dsb sy`) is `platform_reboot`'s on the
 * payload side and `entry_epilogue`'s on this side.
 */
#ifndef STAGE90_ENTRY_RESET_H
#define STAGE90_ENTRY_RESET_H

/* The IMEM word Android's `bootinfo.c` reports as `powerup_reason_details`. */
#define STAGE90_ENTRY_RESET_REASON_ADDR     0x0fa0065cu
/* What this project writes there when it ends a run on purpose. */
#define STAGE90_ENTRY_RESET_REASON_NORMAL   0x78665501u
/* The PMIC's power-hold line; 0 releases it and the SoC restarts. */
#define STAGE90_ENTRY_PSHOLD_ADDR           0xfc4ab000u

#endif /* STAGE90_ENTRY_RESET_H */
