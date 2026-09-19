# Experiment 441 — the instrument's guard refuses the kernel's own stack, and the tree the device builds walks clean under XNU's own formula

**Status: prediction written; hardware run below.**

## Why: 440 named a property whose `length` cannot be a length, and three words that would name it read zero

440's run stopped on XNU's own deliberate trap, `udf #0xfdee` inside `DebuggerTrapWithState`, and
the message out of `r9` resolved to exactly one source line:

```
xnu_entry_undef_pc=0x8002dd88   xnu_entry_undef_lr=0x8002dd8c
xnu_entry_trap_r9_fmt=0x8045bf97 -> "Device tree property overflow: prop %p, length 0x%x\n"
```

`pexpert/gen/device_tree.c:56`, inside `next_prop()`:

```c
static inline DeviceTreeNodeProperty* next_prop(DeviceTreeNodeProperty* prop)
{
	uintptr_t next_addr;
	if (os_add3_overflow((uintptr_t)prop, prop->length, sizeof(DeviceTreeNodeProperty) + 3, &next_addr))
		panic("Device tree property overflow: prop %p, length 0x%x\n", prop, prop->length);
```

That panic **names two values** — `prop` and `prop->length` — and they are the only two things that
would say where the walk went wrong. The run printed neither. It printed:

```
xnu_entry_panic_str=0x00000000    xnu_entry_panic_caller=0x00000000
xnu_entry_panic_message=0x00000000
xnu_entry_panic_ap=0x00000000     xnu_entry_panic_element=0x00000000
xnu_entry_panic_zonename=0x00000000
```

440's doc read those zeros correctly as *"the `va_list` read is refused by the image guard"* — and
that sentence is true, and it reads like the guard working. It is the guard being wrong, and the
wrongness has a date.

## The defect: a bound that was right for one image base, and a refusal that prints as a zero

`fleh_undef` reads the two arguments out of `panic`'s `va_list`, and it gated every one of those
reads on `entry_image_ptr()`, whose bound is `__entry_image_end`. That was correct when it was
written. Experiment 239 measured `r_args = 0x0029be88` with the image linked at **0x00200000** — the
bootstrap stack was inside the image — and experiment 240's log shows the chain working end to end:

```
xnu_entry_frame_sp=0x002acb60      xnu_entry_frame_r8=0x0029be88   (== trap_r8_args)
xnu_entry_panic_ap=0x0029be94      (== r_args + 12)
xnu_entry_panic_element=0x40401f20 xnu_entry_panic_zonename=0x0028b1b6
```

Experiment 241 moved the base to **0x80000000**, and `arm_vm_init.c:505` derives

```c
virtual_space_start = (gVirtBase + MEM_SIZE_MAX + 0x3FFFFF) & 0xFFC00000;   /* MEM_SIZE_MAX 0x40000000 */
```

so the kernel's map moved from `0x40400000` to **`0xC0000000`** — and the bootstrap thread's stack
moved with it. The two runs since have read

| run | `trap_r8_args` (`panic_args`, a `va_list *`) | inside the image? | `panic_element` |
| --- | --- | --- | --- |
| 240 (base 0x00200000) | 0x0029be88 | yes | **0x40401f20** |
| 362 (base 0x80000000) | 0xc80abeb0 | no | 0x00000000 |
| 440 (base 0x80000000) | 0xc2113d80 | no | 0x00000000 |

**Zero is what makes this a measurement defect and not a missing reading.** A panic whose arguments
really are zero and an instrument that refused to look at them produce the same two keys, and the
guard's refusal is not itself reported. So the failure survived a review that read the line — 440's
doc did read it, and named the guard, and the naming was enough to close the question.

It is the same shape as the defect class this project already carries
([[mi4-one-value-two-definitions]], [[mi4-measurement-defects]]): **a predicate correct for one
configuration becomes a silent refusal in the next, and its output is indistinguishable from an
answer.** The guard even says so about itself, one paragraph up, about a *different* pair of magic
numbers:

> This function used to test `[0x00200000, 0x04000000)` — two magic numbers that had to be edited
> whenever the base or the image grew — and experiment 241 moved the base, which would have turned
> every guarded read in this file into a skipped one *silently*, because a guard that says "not
> worth dereferencing" and a guard that is wrong about where the image is look identical in a log.

That fix removed the constants and kept the bound. The bound was the thing that was wrong.

## The first half, done before any device was touched: the blob the device builds walks clean

440's "frontier is now" named a host-side step, and it is a *negative* result, which is why it is
here before the prediction rather than after it.

`tools/apple_dt_host_dump.sh` slices the **real** `build_stage90_apple_dt()` out of
`stages/stage90/stage90_main.c` by line range (asserting each boundary line, so an edit that moves
the function fails the script instead of dumping a different tree) and compiles it with the real
`stages/stage90/apple_dt.c` and two host stubs. The alternative — re-implementing the builder in
Python — would have measured the re-implementation. It writes `out/apple_dt_host/apple_dt.bin`,
**29528 bytes (0x7358)**, which is the device's own `deviceTreeLength`.

`tools/xnu_dt_walk.py` replays XNU's own formula over it — `next = prop + 36 + align4(length)`, with
index 0 taken as `entry + 1` and `next_prop` called only from index 1, exactly as
`DTIterateProperties` does it — and walks into every grandchild, because `MakeReferenceTable` is
called for the root and for every entry at every level:

```
blob out/apple_dt_host/apple_dt.bin  29528 bytes (0x7358)
XNU's walk ends at 0x7358 of 0x7358; the payload's skip_node ends at 0x7358; agree
nodes walked: 26; nodes whose property headers are not all plausible names: 0

ok: XNU's walk completes over every node with no property length that cannot be one,
and every node's declared nProperties matches the property headers under it
```

**The blob is structurally sound under the reader that panicked on it.** The independent signal in
that count is the *name*: a property header that is really a value or a child header has a name that
is not a printable C string, so a node whose declared `nProperties` is one too many is caught even
when the arithmetic happens to survive. 0 of 26.

`--probe` was added to the same tool for the second half of the step, and it earns its place by
demonstrating the drift signature on demand — here reading two bytes into the root's first property
*value* as if it were a header:

```
probe 0x12 is not the start of any node or property this walk reaches
  as a property header: name (not a printable C string: 00 00 00 00 00 00 00 00 00 00 00 00 ...), length 0x7461706d
  next_prop would land at 0x746170a6 - PAST the blob end (0x7358), so this walk leaves the tree
```

`0x7461706d` is `"mpat"` — the ASCII of the bytes after it. **A `length` that looks like text is what
reading a value as a header produces**, and that is a shape a `length` report can be checked against.

Two other host-side facts the probe mode derives rather than writes down: `ENTRY_DT_PA` is
**0x80900000**, parsed out of `out/stage90/xnu_arm_entry.h` (`STAGE90_XNU_ENTRY_BASE` +
`STAGE90_XNU_ENTRY_DT_OFFSET`), so the tool states the tree's physical address from the build that
decided it; and the payload's `g_apple_dt` is at PA **0x006083a4** (`nm`), which is what
`boot_args->deviceTreeP` carries and what `xnu_entry_copy_device_tree` copies 0x7358 bytes from.

So the contradiction the step is about is sharp, and it is not a puzzle about arithmetic:

> the tree the device copies to 0x80900000 walks clean under XNU's own formula, and XNU's own
> `next_prop` panicked on it.

## The fix: read the arguments where they are, and prove the page rather than the base

The reads do **not** need to know where the image is, because the address is not a guess. `r_args` is
`panic`'s own live frame address and the CPU is executing on that stack, so the page holding it is
mapped. The three words that follow it are `panic`'s frame — the `va_list` at `sp+16`, its `__ap` at
`sp+28`, the first two spilled varargs at `sp+32`/`sp+36` — so the whole chain is 20 bytes wide and
sits *behind* `r_args`; experiment 239 measured `ap - r_args = 12`.

The guard is therefore:

```c
#define ENTRY_PANIC_ARG_BYTES 32u

static int entry_panic_args_page(uintptr_t args)   /* 4-aligned, 32-byte window in ONE page */
static int entry_panic_arg_word(uintptr_t args, uintptr_t p, uint32_t n)  /* p..p+n inside that window */
```

Both halves are derived — 32 from the frame layout, 4096 from the page size — and neither is a base
or an image size a later experiment can move. The window's size is what says the reads cannot walk
off into a page the stack does not own; the single-page test is what says the one page they do use is
the one the CPU is running on. `entry_image_ptr` is kept as the first branch, because the strings and
the digit table really are in the image and that bound really is the linker's.

**And the refusal is now visible, which is the part the old guard got wrong.**

| new key | what it is | what it means |
| --- | --- | --- |
| `xnu_entry_panic_args_page` | the page base the window was accepted on | **0 = the reads did not happen** |
| `xnu_entry_panic_ap_delta` | `ap - r_args` | the control: 12 is the layout 239 measured; another number says the frame moved and the two args mean something else |
| `xnu_entry_panic_arg0` | the panic's first argument, under a generic name | for this panic, `prop` |
| `xnu_entry_panic_arg1` | the second | `prop->length` |

`xnu_entry_panic_element` and `xnu_entry_panic_zonename` are kept, after the generic pair, with the
values they always had: three experiments quoted that pair and renaming a key is a discontinuity a
reader has to be told about. **And the old names are half the defect** — for the panic that stopped
440, `element` *is* `prop` and `zonename` *is* `prop->length`, so the answer was in the log all
along, under two names that said "this is about zones". `free_to_zone` was the first panic to use
them and the names never generalized; a reader who skipped the pair because the panic was about the
device tree skipped the answer.

The change is confined to `fleh_undef`, which runs only at the stop, so the boot path is unchanged by
construction and the run is a controlled one: the same stop, three more words.

## The prediction, written before the run

The build is done and no device has been touched. Text **4995104 → 4995456** (+352 for four extra
`entry_kv` calls and four key strings) and **`image bytes` unchanged at 5192212**, which is the
linker's fill term again ([[mi4-linker-fill-term]]): `.text` ends at `0x804c3980` and `.data` is
pinned to `0x804c4000` by its `2**14` alignment, so 352 bytes of text growth is absorbed and every
address above the image — the tree at `+9437184` included — does not move. Payload
`out/stage90/stage90-qcdt.img`, **8212480 bytes**, sha256
`6c4bd9faa4b30ed58a63910c38a5274c483a287bf87732f3df748be96686d818`.

**Predicted, from 440's own `r_args = 0xc2113d80`:**

| key | predicted | why |
| --- | --- | --- |
| `xnu_entry_panic_args_page` | `0xc2113000` | 4-aligned, 32-byte window inside one page; **non-zero is the whole point** |
| `xnu_entry_panic_ap_delta` | `0x0000000c` | the layout 239 measured has not moved |
| `xnu_entry_panic_ap` | `0xc2113d8c` | `r_args + 12` |
| `xnu_entry_panic_arg0` (`prop`) | an address in `[0x80900000, 0x80907358)` | the tree is 0x7358 bytes at `ENTRY_DT_PA` |
| `xnu_entry_panic_arg1` (`length`) | **≥ 0x10000000, and ASCII-shaped** | the only way a `length` overflows a 32-bit `prop + length + 39` at `0x8090xxxx` is a value that is not a length, and reading value bytes as a header is what produces one |
| the same stop | `undef_pc=0x8002dd88`, `r9_fmt`→`device_tree.c:56` | the change touches nothing on the boot path |

**And the three hypotheses it discriminates between, because 440's caller was recovered by
reachability and not by measurement.** `next_prop` is `static inline` and each of its four callers
materializes the same literal; 440's doc named `DTIterateProperties+0x60` because it has exactly one
caller in the image, and that is an argument about the call graph rather than a reading. `prop` and
`length` are what turn it into a reading, and the host probe then says which of these it is:

- **H1 — `prop` is a legitimate property header of the tree, with a small `length`.** Then the blob's
  structure is not the cause at all: `DTIterateProperties` advances at most `nProperties − 1` times,
  each step on a real header, and cannot panic. The frontier leaves the tree and goes to whichever of
  the four call sites `prop`'s position implies — `DTIterateEntries`/`skipTree` (the entry iterator's
  descent) or `DTLookupEntry` (a `/chosen/...` lookup).
- **H2 — `prop` is inside the tree but not at any property-header boundary, and `length` is
  ASCII-shaped.** Then the walk drifted *inside* a node, the probe report names the node and the byte
  offset into the property it drifted into, and the cause is a node the **device** built differently
  from the host dump — which the host tool cannot see by construction, and which the next step has to
  reproduce from the device (dump the tree out of the running kernel's own memory over the same
  channel `fleh_undef` writes to).
- **H3 — `prop` is outside `[0x80900000, 0x80907358)`.** Then `DTIterateProperties` was handed an
  `entry` that is not this tree, and the address says which one; the tree's structure is irrelevant
  and the frontier is the value of `PE_state.deviceTreeHead`.

The prediction is that `args_page` and `ap_delta` come back exactly as above — those two are
properties of the instrument and the compiler, not of the boot — and that `arg0`/`arg1` land in
**H2**, on the arithmetic that the blob walks clean and something therefore has to differ between the
blob and what the walk saw. **If `arg1` comes back large but *not* ASCII-shaped, or `arg0` comes back
below `0x80900000`, the H2 reading is wrong and the numbers say which of H1/H3 to take instead.**

**What this step does not claim.** It does not claim the boot proceeds past `IOKitBSDInit`; it claims
that the next run's log names the property and the offset, which is the difference between a frontier
that is a *symbol* and a frontier that is a *value in data this project hands the kernel*. It also
does not claim the guard is now fault-proof in general — it claims the specific chain it enables is
on a page proven mapped by the CPU executing on it, and that a refusal is a number a reader can see.

Safety, unchanged and re-stated because this run touches the device: `fastboot boot` only, never
flash; every touch through `stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed and proven; the
dead-man disarmed on the last line the payload executes and the hardware watchdog across it, because
the payload's GIC and vector state are gone the moment `_start` switches tables.

## The measurement: the three words arrive, and `prop` is 0x5788 bytes past the end of the tree

Payload `out/stage90/stage90-qcdt.img`, **8212480 bytes**, sha256
`6c4bd9faa4b30ed58a63910c38a5274c483a287bf87732f3df748be96686d818`. Booted non-persistently through
both gates (`fastboot boot`, no flash). The log is **303063 bytes / 4012 lines**, ending
`No errors detected`; 25 × `persistent_write_attempted=0x00000000`, 87 × `failure_mask=0x00000000`,
**0 × `stub_hit=`**, and the device returned to Android on its own. `xnu_entry_kv_written=0x552`,
`kv_dropped=0`, so the six new keys fit.

**The stop is the same stop.** `xnu_entry_undef_pc=0x8002de88`, `lr=0x8002de8c`,
`spsr=0x60000093`, `why=0x804543d8` → `"exception: undefined instruction"`, `abort_entries=0`. Both
addresses are 440's plus `0x100`, and both resolve in the new image to the same two literals:
`xnu_entry_trap_r9_fmt=0x8045c0ff` is the first byte of
`"Device tree property overflow: prop %p, length 0x%x\n"`, and `0x8002de88` is the `udf #0xfdee` in
`DebuggerTrapWithState`. `frame_r8` equals the live `r8` and `frame_r9` equals `undef_lr`, so the
block is not shifted.

**And now so is what the panic names:**

```
xnu_entry_trap_r8_args=0xc20cbd80      (panic_args, a va_list *)
xnu_entry_panic_args_page=0xc20cb000    <- the guard ACCEPTED
xnu_entry_panic_ap=0xc20cbd8c
xnu_entry_panic_ap_delta=0x0000000c     <- the control: 12, exactly the layout experiment 239 measured
xnu_entry_panic_arg0=0x8090cae0         <- prop
xnu_entry_panic_arg1=0xff000000         <- prop->length
xnu_entry_panic_element=0x8090cae0      <- the same two values, under the old names
xnu_entry_panic_zonename=0xff000000
```

**The overflow is exact.** `os_add3_overflow(0x8090cae0, 0xff000000, 39)` carries out of 32 bits on
the first addition: `0x8090cae0 + 0xff000000 = 0x17F90CAE0`, whose low word is `0x7F90CAE0` with a
carry. So the trap is `next_prop`'s check firing on a `length` that is not a length — the reading now
has both operands instead of one.

For the panic to happen at all, `prop + prop->length` had to exceed 32 bits, so **`prop` is at or
above `0x80900000` and `length` is at least `0xF6F8C9A9`.** Both hold, and `prop` is inside the
device-tree *buffer* — `ENTRY_DT_MAX` is `0x20000` and `prop - 0x80900000 = 0xcae0` — but the tree is
**0x7358** bytes, so `prop` is **0x5788 bytes past the end of the tree**, in the part of the buffer
`xnu_entry_copy_device_tree` never wrote. The log's own copy of the tree agrees: `apple_dt_len =
built_apple_dt_len = xnu_entry_device_tree_len = 0x7358`, `apple_dt_root_props=0x4`,
`apple_dt_root_children=0x15`, `xnu_entry_device_tree_pa=0x80900000` — the same four numbers the host
dump produces.

**The prediction's scorecard, and it is a mixed one:**

| predicted | measured | |
| --- | --- | --- |
| `args_page` non-zero | `0xc20cb000` | ✓ in kind — the *value* I wrote down (`0xc2113000`) was over-specific: the bootstrap thread's stack is a `kernel_map` allocation and its address differs between runs (362: `0xc80abeb0`, 440: `0xc2113d80`, 441: `0xc20cbd80`). The key is non-zero, which is what the step was for |
| `ap_delta = 0x0000000c` | `0x0000000c` | ✓ **exact** — the frame layout has not moved, so `arg0`/`arg1` really are the panic's first two varargs |
| `ap = r_args + 12` | `0xc20cbd8c` | ✓ |
| `arg0` in `[0x80900000, 0x80907358)` | `0x8090cae0` | ✗ **inside the buffer, past the tree** |
| `arg1` ≥ `0x10000000` and **ASCII-shaped** | `0xff000000` | half — the magnitude held, the shape did not |

**The ASCII shape came from `--probe`'s own demonstration** — reading two bytes into a property
*value* as a header gives `length 0x7461706d` = `"mpat"` — and it was the wrong generalization: it is
the signature of drifting into a *value* **inside** the tree, and this walk had already left the tree
entirely by the time it hit an overflowing length. The host probe makes that visible for free:

```
probe 0x8090cae0 -> offset 0xcae0 of a 0x7358-byte tree
```

which is not covered by the walk's node table at all — nothing in the blob reaches past `0x7358`.

**So the step lands between H2 and H3, and the useful part is that it says which of its own
premises was wrong.** *Inside* the tree is refuted: the walk did not drift between two properties, it
consumed the tree and kept going. That is consistent with a drift whose first step is somewhere the
0x7358 figure does not describe — a node read at a position that is not a node — and it is equally
consistent with a walk handed a root that is not `0x80900000`. The two are told apart by one number
neither run has printed yet: **what the kernel's own `DTInit` was handed, i.e. `DTRootNode`.** 440's
doc inferred `DTIterateProperties` from reachability because `next_prop`'s four callers inline the
same literal and the caller register is unrecoverable; `prop` and `length` do not settle that either,
because all four callers call `next_prop` on the same kind of pointer. **`DTRootNode` does**, and it
is reachable without a link change or an offset: `DTLookupEntry(NULL, "/", &root)` returns it through
XNU's own accessor, because `searchPoint == NULL` means "the root" and the path `"/"` returns
immediately.

## Readings

| | |
| --- | --- |
| entry image text size | 4995104 → **4995456** (+352, four `entry_kv` calls and four key strings) |
| entry image bytes | **5192212, unchanged** — the linker's fill term: `.text` ends at 0x804c3980 and `.data` is pinned to 0x804c4000 by its `2**14` alignment |
| `.bss` | 0x804f3a40 .. 0x80544a18 (331736), `topOfKernelData` **0x80700000**, tree **0x80900000** |
| payload | `out/stage90/stage90-qcdt.img`, 8212480 bytes |
| log | 303063 bytes, 4012 lines, `No errors detected` |
| safety | 25 × `persistent_write_attempted=0`, 87 × `failure_mask=0`, 0 × `stub_hit=`, `checks=5`, `failures=0`, device back on Android on its own |
| `zone_map_min` / `zone_map_max` | 0xc0528000 / 0xc0766000 — non-zero for the second run in a row |
| the stop | `undef_pc=0x8002de88` (`udf #0xfdee` in `DebuggerTrapWithState`), `r9_fmt`→`pexpert/gen/device_tree.c:56` |
| the panic's arguments | `prop=0x8090cae0`, `length=0xff000000`, `ap_delta=0x0c` |
| where `prop` is | offset `0xcae0` into a `0x20000`-byte buffer holding a `0x7358`-byte tree — **0x5788 bytes past the tree** |

## Where the frontier is now

The frontier is still a **value**, and it is now a value with two operands attached instead of one
name. What the step bought is that the question is no longer "which property's `length` is wrong" —
the answer is `0xff000000` read at an address 0x5788 bytes past the end of the tree, i.e. the `length`
was read from memory the payload never wrote — it is **"what did the kernel's walk start from, and
where did it first leave the tree"**.

Two measurements name it, and both are cheap:

1. **`DTRootNode`, read through XNU's own accessor.** `DTLookupEntry(NULL, "/", &root)` needs no
   offset, no `nm` literal and no link change — `DTInit`'s root is what a `NULL` search point means,
   and the path `"/"` returns it on the second statement. With it, the root's own two header words
   say whether the tree at that address is the blob's root (`nProperties 4`, `nChildren 0x15`) or
   something else. If `root == 0x80900000` **and** the root reads 4/0x15, the tree is intact and the
   drift is inside the walk, and the next instrument is a **bounded, non-panicking** copy of XNU's
   own formula that runs inside `fleh_undef` and reports the first node where its declared
   `nProperties` does not match the property headers under it — the device-side twin of
   `tools/xnu_dt_walk.py`, which is the only thing that can see a tree the host dump cannot, because
   the host dump is byte-exact and this run's log says the device's length and root counts match it.
   If `root` is anything else, the walk was handed a different tree and this is H3 outright.
2. **The 32 name bytes at `prop`**, which are **provably readable**: `next_prop` read `prop->length`
   at `prop + 32` before it panicked, so `prop .. prop + 35` is mapped. They say whether the bytes at
   the failing address look like a property header — a printable name and a small length — or like
   the arithmetic garbage of a pointer that has been adding lengths to itself for 0x5788 bytes.

Also still owed, unchanged by this step: the timer (`ml_init_timebase` + an MSM8974 `tbd_ops_t` over
the GPT at `0xf9020000`, 19.2 MHz, IRQ 19, plus 405's `IOCPUInterruptController`); the pthread
table's other 38 slots; `osfmk/kperf/kperfbsd.c` (the one remaining compile failure, off the boot
path); and `thread_bootstrap_return` (`osfmk/arm/locore.s:1902`) as a future stop.
