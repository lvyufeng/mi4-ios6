# Experiment 105 — XNU writes into the crash log, and acts on our boot arguments

Date: 2026-09-17
Commit under test: `4a1d6b1`, plus the changes described below
Build switch: `STAGE90_XNU_REAL_DT = 1`
Other switches: `HANDOFF_MODE = HARD_SKIP`, `PMAP_ATTR_MODE = SO_ONLY`, `CACHE_MODE = NONE`
Device: Xiaomi Mi 4 LTE `cancro`, serial `4a2fe00b`, non-persistent `fastboot boot`
Capture: `/tmp/kmsg-console1.txt`

## The result

```
xnu_real_dt_status=0x90000001   checks=12   failures=0
xnu_real_dt_pe_init_debug_ok=0x00000001
xnu_real_dt_pe_putc_installed=0x00000001
xnu_real_dt_console_bytes=0x0000003e       62 bytes
xnu_real_dt_debugger_calls=0x00000001
```

and, in the log this was read from, a line that **XNU's own console path wrote**:

```
MI4IOS6_STAGE90 xnu console: wrote this line through PE_putc
```

`pe_gen.c` is the fifth public-XNU object and the last one that was not executing. Both of its
paths are now driven end to end, and each produces a different kind of evidence.

## XNU's console writes into the payload's crash log

`PE_init_printf(FALSE)` sets `PE_putc = cnputc` — that is three lines of Apple source, and
`pe_putc_installed=1` confirms it took. The payload has pointed `cnputc` at its own `log_puts`, so
the 62 bytes counted above went out through XNU's function pointer and landed in the ram_console
that `/proc/last_kmsg` reads.

The console hook is installed as a **function pointer the payload sets**, not a direct call into
the payload's log, so `xnu_object_shims.c` stays independent of `stage90.h` — it is compiled by a
separate script with its own include set, and that separation is worth keeping.

This is the first XNU output on this device that a person reads. It is not much: one banner line,
written because the payload asked for it. But it is XNU's `PE_putc` path doing the writing, and it
means the plumbing for a future `kprintf` — the console path, the character sink, the log — is
exercised.

## XNU acts on our boot arguments, observably

`pe_init_debug()` is:

```c
if (!PE_parse_boot_argn("debug", &DEBUGFlag, sizeof (DEBUGFlag)))
    DEBUGFlag = 0;
```

`DEBUGFlag` is `static` in `pe_gen.c`, so there is no way to read the parse's result directly.
The way to observe it is to make XNU **act** on it: `PE_enter_debugger()` calls `Debugger()` only
when `DB_NMI` is set in that flag, and the command line the payload builds says `debug=0x144`,
whose `DB_NMI` bit is `0x4`.

`xnu_real_dt_debugger_calls=0x00000001` therefore means the whole chain worked: XNU's
`PE_parse_boot_argn` read our command line, parsed `0x144` as a number, stored it in XNU's own
static, and a later XNU function consulted it and took the branch. The shim counts the calls so
the log can *read* that fact rather than infer it from a side effect.

That is a stronger demonstration than a value comparison would be: it is not that a number
matched, it is that XNU's decision changed because of something this payload supplied.

## Where the counter came from

`xnu_object_shims.c`'s `cnputc` and `Debugger` were empty stubs marked "compile/link support only;
public consistent-debug code is never executed on hardware". That comment was true when written
and is now false, so both were replaced: `cnputc` forwards to the payload's hook, and `Debugger`
counts and records the reason. The comment on `ml_map_high_window` was corrected the same way in
`experiment-102`.

Worth stating as a pattern: this project's shims carry comments asserting that the code they
stub is never executed, and every one of those comments becomes false the moment the boundary
moves. Three have now been found and corrected by the change that moved it — which is the argument
for the switch being per-run and explicit rather than always-on.

## What this does and does not establish

**Does:** all five public-XNU objects this project compiles now execute on MSM8974, and two of
them do work rather than report a measurement — the consistent-debug registry and the console.

**Does not:** make XNU run. This is still five pexpert objects in the payload's own context, not
a kernel. The `kprintf` that would make the console genuinely useful is in
`osfmk/kern/printf.c`, which is behind the same build-configuration wall as the rest of the
kernel; what exists here is the *path* it would write through.
