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
- **Not pressed.** Nothing here was run against hardware.

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