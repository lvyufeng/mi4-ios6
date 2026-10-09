# 970 — the window is RAM, not just address: a 1008 MiB window, payload-only (2026-10-09)

969 measured the wall — the real iOS 7.1.2 `/sbin/launchd` links libSystem/libbsm out of the **301 MiB**
dyld shared cache, but XNU on every arm so far manages only the **16 MiB** entry window — and concluded the
fix was the whole-kernel **915-B** low-bank pmap port. This rung tests the *premise* of that conclusion,
because reading the D13 tree shows the window is not only an address: **`avail_end = gPhysBase + gMemSize`
makes the window the ALLOCATOR's physical-RAM end.** Widening it is payload-only, and on the mi4 it lands
in the real high bank. Arm: **`armed-window-85d8f2a7`**, `STAGE90_XNU_ENTRY_WINDOW=0x3f000000` (1008 MiB).

It is a **host-side arm, PARKED.** No device was touched. **PRESS IS THE OPERATOR'S** — a park is not a press.

## 1. What 969 left open, and the one line that reopens it

969's chain: real launchd is 154,736 B but dynamically linked, and its dylibs are **not files** — they live
in the 301 MiB shared cache, so `execve` must map hundreds of MiB of address space; XNU manages only
`gMemSize = args->memSize` = 16 MiB; therefore the window is a *runnability* prerequisite, and the window
"is 915".

The premise is that the window is **only address space**. It is not. From
`external/xnu-hd2-darwin13/xnu/osfmk/arm/arm_vm_init.c`:

```
:333  gMemSize = args->memSize;                                   /* the window IS memSize        */
:334  max_mem = mem_size = sane_size = gMemSize;                   /* and it is what XNU reports  */
:362  managedCachePA = identityCachePA + l2_size(gMemSize);
:379  l2_cache_to_range(managedCachePA, MANAGED_BASE, ttb, gMemSize, TRUE);
:422  avail_end = gPhysBase + gMemSize;                            /* THE ALLOCATOR'S END         */
:503  pmap_bootstrap(gMemSize, ...);
```

`avail_end = gPhysBase + gMemSize` is the decisive line. The window does not merely *describe* a VA range;
it is the end of the memory `pmap` will allocate from. So raising `gMemSize` raises XNU's **own physical
RAM**, and on the mi4 that RAM is real: the window is anchored at `gPhysBase = 0x80000000` inside the high
bank `[0x80000000, 0xDE500000)` (958: `RAM_BOOT_BANK_SIZE = 0x5e500000` = 1509 MiB). There is a contiguous
1509 MiB there, so the only bound on the window is the page-table arithmetic (§3), not the DRAM.

## 2. The mechanism, and why the press is payload-only

The window reaches XNU through the **generated header** `out/stage90/xnu_arm_entry.h`'s
`STAGE90_XNU_ENTRY_SIZE`, consumed by the payload at `src/xnu_entry_jump.c:150` (`a->memSize = …`), `:164`
(`memSizeActual`), `:239` (the dcache clean). No entry source reads it. So:

- the **entry bin does not move** — `xnu_arm_entry.bin` on this arm is `7107b998…`, **byte-identical** to
  968's; and the entry ELF/sources are identical too;
- **the payload moves** — `stage90.bin` embeds the new immediate. Measured by disassembly of the linked
  object: `xnu_entry_jump.o` carries `mov r8, #0x3f000000` (`e3a0843f`), and `stage90.bin` holds
  `0x3f000000` at 13 little-endian sites (the `memSize`/`memSizeActual` stores, the dcache clean, the
  bounds asserts).

So the arm is the **`armed-window-*`** family: the set-name suffix comes from `stage90-qcdt.img`
(`85d8f2a7`) because the entry bin did not move — exactly the case `check_set_name_rule.sh`'s census row
records (memory: `mi4-build-variant-comes-from-an-env-default`).

## 3. Why `0x3f000000` and not `0x40000000`

`src/entry/build_entry.sh` refuses a D13 window `>= 0x40000000`, and its refusal is itself the bound:
D13's managed map is built from the **fixed** VA `MANAGED_BASE = 0xC0000000` (`:142`), and its L1 is filled
with `(gMemSize>>20)` entries from byte `0x3000` of a `0x4000` table (`L1_SIZE`). At `gMemSize =
0x40000000` that is **exactly** `[0x3000, 0x4000)` — the whole table — and `MANAGED_BASE + gMemSize`
reaches `0x100000000`. One byte more wraps 32-bit VA and writes past the L1 **with no fault** (a brick
with no cause in the log). The refusal's own advice is *"use at most `0x3f000000` for headroom"* — one
16 MiB L1 step below the wrap, and this arm uses it.

| arm | window | XNU free region after `topOfKernelData` (`0x80A00000`, 968's COW arena) |
|---|---|---|
| 968 | `0x01000000` (16 MiB) | `0x81000000 - 0x80A00000` = **6 MiB** |
| 970 | `0x3f000000` (1008 MiB) | `0xBF000000 - 0x80A00000` = **998 MiB** |

**998 MiB free** is what the 301 MiB shared cache — plus the rest of iOS userspace — needs. That is the
whole point: the number 969 said required a whole-kernel port is available with one switch.

## 4. The arm

Same switch set as `armed-d13-7107b998` (the COW writable root over the real 896 MiB HFSX rootfs, the
full D13 USB ladder, `MEM_TOTAL=1`, `CARD_COW=1` with `HDD_WRITE=0`) with **one** switch changed:

| switch | 968 | 970 |
|---|---|---|
| `STAGE90_XNU_ENTRY_WINDOW` | `0x01000000` (16 MiB) | **`0x3f000000` (1008 MiB)** |

Everything else is byte-for-byte the 968 arm. `stage90-build-config.txt` is `6c2b6038…`, identical to
968's — because the window is an **entry** build parameter that reaches the payload through the generated
header, not through a payload switch, so the payload's own record cannot see it. That is why the *entry*
record must carry the key (the `533` defect).

**Park:** `out/stage90/frozen/armed-window-85d8f2a7/`, 11 members. Entry bin `7107b998…` (6331476 B,
unchanged from 968); payload `stage90.bin` `221c614f…` (6826876 B); qcdt `85d8f2a7…` (9351168 B).
`records/revert-set.txt` carries the block. Nothing in `src/` or `scripts/` was edited to build it — the
arm is a build *parameter*, not new code.

## 5. Host-side verification (all green, no device)

- `resolve_arm_set` resolves `armed-window-85d8f2a7`; `check_set_name_rule` rc=0 (suffix from the qcdt);
  `verify_revert_set out/stage90/frozen/armed-window-85d8f2a7` — all 11 members `ok`, and the note *"this
  directory matches armed-window-85d8f2a7 exactly"*.
- `make check` rc=0. `verify_press_ready.sh` **5/5** rc=0, which resolves the live arm to this park and
  reads the entry record key `STAGE90_XNU_ENTRY_WINDOW=0x3f000000`.
- The linked `memSize` is proved by value: `xnu_entry_jump.o` `mov r8, #0x3f000000`; `stage90.bin` carries
  `0x3f000000` at 13 sites.

## 6. What the press decides

**If the premise holds**, a press of `armed-window-85d8f2a7` lets real iOS userspace map its shared cache —
launchd, then SpringBoard — with **no 915-B port**. The reading is the runner's own: `BSD root:` names the
card's HFSX volume (not `md0`), the COW serves the fixture's write, `xnu_entry_args_memSize = 0x3f000000`,
and — the new question — a userspace launchd that gets past `dyld`.

**Falsification**: XNU panics before idle (the 998 MiB map faults), or the log shows launchd still cannot
map the cache, in which case the window's *size* was not the wall and 915-B is back on the table. Either
way this press is the cheap next measurement, and it is reversible — `fastboot boot` only, never flash, and
`CARD_COW=1` means the base volume is never written.

**This is what the operator's press is for.** `scripts/press_970.sh` is the recipe (the same real-rootfs
medium as 968, the widened window, the `--expect-arm`).

## 7. What it does NOT do

It does **not** make XNU *recognize 3 GB*. `max_mem`/`mem_size`/`sane_size` are all `gMemSize` here, so
XNU still reports the window (1008 MiB), not the device total — that reader half is 958's, and the *3 GB
owned for allocation* clause is still 915-B. This arm is the **"iOS runs"** prerequisite, which 969 showed
is a different thing from clause 5c. The two can share the alloy only if a later rung unifies them; here
they are kept separate so each arm answers one question.

Nothing was pressed: **PRESS IS THE OPERATOR'S.** `33e80afe` must be unplugged; `fastboot boot` only.

## 8. Provenance

- `external/xnu-hd2-darwin13/xnu/osfmk/arm/arm_vm_init.c:333/334/362/379/422/503` (the window is RAM);
  956/969 (`mi4-956-d13-entry-window-ceiling`, `mi4-969-the-16mb-window-is-the-wall`).
- Built by replaying `out/stage90/frozen/armed-d13-7107b998/xnu_arm_entry-config.txt` with
  `STAGE90_XNU_ENTRY_WINDOW=0x3f000000`; `src/entry/build_entry.sh` (the D13 `>= 1 GiB` refusal, the
  `0x3f000000` advice); `scripts/build.sh` (the payload reads the regenerated header).
- Follows [[mi4-968-cow-writable-root-arm]] (whose switch set this arm is) and
  [[mi4-969-the-16mb-window-is-the-wall]] (whose premise it tests). Device unmodified.