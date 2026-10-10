# 970 — the window is RAM, not just address: a 484 MiB window, payload-only (2026-10-09)

969 measured the wall — the real iOS 7.1.2 `/sbin/launchd` links libSystem/libbsm out of the **301 MiB**
dyld shared cache, but XNU on every arm so far manages only the **16 MiB** entry window — and concluded the
fix was the whole-kernel **915-B** low-bank pmap port. This rung tests the *premise* of that conclusion,
because reading the D13 tree shows the window is not only an address: **`avail_end = gPhysBase + gMemSize`
makes the window the ALLOCATOR's physical-RAM end.** Widening it is payload-only, and on the mi4 it lands
in the real high bank. Arm: **`armed-window-c74bde1d`**, `STAGE90_XNU_ENTRY_WINDOW=0x1e400000` (484 MiB).

It is a **host-side arm, PARKED.** No device was touched. **PRESS IS THE OPERATOR'S** — a park is not a press.

> **Correction, same day.** The arm was first built at `0x3f000000` (1008 MiB, `armed-window-85d8f2a7`). That
> arm was **defective and unbuildable-safe**: it clobbers the entry's own RAM console (see §3). It has been
> replaced by `0x1e400000` (484 MiB), a new arm `armed-window-c74bde1d`, and `build_entry.sh` now refuses any
> window `>= 0x1e500000`. §3 is the finding that forced the correction.

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
- **the payload moves** — `stage90.bin` embeds the new immediate. Read by value from the linked object
  (not a raw byte count — an ARM `mov` immediate is *rotated*, `movw`/`movt` are halfwords, so a
  `bytes.count()` finds nothing): `xnu_entry_jump.o` disassembles to `mov r8, #0x1e400000`
  (`e3a08579`), and `stage90.elf` carries exactly one `0x1e400000` and zero `0x3f000000`.

So the arm is the **`armed-window-*`** family: the set-name suffix comes from `stage90-qcdt.img`
(`c74bde1d`) because the entry bin did not move — exactly the case `check_set_name_rule.sh`'s census row
records (memory: `mi4-build-variant-comes-from-an-env-default`).

## 3. Why `0x1e400000` and not a wider window — the console is the ceiling

**This is the finding the session's own defect produced.** D13's managed map is **fixed-base with no
clamp**: `l2_cache_to_range(managedCachePA, MANAGED_BASE = 0xC0000000, ttb, gMemSize, TRUE)` builds a
linear map of length `gMemSize` starting at exactly `MANAGED_BASE`, i.e. it spans
`[0xC0000000, 0xC0000000 + gMemSize)`. (4570 clamps its managed map below the alias base; D13 does not —
see `check_d13_managed_base.py`, the check that reads the map's own construction.)

The entry's **RAM console** — the run's only log — lives at VA `0xde500000` (`entry_stubs.c`'s
`RAM_CONSOLE_BASE`, published as the console alias). It sits *above* `MANAGED_BASE`, and it works only
because the entry installs a **section descriptor into XNU's live L1** at that slot
(`entry_live_map(RAM_CONSOLE_BASE, …)`, `entry_stubs.c:2357`). And `entry_section_install`
(`entry_stubs.c:2110`) **refuses an occupied slot**:

```c
if ((before & LIVE_TTE_TYPE_MASK) != 0u) return 0u;
```

So a managed map that **reaches** `0xde500000` occupies the very L1 slot the console needs. The install is
refused, `entry_write_kv` never runs, and the boot is **silent with no log at all** — the whole record of
the run is that console. A window that reaches the console is therefore not merely wide: it is
**unobservable**.

The GIC (`0xf9000000`), USB OTG (`0xf9a55000`), WDT (`0xf9017000`), SMCC (`0xf9824000`), GCC
(`0xfc400000`), TLMM (`0xfd500000`) and 911c's SMEM alias (`0xe0000000`) are all **above** the console and
are swallowed the same way.

The ceiling is therefore `RAM_CONSOLE_BASE − MANAGED_BASE = 0xde500000 − 0xC0000000 = 0x1e500000`
(**485 MiB**), and the **safe max is `0x1e400000` (484 MiB)** — one 16 MiB step below, with the console and
every MMIO window above it left intact. `src/entry/build_entry.sh` now **refuses** any D13 window
`>= 0x1e500000`, naming all of the above (it used to check only the **top** — the 1 GiB L1 wrap — and
advised `0x3f000000`, which is exactly the value that produces a silent boot).

| arm | window | free after `topOfKernelData` (`0x80A00000`) | console `0xde500000` |
|---|---|---|---|
| 968 | `0x01000000` (16 MiB) | `0x81000000 − 0x80A00000` = **6 MiB** | clear |
| 970 (defective) | `0x3f000000` (1008 MiB) | ~998 MiB | **clobbered → silent** |
| 970 | `0x1e400000` (484 MiB) | `0x9E400000 − 0x80A00000` = **~470 MiB** | clear |

**~470 MiB free** is enough for the 301 MiB shared cache plus the rest of iOS userspace — the number 969
said required a whole-kernel port, available with one switch that does **not** cross the console.

## 4. The arm

Same switch set as `armed-d13-7107b998` (the COW writable root over the real 896 MiB HFSX rootfs, the
full D13 USB ladder, `MEM_TOTAL=1`, `CARD_COW=1` with `HDD_WRITE=0`) with **one** switch changed:

| switch | 968 | 970 |
|---|---|---|
| `STAGE90_XNU_ENTRY_WINDOW` | `0x01000000` (16 MiB) | **`0x1e400000` (484 MiB)** |

Everything else is byte-for-byte the 968 arm. `stage90-build-config.txt` is `6c2b6038…`, identical to
968's — because the window is an **entry** build parameter that reaches the payload through the generated
header, not through a payload switch, so the payload's own record cannot see it. That is why the *entry*
record must carry the key (the `533` defect).

**Park:** `out/stage90/frozen/armed-window-c74bde1d/`, 11 members. Entry bin `7107b998…` (6331476 B,
unchanged from 968); payload `stage90.bin` `fc8929eb…` (6826884 B); qcdt `c74bde1d…` (9351168 B).
`records/revert-set.txt` carries the block. Nothing in `src/` was edited to build it — the arm is a build
*parameter*, not new code; the only code change this rung made is `build_entry.sh`'s **refusal** (§3).

## 5. Host-side verification (all green, no device)

- `resolve_arm_set` resolves `armed-window-c74bde1d`; `check_set_name_rule` rc=0 (suffix from the qcdt);
  `verify_revert_set out/stage90/frozen/armed-window-c74bde1d --set=armed-window-c74bde1d` — all 11
  members `VERIFIED`.
- The linked `memSize` is proved by value: `xnu_entry_jump.o` → `mov r8, #0x1e400000`; `stage90.elf`
  carries one `0x1e400000` and zero `0x3f000000`.
- `make check` rc=0. `verify_press_ready.sh` **5/5** rc=0, which resolves the live arm to this park and
  reads the entry record key `STAGE90_XNU_ENTRY_WINDOW=0x1e400000`.

## 6. What the press decides

**If the premise holds**, a press of `armed-window-c74bde1d` lets real iOS userspace map its shared cache —
launchd, then SpringBoard — with **no 915-B port**. The reading is the runner's own: `BSD root:` names the
card's HFSX volume (not `md0`), the COW serves the fixture's write, `xnu_entry_args_memSize = 0x1e400000`,
and — the new question — a userspace launchd that gets past `dyld`.

**Falsification**: XNU panics before idle (the 484 MiB map faults), or the log shows launchd still cannot
map the cache, in which case the window's *size* was not the wall and 915-B is back on the table. Either
way this press is the cheap next measurement, and it is reversible — `fastboot boot` only, never flash, and
`CARD_COW=1` means the base volume is never written.

**This is what the operator's press is for.** `scripts/press_970.sh` is the recipe (the same real-rootfs
medium as 968, the widened window, the `--expect-arm`).

## 6b. The press result (2026-10-09, operator-authorized) — FALSIFIED

`armed-window-c74bde1d` was pressed. **The premise did not hold: the 484 MiB window does not boot.**

- The payload runs clean to the jump — `xnu_entry_status=0x90000001`, `xnu_entry_failures=0x00000000`,
  `xnu_entry_args_memSize=0x1e400000` — and then **XNU emits nothing at all**. Not one `xnu_live_*` probe
  key, no `Darwin Kernel Version` banner. Log: `out/stage90/captures/970-484mib-window-20261009-last_kmsg.txt`
  (3932 lines, `out/` is gitignored so it is not in the tree; sha256 `fe538f69…`).
- **Where XNU dies.** The `xnu_live_*` keys are written by `--wrap`ped **very early** XNU symbols
  (`kalloc_canblock`, `thread_block`, `ml_get_max_cpus`, …), so their first appearance sits inside
  `arm_init` **before `arm_vm_init`**; the working log's `xnu_live_ttbr0=0x80800000` shows the payload's own
  TTB is still active when the probe fires (`cpu_ttb = topOfKernel + L1_SIZE = 0x80800000 + 0x4000 =
  0x80804000`). **970 emits zero probe lines**, so XNU dies in the `_start` → `arm_init` prologue, before
  the first `--wrap` target — i.e. in `_start`'s section-map loop (`osfmk/arm/locore.s:189-197`, which maps
  `memSize` = 484 sectors and is the code the window alone changes) or the `start_trampoline` MMU switch
  immediately after it.
- **Contrast.** `out/stage90/captures/cb4e17f1-20261008-last_kmsg.txt` — the same tree, the card HFSX root
  mounted, `MEM_TOTAL=1`, but window **16 MiB** — emits 12971 `xnu_live_` keys and a full boot to kalloc.
  The console cap of §3 was **not** the cause: 484 MiB kept `0xde500000` clear and the payload completed.
- **Two variables moved at once — the window is *suggested*, not *isolated*.** The entry bin `7107b998` is
  shared by 968 and 970, and **neither was ever pressed at 16 MiB** (`xnu_entry_args_pa=0x80861000` appears
  in exactly one capture: this one). So this press is simultaneously (a) the first press of the 968 entry
  *and* (b) the first window wider than 16 MiB — 911a raised the window but was never pressed. The clean
  isolation is `scripts/press_968.sh` (`armed-d13-7107b998`, same entry, same medium, 16 MiB window); it
  has **not** been run. **PRESS IS THE OPERATOR'S.**

**Conclusion.** The window-size route is **not** closed as a *cause*, but it is closed as a *cheap fix*: a
window wider than 16 MiB has never booted on this entry, and the widened arm produced no measurement at all.
Until `press_968.sh` separates the two variables, 969's wall (the window is a prerequisite, met by 915-B)
stands as the only demonstrated route to running iOS userspace. Nothing was bricked; the device re-enumerated.

## 6c. The control press (2026-10-09) — the window is NOT the cause; entry `7107b998` never boots (cause unproven, see §6d)

`armed-d13-7107b998` (the 968 arm — the **same entry bin `7107b998`**, same medium, but a **16 MiB**
window) was then pressed. **It failed identically.** The captured log
(`/tmp/cancro-last_kmsg.txt`, sha256 `56b21710…`) runs the payload clean to the jump and then XNU emits
**nothing** — zero `xnu_live_*`, no banner — exactly as 970 did. A byte-level diff of the two logs shows
only `memSize` (`0x01000000` vs `0x1e400000`) plus timing/checksum noise; the post-jump behaviour is the
same silence.

- **Therefore the window is irrelevant.** Both windows fail on entry `7107b998`; the 484 MiB map's
  arithmetic (§3, and the console cap) is not the cause — a fact confirmed independently by a read-only
  analysis that walked `arm_vm_init` end-to-end and found no window-dependent fatality below 1 GiB.
- **The failure travels with the entry bin `7107b998`**, which is shared by 968 and 970 and was **never
  pressed before** (both arms are its first press). §6d establishes this is **not** a new regression of
  one 968 switch — no D13 entry has ever booted. The last entry known to boot on this device is
  `cb4e17f1` (2026-10-08, the storage/COW line), a **different bin** (`0x632934` bytes, bss `0x5d350`,
  `topOfKernelData 0x80800000`, `SEAM_POC=1`, `ISTACK_SEPARATE=0`, no USB ladder, `MEM_SIZE_MAX` set).
- **What moved between `cb4e17f1` and `7107b998`:** the whole D13 tree-pin + USB-ladder + COW series
  (935–968) and/or its builder changes — `ISTACK_SEPARATE 0→1`, `SEAM_POC 1→0`, the four `USB_*`
  switches `0→1`, `TREE_D13` added, `MEM_TOTAL=1`+`MEM_SIZE_MAX` unset, `CARD_COW`/`HFS_ROOT_RW`/
  `FULL_EXTENT` added. The entry bin grew to `0x609c54` bytes with a **2.4 MB bss** (vs 0.38 MB).
- **Nothing was bricked; the device re-enumerated.** The bisection is host-side-build + one press per
  step; **PRESS IS THE OPERATOR'S.** The clean first cut is `cb4e17f1`'s exact switch set rebuilt through
  the current builder: if it boots, the builder is sound and a 968 switch breaks XNU's early boot; if it
  fails, the builder regressed.

## 6d. The D13 entry has NEVER booted — cause NOT ESTABLISHED (all six static hypotheses refuted or unproven)

The control press established the window is not the cause (§6c). Six independent read-only
investigations were run against the real ELFs, **each then adversarially re-verified by a second agent
tasked to refute it.** The honest outcome: **no hypothesis survived as a cause.** The earlier revision of
this section named D13's `__start` MMU fast path as the "CONFIRMED cause"; that claim was **REFUTED by
the verifier** and is retracted below. ⚠️ The commit that carried it (`3662bcf`) and the first memory
draft were **overclaiming** — corrected here.

**Fact that survives: the whole Darwin-13 entry line has never run.** The two D13 presses (968's
`armed-d13-7107b998`, 970's `armed-window-c74bde1d` — the *same* entry bin, byte-identical
`7107b998…`, 6331476 B) both produced zero XNU output. Every capture in the tree with real XNU output
(`cb4e17f1-20261008`, `908-*`, `909-*`, all `rung*`) is a **4570-tree** entry (no `STAGE90_XNU_TREE_D13`).
So this is not a regression of one 968 switch — the D13 *entry→kernel* line has never booted on hardware.

**What was REFUTED (each verified by value against the linked image, not a comment):**

| hypothesis | verdict | evidence |
|---|---|---|
| H1 a branch to an unresolved (0/GOT/stub) address | **REFUTED** | every reachable `bl` target and literal in `__start`→`arm_init` resolves to real linked `T` code; the 3 out-of-segment `blx` sit after `bx lr` (unreachable padding); no relocations, no .plt/.got |
| H2 boot_args offsets wrong (entry reads garbage) | **REFUTED** | D13 read offsets {2,4,8,12,16} == the payload's write offsets; D13 `assym.s`: VERSION=2, VIRTBASE=4, PHYSBASE=8, MEMSIZE=12, TOP_OF_KERNEL=16 |
| H5 the 935–937 entry-source tree-pin regressed the entry | **REFUTED** | the entry chain `entry_write`→`entry_live_write`→`entry_live_init`→`entry_live_map` is at **identical addresses and byte-identical** in the D13 and 4570 ELFs; symbol-set diff is empty both ways |
| H6 `__start` maps a broken page table | **REFUTED** | the map loop makes every descriptor VA==PA (`virtBase==physBase==0x80000000`); PC, intstack, vectors all land in index 0x800/0x805, all mapped |

**What was NOT-ESTABLISHED (the hunt agents called them CONFIRMED; the adversarial verify killed that):**

- **H4 — "D13's `__start` MMU fast path skips the VBAR/vector install → silent fault."** The *premises*
  reproduce exactly (D13 `beq mmu_initialized` at `80000014`; the image's **only** VBAR `mcr` at
  `800000fc`, inside `fix_boot_args_hack`, is **after** the branch; 4570 has **0** VBAR writes; the
  payload enters with SCTLR.M=1 and its **own** VBAR=0x80a0). **But the causal step is contradicted:**
  if the fast path is taken, the payload's L1 (TTBR0 stays `0x6c4000`) and VBAR `0x80a0` stay active, so
  the payload's **own** loud handlers (`src/vectors.S` prints "MI4IOS6_STAGE90 exception: …";
  `xnu_arm_vm_init_high_va_data_abort_handler.c:142` prints "data abort: dfar=…") would fire on any
  fault — **yet the capture shows zero output after the jump.** And 4570 boots fully while writing VBAR
  **0** times, so a skipped VBAR write is **not** the discriminator. **REFUTED as the cause.**
- **H3 — "the empty log ⇒ a fault strictly before `processor_init`."** The measurement distinction
  reproduces (D13's first would-be console key is `processor_init`'s wrapped `timer_call_setup`), **but
  the log cannot separate the two cases:** `entry_live_write` calls `entry_live_init`, which has refuse
  paths (`entry_stubs.c:2305/2328/2348/2365/2371 → entry_live_refuse`, silent); a **refused console
  install is silent too.** So "XNU faulted early" and "XNU runs but the console was refused" are
  **indistinguishable by this log.**

**The surviving open question (the real next measurement).** Two mechanisms remain, and the current log
cannot tell them apart:
1. **XNU faults early and the payload's vectors did NOT print** (which itself contradicts the "loud
   handlers" premise — so this branch requires explaining why the payload's VBAR was not active), or
2. **the payload's identity L1 occupies the console's L1 slot** (`src/mmu.c:5478` maps `0xde500000`;
   `entry_live_map` finds the slot taken → `entry_live_refuse(2u)` → every record dropped, and the run
   is simply **unobservable** — not necessarily a crash).

The discriminator must be a measurement **not masked by the console install**: e.g. a pre-console
raw-store marker written in `processor_init`/`arm_init` that does not go through `entry_live_write`
(so a refusal cannot swallow it), or capturing the fault PC/abort path directly. Building that marker is
host-side and reversible; the press to observe it is the operator's. **The window remains a separate,
later question; the D13 `locore.s` fast path is a real structural difference but is no longer held to be
the cause.**

## 7. What it does NOT do

It does **not** make XNU *recognize 3 GB*. `max_mem`/`mem_size`/`sane_size` are all `gMemSize` here, so
XNU still reports the window (484 MiB), not the device total — that reader half is 958's, and the *3 GB
owned for allocation* clause is still 915-B. This arm is the **"iOS runs"** prerequisite, which 969 showed
is a different thing from clause 5c. The two can share the alloy only if a later rung unifies them; here
they are kept separate so each arm answers one question.

Nothing was pressed: **PRESS IS THE OPERATOR'S.** `33e80afe` must be unplugged; `fastboot boot` only.

## 8. Provenance

- `external/xnu-hd2-darwin13/xnu/osfmk/arm/arm_vm_init.c:333/334/362/379/422/503` (the window is RAM);
  956/969 (`mi4-956-d13-entry-window-ceiling`, `mi4-969-the-16mb-window-is-the-wall`).
- `src/entry/entry_stubs.c` (`RAM_CONSOLE_BASE 0xde500000u`; `entry_section_install` refusing an occupied
  slot; the console section installed into XNU's live L1); `tools/check_d13_managed_base.py` (D13's
  fixed-base, unclamped map).
- Built by replaying `out/stage90/frozen/armed-d13-7107b998/xnu_arm_entry-config.txt` with
  `STAGE90_XNU_ENTRY_WINDOW=0x1e400000`; the payload rebuilt with
  `STAGE90_EXTRA_CFLAGS='-DSTAGE90_XNU_ENTRY=1'` (a plain `./build.sh` leaves `STAGE90_XNU_ENTRY` **off**
  by default, so it would neither jump into XNU nor match `6c2b6038`); `src/entry/build_entry.sh` (the new
  `>= 0x1e500000` console refusal).
- Follows [[mi4-968-cow-writable-root-arm]] (whose switch set this arm is) and
  [[mi4-969-the-16mb-window-is-the-wall]] (whose premise it tests). Device unmodified.