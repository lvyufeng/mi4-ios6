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
    tools/derive_sdc1_pads.py --rails             # the regulator each SDC1 supply resolves to, per tree
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


# ---------------------------------------------------------------- the rails


#: The two ways this SoC's device trees put a supply on a PMIC, and they are not the same thing.
#: A rail under `qcom,rpm-smd` has **no `reg` at all** -- its address belongs to the RPM and the
#: AP names it on a channel -- while a `regulator@XXXX` under an SPMI PMIC child is the AP's
#: direct register window into the chip. Which family the eMMC's two supplies are declared in
#: decides whether any rung can power the card with a store, so it is reported and not assumed.
RPM_RAIL = '/soc/qcom,rpm-smd/'
SPMI_ARB = '/soc/qcom,spmi@'

#: What a rail node says about itself. `qcom,set` is the RPM state set the request applies to.
RAIL_PROPS = ('regulator-name', 'qcom,init-voltage', 'qcom,set', 'regulator-min-microvolt',
              'regulator-max-microvolt', 'regulator-always-on', 'regulator-boot-on',
              'qcom,always-on')

#: The props that mean *do not switch this off*, as a set: the file's own vocabulary for it.
ALWAYS_TOKEN = ('regulator-always-on', 'regulator-boot-on', 'qcom,always-on')


def rail_name(props):
    """A rail's own name, from `regulator-name` in either of the spellings a DT uses for it."""
    raw = props.get('regulator-name')
    if raw is None:
        return None
    return raw.rstrip(b'\0').decode('latin1')


def rail_pmic(name):
    """The PMIC token a rail's name carries -- `8941` from `8941_l20`, `8084` from `8084_s4`.

    **The rail's name and the chip's node name have two spellings and neither contains the other
    in the direction a substring test would need.** The rail is `8941_l20`; the SPMI child is
    `qcom,pm8941@0`. `8941` IS a substring of `pm8941` -- but the PMA8084's rail is `8084_l20`
    while its child is `qcom,pma8084@0`, where `8084` is a **suffix** and not a substring of the
    front. So the rule `rail_verdict` applies is *the token is the last four characters of a
    declared SPMI child's name*, and it is stated as a rule because it is one: a rail named with
    no `_` has no token at all, and is not a rail this tool will attribute to a chip.
    """
    if not name or '_' not in name:
        return None
    return name.split('_', 1)[0]


def spmi_children(nodes):
    """`([(child name, slave-id cells)], arbiter path)` for every PMIC the arbiter declares."""
    arb = sorted(p for p in nodes if p.startswith(SPMI_ARB) and p.count('/') == 2)
    if not arb:
        return [], None
    arb = arb[0]
    kids = sorted(q for q in nodes if q.startswith(arb + '/') and q.count('/') == 3)
    return [(k.rsplit('/', 1)[-1], cells(nodes[k].get('reg', b''))) for k in kids], arb


def phandle_map(nodes):
    """Every phandle in one tree, as `{phandle: path}`."""
    out = {}
    for path, props in nodes.items():
        for key in ('phandle', 'linux,phandle'):
            for h in cells(props.get(key, b'')):
                out[h] = path
    return out


def rail_verdict(name, children):
    """Whether a rail's own name can be attributed to a PMIC this tree declares. Pure.

    `children` is the arbiter's child node names (`['qcom,pm8941@0', 'qcom,pm8841@4']`). The
    verdict is the 779 class: a rail whose name names a chip this device's tree does not declare
    is a citation to another board standing where a reading of this one should be.
    """
    tok = rail_pmic(name)
    if tok is None:
        return False, f'{name!r} carries no PMIC token (no underscore)'
    for c in children:
        chip = c.rsplit('@', 1)[0]
        chip = chip.split(',', 1)[-1]
        if chip[-4:] == tok:
            return True, f'{tok} is {c}'
    return False, (f'{name!r} names a PMIC this tree does not declare as an SPMI child '
                   f'({", ".join(children) or "none"})')


def rails(dt_path):
    """Every tree's resolution of SDC1's two supplies, and what it declares around them."""
    raw = open(dt_path, 'rb').read()
    table = parse_qcdt(raw)
    rows, trees = [], []
    for idx, off, blob in table['trees']:
        nodes = fdt_nodes(blob)
        ph = phandle_map(nodes)
        root = nodes.get('', {})
        kids, arb = spmi_children(nodes)
        rpm = next((p for p in nodes if p.endswith('/qcom,rpm-smd')), None)
        smd = next((p for p in nodes if p.endswith('/qcom,smd-rpm')), None)
        model = root.get('model', b'').rstrip(b'\0').decode('latin1') or '(unnamed)'
        trees.append({
            'index': idx, 'model': model,
            'board_id': cells(root.get('qcom,board-id', b'')),
            'spmi_arb': arb,
            'spmi_reg': cells(nodes[arb].get('reg', b'')) if arb else [],
            'spmi_regnames': texts(nodes[arb].get('reg-names', b'')) if arb else [],
            'spmi_children': kids,
            'rpm_channel': strv(nodes[rpm].get('rpm-channel-name', b'')) if rpm else None,
            'rpm_type': cells(nodes[rpm].get('rpm-channel-type', b'')) if rpm else [],
            'smd_edge': cells(nodes[smd].get('qcom,smd-edge', b'')) if smd else [],
            'smem': cells(nodes.get('/soc/qcom,smem@fa00000', {}).get('reg', b'')),
            'always_on_props': sorted({(k, p) for p, pr in nodes.items() for k in pr
                                       if k in ALWAYS_TOKEN}),
        })
        sdc = next((p for p in nodes if p.endswith('sdhci@f9824900')), None)
        if sdc is None:
            continue
        for supply in ('vdd-supply', 'vdd-io-supply'):
            hs = cells(nodes[sdc].get(supply, b''))
            for h in hs:
                path = ph.get(h)
                rp = nodes.get(path, {})
                rows.append({
                    'index': idx, 'model': model, 'supply': supply, 'phandle': h,
                    'rail_path': path, 'name': rail_name(rp) if path else None,
                    'family': ('rpm' if path and path.startswith(RPM_RAIL)
                               else 'spmi' if path and path.startswith(SPMI_ARB)
                               else 'other' if path else 'unresolved'),
                    'props': {k: rp[k] for k in RAIL_PROPS if k in rp},
                })
    return table, trees, rows, hashlib.sha256(raw).hexdigest()


def report_rails(table, trees, rows, out=sys.stdout):
    """The reading, in the order the question has to be asked: which rail, whose, how reached."""
    w = out.write
    by = {t['index']: t for t in trees}
    w('the two supplies the ladder\'s own controller declares (`sdhci@f9824900`), resolved\n'
      'through their phandles, per tree:\n\n')
    w('  %-4s %-34s %-14s %s\n' % ('tree', 'model', 'supply', 'rail the device declares'))
    for r in rows:
        w('  #%-3d %-34s %-14s %s\n'
          % (r['index'], r['model'][:34], r['supply'], r['rail_path'] or '(phandle resolves to no node)'))
        if r['name']:
            pr = r['props']
            w('       %-10s family %-10s init %-9s set %-5s min/max %s/%s  always-on %s\n'
              % (r['name'], r['family'],
                 ' '.join(str(c) for c in cells(pr.get('qcom,init-voltage', b''))) or '-',
                 ' '.join(str(c) for c in cells(pr.get('qcom,set', b''))) or '-',
                 ' '.join(str(c) for c in cells(pr.get('regulator-min-microvolt', b''))) or '-',
                 ' '.join(str(c) for c in cells(pr.get('regulator-max-microvolt', b''))) or '-',
                 ','.join(k for k in ALWAYS_TOKEN if k in pr) or 'NONE'))

    paths = {}
    for r in rows:
        if r['name']:
            paths.setdefault(r['rail_path'], {}).setdefault(r['name'], []).append(r['index'])
    w('\nthe rail PATHS, and every name each one carries on this device. A path that carries\n'
      'more than one name is one path with two definitions, and the NAME is the one that says\n'
      'which chip the rail is on:\n\n')
    for path, names in sorted(paths.items()):
        w('  %s\n' % path)
        for n, ts in sorted(names.items()):
            w('      %-12s on trees %s   (%s)\n'
              % (n, ', '.join('#%d' % i for i in ts),
                 'the PMIC the name carries: ' + (rail_pmic(n) or 'none')))
        if len(names) > 1:
            w('      ^ %d names on one path: the name is board-dependent.\n' % len(names))

    w('\nand whether this device\'s own trees mark ANY rail always-on, so that the absence on\n'
      'these two is read as a choice the file makes and not as a property it never uses:\n\n')
    named = {r['rail_path'] for r in rows if r['rail_path']}
    hit_any = []
    for t in trees:
        aop = [k for k, p in t['always_on_props']]
        w('  #%-3d %-34s %d always-on declaration(s) %s\n'
          % (t['index'], t['model'][:34], len(t['always_on_props']),
             sorted(set(aop)) or '(none)'))
        on_rails = sorted({p.rsplit('/', 1)[-1] for k, p in t['always_on_props']
                           if p.startswith(RPM_RAIL) and '/regulator-' in p})
        w('        the RPM rails it marks: %s   -- and any of the eMMC\'s two among them: %s\n'
          % (', '.join(on_rails) or '(none)',
             ', '.join(sorted(p for k, p in t['always_on_props'] if p in named)) or 'NO'))
        hit_any += [p for k, p in t['always_on_props'] if p in named]
    w('  COMPUTED, not asserted: %d of the %d always-on declarations across these trees lands\n'
      '  on a rail the ladder\'s controller declares.\n' % (len(hit_any), sum(
          len(t['always_on_props']) for t in trees)))

    w('\nand the AP\'s declared routes to a PMIC, which is what a rung would have to use:\n\n')
    for t in trees:
        w('  #%-3d SPMI arbiter %s  reg %s  reg-names %s\n'
          % (t['index'], t['spmi_arb'] or '(none)',
             ' '.join('0x%x' % c for c in t['spmi_reg']) or '-',
             ' '.join(t['spmi_regnames']) or '-'))
        w('        children %s\n' % ', '.join('%s (slave %s)' % (n, c)
                                              for n, c in t['spmi_children']) or '(none)')
        w('        RPM channel %r type %s  SMD edge %s  smem %s\n'
          % (t['rpm_channel'], ' '.join(str(c) for c in t['rpm_type']) or '-',
             ' '.join(str(c) for c in t['smd_edge']) or '-',
             ' '.join('0x%x' % c for c in t['smem'][:2]) or '-'))

    # **The verdict the check downstream will apply, printed here so the two can never disagree
    # silently.** `--write-record` will write a rail line whatever it reads -- a rail whose name
    # carries no PMIC token gets `pmic -` -- and the check REFUSES that line. A producer that can
    # write what its own checker refuses is the m749 class, so the tool reports the verdict at
    # read time, on the same rule object, rather than leaving the disagreement for `make check`.
    w('\nand the verdict the tracked record\'s check will reach about each rail, computed here on\n'
      'the same rule: a rail\'s name carries the last four characters of the PMIC node it lives on,\n'
      'so a rail named for a chip this tree does not declare is refused.\n\n')
    refusals = 0
    for t in trees:
        kids = [n for n, _c in t['spmi_children']]
        for r in [q for q in rows if q['index'] == t['index'] and q['name']]:
            ok, why = rail_verdict(r['name'], kids)
            if not ok:
                refusals += 1
                w('  #%-3d REFUSED %s\n' % (t['index'], why))
    w('  %d refusal(s) over %d rail reading(s).\n'
      % (refusals, len([r for r in rows if r['name']])))
    return paths


def rails_record_lines(trees, rows, total_trees):
    """The `# rail` / `# spmi` / `# rpm` lines the tracked record carries.

    One line per DISTINCT reading and never one per tree: the same rail read off five trees is
    one reading with a tree list, and five identical lines is how a reader learns to skip a
    column. The path is what is invariant and the name is what varies, so both are printed and
    the check downstream reads the NAME against the arbiter's children.
    """
    lines = []

    def trees_of(pred):
        ts = sorted({r['index'] for r in rows if pred(r)})
        return ','.join(str(i) for i in ts) or '-'

    for supply in ('vdd-supply', 'vdd-io-supply'):
        seen = []
        for r in [q for q in rows if q['supply'] == supply and q['name']]:
            key = (r['rail_path'], r['name'])
            if key in seen:
                continue
            seen.append(key)
            pr = r['props']
            lines.append(
                '# rail %s %s name %s pmic %s init-uv %s set %s always-on %s trees %s/%d'
                % (supply, r['rail_path'], r['name'], rail_pmic(r['name']) or '-',
                   (cells(pr.get('qcom,init-voltage', b'')) or ['-'])[0],
                   (cells(pr.get('qcom,set', b'')) or ['-'])[0],
                   'yes' if any(k in pr for k in ALWAYS_TOKEN) else 'no',
                   trees_of(lambda q, s=supply, k=key: q['supply'] == s
                            and (q['rail_path'], q['name']) == k),
                   total_trees))
    for key in sorted({(t['spmi_arb'], tuple(n for n, _ in t['spmi_children'])) for t in trees}):
        arb, kids = key
        t0 = next(t for t in trees
                  if (t['spmi_arb'], tuple(n for n, _ in t['spmi_children'])) == key)
        # `;` and not `,`: a PMIC node name is `qcom,pm8941@2`, so a comma-separated list of
        # them is not separable -- splitting it yields the vendor prefix as a chip name.
        lines.append('# spmi %s reg %s reg-names %s children %s trees %s/%d'
                     % (arb or '-', ' '.join('0x%x' % c for c in t0['spmi_reg']) or '-',
                        ';'.join(t0['spmi_regnames']) or '-', ';'.join(kids) or '-',
                        ','.join(str(t['index']) for t in trees
                                 if (t['spmi_arb'],
                                     tuple(n for n, _ in t['spmi_children'])) == key),
                        total_trees))
    seen_rpm = []
    for t in trees:
        key = (t['rpm_channel'], tuple(t['rpm_type']), tuple(t['smd_edge']))
        if key in seen_rpm:
            continue
        seen_rpm.append(key)
        lines.append('# rpm channel %s type %s smd-edge %s smem %s trees %s/%d'
                     % (t['rpm_channel'] or '-', (t['rpm_type'] or ['-'])[0],
                        (t['smd_edge'] or ['-'])[0],
                        ('0x%x' % t['smem'][0]) if t['smem'] else '-',
                        ','.join(str(q['index']) for q in trees
                                 if (q['rpm_channel'], tuple(q['rpm_type']),
                                     tuple(q['smd_edge'])) == key),
                        total_trees))
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


def write_record(path, dt_path, table, rows, sha, seen, src, node_lines=(), rail_lines=()):
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
        '# The rails: the two supplies `sdhci@f9824900` declares, resolved through their own',
        '# phandles to the regulator nodes this device\'s trees put them on, and the routes the',
        '# AP has to a PMIC at all. The RAIL PATH is the same in every tree and the NAME is not,',
        '# so the name is what says which chip a rail is on -- and a rail named for a PMIC this',
        '# device does not declare is a citation to another board, which is what the check',
        '# refuses. The eMMC\'s two rails are declared under `qcom,rpm-smd`, i.e. as RPM',
        '# resources with NO `reg` and no AP address: the AP names them on a channel. That is',
        '# why no store can power the card, and it is why the rails are still UNMEASURED -- see',
        '# the record\'s own closing note.',
    ]
    lines += list(rail_lines)
    lines += [
        '#',
        '# always-on flags, per tree: every tree above carries both qcom,vdd-always-on and',
        '# qcom,vdd-io-always-on on the collector node, or the row says which it lacks. And the',
        '# rail lines above say `always-on no` for ALL of them: the device\'s own tree marks',
        '# eight other rails always-on (vph_pwr_vreg, l2, l3, l12, l18, l22, lvs1, the disp_*',
        '# pair, spi_eth_phy_vreg) and does NOT mark the eMMC\'s two -- so qcom,vdd-always-on is',
        '# the CONSUMER-side flag the vendor driver reads on its release path (780), and it is',
        '# not the RPM\'s statement that the rail is held up. A DEVICE TREE IS A DECLARATION,',
        '# NOT A MEASUREMENT: this closes the provenance gap -- the file is the device\'s own --',
        '# and it does not establish that the rails are up. No capture in this archive holds a',
        '# PMIC, SPMI, RPM or card rail reading of any kind.',
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


#: The rail rules, on the arithmetic alone: `rail_pmic`'s token and `rail_verdict`'s attribution.
#: The cells that matter are the two the device's own trees produce (`8941_l20` on a board whose
#: arbiter declares `qcom,pm8941@0`, and `8084_l20` on a board whose arbiter declares
#: `qcom,pma8084@0`) -- and the refusals: the SAME `8084` rail read against a tree that declares
#: only the PM8941, where a substring test on the name would have called it a match.
SELFTEST_RAILS = (
    ('8941_l20', ['qcom,pm8941@0', 'qcom,pm8841@4'], True),
    ('8941_s3', ['qcom,pm8941@0', 'qcom,pm8841@4'], True),
    ('8084_l20', ['qcom,pma8084@0', 'qcom,pma8084@1'], True),
    ('8084_s4', ['qcom,pm8941@0', 'qcom,pm8941@1'], False),   # the other board's chip
    ('8941_l20', ['qcom,pma8084@0'], False),                  # and the converse
    ('lvs1', ['qcom,pm8941@0'], False),                       # no token at all
    (None, ['qcom,pm8941@0'], False),                         # an unnamed rail is not attributed
    ('8941_l20', [], False),                                  # no arbiter, nothing to attribute
)


def selftest():
    bad = 0
    for pull, drv, want in SELFTEST_DERIVE:
        got = derive_or_none(pull, drv)
        if got != want:
            print(f'  derive_or_none({pull}, {drv}) -> {got}, wanted {want}')
            bad += 1
    for name, children, want in SELFTEST_RAILS:
        got, why = rail_verdict(name, children)
        if got != want:
            print(f'  rail_verdict({name!r}, {children}) -> {got} ({why}), wanted {want}')
            bad += 1
    if rail_pmic('8941_l20') != '8941' or rail_pmic('lvs1') is not None:
        print('  rail_pmic no longer reads the token off the underscore: '
              f'{rail_pmic("8941_l20")!r} / {rail_pmic("lvs1")!r}')
        bad += 1
    # The two words the device declares, and the state between them, pinned as constants so a
    # change to the shift table is caught here rather than at a press.
    if len(SELFTEST_DERIVE) < 3 or derive_or_none([0, 3, 3, 1], [4, 4, 4]) != 0x00009F24:
        print('  0x9F24 has moved')
        bad += 1
    ran = len(SELFTEST_DERIVE) + len(SELFTEST_RAILS) + 1

    # The rails, on the device's own file: the same two rules `--rails` prints, pinned.
    if not os.path.exists(DEFAULT_DT):
        print('  (4 rail cells SKIPPED: no dt.img at '
              f'{os.path.relpath(DEFAULT_DT, REPO)} on this machine)')
    else:
        _t, rtree, rrows, _s = rails(DEFAULT_DT)
        want_pats = {'/soc/qcom,rpm-smd/rpm-regulator-ldoa20/regulator-l20',
                     '/soc/qcom,rpm-smd/rpm-regulator-smpa3/regulator-s3',
                     '/soc/qcom,rpm-smd/rpm-regulator-smpa4/regulator-s4'}
        got_pats = {r['rail_path'] for r in rrows}
        if got_pats != want_pats:
            print(f'  the rail paths these trees declare have moved: {sorted(got_pats)}')
            bad += 1
        ran += 1
        if {r['family'] for r in rrows} != {'rpm'}:
            print('  a supply no longer resolves into the RPM family: '
                  f'{sorted({r["family"] for r in rrows})}')
            bad += 1
        ran += 1
        got_names = sorted({r['name'] for r in rrows})
        if got_names != ['8084_l20', '8084_s4', '8941_l20', '8941_s3']:
            print(f'  the rail names have moved: {got_names}')
            bad += 1
        ran += 1
        # And the rule the check downstream applies, run here against the device's own file: every
        # rail this device declares must attribute to a PMIC its own arbiter declares.
        for t in rtree:
            kids = [n for n, _c in t['spmi_children']]
            for r in [q for q in rrows if q['index'] == t['index']]:
                ok, why = rail_verdict(r['name'], kids)
                if not ok:
                    print(f'  tree #{t["index"]}: {why}')
                    bad += 1
        ran += 1
        # The load-bearing absence, COMPUTED: this device's trees mark other rails always-on and
        # mark the eMMC's two not at all, which is why `qcom,vdd-always-on` on the collector node
        # is read as the vendor driver's consumer flag and not as the RPM holding the rail up.
        named = {r['rail_path'] for r in rrows if r['rail_path']}
        hits = [(k, p) for t in rtree for k, p in t['always_on_props'] if p in named]
        if hits:
            print(f'  an eMMC rail is now marked always-on: {hits}')
            bad += 1
        ran += 1
        if sum(len(t['always_on_props']) for t in rtree) < 8:
            print('  the trees no longer mark OTHER rails always-on, so the absence above is no '
                  'longer a choice the file makes: %d declaration(s)'
                  % sum(len(t['always_on_props']) for t in rtree))
            bad += 1
        ran += 1

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
    arith = len(SELFTEST_DERIVE) + len(SELFTEST_RAILS) + 1
    print(f'selftest ok: {ran} cells ran -- {arith} on the arithmetic alone '
          f'({len(SELFTEST_DERIVE)} pad words, {len(SELFTEST_RAILS)} rail attributions, 1 token '
          f'rule), {ran - arith} against the device\'s own file')
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
    ap.add_argument('--rails', action='store_true',
                    help='print the regulator the SDC1 supplies resolve to, per tree, and the '
                         'AP routes to a PMIC')
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

    rtable, rtree, rrows, rsha = rails(args.dt)
    if args.rails:
        print()
        report_rails(rtable, rtree, rrows)

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
                     census_record_lines(crows, bases, len(table['trees'])),
                     rails_record_lines(rtree, rrows, len(table['trees'])))
        print(f'\nwrote {os.path.relpath(args.record, REPO)}')

    if args.check and not match:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
