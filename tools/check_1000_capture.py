#!/usr/bin/env python3
"""Mechanical adjudicator for the 1000 press capture - by value, against the FROZEN pre-registration.

The authority is the `# 1000 PRESS PRE-REGISTRATION` block embedded in `scripts/press_1000.sh` (its
POINTS (1)-(4), REGRESSION GUARDS (5) and the FALSIFIERS (6)).  This tool extracts ONLY the keys and
literals that block names, evaluates ONLY the predicates it fixes, and echoes the block's own
reading text next to the values; it never reinterprets.  Where a frozen sentence and the capture
disagree, the output says so and the lead adjudicates - that is a pre-reg deviation to REPORT, not
to paper over (`mi4-a-claim-in-a-comment-is-not-a-check`, `mi4-self-written-record-is-not-a-constraint`).

WHY A TOOL, not grep: the capture's key lines are a fraction of a 20k-line console that also carries
multi-KB blob lines (a naive `grep key` prints a haystack), the record GROUPS (one usvc/pet/anom
record = several consecutive kv lines) need an ordered walk of the stream, the `_calls` attribution
needs "the last preceding xnu_live_storage_rd_ordinal", and the same reading must be repeatable.

The predicates, one per producer (read by value, 2026-10-13):
  (1) tick anomalies:  d = CNTVCT.lo(entry) - CNTVCT.lo(arm), period = tick_hz/1000;  `_tval`
      consistent with `_d` iff (tval + d) mod 2^32 == period (within TVAL_SLACK - the two reads'
      distance): the countdown ran through the whole gap = MASKED DELIVERY; anything else = a
      SECOND WRITER armed the deadline.  `_ctl` bit 1 (IMASK) set or bit 0 (ENABLE) clear = the
      second writer's second signature.
  (2) pet feed: publish rule n in {1..4} u powers-of-two; ATTRIBUTION: every `_calls` is a power of
      two OR <= 4, AND <= the last preceding `xnu_live_storage_rd_ordinal`.
  (3) usvc: n <= 64 => all eight fields; past 64 => `_n`/`_num` only, n a power of two.
  (5) guards: literals + the frozen watchdog values (29 s net unchanged from 999).

Usage:  tools/check_1000_capture.py <capture.txt>

Known-good self-test (the 999 capture = the PRE-1000 image, so the NEW-instrument predicates are the
discriminating half): run it on
  out/stage90/captures/recovered-unknown-arm-20261013-030640-last_kmsg.txt
It must reproduce 999's recorded facts - `xnu_live_cap=0x00006000`; the exec-done trio present;
`IOS7SHREG map_slide ret=0`; `map_file kr=0`; `check_np ret=12` x1; the four `IOS7LAB cache
mapping` lines; the geometry line x1; `VMHINT991` x2; `vm_allocate64` x0;
`hw_watchdog_timeout_s=0x1d` - AND exit 1 with the falsifiers (6)(a)/(b) firing: zero
`xnu_live_usvc_*`, zero `xnu_live_tick_anom_*`, zero `xnu_live_wdt_pet_*` is exactly what "this
capture is not the 1000 arm" looks like, and reporting that instead of judging is the point.

Exit: 0 = no hard anomaly; 1 = at least one hard anomaly (any `[FAIL]` line).  A hard anomaly means
STOP-and-report per the pre-reg's FAILURE SIGNATURES, not "rerun until green".
"""

import re
import sys

KV_RE = re.compile(r'(xnu_live_[a-z0-9_]+|hw_watchdog_[a-z0-9_]+)=([0-9A-Za-z_.+-]+)')

# The four instrument families 1000 is (the census discriminates a 1000 arm from its baselines).
NEW_FAMILIES = (
    ("xnu_live_tick_anom_", "(1) the late-delivery anomalies"),
    ("xnu_live_tick_sum_",  "(1) the tick aggregate summaries"),
    ("xnu_live_wdt_pet_",   "(2) the pet feed records (direct)"),
    ("xnu_live_usvc_",      "(3) the user-SVC frontier"),
)

LADDER_LITERALS = (
    ("IOS7SHREG map_slide ret=", "438's own exit - MUST be present and 0 (999's success; else STOP)"),
    ("IOS7SHREG map_file kr=",   "the G1 reading (must stay kr=0)"),
    ("check_np ret=12",          "the pre-438 constant (must stay; exactly once)"),
    ("IOS7LAB cache mapping",    "the wire's mapping lines (exactly 4; 0 = wire regression)"),
    ("IOS7LAB shared region",    "the geometry line (exactly once; 0 = the define did not reach)"),
    ("VMHINT991",                "the 991 guard (exactly 2x at exec, both captures)"),
    ("vm_allocate64",            "absent = EXPECTED (997: the failure path's buffer alloc)"),
)

# (5) REGRESSION: the watchdog net is 999's (29 s), unchanged by 1000.
FIXED_1000 = {
    "hw_watchdog_timeout_s": 0x0000001D,          # 29 s
    "hw_watchdog_bite_ticks_written": 0x000FFFA0, # bark 0xE7FA9 + the same extra 0x17FF7
}
BARK_1000 = 0xE7FA9          # xnu_live_wdt_bark, frozen (25 s was 0xC7FB5)
PERIOD_FALLBACK = 19200      # tick_hz 19,200,000 / MSM8974_TICK_HZ_DIV 1000
TVAL_SLACK = 512             # ticks the entry's two reads may sit apart
FULL_N = 64                  # USVC_FULL_N - the full-field window

FAILURES = []


def to_int(s):
    try:
        return int(s, 16) if s.lower().startswith("0x") else int(s, 10)
    except ValueError:
        return None


def fmt(v):
    return "?" if v is None else "0x%08x(%d)" % (v, v)


def is_pow2(v):
    return v is not None and v != 0 and (v & (v - 1)) == 0


def clip(s, n=200):
    return s if len(s) <= n else s[:n] + "..."


def hard(msg):
    FAILURES.append(msg)
    print("  [FAIL] " + msg)


def say(tag, msg=""):
    print(("  [%s] " % tag) + msg if msg else ("  [%s]" % tag))


class Capture:
    def __init__(self, path):
        # `newline=""` + `split("\n")`: NO universal-newline translation. The capture carries 67
        # lone CR bytes (census 2026-10-13); a text-mode read folds each into a line break and the
        # `@:line` numbers come out +67 off the project's grep/wc numbering after the first one
        # (measured: the cap tombstone reads @:28781 translated vs @:28714 in records/999-press-
        # read.txt). Line numbers are the reader's property (m997); this reader matches the record.
        with open(path, "r", errors="replace", newline="") as fh:
            self.text = fh.read()
        self.nbytes = len(self.text)
        self.lines = self.text.split("\n")
        if self.lines and self.lines[-1] == "":
            self.lines.pop()  # a trailing newline is not a line
        self.toks = []  # (line_no, key, raw, value_int|None) in stream order
        for line_no, line in enumerate(self.lines, 1):
            for m in KV_RE.finditer(line):
                self.toks.append((line_no, m.group(1), m.group(2), to_int(m.group(2))))
        self.lit = {}
        for line_no, line in enumerate(self.lines, 1):
            for lit, _why in LADDER_LITERALS:
                if lit in line:
                    e = self.lit.setdefault(lit, [0, line_no, line_no, clip(line.strip())])
                    e[0] += 1
                    e[2] = line_no

    def all(self, key):
        return [t for t in self.toks if t[1] == key]

    def first(self, key):
        a = self.all(key)
        return a[0] if a else None

    def last(self, key):
        a = self.all(key)
        return a[-1] if a else None

    def lit_count(self, lit):
        return self.lit.get(lit, [0])[0]

    def family_count(self, prefix):
        return sum(1 for t in self.toks if t[1].startswith(prefix))

    def ordered_pairs(self, key_a, key_b):
        """Values of (a, b) walked in stream order - for 'b at the last preceding a' predicates."""
        out, last_a = [], None
        for _, key, _raw, val in self.toks:
            if key == key_a:
                last_a = val
            elif key == key_b:
                out.append((val, last_a))
        return out

    def groups_of(self, nkey, prefix, fields):
        """One record = the nkey line + the field lines that follow it, in stream order."""
        out, cur = [], None
        fkeys = {prefix + f: f for f in fields}
        for ln, key, _raw, val in self.toks:
            if key == nkey:
                cur = [ln, val, {}, []]
                out.append(cur)
            elif key in fkeys and cur is not None:
                f = fkeys[key]
                cur[2][f] = (ln, val)
                cur[3].append(f)
        return out


def s_identity(cap):
    print("\n== IDENTITY / the 1000 instrument census ==")
    cap_hdr = cap.all("xnu_live_cap")
    if cap_hdr:
        vals = sorted(set(t[3] for t in cap_hdr))
        say("OBS", "xnu_live_cap header: %s%s" % (", ".join(fmt(v) for v in vals),
            "  (the 999/1000 cap; 0x4000 = a pre-999 image)" if vals == [0x6000] else ""))
        if 0x4000 in vals:
            hard("xnu_live_cap=0x00004000 - a PRE-999 image; not the 1000 arm")
    else:
        say("OBS", "no xnu_live_cap header token (older image or the header itself was lost)")
    census = {}
    for prefix, why in NEW_FAMILIES:
        n = cap.family_count(prefix)
        census[prefix] = n
        first = next((t for t in cap.toks if t[1].startswith(prefix)), None)
        say("OBS" if n else "---", "%s x%d%s" % (prefix, n,
            "  first @:%d" % first[0] if first else "  (%s)" % why))
    if not any(census.values()):
        hard("ZERO tokens of ALL FOUR new families - the capture carries none of 1000's instruments "
             "- either a pre-1000 image or the edits did not reach the image: REBUILD, do not judge")
    rd = cap.all("xnu_live_storage_rd_ordinal")
    if rd:
        say("OBS", "xnu_live_storage_rd_ordinal: %d records, first %s @:%d, last %s @:%d" % (
            len(rd), fmt(rd[0][3]), rd[0][0], fmt(rd[-1][3]), rd[-1][0]))
    else:
        say("OBS", "xnu_live_storage_rd_ordinal: none (the read door never ran?)")
    hz = cap.first("xnu_live_tick_hz")
    say("OBS", "xnu_live_tick_hz: %s%s" % (fmt(hz[3]) if hz else "ABSENT",
        "  (period recorded = tick_hz/%du = %d ticks)" % (1000, (hz[3] // 1000) if hz else PERIOD_FALLBACK)
        if hz else "  (fallback period %d)" % PERIOD_FALLBACK))


def s1_tick(cap):
    print("\n== [1] TICK - the delivery sampled at its source (frozen A/B: masked vs 2nd writer) ==")
    hz = cap.first("xnu_live_tick_hz")
    period = (hz[3] // 1000) if hz else PERIOD_FALLBACK
    recs = cap.groups_of("xnu_live_tick_anom_n", "xnu_live_tick_anom_", ("d", "tval", "ctl"))
    if not recs:
        say("OBS", "no xnu_live_tick_anom_* records")
    for ln, n, f, order in recs:
        d = f.get("d", (0, None))[1]
        tval = f.get("tval", (0, None))[1]
        ctl = f.get("ctl", (0, None))[1]
        if d is None or tval is None or ctl is None:
            hard("anom n=%s @:%d is missing fields (%s) - the record's shape broke" % (
                fmt(n), ln, ",".join(order)))
            continue
        resid = min(d % period, period - (d % period)) if period else 0
        pred = (period - d) & 0xFFFFFFFF
        delta = min((tval - pred) & 0xFFFFFFFF, (pred - tval) & 0xFFFFFFFF)
        ctl_sig = bool(ctl & 0x2) or not (ctl & 0x1)
        say("OBS", "anom n=%s @:%d d=%s (mod period %d: residue %d) tval=%s pred=%s delta=%d ctl=%s%s" % (
            fmt(n), ln, fmt(d), period, resid, fmt(tval), fmt(pred), delta, fmt(ctl),
            "  CTL SIGNATURE (IMASK set / ENABLE clear)" if ctl_sig else ""))
        if delta <= TVAL_SLACK:
            say("READING", "  -> _tval consistent with _d: MASKED DELIVERY (the countdown ran "
                           "through the whole gap)")
        else:
            say("READING", "  -> _tval a value the gap could not have produced: a SECOND WRITER "
                           "armed the deadline")
        if ctl_sig:
            say("READING", "  -> _ctl carries the second writer's signature (bit1 IMASK / bit0 clear)")
    if recs:
        ns = [r[1] for r in recs]
        if ns != list(range(1, len(ns) + 1)):
            hard("anom _n not 1..N ascending (got %s) - the ordinal-before-write rule broke" %
                 [fmt(v) for v in ns])
        if len(recs) > 4:
            hard("anom records %d > 4 (the first-four publish bound violated)" % len(recs))
    sums = cap.groups_of("xnu_live_tick_sum_count", "xnu_live_tick_sum_", ("anom", "maxd"))
    if not sums:
        say("OBS", "no tick summaries (xnu_live_tick_sum_*) at all")
    for ln, cnt, f, _o in sums:
        say("OBS", "summary @:%d count=%s anom=%s maxd=%s" % (ln, fmt(cnt),
            fmt(f.get("anom", (0, None))[1]), fmt(f.get("maxd", (0, None))[1])))
    tq = [t[3] for t in cap.all("xnu_live_tick_irq")]
    if tq:
        say("OBS", "tick_irq ladder: %s" % " ".join(fmt(v) for v in tq))
    if sums:
        last_cnt = sums[-1][1]
        last_anom = sums[-1][2].get("anom", (0, None))[1]
        cnts = [g[1] for g in sums if g[1] is not None]
        climbing = len(cnts) >= 2 and cnts[-1] > cnts[0]
        stopped = (last_cnt == 122 and (last_anom or 0) == 0 and
                   not any(v is not None and v > 0x40 for v in tq))
        if last_cnt is not None and last_cnt > 122:
            say("READING", "sum_count=%d > 122 - the tick RESUMED after 999's collapse" % last_cnt)
        elif stopped:
            say("READING", "sum_count still 122 with 0 anomaly events and no later ladder value - "
                           "deliveries stopped entirely (a different finding than the collapse)")
        elif climbing and (last_anom or 0) == 0:
            say("READING", "absence of anomaly records WITH sum_count climbing - the healthy tick "
                           "(the negative cell this instrument is built to make readable)")


def s2_direct_pet(cap):
    print("\n== [2] THE DIRECT CHANNEL + pet feed (frozen: absence is evidence again) ==")
    cells = cap.all("xnu_live_capped")
    if not cells:
        say("PASS", "no xnu_live_capped key at all - the whole boot fit in the counted channel")
    else:
        for ln, _k, _r, v in cells:
            say("OBS", "xnu_live_capped=%s @:%d" % (fmt(v), ln))
        if any(t[3] == 0x4000 for t in cells):
            hard("capped==0x00004000 - the cap did NOT move (pre-999 image; STOP)")
        last_cap_ln = cells[-1][0]
        after = [t for t in cap.toks if t[0] > last_cap_ln]
        if after:
            say("READING", "capped tombstone @:%d and %d kv record(s) AFTER it (first: %s @:%d) - "
                           "those are direct by construction: the rung works as designed" % (
                               last_cap_ln, len(after), after[0][1], after[0][0]))
        else:
            hard("capped tombstone @:%d followed by NOTHING (no direct key of any kind) - per (2)'s "
                 "last sentence the channel edits did not reach the image: REBUILD" % last_cap_ln)
    recs = cap.groups_of("xnu_live_wdt_pet_n", "xnu_live_wdt_pet_", ("calls", "countdown"))
    if not recs:
        say("OBS", "no xnu_live_wdt_pet_* records - no pet fired (or the direct edit did not reach)")
    ns = [r[1] for r in recs]
    for ln, n, f, _o in recs:
        say("OBS", "pet n=%s @:%d calls=%s countdown=%s" % (fmt(n), ln,
            fmt(f.get("calls", (0, None))[1]), fmt(f.get("countdown", (0, None))[1])))
    for n in ns:
        if n is not None and n > 4 and not is_pow2(n):
            hard("pet _n=%s is neither <= 4 nor a power of two - the publish rule broke" % fmt(n))
    if ns and all(v is not None for v in ns) and ns != sorted(ns):
        hard("pet _n not ascending: %s" % [fmt(v) for v in ns])
    bark = cap.first("xnu_live_wdt_bark")
    if recs and recs[0][2].get("countdown"):
        cd0 = recs[0][2]["countdown"][1]
        say("OBS", "first feed's countdown %s vs bark/2 %s - the threshold-gate reading" % (
            fmt(cd0), fmt((bark[3] // 2) if bark else None)))
    pairs = cap.ordered_pairs("xnu_live_storage_rd_ordinal", "xnu_live_wdt_pet_calls")
    if not pairs:
        say("OBS", "xnu_live_wdt_pet_calls: none")
    else:
        say("OBS", "pet _calls vs the FROZEN sentence (power-of-two OR <= 4, AND <= last preceding "
                   "xnu_live_storage_rd_ordinal):")
        ok = 0
        for v, last_rd in pairs:
            p = is_pow2(v) or (v is not None and v <= 4)
            l = v is not None and last_rd is not None and v <= last_rd
            ok += (p and l)
            say("OBS", "  calls=%s  (pow2-or-<=4)=%s  <=last-rd(%s)=%s" % (
                fmt(v), "yes" if p else "no", fmt(last_rd), "yes" if l else "no"))
        say("OBS", "tally: both %d/%d - report any miss as a pre-reg deviation at adjudication" % (
            ok, len(pairs)))
    # the summaries must appear at BOTH claimed moments
    trio = [cap.first(k) for k in
            ("xnu_live_exec_done_seq", "xnu_live_exec_done_who", "xnu_live_exec_done_current")]
    sums = cap.groups_of("xnu_live_tick_sum_count", "xnu_live_tick_sum_", ("anom", "maxd"))
    sum_lines = [g[0] for g in sums]
    if all(t is None for t in trio):
        hard("exec-done trio ABSENT - per pre-reg the exempt-path edit did not reach the image => "
             "rebuild, do not judge")
    else:
        exec_ln = max(t[0] for t in trio)
        at_exec = [l for l in sum_lines if l > exec_ln]
        say("OBS" if at_exec else "---", "summaries after the trio (exec-done moment, trio ends "
            "@:%d): %s" % (exec_ln, at_exec if at_exec else "NONE"))
        if not at_exec and sums:
            hard("tick summaries never appear after the exec-done trio - the exec-moment call did "
                 "not reach the image: REBUILD")
    if recs:
        pet_ln = recs[0][0]
        at_pet = [l for l in sum_lines if l >= pet_ln]
        say("OBS" if at_pet else "---", "summaries at/after the first pet publish (@:%d): %s" % (
            pet_ln, at_pet if at_pet else "NONE"))


def s3_usvc(cap):
    print("\n== [3] THE USER-SVC FRONTIER (last _n = the last syscall the user side managed) ==")
    fields = ("num", "pid", "thr", "pc", "lr", "cpsr", "r0")
    recs = cap.groups_of("xnu_live_usvc_n", "xnu_live_usvc_", fields)
    if not recs:
        say("OBS", "no xnu_live_usvc_* records (falsifier (6)(a) is judged below)")
        return
    ns = [r[1] for r in recs]
    if ns != sorted(ns) or len(set(ns)) != len(ns):
        hard("usvc _n not strictly ascending: %s" % [fmt(v) for v in ns])
    full = [r for r in recs if r[1] is not None and r[1] <= FULL_N]
    lad = [r for r in recs if r[1] is not None and r[1] > FULL_N]
    for i, r in enumerate(full):
        if r[1] != i + 1:
            hard("full-window usvc _n skips: expected %d got %s @:%d" % (i + 1, fmt(r[1]), r[0]))
            break
    for r in lad:
        if not is_pow2(r[1]):
            hard("ladder usvc _n=%s not a power of two @:%d" % (fmt(r[1]), r[0]))
    nums = []
    for ln, n, f, order in recs:
        num = f.get("num", (0, None))[1]
        nums.append(num)
        if n is None:
            hard("usvc record @:%d has an unparsable _n - the reader cannot walk the frontier" % ln)
            continue
        if n <= FULL_N:
            missing = [k for k in fields if k not in f]
            if missing:
                hard("usvc n=%s @:%d missing full-window fields: %s (the 64-window shape broke)" % (
                    fmt(n), ln, ",".join(missing)))
                continue
            pid = f["pid"][1]
            cpsr = f["cpsr"][1]
            pc = f["pc"][1]
            if pid != 1:
                hard("usvc n=%s _pid=%s (frozen expectation `_pid` = 1 - launchd) - pre-reg "
                     "deviation: report, do not reinterpret" % (fmt(n), fmt(pid)))
            if cpsr is None or (cpsr & 0x1F) != 0x10:
                hard("usvc n=%s _cpsr=%s is not a USER-mode CPSR (mode bits 0x10; frozen) - a "
                     "kernel CPSR means the offset or the pointer is wrong: REBUILD" % (
                         fmt(n), fmt(cpsr)))
            if pc is None or pc == 0 or (pc >> 16) == 0x8000:
                hard("usvc n=%s _pc=%s - not a user VA (never 0x8000xxxx / 0; frozen) - REBUILD" % (
                    fmt(n), fmt(pc)))
        else:
            if num == 0:
                hard("ladder usvc _n=%s carries _num=0 (frozen: a deviation - SS_R12 points at "
                     "the wrong frame word; re-run the checker, then rebuild)" % fmt(n))
    if nums and all(v == 0 for v in nums):
        hard("(6)(c): ALL usvc _num = 0 - SS_R12 points at the wrong frame word")
    pcs = [r[2]["pc"][1] for r in full if "pc" in r[2]]
    if pcs:
        in_win = sum(1 for v in pcs if v is not None and 0x2C000000 <= v < 0x40000000)
        say("OBS", "full-window _pc range [%s .. %s]; %d/%d inside the 0x2c000000..0x40000000 "
                   "window" % (fmt(min(pcs)), fmt(max(pcs)), in_win, len(pcs)))
    cpsrs = sorted(set(r[2]["cpsr"][1] for r in full if "cpsr" in r[2]))
    say("OBS", "full-window _cpsr values: %s" % (" ".join(fmt(v) for v in cpsrs[:8]) +
        (" ..." if len(cpsrs) > 8 else "")))
    ln, n, f, order = recs[-1]
    frontier = "n=%s _num=%s" % (fmt(n), fmt(f.get("num", (0, None))[1]))
    if n is not None and n <= FULL_N:
        frontier += " pid=%s pc=%s lr=%s cpsr=%s r0=%s" % (
            fmt(f.get("pid", (0, None))[1]), fmt(f.get("pc", (0, None))[1]),
            fmt(f.get("lr", (0, None))[1]), fmt(f.get("cpsr", (0, None))[1]),
            fmt(f.get("r0", (0, None))[1]))
    say("READING", "LAST usvc record @:%d %s - THE FRONTIER: the last syscall the user side "
                   "managed, by construction whether or not the counted channel survives it" % (
                       ln, frontier))
    say("OBS", "full-window records: %d; ladder records: %d" % (len(full), len(lad)))


def s4_context(cap):
    print("\n== [4] THE CAP AND THE THINNING (context, not a new reading) ==")
    say("OBS", "cap stays 24576 (0x6000); the read-dump thinning ordinal moved 256 -> 64 at "
               "committed 0504be5 and is checked AT THE SOURCE by scripts/build_1000.sh "
               "(`rd_full_ = (st_read_count < 64u)`), not re-derivable from the capture")
    cells = cap.all("xnu_live_capped")
    if cells:
        say("OBS", "capped tombstone present (%s) - expected: the counted channel saturates and "
                   "the direct channel carries the trio, the pet feed, the anomalies, the "
                   "summaries and the SVC frontier past it" % fmt(cells[-1][3]))
    else:
        say("OBS", "no capped tombstone - the whole boot fit the counted channel")


def s5_guards(cap):
    print("\n== [5] REGRESSION GUARDS ==")
    for lit, n, want_txt in (("IOS7LAB cache mapping", 4, "the wire's four mapping lines"),
                             ("IOS7LAB shared region", 1, "the geometry line, exactly once"),
                             ("check_np ret=12", 1, "the pre-438 constant")):
        n_have = cap.lit_count(lit)
        if n_have == n:
            say("PASS", "%s x%d (%s)" % (lit, n_have, want_txt))
        elif n_have == 0:
            hard("%s x0 - regression (%s)" % (lit, want_txt))
        else:
            say("OBS", "%s x%d (expected %d: %s)" % (lit, n_have, n, want_txt))
    ret = [int(m.group(1)) for m in re.finditer(r'IOS7SHREG map_slide ret=(-?\d+)', cap.text)]
    if not ret:
        say("OBS", "no `map_slide ret=` - 999 had ret=0; its absence here is a reading, not a crash")
    elif all(v == 0 for v in ret):
        say("PASS", "IOS7SHREG map_slide ret=0 x%d (999's success held)" % len(ret))
    else:
        hard("map_slide ret=%s - STOPS per pre-reg (5): the slide regressed, STOP" % ret)
    mf = [int(m.group(1)) for m in re.finditer(r'IOS7SHREG map_file kr=(-?\d+)', cap.text)]
    if any(v != 0 for v in mf):
        hard("map_file kr!=0 - GEOMETRY REGRESSION (G1 held kr=0); stop and re-model")
    say("OBS", "map_file kr= values: %s" % (mf if mf else "none"))
    for lit, want in (("VMHINT991", 2), ("vm_allocate64", 0)):
        n = cap.lit_count(lit)
        say("PASS" if n == want else "OBS", "%s x%d (expected %d)" % (lit, n, want))
    for k, want in FIXED_1000.items():
        t = cap.first(k)
        if t is None:
            say("OBS", "%s: ABSENT" % k)
        elif t[3] == want:
            say("PASS", "%s=%s (frozen value, 999's net unchanged)" % (k, fmt(t[3])))
        else:
            say("OBS", "%s=%s (frozen value is %s - report verbatim)" % (k, fmt(t[3]), fmt(want)))
    bark = cap.first("xnu_live_wdt_bark")
    if bark is not None:
        say("PASS" if bark[3] == BARK_1000 else "OBS", "xnu_live_wdt_bark=%s (frozen %s)" % (
            fmt(bark[3]), fmt(BARK_1000)))
    slept = sum(1 for line in cap.lines if "mini4: the CPU slept" in line)
    usb = cap.family_count("xnu_live_usb_")
    say("OBS" if (slept or usb) else "PASS", "USB/pcx keys: `xnu_live_usb_*` x%d, "
        "`mini4: the CPU slept` lines x%d (expected 0/0 - the kernel never idles on this rung)" % (
            usb, slept))


def s6_falsifiers(cap):
    print("\n== [6] FALSIFIERS (frozen: rebuild, do not judge) ==")
    usvc = cap.family_count("xnu_live_usvc_")
    pets = cap.family_count("xnu_live_wdt_pet_")
    trio_any = any(cap.first(k) for k in
                   ("xnu_live_exec_done_seq", "xnu_live_exec_done_who", "xnu_live_exec_done_current"))
    ret = [int(m.group(1)) for m in re.finditer(r'IOS7SHREG map_slide ret=(-?\d+)', cap.text)]
    if usvc == 0 and trio_any and (ret and all(v == 0 for v in ret)):
        hard("(6)(a): `xnu_live_usvc_n` ABSENT while the exec trio and `map_slide ret=0` are "
             "present => the hook did not reach the image or the state pointer is wrong - REBUILD, "
             "the press cannot adjudicate it")
    rd = cap.all("xnu_live_storage_rd_ordinal")
    climbing = bool(rd) and (rd[-1][3] or 0) > (rd[0][3] or 0)
    if pets == 0 and climbing:
        hard("(6)(b): zero pet-feed records with the storage door demonstrably used (rd_ordinal "
             "%s -> %s) => the direct edit did not reach the image - same verdict: REBUILD" % (
                 fmt(rd[0][3]), fmt(rd[-1][3])))


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[0])
        print("usage: %s <capture.txt>" % sys.argv[0])
        return 2
    cap = Capture(sys.argv[1])
    print("1000 capture adjudicator - %s (%d bytes, %d lines)" % (
        sys.argv[1], cap.nbytes, len(cap.lines)))
    print("authority: the frozen 1000 PRESS PRE-REGISTRATION block in scripts/press_1000.sh")
    s_identity(cap)
    s1_tick(cap)
    s2_direct_pet(cap)
    s3_usvc(cap)
    s4_context(cap)
    s5_guards(cap)
    s6_falsifiers(cap)
    print("\n== VERDICT ==")
    if FAILURES:
        print("  %d hard anomal%s - STOP and report per the pre-reg FAILURE SIGNATURES:" % (
            len(FAILURES), "y" if len(FAILURES) == 1 else "ies"))
        for f in FAILURES:
            print("    - " + f)
        return 1
    print("  no hard anomaly; readings above are the evidence (absence lines are evidence too)")
    return 0


if __name__ == "__main__":
    sys.exit(main())