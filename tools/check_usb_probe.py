#!/usr/bin/env python3
"""910a's guard: the USB2 OTG probe's transcribed offsets, its write-nothing property, and whether the
arm's switch reached the linked image.

Three questions, and the first is the one this project has paid for most often.

1. **ONE VALUE, TWO DEFINITIONS ([[mi4-one-value-two-definitions]]).** `entry_usb.h` cannot include the
   device's Linux headers, so every register offset `entry_usb.c` reads is transcribed, and a
   transcribed offset is this repository's most repeated defect. The value's *owner* is the Android
   checkout at `external/android_kernel_xiaomi_cancro`, whose `include/linux/usb/msm_hsusb_hw.h` and
   `arch/arm/mach-msm/include/mach/msm_hsusb_hw.h` spell each offset as `MSM_USB_BASE + N`. This check
   compares the two in both directions: every offset this header defines must appear in one of those
   files with the same value, and the base must be the one `msm8974.dtsi` gives the `qcom,hsusb-otg`
   node. A header that drifted from the source that owns it is refused here rather than printing a
   reading of the wrong register.

2. **THE WRITE-NOTHING PROPERTY, AT THE SOURCE.** The whole arm is a READ - the port may be live with
   the host - so `entry_usb.c` must define no write accessor and take no store through the USB window.
   The check is on comment-stripped text: a store that exists only in prose is not a store, and a
   promise that exists only in prose is not a check ([[mi4-a-claim-in-a-comment-is-not-a-check]]).

3. **THE SWITCH REACHED THE IMAGE, IN BOTH DIRECTIONS.** Read `STAGE90_XNU_USB_PROBE` out of
   `xnu_arm_entry-config.txt` (the arm's own record) and the linked `xnu_arm_entry.elf`. With the
   switch 1 the probe's body must be non-empty and take its loads from the OTG window; with it 0 the
   body must be a bare return. An image whose record says the arm is on and whose body is empty is
   `mi4-off-option-two-spellings`' defect one build further out.
"""
import argparse
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(HERE)

USB_H = os.path.join(REPO_ROOT, "src/entry/entry_usb.h")
USB_C = os.path.join(REPO_ROOT, "src/entry/entry_usb.c")
ANDROID = os.path.join(REPO_ROOT, "external/android_kernel_xiaomi_cancro")
LINUX_HW_H = os.path.join(ANDROID, "include/linux/usb/msm_hsusb_hw.h")
MACH_HW_H = os.path.join(ANDROID, "arch/arm/mach-msm/include/mach/msm_hsusb_hw.h")
DTSI = os.path.join(ANDROID, "arch/arm/boot/dts/msm8974.dtsi")

NM = "arm-none-eabi-nm"
OBJDUMP = "arm-none-eabi-objdump"

# The header's name -> the Android header's name, so the comparison is of VALUES and not of spellings.
# Every entry is a register `entry_usb.c` actually reads; a name here with no counterpart in the
# Android source is a transcription with no owner, which is itself the defect this check exists for.
CROSS = {
    "STAGE90_USB_ID_CAP":            "USB_ID",
    "STAGE90_USB_HWGENERAL":         "USB_HWGENERAL",
    "STAGE90_USB_HWHOST":            "USB_HWHOST",
    "STAGE90_USB_HWDEVICE":          "USB_HWDEVICE",
    "STAGE90_USB_HWTXBUF":           "USB_HWTXBUF",
    "STAGE90_USB_HWRXBUF":           "USB_HWRXBUF",
    "STAGE90_USB_SBUSCFG":           "USB_SBUSCFG",
    "STAGE90_USB_AHBMODE":           "USB_AHB_MODE",
    "STAGE90_USB_GENCONFIG":         "USB_GEN_CONFIG",
    "STAGE90_USB_CAPLENGTH_HCIVERSION": "USB_CAPLENGTH",
    "STAGE90_USB_HCCPARAMS":         "USB_HCCPARAMS",
    "STAGE90_USB_DCIVERSION":        "USB_DCIVERSION",
    "STAGE90_USB_USBCMD":            "USB_USBCMD",
    "STAGE90_USB_USBSTS":            "USB_USBSTS",
    "STAGE90_USB_USBINTR":           "USB_USBINTR",
    "STAGE90_USB_FRINDEX":           "USB_FRINDEX",
    "STAGE90_USB_DEVICEADDR":        "USB_DEVICEADDR",
    "STAGE90_USB_ENDPOINTLISTADDR":  "USB_ENDPOINTLISTADDR",
    "STAGE90_USB_BURSTSIZE":         "USB_BURSTSIZE",
    "STAGE90_USB_ULPI_VIEWPORT":     "USB_ULPI_VIEWPORT",
    "STAGE90_USB_ENDPTNAK":          "USB_ENDPTNAK",
    "STAGE90_USB_ENDPTNAKEN":        "USB_ENDPTNAKEN",
    "STAGE90_USB_PORTSC":            "USB_PORTSC",
    "STAGE90_USB_OTGSC":             "USB_OTGSC",
    "STAGE90_USB_USBMODE":           "USB_USBMODE",
    "STAGE90_USB_ENDPTSETUPSTAT":    "USB_ENDPTSETUPSTAT",
    "STAGE90_USB_ENDPTPRIME":        "USB_ENDPTPRIME",
    "STAGE90_USB_ENDPTFLUSH":        "USB_ENDPTFLUSH",
    "STAGE90_USB_ENDPTSTAT":         "USB_ENDPTSTAT",
    "STAGE90_USB_ENDPTCOMPLETE":     "USB_ENDPTCOMPLETE",
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


def header_offsets(text):
    """`#define STAGE90_USB_<NAME> 0xNNNu` -> {NAME: value}. The `[ \\t]+` discipline: a `\\s+` can
    cross a newline and read the next `#define` as the value (482's lesson)."""
    out = {}
    for match in re.finditer(r"^#define[ \t]+STAGE90_USB_([A-Z0-9_]+)[ \t]+(0x[0-9a-fA-F]+)u?[ \t]*$",
                             strip_comments(text), re.M):
        out[match.group(1)] = int(match.group(2), 0)
    return out


def android_offsets(text):
    """`#define USB_<NAME> (MSM_USB_BASE + 0xNNN)` -> {NAME: value}. Only the plain form; the
    endpoint-control macro and the bit fields are not register *offsets* and are handled elsewhere."""
    out = {}
    for match in re.finditer(r"^#define[ \t]+USB_([A-Z0-9_]+)[ \t]+\(MSM_USB_BASE[ \t]*\+[ \t]*(0x[0-9a-fA-F]+)\)",
                             strip_comments(text), re.M):
        out[match.group(1)] = int(match.group(2), 0)
    return out


def dtsi_usb_base(text):
    """The OTG node's base, from the device tree that owns it. The `usb_otg: usb@<base>` label and the
    `qcom,hsusb-otg` compatible are two readings of one node; both are required so a node renamed out
    from under the label is a refusal rather than a match against the wrong address."""
    match = re.search(r"usb_otg:\s*usb@([0-9a-fA-F]+)\s*\{(.*?)\n\t\};", text, re.S)
    if not match:
        raise Refused("msm8974.dtsi carries no `usb_otg: usb@<base> { … };` node")
    if "qcom,hsusb-otg" not in match.group(2):
        raise Refused("the `usb_otg:` node is not `qcom,hsusb-otg` - the OTG core's compatible moved")
    return int(match.group(1), 16)


def check_sources():
    """(1) and (2): the offsets against their owner, and the write-nothing property."""
    header = header_offsets(read(USB_H))
    body = strip_comments(read(USB_C))
    android = {}
    for path in (LINUX_HW_H, MACH_HW_H):
        android.update(android_offsets(read(path)))
    problems = []

    for name, a_name in sorted(CROSS.items()):
        short = name[len("STAGE90_USB_"):]
        a_short = a_name[len("USB_"):]
        if short not in header:
            problems.append("entry_usb.h defines no STAGE90_USB_%s, but the check names it" % short)
            continue
        if a_short not in android:
            problems.append("the Android headers spell no %s, so STAGE90_USB_%s has no owner"
                            % (a_name, short))
            continue
        if header[short] != android[a_short]:
            problems.append("STAGE90_USB_%s = 0x%x but %s = 0x%x ([[mi4-one-value-two-definitions]])"
                            % (short, header[short], a_name, android[a_short]))
    say("  usb offsets: %d transcribed, each compared against the Android header that owns it"
        % len(CROSS))

    base = header.get("OTG_BASE")
    if base is None:
        problems.append("entry_usb.h defines no STAGE90_USB_OTG_BASE")
    else:
        dwt_base = dtsi_usb_base(read(DTSI))
        if base != dwt_base:
            problems.append("STAGE90_USB_OTG_BASE = 0x%x but msm8974.dtsi's usb_otg node is 0x%x"
                            % (base, dwt_base))
        else:
            say("  usb base: 0x%x, from msm8974.dtsi's own `qcom,hsusb-otg` node" % base)

    # (2) the write-nothing property, on comment-stripped source.
    if re.search(r"\busb_(ss_)?write32\b", body):
        problems.append("entry_usb.c defines or uses a `usb_write32`: the probe's whole point is that "
                        "it writes NOTHING to a port that may be live with the host")
    # A store through the window would be a `*(volatile …) = …`. In this file every `*(volatile …)`
    # read is the `return` of `usb_read32`/`usb_ss_read32`, so a `*(volatile …)` on any line that does
    # NOT start with `return` is a store - the exact rule, where a character-class regex missed the
    # cast chain `*(volatile uint32_t *)(uintptr_t)(…) = …` (the mutation battery caught that).
    for line in body.splitlines():
        stripped = line.strip()
        if "*(volatile" in stripped and not stripped.startswith("return"):
            problems.append("entry_usb.c takes a store through a volatile pointer (%r): the probe must "
                            "only read" % stripped[:60])
            break
    say("  write-nothing: entry_usb.c defines no write accessor and no volatile store")

    if problems:
        raise Refused("\n".join("  - " + p for p in problems))


def record_switch(image_path):
    """Read `STAGE90_XNU_USB_PROBE` out of the arm's own record, which sits beside the ELF. Absent
    record or absent key is not an error here: a build older than this switch simply has no key, and
    the image check below still runs on whatever the ELF says."""
    record = os.path.join(os.path.dirname(image_path), "xnu_arm_entry-config.txt")
    if not os.path.isfile(record):
        return None
    match = re.search(r"^STAGE90_XNU_USB_PROBE=(\d+)$", read(record), re.M)
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
    """(3): the switch reached the linked image, in both directions."""
    syms = nm_sizes(image)
    if "entry_usb_probe" not in syms:
        raise Refused("%s defines no `entry_usb_probe`: the call site in entry_trace.c would be an "
                      "undefined symbol" % image)
    _addr, size, kind = syms["entry_usb_probe"]
    if kind != "T":
        raise Refused("`entry_usb_probe` is a `%s` symbol, not `T` (text): the probe's body did not "
                      "link into the image" % kind)
    body = body_of(image, "entry_usb_probe") or ""
    switch = record_switch(image)
    # The base's high halfword, 0xf9a5 - what a body reading this window must materialize. A bare
    # `movw rX, #0x5000` / `movt rX, #0xf9a5` pair, or a literal pool entry, both show 0xf9a5.
    reads_window = bool(re.search(r"0xf9a5", body, re.I))
    if switch == 1:
        if size <= 8 or not reads_window:
            raise Refused("the record says STAGE90_XNU_USB_PROBE=1 but the linked `entry_usb_probe` "
                          "body is size %d with no load from the 0xf9a5 window: the arm's switch did "
                          "not reach the image - an image whose record claims the arm and whose body "
                          "is empty is the defect one build further out "
                          "([[mi4-off-option-two-spellings]])" % size)
        say("  image: STAGE90_XNU_USB_PROBE=1 and `entry_usb_probe` is a %d-byte T body that reads the "
            "0xf9a5 window" % size)
    elif switch == 0:
        if size > 8:
            raise Refused("the record says STAGE90_XNU_USB_PROBE=0 but the linked `entry_usb_probe` "
                          "body is %d bytes: the OFF arm must be a bare return" % size)
        say("  image: STAGE90_XNU_USB_PROBE=0 and `entry_usb_probe` is a bare return")
    else:
        say("  image: no USB_PROBE key in the record; `entry_usb_probe` is %d bytes" % size)


# ------------------------------------------------------------------------------------------------
# The falsification battery: mutate one property at a time and require the check to refuse it.
# ------------------------------------------------------------------------------------------------

def selftest():
    import tempfile
    global USB_H, USB_C
    original_h = read(USB_H)
    original_c = read(USB_C)
    cases = []

    def with_edit(name, path, old, new, expect_in):
        text = original_h if path == "h" else original_c
        mutated = text.replace(old, new, 1)
        if mutated == text:
            cases.append((name, "the mutation did not apply: %r not in %s" % (old, path), False))
            return
        if path == "h":
            open(USB_H, "w").write(mutated)
        else:
            open(USB_C, "w").write(mutated)
        try:
            check_sources()
            cases.append((name, "check_sources accepted the mutation (it should have refused)", False))
        except Refused as exc:
            hit = expect_in in str(exc)
            cases.append((name, str(exc).strip().splitlines()[0] if str(exc).strip() else "(refused)",
                          hit))
        finally:
            open(USB_H, "w").write(original_h)
            open(USB_C, "w").write(original_c)

    # The offset of one register moved: the cross-check must catch it against the Android header.
    with_edit("an offset drifts from the Android header", "h",
              "#define STAGE90_USB_USBCMD      0x140u", "#define STAGE90_USB_USBCMD      0x144u",
              "USB_USBCMD")
    # The base moved away from the device tree's node.
    with_edit("the base drifts from the device tree", "h",
              "#define STAGE90_USB_OTG_BASE    0xf9a55000u", "#define STAGE90_USB_OTG_BASE    0xf9a56000u",
              "msm8974.dtsi")
    # A write accessor appears: the write-nothing property must refuse it.
    with_edit("a write accessor appears", "c",
              "static uint32_t usb_read32(uint32_t off)",
              "static void usb_write32(uint32_t off, uint32_t v) { *(volatile uint32_t *)(uintptr_t)(STAGE90_USB_OTG_BASE + off) = v; }\nstatic uint32_t usb_read32(uint32_t off)",
              "write")
    # A store through the window appears in the probe body.
    with_edit("a volatile store appears", "c",
              "    id_cap = usb_read32(STAGE90_USB_ID_CAP);",
              "    *(volatile uint32_t *)(uintptr_t)(STAGE90_USB_OTG_BASE + STAGE90_USB_USBCMD) = 0u;\n"
              "    id_cap = usb_read32(STAGE90_USB_ID_CAP);",
              "store")

    refused = sum(1 for _n, _r, ok in cases if ok)
    for name, reason, ok in cases:
        say("  %-45s %s  %s" % (name, "refused:" if ok else "ACCEPTED:", reason))
    say("  --selftest: %d of %d mutations were refused" % (refused, len(cases)))
    if refused != len(cases):
        raise SystemExit("the falsification battery is not watertight: %d of %d refused"
                         % (refused, len(cases)))


def main():
    parser = argparse.ArgumentParser(description="910a's USB-probe guard")
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