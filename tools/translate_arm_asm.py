#!/usr/bin/env python3
"""
Translate the Darwin-assembler constructs in XNU's ARM `.s` files into GNU-as equivalents.

    ./tools/translate_arm_asm.py IN OUT

Why this exists, and what it is not. `experiment-142` measured that `--target=armv7-apple-darwin`
assembles XNU's ARM assembly and `--target=armv7-none-eabi` does not — but `experiment-150` then
measured that **LLVM's `ld64.lld` cannot link 32-bit ARM Mach-O** (`unhandled relocation type`, in lld
14 and 15, while `-arch arm64` links). So Mach-O compiles here and cannot link here, and ELF is the
only route to a **linked image**. That makes these constructs the last four files.

**It is a dialect translation, not a source change.** The tree is never written to; the output goes
beside the object files. It is the same category as the `objcopy --redefine-sym` step in
`assemble_arm_layer.sh`, which exists because `asm.h`'s `EXT(x)` is `_##x` under Apple's convention.

Four constructs, each verified against the EABI assembler before being written down:

| Darwin | GNU as | where |
| --- | --- | --- |
| `.section __DATA, __data` | `.section .data,"aw",%progbits` | `osfmk/arm/data.s:41` |
| `.section __DATA, __const` | `.section .rodata,"a",%progbits` | `osfmk/arm/data.s:100` |
| `.const` | `.section .rodata,"a",%progbits` | `osfmk/arm/WKdmData_new.s:29` |
| `.thumb_func name` | `.thumb_func` (bare; applies to the next label) | `lz4_decode_armv7NEON.s:133` |
| `.macro NAME` + `$N` in the body, invoked as `NAME a,b` | `.macro NAME p0,p1` + `\\pN\\()` | `machine_routines_asm.s:570` (9 sites), `lz4_decode_armv7NEON.s` (10 sites) |

The last one is the substantive one and it is **general**: any parameterless `.macro` whose body
refers to `$N` gets the parameters it is evidently being called with, discovered by scanning the body
for the largest `$N`. That is a mechanical rule, not a per-file patch, and it means a file Apple adds
later with the same idiom translates without editing this script.

Every substitution is counted and the count is reported, because a translation that silently matched
nothing would leave the file failing in a way that looks exactly like the original problem — the
defect class this project has met repeatedly.
"""

import os
import re
import sys

# Ordered: the longest/most specific first, so `.section __DATA, __data` is not caught by a later
# broader rule.
SIMPLE = [
    (re.compile(r"^(\s*)\.section\s+__DATA\s*,\s*__data\b.*$", re.M),
     r'\1.section .data,"aw",%progbits'),
    (re.compile(r"^(\s*)\.section\s+__DATA\s*,\s*__const\b.*$", re.M),
     r'\1.section .rodata,"a",%progbits'),
    (re.compile(r"^(\s*)\.section\s+__DATA\s*,\s*__common\b.*$", re.M),
     r'\1.section .bss,"aw",%nobits'),
    (re.compile(r"^(\s*)\.const\s*$", re.M),
     r'\1.section .rodata,"a",%progbits'),
    # `.thumb_func _foo` names the function; GNU's form is bare and applies to the following label,
    # which in every one of these files is the very next line.
    (re.compile(r"^(\s*)\.thumb_func\s+[A-Za-z_.][A-Za-z0-9_.]*\s*$", re.M),
     r"\1.thumb_func"),
]

# `NAME` at the start of a line, preceded by a `.macro NAME` with nothing after it.
# `[ \t]*` and not `\s*` for the indent: `\s` matches newlines, so `^(\s*)\.macro` swallowed the
# preceding line break into the captured indent and produced `.macro` lines with a `\n` in them. The
# substitution then wrote a malformed directive rather than failing outright, which is the kind of
# defect that reads like something else entirely.
MACRO_DECL = re.compile(r"^([ \t]*)\.macro[ \t]+([A-Za-z_.][A-Za-z0-9_.]*)[ \t]*$", re.M)
# Both spellings: Apple's assembler takes `.endmacro`, GNU takes `.endm`.
MACRO_END = re.compile(r"^[ \t]*\.end(?:m|macro)\b", re.M)
ARG_USE = re.compile(r"\$([0-9])")


def translate_macros(text):
    """Parameterless `.macro`s whose bodies use `$N` get parameters named p0..pN."""
    out = []
    pos = 0
    conversions = 0
    for m in MACRO_DECL.finditer(text):
        out.append(text[pos:m.start()])
        indent, name = m.group(1), m.group(2)
        end = MACRO_END.search(text, m.end())
        if not end:
            out.append(m.group(0))     # unterminated; leave it exactly as it was
            pos = m.end()
            continue
        body = text[m.end():end.start()]
        used = ARG_USE.findall(body)
        if not used:
            out.append(m.group(0))     # genuinely takes no arguments
            pos = m.end()
            continue
        n = max(int(u) for u in used) + 1
        params = ",".join(f"p{i}" for i in range(n))
        # `$N` -> `\pN\()`. The `\()` is required: the very next character is always `_` or `]`
        # (`L$0_wordwise`, `[$0]`), and a bare `\p0_` is read as the parameter named `p0_`.
        newbody = ARG_USE.sub(lambda mm: "\\p%s\\()" % mm.group(1), body)
        out.append(f"{indent}.macro {name} {params}")
        out.append(newbody)
        out.append(text[end.start():end.end()])
        conversions += 1
        pos = end.end()
    out.append(text[pos:])
    return "".join(out), conversions


def translate(text):
    counted = []
    for pattern, repl in SIMPLE:
        text, n = pattern.subn(repl, text)
        counted.append(n)
    text, n = translate_macros(text)
    counted.append(n)
    return text, counted


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[2], file=sys.stderr)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    new, counted = translate(text)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    with open(dst, "w", encoding="utf-8") as fh:
        fh.write(new)
    total = sum(counted)
    # The count goes to stdout so the caller can decide whether the translation was needed at all,
    # and can say so when it was not.
    print(total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
