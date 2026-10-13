#!/usr/bin/env python3
"""Hook the two candidate unmask edges for 999's timer discriminator (999 amendment #2).

WHY. 997/998 both deliver `xnu_live_tick_irq` on the powers of two up to `0x40` (captures :7344 /
:5463) and `0x80` never appears: deliveries stop at tick 64. The timer-freeze lane's verdict could
not cleanly separate two models on the captures -

  * model A: a delivery-side failure (ISENABLER0 bit 20 CLEAR at 998 :3922 with no writer in a
    4.78-7.30 s window is the one measured A fact), and
  * model B: the handed-off kernel's I=1 (post-spin cpsr 0x20000193) was never cleared, so the CPU
    mask - not the distributor - is the wall.

- so 999 measures PRESENCE OF THE EDGE instead: publish at the two places the kernel can lift its own
mask or leave the kernel with the user CPSR restored, and read the record against the frozen count.

THE TWO SITES (read by value; the lead's citations corrected):

  1. `osfmk/arm/machine_routines_asm.s` - `ml_set_interrupts_enabled`'s enable path, immediately
     after its `cpsie i` (:151-153). The citation `machine_routines.c:143-146` is NOT a wrapper: that
     region is `ml_init_interrupt` calling the function, and the whole body is assembly. This is the
     ENABLE edge only - the `disable_interrupts` arm right below is untouched.
  2. `osfmk/arm/cswitch.s` - `thread_exception_return` -> `return_to_user` (:144, :167-170), the
     return-to-user tail shared with `thread_syscall_return` (:137, `b thread_return_join`). The
     citation `status.c:112-123` is `sanitise_cpsr`, a different function (it builds the user CPSR at
     thread-bootstrap time); `thread_exception_return` exists ONLY as assembly, so the hook is the
     `bl` the amendment's parenthetical prescribes ("if asm-only, add a bl-callable C helper at the
     site"). `r1` carries the CPSR the edge is ABOUT to install - the word at `[uss + 0x40]` that
     the tail `msr`s into `spsr_cxsf` - because the post-edge CPSR itself lives in user mode and no
     kernel hook can read it.

MECHANICS. Both hooks save an even number of words before the `bl` (6 and 2): the astloop lesson of
990, by value - its 9-word frame was 4 (mod 8) and `_ast_taken`'s `vld1.32` died on the unaligned
sp. Site 1 saves r2 (the saved cpsr `out` returns) and r8 purely for alignment; site 2 saves r12
(the thread, CALLER-SAVED and consumed two lines later) and lr. The C helper
`entry_timer_edge_probe` lives in `src/platform/darwin13/pe_msm8974.c` beside the raw tick counter,
writes DIRECT (cap-exempt) records through `entry_write_kv`, gates on the raw count reaching 64 and
caps at four events per site. Keys: `xnu_live_edge_mlie_seq/_cpsr/_tick` and
`xnu_live_edge_texr_seq/_cpsr/_tick`.

GATE. `#if STAGE90_XNU_RESIDENT` - the 999 rung's existing switch (no new switch anywhere), the same
one the resident pet and the WDT feed ride. `tools/assemble_arm_layer.sh` appends
`-DSTAGE90_XNU_RESIDENT=$RESIDENT` to ASFLAGS; with the switch 0 the inserted text is not emitted and
the tree's original text compiles byte for byte. The `#if` delimiters themselves are directives -
cpp drops them - so OFF is textually the original.

REBUILD. Both files are ASSEMBLY-layer objects (`out/xnu_asm_obj_d13/`): after staging, re-run
`tools/assemble_arm_layer.sh` with `STAGE90_XNU_RESIDENT=1` EXPORTED, then relink (`build_entry.sh`
only links). No POOL object changes - the C helper is entry-side.

Idempotent: a second run re-verifies the two inserted forms and is a no-op.
"""
import sys

# The marker that proves this patch ran. One spelling, checked in both files.
MARK = "999: TIMER_EDGE"

_ASM1_OLD = (
    "enable_interrupts:\n"
    "    cpsie   i\n"
    "    b       out\n"
)

_ASM1_NEW = (
    "enable_interrupts:\n"
    "    cpsie   i\n"
    "#if STAGE90_XNU_RESIDENT\n"
    "    /* **999: TIMER_EDGE, site 1 - the enable edge itself.** `cpsie i` has just run, so\n"
    "     * `mrs r1, cpsr` records the mask lifting; the record and its per-site cap live in\n"
    "     * `entry_timer_edge_probe` (`src/platform/darwin13/pe_msm8974.c`). Six saved words keep\n"
    "     * sp 8-aligned at the `bl` - the 990 lesson verbatim: astloop's 9-word frame was\n"
    "     * 4 mod 8 and `_ast_taken`'s NEON load died on it. r0/r1 are dead on this path and r2\n"
    "     * is the saved cpsr `out` returns - it must survive the call. */\n"
    "    stmfd   sp!, {r0, r1, r2, r7, r8, lr}\n"
    "    mov     r0, #1\n"
    "    mrs     r1, cpsr\n"
    "    bl      _entry_timer_edge_probe\n"
    "    ldmfd   sp!, {r0, r1, r2, r7, r8, lr}\n"
    "#endif /* STAGE90_XNU_RESIDENT */\n"
    "    b       out\n"
)

_ASM2_OLD = (
    "return_to_user:\n"
    "    /* Restore registers */\n"
    "    LoadThreadRegister(r12)\n"
    "    ldr     sp, [r12, TH_PCB_USS]\n"
)

_ASM2_NEW = (
    "return_to_user:\n"
    "    /* Restore registers */\n"
    "    LoadThreadRegister(r12)\n"
    "#if STAGE90_XNU_RESIDENT\n"
    "    /* **999: TIMER_EDGE, site 2 - the return-to-user edge.** r1 is the CPSR this edge is\n"
    "     * about to install (`[uss + 0x40]`, the word `msr`'d into `spsr_cxsf` two lines down);\n"
    "     * the post-edge CPSR itself is unobservable from the kernel - the edge ends in\n"
    "     * `movs pc, lr`. r12 is CALLER-SAVED and the `bl` clobbers it, so it is saved and the\n"
    "     * macro re-run; two words keep sp 8-aligned at the call (990). */\n"
    "    stmfd   sp!, {r12, lr}\n"
    "    mov     r0, #2\n"
    "    ldr     r1, [r12, TH_PCB_USS]\n"
    "    ldr     r1, [r1, #0x40]\n"
    "    bl      _entry_timer_edge_probe\n"
    "    ldmfd   sp!, {r12, lr}\n"
    "    LoadThreadRegister(r12)\n"
    "#endif /* STAGE90_XNU_RESIDENT */\n"
    "    ldr     sp, [r12, TH_PCB_USS]\n"
)

EDITS = (
    ("osfmk/arm/machine_routines_asm.s", (
        (_ASM1_OLD, _ASM1_NEW),
    )),
    ("osfmk/arm/cswitch.s", (
        (_ASM2_OLD, _ASM2_NEW),
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
                    f"patch_d13_timer_edge: the marker is present in {path} but an inserted form "
                    f"occurs {text.count(new)} times - moved by a hand edit, or half-applied. "
                    "Refusing rather than reporting a patch the switch does not carry."
                )
        return text, False
    for old, new in pairs:
        n = text.count(old)
        if n != 1:
            raise SystemExit(
                f"patch_d13_timer_edge: anchor in {path} occurs {n} times, not once - the tree is "
                f"not the expected revision, or it was edited by hand. Anchor begins: {old[:60]!r}. "
                "Refusing."
            )
    for old, new in pairs:
        text = text.replace(old, new, 1)
    # Sanity by value, before writing: every inserted form in place exactly once.
    for _old, new in pairs:
        if text.count(new) != 1:
            raise SystemExit(f"patch_d13_timer_edge: an inserted form did not land once in {path} - "
                             "refusing to write")
    return text, True


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: patch_d13_timer_edge.py <XNU_TREE>", file=sys.stderr)
        return 2
    root = sys.argv[1]
    for rel, pairs in EDITS:
        path = root + "/" + rel
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except FileNotFoundError:
            print(f"patch_d13_timer_edge: no {path}", file=sys.stderr)
            return 2
        new, changed = patch_text(text, pairs, rel)
        if not changed:
            print(f"patch_d13_timer_edge: already applied ({path})")
            continue
        with open(path, "w", encoding="utf-8") as f:
            f.write(new)
        print(f"patch_d13_timer_edge: {rel} now publishes at the unmask/return edge, gated by "
              f"STAGE90_XNU_RESIDENT ({path})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())