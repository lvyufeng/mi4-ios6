# Experiment 442 — what the walk was handed, and the thirty-six bytes it died on

**Status: measured. The four root readings are exact, and the thirty-six bytes at `prop` are
`0xff000000` repeated — so the walk had the blob and the blob cannot produce that address.**

## Why: 441 named the address and not the tree

441 made the panic's arguments readable, and they came back

```
xnu_entry_panic_arg0=0x8090cae0     (prop)
xnu_entry_panic_arg1=0xff000000     (prop->length)
xnu_entry_panic_args_page=0xc20cb000   <- the read happened
xnu_entry_panic_ap_delta=0x0000000c    <- the layout control
```

`0x8090cae0 + 0xff000000` carries out of 32 bits, so the overflow is exact and the trap is
`next_prop`'s check. And `prop` is inside the `0x20000`-byte device-tree buffer at offset `0xcae0`
but **0x5788 bytes past the end of the 0x7358-byte tree**, in the part `xnu_entry_copy_device_tree`
never wrote.

That says the walk left the tree. It does not say **whether it left a correct tree or was never in
one**, and the two produce the same final pointer:

- a walk that started at the blob's root and drifted inside some node eventually reads a `length`
  out of a value and marches off the end — one property's `nProperties` is one too many, or a name
  is read where a header is;
- a walk handed a root that is not the blob at all starts in uninitialized memory and marches from
  the first step.

440's doc chose between these by **reachability** — it named `DTIterateProperties` because, of
`next_prop`'s four inlined call sites, that is the one with exactly one caller in the image. That is
an argument about the call graph, and `prop`/`length` do not settle it either: all four call sites
hand `next_prop` the same kind of pointer, so both variables are equally consistent with
`DTIterateProperties`, `DTIterateEntries+0x6c`, `skipTree+0x20` and `DTLookupEntry+0x50`.

The host side has already said the blob is sound — `tools/xnu_dt_walk.py` walks all 26 nodes under
XNU's own formula and finds every node's declared `nProperties` matching the property headers under
it, with both walks agreeing at `0x7358`. **So if the walk started at the blob's root, the drift has
to be something the host dump cannot see; if it did not, the host dump is irrelevant.** One number
separates those, and it is the root.

## The instrument: XNU's own accessor, and the bytes at the failing address

`DTRootNode` is `static` in `pexpert/gen/device_tree.c` and cannot be named from the entry image. An
`nm` literal for its address would be a second definition of one value — the defect this file has
spent three experiments removing — and a linker change is more machinery than the reading needs.

`DTLookupEntry(NULL, "/", &root)` reads it through the kernel's own accessor, with neither. With
`searchPoint == NULL` the function takes `DTRootNode` (`device_tree.c:226-229`), and the path `"/"`
returns it on the second statement (`*cp` is 0 after the separator, `:230-235`): one call, no walk,
no allocation, no `kalloc`. It is also already linked — `DTLookupEntry+0x50` is one of the four
sites that materialize `next_prop`'s panic literal.

Three readings come out of it, and the third is the one that makes the first two trustworthy:

| key | what it is | the blob's value |
| --- | --- | --- |
| `xnu_entry_dt_root` | the pointer, as XNU's own walk sees it | predicted `0x80900000` |
| `xnu_entry_dt_root_nprops` / `_nchildren` | the root's own two header words | predicted `0x00000004` / `0x00000015` |
| `xnu_entry_dt_first_prop_w0..w3` | the first property header's first sixteen bytes | predicted `0x656d616e` (`"name"`), `0`, `0`, `0` |

The counts alone are weak — two words can coincide. The name bytes are not: `"name"` is a C string
this project wrote into the tree, and the host dump prints the same four words for offset 0. **The
pair of readings together is the check `nProperties`-against-headers that the host walk performs, run
on the device's own memory.**

**And the 36 bytes at `prop`**, the address `next_prop` died on. These are read because they are
*known mapped* rather than because a bound allows it: `next_prop` read `prop->length` at `prop + 32`
before it panicked, so `prop .. prop + 35` has just been read by the kernel and the thirty-two below
are a subset of it. Eight words, `xnu_entry_panic_prop_w0..w7`. A printable name followed by a small
length is a property header something walked to by mistake, and the name says which property;
anything else is the arithmetic garbage of a pointer that has been adding lengths to itself for
`0x5788` bytes.

`entry_kernel_ptr` gates the root reads: 4-aligned, and at or above `__entry_text_start` — which is
the base `entry.ld` links this image at and the same number `xnu_entry_jump.c` hands `_start` as both
`physBase` and `virtBase`, so the bound is the linker's. The residual risk (a pointer above the base
that is unmapped) is accepted and bounded: every key above is already in `g_kv_buf`, `entry_epilogue`
writes the whole buffer out, and a nested fault still runs that epilogue — so a bad address costs the
rest of the keys and not the report. The `prop` block is gated on the same predicate for the one case
the "known mapped" argument does not cover, an `element` of zero, and the refusal is not silent
because `xnu_entry_panic_arg0` is the same number and is logged above it.

## The prediction, written before the run

The build is done and no device has been touched. Text **4995456 → 4996256** (+800), **image bytes
unchanged at 5192212** and the layout above the image unchanged — the tree is still `ENTRY_BASE +
9437184` = `0x80900000`, `topOfKernelData` still `0x80700000`, `.bss` still
`0x804f3a40 .. 0x80544a18` — which is the linker's fill term for the second step in a row (`.text`
ends below `0x804c3980 + 0x1000` and `.data` stays pinned to `0x804c4000` by its `2**14`
alignment). Payload `out/stage90/stage90-qcdt.img`, **8212480 bytes**, sha256
`a6be1b1771e5746451a615416edf3f625b9ad031f7eb65a33256cb796d1cee69`.

| key | predicted | why |
| --- | --- | --- |
| `xnu_entry_dt_root` | `0x80900000` | `PE_state.deviceTreeHead = boot_args->deviceTreeP`, which `xnu_entry_jump.c:161` sets to `ENTRY_DT_PA` |
| `xnu_entry_dt_root_nprops` | `0x00000004` | the blob's root, and the payload's own `apple_dt_root_props` |
| `xnu_entry_dt_root_nchildren` | `0x00000015` | likewise `apple_dt_root_children` |
| `xnu_entry_dt_first_prop_w0` | `0x656d616e` | `"name"` |
| `w1`/`w2`/`w3` | `0` | the name is four characters and the rest of the 32-byte field is zero |
| the same stop | `undef_pc=0x8002de88`-plus-the-text-delta, `r9_fmt`→`device_tree.c:56` | the change is confined to `fleh_undef` |

**The prediction is that the root readings are the blob's, and that makes the first branch of the
disjunction the true one: the walk started at the right address, on the right bytes, and drifted
inside a node the host walk says is sound.** That is the interesting outcome and it is the one the
whole step is arranged to detect, because it puts the frontier *inside* a structure this project
believes it has verified — and the ways that can happen are all things the host replay cannot see:

- **the walk is not `DTIterateProperties` at all.** 440's `DTIterateProperties` is reachability;
  `DTIterateEntries`'s descent uses `skipProperties`/`skipTree` on the *same* bytes, and
  `DTIterateEntries` is called from `IODeviceTreeAlloc` for **every** node, after
  `MakeReferenceTable` has already been called for it. `MakeReferenceTable` itself is harmless to the
  tree — it only reads. So a drift in the *entry* iterator would panic in `DTIterateEntries+0x6c` and
  the root readings would be the blob's. `prop`'s 36 bytes would then be a header inside the tree.
- **the bytes at `0x80900000` are the blob's but the walk began at an offset into them.** Then the
  root reads `0x80900000`, its counts are 4/0x15, and `prop` is still 0x5788 bytes past the end — and
  the 36 bytes at `prop` are garbage, which is what distinguishes this from the bullet above.

**And if the root readings are not the blob's** — a different pointer, or the right pointer with
counts that are not 4/0x15, or name bytes that are not `"name"` — then the walk was handed a
different tree, the host dump is irrelevant to this stop, and the frontier is whatever produced
`PE_state.deviceTreeHead`. That is the branch the prediction expects *not* to happen, stated so that
its happening is legible.

Safety, unchanged: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed and proven; the
dead-man disarmed on the last line the payload executes and the hardware watchdog across it, because
the payload's GIC and vector state are gone the moment `_start` switches tables.

## The measurement

Payload `out/stage90/stage90-qcdt.img`, **8212480 bytes**, sha256
`a6be1b1771e5746451a615416edf3f625b9ad031f7eb65a33256cb796d1cee69` — the predicted hash. The entry
image inside it is **5192212 bytes**, `STAGE90_XNU_ENTRY_BIN_BYTES` — the predicted size, unchanged
from 441 — and its `.text` is **4996256**, the predicted value to the byte. (`arm-none-eabi-size`
prints `5000380` for the same file: its text column is `.text` *plus* the orphan `.sysctl_set`, 4124
bytes of it, which 298 named. The readelf sum of the `AX` sections is the 4996256 the prediction is
about.) The device returned to Android on its own; nothing was flashed.

```
xnu_entry_kv_written=0x00000779   xnu_entry_kv_dropped=0x00000000
25 x persistent_write_attempted=0x00000000     87 x failure_mask=0x00000000     0 x stub_hit=
xnu_entry_undef_pc=0x8002e028     xnu_entry_trap_r9_fmt=0x8045c41f
xnu_entry_frame_sp=0x804f4a20     xnu_entry_trap_r8_args=0xc2013d80
xnu_entry_panic_str=0x00000000    xnu_entry_panic_caller=0x00000000
xnu_entry_panic_message=0x00000000
```

The stop is the same stop. `0x8002e028` disassembles to `udf #0xfdee` — `DebuggerTrapWithState`'s
trap (`debug.c:371`) — and `0x8045c41f` reads as `"Device tree property overflow: prop %p, length "`,
`next_prop`'s `device_tree.c:56` literal. The prediction wrote `0x8002de88`-plus-the-text-delta and
said why; the delta is `+0x1a0`, the linker's fill term again.

The three zeros are expected and are a confirmation rather than a surprise.
`debugger_panic_str`, `debugger_message` and `debugger_panic_caller` are real XNU `.bss` globals at
`0x804fedec`/`0x804fee00`/`0x804fee04`, and `panic()`'s single-CPU path sets none of them — the
message reaches the trap in `r9`, which is the reading 441 turned into an instrument. `frame_sp` is
the stub's own frame at `0x804f4a20`, inside the image's `.bss`.

**The four predicted rows, and they are exact:**

| key | predicted | measured |
| --- | --- | --- |
| `xnu_entry_dt_root` | `0x80900000` | `0x80900000` |
| `xnu_entry_dt_root_nprops` | `0x00000004` | `0x00000004` |
| `xnu_entry_dt_root_nchildren` | `0x00000015` | `0x00000015` |
| `xnu_entry_dt_first_prop_w0` | `0x656d616e` | `0x656d616e` |
| `w1`/`w2`/`w3` | `0` | `0x00000000` each |

And the panic's arguments, from 441's instrument, unchanged in form:

```
xnu_entry_panic_args_page=0xc2013000   xnu_entry_panic_ap=0xc2013d8c
xnu_entry_panic_ap_delta=0x0000000c    xnu_entry_panic_arg0=0x8090cae0
xnu_entry_panic_arg1=0xff000000        xnu_entry_panic_element=0x8090cae0
xnu_entry_panic_zonename=0xff000000
```

`ap_delta=0x0c` is the same as 441's, so the frame layout the guard was derived from has not moved.
The stack's page (`0xc2013000`, args at `0xc2013d80`) is a different address from 441's
(`0xc20cb000`) — the bootstrap stack is a `kernel_map` allocation, and that is the number 441
recorded itself as having over-specified rather than predicted.

**And the thirty-six bytes at `prop`:**

```
xnu_entry_panic_prop_w0=0xff000000   w1=0xff000000   w2=0xff000000   w3=0xff000000
xnu_entry_panic_prop_w4=0xff000000   w5=0xff000000   w6=0xff000000   w7=0xff000000
```

Eight words, one value. `prop->length` was read at `prop + 32` and is `0xff000000`, and so is every
word around it, including the four zero-valued name words that would have to hold a C string for
this to be a property header.

## What it means: the root is the blob's, and the blob cannot reach that address

**The first branch of the disjunction is the one that happened** — the walk was started at the right
address, on the right bytes: the root's own two header words are 4 and 0x15 and the first property's
name field is `"name"`, which is the host walk's `nProperties`-against-headers check re-run on the
device's memory. Whatever went wrong happened after a correct start, on a tree this project
believes it has verified. The `PE_state.deviceTreeHead` branch is dead, and with it the suspicion
that the payload never got the pointer right.

**The eight identical words also kill the second half of what 441 left open.** 441 could not tell
whether `prop` was a *landing* — a property header something walked to by mistake — or the middle of
a value that a drifting pointer read as one. The bytes say it is neither: the thirty-six bytes are
`0xff000000` repeated, so there is no name here, no length here, and no boundary; `prop` is in a
stretch of memory that reads the same word everywhere. And 441's recheck of the arithmetic said the
same thing from the other side: `prop` is `0xcae0` into a `0x20000`-byte buffer, `0x5788` past the
end of the `0x7358`-byte tree, in the part `xnu_entry_copy_device_tree` never wrote.

So 442 adds the tool that turns "that address looks unreachable" into a statement about the blob.
`tools/xnu_dt_walk.py --landings` prints every address `next_prop` can put the pointer at, which is
the whole of where a walk over these bytes can go:

```
next_prop landings: 603 over 26 nodes (629 properties; the first property of each node never calls
next_prop)
  distinct landing addresses: 603
  landings that are not a node header, a property header, or the tree end: 0
  the highest landing: 0x7358; the tree ends at 0x7358
probe 0xcae0 is NOT any next_prop landing over this blob
```

603 is 629 minus 26, one for each node's first property, which `DTInitPropertyIterator` sets as
`entry + 1` and never passes through `next_prop`. Every one of the 603 lands on a boundary — the
next property header, the next child node header, or the tree's end — and the set of them contains
nothing at `0xcae0`.

**That is a stronger statement than "the tree walks clean".** A walk over these bytes moves only from
one of those 603 addresses to another, so `prop` cannot be an output of `next_prop` on this blob at
all — the address the device died on is not reachable from the root of these bytes by this algorithm.
There is one more way to see the same thing without the set: to arrive at `0x8090cae0` from a
property inside the tree, the previous property's `length` would have to be at least about `0x5000`,
because the walk would have to cross the tree's remaining bytes in a single step. The blob's largest
property value is far below that. No step on these bytes crosses the tree.

The two surviving explanations are one number apart, and they are the two halves of what a walk
needs — its bytes and its pointer:

- **the tree in memory is not the tree in the file.** Then some property's `length` in the device's
  `0x80900000` is not what the host dump says, one step crosses the tree, and the walk dies at
  `0xcae0` in the untouched tail of the buffer. The writer of those bytes is then the bug, and the
  region that reads `0xff000000` everywhere is its signature.
- **the bytes are the file's and the walk was handed a pointer off this tree.** Then `next_prop`'s
  first call already had a `prop` that is not a property header on these bytes, and the frontier is
  the caller — which node pointer, and where it came from — not the tree.

These are separated by one reading, and it is not a reading about `prop`: it is a checksum of the
tree in the device's memory next to a checksum of the blob. If the bytes are the file's, the two
numbers are equal, and the walk's inputs are the only thing left; if they are not, the first chunk
that differs bounds where the writer worked. And the same run can carry the second half, which is
what makes the answer robust to a checksum that happens to collide: a replay of the device's own
tree with XNU's formula, from `DTRootNode` through every node and every property, reporting where it
ends and how many nodes and properties it saw. The host walk ends at `0x7358` with 26 nodes and 629
properties. A device replay that ends there too, on a tree whose checksum matches, is a tree that is
byte-for-byte the file's — and then the bytes are not the frontier and the pointer is.

## Where the frontier is now

441 moved the frontier from "the panic message is unreadable" to "the panic's two operands, and a
tree that walks clean on the host". 442 measured the operands' provenance and the reachability of
the address, and both came back negative for the tree:

- the walk's root is `0x80900000`, its header words are 4 and 0x15, its first property is named
  `"name"` — **the walk had the blob**;
- the thirty-six bytes at `prop` are `0xff000000` repeated, and `prop` is `0x5788` past the tree's
  end — **`prop` is not a boundary, not a value, and not a header**;
- no walk over the blob's bytes, from the blob's root and following `next_prop`, can produce
  `0x8090cae0` — **the walk that died was not working on these bytes, or not from this pointer**;

and the two readings that separate those — the device tree's checksum against the blob's, and a
replay of the device's own tree — are what the next step is for. The step is small on purpose:
one run, one comparison, and a result that either names the writer of the region above the image or
retires the bytes and leaves the caller as the frontier.

Unchanged and still owed: the timer (`ml_init_timebase` and an MSM8974 `tbd_ops_t` over the GPT at
`0xf9020000`, 19.2 MHz, IRQ 19), 405's `IOCPUInterruptController`, the pthread table's other 38
slots, and `osfmk/kperf/kperfbsd.c`, which is off the boot path.
