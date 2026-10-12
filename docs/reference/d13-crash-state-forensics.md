# D13 crash-state forensics — register/memory decode of the 993 launchd trap

Read-only register and memory forensics on the 993 hardware capture. No device commands; one new file.
Sources read **by value**: `/tmp/cancro-last_kmsg.txt` (22,748 lines, the 993 capture, mtime Oct 12 15:49),
`/tmp/dyld_ios7` (196,576 B, dyld 324.1 as shipped), the persisted disassembly of that file
(`…/tool-results/bksrpbt04.txt`, llvm-objdump-14), and OUR build tree
`external/xnu-hd2-darwin13/xnu` (relative paths below are from it). Companion docs are cited, not
duplicated: `d13-dyld-start-trap-forensics.md`, `d13-dyld-halt-callers.md`, `d13-dyld-halt-message-recovery.md`,
`d13-user-udf-retry-mechanism.md`, `d13-commpage-content-verification.md`. Every claim is a machine reading
with a file:line / log-line / byte offset, or is explicitly marked **inference**.

---

## 0. The frame, and the one arithmetic that decodes it

The crash block (log:4101, repeated 236×, contiguous through 5981):

```
launchd[1]: usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x2be7e0c4 in map 0xc00057b8 (pmap 0xc0ba3cc0)
Thread has ARM register state:
    r0: 0x2bec6564  r1: 0x00000001  r2: 0x2be9eb1c  r3: 0x2be9ec8c
    r4: 0x2bec6564  r5: 0x2be997fd  r6: 0x00000002  r7: 0x2fd829f8
    r8: 0x2bea13c0  r9: 0x2fd829e4 r10: 0x2fd82728 r11: 0x2fd82a24
   r12: 0x00000000  sp: 0x2fd829f0  lr: 0x2be7ef07  pc: 0x2be7e0c4
  cpsr: 0x80000010
dyld_all_image_info_addr: 0x2be9ec8c   dyld_all_image_info_size: 0x000000a4
```

**Slide = 0x7D000**, established by `d13-dyld-halt-message-recovery.md:14-15` against dyld's own symtab
(`0x2be9ec8c−0x2be21c8c = 0x2be7e028−0x2be01028 = 0x2bec6564−0x2be49564 = 0x7D000`), and re-checked here
against `llvm-objdump --syms`: `__dyld_start = 0x2be01028` and `_error_string = 0x2be49564` are the exact
unslid ends of two of those differences.

**The decode is one formula.** dyld's only two segments are `__TEXT {vmaddr 0x2be00000, vmsize 0x21000,
fileoff 0}` and `__DATA {vmaddr 0x2be21000, vmsize 0x29000, fileoff 0x21000}` (`--private-headers`), and
`__DATA.fileoff == vmaddr − 0x2be00000` for both — so

```
unslid      = value − 0x7D000
file offset = unslid − 0x2be00000            (valid for __TEXT and for __DATA's 4096 file-backed bytes)
```

`__bss` (addr 0x2be21d30, size 0x27831) and `__common` (addr 0x2be49564, size 0x428) are zerofill: **no
on-disk bytes**, so a register landing in them names a *symbol* and nothing more.

---

## 1. Every register, by segment

User segments: dyld slid image `[0x2be7d000, 0x2be9e000)` (`__TEXT`, read-exec) and
`[0x2be9e000, 0x2bec7000)` (`__DATA`/`__bss`/`__common`, read-write); user stack, growing down from
`USRSTACK = 0x2FE00000` (`osfmk/arm/status.c:59`), i.e. the `[0x2fd80000, 0x2fe00000)` band the block's
`sp`/`r7`/`r9`/`r10`/`r11` all sit in; the kernel window `[0x80000000, 0xfffeffff]`.

| reg | value | unslid | segment | file off | what it is |
|---|---|---|---|---|---|
| r0 | 0x2bec6564 | 0x2be49564 | dyld `__common` (bss) | — (zerofill) | **`_error_string`** — symtab-confirmed `g O __DATA,__common`; halt's 1024-byte message buffer |
| r1 | 0x00000001 | — | scalar | — | halt's `setAlImageInfosHalt` 2nd arg = `1 − libSystemInitialized` |
| r2 | 0x2be9eb1c | 0x2be21b1c | dyld `__DATA,__data` | 0x21b1c | **`dyld::gProcessInfo`** — the pointer *variable*, symtab-confirmed `__ZN4dyld12gProcessInfoE` |
| r3 | 0x2be9ec8c | 0x2be21c8c | dyld `__DATA,__all_image_info` | 0x21c8c | **`dyld_all_image_infos`** — `*gProcessInfo`; the exact addr `trap.c:919` prints |
| r4 | 0x2bec6564 | 0x2be49564 | `__common` | — | same as r0: halt keeps `_error_string` in r4 for the `setAlImageInfosHalt`/`blx` pair |
| r5 | 0x2be997fd | 0x2be1c7fd | dyld `__TEXT,__cstring` | 0x1c7fd | **`"throwf, out of memory error"`** — the message halt was called with |
| r6 | 0x00000002 | — | scalar | — | `dyld::log`'s variadic arg count (halt passes none; a live leftover) |
| r7 | 0x2fd829f8 | — | user stack | — | halt's Thumb frame pointer: `add r7, sp, #8` (0x2be01ecc) with sp 0x2fd829f0 |
| r8 | 0x2bea13c0 | 0x2be2a3c0 | dyld `__bss` | — (zerofill) | unnamed static in dyld's allocator region — nearest named: `__ZL18initialPoolContent` 0x2be231b0 (**inference**) |
| r9 | 0x2fd829e4 | — | user stack | — | callee-saved, live |
| r10 | 0x2fd82728 | — | user stack | — | callee-saved, live |
| r11 | 0x2fd82a24 | — | user stack | — | callee-saved (ARM frame chain), live |
| r12 | 0x00000000 | — | scalar | — | scratch, zeroed |
| sp | 0x2fd829f0 | — | user stack | — | the fault-time stack; **the only sp value printed anywhere in the log** |
| lr | 0x2be7ef07 | 0x2be01f07 | dyld `__TEXT,__text` | 0x1f07 | `dyld::halt`**+0x4b** — the instruction *after* `blx _dyld_fatal_error` (0x2be01f02) |
| pc | 0x2be7e0c4 | 0x2be010c4 | dyld `__TEXT,__text` | 0x10c4 | **`_dyld_fatal_error`** — symtab `g F __TEXT,__text` |
| cpsr | 0x80000010 | — | — | — | `N` set, **mode 0x10 = user**, T=0 (**ARM** state) |

The three __TEXT bytes that matter, by `xxd`:

```
file 0x10c4  (pc, _dyld_fatal_error):  fe de ff e7 00 f0 20 e3 1e ff 2f e1
file 0x1c7fd (r5):                     74 68 72 6f 77 66 2c 20 6f 75 74   "throwf, out
file 0x1f07 (lr, halt+0x4b):           00 bf f0 b5 ...                   nop; push {r4,r5,r6,r7,lr}
```

`fe de ff e7` is little-endian `0xfedeffe7` — the `udf #65006` the kernel's own NEON test
(`osfmk/arm/trap.c:877`, `if (instruction != 0xfedeffe7)`) treats as "not NEON", i.e. the deliberate
trap. r5 is 4 bytes into the string (`throwf` begins at 0x1c7fd; the byte at 0x1c7fc is the NUL that
terminates `"mkstringf, out of memory error"` at 0x1c7e8). `lr` is one byte past halt's `blx`: the `blx`
at 0x2be01f02 wrote `lr = 0x2be01f07`, so **halt's caller is unrecoverable from lr** (see §3).

---

## 2. map 0xc00057b8 / pmap 0xc0ba3cc0

Both are kernel VAs in `[0x80000000, 0xfffeffff]`, so the printed map is a kernel `vm_map_t` — and it is
**pid 1's own user map**, proven three ways from the source of the print:

- The line comes from `osfmk/arm/trap.c:905-908` (the `cpsr == 0x10` branch, entered at `trap.c:843`).
  `trap.c:849` is `map = thread->map` — the *current thread's* map, not `kernel_map` — and `trap.c:908`
  prints that same `map` and `map->pmap`. `thread` is the running thread, whose `task->bsd_info` is
  `launchd`/pid 1 (`proc_name_address`/`proc_pid`, trap.c:907).
- `0xc00057b8` sits in the kernel's map pool: 990's capture logs `VMHINT991: obj=0xc0005b48 … obj=0xc0005ab0`
  (log:4096-4097), so `0xc0005xxx` is the range XNU hands out to `vm_map_create` — consistent with a
  `task->map` allocated there. **Inference** (no kernel memory is readable here): the map object is pid 1's.
- The map is not `kernel_map`, and `cpsr = 0x10` says the fault was taken in **user mode**, which for
  `sleh_undef` is only reachable with a user `thread->map`.

**Is 0x2be7e0c4 inside the map's dyld region?** Yes, by correlation and without touching kernel memory:

- `0x2be7e0c4` is dyld `_dyld_fatal_error`, inside dyld's `__TEXT` `[0x2be7d000, 0x2be9e000)` — the slid
  image, i.e. the region the map must already have mapped r-x for pid 1 to be executing there at all.
- The kernel *proves* the page is mapped in that map: `trap.c:862` does
  `copyin((uint8_t *)(arm_ctx->pc + thumb_offset), &instruction, 4)` and the following `OSSwapInt32`
  produces `0xfedeffe7` — the value that made the kernel take this branch. A `copyin` of the faulting pc
  only succeeds if `pc` is mapped and readable **in `thread->map`**. `map->pmap` (0xc0ba3cc0) is the pmap
  the kernel's fast-fault path (`trap.c:449`, `arm_fast_fault`) is handed for that same map.
- `0x2be7e0c4` is the *slid* pc; `map` is a runtime object, so no static file can name its regions. The
  correlation that stands is: **pc lies in dyld's slid __TEXT, and the kernel's own copyin of that pc
  succeeded in this map** — so the dyld __TEXT region is mapped in `0xc00057b8` and the fault is a
  legitimate execution inside a mapped, readable image, not an unmapped-page trap.

This is the 993 `udf`; it is **not** the 991 fault. `grep -c 0xffff4090` = **0**, so the
commpage/unmapped-page wall of 991 is behind us (the capture is the post-992/993 arm: log:3977-4021 carries
both `IOS7LAB vecl2` markers and `IOS7LAB commpage alias 0xffff4000: same page, user read-only, XN`).

---

## 3. The call chain from sp = 0x2fd829f0

**There is no stack dump in this capture to reconstruct it from.** Measured:

- `grep -c "Thread has ARM register state"` = 235 blocks, and the 236th (log:5981) is cut mid-line at
  log:5985 by the ring-buffer wrap. **No block contains a single stack-memory line** — `trap.c:905-920`
  prints the 17 registers and the aii addr/size and nothing else, no `sp`-relative words.
- `sp` has exactly **one** distinct value in the whole log: `0x2fd829f0`, 235×. There is no second frame,
  no `r7`-chain dump, and the `Backtrace 0xe92d4800 0x1 0x800ca158 …` at log:4094 is the *kernel's* own
  backtrace of the abort handler (values in the kernel window), not a user stack.

So the chain is reconstructed from **register values, not memory**, and it is one frame deep:

```
pc  0x2be7e0c4  _dyld_fatal_error          <- the trap instruction (udf #65006)
lr  0x2be7ef07  dyld::halt+0x4b            <- blx at 0x2be01f02 set lr; +0x4b = past the blx
r7  0x2fd829f8  halt's frame pointer       <- add r7, sp, #8 (0x2be01ecc)
sp  0x2fd829f0  halt's frame               <- push {r4,r5,r7,lr} at 0x2be01ebc took it down 16
r5  0x2be997fd  halt's message  = "throwf, out of memory error"
r0/r4 0x2bec6564 _error_string             <- halt's buffer, kept in r4 across the two calls
r2/r3 gProcessInfo / *gProcessInfo          <- the operands of the just-returned setAlImageInfosHalt
```

The `lr` is overwritten by the very `blx` that is the trap's call site, so **the function that called
`halt` cannot be read from this frame** (it is provable only that some frame called `halt(msg)`; the
candidate set is the 13 `dyld::halt` call sites enumerated in `d13-dyld-halt-callers.md:127`). What the
registers *do* fix is the last two operations halt performed, in order:

1. `dyld::log("dyld: %s\n", r5)` (0x2be01ece) — silent (see §5).
2. `strlcpy(_error_string, r5, 1024)` (0x2be01ee4) → r0/r4 = `_error_string`.
3. `setAlImageInfosHalt(_error_string, 1)` (0x2be01efc) — which loads r2 = `&gProcessInfo` and
   r3 = `*gProcessInfo`, i.e. the slid aii `0x2be9ec8c`, and stores `_error_string` at `aii+0x20`
   (errorMessage) and `1` at `aii+0x24` (terminationFlags). **The leftover r2/r3 are the fingerprint of
   this call** — the faulting `blx` is the instruction immediately after it.
4. `blx _dyld_fatal_error` (0x2be01f02) — the trap.

---

## 4. `dyld_all_image_infos` at file 0x21c8c

The kernel prints `dyld_all_image_info_addr: 0x2be9ec8c` and `size: 0x000000a4` — these are
`thread->task->all_image_info_addr/size` (`trap.c:919-920`), set by `note_all_image_info_section`
(`bsd/kern/mach_loader.c:249-269`) from the `__all_image_info` section: `vmaddr 0x2be21c8c` + slide 0x7d000
= **0x2be9ec8c**, size `0xa4`. The two agree exactly.

`0x2be9ec8c − 0x7d000 = 0x2be21c8c` = file 0x21c8c, section `__all_image_info` (addr 0x2be21c8c, size 0xa4,
the last file-backed bytes of `__DATA`). The 0xa4 bytes **on disk are the un-slid link-time template**,
parsed with `struct.unpack`:

| off | field | template value | reading |
|---|---|---|---|
| 0x00 | version | 0x0000000e | **14** |
| 0x04 | infoArrayCount | 0x00000000 | 0 in the template |
| 0x08 | infoArray | 0x00000000 | 0 in the template |
| 0x0c | notification | 0x2be010cc | dyld `__TEXT` file 0x10cc (`1e ff 2f e1` = `bx lr`), a link-time stub |
| 0x10 | processDetachedFromSharedRegion | 0x00000000 | |
| 0x11 | libSystemInitialized | 0x00 | |
| 0x14 | dyldImageLoadAddress | 0x2be00000 | **= the un-slid dyld base** |
| 0x18 | jitInfo | 0x00000000 | |
| 0x1c | dyldVersion | 0x2be1df23 | file 0x1df23 = **`"324.1"`** |
| 0x20 | errorMessage | 0x00000000 | ← halt's `setAlImageInfosHalt` writes `_error_string` here |
| 0x24 | terminationFlags | 0x00000000 | ← halt writes `1` here |
| 0x28..0x34 | coreSymbolicationShmPage … uuidArray | 0x00000000 | |
| 0x38 | dyldAllImageInfosAddress | 0x2be21c8c | self-pointer |
| 0x3c..0xa0 | remaining fields | all 0 | verified zero |

**Does `dyldImageLoadAddress` equal the slid dyld base?** The *template* holds `0x2be00000`, the un-slid
link-time base; `0x2be00000 + 0x7d000 = 0x2be7d000` = the slid base, and the slid value is what a correct
runtime would show. **The on-disk bytes cannot tell us the runtime value** — dyld rebases its own `__DATA`
(including this struct) at startup, and the log carries no dump of the runtime aii. What the log *does*
prove is that the pointer and size the kernel derived agree with this section to the byte (§ above), and
that `setAlImageInfosHalt` wrote into the runtime copy (r3 = the slid address). **Inference:** at runtime
`dyldImageLoadAddress` held the slid `0x2be7d000`; this is not directly observable from the capture.

**Does `infoArrayCount` look sane?** In the template it is `0`, which is the "not yet populated" link-time
state and is *not* evidence of a runtime count. A sane runtime value at halt would be ≥ 2 (dyld's own image
+ `/sbin/launchd`); the capture does not expose it. The one aii field the register state corroborates is
the write side: r3 = the slid aii address and r2 = its pointer-variable, matching `setAlImageInfosHalt`'s
`r2 = &gProcessInfo; r3 = [r2]; str …, [r3, #32]`.

---

## 5. Verdict — how far into dyld startup, and what was initialized

**How far.** dyld ran from its own entry, not from a fault on entry:

- The entry trace (log:4098-4100) shows `mini4: the OS starts the process at 0x2be7e028` =
  `__dyld_start` (symtab `0x2be01028` + slide), then `mini4: the OS's own init load returned` and
  `mini4: the AST is done -- pid 1's thread is at 0x2fd82f54 for user mode`. So the kernel set the user pc
  to dyld's entry and entered user mode there.
- The crash pc is deep inside dyld's `__TEXT` (file 0x10c4), not at the entry; dyld's own globals were
  relocated (`gProcessInfo` resolves to a slid `__DATA` address; `_error_string` to a slid `__common`
  address; the aii's `dyldVersion` string is at a slid address). dyld's `__DATA` had been slid — the
  template's `0x2be21c8c` is `0x2be9ec8c` at runtime per the kernel.
- The trap is dyld's **own deliberate halt**: `pc = _dyld_fatal_error`, `lr = dyld::halt+0x4b`,
  `r5 = "throwf, out of memory error"`. dyld called `halt` from its `throwf` out-of-memory path and halt
  ran to its `blx _dyld_fatal_error`. So dyld got far enough to enter its C++ startup, allocate, and hit
  its own OOM diagnostic — not to link and run launchd.

**Which structures were initialized.** From the registers (which is all the capture shows):

- `gProcessInfo` (`__DATA`, 0x2be9eb1c) — allocated/relocated, since halt dereferenced it as
  `*gProcessInfo` to reach the aii. **Yes.**
- `dyld_all_image_infos` at 0x2be9ec8c — present and being written: `setAlImageInfosHalt` stored
  errorMessage/terminationFlags into it. **Yes, at least the halt-writable tail.**
- `_error_string` (`__common`, 0x2bec6564) — filled by `strlcpy`. **Yes.**
- `dyld::log` sinks — **not** producing output (`dyld::log` at 0x2be01ece emitted nothing to the console),
  consistent with `d13-dyld-halt-message-recovery.md:§5` (all three sinks dead). **No.**
- `infoArray` / `infoArrayCount` — not observable; the template is 0 and the runtime copy is not dumped.

**Why the 236 repeats, and why the witness is silent.** The 236 identical blocks are the kernel
re-executing the same user `udf` pc (the retry mechanism is `d13-user-udf-retry-mechanism.md:§2-3`; the run
ends on the 128 KB console ring, not a counter). The port's own `ios7leo_dyld_witness` is compiled out on
this board: it sits under `#if defined(BOARD_CONFIG_QSD8250_LEO)` (`trap.c:858-861, 898-903, 922-924,
942-946`), while the D13 build is `BOARD_CONFIG_MSM8974_CANCRO`
(`tools/stage_d13_board.sh:74-75`), so only the unconditional `printf` at trap.c:905-920 runs — which is
exactly the 8-line block, 236×. (Machine reading: `out/xnu_kernel_obj_d13/osfmk_arm_trap.o` has **0**
`"Leo DYLD"` strings and 1 `"usermode undefined instruction"`; the control `osfmk_arm_pmap.o` has **4**
`IOS7LAB`.)

**The frontier.** Not a fault in a mapped page and not the commpage: dyld is running its own C++ startup
and has **halted itself** via `throwf`'s out-of-memory path → a catch handler → `halt(msg)`. The next wall
to answer is dyld's internal allocator (why `__simple_salloc`/the exception allocation returned NULL), not
an unmapped-address fault.

**What this capture does not establish** (stated, not inferred away):

1. The **caller of `halt`** — `lr` was overwritten by the `blx`; no stack dump exists anywhere in the log
   (0 stack-memory lines; one distinct `sp`). The 13 candidates are enumerated in
   `d13-dyld-halt-callers.md:127`; this frame cannot rank them.
2. The **runtime contents of the aii** — the log gives only its address/size; the on-disk 0xa4 bytes are
   the un-slid template. `infoArrayCount`/`infoArray`/`dyldImageLoadAddress` runtime values are not
   readable here.
3. **Who rebased dyld's `__DATA`** — the kernel's `note_all_image_info_section` or dyld's own startup —
   is not distinguishable from this log.
4. The entry-side trace `(sp 0x0)` at log:4100 is **not** a real sp: the D13 wrapper reads
   `thread + STAGE90_ACT_PCBDATA + STAGE90_SS_SP` with `STAGE90_ACT_PCBDATA = 580` (D13) but the state
   lives at `machine.user_regs` = 588 (`osfmk/arm/thread.h:67`), so thread+640 reads a **pointer word**,
   not the frame's sp. The same shift makes the printed `pc 0x2fd82f54` actually thread+632 = the state's
   **r11**. That `0x2fd82f54` is 0x564 above the crash `sp 0x2fd829f0` is consistent with it being a
   near-stack address, but it is the wrapper's offset defect, not a verified register — flagged here so it
   is not read as evidence.
