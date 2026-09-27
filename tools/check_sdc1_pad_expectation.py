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

from derive_sdc1_pads import (HDRV, PULL, derive, source_bases,  # noqa: E402
                              source_constants, DEFAULT_RECORD, ENTRY_SRC, REPO)

CANDIDATE = re.compile(r'^#\s*candidate\s+(0x[0-9a-fA-F]{8})\s')
SHA_LINE = re.compile(r'^#\s*sha256\s+([0-9a-f]{64})\s*$')
INPUT_LINE = re.compile(r'^#\s*input\s+(\S+)\s*$')
NODE_LINE = re.compile(r'^#\s*node\s+(0x[0-9a-fA-F]+)\s+(\S+)\s+core\s+(0x[0-9a-fA-F]+)'
                       r'\s+window\s+(0x[0-9a-fA-F]+)\s+status\s+(\S+)\s+trees\s+(\d+)/(\d+)')


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


def selftest():
    bad = 0
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
    print(f'selftest ok: {len(SELFTEST_CELLS) + 2} word cells and '
          f'{len(SELFTEST_NODES)} census cells; both words this device declares are pinned')
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
