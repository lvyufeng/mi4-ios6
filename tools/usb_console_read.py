#!/usr/bin/env python3
"""usb_console_read.py - read the D13 payload's RAM console from EP1-IN over USB.

*** THE HOST SIDE OF THE LADDER (959-962). *** The D13 USB ladder (probe -> device -> enum ->
stream) hands the payload's RAM console byte-for-byte to bulk endpoint **EP1-IN (0x81)** of a device
with VID:PID `0x18d1:0x0910` (`src/entry/entry_usb_enum.h:174-175`). The ladder code was built and
validated host-side, but the **repo had no host reader** - so a press of a ladder arm (or of any
**resident** arm, which streams this way) went dark with nothing reading the bytes. This is that
reader. It is the goal's 「可以通过 usb 进行调试」 in its host form.

Why this reader matters beyond the ladder: a **resident** arm has **no ending by construction**
(`build_entry.sh:1094-1103` refuses POST_END_* when STAGE90_XNU_RESIDENT=1), so nothing preserves its
RAM-console record and its log survives at most one boot. Streaming over USB is the only way to see a
resident run **live** - no `/proc/last_kmsg`, no recovery window, no losing the log ([[mi4-975...]]).

It is READ-ONLY on the host: open device, claim interface, `libusb_bulk_transfer` in a loop, write to
the capture file and (optionally) stdout. It writes nothing to the device. Not a press.

Usage:
  tools/usb_console_read.py [--out FILE] [--seconds N] [--stdout] [--vid 0x18d1] [--pid 0x0910]

  --out FILE      capture file (default out/stage90/captures/usb-console-<stamp>.txt; '-' = discard)
  --seconds N     stop after N seconds with no new bytes (default 120; 0 = run until Ctrl-C)
  --wait N        wait up to N seconds for the device to appear before giving up (default 0).
                  Start the reader with `--wait 180 --seconds 0`, THEN press: the reader attaches when
                  the payload enumerates and streams until Ctrl-C.
  --stdout        also echo bytes to stdout as they arrive

Exit: 0 = bytes were read; 2 = the device was never seen (nothing read); 3 = device seen but the
endpoint never delivered (claimed OK, all transfers timed out) - distinct, so "not there" and "there
but silent" are not the same reading ([[mi4-silence-is-a-reading-only-if-success-is-silent]]).
"""
import argparse
import ctypes
import os
import sys
import time

# ---- libusb-1.0 via ctypes (no pyusb; libusb-1.0.so.0 is present, headers are not) ----------------

EP_IN = 0x81          # EP1-IN, the bulk endpoint the stream primes (entry_usb_enum.h:68-70)
TIMEOUT_MS = 500      # per bulk transfer; a NAK just returns rc=TIMEOUT and we loop


class _Endpoint(ctypes.Structure):
    _fields_ = [("bLength", ctypes.c_uint8), ("bDescriptorType", ctypes.c_uint8),
                ("bEndpointAddress", ctypes.c_uint8), ("bmAttributes", ctypes.c_uint8),
                ("wMaxPacketSize", ctypes.c_uint16), ("bInterval", ctypes.c_uint8),
                ("bRefresh", ctypes.c_uint8), ("bSynchAddress", ctypes.c_uint8),
                ("extra", ctypes.c_void_p), ("extra_length", ctypes.c_int)]


class _InterfaceDesc(ctypes.Structure):
    _fields_ = [("bLength", ctypes.c_uint8), ("bDescriptorType", ctypes.c_uint8),
                ("bInterfaceNumber", ctypes.c_uint8), ("bAlternateSetting", ctypes.c_uint8),
                ("bNumEndpoints", ctypes.c_uint8), ("bInterfaceClass", ctypes.c_uint8),
                ("bInterfaceSubClass", ctypes.c_uint8), ("bInterfaceProtocol", ctypes.c_uint8),
                ("iInterface", ctypes.c_uint8), ("endpoint", ctypes.POINTER(_Endpoint))]


class _Interface(ctypes.Structure):
    _fields_ = [("altsetting", ctypes.POINTER(_InterfaceDesc)), ("num_altsetting", ctypes.c_int)]


class _ConfigDesc(ctypes.Structure):
    _fields_ = [("bLength", ctypes.c_uint8), ("bDescriptorType", ctypes.c_uint8),
                ("wTotalLength", ctypes.c_uint16), ("bNumInterfaces", ctypes.c_uint8),
                ("bConfigurationValue", ctypes.c_uint8), ("iConfiguration", ctypes.c_uint8),
                ("bmAttributes", ctypes.c_uint8), ("MaxPower", ctypes.c_uint8),
                ("interface", ctypes.POINTER(_Interface))]


def load_libusb():
    for name in ("libusb-1.0.so.0", "libusb-1.0.so"):
        try:
            lib = ctypes.CDLL(name)
            break
        except OSError:
            lib = None
    if lib is None:
        sys.stderr.write("usb_console_read: cannot load libusb-1.0 (install libusb-1.0-0)\n")
        return None
    lib.libusb_init.argtypes = [ctypes.POINTER(ctypes.c_void_p)]
    lib.libusb_init.restype = ctypes.c_int
    lib.libusb_open_device_with_vid_pid.argtypes = [ctypes.c_void_p, ctypes.c_uint16, ctypes.c_uint16]
    lib.libusb_open_device_with_vid_pid.restype = ctypes.c_void_p
    lib.libusb_set_auto_detach_kernel_driver.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.libusb_claim_interface.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.libusb_claim_interface.restype = ctypes.c_int
    lib.libusb_release_interface.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.libusb_get_active_config_descriptor.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(_ConfigDesc))]
    lib.libusb_get_active_config_descriptor.restype = ctypes.c_int
    lib.libusb_free_config_descriptor.argtypes = [ctypes.POINTER(_ConfigDesc)]
    lib.libusb_bulk_transfer.argtypes = [ctypes.c_void_p, ctypes.c_ubyte, ctypes.c_char_p,
                                         ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.c_uint]
    lib.libusb_bulk_transfer.restype = ctypes.c_int
    lib.libusb_close.argtypes = [ctypes.c_void_p]
    lib.libusb_exit.argtypes = [ctypes.c_void_p]
    return lib


def find_bulk_interface(lib, handle):
    """Return bInterfaceNumber of the interface that carries EP1-IN, else 0."""
    cfg = ctypes.POINTER(_ConfigDesc)()
    if lib.libusb_get_active_config_descriptor(handle, ctypes.byref(cfg)) != 0:
        return 0
    try:
        c = cfg.contents
        for i in range(c.bNumInterfaces):
            iface = c.interface[i]
            if iface.num_altsetting <= 0:
                continue
            alt = iface.altsetting[0]
            for e in range(alt.bNumEndpoints):
                if alt.endpoint[e].bEndpointAddress == EP_IN:
                    return alt.bInterfaceNumber
    finally:
        lib.libusb_free_config_descriptor(cfg)
    return 0


def now_stamp():
    return time.strftime("%Y%m%d-%H%M%S", time.gmtime())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=None)
    ap.add_argument("--seconds", type=int, default=120)
    ap.add_argument("--wait", type=int, default=0)
    ap.add_argument("--stdout", action="store_true")
    ap.add_argument("--vid", type=lambda s: int(s, 0), default=0x18d1)
    ap.add_argument("--pid", type=lambda s: int(s, 0), default=0x0910)
    a = ap.parse_args()

    lib = load_libusb()
    if lib is None:
        return 2
    ctx = ctypes.c_void_p()
    if lib.libusb_init(ctypes.byref(ctx)) != 0:
        sys.stderr.write("usb_console_read: libusb_init failed\n")
        return 2

    handle = lib.libusb_open_device_with_vid_pid(ctx, a.vid, a.pid)
    if not handle and a.wait > 0:
        deadline = time.time() + a.wait
        sys.stderr.write("usb_console_read: waiting up to %ds for %04x:%04x to appear (press now)...\n"
                         % (a.wait, a.vid, a.pid))
        while not handle and time.time() < deadline:
            time.sleep(0.5)
            handle = lib.libusb_open_device_with_vid_pid(ctx, a.vid, a.pid)
    if not handle:
        sys.stderr.write("usb_console_read: no device %04x:%04x - is the ladder arm running "
                         "(device mode) and the phone connected?\n" % (a.vid, a.pid))
        lib.libusb_exit(ctx)
        return 2

    lib.libusb_set_auto_detach_kernel_driver(handle, 1)
    ifn = find_bulk_interface(lib, handle)
    rc = lib.libusb_claim_interface(handle, ifn)
    if rc != 0:
        sys.stderr.write("usb_console_read: claim interface %d rc=%d (try unplugging adb / the "
                         "kernel driver)\n" % (ifn, rc))

    out = a.out
    if out is None:
        out = os.path.join("out", "stage90", "captures", "usb-console-%s.txt" % now_stamp())
    fh = None
    if out != "-":
        os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
        fh = open(out, "ab", buffering=0)

    buf = ctypes.create_string_buffer(4096)
    got = 0
    last = time.time()
    sys.stderr.write("usb_console_read: reading EP1-IN (0x%02x) if=%d vid:pid=%04x:%04x -> %s\n"
                     % (EP_IN, ifn, a.vid, a.pid, out))
    try:
        while True:
            n = ctypes.c_int(0)
            r = lib.libusb_bulk_transfer(handle, EP_IN, buf, len(buf), ctypes.byref(n), TIMEOUT_MS)
            if n.value > 0:
                data = buf.raw[:n.value]
                got += n.value
                last = time.time()
                if fh:
                    fh.write(data)
                if a.stdout:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
            else:
                if a.seconds and (time.time() - last) > a.seconds:
                    break
    except KeyboardInterrupt:
        pass
    finally:
        if fh:
            fh.close()
        lib.libusb_release_interface(handle, ifn)
        lib.libusb_close(handle)
        lib.libusb_exit(ctx)

    sys.stderr.write("usb_console_read: read %d bytes -> %s\n" % (got, out))
    if got == 0:
        sys.stderr.write("usb_console_read: the device was seen and claimed but EP1-IN delivered "
                         "nothing in the window (the stream may not be priming yet, or the ladder "
                         "did not reach the idle path).\n")
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())