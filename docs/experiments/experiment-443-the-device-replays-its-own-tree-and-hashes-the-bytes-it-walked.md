# Experiment 443 — the device replays its own tree, and hashes the bytes it walked

**Status: measured. The prediction named the wrong branch, and the measurement is better for it: the
device's own replay of the tree ends at `0xcae0` — the very offset the panic named — after 13 nodes
and 439 properties where the host walks 26 and 629, so the bytes in memory are **not** the file's.**

## Why: 442 left two explanations one reading apart

442 read the root through XNU's own accessor and every one of the four predicted rows came back
exact — `0x80900000`, `nProperties 4`, `nChildren 0x15`, a first property named `"name"`. The walk
**had** the blob. And `tools/xnu_dt_walk.py --landings` then said the address the walk died on,
`0x8090cae0`, is not one `next_prop` can produce over the blob's own bytes: 603 landings, every one
of them a node header, a property header or the tree's end, the highest exactly the tree's `0x7358`,
and none at `0xcae0`. Two facts, both about the **file**.

Neither is about the memory the walk was reading, and the host dump cannot see the difference. What
442 left was exactly two explanations, and they are the two halves of what any walk needs:

- **the bytes in memory are not the bytes in the file.** Then a `length` the walk read is not the
  value the dump holds, one step crosses the tree, and the walk dies at `0xcae0` in the untouched
  tail of the `0x20000`-byte buffer. The writer of those bytes is then the bug, and the region that
  reads `0xff000000` everywhere is its signature.
- **the bytes are the file's, and `next_prop`'s first call was already handed a pointer off this
  tree.** Then the tree is not the frontier at all and the caller is: which node pointer, and where
  it came from.

## The instrument: XNU's walk over the device's bytes, and a hash of what it consumed

Both halves of that disjunction are statements about device memory, so the reading has to be taken
there. `fleh_undef` now carries two things, and they run in the same trap that produced 442's
readings — nothing else executes between the panic and the trap, so the memory is the memory the
walk died on.

**The replay** is XNU's own walk, on the device's bytes: from `DTRootNode` (via `DTLookupEntry`, as
in 442), a node's `nProperties` and `nChildren` read from its header, and the property advance
`p + 36 + align4(length)` — the same formula `tools/xnu_dt_walk.py` uses, step for step, including
the two asymmetries that matter: the advance runs for **every** property while the overflow test
runs only from the **second** on (`DTInitPropertyIterator` sets the first property as `entry + 1`
and `next_prop` is never called for it), and the overflow test is on **absolute addresses**, because
`os_add3_overflow` is 32-bit arithmetic on the pointer and not on an offset — `0xff000000` plus an
offset of `0x1234` does not carry while plus `0x80901234` does, so an offset-space test would walk
straight past 442's own panic and report a clean tree. It reports `nodes`, `props`, where it stopped,
and **why** it stopped, as a named kind rather than a silence:

| kind | what fired |
| --- | --- |
| 0 | nothing — the walk finished |
| 1 | `os_add3_overflow`: the condition XNU panics on, at `stop` |
| 2 | the step cap (`0x4000`, nodes + properties; the blob's tree needs 655) |
| 3 | the depth cap (16; the blob's tree is 3 deep) |
| 4 | a read outside the linear map, below `gVirtBase` or above it + `MEM_SIZE_MAX` |

Kind 4 is the one that needs justifying, because every other kind is a statement about the tree.
Within `[gVirtBase, gVirtBase + MEM_SIZE_MAX)` every address is XNU's own linear map of physical
memory and therefore readable — the same fact that lets this file read the kernel's bootstrap stack
at `0xc2013000` — and `MEM_SIZE_MAX` is `0x40000000` in `osfmk/arm/arm_vm_init.c`, the constant
`:505` turns into `virtual_space_start`, which is why 441's bootstrap stack moved to `0xc0000000`
and above. So the bound is derived from XNU rather than written down, and `xnu_entry_dt_map_base`
prints `gVirtBase` beside it so a refusal is a reading. A bound has to exist for one case the
overflow check does not cover: a **first** property whose `length` is enormous is refused by the
host tool's `limit` and only reaches XNU's check one iteration later.

**The hash** is FNV-1a 32 — offset basis 2166136261, prime 16777619 — over exactly the bytes the
replay consumed, `[root, root + end)`, and then over each eighth of that same range in the same
pass. A whole-range mismatch says *that* the bytes differ; the eight chunk hashes say *where*; and a
replay that agrees on structure while a value inside a property differs is caught by the hash and
not by the counts, which is the whole reason for it. `tools/xnu_dt_walk.py --fnv` computes the
identical numbers over `out/apple_dt_host/apple_dt.bin`, printing them as the entry image's own key
block — so the comparison at the end of this experiment is a **diff of two blocks of text**, not a
hex literal checked by eye.

There is a second reading in the same run that the instrument gets for free, and it is the control
on the first: the keys are printed **unconditionally**, zeros included. A replay that did not run
and a replay that ran and found nothing would otherwise look the same, which is the shape of defect
441 was about. `xnu_entry_dt_root` is logged above all of them and is the gate's own report.

## The prediction, written before the run

The build is done and no device has been touched.

| key | predicted | why |
| --- | --- | --- |
| `xnu_entry_undef_pc` | `0x8002e468` | `DebuggerTrapWithState`+0x28 is the `udf #0xfdee`; the symbol is at `0x8002e440` in this link |
| `xnu_entry_trap_r9_fmt` | `0x8045ca14` | the first byte of `device_tree.c:56`'s literal, at `0x8045ca14` in this link |
| `xnu_entry_dt_root` | `0x80900000` | unchanged — `STAGE90_XNU_ENTRY_DT_OFFSET` is still 9437184 |
| `xnu_entry_dt_map_base` | `0x80000000` | `gVirtBase`, set by `arm_vm_init` from `boot_args->virtBase` |
| `xnu_entry_dt_replay_nodes` | `0x0000001a` | the host walk's 26 nodes |
| `xnu_entry_dt_replay_props` | `0x00000275` | 629 properties |
| `xnu_entry_dt_replay_steps` | `0x0000028f` | 655, nodes + properties |
| `xnu_entry_dt_replay_end` | `0x00007358` | where the host walk ends, and the tree's length |
| `xnu_entry_dt_replay_stop` | `0x00000000` | nothing fired |
| `xnu_entry_dt_replay_stop_kind` | `0x00000000` | the walk finished |
| `xnu_entry_dt_checksum` | `0x140a4cb9` | `tools/xnu_dt_walk.py --fnv` over the 29528-byte blob |
| `xnu_entry_dt_chunk_bytes` | `0x00000e6b` | `ceil(29528 / 8)` = 3691, and `8 * 3691` is exactly 29528, so the chunks tile the range |
| `chunk0..7` | `0xaade0189`, `0x8c579873`, `0xdcc24fff`, `0x94effc02`, `0x12af0189`, `0xdfe984af`, `0xd4bd9bc4`, `0x18cf302b` | likewise |
| `xnu_entry_panic_arg0` | `0x8090cae0` | the same stop; 442's operands are not touched by this step |
| `xnu_entry_kv_dropped` | `0x00000000` | 15 new keys against a buffer with 6 KB free |

**The prediction is that the whole block matches and the branch is the second one.** If the device's
replay ends at `0x7358` having seen the same 26 nodes and 629 properties, and the hash over those
bytes is the file's own, then the bytes the walk was reading **are** the bytes in the blob — and
then the only input left that could have sent `next_prop` to `0xcae0` is the pointer it was handed,
so the frontier is the **caller** and the tree is retired.

The other outcome is the more interesting one and it is not excluded: a replay that stops early, or
ends somewhere other than `0x7358`, or a hash that differs while the structure agrees, is a memory
that is not the file's — and then `xnu_entry_dt_replay_stop` names the offset where the walk's view
of the tree diverges, the first differing chunk among the eight bounds it, and the question becomes
*who wrote there*. That is a bug in the boot path rather than in the tree, and the region above the
image that reads `0xff000000` everywhere is where it should be looked for.

Sizes, all of which this step moves: `.text` **4996256 → 4997792** (+1536; the replay, the hasher and
their comments), and the entry image's `.data` moved one alignment quantum — `.text` now crosses its
`2**14` boundary, so `.data` goes from `0x804c4000` to `0x804c8000` and the entry bin from **5192212
to 5208596** bytes (+16384, all of it the gap the alignment opens) with `.bss` following from
`0x804f3a40` to **`0x804f7a40`**. That is the linker's fill term from the other side
(`mi4-linker-fill-term`), and it is why the bin grew by 16 KB for 1.5 KB of code. The layout above
the image is unchanged where it matters: the tree is still `ENTRY_BASE + 9437184` = `0x80900000`,
`topOfKernelData` is still `0x80700000`, and `ARGS_OFFSET` moved with the image to 5545984. Payload
`out/stage90/stage90-qcdt.img`, **8228864 bytes**, sha256
`1043464d4de94c3b9109d81da4d13205b269f171a85010c71b681857ef0d16dc`.

Safety, unchanged: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed and proven; the
dead-man disarmed on the last line the payload executes and the hardware watchdog across it, because
the payload's GIC and vector state are gone the moment `_start` switches tables.

## The measurement

The device returned to Android on its own; nothing was flashed. Payload `out/stage90/stage90-qcdt.img`,
**8228864 bytes**, sha256 `1043464d4de94c3b9109d81da4d13205b269f171a85010c71b681857ef0d16dc` — the
predicted hash, and the predicted 16384-byte growth over 442's 8212480. The stop is the same stop:

```
xnu_entry_undef_lr=0x8002e46c    xnu_entry_undef_pc=0x8002e468    xnu_entry_undef_spsr=0x60000093
xnu_entry_trap_r9_fmt=0x8045ca13  xnu_entry_panic_arg0=0x8090cae0  xnu_entry_panic_arg1=0xff000000
xnu_entry_frame_sp=0x804f89d0
25 x persistent_write_attempted=0x00000000   87 x failure_mask=0x00000000   0 x stub_hit=
xnu_entry_kv_written=0x000009c7   xnu_entry_kv_dropped=0x00000000
log 304204 bytes / 4044 lines, ending `No errors detected`
```

`0x8002e468` is `DebuggerTrapWithState`+0x28 and disassembles to `udf #0xfdee` — the predicted
address, exactly. `0x8045ca13` is the **first byte of the `device_tree.c:56` literal**, which is its
opening `"`: the bytes there read `"Device tree property overflow: prop %p, length 0x%x\n"`, and the
prediction said `0x8045ca14` because the search it was written from started at the first *character
of the text* and not at the quote. The finding is the same; the one-byte miss is the search's, and it
is recorded rather than rounded. `frame_sp` is inside the image's `.bss`
(`0x804f7a40 .. 0x80548a18`), which is 442's reading with the alignment step accounted for.

**The four control readings are unchanged, and they are what make the rest trustworthy:**

```
xnu_entry_dt_root=0x80900000        xnu_entry_dt_root_nprops=0x00000004
xnu_entry_dt_root_nchildren=0x00000015   xnu_entry_dt_first_prop_w0=0x656d616e
xnu_entry_dt_map_base=0x80000000
```

— the tree's root, its two header words, its first property's name, and the range the replay bounded
its reads by, all as before. And then:

```
xnu_entry_dt_replay_nodes=0x0000000d          (13)
xnu_entry_dt_replay_props=0x000001b7          (439)
xnu_entry_dt_replay_steps=0x000001c4          (452 = 13 + 439)
xnu_entry_dt_replay_end=0x0000cae0
xnu_entry_dt_replay_stop=0x0000cae0
xnu_entry_dt_replay_stop_kind=0x00000001      (os_add3_overflow — the condition XNU panics on)
xnu_entry_dt_checksum=0x8a84090d              xnu_entry_dt_chunk_bytes=0x0000195c
```

## What it means: the tree in memory is not the tree in the file

**The replay reproduces the panic exactly, and that is the whole answer.** `end` is `0xcae0` and
`stop` is `0xcae0` and `stop_kind` is 1, the overflow. `0xcae0` is the offset 442 read out of
`panic`'s own frame as `prop`, and an `os_add3_overflow` there is the `length = 0xff000000` 441 read
beside it — the same event, reached independently by a walk that starts at the tree's root and knows
nothing about the panic, the trap, or `prop`. A walk that is handed the blob's root and follows XNU's
formula over the blob's bytes ends at `0x7358`; over these bytes it ends at `0xcae0`. **So the bytes
the walk was reading are not the bytes in `out/apple_dt_host/apple_dt.bin`** — the first branch of
442's disjunction, and not the second. The pointer was never the problem.

**The prediction called this the less likely of the two outcomes and said so**, on the grounds that
the tree walks clean on the host and a walk that starts right should stay right. That reasoning was
about the file and the measurement was about the memory, which is the gap the step existed to close;
the four control readings would have gone the same way under either branch, and only the replay could
tell them apart. It is recorded here as the miss it is.

**And the divergence is bounded, because the counts say how far the two walks agree.** The device's
replay saw **13 nodes and 439 properties**; the host's sees **26 and 629**. Where they part is not
the root — the root's four header words and its first property's name came back exact, and a walk
that had started wrong would not have read those. The host's own walk-order table gives the rest:

| node | offset | nprops | cumulative properties |
| --- | --- | --- | --- |
| 12 | `0x31c8` | 100 | 368 |
| **13** | **`0x43d4`** | **100** | **468** |
| 14 | `0x55d0` | 99 | 567 |

368 is exactly the host's cumulative count after twelve nodes, and **439 − 368 = 71**. So the
device's walk ran the first twelve nodes and their 368 properties to the byte, then inside the
thirteenth node — which the host says is at `0x43d4` and holds 100 properties — it took **71**
property steps and stopped the node, having seen 13 nodes and 439 properties in total, which is
precisely what it reported. **That is a hypothesis and not a second measurement**: the identity of
the device's thirteen nodes has not been read, and only the counts are measured. But it is the only
reading consistent with all four numbers at once, and the number it points at is specific: the
thirteenth node's `nProperties` word, or a `length` in the seventy-second property's header, at or
just before offset `0x50cc` in the blob — the header of the property named `callback-runtime-exec`,
whose declared length is 4.

**An instrument defect, recorded because it is the shape this project keeps finding.** The hash was
taken over `[root, root + end)` — over the bytes the replay consumed — so that structure and content
would be read over the same range. In the case the hash exists for, the two ranges are not the same
range: the device's `end` is `0xcae0` and the host's is `0x7358`, so `chunk_bytes` came back `0x195c`
(6492, a quarter of the distance) instead of `0xe6b`, and the nine hashes describe different bytes.
The reading that settled the experiment was therefore the **counts and the stop offset**, which are
range-free, and not the hash. A fixed range — the tree's declared length, or a stated window — would
have made the hash work when it was needed; the cost of getting this wrong was that the localization
the chunks were supposed to give is a second run instead of none.

**What the eight chunk hashes do say, even at the wrong range**, is worth one line: the first four
chunks are *outside* the tree — `end` is 51936 bytes and the tree is 29528 — so a third of the
reported block describes memory past `0x7358` that no part of the blob covers. They are not
comparable to anything and are not used.

## Where the frontier is now: one word inside the tree

The tail region above the image is retired as a candidate. `0xff000000` repeated at `0xcae0` is what
the walk *read there*, not what the walk was doing there; the walk arrived by a normal landing from a
property whose `length` in memory is not the file's, and it is the file's own bytes that no longer
match. The frontier is now the smallest thing this chain has had: **one header word inside a
29 KB structure this project believes it has verified** — the thirteenth node's own count, or a
length inside it, at or just before `0x50cc`.

The next instrument is the same replay with a **trace** instead of a total: every node's
`(offset, nProperties, nChildren)` in visit order, and a ring of the last sixteen property reads as
`(offset, length)` pairs. Both are compared against the tables the host already prints, and both
answer a question the counts cannot: *which* node the device's thirteenth is, and *which* header it
read differently. A trace that agrees with the host's through twelve nodes and then shows
`0x43d4` with a count that is not 100 names the clobbered word; a trace that diverges earlier names
it earlier. Either way the offset is exact, and the dump that follows is 36 bytes.
