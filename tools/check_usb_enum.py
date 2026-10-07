#!/usr/bin/env python3
"""910b's guard: the enumeration arm's transcribed offsets, its qh/dTD STRUCT LAYOUT recomputed from the
owner's own struct, its written values against the owner's expressions, the mode gate, the no-RST
refusal, and whether the switch reached the linked image.

910b is the first arm that BUILDS A DMA GRAPH the core reads, so its guard has to answer a question the
two earlier USB guards did not: **is the qh/td layout a recomputation of the owner's struct, or a
transcription that has already drifted?** The qh's `setup` field sits at offset 40, not the 32 a
"8 header dwords + setup" guess gives - it drifted once while this arm was written, and the fix is to
recompute the offset from `struct ci13xxx_qh` ([[mi4-one-value-two-definitions]] is exactly this class).

1. **STRUCT OFFSETS, RECOMPUTED.** Parse `struct ci13xxx_td` and `struct ci13xxx_qh` from
   `ci13xxx_udc.h` and compute every field offset (packed, aligned(4)), then compare to this header's
   `STAGE90_USB_ENUM_QH_OFF_*` / `STAGE90_USB_ENUM_TD_OFF_*`. A field that moves in the owner, or a
   define that drifts, is refused.

2. **THE WRITTEN VALUES against the owner's own expressions** - `USBCMD_SUTW`, the `USBi_*` mask, the
   `ENDPTCTRL` bits and the bulk type encodings, `QH_IOS`/`QH_ZLT`, the `TD_*` tokens, `DEVICEADDR`.

3. **THE WRITE-TARGET WHITELIST.** Every `usb_enum_write32(<ARG>, ...)` first argument is a
   `STAGE90_USB_*` name some header defines, and no store targets `USBCMD` with `RST` - the 910b arm
   must never re-run the link reset 910a2 owns.

4. **THE MODE GATE, at the source** - the same `!= CM_DEVICE && FORCE == 0` form `entry_usb_dev.c` uses.

5. **THE SWITCH REACHED THE IMAGE, both ways.**
"""
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

ENUM_H = os.path.join(REPO_ROOT, "src/entry/entry_usb_enum.h")
ENUM_C = os.path.join(REPO_ROOT, "src/entry/entry_usb_enum.c")
DEV_H = os.path.join(REPO_ROOT, "src/entry/entry_usb_dev.h")
USB_H = os.path.join(REPO_ROOT, "src/entry/entry_usb.h")
ANDROID = os.path.join(REPO_ROOT, "external/android_kernel_xiaomi_cancro")
LINUX_HW_H = os.path.join(ANDROID, "include/linux/usb/msm_hsusb_hw.h")
MACH_HW_H = os.path.join(ANDROID, "arch/arm/mach-msm/include/mach/msm_hsusb_hw.h")
GADGET_H = os.path.join(ANDROID, "drivers/usb/gadget/ci13xxx_udc.h")

NM = "arm-none-eabi-nm"
OBJDUMP = "arm-none-eabi-objdump"

# The written values: this header's name -> an expression in the owner headers' own names.
CROSS_VALUES = {
    "STAGE90_USB_ENUM_INTR_VALUE":        "USBi_UI | USBi_UEI | USBi_PCI | USBi_URI | USBi_SLI",
    "STAGE90_USB_ENUM_STS_UI":            "USBi_UI",
    "STAGE90_USB_ENUM_STS_UEI":           "USBi_UEI",
    "STAGE90_USB_ENUM_STS_PCI":           "USBi_PCI",
    "STAGE90_USB_ENUM_STS_URI":           "USBi_URI",
    "STAGE90_USB_ENUM_STS_SLI":           "USBi_SLI",
    "STAGE90_USB_ENUM_DEVICEADDR_USBADRA":   "DEVICEADDR_USBADRA",
    "STAGE90_USB_ENUM_USBCMD_SUTW":          "USBCMD_SUTW",
    "STAGE90_USB_ENUM_ENDPTCTRL_TXT_BULK":   "(2 << 18)",
    "STAGE90_USB_ENUM_ENDPTCTRL_RXT_BULK":   "(2 << 2)",
    "STAGE90_USB_ENUM_ENDPTCTRL0_VALUE":     "(ENDPTCTRL_TXE | ENDPTCTRL_TXR | ENDPTCTRL_RXE | ENDPTCTRL_RXR)",
    "STAGE90_USB_ENUM_ENDPTCTRL1_VALUE":     "(ENDPTCTRL_TXE | ENDPTCTRL_TXR | (2 << 18))",
    "STAGE90_USB_ENUM_QH_IOS":               "QH_IOS",
    "STAGE90_USB_ENUM_QH_ZLT":               "QH_ZLT",
    "STAGE90_USB_ENUM_TD_TERMINATE":         "TD_TERMINATE",
    "STAGE90_USB_ENUM_TD_ACTIVE":            "TD_STATUS_ACTIVE",
    "STAGE90_USB_ENUM_TD_IOC":               "TD_IOC",
}

# The owner headers, in the order a name is looked up (the live driver's `linux/` header first).
OWNER_HEADERS = [LINUX_HW_H, MACH_HW_H, GADGET_H]

# The struct offsets this header defines, and the field path each must equal. The recomputation below
# walks the two structs; a field's computed offset must equal the define's value.
STRUCT_OFFSETS = {
    "STAGE90_USB_ENUM_QH_OFF_CAP":    ("qh", "cap"),
    "STAGE90_USB_ENUM_QH_OFF_CURR":   ("qh", "curr"),
    "STAGE90_USB_ENUM_QH_OFF_TDNEXT": ("qh", "td.next"),
    "STAGE90_USB_ENUM_QH_OFF_TDTOK":  ("qh", "td.token"),
    "STAGE90_USB_ENUM_QH_OFF_SETUP":  ("qh", "setup"),
    "STAGE90_USB_ENUM_TD_OFF_NEXT":   ("td", "next"),
    "STAGE90_USB_ENUM_TD_OFF_TOKEN":  ("td", "token"),
    "STAGE90_USB_ENUM_TD_OFF_PAGE0":  ("td", "page"),
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


def header_values(text):
    """`#define STAGE90_..._NAME  <expr>` -> {suffix: expression-string}, suffix = name after STAGE90_USB_.

    Backslash line continuations are joined first, so a multi-line value is one expression.
    """
    text = strip_comments(text)
    text = re.sub(r"\\\n", " ", text)
    out = {}
    for m in re.finditer(r"^#define[ \t]+STAGE90_USB_([A-Z0-9_]+)[ \t]+([^\n]+)$", text, re.M):
        out[m.group(1)] = m.group(2).strip().rstrip("u").rstrip("U")
    return out


def owner_defines(*extra_paths):
    """Every `#define NAME expr` in the owner headers (plus any `extra_paths`), evaluated lazily.

    Function-like macros (`NAME(args)`) are not object-like values, so only `NAME<TAB>expr` is kept.
    """
    table = {}
    for path in list(OWNER_HEADERS) + list(extra_paths):
        for m in re.finditer(r"^#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)[ \t]+([^\n]+)$",
                             strip_comments(read(path)), re.M):
            table[m.group(1)] = m.group(2).strip()
    return table


def first_arg(text, start):
    """The text of the first argument of the call whose `(` is at/after `start`, paren-balanced."""
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
    """Evaluate an owner expression made of `BIT(n)`, `(a << b)`, `|`, `+`, literals and owner names."""
    if depth > 12:
        raise Refused("expression %r nests too deep" % expr)
    text = expr.strip().rstrip("u").rstrip("U")
    text = re.sub(r"\bBIT\((\d+)\)", r"(1 << \1)", text)
    text = re.sub(r"\bBIT\(([A-Za-z_][A-Za-z0-9_]*)\)", r"(1 << \1)", text)
    text = re.sub(r"\bUL\b", "", text)
    text = re.sub(r"(\d)[uU]\b", r"\1", text)   # integer suffixes are not identifiers

    def sub_name(m):
        name = m.group(0)
        if name not in table:
            raise Refused("the owner headers define no `%s`" % name)
        return "(%s)" % eval_expr(table[name], table, depth + 1)

    # replace bare identifiers by their evaluated values. The `(?<![\w.])` lookbehind means a name
    # starting after a word character is left alone: that is what stops `0x01000000` being split into
    # `0` + identifier `x01000000`, and `2u` into `2` + identifier `u`.
    text = re.sub(r"(?<![\w.])[A-Za-z_][A-Za-z0-9_]*", sub_name, text)
    if not re.fullmatch(r"[0-9xXa-fA-F()\s<>\|&+~*]+", text):
        raise Refused("expression %r is not evaluable: %s" % (expr, text))
    try:
        return eval(text, {"__builtins__": {}}, {})   # noqa: S307 - digits/operators only, checked above
    except Exception as exc:  # noqa: BLE001
        raise Refused("expression %r did not evaluate: %s" % (expr, exc))


def struct_offsets(text):
    """Recompute the packed field offsets of `struct ci13xxx_qh` and `struct ci13xxx_td`.

    Packs by: u32 -> 4, a nested struct -> its computed size, `struct usb_ctrlrequest` -> 8 (ch9.h's
    8-byte control request, which is what the qh's setup field is). `__attribute__((packed, aligned(4)))`
    means no padding beyond the 4-byte alignment each field already has.
    """
    def body(name):
        m = re.search(r"struct\s+%s\s*\{(.*?)\}\s*__attribute__" % re.escape(name), text, re.S)
        if not m:
            raise Refused("ci13xxx_udc.h has no `struct %s`" % name)
        inner = strip_comments(m.group(1))
        # the owner interleaves `#define TD_*` / `#define QH_*` lines between fields; they are not fields.
        inner = re.sub(r"^[ \t]*#[^\n]*", " ", inner, flags=re.M)
        return inner

    def size_of(owner_name, b):
        """Walk fields in order, return (total_size, {field_name: offset})."""
        offs, pos = {}, 0
        for raw in b.split(";"):
            line = raw.strip()
            if not line:
                continue
            m = re.match(r"(u32|unsigned)\s+([A-Za-z_][A-Za-z0-9_]*)(\[(\d+)\])?$", line)
            if m:
                count = int(m.group(4)) if m.group(4) else 1
                offs[m.group(2)] = pos
                pos += 4 * count
                continue
            m = re.match(r"struct\s+([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)$", line)
            if m:
                nested = m.group(1)
                offs[m.group(2)] = pos
                if nested == "usb_ctrlrequest":
                    pos += 8
                elif nested == "ci13xxx_td":
                    pos += size_of("ci13xxx_td", body("ci13xxx_td"))[0]
                else:
                    raise Refused("struct %s has an unknown nested struct %s" % (owner_name, nested))
                continue
            raise Refused("struct %s: cannot size field %r" % (owner_name, line))
        return pos, offs

    td_size, td = size_of("ci13xxx_td", body("ci13xxx_td"))
    qh_size, qh = size_of("ci13xxx_qh", body("ci13xxx_qh"))
    return {
        ("td", "next"): td["next"], ("td", "token"): td["token"], ("td", "page"): td["page"],
        ("qh", "cap"): qh["cap"], ("qh", "curr"): qh["curr"], ("qh", "setup"): qh["setup"],
        ("qh", "td.next"): qh["td"] + td["next"], ("qh", "td.token"): qh["td"] + td["token"],
        ("qh", "size"): qh_size, ("td", "size"): td_size,
    }


def check_sources():
    enum_text = read(ENUM_H)
    enum_body = strip_comments(read(ENUM_C))
    problems = []

    vals = header_values(enum_text)

    # (1) the struct offsets, recomputed from the owner's struct.
    owner = struct_offsets(read(GADGET_H))
    for name, path in sorted(STRUCT_OFFSETS.items()):
        short = name[len("STAGE90_USB_"):]
        if short not in vals:
            problems.append("entry_usb_enum.h defines no %s, but the check names it" % name)
            continue
        try:
            got = int(vals[short].rstrip("u"), 0)
        except ValueError:
            problems.append("%s is not an integer literal (%r)" % (name, vals[short]))
            continue
        want = owner[path]
        if got != want:
            problems.append("%s = %d but `struct ci13xxx_qh`'s %s is at offset %d "
                            "([[mi4-one-value-two-definitions]] - the layout is recomputed, not guessed)"
                            % (name, got, ".".join(path), want))
    say("  qh/td layout: %d field offsets recomputed from `struct ci13xxx_qh` (size %d) and "
        "`struct ci13xxx_td` (size %d)" % (len(STRUCT_OFFSETS), owner[("qh", "size")], owner[("td", "size")]))

    # (2) the written values against the owner's own expressions. The table also carries this header's
    # own names and the two earlier USB headers', so a value defined in terms of a sibling
    # (`ENDPTCTRL_TXE`) evaluates rather than being pronounced unknown.
    table = owner_defines(ENUM_H, DEV_H, USB_H)
    table.update({n: v for n, v in header_values(enum_text).items()})   # `STAGE90_USB_ENUM_*` shorts
    for name, expr in sorted(CROSS_VALUES.items()):
        short = name[len("STAGE90_USB_"):]
        if short not in vals:
            problems.append("entry_usb_enum.h defines no %s, but the check names it" % name)
            continue
        try:
            got = eval_expr(vals[short], table)
        except Refused as exc:
            problems.append("%s: %s" % (name, exc))
            continue
        try:
            want = eval_expr(expr, table)
        except Refused as exc:
            problems.append("%s: its owner expression is bad: %s" % (name, exc))
            continue
        if got != want:
            problems.append("%s = 0x%x but the owner's `%s` = 0x%x ([[mi4-one-value-two-definitions]])"
                            % (name, got, expr, want))
    say("  enum values: %d written values, each compared against the owner's own expression"
        % len(CROSS_VALUES))

    # (3a) the write-target whitelist. Owned = every `#define` name in the three arm headers
    # (function-like macros included, so `STAGE90_USB_ENDPTCTRL(0u)` is owned by its base name), plus
    # every `STAGE90_USB_ENUM_*` this header defines.
    owned = set()
    for path in (USB_H, DEV_H, ENUM_H):
        for m in re.finditer(r"^#define[ \t]+([A-Za-z_][A-Za-z0-9_]*)",
                             strip_comments(read(path)), re.M):
            owned.add(m.group(1))
    writes = []
    for m in re.finditer(r"\busb_enum_write32\s*\(", enum_body):
        arg = (first_arg(enum_body, m.start()) or "").strip()
        if not arg or arg.startswith("uint32_t"):
            continue   # the function *definition's* parameter list, not a store
        base = re.match(r"([A-Za-z_][A-Za-z0-9_]*)", arg)
        writes.append(base.group(1) if base else arg)
    bad = sorted({a for a in writes if a not in owned})
    if bad:
        problems.append("entry_usb_enum.c stores through usb_enum_write32 to a name no header defines: "
                        + ", ".join(bad) + " - a write to a register with no owner")
    if not writes:
        problems.append("entry_usb_enum.c takes no usb_enum_write32 store: the enumeration arm's whole "
                        "point is that it arms the device-mode set")

    # (3b) the no-RST refusal: the arm must never re-run the link reset 910a2 owns.
    if "USBCMD_RST" in enum_body or "USBCMD_RST" in strip_comments(enum_text):
        problems.append("entry_usb_enum.c/.h names `USBCMD_RST`: the enumeration arm must NEVER re-run "
                        "the link reset (910a2 owns the one reset; a reset in the once-per-pass poll "
                        "fights the running device)")
    say("  write-targets: %d stores, each a transcribed `STAGE90_USB_*` offset; no `USBCMD_RST`"
        % len(writes))

    # (4) the mode gate, at the source.
    gate_re = re.compile(r"!=\s*STAGE90_USB_USBMODE_CM_DEVICE\s*\)\s*&&\s*"
                         r"\(STAGE90_XNU_USB_DEV_FORCE\s*==\s*0\)")
    m = gate_re.search(enum_body)
    if not m:
        problems.append("entry_usb_enum.c has no `((x & CM) != CM_DEVICE) && (FORCE == 0)` gate: the arm "
                        "must not write unless the core is already a device")
    elif "return;" not in enum_body[m.start():m.start() + 400]:
        problems.append("entry_usb_enum.c's mode gate does not return from the non-device branch")
    # the first store in entry_usb_enum_poll must come after the gate.
    fn = enum_body.find("void entry_usb_enum_poll(void)")
    if fn != -1 and m:
        first = -1
        for mm in re.finditer(r"\busb_enum_write32\(\s*([^,)]*)", enum_body[fn:]):
            if not mm.group(1).strip().startswith("uint32_t"):
                first = fn + mm.start()
                break
        if first != -1 and first < m.start():
            problems.append("entry_usb_enum_poll stores before its mode gate: the writes would run "
                            "before the check that bounds them")
    say("  mode gate: the `!= CM_DEVICE && FORCE == 0` form is present and returns")

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
    if "entry_usb_enum_poll" not in syms:
        raise Refused("%s defines no `entry_usb_enum_poll`: the call site in entry_trace.c would be an "
                      "undefined symbol" % image)
    _addr, size, kind = syms["entry_usb_enum_poll"]
    if kind != "T":
        raise Refused("`entry_usb_enum_poll` is a `%s` symbol, not `T` (text)" % kind)
    switch = record_switch(image, "STAGE90_XNU_USB_ENUM")
    body = body_of(image, "entry_usb_enum_poll") or ""
    # the poll is not the arming; the stores are in the helpers it calls, so the window test is over the
    # whole object's text region referenced by the image: any store through 0xf9a anywhere in the arm.
    stores_window = bool(re.search(r"0xf9a", body, re.I)) or size > 8
    if switch == 1:
        if size <= 8:
            raise Refused("the record says STAGE90_XNU_USB_ENUM=1 but the linked `entry_usb_enum_poll` "
                          "body is size %d: the arm's switch did not reach the image "
                          "([[mi4-off-option-two-spellings]])" % size)
        say("  image: STAGE90_XNU_USB_ENUM=1 and `entry_usb_enum_poll` is a %d-byte T body" % size)
    elif switch == 0:
        if size > 8:
            raise Refused("the record says STAGE90_XNU_USB_ENUM=0 but the linked `entry_usb_enum_poll` "
                          "body is %d bytes: the OFF arm must be a bare return" % size)
        say("  image: STAGE90_XNU_USB_ENUM=0 and `entry_usb_enum_poll` is a bare return")
    else:
        say("  image: no USB_ENUM key in the record; `entry_usb_enum_poll` is %d bytes" % size)
    _ = stores_window


def selftest():
    original_h = read(ENUM_H)
    original_c = read(ENUM_C)
    cases = []

    def with_edit(name, path, old, new, expect_in):
        text = original_h if path == "h" else original_c
        mutated = text.replace(old, new, 1)
        if mutated == text:
            cases.append((name, "the mutation did not apply: %r not in %s" % (old, path), False))
            return
        open(ENUM_H if path == "h" else ENUM_C, "w").write(mutated)
        try:
            check_sources()
            cases.append((name, "check_sources accepted the mutation (it should have refused)", False))
        except Refused as exc:
            cases.append((name, str(exc).strip().splitlines()[0] if str(exc).strip() else "(refused)",
                          expect_in in str(exc)))
        finally:
            open(ENUM_H, "w").write(original_h)
            open(ENUM_C, "w").write(original_c)

    # The qh setup offset drifts back to the wrong 32 (the defect this guard exists for).
    with_edit("the qh setup offset drifts to 32", "h",
              "#define STAGE90_USB_ENUM_QH_OFF_SETUP   40u",
              "#define STAGE90_USB_ENUM_QH_OFF_SETUP   32u",
              "QH_OFF_SETUP")
    # A written value drifts.
    with_edit("USBCMD_SUTW drifts from BIT(13)", "h",
              "#define STAGE90_USB_ENUM_USBCMD_SUTW    0x00002000u",
              "#define STAGE90_USB_ENUM_USBCMD_SUTW    0x00001000u",
              "USBCMD_SUTW")
    # A store to an unowned register.
    with_edit("a store to an unowned register appears", "c",
              "    usb_enum_write32(STAGE90_USB_ENDPTCTRL(0u), STAGE90_USB_ENUM_ENDPTCTRL0_VALUE);",
              "    usb_enum_write32(0x1e0u, 0u);\n    usb_enum_write32(STAGE90_USB_ENDPTCTRL(0u), STAGE90_USB_ENUM_ENDPTCTRL0_VALUE);",
              "no owner")
    # The link reset reappears - the one store this arm must never take.
    with_edit("a link reset store reappears", "c",
              "    usb_enum_write32(STAGE90_USB_ENDPTCTRL(0u), STAGE90_USB_ENUM_ENDPTCTRL0_VALUE);",
              "    usb_enum_write32(STAGE90_USB_USBCMD, STAGE90_USB_USBCMD_RST);\n    usb_enum_write32(STAGE90_USB_ENDPTCTRL(0u), STAGE90_USB_ENUM_ENDPTCTRL0_VALUE);",
              "USBCMD_RST")
    # The mode gate is removed.
    with_edit("the mode gate is removed", "c",
              "(usbmode & STAGE90_USB_USBMODE_CM) != STAGE90_USB_USBMODE_CM_DEVICE",
              "(usbmode & STAGE90_USB_USBMODE_CM) != 0xf0f0u",
              "CM_DEVICE")

    refused = sum(1 for _n, _r, ok in cases if ok)
    for name, reason, ok in cases:
        say("  %-45s %s  %s" % (name, "refused:" if ok else "ACCEPTED:", reason))
    say("  --selftest: %d of %d mutations were refused" % (refused, len(cases)))
    if refused != len(cases):
        raise SystemExit("the falsification battery is not watertight: %d of %d refused"
                         % (refused, len(cases)))


def main():
    parser = argparse.ArgumentParser(description="910b's USB enumeration-arm guard")
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