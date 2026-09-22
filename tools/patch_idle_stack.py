#!/usr/bin/env python3
"""
Point `osfmk/arm/cswitch.s`'s `Idle_context` at a stack of its own.

    ./tools/patch_idle_stack.py IN OUT
    ./tools/patch_idle_stack.py IN --count-only

Why this exists. `Idle_context` puts the idle thread on the **interrupt stack**:

    ldr     r12, [r9, ACT_CPUDATAP]         // Get current cpu
    ldr     sp, [r12, CPU_ISTACKPTR]        // Switch to interrupt stack
    LOAD_ADDR_PC(cpu_idle)

and `cpu_data->istackptr` is the same field the exception vectors read to place a *handler's* stack
(`locore.s`, `ldr sp, [sp, CPU_ISTACKPTR]`). So the idle thread and every interrupt handler run on one
stack, and the handler's 5th and 6th pushed words land on the idle code's own saved return address -
which is what experiment 518's run caught at the instruction level: the idle exit's closing
`pop {fp, pc}` returned into a timebase value, and the frame Apple's panic printed was authentic,
because the damage is one word *above* it.

Moving `cpu_data->istackptr` cannot fix that. That was 518's arm, and it moved both stacks, because both
readers read that one field - `SS_SP == istackptr - 16` before and after, so its run returned the same
panic and could not falsify anything. Breaking it needs the idle thread's stack to be **different memory
at the same time**, and there is exactly one place where that can be said: here, where the idle thread's
stack is chosen.

What the substitution is, and why it is this and not `LOAD_ADDR`. The non-slidable `LOAD_ADDR(reg,label)`
is `ldr reg, L_##label`, which needs a matching `LOAD_ADDR_GEN_DEF(label)` somewhere - and a label
written as `LOAD_ADDR(sp, EXT(x))` glues `L_` onto the whole string and asks LLVM's assembler for the
variant `L_EXT(x)`, which it rejects ("invalid variant 'x'"). So the patch writes the kernel's own
non-slidable idiom by hand, which is the same two pieces `data.s` emits for `intstack_top`:

    ldr     sp, L_stage90_idle_stack_top            // the load
    ...
    .align 2
L_stage90_idle_stack_top:
    .long   stage90_idle_stack + STAGE90_IDLE_STACK_SIZE

The literal goes *after* the function's tail branch (`LOAD_ADDR_PC(cpu_idle)` is `b EXT(cpu_idle)`, which
never falls through), and the patch refuses a body whose last instruction is not a branch, because a
literal in the fall-through path is code the CPU would execute.

The patch is bounded to `Idle_context`, and that matters: `Shutdown_context` a few lines up has the same
`ldr sp, [r12, CPU_ISTACKPTR]`, and it must keep it (shutdown runs on the interrupt stack on purpose, and
nothing in this project measures it). The substitution count is reported, and a source that matches
nothing is an error rather than a no-op - the defect class this project has met repeatedly, where a
transformation that silently does nothing looks exactly like one that worked.

**This is a semantic change and it says so**, unlike `translate_arm_asm.py`, which is a dialect
translation. The output goes beside the object files, in the same mirror that translator writes to -
**and that mirror is not what keeps the tree safe**: it is *directories that are real and files that are
symlinks into the tree*, so the destination is a symlink until something replaces it, and `open(dst, 'w')`
writes the tree. This tool's first run did exactly that, to `osfmk/arm/cswitch.s`, on a comment that said
it could not. Writes go through a temporary and `os.replace` for that reason, which is the same mechanism
(`rename(2)`) that makes the translate step's `mv -f` safe.
"""

import argparse
import os
import re
import sys

# The declaration is on its own line, so the body runs from here to the next top-level label.
LABEL = re.compile(r'^LEXT\(Idle_context\)\s*$', re.M)
# Any top-level label or directive that ends the function. `LEXT(`/`EXT(` are the tree's own spelling
# for a global/local label; `.text` and `.align` mark the next block.
# `#endif` is deliberately *not* one of these: `Idle_context` has a `#if __ARM_VFP__` block of its own,
# and treating it as the end of the function cut the body short of the line being patched.
#
# The directives are matched with leading whitespace allowed and the labels are not: XNU writes
# `LEXT(Idle_context)` at column 0 and indents `.text`, `.align` and `.globl` - so a pattern anchored
# at column 0 for the directives ended the body at the *next function's label* instead of at its own
# `.text`, and the body then read as ending in `.globl EXT(Idle_load_context)`.
NEXT = re.compile(r'^(?:LEXT\(|EXT\(|[ \t]+\.(?:text|globl|align|data)\b)', re.M)

STACK_LOAD = re.compile(
    r'^([ \t]*)ldr[ \t]+sp,[ \t]*\[r12,[ \t]*CPU_ISTACKPTR\][^\n]*$', re.M)

LITERAL_LABEL = 'L_stage90_idle_stack_top'
REPLACEMENT = (
    r'\1ldr' + '\t\t' + r'sp, ' + LITERAL_LABEL + '\t\t\t// 519: a stack of the idle thread\n'
    r'\1' + '\t\t\t\t\t\t\t// of its own, so the handler and the\n'
    r'\1' + '\t\t\t\t\t\t\t// idle loop stop being one register\n')

# The literal block, emitted at the end of the body. `.long <sym> + <size>` is the same shape
# `LOAD_ADDR_GEN_DEF` emits, with the size folded in so the idle thread starts at the array's top.
LITERAL_BLOCK = (
    '\n\t.align 2\n' + LITERAL_LABEL + ':\n'
    '\t.long\tstage90_idle_stack + STAGE90_IDLE_STACK_SIZE\n')

# The tail has to be a branch, or the literal this patch appends is in the fall-through path and the
# CPU executes it as an instruction. `LOAD_ADDR_PC(x)` counts: in this build (`SLIDABLE=0`) it *is*
# `b EXT(x)`, and it is what is written in the source - the expansion is the preprocessor's.
BRANCH = re.compile(r'(?:\b(?:b|bx|bl)\b|LOAD_ADDR_PC\()')


def body_span(text):
    m = LABEL.search(text)
    if not m:
        return None
    start = m.end()
    n = NEXT.search(text, start)
    return (start, n.start() if n else len(text))


def last_instruction(text):
    """(offset_end, line) for a body's last line that is neither blank nor a comment.

    The comment matters: the body runs to the next `LEXT(`/`.text`, and XNU puts the *next* function's
    doc comment between them - so the last non-empty line of the span is that comment's `*/`, and a
    check on it says the wrong thing about a body that ends in a branch.
    """
    pos = 0
    found = (None, '')
    in_c = False
    for line in text.split('\n'):
        s = line.split('//')[0].strip()
        if in_c:
            if '*/' in s:
                in_c = False
            pos += len(line) + 1
            continue
        if s.startswith('/*'):
            if '*/' not in s:
                in_c = True
            pos += len(line) + 1
            continue
        if s:
            found = (pos + len(line), s)
        pos += len(line) + 1
    return found


def patch(text):
    span = body_span(text)
    if span is None:
        return None, 'no LEXT(Idle_context) line'
    lo, hi = span
    body = text[lo:hi]
    new, count = STACK_LOAD.subn(REPLACEMENT, body, count=1)
    if count != 1:
        return None, ('no `ldr sp, [r12, CPU_ISTACKPTR]` in Idle_context\'s body (%d bytes from its '
                      'label) - either the source moved or the patch has already been applied, and '
                      'both must stop the build rather than ship an image whose idle thread is still '
                      'on the interrupt stack' % (hi - lo))
    end, tail = last_instruction(new)
    if not BRANCH.search(tail):
        return None, ('Idle_context\'s last instruction is [%s], which is not a branch - the literal '
                      'this patch appends would sit in the fall-through path and the CPU would '
                      'execute it' % tail)
    # The literal goes directly after the branch that cannot fall through into it, and *not* at the end
    # of the span: the span ends at the next function's doc comment, and a literal between a comment
    # and the `.text` it describes is a reading of the file this patch has no business changing.
    return text[:lo] + new[:end] + LITERAL_BLOCK + new[end:] + text[hi:], None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('dst', nargs='?')
    ap.add_argument('--count-only', action='store_true',
                    help='report the substitution count and write nothing')
    args = ap.parse_args()

    with open(args.src, encoding='utf-8', errors='replace') as fh:
        text = fh.read()

    out, err = patch(text)
    if err:
        print('patch_idle_stack: %s: %s' % (args.src, err), file=sys.stderr)
        return 2

    if args.count_only:
        print(1)
        return 0

    if args.dst:
        # **Written through a temporary and renamed into place, and that is not a style choice.**
        # `assemble_arm_layer.sh`'s mirror is *directories that are real and files that are symlinks
        # into the tree*, so `open(dst, 'w')` on a path that is still a symlink writes the **tree** -
        # which the first version of this tool did, on its first run, to
        # `external/xnu-4570.1.46/osfmk/arm/cswitch.s`. `os.replace` is `rename(2)`: it replaces the
        # symlink rather than following it, so no path this tool opens for writing can reach the tree.
        # The translate step next door is safe by the same mechanism (`mv -f`), not by care.
        tmp = args.dst + '.patchidle.tmp'
        with open(tmp, 'w', encoding='utf-8') as fh:
            fh.write(out)
        os.replace(tmp, args.dst)
    else:
        sys.stdout.write(out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
