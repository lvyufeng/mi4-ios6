#!/usr/bin/env python3
"""Hook the user SVC entry for 1000's syscall frontier (999's given (c)).

WHY. 999's tail read "exec done -> user mode -> `cs_invalid_page` -> silence" off the COUNTED
channel, whose cap (24,576) sits mid-boot - so the silence after `cs_invalid_page` cannot
distinguish "pid 1 stopped there" from "the cap ate everything after there" (999's [1]: the
tombstone `xnu_live_capped=0x6000` proves the cap bit, and every counted record past it is
invisible). Given (c) is the repair: the first 64 SVCs the user side takes are published in full -
n, number, pid, thread, pc, lr, cpsr, r0 - on the DIRECT (cap-exempt) channel, then a
powers-of-two ladder of `_n`/`_num`, so the last record in a log IS the last syscall pid 1
managed. The C half is `entry_note_user_svc` (`src/entry/entry_stubs.c`).

SITE. `osfmk/arm/traps_lo.s`, inside `fleh_swi` - the file's ONE SVC entry (`EnterARM(fleh_swi)`
:115; there is no thumb sibling) - between `cpsie i` (:136) and the LEO `#if` (:137). By value,
at that point: `mov r8, sp` (:130) put the saved frame in r8, and every path below reads it
(`swi_mach`/`swi_exit`/`swi_unix` all `mov r0, r8`); `mov r11, r12` (:135) just put the syscall
number in r11, and the dispatch below (`cmp r11, #0x80000000` :164, `rsbs r5, r11, #0` :170) reads
it, so it must survive the call - AAPCS says r11 is callee-saved and the callee is an AAPCS C
function; r0/r1 are saved because the -3 timebase path below explicitly preserves them; r12 is
dead after :135 (nothing reads it until `swi_mach` loads r1). lr is saved the way the LEO sibling
saves it. FOUR words keep sp 8-aligned at the `bl` - 990's lesson verbatim: astloop's 9-word frame
was 4 mod 8 and `_ast_taken`'s `vld1.32` died on the unaligned sp.

GATE. `#if defined(STAGE90_XNU_USVC) && STAGE90_XNU_USVC` - a NEW switch for the 1000 rung,
default 0. `tools/assemble_arm_layer.sh` appends `-DSTAGE90_XNU_USVC=$USVC` to ASFLAGS; with the
switch off - or the macro undefined, hence the `defined()` leg - the inserted text is not emitted
and the tree's original text compiles byte for byte. The `#if` delimiters themselves are
directives - cpp drops them - so OFF is textually the original.

REBUILD. The file is an ASSEMBLY-layer object (`out/xnu_asm_obj_d13/traps_lo.o`): after staging,
re-run `tools/assemble_arm_layer.sh` with `STAGE90_XNU_USVC=1` EXPORTED, then relink
(`build_entry.sh` only links). No POOL object changes - the C helper is entry-side, and
`entry_stubs.o` is in `LINK_OBJS` before pass 1 runs, so the `bl` resolves there in pass 1 without
a generated stand-in (the `entry_wdt_pet` family trap is the counterexample this avoids).

Idempotent: a second run re-verifies the inserted form and is a no-op.
"""
import sys

# The marker that proves this patch ran. One spelling, checked in the file.
MARK = "1000: USER_SVC"

_ASM_OLD = (
    "    mov     r11, r12\n"
    "    cpsie   i\n"
    "#if defined(BOARD_CONFIG_QSD8250_LEO)\n"
)

_ASM_NEW = (
    "    mov     r11, r12\n"
    "    cpsie   i\n"
    "#if defined(STAGE90_XNU_USVC) && STAGE90_XNU_USVC\n"
    "    /* **1000: USER_SVC - the user SVC frontier.** r8 is the saved frame (`mov r8, sp` above)\n"
    "     * and every path below reads it; r11 - just set to the syscall number - survives the\n"
    "     * call by AAPCS (r4-r11 callee-saved, and the callee is an AAPCS C function); r0/r1 are\n"
    "     * saved because the -3 timebase path below preserves them. Four words keep sp 8-aligned\n"
    "     * at the `bl` - 990's lesson: a 4-mod-8 frame kills the callee's NEON loads. The C half\n"
    "     * is `entry_note_user_svc` (`src/entry/entry_stubs.c`); `assemble_arm_layer.sh`\n"
    "     * de-underscores the `bl` target like every asm-to-C call in this tree. */\n"
    "    stmfd   sp!, {r0,r1,r8,lr}\n"
    "    mov     r0, r8\n"
    "    bl      _entry_note_user_svc\n"
    "    ldmfd   sp!, {r0,r1,r8,lr}\n"
    "#endif /* STAGE90_XNU_USVC */\n"
    "#if defined(BOARD_CONFIG_QSD8250_LEO)\n"
)

EDITS = (
    ("osfmk/arm/traps_lo.s", (
        (_ASM_OLD, _ASM_NEW),
    )),
)


def patch_text(text: str, pairs, path: str) -> tuple[str, bool]:
    """Return (new_text, changed). Idempotent, and a marker present is not taken on trust: every
    inserted form must be present exactly once, or this refuses rather than certifying a tree the
    switch does not actually carry (`patch_d13_kextb_login.py`'s rule)."""
    if MARK in text:
        for _old, new in pairs:
            if text.count(new) != 1:
                raise SystemExit(
                    f"patch_d13_usvc: the marker is present in {path} but an inserted form "
                    f"occurs {text.count(new)} times - moved by a hand edit, or half-applied. "
                    "Refusing rather than reporting a patch the switch does not carry."
                )
        return text, False
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            raise SystemExit(
                f"patch_d13_usvc: anchor in {path} occurs {n} times, not once - the tree is "
                f"not the expected revision, or it was edited by hand. Anchor begins: {old[:60]!r}. "
                "Refusing."
            )
    for old, new in pairs:
        text = text.replace(old, new, 1)
    # Sanity by value, before writing: every inserted form in place exactly once.
    for _old, new in pairs:
        if text.count(new) != 1:
            raise SystemExit(f"patch_d13_usvc: an inserted form did not land once in {path} - "
                             "refusing to write")
    return text, True


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_usvc.py <XNU_TREE>", file=sys.stderr)
        return 2
    root = sys.argv[1]
    for rel, pairs in EDITS:
        path = root + "/" + rel
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except FileNotFoundError:
            print(f"patch_d13_usvc: no {path}", file=sys.stderr)
            return 2
        new, changed = patch_text(text, pairs, rel)
        if not changed:
            print(f"patch_d13_usvc: already applied ({path})")
            continue
        with open(path, "w", encoding="utf-8") as f:
            f.write(new)
        print(f"patch_d13_usvc: {rel} now records the user SVC frontier, gated by "
              f"STAGE90_XNU_USVC ({path})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())