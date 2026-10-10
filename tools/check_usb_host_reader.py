#!/usr/bin/env python3
"""check_usb_host_reader.py - the host reader's protocol constants must follow the DEVICE's.

`tools/usb_console_read.py` is the host half of the D13 USB ladder (959-962): it opens EP1-IN of
VID:PID `0x18d1:0x0910` and drains the RAM console the ladder streams. Those three numbers are a
**protocol contract with `src/entry/entry_usb_enum.h`** - the device's own source of truth. If the
device ever changes its VID, PID, or the endpoint address, a reader with hard-coded literals drifts
**silently**: it would open the wrong device or read the wrong endpoint and go dark, exactly the
"one value, two definitions" class ([[mi4-one-value-two-definitions]]).

This check derives the three values from the device headers (reading `entry_usb_enum.h` for
`ID_VENDOR`, `ID_PRODUCT`, `DIR_IN`, `EP_IN`, and computing EP1-IN as `DIR_IN | EP_IN` - the header's
own `EP_IN_EPADDR` expression) and refuses if `usb_console_read.py` does not carry the matching
value. Every refusal is a build/check stop, not a comment.

`--selftest` mutates one of the three in a temp copy and asserts the check refuses, so the check is
proven to bite rather than merely to pass.
"""
import argparse
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HDR = os.path.join(REPO, "src", "entry", "entry_usb_enum.h")
READER = os.path.join(REPO, "tools", "usb_console_read.py")


def hdr_defines(text):
    """Map of #define NAME -> int value, for the USB enum header's self-contained integer defines."""
    out = {}
    pat = re.compile(r'#define\s+(STAGE90_USB_ENUM_[A-Z0-9_]+)\s+(0x[0-9a-fA-F]+|[0-9]+)u?')
    for m in pat.finditer(text):
        out[m.group(1)] = int(m.group(2), 0)
    return out


def expected_vals(hdr_text):
    d = hdr_defines(hdr_text)

    def need(name):
        if name not in d:
            raise SystemExit("check_usb_host_reader: %s defines no %s; the check has drifted from "
                             "the device header" % (os.path.relpath(HDR, REPO), name))
        return d[name]

    vid = need("STAGE90_USB_ENUM_ID_VENDOR")
    pid = need("STAGE90_USB_ENUM_ID_PRODUCT")
    ep = need("STAGE90_USB_ENUM_DIR_IN") | need("STAGE90_USB_ENUM_EP_IN")  # the header's EP_IN_EPADDR
    return vid, pid, ep


def reader_vals(reader_text):
    """The three literals the reader carries: EP_IN, --vid default, --pid default."""
    ep = re.search(r'^EP_IN\s*=\s*(0x[0-9a-fA-F]+|\d+)', reader_text, re.M)
    vid = re.search(r'"--vid".*?default\s*=\s*(0x[0-9a-fA-F]+|\d+)', reader_text)
    pid = re.search(r'"--pid".*?default\s*=\s*(0x[0-9a-fA-F]+|\d+)', reader_text)
    missing = [n for n, m in (("EP_IN", ep), ("--vid", vid), ("--pid", pid)) if m is None]
    if missing:
        raise SystemExit("check_usb_host_reader: usb_console_read.py does not carry %s in the form "
                         "this check reads" % ", ".join(missing))
    return int(ep.group(1), 0), int(vid.group(1), 0), int(pid.group(1), 0)


def run(hdr_text, reader_text, quiet=False):
    evid, epid, eep = expected_vals(hdr_text)
    reep, revid, repid = reader_vals(reader_text)
    bad = []
    if reep != eep:
        bad.append("EP_IN: reader 0x%02x != device EP1-IN 0x%02x (DIR_IN|EP_IN)" % (reep, eep))
    if revid != evid:
        bad.append("--vid: reader 0x%04x != device ID_VENDOR 0x%04x" % (revid, evid))
    if repid != epid:
        bad.append("--pid: reader 0x%04x != device ID_PRODUCT 0x%04x" % (repid, epid))
    if bad:
        for b in bad:
            sys.stderr.write("check_usb_host_reader: %s\n" % b)
        sys.stderr.write("  the host reader tools/usb_console_read.py has drifted from the device's "
                         "own constants (src/entry/entry_usb_enum.h); it would open the wrong "
                         "device/endpoint and go dark.\n")
        return 1
    if not quiet:
        print("check_usb_host_reader: ok - usb_console_read.py opens EP1-IN 0x%02x of %04x:%04x, "
              "matching entry_usb_enum.h (ID_VENDOR/ID_PRODUCT/DIR_IN|EP_IN)." % (reep, evid, epid))
    return 0


def selftest():
    hdr = open(HDR).read()
    rd = open(READER).read()
    if run(hdr, rd, quiet=True) != 0:
        sys.stderr.write("check_usb_host_reader --selftest: the clean tree is REFUSED; "
                         "the check is wrong, not the reader\n")
        return 1
    # Each mutation must be refused.
    muts = {
        "EP_IN": (re.sub(r'^EP_IN\s*=\s*0x81', 'EP_IN = 0x82', rd, count=1, flags=re.M), "EP_IN"),
        "--vid": (rd.replace("default=0x18d1", "default=0x05ac", 1), "--vid"),
        "--pid": (rd.replace("default=0x0910", "default=0x0911", 1), "--pid"),
    }
    for name, (mut, what) in muts.items():
        if mut == rd:
            sys.stderr.write("check_usb_host_reader --selftest: could not mutate %s (the reader's "
                             "literal form moved)\n" % name)
            return 1
        if run(hdr, mut, quiet=True) == 0:
            sys.stderr.write("check_usb_host_reader --selftest: a reader with %s changed was "
                             "ACCEPTED - the check does not bite\n" % what)
            return 1
    print("check_usb_host_reader: selftest ok - the clean reader passes and each of EP_IN/--vid/"
          "--pid mutated is refused")
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    return run(open(HDR).read(), open(READER).read())


if __name__ == "__main__":
    sys.exit(main())