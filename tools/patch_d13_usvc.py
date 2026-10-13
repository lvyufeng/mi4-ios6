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

Idempotent: a second run re-verifies the inserted form and is a no-op. A tree already staged at the
1000/1001 four-word form is MIGRATED in place to the 1002 six-word form (see `_ASM_LEGACY_1001`).
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
    "    /* **1000: USER_SVC - the user SVC frontier. 1002: r3 IS LIVE ACROSS THIS CALL.** r8 is\n"
    "     * the saved frame (`mov r8, sp` above) and every path below reads it; r11 - just set to\n"
    "     * the syscall number - survives the call by AAPCS (r4-r11 callee-saved). BUT `swi_trap`\n"
    "     * below dispatches the `0x80000000` traps on the LIVE r3 (`cmp r3,#3; addls pc,pc,r3,lsl#2`\n"
    "     * - r3=3 = thread_set_cthread_trap, which writes TPIDRURO). r3 is NOT AAPCS-preserved\n"
    "     * across a C call, and `entry_note_user_svc` clobbers it (movw r3,#0xfff8 / mov r3,r0) -\n"
    "     * so a hook that saved only {r0,r1,r8,lr} corrupted the trap dispatch and dyld's\n"
    "     * thread-pointer store never reached TPIDRURO (1001's 230x data abort at 0x4). The LEO\n"
    "     * sibling block below saves {r0-r12,lr} for exactly this reason. r0/r1 are saved because\n"
    "     * the -3 timebase path preserves them; r2/r3 because the trap dispatch reads r3. SIX\n"
    "     * words keep sp 8-aligned at the `bl` - 990's lesson: a 4-mod-8 frame kills the callee's\n"
    "     * NEON loads (`{r0-r3,r8,lr}` = 6 x 4 = 24 B). The C half is `entry_note_user_svc`\n"
    "     * (`src/entry/entry_stubs.c`); `assemble_arm_layer.sh` de-underscores the `bl` target\n"
    "     * like every asm-to-C call in this tree. */\n"
    "    stmfd   sp!, {r0-r3,r8,lr}\n"
    "    mov     r0, r8\n"
    "    bl      _entry_note_user_svc\n"
    "    ldmfd   sp!, {r0-r3,r8,lr}\n"
    "#endif /* STAGE90_XNU_USVC */\n"
    "#if defined(BOARD_CONFIG_QSD8250_LEO)\n"
)

# The 1000/1001 inserted form (a four-word save set that clobbers r3 across the call). Kept as its
# OWN literal so a tree already staged at 1001 is MIGRATED in place to the 1002 six-word form rather
# than refused - the whole point of 1002 is that this exact text is the defect. If a tree carries the
# marker but neither form below, the patch refuses (moved by hand, or a third variant).
_ASM_LEGACY_1001 = (
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
    """Return (new_text, changed). Idempotent, and a marker present is not taken on trust: the tree
    must carry EITHER the fresh anchor (insert) OR the 1001 legacy form (migrate) OR the 1002 form
    (no-op) - exactly one, or this refuses rather than certifying a tree the switch does not carry
    (`patch_d13_kextb_login.py`'s rule).

    The 1001 -> 1002 migration is deliberate: the four-word `{r0,r1,r8,lr}` save set is the DEFECT
    1002 fixes (r3 is live across the call and `swi_trap` dispatches on it), so a tree already
    staged at 1000/1001 is brought forward in place rather than left broken."""
    for _old, new in pairs:
        if text.count(new) == 1:
            # Already 1002 (or a fresh tree that needs no edit): verify nothing else is stale.
            if text.count(_ASM_LEGACY_1001) != 0:
                raise SystemExit(
                    f"patch_d13_usvc: {path} carries BOTH the 1002 form and the 1001 legacy form - "
                    "a half-applied migration. Refusing."
                )
            return text, False
    for old, new in pairs:
        legacy = _ASM_LEGACY_1001
        n_new = text.count(new)
        n_legacy = text.count(legacy)
        n_fresh = text.count(old)
        if n_new == 0 and n_legacy == 1:
            # Migrate the 1001 tree in place.
            text = text.replace(legacy, new, 1)
        elif n_new == 0 and n_legacy == 0 and n_fresh == 1:
            # Fresh tree: insert.
            text = text.replace(old, new, 1)
        else:
            raise SystemExit(
                f"patch_d13_usvc: {path} matches none of (one 1002 form, one 1001 legacy form, "
                f"one fresh anchor) - 1002 forms={n_new}, 1001 forms={n_legacy}, anchors={n_fresh}. "
                "The tree is not the expected revision, or it was hand-edited. Refusing."
            )
    # Sanity by value, before writing: the target form in place exactly once, the legacy gone.
    for _old, new in pairs:
        if text.count(new) != 1:
            raise SystemExit(f"patch_d13_usvc: the 1002 form did not land once in {path} - "
                             "refusing to write")
        if text.count(_ASM_LEGACY_1001) != 0:
            raise SystemExit(f"patch_d13_usvc: the 1001 legacy form survives in {path} - "
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