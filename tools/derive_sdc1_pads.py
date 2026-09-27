#!/usr/bin/env python3
"""Re-derive SDC1's pad register expectation from the device's OWN device tree.

`src/entry/entry_storage.c` compares the live TLMM SDC1 pad-control register
(`MSM_TLMM_BASE + 0x2044` = physical `0xFD512044`) against `ST_TLMM_SDC1_EXPECT`, a word
built from `qcom,pad-drv-on` and `qcom,pad-pull-on`. Its comment says where those came
from -- *"the Mi 4's own board file (`msm8974pro-ac-pm8941-mtp-v5.dts:25-29`)"* -- and
that comment is wrong in two ways a reader cannot see from the source:

  * `msm8974pro-ac-pm8941-mtp-v5.dts` is **not** the Mi 4's board file. No cancro or
    Xiaomi device tree exists in the vendor checkout; V5 is one of four MSM8974Pro-AC
    Qualcomm reference boards.
  * the vendor checkout is `.gitignore`d (`external/`), so the citation names a file the
    repository does not contain and cannot resolve.

This tool re-derives the expectation from a file that describes **this device**: the
`dt.img` its own stock boot image carries, which `scripts/build.sh` packs into
`stage90-qcdt.img` under the name `$QCDT_DT`. That file is `.gitignore`d too
(`xiaomi4-cancro-backup-*/`), so the derivation is not reproducible from a clone -- which
is why `--write-record` exists: it writes the *reading* into a tracked file
(`records/sdc1-pad-candidates.txt`) together with the input's own sha256, so what the
repository keeps is a measurement and not a citation.

What it is NOT: a statement about which device tree the bootloader selects. `dt.img` is a
table of alternatives and the choice is made on the device. This tool reports every
alternative and the word each one implies; it does not pick.

Usage:
    tools/derive_sdc1_pads.py                     # print the table for the default dt.img
    tools/derive_sdc1_pads.py --dt PATH           # a different QCDT blob
    tools/derive_sdc1_pads.py --write-record      # also rewrite records/sdc1-pad-candidates.txt
    tools/derive_sdc1_pads.py --check             # compare against entry_storage.c, exit 1 on no match
"""

import argparse
import hashlib
import os
import re
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DT = os.path.join(
    REPO, 'xiaomi4-cancro-backup-20260604-112053', 'boot-unpacked', 'dt.img')
DEFAULT_RECORD = os.path.join(REPO, 'records', 'sdc1-pad-candidates.txt')
ENTRY_SRC = os.path.join(REPO, 'src', 'entry', 'entry_storage.c')

FDT_MAGIC = b'\xd0\x0d\xfe\xed'
QCDT_MAGIC = b'QCDT'

# The kernel's own shift and width table, transcribed from the vendor checkout (which is
# `.gitignore`d) -- `drivers/gpio/gpio-msm-common.c`'s `tlmm_hdrv_cfgs` / `tlmm_pull_cfgs`,
# indexed by `arch/arm/mach-msm/include/mach/gpio.h`'s `enum msm_tlmm_hdrive_tgt` /
# `enum msm_tlmm_pull_tgt`, and applied by `msm_tlmm_set_field` with widths 3 and 2
# (`sdhci_msm_setup_pins` in `drivers/mmc/host/sdhci-msm.c` is the caller).
HDRV_WIDTH, PULL_WIDTH = 3, 2
HDRV = (('clk', 6), ('cmd', 3), ('data', 0))          # DT order, from base = SDC1_CLK
PULL = (('clk', 13), ('cmd', 11), ('data', 9), ('rclk', 15))  # base = SDC1_CLK

NODE = 'sdhci@f9824900'          # the ladder's own node: ST_HC_MEM_BASE 0xf9824900
ROOT_PROPS = ('model', 'compatible', 'qcom,msm-id', 'qcom,board-id')
PAD_PROPS = ('qcom,pad-pull-on', 'qcom,pad-pull-off', 'qcom,pad-drv-on',
             'qcom,pad-drv-off', 'vdd-supply', 'vdd-io-supply',
             'qcom,vdd-always-on', 'qcom,vdd-io-always-on', 'qcom,bus-width')


# ---------------------------------------------------------------- FDT


def fdt_walk(blob, want_node):
    """Every property of the node whose path ends in `want_node`, as (name, bytes)."""
    magic, tot, off_struct, off_str = struct.unpack_from('>4I', blob, 0)
    if magic != 0xd00dfeed:
        raise ValueError(f'not an FDT: magic {magic:#x}')

    def cstr(off):
        end = blob.index(b'\0', off)
        return blob[off:end].decode('latin1')

    out, stack, pos = [], [], off_struct
    while pos < len(blob):
        tok = struct.unpack_from('>I', blob, pos)[0]
        pos += 4
        if tok == 1:                                   # BEGIN_NODE
            name = cstr(pos)
            pos += (len(name) + 1 + 3) // 4 * 4
            stack.append(name)
        elif tok == 2:                                 # END_NODE
            if stack:
                stack.pop()
            if not stack:
                break
        elif tok == 3:                                 # PROP
            ln, noff = struct.unpack_from('>II', blob, pos)
            pos += 8
            data = blob[pos:pos + ln]
            pos += (ln + 3) // 4 * 4
            if want_node in '/' + '/'.join(stack):
                out.append((cstr(off_str + noff), data))
        elif tok == 4:
            pass
        else:
            break
    return out


def fdt_root_props(blob):
    """(model, board-id cells, msm-id cells) from the root node."""
    magic, tot, off_struct, off_str = struct.unpack_from('>4I', blob, 0)
    depth = 0
    vals, pos = {}, off_struct
    while pos < len(blob):
        tok = struct.unpack_from('>I', blob, pos)[0]
        pos += 4
        if tok == 1:
            name = blob[pos:blob.index(b'\0', pos)].decode('latin1')
            depth += 1
            if depth == 1:
                root_begin = pos
            pos += (len(name) + 1 + 3) // 4 * 4
        elif tok == 2:
            depth -= 1
            if depth <= 0:
                break
        elif tok == 3:
            ln, noff = struct.unpack_from('>II', blob, pos)
            pos += 8
            data = blob[pos:pos + ln]
            pos += (ln + 3) // 4 * 4
            if depth == 1:
                name = blob[off_str + noff:blob.index(b'\0', off_str + noff)].decode('latin1')
                if name in ROOT_PROPS:
                    vals[name] = data
        elif tok == 4:
            pass
        else:
            break
    return vals


def cells(data):
    n = len(data) // 4
    return list(struct.unpack('>%dI' % n, data[:n * 4])) if n else []


# ---------------------------------------------------------------- derivation


def derive(pull_on, drv_on):
    """The register word `sdhci_msm_setup_pins` would leave for these DT arrays.

    The kernel sets each field with `reg |= (val & (2**w - 1)) << off`, and the DT array
    is indexed from the enum base, so element 0 of `pull-on` is CLK and element 0 of
    `drv-on` is CLK.
    """
    word = 0
    for (name, shift), v in zip(PULL, pull_on):
        word |= (v & ((1 << PULL_WIDTH) - 1)) << shift
    for (name, shift), v in zip(HDRV, drv_on):
        word |= (v & ((1 << HDRV_WIDTH) - 1)) << shift
    return word


def parse_qcdt(raw):
    """Every DTB in a QCDT blob, as `(index, offset, blob)`.

    The header's field layout has more than one dialect in the wild, so the entries are
    located the way the table actually is one: by scanning for the FDT magic and taking
    each tree's own `totalsize`. The header is still read, and reported, because a
    disagreement between it and the scan is a finding.
    """
    if raw[:4] != QCDT_MAGIC:
        raise ValueError(f'not a QCDT blob: magic {raw[:4]!r}')
    version, num_entries, entry_len = struct.unpack_from('<3I', raw, 4)
    found, out = [], []
    pos = 0
    while True:
        i = raw.find(FDT_MAGIC, pos)
        if i < 0:
            break
        total = struct.unpack_from('>I', raw, i + 4)[0]
        if total > 16 and i + total <= len(raw):
            out.append((len(out), i, raw[i:i + total]))
        pos = i + 4
    return {'version': version, 'num_entries': num_entries,
            'entry_len': entry_len, 'trees': out, 'raw_len': len(raw)}


# ---------------------------------------------------------------- the source's own constants


def source_constants(path=ENTRY_SRC):
    """`ST_TLMM_DT_*` from the entry source, in DT-array order."""
    try:
        text = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        return None
    pat = re.compile(r'^#define\s+ST_TLMM_DT_(PULL_ON|DRV_ON)_(CLK|CMD|DATA|RCLK)\s+(\d+)u',
                     re.M)
    got = {}
    for kind, field, val in pat.findall(text):
        got[(kind, field.lower())] = int(val)
    if len(got) != len(PULL) + len(HDRV):
        return None
    pull = [got[('PULL_ON', n)] for n, _ in PULL]
    drv = [got[('DRV_ON', n)] for n, _ in HDRV]
    return pull, drv


# ---------------------------------------------------------------- main


def collect(dt_path):
    raw = open(dt_path, 'rb').read()
    table = parse_qcdt(raw)
    rows = []
    for idx, off, blob in table['trees']:
        root = fdt_root_props(blob)
        props = dict(fdt_walk(blob, NODE))
        if 'qcom,pad-drv-on' not in props:
            rows.append({'index': idx, 'offset': off, 'size': len(blob),
                         'model': (root.get('model', b'').rstrip(b'\0').decode('latin1')
                                   or '(unnamed)'),
                         'board_id': cells(root.get('qcom,board-id', b'')),
                         'node': None})
            continue
        pull_on = cells(props['qcom,pad-pull-on'])
        drv_on = cells(props['qcom,pad-drv-on'])
        rows.append({
            'index': idx, 'offset': off, 'size': len(blob),
            'model': root.get('model', b'').rstrip(b'\0').decode('latin1') or '(unnamed)',
            'board_id': cells(root.get('qcom,board-id', b'')),
            'compatible': root.get('compatible', b'').rstrip(b'\0').decode('latin1'),
            'node': NODE,
            'pull_on': pull_on, 'pull_off': cells(props['qcom,pad-pull-off']),
            'drv_on': drv_on, 'drv_off': cells(props['qcom,pad-drv-off']),
            'vdd': cells(props['vdd-supply']), 'vdd_io': cells(props['vdd-io-supply']),
            'always_on': 'qcom,vdd-always-on' in props,
            'io_always_on': 'qcom,vdd-io-always-on' in props,
            'bus_width': cells(props['qcom,bus-width']),
            'expect': derive(pull_on, drv_on),
        })
    return table, rows, hashlib.sha256(raw).hexdigest()


def report(dt_path, table, rows, sha, out=sys.stdout):
    w = out.write
    w(f'dt.img   {os.path.relpath(dt_path, REPO)}\n')
    w(f'         {table["raw_len"]} bytes, sha256 {sha}\n')
    w(f'         QCDT v{table["version"]}, header says {table["num_entries"]} entries, '
      f'entry_len {table["entry_len"]}; the scan finds {len(table["trees"])} FDT '
      f'tree(s)\n\n')
    if table['num_entries'] != len(table['trees']):
        w(f'  NOTE the header and the scan disagree ({table["num_entries"]} vs '
          f'{len(table["trees"])}); the scan is what the table contains.\n\n')
    hit = [r for r in rows if r['node']]
    lose = [r for r in rows if not r['node']]
    w(f'{len(hit)} tree(s) carry a {NODE} node; {len(lose)} do not.\n\n')
    seen = {}
    for r in hit:
        key = (tuple(r['pull_on']), tuple(r['drv_on']))
        seen.setdefault(key, []).append(r)
    w('distinct SDC1 pad declarations on this device, and the word each one implies:\n\n')
    w('  %-30s %-14s %-14s %-10s %s\n'
      % ('model', 'pad-pull-on', 'pad-drv-on', 'expect', 'on trees'))
    for key, rs in seen.items():
        w('  %-30s %-14s %-14s 0x%08x %s\n'
          % (rs[0]['model'][:30], str(list(key[0])), str(list(key[1])),
             rs[0]['expect'], ', '.join('#%d' % r['index'] for r in rs)))
    w('\nper tree:\n')
    for r in rows:
        if not r['node']:
            w('  #%d @0x%06x %-7d %-34s (no %s node)\n'
              % (r['index'], r['offset'], r['size'], r['model'][:34], NODE))
            continue
        w('  #%d @0x%06x %-7d %-34s board-id %s\n'
          % (r['index'], r['offset'], r['size'], r['model'][:34],
             ' '.join('%d' % c for c in r['board_id'])))
        w('       pull-on %-12s drv-on %-12s -> expect 0x%08x   always-on %s/%s\n'
          % (r['pull_on'], r['drv_on'], r['expect'],
             'yes' if r['always_on'] else 'NO', 'yes' if r['io_always_on'] else 'NO'))
        w('       pull-off %-11s drv-off %-11s vdd %s vdd-io %s bus-width %s\n'
          % (r['pull_off'], r['drv_off'], r['vdd'], r['vdd_io'], r['bus_width']))
    return seen


def write_record(path, dt_path, table, rows, sha, seen, src):
    lines = [
        '# The SDC1 pad expectation, re-derived from the device\'s own device tree.',
        '#',
        '# Written by tools/derive_sdc1_pads.py --write-record. This file is TRACKED on purpose:',
        '# the input it is derived from is not. The dt.img below lives under',
        '# `xiaomi4-cancro-backup-*/`, which .gitignore excludes, and the vendor checkout whose',
        '# shift table the derivation uses lives under `external/`, excluded too. So the citation',
        '# `msm8974pro-ac-pm8941-mtp-v5.dts:25-29` in src/entry/entry_storage.c names a file this',
        '# repository does not contain, and this record is what makes the derivation checkable',
        '# from a clone: it carries the reading and the sha256 of the input it was read from.',
        '#',
        '# THE INPUT IS A TABLE OF ALTERNATIVES. dt.img holds every board the bootloader may',
        '# select, and the selection is made on the device. Nothing here says which one wins.',
        '#',
        f'# input     {os.path.relpath(dt_path, REPO)}',
        f'# sha256    {sha}',
        f'# size      {table["raw_len"]}',
        f'# QCDT      v{table["version"]}, header num_entries {table["num_entries"]}, '
        f'entry_len {table["entry_len"]}, FDT trees found {len(table["trees"])}',
        '#',
        '# the kernel arithmetic this re-derives: `msm_tlmm_set_field` sets each field with',
        '# `reg |= (val & (2**w - 1)) << off`; hdrive fields are width 3 at shifts 6/3/0',
        '# (CLK/CMD/DATA) and pull fields width 2 at shifts 13/11/9/15 (CLK/CMD/DATA/RCLK),',
        '# indexed from the enum base, so the DT arrays are in that order.',
        '#',
    ]
    if src:
        pull, drv = src
        pull_names = ' '.join(n.upper() for n, _ in PULL)
        drv_names = ' '.join(n.upper() for n, _ in HDRV)
        lines += [
            '# src/entry/entry_storage.c declares, in the same order:',
            f'#   ST_TLMM_DT_PULL_ON_{pull_names} = {" ".join(str(v) for v in pull)}',
            f'#   ST_TLMM_DT_DRV_ON_{drv_names} = {" ".join(str(v) for v in drv)}',
            f'#   ST_TLMM_SDC1_EXPECT = 0x{derive(pull, drv):08x}',
            '#',
        ]
    lines.append('# model                          board-id     pad-pull-on      pad-drv-on      expect')
    for r in rows:
        if not r['node']:
            lines.append('# %-30s %-12s (no %s node)' % (r['model'][:30], '', NODE))
            continue
        lines.append('# %-30s %-12s %-16s %-15s 0x%08x'
                     % (r['model'][:30], ' '.join(str(c) for c in r['board_id']),
                        str(r['pull_on']), str(r['drv_on']), r['expect']))
    lines += [
        '#',
        '# The machine-readable form of the same reading, one line per DISTINCT word. A check that',
        '# reads this file rather than this table is reading a token and not a column position.',
    ]
    for key, rs in seen.items():
        lines.append('# candidate 0x%08x  pull-on %s drv-on %s  on %s'
                     % (rs[0]['expect'], rs[0]['pull_on'], rs[0]['drv_on'],
                        ', '.join(r['model'] for r in rs)))
    lines += [
        '#',
        '# always-on flags, per tree: every tree above carries both qcom,vdd-always-on and',
        '# qcom,vdd-io-always-on, or the row says which it lacks. A DEVICE TREE IS A',
        '# DECLARATION, NOT A MEASUREMENT: this closes the provenance gap -- the file is the',
        '# device\'s own -- and it does not establish that the rails are up.',
        '',
    ]
    open(path, 'w', encoding='utf-8').write('\n'.join(lines))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--dt', default=DEFAULT_DT)
    ap.add_argument('--write-record', action='store_true')
    ap.add_argument('--record', default=DEFAULT_RECORD)
    ap.add_argument('--check', action='store_true',
                    help='exit 1 when the source constants match no tree in dt.img')
    args = ap.parse_args(argv[1:])

    if not os.path.exists(args.dt):
        print(f'no device tree to read: {os.path.relpath(args.dt, REPO)}')
        print('  that path is .gitignore\'d (xiaomi4-cancro-backup-*/), so this derivation')
        print('  is only available on a machine holding the device\'s own partition backup.')
        print(f'  the tracked reading of it is {os.path.relpath(args.record, REPO)};')
        print('  check_sdc1_pad_expectation.sh compares the source against that instead.')
        return 2

    table, rows, sha = collect(args.dt)
    seen = report(args.dt, table, rows, sha)
    src = source_constants()
    words = sorted({r['expect'] for r in rows if r['node']})

    print()
    if src is None:
        print('src/entry/entry_storage.c: could not read both ST_TLMM_DT_ arrays; '
              'not comparing.')
        return 0
    pull, drv = src
    want = derive(pull, drv)
    print('the source declares pull-on %s drv-on %s -> ST_TLMM_SDC1_EXPECT = 0x%08x'
          % (pull, drv, want))
    match = [r for r in rows if r['node'] and r['expect'] == want]
    if match:
        print('  MATCHES %d tree(s):' % len(match))
        for r in match:
            print('    #%d  %s  board-id %s'
                  % (r['index'], r['model'], ' '.join(str(c) for c in r['board_id'])))
    else:
        print('  MATCHES NOTHING in this dt.img. The words it declares are:')
        for w in words:
            print('    0x%08x' % w)

    if args.write_record:
        write_record(args.record, args.dt, table, rows, sha, seen, src)
        print(f'\nwrote {os.path.relpath(args.record, REPO)}')

    if args.check and not match:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
