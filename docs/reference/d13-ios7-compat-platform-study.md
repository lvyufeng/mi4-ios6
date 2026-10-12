# D13 `IOS7_COMPAT_PLATFORM` study — 31 uses / 13 files (recon + design memo)

Read-only reconnaissance of `external/xnu-hd2-darwin13/xnu` (paths below relative to it unless prefixed). No build, no device, no git; one file written (this one). Pinned revision (sha256): `osfmk/arm/ios7_compat.h` `420d9d9e…`, `osfmk/kern/syscall_sw.c` `90b7125c…`, `osfmk/ipc/mach_kernelrpc.c` `1126ac35…`, `bsd/dev/arm/unix_syscalls.c` `2376ffbc…`, `osfmk/kern/ipc_kobject.c` `cde22fb5…`, `osfmk/arm/traps_lo.s` `b16e7a66…`.

## Method (all counts machine-read, never visual)
- `grep -rn IOS7_COMPAT_PLATFORM . | grep -v /out/` = **31 lines / 13 files**; `grep -rl` = same 13; token occurs in no `.s` and in no other file type.
- A `python3 -I` walker dumped every guard block with `#if/#else/#endif` spans to JSON (31 sites, 13 files, digest `062f97b9…`); block spans and per-file counts below come from it, not from reading.
- Literals double-checked with exact-string `grep -c` (each = 1) and header greps. Note the two true conventions: **31** = every line containing the token (incl. the `#define` at `ios7_compat.h:5`); **30 `#if` sites over 12 carriers** (`docs/experiment-ios7-abi-staging.md:105` uses this one). A checker must state which it re-derives.

## 1+2. Every use, guarded code in one line, classified
Class: **KB** = kernel-behavior, **UA** = user-visible ABI, **D** = declaration/instrumentation-only (gated).

| # | file:line | guarded code | class |
|---|---|---|---|
|1|`osfmk/arm/ios7_compat.h:5`|the definition `(BOARD_CONFIG_ARMPBA8 \|\| BOARD_CONFIG_QSD8250_LEO)` (+`pexpert.h` under ARMPBA8 only, `:7-9`; selector `:11-21` returns 0 in its `#else`)|D (umbrella)|
|2|`osfmk/arm/status.c:62-73`|`LAB_USER_STACK` = `ios7lab_user_stack()` → `0x2BE00000` vs `USRSTACK`|KB (gated)|
|3|`osfmk/kern/syscall_sw.c:102-108`|5 `extern` decls of the `ios7lab_*_trap` shims|D|
|4|`osfmk/kern/syscall_sw.c:126-130`|trap **15** = `ios7lab_mach_vm_map_trap`(8) vs `kern_invalid`(0)|UA|
|5|`osfmk/kern/syscall_sw.c:139-143`|trap **24** = `ios7lab_mach_port_construct_trap`(5) vs `kern_invalid`|UA|
|6|`osfmk/kern/syscall_sw.c:144-148`|trap **25** = `ios7lab_mach_port_destruct_trap`(5) vs `kern_invalid`|UA|
|7|`osfmk/kern/syscall_sw.c:164-170`|traps **41/42** = `…port_guard`(5)/`…port_unguard`(4) vs `kern_invalid`|UA|
|8|`osfmk/kern/ipc_kobject.c:90-96`|adds `libsa/string.h`, `pexpert.h`, 3 `ios7lab_*_wire.h`|D|
|9|`osfmk/kern/ipc_kobject.c:340-345`|locals `lab_iokit64/lab_context64/lab_named_map64/lab_remap64`|D|
|10|`osfmk/kern/ipc_kobject.c:375-408`|request 64-bit-isation for MIG ids 4813/4811/3228-9/2865, `reply_size += 4`|KB (wire)|
|11|`osfmk/kern/ipc_kobject.c:462-503`|reply-64 rewrite of the same 4 ids (RetCode kept; failure → `MIG_BAD_ARGUMENTS`)|KB (wire)|
|12|`osfmk/ipc/mach_kernelrpc.c:39-272`|includes + the 5 trap bodies (`:59,:123,:158,:163,:168,:203`), `ios7lab_vm_allocate`(`:241`), `struct …vm_allocate_wire`(`:50-54`)|KB (trap targets)|
|13|`osfmk/vm/vm_user.c:126-134`|`ios7lab_remap_allowed_flags()` = `VM_FLAGS_USER_REMAP \| VM_FLAGS_RETURN_DATA_ADDR`|KB (gated)|
|14|`osfmk/vm/vm_user.c:1066-1070`|remap flag filter uses the helper vs `VM_FLAGS_USER_REMAP`|KB (gated)|
|15|`osfmk/vm/vm_user.c:1144-1148`|same filter in the 64-bit entry|KB (gated)|
|16|`osfmk/vm/vm_map.c:11440-11444`|locals `data_offset`/`return_data_address`|D (locals)*|
|17|`osfmk/vm/vm_map.c:11460-11480`|`RETURN_DATA_ADDR` page-offset span check + allocation|KB (gated)*|
|18|`osfmk/vm/vm_map.c:11539-11546`|`*address += data_offset` on success|KB (gated)*|
|19|`osfmk/vm/vm_shared_region.c:626-638`|cache base `0x2C000000`, size `0x14000000`|KB (gated)*|
|20|`bsd/vm/vm_unix.c:101-107`|`ios7lab_cache_abi()` helper|D*|
|21|`bsd/vm/vm_unix.c:1130-1135`|`shared_region_check_np` copyout widens to `uint64_t`|KB (gated)*|
|22|`bsd/vm/vm_unix.c:1174-1204`|`shared_region_copyin_mappings` 32-byte `{u64,u64,u64,u32,u32}` wire|KB (gated)*|
|23|`bsd/kern/kern_descrip.c:1545-1557`|2 MiB CS-blob allowance for `dyld_shared_cache_armv7`|KB (gated)*|
|24|`bsd/kern/kern_fork.c:1098-1100`|`lab_reserve_early_pids = vmabi`|KB (gated)*|
|25|`bsd/kern/kern_fork.c:1146-1154`|`nextpid` 2..9 → 10 (getpid ≥ 10 for CF/XPC init)|UA (gated)*|
|26|`bsd/kern/kern_exec.c:1447-1467`|spawn-port walk: `port_type == 3` (`PSPA_IMP_WATCHPORTS`) actions dropped when vmabi (inner prints ARMPBA8-only)|KB/UA (gated)|
|27|`bsd/dev/arm/unix_syscalls.c:95-97`|`#include <sys/fcntl.h>` (guarded kqueue)|D|
|28|`bsd/dev/arm/unix_syscalls.c:212-274`|`ios7lab_psynch_stack_tail()` + `ios7lab_guarded_kqueue()` bodies|KB (helpers)|
|29|`bsd/dev/arm/unix_syscalls.c:349-351`|`lab_guarded_queue = (code == 443 && vmabi)`|KB|
|30|`bsd/dev/arm/unix_syscalls.c:372-379`|psynch tail: `copyin` 4 words from user `SP+28` for 303/304/305|KB/UA|
|31|`bsd/dev/arm/unix_syscalls.c:434-442`|dispatch: psynch error, else `ios7lab_guarded_kqueue(443)`, else normal `syent` call|UA|

\* sites 16–25 are in files this memo excludes by task order (another agent covers them); classification only.

## 3. By value: the trap/syscall surface a first userspace process exercises
- **Trap numbering.** The kernel table is `mach_trap_table` (`syscall_sw.c:110`), `MACH_TRAP_TABLE_COUNT` = 128 (`syscall_sw.h:80`). ARM userspace passes mach traps **negated** in r12: `rsbs r5, r11, #0; ble swi_unix` (`traps_lo.s:174`) → index = `-r12`; index ≥ 0x80 rejected (`:181-182`). So "15/24/25" are table indices for user-visible **-15/-24/-25** (and -41/-42). This tree's `<mach/mach_traps.h>` carries **no numeric defines** (`grep -c mach_vm_map_trap` = 0; it is declarations + the "definitions as seen from user-space" comment, `mach_traps.h:62`); the only in-tree ABI spelling is the lab's own comment "trap -10" (`mach_kernelrpc.c:45`) and `traps_lo.s`'s negation.
- **Today on Mi4 (umbrella off).** Entries 15/24/25/41/42 are `MACH_TRAP(kern_invalid, 0, NULL, NULL)`. `kern_invalid` (`syscall_sw.c:405-410`) would return `KERN_INVALID_ARGUMENT`, but ARM never calls it: the dispatcher loads it and compares — `LOAD_ADDR(r2, kern_invalid); teq r1,r2; beq swi_mach_error` (`traps_lo.s:189-193`) → `mov r0,#7` = `EXC_SYSCALL` (`exception_types.h:91`), r1 = the trap index, via `_doexception` (`:232-240`); panic 0x81 only if that returns. **Userspace calling these traps today gets a fatal EXC_SYSCALL, never a return value.**
- **With the umbrella on, vmabi off:** the entries become `ios7lab_*`; each returns early — `MACH_SEND_INVALID_DEST` `0x10000003` (`message.h:692`) at `mach_kernelrpc.c:69-70, 131-132, 176-177, 212-213`. That is a **user-visible change** (EXC_SYSCALL → error return) with no marker. Arg counts 0→8/5/5/5/4; the arm `MACH_TRAP` macro ignores `munge32/munge64` (`syscall_sw.h:96-101`) and handlers take `saved_registers` directly (trap 15 reads `regs[8]` — "eighth argument in r8", `mach_kernelrpc.c:200-201`).
- **Trap 10 is NOT covered by the umbrella**: `_kernelrpc_mach_vm_allocate_trap` (`mach_kernelrpc.c:399`) calls `ios7lab_vm_allocate` only under `#if BOARD_CONFIG_ARMPBA8` (`:408-411`); on Mi4 the umbrella compiles it unreachable.
- **BSD unix surface** (`bsd/dev/arm`): `unix_syscall` (`unix_syscalls.c:283`) reads code from r12 (`:327`); code ≥ `NUM_SYSENT` 440 (`bsd/sys/sysent.h:60`) → `sysent[63]` = nosys (`:335`; `syscalls.master:113`) — so **syscall 443 is ENOSYS today**, and site 29/31 turns it into a real `kqueue`+`FD_CLOEXEC` with guard enforcement bypassed (`:236-273`). Sites 30/31 fix the iOS 7 **psynch** wire: 4 extra 32-bit words at user `SP+28` for 303/304/305 = `psynch_cvbroad/cvsignal/cvwait` (`syscalls.master:462-464`, under `#if PSYNCH` at `:455`), and a copy-in error is returned without calling `sysent`.
- **Kernel MIG surface** (`osfmk/kern`): `ipc_kobject_server` (`ipc_kobject.c:331`) rewrites the ARM32 wire of four iOS 7 messages — 4813 remap (`ios7lab_remap_wire.h:1`), 4811 mach_vm_map (`ios7lab_named_map_wire.h:1`), 3228/3229 port context get/set, 2865 `io_connect_method` (`ios7lab_iokit_wire.h:1`) — request `:375-408`, reply `:462-503`.
- **`bsd/kern`**: `kern_exec.c:1447-1467` drops `PSPA_IMP_WATCHPORTS` (type 3) spawn-port actions when vmabi; the site's prints are ARMPBA8-only, so on Mi4 it would be behavior with no marker.

## 4. Blast radius of the umbrella
- Appending `|| (defined(STAGE90_XNU_IOS7_COMPAT) && STAGE90_XNU_IOS7_COMPAT)` to `ios7_compat.h:5` flips **all 30 `#if` sites at once** (12 carriers; the `defined()` idiom is the project's, cf. `scripts/build_992.sh:50-55`).
- **Assembly:** no `.s` file sees the macro (31-site/13-file census contains none; explicit `.s` scan = 0 hits). One C definition suffices — no second asm spelling needed *for this macro*. Caveat: the sibling disjunction **is** spelled in asm directly (`traps_lo.s:116,137,162,202,264,555,659`, 7 board-gate lines), so any later rung extending the umbrella to those lines must solve the asm spelling (the "one value, two definitions" class).
- **Include chain:** each of the 12 carriers includes `<arm/ios7_compat.h>` exactly once (`grep -c` = 1 each); nothing else includes it (only `osfmk/arm/Makefile:11` installs it). `pexpert.h` is included only for ARMPBA8 (`:7-9`) — a CANCRO runtime arm needs that widened.
- **Runtime half is the second definition.** `ios7_compat_vmabi_enabled()` (`:11-21`) returns 0 for CANCRO. So the umbrella alone: (a) changes the trap table (not runtime-gated there) as in §3, and (b) compiles but never runs everything else — every other site is gated on the selector (`status.c:66`, `vm_map.c:11461`, `vm_shared_region.c:629`, `kern_descrip.c:1551`, `kern_fork.c:1099`, `kern_exec.c:1449`, `vm_user.c:130`, `unix_syscalls.c:350/375`, `ipc_kobject.c:377/383/389/395`).
- **Second gate to know:** `mach_kernelrpc.c:408` `#if BOARD_CONFIG_ARMPBA8` — the trap-10 wire call is board-gated separately from the umbrella.
- Excluded from detail by task order (another agent): `vm_shared_region.c:626`, `vm_map.c:11440/11460/11539`, `vm_unix.c:101/1130/1174`, `kern_descrip.c:1545`, `kern_fork.c:1098/1146`, `status.c:62`, `traps_lo.s`.

## 5. Proposed minimal first "userspace-compat" rung (993)
Smallest evidence-bearing slice that cannot half-enable:
1. **Open the trap surface only.** Append `|| (defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS)` to the 5 `syscall_sw.c` gates (`:102,126,139,144,164`) and to `mach_kernelrpc.c:39` (the handler bodies). Why first: 992 moved the wall to userspace; the cheapest next probe is whether launchd/dyld survives a mach trap that is a SIGSYS today. The other 24 sites (shared region, remap data-address, pid skip) are VM/BSD redesigns needing their own pre-registered readings.
2. **Open the runtime half in the same rung, or refuse.** Add a CANCRO arm to `ios7_compat_vmabi_enabled()` mirroring ARMPBA8's (`PE_parse_boot_argn("ios7lab_vmabi")`, already spelled at `unix_syscalls.c:142/397/416`) and widen the `pexpert.h` include. Without it the rung only swaps SIGSYS for `0x10000003` — the half-on state the checker must refuse.
3. **Order after that:** (a) `unix_syscalls.c` 443 + 303-305 (libdispatch/libpthread, per-process); (b) `ipc_kobject.c` + wire headers (IOKit/VM MIG); (c) `vm_shared_region.c` + `vm_unix.c` (dyld cache base `0x2c000000`); (d) `vm_map.c`/`vm_user.c` RETURN_DATA_ADDR; (e) `kern_fork` pid skip + `kern_exec` spawn ports; (f) `status.c`; (g) `kern_descrip.c`.

**Checker sketch `tools/check_ios7_compat_traps.py`** (style of `tools/check_commpage_alias.py`: objdump by value, `FAIL`/`ok` lines, exit 0/1/2, rehearsal against the previous image):
- Anchor A (image): locate `mach_trap_table` in the symbol table; entries are 12 bytes `{int arg_count; void *fn; const char *name}` (`syscall_sw.h:72-78`). Require 128 entries (re-derived from `mach_trap_count`, `syscall_sw.c:402`), entries 15/24/25/41/42 point at symbols named `ios7lab_*` with arg counts 8/5/5/5/4, and each `name` string equals the symbol (the arm macro always stamps `#name`).
- Anchor B (image): `kern_invalid` still exists and is still referenced by ≥1 other entry (re-derive; never store 84).
- Anchor C (source, census): re-derive `grep -c '#if IOS7_COMPAT_PLATFORM'` = 30 over 12 carriers; a gained/lost site fails.
- Anchor D (source, half-on refusal): if the trap switch is on, `ios7_compat.h`'s `#else` must not be CANCRO's only arm — i.e. a boot-arg-gated CANCRO arm exists.
- Refusals (examples): `FAIL mach_trap_table[15] is kern_invalid - the switch did not reach the pool (the 992 image is kern_invalid by construction)`; `FAIL ios7_compat_vmabi_enabled() has no CANCRO arm - the table would return 0x10000003 where userspace expects a mapping (half-on refused)`.
- Rehearsal: against the 992 image it must fail Anchor A only.

## Risks / uncertainties
- Enabling the umbrella does **not** open the sibling board gates (same disjunction spelled directly: `pmap.c` ×4, `traps_lo.s` ×7 lines, `docs/experiment-ios7-abi-staging.md` §4 counts 16–17; union 46) — 992 opened six via a separate switch.
- `ios7lab_vm_allocate` is compiled-but-unreachable on Mi4 (`mach_kernelrpc.c:411`, ARMPBA8-only): do not read the umbrella as covering trap 10.
- The -15/-24/-25 user spellings are read from `traps_lo.s:174`'s negation + the lab's "trap -10" comment; no userspace header in this tree carries the numbers. If exact SDK numbers matter, take them from the iOS 7 SDK, not here.
- MIG ids 4811/4813/2865/3228/3229 are named only by the local `ios7lab_*_wire.h` headers (tree-wide `.defs` hits = 0): routine identity is an in-tree claim, not a checked one.
- Prediction, not evidence: that the next wall after 992 is a mach trap. 992's own candidate list (`scripts/build_992.sh:66-74`) names libdispatch heap and zeroed commpage VERSION/timebase first.