#!/usr/bin/env python3
"""Which command windows write `INT_ENABLE 0x34`, and with which word, at this rung.

WHY THIS EXISTS. 765 section 1 measured that the vendor's own `sdhci_init` writes ELEVEN bits
(`0x01FF0003`) into `INT_ENABLE`, and that every `_status_any = 0` this ladder has read is therefore a
reading about a MASK as much as about the block. Rung 23 acted on that and widened the word -- in the
`st_cmd3_noidx` window, and only there. But the ladder issues THREE response-demanding commands, and each
has its own enable window:

    st_cmd_path       CMD0, CMD1, CMD2   writes  int_enable | ST_SDHCI_INT_RESPONSE   (one bit)
    st_all_send_cid   CMD2               writes  int_enable | ST_SDHCI_INT_RESPONSE   (one bit)
    st_cmd3_noidx     CMD3               writes  int_enable | ST_SDHCI_INT_ENABLE_CMD (five bits, >= 22)

**The scope of a widening is the experiment, and a scope that is only visible by reading three
functions is a scope no build clause can hold.** So this tool computes it from the source and prints it,
one row per store, with the enclosing function and the rung threshold that guards it -- and it refuses on
the one thing that is a defect rather than a choice: **a store to this register whose value is a bare
literal instead of a named constant**, because that is one value with two definitions, and the second
definition is the one nobody re-derives when the first moves.

`st_write32(..., INT_ENABLE, X)` is the shape it looks for. Restores and passthroughs (`was`,
`int_enable`, `wrote`, `enable_before`) are PRINTED and not refused: they write back a value the program
read, which is the opposite of a constant.

Exit 0 when every store names its word; 1 with the offending lines printed; 2 on a usage error.
"""
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ENTRY_SRC = os.path.join(REPO, 'src', 'entry', 'entry_storage.c')

STORE = re.compile(r'st_write32\(\s*ST_HC_MEM_BASE\s*\+\s*ST_SDHCI_INT_ENABLE\s*,\s*(.+?)\s*\);')
FUNC = re.compile(r'^(?:static\s+)?(?:__attribute__\(\([^)]*\)\)\s+)?[\w \*]+?(\w+)\s*\([^;]*\)\s*$')
GUARD = re.compile(r'^#if\s+STAGE90_XNU_STORAGE_PROBE\s*>=\s*(\d+)')

#: The words the ladder names for this register, and what each is.
WORDS = {
    'ST_SDHCI_INT_RESPONSE': ('0x00000001', 'the completion alone - one bit'),
    'ST_SDHCI_INT_ENABLE_CMD': ('0x000f0001', 'the completion with TIMEOUT, CRC, END_BIT, INDEX'),
}

#: Identifiers an expression may name a word THROUGH, i.e. a value the program read. A store whose
#: expression is a disjunction of these and nothing else is a passthrough.
IDENT = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')

#: The assignment shape that BUILDS a word for a store: `<name> = <expr>;`
ASSIGN = re.compile(r'^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.+?);')


def classify(expr):
    """What a store's expression IS, decided per OPERAND and never by substring.

    **A substring test is the bug this function exists to avoid, and it bit the first version of this
    tool**: `int_enable | (uint32_t)ST_SDHCI_INT_RESPONSE` CONTAINS `int_enable`, so a `\"int_enable\" in
    expr` test called every command window a passthrough and the census printed *zero* five-bit
    windows. The reading is per operand: split on `|`, strip casts, and ask what each operand is.
    """
    parts = [re.sub(r'\((?:uint32_t|u32)\)', '', q).strip() for q in expr.split('|')]
    names = [q for q in parts if q in WORDS]
    if names:
        return 'constant:' + names[0]
    if any(re.search(r'0[xX][0-9a-fA-F]', q) for q in parts):
        return 'LITERAL'
    if all(q in ('0', '0u', '0U') for q in parts):
        return 'zero'
    if all(IDENT.match(q) or re.fullmatch(r'0', q) for q in parts):
        return 'passthrough'
    return 'other'


def windows(text=None):
    """One row per `INT_ENABLE` store: (line, function, rung threshold, expression, kind)."""
    if text is None:
        with open(ENTRY_SRC, encoding='utf-8', errors='replace') as fh:
            text = fh.read()
    rows, func, guard, stack = [], '(none)', None, []
    for n, line in enumerate(text.split('\n'), 1):
        m = GUARD.match(line)
        if m:
            stack.append(int(m.group(1)))
            guard = max(stack) if stack else None
        elif line.startswith('#endif'):
            if stack:
                stack.pop()
            guard = max(stack) if stack else None
        if not line.startswith((' ', '\t')):
            m = FUNC.match(line)
            if m and m.group(1) not in ('if', 'for', 'while', 'switch', 'return'):
                func = m.group(1)
        m = STORE.search(line)
        if not m:
            continue
        expr = m.group(1).strip()
        rows.append((n, func, guard, expr, classify(expr)))
    return rows


def assignments(text=None):
    """One row per line that BUILDS a word for a store: (line, function, rung threshold, name, expr).

    This is the other half of the census and the half the stores do not carry: the stores write a
    variable, and *which* word the variable holds is decided at the assignment, under its own `#if`.
    """
    if text is None:
        with open(ENTRY_SRC, encoding='utf-8', errors='replace') as fh:
            text = fh.read()
    out, func, stack = [], '(none)', []
    for n, line in enumerate(text.split('\n'), 1):
        m = GUARD.match(line)
        if m:
            stack.append(int(m.group(1)))
        elif line.startswith('#endif'):
            if stack:
                stack.pop()
        if not line.startswith((' ', '\t')):
            m = FUNC.match(line)
            if m and m.group(1) not in ('if', 'for', 'while', 'switch', 'return'):
                func = m.group(1)
        m = ASSIGN.match(line)
        if m and any(k in m.group(2) for k in WORDS):
            kind = classify(m.group(2))
            if kind.startswith('constant:') or kind == 'LITERAL':
                out.append((n, func, max(stack) if stack else None, m.group(1), m.group(2).strip(),
                            kind))
    return out


def report(rows=(), assigns=(), out=sys.stdout):
    w = out.write
    w(f'{os.path.relpath(ENTRY_SRC, REPO)}  -- the words this ladder names for INT_ENABLE (0x34),\n'
      f'and the stores that write them\n\n')
    w('  the assignments that BUILD a word:\n\n')
    w('  %-6s %-22s %-8s %-12s %-34s %s\n' % ('line', 'function', 'guarded', 'names', 'expression',
                                              'word'))
    for n, func, guard, name, expr, kind in assigns:
        if kind.startswith('constant:'):
            k = kind.split(':', 1)[1]
            val = WORDS[k][0] + '  (' + WORDS[k][1] + ')'
        else:
            val = 'A BARE LITERAL -- refused'
        w('  %-6d %-22s %-8s %-12s %-34s %s\n'
          % (n, func, ('>= %d' % guard) if guard is not None else '-', name, expr, val))
    # What each store's expression RESOLVES to: the store writes a name, and the name holds one of
    # two words depending on the build. Binding by line number would be wrong -- the assignment and
    # the store are different lines by construction.
    amap = {}
    for n, func, guard, name, expr, kind in assigns:
        amap.setdefault(name, []).append((kind, guard))
    w('\n  every store to 0x34:\n\n')
    w('  %-6s %-22s %-8s %-38s %s\n' % ('line', 'function', 'store in', 'expression', 'what it is'))
    for n, func, guard, expr, kind in rows:
        if kind.startswith('constant:'):
            name = kind.split(':', 1)[1]
            val = WORDS[name][0] + '  (' + WORDS[name][1] + ')'
        elif kind == 'passthrough':
            val = 'a value the program read'
            bare = re.sub(r'\((?:uint32_t|u32)\)', '', expr).strip()
            if IDENT.match(bare) and bare in amap:
                val = ('backing ' + ', '.join(
                    '%s (built under %s)' % (WORDS[k.split(':', 1)[1]][0] if k.startswith('constant:') else 'A LITERAL',
                                             ('>= %d' % g) if g is not None else 'no guard')
                    for k, g in amap[bare]))
        elif kind == 'zero':
            val = 'zero -- the absence of an enable, and no second definition'
        elif kind == 'LITERAL':
            val = 'A BARE LITERAL -- refused'
        else:
            val = '(not a word this tool knows)'
        w('  %-6d %-22s %-8s %-38s %s\n'
          % (n, func, ('>= %d' % guard) if guard is not None else '-', expr, val))
    wide = [a for a in assigns if a[5] == 'constant:ST_SDHCI_INT_ENABLE_CMD']
    narrow = [a for a in assigns if a[5] == 'constant:ST_SDHCI_INT_RESPONSE']
    w('\n  the FIVE-BIT word (`0x000f0001`) is built at %d line(s): %s\n'
      % (len(wide), ', '.join('%s:%d (>= %s)' % (a[1], a[0], a[2]) for a in wide) or '(none)'))
    w('  the ONE-BIT word (`0x00000001`) is built at %d line(s): %s\n'
      % (len(narrow), ', '.join('%s:%d (>= %s)' % (a[1], a[0], a[2]) for a in narrow) or '(none)'))
    wide_names = {a[3] for a in wide}
    stores_wide = [r for r in rows
                   if r[4].endswith('ST_SDHCI_INT_ENABLE_CMD')
                   or re.sub(r'\((?:uint32_t|u32)\)', '', r[3]).strip() in wide_names]
    w('  and the FIVE-BIT word reaches %d store(s), all in: %s\n'
      % (len(stores_wide), ', '.join(sorted({r[1] for r in stores_wide})) or '(none)'))
    return wide, narrow


SELFTEST = (
    # (source lines, expected refusal)
    ('static void f(void)\n{\n'
     '    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, x | (uint32_t)ST_SDHCI_INT_RESPONSE);\n'
     '    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, x);\n}\n', False),
    ('static void f(void)\n{\n'
     '    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, 0x00000001u);\n}\n', True),
    ('static void f(void)\n{\n'
     '#if STAGE90_XNU_STORAGE_PROBE >= 22\n'
     '    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, x | (uint32_t)ST_SDHCI_INT_ENABLE_CMD);\n'
     '#endif\n}\n', False),
    ('static void f(void)\n{\n'
     '    st_write32(ST_HC_MEM_BASE + ST_SDHCI_INT_ENABLE, x | ST_OTHER_CONSTANT);\n}\n', False),
)


def selftest():
    bad = 0
    for src, want_refusal in SELFTEST:
        rows = windows(src)
        got = any(r[4] == 'LITERAL' for r in rows)
        if got != want_refusal:
            print(f'  {src.splitlines()[-1].strip()!r}: expected refusal={want_refusal}, got {got}')
            bad += 1
    print(f'selftest ok: {len(SELFTEST)} cells, and a store that names its word is not refused')
    return 1 if bad else 0


def main(argv):
    if '--selftest' in argv:
        return selftest()
    if len(argv) > 1:
        print(__doc__.strip().split('\n')[0])
        print('usage: report_int_enable_windows.py [--selftest]')
        return 2
    if not os.path.exists(ENTRY_SRC):
        print(f'no entry source at {ENTRY_SRC}')
        return 2
    rows = windows()
    assigns = assignments()
    wide, narrow = report(rows, assigns)
    literals = [r for r in rows if r[4] == 'LITERAL'] + [a for a in assigns if a[5] == 'LITERAL']
    for n, func, guard, expr, _ in literals:
        print(f'REFUSED {ENTRY_SRC}:{n} in {func}: a store to INT_ENABLE whose value is the literal '
              f'{expr!r}.\n        Name the word with a constant: a literal here is one value with two '
              f'definitions, and the\n        second is the one nobody re-derives when the first moves.')
    return 1 if literals else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
