# Experiment 910c — the bulk stream, mapped (reconnaissance; no arm built)

**Rung 910c is the one after 910b.** 910a/910a2 brought the USB2 OTG core up and 910b (arm `880a3568`)
answers EP0 control requests — a host can *enumerate* the device. 910c is the next thing a debugger needs:
**a bulk data stream** the host can read (and, later, write), so XNU becomes debuggable over USB rather than
merely enumerable.

This document is the reconnaissance. It is a *map*, not a design and not an arm. Every claim carries the
`file:line` of the Android source that owns it (the same owner `entry_usb.h` and `entry_usb_dev.h` name:
`external/android_kernel_xiaomi_cancro`). Vendor constructs the project's identity-mapped static-graph port
replaces are marked **REPLACED**.

---

## 1. The bulk-OUT path (`ci13xxx_udc.c` / `ci13xxx_udc.h`)

### 1.1 The qh, and which of its fields bulk OUT uses

`struct ci13xxx_qh` is 48 bytes of struct, hardware stride **64** (`ci13xxx_udc.h:63-78`; the arm's
`STAGE90_USB_ENUM_QH_STRIDE 64`).

| field | off | role for bulk OUT |
|---|---|---|
| `cap` | 0 | `QH_IOS` bit15, `QH_MAX_PKT` [26:16], `QH_ZLT` bit29, `QH_MULT` [31:30] (`ci13xxx_udc.h:66-70`) |
| `curr` | 4 | **written by the core**, the in-flight dTD's address (`ci13xxx_udc.h:72`) |
| `td` (embedded, 28 B) | 8 | the chain head: `td.next` ← `phys(dTD)`, `td.token` |
| `RESERVED` | 36 | |
| `setup` (`usb_ctrlrequest`, 8 B) | 40 | control only — the field at **40, not 32** (`entry_usb_enum.h:105`, design §9.c) |

The embedded `td` is **not** the real descriptor — it is the chain head. `_hardware_enqueue` writes
`qh.ptr->td.next = mReq->dma` (`ci13xxx_udc.c:2069`) and clears `qh.ptr->td.token`'s status (`:2103`), so the
core fetches the real dTD by physical address from `td.next`.

### 1.2 The dTD (`ci13xxx_td`, `ci13xxx_udc.h:40-60`, 28 B, aligned(4))

- `next` off 0. `TD_TERMINATE = BIT(0)` (`:43`).
- `token` off 4. `TD_STATUS` [7:0] with `TD_STATUS_ACTIVE = BIT(7)` (`:51`), `TD_STATUS_HALTED` (bit 6),
  `TD_STATUS_TR_ERR` (bit 3), `TD_STATUS_DT_ERR` (bit 5); `TD_IOC = BIT(15)` (`:53`); `TD_TOTAL_BYTES =
  0x7FFF << 16` (`:54`).
- `page[5]` off 8..27; low 12 bits of each `page[i]` are a byte offset within the page.

### 1.3 The dTD build (`_hardware_enqueue`, `ci13xxx_udc.c:1926-2115`)

Same shape for both directions (`:1975-1977`):

```
token  = (length << 16) & TD_TOTAL_BYTES;   /* remaining byte count */
token |= TD_STATUS_ACTIVE;                  /* the core owns it      */
token |= TD_IOC;                            /* if !no_interrupt      */
next   = TD_TERMINATE;                      /* when no zero-length dTD */
page[0] = mReq->req.dma;                    /* the FULL physical buffer address, not masked */
page[i] = (dma + i*4096) & ~0xFFF;          /* i = 1..4 */
```

One dTD spans up to **5 pages = 20 KiB**, requiring each 4 KiB span physically contiguous. `wmb()` before
prime (`:2007`).

**The OUT/IN difference is the DMA direction and who writes the buffer.** `dma_map_single` uses
`DMA_FROM_DEVICE` for RX (OUT) and `DMA_TO_DEVICE` for TX (`:1941-1944`). The address goes into `page[0]`
identically. For OUT the core DMAs host→DRAM into `page[0]`. **There is no `hw_ep_read` in this driver** —
the vendor never copies OUT data in software, so the CPU must **invalidate before reading** (`mb()` at
`:2132`, `:2007`; the arm's `FlushPoC_DcacheRegion`, `entry_usb_enum.c:91`).

### 1.4 `hw_ep_prime` (`ci13xxx_udc.c:582-596`)

```
n = hw_ep_bit(num, dir);   /* = num + (dir ? 16 : 0) */
if (is_ctrl && dir == RX && ENDPTSETUPSTAT bit) return -EAGAIN;   /* :586 */
hw_cwrite(CAP_ENDPTPRIME, BIT(n), BIT(n));                        /* :589 */
```

The prime is **one R/W-set bit** in `ENDPTPRIME`: EP0-OUT = `BIT(0)`, EP0-IN = `BIT(16)`, EP1-IN =
`BIT(17)`. For a **bulk** endpoint `is_ctrl = 0`, so the setup guard is skipped and the prime is a single
store — called at the end of `_hardware_enqueue` (`:2109-2110`).

### 1.5 `ENDPTCOMPLETE` and completion (`isr_tr_complete_handler`, `:2651`)

`hw_test_and_clear_complete(i)` = `hw_ctest_and_clear(CAP_ENDPTCOMPLETE, BIT(ep_to_bit(i)))` (`:775`). Only a
configured endpoint (`mEp->desc != NULL`, `:2670`) is serviced; then `isr_tr_complete_low(mEp)` (`:2674`).

**`_hardware_dequeue` is where a completed OUT is read** (`:2124-2182`):
- `mb()` to defeat speculative fetch of the token (`:2132`);
- if `TD_STATUS_ACTIVE` still set → `-EBUSY` (`:2134`), retried once after `udelay(10)` (`:2578-2584`);
- byte count: **`actual = length - ((ptr->token & TD_TOTAL_BYTES) >> 16)`** (`:2176-2178`);
- error bits → status -1 (`:2168-2174`);
- **`qh.curr` is never read here.** The byte count lives in the **dTD `token`**, not the qh.

### 1.6 `_ep_nuke` and the bus-reset drain

`_ep_nuke` (`:2210-2276`) = `hw_ep_flush(num, dir)` (`:2225`) + drain the request list. `hw_ep_flush`
(`:462-492`) writes `ENDPTFLUSH = BIT(n)` (`:475`), polls it clear (`:476`), then polls `ENDPTSTAT` clear
(`:489`), bounded by `USB_MAX_TIMEOUT`. The **bus-reset drain** (`hw_usb_reset`, `:835-862`) is the mandatory
one: `ENDPTFLUSH ← ~0`, `ENDPTSETUPSTAT ← 0`, `ENDPTCOMPLETE ← 0`, then drain `ENDPTPRIME → 0` (100 µs bound).
The 910b arm already carries this (`entry_usb_enum.c:365-375`).

### 1.7 Bulk endpoint enable

`ep_enable` (`:2877-2930`): `qh.cap = 0`; for a non-control non-isoc endpoint `cap |= QH_ZLT` (`:2911`); then
`cap |= (maxpacket << 16) & QH_MAX_PKT` (`:2914-2915`); `qh.td.next |= TD_TERMINATE` (`:2916`); `mb()`
(`:2919`); and only for `num != 0`, `hw_ep_enable` (`:2925-2926`). `hw_ep_enable` (`:516-545`) writes
`ENDPTCTRL(n)` with `TXE|TXR|TXT<<18` for TX or `RXE|RXR|RXT<<2` for RX. Bulk = `USB_ENDPOINT_XFER_BULK = 2`
→ TXT=2 → high half `0x00C80000` (the 910b arm's `ENDPTCTRL1`, `entry_usb_enum.h:80`).

---

## 2. The multi-packet / chained dTD

### 2.1 How the driver chains

**There is no multi-dTD build for a single request** — the source says so: *"TODO - handle requests which
spawns into several TDs"* (`ci13xxx_udc.c:1972`). One request → one dTD spanning up to 5 pages (20 KiB) via
`page[0..4]`. Chaining happens only:

1. **Between queued requests** (`:2021-2034`): the previous tail's `next` is reprogrammed to the new
   request's dTD address, guarded by the `ATDTW` interlock (`:2038-2051`). **REPLACED** by the arm's
   one-request-per-endpoint reduction.
2. **The zero-length-packet dTD** (`:1951-1969`): a second `zptr` dTD with `next=TD_TERMINATE`,
   `token=ACTIVE|IOC`, linked as `ptr->next = zdma` (`:1979`).
3. **MSM SPS vendor-DMA mode** (`:1990-2001`): `next = MSM_ETD_TYPE | dma`. **REPLACED** — vendor-DMA engine,
   omitted (map §10.6).
4. **The >16 KiB bulk-OUT chunker** (`ep_queue`, `:3106-3122`): a request larger than 16 KiB is split and
   resubmitted one chunk at a time in `isr_tr_complete_low` (`:2590-2620`), allowed only for **bulk OUT**
   (`:3112-3116`). **REPLACED** by the owner's own buffer budget.

### 2.2 What ends the chain

`TD_TERMINATE = BIT(0)` in the last dTD's `next` (`:1981`, `:1965`, `:1994`). The qh's embedded `td.next` is
left as a raw address (TERMINATE=0), i.e. it always points at the first real dTD (`:2069`).

### 2.3 Is there a per-qh `curr` write-back the CPU must read?

**The hardware writes `qh.curr` (the in-flight dTD address), but the driver never reads it for a byte count.**
Every byte count comes from the **dTD `token`** (`_hardware_dequeue`, `:2168-2178`); `qh.curr` appears only in
diagnostics (`:1120`, `:1893`, `:1609`). For the arm: **the arrived count is
`length - ((td.token & TD_TOTAL_BYTES) >> 16)`**, read after invalidating the dTD line — `qh.curr` is not the
accessor. This is the first fact a first build gets wrong.

### 2.4 `QH_IOS` vs `QH_ZLT` for bulk

- `QH_IOS = BIT(15)` (`:66`): set **only for control** endpoints in `ep_enable` (`:2904-2905`).
- `QH_ZLT = BIT(29)` (`:68`): set for everything else (bulk/interrupt) at `ep_enable:2911`, and re-ORed
  unconditionally in `_hardware_enqueue:2104`. Zero-length terminate — correct for bulk.

The 910b arm already encodes this: `cap = (qh_idx==QH_IN1) ? (QH_ZLT | QH_MAX_PKT(...)) : (QH_IOS | ...)`
(`entry_usb_enum.c:263-266`).

---

## 3. What XNU already has for the stream payload

### 3.1 KDP is present — the transport is a `kdp_*` send/receive pair

`external/xnu-4570.1.46/osfmk/kdp/` holds the full engine: `kdp.c`, `kdp_core.c`, `kdp_serial.c`, `kdp_udp.c`,
`processor_core.c`, `kdp_protocol.h`, `ml/arm/{kdp_machdep.c,kdp_vm.c}`.

**The transport abstraction is exactly a send/receive hook** — the entry points a 910c arm would register:

- `kdp_send_t = void(*)(void *pkt, unsigned int pkt_len)` and
  `kdp_receive_t = void(*)(void *pkt, unsigned int *pkt_len, unsigned int timeout)`
  (`osfmk/kdp/kdp_en_debugger.h:33-35`);
- `kdp_register_send_receive(kdp_send_t, kdp_receive_t)` (`:37-41`);
- the hooks land in `kdp_en_send_pkt` / `kdp_en_recv_pkt`, invoked through `kdp_send_data`/`kdp_receive_data`
  (`kdp_udp.c:255-256`, `:379-392`); the poll loop `kdp_poll` calls
  `kdp_receive_data(pkt.data, &pkt.len, 3)` (`kdp_udp.c:886-905`);
- `kdp_register_link(kdp_link_t, kdp_mode_t)` for link/mode (`kdp_udp.c:395-407`; the serial registration at
  `:2177-2181`).

**So KDP expects a `kdp_*` poll/send hook, and a USB-bulk transport is a drop-in pair**: one `send` that
pushes `pkt_len` bytes out EP-IN (bulk), one `receive` that fills a buffer from EP-OUT (bulk) within a
timeout. `kdp_serial.c` is a pure byte-serializer (`kdp_serialize_packet`/`kdp_unserialize_packet`,
`kdp_serial.h:42-49`; framing `0xFA`/`0xFB`/`0xFE` at `kdp_serial.c:34-44`) a bulk channel can carry verbatim
if the arm chooses serial framing over raw UDP/ARP framing. `kdp_serial_send`/`kdp_serial_receive` in
`kdp_udp.c:2042-2070` are the literal template for a USB pair.

**What is NOT there:** the whole `kdp_*` history is compiled as the disabled arm (map §2,
`build_entry.sh:5318-5323`) — `kdp_init`, `kdp_register_send_receive`, `kdp_unregister_link` are bare
`bx lr`; only `osfmk_kdp_kdp_udp.o` is linked (`:22046`). A 910c arm swaps that stub for the real
`kdp_core`/`kdp_serial` and flips `mach_kdp` / `config_serial_kdp` into `STAGE90_BOOT` (map §7).

### 3.2 The RAM console: publisher, and the missing reader

The console is a RAM ring at `RAM_CONSOLE_BASE 0xde500000`, 2 MiB, sig `'DBGC' 0x43474244` (`src/stage90.h:13-15`),
header `struct persistent_ram_buffer { sig, start, size, data[] }` (`src/stage90.h:6468-6473`).

- **`entry_write_kv`** is the publisher of arbitrary key/value bytes — `src/entry/entry_stubs.c:2526-2557`.
  It writes ` key=0x<8 hex>\n` a character at a time via `entry_putc` (`:2496-2502`), advancing the size field
  at `RAM_CONSOLE_BASE+8` and appending at `+12` (`:2529-2531`), then `dsb sy; isb` (`:2556`). Non-static since
  exp 268 (`:2518-2524`).
- **`entry_live_write`** is the wrapper that gates/caps then calls `entry_write_kv` (`:2446-2465`); readiness
  is `entry_live_ready()` (`:2441-2444`). Every `xnu_live_*` key and every `entry_usb_enum` key goes through
  this (`src/entry/entry_usb_enum.c:95`).
- **The OS's own `printf`/`IOLog`/`kprintf` text** is captured by `entry_os_console_char(ch, which)`
  (`src/entry/entry_stubs.c:2689-…`), called from `__wrap_vcputc` (which=1) and `__wrap_uart_putc` (which=2)
  in `src/entry/entry_trace.c:4278-4291`. It reserves one 128 KB `ENTRY_OS_BLOCK` (`entry_stubs.c:2489`) so the
  OS text is a contiguous run in the ring below the key/value records. Its location and length are published
  as `xnu_live_ostext_at` and `xnu_live_ostext_block` (`:2676-2677`).

**Is there an in-image reader a bulk IN endpoint could stream? No.** `entry_write_kv`/`entry_putc`/
`entry_os_console_char` are the only producers; a grep for any `ostext_read`/`entry_read`/`entry_live_read`/
console-read accessor returns nothing. The consumer today is **TWRP**, which reads the buffer as
`/proc/last_kmsg` after the run. **A 910c bulk-IN stream would have to add the reader itself**: treat
`RAM_CONSOLE_BASE+12` (data) and `+8` (size) as a ring and emit bytes from a private read cursor.

**The mapper constraint** (map §9.2; `entry_stubs.c` slot comment `:2071-2097`): `0xde500000` has **no
translation while XNU's page tables are live**. The live channel works only because `entry_live_map` installs
a section for the console's own megabyte (`LIVE_CONSOLE_ALIAS_BASE = RAM_CONSOLE_BASE + 0x1000000`). A bulk path
reading that region must run with that mapping in place — i.e. only after the live channel is armed.

---

## 4. The Android gadget's bulk endpoints (`f_adb.c`, `android.c`)

### 4.1 Interface and endpoint descriptors

- Interface: `bInterfaceClass 0xFF`, `bInterfaceSubClass 0x42`, `bInterfaceProtocol 1`, `bNumEndpoints 2`
  (`f_adb.c:62-70`) — the Google-ADB signature map §10.8 pins.
- **Bulk IN / OUT**, two descriptors each speed: FS `f_adb.c:122-134` (no `wMaxPacketSize` in the descriptor;
  filled to `min(ep->maxpacket, 64)` for bulk FS at `epautoconf.c:189-194`); HS `512` (`:106-120`); SS `1024`
  (`:72-104`).
- Endpoint addresses are assigned by `usb_ep_autoconfig` at bind (`f_adb.c:272-288`, via
  `adb_create_bulk_endpoints` from `adb_function_bind`, `:551-553`). `epautoconf.c` ORs `USB_DIR_IN` with the
  endpoint number parsed from the endpoint name `epN…` (`:175-189`), and `ep->address = desc->bEndpointAddress`
  (`:200`). The CI13xxx names its endpoints `"ep%i%s"` (`ci13xxx_udc.c:3481-3482`), so the autoconfig picks
  the number from the hardware name. **For a single-config port, pick a stable pair and do not chase the
  runtime values.**

### 4.2 The device ID story (`android.c`) — all runtime, not compile-time

Compile-time defaults `VENDOR_ID 0x18D1` / `PRODUCT_ID 0x0001` (`android.c:107-108`) are used in `device_desc`
(`:274-275`, `bcdUSB 0x0200`, `bcdDevice 0xffff`, `:269-277`), but at bind they are overridable by sysfs
attributes (`idVendor`/`idProduct`/`bcdDevice` at `:2443-2445`, `:2574-2576`; strings at `:2580-2582`). The
kernel defaults are `"Android"` and the literal serial `"0123456789ABCDEF"` (`:2678-2681`), with `bcdDevice =
0x0200 + gcnum` (`:2693-2700`). The phone's real `05C6:901D` and `4a2fe00b` come from **userspace writing the
sysfs nodes** (map §10.8), not compile-time facts. `18d1:d00d` is likewise a runtime value, not a literal in
this tree.

### 4.3 What `adb` exchanges

`f_adb.c` is a **byte pipe, not a protocol engine**: `adb_read` queues a bulk-OUT request of
`ADB_BULK_BUFFER_SIZE = 4096` (`f_adb.c:30`, `:346-390`) and `copy_to_user`s `req->actual` bytes (`:378`);
`adb_write` copies user bytes into an IN request (`:408-451`) and queues on `dev->ep_in` (`:437`); a 0-length
packet read is re-queued (`:372-374`). Endpoint-up is gated on `dev->online` set in `adb_function_set_alt`
after enabling both eps (`:602-627`). **The request/response framing is a userspace protocol (the ADB daemon's
24-byte `struct adb_msg` — `SYNC/CNXN/OPEN/OKAY/WRTE/CLSE`, a CRC32, and magic-xor'd payload) and lives in
`adbd`, not in this kernel tree** (a grep for `A_SYNC`/`A_OPEN`/`A_WRTE` is empty). **Map only — 910d's
subject, not designed here.**

---

## 5. The 910b arm's current EP1-IN state, and what a stream changes

### 5.1 What EP1-IN does today

- Constants: endpoint 1, IN; address `0x80|1`; **bulk**, `maxpacket 64` (FS) — `entry_usb_enum.h:62`,
  `:173-175`. Advertised in the config descriptor's endpoint record (`entry_usb_enum.c:196-201`).
- **The payload is a fixed 4-byte magic** `STAGE90_USB_ENUM_IN_MAGIC = 0x910b0910` (`entry_usb_enum.h:184`),
  written into `g_usb_in_buf[0..3]` and primed once in `usb_enum_arm_ep_in` (`entry_usb_enum.c:353-357`),
  called only on `SET_CONFIGURATION != 0` (`:458-461`). The buffer is **64 bytes** (`:116`).
- **The re-prime on completion** (poll UI handler, `:490-495`): on `ENDPTCOMPLETE` bit for EP1-IN, clear it,
  `g_usb_enum_in_reads++`, and `usb_enum_prime(QH_IN1, TD_IN1, g_usb_in_buf, 4u, 1u)` — **the same 4-byte
  magic, forever**. `usb_enum_prime` (`:242-276`) rebuilds the dTD each time: `token = (4<<16)|ACTIVE|IOC`,
  `next=TD_TERMINATE`, `page[0]=pa(buf)`, cleans the dTD + buffer, writes the qh `cap` (`QH_ZLT | MAX_PKT`) and
  `td.next`, cleans the qh, then writes `ENDPTPRIME = EPBIT(1,1) = BIT(17)`.
- Published keys: `xnu_live_usb_enum_in_reads` (`:543`), plus `state`/`requests`/`resets`/`addr`/`configured`
  (`:538-542`).

### 5.2 What a stream would have to change

Three things, each a concrete edit the arm already has the primitives for:

1. **A buffer, not a 4-byte magic.** `g_usb_in_buf` is 64 B (`:116`); a stream needs a page-aligned multi-KiB
   buffer (the map's own `g_usb_buf[4096] aligned(4096)`, design §2). The dTD `page[]` machinery is already
   present in `usb_enum_prime` but only `page[0]` is written (`:254`) — a >64-byte payload needs `page[1..4]`
   (`(buf + i*4096) & ~0xFFF`) and a `token` length proportional to the chunk. For **bulk OUT** the same prime
   is used with `in_dir=0` and `EPBIT(1,0)`, and `page[0]` names the RX buffer.
2. **A re-prime loop with a moving cursor, not a fixed payload.** Today `usb_enum_prime` always sends
   `g_usb_in_buf, 4u` (`:494`). A stream must (a) draw the next chunk from a source cursor (for the console
   text, `RAM_CONSOLE_BASE+12` up to the `+8` size field — §3.2) and (b) on completion, invalidate the dTD to
   read the core-updated `token` and advance by the **actual** byte count (`length - (token>>16)`; §2.3). The
   completion path already invalidates via `FlushPoC_DcacheRegion` in the setup reader (`:300`) — the same
   primitive is needed on `td` here.
3. **Back-pressure when the host is slow.** Today the endpoint is re-primed immediately on every completion
   (`:494`). A stream must **not** re-prime until new payload is available — and the primitive is already
   latent in the hardware: with the endpoint unprimed the core NAKs the host automatically (`USBi_NAKI` is
   never enabled, map §10.1). So **back-pressure = "leave `BIT(17)` unset"**; prime only when
   `cursor_available > 0`, and do not overwrite `g_usb_in_buf` while a prime is outstanding
   (`TD_STATUS_ACTIVE` in the dTD, readable after the invalidate). For bulk OUT the mirror applies: prime
   `EPBIT(1,0)` only when the consumer has drained the previous chunk.

The rest of the arm (the EP0 control state machine, the bus-reset re-init `usb_enum_bus_reset` at `:365-375`,
the static qh/dTD graph `:112-115`, the mode gate `:507-514`) is already the scaffold a stream sits on.

---

## 6. What the static-graph port replaces (named explicitly)

| vendor construct | site | 910b/910c replacement |
|---|---|---|
| `dma_pool` for qh/td | `ci13xxx_udc.h:117`, `:157-158`; allocs `ci13xxx_udc.c:1952`, `:3002`, `:3493` | static `aligned(64)` `.bss` arrays; identity-map DMA |
| `dma_map_single` per request | `ci13xxx_udc.c:1940-1948`, unmap `:2161-2166` | nothing to map — identity-mapped; explicit `CleanPoU_DcacheRegion`/`FlushPoC_DcacheRegion` |
| request list + `_ep_nuke`/`ep_dequeue`/`isr_tr_complete_low` walk | `:2227`, `:2567`, `:3100` | one request per endpoint; reset the two qh/td slots (`usb_enum_nuke`) |
| `multi_req` >16 KiB chunker (bulk OUT only) | `:3106-3122`, `:2590-2620` | the owner's own buffer budget; not ported |
| MSM SPS vendor-DMA mode | `:1990-2001`, `:2071-2100`, `:2236-2248` | omitted (map §10.6) |
| `prime_timer` watchdog | `:2112`, `:1860-…` | omitted; the poll's `ENDPTCOMPLETE` drain replaces it |
| Linux `usb_request`/workqueue/wait_queue | `f_adb.c:37-60` | not ported at the kernel layer |
| interrupt-driven `udc_irq` | `:3653-3695` | polled, once per idle pass |

**Two facts worth flagging because a first build gets them wrong:** (1) the byte count for a completed
transfer is in the **dTD `token`**, not `qh.curr` — `_hardware_dequeue` (`ci13xxx_udc.c:2176-2178`) is the
owner; (2) the bus-reset re-init (`hw_usb_reset`, `:835-862`) is mandatory in a stream arm exactly as in 910b,
or the second plug wedges.

---

## 7. The reduction, and what 910c does not close

**A first 910c arm** is the same reduction 910b made, one step out: **one bulk IN endpoint, fed from one
in-image source, with the three stream changes above** — the buffer, the cursor-driven re-prime reading the
dTD's actual count, and NAK-based back-pressure. The smallest honest slice is probably *the RAM console's
key/value text streamed out EP-IN*, because the source already exists (`entry_write_kv`'s ring, §3.2) and the
arm's verdict (`lsusb`/a host `read()` returns the boot's own `xnu_live_*` lines) is self-describing.

**910c does not close:** KDP registration (a 910c-or-later arm wires `kdp_register_send_receive` and flips
`mach_kdp` on, §3.1); bulk **OUT** (a mirror: the host writes, XNU reads — the console is read-only, so OUT is
a separate consumer); high-speed negotiation; the >16 KiB chunker; and `adbd` (910d). **The goal is not met by
910c**: the goal's clause is XNU resident and *debuggable* over USB, which needs the stream **and** a KDP or
ADB consumer on top of it.