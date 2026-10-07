# Experiment 910c — the bulk stream, designed

**Status: DESIGN ONLY. No arm is built, no arm is parked, nothing is pressed.** This is the document the
map (`experiment-910c-bulk-stream-map.md`) owed before a line is written — the discipline 910b's design
followed, applied to the rung after it. The map is the fact-base; this is the plan. Every register value has
its owner named in the map's `file:line`.

## 0. What 910c is, and what it is not

**Is:** the first rung that moves **a stream of bytes** over the USB link while XNU runs — not a fixed magic,
not a post-mortem TWRP read. A host can `read()` an endpoint and receive text XNU itself produced during the
boot. It is the first rung whose verdict is *self-describing*: the bytes that come out are the boot's own
`xnu_live_*` lines.

**Is not:** the goal. 「可以通过usb进行调试」 needs a *debugger transport* on top of the stream — KDP
(`kdp_register_send_receive`, §3.1 of the map) or `adbd` (910d). 910c builds the pipe; the consumer is the
next rung.

**Precondition:** 910b's arm `880a3568` must have enumerated (map §5.1). If the host never reset the port or
never configured the device, EP1-IN is never armed and 910c is premature — the same ladder rule 910b's §0
names. **910c assumes 910b landed; it does not replace its press.**

## 1. The shape of the arm

Extend the 910b file, one new source file only if the reduction needs it:

- **`src/entry/entry_usb_stream.c` + `.h`** — a NEW pair, not folded into `entry_usb_enum.c`, for 910b's own
  reason: *the properties of one arm must be properties of one file.* `entry_usb_enum.c` is provably
  "answers control, sends a fixed magic"; `entry_usb_stream.c` is "moves a variable-length run of bytes with
  a cursor and back-pressure." Two different clauses, two guards.
- **The call site is shared, the body is separate.** `entry_usb_enum_poll()` already runs once per idle pass
  from `__wrap_Idle_load_context`. 910c adds `entry_usb_stream_poll()` **beside it**, after it, so the stream
  services the endpoint only once the enumeration state machine is in `configured`. Its own file must also
  refuse to act if not configured — the 910b hazard rule.
- **Switch `STAGE90_XNU_USB_STREAM` (default 0)**, an ENTRY switch like `USB_ENUM`, so the payload record
  stays byte-identical.
- **`tools/check_usb_stream.py`** — its own guard (§7), the way every prior USB arm has one.

### 1.1 Why a separate file, not a flag inside the enum file

The 910b arm answers control transfers and **re-primes EP1-IN with the same 4-byte magic forever**
(`entry_usb_enum.c:490-495`). A stream changes *that one behavior* and nothing else: the buffer grows, the
re-prime becomes cursor-driven, and back-pressure appears. Keeping it in one file means the enum guard's
clause ("EP1-IN sends only `STAGE90_USB_ENUM_IN_MAGIC`") stays true and checkable, and the stream guard's
clause ("EP1-IN sends `[cursor, cursor+len)` from the source, never past the source's end") is a separate
assertion over a separate file. The two must not be able to silently merge.

## 2. The source: the RAM console's key/value ring

The map (§3.2) fixes the source: **the arm's own live-record ring**, `RAM_CONSOLE_BASE + 12` (data) with the
byte count at `+8`, sig `'DBGC'` at `+0`. It is the smallest honest source because (a) it already exists and
is written by `entry_write_kv` every time the arm publishes a key, so the arm's *own verdict* is what streams
out; and (b) the alternative — the OS's `entry_os_console_char` block (§3.2) — is a different block with its
own publish/length keys and is a strictly later source.

**The hard bound is `+8`'s size field**, and the ring's own ceiling is `0x00200000 - 12` (the `entry_write_kv`
bound). The stream's read cursor is a private static, monotonic; it **does not wrap**. When the cursor reaches
`min(size, ceiling)` the source is drained and the endpoint simply stops being primed — the host sees NAK,
which is the correct busy signal (§3.3). New records appended later move `size` forward and the stream resumes.
This is deliberately a *forward-only consumer*: a wrap would need a second invariant (the ring's own
read/write ordering) that 910c need not take on.

**The mapper constraint is 910c's real precondition** (map §3.2): `0xde500000` has no translation while XNU's
page tables are live, and the live channel works only because `entry_live_map` installs a section for the
console's megabyte (`LIVE_CONSOLE_ALIAS_BASE = RAM_CONSOLE_BASE + 0x1000000`). So the stream reads through the
**alias**, not the base, and only after the live channel is armed. **The arm must refuse to arm the stream when
the alias is not mapped** — a gate, the 910b way.

## 3. The three stream changes (map §5.2)

Each is a concrete edit the enum arm already has the primitives for.

### 3.1 A page-aligned multi-KiB buffer, and the dTD's `page[]` beyond `page[0]`

`g_usb_in_buf` is 64 B (`entry_usb_enum.c:116`) and `usb_enum_prime` writes only `page[0]`
(`entry_usb_enum.c:254`). The stream needs:
- a **`g_usb_stream_buf[4096] aligned(4096)`** — one page, so a single chunk's `page[0]` is the whole
  buffer and the dTD's `page[1..4]` stay unused (a chunk larger than one page is *not* this arm; the map's
  20 KiB chain is a later refinement, §8);
- `page[0] = pa(buf) + offset-within-page` — the low 12 bits are the in-page byte offset (map §1.2), so a
  chunk starting mid-page is still one dTD;
- `token = (len << 16) | TD_STATUS_ACTIVE | TD_IOC` with `len` the chunk length, not a constant 4 (map §1.3).

### 3.2 A cursor-driven re-prime, reading the **dTD token's** actual count

On `ENDPTCOMPLETE(EP1-IN)`, the vendor reads the arrived byte count from the **dTD `token`**, never `qh.curr`
(map §2.3, `_hardware_dequeue:2176-2178`). So the completion path must:
1. `FlushPoC_DcacheRegion(pa(td), 28)` — invalidate the dTD line the core updated (the same PoC operation the
   enum arm already uses for SETUP, `entry_usb_enum.c:300`; **not** `FlushPoU_Dcache`, which the seam wraps —
   910b §9(b));
2. `actual = len - ((td.token & TD_TOTAL_BYTES) >> 16)`, `TD_TOTAL_BYTES = 0x7FFF<<16` (map §1.2);
3. **advance the source cursor by `actual`** (the bytes the host accepted), not by `len`;
4. re-prime with the next chunk, iff the source has more.

**The arm must confirm the TD's `ACTIVE` bit is clear before reading the count** — the vendor's `-EBUSY`
retry (map §1.5, `_hardware_dequeue:2134`). A poll that re-primes while the core still owns the dTD would
have the host read a chunk the arm already considers consumed.

### 3.3 Back-pressure = leave `BIT(17)` unset

The vendor never enables `USBi_NAKI` (map §10.1 of the 910 map). So with EP1-IN **unprimed** the core NAKs the
host automatically — that *is* back-pressure, with no register to write. The arm primes only when
`cursor < available`, and **must not overwrite `g_usb_stream_buf` while a prime is outstanding** (i.e. while
the current dTD's `ACTIVE` is set). This is the one place a stream arm wedges: writing the next chunk into the
buffer before the core has finished DMA'ing the previous one silently corrupts both.

## 4. What the arm does NOT change

The EP0 control state machine, the descriptor tables, the mode gate, the bus-reset re-init
(`entry_usb_enum.c:365-375`) — **all unchanged**. The enum arm keeps owning them. 910c turns the *one* bulk IN
endpoint from a fixed magic into a cursor, and adds a second poll call. **The bus-reset re-init still runs in
the enum arm's poll and still drains EP1-IN**, so the stream's outstanding dTD is flushed by the enum arm on a
re-plug; the stream's poll must be robust to finding its own dTD gone (it re-checks `ACTIVE` and re-arms from
the current cursor, which the reset does not have to know about — but the cursor position itself survives,
because a bus reset does not un-read bytes the host already received).

## 5. What the press must show — the falsifiable reading

The arm publishes, once per state change (the channel is finite):

- `xnu_live_usb_stream_state` — idle / waiting-configured / streaming / drained.
- `xnu_live_usb_stream_cursor` — the private read cursor (bytes handed to the core).
- `xnu_live_usb_stream_chunks` — the number of IN transfers completed.
- `xnu_live_usb_stream_actual` — the last dTD's arrived byte count (`len - (token>>16)`), the proof the
  **token**, not `qh.curr`, is the accessor.
- `xnu_live_usb_stream_avail` — `min(size, ceiling) - cursor`, the back-pressure headroom.

**Falsifications, named:**
- **`state=waiting-configured` forever** → the host never configured the device: 910c's failure indicts 910b,
  and that is the ladder working.
- **`chunks>0` but `actual` is wrong** (== `len` every time regardless of host read size) → the dTD `token`
  is being read without the invalidate, i.e. a stale cached line — the classic cache-direction defect.
- **The host reads more than `available`** → the buffer overwrite race of §3.3.
- **`lsusb -v` shows EP1-IN and a host `read()` returns the boot's own `xnu_live_usb_enum_*` text** → the rung
  WORKED, measured from *outside* the phone, self-describing. This is the strong reading.

## 6. Scope, size, and what is deferred

**This rung is smaller than 910b.** It is one buffer, one cursor, one completion path, and one extra poll
call; the harder half (the control machine, the graph, the cache discipline) is already built and proven by
910b's arm.

**Deferred, explicitly:** bulk **OUT** (the mirror — the console is read-only, so OUT needs a separate
consumer); the >16 KiB / multi-dTD chain and the 20 KiB `page[]` spread (map §2.1; one page is enough for the
first stream); high-speed negotiation (still full-speed, 64-byte max packet); KDP registration (a 910c-or-later
arm wires `kdp_register_send_receive`); `adbd` (910d); and the OS-text source (`entry_os_console_char`'s block)
— the key/value ring is the source chosen here.

**Not deferred, because it is not optional:** the dTD-`token` (not `qh.curr`) read, the `ACTIVE`-bit guard
before reading it, and the no-overwrite-while-primed rule (§3.3). Each is the kind of thing that works in a
first read and corrupts a second — the failure mode this project keeps being bitten by.