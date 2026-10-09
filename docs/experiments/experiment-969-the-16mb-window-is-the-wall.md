# 969 — the wall is the 16 MiB window: iOS's shared cache is 301 MiB (2026-10-09)

968 mounted the **real** decrypted iOS 7.1.2 rootfs as `/` (measured: `vfstbllist = [hfs_vfsops, "hfs",
NULL]`, no mockfs fallback) and made it **writable** through the COW shadow. This rung reads what that is
*not* enough for: the real `/sbin/launchd` cannot run, because the address space XNU manages on this arm is
the **16 MiB entry window**, and iOS 7's userspace is built on a **301 MiB** shared cache.

## 1. What the real launchd is (measured, not assumed)

`7z` reads the HFSX volume, so the real userspace is inspectable host-side. The real `/sbin/launchd`:

- **154,736 B, 32-bit ARM MH_EXECUTE** (`0xfeedface`), segments `__TEXT` (128 KiB) + `__DATA` (4 KiB) +
  `__LINKEDIT` (28 KiB) → **164 KiB of own segments** (the other ~22 KiB is the fat/binary padding).
- **LC_LOAD_DYLIB: `/usr/lib/libSystem.B.dylib` and `/usr/lib/libbsm.0.dylib`** — it is dynamically linked.
- **No `LC_ENCRYPTION_INFO`** — the decrypted rootfs is genuinely decrypted.
- The volume is a real iOS 7.1.2 tree: **7142 files, 2851 folders**, incl.
  `System/Library/Caches/com.apple.dyld/dyld_shared_cache_armv7` = **315,952,666 B (301 MiB)** and
  `usr/lib/dyld` (196 KB).

## 2. Why 164 KiB of binary cannot exec

`libSystem.B.dylib` and `libbsm.0.dylib` are **not separate files** in an iOS 7 rootfs — they live inside the
**shared cache**. So `execve("/sbin/launchd")` must, through `dyld`, **map the huge cache regions holding
libSystem** (hundreds of MiB of address space for the set), plus the `dyld` from `/usr/lib`. That address
space must fit in what XNU manages.

**What XNU manages on this arm is the 16 MiB window, and that is exact** — 956's finding, re-read:

- `arm_vm_init.c:316/333`: `uint32_t gMemSize; ... gMemSize = args->memSize;`
- The managed map is `gMemSize` long (`check_d13_managed_base.py`), and `gMemSize` is the **boot handoff
  size** — this arm's `xnu_entry_args_memSize = 0x01000000` = **16 MiB** (`STAGE90_XNU_ENTRY_WINDOW`).
- 956: the window's managed map is fixed-base (`MANAGED_BASE 0xC0000000`); **1 GiB fills the 16 KB L1
  exactly** and `build_entry.sh` refuses `>= 0x40000000`.

So the managed map is **16 MiB**, and the iOS userspace chain needs **hundreds of MiB** for the shared
cache. `launchd` will not exec; SpringBoard is far out of reach.

**The 968 arena makes it worse, not better**: the COW shadow's `.bss` moved `topOfKernelData` from
`0x80800000` to `0x80A00000` (968 §3), so XNU's early free region is **~6 MiB** inside the 16 MiB window —
and the fixture's `g_stage90_ramdisk` is itself 8 KiB of `.data` in that window. There is no arrangement of
a 16 MiB managed map that runs iOS 7.

## 3. The consequence: the window is the prerequisite, and the window is 915

The goal's memory clause (「正确识别…3GB内存」) was read as a **reporting** requirement (958: `max_mem` via
`/defaults hw.memsize`, the map unchanged). 969 shows it is also a **runnability** requirement: iOS cannot
run until the mi4 **owns its real RAM**, because the shared cache needs the address space.

That is **915's region-list port** (corrected by 964): make the real banks
`[0,0x60000000) ∪ [0x80000000,0xe0000000)` = **3 GiB** *allocatable* — the low bank owned, not just
reported. It is a whole-kernel pmap change (the single-span linear physmap
`phystokv(a) = a - gPhysBase + gVirtBase` cannot express two banks), with no bounded safe rung, which is why
917–968 built the storage/usb/mount stack *up to* this wall rather than through it.

**So the next rung is not a press.** It is the low-bank port. Pressing `armed-d13-7107b998` would confirm the
mount and the COW on hardware — worth doing, and cheap (reversible) — but it cannot reach SpringBoard, and a
mission of "run iOS" needs 915 first.

## 4. What to press, and what to expect

- **Press 1 (`scripts/press_968.sh`)** — confirms the geometry: the real HFSX rootfs mounts as `/`, the COW
  serves the fixture's write (`xnu_live_rootmedia_cow_wr_blocks` present, no `_cow_refused`), the base
  medium is never written. **Expected**: `BSD root:` names the card volume (not `md0`), and the fixture
  runs from it. **NOT expected**: real launchd / SpringBoard — §2.
- Then the **915-B low-bank port** (964), which is where "iOS runs" actually begins.

## 5. Provenance

- `7z` listing + extraction of `/mnt/data/ios7-payload/v2/ios7/rootfs.hfs` (HFSX `0x4858` v5, 939,524,096 B,
  7142 files); launchd Mach-O parsed host-side (segments, dylibs, no `LC_ENCRYPTION_INFO`).
- `external/xnu-hd2-darwin13/xnu/osfmk/arm/arm_vm_init.c:316/333` (`gMemSize = args->memSize`);
  `tools/check_d13_managed_base.py`; 956 (`mi4-956-d13-entry-window-ceiling`, the 1 GiB L1 ceiling);
  915/964 (`mi4-915-multibank-region-list-design`, `mi4-964-915b-corrected-against-958`).
- Follows [[mi4-968-cow-writable-root-arm]] (this session; the arm 968 built and parked) and
  [[mi4-958-3gb-rides-the-report]] (why the map stayed 16 MiB). Device unmodified.