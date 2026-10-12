# D13 syscall/trap ABI study (the 991 follow-up): what iOS 7.1.2 userland issues vs what our kernel serves

Read-only census. No build, no device, no repo write except this file.
Method: `python3 -I` (repr/unpack), `grep -n`/`-c`, `sha256sum`, `arm-none-eabi-objdump -D -b binary -m arm`,
Mach-O parsed by value. Every number below has its command; where a rendering looked off it was re-read
with a second tool. Artifacts were extracted only into `/tmp/abi993-extract/` (dir separate from the scripts).

## 0. Pins

Kernel tree `external/xnu-hd2-darwin13/xnu` (sha256 first 12):

| file | sha256[:12] |
|---|---|
| osfmk/kern/syscall_sw.c | 90b7125c42fb |
| osfmk/kern/syscall_sw.h | d74950431b4c |
| osfmk/ipc/mach_kernelrpc.c | 1126ac35b590 |
| osfmk/arm/traps_lo.s | b16e7a66d538 |
| osfmk/arm/ios7_compat.h | 420d9d9e9a50 |
| osfmk/arm/rtclock.c | b699d1b92567 |
| osfmk/arm/model_dep.c | 5e98b1437618 |
| osfmk/arm/commpage/commpage.c | 67aa79528b8e |
| osfmk/arm/cpu_capabilities.h | 9fd54db9d5ef |
| bsd/dev/arm/unix_syscalls.c | 2376ffbcb03e |
| bsd/sys/sysent.h | a62b9625087b |
| bsd/kern/syscalls.master | 26722202e2f8 |
| bsd/kern/mach_loader.c | 0d8cee69d0fd |
| tools/build_xnu_arm_kernel.sh | dc6e31edf074 |

Reference (Darwin 13 lineage, not built here): `external/xnu-4570.1.46` — `osfmk/arm/locore.s` 8fad21f74423,
`osfmk/arm/cpu_capabilities.h` 938cc4116221.

Evidence inputs:
- `/tmp/cancro-last_kmsg-991.txt` sha256 `06606177f35bbcd77b2bae75e323e636a0897c81e4c8787a87750115e7b207e2` (the 991 run)
- `/mnt/data/ios7-payload/v2/ios7/rootfs.hfs` sha256 `c9b9080de2a1de00707c6c282650b61608405a7dbc8fe2c0bced475705fb9fea` (939,524,096 B)

## 1. What the D13 kernel installs

### 1.1 Mach traps — `svc #0x80` with r12 = −N

`MACH_TRAP_TABLE_COUNT 128` (`osfmk/kern/syscall_sw.h:80`); the table is `syscall_sw.c:110-263`.
D13 is built with `-DBOARD_CONFIG_MSM8974_CANCRO=1` (`tools/build_xnu_arm_kernel.sh:202`), and

```c
#define IOS7_COMPAT_PLATFORM  (BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO)   /* ios7_compat.h:5 */
```

so **IOS7_COMPAT_PLATFORM = 0 for D13**. Five slots are therefore `kern_invalid` on our board
(verbatim, `syscall_sw.c`):

```
:126-130   #if IOS7_COMPAT_PLATFORM
           /* 15 */ MACH_TRAP(ios7lab_mach_vm_map_trap, 8, NULL, NULL);
           #else   /* 15 */ MACH_TRAP(kern_invalid, 0, NULL, NULL);   #endif
:139-143   /* 24 */ ios7lab_mach_port_construct_trap (5)   vs  kern_invalid
:144-148   /* 25 */ ios7lab_mach_port_destruct_trap  (5)   vs  kern_invalid
:164-170   /* 41 */ ios7lab_mach_port_guard_trap    (5)
           /* 42 */ ios7lab_mach_port_unguard_trap  (4)   vs  two kern_invalid
```

(`:102-108` declares the five externs under the same gate.) Everything else the artifact issues is
present: 10 vm_allocate, 12 vm_deallocate, 14 vm_protect, 16-23 port ops, 26-29, 31 mach_msg, 32,
33-39 semaphores, 44/45/46, 48/49, 51-53, 58-62, 89-94, and **100 = `iokit_user_client_trap`, 8-register
wire** (`syscall_sw.c:235` — the artifact's IOKit calls −100 once and this matches; only the *name* table
at `:370-371` still says "kern_invalid").

Even when compiled in, each body has a second, runtime half — `ios7_compat_vmabi_enabled()` returns 0 on
CANCRO (`ios7_compat.h:11-21`) and the bodies return early: `mach_kernelrpc.c:69-70`, `:131-132`,
`:176-177`, `:212-213`. **Both halves must open** (same rule the sibling dyld-shared-cache study states for
the four BSD gates).

What a `kern_invalid` slot *does*: `traps_lo.s:177` `swi_mach` (index limit `cmp r5,#0x80` at `:181`) →
`swi_mach_error:232-238`

```asm
mov r0, #7            /* EXC_SYSCALL, osfmk/mach/exception_types.h:91 */
mov r1, r4
mov r2, #1
blx _doexception
```

A userland hit on −15/−24/−25/−41/−42 today **raises a fatal exception** (via `doexception`), not an errno.

### 1.2 BSD syscalls

`NUM_SYSENT 440` (`bsd/sys/sysent.h:60`); `syscalls.master`'s last entry is 439 kas_info (`:672`) and
`grep -nE '^(4[4-9][0-9]|5[0-9][0-9])[ \t]' syscalls.master` = no hits. Dispatch:
`unix_syscalls.c:327` `code = state->r[12];` → `:335` `callp = (code >= NUM_SYSENT) ? &sysent[63] : &sysent[code];`
— 63 is `nosys` (`syscalls.master:113`), so **≥440 ⇒ ENOSYS**, not an exception.

## 2. The −3 SWI (the `mach_absolute_time` fallback)

The number and the instruction pair, by value: user code does `mvn ip,#2` (= −3) then `svc #0x80`.

- Real Darwin: `4570 osfmk/arm/locore.s:515-516` `cmp r12,#-3; beq fleh_swi_trap_tb`, and
  `:800-802 fleh_swi_trap_tb: … bl ml_get_timebase` — a platform machine-layer helper.
- This tree, on the boards it was written for: `traps_lo.s:162-168` (`#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO`,
  `cmn r11,#3`, `beq ios7lab_timebase`) and `:264-268`

  ```asm
  ios7lab_timebase:
      blx _mach_absolute_time
      b   swi_exit64            /* :195: writes r0:r1 back into the saved state */
  ```

- **On D13/CANCRO the −3 SWI is compiled as a pure no-op**: `traps_lo.s:116-119`

  ```asm
  #if !(BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO)
      cmn r12, #3
      beq swi_trap_tb
  #endif
  ```

  and `:260-262 swi_trap_tb: movs pc, lr` — a fast return that never touches r0:r1.

So: the trap IS routed on our ARM (all SWIs land in `fleh_swi`), but its handler for CANCRO returns
nothing. Is it wired? No — it is stubbed, and a caller gets whatever r0:r1 already held.

Value note: D13's `mach_absolute_time()` = raw CNTVCT (`pe_msm8974.c:423`) scaled by
`NSEC_PER_SEC/tscFreq` (`rtclock.c:188-193` → `:82-93`) = **ns**, matching what the −3 route is supposed to
return. The commpage route needs `+0x88` (offset) / `+0x90` (gate) — the D13 tree defines neither
(`cpu_capabilities.h` has CPUFAMILY `0x40000080` etc. but nothing at +0x88; base is `0x40000000`), and
`commpage_set_timestamp` (`commpage.c:122-131`, the only writer) writes 0. 4570's equivalents are
`cpu_capabilities.h:153` (`base 0xFFFF4000`) and `:191-195`. That absence is exactly the 991 abort.

## 3. What the iOS 7.1.2 userland actually issues (measured, by value)

### 3.1 `/usr/lib/dyld` (the only MH_DYLINKER in the image; file offset `0x452d000`)

Header: cputype 12, cpusubtype 9, filetype 7, ncmds 14, flags 0x85; `__TEXT` vmaddr 0x2be00000 vmsize 0x21000
fileoff 0; `__DATA` 0x2be21000 (contains `__all_image_info` at 0x2be21c8c size 0xa4); `__LINKEDIT` 0x2be4a000;
`LC_UNIXTHREAD` pc = **0x2be01028** (`__dyld_start`); LC_UUID `651a31c39f71311f965f8ac44de02c88`;
LC_SYMTAB symoff 0x23e10, nsyms 972.

**101 `svc #0x80` sites** (script scan, `/tmp/abi993-tools/dyldsvc.py`), r12 by value:

- mach traps: −3, −10, −12, −14, −15, −16..−23, −24, −25, −26..−29, −31, −32, −33..−39, −41, −42,
  −44..−46, −48, −49, −51..−53, −59..−62, −89..−94, and `0x80000000` (swi_trap).
- BSD positive: 0 (via `___syscall`), 1, 3, 4, 5, 6, 20, 37, 54, 58, 73, 75, 92, 97, 98, 116, 133, 153,
  197, 199, 202, 220, 301-305, 312, 323, 327-334, 336, 338-340, 344-346, 360, 361, 366-368, 372, 423, **446**.

90 intra-dyld `bl`/`blx` sites target these stubs (Thumb BLX included after fixing the classifier).
dyld's own `_mach_absolute_time` is at 0x2be1b2c0 (file 0x1b2c0); its −15 stub is at **0x2be1b464**:

```asm
mov ip, sp                       ; push {r4,r5,r6,r8}; ldm ip,{r4,r5,r6,r8}
mvn ip, #14                      ; -15
svc #0x00000080
pop {r4,r5,r6,r8}; bx lr
```

The wire is r0..r6 + r8 (r7 skipped) — register-for-register the shape the tree's own
`ios7lab_mach_vm_map_trap` reads (`mach_kernelrpc.c:203-237`: `regs[0]` task, `regs[1]` address ptr,
`regs[2..5]` size/mask as lo/hi pairs, `regs[6]` flags, `regs[8]` cur_protection). So the *wire* is
compatible; only the gate stands in the way.

### 3.2 the shared cache's `libsystem_kernel.dylib`

Cache header at file `0x455d000` (magic `dyld_v1   armv7`), 3 mappings (0x2c000000/0xc266000/off 0,
0x38268000/0x1ce0000/0xc266000, 0x39f48000/0x22ed000/0xdf46000), 696 images. Whole-cache svc census
(`negcensus.py`, `stubnames2.py`): **415 `svc #0x80` sites — 352 positive (0..455, all distinct, all in
libsystem_kernel.dylib), 52 negative, 11 unresolved** by my r12 classifier (a limitation, not a claim).

Negative set (image ×count): one site each in `libsystem_kernel.dylib` for **−3, −10, −12, −14, −15,
−16..−29, −31..−39, −41, −42, −44..−46, −48, −49, −51..−53, −59..−62, −89..−94**; IOKit.framework −100 ×1;
`0x80000000` ×2 in libsystem_platform.dylib and ×1 in libsystem_pthread.dylib.

Positive stubs ≥400 (named by nearest-preceding LC_SYMTAB symbol): 400 wait4_nocancel, 404 accept_nocancel,
408 fsync_nocancel, 428 audit_session_self, 430/431 fileport_makeport/makefd, 438 shared_region_map_and_slide,
439 kas_info, **440 memorystatus_control, 441 __guarded_open_np, 442 guarded_close_np, 443 guarded_kqueue_np,
444 change_fdguard_np, 446 proc_rlimit_control, 447 connectx, 448 disconnectx, 449 peeloff, 450 socket_delegate,
451 __telemetry, 452 proc_uuid_policy, 453 memorystatus_get_level, 454 system_override, 455 vfs_purge**.
440..455 are all present except 445; 437 is absent (our master also has it as nosys). libsystem_kernel's
`_mach_absolute_time` (0x381c5784) has the identical byte shape as dyld's: `movw ip,#0x4088` /
`movt ip,#0xffff` / `ldrb r0,[ip,#8]` / beq → `mvn ip,#2; svc #0x80`.

`0x80000000` (swi_trap) is served by D13 (`traps_lo.s:249-258`, r3 ≤ 3 → return / icache_inv / dcache_inv /
thread_set_cthread) — a match, note (upstream 4570 has the same table and the same `cmp r3,#3`).

### 3.3 The delta table

| # | delta | D13 today | artifact | consequence |
|---|---|---|---|---|
| D0 | commpage `+0x88/+0x90` never written | no definitions, no writer; base 0x40000000 | reads 0xffff4088/0xffff4090 | **the 991 abort** (being fixed by 992) |
| D1 | mach traps **−15, −24, −25, −41, −42** | `kern_invalid` → `doexception(EXC_SYSCALL)` | real stubs in dyld **and** libsystem_kernel | fatal exception on first use |
| D2 | `svc #0x80` with r12 = **−3** | `swi_trap_tb: movs pc, lr` (r0:r1 untouched) | real clock expected | garbage time when the +0x90 gate reads 0 (which it does) |
| D3 | BSD **440..455** (446 in dyld; 440-455 in libsystem_kernel) | `NUM_SYSENT 440` → `nosys`/ENOSYS | named stubs (table above) | ENOSYS |

## 4. The 991 abort, by value — and what the log can and cannot say

The log's dyld is **slid by ASLR 0x4f000** (`mach_loader.c:581-587` slides `MH_DYLINKER`), base
**0x2be4f000**, proven three independent ways:

1. entry pc `0x2be50028` − `LC_UNIXTHREAD` 0x2be01028 = 0x4f000;
2. `dyld_all_image_info_addr 0x2be70c8c` − `__all_image_info` 0x2be21c8c = 0x4f000 (size 0xa4 matches);
3. fault pc `0x2be6a2c8` = base + 0x1b2c8, i.e. dyld's `_mach_absolute_time+8` — the `ldrb r0,[ip,#8]`
   with r12(log) = 0xffff4088 = ip.

So the 231 aborts are all the same instruction, reading `_COMM_PAGE+0x90` at **0xffff4090** (unmapped;
`Translation fault`). dyld never reached an `svc`: the fault precedes every trap in dyld's startup.
Conclusion for the fix order: 992 (map the alias) removes D0; the very next thing userland does with that
page is read a gate byte that no code writes (0) → it takes the −3 path → **D2 becomes the next observable**,
then D1 as dyld maps images and builds its port set, and D3 if dyld's `proc_rlimit_control` stub is on the
startup path (it exists; whether its call site executes in pid 1 is not established).

The 991 log **cannot name any syscall or trap**: `grep -c "syscall"` = 0 on the log, and
`mach_syscall_trace` (`model_dep.c:714-734`) has its whole body inside `#if BOARD_CONFIG_ARMPBA8` (`:716`),
i.e. empty on D13. `grep -c IOS7LAB` = 0 too. The census above is therefore vocabulary evidence, not
execution evidence (see §7).

The register block in the log is consistent with all of this (r12 0xffff4088, sp 0x2fdb0870, cpsr 0x40000010;
`dyld_all_image_info_addr/size` echoed by our own loader).

**lr = 0x2be58a9f is attributed** (verified by value, `/tmp/abi993-tools/verify_lr.py`): lr − slide 0x4f000 =
0x2be09a9f, low bit set ⇒ a Thumb call site; the return point is 0x2be09a9e and the calling instruction is
the Thumb BLX at **0x2be09a9a** (halfwords `f011 ec12`, imm +0x11824; target by the BLX rule
`Align(PC,4)+imm` = **0x2be1b2c0 = `_mach_absolute_time`**). The site is
`__ZN11ImageLoader4linkERKNS_11LinkContextEbbbRKNS_10RPathChainE + 0x32` (function opens 0x2be09a68; the
next symbol is `__ZN11ImageLoader22recursiveLoadLibraries…` exactly at 0x2be09cf4), and after the call the
64-bit result is stored — `str r0,[sp,#72]` (`9012` at 0x2be09a9e) and `str r1,[sp,#68]` (`9111` at
0x2be09aa2) — before `bl …recursiveLoadLibraries` at 0x2be09aaa (target re-derived = 0x2be09cf4, equal).

So the faulted `_mach_absolute_time` call is dyld's own, made by `ImageLoader::link` before it recursively
loads libraries. dyld's text holds **21** direct call sites into `_mach_absolute_time` (all Thumb BLX;
0x2be09a9a is one) — the 90 sites in §3.1 are calls into the *svc trap stubs*, a different census. Caution
recorded: the earlier draft's `0x2be58a9f − 0x3f000 = 0x2be19a9f` lands self-consistently on
`__platform_memchr+0x2b` — an arithmetic artifact (the draft then said "attributed to nothing"); the true
slide is 0x4f000, checked three ways above.

## 5. Verdict

**A delta exists today** (D1–D3; D0 is the live wall 992 is opening). None of D1–D3 is what stops the 991
run — the abort precedes every trap — but each becomes load-bearing within launchd's startup the moment
D0 is gone, and D2 is first in line. Minimal staged rung, mirroring 992's shape:

## 6. Minimal staged rung: `STAGE90_XNU_IOS7_TRAPS`

One switch, two independently checkable parts; it must not touch the LEO/ARMPBA8 path and must not be
aliased to `ios7_compat_vmabi_enabled()` (that would also open the four BSD gates the sibling study
lists: `vm_shared_region.c:626`, `vm_map.c:11440/11539`, `vm_unix.c:1130/1174`, `kern_descrip.c:1545`).

**Part A — wire −3 on CANCRO** (`osfmk/arm/traps_lo.s`, 3 line edits; the switch clause is appended to
each gate, whole-line replacements only):

- `:116` `#if !(BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO)` → add
  `|| (defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS)` **inside the negation**
  (so on a STAGE90 build the fast path is compiled out and the saved-state path is taken);
- `:162` and `:264` (`#if BOARD_CONFIG_ARMPBA8 || BOARD_CONFIG_QSD8250_LEO`, 2 identical lines) → same
  clause appended, which compiles `ios7lab_timebase: blx _mach_absolute_time; b swi_exit64` for CANCRO.

Effect: r12 = −3 returns the real 64-bit ns clock (D13 fine on the nanoseconds question because
`mach_absolute_time()` is already ns-scaled — §2).

**Part B — the five mach slots** (4 files):

- `osfmk/kern/syscall_sw.c`: extend the gate at `:102` (externs) **and** at `:126`, `:139`, `:144`, `:164`
  (5 lines) to `#if IOS7_COMPAT_PLATFORM || (defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS)`.
- `osfmk/ipc/mach_kernelrpc.c:39`: extend the file-wide `#if IOS7_COMPAT_PLATFORM` (it wraps all five
  bodies through `:272`) — without this the table's new references do not link, which is a *loud* failure
  if the two halves are staged inconsistently (good).
- `osfmk/arm/ios7_compat.h`: add the narrow selector and use it at the four bodies
  (`mach_kernelrpc.c:69-70`, `:131-132`, `:176-177`, `:212-213`):

  ```c
  static __inline int
  ios7_traps_enabled(void)
  {
  #if defined(STAGE90_XNU_IOS7_TRAPS) && STAGE90_XNU_IOS7_TRAPS
      return (1);
  #else
      return ios7_compat_vmabi_enabled();
  #endif
  }
  ```

- Not included: the BSD 440..455 gap (D3) — it needs master entries + sysent regeneration, and it is not
  on the 991 path; and the LEO-only `ios7leo_*` vm variants (`mach_kernelrpc.c:276-351`), which CANCRO
  must not adopt.

**Staging tools** (mirror `stage_d13_commpage_alias.sh` / `patch_d13_commpage_alias.py` /
`check_commpage_alias.py`, with exact-match-count refusals and no partial application):

- `tools/patch_d13_ios7_traps.py` — line-number-keyed edits, asserting: traps_lo.s sees each OLD gate
  exactly the expected number of times (1, 1, 1 — the two ARMPBA8/LEO lines share one OLD string, count 2
  across the file), syscall_sw.c 5 sites, mach_kernelrpc.c 1 file gate + 4 selector calls, ios7_compat.h 1
  insertion; idempotent (a second run is a no-op).
- `tools/stage_d13_ios7_traps.sh` — sets the switch, refuses to run with
  `STAGE90_XNU_IOS7_TRAPS=1` on a non-CANCRO board (on ARMPBA8/LEO it would override the boot-arg gate).
- `tools/check_ios7_traps.py` — by-value checker on the linked image (read with `arm-none-eabi-objdump`
  / ELF symbol+relocation reads, not source): (1) `_mach_trap_table` entries 15/24/25/41/42 point at
  `_ios7lab_*` (not `_kern_invalid`); (2) `cmn r12,#3` (0xE37C0003) count is 0 in the STAGE90 image and 1
  in a rehearsal against the current/991-armed image (expected FAIL lines for the rehearsal, PASS for the
  staged image); (3) `cmn r11,#3` (0xE37B0003) present with a `bl` to `_mach_absolute_time` on its branch
  target path; (4) exit non-zero on any mismatch. Rehearse against the previous image before any press.

Build: same as 992 plus `XNU_KERNEL_EXTRA_DEFINES='-DSTAGE90_XNU_IOS7_TRAPS=1'` and the matching env var,
and the preflight entry-config gate must learn `STAGE90_XNU_IOS7_TRAPS` the way it learned
`STAGE90_XNU_SP_ALIGN` (`46ed7de`).

Expected evidence after a run: the 231× `0xffff4090` aborts gone (with 992), no `EXC_SYSCALL` at a
`swi_mach_error` site, and the existing `IOS7LAB vm_map64` printf (`mach_kernelrpc.c` marker) if dyld
reaches −15.

## 7. Non-claims

- Stub **vocabulary ≠ executed call set**. The census proves which traps/stubs *exist and are wired to
  `svc`* in the artifact; the 991 log cannot prove which were reached (§4).
- The dyld BSD list includes 446 `proc_rlimit_control`; that a stub exists does not establish that its
  call site runs during launchd's startup.
- The 11 unresolved cache svc sites are excluded, not classified.
- Part A alone makes the −3 route *correct*; it does not make the commpage timebase *literate* — writing
  `+0x88/+0x90` properly (a commpage-populate change, not this switch) is the other, independent route and
  remains owed by D0/992's follow-on.
- Nothing here claims the four BSD IOS7_COMPAT gates or the 915-B region port are on this path.