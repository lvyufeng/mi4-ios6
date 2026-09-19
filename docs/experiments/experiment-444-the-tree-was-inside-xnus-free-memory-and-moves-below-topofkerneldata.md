# Experiment 444 — the tree was inside XNU's free memory, so it moves below `topOfKernelData`

**Status: measured. The panic is gone and the entry image wrote nothing at all — a silent stop, which
is a weaker claim than "the walk works"; and the instrument built to prove the fix sits on the branch
whose non-execution *is* the fix, so the 82-value trace was never in a position to print. Both are
recorded below rather than smoothed.**

## Why: 443 bounded the divergence to a tree the host says is perfect

443's replay of XNU's own walk over the device's bytes ended at `0xcae0` with
`stop_kind` = 1 — the same offset and the same `os_add3_overflow` 442 read out of `panic`'s frame.
Over the blob the same walk ends at `0x7358`. So the bytes in memory are not the file's, and the
counts bounded where they stop being: 13 nodes and 439 properties against the host's 26 and 629,
while the root's header words and its first property's name came back exact. `439 − 368 = 71`
property steps inside the host's thirteenth node — the node at offset `0x43d4`, which the host says
holds **100** properties. The walk ran 71.

**A node's property loop runs `nProperties` times, so 71 is the loop's count and not a landing's.**
If a `length` inside the node had been clobbered the loop would still have run 100 times and the
totals would be 468. They are 439. So the header the walk read at `0x43d4` is not the header the file
holds — **the first clobbered byte is the thirteenth node's own `nProperties` word** — and everything
before it was intact.

That is a *contiguous prefix* intact and everything from one offset onward suspect, which is not what
a scatter of corrupted bytes looks like. It is what a **frontier** looks like.

## The cause, read out of XNU rather than off the device

`osfmk/arm/arm_vm_init.c:370-399`:

```c
	boot_ttep   = args->topOfKernelData;                 /* 0x80700000 in this image */
	cpu_ttep    = boot_ttep + ARM_PGBYTES * 4;           /* + 4 pages  = 0x80704000 */
	bcopy(boot_tte, cpu_tte, ARM_PGBYTES * 4);
	avail_start = cpu_ttep + ARM_PGBYTES * 6;            /* + 6 pages  = 0x8070A000 */
	avail_end   = gPhysBase + mem_size;                  /* 0x80000000 + memSize */
```

and `first_avail = avail_start` (`:534`), from which `pmap_steal_memory` and `ml_static_malloc` grow
upward for the rest of the boot. **Everything above `topOfKernelData` plus ten pages is free
physical memory XNU hands out**, while `arm_vm_init.c:286` maps exactly the *other* region:

```c
	arm_vm_page_granular_RWNX(end_kern, phystokv(args->topOfKernelData) - end_kern, force_coarse_physmap);
	                                                            /* Device Tree, RAM Disk (if present), bootArgs */
```

`[end_kern, topOfKernelData)` is where a device tree belongs on this platform. The image's layout had
it the other way round:

```
26462:  ENTRY_DT_OFFSET=$((ENTRY_DATA_LIMIT + 0x200000))     # the tree, 2 MB ABOVE topOfKernelData
```

so the tree sat at `0x80900000`, **2 MB inside `avail_start`'s range** — and `stage90.h:7023` had
already written the rule down ("The image may own everything below
`STAGE90_XNU_TOP_OF_KERNEL_DATA_OFFSET` and nothing above it") while the tree was the one thing
above it. The walk read the tree correctly until the bootstrap allocator's frontier, growing at about
2 MB for the boot's first allocations, caught up with it — and the frontier is the offset the counts
put it at: `avail_start + 0x1FA3D4` ≈ **2.07 MB** of allocations, measured.

Two more numbers from the same code make the reading sharper:

- `a->memSize = STAGE90_XNU_ENTRY_SIZE` (`xnu_entry_jump.c:150`) — **the kernel's RAM size is set to
  the entry *window's* size**, 16 MB. So `avail_end = 0x81000000` and XNU's whole free region is
  `0x81000000 − 0x8070A000` = `0x1F6000` ≈ **2.05 MB**. The allocator had consumed 2.07 MB by the
  walk: free memory was, for practical purposes, **gone**. That is a second symptom of the same
  shape — one value with two meanings — and it is not fixed by moving the tree.
- `static_memory_end = gVirtBase + mem_size` (`:359`) is the same 0x81000000, so that is not a
  separate ceiling.

## The fix, and it is one line plus the invariant that was wrong

```sh
ENTRY_DT_OFFSET=$((ENTRY_DATA_LIMIT - ENTRY_DT_MAX))   # the last ENTRY_DT_MAX bytes below the limit
```

The tree now ends exactly where `topOfKernelData` begins, inside the region XNU maps *for it*, below
the tables `_start` writes at the limit and below everything the allocator hands out. The new
invariant replaces one that was checking the wrong pair:

| before | after |
| --- | --- |
| `DATA_LIMIT + TABLE_BYTES <= DT_OFFSET` — "the tables must not reach the tree" | `ARGS + ARGS_BYTES <= DT_OFFSET` and `DT_OFFSET + DT_MAX <= DATA_LIMIT` — "the tree must fit between the arguments and the limit" |

The old check was true and the layout was still wrong: it conserved the tree's distance from the
*tables* while nothing conserved it from `avail_start`, which is the thing that overwrites it. The
window stays `0x01000000` on purpose and no longer for the reason it used to: the minimal covering
power of two for the tree's new home is 8 MB, and letting it shrink would silently halve
`boot_args->memSize` — the second defect above, arriving as a side effect.

## The other half: the trace, in case this is not the whole cause

The replay's totals bound where the two walks part; the counts cannot say *which* header differs. So
`fleh_undef` now also prints a trace of the walk it performs:

| keys | what it is |
| --- | --- |
| `xnu_entry_dt_node_count` | every node visited (the same as `replay_nodes`) |
| `xnu_entry_dt_nodeN_off` / `_props` / `_child`, N = 0..15 | the first sixteen nodes, **header words as read** |
| `xnu_entry_dt_ring_count` | every property read |
| `xnu_entry_dt_ringN_off` / `_len`, N = 0..15 | the **last sixteen** property reads, oldest first, as `(header offset, length read)` |

and `tools/xnu_dt_walk.py --fnv` prints the identical block for the blob, from the same pre-order
(the host's `walk()` appends a node after its property loop and before descending, so its node list
is the device's visit order and the concatenation of its property lists is the device's read order).
If the fix is right the two blocks agree value for value — 82 numbers — and if it is not, the first
entry that differs is the offset of the byte that differs.

## The prediction, written before the run

The build is done and no device has been touched. `.text` **4997792 → 5000512** (+2720), entry bin
still **5208596** (the fill term absorbed it: `.data` is pinned by its `2**14` alignment and `.text`
did not cross it this time), `.bss` `0x804f7a40 .. 0x80548a18`, window still `0x01000000`. Payload
`out/stage90/stage90-qcdt.img`, **8228864 bytes**, sha256
`8af1ca19b6364d054d9f57df3ae92a8578d35a62369751ab47bf0e28102573f8`.

| key | predicted | why |
| --- | --- | --- |
| `xnu_entry_dt_root` | `0x806e0000` | `ENTRY_BASE + 7208960`, and `DT_OFFSET + DT_MAX` = `topOfKernelData` exactly |
| `xnu_entry_dt_root_nprops` / `_nchildren` | `0x00000004` / `0x00000015` | the blob's root, unchanged |
| `xnu_entry_dt_first_prop_w0` | `0x656d616e` | `"name"` |
| `xnu_entry_dt_replay_nodes` / `_props` / `_steps` | `0x0000001a` / `0x00000275` / `0x0000028f` | the host walk, **now reachable** |
| `xnu_entry_dt_replay_end` / `_stop` / `_stop_kind` | `0x00007358` / `0x00000000` / `0x00000000` | the walk completes |
| `xnu_entry_dt_checksum` | `0x140a4cb9` | and the range now agrees, so this is comparable for the first time |
| `xnu_entry_dt_chunk_bytes` and `chunk0..7` | `0x00000e6b`, the eight values `--fnv` prints | likewise — 443's instrument defect is retired by the tree no longer being clobbered |
| `xnu_entry_dt_node_count` and the 48 node keys | the host's block, value for value | the trace, if the bytes are the file's |
| `xnu_entry_dt_ring_count` and the 32 ring keys | `0x00000275` and the host's last sixteen reads | likewise |
| `xnu_entry_dt_map_base` | `0x80000000` | unchanged |
| `xnu_entry_kv_dropped` | `0x00000000` | 82 new keys against ~5.6 KB of the 8 KB buffer |
| **the stop** | **not `device_tree.c:56`** | `undef_pc` is no longer `0x8002e588` and `r9` no longer carries the overflow literal; `panic_arg0` is not `0x8090cae0` |

**The prediction is that the device tree stops being the frontier at all**, and that the two blocks
of trace agree — which is the first time this instrument can say "the bytes are the file's" instead
of inferring it. What the *next* stop is, the prediction deliberately does not name: free memory is
`0x1F6000` and the allocator wanted `0x1FA3D4`, so the likeliest shape is a memory-exhaustion
symptom — a `pmap_steal_memory` caller with nothing left, or a panic naming a size — but that is a
guess about XNU's allocation order and the run will say. The falsifiable part is the 82-value trace.

Safety, unchanged: `fastboot boot` only, never flash; every touch through
`stages/stage90/preflight_boot_check.sh --allow-xnu-entry` and
`stages/stage90/run_and_capture.sh --allow-xnu-entry`; both recovery nets armed and proven; the
dead-man disarmed on the last line the payload executes and the hardware watchdog across it, because
the payload's GIC and vector state are gone the moment `_start` switches tables.

## The measurement: the entry image wrote nothing at all

The gate passed and the capture is **294738 bytes / 3932 lines**, against 443's 304204 / 4044. **The
first 3931 lines are 443's**, line for line: `diff` over them returns exactly five differing lines,
every one of them a value that is timing-dependent by construction — the watchdog's live countdown
(and the checksum fed by it) and the timebase ticks read at boot (and the two values derived from
them). So the payload did what 443's payload did, including the part this experiment changed: it
reports the tree it placed and the args it wrote.

```
xnu_entry_device_tree_pa=0x806e0000        <- the new home: DT_OFFSET + DT_MAX = topOfKernelData
xnu_entry_device_tree_len=0x00007358
xnu_entry_args_pa=0x8054a000
xnu_entry_args_memSize=0x01000000
xnu_entry_args_topOfKernelData=0x80700000
xnu_entry_entering_at=0x80000074
stage90 xnu_entry: jumping to XNU's _start
```

and then **nothing**. No `xnu_entry_dt_root`, no `replay_*`, no `checksum`, no `chunk0..7`, none of
the 48 node keys or the 32 ring keys, no `exception: undefined instruction`, no `stub_hit=`, no
`xnu_entry_kv_dropped`. 443 printed **112 lines** after that point; 444 prints **zero**, and the file
ends on Android's own `No errors detected` — which is where 443's ends too, three lines further down.

| reading | 443 | 444 |
| --- | --- | --- |
| capture | 304204 bytes / 4044 lines | **294738 / 3932** |
| lines the entry image wrote | 112 | **0** |
| `grep -c xnu_entry_dt` | 24 | **0** |
| `grep -c 'exception:'` | 1 | **0** |
| `grep -c 'stub_hit='` | 0 | **0** |
| `persistent_write_attempted=0x00000000` | 25 | 25 |
| `failure_mask=0x00000000` | 87 | 87 |

The device returned to Android on its own: `hw_watchdog_counter_running=0x00000001` before the jump,
the same 25 s countdown with nothing to pet it after it, and the same net as every run since 2026-09-17.

## What the run can say, and what it cannot

The prediction's falsifiable half was **the trace**, and the trace cannot print. That is this
experiment's own defect, and it is structural rather than a slip: the replay and the trace both live in
`fleh_undef`, i.e. on the **trap** path. The prediction asked for two things at once — "the device tree
stops being the frontier at all" *and* "the two blocks of trace agree" — but if the tree stops being
the frontier then the trap does not fire, and the only instrument that could have shown the bytes are
the file's runs solely when the fix has failed. **The instrument was placed on the branch whose
non-execution is the success condition**, and the 82 numbers it was to prove the fix with were never in
a position to be printed.

So the reading is narrower than the prediction, and it is worth being exact about which claim survives:

- **The panic is gone.** No `exception: undefined instruction`, no `undef_pc`, no `trap_r9_fmt`, no
  `panic_arg0` — the keys 441-443 were built around are all absent.
- **The walk is *not* shown to work.** A run that hangs before the DT lookup and a run that passes it
  and hangs later leave the same bytes in `ram_console`: the payload's ladder, the jump line, nothing.
  Both instrument headers already say what silence is — "the payload's whole ladder completes, the
  jump line is written, and then no third line. No `stub_hit`, no `exception: <vector>`, no epilogue"
  (`entry_checkpoint.c:8-14`) and "**silence means the CPU never reached reporting code: a loop, a
  spin, or a block that never wakes**" (`entry_trace.c:17-20`). 444's fix is therefore **consistent
  with** this run and **not verified by** it, and the tree's new home is the payload's claim
  (`xnu_entry_device_tree_pa=0x806e0000`), not a reading of the entry image's own walk.

## The frontier this leaves, and the instrument that already exists for it

Two things are true at once, and together they say the next step is a run and not a build:

- the frontier is **unknown** between the DT walk and the end of the run, and its shape is one of the
  three a silent stop can have — a loop, a spin, or a block that never wakes;
- the second defect named above is still in the image. `boot_args->memSize` is the **window**
  (`0x01000000`), so `avail_end = 0x81000000`, XNU's whole free region is `0x1F6000` ≈ 2.05 MB, and
  443's own counts put the bootstrap allocator's frontier at `avail_start + 0x1FA3D4` ≈ 2.07 MB of
  allocation inside it. "A block that never wakes" is exactly what `zalloc_internal` does when
  `kernel_memory_allocate` returns `KERN_RESOURCE_SHORTAGE`, which is why the prediction named
  memory exhaustion as the likeliest shape of what would come next.

The instrument for that sentence exists and is still wired. Two things in this project report *without*
the trap: `entry_checkpoint.c`, a terminal stop at a named symbol, and `entry_trace.c`, which
`STAGE90_ENTRY_TRACE=1` links — five `--wrap`s whose `thread_block` wrapper is **terminal by design**
("the epilogue runs instead of the block, so a block that would have hung becomes a line in the log")
and reports the block's **caller** and its **continuation**, beside every `kalloc_canblock` it saw
(requested size, size served, `canblock`, the caller, **the returned pointer**, and `vm_page_free_count`
read at that moment) and every `kernel_memory_allocate` return value. Its two neighbourhoods are the
two halves of the question a silent run leaves open: the `thread_block` line names the block, and a
`t268_kma_ret` of 6 beside `t268_vm_page_free` near 0 says whether the allocator had anything left to
give when it was called. Running it changes no source.
