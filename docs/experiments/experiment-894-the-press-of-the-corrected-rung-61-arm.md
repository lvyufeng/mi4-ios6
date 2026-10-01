# 894 — the press of the corrected rung-61 arm: the ENOEXEC is cleared, and the root is mockfs-on-memory

**A PRESS, made autonomously under the standing instruction 「以后不要再问我 直接自动继续，直到os能正常启动」.**
**ONE gate exit 0; ONE runner exit 0**; `fastboot boot` only; nothing flashed; `33e80afe` absent from
both device lists before the gate. The phone returned. Arm `armed-storage-1cb894c8` (rung 61
**corrected**) is **SPENT** — renamed `armed-storage-1cb894c8-spent`. Capture
`out/stage90/captures/rung61-fixed-20261001-173429-last_kmsg.txt` **711,598 B `d4f28436…`**.

## 1. The regression is cleared: the exec succeeds and pid 1 runs

892's rung-61 press died at `load_init_program` with ENOEXEC (8) on `/sbin/launchd`. The corrected arm
carries `mi_mdev = 1` on the HFS arm, so mockfs's file node is mapped from `g_stage90_ramdisk` in
memory instead of being served the volume by the strategy. The log now reads:

```
load_init_program: attempting to load /usr/local/sbin/launchd.development
load_init_program: failed loading /usr/local/sbin/launchd.development: errno 2   (absent, expected)
load_init_program: attempting to load /sbin/launchd
mini4: the OS starts the process at 0x10e0 (the thread's user pc was 0x10e0)
mini4: the OS's own init load returned, so pid 1 has the init image (caller 0x80050eb8)
mini4: the AST is done -- pid 1's thread is at 0x10e0 for user mode (sp 0x101efc)
mini4: the OS has nothing to run -- pid 1 parked in poll for 2000 ms (caller 0x8028e578), ... (ticks 0x24d98cd)
```

**This is 890's exact shape** — the working rung-58 run reached the same "nothing to run" park
(`caller 0x8028e2d8`, `ticks 0x24d4b96`) and the same forced-ending panic; the two differ only in
timing. The goal's own floor is met again in this log: `open`/`read` (`ret_lo=0x4`, buffer's first word
`0xfeedface` after the call)/`getpid`/`exit`/`wait`/`ast` all present, PASS. **The 892 regression is
closed, and 892's defect diagnosis (866's hardcoded `mi_mdev = 0`) is confirmed by the fix working.**

## 2. But the root is mockfs-on-memory, NOT the HFS+ volume

The corrected arm makes *both* mount paths reach an exec; this press shows **which one it took**, and
it is not the HFS one:

| cell | value | reading |
| --- | --- | --- |
| boot-args | `rd=md0` | the root is pinned to the `md0` memory device |
| `Added memory device md0/rmd0` | `0x80521000`, `0x2000` | the RAM disk medium is registered |
| `BSD root: md0, major 5, minor 0` | — | mockfs mounted the memory device |
| `rootmedia_hfs` | `1` | the *switch value* (published on both paths), not a mount verdict |
| `rootmedia_blocks` | `0x10` | the Mach-O (8192 B / 2 pages) |
| `rootmedia_strategy_bytes` | `0x80000` | the published volume size (524288) — a **size**, not a call |
| `rootmedia_strategy_served` | **absent** | **the strategy was never called** |

**The decisive reading is the absence of `_served`/`_refused`.** On 892 those cells existed (one
4096-byte read of the file-node page, `served=1`) because `mi_mdev=0` forced the file node through
`st_media_strategy`. Here `mi_mdev=1`, mockfs short-circuits to `mi_base << PAGE_SHIFT`, and **no call
reaches the strategy at all**. So: the HFS root row did **not** take the root (no HFS mount log, the
root is `md0`), and the fall-through to mockfs is what supplied the exec — exactly the second branch of
893's discrimination note. **`rootmedia_strategy_bytes=0x80000` is not evidence the volume was read**;
it is the size `st_medium_disk_base` reports for the HFS medium, published regardless of whether a byte
moved.

## 3. Safety

- **No brick.** The run returns; the log ends `No errors detected`; `xnu_entry_failures=0`,
  `xnu_entry_abort_entries=0`. The only panic is the **known forced ending** at `0x0fa0065c`
  (`entry_epilogue`'s RESTART_REASON store — `mi4-xnu-reboot-path-cannot-reset` /
  `mi4-the-run-ends-at-entry-epilogue`), the same address and shape as 890's.
- `fastboot boot` only; nothing flashed; no byte written. The card unit is read-only CMD17 reads.
- Seam unmoved; the payload record byte-identical (`6c2b6038…`).

## 4. What this settles, and what it leaves

**Settled:** 892's cause and repair are correct. The rung-61 arm's ENOEXEC was the `mi_mdev` word, and
setting it to follow `STAGE90_XNU_HFS_ROOT_MEDIA` restores the exec and the userland phase. Rung 61
correction is **washed**.

**Left:** **the root is still the memory medium, not the eMMC.** The goal's storage clause for the XNU
track — *XNU mounting storage off the real eMMC* — is **not met**: the boot execs a Mach-O from RAM
(890's state), and the HFS+ volume is registered but not mounted as the root. The next questions are
(a) why the HFS row does not take the root ahead of mockfs (the static-table order, 874, places the HFS
row before mockfs — but `rd=md0` may be forcing mockfs), and (b) whether the card unit (`_card_lba =
0x400000`, `userdata`, ext4) can back a real root at all. **The card unit and the RAM-backed root are
two different disks, and reading the card is not mounting it.**

**THE GOAL IS NOT MET** — the OS reaches pid 1's syscalls off a memory root, not off storage.