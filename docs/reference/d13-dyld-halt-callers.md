# d13 — iOS 7.1.2 dyld: every call to `dyld::halt`, and the message this boot died on

Read-only reverse-engineering of the iOS 7.1.2 `dyld` (armv7) that the D13 bring-up
loads as the shared-cache `dyld`. Goal: given that the boot HALTED inside `dyld`
(`halt` → `_dyld_fatal_error`) but the message never reached the console, recover
**which** halt site fired and **what string** it was handed.

**Answer (single most likely):** `"throwf, out of memory error"` — thrown by
`dyld::throwf` (file `0x1640`) when `__simple_salloc`'s 4 KiB `vm_allocate(…,
VM_FLAGS_ANYWHERE)` — or the following `strdup` — returned NULL, caught by the
`catch (const char*)` pad in `dyld::_main` at file `0x4be0`, and passed to
`dyld::halt` at `0x2be04be0`. The *real* error (the one dyld was formatting) was
destroyed by the OOM before it could be printed.

All offsets below are **file offsets**; `link_offset == file_offset` because
`__TEXT` has `fileoff 0, vmaddr 0x2be00000`. Slide in this boot = `0x7D000`.

---

## 0. The image, by value

`/tmp/dyld_ios7`, 196576 bytes (`0x2FFE0`). Mach-O header (28-byte `mach_header`,
`0xFEEDFACE`):

```
magic=0xfeedface cputype=0xc(ARM) cpusubtype=0x9(v7) filetype=7(MH_DYLINKER)
ncmds=14 sizeofcmds=0x4a0 flags=0x85   ; commands end at file 0x4bc
```

Load commands (parsed with `struct.unpack_from('<II', off)`, segment cmd = `0x1`,
56-byte `segment_command`):

| seg | vmaddr | vmsize | fileoff | filesize | sections |
|---|---|---|---|---|---|
| `__TEXT` | `0x2be00000` | `0x21000` | `0x0` | `0x21000` | `__text` `0x1000/+0x1b788`, `__cstring` `0x1c788/+0x36ba`, `__gcc_except_tab` `0x1fe44/+0x52c`, `__const` `0x20370/+0x32b` |
| `__DATA` | `0x2be21000` | `0x29000` | `0x21000` | `0x1000` | `__nl_symbol_ptr` `0x21000/+0xbc`, `__const` `0x210c0/+0x88c`, `__data` `0x21950/+0x33c`, `__all_image_info` `0x21c8c/+0xa4`, `__bss` `0x21d30/+0x27831` (fileoff 0), `__common` `0x2be49564/+0x428` (fileoff 0) |
| `__LINKEDIT` | `0x2be4a000` | `0xe000` | `0x22000` | `0xdfe0` | — |

Two facts this pins down:

- `halt`'s 1024-byte buffer is link `0x2be49564` = `__DATA,__common`, **fileoff 0**
  (zero-fill) — it is not in the file image, which is why `0x49564 > 0x2FFE0`.
- The GOT slot at file `0x21000` (`__nl_symbol_ptr`) holds `0x2be218f0`
  (`&__ZTIPKc`); `__cstring` starts at `0x1c788`, so the `"PKc"` type name is
  `0x2be20604` → file `0x20604`.

Symbols: `LC_SYMTAB` `symoff=0x23e10 nsyms=972`, `stroff=0x26c5c`, `nlist32`
`<IBBHI`. Type `0x1e` = `N_SECT|N_EXT` (exported).

---

## 1. Method

1. **Full `__TEXT` Thumb disassembly.** The image is Thumb throughout; every
   `bl`/`blx`/`b.w` target was decoded by value from the 32-bit Thumb-2 encoding,
   never by trusting objdump's `-M force-thumb` split:
   `arm-none-eabi-objdump -D -b binary -m arm -M force-thumb --adjust-vma=0x2be00000 dyld_ios7`
   over `0x0..0x21000`, chunked, with each chunk start verified to land on a
   function boundary (from the symtab). Cross-check: every `bl` target in the
   result resolves to an in-image symbol or a known stub.
2. **`dyld::halt` call sites.** Searched the full disasm for `bl 0x2be01ebc`,
   `b.w 0x2be01ebc`, `blx 0x2be01ebc`. **13 sites, all `bl`; no tail calls.**
3. **`dyld::throwf` call sites.** 145 sites in 32 functions (`/tmp/throwf_table.txt`),
   each with its format string.
4. **String resolution.** For each site, resolved `movw/movt … add rX,pc` or
   `ldr rX,[pc,#N]` pool loads to file offsets, then read NUL-terminated strings.
5. **Register decode** of the crash state against the slide.
6. **Ranking** by whether a site can fire *before* `initializeMainExecutable`.

### The pc-relative resolver (the trap)

`movw rX,#lo; movt rX,#hi; add rX,pc` — **the immediate is not an address**. The
target is:

```
target = (address of the `add rX,pc` instruction) + 4 + imm16
```

Two consequences that cost time here and are worth writing down:

- **2-byte instructions are interleaved** between `movw`/`movt` and the `add`
  (`movs`, `mov`, `str` — e.g. `movs r1,#26`, `str r4,[sp,#100]`). A fixed
  6-byte stride yields targets **2 bytes high** (this is exactly the earlier
  `0x2be1cdc7` vs `0x2be1cdc5` discrepancy). Parse linearly.
- The `pc` in the formula is the **full 32-bit PC of the `add`**, high half from
  `movt`. Using the raw `movw` immediate as an address produces garbage (e.g.
  `0x83d6`). Validated against two anchors: `halt`'s `"dyld: %s\n"` and
  `throwf`'s OOM literal.

`tbb [pc,r0]` at `0x2be04afc` (table at `0x2be04b00`): `target = table_base +
2*byte`; all 28 targets land on instruction boundaries (verified).

---

## 2. `dyld::halt` itself — file `0x1ebc..0x1f06`

```
2be01ebc: push {r4,r5,r7,lr}
2be01ebe: mov  r5, r0                    ; r5 := message (callee-saved, NEVER rewritten)
2be01ec0: movw r0,#0xaec6; movt r0,#1; mov r1,r5; add r0,pc  ; -> 0x2be1cd94 "dyld: %s\n"
2be01ece: bl dyld::log (0x2be017ec)
2be01ed2: movw r4,#0x7680; movt r4,#4; add r4,pc             ; -> 0x2be49564 (1024-byte buf)
2be01ed6: mov r1,r5; mov.w r2,#1024; mov r0,r4
2be01ee4: bl strlcpy (0x2be1824c)
2be01ee8: movw r0,#0xfa68; movt r0,#1; add r0,pc             ; -> 0x2be2195c (global)
2be01ef2: ldrb.w r0,[r0,#0x97]                               ; byte at 0x2be219f3
2be01ef6: eor.w r1, r0, #1                                   ; r1 := !flag
2be01efa: mov r0, r4
2be01efc: bl setAlImageInfosHalt (0x2be08d04)
2be01f00: mov r0, r4
2be01f02: blx _dyld_fatal_error (0x2be010c4)   ; lr := 0x2be01f06 | 1 = 0x2be01f07
2be01f06: nop
```

- `_dyld_fatal_error` = `udf #254`, **one instruction** at file `0x10c4`. Its
  **only in-image caller** is `halt`'s `blx` at `0x2be01f02` (grep of the full
  disasm: 1 hit).
- `r5` is set once at entry and is also `strlcpy`'s `src`, so at the `blx` it
  still holds the message pointer. **This is the register we decode.**
- `halt`'s first act is `log("dyld: %s\n", msg)` → `dyld::vlog` (file `0x16b8`) →
  `__simple_dprintf` (`0x18d60`) → `__simple_vdprintf` (`0x18744`) → `blx
  _fstat64/_write` plumbing. So the missing console text is an **fd-2 / write-path
  fact**, not evidence about which message it was.

---

## 3. The 13 `dyld::halt` call sites (complete)

Every site is `bl 0x2be01ebc`. "EARLY" = can fire before `initializeMainExecutable`
(file `0x1c88`); "LATE" = cannot.

| # | file | enclosing function | message | source | when |
|---|---|---|---|---|---|
| 1 | `0x1e34` | `dyld::runTerminators(void*)+0xb4` | caught message | `catch(const char*)` | LATE (export-only; exit path) |
| 2 | `0x3554` | `dyld::bindLazySymbol(mach_header const*, unsigned long*)+0xcc` | caught message | `catch` | LATE (needs a running process) |
| 3 | `0x365c` | `dyld::fastBindLazySymbol(ImageLoader**, unsigned long)+0xd4` | `"fast lazy binding from unknown image"` `@0x1cdeb` | **literal** | LATE |
| 4 | `0x369a` | `dyld::fastBindLazySymbol+0x112` | caught message | `catch` | LATE |
| 5 | `0x4ad6` | `dyld::_main(...)+0x722` | `"libdyld.dylib support not present for LC_MAIN"` `@0x1ceac` | **literal** | LATE (state 25, after init) |
| 6 | `0x4b60` | `_main+0x7ac` | `mkstringf("could not load inserted library '%s' because %s\n", …)` `@0x1cf15` | computed | EARLY (`DYLD_INSERT_LIBRARIES`) |
| 7 | `0x4b7c` | `_main+0x7c8` | `mkstringf("could not load inserted library '%s'\n", …)` `@0x1ceef` | computed | EARLY |
| 8 | **`0x4be0`** | **`_main+0x82c`** | **caught message** | **`catch(const char*)`** | **EARLY — region spans init** |
| 9 | `0x7aae` | static `unimplemented()+0xe` | `"unimplemented dyld function\n"` `@0x1dc30` | literal | EARLY (export-only, no in-image caller) |
| 10 | `0x90f2` | `_abort+0xe` | `"dyld calling abort()\n"` `@0x1df29` | literal | EARLY (via `___assert_rtn`, `_readEncodedPointer`, `__enlarge`) |
| 11 | `0x9166` | `_exit+0xe` | `"exit()"` `@0x1dfa2` | literal | EARLY (only caller `_malloc`) |
| 12 | `0x9216` | `___stack_chk_fail+0xe` | `"stack buffer overrun"` `@0x1dfb6` | literal | EARLY |
| 13 | `0x9266` | `std::__vector_base_common<true>::__throw_length_error() const+0xe` | `"std::vector<>::_throw_length_error()"` `@0x1dfde` | literal | EARLY |

Sites 9–13 share one thunk shape:

```
push {r7,lr}; movw r0,#imm; mov r7,sp; movt r0,#1; add r0,pc; bl halt; nop
```

**Why #8 is the one.** Four pads end in `halt(caught message)` — #1, #2, #4, #8.
The register state cannot distinguish them; timing does:

- #1 `runTerminators` has **no in-image caller** (export-only) and runs at exit.
- #2/#4 lazy binding requires the process already running.
- **#8 `_main+0x82c` is the `catch (const char*)` whose SjLj region covers state 24,
  set at `0x2be04a84` immediately before `bl initializeMainExecutable` at
  `0x2be04a88`.** It is the only halt site whose try region includes init.

`_main` tail, file `0x4a60..0x4c00`:

```
2be04a76: movs r0,#23; str r0,[sp,#108]
2be04a7c: bl ImageLoader::weakBind (0x2be0a6d4)
2be04a84: movs r0,#24; str r0,[sp,#108]        ; state 24
2be04a88: bl dyld::initializeMainExecutable (0x2be01c88)
          ... state 25; indirect call [glob]->[+0x2c]; cbz r4
          ... test GOT 0x2be21804 (0 in file image)
2be04ad6: bl halt                                ; #5, "libdyld.dylib support not present for LC_MAIN"
2be04afc: tbb [pc,r0]                            ; table at 0x2be04b00
```

`tbb` state → pad map (all 28 entries verified on boundaries):

```
states 0,4,5,6,7,8,15,16,17,22,23,24,25,26 -> 0x2be04b1c -> b.n 0x2be04bb0 (catch pad, #8)
state 3 -> 0x2be04b24 | 9 -> 0x2be04b34 | 10,11 -> 0x2be04b92
12,13 -> 0x2be04b80 | 14 -> 0x2be04c42 | 18,19,20 -> 0x2be04ba8
21 -> 0x2be04baa | 27 -> 0x2be04c44
```

Catch pad `0x2be04bb0`:

```
2be04bb0: movs r4,#2; bl __cxa_begin_catch; str r0,[sp,#20]
2be04bba: cmp r6,r4; beq 0x2be04bd2
2be04bbc: ... log("dyld: launch failed\n" @0x1ceda) ...     ; mismatch path
2be04bd2: bl dyld::syncAllImages (0x2be0184c)
2be04be0: ldr r0,[sp,#20]; bl halt                          ; #8
```

Inserted-library pads:

```
0x2be04b34: bl __cxa_begin_catch; if tag==2 -> 0x2be04b48 bl mkstringf(0x1cf15, path, msg) -> 0x2be04b60 bl halt  ; #6
                                    else     -> 0x2be04b64 bl mkstringf(0x1ceef, path)      -> 0x2be04b7c bl halt  ; #7
```

---

## 4. The register state, decoded

Crash registers: `r0=r4=0x2bec6564`, `r1=1`, `r5=0x2be997fd`,
`lr=0x2be7ef07`. Slide `0x7D000`:

| reg | slid value | link value | what it is |
|---|---|---|---|
| `r5` | `0x2be997fd − 0x7d000` | **`0x2be1c7fd`** | file `0x1c7fd` = **`"throwf, out of memory error"`** |
| `r0=r4` | `0x2bec6564 − 0x7d000` | `0x2be49564` | `halt`'s 1024-byte buffer (`__DATA,__common`) |
| `lr` | `0x2be7ef07 − 0x7d000` | `0x2be01f07` | `0x2be01f06 \| 1` = Thumb return address of `blx _dyld_fatal_error` @`0x2be01f02` |
| `r1` | — | `1` | `eor.w r1,r0,#1` of byte `[0x2be219f3]` = `!flag` arg to `setAlImageInfosHalt`; that byte was `0` |

`lr` proves the fatal path: the **only** `udf #254` in the image is
`_dyld_fatal_error` @`0x10c4`, and its only caller is `halt`'s `blx` @`0x2be01f02`.
So `halt` is where the boot stopped, and `r5` is its message argument.

**Byte-before check** (as required): file `0x1c7fc = 0x00`, i.e. the byte
immediately before the target is a NUL — `r5` points at a string **start**, not
into the middle of one.

**48 bytes at `0x1c7fd`:**

```
74 68 72 6f 77 66 2c 20 6f 75 74 20 6f 66 20 6d 65 6d 6f 72 79 20 65 72 72 6f 72 00 64 79 6c 64 3a 20 77 61 72 6e 69 6e 67 2c 20 00 64 79 6c 64
"throwf, out of memory error\0dyld: warning, \0dyld"
```

**Near-miss excluded:** `"mkstringf, out of memory error"` sits at file `0x1c7de`,
only **0x1f bytes earlier**. The measured target is unambiguously `0x1c7fd`
(offset by exactly `0x7d000` from `r5`), so it is the **throwf** literal, not the
mkstringf one.

---

## 5. Why the message is an OOM, and why it masks the real error

`dyld::throwf` (file `0x1640..0x16b6`), the origin of the string:

```
2be01640: sub sp,#12; push {r4,r5,r7,lr}; add r7,sp,#8; sub sp,#4
2be01648: add.w r4,r7,#8; mov r5,r0; stmia.w r4,{r1,r2,r3}   ; r5=format, r4=&va
2be01652: bl __simple_salloc (0x2be18dac); mov r4,r0
2be01658: cbz r4, 0x2be0167e                                 ; salloc==NULL -> OOM path
2be01664: bl __simple_vsprintf; bl __simple_string
2be0166e: bl strdup (0x2be18144); mov r5,r0; bl __simple_sfree
2be0167a: cmp r5,#0; 2be0167c: bne 0x2be0169c                 ; strdup ok -> throw(r5)
2be0167e: movs r0,#4; bl __cxa_allocate_exception (0x2be14584)
2be01684: movw r1,#0xf968; movt r1,#1; add r1,pc             ; -> 0x2be21000 (GOT: __ZTIPKc)
2be0168c: movw r2,#0xb163; movt r2,#1; add r2,pc             ; -> 0x2be1c7fd "throwf, out of memory error"
2be01698: str r2,[r0,#0]                                     ; exception object {const char*}
2be016ae: ldr r1,[r1,#0]; movs r2,#0; bl __cxa_throw (0x2be148c0)
```

`__simple_salloc` (file `0x18dac`): `mach_task_self()` → `r2=#4096`,
`r3=1` (`VM_FLAGS_ANYWHERE`) → `bl vm_allocate` @`0x18dc8`; then
`mov r1,r0; movs r0,#0; cbnz r1 → return NULL`. **It returns NULL iff the Mach
status != `KERN_SUCCESS`.**

Typeinfo chain — proves the caught type is `char const*`:

```
GOT slot 0x2be21000 (file 0x21000) = 0x2be218f0   ; symbol __ZTIPKc
file 0x218f0: [0]=0x2be21888 (vtable)  [4]=0x2be20604 -> "PKc"
```

So the exception is a `const char*`, and the only consumers that pass it to
`halt` are the four `catch (const char*)` pads.

**Uniqueness.** The literal `"throwf, out of memory error"` is referenced
**exactly once** in the image — inside `throwf`'s allocation-failure path
(`0x2be0168c..0x2be01698`). Its sole purpose is to be thrown when dyld **could
not allocate the buffer to format the real error message**. The real error
(`Library not loaded`, `mmap() error`, `Symbol not found`, a malformed-mach-o,
…) was destroyed before it could be printed.

**Kernel-side signal for the team:** `__cxa_allocate_exception(4)` (malloc)
succeeded, while a fresh 4 KiB Mach `vm_allocate(…, VM_FLAGS_ANYWHERE)` failed.
malloc's arena is fine; **a new VM region was refused.** That is the asymmetry to
chase on the D13 side — a 4 KiB `VM_FLAGS_ANYWHERE` region should not fail.

---

## 6. The 145 `dyld::throwf` sites (`/tmp/throwf_table.txt`)

**TOTAL 145 sites in 32 functions.** By site count, the biggest are the
compressed-LINKEDIT parser and rebase/bind machinery (all "the input image is
malformed" or "the iterator is corrupt"):

| sites | function |
|---|---|
| 22 | `ImageLoaderMachOCompressed::rebase` |
| 18 | `ImageLoaderMachOCompressed::eachBind` |
| 13 | `ImageLoaderMachOCompressed::exportedSymbolAddress` |
| 12 | `ImageLoaderMachOCompressed::incrementCoalIterator` |
| 12 | `ImageLoaderMachOCompressed::updateUsesCoalIterator` |
| 9 | `ImageLoaderMachOCompressed::eachLazyBind` |
| 7 | `ImageLoaderMachO17sniffLoadCommands` (the `malformed mach-o image: …` strings) |
| 7 | `ImageLoaderMachOCompressed::doBindFastLazySymbol` |
| 5 | `dyld::loadPhase6` (pread/short/truncated fat file) |
| 4 | `ImageLoaderMachOCompressed::trieWalk` |
| 4 | `ImageLoaderMachOCompressed::findExportedSymbol` |
| 3 | `dyld::loadPhase3` (`unsafe use of relative rpath/@loader_path/@executable_path`) |
| 3 | `ImageLoaderMachOClassic::rebase` |
| 3 | `ImageLoaderMachOCompressed::resolve` |
| 2 | `ImageLoader::recursiveLoadLibraries` — `"Library not loaded: %s\n  Referenced from: %s\n  Reason: %s"` `@0x1e0df` and `"Incompatible library version: …"` |
| 2 | `ImageLoaderMachO::mapSegments` (`truncated mach-o error`, `mmap() error …` `@0x1ee3a`) |
| 2 | `ImageLoaderMachO::parseLoadCmds` (`cannot load '%s' …`) |
| 2 | `ImageLoaderMachOCompressed::exportedSymbolIsWeakDefintion` |
| 2 | `ImageLoaderMachOCompressed::doBindLazySymbol` |
| 1 each | `isCompatibleMachO` (`incompatible cpu-subtype: 0x%08X in %s` `@0x1cc76`), `dyld::bindLazySymbol` (`image not found for lazy pointer at %p` `@0x1cd9e`), `ImageLoader::ImageLoader` (`too many dependent dylibs in %s` `@0x1e003`), `loadCodeSignature` (`code signature invalid for '%s'` `@0x1e72e`), `throwSymbolNotFound` (`Symbol not found: %s\n…` `@0x1ea98`), `bindLocation` (`bad bind type %d`), `doImageInit`/`doModInitFunctions` (`initializer function %p not in mapped image for %s` `@0x1ebc0`), `doTermination`, `assignSegmentAddresses` (`can't map unslidable segment %s …` `@0x1ed5b`), `ImageLoaderMachOClassic::resolveUndefined`, `ImageLoaderMachOClassic::doBindExternalRelocations`, `ImageLoaderMachOClassic::doBindLazySymbol` (`lazy pointer not found at address %p in image %s` `@0x1f308`) |

**Early vs late for throwf:** almost all 145 are reachable **during**
`initializeMainExecutable` (image loading, symbol binding, rebasing, init
functions), i.e. **EARLY** — they are exactly the errors that would have been
formatted by the throwf call that OOM'd. The `eachLazyBind` / `doBindFastLazySymbol`
/ `bindLazySymbol` groups are runtime-only (LATE). `recursiveLoadLibraries`
(`Library not loaded`) is the single most likely thing dyld was about to format at
the point of failure.

---

## 7. Ranked: the ~5 most likely halt messages for this boot

| rank | message (file offset) | enclosing function | trigger condition |
|---|---|---|---|
| **1** | **`"throwf, out of memory error"`** `@0x1c7fd` | `dyld::throwf` (thrown) → delivered by `_main+0x82c` (`0x4be0`) | `__simple_salloc`'s `vm_allocate(4096, VM_FLAGS_ANYWHERE)` @`0x18dc8` returned non-zero status, **or** the following `strdup`→`malloc` returned NULL, while formatting the *real* error. **This is the measured register state.** |
| 2 | `"Library not loaded: %s\n  Referenced from: %s\n  Reason: %s"` `@0x1e0df` | `ImageLoader::recursiveLoadLibraries+0x66a` | a dependent dylib of launchd could not be found/loaded during init — the most likely *real* error being masked by rank 1 |
| 3 | `"mmap() error %d at address=0x%08lX, size=0x%08lX segment=%s in Segment::map() mapping %s"` `@0x1ee3a` | `ImageLoaderMachO::mapSegments+0x1fe` | a segment `mmap` of an image failed — consistent with the same VM pressure that made rank 1's 4 KiB `vm_allocate` fail |
| 4 | `"Symbol not found: %s\n  Referenced from: %s\n  Expected in: %s\n"` `@0x1ea98` | `ImageLoaderMachO::throwSymbolNotFound+0x2c` | a bind target could not be resolved in the loaded image set |
| 5 | `"code signature invalid for '%s'"` `@0x1e72e` | `ImageLoaderMachO::loadCodeSignature+0x80` | an image's code signature failed validation (runner-up: the seven `"malformed mach-o image: …"` strings from `sniffLoadCommands`, `@0x1e447`+) |

Explicitly excluded: `"mkstringf, out of memory error"` `@0x1c7de` — only `0x1f`
bytes from the measured `r5` target, but the slide arithmetic lands on `0x1c7fd`.

---

## 8. Reproduction

```bash
# full Thumb disasm (chunked; verify chunk starts on symbol boundaries)
arm-none-eabi-objdump -D -b binary -m arm -M force-thumb \
    --adjust-vma=0x2be00000 /tmp/dyld_ios7 > /tmp/dyld_thumb.txt

# every halt call site (13, all `bl`)
grep -n 'bl\s*0x2be01ebc' /tmp/dyld_thumb.txt

# every _dyld_fatal_error reference (1, halt's blx)
grep -n '0x2be010c4' /tmp/dyld_thumb.txt

# strings at the cited offsets
python3 -I -c "d=open('/tmp/dyld_ios7','rb').read(); \
  print(d[0x1c7fc]); \
  print(repr(d[0x1c7fd:d.index(b'\0',0x1c7fd)]))"    # -> 0 ; b'throwf, out of memory error'

# slide decode
python3 -I -c "print(hex(0x2be997fd-0x7d000), hex(0x2be7ef07-0x7d000), hex(0x2bec6564-0x7d000))"
# -> 0x2be1c7fd 0x2be01f07 0x2be49564
```

---

## 9. One-line conclusion

The boot died in `dyld::halt` with `r5 = 0x2be997fd`; de-slid by `0x7D000` that is
link `0x2be1c7fd` = **`"throwf, out of memory error"`**, thrown by `dyld::throwf`
when its 4 KiB `vm_allocate(…, VM_FLAGS_ANYWHERE)` (or the following `strdup`)
returned NULL, caught and re-halted by the `catch (const char*)` landing pad at
`dyld::_main+0x82c` (file `0x4be0`) — the only halt site whose exception region
spans `initializeMainExecutable`. The message is a **secondary** failure: the real
error was lost because dyld could not allocate 4 KiB to format it. The console
silence is a separate fd-2/write-path fact, not evidence about the message.
