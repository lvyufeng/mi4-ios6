# Experiment 910b — the enumeration rung, designed

**Status: DESIGN ONLY. No arm is built, no arm is parked, nothing is pressed.** This is the document
§10.6 of `experiment-910-usb-debug-map.md` said was owed before a line is written — the same discipline
§9.6 enforced for the *small* USB arm, applied to the large one. The map (§10) is the fact-base; this is
the *plan* the arm will be built against. Every register value here has its owner named in §10.7/§10.8.

## 0. What 910b is, and what it is not

**Is:** the first rung that answers a control transfer. A host plugging in enumerates the phone, reads its
descriptors, and can read **one IN endpoint**. It is the first *live* channel — data crosses while XNU
runs, rather than being read post-mortem out of TWRP's `/proc/last_kmsg`.

**Is not:** the goal. 「可以通过usb进行调试」 needs 910c (KDP over that endpoint, or a CDC-ACM whose
`printf` reaches a terminal) or 910d (`adbd`). 910b is the rung that makes either possible.

**Precondition:** 910a2 (`b3fbcd31`, §9) must have brought the core to device mode with `USBCMD.RS=1`,
and — the open question §9.5 records — its press must have shown the host *accepting* the D+ pull-up.
If 910a2's press shows no connect, 910b is premature and the PHY/regulators become the rung. **910b
assumes 910a2 landed; it does not replace its press.**

## 1. The shape of the arm

One new file pair, one call site, one switch, guards — the shape 910a2 established:

- **`src/entry/entry_usb_enum.c` + `.h`** — the ISR body, the static qh/dTD/graph, the descriptor tables,
  the request handlers. A third file, not folded into `entry_usb_dev.c`, for the same reason 910a2 got its
  own: **the properties of one arm must be properties of one file.** `entry_usb.c` is provably
  write-nothing; `entry_usb_dev.c` writes only the PHY/mode set; `entry_usb_enum.c` writes the endpoint
  set, primes endpoints, and stores `DEVICEADDR`. Each file's guard proves only its own arm's clause.
- **One call site**, in `__wrap_Idle_load_context`, beside the 910a/910a2 calls (`entry_trace.c:2155/2162`):
  `entry_usb_enum_poll();`. It is the one wrapper every pass reaches on every arm (§9.6), so the poll runs
  once per idle pass. Placed **after** the dev-init call, so the endpoint set is armed only once the core
  is already in device mode — but the file must also refuse to act if it is *not*, the 910a2 hazard rule.
- **Switch `STAGE90_XNU_USB_ENUM` (default 0)**, an ENTRY switch like `USB_PROBE`/`USB_DEV`, so the payload
  record stays byte-identical and the arm's identity is its entry-image bytes.
- **`tools/check_usb_enum.py`** — its own guard (§7), the way each prior USB arm has one.

### 1.1 The poll vs. the ISR — the one architectural decision

The vendor's `udc_irq` is an **interrupt handler**. This image runs no USB interrupt at all: the USB2 core
is not in the entry image's vector table, and enabling a GIC SPI for it would mean the GIC probe growing a
second consumer — a much larger change, and one that could not be tested without the interrupt firing.
So 910b is **polled**: `entry_usb_enum_poll` runs once per idle pass and does *exactly what `udc_irq` does*,
minus the enable/ack:

```
intr = USBSTS & USBINTR;          /* the enabled-and-latched set, §10.1 */
for each set bit, in the vendor's fixed priority (URI, PCI, UEI, UI, SLI):
    run the same handler the vendor runs
USBSTS = intr;                    /* R/WC: write back to clear the latched bits */
```

This is honest only because **`USBSTS` latches**: a transfer that completes between two idle passes is
still set when the next pass reads it, so nothing is lost — the poll is not "sampling", it is "draining a
latched register". The trade is **latency** (up to one idle-pass period per poll) for **no interrupt
wiring**. That is acceptable for enumeration, whose control transfers are host-paced and whose timeouts are
milliseconds; it is *not* obviously acceptable for 910c's bulk throughput, which is a 910c question.

**`USBINTR` is still written `0x147`** (§10.3(a)): not to fire an interrupt, but to make `USBSTS & USBINTR`
the right mask. The vendor's handler set and the vendor's enabled set are the same five bits; a poll that
used all of `USBSTS` would additionally see NAKI (which the vendor never enables, §10.1) and act on a bit
the vendor's own code ignores.

## 2. The static graph (replaces the vendor's dma_pool/kzalloc)

The entry image is identity-mapped (`physBase==virtBase==0x80000000`), so a static, aligned array in
`.bss` **is directly DMA-able by the core** (§10.4). The whole of the vendor's allocation machinery
collapses to:

```c
#define STAGE90_USB_QH_STRIDE  64u          /* hardware indexes EP N at list + N*64 (§10.4) */
#define STAGE90_USB_QH_OUT0    0u           /* ep0out  = index 0                    */
#define STAGE90_USB_QH_IN0     16u          /* ep0in   = index hw_ep_max/2 = 16 (§10.8) */
#define STAGE90_USB_QH_IN1     1u           /* the one enumerable IN endpoint's qh  */

/* 64-byte aligned, so the base and every entry satisfy the hardware stride. */
static uint8_t g_usb_qh[STAGE90_USB_QH_STRIDE * 32] __attribute__((aligned(64)));
static uint8_t g_usb_td[STAGE90_USB_TD_STRIDE * 8]   __attribute__((aligned(64)));
static uint8_t g_usb_buf[4096]                        __attribute__((aligned(4096)));
```

- `ENDPOINTLISTADDR ← physical(=virtual, identity) address of `g_usb_qh`** (§10.3(b)).
- Every dTD the CPU links in is a `g_usb_td` slot, 64-byte aligned; every buffer is page-aligned in
  `g_usb_buf`, so each `page[i]`'s low-12-bit offset is well-defined (§10.4).
- **Cache maintenance is explicit and directional**, and it is the part a first attempt gets wrong:
  - **CPU → core** (the qh/dTD the CPU writes before priming): **`CleanPoU_DcacheRegion(va, len)`** —
    *clean*, so the core (which bypasses the cache) sees DRAM. `CleanPoU_DcacheRegion` is the symbol this
    image already links (`osfmk/arm/caches.c` uses it around page-table edits).
  - **core → CPU** (a dTD whose `token` the core updated, or the SETUP bytes it wrote): **invalidate** —
    the core's write is to DRAM, and a stale cached line would shadow it. `FlushPoU_Dcache()` (clean+invalidate,
    already linked) is the sufficient hammer for the first arm; a targeted `invalidate` is a later refinement.
  - The vendor's `wmb()`/`mb()` ordering (`§10.4`) stays: clean-then-barrier-then-prime.

The vendor's `ci13xxx_req` **list machinery collapses to one request per endpoint** (§10.6): a minimal port
needs at most one outstanding control transfer and one IN transfer, so `_ep_nuke`/`ep_dequeue`/the list walk
in `isr_tr_complete_low` are replaced by "reset the two qh/td slots".

## 3. The EP0 state machine — the irreducible path

A faithful reduction of §10.2, in five steps per transfer:

1. **The core has already done step 0**: it DMA'd the 8 SETUP bytes into `qh[OUT0].setup` and raised
   `ENDPTSETUPSTAT.bit0`. The poll sees `USBi_UI` set.
2. **Read SETUP under the guard** — `USBCMD.SUTW=1`, invalidate the qh line, `memcpy` 8 bytes, clear
   `SUTW`, and re-read if the core cleared `SUTW` mid-read (the vendor's retry loop, `:2706`). Clear
   `ENDPTSETUPSTAT.bit0`. Nuke any stale EP0 dTDs first (`_ep_nuke(&ep0out); _ep_nuke(&ep0in);`).
3. **Decode** `bRequest` into one of the standard requests and produce the response:
   - `GET_DESCRIPTOR` (device / config / string) → a `memcpy` out of a static table, `wLength`-clamped.
   - `SET_ADDRESS` → `DEVICEADDR = (v<<25)|USBADRA` (§10.2) and a zero-length status.
   - `SET_CONFIGURATION` → set a `g_configured` flag; the real work (arming the IN endpoint) is accepting
     the request and priming.
   - `GET_STATUS` → 2 bytes; device status is `0x0001` (self-powered) is *not* honest for this port, so
     `0x0000` unless the arm chooses to lie; endpoint status is the halt bit (0).
   - `CLEAR_FEATURE`/`SET_FEATURE` → endpoint halt is a no-op on this minimal port (§10.6 item 10).
4. **Data stage** — a request on the half the direction names (`ep0in` for IN, `ep0out` for OUT), dTD's
   `page[0]` = the response buffer, `token = (len<<16)|ACTIVE|IOC`, clean, prime `ENDPTPRIME=BIT(16)` (IN)
   or `BIT(0)` (OUT).
5. **Status stage** — a zero-length request on the **opposite** half (§10.2), primed the same way.

**Completion** is the poll finding `ENDPTCOMPLETE(bit)`; it clears the bit (R/WC), invalidates the dTD,
reads `actual = length - (token>>16 & 0x7FFF)`, and advances the state. The vendor's "HW has not updated
the dTD status yet" retry (`:2578`, 10 µs `udelay`) is kept for EP0.

**The bus-reset handler** runs on `USBi_URI` and is mandatory (§10.3(c)): `DEVICEADDR←0`, `ENDPTFLUSH←~0`
drain, `ENDPTSETUPSTAT←0`, `ENDPTCOMPLETE←0`, `ENDPTPRIME→0` drain. **A host re-enumerates every time it
is plugged in; skipping this wedges EP0 on the second plug, which would read as "the port worked once".**

## 4. The one IN endpoint

- **`ENDPTCTRL(1)` high half** ← `TXE|TXR|TXT_BULK` = `0x00c80000` (§10.5), then `mb()`; the low half stays
  0 (no OUT). EP1's qh is index 1 in the same 32-entry array (§10.8 — RX and TX are the two halves of one
  word, so IN is the *high* half, not a separate register).
- **`qh[1].cap`** = `QH_ZLT(bit29) | (maxpacket<<16)`. `maxpacket` = **64**, matching `bMaxPacketSize0`
  (the arm runs **full-speed**, §6) — the vendor's HS value 512 (§10.8) is not reachable without a
  high-speed PHY handshake this image does not yet do.
- **The IN dTD** names `g_usb_buf`; the arm publishes a small, fixed payload (the arm's own identity —
  e.g. a magic word + the arm's key set) so a host read is self-describing.
- **Priming** writes `ENDPTPRIME = BIT(1+16) = BIT(17)` (§10.4). After completion the arm re-primes with a
  fresh payload, or leaves it unprimed and let the host NAK until 910c makes it a stream.

## 5. The descriptor tables

Transcribed from §10.8, with a **deliberate divergence** where the vendor's values are board/runtime
decided and would be a lie here:

| field | vendor | this arm | why |
|---|---|---|---|
| `idVendor` | `0x18d1` default, board `05c6`/`18d1` at runtime | **`0x18d1`** | Google/Android id; stable, and what a host's adb/driver matches |
| `idProduct` | `0x0001` default, `d00d`/`901d` at runtime | **a value unique to this arm** (final: build-fixed) | so `lsusb` proves *this* image enumerated, not a leftover Android |
| `bcdDevice` | `0x0200+gcnum` | **`0x0910`** (lit: "910") | self-documenting; `gcnum` is a Linux gadget concept this port has no notion of |
| `iSerialNumber` | literal `"0123456789ABCDEF"` (§10.8) | **hard-coded `"4a2fe00b"`** | the vendor only gets the CID because a userspace script writes sysfs; a bare port must hard-code it, and the phone's serial is a *fact about this device* |
| interface class | `0xFF/0x42/1` (f_adb) | **`0xFF/0x42/1`** | the ADB signature — kept so 910d's `adbd` is a step from here, not a redesign |
| endpoints | 2 bulk, 512 HS | **1 bulk IN, 64 FS** | §4 | 

Strings: `iManufacturer`="mi4-xnu", `iProduct`="xnu-arm-entry", both short; `iConfiguration` = 0. The device
descriptor is 18 B, config 9 B + interface 9 B + endpoint 7 B. `bMaxPacketSize0 = 64` (`CTRL_PAYLOAD_MAX`,
§10.8) — *not* a field of the device struct in source, but it *is* a byte in the 18-byte descriptor, and it
must equal the EP0 qh's `maxpacket` (§3) or the host's first control read mis-sizes.

## 6. What the press must show — the falsifiable reading

The arm publishes, once per state change (not once per poll — the channel is finite):

- `xnu_live_usb_enum_state` — the last state reached (a small enum: idle / armed / got-setup /
  got-descriptor / addressed / configured).
- `xnu_live_usb_enum_setup` — the last 8 SETUP bytes, packed: `bRequestType|bRequest|wValue|wIndex|wLength`.
- `xnu_live_usb_enum_requests` — a count, and `xnu_live_usb_enum_resets` — the URI count.
- `xnu_live_usb_enum_addr` — the address the host assigned (proves `SET_ADDRESS` landed).
- `xnu_live_usb_enum_in_reads` / `xnu_live_usb_enum_in_bytes` — IN-transfer completions, iff the host
  actually read the endpoint.

**Falsifications, named:**
- **`resets=0` and `state=armed` forever** → the host never reset the port: the D+ pull-up is not being
  seen → a PHY/regulator problem, i.e. 910a2 did not truly land. **910b's failure indicts 910a2, and that
  is the point of a ladder.**
- **`resets>0` but `state` never leaves `got-setup`** → the host reset (so enumeration started) but EP0
  does not answer: the qh/dTD or the cache direction is wrong.
- **`state=configured` and the host's `lsusb` shows the device** → the rung WORKED, measured from *outside*
  the phone, which is a stronger reading than any in-image key.
- **The phone dark with no adbd after the press** is the expected resident reading (909), not a hang — the
  escape and the wait are `scripts/press_909_normal.sh`'s, unchanged.

## 7. The guard, and the build refusals

`tools/check_usb_enum.py`, in `make check` and `build_entry.sh`, each clause the shape its siblings use:

1. **Offsets vs. the Android owner** — every register the file writes, cross-checked against
   `ci13xxx_udc.c`/`ci13xxx_udc.h`/`msm_hsusb_hw.h`, both ways (`[[mi4-one-value-two-definitions]]`).
2. **The switch in the linked image, both ways** — ON the poll is called from `__wrap_Idle_load_context`
   and nowhere else; OFF it is absent (the 910a2 refusal clause, extended to the third probe).
3. **No `USBCMD.RST` in this file** — 910a2 owns the link reset; a re-reset in the poll would fight the
   running device. A source-level refusal, the 9100-series way.
4. **`ENDPOINTLISTADDR` is only ever written the once, from `&g_usb_qh`** — a second writer with a
   different base is the classic re-arming bug.
5. **The static graph is 64-byte aligned** — a `_Static_assert` on the array alignment, so a stride error
   stops the build rather than mis-indexing a qh the core reads (§2).

## 8. Scope, size, and what is explicitly deferred

**This is the largest rung so far** in the USB series — a from-scratch EP0 control-transfer state machine,
one endpoint, a poll loop, three descriptor tables, and a static DMA graph, against a driver written for
Linux's request/dma_pool/workqueue world. It will very likely need **more than one arm**: a first arm that
arms EP0 and answers `GET_DESCRIPTOR(device)` only (the *smallest* thing a host will accept), then a second
that completes the config/string set and the IN endpoint. **The map supports that split**; this document
designs the whole, and the *first* arm should be the smallest slice, exactly as 910a was a read before 910a2
a write.

**Deferred, explicitly:** KDP/CDC-ACM (910c), `adbd` (910d), high-speed negotiation, bulk OUT, multiple
configurations, suspend/resume, the interrupt-driven (non-polled) path, and any regulator (`HSUSB_1p8/3p3/
VDDCX`) work — that last one is 910a2's open question and not 910b's to close.

**Not deferred, because it is not optional:** the bus-reset re-init (§3) and the cache-direction discipline
(§2). Both are the kind of thing that works in a first press and wedges in a second — the failure mode this
project has been bitten by before.