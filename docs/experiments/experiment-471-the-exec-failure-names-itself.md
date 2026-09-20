# Experiment 471 — the exec failure names itself, and it is a secure kernel refusing an unsigned file

**470 left one question and the console could not answer it:** the exec of `/sbin/launchd` failed in a way
that printed nothing, and the process it failed for was `initproc`, whose `SIGKILL` is a `panic`. The
answer had to come from the exec path's own record of *why*, and the exec path keeps one: every failure
after the image is activated creates an exit reason —

    exec_failure_reason = os_reason_create(OS_REASON_EXEC, <code>);
    goto badtoolate;

— with the code one of `EXEC_EXIT_REASON_*` (`bsd/sys/reason.h:222-233`, 1 = `BAD_MACHO` through
12 = `UPX`), and the eight `badtoolate` sites plus the two inside `check_for_signature` are the only
producers.

**The instrument is two `--wrap`s and reads arguments rather than structures.**
`os_reason_create(uint32_t osr_namespace, uint64_t osr_code)` passes both in registers, so the wrapper
records them and the caller; reading them back off the `os_reason_t` instead would mean reproducing
`struct os_reason`'s layout, which begins with `decl_lck_mtx_data(, osr_lock)` and is a private-layout
question this build has no business answering. `load_machfile`'s `load_return_t` is the second,
independent reading — the direct one, where the reason code says the same thing by way of a translation.
Both are non-terminal: the real function runs and the run continues, because a terminal instrument here
would stop at the first exit reason the boot creates rather than the one that killed `initproc`.

**The measurement, from one device run:**

    xnu_entry_lmf_ret     = 0x00000004     LOAD_FAILURE  (bsd/kern/mach_loader.h:96)
    xnu_entry_lmf_caller  = 0x80284bb4     the `bl load_machfile` in exec_mach_imgact
    xnu_entry_osr_ns      = 0x00000009     OS_REASON_EXEC   (bsd/sys/reason.h:94)
    xnu_entry_osr_code    = 0x00000001     EXEC_EXIT_REASON_BAD_MACHO (bsd/sys/reason.h:222)
    xnu_entry_osr_caller  = 0x80284d98     the `bl os_reason_create` at that site

The two agree, and they place the failure **inside `load_machfile`**: `LOAD_FAILURE` is not
`LOAD_BADARCH` (1), not `LOAD_BADMACHO` (2), not `LOAD_NOSPACE` (5) and not `LOAD_IOERROR` (6), so the
Mach-O's *shape* was not refused — the magic, the CPU type and subtype, the load commands, the segments
and the entry point all passed. The exec got as far as `exec_mach_imgact`'s call, and the reason code the
caller then created maps any non-`LOAD_SUCCESS` to `EXEC_EXIT_REASON_BAD_MACHO`.

**Which of `load_machfile`'s own `LOAD_FAILURE` sites it was, measured on the host rather than guessed
from the source.** `parse_machfile` is `static`, so `--wrap` cannot see it (455's rule: the linker can
only rewrite an *undefined* reference). The site that does not need a wrap is the second one — and the
first thing to check is whether the parser was even reached, because `load_machfile` returns early on a
parse failure. It was: `parse_machfile` completes, and the block at the end of it that returns
`LOAD_FAILURE` is

    if (!got_code_signatures) { if (cs_enforcement(NULL)) { ret = LOAD_FAILURE; } ... }

`bsd/kern/mach_loader.c:1126-1130` — and the RAM disk's Mach-O has three load commands, none of them
`LC_CODE_SIGNATURE`, so `got_code_signatures` is false. `cs_enforcement(NULL)` is then the whole question,
and it is answered by the object:

    $ arm-none-eabi-nm out/xnu_kernel_obj/bsd_kern_kern_cs.o | grep cs_enforcement_enable
    00000000 R cs_enforcement_enable
    $ arm-none-eabi-strings out/xnu_kernel_obj/bsd_kern_kern_cs.o | grep cs_enforcement
    cs_enforcement
    cs_enforcement_enable

**`R` — read-only, the `const`.** `bsd/kern/kern_cs.c:82-84` is `#if SECURE_KERNEL` /
`const int cs_enforcement_enable = 1;`, so `cs_enforcement()` returns 1 unconditionally; and the
`cs_enforcement_disable` boot-arg string is **absent from the same object**, because `cs_init`'s
`#if !SECURE_KERNEL` block (`:133-155`) that reads it is compiled out — so there is no run-time route
either. `SECURE_KERNEL` is on because `RELEASE` inherits `BSD_RELEASE`, which is
`[ BSD_BASE no_printf_str no_kprintf_str secure_kernel ]` (`config/MASTER.arm:24`), and
`xnu_config/make_defines.sh RELEASE` and `... STAGE90_XNU` both print `-DSECURE_KERNEL=1`. **A kernel built
from this project's configuration is a production secure kernel, and such a kernel cannot activate an
executable without an embedded code signature at all.**

The downstream is 470's panic, exactly: `load_machfile` fails → `exec_failure_reason =
os_reason_create(OS_REASON_EXEC, EXEC_EXIT_REASON_BAD_MACHO)` → `badtoolate` →
`psignal_with_reason(p, SIGKILL, reason)` → `p == initproc` → `panic_plain("unexpected SIGKILL of …")`.

**Two structural checks were added here, both over artifacts rather than over command lines.** Every
`--wrap` this image takes must be reachable *and* must not resolve to a generated stand-in: a `--wrap` on
an undefined name rewrites the reference to `__wrap_`, the wrapper calls `__real_`, and `__real_` would
find the stub — so the reading would be a fact about the stand-in. `os_reason_create` and `load_machfile`
are checked by name against pass 1's undefined set, the same rule 461's mangled names are. And the ARM
layer's per-configuration `assym.s` is checked by `tools/check_assym_cswitch.py`, which is 468's defect
made structural: it reads `TH_CTH_SELF`/`TH_CTH_DATA`/`TH_KSTACKPTR` out of the configuration's `assym.s`
and the corresponding `ldr rX, [r0, #N]` immediates out of the assembled `cswitch.o`, **in source order**
(a set cannot see two equal-sized fields swapped), refuses all three of its own mutations under
`--selftest`, and — as its negative control — reports all three offsets wrong against the real
`out/xnu_assym/RELEASE/assym.s`.

**Measured:** device run exit 0, `No errors detected`, device back on Android by itself; log
`/tmp/cancro-471-last_kmsg.txt`, 484587 bytes, whose only difference from 470's is heap addresses. 43
`--wrap`'d symbols, 40 reached by a branch in this image, 1 same-object-only, 1 never called, 1 by
address. Entry image `.text` 5219392, image 5437860, undefined 26, payload 8458240 bytes.

**The step after this one is a configuration change**, and it is stated here so it is not re-derived:
`SECURE_KERNEL` has to be cancelled for the RAM disk's Mach-O to be activatable at all — `-USECURE_KERNEL`
in the cancellation list `build_xnu_arm_kernel.sh` already keeps for `CONFIG_NO_PRINTF_STRINGS`, because
twenty-four files read the macro across all four spellings (`#if SECURE_KERNEL`, `#if !SECURE_KERNEL`,
`#ifdef SECURE_KERNEL`, `#ifndef SECURE_KERNEL`) and only `-U` — leaving it *undefined* — satisfies every
one of them.
