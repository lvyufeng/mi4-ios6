# D13 dyld start-trap forensics (launchd pid 1, 236 identical crashes)

Read-only forensic analysis of the first userspace crash after the D13 kernel exec'd
`/sbin/launchd` (dylinker `/usr/lib/dyld`). All addresses are decoded **by value**
(`arm-none-eabi-objdump -b binary -m arm -M force-thumb -D --adjust-vma=0x2be00000`
over the `__TEXT` bytes, cross-checked with `xxd`). objdump prints halfword values;
xxd prints memory-order bytes.

Image: `/tmp/dyld_ios7`, 196576 bytes, sha256
`408061f0ec0a2d037f9c791f7c2c744bd1c3fd0ce2f6e0b550670e1d46755310`.
Mach-O armv7 MH_DYLINKER; `__TEXT` fileoff 0x1000, vmaddr 0x2be00000;
therefore **file offset == link VA − 0x2be00000** for `__TEXT`.
Runtime slide (measured): **0x7D000**, so the crash pc 0x2be7e0c4 = link 0x2be010c4.

Crash block (from `/tmp/cancro-last_kmsg.txt`, repeated 236 times byte-identically):

```
launchd[1]: usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x2be7e0c4
r0: 0x2bec6564  r1: 0x00000001  r2: 0x2be9eb1c  r3: 0x2be9ec8c
r4: 0x2bec6564  r5: 0x2be997fd  r6: 0x00000002  r7: 0x2fd829f8
r8: 0x2bea13c0  r9: 0x2fd829e4 r10: 0x2fd82728 r11: 0x2fd82a24
r12: 0x00000000 sp: 0x2fd829f0  lr: 0x2be7ef07  pc: 0x2be7e0c4
cpsr: 0x80000010
```

A note on PC-relative arithmetic used throughout: Thumb `add rX, pc` computes
`PC = instruction_address + 4` with **no word alignment**. This was verified
empirically twice (throwf's literal resolves to the exact byte start of
"throwf, out of memory error"; the bindLazySymbol pad's literal to the exact byte
start of "dyld: lazy symbol binding failed: %s\n"). All literals below use this rule.
(`Align(PC,4)` is wrong for the odd-address sites and yields off-by-2 artifacts.)

## 1. The trap site

`xxd -s 0x10b0 -l 0x28 /tmp/dyld_ios7`:

```
000010b0: a4ef ffff 00c0 9fe5 0cf0 9fe7 2c0a 0200
000010c0: fede ffe7 fede ffe7 00f0 20e3 1eff 2fe1
000010d0: f0b5 03af
```

- 0x2be010b0: literal 0xFFFFEFA4 (two halfwords `efa4 ffff`)
- 0x2be010b4: `ldr ip, [pc]` (`00c0 9fe5`), 0x2be010b8: `ldr pc, [pc, ip]` (`0cf0 9fe7`)
  — the stall/dispatch preamble; LC_FUNCTION_STARTS also lists 0x2be010b4 as a start.
- 0x2be010c0: `dyld_stub_binding_helper` — bytes `fe de ff e7`
  = halfwords 0xdefe 0xe7ff = **`udf #65006`**
- **0x2be010c4: `_dyld_fatal_error` — bytes `fe de ff e7` = `udf #65006`  ← crash pc**
- 0x2be010c8: `nop` (`00f0 20e3` = 0xe320f000)
- 0x2be010cc: `bx lr` (`1eff 2fe1` = 0xe12fff1e)
- 0x2be010d0: `dyldbootstrap::start` — `push {r4,r5,r6,r7,lr}` (`b5f0`)

The trap is a deliberate two-instruction fatal stub (`udf #65006`), not a data or
text corruption: the byte pattern is exact and the neighbouring function
(`dyldbootstrap::start`) begins on its own `push` prologue 4 bytes later.

Reference census: the value 0x2be010c4 occurs in the whole file **only** in the
LC_SYMTAB nlist entry for `_dyld_fatal_error` (file offset 0x26b84). The slid value
0x2be7e0c4 occurs nowhere in the file. The only *code* reference in the image is the
`blx` at link 0x2be01f02 (§2). Nothing loads this address as a function pointer.

## 2. The caller: `dyld::halt(char const*)` at 0x2be01ebc

Symbol `__ZN4dyld4haltEPKc` (LC_SYMTAB). Full body, 0x2be01ebc .. 0x2be01f06,
next function starts 0x2be01f08 with its own `push {r4,r5,r6,r7,lr}`:

```
2be01ebc  b5b0        push {r4,r5,r7,lr}
2be01ebe  4605        mov  r5, r0                  ; r5 = msg
2be01ec0  f64a 60c6   movw r0, #0xaec6
2be01ec4  f2c0 0001   movt r0, #1
2be01ec8  4629        mov  r1, r5
2be01eca  4478        add  r0, pc                  ; 0x2be1cd94 = "dyld: %s\n"
2be01ecc  af02        add  r7, sp, #8
2be01ece  f7ff fc8d   bl   0x2be017ec             ; dyld::log("dyld: %s\n", msg)
2be01ed2  f247 6480   movw r4, #0x7680
2be01ed6  4629        mov  r1, r5
2be01ed8  f2c0 0404   movt r4, #4
2be01edc  f44f 6280   mov.w r2, #1024
2be01ee0  447c        add  r4, pc                  ; r4 = 0x2be49564 = _error_string
2be01ee2  4620        mov  r0, r4
2be01ee4  f016 f9b2   bl   0x2be1824c             ; strlcpy(_error_string, msg, 1024)
2be01ee8  f64f 2068   movw r0, #0xfa68
2be01eec  f2c0 0001   movt r0, #1
2be01ef0  4478        add  r0, pc                  ; r0 = 0x2be2195c = &gLinkContext
2be01ef2  f890 0097   ldrb.w r0, [r0, #151]        ; byte @0x2be219f3 (== 0 in file)
2be01ef6  f080 0101   eor.w r1, r0, #1             ; r1 = !flag
2be01efa  4620        mov  r0, r4                  ; r0 = _error_string
2be01efc  f006 ff02   bl   0x2be08d04             ; setAlImageInfosHalt(_error_string, !flag)
2be01f00  4620        mov  r0, r4
2be01f02  f7ff e8e0   blx  0x2be010c4             ; _dyld_fatal_error  <-- UNCONDITIONAL TAIL
2be01f06  bf00        nop
```

Findings:

- **The `blx` is unconditional and terminal.** There is no conditional branch
  anywhere between the prologue and 0x2be01f02; the function ends at 0x2be01f06
  (nop) and the next function begins at 0x2be01f08. Every invocation of
  `dyld::halt` reaches the udf. objdump's linear Thumb decode is correctly aligned
  here — there is no hidden branch flow.
- Frame check against the crash: halt's `push {r4,r5,r7,lr}` + `add r7, sp, #8`
  puts r7 at sp+8. Crash r7 = 0x2fd829f8 and sp = 0x2fd829f0 match exactly, so the
  crash-time register file **is** halt's frame at its last instruction. The caller's
  return address (the catching pad's `bl`+4) is saved at [sp+12] = **[0x2fd829fc]**.
- Literal resolutions (rule: instr+4): 0x2be1cd94 = `"dyld: %s\n"`;
  0x2be49564 = `_error_string` (`__common`, vmaddr 0x2be49564);
  0x2be2195c = `gLinkContext` (`__DATA.__data`), flag byte at +151 = 0x2be219f3.

## 3. The enclosing function of 0x1EF0

0x2be01ef2 (`ldrb r0,[r0,#151]`) lies inside **`dyld::halt`** itself
(0x2be01ebc..0x2be01f06). Walking back from 0x1EF0, the nearest
`push {..., lr}` prologue is at 0x2be01ebc; LC_FUNCTION_STARTS lists 0x2be01ebc as
a function start and lists no start between it and 0x2be01f08. The enclosing
function of the `ldrb`/`eor`/`bl 0x8d04` sequence is `dyld::halt`, whose only
argument is the message string in r0.

## 4. The callee 0x2be08d04: `setAlImageInfosHalt(char const*, unsigned long)`

Mangled `__Z19setAlImageInfosHaltPKcm`. Full body:

```
2be08d04  f648 620c   movw r2, #0x8e0c
2be08d08  f2c0 0201   movt r2, #1
2be08d0c  447a        add  r2, pc          ; r2 = 0x2be21b1c (holds gProcessInfo)
2be08d0e  6813        ldr  r3, [r2, #0]
2be08d10  6218        str  r0, [r3, #32]   ; gProcessInfo+0x20 = message
2be08d12  6810        ldr  r0, [r2, #0]
2be08d14  6241        str  r1, [r0, #36]   ; gProcessInfo+0x24 = second arg
2be08d16  4770        bx   lr
```

It records the halt message (r0) and the second word (r1) into the global
`gProcessInfo` (the `dyld_all_image_infos` block) before the trap. Single call site
in the whole image: 0x2be01efc, inside halt.

The `ldrb r0,[r0,#151]; eor.w r1,r0,#1` pair at 0x2be01ef2/0x2be01ef6: r0 = the
byte at `gLinkContext+0x97`, r1 = its complement; halt passes
`setAlImageInfosHalt(_error_string, !flag)`. A grep of the whole disassembly for
`#151`/`#0x97` finds **exactly two accesses** of that byte:

- 0x2be01cbe `strb.w r1,[r0,#151]` with r1 = 1 — the *first* statement of
  `initializeMainExecutable` (0x2be01c88), which also registers `runTerminators`
  with the libSystem helpers;
- 0x2be01ef2 `ldrb.w r0,[r0,#151]` — the halt site above.

Crash r1 = 1 ⟹ `!flag` = 1 ⟹ the byte was **0** ⟹ **halt ran before
`initializeMainExecutable`'s first statement.** (The file image's byte is also 0;
only initializeMainExecutable writes 1.)

## 5. Ranked mechanism verdict

### (a) dyld deliberately called its fatal-error trap from `dyld::halt` — CONFIRMED

Every crash register decodes to halt's frame by value (slide 0x7D000):

| reg | crash value | − slide | identity |
|---|---|---|---|
| pc | 0x2be7e0c4 | 0x2be010c4 | `_dyld_fatal_error`, `udf #65006` |
| lr | 0x2be7ef07 | 0x2be01f07 | 0x2be01f06 \| Thumb = just past the `blx` at 0x2be01f02 |
| r4 = r0 | 0x2bec6564 | 0x2be49564 | `_error_string` — exactly the ptr passed to setAlImageInfosHalt |
| r5 | 0x2be997fd | 0x2be1c7fd | **"throwf, out of memory error"** |
| r1 | 1 | — | `!flag`; flag byte still 0 (pre-initializeMainExecutable) |
| r7/sp | 0x2fd829f8/0x2fd829f0 | — | halt's frame (r7 = sp+8) |

So the abort is `dyld::halt(r5)` running to completion: log "dyld: %s\n",
`strlcpy(_error_string)`, `setAlImageInfosHalt`, `_dyld_fatal_error`.

The message in r5 identifies the failure: `dyld::throwf` (0x2be01640) took its own
out-of-memory branch. throwf's OOM path (`movw r2,#0xb163; movt r2,#1; add r2,pc`
at 0x2be01696 → 0x2be1c7fd) builds "throwf, out of memory error" and
`__cxa_throw`s it as a `const char*` (4-byte `__cxa_allocate_exception`).
The sibling literal "mkstringf, out of memory error" sits at 0x2be1c7de.

Chain: something called `dyld::throwf(...)` → `__simple_salloc()` (a 4096-byte
`vm_allocate` via mach trap) or the following `_strdup`→`_malloc` (zone bootstrap
via `vm_allocate` 0x25800) returned NULL → the OOM branch threw the literal →
SJLJ (`__Unwind_SjLj_*`) unwinding to a `catch (const char*)` landing pad →
`__cxa_begin_catch` → `dyld::halt(msg)` → the udf.

### (b) dyld called an unimplemented stub — EXCLUDED

The "unimplemented dyld function" mechanism in this image is a distinct site: the
`_lookupDyldFunction_ptr` slot (0x2be21aec) → 0x2be07a3d → `unimplemented()`
(0x2be07aa0) with its own literal 0x2be1dc30. The crash pc/lr are halt's, and r5 is
throwf's literal, not the unimplemented message. Not this path.

### (c) NULL/garbage function pointer landing on 0x2be010c4 — EXCLUDED

0x2be010c4 appears in the file only as an LC_SYMTAB nlist value; the slid
0x2be7e0c4 appears nowhere; the only code reference is the direct `blx` at
0x2be01f02. A garbage call would not reproduce r4 (= `_error_string`), r5
(= throwf's literal), r1 (= `!flag`), and r7/sp (= halt's frame) simultaneously —
all four are exactly what halt itself writes.

### Catcher frame (honest bound)

All landing pads that pass a caught `char*` to halt, and why each is excluded as
the caller of *this* halt:

- 0x2be01e34 in `dyld::runTerminators` (0x2be01d80) — excluded: it can only run
  after `initializeMainExecutable` has set the flag byte to 1 (crash r1 = 1 proves
  the flag was still 0).
- 0x2be03554 in `dyld::bindLazySymbol` (0x2be03488) and 0x2be0369a in
  `dyld::fastBindLazySymbol` (0x2be03588) — excluded: both log
  `"dyld: lazy symbol binding failed: %s\n"` (0x2be1cdc5) immediately before
  halting, and that string is absent from the captured log.
- 0x2be04b60 / 0x2be04be0 in `dyld::_main` (0x2be043b4..0x2be04c54) — their halt
  argument is built by `mkstringf` (0x2be015ec), i.e. a heap pointer, not the
  `__cstring` pointer the crash shows in r5.

The definitive answer is the saved lr at [0x2fd829fc]: the catcher's `bl halt`+4.
That memory was not captured in this run. A future arm that dumps 0x2fd829fc (or
logs it from the kernel on the exception) closes the catcher's identity exactly.

### Next lever

The halt message says the failing operation is a dyld-internal allocation:
`throwf`'s `__simple_salloc` (4096 bytes) or `_malloc`'s zone bootstrap
(0x25800 bytes), both of which bottom out in dyld's `_vm_allocate` (0x2be1bb70)
→ the kernel's `mach_vm_allocate`. The kernel side returning failure (or a return
shape dyld's minimal libc misreads) is the prime suspect; the Python-driven
question is why a 4 KiB `vm_allocate` fails in a freshly exec'd pid 1 given the
kernel's remaining VM state at that point.