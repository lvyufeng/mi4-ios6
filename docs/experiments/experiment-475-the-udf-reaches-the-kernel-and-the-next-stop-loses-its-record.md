# Experiment 475 — the `udf` reaches the kernel, and the next stop loses its own record

**474 measured process 1 entering user mode and died of the instrument's own handler reading address 0.**
This step gives the handler the one test it was missing — *which* `udf` it is looking at, decided from
the interrupted mode — refuses a NULL frame rather than trusting it, and forwards the user case to
Apple's own body so the kernel decides what a user `udf` means. The run that follows is the first in
this project in which **the kernel's own trap path names process 1's first instruction**:

    xnu_live_undef_user_seq = 0x00000001
    xnu_live_undef_pc       = 0x000010e0      the RAM disk Mach-O's `udf #0`
    xnu_live_undef_lr       = 0x000010e4      LR_und = pc + 4 for an ARM undefined instruction
    xnu_live_undef_spsr     = 0x00000010      user mode, PSR_USERDFLT
    xnu_entry_undef_pc      = 0x000010e0      the same three keys again, in the report the epilogue printed
    xnu_entry_undef_lr      = 0x000010e4
    xnu_entry_undef_spsr    = 0x00000010
    xnu_entry_undef_user    = 0x00000001

**The step is one branch, one refusal and one check.** In `fleh_undef`, the SPSR is decoded before
anything else (`spsr & PSR_MODE_MASK == PSR_USER_MODE`), the user case writes its record to the *live*
channel and calls Apple's `locore_fleh_undef` instead of walking `panic`'s arguments, and the kernel
case keeps the walk with one new condition in `entry_panic_args_page` (`args == 0` → refuse). The
decision is published as its own key, `xnu_entry_panic_args_ok`, because 474 showed the old page key
could not distinguish an accepted NULL from a refusal. And `tools/check_undef_handler.py` makes both
promises build failures: `--guard` compiles the two guard functions **out of the source** and runs an
18-row case table on the host (the AES check's shape), and `--forward` reads the *linked image* and
requires `fleh_undef` and `locore_fleh_undef` to be distinct symbols with a `bl` between them whose
target is computed from its encoding. Measured in the build:

    xnu_entry_475: the panic-argument guard answers the case table (18 rows) - a NULL frame is refused,
    the window that fits a page is accepted and the one that straddles it is not
    xnu_entry_475: `fleh_undef` at 0x80006638 forwards to `locore_fleh_undef` at 0x80014024 - distinct
    symbols, and the `bl` inside the handler computes to Apple's body

**The discriminator is derived, and the run confirms it from the other side.** `panic`'s `udf` is
executed by `DebuggerTrapWithState` and every route to it is kernel code: `sleh_undef`'s kernel arm ends
in `panic_context` and its user arm in `exception_triage(EXC_BAD_INSTRUCTION, ...)`, which never
returns — and Apple's own `locore_fleh_undef` makes exactly this test as its first two instructions
(`mrs sp, SPSR`, `tst sp, #15`, `bne undef_from_kernel` at `+0x10`). So a `udf` arriving with a user
SPSR cannot be `panic`'s. **The measurement that says the branch is what did the work is the absence of
the walk**: `xnu_entry_panic_len = 0x000000a4` with `xnu_entry_panic_dropped = 0x00000000` — 164 bytes,
the four keys of the user branch and nothing else — and no `xnu_entry_panic_args_ok`,
`xnu_entry_panic_arg0` or `xnu_entry_dt_root` anywhere in the log. 474's read of address 0 was
`entry_word_at((uintptr_t)r_args)` with `r_args = 0`, and the keys that would have carried it are not
in this run at all.

**And the 474 storm is gone, measured in the same channel that showed it.** The live records are now
the four aborts of the exec and nothing else — `xnu_live_sleh_seq` 1 to 4, each with `_back` 1 to 4 (so
all four returned), `xnu_live_sleh_seen` stopping at 4 rather than running to 0x40, and no
`xnu_live_sleh_storm` at all. The live channel's own order is the evidence that the `udf` came last:
four `sleh` groups, then the four `undef` keys, with nothing after them. In 474 the same channel ended
9 → 0x40 with `far = 0x28` repeating and no `_back` record for entries 5 to 8.

**The forward did not return**, and that is a reading rather than an assumption: the run ends with the
*prefetch* handler's message (`real XNU entry: exception: prefetch abort`), not with the defensive
`entry_epilogue` this step added for "Apple's handler returned". So the user `udf` reached Apple's body,
the mode switch and `sleh_undef` ran, and the boot went on to a *different* fault. How far it got inside
the signal path is **not** decided by this log, and the reason is the next frontier.

## The next stop is a prefetch abort whose record was refused

The instrument's own slot-3 handler still owns prefetch aborts (467 installed Apple's body in slot 4,
for data aborts, and left slot 3 alone), and it reports through `entry_kv` — the shared trace channel,
8 KB, first-write-wins:

    xnu_entry_kv_written  = 0x00001fea   (8170 of 8192 bytes)
    xnu_entry_kv_dropped  = 0x0000820c   (33292 refusals)
    xnu_entry_why         = 0x80476db4   "exception: prefetch abort"
    xnu_entry_abort_entries = 0x00000000 (the instrument's data-abort tracer never fired; slot 4 is Apple's)

**The nine keys that would name the fault — `xnu_entry_prefetch_abort_ifar`, `_ifsr`, `_pc`, `_lr`,
`_spsr`, `_ttbr0`, `_ttbr1`, `_ttbcr`, `_sctlr` — are not in the log**, because the channel had been
full for 33292 refusals by the time the fault arrived. This is 461's defect on a different handler: the
undef report was given a buffer of its own (`g_panic_buf`, sized for the whole record, printed under its
own heading) for exactly this reason, and the abort handlers were not. **So the address is lost, and the
run cannot say whether the boot prefetch-aborted on a page the kernel had not mapped yet (which XNU's
own handler would have resolved) or on a pointer that should never have been called** — which is the
question 476 has to answer, by giving those reports a buffer that cannot be full.

**The rest of the run is the most this boot has done.** `xnu_entry_block_count = 0x46` (70 entries)
with `xnu_entry_block_returned = 0x0d` (13 returns) and `xnu_entry_sleep_calls = 0x07`, the block table
naming five distinct threads; the kalloc tracer's `t268_*` records run to the end of their table; and
the OS console — measured this time, because the epilogue ran — is 1291 characters over 25 lines
(`xnu_entry_ostext_chars = 0x50b`, `xnu_entry_ostext_lines = 0x19`), ending where 473 and 474 ended,
`load_init_program: attempting to load /sbin/launchd`, with no failure line for it and **no panic
text anywhere in the console**. The four aborts of the exec are at `pc` 0x80011a20, 0x80007a28,
0x800076f4, 0x80011a20 — 0x60 and 0x64 past 474's, not uniformly, because this step's code grew and the
linker's alignment fills the difference: **the addresses are this build's and have to be looked up in
this build's ELF.**

**Measured:** gate passed, exit 0, device back on Android by itself (`No errors detected` after the
payload's output), log 481894 bytes / 5534 lines / 3933 payload lines. Entry image `.text` 5224384
(+192), image bytes 5438068 (**unchanged**: the growth sits inside `.data`'s 16 KB alignment slack, the
effect 283 recorded from the other side), `.bss` unchanged at 0x8052fa80..0x80587500, undefined 26;
payload `stage90-qcdt.img` 8458240 bytes, sha256 `a5fb7aec…72566810`.

**What would have made this wrong, and did not.** If the mode test were inverted or wrong,
`xnu_entry_undef_user` would read 0 and the panic walk would have run — and its keys would be in the
report, which they are not. If the forward had resolved to this handler, the build would have failed
(`--forward`), and if it had resolved to nothing the link would have. If the SIGILL path returned the
thread to its `udf`, `xnu_live_undef_user_seq` would be climbing instead of 1 — the falsifier this step
put in the source before the build. And if the trace channel had had room, the prefetch abort's address
would be in the log; its absence is a property of the channel and not of the fault.

**One thing the check does not prove, said plainly.** `tools/check_undef_handler.py --forward` has two
assertions: the two symbols are distinct, and no `bl` inside `fleh_undef` targets `fleh_undef` itself.
The second arm has never fired — the mutation that would fire it is a self-call in the image, which is
the thing being prevented — so it is an assertion and not a measurement; `--selftest` covers the
symbol-equality arm only.

## What 476 has to do

**Give the abort handlers' reports a buffer that cannot be full, before changing what they do.** The
prefetch handler is the only vector entry left that stops the boot on a fault that the kernel might have
resolved, and the fix that makes it readable is the one 461 applied to `fleh_undef`: a buffer of its
own, sized for the record, printed under its own heading — with the refusal count kept visible, because
33292 refusals is what this run had instead of an address. Only then is the choice for slot 3 a
measurement rather than a guess: if the report shows a fault address in a page the kernel had not
mapped, the answer is to forward it to `locore_fleh_prefabt` the way this step forwards a user `udf`;
if it shows a `pc` that is not an instruction at all, the answer is the pointer that got there.
