#!/usr/bin/env python3
"""910a2's guard: the write arm's transcribed offsets and *values* against their owner, the
write-target whitelist, the mode gate, and whether the arm's switch reached the linked image.

910a2 is the first arm in this walk that STORES to a device register, so its guard has to answer the
questions 910a's did not:

1. **ONE VALUE, TWO DEFINITIONS ([[mi4-one-value-two-definitions]]), now for the written VALUES too.**
   `entry_usb_dev.h` cannot include the device's Linux headers, so every register offset it writes and
   every bit it sets is transcribed. The value's *owner* is `external/android_kernel_xiaomi_cancro`'s
   `include/linux/usb/msm_hsusb_hw.h`, `arch/arm/mach-msm/include/mach/msm_hsusb_hw.h`,
   `drivers/usb/gadget/ci13xxx_udc.h` and `include/linux/usb/ulpi.h`. This check evaluates the owner's
   own expression (`BIT(n)`, `(1 << n)`, `(3 << 30)`, a sum of interrupt bits) and compares it to the
   value this header writes. Where the two msm_hsusb headers disagree, the value the **live** driver
   uses is the one recorded, and the check reads the file `msm_otg.c`/`ci13xxx_udc.c` includes.

2. **THE WRITE-TARGET WHITELIST.** Every store in `entry_usb_dev.c` goes through `usb_dev_write32`,
   whose first argument must be a `STAGE90_USB_*` name this header (or `entry_usb.h`) defines - so a
   store to a register with no owner is refused here rather than made at run time. A raw hex offset, or
   a name `entry_usb_dev.h` does not define, is the refusal.

3. **THE MODE GATE, AT THE SOURCE.** The hazard is the link reset (`USBCMD.RST`) dropping the host's
   enumeration. The arm bounds it by refusing to write unless the core is already a device
   (`USBMODE[1:0] == CM_DEVICE`). This check asserts, on comment-stripped source, that the comparison
   and the `FORCE` escape are both present, and that the stores come after the gate
   ([[mi4-a-claim-in-a-comment-is-not-a-check]]).

4. **THE SWITCH REACHED THE IMAGE, IN BOTH DIRECTIONS.** Read `STAGE90_XNU_USB_DEV` out of the arm's
   record and the linked ELF: with it 1 the body must be a real `T` body that stores through the `0xf9a5`
   window; with it 0 a bare return.
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

DEV_H = os.path.join(REPO_ROOT, "src/entry/entry_usb_dev.h")
DEV_C = os.path.join(REPO_ROOT, "src/entry/entry_usb_dev.c")
USB_H = os.path.join(REPO_ROOT, "src/entry/entry_usb.h")
ANDROID = os.path.join(REPO_ROOT, "external/android_kernel_xiaomi_cancro")
LINUX_HW_H = os.path.join(ANDROID, "include/linux/usb/msm_hsusb_hw.h")
MACH_HW_H = os.path.join(ANDROID, "arch/arm/mach-msm/include/mach/msm_hsusb_hw.h")
GADGET_H = os.path.join(ANDROID, "drivers/usb/gadget/ci13xxx_udc.h")
ULPI_H = os.path.join(ANDROID, "include/linux/usb/ulpi.h")
DTSI = os.path.join(ANDROID, "arch/arm/boot/dts/msm8974.dtsi")

NM = "arm-none-eabi-nm"
OBJDUMP = "arm-none-eabi-objdump"

# The registers 910a2 writes that 910a's read header does not name: this header's name -> the Android
# header's name, so the comparison is of VALUES not spellings ([[mi4-one-value-two-definitions]]).
CROSS_OFFSETS = {
    "STAGE90_USB_AHBBURST":    "USB_AHBBURST",
    "STAGE90_USB_PHY_CTRL":    "USB_PHY_CTRL",
    "STAGE90_USB_PHY_CTRL2":   "USB_PHY_CTRL2",
    "STAGE90_USB_L1_EP_CTRL":  "USB_L1_EP_CTRL",
    "STAGE90_USB_L1_CONFIG":   "USB_L1_CONFIG",
}

# The written VALUES: this header's name -> an expression in the owner headers' own names, evaluated and
# compared. A name here with no owner definition is a transcription with no owner - the defect.
CROSS_VALUES = {
    "STAGE90_USB_USBCMD_RS":               "USBCMD_RS",
    "STAGE90_USB_USBCMD_RST":              "USBCMD_RST",
    "STAGE90_USB_USBMODE_CM":              "USBMODE_CM",
    "STAGE90_USB_USBMODE_CM_IDLE":         "USBMODE_CM_IDLE",
    "STAGE90_USB_USBMODE_CM_DEVICE":       "USBMODE_CM_DEVICE",
    "STAGE90_USB_USBMODE_CM_HOST":         "USBMODE_CM_HOST",
    "STAGE90_USB_USBMODE_SLOM":            "USBMODE_SLOM",
    "STAGE90_USB_USBMODE_SDIS":            "USBMODE_SDIS",
    "STAGE90_USB_ENDPTCTRL_RXS":           "ENDPTCTRL_RXS",
    "STAGE90_USB_ENDPTCTRL_RXT":           "ENDPTCTRL_RXT",
    "STAGE90_USB_ENDPTCTRL_RXR":           "ENDPTCTRL_RXR",
    "STAGE90_USB_ENDPTCTRL_RXE":           "ENDPTCTRL_RXE",
    "STAGE90_USB_ENDPTCTRL_TXS":           "ENDPTCTRL_TXS",
    "STAGE90_USB_ENDPTCTRL_TXT":           "ENDPTCTRL_TXT",
    "STAGE90_USB_ENDPTCTRL_TXR":           "ENDPTCTRL_TXR",
    "STAGE90_USB_ENDPTCTRL_TXE":           "ENDPTCTRL_TXE",
    "STAGE90_USB_USBINTR_VALUE":           "USBi_UI | USBi_UEI | USBi_PCI | USBi_URI | USBi_SLI",
    "STAGE90_USB_OTGSC_IDIE":              "OTGSC_IDIE",
    "STAGE90_USB_OTGSC_BSVIE":             "OTGSC_BSVIE",
    "STAGE90_USB_PORTSC_PTS":              "PORTSC_PTS",
    "STAGE90_USB_PORTSC_PTS_ULPI":         "PORTSC_PTS_ULPI",
    "STAGE90_USB_AHBMODE_AHB2AHB_BYPASS":  "AHB2AHB_BYPASS",
    "STAGE90_USB_PHY_POR_BIT_MASK":        "PHY_POR_BIT_MASK",
    "STAGE90_USB_PHY_POR_ASSERT":          "PHY_POR_ASSERT",
    "STAGE90_USB_PHY_POR_DEASSERT":        "PHY_POR_DEASSERT",
    "STAGE90_USB_ULPI_RUN":                "ULPI_RUN",
    "STAGE90_USB_ULPI_WRITE":              "ULPI_WRITE",
    "STAGE90_USB_ULPI_READ":               "ULPI_READ",
    "STAGE90_USB_ULPI_INT_SESS_VALID":     "ULPI_INT_SESS_VALID",
    "STAGE90_USB_ULPI_USB_INT_EN_RISE":    "ULPI_USB_INT_EN_RISE",
    "STAGE90_USB_ULPI_USB_INT_EN_FALL":    "ULPI_USB_INT_EN_FALL",
    "STAGE90_USB_L1_CONFIG_LPM_EN":        "L1_CONFIG_LPM_EN",
    "STAGE90_USB_L1_CONFIG_REMOTE_WAKEUP": "L1_CONFIG_REMOTE_WAKEUP",
    "STAGE90_USB_L1_CONFIG_GATE_SYS_CLK":  "L1_CONFIG_GATE_SYS_CLK",
    "STAGE90_USB_L1_CONFIG_PHY_LPM":       "L1_CONFIG_PHY_LPM",
    "STAGE90_USB_L1_CONFIG_PLL":           "L1_CONFIG_PLL",
}

# The owner headers, in the order a name is looked up: the `linux/` header first because that is the one
# `msm_otg.c` and `ci13xxx_udc.c` include, then the `mach/` header (used by the unbuilt msm72k driver),
# then the gadget's own header and the ULPI register header. First match wins, which is the live driver's.
OWNER_HEADERS = [LINUX_HW_H, MACH_HW_H, GADGET_H, ULPI_H]


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


def header_values(text):
    """Every `#define NAME <expr>` in one header, as NAME -> raw expression (comments stripped). Only
    the object-like forms; a function-like macro (`NAME(n)`) is skipped - it is not a value."""
    out = {}
    for match in re.finditer(r"^#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+([^\n]+)$",
                             strip_comments(text), re.M):
        name, expr = match.group(1), match.group(2).strip()
        if "(" in name:  # function-like: `#define FOO(n)` - the name would have absorbed the paren
            continue
        out[name] = expr
    return out


def owner_table():
    table = {}
    for path in OWNER_HEADERS:
        for name, expr in header_values(read(path)).items():
            table.setdefault(name, expr)   # first header wins - the live driver's
    return table


def eval_expr(expr, table, depth=0):
    """Evaluate a C constant expression built from BIT(n), shifts, bit-or, hex/decimal, and the owner
    names. Enough for the header's forms; anything else raises Refused so the check never silently
    passes on an expression it could not read."""
    if depth > 16:
        raise Refused("expression %r nests too deep to evaluate" % expr)
    s = expr.strip()
    # Resolve names (BIT is a macro `(1 << (n))` in the kernel; treat it directly).
    s = re.sub(r"\bBIT\((\d+)\)", lambda m: str(1 << int(m.group(1))), s)
    s = re.sub(r"\b([A-Za-z_][A-Za-z0-9_]*)\b",
               lambda m: "(" + str(eval_expr(table[m.group(1)], table, depth + 1)) + ")"
               if m.group(1) in table else m.group(0), s)
    # C integer suffixes (`UL`, `U`, `L`) are not Python: strip them (`0x03UL` -> `0x03`).
    s = re.sub(r"\b(0x[0-9a-fA-F]+|\d+)[uUlL]+\b", r"\1", s)
    try:
        return int(eval(s, {"__builtins__": {}}, {}))
    except Exception as exc:  # noqa: BLE001 - any failure is a refusal, not a crash
        raise Refused("could not evaluate owner expression %r -> %r (%s)" % (expr, s, exc))


def dev_header_offsets(text):
    """`#define STAGE90_USB_<SHORT> 0xNNNu` -> {SHORT: value} (the `[ \\t]+` discipline: a `\\s+` can
    cross a newline and read the next `#define`, 482's lesson)."""
    out = {}
    for match in re.finditer(r"^#define[ \t]+STAGE90_USB_([A-Z0-9_]+)[ \t]+(0x[0-9a-fA-F]+)u?[ \t]*$",
                             strip_comments(text), re.M):
        out[match.group(1)] = int(match.group(2), 0)
    return out


def dev_header_values(text):
    """Every plain-valued `STAGE90_USB_<NAME> <hex|decimal>` in this header, by short name."""
    out = {}
    for match in re.finditer(r"^#define[ \t]+STAGE90_USB_([A-Z0-9_]+)[ \t]+(0x[0-9a-fA-F]+|\d+)u?[ \t]*$",
                             strip_comments(text), re.M):
        out[match.group(1)] = int(match.group(2), 0)
    return out


def android_offsets():
    out = {}
    for path in (LINUX_HW_H, MACH_HW_H):
        for match in re.finditer(r"^#define[ \t]+USB_([A-Z0-9_]+)[ \t]+\(MSM_USB_BASE[ \t]*\+[ \t]*(0x[0-9a-fA-F]+)\)",
                                 strip_comments(read(path)), re.M):
            out[match.group(1)] = int(match.group(2), 0)
    return out


def check_sources():
    dev_text = read(DEV_H)
    dev_body = strip_comments(read(DEV_C))
    usb_text = read(USB_H)
    problems = []

    # (1a) the offsets against their owner.
    offs = dev_header_offsets(dev_text)
    usb_offs = dev_header_offsets(usb_text)   # entry_usb.h's, so a write to a read header name also owns
    android = android_offsets()
    for name, a_name in sorted(CROSS_OFFSETS.items()):
        short = name[len("STAGE90_USB_"):]
        a_short = a_name[len("USB_"):]
        if short not in offs:
            problems.append("entry_usb_dev.h defines no %s, but the check names it" % name)
            continue
        if a_short not in android:
            problems.append("the Android headers spell no %s, so %s has no owner" % (a_name, name))
            continue
        if offs[short] != android[a_short]:
            problems.append("%s = 0x%x but %s = 0x%x ([[mi4-one-value-two-definitions]])"
                            % (name, offs[short], a_name, android[a_short]))
    say("  usb-dev offsets: %d transcribed, each compared against the Android header that owns it"
        % len(CROSS_OFFSETS))

    # (1b) the written VALUES against their owner, by evaluating the owner's own expression.
    table = owner_table()
    vals = dev_header_values(dev_text)
    for name, owner_expr in sorted(CROSS_VALUES.items()):
        short = name[len("STAGE90_USB_"):]
        if short not in vals:
            problems.append("entry_usb_dev.h defines no %s, but the check names it" % name)
            continue
        try:
            want = eval_expr(owner_expr, table)
        except Refused as exc:
            problems.append("%s: %s" % (name, exc))
            continue
        if vals[short] != want:
            problems.append("%s = 0x%x but the owner's `%s` = 0x%x ([[mi4-one-value-two-definitions]])"
                            % (name, vals[short], owner_expr, want))
    say("  usb-dev values: %d written values, each compared against the owner's own expression"
        % len(CROSS_VALUES))

    # (2) the write-target whitelist: every `usb_dev_write32(<ARG>, ...)` call's first argument is a
    # defined `STAGE90_USB_*` name. The accessor's own definition (`static void usb_dev_write32(uint32_t
    # off, ...)`) is skipped by its `uint32_t` type-name, so the definition is not a "call". A raw
    # numeric first argument (a store to an un-owned offset) is captured and refused.
    owned = set("STAGE90_USB_" + s for s in offs) | set("STAGE90_USB_" + s for s in usb_offs)

    def write_args(text):
        out = []
        for m in re.finditer(r"\busb_dev_write32\(\s*([^,)]*)", text):
            arg = m.group(1).strip()
            if arg.startswith("uint32_t"):
                continue                      # the accessor's own definition, not a call
            out.append((m.start(), arg))
        return out

    writes = write_args(dev_body)
    bad = sorted({a for _p, a in writes if a not in owned})
    if bad:
        problems.append("entry_usb_dev.c stores through usb_dev_write32 to a name no header defines: "
                        + ", ".join(bad) + " - a write to a register with no owner")
    if not writes:
        problems.append("entry_usb_dev.c takes no usb_dev_write32 store: the write arm's whole point is "
                        "that it writes")
    say("  write-targets: %d distinct registers stored to, each a transcribed `STAGE90_USB_*` offset"
        % len({a for _p, a in writes}))

    # (3) the mode gate, at the source - the exact FORM, not the presence of a token the file uses four
    # times. The gate is `if (((x & CM) != CM_DEVICE) && (FORCE == 0)) { ...; return; }`, and the first
    # store of the init body must come AFTER it (the gate is a claim until its position proves it).
    gate_re = re.compile(r"!=\s*STAGE90_USB_USBMODE_CM_DEVICE\s*\)\s*&&\s*"
                         r"\(STAGE90_XNU_USB_DEV_FORCE\s*==\s*0\)")
    gate_match = gate_re.search(dev_body)
    if not gate_match:
        problems.append("entry_usb_dev.c has no `((x & CM) != CM_DEVICE) && (FORCE == 0)` gate: the "
                        "write arm must not write unless the core is already a device (the link reset "
                        "can drop the host's enumeration), and `FORCE` is the only escape")
    gate = gate_match.start() if gate_match else -1
    # The refusal branch must `return` (not fall through) - a gate that continues anyway is no gate.
    if gate != -1 and "return;" not in dev_body[gate:gate + 400]:
        problems.append("entry_usb_dev.c's mode gate does not return from the non-device branch")

    # The first store, scanned in the FUNCTION BODY only - the helpers above the init function take
    # stores too (the ULPI write, the PHY POR), and those are the arm's vocabulary, not its body.
    fn = dev_body.find("void entry_usb_dev_init(void)")
    init_body = dev_body[fn:] if fn != -1 else dev_body
    first_store = -1
    for pos, arg in write_args(init_body):
        if arg.startswith("STAGE90_USB_") or not arg[0].isalpha():
            first_store = pos
            break
    rel_gate = (gate - fn) if (fn != -1 and gate != -1) else gate
    if fn != -1 and gate != -1 and first_store != -1 and first_store < rel_gate:
        problems.append("entry_usb_dev.c's first usb_dev_write32 appears BEFORE the mode gate: the "
                        "stores would run before the check that bounds them")
    say("  mode gate: the `!= CM_DEVICE && FORCE == 0` form is present and returns, and the init body's "
        "first store is after it")

    # (4) the collapse and the deliberate omissions, published rather than silent.
    for want in ("xnu_live_usb_dev_rst_count", "xnu_live_usb_dev_intr", "xnu_live_usb_dev_eplist"):
        if want not in dev_body:
            problems.append("entry_usb_dev.c never publishes `%s`: the collapse/omission must be a "
                            "reading, not a silence" % want)
    say("  omissions: the single reset and the un-programmed USBINTR/ENDPOINTLISTADDR are published")

    if problems:
        raise Refused("\n".join("  - " + p for p in problems))


def record_switch(image_path, key):
    record = os.path.join(os.path.dirname(image_path), "xnu_arm_entry-config.txt")
    if not os.path.isfile(record):
        return None
    match = re.search(r"^%s=(\d+)$" % re.escape(key), read(record), re.M)
    return int(match.group(1)) if match else None


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
    if "entry_usb_dev_init" not in syms:
        raise Refused("%s defines no `entry_usb_dev_init`: the call site in entry_trace.c would be an "
                      "undefined symbol" % image)
    _addr, size, kind = syms["entry_usb_dev_init"]
    if kind != "T":
        raise Refused("`entry_usb_dev_init` is a `%s` symbol, not `T` (text)" % kind)
    body = body_of(image, "entry_usb_dev_init") or ""
    switch = record_switch(image, "STAGE90_XNU_USB_DEV")
    stores_window = bool(re.search(r"0xf9a5", body, re.I))
    if switch == 1:
        if size <= 8 or not stores_window:
            raise Refused("the record says STAGE90_XNU_USB_DEV=1 but the linked `entry_usb_dev_init` "
                          "body is size %d with no store through the 0xf9a5 window: the arm's switch "
                          "did not reach the image ([[mi4-off-option-two-spellings]])" % size)
        say("  image: STAGE90_XNU_USB_DEV=1 and `entry_usb_dev_init` is a %d-byte T body that stores "
            "through the 0xf9a5 window" % size)
    elif switch == 0:
        if size > 8:
            raise Refused("the record says STAGE90_XNU_USB_DEV=0 but the linked `entry_usb_dev_init` "
                          "body is %d bytes: the OFF arm must be a bare return" % size)
        say("  image: STAGE90_XNU_USB_DEV=0 and `entry_usb_dev_init` is a bare return")
    else:
        say("  image: no USB_DEV key in the record; `entry_usb_dev_init` is %d bytes" % size)


def selftest():
    global DEV_H, DEV_C
    original_h = read(DEV_H)
    original_c = read(DEV_C)
    cases = []

    def with_edit(name, path, old, new, expect_in):
        text = original_h if path == "h" else original_c
        mutated = text.replace(old, new, 1)
        if mutated == text:
            cases.append((name, "the mutation did not apply: %r not in %s" % (old, path), False))
            return
        if path == "h":
            open(DEV_H, "w").write(mutated)
        else:
            open(DEV_C, "w").write(mutated)
        try:
            check_sources()
            cases.append((name, "check_sources accepted the mutation (it should have refused)", False))
        except Refused as exc:
            hit = expect_in in str(exc)
            cases.append((name, str(exc).strip().splitlines()[0] if str(exc).strip() else "(refused)",
                          hit))
        finally:
            open(DEV_H, "w").write(original_h)
            open(DEV_C, "w").write(original_c)

    # An offset drifts from the Android header.
    with_edit("a written offset drifts from its owner", "h",
              "#define STAGE90_USB_PHY_CTRL     0x240u", "#define STAGE90_USB_PHY_CTRL     0x244u",
              "USB_PHY_CTRL")
    # A written VALUE drifts from the owner's expression.
    with_edit("a written value drifts from its owner", "h",
              "#define STAGE90_USB_PORTSC_PTS_ULPI    0xc0000000u",
              "#define STAGE90_USB_PORTSC_PTS_ULPI    0x80000000u",
              "PORTSC_PTS_ULPI")
    # A store to a register with no owner: a raw numeric offset, not a transcribed name.
    with_edit("a store to an unowned register appears", "c",
              "    usbcmd = usb_dev_read32(STAGE90_USB_USBCMD);\n    USB_DEV_LIVE(\"xnu_live_usb_dev_usbcmd_before\", usbcmd);",
              "    usb_dev_write32(0x1e0u, 0u);\n    usbcmd = usb_dev_read32(STAGE90_USB_USBCMD);\n    USB_DEV_LIVE(\"xnu_live_usb_dev_usbcmd_before\", usbcmd);",
              "no owner")
    # The mode gate is removed: the writes may run on any core.
    with_edit("the mode gate is removed", "c",
              "if (((usbmode_before & STAGE90_USB_USBMODE_CM) != STAGE90_USB_USBMODE_CM_DEVICE)",
              "if (((usbmode_before & STAGE90_USB_USBMODE_CM) != 0xf0f0u)",
              "CM_DEVICE")
    # The first store is moved above the gate.
    with_edit("a store moves above the mode gate", "c",
              "    usbmode_before = usb_dev_read32(STAGE90_USB_USBMODE);",
              "    usb_dev_write32(STAGE90_USB_AHBBURST, 0u);\n    usbmode_before = usb_dev_read32(STAGE90_USB_USBMODE);",
              "BEFORE the mode gate")

    refused = sum(1 for _n, _r, ok in cases if ok)
    for name, reason, ok in cases:
        say("  %-45s %s  %s" % (name, "refused:" if ok else "ACCEPTED:", reason))
    say("  --selftest: %d of %d mutations were refused" % (refused, len(cases)))
    if refused != len(cases):
        raise SystemExit("the falsification battery is not watertight: %d of %d refused"
                         % (refused, len(cases)))


def main():
    parser = argparse.ArgumentParser(description="910a2's USB write-arm guard")
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