# Experiment 905d — the write rung's only defect was a switch the build's own default zeroed

**Arm `armed-storage-0e6eb4b4` (entry bin sha `0e6eb4b4…`, payload `507bd126…`). BUILT AND PARKED, NOT
PRESSED.** This is 905c **rebuilt with the run's own self-ending restored** — `STAGE90_XNU_POST_END_TICKS`
`0` → `115200000`. Nothing else moved. It is the arm to press now that the device is back.

## The finding

905c's press **did not return**: run exit 2, no adb, no fastboot, and the device stayed dark until a
power press (which dies with the RAM console, so the log is unrecoverable). The question the whole
lineage turned on was *why the write arm hangs when the read arms return*. **It is not a hang in the
write code.** It is a build switch that was left off the environment.

`src/entry/build_entry.sh` resolves the self-ending with `SEAM_POST_END_TICKS=${STAGE90_XNU_POST_END_TICKS:-0}`
and `src/entry/entry_trace.c` defaults the macro to `0` under `#ifndef`. 905b and 905c were both built
with `STAGE90_XNU_POST_END_TICKS` **unset**, so their images compiled the ending **out**:

- `entry_post_clock` takes `entry_counter()` at the first `__wrap_platform_cache_idle_exit` call
  (`g_slot_post.calls == 1`) as a baseline and calls `entry_seam_end_run()` — `noreturn`, two stores
  then a `wfe` loop — once `now - t0` reaches the deadline. With the deadline `0` the ending never
  fires.
- Every arm that ever **returned** (902, 903, 904) carries `POST_END_TICKS = 115200000` — 6000 ms at
  19200 ticks/ms. It is the **only** net that returns this device: PS_HOLD and the hardware watchdog do
  not reset it ([[mi4-xnu-reboot-path-cannot-reset]], [[mi4-the-run-ends-at-entry-epilogue]]), and this
  lineage's payloads arm both nets anyway and the device still stayed dark. So with the ending compiled
  out, the boot reaches the idle loop and idles forever = the black screen.
- `bb2269bf` (the *first* 905 arm) returned only because its exec failed **early** — it never reached
  the idle loop, so the missing ending never showed.

**So the 905 write fix actually succeeded.** It advanced the boot **further than any earlier arm** — into
idle (`slot_post`, `post_elapsed`) — and *that is exactly where a missing ending presents as a hang*. The
black screen was the price of a working write path on an image whose return was compiled away. This is
the `mi4-build-variant-comes-from-an-env-default` class: a variant switch with a default is a decision
the build makes **for** the caller, silently, and its default is not "off" — it is **the other
experiment** (`USERDATA` head rewrite vs. self-ending), one `fastboot boot` away, and no hash in the tree
can see it.

## What this arm is

905c, byte-for-byte, except `STAGE90_XNU_POST_END_TICKS` — `0` → `115200000`. The 24 arm keys, the C1
write **verdict** (`xnu_live_storage_wr_int_data_end` / `_wr_data_err`, read after the programming
wait), the recorded rw-root switch (`STAGE90_XNU_HFS_ROOT_RW = 1`, checked against the linked
`hfs_mountroot` in **both** directions), and the seam (`STAGE90_XNU_SEAM_LR = 0x8004e2dc`) are all
**unchanged**. The payload's own switch set is unchanged too (`stage90-build-config.txt` is still
`6c2b6038…`), so the only member that differs in kind is the entry image.

| member | 905c (`d6e2fbe6`) | 905d (`0e6eb4b4`) |
| --- | --- | --- |
| `xnu_arm_entry.bin` | `d6e2fbe6…` | **`0e6eb4b4…`** |
| `stage90-qcdt.img` | `50900d16…` | **`dcf14319…`** |
| `POST_END_TICKS` (entry record) | **0** | **115200000** |

## Verification

- **Built.** Entry image `0e6eb4b4…`, 0 refusals, 24 arm keys; `xnu_entry_678` (24 keys) and
  `xnu_entry_535` (*"ENDS ON A CLOCK — STAGE90_XNU_POST_END_TICKS=115200000"*) both fire green. Payload
  rebuilt via `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1' ./scripts/build.sh`; `stage90-qcdt.img`
  `dcf14319…`, ANDROID! magic, 9519104 bytes.
- **Parked** at `out/stage90/frozen/armed-storage-0e6eb4b4/` (11 members); `tools/verify_revert_set.sh`
  — **11 file(s) matched**; `tools/check_set_name_rule.sh` — every `armed-storage-*` name is the entry
  image's hash, **ok**.
- **Gate accepts the tree:** `verify_press_ready.sh` — **5/5 ok** (all four tree checks green; the device
  reads back as `4a2fe00b`, so `the press would be caught` is green too). `make check` **exit 0**.
- **PRESSED 2026-10-06, run exit 0, device returned in 28 s, no brick.** See *The press* below.

## How to press

`scripts/press_905d.sh` — device check, rewrite the **clean** card image to `userdata`'s head
(`seek=0`), then the gate + runner with `--expect-arm=armed-storage-0e6eb4b4`. The medium must be clean
because an aborted rw mount dirties the HFS+ volume and the next boot would fall back to read-only
(`docs/experiments/experiment-905-write-through-the-emmc.md`).

**What the press must show** (the readings 905b's press never produced, now on an image that returns):
`xnu_live_post_end_calls` present — **the self-ending fired**, i.e. the run came back — **and**
`xnu_live_storage_wr_prog_*` (`_wr_prog_polls >= 1`), **and** `xnu_live_storage_wr_int_data_end = 1`
with `xnu_live_storage_wr_data_err = 0` (the write was accepted — the cell 905c added), **and** a
`B_WRITE` strategy call at LBA `0x400000+`, **and** after a power cycle `/newfile` reads back.
`_wr_data_err != 0` is the falsification that the write path is right.

**GOAL: not yet established (write side).** The rung is built and parked, not proven.

## The press — 2026-10-06 (arm `armed-storage-0e6eb4b4-spent`)

**The self-ending restore worked and the write reached the card, and both survived a return.** Run
**exit 0**, device back in **28 s**, `4a2fe00b` re-enumerated, no brick.

### The self-ending fired — the 905b/905c hang is closed

`xnu_live_post_end_calls = 0x4` (was absent on 905b/905c). `xnu_live_slot_post_calls` 1→4 with
`xnu_live_post_elapsed` reaching `0x06e632c9`. **The run ended on its own clock and returned** — the
reading the whole lineage was missing. This is the falsification-side confirmation of the diagnosis: the
write rungs' black screen was the compiled-out ending, not the write code.

### The write path ran and the card accepted every block

- **`xnu_live_storage_wr_calls = 1`**, `_wr_complete = 1`, **`_wr_err = 0`**, **`_wr_data_err = 0`**
  (`DATA_TIMEOUT|CRC|END_BIT` clear), `_wr_data_wait_timeout = 0`, `_wr_prog_timeout = 0`. The
  programming wait ran and the card released in time (`_wr_prog_polls` in the 0xc635–0xe54c range).
- **17 blocks issued and accepted** through the ladder (`xnu_live_storage_writes` 0→…→17), with
  `xnu_live_storage_wr_lba` covering `0x00400002` (the volume header) and `0x004000d0`–`0x004000df`
  (the catalog file's B-tree node block, at partition offset `0x400000 + d0*512` = `0x1a000`).
- The card unit's own counters: `xnu_live_rootmedia_write_served` = 1, 2, 3;
  `xnu_live_rootmedia_card_wr_first_lba`/`_last_lba` in the `0x00400002`–`0x004000df` range.

### The write persisted on the device — read back off the eMMC, host-side

The `userdata` head (LBA `0x400000`, `seek=0`) was pulled with `adb dd` after the run and compared to the
clean pre-image (`b321db0d…`). **375 bytes differ**, in exactly the two regions the log named:

| HFS volume header (offset `0x400`) | clean | after |
| --- | --- | --- |
| `lastMountedVersion` | `H+Lx` | **`10.0`** (Darwin mounted it) |
| `modifyDate` | `3873765233` | **rewritten** |
| `attributes` | `0x80000100` | **`0x80000000`** (`kHFSVolumeUnmountedBit` cleared — dirty) |
| `folderCount` | `2` | **`3`** |
| `nextCatalogID` | `19` | **`20`** |
| `writeCount` | `1` | **`2`** |

So XNU **wrote HFS+ metadata to the device's own eMMC and it is on the medium now** — the goal's write
half is measured on the device, not inferred from a return.

### What is NOT yet established, and it is now a specific question

**The `/newfile` name is not in the catalog.** The fixture's `open("/newfile", O_CREAT|O_WRONLY, 0644)`
**succeeded** — `xnu_live_open_seq = 3`, `_open_error = 0`, `_open_flags = 0x201`, `_open_mode = 0x1a4` —
so the mount is genuinely read-write and the create returned 0. But the catalog B-tree leaf (`0x1b000`),
parsed cleanly (descriptor `numRecords` and the node's offset table **agree**), shows the create left
**two** new records and neither is called `newfile`:

| | CLEAN (8 records) | AFTER (10 records) |
| --- | --- | --- |
| root children | `sbin`, `HFS+ Private Data` | `sbin`, `HFS+ Private Data`, **`.HFS+ Private Directory Data\r`**, *(new object 19's thread)* |
| new folder cnid | — | **19** (`nextCatalogID` 19→20; `folderCount` 2→3) |

- The two new records are **internally consistent and are one folder**: a new child of the root named
  `.HFS+ Private Directory Data` and a **thread record for cnid 19** — i.e. HFS+ materialised its
  **private directory** (a lazily-created folder a read-write mount needs) under cnid 19, and the volume
  header agrees (`folderCount` +1, `nextCatalogID` +1, `writeCount` +1).
- **No record named `newfile` is anywhere on the medium** (both ASCII and UTF-16-BE searches over the
  whole 512 KB head are negative) — yet the create returned 0.

**So the verdict is: the WRITE path is proven (the C1 cell is measured, the volume changed and persisted
across the run) and the FIXTURE's file did not appear.** The next experiment is *why a create that
returned 0 left the private directory but not the file* — a write-ordering / dirty-block-budget question
(`WRITE_BLKS` 16 vs the two new records' block), or a defect in how the fixture's create walks the
catalog. Recorded as the specific falsifier, not papered over.

> **Correction.** An earlier draft of this section claimed the leaf's `numRecords` field "still reads 8
> while 10 records are present — a half-flushed B-tree". That was a **parser artifact**: the first parse
> mis-read the node's trailing fields and produced a bogus count of 14. Re-parsed against the node's own
> offset table, the descriptor is **8 → 10** and the leaf is **consistent**. No stale-count defect
> exists; the claim is retracted here rather than left standing. (`mi4-measurement-defects`.)

**Arm spent:** renamed to `armed-storage-0e6eb4b4-spent` (dir + record). Next: press a fresh arm (or read
the medium back after a power cycle) to take the `/newfile` read-back under a cleanly-parsed catalog.