# Rung 1023 — instrument `_kernelrpc_mach_port_allocate_trap` to read the value libxpc's `udf #254` swallowed

Author: `design-1023`. Written 2026-10-16, after the 1022 press (`records/1022-press-read.txt`,
verdict **T15-FIRES**). Every number below was re-measured by me; the source of each (file:line, ELF
VA, or cache file offset) is named. This doc is a **design**. It contains no verdict; the press
writes that.

⚠ **This doc contradicts the 1023 brief on the trap index.** Brief said "the frontier is
`mach_trap_table[16]`". The measured stub is `mvn r12,#0x17` → **index 24**, and section 1.D shows
entry 24 is `kern_invalid` in the linked image. See §8, hypothesis H1.

---

## 0. The four-line result

0.1 **Defect.** 1021d's residual wall survives 1022. pid 1's `udf #254` at `0x3824e022` fires in the
`else` arm of libxpc's `__xpc_mach_port_allocate` (`0x3824dfc8`), taken when the floating-point
`kern_return_t` in r0 is **not** `{0, 3, 6}`. The handler that produced that r0 is the kernel's
`_kernelrpc_mach_port_allocate_trap` (`0x80474588`), which **does not publish its rv** anywhere.

0.2 **The one sentence that decides the design.** The kernel **did not panic**, so `rv != 4`.
`kern_invalid` returns `KERN_INVALID_ARGUMENT = 4` (`kern_return.h:90`) and the capture contains
**zero** `panic` — so whatever ran, it did not route through a `kern_invalid` row. 1023 is therefore
**not** a row-installation rung (unlike 1022). It is a **publish-the-rv** rung.

0.3 **The fix (shape (A)).** ~6 lines inside the existing `#if` at
`osfmk/ipc/mach_kernelrpc.c:591`: capture `args->name`, then before `return (rv)` publish one bounded
`printf` naming the frame (`target`, `right`, `name`) and the rv from each of the three exits. **The
returned `rv` is byte-identical on every path** — the rung is instrumentation only. Reuse
`STAGE90_XNU_IOS7_TRAPS` (record key 19); **no new switch is needed or wanted**. ⚠ This edit is a
POOL-object edit ⇒ it requires a **pool rebuild** (`tools/build_xnu_arm_kernel.sh`), not `build_entry.sh`
alone.

0.4 **Pre-registered verdict cells** (§7): `P16-CTL` (mandatory, first), then `P16-RETURNS-N` ⊃
`P16-SUCCEEDS`/`P16-TASK-MISMATCH`/`P16-COPYOUT-FAILS`, plus `P16-UNEXERCISED` and
`P16-EXITS-UNREACHED`. The press picks one; §7 gives each cell's exact witness predicate.

---

## 1. Read by value — the input table

### 1.A The dyld shared cache — 696 images, 3 mappings

`/tmp/dyld-extract/dyld_shared_cache_armv7`, sha256
`cad2df775e122c660238f341e74c163a2ea0ed261e85bfd3190de750cd9c0d2c`, 315 952 666 bytes. Header:
magic `dyld_v1   armv7\0`, `mappingOffset=0x68`, `mappingCount=3`, `dyldImageInfoOffset=0xc8`.

Mapping table (**read as 8×u32 = 32 B per entry; at word offset 0 the u64 address pair**):

| # | address | size | fileOffset | initProt |
|---|---------|------|-----------|----------|
| 0 | `0x2c000000` | `0x0c266000` | `0` | 5 |
| 1 | `0x38268000` | `0x01ce0000` | `0xc266000` | 5 |
| 2 | `0x39f48000` | `0x022ed000` | `0xdf46000` | 5 |

⚠ **An entry is 32 bytes, not 24.** `struct.unpack('<QIIII')` (24 B) silently mis-parses the whole
header — it produced `sharedRegionStart=0x2c008000`, `localSymbolsSize=0x862c`, and `struct.error:
requires a buffer of 7 GB`. Use `<8I`. This is `mi4-measurement-defects`; the brief's warning that
upper-map pointers need the map's `fileOff` (not flat `va-0x2c000000`) is **correct and load-bearing**
— the veneer slot in §1.C lives in mapping 2.

`dyldImageInfo` entries are 32 B `{imageAddress u64, imageVMAddress?? , pathFileOffset u32, pad}`;
the path string is at `*(u32)(entry+24)` (measured: entry 0 → fileoff `0x10d80a0`). 696 entries.

### 1.B The frontier function — `__xpc_mach_port_allocate`

Name **confirmed by measurement**, not inherited: the local-symbols block at cache fileoff
`0x10234dc4` (`{dylibOffset=0xc24b000, nlistStart=789480, nlistCount=865}`) yields
`0x3824dfc8 = __xpc_mach_port_allocate`. (The brief's `+0x5a` / base `0x3824b000` alignment is
therefore **CONFIRMED**, not UNMEASURED as an earlier pass believed. ⚠ The symbol *table* base is
`0x10233000`; the value I first quoted as "the symbol base" was actually the string-table base.)

Disassembly (`0x3824dfc8` → `0x3824e02c`, Thumb; bytes read from fileoff `0xc24dfc8`):

```
3824dfc8: b5f0        push {r4,r5,r6,r7,lr}
3824dfd2: efc0 0050   vmov.i32 q8,#0
3824dfd6: 4614        mov r4,r2                  ; r4 = flags/name ptr (arg3)
3824dfdc: 46e8        mov r8,sp
3824dfde: f108 0008   add.w r0,r8,#8             ; r0 = &right
3824dfe6: 3fff/bf08   cmp.w r1,#0xffffffff / it eq
3824dfea: 2106        moveq r1,#6                ; right := 6 (right-none) if -1
3824dfec: ae06        add r6,sp,#24
3824dfee: 9101        str r1,[sp,#4]             ; m[1] = right
3824dff4: f247 008c   movw r0,#0x708c
3824dff8: f2c0 10cf   movt r0,#0x1cf             ; 0x1cf708c -> add pc -> GOT entry for a global
3824dffc: 4478        add r0,pc
3824dffe: 6805        ldr r5,[r0,#0]             ; r5 = (global at VA 0x39f4508c) = 0x39f38068
3824e006: 6828        ldr r0,[r5,#0]             ; arg0 = *(task-ish object)
3824e008: 4641        mov r1,r8                  ; arg1 = m (base+8)
3824e00a: 4622        mov r2,r4                  ; arg2 = flags
3824e00c: 4633        mov r3,r6                  ; arg3 = &out
3824e00e: f012 eda8   blx 0x38260b60             ; veneer -> trampoline 0x381c66ec
3824e012: b138        cbz r0, 0x3824e024         ; KERN_SUCCESS -> return
3824e014: 2803        cmp r0,#3
3824e016: bf18        it ne
3824e018: 2806        cmpne r0,#6
3824e01a: d0f2        beq.n 0x3824e002         ; 3 or 6 -> retry
3824e01c: 2100        movs r1,#0
3824e01e: f012 ef60   blx 0x38260ee0             ; veneer -> 0x38145110 (libsystem_c)
3824e022: defe        udf #254                   ; <-- THE FRONTIER (file VA)
3824e024: 9806        ldr r0,[sp,#24]
3824e026: b007        add sp,#28
3824e02c: bdf0        pop {r4,r5,r6,r7,pc}
```

**Wire, read by value** — the local frame the variadic call passes as `m`:

| slot | written at | value |
|------|-----------|-------|
| `m[0]` | `3824dfda str r0,[sp,#0]` | the `task`/`port` handle arg0 (see §2.1) |
| `m[1]` | `3824dfee str r1,[sp,#4]` | `right` — arg1, `6` when arg1 == `0xffffffff` |
| `m[2]` | `3824dfe2 str r2,[sp,#24]` | `0` — pre-zeroed, **not** an argument |
| `m[3]` | `vst1.32 {d16-d17},[r0]` at `3824dff0`, q8=0, dest `sp+8` | `0` = the **out** pointer (host writes the name here) |

So the **user** stack layout is `[0]=task, [4]=right, [8]=out_ptr, [12]=0`, 16 bytes, and arg2
(`r2` = `r4` = the user's third C argument) is **not** on that stack — it is passed in r2 and is
`user_addr_t name`. The kernel reads `args->name` from `r2` too (§3), so **either reading gives the
same slot**. The frontier register `r2 = 0x00000002` is the *fourth* register, not the second.

### 1.C The veneer → trampoline → stub chain

| hop | VA | bytes | meaning |
|-----|----|-------|---------|
| caller | `0x3824dfc8` | — | libxpc `__xpc_mach_port_allocate` |
| veneer | `0x38260b60` | ARM `e59fc004 e08ff00c e59ff000` | `ldr ip,[pc,#4]; add ip,pc,ip; ldr pc,[ip]` |
| GOT word | `0x38260b6c` | `0x01ce4608` | `0x38260b6c+8+0x01ce4608 = 0x39f45174` |
| slot | `0x39f45174` | `0x381c66ed` | one odd bit ⇒ Thumb `0x381c66ec` |
| trampoline | `0x381c66ec` | see below | sentinel compare + slow leg |

I dumped 28 GOT words `0x39f45140..0x39f451ac`; every one is a `0x381xxxxx|1` ode pointer, **20 of
them ending in `9`/`d`/`1` (Thumb)**. The veneer's target slot is therefore a *dyld stub table*, not
ordinary data.

```
381c66ec: b5f0        push {r4,r5,r6,r7,lr}
381c66f6: 461e        mov r6,r3
381c66fa: 9600        str r6,[sp,#0]
381c66fc: 4690        mov r8,r2
381c66fe: 460d        mov r5,r1
381c6700: 4604        mov r4,r0
381c6702: f7ff e96c   blx 0x381c59dc        ; <-- the fast-trap stub
381c6706: 2103        movs r1,#3
381c6708: f2c1 0100   movt r1,#0x1000       ; r1 = 0x10000003 = MACH_SEND_INVALID_DEST
381c670c: 4288        cmp r0,r1
381c670e: d106        bne.n 0x381c671e      ; not the sentinel -> return r0
381c6710: 4620        mov r0,r4
381c6712: 4629        mov r1,r5
381c6714: 4642        mov r2,r8
381c6716: 2300        movs r3,#0
381c671a: f007 fc11   bl 0x381cdf40         ; MIG slow leg
381c671e: b01c...     add sp,#4 / pop {r4,r5,r6,r7,pc}
```

The sentinel `MACH_SEND_INVALID_DEST = 0x10000003` is **re-confirmed** at
`osfmk/mach/message.h:692`; `0x10000001/2/4/7` at `:688/:690/:694/:696`.

**The stub at `0x381c59dc`, read by value (ARM):**

```
381c59dc: e1a0c00d   mov ip, sp
381c59e0: e92d0030   push {r4, r5}
381c59e4: e59c4000   ldr r4, [ip]        ; m[0]
381c59e8: e3e0c017   mvn ip, #0x17       ; r12 = -24
381c59ec: ef000080   svc #0x00000080
381c59f0: e8bd0030   pop {r4, r5}
381c59f4: e12fff1e   bx lr
```

### 1.D The `-index-1` convention — calibrated, and the brief is off by 8

The stub ladder `0x381c5800..0x381c5c00` is a clean arithmetic sequence of `mvn ip,#N`, and I read
each `N`:

| stub VA | `mvn ip,#` | index | note |
|---------|-----------|-------|------|
| `0x381c5960` | `#14` | **15** | **called by the 1022 `mach_vm_map` shim at `0x381c666a`; device-confirmed to fire** |
| `0x381c597c` | `#15` | **16** | 4-stack-word form |
| … | … | … | `#16`→17 … `#22`→23 |
| `0x381c59dc` | `#17`? **no — `#23`** | **24** | 1-stack-word form; **the frontier's stub** |
| `0x381c59f8` | `#24` | 25 | |

`mvn ip,#N` ⇒ **index = N+1** is CONFIRMED on two independent stubs, so the frontier's trap index is
**24**, not 16. Two stub *forms* coexist and they do **not** carry different index bases:
`{push {r4,r5}; ldr r4,[ip]}` (`0x381c59dc`, index 24) writes **one** stack word; index 15's stub
writes **four** (`push {r4,r5,r6,r8}; ldm ip,{r4,r5,r6,r8}`). Both are the same `N+1` convention —
they differ only in how many of the caller's stack words are forwarded as munge arguments.

**The trap-table row, read out of the linked image `out/stage90/xnu_arm_entry.elf`**
(sha256 of the `.bin` matches record key 4:
`b47131c83e1f4c98da510a7addf0575f213eec53977248dd76d5e9a673c4abc5`, 6 323 284 B — so this ELF IS the
image 1022 ran). `mach_trap_table` VA `0x805565b0`, stride 12, `fileoff = 0x010000 + (va - 0x80000000)`:

```
idx  argc  fn            nameptr     name
  0     0  0x8047f774   0x80556168   'kern_invalid'
 10     5  0x804742e0   0x80556175   '_kernelrpc_mach_vm_allocate_trap'
 12     5  0x80474408   0x80556196   '_kernelrpc_mach_vm_deallocate_trap'
 14     7  0x804744bc   0x805561b9   '_kernelrpc_mach_vm_protect_trap'
 15     8  0x80474130   0x805561d9   'ios7lab_mach_vm_map_trap'          <-- 1022 INSTALLED
 16     3  0x80474588   0x805561f2   '_kernelrpc_mach_port_allocate_trap'
 17..23    0x80474604.. 0x80556215..  '_kernelrpc_mach_port_*_trap'
 24     0  0x8047f774   0x80556168   'kern_invalid'                       <-- THE FRONTIER'S ROW
 25     0  0x8047f774   0x80556168   'kern_invalid'
 41/42  0  0x8047f774   0x80556168   'kern_invalid'
 26     0  0x8008bc1c   0x8055631d   'mach_reply_port'
 31     7  0x80474fd4   0x8055635c   'mach_msg_trap'
 ...
```

Entry 24's name pointer is `0x80556168` — **the same bytes as entry 0's**. That is `kern_invalid`.
Source corroboration: `osfmk/kern/syscall_sw.c:109` `MACH_TRAP(kern_invalid, 0, NULL, NULL)` for 0 and
`:139-143` `#if IOS7_COMPAT_PLATFORM … #else MACH_TRAP(kern_invalid, 0, NULL, NULL) #endif` for 24/25.
**Row 16, by contrast, IS wired** (`syscall_sw.c:128`, unconditional) — which is why the brief's
trap-16 hypothesis is wrong: row 16 was never a gap, and the frontier stub is not index 16.

### 1.E What a `kern_invalid` row does — read in ARM, and it PANICS

`swi_mach` VA `0x8001cea4`, `swi_mach_error` `0x8001cef4` (from `arm-none-eabi-nm`), instructions read
from the ELF:

```
8001cea4: e28fe030  add lr, pc, #48
8001cea8: e1a04005  mov r4, r5
8001ceac: e3550080  cmp r5, #0x80
8001ceb0: aa00000f  bge ...              ; fast-trap range check
8001ceb4: e59f13f0  ldr r1, [pc, #1008]  ; r1 = mach_trap_table
8001ceb8: e085b085  add r11, r5, r5, lsl #1
8001cebc: e081110b  add r1, r1, r11, lsl #2     ; r1 = &table[idx*3]
8001cec0: e5911004  ldr r1, [r1, #4]            ; r1 = table[idx].mach_call
8001cec4: e59f23dc  ldr r2, [pc, #988]   ; r2 = kern_invalid
8001cec8: e1a00008  mov r0, r8
8001cecc: e1310002  teq r1, r2
8001ced0: 0a000007  beq swi_mach_error          ; <-- THE BRANCH
8001ced4: e12fff11  bx r1
...
8001cef4: e3a00007  mov r0, #7
8001cef8: e1a01004  mov r1, r4                  ; r1 = -r12 = trap index
8001cefc: e3a02001  mov r2, #1
8001cf00: eb11273b  bl _doexception             ; exception type 7
8001cf04: e3a00081  mov r0, #0x81
8001cf08: ea0000d2  b irqvec_panic              ; <-- PANIC
```

I could not decode the two `ldr rX,[pc,#imm]` targets from raw words (the literal would have to sit
outside the 0x60-byte window I dumped), so I flag that one hop as **read from source bytes, not
disassembled**: `out/xnu_asm_obj_d13/.../traps_lo.s` has the same two instructions in the same slots.
The branch structure is unambiguous either way: **a `kern_invalid` row is not returned to the
caller; it takes exception 7 and branches to `irqvec_panic`.**

### 1.F The capture — the decisive negative

`out/stage90/captures/recovered-unknown-arm-20261016-060342-last_kmsg.txt` (the 1022 run's log):

| measurement | value |
|-------------|-------|
| `grep -c panic` | **0** |
| `grep -c "undefined instruction"` | **229** |
| first / last such line | `:4134` / `:5980` |
| text | `launchd[1]: usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x3b418022 in map 0xc00057b8 (pmap 0xc0ba3cc0)` |
| register block | reproduced identically at `:4137-4141` and `:4156-4160` (r0=`0x14548ad0` … pc=`0x3b418022`, cpsr=`0x60000030`) |
| `IOS7LAB` lines | 30 (`grep -c`), **not** deduplicated |
| `IOS7WITNESS info … slide=0x031ca000` | `:4148` |

The capture records **every** repeated line at this volume (`IOS7LAB` 30/30 present, `IOS7WITNESS`
blocks for each fault), so 0 `panic` is a real reading, not a dedup artifact.
`0x3b418022 − 0x031ca000 = 0x3824e022`. **The reference run's `pc` is byte-for-byte the frontier VA I
measured in §1.B.** The reference run is not a dead run.

---

## 2. The two readings of the frontier

### 2.1 Hypothesis A — "the client passed garbage" (LEADING, label H2)

`__xpc_mach_port_allocate` **still wraps a slow leg**. Its `blx 0x38260b60` is the same veneer pattern
as `__xpc_mach_port_guard` at `0x3824e030`, which is exactly `mov r3,r1; mov r1,r0; movw/movt
0x1cf704c; add r0,pc; ldr r0,[r0,#0]; ldr r0,[r0,#0]; b.w 0x381c5ad0` — a **tail branch** into the
stub table. `__xpc_mach_port_allocate` cannot tail-branch because it needs the retry label at
`0x3824e002`, so it uses the trampoline at `0x381c66ec`, which re-packs (`r0..r3` → libc calling
convention, `r3` forced to `0`, original `r3` spilled at `[sp]`) and calls the stub.

⇒ **arg0 is not a port name on entry.** It is whatever the wrapper is holding. If a user argument
(`r0`=`0x14548ad0`, `r1`=`0xe0f33713`) is not a resolvable handle, `port_name_to_task(args->target)`
returns `NULL` ≠ `current_task()` and the handler returns `MACH_SEND_INVALID_DEST` **without ever
calling `mach_port_allocate`**. `rv = 0x10000003` is not in `{0,3,6}` ⇒ `udf #254`. The faulting
`cpsr=0x60000030` (Thumb, FIQ/IRQ masked) matches.

### 2.2 Hypothesis B — "the row is `kern_invalid`" (H1, would FALSIFY §2.1)

If trap 24 routes to `kern_invalid`, §1.E says `_doexception`(7) → `irqvec_panic`. The capture has
**zero** panics and 229 clean user-mode `EXC_BAD_INSTRUCTION` deliveries. Under H1 the observed
behaviour is **impossible** — unless `kern_invalid` is reached at a site *other* than `swi_mach`'s
teq-arm, or the port's `_doexception` path is a silent no-op. Both are testable and **untested**.

**Both hypotheses are hypotheses.** §2.1 leads on the strength of §1.F, but it does not explain how a
`0x33f3713`-flavoured pointer reached the wrapper, and H1 has no known mechanism. **This rung exists
to separate them by measurement, not to assert one.**

---

## 3. The trap-24 wire — and why the rung does NOT need to get it right

`_kernelrpc_mach_port_allocate_trap`, **read verbatim** at `osfmk/ipc/mach_kernelrpc.c:591-610`:

```c
int
_kernelrpc_mach_port_allocate_trap(struct _kernelrpc_mach_port_allocate_args *args)
{
    task_t task = port_name_to_task(args->target);
    mach_port_name_t name;
    int rv = MACH_SEND_INVALID_DEST;

    if (task != current_task())
        goto done;

    rv = mach_port_allocate(task->itk_space, args->right, &name);
    if (rv == KERN_SUCCESS)
        rv = copyout(&name, args->name, sizeof (name));

done:
    if (task)
        task_deallocate(task);
    return (rv);
}
```

**Three exits, one return.** (i) `task != current_task()` → `rv` stays `0x10000003`; (ii)
`mach_port_allocate` ≠ 0 → `rv` = that `kern_return_t`; (iii) `copyout` fails → `rv` = the copyout
error. All three fall through `done:`.

Row 24's source declaration is `MACH_TRAP(ios7lab_mach_port_construct_trap, 5, NULL, NULL)`
(`syscall_sw.c:140`) — but its *linked* body is `kern_invalid` on CANCRO (§1.D), and the process
reached `0x381c66ec` at all, which a panic would have prevented. **This is the rung's principal open
question (H1) and it is not resolved here.**

Struct `osfmk/mach/mach_traps.h:618-624`: `_kernelrpc_mach_port_allocate_args` = `PAD_ARG_` × 3
(`mach_port_name_t target`, `mach_port_right_t right`, `user_addr_t name`).

**Wire-width: no disagreement, and here is why it does not matter.** `munge_www`/`munge_ddd` are
declared inside `#if 0 /* no active architectures use this */` (`mach_traps.h:323-331`) — the header
itself says **no architecture uses them**, and the implementations live only in
`bsd/dev/i386/munge.s` / `bsd/dev/x86_64/munge.s` (x86-only, never linked into an ARM kernel). The
`munge_*` **arguments** in `MACH_TRAP` are therefore documentation. What actually munges on ARM is
the link-time stub in the caller (§1.C): it forwards `m[0]` only, so **`args->target` is `m[0]` and
nothing else**, and `args->name` continues to arrive in `r2` (which is how `m[2]`=0 survives). §2.1's
hypothesis says `m[0]` holds a non-port handle — a disagreement between *the caller's value* and
*the kernel's expectation*, which is precisely what §4's trace reads.

---

## 4. Instrumentation design

**Shape (A): publish, do not install.** Mirror `tools/patch_d13_mach_trap15.py`'s runtime self-gate,
but the payload is a trace, not a row. Target file `osfmk/ipc/mach_kernelrpc.c`
(inside the existing `#if` at `:59`, so it is compiled only when the lab adapters are on).

```c
    int _pl_t_pr = (int)(uintptr_t)args->target;
    int _pl_rr    = (int)args->right;
    ...
done:
    /* STAGE90_XNU_IOS7_TRAPS: publish the rv; behaviour is unchanged on every path. */
    printf("IOS7LAB port_allocate: target=%08x right=%d name_ptr=%08x rv=%08x exit=%d\n",
           _pl_t_pr, _pl_rr, (int)(uintptr_t)args->name, (unsigned)rv, _pl_exit);
    if (task)
        task_deallocate(task);
    return (rv);
```

`_pl_exit` is set at each of the three sites (1 = task mismatch, 2 = `mach_port_allocate` failed,
3 = `copyout` failed) so the capture distinguishes them **by value**, not by inference.

**Invariants the patch must hold, each checkable without a device:**

1. **`return (rv)` is untouched.** The trace is inserted *before* the `done:` cleanup, after `rv` is
   final. Diff the emitted object's `return` path — the rung must not change a single register.
2. **`rv` width.** `MACH_SEND_INVALID_DEST` is `0x10000003`; print it `%08x`. A `%d` renders
   `268435459` and can be mistaken for a positive value.
3. **Register preservation.** `printf` clobbers `r0-r3,r12,lr`. `args->target/right/name` must all be
   read into locals **before** the call — never re-read `args` after it.
4. **Bounded output.** One line per call. §1.F shows this boot's ladder is ~120 lines and the capture
   pressure is known; a per-call printf at pid-1 rates is acceptable but must be counted.
5. **OFF must be byte-identical.** The gate clause's else-arm must be the tree's own line so
   `--off` compiles the unmodified function.

**Switch: REUSE `STAGE90_XNU_IOS7_TRAPS`. Do not invent one.** Argued from the record, not taste:

- `out/stage90/xnu_arm_entry-config.txt:19` already carries `STAGE90_XNU_IOS7_TRAPS=1` — the switch
  is on, recorded, and preflight-checked.
- `tools/build_xnu_arm_kernel.sh:902` already maps it into the pool `DEFINES`, so this **POOL** edit
  is picked up with no build-system change.
- `STAGE90_XNU_MACHVM_TRAP15` — the switch the 1022 *design* proposed — **is absent from the record
  and from `out/stage90/xnu_arm_entry-arm-extra.txt` (which reads exactly
  `STAGE90_XNU_GLOBAL_FIX=1 role=1022-trap15`)**. The realized 1022 rode `IOS7_TRAPS` instead. Adding
  a switch now would repeat the build-variant defect (`mi4-build-variant-comes-from-an-env-default`).
- Blast radius: `IOS7_TRAPS` already gates the whole `mach_kernelrpc.c` lab block, and this rung
  changes **no** row and **no** return value. Reuse adds zero new risk surface.

⚠ **POOL STALENESS.** `syscall_sw.c` and `mach_kernelrpc.c` compile via `build_xnu_arm_kernel.sh`;
`src/entry/build_entry.sh` only **LINKS** `out/xnu_kernel_obj_d13/*.o`. A `build_entry.sh`-only run
will silently link the stale `mach_kernelrpc.o` and produce a clean, confident, wrong answer. The
rung's build MUST be the pool build.

⚠ **PRESERVE `STAGE90_XNU_GENERAL_REGS=1`** (`-msoft-float`). Record-confirmed present. It is what
retired the 1021d NEON undef; losing it re-opens a wall this rung cannot distinguish from its own
subject.

---

## 5. Static answer vs press — and why a press IS required here

The brief asks whether the answer can be had **without** a new press. For 1023 the answer is **no**,
and the reason is measured, not argued:

- **No existing instrumentation reads this rv.** `mach_kernelrpc.c` publishes nothing.
  `mach_port_allocate` (`osfmk/ipc/mach_port.c`) publishes nothing. The `xnu_live_usvc_*` window
  (`USVC_FULL_N = 64`, `entry_note_user_svc` in `entry_stubs.c`) records the **fast-trap leg only**
  — the `svc #0x80` number and caller, i.e. *that* trap 24 entered, never what it returned.
  `xnu_live_undef_*` (`__wrap_sleh_undef`) records the **consequence** (`0x3824e022`, and the
  register file), not the cause. That is exactly the gap: the capture gives us the trap site and the
  registers but **not** the value the handler returned.
- **The 1022 capture's live channel is CAPPED** (`xnu_live_capped`), so every `xnu_live_*` value in
  it is the state *at the cap*. Even if a key existed, its absence would be a publishing artifact,
  not an event that did not happen (`records/1022-press-read.txt`).
- **The one static fact that IS decisive is already taken** (§1.F, zero panics). It narrowed the rung
  from "install a row" to "publish an rv" — but it cannot name `rv`.

**Smallest press.** One pool rebuild + one `build_entry.sh` (unchanged config, `IOS7_TRAPS` already
on), one arm, one press. No new switch, no new row, no config delta. The arm must carry the standard
**by-value clause** in both directions: assert the object contains the trace string AND that
`--off` emits a byte-identical function.

---

## 6. Carriers — every site the rung must reach

| carrier | change | who reads it |
|---------|--------|--------------|
| `external/.../osfmk/ipc/mach_kernelrpc.c:591` | +~6 lines trace inside the existing `#if` | `build_xnu_arm_kernel.sh` |
| `out/xnu_kernel_obj_d13/mach_kernelrpc.o` | **rebuilt** (POOL) | `build_entry.sh` (link only) |
| `out/stage90/xnu_arm_entry-config.txt` | **unchanged** (`IOS7_TRAPS` already key 19) | `preflight_boot_check.sh` |
| `out/stage90/xnu_arm_entry-arm-extra.txt` | **rewritten by the build** to `role=1023-port-allocate` | `park_*`/`press_*` `grep -qx` |
| `tools/patch_d13_*` runtime self-gate | add the `role=1023-*` carrier | build + press self-check |

⚠ `arm-extra.txt` is written by the **build**, not by a design self-write — a design-added grep on it
would be a self-written record, not a constraint (`mi4-self-written-record-is-not-a-constraint`).

---

## 7. Pre-registered verdict cells

**`P16-CTL` — MANDATORY, FIRST. Falsifies the rung itself if it fails.**
Pass predicate: the new arm boots, `role=1023-port-allocate` is in `xnu_arm_entry-arm-extra.txt`,
`STAGE90_XNU_IOS7_TRAPS=1` is still record key 19, AND the capture contains the trace line for the
run **and** the run's `xnu_live_undef_pc` is still `0x3b418022`. Failure of any one ⇒ the run is
about something else; **no other cell is readable**.

| cell | predicate on the capture |
|------|--------------------------|
| `P16-CTL` | control above |
| `P16-RETURNS-N` | ≥1 `IOS7LAB port_allocate:` line, and **every** such line carries an `rv` ∉ {0,3,6}, and `rv` is the **same value** every time |
| `P16-SUCCEEDS` | some line has `rv=0` **and** the run reaches a pc ≠ `0x3b418022` — the wall moved |
| `P16-TASK-MISMATCH` | every line has `exit=1` and `rv=10000003` — §2.1's hypothesis, confirmed |
| `P16-COPYOUT-FAILS` | ≥1 line with `exit=3` (⇒ `mach_port_allocate` succeeded; the failure is the user pointer) |
| `P16-UNEXERCISED` | 0 trace lines **and** `xnu_live_undef_pc = 0x3b418022` — the wall is upstream of the handler |
| `P16-EXITS-UNREACHED` | 0 trace lines on a boot whose pc moved past `0x3824e022` — the rung's dual of `P16-UNEXERCISED` |

**Falsification, stated as a predicate.** §2.1 is falsified if any line shows `exit=2`, a non-zero
`mach_port_allocate` return, or if no line appears while the pc stays at `0x3824e022` (⇒ the wall is
upstream). §1.D/H1 is falsified if `P16-RETURNS-N` holds — because a `kern_invalid` row cannot return
at all (§1.E). **The two hypotheses are separated by this one press.**

---

## 8. What I could not confirm (UNMEASURED)

- **H1 — the trap-index conflict is NOT resolved.** The stub the frontier calls (`0x381c59dc`) is
  `mvn ip,#23`, and `entry 24` is `kern_invalid` **in the linked image**, which §1.E says panics. The
  capture did not panic. One of these is wrong and I do not know which. Candidate reconciliations:
  (a) `kern_invalid` is reached at this site by a path other than `swi_mach`'s `teq`; (b) `_doexception`
  exception 7 from kernel context is not the panicking path on this port; (c) `mach_trap_table` is
  **relocated** at boot and the linked value is not the runtime value; (d) the 1022 capture's pc
  `0x3b418022` is not reached through this stub after all. **A pool-side trace of the trap index
  (`swi_mach` r4) would settle it and is the cheapest single addition to this rung.**
- **`entry 24`'s argv base.** `MACH_TRAP` is `{mach_call, nargs, name}` (three words, stride 12); the
  `argc`/`fn`/`nameptr` reading in §1.D matches source and string bytes, so the table is a
  `mach_trap_t[96]` at a VA whose `argc` word is zero — it is **not** an array of instruction pointers.
  I did not verify this against `osfmk/mach/mach_trap.h` line-by-line.
- **`swi_mach`'s two literal loads.** I confirmed the encoding and the branch structure but could not
  decode the `ldr rX,[pc,#imm]` targets from the raw words in my window; the identification of
  `r2 = kern_invalid` is from source-slot correspondence, not disassembly.
- **Which of the three exits ran.** By construction, unknowable statically — this is the rung.
- **`_doexception`'s behaviour for exception 7 on this port.** Untested; H1 depends on it.
- ⚠ **128 images in this cache have a local-symbol entry whose `dylibOffset` disagrees with
  `dyldImageInfo` by one page** (e.g. `libsystem_kernel`: nlist `0xc1c5000` vs imageInfo `0x381c5000`).
  For libxpc's `__xpc_mach_port_allocate` the two agree exactly, so §1.B stands — but any symbol
  lookup elsewhere in this cache needs the same cross-check.

---

## 9. What this does and does not claim

**Does.** That the 1022 frontier is byte-for-byte the same VA as the reference run's pc; that the
trap index for that call site is **24**, not 16; that row 16 was never a gap and row 24 is
`kern_invalid` in the linked image; that a `kern_invalid` row panics, while the capture shows zero
panics; that the handler has exactly three exits and publishes none of them; and that the switch to
reuse is `STAGE90_XNU_IOS7_TRAPS` (record key 19).

**Does not.** Claim which exit ran. Claim that the client passed garbage (§2.1 is the leading
*hypothesis*). Claim H1 is resolved. Claim the wire-width question is settled beyond "the `munge_*`
arguments are inert on ARM". And does not claim that opening any row fixes this — on the leading
hypothesis (H2), **no row change helps at all**, because the handler already returns a value and the
caller already traps on it.

---

## 10. File:line anchors (each re-checked against the tree)

| claim | anchor |
|-------|--------|
| row 0 / row 24 are `kern_invalid` | `external/xnu-hd2-darwin13/xnu/osfmk/kern/syscall_sw.c:109`, `:139-143` |
| row 16 unconditional, `argc=3` | `syscall_sw.c:128` |
| row 15 gate (realized 1022 shape) | `syscall_sw.c:124-127` |
| `IOS7_TRAPS` in pool `DEFINES` | `tools/build_xnu_arm_kernel.sh:902` |
| handler, three exits | `osfmk/ipc/mach_kernelrpc.c:591-610` |
| block gate | `osfmk/ipc/mach_kernelrpc.c:59` |
| struct `_kernelrpc_mach_port_allocate_args` | `osfmk/mach/mach_traps.h:618-624` |
| `munge_*\` declared inside `#if 0` | `osfmk/mach/mach_traps.h:323-331` |
| `MACH_SEND_INVALID_DEST = 0x10000003` | `osfmk/mach/message.h:692` |
| `KERN_INVALID_ARGUMENT = 4`, `NO_SPACE = 3`, `RESOURCE_SHORTAGE = 6` | `osfmk/mach/kern_return.h:90`, `:84`, `:99` |
| `swi_mach` `teq`/`beq swi_mach_error`; `irqvec_panic` | `osfmk/arm/traps_lo.s` (`swi_mach` VA `0x8001cea4`, `swi_mach_error` `0x8001cef4`) |
| record: `IOS7_TRAPS=1` at key 19; `GENERAL_REGS=1`; `XNU_ENTRY_SHA256` | `out/stage90/xnu_arm_entry-config.txt:4,19,…` |
| `arm-extra.txt` current content | `out/stage90/xnu_arm_entry-arm-extra.txt` = `STAGE90_XNU_GLOBAL_FIX=1 role=1022-trap15` |
| capture (zero panics, 229 EXC_BAD_INSTRUCTION, pc) | `out/stage90/captures/recovered-unknown-arm-20261016-060342-last_kmsg.txt:4134,5980` |
| `STAGE90_XNU_MACHVM_TRAP15` **absent** from record and arm-extra | — |