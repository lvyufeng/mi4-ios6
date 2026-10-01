# 897 — the press of the HFS-port arm: `hfs_mountroot` reads the volume header, the mount does not complete, and userland is lost

**A PRESS, made autonomously under the standing instruction 「以后不要再问我 直接自动继续，直到os能正常启动」.**
ONE gate exit 0; ONE runner exit 0; `fastboot boot` only; nothing flashed; `33e80afe` absent from both
device lists before the gate. **The phone returned — NO BRICK.** Arm `armed-storage-59ec5fd6` (HFS port
linked into the entry image, 896) is **SPENT** — renamed `armed-storage-59ec5fd6-spent`. Capture
`out/stage90/captures/rung61-hfsport-20261001-232751-last_kmsg.txt` **625,546 B `cc29436c…`**;
`…-press.log` 87,352 B.

## 1. The progress: the HFS reader ran and read the volume header

894's press had `rootmedia_strategy_served` **absent** — the strategy was never called, because
`mi_mdev = 1` made mockfs short-circuit to `mi_base << PAGE_SHIFT` and nothing read the volume. **896's
arm changes that, and the change is visible**: the strategy was called, exactly once, on disk 0, and it
served a read at the HFS+ volume-header offset:

| key | value | reading |
| --- | --- | --- |
| `xnu_live_rootmedia_strategy_read` | `1` | a read (not a write) |
| `xnu_live_rootmedia_strategy_dev` | `0x05000000` | `makedev(5,0)` — **disk 0**, the `md0` the root is on |
| `xnu_live_rootmedia_strategy_blkno` | `2` | block 2 |
| `xnu_live_rootmedia_strategy_count` | `0x200` | 512 bytes |
| `xnu_live_rootmedia_strategy_offset` | `0x400` | **1024** — the HFS+ volume header |
| `xnu_live_rootmedia_strategy_medium` | `0` | the disk-0 medium = the volume (`g_stage90_root_hfs`), on the HFS arm |
| `xnu_live_rootmedia_hfs` | `1` | the arm's switch, as always |
| `xnu_live_rootmedia_served` | `1` | **served** (894: absent) |
| `xnu_live_rootmedia_refused` | `0` | the strategy is called once and it increments then decrements the refusal count |

`hfs_mountroot` reads the volume header at offset 1024 as its first act. It ran and the strategy answered
it. **That is the first time any run on this project has read the HFS+ volume off the root device.**

## 2. But the mount does not complete: exactly ONE read, then nothing

`served` is `1` and there is **no second call** — no `served=2`, no further `_strategy_*` record. A HFS+
mount cannot be one read: after the volume header, the reader parses it and reads the **catalog B-tree**
(and usually the extents and allocation btrees), each a separate strategy call. **The mount began and did
not finish.** The `vfs_mountroot` caller never printed a successor console line, so the mount's
continuation — the step between the volume-header read and whatever read came next — is where the boot
stopped.

## 3. The regression: userland is LOST, and it is the arm's own design that lost it

894 reached userland off the memory root. **896 does not.** The two captures share the endpoint
`BSD root: md0, major 5, minor 0`, and then diverge:

| capture | console after `BSD root` | `ostext_lines` | `ostext_heals` | userland |
| --- | --- | --- | --- | --- |
| **894** `1cb894c8` | `VM_TEST_DEVICE_PAGER_TRANSPOSE: PASS` → `load_init_program: attempting to load /sbin/launchd` → `mini4: the OS starts the process at 0x10e0` → `mini4: the OS has nothing to run -- pid 1 parked in poll … idle path entered 70173 time(s)` | `0x2e` (46) | `0x5` | **yes** — `open`/`read`(`ret_lo=0x4`)/`getpid`/`exit`/`wait`/`ast` all present |
| **896** `59ec5fd6` | **stops at `BSD root`** | `0x15` (21) | `0x1` | **no** — 0 calls of every fixture syscall |

**This is the arm's own consequence, and it is `mi4-a-lower-rungs-side-effect-poisoned-the-rung-above`
turned on the root.** 894's safety argument — written in `stage90_root_media.c`'s `st_media_memdev_info`
comment and in 896 §7 — is: *"if `hfs_mountroot` succeeds the root is HFS+ … **if it fails, mockfs mounts
and memory-backs the Mach-O. BOTH PATHS THEN EXEC."* The fall-through half of that argument is **false on
this arm**: the HFS row is placed **before** mockfs (874/896), and `vfs_mountroot` walks `vfstbllist[]`
and takes the **first** row whose `vfc_mountroot` returns 0. When HFS neither mounts nor cleanly refuses,
`vfs_mountroot` does not advance to mockfs — and the boot stops at the mount. `BSD root: md0` is the
*boot-args device name* (`IOFindBSDRoot`, `IOKitBSDInit.cpp:475`), printed **before** any filesystem is
tried, so it names the disk, not the mount, on both arms.

**Net: the port did not yield "mount HFS+ and exec from it", and by taking the root ahead of mockfs it
also cost the memory-root exec 894 had. The arm is further from the goal than 894, not closer.**

## 4. The shape of the stop: no abort, and the machine did not hang

- **896's own capture carries no abort**: every `xnu_entry_sleh_*` key is `0` (`seq`, `storm`, `pc`,
  `lr` all zero), and `xnu_entry_abort_entries=0`, `xnu_entry_failures=0`. A fault caught by the sleh
  path would have published a non-zero `storm`.
- The capture ends `No errors detected`, and `adb` read it back — so the device **rebooted** after the
  run (the hardware watchdog and the dead-man net fired), which is also why `platform_reboot entered` is
  `0`: the entry side never saw a return. `xnu_entry_panic_entered=1`, `panic_len=0x12da` — the panic
  buffer recorded something (the watchdog's boot after a stop), but the **OS console froze at `BSD root`**
  and no panic text reached it.
- **The machine did not hang in the sense that matters**: the net held, the device came back, and the
  operator's one interlock (`fastboot`) survives. **No brick.**

## 5. A runner-read defect found while reading this press (`mi4-measurement-defects`)

The runner printed `this log's abort: xnu_live_sleh_lr=0x804832bc pc=0x80483188` with
`storm=0x00000009`. **That address is not in 896's capture** — it is in the *previous* log (894's,
`cancro-last_kmsg.txt.prev.38`, 711,598 B), which the runner summarises as "payload output" before the
press. `run_and_capture.sh`'s summary block publishes the abort from the **last** `xnu_live_sleh_lr=`
record in whatever file it was handed, and the header it prints (`this log's abort:`) **does not say which
log**. On this press the two are different files, and a reader attributing section 5's address to 896
(IOKit's `IOInterruptEventSource`, resolved against the ELF) would blame the wrong boot. **The address
belongs to 894.** Not repaired this session — recorded here so the next reader does not repeat it.

## 6. What this settles, and the next question

**Settled:** the port is genuinely integrated — `hfs_mountroot` is reached and reads the HFS+ volume
header off disk 0 through the strategy. 896's build half is confirmed on hardware.

**Not met:** the mount does not complete; the root is not HFS+; and userland is lost. **The goal's
storage clause is not met, and this arm is a regression relative to 894.**

**Next question (host-side, before any further press):** what lies between the volume-header read and the
next read — a fault off the volume's bytes, a `buf_map`/`cluster_io` return on a device that is memory-
backed for `DKIOCGETMEMDEVINFO` but served as a strategy for HFS, or a clean second read that HFS
rejected and `vfs_mountroot` did not fall through from. The single served read and the absent successor
localize it to that gap; the disassembly of the HFS+ mount continuation around the first read is the
place to read, and it is a **read**, not a press.

**Falsification of 896 §7:** 896 predicted "if a press still mounts `md0`, the row is not being taken".
The press shows the row **is** taken and `hfs_mountroot` **does** run — so 896's falsifier mis-named the
failure: the danger was never that the row would be skipped, it was that a row taken *ahead of mockfs*
and not completing **removes the fall-through that the memory root depended on**.