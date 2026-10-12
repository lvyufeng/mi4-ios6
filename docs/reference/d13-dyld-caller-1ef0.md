# d13-dyld-caller-1ef0 — what dyld was doing when it executed `udf #65006`

READ-ONLY analysis of iOS 7.1.2 `dyld` (armv7, dyld-324.1) at `/tmp/dyld_ios7`
(196576 B, MH_DYLINKER, `__TEXT` vmaddr `0x2be00000`, slide `0x7D000`; for
`__TEXT` file offset == link offset, so `link = file + 0x2be00000` and
`runtime = link + 0x7D000`).

Crash: `EXC_BAD_INSTRUCTION`, `pc = 0x2be7e0c4`, 236 identical repeats.
Every register in the report is reproduced by the analysis below.

## Verdict

dyld was executing its **own designed fatal-error path**, not a random fault:

```
dyld::halt("throwf, out of memory error")
  -> dyld::log("dyld: %s\n", msg)
  -> strlcpy(_error_string, msg, 1024)
  -> setAlImageInfosHalt(_error_string, gLinkContext.dyldFatalErrorFlag ^ 1)
  -> blx _dyld_fatal_error            // ARM  udf #65006   <-- the crash
```

The message string is `dyld::throwf`'s out-of-memory literal, so the immediate
cause is **an out-of-memory while `dyld::throwf` was formatting an exception
message**, reached from a `catch (const char*)` landing pad.

## 1. The enclosing function of the crash address

`pc = 0x2be7e0c4` -> link `0x2be010c4` -> symbol **`_dyld_fatal_error`**
(`LC_SYMTAB`, nsyms 972). Its bytes (file `0x10c0..0x10d0`):

```
000010c0: fede ffe7  fede ffe7  00f0 20e3  1eff 2fe1
```

Both `0x2be010c0` (`dyld_stub_binding_helper`) and `0x2be010c4`
(`_dyld_fatal_error`) are the ARM word `0xE7FFDEFE` = **`udf #65006`**.
`cpsr = 0x80000010` has the T bit (bit 5) clear, i.e. the core really is in
ARM state, so `udf #65006` is the correct decode.

The single caller of `_dyld_fatal_error` in the whole binary is
`dyld::halt` at `2be01f02` (`blx 0x2be010c4`); `0x2be01f06` is the `nop`
after it, so `lr = 0x2be7ef07` (`0x2be01f06 + 1`, Thumb) is exactly the
return address `blx` wrote. **`lr` and `pc` are consistent with `halt`'s
tail call and nothing else.**

Enclosing function of the *lead's* address `0x2be01ef0`: **`dyld::halt`**
(`__ZN4dyld4haltEPKc`), link `0x2be01ebc`, size `0x4C`, ends `0x2be01f08`.
`0x2be01ef0` is the `add r0, pc` that materializes `gLinkContext`.

## 2. The callee at `0x2be08d04` (runtime `0x2be8fd04`)

`__Z19setAlImageInfosHaltPKcm` = `setAlImageInfosHalt(char const*, unsigned long)`.
Full body (18 bytes, no branches at all):

```
2be08d04: movw r2,#0x8e0c ; movt r2,#1 ; add r2,pc   -> 0x2be21b1c (gProcessInfo)
2be08d0e: ldr  r3,[r2,#0]                            -> 0x2be21c8c (dyld_all_image_infos)
2be08d10: str  r0,[r3,#32]                           -> all_image_infos + 0x20  (errorMessage)
2be08d12: ldr  r0,[r2,#0]
2be08d14: str  r1,[r0,#36]                           -> all_image_infos + 0x24  (terminationFlags)
2be08d16: bx   lr
```

- **No conditional branch is hidden before the `blx`** — and none is possible
  inside this callee either: `setAlImageInfosHalt` is straight-line code.
  In `halt`, the only conditional instruction is the `bl 0x2be08d04` itself
  (unconditional); the flow reaches `blx 0x2be010c4` unconditionally.
- **It returns no decision value** (`void`). It is a pure store of the two
  arguments into `dyld_all_image_infos`.
- Crash-register check: `r2 = 0x2be9eb1c` = `gProcessInfo + 0x7d000` ✓,
  `r3 = 0x2be9ec8c` = `dyld_all_image_infos + 0x7d000` ✓. `r0`/`r1` at the
  `blx` are the arguments `halt` set up (see §3).

## 3. The `ldrb.w r0,[r0,#0x97]` — and the lead's arithmetic

The lead computed link `0x2be01f8d` / file `0x1f8d`. **That is wrong.** The
real instruction is at link **`0x2be01ef2`** (file `0x1ef2`):

```
2be01ee8: movw r0,#0xfa68 ; movt r0,#1 ; add r0,pc   -> 0x2be2195c
2be01ef2: f890 0097   ldrb.w r0,[r0,#151]           ; 0x97
2be01ef6: f080 0101   eor.w  r1,r0,#1
```

- The byte is `dyld::gLinkContext` (`__ZN4dyld12gLinkContextE`, link
  `0x2be2195c`) **+ 0x97 = link `0x2be219f3`, file `0x219f3`** — inside
  `__DATA.__data`, *not* file `0x1f8d`.
- Its file value is `0x00`, so `r1 = 0 ^ 1 = 1` — which is the crash report's
  `r1 = 1`. ✓
- **What it is:** a `gLinkContext` boolean field (a struct member of the
  packed `LinkContext` boolean block). It is written in
  exactly **one** place in the whole binary — `dyld::initializeMainExecutable`
  (`2be01cbe`: `movs r1,#1; strb.w r1,[r0,#151]`) — and read in exactly
  **one** place — here, `dyld::halt`. That is the classic
  "only print the all_image_infos error to a debugger/console while still
  initializing" flag; the name is not in the symbol table (a bool member),
  so this doc reports the field by address and access sites rather than
  over-claiming the C++ identifier.
- **`^1` means:** the flag's polarity is inverted on the way into
  `dyld_all_image_infos.terminationFlags`. `terminationFlags` bit 0 is
  `dyld_termination_flag_unload_signals`-style "this is a dyld failure"
  encoding; `halt` stores `!dyldFatalErrorFlag`. **Observation worth keeping:**
  `initializeMainExecutable` sets this byte to 1, and the crash-time value
  is 0. Either the halt happened before that store executed, or (more likely
  on this hardware) the store never reached memory — consistent with a
  `__DATA` page that was not writable. Reported as an observation, not a
  conclusion.

## 4. What `r4 = 0x2bec6564` is

`r4`/`r0` = runtime `0x2bec6564` -> link `0x2be49564` -> symbol
**`_error_string`**, link `0x2be49564`, size `0x428`, in
`__DATA.__common` (link range `0x2be49564..0x2be4998c`).

It is dyld's **own static 1024-byte halt-message buffer**, not a heap
allocation and not an input string:

```
2be01ed2: movw r4,#0x7680 ; movt r4,#4 ; add r4,pc  -> 0x2be49564
2be01ee2: mov r0,r4 ; mov r1,r5 ; mov.w r2,#1024
2be01ee4: bl  0x2be1824c                              ; strlcpy(_error_string, msg, 1024)
```

`__common` is `fileoff 0` (BSS, zero-fill), so there is **no file content to
dump** — a 64-byte dump of the file at `0x49564` is empty by construction.
`__DATA.__bss` is `0x2be21d30..0x2be49561` and `__common` follows at
`0x2be49564`; both are zero-filled at load. At crash time the buffer held the
message (already copied) but its contents are not in the report.

## 5. Verdict, and the failing condition

**Function:** `dyld::halt(char const*)` (`__ZN4dyld4haltEPKc`).
**Failing condition:** it was handed the literal
`"throwf, out of memory error"`.

Evidence, by value:

- Crash `r5 = 0x2be997fd` -> link `0x2be1c7fd` = file `0x1c7fd` =
  **`"throwf, out of memory error"`** (odd address; `__cstring` packs it
  immediately after `"mkstringf, out of memory error"` at `0x1c7de`). `r5`
  is `halt`'s saved message (`2be01ebe: mov r5,r0`) and the `strlcpy` source.
- That literal is materialized in exactly one function: `dyld::throwf`
  (`__ZN4dyld6throwfEPKcz`, link `0x2be01640`) at `2be01696`:
  `movw r2,#0xb163 ; movt r2,#1 ; add r2,pc` -> link `0x2be1c7fd`
  (the Thumb `add rX,pc` uses the *unaligned* PC for this encoding; the
  aligned value `0x2be1c7fb` is the `r` of `error`, a trap my own simulator
  initially fell into — the unaligned PC is correct).
- `throwf` reaches that literal on the path
  `__simple_salloc` -> `__simple_vsprintf` -> `__simple_string` -> `_strdup`
  (which mallocs via `dyld::malloc` `0x2be0dfc0`), and when `_strdup`
  returns NULL it calls `__cxa_allocate_exception(4)` and stores the literal
  into the exception object. i.e. **the out-of-memory is the malloc failure
  inside `throwf`'s own message formatting** — dyld could not allocate ~4
  bytes while trying to report an error.
- The `halt` call that crashed came from `dyld::runTerminators`
  (`__ZN4dyld14runTerminatorsEPv`, link `0x2be01d80`), whose
  `catch (const char* msg)` landing pad at `2be01e34` is the only `halt`
  call site that passes a *caught exception* rather than a static string.
  The other 12 `halt` callers (`bindLazySymbol`, `fastBindLazySymbol` ×2,
  `_main` ×4, `unimplemented`, `abort`, `exit`, `__stack_chk_fail`,
  `std::__vector_base_common::__throw_length_error`) all pass fixed
  `__cstring` literals. (For completeness: `bindLazySymbol`'s
  "image not found for lazy pointer at %p" path *also* funnels through
  `throwf` -> `throw` -> landing pad -> `halt`, but then `r5` would be the
  real message, not the OOM literal.)
- The call chain the machine would have taken:
  `dyld::_main` -> `dyld::initializeMainExecutable` (`2be04a88`) ->
  `runInitializers` -> `runTerminators` -> `__cxa_throw` ->
  SjLj unwind -> landing pad `2be01e34` -> `halt`. This is the tail of
  process teardown, matching a launchd exec that got all the way through
  initializers.

## Register map (all reproduced)

| reg | value | meaning |
|-----|-------|---------|
| pc | `0x2be7e0c4` | `_dyld_fatal_error`, ARM `udf #65006` |
| lr | `0x2be7ef07` | `halt` `0x2be01f06 + 1` (return from `blx`) |
| r0/r4 | `0x2bec6564` | `_error_string` (halt message buffer, `__DATA.__common`) |
| r1 | `1` | `gLinkContext+0x97 (=0) ^ 1` -> `terminationFlags` |
| r2 | `0x2be9eb1c` | `gProcessInfo` (slot holding `dyld_all_image_infos`) |
| r3 | `0x2be9ec8c` | `dyld_all_image_infos` |
| r5 | `0x2be997fd` | `"throwf, out of memory error"` |
| cpsr | `0x80000010` | T bit clear -> ARM state -> `udf` decodes |

## Loose ends (stated, not resolved)

1. The `gLinkContext+0x97` byte reads 0 at crash while
   `initializeMainExecutable` sets it to 1. Not proven whether the store
   executed.
2. The exact `LinkContext` member name at `+0x97` is not in the symbol table.
3. `runTerminators`' `0x2be01e9c: b.n 0x2be02004` targets the *middle* of
   `processDyldEnvironmentVariable` (`0x2be01f08 + 0xfc`), i.e. a shared
   landing-pad tail block, not a named caller. The caller edge into
   `runTerminators` is therefore inferred from the `_main` ->
   `initializeMainExecutable` -> `runInitializers` -> terminator path, not
   read as a direct `bl`.
