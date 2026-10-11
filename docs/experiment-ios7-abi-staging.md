# The iOS7 ABI is a build switch, not a runtime boot arg — staging plan (critic item M5)

**Goal clause:** 「完全正常跑iOS7」 (run iOS 7 completely normally).
**Deliverable:** ANALYSIS + DESIGN. No source is edited by this document; every edit named below is
for a *later* lane.
**Repo:** `/mnt/data/mi4-ios6`, git `master`. D13 tree = `external/xnu-hd2-darwin13/xnu`.

## 0. Why this doc exists (the M5 gap)

The clause has no live workstream. Three designs that proposed to carry it were all refuted, and each
refutation left the same thing standing: the D13 kernel **compiles out the entire iOS7-ABI adapter
set**, and nothing in the build says so.

This document fixes the framing those three designs got wrong:

> `IOS7_COMPAT_PLATFORM` is **not** a runtime property. It is a **preprocessor** gate. The sibling
> `ios7lab_vmabi` boot arg lives *inside* the enabled bodies; it is a **secondary** switch that can
> only ever be reached after the primary, compile-time gate is already true. A boot arg cannot turn
> the gate on, cannot even be compiled in, when the gate is false.

Every site below is `#if IOS7_COMPAT_PLATFORM` / `#if BOARD_CONFIG_QSD8250_LEO` etc. — all
compile-time. The payload's own `CommandLine` (`src/boot_args.c:48`,
`src/xnu_boot_args_conformant.c:191`) carries no `ios7lab_vmabi` token today, and
`grep -rn ios7lab_vmabi src/ scripts/ tools/` returns **nothing** — but that is the *smaller* half of
the problem.

## 1. The define mechanism, and the exact edit a later lane must make

`osfmk/arm/ios7_compat.h:5`:

```c
#define IOS7_COMPAT_PLATFORM (BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO)
```

`BOARD_CONFIG_MSM8974_CANCRO` is defined by the build (`tools/build_xnu_arm_kernel.sh:202`,
`-DBOARD_CONFIG_MSM8974_CANCRO=1`) and is **not** in that disjunction ⇒ the gate is `0` for cancro ⇒
all 30 sites fall to their `#else`.

### Decision: edit the definition in `ios7_compat.h:5`, and keep it board-shaped

Chosen form (for a later lane; **not applied here**):

```c
#define IOS7_COMPAT_PLATFORM \
    (BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO || BOARD_CONFIG_MSM8974_CANCRO)
```

**Why this and not `-DIOS7_COMPAT_PLATFORM=1`.** `ios7_compat.h` is included by **all 12** carrier
files (verified: each of the 12 `#include <arm/ios7_compat.h>` exactly once). A `-D` on the command
line would also reach every file, but it has three costs the edit does not:

1. `-D` makes the *build script* the source of truth for a kernel ABI decision, and
   `tools/check_d13_board_staged.sh`'s whole purpose is that a re-provisioned `external/` cannot
   silently disagree with the build. A `-D` is invisible to a source re-derivation.
2. `-D` fights the `nokextd` board gate (`tools/stage_d13_board.sh`): the tree is already
   board-shaped, and the other 17 sibling gates (`BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO`,
   §4) would still be false. Adding a macro that does not match the board vocabulary creates a
   *second* mechanism that only some gates use — the "one value, two definitions" class
   ([[mi4-one-value-two-definitions]]).
3. The edit is the one already staged twice by the same script pattern
   (`stage_d13_board.sh` sentinel `MSM8974_CANCRO`), so it is idempotent and drift-checkable.

**Why not per-file.** Per-file defines for 12 files is 12 places to drift, and `ios7_compat.h` is
already the single include point. Per-file would also leave `ios7_compat_vmabi_enabled()`
(`:11-21`) — which is compiled from the *same* header — answering `0` for cancro, silently
disabling the runtime half even where the compile half was on. The header must be one edit.

### The build refusal ([[mi4-a-claim-in-a-comment-is-not-a-check]])

A comment saying "true for cancro" is not a check. The gate must **stop the build**, and the
implementation must name the mechanism that makes it true. Two layers:

**Layer 1 — `#error` in the tree, adjacent to the definition** (in `ios7_compat.h`, after `:5`):

```c
#if !IOS7_COMPAT_PLATFORM
#error "IOS7_COMPAT_PLATFORM is false for this board; the iOS7 ABI is compiled out"
#endif
```

This is the refusal. It is *the definition's own value*, not a restatement of it: if a later edit
narrows the disjunction, the file refuses to compile. It converts the current silent
`0` (which the 977/979 line lived with) into a hard stop.

**Layer 2 — `tools/check_d13_ios7_abI_staged.sh`, wired into `make check`** (sibling of
`tools/check_d13_board_staged.sh:184`), re-deriving the source property the way the other D13 checks
do — no compiler, no device:

- (a) `ios7_compat.h`'s `IOS7_COMPAT_PLATFORM` expansion **names `BOARD_CONFIG_MSM8974_CANCRO`**;
- (b) the `#error` guard on `!IOS7_COMPAT_PLATFORM` is **present** (a definition with no refusal is
  the defect: it lets the disjunction be narrowed silently);
- (c) the build defines `-DBOARD_CONFIG_MSM8974_CANCRO` (already checked by
  `check_d13_board_staged.sh`, cited not duplicated);
- (d) **the site census is re-derived, not stored.** The check counts `#if IOS7_COMPAT_PLATFORM`
  sites across the 12 carriers and against a **committed baseline count of 30** (the number §2 reads
  today). Any change to the count — a site added by a refactor, or a site *lost* by a bad edit —
  fails the check and forces the baseline to be updated deliberately. A hard-coded `30` that is
  never compared to the tree would be the defect; a hard-coded `30` that *is* the comparison is the
  check. R0's falsification tests exactly this.

**(d) is the load-bearing one.** The abi-shim proposal died because it asserted on a string that
never recurs (line 5 *is* the definition). This check asserts on a **count it re-derives**, so it
cannot be satisfied by prose.

## 2. The complete `IOS7_COMPAT_PLATFORM` site census — 30 sites, 12 files

Read by value from the tree (`grep -c '#if IOS7_COMPAT_PLATFORM'` = **30**, over **12** files). Every
one is compile-time and every carrier includes the header once (verified 12/12). The verifier's "≥40"
is the union of this family and the sibling family in §4: **30 + 16 = 46**.

| # | file (under `external/xnu-hd2-darwin13/xnu/`) | line(s) | what the guarded body does |
|---|---|---|---|
| 1 | `osfmk/arm/status.c` | **:62** | user stack: `LAB_USER_STACK` = `ios7lab_user_stack()` (`:63-70`) vs plain `USRSTACK` (`:59` `0x2FE00000`) |
| 2 | `osfmk/kern/syscall_sw.c` | **:102** | declares the 5 trap shims `ios7lab_mach_vm_map_trap`, `…port_construct`, `…port_destruct`, `…port_guard`, `…port_unguard` (`:103-107`) |
| 3 | `osfmk/kern/syscall_sw.c` | **:126** | mach trap **15** = `ios7lab_mach_vm_map_trap` vs `kern_invalid` (`:127/:129`) |
| 4 | `osfmk/kern/syscall_sw.c` | **:139** | mach trap **24** = `ios7lab_mach_port_construct_trap` vs `kern_invalid` |
| 5 | `osfmk/kern/syscall_sw.c` | **:144** | mach trap **25** = `ios7lab_mach_port_destruct_trap` vs `kern_invalid` |
| 6 | `osfmk/kern/syscall_sw.c` | **:164** | mach traps **41/42** = `ios7lab_mach_port_guard/unguard_trap` vs `kern_invalid` (`:165-169`) |
| 7 | `osfmk/kern/ipc_kobject.c` | **:90** | includes `libsa/string.h`, `pexpert/pexpert.h`, `ios7lab_iokit_wire.h`, `ios7lab_named_map_wire.h`, `ios7lab_remap_wire.h` (`:91-95`) |
| 8 | `osfmk/kern/ipc_kobject.c` | **:340** | locals `lab_iokit64`, `lab_context64`, `lab_named_map64`, `lab_remap64` (`:341-344`) |
| 9 | `osfmk/kern/ipc_kobject.c` | **:375** | **request** 64-bit-isation: msgIDs 4813 (remap), 4811 (named-map), 3228/3229 (context), 2865 (IOKit) — each `reply_size += 4` (`:376-407`) |
| 10 | `osfmk/kern/ipc_kobject.c` | **:462** | **reply** 64-bit-isation: `ios7lab_remap_reply64`, `…named_map_reply64`, `…context_reply64`, `…iokit_reply64` (`:463-502`) |
| 11 | `osfmk/ipc/mach_kernelrpc.c` | **:39** | includes + `struct ios7lab_vm_allocate_wire` and the whole legacy trap-10 5-word ABI (`:40-…`) |
| 12 | `osfmk/vm/vm_user.c` | **:126** | `ios7lab_remap_allowed_flags()` = `VM_FLAGS_USER_REMAP \| VM_FLAGS_RETURN_DATA_ADDR` when vmabi on (`:127-133`) |
| 13 | `osfmk/vm/vm_user.c` | **:1066** | `vm_map_remap` flag filter uses that helper (vs `VM_FLAGS_USER_REMAP`) (`:1067/:1069`) |
| 14 | `osfmk/vm/vm_user.c` | **:1144** | same filter in the 64-bit entry point (`:1145/:1147`) |
| 15 | `osfmk/vm/vm_shared_region.c` | **:626** | **the cache base/size**: `0x2C000000U` / `0x14000000U` (`:633-634`) — see §3 |
| 16 | `osfmk/vm/vm_map.c` | **:11440** | locals `data_offset`, `return_data_address` (`:11441-11443`) |
| 17 | `osfmk/vm/vm_map.c` | **:11460** | `VM_FLAGS_RETURN_DATA_ADDR` handling, page-offset span arithmetic (`:11461-11475`) |
| 18 | `osfmk/vm/vm_map.c` | **:11539** | `*address += data_offset` after a successful remap (`:11540-11545`) |
| 19 | `bsd/kern/kern_fork.c` | **:1098** | `lab_reserve_early_pids` (`:1099`) |
| 20 | `bsd/kern/kern_fork.c` | **:1146** | skip PIDs 2..9 so `getpid() >= 10` for the iOS7 CF/XPC initializer (`:1152-1153`) |
| 21 | `bsd/kern/kern_descrip.c` | **:1545** | raise `CS_MAX_BLOB_SIZE` (`:1543` `1280 KiB`) to 2 MiB for `dyld_shared_cache_armv7` + vmabi (`:1550-1554`) |
| 22 | `bsd/kern/kern_exec.c` | **:1447** | spawn-port action walk: `PSPA_IMP_WATCHPORTS=3` importance hint (`:1448-…`) |
| 23 | `bsd/dev/arm/unix_syscalls.c` | **:95** | `#include <sys/fcntl.h>` for the guarded kqueue |
| 24 | `bsd/dev/arm/unix_syscalls.c` | **:212** | `ios7lab_psynch_stack_tail()` — 4 extra 32-bit arg words at user `SP+28` (`:213-230`) |
| 25 | `bsd/dev/arm/unix_syscalls.c` | **:349** | `lab_guarded_queue = code == 443 && vmabi` (`:350`) |
| 26 | `bsd/dev/arm/unix_syscalls.c` | **:372** | psynch tail applied for syscalls 303/304/305 (`:373-377`) |
| 27 | `bsd/dev/arm/unix_syscalls.c` | **:434** | dispatch: psynch error, else `ios7lab_guarded_kqueue` (`:435-440`) |
| 28 | `bsd/vm/vm_unix.c` | **:101** | `ios7lab_cache_abi()` (`:102-106`) |
| 29 | `bsd/vm/vm_unix.c` | **:1130** | `shared_region_check_np` copyout widens to **`uint64_t address64`** (`:1131-1133`) — see §3 |
| 30 | `bsd/vm/vm_unix.c` | **:1174** | `shared_region_copyin_mappings` reads a **32-byte** `{u64,u64,u64,u32,u32}` wire (`:1175-1180`) |

The rows above are not 30 distinct files — sites 2–6 are five `#if`s in one file. The exact
distribution (`grep -c '#if IOS7_COMPAT_PLATFORM'` per file) is:

`syscall_sw.c`×5, `unix_syscalls.c`×5, `ipc_kobject.c`×4, `vm_map.c`×3, `vm_user.c`×3,
`vm_unix.c`×3, `kern_fork.c`×2, `status.c`×1, `mach_kernelrpc.c`×1, `kern_descrip.c`×1,
`kern_exec.c`×1, `vm_shared_region.c`×1 = **30**.

### The four that name an ABI number the cache must match

Of the 30, exactly **four** carry a numeric address/size that the real iOS 7 cache fixes:

- `vm_shared_region.c:633` `base_address = 0x2C000000U`
- `vm_shared_region.c:634` `size = 0x14000000U`
- `status.c:67` user stack `0x2BE00000U`
- `vm_unix.c:1130-1133` the `check_np` copyout that must be **8 bytes**, not 4

Everything else is behaviour (traps, wire formats, PID policy) with no magic constant.

## 3. The real cache ABI — measured from the actual rootfs, by value

**The real dyld cache is present on this host, outside the repo:**
`/mnt/data/ios7-payload/v2/ios7/rootfs.hfs` (939,524,096 B) and its identical v1 twin. Parsing the
cache header at file offset `0x455d000`:

```
magic             'dyld_v1   armv7'
mappingOffset     0x68    mappingCount  3
imagesOffset      0xc8    imagesCount   696
dyldBaseAddress   0x2be00000
codeSignatureOffset 0x12bda000   codeSignatureSize 0x0
map[0] addr=0x2c000000 size=0x0c266000 maxp=5 initp=5   (r-x)
map[1] addr=0x38268000 size=0x01ce0000 maxp=3 initp=3   (rw-)
map[2] addr=0x39f48000 size=0x022ed000 maxp=1 initp=1   (r--)
span 0x2c000000..0x3c235000 = 258.2 MiB
```

**Facts the shim must yield for iOS 7.1.2:**

| quantity | value | source |
|---|---|---|
| cache mapping base | **`0x2C000000`** | `map[0].address`, measured |
| cache mapping span | **`0x10235000`** (258.2 MiB), top `0x3C235000` | `map[2].end − map[0].start` |
| `dyldBaseAddress` | **`0x2BE00000`** | header `+0x20`, measured |
| user stack (below the cache) | **`0x2BE00000`** | equals `dyldBaseAddress`; `status.c:67` |
| `libSystem.B.dylib` load VA | `0x37267000` | image table, **inside `map[1]`** |
| `libsystem_c` / `_kernel` / `_pthread` / `_malloc` VAs | `0x38140000` / `0x381c5000` / `0x3823f000` / `0x381fe000` | image table |

**Cross-check against the tree's defaults — the shim yields the measured left column, the default is
the right:**

| quantity | shim (iOS7) | today's `#else` | tree site |
|---|---|---|---|
| shared region base | `0x2C000000` | `SHARED_REGION_BASE_ARM` = **`0x30000000`** | `mach/shared_region.h:67` |
| shared region size | `0x14000000` (320 MiB) | `SHARED_REGION_SIZE_ARM` = **`0x10000000`** (256 MiB) | `mach/shared_region.h:68` |
| nesting base/size | `0x30000000` / `0x10000000` (unchanged by the shim) | same | `shared_region.h:69-70` |
| user stack | `0x2BE00000` | `USRSTACK` = **`0x2FE00000`** | `status.c:59,67` |
| `check_np` copyout width | **8 B** (`uint64_t`) | 4 B (`start_address`) | `vm_unix.c:1130-1138` |
| mapping wire stride | **32 B** `{u64,u64,u64,u32,u32}` | `sizeof(shared_file_mapping_np)` | `vm_unix.c:1176-1180` |

### Four findings this measurement produces (all latent, all for the fix lane)

**(a) The shim's shared-region base is inconsistent with its own `check_np` width.**
`vm_unix.c:1131-1133` decides the 64-bit wire by `ios7lab_cache_abi()`, which is `vmabi`. But the
cache is placed by `vm_shared_region.c:633-636`, which sets `base_address`/`size` to the *same* iOS7
values whenever `vmabi` is on. So whenever the wire is 64-bit, the base is **also** moved to
`0x2C000000` — yet the code comment at `vm_shared_region.c:630-632` says *"Keep the existing aligned
nesting range at 0x30000000; vm_shared_region_enter handles the 64 MiB prefix."* That comment
describes a base of `0x30000000` while the code three lines below sets `0x2C000000`. **The comment
and the assignment disagree**, and the shim cannot both `0x2C000000` and `0x30000000`. The measured
`map[0].address` is unambiguous: it must be **`0x2C000000`**, so the comment is the wrong half. This
is the "a claim in a comment is not a check" class again — the fix lane should delete the comment
sentence rather than trust it.

**(b) The cache size 258.2 MiB needs the 484 MiB window.**
`map[0]` alone is 194.4 MiB, spanning `0x2C000000..0x38266000`, plus the two following maps to
`0x3C235000`. Only **975's 484 MiB `ENTRY_WINDOW=0x1e400000`** ([[mi4-975-window-on-the-working-entry]])
covers it; the 16 MiB default does not. So §2's shim work is *necessary but not sufficient* — it is
gated on 975's window arm, which is itself gated on the 979 `arm_vm_entry` fix. The ladder below
orders this explicitly.

**(c) The `armv7s` cache name and the `kern_descrip.c:1550` string differ.** The task names
`dyld_shared_cache_armv7s`; the actual file in both v1 and v2 rootfs is the **`armv7`** cache
(`grep -c dyld_shared_cache_armv7s` over the whole rootfs = **0**), and `kern_descrip.c:1550` compares
`"dyld_shared_cache_armv7"` — which **matches the real file**. The `armv7s` spelling is a phantom; the
code is right and the task's string is stale. Do not "fix" the code toward `armv7s`.

**(d) The `kern_descrip.c` signature allowance is real and correctly sized.** `CS_MAX_BLOB_SIZE` =
`1,280 KiB` = `1,310,720` B; the actual trailing signature is `file_size − codeSignatureOffset` =
`315,952,666 − 314,417,152` = **`1,535,514` B**, matching the comment at `:1546` exactly, and
exceeding `CS_MAX_BLOB_SIZE` by 224,794 B. The 2 MiB allowance is required, and the file's
`init_prot`/`max_prot` decode cleanly from the same cache.

## 4. The sibling vmabi gates — the other half of the "≥40" (16 sites, 6 files)

These are *not* `IOS7_COMPAT_PLATFORM` but the same board disjunction, and they are compiled out for
cancro identically. They are the sites the launchd-exec proposal missed. The sentinel
`BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO` occurs **17 times** across **7 files** — one of
those is `ios7_compat.h:5`'s own definition, so **16 real gate sites across 6 files**:

| file | lines |
|---|---|
| `osfmk/arm/pmap.c` | **205** (commpage alias const), **2848** (alias `pmap_enter`), **3242**, **3434** (alias PTE attrs) |
| `osfmk/arm/traps_lo.s` | **116**, **162**, **264** — the ARMv7 `-3` SWI timebase fallback (`ios7lab_timebase`) |
| `bsd/kern/pthread_synch.c` | **228** (stack `0x20000000` vs `0x2FD00000`), **360**, **370** (thumb-bit `cpsr`/`pc`), **1082**, **2308** |
| `osfmk/arm/commpage/commpage.c` | **57**, **99** — see §5 |
| `osfmk/arm/hw_lock.s` | **457** — `orr r3,r3,#3` (interlock + wake bit) vs `#0` |
| `osfmk/mach/arm/vm_param.h` | **63** — **`VM_MAX_ADDRESS 0x40000000`** vs `0x80000000` |

`osfmk/mach/arm/vm_param.h:63-69` is the **load-bearing** sibling: with the 1 GiB TTBR0 split the
user address ceiling is `0x40000000`, so `0x2C000000 + 0x10235000 = 0x3C235000` still fits. If a
lane ever widens the user map, this gate and the shim's `vm_unix.c:1191` bound check
(`address + size > 0x40000000`) must move together — they are one value with two definitions.

## 5. The commpage CPUFAMILY gate — exactly what a non-Apple CPU publishes

`osfmk/arm/commpage/commpage.c`, read by value:

```c
:87  #if BOARD_CONFIG_QSD8250_LEO
:88      *(uint32_t*)_COMM_PAGE_CPUFAMILY = CPUFAMILY_UNKNOWN; /* Scorpion has no matching Apple family ID. */
:89  #else
:90      *(uint32_t*)_COMM_PAGE_CPUFAMILY = CPUFAMILY_ARM_13;  /* Cortex-A8 */
:91  #endif
```

`_COMM_PAGE_CPUFAMILY` is the VA **`0x40000080`** (`osfmk/arm/cpu_capabilities.h:47`);
`CPUFAMILY_UNKNOWN` = **`0`** and `CPUFAMILY_ARM_13` = **`0x0cc90e64`** (`osfmk/mach/machine.h:383`,
`:399`). Two more gates in the same file:

```c
:43/57/99   #if BOARD_CONFIG_QSD8250_LEO / (ARMPBA8 || QSD8250_LEO)
:100-104    cache line size from ml_cpu_get_info(), asserted 0 < n <= UINT16_MAX, published at
            _COMM_PAGE_BASE_ADDRESS+0x26
:57-67      LOGICAL/ACTIVE/PHYSICAL cpus as FOUR bytes at +0x22,+0x34,+0x35,+0x36 with
            assert(processor_avail_count == 1)   [vs the plain uint32 at _COMM_PAGE_NCPUS]
```

**What the gate does for a non-Apple CPU.** For cancro the whole block at `:87-91` again falls to the
`#else` at `:90` — so cancro *would* publish `CPUFAMILY_ARM_13` (`0x0cc90e64`, "Cortex-A8"), the
value the gate reserves for its *non*-QSD8250 boards. That is the current D13 answer: a Cortex-A8
family ID for Krait 400. The gate's intent (per `:88`'s comment) is that a Qualcomm part with no
Apple family should publish `CPUFAMILY_UNKNOWN` (`0`).

**Why `CPUFAMILY_UNKNOWN` breaks iOS7 userspace.** `dyld` and `libsystem_platform` read
`_COMM_PAGE_CPUFAMILY` to select the CPU-specific `memcpy`/`memmove`/`strncmp` variants and the
`mach_absolute_time` fast path. `CPUFAMILY_UNKNOWN` (`0`) matches **no** variant table, so libSystem
falls back to — at best — a generic C path and, on the iOS 7 `armv7` cache, to a **dispatch on a
family id with no entry for `0`**. The measured cache is the `armv7` one (not `armv7s`), so its
selected baselines are the armv6/armv7 generic set; publishing `ARM_13` (Cortex-A8) is *closer* to
correct for armv7 code than `0` is, because A8 supports the NEON/`swp` subset those baselines assume,
whereas `0` selects nothing. **The clean answer is neither:** a real Krait 400 is `Cortex-A9` class
(`PlatformConfigs.h:42-45` already maps cancro to `__ARM_PROCESSOR_CLASS_CORTEX_A9__` /
`QUALCOMM_A9__`), so the right value is a *chosen, asserted* family the cache actually keys armv7
code against — not `0`, and not the A8 `#else` accident.
The gate must be widened so cancro gets a **chosen, asserted** value rather than the A8 accident —
and the `:108-112` `printf` already prints configured/active/physical/logical cpus, so the check can
be observed on the console.

Secondary: `:60`'s `assert(processor_avail_count == 1)` and the four-byte CPU block are gated out for
cancro, so D13 publishes only the plain `uint32_t` at `_COMM_PAGE_NCPUS` (`0x40000022`). The HD2 lab
comment (`:58-59`) says libdispatch sizes its heap from the **+0x36 byte**; cancro does not publish
`+0x34/0x35/0x36` at all, so if libdispatch is the same build, it reads **zeroed** commpage bytes.
**This is a second commpage gate hiding in the same file** and belongs in the same rung as CPUFAMILY.

## 6. Ordered rungs — each testable, each with its falsification

Each rung is a single switch set. `device?` = needs an operator press. **PRESS IS THE OPERATOR'S.**

| rung | what it changes | host-testable verdict | falsification | device? |
|---|---|---|---|---|
| **R0** | `ios7_compat.h:5` + `#error` + `check_d13_ios7_abi_staged.sh` in `make check` | `make check` passes; the 30-site / 16-gate counts re-derive; a narrowed disjunction refuses to compile (negative test) | the `#error` fires on cancro (means the disjunction edit did not land) or the census check passes after deliberately adding a 31st site (means (d) is a stored number, not re-derived) | no |
| **R1** | `commpage.c:87-91` + the `+0x34/0x35/0x36` block, widened to cancro with an explicit family id | host: diff the compiled `commpage.o`'s `_COMM_PAGE_CPUFAMILY` store against the chosen constant; **no** CPUFAMILY choice may be a bare `#else` | a boot log shows `IOS7LAB commpage` absent or CPUFAMILY not printed | no (host) → yes (observe) |
| **R2** | the 4-number block: `vm_shared_region.c:633-634`, `status.c:67`, and **delete** the `:630-632` comment sentence | host: `nm`/`objdump` the two objects, assert the literals `0x2C000000`/`0x14000000`/`0x2BE00000` appear; assert they equal the measured cache `map[0].address`/span | the compiled constant differs from `0x2C000000`; or `check_np` still copies 4 bytes while the base moved | no |
| **R3** | machine traps 15/24/25/41/42 (`syscall_sw.c`) + the ipc_kobject request/reply 64-bit-isation + `mach_kernelrpc.c` + `vm_user.c`/`vm_map.c` remap | build links: the five `ios7lab_*_trap` symbols become **defined** (`nm` on the D13 link), not just `extern` | the link closes but `magic`-numberless: a trap slot still resolves to `kern_invalid` | no |
| **R4** | `unix_syscalls.c` psynch tail + guarded kqueue; `vm_unix.c:1174` 32-byte wire; `kern_fork.c` early-PID reserve; `kern_exec.c` spawn ports; `kern_descrip.c` 2 MiB blob | build + host decode of the wire struct (`_Static_assert` on `sizeof == 32`, already present at `vm_unix.c:1180`) | the `_Static_assert` fires (struct grew/shrunk) | no |
| **R5** | the `ios7lab_vmabi=1` token added to `CommandLine` (`src/boot_args.c:48` and `src/xnu_boot_args_conformant.c:191`) | the two `_Static_assert`s on `sizeof(cmd) <= BOOT_LINE_LENGTH` (`boot_args.c:49`) and the conformance check still pass — **the 255/256-byte budget is the whole risk** | the string no longer fits ⇒ the token is silently truncated off the tail (the 515 failure mode) | no |
| **R6** | fire on device | — | console shows `IOS7LAB shared region 0x2c000000..0x40000000` (`vm_shared_region.c:635`) and `IOS7LAB commpage cpus …` | **yes** |

**R6 is gated on 979 + 975.** Per [[mi4-979-xnu-panicked-in-ipc-bootstrap]] / [[mi4-975-window-on-the-working-entry]]:
the last real xnu boot died at `lock_set_init+0` and the 484 MiB window is only needed once userspace
actually maps the cache. R6 cannot be observed before the 979 fix is pressed and the window arm is
real, and the 258.2 MiB measured span **proves** it needs the window — the 16 MiB default is
insufficient by construction.

## 7. The single highest-value first rung

**R0 — the build refusal.** It is highest-value because:

1. It is the *only* rung whose failure mode is currently **silent**. Every other rung's absence shows
   up as a missing symbol or a wrong constant; R0's absence is a kernel that compiles, links, boots,
   and has 30 adapter sites quietly `#else`d with no diagnostic anywhere. That is the exact class the
   memory index calls out ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
2. It makes the **other 29 sites addressable**. With the gate on, the D13 build immediately gains 30
   compiled-in sites, and `nm` on the link reveals which shim symbols are still undefined — turning
   the "≥40 un-staged sites" from an estimate into a build-verifiable list.
3. It needs **no device** and **no compiler risk**: it is a preprocessor edit plus a source check, and
   it composes with `tools/check_d13_board_staged.sh`'s existing pattern rather than inventing one.
4. It is **falsifiable on the host**: add a 31st `IOS7_COMPAT_PLATFORM` site behind the gate; if
   `make check` still passes, check (d) is a stored number and the refusal is theatre.

R1 (commpage) is the strong second: it is the other gate the launchd-exec refutation named, and the
`CPUFAMILY_UNKNOWN` hazard is a userspace-visible wrong answer, not a build error.

## 8. What this design explicitly does NOT claim

- **Not that the gate alone boots iOS.** R0–R5 compile the adapter set in; §3(b) shows the cache
  still needs the 484 MiB window, so R6 is downstream of 975/979.
- **Not that per-file defines are wrong-only.** They are merely 12× the drift surface; the header is
  the single include point (verified 12/12) and should be edited there.
- **Not that `CPUFAMILY_UNKNOWN` is the goal.** §5 argues the *opposite*: the right value is the one
  the `armv7` cache keys armv7 code against, and a bare `#else` is how cancro got the A8 accident.
- **Not that the `armv7s` name is real.** §3(c): measured zero occurrences; the code's `armv7` string
  is correct.