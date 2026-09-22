/*
 * 521's half of 520's instrument: the four words of the idle exit's `{fp, lr}` slot, captured where
 * they can still be read.
 *
 * **Why this is a header with a macro in it rather than a function call.** 520's run read the slot
 * through `entry_slot_note`'s own call frame - `xnu_live_slot_pre_m4 = 0x8047c974`, which is the
 * instruction after the `bl <entry_slot_note>` at `0x8047c970`, i.e. the note's *own saved `lr`*, and
 * `pre_m8 = 0x80553520` is one of its saved general registers. The four words the reading is about end
 * at the caller's `sp`; a C function called at that `sp` saves its registers *below* it, i.e. in
 * exactly the words to be read, because `sp` is the top of its caller's frame and its own frame starts
 * there. No arrangement of a called function avoids this: the frame is written by the compiler before
 * any C statement runs. So the capture is done by the **caller**, as four loads at fixed negative
 * offsets from the register `sp` was read from, and the values are handed over in the publisher's own
 * memory (`struct entry_slot_keys`' four `pend_*` fields) because *registers cannot survive the call*:
 * the values live in r0-r3 and r12 and every one of them is the callee's to use.
 *
 * **The two constraints the shape above has to satisfy, and they pull against each other.**
 *
 *  - The wrapper's own frame must stay **8 bytes**, and this is not tidiness: the slot's address is
 *    `E - 4` where `E` is the wrapper's `sp` at its call to the real `platform_cache_idle_exit`, and
 *    `E = X - 8` is what puts the idle *enter* wrapper's `strd r4, [sp, #-12]!` (`r4`, the deadline) at
 *    the address the exit's `pop {fp, pc}` reads as `pc`. A wrapper with a 16-byte frame moves the slot
 *    to `X - 16` and out from under that store, so the arm would stop testing 520's mechanism while
 *    still looking green - which is why `build_entry.sh` asserts the frame's *size* as well as its
 *    shape, and why the capture is written so that gcc needs no callee-saved register beyond the `r4`
 *    the frame already holds: the publisher takes two arguments (no outgoing stack arguments, so no
 *    `sub sp` at the call site) and the four values are staged in the table itself.
 *  - The four loads must happen **before the first call**, which 520's clause asserts on addresses
 *    rather than trusting the source order, because the first call's own frame lands in those words.
 *
 * **The staging is single-threaded by construction.** The exit wrapper is reached only from `cpu_idle`'s
 * own exit path, so no nested pass can stage a second site's words between a capture and its publisher.
 * The fields are named `pend_*` rather than `m16`-and-so-on so that a reader of `entry_slot_note` cannot
 * mistake a staged word for a live one: the note publishes what the caller put there, and the caller is
 * the only writer.
 */
#ifndef ENTRY_SLOT_CAPTURE_H
#define ENTRY_SLOT_CAPTURE_H

#include <stdint.h>

/* 6 key pointers and 2 counters = 48 bytes, and the four `pend_*` words are inside that size - the
 * clause asserts 48 against the linked image, so a field added here without the clause changing stops
 * the build rather than moving the reading. */
struct entry_slot_keys {
    const char *k_sp, *k_m16, *k_m12, *k_m8, *k_m4, *k_calls;
    uint32_t    calls, live;
    uint32_t    pend_m16, pend_m12, pend_m8, pend_m4;
};

/*
 * The four loads, at `sp - 16` through `sp - 4`, so the window is the two doublewords ending at `sp` -
 * the slot is the upper one and the lower one is the 8 bytes the exception frame reserves and leaves
 * alone. `sp` here is the caller's own stack pointer, read from the register by the caller's own
 * `mov %0, sp`, and the clause requires the four offsets to be these four numbers relative to the
 * register that `mov` produced: an instrument that read four words somewhere else would publish four
 * real, quiet, wrong numbers, which is the failure this whole step exists to stop repeating.
 */
#define STAGE90_SLOT_CAPTURE(k, spv) do {                                        \
        (k)->pend_m16 = *(volatile uint32_t *)(uintptr_t)((spv) - 16u);          \
        (k)->pend_m12 = *(volatile uint32_t *)(uintptr_t)((spv) - 12u);          \
        (k)->pend_m8  = *(volatile uint32_t *)(uintptr_t)((spv) - 8u);           \
        (k)->pend_m4  = *(volatile uint32_t *)(uintptr_t)((spv) - 4u);           \
    } while (0)

#endif /* ENTRY_SLOT_CAPTURE_H */
