#!/usr/bin/env python3
"""Refuse an `ST_TLMM_SDC1_EXPECT` that matches no board the device's own device tree declares.

`src/entry/entry_storage.c` compares the live TLMM SDC1 pad register at `0xFD512044`
against a word built from `qcom,pad-pull-on` and `qcom,pad-drv-on`. The comment beside it
cites `msm8974pro-ac-pm8941-mtp-v5.dts:25-29` as *the Mi 4's own board file*, and that
citation names a file this repository does not contain:

  * **no cancro or Xiaomi device tree exists in the vendor checkout at all** -- it holds
    Qualcomm's reference boards (MTP, CDP, FLUID, LIQUID) and nothing else; and
  * `external/` is `.gitignore`d, so the vendor checkout is not in the repository either.

What *does* describe this device is the `dt.img` its own stock boot image carries --
and that is `.gitignore`d too (`xiaomi4-cancro-backup-*/`). So the provenance of the
ladder's only board-derived constant was, until this check, two citations to files that a
clone cannot resolve. `records/sdc1-pad-candidates.txt` is the repair: a TRACKED record of
what the device's own tree declares, written by `tools/derive_sdc1_pads.py --write-record`
together with the sha256 of the input it was read from.

This check reads the **tracked record** and not the device tree, so it runs on a clone
where `dt.img` does not exist. It refuses when the constant the source declares matches no
candidate the record holds. It does NOT refuse on ambiguity: four of this device's trees
declare the same word and the bootloader's choice is not established -- so an ambiguous
record prints its candidates and passes, and a *no*-match is the only refusal. That is the
honest scope: this can catch a constant that no board on this device implies, and it
cannot catch one that the wrong board implies.

**A third refusal, added by 782: a constant set to an OFF word.** The same declarations
imply a second word -- the state with the controller powered down (`qcom,pad-pull-off` /
`qcom,pad-drv-off` through the same arithmetic), which shares the pull fields and differs in
drive. The guard asks *does the register already hold the word I expect*, so a constant set
to an off word would report a powered-down pad as already configured. One register, two
states, one name for both: the same defect class 779 found one level down.

**A second refusal, added by 781: the record's controller census.** The same record now
carries one `# node 0x........ <name> core 0x........ window 0x.. status .. trees n/m`
line per controller the device's own tree declares, and this check refuses when the address
`ST_HC_MEM_BASE` holds is not a node the record carries, is carried with a status other
than `ok`, or is paired with a `core` base that is not `ST_CORE_MEM_BASE`. That is a check
of a different kind from the one above -- it is about the *addresses the ladder uses* being
a controller this device's own device tree enables, which no earlier check could see: the
source's own comment says what the block is called, and a name is not an address.

Usage:
    tools/check_sdc1_pad_expectation.py
    tools/check_sdc1_pad_expectation.py --selftest
"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from derive_sdc1_pads import (HDRV, PULL, derive, rail_pmic, rail_verdict, source_bases,  # noqa: E402
                              source_constants, DEFAULT_RECORD, ENTRY_SRC, REPO)

CANDIDATE = re.compile(r'^#\s*candidate\s+(0x[0-9a-fA-F]{8})\s')
SHA_LINE = re.compile(r'^#\s*sha256\s+([0-9a-f]{64})\s*$')
INPUT_LINE = re.compile(r'^#\s*input\s+(\S+)\s*$')
OFFWORD_LINE = re.compile(r'^#\s*offword\s+(0x[0-9a-fA-F]{8})\s')
NODE_LINE = re.compile(r'^#\s*node\s+(0x[0-9a-fA-F]+)\s+(\S+)\s+core\s+(0x[0-9a-fA-F]+)'
                       r'\s+window\s+(0x[0-9a-fA-F]+)\s+status\s+(\S+)\s+trees\s+(\d+)/(\d+)')
RAIL_LINE = re.compile(r'^#\s*rail\s+(\S+)\s+(\S+)\s+name\s+(\S+)\s+pmic\s+(\S+)\s+'
                       r'init-uv\s+(\S+)\s+set\s+(\S+)\s+always-on\s+(\S+)\s+trees\s+(\S+)')
SPMI_LINE = re.compile(r'^#\s*spmi\s+(\S+)\s+reg\s+(.*?)\s+reg-names\s+(\S+)\s+'
                       r'children\s+(\S+)\s+trees\s+(\S+)')


def candidates(path=DEFAULT_RECORD):
    """(candidate words, the input's sha256, the input's path) from the tracked record."""
    words, sha, inp = [], None, None
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            m = CANDIDATE.match(line)
            if m:
                words.append(int(m.group(1), 16))
            m = SHA_LINE.match(line)
            if m:
                sha = m.group(1)
            m = INPUT_LINE.match(line)
            if m:
                inp = m.group(1)
    return words, sha, inp


def nodes(path=DEFAULT_RECORD):
    """The record's controller census, as (base, name, core, window, status, n, total)."""
    out = []
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            m = NODE_LINE.match(line)
            if m:
                out.append((int(m.group(1), 16), m.group(2), int(m.group(3), 16),
                            int(m.group(4), 16), m.group(5), int(m.group(6)), int(m.group(7))))
    return out


def offwords(path=DEFAULT_RECORD):
    """The record's OFF words: what its declarations imply with the controller powered down."""
    out = []
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            m = OFFWORD_LINE.match(line)
            if m:
                out.append(int(m.group(1), 16))
    return out


def offword_verdict(offs, want):
    """(ok, why) for the guard's constant against the register's two-valued story.

    **One register, two states, one name for both -- this project's oldest defect class.**
    `ST_TLMM_SDC1_EXPECT` is the word the vendor's `CORE_PWRCTL_BUS_ON` leaves, i.e. the ON
    word. The same board declarations also imply an OFF word (same pull fields, drive at
    zero), and the arm's guard asks *does the register already hold the word I expect* --
    so a constant set to an OFF word would make the arm answer **the pads are already
    configured** about a pad with its drive fields at zero. That is exactly the mistake
    779 found one level down (a value that is *a* board's being read as *the* device's), so
    it is a refusal and not a note.

    Pure, so its cells need no device tree.
    """
    if want in offs:
        return False, ('0x%08x is an OFF word in the record -- the value these declarations '
                       'imply with the controller powered DOWN. The guard compares the live '
                       'register against the ON word, so a constant set to an off word would '
                       'report a powered-down pad as already configured' % want)
    return True, ('0x%08x is not any of the %d off word(s) the record declares (%s)'
                  % (want, len(offs), ', '.join('0x%08x' % o for o in sorted(offs))))


def node_verdict(census, hc, core):
    """(ok, why) for the ladder's own two window bases against the record's census.

    **Why this is a check and not a sentence.** `src/entry/entry_storage.c` says in prose
    which controller it reads; the two `#define`s beside that prose are the addresses a run
    actually dereferences, and the record carries what the device's own device tree
    declares at them. A mismatch means the ladder is reading an address no tree in the
    device's own file enables -- which is the difference between *the card did not answer*
    and *the run was never looking at the card*. Pure, so its cells can be tested without a
    device tree, which is the same reason `derive` has cells.
    """
    if not census:
        return False, ('the record carries no `# node 0x........ ...` line, so nothing in it '
                       'says the addresses the ladder uses are controller(s) this file declares')
    match = [n for n in census if n[0] == hc]
    if not match:
        return False, ('no `# node` line names 0x%08x, the address ST_HC_MEM_BASE holds; '
                       'the record declares %s'
                       % (hc, ', '.join('0x%08x' % n[0] for n in census)))
    base, name, ncore, window, status, ntree, total = match[0]
    if status != 'ok':
        return False, ('the record carries 0x%08x with status %s: the node the ladder reads '
                       'is not enabled in the file the record was read from' % (base, status))
    if ncore != core:
        return False, ('the record pairs 0x%08x with core 0x%08x while ST_CORE_MEM_BASE is '
                       '0x%08x -- not the pair this node declares' % (base, ncore, core))
    return True, ('0x%08x is %s, core 0x%08x, window 0x%x, status ok on %d/%d trees'
                  % (base, name, ncore, window, ntree, total))


def rails(path=DEFAULT_RECORD):
    """The record's rails: (supply, path, name, pmic, init-uv, set, always-on, trees)."""
    out = []
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            m = RAIL_LINE.match(line)
            if m:
                out.append(m.groups())
    return out


def spmi_declared(path=DEFAULT_RECORD):
    """Every PMIC name the record's own `# spmi` lines declare, unioned across them."""
    kids = []
    with open(path, encoding='utf-8', errors='replace') as fh:
        for line in fh:
            m = SPMI_LINE.match(line)
            if m and m.group(4) != '-':
                # `;`-separated, because a PMIC node name is itself `qcom,pm8941@2`
                kids += [k for k in m.group(4).split(';') if k]
    return sorted(set(kids))


def rail_census_verdict(rail_rows, children):
    """(ok, why) for the record's rails against the PMICs the same record declares. Pure.

    **Why this is a refusal and not a note.** The record's `# rail` lines say which regulator the
    eMMC's two supplies resolve to; its `# spmi` lines say which PMICs the device's own arbiter
    declares. A rail attributed to a chip that is not among them is **a citation to another
    board standing where a reading of this one should be** -- 779's defect, one level down, and
    the same shape as 781's node refusal and 782's off-word refusal. Pure, so its cells need no
    device tree, and it reuses `rail_verdict` rather than defining a second spelling of the rule
    (`rail_pmic` is the token rule; a second implementation would be two producers of one name).
    """
    if not rail_rows:
        return False, ('the record carries no `# rail <supply> <path> name ...` line, so nothing '
                       'in it says what the controller\'s two supplies resolve to')
    if not children:
        return False, ('the record carries rails but no `# spmi ... children ...` line, so there '
                       'is no declared PMIC set to attribute them to')
    for _supply, rpath, name, _pmic, _uv, _set, _always, _trees in rail_rows:
        ok, why = rail_verdict(name, children)
        if not ok:
            return False, (f'{why}; the rail line names path {rpath}. A rail attributed to a '
                           f'chip this record does not declare is another board\'s reading')
    return True, ('%d rail(s) on %d declared PMIC(s); every rail\'s token is the last four '
                  'characters of one of them: %s'
                  % (len(rail_rows), len(children), ', '.join(children)))


def check(record=DEFAULT_RECORD, source=ENTRY_SRC, out=sys.stdout):
    w = out.write
    words, sha, inp = candidates(record)
    if not words:
        w(f'REFUSED {record}: no `# candidate 0x........` line. The record is the tracked '
          f'reading of the\n        device\'s own device tree; without it the constant '
          f'below cannot be checked at all.\n')
        return 1
    if sha is None or inp is None:
        w(f'REFUSED {record}: a candidate list with no `# input` and `# sha256` header. The '
          f'whole point of the\n        record is that it names the input it was read from, '
          f'which is a file this repository does not hold.\n')
        return 1

    bases = source_bases(source)
    if bases is None:
        w(f'REFUSED {source}: could not read ST_HC_MEM_BASE and ST_CORE_MEM_BASE. The '
          f'addresses the ladder\n        dereferences are what the census below is '
          f'checked against; an unreadable source refuses\n        rather than passing '
          f'quietly.\n')
        return 1
    census = nodes(record)
    ok, why = node_verdict(census, bases['hc'], bases['core'])
    w(f'source    ST_HC_MEM_BASE = 0x{bases["hc"]:08x}  '
      f'ST_CORE_MEM_BASE = 0x{bases["core"]:08x}\n')
    w(f'record    {len(census)} controller node(s) declared\n')
    if not ok:
        w(f'REFUSED {why}\n')
        return 1
    w(f'OK: {why}\n')

    rail_rows = rails(record)
    kids = spmi_declared(record)
    ok_rail, why_rail = rail_census_verdict(rail_rows, kids)
    w(f'record    {len(rail_rows)} rail line(s), {len(kids)} declared PMIC(s)\n')
    if not ok_rail:
        w(f'REFUSED {why_rail}\n')
        return 1
    w(f'OK: {why_rail}\n')
    for supply, rpath, name, pmic, uv, st, always, trees in rail_rows:
        w(f'          {supply:<14} {rpath}\n'
          f'          {"":<14} name {name:<10} pmic {pmic:<6} init-uv {uv:<8} set {st:<3} '
          f'always-on {always:<4} trees {trees}\n')
    if any(r[6] == 'yes' for r in rail_rows):
        w('    NOTE a rail line says `always-on yes`: this device\'s own trees mark other\n'
          '         rails always-on and mark the eMMC\'s two not at all, so such a line means\n'
          '         the record was written from a different device tree than the one above.\n')

    src = source_constants(source)
    if src is None:
        w(f'REFUSED {source}: could not read both ST_TLMM_DT_ arrays. This check refuses '
          f'on an unreadable\n        source rather than passing quietly -- the constants it '
          f'compares are the point.\n')
        return 1

    pull, drv = src
    want = derive(pull, drv)
    w(f'source    {os.path.relpath(source, REPO)} declares pull-on {pull} drv-on {drv}\n')
    w(f'          -> ST_TLMM_SDC1_EXPECT = 0x{want:08x}\n')
    offs = offwords(record)
    ok_off, why_off = offword_verdict(offs, want)
    if not ok_off:
        w(f'REFUSED {why_off}\n')
        return 1
    w(f'OK: {why_off}\n')
    w(f'record    {os.path.relpath(record, REPO)}  (input {inp}, sha256 {sha[:16]}...)\n')
    w(f'          {len(words)} distinct word(s) declared: '
      f'{", ".join("0x%08x" % x for x in sorted(set(words)))}\n')

    if want in words:
        w(f'OK: 0x{want:08x} is a word some tree on this device declares.\n')
        if len(set(words)) > 1:
            w('    NOTE this is not a verdict on which tree the bootloader selects: the '
              'record holds more\n         than one word and the choice is made on the '
              'device. The guard\'s else-arm therefore\n         means "the register did '
              'not hold THIS word", not "the vendor\'s act was never applied".\n')
        return 0

    w(f'REFUSED 0x{want:08x} is not a word any tree in {inp} declares.\n')
    w('        The constant is derived from a board file that is not in this repository '
      'and does not describe\n        this device. Re-derive it with '
      '`tools/derive_sdc1_pads.py --write-record`, or name the tree\n        it came from '
      'if one of the words above is the intended one.\n')
    return 1


# ------------------------------------------------------------------ self-test


SELFTEST_CELLS = (
    # (candidates, src pull/drv, wants_finding)
    ([0x00009F24, 0x00009FE4], ([0, 3, 3, 1], [4, 4, 4]), False),   # exact hit
    ([0x00009F24, 0x00009FE4], ([0, 3, 3, 1], [7, 4, 4]), False),   # the other board
    ([0x00009F24], ([0, 3, 3, 1], [7, 4, 4]), True),                # no match
    ([0x00009F24], ([0, 3, 3, 1], [4, 4, 4]), False),               # single candidate, hit
    ([], ([0, 3, 3, 1], [4, 4, 4]), True),                          # empty candidate list
    # the arithmetic itself, against the vendor's own array order
    ([derive([0, 3, 3, 1], [4, 4, 4])], ([0, 3, 3, 1], [4, 4, 4]), False),
    ([derive([0, 3, 3, 1], [7, 4, 4])], ([0, 3, 3, 1], [4, 4, 4]), True),
)


SELFTEST_OFFWORDS = (
    # (off words the record declares, the constant, wants_refusal)
    ([0x00009E00], 0x00009F24, False),   # the ladder's own constant, an ON word
    ([0x00009E00], 0x00009FE4, False),   # the other tree's ON word
    ([0x00009E00], 0x00009E00, True),    # the OFF word: a powered-down pad called configured
    ([], 0x00009F24, False),             # a record that declares none is not a refusal
    ([0x00009E00, 0x00000000], 0x00000000, True),   # an all-zero off word too
)

SELFTEST_NODES = (
    # (census rows, hc, core, wants_refusal)
    ([(0xf9824900, 'sdhci@f9824900', 0xf9824000, 0x1a0, 'ok', 5, 6)], 0xf9824900,
     0xf9824000, False),
    ([], 0xf9824900, 0xf9824000, True),                                   # no census at all
    ([(0xf98a4900, 'sdhci@f98a4900', 0xf98a4000, 0x11c, 'ok', 5, 6)], 0xf9824900,
     0xf9824000, True),                                                   # the ladder's node absent
    ([(0xf9824900, 'sdhci@f9824900', 0xf9824000, 0x1a0, 'disable', 5, 6)], 0xf9824900,
     0xf9824000, True),                                                   # carried, not enabled
    ([(0xf9824900, 'sdhci@f9824900', 0xf98a4000, 0x1a0, 'ok', 5, 6)], 0xf9824900,
     0xf9824000, True),                                                   # core base is another node's
    ([(0xf98a4900, 'sdhci@f98a4900', 0xf98a4000, 0x11c, 'ok', 5, 6),
      (0xf9824900, 'sdhci@f9824900', 0xf9824000, 0x1a0, 'ok', 5, 6)], 0xf9824900,
     0xf9824000, False),                                                  # both enabled: the ladder's is found
)


SELFTEST_RAILS = (
    # (rail rows, declared children, wants_refusal)
    ([('vdd-supply', '/soc/qcom,rpm-smd/rpm-regulator-ldoa20/regulator-l20', '8941_l20', '8941',
       '2950000', '3', 'no', '1,2,3,5/6')],
     ['qcom,pm8941@0', 'qcom,pm8841@4'], False),                       # the device's own reading
    ([('vdd-io-supply', '/soc/qcom,rpm-smd/rpm-regulator-smpa3/regulator-s3', '8941_s3', '8941',
       '1800000', '3', 'no', '1,2,3,5/6')],
     ['qcom,pm8941@0', 'qcom,pm8841@4'], False),
    ([('vdd-supply', '/soc/qcom,rpm-smd/rpm-regulator-ldoa20/regulator-l20', '8941_l20', '8941',
       '2950000', '3', 'no', '1,2,3,5/6')],
     ['qcom,pma8084@0'], True),                                        # the other board's chip
    ([('vdd-supply', '/soc/qcom,rpm-smd/rpm-regulator-ldoa20/regulator-l20', '8084_l20', '8084',
       '2950000', '3', 'no', '0/6')],
     ['qcom,pm8941@0'], True),                                         # and the converse
    ([], ['qcom,pm8941@0'], True),                                     # no rail line at all
    ([('vdd-supply', '/soc/qcom,rpm-smd/rpm-regulator-ldoa20/regulator-l20', '8941_l20', '8941',
       '2950000', '3', 'no', '1/6')], [], True),                       # rails, no declared chip
)


def selftest():
    bad = 0
    for rail_rows, kids, want_refusal in SELFTEST_RAILS:
        ok, why = rail_census_verdict(rail_rows, kids)
        if ok == want_refusal:
            print(f'  rails {[r[2] for r in rail_rows]} children {kids}: expected '
                  f'refusal={want_refusal}, got {not ok} ({why})')
            bad += 1
    for offs, want, want_refusal in SELFTEST_OFFWORDS:
        ok, why = offword_verdict(offs, want)
        if ok == want_refusal:
            print(f'  offwords {[hex(o) for o in offs]} want 0x{want:08x}: expected '
                  f'refusal={want_refusal}, got {not ok} ({why})')
            bad += 1
    for census, hc, core, want_refusal in SELFTEST_NODES:
        ok, why = node_verdict(census, hc, core)
        if ok == want_refusal:
            print(f'  census {census} hc=0x{hc:08x}: expected refusal={want_refusal}, got '
                  f'{not ok} ({why})')
            bad += 1
    # the constant the ladder's source carries, written out so a change to the arithmetic is
    # caught here and not at a press: 0x9E00 of pull | 0x124 of drv
    for cands, (pull, drv), want_finding in SELFTEST_CELLS:
        got = derive(pull, drv) not in cands
        if got != want_finding:
            print(f'  cell pull={pull} drv={drv} candidates={cands}: '
                  f'expected finding={want_finding}, got {got}')
            bad += 1
    if derive([0, 3, 3, 1], [4, 4, 4]) != 0x00009F24:
        print('  0x9F24 is what the source\'s own arrays imply, and it has moved')
        bad += 1
    if derive([0, 3, 3, 1], [7, 4, 4]) != 0x00009FE4:
        print('  the other tree on this device implies 0x9FE4, and it has moved')
        bad += 1
    print(f'selftest ok: {len(SELFTEST_CELLS) + 2} word cells, {len(SELFTEST_OFFWORDS)} '
          f'off-word cells, {len(SELFTEST_NODES)} census cells and {len(SELFTEST_RAILS)} rail '
          f'cells; both words this device declares are pinned, and so is the PMIC every rail '
          f'is attributed to')
    return 1 if bad else 0


def main(argv):
    if '--selftest' in argv:
        return selftest()
    if len(argv) > 1:
        print(__doc__.strip().split('\n')[0])
        print('usage: check_sdc1_pad_expectation.py [--selftest]')
        return 2
    return check()


if __name__ == '__main__':
    sys.exit(main(sys.argv))
