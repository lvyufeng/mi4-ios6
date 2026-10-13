#!/usr/bin/env python3
"""Mechanical adjudicator for the 999 press capture - by value, against the FROZEN pre-registration.

The authority is the `# 999 PRESS PRE-REGISTRATION` block embedded in `scripts/press_999.sh` (its
readings [1]-[5] and its CONSTANTS/GUARDS / REGRESSION lines).  This tool extracts ONLY the keys and
literals that block names, evaluates ONLY the predicates it fixes, and echoes the block's own reading
text next to the values; it never reinterprets.  Where a frozen sentence and the capture disagree,
the output says so and the lead adjudicates - that is a pre-reg deviation to REPORT, not to paper
over (`mi4-a-claim-in-a-comment-is-not-a-check`, `mi4-self-written-record-is-not-a-constraint`).

WHY A TOOL, not grep: the capture's key lines are a fraction of a 20k-line console that also carries
multi-KB blob lines (a naive `grep key` prints a haystack), the attribution sentences need an
ORDERED walk of the record stream, and the same reading must be repeatable for a second pass.

Usage:  tools/check_999_capture.py <capture.txt>

Known-good self-test (the 998 capture = the PRE-999 image): run it on
  out/stage90/captures/cancro-last_kmsg-998-armed-d13-dee822b0.txt
and it must reproduce 998's recorded facts: `xnu_live_capped=0x00004000` at :20502; ZERO
`xnu_live_wdt_*` / `xnu_live_exec_done_*` / `xnu_live_edge_*` tokens; exactly 4 `IOS7SHREG` lines
(check_np x2, map_slide entry, map_file kr=0) and NO `map_slide ret=`; 4 `IOS7LAB cache mapping`
lines; 1 `IOS7LAB shared region` line; `xnu_live_storage_rd_ordinal` 1..129 (last 0x81);
`hw_watchdog_timeout_s=0x19` (the 25 s pre-999 net).  The predicates that assert 999's frozen values
(mark them FAIL on the 998 capture) are the discriminating half of that self-test.

Exit: 0 = no hard anomaly; 1 = at least one hard anomaly (any `[FAIL]` line).  A hard anomaly means
STOP-and-report per the pre-reg's FAILURE SIGNATURES, not "rerun until green".
"""

import re
import sys

KV_RE = re.compile(r'(xnu_live_[a-z0-9_]+|hw_watchdog_[a-z0-9_]+)=([0-9A-Za-z_.+-]+)')

LADDER_LITERALS = (
    ("IOS7SHREG slide_enter",  "999 ladder (a) - _shared_region_slide entry"),
    ("IOS7SHREG slide_init",   "999 ladder (b) - step past slide_init (SUCCESS only)"),
    ("IOS7SHREG slide_copyin", "999 ladder (c) - past the slide_info copyin (both outcomes)"),
    ("IOS7SHREG map_slide ret=", "438's own exit - its absence at :1654 is the sanity-stop locator"),
    ("IOS7SHREG map_file kr=", "the G1 reading (must stay kr=0)"),
    ("check_np ret=12",        "the pre-438 constant (must stay; exactly once in 998)"),
    ("IOS7LAB cache mapping",  "the wire's mapping lines (exactly 4; 0 = wire regression)"),
    ("IOS7LAB shared region",  "the geometry line (exactly once; 0 = the define did not reach)"),
    ("VMHINT991",              "the 991 guard (exactly 2x at exec, both captures)"),
    ("vm_allocate64",          "absent = EXPECTED (997: the failure path's buffer alloc)"),
)

# hw_watchdog payload keys 999 moves (pre-reg [3] OBSERVABLES, frozen)
FIXED_999 = {
    "hw_watchdog_timeout_s": 0x0000001D,          # 29 s
    "hw_watchdog_bite_ticks_written": 0x000FFFA0, # bark 0xE7FA9 + the same extra 0x17FF7
}
# xnu_live_wdt_bark is published at runtime by the pet - checked in S3, not here.
RD_LBA_LO, RD_LBA_HI = 0x4A3838, 0x4A3C80  # pre-reg [4]: the slide copyin's range, if it runs

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
        with open(path, "r", errors="replace") as fh:
            self.text = fh.read()
        self.nbytes = len(self.text)
        self.toks = []  # (line_no, key, raw, value_int|None) in stream order
        for line_no, line in enumerate(self.text.splitlines(), 1):
            for m in KV_RE.finditer(line):
                self.toks.append((line_no, m.group(1), m.group(2), to_int(m.group(2))))
        self.lit = {}  # literal -> [count, first_line, last_line, sample]
        for line_no, line in enumerate(self.text.splitlines(), 1):
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

    def ordered_pairs(self, key_a, key_b):
        """Values of (a, b) walked in stream order - for 'b at the last preceding a' predicates."""
        out, last_a = [], None
        for _, key, _raw, val in self.toks:
            if key == key_a:
                last_a = val
            elif key == key_b:
                out.append((val, last_a))
        return out


def s_arm(cap):
    print("\n== ARM IDENTITY / channel state ==")
    cap_hdr = cap.all("xnu_live_cap")
    if cap_hdr:
        vals = sorted(set(t[3] for t in cap_hdr))
        say("OBS", "xnu_live_cap header: %s%s" % (
            ", ".join(fmt(v) for v in vals),
            "  <- 0x6000 = the 999 image; 0x4000 = a PRE-999 image" if vals != [0x6000] else "  (= the 999 image)"))
        if 0x6000 not in vals:
            hard("xnu_live_cap does not carry 0x00006000 - this capture is NOT the 999 arm")
    else:
        say("OBS", "no xnu_live_cap header token (older image or the header itself was lost)")

    rd = cap.all("xnu_live_storage_rd_ordinal")
    if rd:
        say("OBS", "xnu_live_storage_rd_ordinal: %d records, first %s @:%d, last %s @:%d" % (
            len(rd), fmt(rd[0][3]), rd[0][0], fmt(rd[-1][3]), rd[-1][0]))
    else:
        say("OBS", "xnu_live_storage_rd_ordinal: none (the read door never ran?)")
    drv = cap.all("xnu_live_storage_drv_calls")
    dl = cap.last("xnu_live_storage_drv_lba")
    rl = cap.last("xnu_live_storage_rd_lba")
    if drv:
        say("OBS", "xnu_live_storage_drv_calls: %d records; last drv_lba %s; last rd_lba %s" % (
            len(drv), fmt(dl[3]) if dl else "-", fmt(rl[3]) if rl else "-"))


def s1_cap(cap):
    print("\n== [1] THE CAP (pre-reg: PASS = the tail extends past 998's boundary ~:20501) ==")
    cells = cap.all("xnu_live_capped")
    if not cells:
        say("PASS", "no xnu_live_capped key at all - the whole boot fit in the (raised) channel")
        return
    for ln, _k, _r, v in cells:
        say("OBS", "xnu_live_capped=%s @:%d" % (fmt(v), ln))
    if any(t[3] == 0x4000 for t in cells):
        hard("capped==0x00004000 - the cap did NOT move (regression; pre-reg: STOP)")
    elif any(t[3] == 0x6000 for t in cells):
        say("READING", "wall moved (24576) but still hit - nothing past the capped key may be "
                       "read as a stop; the last pre-cap record IS the frontier")
    else:
        say("OBS", "capped value is neither 0x4000 nor 0x6000 - report verbatim")


def s2_exec_done(cap):
    print("\n== [2] EXEC_DONE trio + stash probe (pre-reg: absence AGAIN => rebuild, do not judge) ==")
    trio = [cap.first(k) for k in
            ("xnu_live_exec_done_seq", "xnu_live_exec_done_who", "xnu_live_exec_done_current")]
    probe = [cap.first(k) for k in
             ("xnu_live_exec_done_probe_seq", "xnu_live_exec_done_probe_sig",
              "xnu_live_exec_done_probe_size", "xnu_live_exec_done_probe_records",
              "xnu_live_exec_done_probe_state")]
    for k, t in zip(("seq", "who", "current"), trio):
        say("OBS" if t else "---", "trio _%s: %s" % (
            k, ("%s @:%d" % (fmt(t[3]), t[0])) if t else "ABSENT"))
    if all(t is None for t in trio):
        hard("trio ABSENT again - per pre-reg the exempt-path edit did not reach the image => "
             "rebuild, do not judge")
    for k, t in zip(("seq", "sig", "size", "records", "state"), probe):
        say("OBS" if t else "---", "probe _%s: %s" % (
            k, ("%s @:%d" % (fmt(t[3]), t[0])) if t else "ABSENT"))
    if any(t is not None for t in trio) and all(t is None for t in probe):
        say("OBS", "trio present, probe absent - the NEXT direct write of any kind never happened "
                   "(the carried records/state in _size still localize the channel)")


def s3_wdt(cap):
    print("\n== [3] WDT door feed (pre-reg: 998 had ZERO xnu_live_wdt_*; 999 must have them) ==")
    for k, want in FIXED_999.items():
        t = cap.first(k)
        if t is None:
            say("OBS", "%s: ABSENT" % k)
        elif t[3] == want:
            say("PASS", "%s=%s (frozen value)" % (k, fmt(t[3])))
        else:
            say("FAIL", "%s=%s (frozen value is %s - the 29 s net did not reach the block)" % (
                k, fmt(t[3]), fmt(want)))
            FAILURES.append("%s=%s (frozen %s)" % (k, fmt(t[3]), fmt(want)))
    wdt_any = [t for t in cap.toks if t[1].startswith("xnu_live_wdt_")]
    if not wdt_any:
        hard("ZERO xnu_live_wdt_* keys - the pet never ran (pre-reg: 998 had zero and 999 must have them)")
        return
    mp = cap.first("xnu_live_wdt_map")
    say("OBS", "install: map=%s slot_before=%s desc=%s via=%s" % (
        fmt(mp[3]) if mp else "-", fmt((cap.first("xnu_live_wdt_slot_before") or (0, 0, 0, None))[3]),
        fmt((cap.first("xnu_live_wdt_desc") or (0, 0, 0, None))[3]),
        fmt((cap.first("xnu_live_wdt_via") or (0, 0, 0, None))[3])))
    bark = cap.first("xnu_live_wdt_bark")
    if bark is not None:
        if bark[3] == 0xE7FA9:
            say("PASS", "xnu_live_wdt_bark=%s (frozen observable; 25 s was 0xC7FB5)" % fmt(bark[3]))
        else:
            say("OBS", "xnu_live_wdt_bark=%s (frozen observable is 0x000e7fa9)" % fmt(bark[3]))
    if mp is not None and mp[3] == 0:
        desc = cap.first("xnu_live_wdt_desc")
        d = desc[3] if desc else None
        plausible = d is not None and d >= 0xF9000000 and d < 0xFA000000 and (d & 0xFFFFF) == 0
        if plausible:
            say("OBS", "map=0 but desc %s IS a 1 MB block of PA 0xf9000000 - the GIC's cell "
                       "(fallback path), install refused" % fmt(d))
        else:
            hard("FALSIFICATION: map=0 and desc %s is not a 1 MB block of PA 0xf9000000 - "
                 "install refused, run dies at the deadline" % fmt(d))
    for k in ("sts", "countdown", "pets"):
        vals = [t[3] for t in cap.all("xnu_live_wdt_" + k)]
        if vals:
            say("OBS", "xnu_live_wdt_%s (%d): %s" % (k, len(vals),
                " ".join(fmt(v) for v in vals[:10]) + (" ..." if len(vals) > 10 else "")))
    pairs = cap.ordered_pairs("xnu_live_storage_rd_ordinal", "xnu_live_wdt_pet_calls")
    if not pairs:
        say("OBS", "xnu_live_wdt_pet_calls: none (no pet fired - countdown never passed half bark?)")
        return
    say("OBS", "xnu_live_wdt_pet_calls, evaluated against the FROZEN sentence (power of two AND <= "
               "last preceding xnu_live_storage_rd_ordinal):")
    pow_ok = le_ok = 0
    for v, last_rd in pairs:
        p, l = is_pow2(v), (v is not None and last_rd is not None and v <= last_rd)
        pow_ok += p
        le_ok += l
        say("OBS", "  pet_calls=%s  pow2=%s  <=last-rd(%s)=%s" % (
            fmt(v), "yes" if p else "no", fmt(last_rd), "yes" if l else "no"))
    say("OBS", "tally: power-of-two %d/%d, <=last-rd %d/%d - report any miss as a pre-reg "
               "deviation at adjudication (the frozen sentence is the check)" % (
                   pow_ok, len(pairs), le_ok, len(pairs)))


def s4_slide(cap, is_999):
    print("\n== [4] SLIDE-HALF LADDER (none => map tail; +a => slide_init; +b => copyin; +c => sanity/EFAULT) ==")
    if not is_999:
        say("NOTE", "image is not bound to the 999 switch set - ladder READING text below is "
                    "suppressed; the literal counts still print for the 998-baseline comparison")
    a = cap.lit_count("IOS7SHREG slide_enter")
    b = cap.lit_count("IOS7SHREG slide_init")
    c = cap.lit_count("IOS7SHREG slide_copyin")
    for lit, why in LADDER_LITERALS[:3]:
        e = cap.lit.get(lit)
        say("OBS" if e else "---", "%s: %d%s" % (lit, e[0] if e else 0,
            ("  @:%d  %s" % (e[1], e[3])) if e else "  (%s)" % why))
    ret = [int(m.group(1)) for m in re.finditer(r'IOS7SHREG map_slide ret=(-?\d+)', cap.text)]
    mf = [int(m.group(1)) for m in re.finditer(r'IOS7SHREG map_file kr=(-?\d+)', cap.text)]
    rd = (lambda m: say("READING", m)) if is_999 else (lambda m: say("OBS", "(reading suppressed) " + m))
    if a == 0 and b == 0 and c == 0:
        rd("no ladder print - stop in the map tail (atime RULED OUT by 11: hfs_update "
           "hfs_vnops.c:1352 is the only catalog-write call)")
    elif a > 0 and b == 0 and c == 0:
        rd("slide_enter only - stop inside slide_init (obj lock vm_shared_region.c:1458 "
           "/ kmem_alloc :1502)")
    elif a > 0 and b > 0 and c == 0:
        rd("slide_init printed, no copyin print - stop inside the copyin or its page "
           "delivery")
    elif c > 0:
        last_kr = None
        for m in re.finditer(r'IOS7SHREG slide_copyin kr=(-?\d+)', cap.text):
            last_kr = int(m.group(1))
        if last_kr == 0:
            rd("slide_copyin kr=0 printed - stop in sanity :1541 (its SUCCESS prints "
               "NOTHING, DEBUG=0); the absent `map_slide ret=` is the locator")
        else:
            rd("slide_copyin kr!=0 (%s) - real EFAULT; map_slide ret=14 must follow" %
               last_kr)
    else:
        say("OBS", "inconsistent ladder shape a=%d b=%d c=%d - report verbatim" % (a, b, c))
    if ret:
        say("OBS", "map_slide ret= (%d): values %s - the slide stage COMPLETED; the hang moved "
                   "into dyld's load/link path" % (len(ret), ret))
    else:
        say("OBS", "no map_slide ret= - consistent with a stop inside the 438 call")
    for kr in mf:
        if kr != 0:
            hard("map_file kr=%d - GEOMETRY REGRESSION (G1 held kr=0); stop and re-model" % kr)
    say("OBS", "map_file kr= values: %s" % (mf if mf else "none"))
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
    hits = []
    for key in ("xnu_live_storage_rd_lba", "xnu_live_storage_drv_lba"):
        for ln, _k, _r, v in cap.all(key):
            if v is not None and RD_LBA_LO <= v <= RD_LBA_HI:
                hits.append((key, ln, v))
    say("OBS", "LBAs inside the copyin range [0x%x,0x%x]: %s" % (RD_LBA_LO, RD_LBA_HI,
        ", ".join("0x%x@:%d" % (h[2], h[1]) for h in hits) if hits else
        "none - (cap+thinning prediction) copyin not reached before the stop"))
    for key in ("xnu_live_storage_rd_lba", "xnu_live_storage_drv_lba"):
        lastv = cap.last(key)
        if lastv:
            say("OBS", "last %s=%s @:%d" % (key, fmt(lastv[3]), lastv[0]))


def s5_timer(cap, is_999):
    print("\n== [5] TIMER EDGE (no record => B; record + no tick_irq>=0x80 => A; then 0x80 => end) ==")
    if not is_999:
        say("NOTE", "image is not bound to the 999 switch set - the READING text below is suppressed")
    sites = {}
    for site in ("mlie", "texr"):
        seqs = cap.all("xnu_live_edge_%s_seq" % site)
        cpsr = cap.all("xnu_live_edge_%s_cpsr" % site)
        tick = cap.all("xnu_live_edge_%s_tick" % site)
        sites[site] = (seqs, cpsr, tick)
        if not seqs:
            say("---", "edge_%s: no record" % site)
            continue
        for i, s in enumerate(seqs):
            cv = fmt(cpsr[i][3]) if i < len(cpsr) else "?"
            tv = tick[i][3] if i < len(tick) else None
            say("OBS", "edge_%s seq=%s cpsr=%s tick=%s%s" % (
                site, fmt(s[3]), cv, fmt(tv), "" if (tv or 0) >= 64 else "  <- BELOW the tick>=64 gate"))
        if len(seqs) > 4:
            hard("edge_%s: %d records > 4 (first-4-per-site bound violated)" % (site, len(seqs)))
    total = sum(len(v[0]) for v in sites.values())
    if total > 8:
        hard("edge events total %d > 8" % total)
    tq = [t[3] for t in cap.all("xnu_live_tick_irq")]
    if tq:
        say("OBS", "tick_irq ladder: %s" % " ".join(fmt(v) for v in tq))
    saw128 = any(v is not None and v >= 0x80 for v in tq)
    any_edge = total > 0
    rd = (lambda m: say("READING", m)) if is_999 else (lambda m: say("OBS", "(reading suppressed) " + m))
    if not any_edge:
        rd("B: no edge record - no unmask ever ran; the handoff's I=1 never cleared")
    elif not saw128:
        rd("A-favored: edge record(s) present and NO tick_irq>=0x80 - delivery hardware "
           "(state the tick values above)")
    else:
        rd("the freeze ENDED at an edge: tick_irq=0x80 present after an edge record")


def s6_guards(cap):
    print("\n== CONSTANTS/GUARDS + REGRESSION echo ==")
    for lit, want in (("VMHINT991", 2), ("vm_allocate64", 0)):
        n = cap.lit_count(lit)
        if n == want:
            say("PASS", "%s x%d (expected %d)" % (lit, n, want))
        else:
            say("OBS", "%s x%d (expected %d)" % (lit, n, want))
    tot = cap.all("xnu_live_ostext_total")
    if tot:
        say("OBS", "xnu_live_ostext_total last = %s (998 ended 0x17ee)" % fmt(tot[-1][3]))
    lim = cap.lit_count("xnu_live_ostext_limited")
    if lim:
        say("OBS", "xnu_live_ostext_limited x%d (998: 0)" % lim)


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[0])
        print("usage: %s <capture.txt>" % sys.argv[0])
        return 2
    cap = Capture(sys.argv[1])
    print("999 capture adjudicator - %s (%d bytes)" % (sys.argv[1], cap.nbytes))
    print("authority: the frozen 999 PRESS PRE-REGISTRATION block in scripts/press_999.sh")
    is_999 = any(t[3] == 0x6000 for t in cap.all("xnu_live_cap"))
    s_arm(cap)
    s1_cap(cap)
    s2_exec_done(cap)
    s3_wdt(cap)
    s4_slide(cap, is_999)
    s5_timer(cap, is_999)
    s6_guards(cap)
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