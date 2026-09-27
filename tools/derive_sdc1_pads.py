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
    tools/derive_sdc1_pads.py --selftest          # the arithmetic and the census rules, no dt.img needed
    tools/derive_sdc1_pads.py                     # print the table for the default dt.img
    tools/derive_sdc1_pads.py --dt PATH           # a different QCDT blob
    tools/derive_sdc1_pads.py --mmc-census        # every controller node, and which one is the eMMC
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


def fdt_nodes(blob):
    """Every node of one FDT, as `{path: {property: bytes}}`.

    `fdt_walk` answers for one node by name and `fdt_root_props` for the root only; the
    census below needs the whole tree, because the question it answers is *which
    controllers this device enables* and that is only visible from the sibling nodes.
    """
    magic, tot, off_struct, off_str = struct.unpack_from('>4I', blob, 0)
    if magic != 0xd00dfeed:
        raise ValueError(f'not an FDT: magic {magic:#x}')
    path, out, pos = [], {}, off_struct
    while pos < len(blob):
        tok = struct.unpack_from('>I', blob, pos)[0]
        pos += 4
        if tok == 1:                                   # BEGIN_NODE
            name = blob[pos:blob.index(b'\0', pos)].decode('latin1')
            pos += (len(name) + 1 + 3) // 4 * 4
            path.append(name)
            out.setdefault('/'.join(path), {})
        elif tok == 2:                                 # END_NODE
            if path:
                path.pop()
        elif tok == 3:                                 # PROP: token, len, nameoff, value
            ln, noff = struct.unpack_from('>II', blob, pos)
            pos += 8
            name = blob[off_str + noff:blob.index(b'\0', off_str + noff)].decode('latin1')
            out['/'.join(path)][name] = blob[pos:pos + ln]
            pos += (ln + 3) // 4 * 4
        elif tok == 4:
            continue
        else:
            break
    return out


#: The controller nodes this device could hand an eMMC to. `sdhci@` is this device's own
#: spelling (`qcom,sdhci-msm`); the others are accepted so a differently named node in a
#: future image is *printed* rather than silently missed.
CONTROLLER_PREFIXES = ('sdhci@', 'sdcc@', 'sdhc@', 'mmc@')

#: The properties that say which controller is the eMMC, in the order they are read.
CENSUS_PROPS = ('compatible', 'status', 'reg', 'vdd-supply', 'vdd-io-supply',
                'qcom,vdd-always-on', 'qcom,vdd-io-always-on', 'qcom,bus-speed-mode',
                'qcom,vdd-voltage-level', 'qcom,vdd-io-voltage-level',
                'qcom,pad-pull-on', 'qcom,pad-drv-on', 'non-removable')


def cells(data):
    n = len(data) // 4
    return list(struct.unpack('>%dI' % n, data[:n * 4])) if n else []


# ---------------------------------------------------------------- derivation


def derive_or_none(pull_arr, drv_arr):
    """`derive` when the arrays are complete, None when they are short or absent."""
    if pull_arr is None or drv_arr is None:
        return None
    if len(pull_arr) != len(PULL) or len(drv_arr) != len(HDRV):
        return None
    return derive(pull_arr, drv_arr)


def derive(pull_on, drv_on):
    """The register word `sdhci_msm_setup_pins` would leave for these DT arrays.

    The kernel sets each field with `reg |= (val & (2**w - 1)) << off`, and the DT array
    is indexed from the enum base, so element 0 of `pull-on` is CLK and element 0 of
    `drv-on` is CLK.

    **The arrays must be exactly as long as the field list, or this returns None.** An
    array of three entries (SDC2's, which has no RCLK field) is silently truncated by
    `zip`, and a truncated word looks exactly like a real one -- so the honest answer for
    a short array is *no word*, which the caller prints as such. This is the same defect
    class as a value derived from the wrong board: plausible and wrong.
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


# ---------------------------------------------------------------- the controller census


def census(dt_path):
    """Every controller node of every tree in `dt_path`, with the properties that name one.

    **The question this answers, and why it is not the pad table's question.** 779 derived
    the pad word for ONE node -- `sdhci@f9824900`, the node the ladder reads -- and did not
    ask whether that node is the controller this device boots from, nor whether a second
    controller is enabled beside it. Both are visible in the same file, and both have to be
    read before *the card does not answer* can be a statement about the card rather than
    about the address.
    """
    raw = open(dt_path, 'rb').read()
    table = parse_qcdt(raw)
    rows = []
    for idx, off, blob in table['trees']:
        root = fdt_root_props(blob)
        model = root.get('model', b'').rstrip(b'\0').decode('latin1') or '(unnamed)'
        bid = cells(root.get('qcom,board-id', b''))
        for path, props in sorted(fdt_nodes(blob).items()):
            if not path.rsplit('/', 1)[-1].startswith(CONTROLLER_PREFIXES):
                continue
            reg = cells(props.get('reg', b''))
            rows.append({
                'index': idx, 'offset': off, 'model': model, 'board_id': bid,
                'path': path,
                'base': reg[0] if reg else None,
                'window': reg[1] if len(reg) >= 2 else None,
                'core': reg[2] if len(reg) >= 3 else None,
                'props': {k: props[k] for k in CENSUS_PROPS if k in props},
            })
    return table, rows, hashlib.sha256(raw).hexdigest()


def texts(data):
    """A DT string-list property as the strings it holds."""
    return [s for s in data.decode('latin1').split('\0') if s]


def strv(data):
    """A DT string property as one string."""
    return data.rstrip(b'\0').decode('latin1')


def reg_str(r):
    """The node's `reg` cells, as `<base window core size>` and however many there are."""
    return ' '.join('0x%x' % c for c in cells(r['props'].get('reg', b'')))


def census_words(rows):
    """`{controller base: [rows]}`, one entry per distinct address."""
    out = {}
    for r in rows:
        if r['base'] is not None:
            out.setdefault(r['base'], []).append(r)
    return out


def prop_summary(rs, key):
    """One property across the trees that declare this controller.

    **A property that differs between trees is printed as differing, with the trees named.**
    779's whole finding is that the pad declaration is one board's and not the device's, so
    an aggregate row that printed the first tree's array would be the same defect one level
    up: a value presented as the device's when it is a tree's.
    """
    seen = {}
    for r in rs:
        if key not in r['props']:
            continue
        v = r['props'][key]
        if key == 'compatible':
            s = strv(v)
        elif key == 'qcom,bus-speed-mode':
            s = ', '.join(texts(v))
        else:
            s = ' '.join('0x%x' % c for c in cells(v))
        seen.setdefault(s, []).append(r['index'])
    if not seen:
        return None
    if len(seen) == 1:
        return next(iter(seen))
    return ' | '.join('%s (trees %s)' % (s, ', '.join('#%d' % i for i in idx))
                      for s, idx in seen.items())


def report_census(dt_path, table, rows, sha, src, out=sys.stdout):
    w = out.write
    by = census_words(rows)
    w(f'controller census of {os.path.relpath(dt_path, REPO)} (sha256 {sha[:16]}...)\n')
    w(f'  {len(table["trees"])} FDT tree(s), {len(rows)} controller node(s), '
      f'{len(by)} distinct address(es)\n\n')
    for base in sorted(by):
        rs = by[base]
        st = sorted({r['props'].get('status', b'(absent)').rstrip(b'\0').decode('latin1')
                     for r in rs})
        w('  0x%08x  %-18s status %s\n        reg <%s>  on trees %s\n'
          % (base, rs[0]['path'].rsplit('/', 1)[-1], '/'.join(st), reg_str(rs[0]),
             ', '.join('#%d' % r['index'] for r in rs)))
        for k in ('compatible', 'qcom,vdd-always-on', 'qcom,vdd-io-always-on',
                  'non-removable', 'qcom,bus-speed-mode', 'qcom,vdd-voltage-level',
                  'qcom,vdd-io-voltage-level', 'qcom,pad-pull-on', 'qcom,pad-drv-on'):
            if k in ('qcom,vdd-always-on', 'qcom,vdd-io-always-on', 'non-removable'):
                n = sum(1 for r in rs if k in r['props'])
                if n:
                    w('        %-26s %s\n'
                      % (k, 'present' if n == len(rs) else 'present on %d of %d trees'
                         % (n, len(rs))))
                continue
            s = prop_summary(rs, k)
            if s is not None:
                w('        %-26s %s\n' % (k, s))
    hc = (src or {}).get('hc')
    core = (src or {}).get('core')
    w('\nthe ladder reads ')
    if hc is None:
        w('an address this run could not read from the source\n')
        return by
    w('0x%08x (hc_mem) / 0x%08x (core_mem)\n' % (hc, core))
    rs = by.get(hc, [])
    if not rs:
        w('  NO TREE in this file declares a controller at that address. The ladder is not\n'
          '  reading a node this device\'s own tree carries.\n')
        return by
    st = sorted({r['props'].get('status', b'(absent)').rstrip(b'\0').decode('latin1')
                 for r in rs})
    w('  %d of %d tree(s) declare it, status %s; always-on %s / %s; bus-speed-mode %s\n'
      % (len(rs), len(table['trees']), '/'.join(st),
         'yes' if 'qcom,vdd-always-on' in rs[0]['props'] else 'NO',
         'yes' if 'qcom,vdd-io-always-on' in rs[0]['props'] else 'NO',
         ', '.join(texts(rs[0]['props'].get('qcom,bus-speed-mode', b''))) or '(none)'))
    others = [b for b in by if b != hc
              and 'ok' in {r['props'].get('status', b'').rstrip(b'\0').decode('latin1')
                           for r in by[b]}]
    if others:
        w('  and it is NOT the only enabled one: %s also carry status ok, so the\n'
          '  address alone does not say which controller is the eMMC. The properties do:\n'
          % ', '.join('0x%08x' % b for b in sorted(others)))
        for b in sorted(others):
            r = by[b][0]
            w('    0x%08x  always-on %s / %s  bus-speed-mode %s\n'
              % (b, 'yes' if 'qcom,vdd-always-on' in r['props'] else 'NO',
                 'yes' if 'qcom,vdd-io-always-on' in r['props'] else 'NO',
                 ', '.join(texts(r['props'].get('qcom,bus-speed-mode', b''))) or '(none)'))
    return by


def census_record_lines(rows, src, total_trees=None):
    """The `# node ...` lines the tracked record carries, one per distinct controller.

    `total_trees` is the file's own FDT count, so the fraction reads as *trees carrying
    this node out of the trees in this file* -- tree #4 carries no controller at all.
    """
    lines = []
    for base in sorted(census_words(rows)):
        rs = census_words(rows)[base]
        st = sorted({r['props'].get('status', b'(absent)').rstrip(b'\0').decode('latin1')
                     for r in rs})
        lines.append('# node 0x%08x %s core 0x%08x window 0x%x status %s trees %d/%d'
                     % (base, rs[0]['path'].rsplit('/', 1)[-1], rs[0]['core'] or 0,
                        rs[0]['window'] or 0, '/'.join(st), len(rs),
                        total_trees if total_trees is not None
                        else len({r['index'] for r in rows})))
    return lines


# ---------------------------------------------------------------- the source's own constants


def source_bases(path=ENTRY_SRC):
    """`ST_HC_MEM_BASE` and `ST_CORE_MEM_BASE` from the entry source."""
    try:
        text = open(path, encoding='utf-8', errors='replace').read()
    except OSError:
        return None
    pat = re.compile(r'^#define\s+(ST_HC_MEM_BASE|ST_CORE_MEM_BASE)\s+(0x[0-9a-fA-F]+)u', re.M)
    got = {k: int(v, 16) for k, v in pat.findall(text)}
    if 'ST_HC_MEM_BASE' not in got or 'ST_CORE_MEM_BASE' not in got:
        return None
    return {'hc': got['ST_HC_MEM_BASE'], 'core': got['ST_CORE_MEM_BASE']}


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
            'offword': derive_or_none(cells(props['qcom,pad-pull-off']),
                                      cells(props['qcom,pad-drv-off'])),
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
    offs = sorted({r['offword'] for r in hit if r['offword'] is not None})
    if offs:
        w('\nthe OFF words, which are what these same declarations imply for the register at a')
        w('\nmoment the controller is powered DOWN -- derived from the same two arrays by the same')
        w('\narithmetic, and the reason a two-row fork is not enough: an off word shares its PULL')
        w('\nfields with the on word and differs only in DRIVE:\n\n')
        for o in offs:
            rs = [r for r in hit if r['offword'] == o]
            w('  0x%08x  pull-off %-12s drv-off %-12s on %s\n'
              % (o, rs[0]['pull_off'], rs[0]['drv_off'],
                 ', '.join(r['model'] for r in rs)))
    short = [r for r in hit if r['offword'] is None]
    if short:
        w('\n  %d node(s) declare fewer pad fields than the register has (no RCLK field), so no\n'
          '  off word is derived for them: %s\n'
          % (len(short), ', '.join(r['model'] for r in short)))
    return seen


def write_record(path, dt_path, table, rows, sha, seen, src, node_lines=()):
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
    lines.append('# model                          board-id     pad-pull-on      pad-drv-on      '
                 'ON word     OFF word')
    for r in rows:
        if not r['node']:
            lines.append('# %-30s %-12s (no %s node)' % (r['model'][:30], '', NODE))
            continue
        lines.append('# %-30s %-12s %-16s %-15s 0x%08x  %s'
                     % (r['model'][:30], ' '.join(str(c) for c in r['board_id']),
                        str(r['pull_on']), str(r['drv_on']), r['expect'],
                        ('0x%08x' % r['offword']) if r['offword'] is not None else '(short array)'))
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
        '# The OFF words: what the SAME declarations imply for this register at a moment the',
        '# controller is powered DOWN, through the same arithmetic on `qcom,pad-pull-off` and',
        '# `qcom,pad-drv-off`. This is the second column of the register\'s own two-valued',
        '# story and the reason a two-row fork on the arm is not enough: an off word carries the',
        '# SAME pull fields as the on word and differs only in drive, so a register holding the',
        '# off word has SDC1_CMD\'s pull-up PRESENT -- the mechanism 769 section 2 named for a',
        '# card that cannot see the host -- while its drive fields are not the vendor\'s on',
        '# state. A check that reads these lines refuses a guard set to an off word, which is a',
        '# guard that would call a powered-down pad CONFIGURED.',
    ]
    for o in sorted({r['offword'] for r in rows if r['node'] and r['offword'] is not None}):
        rs = [r for r in rows if r['node'] and r['offword'] == o]
        lines.append('# offword 0x%08x  pull-off %s drv-off %s  on %s'
                     % (o, rs[0]['pull_off'], rs[0]['drv_off'],
                        ', '.join(r['model'] for r in rs)))
    lines += [
        '#',
        '# The controller census of the same file: every node whose name this SoC gives a',
        '# controller, with the properties that say which one carries the eMMC. Read by',
        '# tools/check_sdc1_pad_expectation.py, which compares the ladder\'s own two window',
        '# bases against the enabled node named here -- so "the ladder reads a controller this',
        '# device\'s own tree enables" is a check on tracked files and not a sentence.',
    ]
    lines += list(node_lines)
    lines += [
        '#',
        '# always-on flags, per tree: every tree above carries both qcom,vdd-always-on and',
        '# qcom,vdd-io-always-on, or the row says which it lacks. A DEVICE TREE IS A',
        '# DECLARATION, NOT A MEASUREMENT: this closes the provenance gap -- the file is the',
        '# device\'s own -- and it does not establish that the rails are up.',
        '',
    ]
    open(path, 'w', encoding='utf-8').write('\n'.join(lines))


# ---------------------------------------------------------------- self-test


SELFTEST_DERIVE = (
    # (pull array, drv array, expected word or None)
    ([0, 3, 3, 1], [4, 4, 4], 0x00009F24),   # four of the device's trees, the ON word
    ([0, 3, 3, 1], [7, 4, 4], 0x00009FE4),   # the PMA8084 MTP tree, the other ON word
    ([0, 3, 3, 1], [0, 0, 0], 0x00009E00),   # every tree's OFF word: same pulls, no drive
    ([0, 3, 3], [7, 4, 4], None),            # SDC2's short array is NOT silently truncated
    ([0, 3, 3, 1], [4, 4], None),            # nor a short drv array
    (None, [4, 4, 4], None),                 # nor an absent one
    ([], [], None),                          # and an empty pair is not the zero word
)


def selftest():
    bad = 0
    for pull, drv, want in SELFTEST_DERIVE:
        got = derive_or_none(pull, drv)
        if got != want:
            print(f'  derive_or_none({pull}, {drv}) -> {got}, wanted {want}')
            bad += 1
    # The two words the device declares, and the state between them, pinned as constants so a
    # change to the shift table is caught here rather than at a press.
    if len(SELFTEST_DERIVE) < 3 or derive_or_none([0, 3, 3, 1], [4, 4, 4]) != 0x00009F24:
        print('  0x9F24 has moved')
        bad += 1
    ran = len(SELFTEST_DERIVE)

    # The census, on the device's own file, when this machine has it. The four cells below are
    # the rules `census` and `prop_summary` exist for; on a machine without `dt.img` they are
    # SKIPPED BY NAME rather than passed, because a cell that cannot run must not read as one
    # that ran and agreed.
    if not os.path.exists(DEFAULT_DT):
        print('  (4 census cells SKIPPED: no dt.img at '
              f'{os.path.relpath(DEFAULT_DT, REPO)} on this machine)')
    else:
        table, rows, _sha = census(DEFAULT_DT)
        by = census_words(rows)
        if sorted(by) != [0xF9824900, 0xF9864900, 0xF98A4900, 0xF98E4900]:
            print(f'  the four controller addresses have moved: {[hex(b) for b in sorted(by)]}')
            bad += 1
        ran += 1
        enabled = [b for b in by
                   if 'ok' in {r['props'].get('status', b'').rstrip(b'\0').decode('latin1')
                               for r in by[b]}]
        if enabled != [0xF9824900, 0xF98A4900]:
            print(f'  the enabled controller set has moved: {[hex(b) for b in enabled]}')
            bad += 1
        ran += 1
        hc = by.get(0xF9824900, [])
        win = cells(hc[0]['props']['reg'])[1] if hc else 0
        if len(hc) != 5 or win != 0x1A0:
            print(f"  the ladder's node: {len(hc)} tree(s), window 0x{win:x} (wanted 5, 0x1a0)")
            bad += 1
        ran += 1
        if prop_summary(hc, 'qcom,pad-drv-on').count('|') != 1:
            print('  prop_summary no longer reports the differing pad array AS differing: '
                  f'{prop_summary(hc, "qcom,pad-drv-on")!r}')
            bad += 1
        ran += 1
    print(f'selftest ok: {ran} cells ran -- {len(SELFTEST_DERIVE)} on the arithmetic alone, '
          f'{ran - len(SELFTEST_DERIVE)} against the device\'s own file')
    return 1 if bad else 0


def main(argv):
    if '--selftest' in argv:
        return selftest()
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--dt', default=DEFAULT_DT)
    ap.add_argument('--write-record', action='store_true')
    ap.add_argument('--record', default=DEFAULT_RECORD)
    ap.add_argument('--check', action='store_true',
                    help='exit 1 when the source constants match no tree in dt.img')
    ap.add_argument('--mmc-census', action='store_true',
                    help='print every controller node of every tree, and which one is the eMMC')
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
    bases = source_bases()
    words = sorted({r['expect'] for r in rows if r['node']})

    _, crows, csha = census(args.dt)
    if args.mmc_census:
        print()
        report_census(args.dt, table, crows, csha, bases)

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
        write_record(args.record, args.dt, table, rows, sha, seen, src,
                     census_record_lines(crows, bases, len(table['trees'])))
        print(f'\nwrote {os.path.relpath(args.record, REPO)}')

    if args.check and not match:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
