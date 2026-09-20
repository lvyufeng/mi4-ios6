# Experiment 451 — the live console refuses, and the refusal is invisible

**Status: built, gated, run on hardware, and the run is silent.** The step was designed to give the
instrument a channel that survives a hang - a section descriptor written into XNU's own L1 so that
the ram console can be written from *inside* XNU instead of only from the epilogue. It wrote
nothing, and the reason it wrote nothing was not visible either, because the refusal was recorded in
`.bss` and only the epilogue prints `.bss`. **The measurement this step produced is the diagnosis of
its own silence**, and it is the third time in this project's recent history that the instrument's
failure mode has been the exact silence it was built to remove (441, 449, 451). `.text` 5004992 ->
**5013116** at build time (the number is revised below by 452, which removes the code this step
added), entry bin and window unchanged, `.bss` `0x804f7a40 .. 0x80548b98`, payload `6fc92d46...`
8228864 bytes.

## Why: 450 measured the boot, and the boot's report does not exist

450 retired the terminal `thread_block` and the run came back with **nothing at all** - 294738 bytes
of payload ladder, no `stub_hit=`, no `exception:`, no `panic:`, the device back on its own. Every
number the instrument has is written by the epilogue, and a boot that hangs after the jump never
reaches it. So the measurement of 450 is not "the boot stopped at X" but "the instrument cannot see
where the boot went", and that is what this step is about.

The ram console is the one channel that survives - it is where every log this project has read came
from, including this one, because Android preserves the buffer and exposes it as `/proc/last_kmsg`.
It is written at VA `RAM_CONSOLE_BASE` = `0xde500000`, and that address has no translation while
XNU's page tables are live: 268's own `exception: data abort` with `dfar=0xde500000` and `pc` inside
`entry_write_kv` is that fact measured from the receiving end. The epilogue works around it by
tearing the MMU down first, which is sound for the last thing a boot does and useless for a report
that has to arrive *during* the boot.

## The design

One section descriptor, written into XNU's live L1 at the index that answers for `0xde500000`, with
the memory type set by hand to `TEX=000 C=0 B=0` (Strongly-ordered) so that the writes go to DRAM
and not to a cache a hang would never flush. Four checks before the write, and one after:

| check | refusal code |
| --- | --- |
| the neighbour (index `0xde4`) is XNU's own BLOCK for the last declared MB | 1 |
| the target slot is invalid (`type == 0`) | 2 |
| DACR permits the neighbour's domain | 3 |
| the console's signature reads back through the new descriptor | 4 |
| (with `SCTLR.TRE` set) PRRR has an encoding whose field is 0, i.e. a Strongly-ordered one | 5 |

and the attribute is *derived* rather than assumed - `SCTLR.TRE` decides whether TEX is remapped,
PRRR says which encodings are Strongly-ordered, and 451 reads both. The neighbour is used as the
source of everything except the physical address and the type (domain, AP, S, nG, XN), so that the
console's mapping is XNU's own idea of a kernel section and not this project's.

Nothing was predicted about *how far* a boot would get with the channel open, because the channel is
what makes that question askable. The prediction that mattered was narrower: that the header would
appear in the log if and only if the descriptor worked, and that the header would name the table,
the index, the descriptor and the refusal.

## The measurement: 294738 bytes, and not one live record

    wrote 294738 bytes to /tmp/cancro-451-last_kmsg.txt
    25 x persistent_write_attempted=0x00000000
    87 x failure_mask=0x00000000
    no stub_hit=, no exception:, no panic:
    last line: MI4IOS6_STAGE90_XNU stage90 xnu_entry: jumping to XNU's _start
    device returned on its own

Byte-for-byte the same shape as 450, and the same *size*; the only differences are the two timing
keys that move with the clock (`hw_watchdog_countdown_second`, `timebase_boot_ticks_lo`,
`ml_timebase_start`, `gic_timer_cntp_tval_before`) and the checksums derived from them. **Zero
`xnu_live_*` records exist in the log**, so `entry_live_init` never reached the line that writes the
header, so it refused (or faulted) at or before check 1 - and the log cannot say which, because the
refusal went into `.bss`.

## What the refusal was: the anchor the design was built on does not exist

The step's real content is the arithmetic that was wrong, and it was wrong in a way that the same
log was already reporting. The 451 code's own comment said:

> `boot_args.memSize` is 0x5e500000 and `physBase` is 0x80000000, so XNU maps
> [0x80000000, 0xde500000) and the ram console sits at the first address of the next 1 MB section.

`0x5e500000` is `xmaxmem` - the clamp `arm_vm_init` applies (`mem_size > memory_size` and
`mem_size > MEM_SIZE_MAX` are the two places it can shrink) - and it is not what the boot args say.
What the boot args say is in this very log, in the ladder, three thousand lines above the live
records that were never written:

    MI4IOS6_STAGE90_XNU xnu_entry_args_memSize=0x01000000
    MI4IOS6_STAGE90_XNU xnu_entry_args_physBase=0x80000000
    MI4IOS6_STAGE90_XNU xnu_entry_args_topOfKernelData=0x80700000

`memSize` is **0x01000000**, the entry window (`xnu_entry_jump.c` writes `STAGE90_XNU_ENTRY_SIZE`
there so that `_start`'s section loop covers the image, its tables, the tree and the boot args, and
nothing else). `start.s` sets up `mapveqp` over exactly that many megabytes, `arm_vm_init` refines
the same range into pages and sections, and the live L1 therefore contains descriptors for indices
**0x800..0x80f** (VA/PA `0x80000000..0x81000000`) plus `0xfff` (the high exception vectors). Index
`0xde4` - the anchor - is **fault**. The scan ran all 4096 entries, found no `(type == BLOCK) &&
(PA field == 0xde400000)`, broke out with `found == 0xffffffff`, and refused with 1.

Two smaller things follow from the same measurement and are worth carrying:

  * **Nothing the payload plants in the boot table can survive, so a pre-emptive mapping is not an
    alternative.** `start.s`'s `invalidate_tte` writes FAULT over 10240 entries starting at
    `topOfKernelData` - the whole 16 KB boot table and 24 KB past it - and `_start` runs *after* the
    payload's last act. A descriptor written there by the payload before the jump is erased by the
    first thing XNU does with the table.
  * **The console's address is not "one section past the end of memory"; that framing was the
    mistake.** It is simply an address XNU does not map, 1.5 GB past the window it was given. The
    install has to be unconditional, with the *bits* chosen by construction and only the *memory
    type* measured.

## The defect class, named

`mi4-a-claim-in-a-comment-is-not-a-check` has this exact shape: the assembler translator that once
asserted its own invariant in a comment was violating it. Here a comment asserted a *reading* - the
boot args' `memSize` - that no check and no code consumed, and the reading was of the wrong
variable. The falsifier was in the log the whole time, one `grep` from the analysis, and the step
that needed it did not run it. 452 removes the claim by removing the anchor: no descriptor bits are
taken from a neighbour at all, so there is nothing left to be wrong about.

Safety, unchanged and re-measured: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed; the device returned
to Android on its own; 25 x `persistent_write_attempted=0x00000000`, 87 x
`failure_mask=0x00000000`, no `exception:` line.
