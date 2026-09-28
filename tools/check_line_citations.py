#!/usr/bin/env python3
"""Refuse a line-number citation in the press path's own prose whose site has MOVED.

The problem this exists for (owed by 802 section 7, named again in 804 section 11)
-----------------------------------------------------------------------------------
The readiness narration and the record cite sites as `file.c:4702`. A citation is a claim about the
tree **as it was when written**, and the tree grows: 802 measured by hand that the ladder's CMD2 gate
line had moved from `:4785` to `:4964` and the single CMD1 call from `:4702` to `:4808`, and that
798 section 5, 799 section 3 and 800 section 10 still name the old numbers. A number that still
resolves to *some* line is not a citation that still resolves: it points at whatever now sits there.
Measured on this tree, that is not three instances but a population - 45 in the press path alone.

The baseline is git, not a file this tool writes
------------------------------------------------
A tool that records its own expected values is a self-written record, and
`mi4-self-written-record-is-not-a-constraint` is this project's rule about those. So the ground truth
is taken from the **commit that wrote the citation**: the oldest commit on the branch at which the
count of the token `path:N` in the citing file changed. That commit's version of the cited file, at
the cited line, is what the citation meant. The verdicts are then:

    SAME               the cited line reads the same now as when it was written - nothing to say
    MOVED              the text it named is now at exactly one other line -> the new line is PRINTED
    MOVED-AMBIGUOUS    the text it named now appears at TWO OR MORE lines -> NOT decided here; the
                       candidates are named and a reader decides (`entry_storage.c` has 147 distinct
                       line texts that occur more than once, so this class is real and not a dodge)
    RE-WRITTEN         the text it named is gone from the file entirely
    UNDATED            no commit changed the count of this token in the citing file, so there is no
                       commit to take the meaning from
    SITE-ABSENT        the cited path did not exist, or the line was past its end, at that commit
    PAST-END           the cited line is past the end of the live file
    NOT-IN-REPO        the cited basename is not tracked here (the vendor sources: sdhci.c, mmc_ops.c,
                       sdhci-msm.c ...) AND the citation is not one the vendor axis can read (below).
                       Passed over BY NAME, never by accident
    AMBIGUOUS-PATH     the cited basename is tracked more than once in the live tree - `build.sh` is
                       `scripts/build.sh` and also the archived snapshot's copy. Picking the first is
                       a `one value, two definitions` defect committed by this tool, so it refuses

The vendor axis - a `#define`'s trailing citation, checked against the header on disk
-------------------------------------------------------------------------------------
`#define ST_SDHCI_HOST_CONTROL2 0x3Eu /* sdhci.h:79 */` is a citation with **two readings of the same
site in the same line**: the local NAME and the local VALUE, against a header that is on disk under
`external/` and that this project never edits. So the vendor axis needs NO git - which is exactly why
it can be run over `src/` on every check - and its verdicts are:

    VENDOR-OK          the anchor is `#define`d at the cited line, OR the cited line defines a value
                       equal to the local one (the second reading is what carries the re-spellings:
                       `ST_SDHCI_SLOT_INT_STAT` for the vendor's `SDHCI_SLOT_INT_STATUS`, and the
                       whole `ST_CMD_OP_*` family for the vendor's `MMC_*`)
    VENDOR-ELSEWHERE   the anchor is `#define`d at exactly one other line -> the new line is PRINTED
    VENDOR-MULTI       the anchor is `#define`d at two or more lines -> NOT decided here
    VENDOR-UNRESOLVED  the anchor is `#define`d nowhere and the cited line is not a `#define` whose
                       value agrees - nothing in this line can be checked
    VENDOR-NOFILE      no `.h` with that basename exists under the vendor roots

**The ANCHOR is the identifier the citation names**, and it is taken from both halves of the line:
the local macro's own name, that name with its `ST_` prefix stripped, and every ALL-CAPS identifier
written in the comment AHEAD of the citation (`old_comment /* SDHCI_MAX_DIV_SPEC_300, sdhci.h:255 */`
anchors on the vendor's own symbol).

What is refused, and what is only reported
------------------------------------------
**The refusing scope is the text that PRINTS FOR THE LIVE ARM** - the `rung_para N` line and the
`entry_conseq+=` lines inside `[[ $wst == N ]]` for the rung `out/stage90/xnu_arm_entry-config.txt`
names. That is what the operator reads at press time, and a stale number there is a false claim about
the tree in front of them. The rung is read from the artifact's own switch record, exactly as the
readiness tool reads it.

**Everything else is REPORTED**: the paragraphs for other rungs (which do not print on this arm, but
would print wrong if an arm returned to them) and the `#` comments (which never print); and
`records/revert-set.txt` and `docs/experiments/**`, which are **historical records, deliberately not
rewritten** - their rows describe what was true when written. That is the same split
`tools/check_count_citations.py` already makes, and for the same reason: a rule that refuses the
historical record would be turned off within a step (m775).

What it does NOT claim
----------------------
- **It is not a claim that a SAME citation is correct.** It says the cited line has not changed since
  the citation was written. A citation that was wrong the day it was written is invisible here.
- **About a vendor citation it says what the vendor axis can read and no more.** The axis covers a
  citation written in a `#define`'s own line - measured on this tree, 60 of them over five headers,
  52 carried by the NAME reading and 7 by the VALUE reading, and the one it refuses is a real one.
  Every other vendor citation, and every one in `docs/experiments/**`, is still NOT-IN-REPO: named,
  not checked. 813 section 7 named this gap; this axis is the part of it a mechanical property
  actually holds over.

Usage:
    tools/check_line_citations.py                 # the live arm's own prose refuses; the rest reports
    tools/check_line_citations.py --selftest
    tools/check_line_citations.py --all --all-refusing   # the whole tree, every file refusing
    tools/check_line_citations.py --live-rung 31        # examine another rung's paragraph
    tools/check_line_citations.py --file P        # add a citing file (repeatable)
Exit status: 0 nothing to refuse, 1 at least one refusal, 2 the tool could not run or its own
selftest failed.
"""

import argparse
import collections
import os
import re
import subprocess
import sys

TOKEN = re.compile(r'([A-Za-z0-9_][A-Za-z0-9_./-]*\.(?:c|h|S|sh|py|txt|md|ld)):(\d+)')

# Where a bare basename is looked for, in order. `archive/` is deliberately absent: an archived
# snapshot must never answer for a live name, which is the `build.sh` defect.
ROOTS = ('', 'src/', 'src/entry/', 'src/platform/', 'src/supply/', 'src/shims/', 'src/shims_arm/',
         'src/firehose/', 'src/targets/', 'tools/', 'scripts/', 'records/', 'docs/')

CONFIG = 'out/stage90/xnu_arm_entry-config.txt'

# The vendor checkouts, on disk but NOT tracked in this repository (`git ls-files external` is empty),
# which is why the git baseline has no answer for a citation into one. A root that is absent is
# SKIPPED and the axis says so; if every root is absent the axis reports itself OFF rather than
# passing, because a check whose data is missing and a check that found nothing read the same.
VENDOR_ROOTS = ('external/android_kernel_xiaomi_cancro',)

# A citing line the vendor axis can read: a `#define` whose trailing comment carries a citation.
SHAPE_DEFINE = re.compile(r'^\s*#\s*define\s+(ST_[A-Z0-9_]+)\s+(\S+)\s*/\*(.*)$')
VENDOR_CITE = re.compile(r'([A-Za-z0-9_.-]+\.h):(\d+)')
VENDOR_DEF = re.compile(r'^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.*)$')
ALL_CAPS = re.compile(r'\b([A-Z][A-Z0-9_]{3,})\b')
INT_LITERAL = re.compile(r'^(0[xX][0-9a-fA-F]+|\d+)[uUlL]*$')


def int_literal(text):
    """The integer this text is, or None when it is not a plain integer literal.

    `(1 << 11) - 2` is a perfectly good `#define` and is NOT a value this tool compares: an
    expression that happens to evaluate the same is a different reading, and comparing it would be
    this tool inventing an agreement. None means "do not compare", never "they differ"."""
    text = text.split('/*')[0].strip().rstrip('uUlL')
    return int(text, 0) if INT_LITERAL.match(text) else None

# The press path's own prose: what the operator reads at press time, and what the record claims
# about the tree in front of it. THE DEFAULT IS BOUNDED ON PURPOSE, as a cost: the whole tree is
# 3687 citations and about eighty seconds even after the dating was made a single history walk, and
# a `make check` that takes a minute and a half is a `make check` that gets skipped. `--all` sweeps.
PRESS_PATH = ('tools/verify_press_ready.sh', 'records/revert-set.txt')
RUNG_SWITCH = 'STAGE90_XNU_STORAGE_PROBE'

# The narration structures that bind a run of lines to ONE rung, and which therefore decide whether
# those lines print on this arm.
RUNG_PARA = re.compile(r'^\s*rung_para\s+([0-9]+)\s')
WST_GUARD = re.compile(r'\[\[\s*\$?wst\s*==\s*([0-9]+)\s*\]\]|wst\s*==\s*([0-9]+)')
ELIF_GUARD = re.compile(r'^\s*(?:if|elif)\s')

VERDICTS_PASS = ('SAME', 'VENDOR-OK')
VERDICTS_REPORT = ('MOVED-AMBIGUOUS', 'RE-WRITTEN', 'UNDATED', 'SITE-ABSENT', 'PAST-END',
                   'NOT-IN-REPO', 'VENDOR-MULTI', 'VENDOR-UNRESOLVED', 'VENDOR-NOFILE')
VERDICTS_REFUSE_LIVE = ('MOVED', 'AMBIGUOUS-PATH', 'VENDOR-ELSEWHERE')


def repo_root():
    try:
        out = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True,
                             check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        print('check_line_citations: cannot find the repository root (is git available?)', file=sys.stderr)
        sys.exit(2)
    if not out:
        print('check_line_citations: git rev-parse returned nothing - a refusal, not a pass', file=sys.stderr)
        sys.exit(2)
    return out


def git(root, *args, text=True):
    return subprocess.run(['git'] + list(args), capture_output=True, text=text, cwd=root)


def norm(text):
    return ' '.join(text.split())


class Tree:
    """The live tree's tracked files, and a resolution that refuses to guess."""

    def __init__(self, root):
        self.root = root
        p = git(root, 'ls-files')
        if p.returncode != 0:
            print('check_line_citations: git ls-files failed', file=sys.stderr)
            sys.exit(2)
        self.tracked = [t for t in p.stdout.split('\n') if t]
        if not self.tracked:
            print('check_line_citations: the tracked file list is EMPTY - a refusal, not a pass',
                  file=sys.stderr)
            sys.exit(2)
        self.live = [t for t in self.tracked if not t.startswith('archive/')]
        self.by_base = collections.defaultdict(list)
        for t in self.live:
            self.by_base[os.path.basename(t)].append(t)
        self._lines = {}
        self._at = {}

    @staticmethod
    def _ambiguous(cand, cited, population):
        """A bare basename that more than one root holds is a refusal, not a first hit.

        `build.sh` is `scripts/build.sh` and is also six archived snapshots' copy - so a citation
        written without a directory resolves to whichever root this tool happens to list first, which
        is a `one value, two definitions` defect committed BY THE CHECKER. A cited path that carries
        its own directory cannot be ambiguous, because the roots are prefixes."""
        if '/' in cited:
            return None
        hits = [os.path.join(r, cited) for r in ROOTS if os.path.join(r, cited) in population]
        if len(hits) > 1:
            return 'AMBIGUOUS-PATH'
        return None

    def lines(self, path):
        if path not in self._lines:
            try:
                with open(os.path.join(self.root, path), encoding='utf-8', errors='replace') as fh:
                    self._lines[path] = fh.read().split('\n')
            except OSError:
                self._lines[path] = None
        return self._lines[path]

    def resolve(self, cited, at=None):
        """`at` is a revision: resolve as the tree stood there, else against the live tree.

        Returns (path, None) or (None, verdict)."""
        if at:
            if at not in self._at:
                listing = git(self.root, 'ls-tree', '-r', '--name-only', at)
                self._at[at] = [t for t in listing.stdout.split('\n') if t]
            paths = self._at[at]
            for r in ROOTS:
                cand = os.path.join(r, cited)
                if cand in paths:
                    return cand, self._ambiguous(cand, cited, paths)
            base = os.path.basename(cited)
            hits = [t for t in paths if os.path.basename(t) == base]
            if len(hits) == 1:
                return hits[0], None
            if len(hits) > 1:
                return None, 'AMBIGUOUS-PATH'
            return None, 'SITE-ABSENT'
        for r in ROOTS:
            cand = os.path.join(r, cited)
            if cand in self.live:
                return cand, self._ambiguous(cand, cited, self.live)
        hits = self.by_base.get(os.path.basename(cited))
        if not hits:
            return None, 'NOT-IN-REPO'
        if len(hits) == 1:
            return hits[0], None
        return None, 'AMBIGUOUS-PATH'


class History:
    """`git log -S` and `git show`, both memoised."""

    def __init__(self, root, ref='HEAD'):
        self.root = root
        self.ref = ref
        self._sha = {}
        self._blob = {}

    def _first_seen(self, citing):
        """{token: the oldest commit whose diff ADDED a line carrying it}, in ONE git call.

        This replaces a `git log -S <token>` per citation, which cost one full-history diff per
        citation and took minutes over the press path. `--reverse -U0` walks the history once and
        the added lines are exactly the lines a citation is introduced by, so the walk answers every
        token in the file at the price of one call."""
        if citing in self._sha:
            return self._sha[citing]
        # The marker is `@@@`, NOT `@@`: a unified diff's own hunk header also starts with `@@`
        # (`@@ -12,7 +12,8 @@`), so an `@@` marker reads a hunk header as a commit id and every
        # citation is then dated to garbage. Measured: with `@@` the whole press path classified
        # SITE-ABSENT - 920 of them - and SAME and MOVED both fell to ZERO.
        p = git(self.root, 'log', '-p', '--reverse', '--format=@@@%H', '-U0', self.ref, '--', citing)
        first = {}
        if p.returncode == 0:
            sha = None
            for line in p.stdout.split('\n'):
                if line.startswith('@@@'):
                    sha = line[3:].strip() or None
                    continue
                if sha is None or not line.startswith('+') or line.startswith('+++') \
                        or line.startswith('@@@'):
                    continue
                for m in TOKEN.finditer(line):
                    token = '%s:%s' % (m.group(1), m.group(2))
                    first.setdefault(token, sha)
        self._sha[citing] = first
        return first

    def introducing_commit(self, citing, token):
        return self._first_seen(citing).get(token)

    def blob(self, sha, path):
        key = (sha, path)
        if key not in self._blob:
            p = git(self.root, 'show', '%s:%s' % (sha, path))
            self._blob[key] = p.stdout.split('\n') if p.returncode == 0 else None
        return self._blob[key]


class Vendors:
    """The headers the repository does not track, indexed by basename and read on demand.

    No git is involved anywhere in this class: the baseline for a vendor citation is the file itself,
    and the file is a stable upstream checkout that this project never writes to. The index is one
    walk; a header is read only when a citation names its basename. `defines()` is memoised per
    basename because all candidate files for a basename are read together."""

    def __init__(self, root, roots=VENDOR_ROOTS):
        self.root = root
        self.present = [r for r in roots if os.path.isdir(os.path.join(root, r))]
        self.by_base = collections.defaultdict(list)
        for r in self.present:
            for dirpath, _dirs, files in os.walk(os.path.join(root, r)):
                for f in files:
                    if f.endswith('.h'):
                        self.by_base[f].append(os.path.relpath(os.path.join(dirpath, f), root))
        self._lines = {}
        self._defs = {}

    def lines(self, path):
        if path not in self._lines:
            try:
                with open(os.path.join(self.root, path), encoding='utf-8', errors='replace') as fh:
                    self._lines[path] = fh.read().split('\n')
            except OSError:
                self._lines[path] = []
        return self._lines[path]

    def defines(self, base):
        """[(path, lineno, name, value)] for every `#define` under this basename, on disk."""
        if base not in self._defs:
            out = []
            for p in self.by_base.get(base, []):
                for j, line in enumerate(self.lines(p), 1):
                    d = VENDOR_DEF.match(line)
                    if d:
                        out.append((p, j, d.group(1), d.group(2)))
            self._defs[base] = out
        return self._defs[base]

    @staticmethod
    def anchors(local, comment, cite_start):
        """The identifiers a `#define`'s own line names, in the order they are trusted.

        The local macro's name, that name with its `ST_` prefix stripped, and every ALL-CAPS
        identifier the comment writes AHEAD of the citation - which is how `SDHCI_MAX_DIV_SPEC_300`
        is named by a line whose own macro is called `ST_SET_DIV_MAX`."""
        out = []
        stripped = local[3:] if local.startswith('ST_') else local
        for a in [stripped, local] + ALL_CAPS.findall(comment[:cite_start]):
            if a not in out and a != 'ST':
                out.append(a)
        return out

    def classify(self, line, cited, n):
        """The vendor verdict for one citing line, or None when the line is not one to read.

        Returns (verdict, detail) with `detail` the thing a reader needs: which line the anchor is
        at, or which name the cited line does define."""
        m = SHAPE_DEFINE.match(line)
        if not m:
            return None
        local, value, comment = m.group(1), m.group(2), m.group(3)
        c = VENDOR_CITE.search(comment)
        if not c or c.group(1) != cited:
            return None
        base, want = c.group(1), int(c.group(2))
        if not self.by_base.get(base):
            return 'VENDOR-NOFILE', 'no %s under the vendor roots (%s)' % (
                base, ', '.join(self.present) or 'none present')
        defs = self.defines(base)
        anchors = self.anchors(local, comment, c.start())

        named = [d for d in defs if d[2] in anchors]
        if any(d[1] == want for d in named):
            return 'VENDOR-OK', '%s is defined at %s:%d' % (
                [d for d in named if d[1] == want][0][2], base, want)
        # The NAME reading is decided BEFORE the value reading, and the order matters: a name is an
        # identity and a value can coincide. So a citation whose anchor is #define'd at exactly one
        # other line is refused even when the cited line happens to define the same number.
        lines_named = sorted({d[1] for d in named})
        if len(lines_named) == 1:
            return 'VENDOR-ELSEWHERE', '%s is #define at %s:%d' % (anchors[0], base, lines_named[0])
        if lines_named:
            return 'VENDOR-MULTI', '%s is #define at %d lines (%s)' % (
                anchors[0], len(lines_named),
                ', '.join(':%d' % x for x in lines_named[:6]))
        at_line = [d for d in defs if d[1] == want]
        lv = int_literal(value)
        same = [d for d in at_line if lv is not None and int_literal(d[3]) == lv]
        if same:
            return 'VENDOR-OK', '%s:%d is %s = %s (the local name is a re-spelling)' % (
                base, want, same[0][2], value)
        if at_line:
            return 'VENDOR-UNRESOLVED', '%s:%d defines %s, a name this line does not carry' % (
                base, want, at_line[0][2])
        return 'VENDOR-UNRESOLVED', '%s:%d is not a #define' % (base, want)


def classify(tree, hist, citing, cited, n, line='', vendors=None):
    """The verdict for one citation, plus the line it moved to when that is decidable."""
    path, why = tree.resolve(cited)
    if why:
        if why == 'NOT-IN-REPO' and vendors is not None:
            got = vendors.classify(line, cited, n)
            if got is not None:
                return got
        return why, None
    now = tree.lines(path)
    if now is None:
        return 'NOT-IN-REPO', None
    if n < 1 or n > len(now):
        return 'PAST-END', None

    sha = hist.introducing_commit(citing, '%s:%d' % (cited, n))
    if sha is None:
        return 'UNDATED', None
    old_path, old_why = tree.resolve(cited, at=sha)
    if old_why:
        return old_why, None
    old = hist.blob(sha, old_path)
    if old is None or n < 1 or n > len(old):
        return 'SITE-ABSENT', None
    want = norm(old[n - 1])
    if want == norm(now[n - 1]):
        return 'SAME', None
    if not want:
        return 'SAME', None          # the cited line was blank when written: nothing to follow
    hits = [i for i, line in enumerate(now, 1) if norm(line) == want]
    if len(hits) == 1:
        return 'MOVED', hits[0]
    if len(hits) > 1:
        return 'MOVED-AMBIGUOUS', hits
    return 'RE-WRITTEN', None


def citations_in(root, citing):
    try:
        with open(os.path.join(root, citing), encoding='utf-8', errors='replace') as fh:
            for lineno, line in enumerate(fh, 1):
                for m in TOKEN.finditer(line):
                    yield lineno, m.group(1), int(m.group(2)), line
    except OSError:
        return


def live_rung(root, override=None):
    """The rung whose narration prints on this arm, or None with the reason it is unknown.

    `override` is a TEST SEAM and not a switch on the artifact: it changes WHICH PARAGRAPH is
    examined, never a byte of anything, and it is what makes the refusal path demonstrable
    without editing this file. `--live-rung 31` refuses the rung-31 paragraph's citation and
    prints the line it moved to, on the same tree where the rung-33 paragraph is clean.
    """
    if override is not None:
        return override, None
    try:
        with open(os.path.join(root, CONFIG), encoding='utf-8', errors='replace') as fh:
            for line in fh:
                if line.startswith(RUNG_SWITCH + '='):
                    value = line.split('=', 1)[1].strip()
                    if value.isdigit():
                        return int(value), None
                    return None, 'the live record carries a non-numeric %s (%s)' % (RUNG_SWITCH, value)
    except OSError:
        return None, 'there is no %s, so which paragraph prints cannot be read' % CONFIG
    return None, 'the live record does not carry %s' % RUNG_SWITCH


def rung_of_each_line(root, citing):
    """For each line of a script, the rung its narration is attributed to, or None.

    `rung_para N` binds its own line; an `if [[ $wst == N ]]` binds the block it opens, which
    `elif`/`if` at the same-or-less indent ends. A `#` comment is attributed to None: it never
    prints."""
    out = {}
    current = None
    guard_indent = None
    try:
        with open(os.path.join(root, citing), encoding='utf-8', errors='replace') as fh:
            for lineno, line in enumerate(fh, 1):
                stripped = line.strip()
                indent = len(line) - len(line.lstrip())
                if guard_indent is not None:
                    if ELIF_GUARD.match(line) and indent <= guard_indent:
                        current, guard_indent = None, None
                    elif stripped and indent < guard_indent and not stripped.startswith('#'):
                        current, guard_indent = None, None
                m = WST_GUARD.search(line)
                if m:
                    current = int(m.group(1) or m.group(2))
                    guard_indent = indent
                m = RUNG_PARA.match(line)
                if m:
                    current = int(m.group(1))
                out[lineno] = None if stripped.startswith('#') else current
    except OSError:
        return {}
    return out


def scan(root, citing_files, all_refusing, live_override=None, vendors=None):
    tree = Tree(root)
    hist = History(root)
    if vendors is None:
        vendors = Vendors(root)
    rung, why = live_rung(root, live_override)
    tally = collections.Counter()
    refused, reported = [], []
    seen = set()
    n_cites = 0
    for citing in citing_files:
        attributed = rung_of_each_line(root, citing)
        for lineno, cited, n, line in citations_in(root, citing):
            # The dedupe is per OCCURRENCE, not per site. Keyed on (citing, cited, n) it swallowed
            # the citation this axis exists for: `sdhci.h:79` is cited twice in entry_storage.c,
            # once in a prose comment at :253 and once in the `#define` at :266 - and the prose line
            # comes first, so the readable copy was dropped and the defect went unreported. One site,
            # two readings, and the check kept the one it could not read: measured, 59 sites read 49.
            key = (citing, lineno, cited, n)
            if key in seen:
                continue
            seen.add(key)
            n_cites += 1
            verdict, at = classify(tree, hist, citing, cited, n, line, vendors)
            tally[verdict] += 1
            live_here = all_refusing or (rung is not None and attributed.get(lineno) == rung)
            row = (citing, lineno, cited, n, verdict, at)
            if verdict in VERDICTS_REFUSE_LIVE and live_here:
                refused.append(row)
            elif verdict not in VERDICTS_PASS:
                reported.append((verdict, row))
    return tally, refused, reported, n_cites, rung, why, vendors


def describe(row):
    citing, lineno, cited, n, verdict, at = row
    if verdict == 'MOVED':
        return '%s:%d cites %s:%d - that line now reads something else and the text it named is at :%d' \
               % (citing, lineno, cited, n, at)
    if verdict == 'VENDOR-ELSEWHERE':
        return '%s:%d cites %s:%d - that header is not tracked here, and %s' \
               % (citing, lineno, cited, n, at)
    if verdict == 'AMBIGUOUS-PATH':
        return '%s:%d cites %s:%d - that basename is tracked more than once in the live tree, so this' \
               ' citation cannot be resolved without guessing' % (citing, lineno, cited, n)
    if verdict == 'MOVED-AMBIGUOUS':
        cands = ', '.join(':%d' % c for c in at[:6])
        return '%s:%d cites %s:%d - the text it named now appears at %d lines (%s), so where it went' \
               ' is not decided here' % (citing, lineno, cited, n, len(at), cands)
    return '%s:%d cites %s:%d - %s' % (citing, lineno, cited, n, verdict)


def selftest(root):
    """Fixtures for the verdicts, driven through a synthetic repository in a temp directory.

    A check whose baseline is git cannot be tested against its own repository without pinning that
    repository's history, so the end-to-end fixtures build one: two commits, a citation in the first,
    and a cited line that MOVES, that is REWRITTEN, and that is DUPLICATED in the second."""
    import tempfile
    wrong = [0]
    total = [0]

    def say(ok, why, extra=''):
        total[0] += 1
        print('  %-4s %s%s' % ('ok' if ok else 'FAIL', why, (' - ' + extra) if extra and not ok else ''))
        if not ok:
            wrong[0] += 1

    # --- the pure fixtures ---
    say(norm('    a  b\tc') == 'a b c', 'norm collapses indentation and runs of space')
    say(norm('a b') != norm('a  c'), 'norm keeps two different lines apart')
    say(norm('') == '', 'norm of a blank line is empty, which is the "nothing to follow" case')

    with tempfile.TemporaryDirectory() as tmp:
        def sh(*args):
            return subprocess.run(args, capture_output=True, text=True, cwd=tmp)
        sh('git', 'init', '-q')
        sh('git', 'config', 'user.email', 'selftest@example.invalid')
        sh('git', 'config', 'user.name', 'selftest')
        os.makedirs(os.path.join(tmp, 'cited'))

        # ---- commit 1: the citation is written, and it is TRUE ----
        with open(os.path.join(tmp, 'cited', 'site.c'), 'w') as fh:
            fh.write('int alpha(void) { return 1; }\n'
                     'int beta(void)  { return 2; }\n'
                     'int gamma(void) { return 3; }\n')
        with open(os.path.join(tmp, 'doc.sh'), 'w') as fh:
            fh.write('# cited/site.c:3 is gamma\n'
                     '# cited/site.c:2 is beta\n'
                     '# cited/site.c:1 is alpha\n'
                     '# cited/site.c:9 is past the end\n'
                     '# vendor.c:4 is not ours\n'
                     '# doc.sh:1 cites itself\n')
        sh('git', 'add', '-A')
        sh('git', 'commit', '-q', '-m', 'one')

        # ---- commit 2: gamma moves to :5, beta is rewritten, alpha is duplicated ----
        with open(os.path.join(tmp, 'cited', 'site.c'), 'w') as fh:
            fh.write('/* a new head comment */\n'
                     'int alpha(void) { return 1; }\n'          # the text :1 named, now twice
                     'int delta(void) { return 4; }\n'
                     'int epsilon(void) { return 5; }\n'
                     'int gamma(void) { return 3; }\n'          # moved from :3 to :5
                     'int alpha(void) { return 1; }\n')
        with open(os.path.join(tmp, 'doc.sh'), 'a') as fh:
            fh.write('# a citation added late: cited/site.c:5\n')
        sh('git', 'add', '-A')
        sh('git', 'commit', '-q', '-m', 'two')

        # ---- commit 3: :5 moves again, so a citation ADDED at commit 2 has something to follow.
        #      If the tool dated it to commit 1 instead, line 5 did not exist then and the verdict
        #      would be SITE-ABSENT rather than MOVED - which is what makes this fixture sharp. ----
        with open(os.path.join(tmp, 'cited', 'site.c'), 'w') as fh:
            fh.write('/* head */\n'
                     'int alpha(void) { return 1; }\n'
                     'int delta(void) { return 4; }\n'
                     'int epsilon(void) { return 5; }\n'
                     'int zeta(void) { return 6; }\n'
                     'int alpha(void) { return 1; }\n'
                     'int gamma(void) { return 3; }\n')          # moved from :5 to :7
        sh('git', 'add', '-A')
        sh('git', 'commit', '-q', '-m', 'three')

        tree = Tree(tmp)
        hist = History(tmp, ref='HEAD')
        got = {}
        for cited, n in [('cited/site.c', 3), ('cited/site.c', 2), ('cited/site.c', 1),
                         ('cited/site.c', 9), ('vendor.c', 4), ('cited/site.c', 5)]:
            got['%s:%d' % (cited, n)] = classify(tree, hist, 'doc.sh', cited, n)

        say(got['cited/site.c:3'][0] == 'MOVED' and got['cited/site.c:3'][1] == 7,
            'a moved citation is MOVED and the new line is the one printed',
            str(got['cited/site.c:3']))
        say(got['cited/site.c:2'][0] == 'RE-WRITTEN',
            'a rewritten site is RE-WRITTEN rather than moved', str(got['cited/site.c:2']))
        say(got['cited/site.c:1'][0] == 'MOVED-AMBIGUOUS' and len(got['cited/site.c:1'][1]) == 2,
            'a text that now appears twice is MOVED-AMBIGUOUS and the candidates are named',
            str(got['cited/site.c:1']))
        say(got['cited/site.c:9'][0] == 'PAST-END',
            'a line past the end of the live file is PAST-END', str(got['cited/site.c:9']))
        say(got['vendor.c:4'][0] == 'NOT-IN-REPO',
            'a file that is not tracked here is named, not guessed', str(got['vendor.c:4']))
        say(got['cited/site.c:5'][0] == 'MOVED' and got['cited/site.c:5'][1] == 7,
            'a citation added in a later commit is dated to THAT commit, not to the file',
            str(got['cited/site.c:5']))

        # --- the resolution fixtures, which are the `build.sh` defect ---
        os.makedirs(os.path.join(tmp, 'tools'))
        with open(os.path.join(tmp, 'tools', 'dup.sh'), 'w') as fh:
            fh.write('a\n')
        os.makedirs(os.path.join(tmp, 'scripts'))
        with open(os.path.join(tmp, 'scripts', 'dup.sh'), 'w') as fh:
            fh.write('b\n')
        sh('git', 'add', '-A')
        sh('git', 'commit', '-q', '-m', 'three')
        t2 = Tree(tmp)
        say(t2.resolve('tools/dup.sh')[0] == 'tools/dup.sh',
            'a path that names its own directory resolves to itself')
        say(t2.resolve('dup.sh')[1] == 'AMBIGUOUS-PATH',
            'a bare basename two roots hold is AMBIGUOUS-PATH rather than the first hit',
            str(t2.resolve('dup.sh')))
        say(t2.resolve('doc.sh')[0] == 'doc.sh',
            'a bare basename only one root holds still resolves')

    # --- the attribution fixtures, against the REAL narration file ----------------
    # The live/report split is decided by `rung_of_each_line`, so the rule that decides what prints
    # is itself tested - and it is tested STRUCTURALLY (by what the lines say), never by line number,
    # because a fixture frozen to a line number would be the defect this tool exists to catch.
    real = os.path.join(root, 'tools', 'verify_press_ready.sh')
    if os.path.isfile(real):
        attr = rung_of_each_line(root, 'tools/verify_press_ready.sh')
        with open(real, encoding='utf-8', errors='replace') as fh:
            rp31 = [i for i, l in enumerate(fh, 1) if l.startswith('    rung_para 31 ')]
        say(bool(rp31) and attr.get(rp31[0]) == 31,
            'a rung_para line is attributed to that rung',
            'rung_para 31 at %s' % rp31)
        with open(real, encoding='utf-8', errors='replace') as fh:
            cmt = [i for i, l in enumerate(fh, 1)
                   if l.strip().startswith('#') and 'entry_storage.c:' in l]
        say(bool(cmt) and all(attr.get(i) is None for i in cmt),
            'a commented citation is attributed to NO rung, because a comment never prints',
            '%d comment lines, first at %s' % (len(cmt), cmt[:1]))

    # --- the vendor-axis fixtures, against a synthetic vendor root ----------------
    # The axis is driven through `Vendors.classify` on hand-written lines, so each verdict is
    # produced by a line built to produce it and the test says nothing about this repository. The
    # local names carry the real `ST_` prefix, because the prefix strip is part of the anchor rule.
    with tempfile.TemporaryDirectory() as tmp:
        os.makedirs(os.path.join(tmp, 'vendor'))
        with open(os.path.join(tmp, 'vendor', 'site.h'), 'w') as fh:
            fh.write('#define SITE_ALPHA\t0x10\n'
                     '#define SITE_BETA\t0x20\n'
                     '#define SITE_GAMMA\t0x30\n'
                     '#define SITE_DELTA\t(1 << 6)\n')
        v = Vendors(tmp, roots=('vendor',))
        cases = [
            ('#define ST_SITE_ALPHA 0x10u /* site.h:1 */', 'site.h', 1,
             'VENDOR-OK', 'a #define cited at the line that defines it is VENDOR-OK'),
            ('#define ST_SITE_BETA 0x20u /* site.h:2 */', 'site.h', 2,
             'VENDOR-OK', 'likewise for the second one, so the verdict is not a constant'),
            ('#define ST_SITE_BETA 0x20u /* site.h:5 */', 'site.h', 5,
             'VENDOR-ELSEWHERE', 'a #define cited at a line that defines it nowhere else is refused'),
            ('#define ST_OMEGA 0x30u /* site.h:3 */', 'site.h', 3,
             'VENDOR-OK', 'a re-spelled local name is carried by the VALUE reading, not by the name'),
            ('#define ST_NOPE 0x99u /* site.h:3 */', 'site.h', 3,
             'VENDOR-UNRESOLVED', 'a name that is nowhere and a value that differs is unresolved'),
            ('#define ST_SITE_BETA 0x20u /* absent.h:1 */', 'absent.h', 1,
             'VENDOR-NOFILE', 'a basename no header carries is VENDOR-NOFILE, not a silent pass'),
            ('#define ST_DELTA 0x40u /* site.h:4 */', 'site.h', 4,
             'VENDOR-UNRESOLVED',
             'a VALUE that is an EXPRESSION is not evaluated, so it cannot agree (0x40 == (1 << 6))'),
        ]
        for line, base, n, want, why in cases:
            got = v.classify(line, base, n)
            say(got is not None and got[0] == want, why, 'want %s, got %s' % (want, got))
        # The one-value-two-readings fixture: the anchor is #define'd at :2 and the line cited is
        # :1, where a DIFFERENT name carries the SAME value. The name reading must win, because a
        # coincidence of values is not a citation that resolves.
        with open(os.path.join(tmp, 'vendor', 'twin.h'), 'w') as fh:
            fh.write('#define TWIN_X\t0x10\n'
                     '#define SITE_BETA\t0x10\n')
        v2 = Vendors(tmp, roots=('vendor',))
        got = v2.classify('#define ST_SITE_BETA 0x10u /* twin.h:1 */', 'twin.h', 1)
        say(got is not None and got[0] == 'VENDOR-ELSEWHERE',
            'a value that coincides at the cited line does not carry a citation whose name is elsewhere',
            str(got))

    # The count is COUNTED, not typed: 675 section 4 measured the cost of a number written into
    # the sentence that reports it, and this one had been typed. Counting `total` at `say` also
    # makes a fixture that is deleted show up as a smaller total rather than as nothing.
    print('__SELFTEST__ %d fixture(s), %d wrong' % (total[0], wrong[0]))
    return wrong[0]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--selftest', action='store_true')
    ap.add_argument('--file', action='append', default=[], help='a citing file to scan (repeatable)')
    ap.add_argument('--live-rung', type=int, default=None,
                    help="examine this rung's paragraph instead of the arm's own (a test seam)")
    ap.add_argument('--all', action='store_true',
                    help='scan every tracked .sh/.py/.md/.txt instead of just the press path')
    ap.add_argument('--all-refusing', action='store_true',
                    help='refuse in every citing file instead of only the live arm\'s own prose')
    ap.add_argument('-v', '--verbose', action='store_true')
    args = ap.parse_args()

    root = repo_root()
    if args.selftest:
        print('check_line_citations: selftest (a synthetic repository, so the baseline is pinned)')
        sys.exit(2 if selftest(root) else 0)

    if args.file:
        citing_files = args.file
    else:
        tracked = [t for t in git(root, 'ls-files').stdout.split('\n') if t]
        if args.all:
            citing_files = [t for t in tracked if t.endswith(('.sh', '.py', '.md', '.txt'))
                            or (t.startswith('src/') and t.endswith(('.c', '.h')))]
        else:
            # The press path, plus the file the ladder's own constant table lives in. That file is
            # here for the VENDOR axis, whose baseline is a file on disk and needs no history walk -
            # so it costs 0.4 s for 207 citations, measured - and it is where the register offsets
            # the whole ladder is built on were read from.
            citing_files = [t for t in list(PRESS_PATH) + ['src/entry/entry_storage.c'] if t in tracked]

    tally, refused, reported, n_cites, rung, why, vendors = scan(
        root, citing_files, args.all_refusing, args.live_rung)

    print('check_line_citations: %d citation(s) over %d citing file(s)' % (n_cites, len(citing_files)))
    if not vendors.present:
        print('  VENDOR AXIS OFF: none of %s is present, so no vendor citation was read'
              % (', '.join(VENDOR_ROOTS),))
    else:
        print('  vendor axis over %s: %d header(s) indexed by basename'
              % (', '.join(vendors.present), len(vendors.by_base)))
    if rung is None:
        print('  the live arm\'s rung is UNKNOWN: %s' % why)
        print('  so nothing is refused on the live-prose axis; every moved citation is reported below')
    else:
        print('  the prose that prints for rung %d refuses%s'
              % (rung, ' (read from %s)' % CONFIG if args.live_rung is None else
                 ' (FORCED by --live-rung, so this tests the CHECK and not the arm)'))
    order = ['SAME'] + [v for v in VERDICTS_REPORT] + ['MOVED']
    for v in order:
        if tally.get(v):
            print('    %-18s %d' % (v, tally[v]))
    for v in sorted(set(tally) - set(order)):
        print('    %-18s %d' % (v, tally[v]))

    if args.verbose and reported:
        print('\nREPORTED (not refused - the historical record, a dormant paragraph, or a comment):')
        for verdict, row in reported:
            print('  %-16s %s' % (verdict, describe(row)))

    if refused:
        print('\nREFUSED - these citations are in the prose that prints for the live arm, and the')
        print('  text each one named is not where it says it is:')
        for row in refused:
            print('  %s' % describe(row))
        print('\ncheck_line_citations: REFUSED. A citation is a claim about the tree; when the tree')
        print('  grows under it the number still resolves and the claim does not. Update the citation')
        print('  to the line printed above, or say in the sentence that it names the rung where it')
        print('  was true.')
        return 1

    print('\ncheck_line_citations: ok - no citation in the live arm\'s own prose has moved.')
    if tally.get('MOVED'):
        print('  %d other citation(s) name a line that has moved; they are in the historical record,'
              % tally['MOVED'])
        print('  in a paragraph for another rung or in a comment, and -v prints them.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
