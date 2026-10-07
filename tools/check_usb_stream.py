#!/usr/bin/env python3
"""910c's guard: the bulk stream arm's transcribed values, its SOURCE-OWNER cross-checks, the two-way
handover (who owns EP1-IN), the no-ENDPTCTRL refusal, and whether the switch reached the linked image.

910c is the first arm whose payload comes from ANOTHER part of this image (the RAM console ring the live
channel publishes into), so its guard has to answer a question the three earlier USB guards did not:
**does the source's layout here recompute the owner's, or has it drifted?** The console ring is
`{ u32 sig; u32 start; u32 size; u8 data[]; }` at 0xde500000, read through the `+0x01000000` alias - and
the offsets (sig 0, size 8, data 12) and the sig ('DBGC') are the owner's (`entry_stubs.c`), not this
arm's to restate. `[[mi4-one-value-two-definitions]]` is exactly this class.

1. **THE SOURCE'S VALUES against the owner's** - the console sig, the three offsets, the size bound, and
   the alias (`LIVE_CONSOLE_ALIAS_BASE = RAM_CONSOLE_BASE + 0x01000000`), each compared to the owner's
   own expression in `entry_stubs.c`, and the ENDPOINT BITS re-used from `entry_usb_enum.h`.

2. **THE COMPLETION ACCESSOR is the dTD `token`, not the qh `curr`.** The stream reads a completed
   transfer's byte count as `length - ((token & TD_TOTAL_MASK) >> TD_TOTAL_SHIFT)`; the guard refuses a
   stream that reads `QH_OFF_CURR` for a count (the vendor never does - `_hardware_dequeue:2176-2178`),
   and refuses a completion that does not test `TD_STATUS_ACTIVE` first (the `-EBUSY` guard).

3. **THE WRITE-TARGET WHITELIST, and the two boundaries.** Every `usb_stream_write32(<ARG>, ...)` first
   argument is a `STAGE90_USB_*` name a header defines; no store targets `ENDPTCTRL` (the enum arm owns
   the endpoint enable); no store targets `USBCMD` or `DEVICEADDR` (the enum arm owns those). The stream
   writes ONLY `ENDPTPRIME` and `ENDPTCOMPLETE` - one owner of the endpoint's state at a time.

4. **THE HANDOVER, at the source, both ways.** The stream arm requires the enum arm (`STREAM=1 ENUM=0`
   refused at build); and `entry_usb_enum.c` must guard its EP1-IN prime AND its EP1-IN completion
   re-prime on `STAGE90_XNU_USB_STREAM`, or both files write qh[IN1].

5. **THE SWITCH REACHED THE IMAGE, both ways.**
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

STREAM_H = os.path.join(REPO_ROOT, "src/entry/entry_usb_stream.h")
STREAM_C = os.path.join(REPO_ROOT, "src/entry/entry_usb_stream.c")
ENUM_H = os.path.join(REPO_ROOT, "src/entry/entry_usb_enum.h")
ENUM_C = os.path.join(REPO_ROOT, "src/entry/entry_usb_enum.c")
STUBS_C = os.path.join(REPO_ROOT, "src/entry/entry_stubs.c")

NM = "arm-none-eabi-nm"
OBJDUMP = "arm-none-eabi-objdump"

# The source owners: `entry_stubs.c` for the console ring, `entry_usb_enum.h` for the endpoint bits.
OWNER_FILES = [STUBS_C, ENUM_H, os.path.join(REPO_ROOT, "src/entry/entry_usb.h")]

# This header's name -> an expression in the OWNER's own names / literals.
CROSS_VALUES = {
    # the console ring, owned by `entry_stubs.c`
    "STAGE90_USB_STREAM_CONSOLE_SIG":  "RAM_CONSOLE_SIG",
    "STAGE90_USB_STREAM_OFF_SIG":      "0",
    "STAGE90_USB_STREAM_OFF_SIZE":     "8",
    "STAGE90_USB_STREAM_OFF_DATA":     "12",
    # the endpoint bits re-used from the enum header (NOT restated - a copy would be the defect)
    "STAGE90_USB_STREAM_QH_IN1":       "STAGE90_USB_ENUM_QH_IN1",
}

# The values that must be a SPECIFIC literal, because the owner expresses them as arithmetic over an
# absolute address the guard cannot evaluate from a `#define` alone: the alias base is the ring's base
# plus 1 MB, and the size bound is the 2 MB buffer minus the 12-byte header.
LITERAL_EXPECT = {
    "STAGE90_USB_STREAM_RAM_CONSOLE_BASE": 0xde500000,
    "STAGE90_USB_STREAM_CONSOLE_VA":       0xde500000 + 0x01000000,
    "STAGE90_USB_STREAM_SIZE_MAX":         0x00200000 - 12,
    "STAGE90_USB_STREAM_CHUNK":            4096,
    "STAGE90_USB_STREAM_TD_LEN":           28,
}


class Refused(Exception):
    pass


def say(message):
    print(message)


def read(path):
    with open(path, "r", errors="replace") as handle:
        return handle.read()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def header_values(text, prefix="STAGE90_USB_"):
    """`#define <prefix>NAME  <expr>` -> {NAME: expression-string}. Continuations joined first."""
    text = strip_comments(text)
    text = re.sub(r"\\\n", " ", text)
    out = {}
    for m in re.finditer(r"^#define[ \t]+%s([A-Z0-9_]+)[ \t]+([^\n]+)$" % re.escape(prefix), text, re.M):
        out[m.group(1)] = m.group(2).strip().rstrip("u").rstrip("U")
    return out


def owner_defines(*paths):
    table = {}
    for path in paths:
        for m in re.finditer(r"^#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+([^\n]+)$",
                             strip_comments(read(path)), re.M):
            table[m.group(1)] = m.group(2).strip()
    return table


def first_arg(text, start):
    i = text.find("(", start)
    if i == -1:
        return None
    depth, j = 0, i
    while j < len(text):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                return text[i + 1:j]
        elif text[j] == "," and depth == 1:
            return text[i + 1:j]
        j += 1
    return None


def eval_expr(expr, table, depth=0):
    if depth > 12:
        raise Refused("expression %r nests too deep" % expr)
    text = expr.strip().rstrip("u").rstrip("U")
    text = re.sub(r"\bBIT\((\d+)\)", r"(1 << \1)", text)
    text = re.sub(r"\bBIT\(([A-Za-z_][A-Za-z0-9_]*)\)", r"(1 << \1)", text)
    text = re.sub(r"\bUL\b", "", text)
    text = re.sub(r"(\d)[uU]\b", r"\1", text)

    def sub_name(m):
        name = m.group(0)
        if name not in table:
            raise Refused("the owner headers define no `%s`" % name)
        return "(%s)" % eval_expr(table[name], table, depth + 1)

    text = re.sub(r"(?<![\w.])[A-Za-z_][A-Za-z0-9_]*", sub_name, text)
    if not re.fullmatch(r"[0-9xXa-fA-F()\s<>\|&+~*-]+", text):
        raise Refused("expression %r is not evaluable: %s" % (expr, text))
    try:
        return eval(text, {"__builtins__": {}}, {})   # noqa: S307 - digits/operators only, checked above
    except Exception as exc:  # noqa: BLE001
        raise Refused("expression %r did not evaluate: %s" % (expr, exc))


def check_sources():
    stream_text = read(STREAM_H)
    stream_body = strip_comments(read(STREAM_C))
    enum_body = strip_comments(read(ENUM_C))
    problems = []

    vals = header_values(stream_text)

    # (1) the source's values against the owner's own expressions.
    table = owner_defines(*OWNER_FILES)
    # both keyed forms: the short name (`STREAM_CONSOLE_VA`) and the full one a sibling define references
    # (`STAGE90_USB_STREAM_RAM_CONSOLE_BASE`) - CROSS_VALUES names the latter, header_values yields the
    # former.
    from_shorts = header_values(stream_text)
    table.update(from_shorts)
    table.update({"STAGE90_USB_" + k: v for k, v in from_shorts.items()})
    for name, expr in sorted(CROSS_VALUES.items()):
        short = name[len("STAGE90_USB_"):]
        if short not in vals:
            problems.append("entry_usb_stream.h defines no %s, but the check names it" % name)
            continue
        try:
            got = eval_expr(vals[short], table)
            want = eval_expr(expr, table)
        except Refused as exc:
            problems.append("%s: %s" % (name, exc))
            continue
        if got != want:
            problems.append("%s = 0x%x but the owner's `%s` = 0x%x ([[mi4-one-value-two-definitions]])"
                            % (name, got, expr, want))

    # the literals the owner expresses as arithmetic over an absolute address.
    for name, want in sorted(LITERAL_EXPECT.items()):
        short = name[len("STAGE90_USB_"):]
        if short not in vals:
            problems.append("entry_usb_stream.h defines no %s, but the check names it" % name)
            continue
        try:
            got = eval_expr(vals[short], table)
        except Refused as exc:
            problems.append("%s: %s" % (name, exc))
            continue
        if got != want:
            problems.append("%s = 0x%x but the owner's layout fixes it at 0x%x "
                            "([[mi4-one-value-two-definitions]])" % (name, got, want))
    say("  source values: %d owner expressions + %d fixed literals (console ring, alias, chunk, dTD size)"
        % (len(CROSS_VALUES), len(LITERAL_EXPECT)))

    # (2) the completion accessor: the count is in the dTD `token`, never the qh `curr`.
    if "QH_OFF_CURR" in stream_body:
        problems.append("entry_usb_stream.c reads `QH_OFF_CURR` - the arrived byte count is in the dTD "
                        "`token` (`length - ((token & TD_TOTAL_MASK) >> TD_TOTAL_SHIFT)`), and the vendor "
                        "never reads `qh.curr` for a count (`_hardware_dequeue:2176-2178`)")
    if "TD_TOTAL_MASK" not in stream_body or "TD_TOTAL_SHIFT" not in stream_body:
        problems.append("entry_usb_stream.c does not compute the arrived count from `TD_TOTAL_MASK`/"
                        "`TD_TOTAL_SHIFT`: the completion accessor must be the dTD `token`")
    if "TD_ACTIVE" not in stream_body:
        problems.append("entry_usb_stream.c has no `TD_STATUS_ACTIVE` test: a still-active dTD is not "
                        "done and must not be read as a completed transfer (the vendor's -EBUSY guard)")
    if "FlushPoC_DcacheRegion" not in stream_body:
        problems.append("entry_usb_stream.c does not invalidate the dTD with `FlushPoC_DcacheRegion`: the "
                        "core's write to the dTD `token` is to DRAM and a stale cached line would shadow "
                        "it (and `FlushPoU_Dcache` is the seam-wrapped symbol, 910b design §9.b)")
    say("  completion: the dTD `token` count with the ACTIVE guard and the PoC invalidate; no `qh.curr`")

    # (3) the write-target whitelist, and the two boundaries.
    owned = set()
    for path in (ENUM_H, STREAM_H, os.path.join(REPO_ROOT, "src/entry/entry_usb.h"),
                 os.path.join(REPO_ROOT, "src/entry/entry_usb_dev.h")):
        for m in re.finditer(r"^#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)",
                             strip_comments(read(path)), re.M):
            owned.add(m.group(1))
    writes = []
    for m in re.finditer(r"\busb_stream_write32\s*\(", stream_body):
        arg = (first_arg(stream_body, m.start()) or "").strip()
        if not arg or arg.startswith("uint32_t"):
            continue
        base = re.match(r"([A-Za-z_][A-Za-z0-9_]*)", arg)
        writes.append(base.group(1) if base else arg)
    bad = sorted({a for a in writes if a not in owned})
    if bad:
        problems.append("entry_usb_stream.c stores through usb_stream_write32 to a name no header defines: "
                        + ", ".join(bad) + " - a write to a register with no owner")
    if not writes:
        problems.append("entry_usb_stream.c takes no usb_stream_write32 store: the stream's whole point is "
                        "that it primes and completes the one bulk IN endpoint")
    # the stream writes ONLY ENDPTPRIME and ENDPTCOMPLETE: the enum arm owns the endpoint enable and the
    # address; a stream store to either is a second writer of one value.
    for forbidden, why in (("ENDPTCTRL", "the ENUM arm owns the endpoint enable (SET_CONFIGURATION)"),
                           ("USBCMD", "the ENUM arm / 910a2 own the link state"),
                           ("DEVICEADDR", "the ENUM arm owns the address")):
        if any(forbidden in w for w in writes):
            problems.append("entry_usb_stream.c stores `%s` - %s; the stream arm primes and completes only"
                            % (forbidden, why))
    say("  write-targets: %d stores, each a transcribed `STAGE90_USB_*` offset; only ENDPTPRIME/ENDPTCOMPLETE"
        % len(writes))

    # (4) the handover, at the source, both ways. The enum file must guard BOTH its EP1 prime and its EP1
    # completion re-prime on the stream switch, or both files write qh[IN1].
    if "STAGE90_XNU_USB_STREAM" not in enum_body:
        problems.append("entry_usb_enum.c does not mention `STAGE90_XNU_USB_STREAM`: with the stream arm "
                        "ON the enum arm must not prime or re-prime EP1-IN (it hands the endpoint over)")
    else:
        # both guarded primes: the arm-time prime and the completion re-prime.
        primes = [m.start() for m in re.finditer(r"usb_enum_prime\(\s*STAGE90_USB_ENUM_QH_IN1", enum_body)]
        if len(primes) < 2:
            problems.append("entry_usb_enum.c no longer primes EP1-IN twice (arm + completion): the "
                            "handover guard cannot be proven over %d site(s)" % len(primes))
        else:
            # each prime must sit inside a `#if !STAGE90_XNU_USB_STREAM` ... `#endif` region.
            for p in primes:
                before = enum_body[:p]
                guard = before.rfind("#if !STAGE90_XNU_USB_STREAM")
                if guard == -1 or enum_body[guard:p].find("#endif") != -1:
                    problems.append("an `usb_enum_prime(... QH_IN1 ...)` in entry_usb_enum.c is NOT under "
                                    "`#if !STAGE90_XNU_USB_STREAM`: with the stream ON both files would "
                                    "write qh[IN1]")
                    break
    say("  handover: entry_usb_enum.c guards both EP1-IN primes on the stream switch")

    if problems:
        raise Refused("\n".join("  - " + p for p in problems))


def record_switch(image_path, key):
    record = os.path.join(os.path.dirname(image_path), "xnu_arm_entry-config.txt")
    if not os.path.isfile(record):
        return None
    m = re.search(r"^%s=(\d+)$" % re.escape(key), read(record), re.M)
    return int(m.group(1)) if m else None


def nm_sizes(image):
    out = {}
    for line in subprocess.run([NM, "-S", "-n", image], capture_output=True, text=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 4:
            try:
                out[parts[3]] = (int(parts[0], 16), int(parts[1], 16), parts[2])
            except ValueError:
                continue
    return out


def body_of(image, name):
    syms = nm_sizes(image)
    if name not in syms:
        return None
    addr, size, _kind = syms[name]
    if size == 0:
        return ""
    return subprocess.run([OBJDUMP, "-d", "--start-address=0x%x" % addr,
                           "--stop-address=0x%x" % (addr + size), image],
                          capture_output=True, text=True).stdout


def check_image(image):
    syms = nm_sizes(image)
    if "entry_usb_stream_poll" not in syms:
        raise Refused("%s defines no `entry_usb_stream_poll`: the call site in entry_trace.c would be an "
                      "undefined symbol" % image)
    _addr, size, kind = syms["entry_usb_stream_poll"]
    if kind != "T":
        raise Refused("`entry_usb_stream_poll` is a `%s` symbol, not `T` (text)" % kind)
    switch = record_switch(image, "STAGE90_XNU_USB_STREAM")
    enum_switch = record_switch(image, "STAGE90_XNU_USB_ENUM")
    if switch == 1 and enum_switch != 1:
        raise Refused("the record says STAGE90_XNU_USB_STREAM=1 but STAGE90_XNU_USB_ENUM=%s: the stream "
                      "requires the enum arm (the build refuses STREAM=1 ENUM=0; this is the linked check)"
                      % enum_switch)
    if switch == 1:
        if size <= 8:
            raise Refused("the record says STAGE90_XNU_USB_STREAM=1 but the linked `entry_usb_stream_poll` "
                          "body is size %d: the arm's switch did not reach the image "
                          "([[mi4-off-option-two-spellings]])" % size)
        say("  image: STAGE90_XNU_USB_STREAM=1 and `entry_usb_stream_poll` is a %d-byte T body" % size)
    elif switch == 0:
        if size > 8:
            raise Refused("the record says STAGE90_XNU_USB_STREAM=0 but the linked `entry_usb_stream_poll` "
                          "body is %d bytes: the OFF arm must be a bare return" % size)
        say("  image: STAGE90_XNU_USB_STREAM=0 and `entry_usb_stream_poll` is a bare return")
    else:
        say("  image: no USB_STREAM key in the record; `entry_usb_stream_poll` is %d bytes" % size)


def selftest():
    original_h = read(STREAM_H)
    original_c = read(STREAM_C)
    original_enum = read(ENUM_C)
    cases = []

    def with_edit(name, paths, old, new, expect_in):
        # `paths` is a single letter: "h" (stream header), "c" (stream body), "e" (enum body).
        target = {"h": STREAM_H, "c": STREAM_C, "e": ENUM_C}[paths]
        text = {"h": original_h, "c": original_c, "e": original_enum}[paths]
        mutated = text.replace(old, new, 1)
        if mutated == text:
            cases.append((name, "the mutation did not apply: %r not in %s" % (old, paths), False))
            return
        open(target, "w").write(mutated)
        try:
            check_sources()
            cases.append((name, "check_sources accepted the mutation (it should have refused)", False))
        except Refused as exc:
            cases.append((name, str(exc).strip().splitlines()[0] if str(exc).strip() else "(refused)",
                          expect_in in str(exc)))
        finally:
            open(STREAM_H, "w").write(original_h)
            open(STREAM_C, "w").write(original_c)
            open(ENUM_C, "w").write(original_enum)

    # the console sig drifts
    with_edit("the console sig drifts", "h",
              "#define STAGE90_USB_STREAM_CONSOLE_SIG   0x43474244u",
              "#define STAGE90_USB_STREAM_CONSOLE_SIG   0x43474245u",
              "CONSOLE_SIG")
    # the data offset drifts off the 12-byte header
    with_edit("the console data offset drifts", "h",
              "#define STAGE90_USB_STREAM_OFF_DATA      12u",
              "#define STAGE90_USB_STREAM_OFF_DATA      16u",
              "OFF_DATA")
    # the alias base drifts off the ring + 1 MB
    with_edit("the alias base drifts", "h",
              "STAGE90_USB_STREAM_CONSOLE_VA    (STAGE90_USB_STREAM_RAM_CONSOLE_BASE + 0x01000000u)",
              "STAGE90_USB_STREAM_CONSOLE_VA    (STAGE90_USB_STREAM_RAM_CONSOLE_BASE + 0x00200000u)",
              "CONSOLE_VA")
    # the completion reads qh.curr instead of the dTD token
    with_edit("the completion reads qh.curr", "c",
              "token = td[STAGE90_USB_ENUM_TD_OFF_TOKEN / 4u];",
              "token = *(volatile uint32_t *)(uintptr_t)((uintptr_t)qh + STAGE90_USB_ENUM_QH_OFF_CURR);",
              "QH_OFF_CURR")
    # the stream enables the endpoint itself (the handover broken)
    with_edit("the stream writes ENDPTCTRL", "c",
              "usb_stream_write32(STAGE90_USB_ENDPTPRIME, STAGE90_USB_ENUM_EPBIT(STAGE90_USB_ENUM_EP_IN, 1u));",
              "usb_stream_write32(STAGE90_USB_ENDPTCTRL(1u), 0x00c80000u);\n    "
              "usb_stream_write32(STAGE90_USB_ENDPTPRIME, STAGE90_USB_ENUM_EPBIT(STAGE90_USB_ENUM_EP_IN, 1u));",
              "ENDPTCTRL")
    # the enum arm's handover guard is removed
    with_edit("the enum handover guard is removed", "e",
              "#if !STAGE90_XNU_USB_STREAM",
              "#if 1",
              "STAGE90_XNU_USB_STREAM")

    refused = sum(1 for _n, _r, ok in cases if ok)
    for name, reason, ok in cases:
        say("  %-45s %s  %s" % (name, "refused:" if ok else "ACCEPTED:", reason))
    say("  --selftest: %d of %d mutations were refused" % (refused, len(cases)))
    if refused != len(cases):
        raise SystemExit("the falsification battery is not watertight: %d of %d refused"
                         % (refused, len(cases)))


def main():
    parser = argparse.ArgumentParser(description="910c's USB bulk-stream arm guard")
    parser.add_argument("--image", help="the linked xnu_arm_entry.elf")
    parser.add_argument("--selftest", action="store_true", help="run the falsification battery")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    try:
        check_sources()
        if args.image:
            check_image(args.image)
    except Refused as exc:
        print("FAIL: " + str(exc), file=sys.stderr)
        return 1

    if args.selftest:
        try:
            selftest()
        except SystemExit as exc:
            print("FAIL: " + str(exc), file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())