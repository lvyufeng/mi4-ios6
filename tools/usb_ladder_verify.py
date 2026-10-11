#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
usb_ladder_verify.py - turn a USB-capture + RAM-console capture into a per-rung verdict for the
D13 USB-debug ladder (experiments 959 probe / 960 device / 961 enum / 962 stream).

WHY THIS EXISTS
---------------
The ladder fires from `__wrap_machine_idle` (`src/entry/entry_trace.c:2235`), the D13 seam of
experiment 951. Its arms are PARKED and, on D13, the ladder has never been observed to enumerate or
stream even once: the 977 press died at `lock_set_init` (experiment 979), i.e. BEFORE `machine_idle`.
`tools/check_usb_host_reader.py` only proves the *reader's* EP/VID/PID match `entry_usb_enum.h`; it
says NOTHING about the payload actually transmitting.

This tool reads OBSERVABLE evidence and returns, per rung, one of:

    fired          the payload wrote the rung's own live keys with the values only a real
                   transition produces
    did-not-fire   the payload reached the rung and the value proves it did NOT advance
    indeterminate  the payload never reached the rung's site (the run died upstream) - a veto on
                   reading silence as absence, per `mi4-silence-is-a-reading-only-if-success-is-silent`

And it REFUSES to call a rung verified from the arm RECORD alone: the arm record is a switch set,
not an observation of the wire. `--arm-record` is accepted only so the refusal is explicit and
testable.

INPUTS
  - a RAM-console capture (the ` xnu_live_...=0x........` key/value text - what the 962 stream
    carries over EP1-IN; also what a non-resident run leaves in /proc/last_kmsg), and/or
  - the reader's own exit code (`--reader-rc`): 0 = bytes seen, 2 = device never seen,
    3 = device claimed but EP1-IN silent (`tools/usb_console_read.py`).

USAGE
  tools/usb_ladder_verify.py CAPTURE.txt [--reader-rc N] [--json]
  tools/usb_ladder_verify.py --selftest          # synthetic captures; proves each branch bites
"""

import argparse
import json
import os
import re
import sys
import tempfile

# --- the ladder's own observable keys, read by value from src/entry/*.c ------------------------
# Every one of these is a `entry_live_write("xnu_live_usb...", v)` in the rung's source. They are
# NOT the arm switch (`STAGE90_XNU_USB_PROBE` etc.) - they are what the payload wrote at run time.

RUNG_ORDER = ["probe", "device", "enum", "stream"]

# rung -> (human name, source file that owns the keys, the key that proves the switch was linked)
RUNG_SRC = {
    "probe":  ("959", "src/entry/entry_usb.c"),
    "device": ("960", "src/entry/entry_usb_dev.c"),
    "enum":   ("961", "src/entry/entry_usb_enum.c"),
    "stream": ("962", "src/entry/entry_usb_stream.c"),
}

# The one switch each rung's write is gated on (`src/entry/entry_trace.c:2303-2329`).
RUNG_SWITCH = {
    "probe":  "STAGE90_XNU_USB_PROBE",
    "device": "STAGE90_XNU_USB_DEV",
    "enum":   "STAGE90_XNU_USB_ENUM",
    "stream": "STAGE90_XNU_USB_STREAM",
}

# Grounding for the stream's byte floor: one full FS bulk packet on EP1-IN. From
# `entry_usb_enum.h:186` `STAGE90_USB_ENUM_EP_IN_MAXPKT = 64` (FS bulk; the vendor's 512 is HS,
# not reachable). So >= 64 captured bytes == at least one whole max-size packet the host accepted.
STREAM_MIN_BYTES = 64

# The KV record grammar, from `entry_write_kv` (`src/entry/entry_stubs.c:2526`):
#   ' ' <key> '=' '0' 'x' <8 hex> '\n'
KV_RE = re.compile(rb"([A-Za-z0-9_.]+)=0x([0-9a-fA-F]{8})")

# The VID:PID the device MUST appear at, from `entry_usb_enum.h:174-175`.
VID = 0x18D1
PID = 0x0910


def parse_kv(raw):
    """Return {key: max(observed value)} plus {key: count}. Last/max wins: the live keys are either
    monotonic counters or one-shot latches set to 1, so the max over all occurrences is the living
    value in both cases."""
    vals, counts = {}, {}
    for m in KV_RE.finditer(raw):
        k = m.group(1).decode("ascii", "replace")
        v = int(m.group(2), 16)
        counts[k] = counts.get(k, 0) + 1
        if k not in vals or v > vals[k]:
            vals[k] = v
    return vals, counts


def any_usb_keys(vals):
    return any(k.startswith("xnu_live_usb") for k in vals)


def verdict(rung, vals, host_bytes, reader_rc):
    """Return (verdict, reason). vals carries max-observed; host_bytes is the size of the EP1-IN
    capture attributed to this device (0 if the reader saw nothing)."""
    def has(k):
        return k in vals

    def val(k):
        return vals.get(k, None)

    if rung == "probe":
        # 959: entry_usb.c. `mapped` is the one branch that is a reading in itself.
        if has("xnu_live_usb_mapped") and val("xnu_live_usb_mapped") == 0:
            return "did-not-fire", "xnu_live_usb_mapped=0x0 - entry_mmio_section did not install the " \
                                   "section, so no USB register was read (entry_usb.c:160-166)"
        if val("xnu_live_usb_mapped") == 1 and has("xnu_live_usb_id_cap") and has("xnu_live_usb_hciversion"):
            return "fired", "mapping installed and the core was identified " \
                            "(id_cap=0x%08x hciversion=0x%08x)" % (val("xnu_live_usb_id_cap"),
                                                                  val("xnu_live_usb_hciversion"))
        if has("xnu_live_usb_map") or has("xnu_live_usb_base"):
            return "did-not-fire", "the probe entered (emitted mapping keys) but no identification " \
                                   "cells followed - the read did not complete"
        return "indeterminate", "no xnu_live_usb_* key at all - __wrap_machine_idle's probe site was " \
                                "never reached (the run died upstream; 979's fault is before machine_idle)"

    if rung == "device":
        up, _ = verdict("probe", vals, 0, reader_rc)
        if up != "fired":
            return "indeterminate", "probe did not fire, so the device rung's site was not reached"
        if val("xnu_live_usb_dev_proceed") == 0:
            return "did-not-fire", "xnu_live_usb_dev_proceed=0x0 - the core was not in device mode " \
                                   "and FORCE was off, so the reset sequence was skipped " \
                                   "(entry_usb_dev.c:311-314)"
        if val("xnu_live_usb_dev_devmode_ok") == 1 and val("xnu_live_usb_dev_loaded") == 1:
            return "fired", "USBMODE read back CM_DEVICE and the arm loaded " \
                            "(usbmode_after=0x%08x)" % (val("xnu_live_usb_dev_usbmode_after") or 0)
        if val("xnu_live_usb_dev_loaded") == 0 or val("xnu_live_usb_dev_devmode_ok") == 0:
            return "did-not-fire", "the device arm ran but USBMODE did not read back CM_DEVICE " \
                                   "(devmode_ok=%s)" % val("xnu_live_usb_dev_devmode_ok")
        return "did-not-fire", "probe fired but no xnu_live_usb_dev_* keys - STAGE90_XNU_USB_DEV was 0"

    if rung == "enum":
        up, _ = verdict("device", vals, 0, reader_rc)
        if up != "fired":
            return "indeterminate", "device rung did not fire, so enumeration's site was not reached"
        if val("xnu_live_usb_enum_gated") == 1:
            return "did-not-fire", "xnu_live_usb_enum_gated=0x1 - the core was not CM_DEVICE and " \
                                   "FORCE was off, so EP0 was never armed (entry_usb_enum.c:538-544)"
        if val("xnu_live_usb_enum_armed") == 1 and (val("xnu_live_usb_enum_requests") or 0) > 0:
            return "fired", "EP0 armed and the host drove %d control request(s); " \
                            "addr=0x%x configured=%d" % (val("xnu_live_usb_enum_requests"),
                                                         val("xnu_live_usb_enum_addr") or 0,
                                                         val("xnu_live_usb_enum_configured") or 0)
        if val("xnu_live_usb_enum_armed") == 1:
            return "did-not-fire", "EP0 was armed but the host sent 0 requests " \
                                   "(xnu_live_usb_enum_requests=0) - no enumeration on the wire"
        return "did-not-fire", "device fired but no xnu_live_usb_enum_* keys - STAGE90_XNU_USB_ENUM was 0"

    if rung == "stream":
        up, _ = verdict("enum", vals, 0, reader_rc)
        if up != "fired":
            return "indeterminate", "enum rung did not fire, so the stream's site was not reached"
        if val("xnu_live_usb_stream_waiting") == 1 and (val("xnu_live_usb_stream_armed") or 0) == 0:
            return "did-not-fire", "xnu_live_usb_stream_waiting=0x1 - EP1-IN was never ENABLED by the " \
                                   "enum arm, so the stream has no endpoint to prime " \
                                   "(entry_usb_stream.c:235-242)"
        if val("xnu_live_usb_stream_armed") == 1 and (val("xnu_live_usb_stream_chunks") or 0) > 0:
            if host_bytes >= STREAM_MIN_BYTES:
                return "fired", "EP1-IN armed, %d chunk(s) completed, and the host captured %d bytes " \
                                "(>= %d = one full FS bulk packet)" % (val("xnu_live_usb_stream_chunks"),
                                                                       host_bytes, STREAM_MIN_BYTES)
            if host_bytes == 0 and reader_rc == 3:
                return "did-not-fire", "the payload completed %d chunk(s) but the host captured 0 " \
                                       "bytes (reader rc=3: claimed but silent) - the DMA completed " \
                                       "into the wrong place" % val("xnu_live_usb_stream_chunks")
            if host_bytes == 0 and reader_rc == 2:
                return "did-not-fire", "the payload streamed %d chunk(s) but the host NEVER SAW " \
                                       "%04x:%04x (reader rc=2)" % (val("xnu_live_usb_stream_chunks"),
                                                                    VID, PID)
            return "did-not-fire", "the payload streamed %d chunk(s) but only %d host byte(s) arrived " \
                                   "(< %d)" % (val("xnu_live_usb_stream_chunks"), host_bytes, STREAM_MIN_BYTES)
        if val("xnu_live_usb_stream_armed") == 1:
            return "did-not-fire", "xnu_live_usb_stream_armed=0x1 but 0 chunks completed " \
                                   "(xnu_live_usb_stream_chunks=0) - the source never produced"
        return "did-not-fire", "enum fired but no xnu_live_usb_stream_* keys - STAGE90_XNU_USB_STREAM was 0"

    raise AssertionError(rung)


def analyze(raw, reader_rc=None, host_bytes=None):
    vals, counts = parse_kv(raw)
    # The host byte count is an OBSERVATION of the wire, not the size of whatever file we were handed.
    # A `--reader-rc 0` capture IS the EP1-IN byte stream, so its size is that observation. A
    # /proc/last_kmsg (or any payload console) is NOT - its size says nothing about what the host
    # received, so it must never be allowed to make the stream read as fired from payload keys alone.
    # Only the DERIVED path is gated on rc=0; an explicit --host-bytes is the caller's own observation.
    if host_bytes is None:
        host_bytes = len(raw) if reader_rc == 0 else 0
    out = {"reader_rc": reader_rc, "host_bytes": host_bytes,
           "kv_keys": len(vals), "usb_keys_seen": sorted(k for k in vals if k.startswith("xnu_live_usb"))}
    rungs = {}
    for r in RUNG_ORDER:
        v, why = verdict(r, vals, host_bytes, reader_rc)
        rungs[r] = {"experiment": RUNG_SRC[r][0], "switch": RUNG_SWITCH[r],
                    "source": RUNG_SRC[r][1], "verdict": v, "reason": why}
    out["rungs"] = rungs
    out["overall"] = _overall(rungs)
    return out


def _overall(rungs):
    """The ladder's overall state: the top rung that FIRED, and whether the next one is blocked by
    a did-not-fire or merely unreached."""
    fired = [r for r in RUNG_ORDER if rungs[r]["verdict"] == "fired"]
    if not fired:
        if rungs["probe"]["verdict"] == "indeterminate":
            return "not-reached"
        return "blocked-at-%s" % RUNG_ORDER[0]
    top = fired[-1]
    if len(fired) == len(RUNG_ORDER):
        return "top-fired=%s" % top
    nxt = RUNG_ORDER[len(fired)]
    if rungs[nxt]["verdict"] == "indeterminate":
        return "reached-%s" % top
    return "top-fired=%s" % top


# --- the refusal -------------------------------------------------------------------------------

class ArmRecordRefused(SystemExit):
    pass


def refuse_arm_record(path):
    """The whole point of M1: an arm RECORD is a switch set, not a wire observation. Reading it is
    exactly the error that let 962 read as 'built' while never once enumerating. Refuse, loudly."""
    sys.stderr.write(
        "usb_ladder_verify: REFUSING to derive a verdict from the arm record %s.\n"
        "  An arm record carries the *switches* (%s=1 ...), not evidence that the payload\n"
        "  transmitted. Use it only to confirm WHICH ladder was armed; the verdict needs a capture\n"
        "  from tools/usb_console_read.py (or /proc/last_kmsg).\n" % (path, "STAGE90_XNU_USB_*"))
    raise ArmRecordRefused(4)


# --- selftest: synthetic captures that prove each branch bites ---------------------------------

def _kv(key, v):
    return b" %s=0x%08x\n" % (key.encode(), v)


def _selftest():
    ok = True

    def check(name, cond, extra=""):
        nonlocal ok
        mark = "ok  " if cond else "FAIL"
        if not cond:
            ok = False
        sys.stdout.write("  [%s] %s%s\n" % (mark, name, (" - " + extra) if extra else ""))

    sys.stdout.write("usb_ladder_verify --selftest (synthetic captures)\n")

    # 1. everything fired, host captured a full packet
    cap = b"".join([
        _kv("xnu_live_usb_mapped", 1), _kv("xnu_live_usb_id_cap", 0x10002000),
        _kv("xnu_live_usb_hciversion", 0x0004),
        _kv("xnu_live_usb_dev_proceed", 1), _kv("xnu_live_usb_dev_devmode_ok", 1),
        _kv("xnu_live_usb_dev_usbmode_after", 0x3), _kv("xnu_live_usb_dev_loaded", 1),
        _kv("xnu_live_usb_enum_armed", 1), _kv("xnu_live_usb_enum_requests", 7),
        _kv("xnu_live_usb_enum_addr", 5), _kv("xnu_live_usb_enum_configured", 1),
        _kv("xnu_live_usb_stream_armed", 1), _kv("xnu_live_usb_stream_chunks", 3),
        _kv("xnu_live_usb_stream_state", 2),
    ] + [b"x" * 200])
    r = analyze(cap, reader_rc=0, host_bytes=200)
    check("all-four-fired", all(r["rungs"][k]["verdict"] == "fired" for k in RUNG_ORDER),
          r["overall"])
    check("overall top-fired=stream", r["overall"] == "top-fired=stream", r["overall"])

    # 2. empty capture -> indeterminate (NOT 'did-not-fire'): the run died upstream.
    r = analyze(b"", reader_rc=2, host_bytes=0)
    check("empty-capture-probe-indeterminate", r["rungs"]["probe"]["verdict"] == "indeterminate",
          r["rungs"]["probe"]["reason"])
    check("empty-capture-not-verified", r["overall"] == "not-reached", r["overall"])

    # 3. the 979 shape: keys exist, but no usb keys at all because the site was never reached.
    r = analyze(_kv("xnu_entry_args_memSize", 0x1E400000) + _kv("Darwin Kernel Version", 0x0D0000),
                reader_rc=2, host_bytes=0)
    check("non-usb-console-probe-indeterminate", r["rungs"]["probe"]["verdict"] == "indeterminate")

    # 4. mapping failed -> did-not-fire with the specific reason.
    r = analyze(_kv("xnu_live_usb_map", 0) + _kv("xnu_live_usb_mapped", 0), reader_rc=2)
    check("mapped-0-did-not-fire", r["rungs"]["probe"]["verdict"] == "did-not-fire" and
          "entry_mmio_section" in r["rungs"]["probe"]["reason"])
    check("probe-gate-blocks-device", r["rungs"]["device"]["verdict"] == "indeterminate")

    # 5. device gated -> did-not-fire, enum/stream indeterminate.
    cap5 = b"".join([_kv("xnu_live_usb_mapped", 1), _kv("xnu_live_usb_id_cap", 1),
                     _kv("xnu_live_usb_hciversion", 0x0004), _kv("xnu_live_usb_dev_proceed", 0)])
    r = analyze(cap5, reader_rc=2)
    check("dev-gated-did-not-fire", r["rungs"]["device"]["verdict"] == "did-not-fire" and
          "device mode" in r["rungs"]["device"]["reason"])
    check("dev-gate-blocks-enum", r["rungs"]["enum"]["verdict"] == "indeterminate")

    # 6. armed but the host never drove enumeration -> did-not-fire at enum.
    cap6 = b"".join([_kv("xnu_live_usb_mapped", 1), _kv("xnu_live_usb_id_cap", 1),
                     _kv("xnu_live_usb_hciversion", 4), _kv("xnu_live_usb_dev_proceed", 1),
                     _kv("xnu_live_usb_dev_devmode_ok", 1), _kv("xnu_live_usb_dev_loaded", 1),
                     _kv("xnu_live_usb_enum_armed", 1), _kv("xnu_live_usb_enum_requests", 0)])
    r = analyze(cap6, reader_rc=2)
    check("enum-armed-0-requests-did-not-fire", r["rungs"]["enum"]["verdict"] == "did-not-fire" and
          "0 requests" in r["rungs"]["enum"]["reason"])

    # 7. enum armed + requests, but EP1-IN never enabled -> stream waiting.
    cap7 = cap6 + b"".join([_kv("xnu_live_usb_enum_requests", 7),
                                 _kv("xnu_live_usb_stream_waiting", 1)])
    r = analyze(cap7, reader_rc=3)
    check("stream-waiting-did-not-fire", r["rungs"]["stream"]["verdict"] == "did-not-fire" and
          "ENABLED" in r["rungs"]["stream"]["reason"])

    # 8. armed + chunks>0 but the host captured nothing -> did-not-fire (rc=3 reading).
    cap8 = cap7 + b"".join([_kv("xnu_live_usb_stream_armed", 1), _kv("xnu_live_usb_stream_chunks", 4)])
    r = analyze(cap8, reader_rc=3, host_bytes=0)
    check("stream-chunks-but-silent-did-not-fire", r["rungs"]["stream"]["verdict"] == "did-not-fire" and
          "wrong place" in r["rungs"]["stream"]["reason"])

    # 9. chunks>0 and host got 200 bytes -> fired (even though it is the same payload record as #8:
    #    the verdict changes ONLY with the host observation).
    r = analyze(cap8, reader_rc=0, host_bytes=200)
    check("stream-chunks-and-host-bytes-fired", r["rungs"]["stream"]["verdict"] == "fired")

    # 10. chunks>0 but only 10 host bytes (< 64) -> did-not-fire.
    r = analyze(cap8, reader_rc=0, host_bytes=10)
    check("stream-short-capture-did-not-fire", r["rungs"]["stream"]["verdict"] == "did-not-fire" and
          "< 64" in r["rungs"]["stream"]["reason"])

    # 11. THE DERIVED-PATH GUARD: an armed+chunks payload passed as a /proc/last_kmsg (rc!=0) with no
    #     explicit byte count must yield host_bytes=0 and NOT fire the stream, however big the file is.
    r = analyze(cap8, reader_rc=2)
    check("derived-host-bytes-gated-on-rc0",
          r["host_bytes"] == 0 and r["rungs"]["stream"]["verdict"] == "did-not-fire",
          "host_bytes=%d" % r["host_bytes"])
    r = analyze(cap8, reader_rc=0)   # same file, but rc=0 => it IS the stream: size is the observation
    check("derived-host-bytes-honoured-on-rc0", r["host_bytes"] == len(cap8))

    # 12. THE REFUSAL: an arm record alone must not yield a verdict.
    d = tempfile.mkdtemp(prefix="usb_ladder_selftest_")
    rec = os.path.join(d, "armed-x.txt")
    with open(rec, "w") as f:
        f.write("STAGE90_XNU_USB_PROBE=1\nSTAGE90_XNU_USB_DEV=1\n"
                "STAGE90_XNU_USB_ENUM=1\nSTAGE90_XNU_USB_STREAM=1\n")
    try:
        refuse_arm_record(rec)
        check("arm-record-refused", False, "it did NOT refuse")
    except SystemExit as e:
        check("arm-record-refused", e.code == 4, "exit=%s" % e.code)

    sys.stdout.write("usb_ladder_verify: %s\n" % ("ALL OK" if ok else "FAILURES ABOVE"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description="Per-rung verdict for the D13 USB ladder (959-962).")
    ap.add_argument("capture", nargs="?", help="RAM-console / EP1-IN capture file (raw KV text)")
    ap.add_argument("--reader-rc", type=int, default=None,
                    help="exit code of tools/usb_console_read.py (0=bytes,2=never seen,3=silent)")
    ap.add_argument("--host-bytes", type=int, default=None,
                    help="bytes the reader captured (default: derived from the capture file size)")
    ap.add_argument("--arm-record", default=None,
                    help="an arm record; accepted ONLY to be refused as a verdict source")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()

    if a.selftest:
        return _selftest()

    if a.arm_record:
        refuse_arm_record(a.arm_record)

    if not a.capture:
        ap.error("need a CAPTURE file (or --selftest). An arm record is not evidence.")

    if a.capture == "-":
        raw = sys.stdin.buffer.read()
    else:
        with open(a.capture, "rb") as f:
            raw = f.read()

    res = analyze(raw, reader_rc=a.reader_rc, host_bytes=a.host_bytes)

    if a.json:
        json.dump(res, sys.stdout, indent=2, sort_keys=True)
        sys.stdout.write("\n")
        return 0 if res["overall"].startswith("top-fired") else 1

    print("usb_ladder_verify: %s" % a.capture)
    print("  reader_rc=%s host_bytes=%d usb_keys=%d" % (res["reader_rc"], res["host_bytes"],
                                                        len(res["usb_keys_seen"])))
    for r in RUNG_ORDER:
        d = res["rungs"][r]
        print("  %-7s (%s, %-24s): %-13s %s" % (r, d["experiment"], d["switch"], d["verdict"],
                                                d["reason"]))
    print("  overall: %s" % res["overall"])
    return 0 if res["overall"].startswith("top-fired") else 1


if __name__ == "__main__":
    sys.exit(main())