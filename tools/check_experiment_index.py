#!/usr/bin/env python3
"""Check the experiment index's *order*, not just its contents.

`docs/experiments/README.md` is an ordered record and, until this script existed, nothing checked
that. Two separate steps inserted a row one position early and both survived a review that listed
the rows around the insertion and read the listing as "in order" - a listing is not a check of order
(measurement defect 138), and the same defect was then found again at 379/380, where the rows were
swapped for thirty experiments (defect 139).

The property the file actually has, measured rather than assumed: **within each stage column the
experiment numbers are strictly unimodal - ascending to a single peak and strictly descending after
it - and no number appears twice.** The descending half is history (rows prepended as a stage's
earlier steps were written up), so it is not a defect; a *second* ascension, a plateau, or a repeat
is.

Usage:
    tools/check_experiment_index.py                 # exits 1 on any violation
    tools/check_experiment_index.py --file PATH     # a different index

Every violation is printed with its line number, so the fix is mechanical.
"""

import argparse
import os
import re
import sys

ROW = re.compile(r'^\|\s*(?P<stage>[A-Za-z0-9_-]+)\s*\|\s*\[experiment-(?P<num>\d+)\]\((?P<path>[^)]+)\)')
DEFAULT = os.path.join('docs', 'experiments', 'README.md')


def rows(path):
    """Every indexed row, in file order, as (line number, stage, number, link target)."""
    out = []
    with open(path, encoding='utf-8', errors='replace') as fh:
        for lineno, line in enumerate(fh, 1):
            m = ROW.match(line)
            if m:
                out.append((lineno, m.group('stage'), int(m.group('num')), m.group('path')))
    return out


def check(path, index_dir):
    bad = 0
    seen = {}
    for lineno, stage, num, target in rows(path):
        if (stage, num) in seen:
            print(f'{path}:{lineno}: experiment {num} appears twice under {stage} '
                  f'(first at line {seen[(stage, num)]})')
            bad += 1
        seen[(stage, num)] = lineno

        full = os.path.join(index_dir, target)
        if not os.path.exists(full):
            print(f'{path}:{lineno}: {stage} experiment {num} links {target}, which does not exist')
            bad += 1

    stages = {}
    for lineno, stage, num, _ in rows(path):
        stages.setdefault(stage, []).append((lineno, num))

    for stage, entries in stages.items():
        nums = [n for _, n in entries]
        peak = max(range(len(nums)), key=lambda i: nums[i])
        for i in range(peak):
            if nums[i] >= nums[i + 1]:
                print(f'{path}:{entries[i][0]}: {stage} is not ascending before its peak: '
                      f'{nums[i]} at line {entries[i][0]} then {nums[i + 1]} at line '
                      f'{entries[i + 1][0]}')
                bad += 1
        for i in range(peak, len(nums) - 1):
            if nums[i] <= nums[i + 1]:
                print(f'{path}:{entries[i][0]}: {stage} is not descending after its peak: '
                      f'{nums[i]} at line {entries[i][0]} then {nums[i + 1]} at line '
                      f'{entries[i + 1][0]}')
                bad += 1

    total = sum(len(v) for v in stages.values())
    if bad:
        print(f'\n{bad} violation(s) in {total} row(s) across {len(stages)} stage column(s)')
        return 1
    print(f'ok: {total} row(s) across {len(stages)} stage column(s), each strictly unimodal '
          f'and unique, and every link target present')
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--file', default=DEFAULT)
    args = ap.parse_args()
    return check(args.file, os.path.dirname(args.file))


if __name__ == '__main__':
    sys.exit(main())
