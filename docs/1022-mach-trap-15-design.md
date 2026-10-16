# 1022 — INSTALL `mach_trap_table[15]` (`ios7lab_mach_vm_map_trap`) FOR D13/CANCRO

**READ-ONLY design. No build, no press, no device action, no `git add`, no write to `out/stage90`. The
only file written is this one.**

Input read BY VALUE for this document (every number below is re-measured here, not quoted):

| source | what it gave |
|---|---|
| `records/1021d-mmap-failure.txt` (187 lines, read in full) | the port defect: the shim's guard PASSES, `svc` carries `ip=0xFFFFFFF1` = index 15, `table[15] == table[0] == kern_invalid`; §B.3's undecided (i)/(ii); §E.1-E.4's four owed measurements |
| `external/xnu-hd2-darwin13/xnu/osfmk/kern/syscall_sw.c:110-131` | the row's two arms (`#if IOS7_COMPAT_PLATFORM` `:126-130`) |
| `external/xnu-hd2-darwin13/xnu/osfmk/arm/ios7_compat.h:5`, `:11-21`, `:33-40` | the macro, the three selectors, which `#if`-site each one gates |
| `external/xnu-hd2-darwin13/xnu/osfmk/ipc/mach_kernelrpc.c:39`, `:59`, `:208-243` | the body, its file gate, its block gate, its frame reads, its three validations |
| `out/xnu_asm_obj_d13/translated/osfmk/arm/traps_lo.s` (`out/xnu_asm_obj_d13` is the **staged/translated** tree, not the source tree) | `swi_mach`'s dispatch arithmetic and the two `swi_mach_error` sites |
| `out/stage90/xnu_arm_entry.elf` + `out/stage90/xnu_arm_entry-config.txt` + `-arm-extra.txt` | the OFF shape of the table, and the arm currently **on disk** |
| `/tmp/dyld-extract/dyld_shared_cache_armv7` (`0x381c6618`, `0x381c5958`, `0x3823a94c`, `0x3823e6e4`) | the shim's tail, the fast-trap stub, the caller, the veneer |
| `out/stage90/captures/recovered-unknown-arm-20261016-014316-last_kmsg.txt` (35031 lines) | the 1020b capture, re-measured for §0/§6 |
| `scripts/build_1021d.sh`, `src/entry/build_entry.sh`, `tools/build_xnu_arm_kernel.sh`, `scripts/preflight_boot_check.sh`, `src/entry/entry_trace.c:5578`, `src/entry/entry_stubs.c:7323` | the carriers and the two candidate witness hooks |

---

## 0. THE RESULT IN FOUR LINES

**0.1 The defect is confirmed and the fix shape is settled.** `mach_trap_table[15]` is `kern_invalid`
in the LINKED image — re-read here, not quoted: `mach_trap_table = 0x805564e4`, `entry0.fn ==
entry15.fn == 0x8047f5a4 == kern_invalid` (§3.3, the full read). The row exists in exactly one place
and is gated shut by `IOS7_COMPAT_PLATFORM` (`syscall_sw.c:126-130`), and the body the row needs
exists too (`mach_kernelrpc.c:208-243`), gated shut by the same macro at `:59`. **The narrow switch is
the right shape (§2): 26 `#if IOS7_COMPAT_PLATFORM` sites in 12 pool `.c` files, 8 of which change
behaviour on a 1 GiB board, and one of them (`vm_param.h:63`) shrinks the user address space.**

**0.2 The fix is the trap row *and* the runtime selector, not the row alone.** This is the single
sharpest thing in the design and it is re-stated from `docs/1021b-trap15-design.md` §2.2 because it is
what makes the difference between a fix and a regression: the adapter's first statement is
`if (!ios7_compat_vmabi_enabled()) return MACH_SEND_INVALID_DEST;` (`mach_kernelrpc.c:217`), and on
CANCRO `ios7_compat.h:11-21` compiles that inline to a **constant 0**. A row installed without the
selector returns a **non-sentinel** value, and the shim's sentinel test (`0x381c6688 cmp r4,r0; bne
0x381c66ba`, sentinel `0x10000003`) treats any non-sentinel as "the fast trap ran" and **branches away
from `bl 0x381c7f74`, the MIG demux that works today**. So: **row + selector together, or neither.**

**0.3 ⚑ THIS RUNG MUST NOT BE PRESSED UNTIL ITS DEPENDENCY IS DECIDED — see §5.** The arm currently
on disk is **1021d** (`STAGE90_XNU_GENERAL_REGS=1`, `xnu_arm_entry.bin` sha `ee2dd4fb`), which exists
to retire `records/1021-fpu-rootcause.txt`'s **kernel-mode** NEON undef. **`ios7lab_mach_vm_map_trap`'s
body is a kernel function reached only after the `svc` has entered `fleh_swi`, and `fleh_swi` for a
user `svc` does not touch the FP register file.** Therefore, **if `-msoft-float` retires the
kernel-mode undef, it cannot also fix pid 1's `udf #254` at `__os_alloc_slow+0x108`, and this rung is
still required**; and **if the run still stops at the same `udf`, this rung is still the next one.**
Either way the two rungs compose. But the cell that decides *which frontier is live* is `1021d`'s, and
a 1022 press that races it would burn a press to learn the same thing twice.

**0.4 I found a by-value defect in 1021d's own carrier, and it is the pattern this project has paid
for before.** `scripts/build_1021d.sh:468` writes `out/stage90/xnu_arm_entry-arm-extra.txt` with
**exactly one line** (`STAGE90_XNU_GLOBAL_FIX=1 role=1021d-general-regs`). `scripts/build_entry.sh`'s
record echo enumerates *nothing* of `arm-extra.txt`: that file's only writer is the build script and
its only reader is `park_*/press_*`'s `grep -qx`. So **every switch that is not in the record and not
in `arm-extra.txt` is a switch the press will not read.** For 1021d that wrapper is empty *by the
record's own evidence* (the record on disk carries `GENERAL_REGS=1` at `build_entry.sh:40366`'s echo),
so 1021d is not damaged — but **1022's carriers must not assume the wrapper is where switches live**
(§8, item 5).

---

## 1. WHAT IS BEING ASKED, AND WHAT IS ALREADY SETTLED

`records/1021d-mmap-failure.txt` §0/§A/§C put three facts beyond doubt by value, and this document
re-derives the third on the current image rather than trusting the record:

1. `_mach_vm_map`'s shim (`0x381c6618`, Thumb) is entered, its guard **loads** ran (the dump's
   `r9=1, lr=7, ip=3` are exactly the three entry loads; re-confirmed by reading the shim's own
   prologue: `0x381c662a ldr.w r9,[r7,#44]`, `0x381c662e ldr.w lr,[r7,#40]`, `0x381c6632 ldr.w
   ip,[r7,#36]`, then `0x381c6638 ldr r0,[r7,#20]`).
2. The fast-trap stub `0x381c5960` is `mov ip,sp; push {r4,r5,r6,r8}; ldm ip,{r4,r5,r6,r8}; mvn ip,#14;
   svc #0x80` — **re-read here, byte for byte** (`381c5960 e1a0c00d / e92d0170 / e89c0170 / e3e0c00e /
   ef000080`), so `ip = ~14 = 0xFFFFFFF1 = -15` = table index 15.
3. `mach_trap_table[15] == kern_invalid == mach_trap_table[0]` in
   `out/stage90/xnu_arm_entry.elf`, and the symbol `ios7lab_mach_vm_map_trap` is absent from it.

**What is NOT settled, and what this document is honest about.** §B.3 of the record left two residues
undecided: **(i)** the guard's *branch* failed live (the loads ran, the `cmp`/`bne` chose SLOW) because
the failing call is the retry site `0x3823aa1e` and not site `0x3823a9aa`; or **(ii)** the shim whose
loads ran is a *successful* map and the `udf`'s `r0=0x103` came from a different call. **I re-read the
caller and I can strengthen the case for (i) materially — see §6.1 — but I cannot close it.** Closing
it is a live measurement, and §6 specifies exactly one.

---

## 2. THE FIX SHAPE — (A) vs (B), DECIDED BY THE COUNT AND THE BLAST RADIUS

### 2.1 The two shapes

| shape | what changes | where |
|---|---|---|
| **(A) widen the macro** | `#define IOS7_COMPAT_PLATFORM (BOARD_CONFIG_ARMPBA8 \|\| BOARD_CONFIG_QSD8250_LEO \|\| BOARD_CONFIG_MSM8974_CANCRO)` | `osfmk/arm/ios7_compat.h:5` — **one line** |
| **(B) a narrow switch that reaches only the table row** | one `#else`-arm `#if` around `syscall_sw.c:126-130`'s row + one widened `#if` at `mach_kernelrpc.c:59` so the row's body compiles | two files, two anchors |

### 2.2 The census, measured now (`grep -rn` over `external/xnu-hd2-darwin13/xnu/`)

**32 textual occurrences, in 13 files. The breakdown matters more than the total:**

| form | count | does opening it change behaviour? |
|---|---|---|
| the definition `#define IOS7_COMPAT_PLATFORM (…)` | 1 | it *is* the switch |
| **bare `#if IOS7_COMPAT_PLATFORM`** | **24** | **YES — every one of these is a compiled-in `#else` arm** |
| `#if IOS7_COMPAT_PLATFORM \|\| (defined(STAGE90_XNU_…))` (compound) | 6 | these *already* open on CANCRO under their sibling switches (`SHREG_WIRE`×3, `SHREG_GEOM`×2, `IOS7_TRAPS`×1) and their `#else` arms do **not** carry the `ios7lab_*` bodies — (A) changes nothing here except the value of `IOS7_COMPAT_PLATFORM` inside them |
| `#if IOS7_COMPAT_PLATFORM /* comment */` | 1 (`mach_kernelrpc.c:59`) | `#if` with a trailing comment is still a bare `#if` — it is one of the 24 |

**24 bare sites, and what opening them turns on (each read this session):**

* `osfmk/mach/arm/vm_param.h:63` — **`VM_MAX_ADDRESS` falls from `0x80000000` to `0x40000000`.** This is
  not a lab adapter; it is the user address space. It is also the only site in the census that is a
  **header**, so it changes every TU that includes it. *(Note for the record: this is a third, unnamed
  definition of the same platform property that `ios7_traps_enabled()` and `ios7_shreg_geom_enabled()`
  already re-express — the `[[mi4-one-value-two-definitions]]` shape.)*
* `osfmk/vm/vm_map.c:11472/11492/11571` — enables the entire `VM_FLAGS_RETURN_DATA_ADDR` /
  `return_data_address` mechanism in `vm_map_enter_mem_object`, including a new
  `vm_map_trunc_page(memory_address)` interval computation. This is on **every** map the kernel makes.
* `osfmk/vm/vm_user.c:126/1066/1144` — `ios7lab_remap_allowed_flags()` becomes the remap flag filter
  wherever `ios7_compat_vmabi_enabled()` answers 1.
* `osfmk/kern/ipc_kobject.c:90` — pulls `libsa/string.h`, `pexpert` and **three wire headers**
  (`ios7lab_iokit_wire.h`, `ios7lab_named_map_wire.h`, `ios7lab_remap_wire.h`) into an osfmk T1 file.
* `osfmk/kern/syscall_sw.c` — the file-wide gate at `:102`, which brings the five `extern`s at
  `:103-107`, **and the four `row` gates** `:126` (15), `:139` (24), `:144` (25), `:164` (41/42).
* `bsd/kern/kern_fork.c:1098/1146`, `bsd/kern/kern_exec.c:1447`, `bsd/kern/kern_descrip.c:1545`,
  `bsd/dev/arm/unix_syscalls.c` (5), `bsd/vm/vm_unix.c` (3), `osfmk/arm/status.c:62`.

**And here is the clause that settles it, by value:** `ios7_compat_vmabi_enabled()` (`ios7_compat.h:11-21`)
returns **1 for anything that is not `ARMPBA8` or `QSD8250_LEO`** — its `#else` is `return 0;`. So (A)
does not merely *open* those `#if`s: it flips the **runtime selector** from a compile-time constant 0 to
a compile-time constant 1 for all 26 of the sites that call it (`kern_fork.c:1099`,
`kern_descrip.c:1551`, `kern_exec.c:1449`, `unix_syscalls.c:350/375`, `ipc_kobject.c:377/383/389/395`,
`vm_map.c:11494`, `vm_user.c:130`, `status.c:66`, `vm_shared_region.c:629`, and `mach_kernelrpc.c`'s
five adapters). **That is one boot-changing edit to open one table word**, and it is the "one macro,
two definitions" hazard in its purest form.

### 2.3 RECOMMENDATION: **(B)**, the narrow switch, and this is what it must change

**(B) is chosen.** It is 2 anchors in 2 files, its OFF is the tree's bytes by construction, and its
blast radius is one table word plus one function that is statically unreachable unless the row is
called. The exact edit is `docs/1021b-trap15-design.md` §2.1/§2.2, adopted here unchanged (this document
does not re-litigate it); the two anchors, restated so no reader has to open the other file:

```c
/* external/xnu-hd2-darwin13/xnu/osfmk/kern/syscall_sw.c, the #else arm at :128-130 */
#if IOS7_COMPAT_PLATFORM
/* 15 */	MACH_TRAP(ios7lab_mach_vm_map_trap, 8, NULL, NULL),
#else
#ifdef STAGE90_XNU_MACHVM_TRAP15          /* IOS7MVT15_BEGIN */
/* The extern list at :103-107 is INSIDE the :102 gate, so on this board the switched row must
 * bring its OWN declaration. Use the tree's own prototype text verbatim (`mach_kernelrpc.c:208`). */
extern int ios7lab_mach_vm_map_trap(void *saved_registers);
/* 15 */	MACH_TRAP(ios7lab_mach_vm_map_trap, 8, NULL, NULL),
#else
/* 15 */	MACH_TRAP(kern_invalid, 0, NULL, NULL),
#endif
#endif                                    /* IOS7MVT15_END */
```

```c
/* external/xnu-hd2-darwin13/xnu/osfmk/ipc/mach_kernelrpc.c:59 — ONE token; its #endif at :244 is
 * NOT edited. This is what brings the EXISTING body (:208-243) into the D13 compile. */
#if IOS7_COMPAT_PLATFORM || defined(STAGE90_XNU_MACHVM_TRAP15)
```

**And the third thing (B) needs, which is not an edit to either of those two files: the runtime
selector.** `ios7lab_mach_vm_map_trap`'s early-out is `ios7_compat_vmabi_enabled()`
(`mach_kernelrpc.c:217`), which is a **constant 0** on CANCRO. `docs/1021b-trap15-design.md` §2.2's
table offers S-A / S-B / S-C; **this document endorses S-A only** — a boot-arg arm added to
`ios7_compat.h:11-21`'s `#else`, reaching `PE_parse_boot_argn` exactly as the ARMPBA8 arm does at
`:16-17`, carried as a token in the arm's own declaration file — because it is the only variant that
does **not** re-open the 26-site census. ⚠ **S-A is NOT IMPLEMENTED and is UNMEASURED here**: it needs
a boot-arg name, a reader, and a press-side declaration. **Without it, `TRAP15=1` alone installs an
inert row, which per §0.2 is strictly worse than today.** §5 is the gate that keeps that build off the
device.

**What (B) does NOT need.** `traps_lo.s` is not edited: `swi_mach` loads the row generically
(`LOAD_ADDR(r1, mach_trap_table)` / `add r11,r5,r5,lsl#1` / `add r1,r1,r11,lsl#2` / `ldr r1,[r1,#4]` /
`bx r1`), so installing a row is a **data** change, and the only `swi_mach_error` site the row can
affect is the `teq r1, kern_invalid` / `beq` at `traps_lo.s:222` (the `bge` at `:211` is the `>= 0x80`
range test and cannot fire for index 15). **Measured, so it can be checked:** today
`swi_mach_error` is reachable from exactly those two sites and both are in the staged file.

**The `mach_syscall_name_table[15]` row is deliberately NOT edited** (it stays `"kern_invalid"`): 1021b
§2.1 proved its only reader (`mach_syscall_trace`, `osfmk/arm/model_dep.c:713-734`) is
`#if BOARD_CONFIG_ARMPBA8`-gated and therefore inert on CANCRO, and leaving it **gives the by-value
clause a second, independent word to read** (§3.4).

---

## 3. THE BY-VALUE CLAUSE (a build REFUSAL, with a negative control)

### 3.1 The rule

> **`STAGE90_XNU_MACHVM_TRAP15=1` ⇒ REFUSE the link unless
> `mach_trap_table[15].fn == the linked address of the defined symbol ios7lab_mach_vm_map_trap`
> **and** `mach_trap_table[15].fn != mach_trap_table[0].fn`
> **and** `mach_trap_table[0].fn == the linked address of kern_invalid`.
> `STAGE90_XNU_MACHVM_TRAP15=0` ⇒ REFUSE unless `mach_trap_table[15].fn == mach_trap_table[0].fn`
> (the pre-rung image, byte for byte at that word).**

Both directions are required (`[[mi4-off-option-two-spellings]]`: OFF is a shape, and the shape is the
tree's bytes). The clause is a **refusal**, not a print
(`[[mi4-a-claim-in-a-comment-is-not-a-check]]`), and it is placed in `src/entry/build_entry.sh` at the
existing 1018/1020/1021d clause cluster so that a bare entry relink cannot skip it.

### 3.2 The read — exact commands, run on the CURRENT image (this is the negative control)

The table's base is resolved from the image per build, never hard-coded (a hard-coded VA would report a
**relocation** as a **defect** on the second build of the same rung —
`[[mi4-measurement-defects]]`). Verified this session:

```
$ arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="mach_trap_table"'
805564e4 T mach_trap_table                     # 15*12+4 = +184 = 0xb8
$ arm-none-eabi-nm out/stage90/xnu_arm_entry.elf | awk '$3=="kern_invalid"'
8047f5a4 T kern_invalid
```

⚠ **`mach_trap_table` is a `T` symbol inside `.text`.** `readelf -S` on this image:
`.text` `Addr 0x80000000 / Off 0x010000 / Size 0x568000`; `.data` starts at `0x80568000`. So the table
lives in `.text` (`const` data, `AX` flags). **`objdump -s -j .rodata` returns nothing on this image —
there is no `.rodata` section — and a clause written that way fails vacuously.** Read it either way:

```
# (1) objdump, VA-addressed (works because objdump maps VA -> LMA itself)
$ arm-none-eabi-objdump -s -j .text --start-address=0x805564e4 --stop-address=0x805565a0 \
      out/stage90/xnu_arm_entry.elf
 80556554 00000000 a4f54780 b8605580 05000000  ......G..`U.....
 80556564 10414780 c5605580 00000000 a4f54780

# (2) by hand, from the section table: fileoff = 0x010000 + (va - 0x80000000); entry i at
#     base + 12*i; fn is the +4 word.  MEASURED NOW, the OFF image:
  entry  0: argc=0 fn=0x8047f5a4 nameptr=0x805560b8
  entry 10: argc=5 fn=0x80474110 nameptr=0x805560c5
  entry 12: argc=5 fn=0x80474238 nameptr=0x805560e6
  entry 14: argc=7 fn=0x804742ec nameptr=0x80556109
  entry 15: argc=0 fn=0x8047f5a4 nameptr=0x805560b8   <-- == entry 0, BY VALUE
  entry 16: argc=3 fn=0x804743b8 nameptr=0x80556129
  entry15.fn == entry0.fn : True
```

**The negative control reads exactly the way the clause must read it**: `idx15.fn == idx0.fn ==
kern_invalid` ⇒ "not installed", and the clause's `=1` direction must REFUSE this image. The `=0`
direction must PASS it. (A clause that only ever saw the ON image would be
`[[mi4-silence-is-a-reading-only-if-success-is-silent]]`.)

### 3.3 Why the clause cannot be fooled by a differently-indexed row

Four structural reasons, all re-derived here rather than inherited:

1. `mach_trap_t` is **3 words** on `__arm__` — `syscall_sw.h:72-77` (`{int mach_trap_arg_count;
   kern_return_t (*mach_trap_function)(void *); const char *mach_trap_name;}`) and the `__arm__`
   `MACH_TRAP` expansion (`syscall_sw.h:91-97`) emits exactly `{ (arg_count), (fn), #name }` in **both**
   `MACH_ASSERT` arms. **Measured against the OFF image: `entry15.fn - entry14.fn = 0xC`, `entry16.fn -
   entry15.fn = 0xC`** — the stride is 12 and the `+4` word is `fn`. It is 12 **by design, not by
   accident**: `swi_mach` computes `idx*12+4` (`add r11, r5, r5, lsl#1` = ×3, then `lsl#2` = ×12, then
   `[r1,#4]`).
2. The edit **replaces an initializer inside an existing row**; no `MACH_TRAP` line is added, removed or
   reordered, so `MACH_TRAP_TABLE_COUNT = 128` (`syscall_sw.h:80`) and `mach_trap_count`
   (`syscall_sw.c:402`) are untouched — and the row count is the one thing a mis-anchored patch could
   silently change.
3. `swi_mach` addresses entries **arithmetically**, never by a section symbol, so a change *inside* row
   15 cannot move row 16.
4. **Measured, this session:** entries 10/12/14/16 resolve to
   `_kernelrpc_mach_vm_allocate/deallocate/protect/port_allocate_trap` with `argc = 5/5/7/3` — three
   independent runs of this reading (1020 §4(a), 1021b §5, and the read above) agree. **The clause
   freezes that by asserting entry 16's `{argc, fn, nameptr}` unchanged and entry 0's `fn ==
   kern_invalid`.**

### 3.4 The second word, and the name table

Keep `mach_syscall_name_table[15] == "kern_invalid"` (do not edit it, §2.3) and have the `=1` direction
assert `entry15.nameptr == entry0.nameptr` **while** `entry15.fn != entry0.fn`. A row where both words
moved would be a **differently-indexed or differently-shaped table**, and asserting the name is how the
clause proves it is reading the fn word of index 15 and not a row that shifted under it
(`[[mi4-a-stand-in-can-be-the-right-size-and-the-wrong-value]]`).

### 3.5 The object-side assertion, and a stale-pool guard for **both** objects

`build_entry.sh` only **LINKS** pool objects, so the linked image can be green over a stale pool
(`records/1020-build-defect.txt` is the precedent). Read the objects too:

```
$ arm-none-eabi-nm out/xnu_kernel_obj_d13/osfmk_ipc_mach_kernelrpc.o | grep -E ' T ios7lab_mach_vm_map_trap$'
$ arm-none-eabi-nm out/xnu_kernel_obj_d13/osfmk_kern_syscall_sw.o | ...
```

* `mach_kernelrpc.o` must define `ios7lab_mach_vm_map_trap` as a `T`. **Today it does not** — the 994
  rung compiles that file (`_kernelrpc_mach_vm_allocate/deallocate/protect_trap` are present) and the
  lab bodies are gated out, so this symbol's appearance is this rung's own fingerprint.
* `syscall_sw.o` must carry a relocation to `ios7lab_mach_vm_map_trap` at the table's row-15 fn slot.
  ⚠ **Resolve that offset by arithmetic from the object's own table, never by typing `0xB8` into a
  second script** — the `0xB8` quoted in 1021b §3.5 is `15*12+4` for the **pre-rung** object, and a
  constant is exactly the `[[mi4-measurement-defects]]` shape. Read the object's relocation list,
  subtract the `.text`/`.rodata` section base, and assert the offset equals `15*12+4`.

---

## 4. THE ROW'S SEMANTICS — WHAT THE INSTALLED TRAP DOES, AND WHAT IT NEEDS

### 4.1 The body, by value (`mach_kernelrpc.c:208-243`)

```c
int ios7lab_mach_vm_map_trap(void *saved_registers)
{
    uint32_t *regs = (uint32_t *)saved_registers;        /* the SAVED REGISTER FRAME, not a C ABI */
    int rv = MACH_SEND_INVALID_DEST;                     /* 0x10000003 — the shim's OWN SENTINEL */
    uint64_t address64 = 0; mach_vm_offset_t address32 = 0; task_t task = TASK_NULL;
    if (!ios7_compat_vmabi_enabled()) return rv;         /* <-- S-A's target; constant 0 on CANCRO */
    task = port_name_to_task(regs[0]);
    if (task != current_task()) goto done;               /* only the CURRENT task's maps */
    if (copyin((user_addr_t)regs[1], &address64, 8)) goto done;
    if (regs[3] || regs[5] || (address64 >> 32)) { rv = KERN_INVALID_ARGUMENT; goto done; }
    address32 = (mach_vm_offset_t)address64;
    rv = mach_vm_map(task->map, &address32, regs[2], regs[4], regs[6],
                     IPC_PORT_NULL, 0, FALSE, regs[8], VM_PROT_ALL, VM_INHERIT_DEFAULT);
    if (rv == KERN_SUCCESS) { address64 = address32; rv = copyout(&address64, (user_addr_t)regs[1], 8); }
 done:
    if (logged++ < 12) printf("IOS7LAB vm_map64 size=%08x:%08x mask=%08x:%08x flags=%x prot=%x result=%d address=%08x\n",
                              regs[3], regs[2], regs[5], regs[4], regs[6], regs[8], rv, address32);
    if (task) task_deallocate(task);
    return rv;
}
```

**Answers to the brief's four questions:**

* **The clamp/translate is real and it is the whole point of the adapter.** The wire carries
  **64-bit** `address`, `size` and `mask` as low/high word pairs (that is iOS 7's ARMv7 `mach_vm_map`
  ABI, `docs/1021b-trap15-design.md` §1), but this tree's `mach_vm_offset_t`/`mach_vm_size_t` are
  **32-bit** (`osfmk/mach/arm/vm_types.h:107-109`, "We target iOS 4.3 for compatibility"). So the
  adapter (a) `copyin`s the userspace **address** (the address is an in/out pointer, not a value),
  (b) **rejects rather than truncates** any high word that is non-zero (`regs[3]` = size_hi, `regs[5]` =
  mask_hi, `address64 >> 32`) with `KERN_INVALID_ARGUMENT`, and (c) on success writes the chosen
  address back as a 64-bit value. **There is no silent truncation anywhere in this body** — that is a
  deliberate property and it is what makes a wrong wire a clean refusal instead of a corrupt map.
* **`argc = 8` in the row is documentation, not a mechanism.** On ARM the table's
  `mach_trap_arg_count` is never read by the dispatcher (`traps_lo.s` reads `[entry+4]` only; the only
  readers in the tree are `i386/bsd_i386.c:478/550` and `dtrace/systrace.c:743`, none of them in this
  image). The body reads `regs[0..6]` and `regs[8]`.
* **It does NOT need the shared-cache window and it does NOT need the managed map to be any particular
  size.** `mach_vm_map` (`osfmk/vm/vm_user.c:921`) is in the pool (measured:
  `out/xnu_kernel_obj_d13/osfmk_vm_vm_user.o` defines `mach_vm_map` at `0x8f8`), and its first act is
  `if (flags & ~VM_FLAGS_USER_MAP) return KERN_INVALID_ARGUMENT;` with `VM_FLAGS_ANYWHERE = 0x0001`
  (`vm_statistics.h:257`). The **window** (base `0x2C000000`, size `0x14000000`, i.e. the
  `IOS7LAB shared region 0x2c000000..0x40000000` line the 1020b capture prints) is **only** what makes
  the *user* VAs that the mapping will later be *used* at legal; a 1-page `VM_FLAGS_ANYWHERE` map is
  placed by the map's own allocator anywhere in `[VM_MIN_ADDRESS, VM_MAX_ADDRESS)` and succeeds or
  fails on **managed size**, not on the window.
  ⚠ **`VM_MAX_ADDRESS` on CANCRO is `0x80000000`** (`vm_param.h:63`'s `#else`), which is good here: the
  ✅ `IOS7SHREG map_slide … slide=0x13cb000 slide_start=0x3d577000` line in the 1020b capture shows the
  map already contains 321 MiB of cache **below `0x40000000`** with no overlap problem. **Not that
  shape's merit — it is the (A)-shape's blast radius (§2.2) — but the consequence is that (B) does not
  have to argue about address space.**
* **What it DOES need, and this is a real builder asymmetry this rung must not discover late:**
  (a) the `vm_map.c` / `vm_user.c` lab remap arms are **`#if IOS7_COMPAT_PLATFORM`-gated and therefore
  OFF on CANCRO** — the adapter's `regs[6]` (flags) goes through the tree's **native** flag filter
  (`VM_FLAGS_USER_MAP`), *not* through `ios7lab_remap_allowed_flags()`. **So the flag filter this row
  actually sees is the native one**, which is the intended behaviour for `VM_FLAGS_ANYWHERE` and is
  stated here because a reader who assumes (A) was taken will assume the wrong filter.
  (b) **the `svc` enters `fleh_swi`, which does not touch the FP register file** — see §0.3.

### 4.2 The live calibration: this row's printf is `vl_*`'s analogue

**This is the strongest evidence in this document, and it is by value.** The 1020b capture contains
**zero** `IOS7LAB vm_allocate64` / `vm_deallocate64` / `vm_protect64` lines (`grep` over the whole
106 KB log → 0 hits, against 11 `IOS7LAB cache`/`commpage`/`vecl2` boot lines), even though
`STAGE90_XNU_IOS7_TRAPS=1` is **set in the very arm this capture came from**
(`out/stage90/xnu_arm_entry-config.txt`: `STAGE90_XNU_IOS7_TRAPS=1`). Those three printfs live in
`ios7lab_vm_allocate`/`_deallocate`/`_protect` bodies that are **compiled and linked** (the `-3` SWI
witness proves the `:39` file gate is open) and are simply **never executed** on pid 1's path. So:

> **On this port there is exactly one IOS 7 lab trap whose body we have ever seen run, and it is the
> one trap whose row is `kern_invalid`.** The trap-15 adapter's `printf` will therefore be a *first*
> execution of a first-time instruction stream, and a capture with `IOS7LAB vm_map64` in it is
> unambiguous proof the row was installed, dispatched and executed.

Corollary, recorded because it is a **live** reading not a static one: the same capture shows **no
`IOS7LAB vm_map64` line, and no panic string** (`grep -i panic` over the whole log → only
`xnu_entry_panic_*` keys, which are this image's own record slots, not a panic). That is precisely the
tension §B.3 names, and §6 is where it gets measured.

---

## 5. ⚑ THE COUPLING RISK — READ THIS BEFORE ANY PRESS (and a defect found while reading)

### 5.1 The 1021d dependency

* **The arm on disk is 1021d**: `out/stage90/xnu_arm_entry-arm-extra.txt` =
  `STAGE90_XNU_GLOBAL_FIX=1 role=1021d-general-regs`; its record carries
  `STAGE90_XNU_UNDEF_RAISE=1`, `STAGE90_XNU_IOS7_TRAPS=1`, `STAGE90_XNU_GENERAL_REGS=1`;
  `xnu_arm_entry.bin` sha `ee2dd4fb…`. **The last capture on disk is 1020b's**
  (`recovered-unknown-arm-20261016-014316-last_kmsg.txt`, `014316` = 01:43, while the record's echo
  carries `GENERAL_REGS=1`). So 1021d has **not** been pressed, or its capture is not on disk.
* **The two rungs are independent and compose.** `-msoft-float` (1021d) removes **compiler-emitted**
  FP/NEON from the pool; the trap-15 row (1022) is a `data`-plane install. Nothing in the row's
  execution path (`svc` → `fleh_swi` → `swi_mach` → `bx` the row → `mach_vm_map`) requires a vector
  register. **But the reverse is also true: nothing in `-msoft-float` can install the row.** So if
  1021d's press still stops at `__os_alloc_slow+0x108`'s `udf`, the row is the next rung — and if it
  does not, the row is *still* the next rung, because pid 1's `alloc` returning failure is on the same
  path either way.
* **Therefore: do not press a 1022 arm until 1021d's capture exists**, or the run's verdict will be
  attributed to the wrong edit if both changed at once (`[[mi4-measurement-defects]]` — one edit per
  press is this project's rule for a reason).

### 5.2 A by-value defect in the carrier chain — struck, and stated so it is not re-litigated

I read the record echo enumeration looking for `arm-extra.txt`'s writer and found, instead, that
**`build_1021d.sh:468`'s one-line `arm-extra.txt` would leave `STAGE90_XNU_GENERAL_REGS` out of the
press-visible declaration**, while the comment at `build_1021d.sh:459-465` says the file is
"its ONE declaration".

**That is not a defect of 1021d, and the record on disk proves it:** `build_entry.sh:40366` echoes
`STAGE90_XNU_GENERAL_REGS=$GENERAL_REGS` into `out/stage90/xnu_arm_entry-config.txt`, and `:40251`'s
family echoes `STAGE90_XNU_IOS7_TRAPS` / `STAGE90_XNU_UNDEF_RAISE`, and
`scripts/preflight_boot_check.sh`'s `ENTRY_CFG_KEYS` (`:716-763`) carries `UNDEF_RAISE` (`:762`) and
`GENERAL_REGS` (`:763`) **and refuses a record that carries a key the list does not name**
(`:1222-1224`). So 1021d's switches **are** visible to the gate through the **record**, and
`arm-extra.txt` is exactly what its own comments say it is: the one channel for the switch that has no
`-D` and no record echo anywhere (`GLOBAL_FIX`, `records/1020-build-defect.txt`).

**The lesson that carries into 1022 is the phrasing, not the finding:** *"its ONE declaration"* is
false of two of the three switches it sits beside, and a future build that trusts that sentence to
decide where a switch is declared will put it in the wrong place. **For `STAGE90_XNU_MACHVM_TRAP15`
this document requires BOTH channels (§8), and the wrapper file must carry it as a second line only if
the press scripts are written to grep it — and if they are, the line must be written by the BUILD.**

---

## 6. THE ONE LIVE MEASUREMENT THIS RUNG MUST RECORD (closes §B.3's (i)/(ii))

### 6.1 What I can already contribute by value — the retry-site reading

`records/1021d-mmap-failure.txt` §B.3 leaves two residues. I re-read the caller and the **call
sequence** because "which call site reached the udf" is a static question too, and the answer is
**stronger than the record's**:

```
$ arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x3823a94c  (0x130 bytes)
3823a97c  movw r8, #1
3823a982  movt r8, #18688   ; 0x4900   ==>  r8 = 0x00004900  (written to [sp,#8] at :3823a9a2)
3823a9a6  str r5, [sp,#4]   ;  0
3823a9a8  str r5, [sp,#0]   ;  0
3823a9aa  blx 0x3823e6e4    ; ---- SITE (a) ----
3823a9ae  cmp r0, #0
3823a9b0  bne.n 0x3823aa54  ; ---- SITE (a)'s failure branch, DIRECTLY to the udf ----
   ... :3823a9bc..:3823aa26  (the atomic-registry / bounds steps over the 0x1000 bytes)
3823aa1e  blx 0x3823e6e4    ; ---- SITE (b) ----
3823aa22  cmp r0, #0
3823aa24  beq.n 0x3823a9bc
3823aa26  b.n   0x3823aa54  ; ---- SITE (b)'s failure branch, DIRECTLY to the udf ----
3823aa54  defe            ; udf #254   <-- the frontier, cache 0x3823aa54 = slide+0x39605a54
```

**Both failure branches jump straight to `0x3823aa54`, and site (b)'s `blx` is `f003 ee62` = the
SAME 4-byte encoding as site (a)'s** (`f003 ee9c` at `3823a9aa` vs `f003 ee62` at `3823aa1e` — same
opcode, a different PC-relative displacement; both targets decode to the same veneer
`0x3823e6e4`, which I decoded in ARM mode: `ldr ip,[pc,#4] / add ip,pc,ip / ldr pc,[ip]` = a
lazy-binder thunk, so both sites dispatch through the **same GOT slot**).

**Consequence for §B.3 (i)/(ii): the two sites differ in exactly ONE frame word.** Every store that
builds the call frame at site (b) (`:3823a9ee-:3823aa1c`) is **byte-identical** to site (a)'s
(`:3823a976-:3823a9a8`) — I checked each: `950b/`900a`/`9208`/`9109`/`9107`/`9506`/`9505`/`9504`/
`9503`/`f8cd 8008`/`9501`/`9500` all store the same registers with the same immediates. **The only
difference is `sp`**: site (b)'s frame sits **lower** (the stack is not restored between them), so the
**absolute** values that the shim's guard reads (`[r7,#20]`, `[r7,#40]`, `[r7,#44]` — caller words
4/9/10) are the *same numbers* at both sites. **`fp`/`r8 = 0x4900` is written last at BOTH sites
(`str.w r8,[sp,#8]` at `:3823a9a2` and `:3823aa16`) and never between them.**

So a hypothesis that (i) is *false because the two sites build different frames* is **weakened by
value** — and the residue narrows to: **did the guard's `cmp`/`bne` take the SLOW branch at a call
site whose three words are 0/7/1?** That is a question about the *branch*, not the frame, and only a
live measurement answers it. ⚠ **I could NOT confirm this**: the two frames are different addresses
(both below `0x2bdf0450`), and the capture has exactly one frame dump, at the udf, which names only
the udf's own `r7`. **UNMEASURED.**

**One more constraint that matters for the instrument, and it is measured:** if site (a) took the fast
leg and the row were installed, the adapter's `printf` would fire (the log showed zero such lines for
the *other* three traps). The udf at `0x3823aa54` is reached from **whichever** `blx` returned
non-zero — and if the row is not installed, a `-15` svc would have panicked **before** any return. The
1020b capture shows the udf and no panic. So on that run: **no `-15` svc reached `swi_mach`** — which
is consistent with (i) and with the row being absent, and **cannot be distinguished without the
instrument below.**

### 6.2 THE instrument: extend the **existing** hook, keyed on the **svc number**

**Add ONE key. Do not add a second shim probe.**

* **Where:** `src/entry/entry_trace.c`, inside `__wrap_sleh_undef` (`:5578-5612`), after the existing
  `g_undef_seq++/UNDEF_PUT("xnu_live_undef_seq", …)` and inside the existing
  `if (g_undef_seq <= UNDEF_LIVE_MAX)` block (`:5596-5600`), whose off-channel route is already
  `STAGE90_XNU_UNDEF_DIRECT`'s `entry_write_kv` (`:5568-5571`) — i.e. **cap-exempt and direct**, which
  is the 1018 lesson applied for free.
* **What it reads (three words, all already in the frame the wrapper holds):**
  * `frame[STAGE90_SS_SP / 4]` — the trapping thread's saved `sp`.
  * `*(uint32_t *)(uintptr_t)sp` — the word at the top of that frame.
* **The key:** `xnu_live_undef_frame0` = that word. **Why this closes (i)/(ii), by value:**
  at the shim's FAST path, the last thing the shim does before `blx 0x381c5960` is
  `0x381c665c str.w sl, [sp, #40]` — **`sl` is the shim's saved original `r1`**, the userspace address
  pointer. So on the fast path the word at the frame's top is the page-**address** argument; on the
  SLOW path (`0x381c6692-0x381c66b2`) the same slot is overwritten with `lr` (the guard's third field,
  the value **7**), and the address is stored one slot lower at `[sp,#40]`.
  ⇒ **`frame0 == 7` means the guard's branch chose SLOW; `frame0` = a userspace pointer means it chose
  FAST.** And because the `udf` is reached only from a failing `blx`, a FAST `frame0` here would prove
  residue **(ii)** (the shim that ran fast was a *successful* map, and the failing call is another) —
  while a SLOW `frame0` proves **(i)**. The word is published **on the udf's own frame**, so it names
  the call that died, not the call that succeeded.
* **The alternative instrument, if the first is ambiguous:** the same hook records
  `frame[STAGE90_SS_R0 / 4]` (the saved `r0` at the udf). The capture already prints `r0=0x00000103`
  in the panic dump; the wrapper's own read is the same word from the same frame, **which is a
  self-check of the instrument against the capture** — two independent producers of one number, and
  they must agree or the hook is reading a different frame
  (`[[mi4-a-status-is-a-verdict-only-if-its-producer-delivered-one]]`).
* **What this instrument does NOT do:** it does not measure the guard's three words themselves
  (`[r7,#20]/[r7,#40]/[r7,#44]`). Adding a shim-entry probe is the **second** instrument the brief
  offers and it is strictly worse: it needs the `--wrap`ed `mach_vm_map`'s `sl`/`fp`, i.e. a
  signature the wrapper does not have, and `--wrap` **misses intra-object calls** (the 984 lesson) —
  the shim is in the cache, so a `--wrap` cannot reach it at all. **The udf frame is the only place a
  wrapper already has the caller's shape in hand.** (1021d §E.3 asks for the guard words; this design
  answers "not from a wrapper — from the frame the udf leaves".)

---

## 7. PRE-REGISTERED VERDICT CELLS FOR THE PRESS

The ONE question:

> **With the row installed and the selector answering 1, does pid 1's 1-page `VM_FLAGS_ANYWHERE`
> `mach_vm_map` succeed — i.e. does the row get used, does the adapter's view of the frame match the
> frame libsystem sent, and does the allocator stop failing?**

**G-CTL comes first and is mandatory.** The instrument is `xnu_live_undef_*` (channel-gated by
`STAGE90_XNU_UNDEF_DIRECT`, `entry_trace.c:5568`) and the row's own `IOS7LAB vm_map64` printf (a
kernel `printf`, unbounded). **If the capture shows neither the udf witness (`xnu_live_undef_seq`) nor
any `IOS7LAB vm_map64` line, the run is INSTRUMENT_UNSOUND even if the row was installed — the 1018
lesson, and the reason the new key rides the direct channel and not the capped one.**

| # | condition (read from the capture) | verdict | what it means |
|---|---|---|---|
| **G-CTL** | neither `xnu_live_undef_seq` nor any `IOS7LAB vm_map64` line anywhere | **INSTRUMENT_UNSOUND — WITHHOLD** | the witness was not live or was capped out. Nothing below is a reading |
| **T15-FIRES** | `IOS7LAB vm_map64 … result=0 address=<nonzero>` fires **and** the boot proceeds past `__os_alloc_slow` (no `usermode undefined instruction … 0x39605a54`, or the same pc with a **different** `_seq` story) | **ROW INSTALLED, USED, AND SUCCESSFUL — THE WALL MOVED** | the row dispatched, the frame validated, `mach_vm_map` returned `KERN_SUCCESS`. The frontier is downstream: read *what* the next stop is, in the 1015b/1016a style |
| **T15-REFUSED** | `IOS7LAB vm_map64 … result=4` (`KERN_INVALID_ARGUMENT`) at least once, no panic | **ROW USED; THE WIRE IS REFUSED** | `regs[3]‖regs[5]‖address>>32` rejected the frame — the iOS-7 wire is malformed on this port. Read `size_hi:size_lo`/`mask_hi:mask_lo`/`flags`/`prot` in the SAME line: they are the wire's own numbers, and one of them is where §4.1's frame derivation is wrong |
| **T15-SENTINEL / INERT** | the run fails **earlier or differently** than 1020b's, `-15` never reaches the row, the shim's MIG fallback is skipped | **REGRESSION — ROW INSTALLED, SELECTOR STILL 0** | S-A did not land (§2.3). The adapter returned `MACH_SEND_INVALID_DEST = 0x10000003`; the shim's `cmp r4,r0` **matched its own sentinel** and branched to `0x381c66ba`, skipping `bl 0x381c7f74`. **This is the one cell that is a defect OF THIS RUNG**, and §5.1 is how it is avoided |
| **T15-PANICS** | a panic whose string is `swi_mach_error`-shaped, or the panic walk reached from `0x804a851c` (`xnu_entry_why`) with a mach-trap frame | **STALE POOL — NOT A BEHAVIOUR READING** | the `-D` did not reach one of the two pool objects, or a stale object was linked. Read `nm` on the objects (§3.5), not the env |
| **T15-UNEXERCISED** | the new `xnu_live_undef_frame0` reads the **SLOW** shape (the word `7`) | **THE GUARD FAILED — residue (i)** | the failing call did **not** issue `-15`. The row is inert for this path and the missing object is on the **MIG slow leg** (`bl 0x381c7f74`), i.e. `docs/1019-design.md`'s `--wrap=mach_vm_map` rung, **not** a further table rung. **This is the design's own falsifier, stated as a predicate** |
| **T15-OTHER-CALL** | `xnu_live_undef_frame0` reads a **FAST** shape (a userspace pointer) | **residue (ii)** — the shim that ran fast mapped successfully and the `udf`'s failing call is another. Read the two `_r0` producers (§6.2): they must agree with the capture's `r0=0x103` or the hook is on a different frame |

**Falsification, stated as a predicate.** This design is falsified if **T15-UNEXERCISED** holds with a
**green** §3 clause: the row is in the image, the wire is right, and pid 1's failing `mach_vm_map`
never touches it. The next rung is then 1019's kernel-side `mach_vm_map` witness on the MIG demux —
**not** a table variant, and **not** a wider gate. The design is **also** falsified-as-stated (not
as-mechanism) by **T15-REFUSED**, because §4.1's frame derivation would then be wrong and the repair
is a re-derivation from the capture's own printed words.

---

## 8. THE CARRIERS — EVERY SITE `STAGE90_XNU_MACHVM_TRAP15` MUST REACH

Following `scripts/build_1021d.sh`'s pattern (read this session) and `docs/905-design.md` §7. **A
switch whose value is not in the record is a switch the press gate cannot refuse** — so the list below
is closed on purpose, and each item names what *reads* it.

**The pool side (what actually compiles the row) — three, and the third is not optional.**

1. **`tools/build_xnu_arm_kernel.sh` — the `DEFINES` array, beside `:929`** (the `UNDEF_RAISE` line;
   `:902` is `IOS7_TRAPS`'s): add
   `[[ ${STAGE90_XNU_MACHVM_TRAP15:-0} -eq 1 ]] && DEFINES+=(-DSTAGE90_XNU_MACHVM_TRAP15=1)`.
   ⚠ **Both files ARE pool files** — measured: `out/xnu_kernel_obj_d13/osfmk_kern_syscall_sw.o` and
   `out/xnu_kernel_obj_d13/osfmk_ipc_mach_kernelrpc.o` both exist, and the latter already carries the
   994 rung's `_kernelrpc_*` bodies, so this array is the channel that reached it before.
2. **`XNU_KERNEL_EXTRA_DEFINES`** (the same script's `EXTRA_DEFINES`, `:1257-1260`): the build script
   must set `EXTRA="$EXTRA -DSTAGE90_XNU_MACHVM_TRAP15=1"` **and assert both carriers live at the site
   that establishes them** (the `build_1020b.sh:193` pattern):
   ```bash
   [[ -n ${STAGE90_XNU_MACHVM_TRAP15:-} ]] || { echo "FAIL: …not exported…"; exit 1; }
   [[ "$EXTRA" == *"-DSTAGE90_XNU_MACHVM_TRAP15=1"* ]] || { echo "FAIL: …not in EXTRA…"; exit 1; }
   ```
   **Neither alone is sufficient**, and a build that sets only one produces a row that is silently
   `kern_invalid` again — the exact defect `records/1020-build-defect.txt` records for `GLOBAL_FIX`.
3. **S-A's runtime selector** (§2.3): the boot-arg arm in `ios7_compat.h:11-21`'s `#else`, plus
   whatever carrier its boot arg needs. ⚠ **UNMEASURED / NOT IMPLEMENTED.** Without it the arm must
   not be pressed (§5.1).

**The entry side (what makes the switch legible to the gate) — six sites, mirroring `GENERAL_REGS`.**

4. **Resolve + 0/1 refusal** in `src/entry/build_entry.sh`, beside `:1673` (`GENERAL_REGS`'s refusal);
   the `UNDEF_RAISE` pattern is at `:1648-1651`. **The refusal is mandatory**
   (`[[mi4-off-option-two-spellings]]`: an unvalidated `2` is not an error, it is a third value).
5. **`ENTRY_ARM_KEYS`**, `src/entry/build_entry.sh:2076-2131` — append at the tail, before
   `STAGE90_XNU_TREE_D13)` at **`:2131`** (after `STAGE90_XNU_GENERAL_REGS` at `:2130`), so the
   arm-change refusal sees it.
6. **The record resolver's `case`**, `build_entry.sh:2329`'s region — add
   `STAGE90_XNU_MACHVM_TRAP15) _v=$MACHVM_TRAP15 ;;` reading the **validated** variable, never
   `${STAGE90_XNU_MACHVM_TRAP15}`.
7. **The record echo**, beside `:40366` (`GENERAL_REGS`) / `:40153` (`IOS7_TRAPS`) — add
   `echo "STAGE90_XNU_MACHVM_TRAP15=$MACHVM_TRAP15"`. **A key in `ENTRY_ARM_KEYS` with no echo is an
   arm nobody read.**
8. **`scripts/preflight_boot_check.sh`'s `ENTRY_CFG_KEYS`**, tail at **`:762-763`** — append the name
   (after `STAGE90_XNU_GENERAL_REGS`). Mandatory in **both** directions: the loop at `:891` refuses a
   record missing a listed key, and `:1222-1224` refuses a recorded key the list does not name. No
   count moves — 1005 made that a sorted set difference.
9. **The by-value clause** (§3), in `build_entry.sh`'s 1018/1020/1021d clause cluster, plus the
   object-side assertions at the build script's step 5 (the `1021d` `5a/5b` pattern).

**The declaration carrier, and where it must and must not go.**

10. **`out/stage90/xnu_arm_entry-arm-extra.txt`.** ⚑ **This file is written by the BUILD** (e.g.
    `build_1021d.sh:468`'s `printf`), its only readers are the `park_*`/`press_*` `grep -qx`, and
    **its content is a declaration of a different build the moment the arm moves**
    (`[[mi4-self-written-record-is-not-a-constraint]]`). Two consequences, both mandatory:
    * the 1022 build must **re-emit `STAGE90_XNU_GLOBAL_FIX=1`** and must tag its own line
      (`role=1022-machvm-trap15`), because 1015b's fix is still in the pool;
    * **the 1021d line must NOT be left in place un-replaced** — and, per §5.2, **`GENERAL_REGS`'s
      presence there is at most a courtesy: the RECORD is where the gate reads it** (items 5/7/8).
      A press script that greps this file for a switch must grep a line the **build** wrote for **that**
      arm, or it is checking a self-written record.

**The omission, stated so it is not looked for:** **there is no `.s` carrier.**
`tools/assemble_arm_layer.sh`'s `ASFLAGS` does not need this switch: `traps_lo.s`'s `swi_mach` reads
the table generically and is not edited (§2.3). The rung that *does* have an asm half is 994
(`STAGE90_XNU_IOS7_TRAPS`); a reviewer who carried that checklist forward would add a token to the
wrong file. **OFF is byte-identical for the whole .s layer by construction** — that is the checkable
form of this omission.

---

## 9. WHAT I COULD NOT CONFIRM (UNMEASURED)

1. **Which residue, (i) or (ii), obtains.** §6.1 narrows it — both call sites build the **same frame
   words**, so a frame-difference explanation of (i) is out — but *whether the guard's branch chose
   SLOW on the failing call* is a **live** fact and no capture has it. **UNMEASURED.**
2. **S-A.** The switch that makes `ios7_compat_vmabi_enabled()` answer 1 on CANCRO is designed
   (1021b §2.2's table) but **does not exist**. Its boot-arg name, reader and press declaration are
   owed. **UNMEASURED.**
3. **The trap-15 frame's exact word mapping.** I re-read the shim's fast path
   (`0x381c664c-0x381c666a`) and the stub, and I did **not** re-derive it end-to-end: the stub's
   `mov ip,sp; push {r4,r5,r6,r8}; ldm ip,{r4,r5,r6,r8}` re-loads its four registers from the shim's
   **stack**, so the mapping is not "user r0-r3 → stub r4-r8" as a first reading suggests. **1021b §1's
   table is adopted here unchanged and is NOT re-verified by this document.** The live calibration is
   the adapter's own `printf` (§4.1/§7's T15-REFUSED cell).
4. **Whether `-msoft-float` (1021d) retires the kernel-mode undef, and therefore what the frontier is
   when 1022 lands.** No 1021d capture exists on disk. **UNMEASURED.**
5. **Whether the 1020b capture's missing `-15` is evidence about the trap or about the instrument**
   (the usvc witness's full window is exhausted at `n = 0x40` — measured: the capture's last
   `xnu_live_usvc_n=0x00000040` sits at line 33930, ~4000 lines before the run's end — so a post-window
   `-15` would be invisible, and the ladder past 64 publishes `_n`/`_num` only, which the capture shows
   at lines 23748-24107). **This is exactly why §6.2's instrument rides the DIRECT channel.**
6. **The exact behaviour of the veneer at `0x3823e6e4`.** I decoded it (`ldr ip,[pc,#4]; add ip,pc,ip;
   ldr pc,[ip]` — a lazy-binder thunk into a dyld-bound GOT). **Which slot** it binds, and whether the
   runtime fixer has written it, is **UNMEASURED** (1021d §A.2 records that the *other* veneer's slot
   `0x39f3f27c` is 0 in the frozen cache and that the runtime fixer writes it).

---

## 10. WHAT THIS DOES AND DOES NOT CLAIM

* **CONFIRMS (by value, this session):** the row exists at `syscall_sw.c:126-130` and is `kern_invalid`
  in the linked image (`mach_trap_table = 0x805564e4`, `entry15.fn == entry0.fn == 0x8047f5a4 ==
  kern_invalid`); the stride is 12 and `mach_trap_table` is a `T` symbol in `.text` (there is **no**
  `.rodata`); entries 10/12/14/16 carry the same `argc 5/5/7/3` three independent reads report; the
  census is **32 occurrences / 13 files, of which 24 are bare `#if`s** and one of them
  (`vm_param.h:63`) halves `VM_MAX_ADDRESS`; opening the macro turns `ios7_compat_vmabi_enabled()` into
  a constant **1**, which reaches ~15 runtime sites including `vm_map.c`'s remap mechanism; the
  fast-trap stub `0x381c5960` is `mvn ip,#14` + `svc #0x80`; `swi_mach`'s `swi_mach_error` has exactly
  two branch sites and index 15 can reach neither with the row installed; the 1020b capture contains
  **zero** `IOS7LAB vm_*64` lines and zero panic strings; both `ios7lab_mach_vm_map_trap` call sites in
  `__os_alloc_slow` build **identical** frame words and both failure branches jump to `0x3823aa54`.
* **does NOT claim:** that the guard's branch took FAST live (§9.1); that the row's insertion is
  *sufficient* (`§0.2`: it is not, without S-A); that the trap-15 frame mapping is re-verified (§9.3);
  that this rung should be pressed before 1021d's verdict (§5.1); or that trap 15 is the frontier once
  1021d lands (§0.3).
* **SAFETY.** One table initializer + one `#if` in a pool `.c` file + one boot-arg arm. Blast radius:
  trap 15 only (index-stable, §3.3); all other rows byte-identical; the `.s` layer untouched. **No
  device action of any kind was taken while authoring this file, and none is authorized by it** —
  `[[mi4-hardware-run-safety-gate]]`: `fastboot boot` only, never flash, and **the press is the
  operator's**.

---

## 11. FILE:LINE ANCHORS (all checked against the tree on disk, this session)

**The row and its gate.** `osfmk/kern/syscall_sw.c:102` (the file-wide `#if`), `:103-107` (the five
`extern`s, inside the gate), **`:126-130` (the row)**, `:139-148` (rows 24/25), `:164-170` (rows
41/42), `:282` (`mach_syscall_name_table[15] = "kern_invalid"` — **not edited**), `:402`
(`mach_trap_count`); `osfmk/kern/syscall_sw.h:72-77` (`mach_trap_t`, 3 words), `:80`
(`MACH_TRAP_TABLE_COUNT 128`), `:91-97` (the `__arm__` `MACH_TRAP`).
`osfmk/arm/ios7_compat.h:5` (`IOS7_COMPAT_PLATFORM`), `:11-21` (`ios7_compat_vmabi_enabled`),
`:33-40` (`ios7_traps_enabled`), `:46-53` (`ios7_shreg_geom_enabled`).
`osfmk/arm/PlatformConfigs.h:42` (`MSM8974_CANCRO` → the Cortex-A9/Krait path).
`osfmk/mach/arm/vm_param.h:63-68` (**`VM_MAX_ADDRESS`, the (A)-shape's sharpest edge**).

**The body (reused, not rewritten).** `osfmk/ipc/mach_kernelrpc.c:39` (the file gate 994 opened),
**`:59`** (the block gate this rung opens), `:64/128/163/168/173` (the five port adapters this rung
links but cannot reach), **`:208-243` (`ios7lab_mach_vm_map_trap`)**, `:217` (the selector early-out —
S-A's target), `:244` (`#endif /* port/vm_map lab adapters */`, **not edited**).
`osfmk/vm/vm_user.c:921` (`mach_vm_map`, in the pool: `osfmk_vm_vm_user.o`),
`osfmk/mach/vm_statistics.h:257` (`VM_FLAGS_ANYWHERE 0x0001`), `:309-310` (`VM_FLAGS_USER_MAP`),
`osfmk/mach/message.h:692` (`MACH_SEND_INVALID_DEST 0x10000003` — **the shim's sentinel**).

**The dispatcher (unchanged; read for the index arithmetic).** staged
`out/xnu_asm_obj_d13/translated/osfmk/arm/traps_lo.s`: `mov r8, sp` (the saved frame), `mov r11, r12`,
`swi_mach:` (`LOAD_ADDR(r1, mach_trap_table)` / `add r11, r5, r5, lsl#1` / `add r1, r1, r11, lsl#2` /
`ldr r1, [r1, #4]` / `LOAD_ADDR(r2, kern_invalid)` / `teq r1, r2` / `beq swi_mach_error` / `bx r1`),
`swi_mach_error:` (→ `_doexception`, `irqvec_panic`), `swi_exit64:` (`str r1, [r8, #4]`),
`LOAD_ADDR_GEN_DEF(mach_trap_table)`.

**The carriers.** `tools/build_xnu_arm_kernel.sh:902` (`IOS7_TRAPS`), `:929` (`UNDEF_RAISE` — the
`DEFINES+=` sibling site), `:1257-1260` (`EXTRA_DEFINES`), `:202`
(`-DBOARD_CONFIG_MSM8974_CANCRO=1`), `:332-345` (`FP_FLAGS`). `src/entry/build_entry.sh:1673`
(`GENERAL_REGS` 0/1), `:1648-1651` (`UNDEF_RAISE`), `:2076-2131` (`ENTRY_ARM_KEYS`, tail `:2131`),
`:2329` (the resolver `case`), `:40153` (`IOS7_TRAPS` echo), `:40366` (`GENERAL_REGS` echo),
`:31400-31466` (1021d's clause cluster). `scripts/preflight_boot_check.sh:716-763` (`ENTRY_CFG_KEYS`,
tail `:762-763`), `:891` (missing key), `:1222-1224` (unshown key).
`scripts/build_1021d.sh:210-240` (the carry-the-whole-arm loop), `:459-470` (the arm-extra writer).
`tools/check_d13_ios7_vmwire.py:61-75` (`TRAPS`), **`:85` (`DEFERRED_TRAPS = (15, 24, 25, 41, 42)`)**,
`:390-403` (**rule 6, which this rung makes fail by design** — the rung must run it, treat exactly that
one failure as expected, and add a `--trap15 on|off` flag rather than deleting `15` from the tuple:
deleting it deletes the check that the other four rows are still closed).
`tools/check_d13_ios7_traps.py` (the `--expect on|off` + `--control` shape the §3 clause tool should copy,
including its decode-the-encoding-not-the-mnemonic discipline).

**The witness.** `src/entry/entry_trace.c:5500-5520` (the wrapper's doc), `:5568-5571`
(`STAGE90_XNU_UNDEF_DIRECT` → `entry_write_kv`, `UNDEF_LIVE_MAX 64u`), `:5578` (`__wrap_sleh_undef`),
`:5584` (`xnu_live_undef_seq`), `:5596-5600` (the four detail keys — **the new key goes here**);
`src/entry/entry_stubs.c:4254` (`ENTRY_LIVE_CAP 24576u`), `:7323-7357` (`entry_note_user_svc`, the
`USVC_FULL_N = 64` window at `:7313`).

**The evidence.** `records/1021d-mmap-failure.txt` (§0 the verdict, §A the guard, §B.2 the table read,
§B.3 the tension, §C the port defect, §D the rung, §E the four owed measurements, §F the unknowns);
`docs/1021b-trap15-design.md` §1 (the frame), §2 (the two-file edit — **adopted here**), §3.4 (the
census), §5 (the clause, adopted), §9 (the deferral this document revives);
`docs/1021c-general-regs-design.md` §4.4 (the cell style), §1.1/§2.1 (the flag);
`records/1020-trap-dispatch.txt`; `records/1019-rootcause.txt`; `records/1021-fpu-rootcause.txt`;
capture `out/stage90/captures/recovered-unknown-arm-20261016-014316-last_kmsg.txt`.

**The cache reads (all reproducible).** `/tmp/dyld-extract/dyld_shared_cache_armv7` — the two-formula
VA→fileoff map (`va - 0x2c000000` below `0x38268000`, else `0xc266000 + (va - 0x38268000)`);
`0x381c5958-0x381c5980` (the fast-trap stubs; `0x381c5960` = `mvn ip,#14; svc #0x80`);
`0x381c6618-0x381c66ec` (the shim: prologue `:6618-:6632`, guard `:6638-:664a`, fast call `:666a`,
sentinel test `:667e-:668a`, slow `stmia` `:6692-:66b2`, `bl 0x381c7f74` `:66b4`, GOT `:66ba-:66c6`,
`blx r6` `:66e0`); `0x3823a94c-0x3823aa58` (`__os_alloc_slow`, both call sites, the `udf #254` at
`0x3823aa54`); `0x3823e6e4` (the lazy-binder veneer, decoded in ARM mode).

**No build, no press, no device action, no `git add`, and no write to `out/stage90` was performed while
authoring this file.**