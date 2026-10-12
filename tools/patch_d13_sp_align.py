#!/usr/bin/env python3
"""Make the D13 tree hand C code 8-byte-aligned stacks at its two measured gates (990).

WHY. 989's press (arm `armed-d13-2e0eebf5`, commit `1bc3fdb`) retired 988's abort - the DT-census guard
held (`xnu_live_dtref_ok=0`) - and ran to the pid-1 launchd exec: `bsd_init: done` ->
`attempting to start init of /sbin/launchd` -> `Fatal exception: sleh_abort: alignment fault`:

  * BY VALUE: `pc=0x8021b6a8` = `parse_machfile+0x60c`, instruction `f461279f  vld1.32 {d18}, [r1 :64]`,
    `r1 = sp+168 = 0xcfdbbb24`, `sp = 0xcfdbba7c`. 168 is 8-aligned, so the qualified NEON pair load
    faults iff the running sp is 4 (mod 8) - and it was (`0xcfdbba7c & 7 == 4`).
  * The thread's first C function was `ast_taken`, entered from `thread_return_join`
    (`cswitch.s:149-163`, no push between the continuation entry and the `blx`), whose sp came straight
    from `Call_continuation`; the three recovered aborts on the way (copyout at `0x80494970`, copyinstr
    at `0x80494d9c`, bcopy) all decode to the same 4-mod-8 C island (their prologues are 12/12/20 bytes
    = 4 mod 8, and `parse_machfile`'s own entry frame - 36 + 268 = 304 = 0 mod 8 - preserves the
    residue, so it was already 4 when the exec path was entered).

THE TWO GATES (both in this tree only; 4570 has neither):

  1. `osfmk/arm/pcb.c` - `iss = STACK_IKS(stack) = stack + 16384 - FP_SIZE`, and
     `FP_SIZE = sizeof(arm_saved_state_t) = 68 = 4 (mod 8)`, so the saved-state block sits at
     `stack_top - 68`, 4 (mod 8). `Call_continuation` (`cswitch.s:46`) loads the continuation's sp
     straight from `machine.iss`, and `fleh_swi` (`traps_lo.s:132-133`) runs syscall handlers on the
     same value - so every continuation and every syscall C function starts 4 (mod 8). The C shim
     `call_continuation` (`pcb.c:368`) even computes `kss = STACK_IKS(...)` and passes it, but the asm
     reloads the same 4-mod-8 field from the thread register. 4570 used
     `thread_kernel_state` = 68 + 4 rounded to `aligned(16)` = 80 - a multiple of 16 - so Apple's
     continuations were aligned: one value, two definitions.
  2. `osfmk/arm/machine_routines_asm.s:133` - `ml_set_interrupts_enabled`'s `astloop` saves
     `stmfd sp!,{r0-r7,lr}` = 9 words = 36 bytes = 4 (mod 8) immediately before `blx _ast_taken`
     (a C function). The same file's `__preempt` saves an even number of words (and the LEO branch
     even carries the comment "Four words preserve stack alignment and callee-saved r4/r7 as well as
     LR") - only `astloop` was left odd. clang only *exposes* gate 1/2 when it emits an
     -eight-byte-qualified NEON access (`-mfpu=neon-vfpv4` on this build); the violation is there
     either way, and this tree's `sleh_abort` patch makes any alignment fault fatal.

THE FIX.

  * Gate 1: round the state block AND the frame below it down to 8 - `& ~7u` on `STACK_IKS(stack)` and
    on `kstack - sizeof(arm_saved_state_t)` - in `machine_stack_attach` AND `machine_stack_handoff`.
    The block address is the continuation sp (`Call_continuation`), the block's `sp` field is the
    resume sp (`machine_load_context`); both become 0 (mod 8). The top 4 bytes of the stack go unused.
  * Gate 2: `sub sp, sp, #4` / `add sp, sp, #4` around `blx _ast_taken` - the plain (non-`s`) forms do
    not touch the flags between `movs r0, #7` and the call.

Both edits are wrapped in `#if STAGE90_XNU_SP_ALIGN ... #else <original text> #endif`, so switch OFF
compiles the tree's original text byte for byte and neither the pool nor an old arm's entry image
moves (`stage_d13_kextb_login.sh`'s rule, `mi4-two-branch-split-4570-vs-master`). The linked-image
property is asserted BY VALUE by `tools/check_sp_alignment.py` (constants 16316/16248 absent,
16312 present in both stack functions; each `_ast_taken` call site's cumulative sp delta = 0 mod 8;
`Call_continuation`'s `ldr sp` still reads `TH_PCB_ISS`).

The tree is a re-provisionable, gitignored checkout; this script is the single source of truth for the
edit (`stage_d13_board.sh`'s rule). Idempotent: a second run verifies the six inserted forms and is a
no-op.
"""
import sys

# The marker that proves this patch ran. One spelling, checked in both files.
MARK = "990: SP_ALIGN"

# --- pcb.c ----------------------------------------------------------------------------------------
# Anchors are the tree's EXACT text (4-space indent, verified with `cat -A`). Each `old` is unique in
# `pcb.c` (the two `STACK_IKS` lines are disambiguated by their following line); `new` contains `old`
# in its `#else` branch, so switch OFF is textually the original and compiles identically.
_PCB_H1_OLD = (
    "    uint32_t *kstack = (uint32_t *) STACK_IKS(stack);\n"
    "    new->machine.iss = (arm_saved_state_t *) kstack;\n"
)
_PCB_H1_NEW = (
    "    /* **990: SP_ALIGN - the saved-state block must sit 8-byte aligned.** `Call_continuation`\n"
    "     * (osfmk/arm/cswitch.s:46) loads the continuation's sp straight from `machine.iss`, and\n"
    "     * `fleh_swi` runs syscall handlers on the same value, so a block at `stack_top - 68`\n"
    "     * (68 = sizeof(arm_saved_state_t) = 4 mod 8) makes every continuation and every syscall run\n"
    "     * C at a 4-mod-8 sp - an AAPCS violation clang exposes with any 8-byte-qualified NEON\n"
    "     * access (989: parse_machfile+0x60c `vld1.32 {d18},[r1 :64]`, sp = 0xcfdbba7c). 4570's\n"
    "     * thread_kernel_state was 16-aligned; this tree re-derived the offset from FP_SIZE - one\n"
    "     * value, two definitions. The block is rounded down to 8 (top 4 bytes unused), and the\n"
    "     * frame below it likewise. See tools/patch_d13_sp_align.py. */\n"
    "#if STAGE90_XNU_SP_ALIGN\n"
    "    uint32_t *kstack = (uint32_t *) (((vm_offset_t) STACK_IKS(stack)) & ~7u);\n"
    "#else\n"
    "    uint32_t *kstack = (uint32_t *) STACK_IKS(stack);\n"
    "#endif /* STAGE90_XNU_SP_ALIGN */\n"
    "    new->machine.iss = (arm_saved_state_t *) kstack;\n"
)
_PCB_H2_OLD = (
    "    new->machine.iss->sp = (uint32_t) kstack - sizeof(arm_saved_state_t);\n"
)
_PCB_H2_NEW = (
    "#if STAGE90_XNU_SP_ALIGN\n"
    "    new->machine.iss->sp = (((uint32_t) kstack) - sizeof(arm_saved_state_t)) & ~7u;\n"
    "#else\n"
    "    new->machine.iss->sp = (uint32_t) kstack - sizeof(arm_saved_state_t);\n"
    "#endif /* STAGE90_XNU_SP_ALIGN */\n"
)
_PCB_A1_OLD = (
    "    uint32_t *kstack = (uint32_t *) STACK_IKS(stack);\n"
    "\n"
    "    thread->kernel_stack = stack;\n"
)
_PCB_A1_NEW = (
    "#if STAGE90_XNU_SP_ALIGN\n"
    "    uint32_t *kstack = (uint32_t *) (((vm_offset_t) STACK_IKS(stack)) & ~7u);\n"
    "#else\n"
    "    uint32_t *kstack = (uint32_t *) STACK_IKS(stack);\n"
    "#endif /* STAGE90_XNU_SP_ALIGN */\n"
    "\n"
    "    thread->kernel_stack = stack;\n"
)
_PCB_A2_OLD = (
    "    thread->machine.iss->sp = (uint32_t) kstack - sizeof(arm_saved_state_t);\n"
)
_PCB_A2_NEW = (
    "#if STAGE90_XNU_SP_ALIGN\n"
    "    thread->machine.iss->sp = (((uint32_t) kstack) - sizeof(arm_saved_state_t)) & ~7u;\n"
    "#else\n"
    "    thread->machine.iss->sp = (uint32_t) kstack - sizeof(arm_saved_state_t);\n"
    "#endif /* STAGE90_XNU_SP_ALIGN */\n"
)

# --- machine_routines_asm.s -----------------------------------------------------------------------
# The `stmfd   sp!,{r0-r7,lr}` line is UNIQUE in the file (measured); the closing anchor needs
# `ldmfd   sp!,{r0-r7,lr}` as its second line because `blx     _ast_taken` alone occurs three times
# (astloop and `__preempt`'s two branches, which this patch must not touch).
_ASM_S1_OLD = (
    "    stmfd   sp!,{r0-r7,lr}\n"
    "    movs    r0, #7\n"
)
_ASM_S1_NEW = (
    "    stmfd   sp!,{r0-r7,lr}\n"
    "#if STAGE90_XNU_SP_ALIGN\n"
    "    /* **990: SP_ALIGN** - nine saved words are 36 bytes = 4 (mod 8), and the AAPCS needs sp\n"
    "     * 8-byte aligned at `blx _ast_taken` below (989 died at parse_machfile+0x60c's\n"
    "     * `vld1.32 {d18},[r1 :64]` on the 4-mod-8 sp this odd frame produced upstream). Plain\n"
    "     * sub/add - they do not touch the flags between `movs r0, #7` and the call. */\n"
    "    sub     sp, sp, #4\n"
    "#endif /* STAGE90_XNU_SP_ALIGN */\n"
    "    movs    r0, #7\n"
)
_ASM_S2_OLD = (
    "    blx     _ast_taken\n"
    "    ldmfd   sp!,{r0-r7,lr}\n"
)
_ASM_S2_NEW = (
    "    blx     _ast_taken\n"
    "#if STAGE90_XNU_SP_ALIGN\n"
    "    add     sp, sp, #4\n"
    "#endif /* STAGE90_XNU_SP_ALIGN */\n"
    "    ldmfd   sp!,{r0-r7,lr}\n"
)

# (relative path, [(old, new), ...]) - one entry per file, all edits listed for that file.
EDITS = (
    ("osfmk/arm/pcb.c", (
        (_PCB_H1_OLD, _PCB_H1_NEW),
        (_PCB_H2_OLD, _PCB_H2_NEW),
        (_PCB_A1_OLD, _PCB_A1_NEW),
        (_PCB_A2_OLD, _PCB_A2_NEW),
    )),
    ("osfmk/arm/machine_routines_asm.s", (
        (_ASM_S1_OLD, _ASM_S1_NEW),
        (_ASM_S2_OLD, _ASM_S2_NEW),
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
                    f"patch_d13_sp_align: the 990 marker is present in {path} but an inserted form "
                    f"occurs {text.count(new)} times - moved by a hand edit, or half-applied. "
                    "Refusing rather than reporting a patch the switch does not carry."
                )
        return text, False
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            raise SystemExit(
                f"patch_d13_sp_align: anchor in {path} occurs {n} times, not once - the tree is not "
                f"the expected revision, or it was edited by hand. Anchor begins: {old[:60]!r}. "
                "Refusing."
            )
    for old, new in pairs:
        text = text.replace(old, new, 1)
    # Sanity by value, before writing: every inserted form in place exactly once.
    for _old, new in pairs:
        if text.count(new) != 1:
            raise SystemExit(f"patch_d13_sp_align: an inserted form did not land once in {path} - "
                             "refusing to write")
    return text, True


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_sp_align.py <XNU_TREE>", file=sys.stderr)
        return 2
    root = sys.argv[1]
    for rel, pairs in EDITS:
        path = root + "/" + rel
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except FileNotFoundError:
            print(f"patch_d13_sp_align: no {path}", file=sys.stderr)
            return 2
        new, changed = patch_text(text, pairs, rel)
        if not changed:
            print(f"patch_d13_sp_align: already applied ({path})")
            continue
        with open(path, "w", encoding="utf-8") as f:
            f.write(new)
        print(f"patch_d13_sp_align: {rel} now rounds the saved-state block and the frame below it to "
              f"8 and realigns astloop's _ast_taken call, all gated by STAGE90_XNU_SP_ALIGN ({path})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())