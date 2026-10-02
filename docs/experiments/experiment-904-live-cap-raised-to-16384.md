# Experiment 904 — the live cap raised to 16384, and 903's exec-from-card read made into a measurement

**PRESSED 2026-10-02 (arm `armed-storage-4205a945`, spent; run exit 0, no brick). Rung A of the
「坐实 + 写通存储」 pair.** The one code change is `ENTRY_LIVE_CAP` 8192 → 16384. Its effect is that
903's `blkno=0x110` exec-page read — the read that proves `/sbin/launchd` came off the card — is
**now in the captured log** instead of being dropped at the cap.

## Why this is the next rung

903 mounted HFS+ off the card unit and exec'd pid 1 from it, but the proof was **inferred**: the
live-record channel capped at 8192 records and the launchd `__TEXT` page read (volume block 34 =
offset `0x22000`, `blkno=0x110`) landed a few records **past** the cap. The exec was concluded from
console silence (`attempting to load /sbin/launchd` with no `failed loading`), not from a logged read
of the page off the card. That is `mi4-measurement-defects` — a conclusion standing where a reading
should be.

## The change

`src/entry/entry_stubs.c:2267` — `#define ENTRY_LIVE_CAP 8192u` → `16384u`. Two comments that named
8192 (`entry_stubs.c`'s cap-rationale block and the timebase cadence comment at `:5893`) were updated
in the same edit so no comment carries a stale number. **Nothing else changed** — no switch, no new
code path, no new device access. The runner's truncation detector reads `xnu_live_cap` from the log
(`run_and_capture.sh:973,980`), so it scaled with the new value and needed no edit.

**16384 and not more, on purpose.** The 2 MB ram console is the hard ceiling (`entry_write_kv`'s
`max = 0x00200000u - 12u`); at ~50 B/record 16384 ≈ 800 KB is comfortably inside, whereas a cap that
approached the ceiling would recreate this exact defect at a higher number — the trace cut by the
**silent** `entry_write_kv` bound instead of by the **named** `ENTRY_LIVE_CAP` one, which is the harder
failure to read. The margin is published, so the next reader sees it.

## The press, read off `/tmp/cancro-last_kmsg.txt`

**The cap held and nothing was dropped.** `xnu_live_cap = 0x00004000` (16384), **no `xnu_live_capped`
record**, and **11863** `xnu_live_*` records in the log (903 carried 8206 and was capped).

**The exec-page read is now present — the rung's whole object.** `xnu_live_rootmedia_card_off` now
carries `0x00022000` — the launchd `__TEXT` page at volume block 34 — alongside the mount sequence's
`0x400` (volume header), `0x2000` (extents btree), `0x1a000` (catalog btree), `0xa000` (attributes
btree), `0x1b000` (catalog node 27). The **last LBA served** walked to **`0x00400117`** =
`0x400000 + block 0x110 + 7` (the last 4 KiB of the 8 KiB launchd page). So the mount's *last read is
the launchd `__TEXT` page, off the eMMC* — 903's exec-from-card is **measured now, not inferred**.

**The console agrees.** `BSD root: md0, major 4, minor 2` — the **card unit** (`ST_MEDIA_DRIVER=2`),
the string `md0` being the boot-arg's and not the device's; `attempting to load /sbin/launchd` with no
`failed loading`; `mini4: the OS starts the process at 0x10e0`; `pid 1 parked in poll`. **No brick**
(`4a2fe00b` re-enumerated; `33e80afe` never appeared); run exit 0; ended on the known since-690 forced
end clock, not a fault.

## What the wider window also exposed (a pre-existing defect, not this arm's)

Because 903's log was truncated before userland, **904's is the first log that shows the fixture's own
syscalls** — and the goal block's `open` reading is a **FAIL**: the fixture's `open("/dev/rmd0")`
returned **ENOENT (2)**, and its `read` returned **0 of 4 bytes** (the MH_MAGIC never arrived).

**This is not caused by 904, and not by 903.** It is **pre-existing**: the same pair
(`open_error = 0x00000002` twice, `read` present) appears in `/tmp/cancro-last_kmsg.txt.prev.43`
(2026-10-02 02:46, an *uncapped* arm), which predates 903. The fixture's `g_stage90_ramdisk`
(`src/entry/entry_ramdisk.s`) was never the memory device the boot names `md0`/`rmd0` — that device is
`Added memory device md0/rmd0 (02000000/0D000000)` over `g_stage90_ramdisk`'s bytes only under
mockfs; on the card-root arm the root is the eMMC's HFS+, and the fixture's ramdisk is not registered
as `rmd0`. So the fixture's driver-reading (the goal block's floor) is **unmet on every card-root boot
so far**, hidden until now behind the cap.

**This does not touch the storage clause.** The goal's storage reading — *XNU mounts the device's own
eMMC* — is carried by the mount reads (`0x22000`, last LBA `0x00400117`) and the console (`major 4,
minor 2`, launchd loaded and exec'd), not by the fixture's `/dev/rmd0` open. But it **is a real
finding**: the goal block's PASS needs the fixture's driver open to answer 0, and it does not. Named
here as owed, not papered over.

## What this establishes, and what remains

**Establishes** — rung A of the chosen pair: 903's exec-from-card is a *measurement* (the page read is
in the log, the channel did not truncate, the console agrees), and the wider live window surfaced a
pre-existing fixture `/dev/rmd0` defect that every card-root arm has carried.

**Remains** — rung B: the write path (`entry_storage_driver_write` + CMD24, the narrowed `EROFS`
guard, `hfs_mountroot`'s `MNT_RDONLY` clear, a fixture that creates a file) and the persistence test.
Also owed, and now visible: **make the fixture's driver open reach `rmd0` on the card-root arm**, or
retire the goal block's floor reading as the wrong device for this arm.

See [[mi4-903-xnu-mounts-the-emmc-goal-met]]; the write-through rung that follows is 905.