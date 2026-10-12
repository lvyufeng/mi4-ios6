# dyld's halt message: what it says, why the log never shows it, and the one offset that recovers it

Scope: iOS 7.1.2 armv7 dyld (`PROJECT:dyld-324.1`, `_dyldVersionString` at `0x2be1df23`)
and the XNU contract it uses on D13. Read-only; no device commands; one file written
(this one). Method: `arm-none-eabi-objdump -D -b binary -m arm -M force-thumb` on
`dd`-extracted windows of `/tmp/dyld_ios7`, `struct.unpack_from` for Mach-O tables and
pointers, by value, offsets cited in link-time addresses. Companion to
`docs/reference/d13-dyld-helper-table.md` (§5-6 of that doc predicted this event from the
file alone; the newest press log now confirms it — see §0).

Link-time geometry used throughout: `__TEXT` vmaddr `0x2be00000`, fileoff 0, so for
`VA < 0x2be21000` **file offset = VA − 0x2be00000**; `__DATA` `0x2be21000` (vm 0x29000,
file size 0x1000 — only the first page is file content, the rest is zero-fill);
`__LINKEDIT` `0x2be4a000`. Runtime slide of the boot below: **0x7D000**
(`0x2be9ec8c−0x2be21c8c = 0x2be7e028−0x2be01028 = 0x2bec6564−0x2be49564 = 0x7D000`).

## 0. Which log this is: the frontier moved off the commpage

| log (mtime) | trap that repeats | count |
|---|---|---|
| `/tmp/cancro-last_kmsg-991.txt` (Oct 12 12:07) | `usermode data abort, EXC_BAD_ACCESS at 0xffff4090 ... (Translation fault on page)` | 233× |
| `/tmp/cancro-last_kmsg.txt` (Oct 12 15:49) | `launchd[1]: usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x2be7e0c4` | 236× |

The 15:49 log carries **992's** marker lines (`IOS7LAB commpage alias 0xffff4000: same
page, user read-only, XN`) *and* **993's** (`IOS7LAB vecl2: … before/after zeroing`), has
**zero** commpage faults, and dies 236× on an **undefined instruction** at
`0x2be7e0c4`. So the commpage wall is behind us and the current wall is dyld's own
deliberate trap. Nothing else in that log is a fatal stop.

`0x2be7e0c4 − 0x7D000 = 0x2be010c4` = `_dyld_fatal_error`
(`fe de ff e7` at file `0x10c4`, i.e. `udf #0xdefe`; `trap.c` compares the swapped word
`0xfedeffe7`). The only caller of `_dyld_fatal_error` is `dyld::halt`.

## 1. `dyld::halt` and `setAlImageInfosHalt`, by value

```
dyld::halt(char const*)                            0x2be01ebc  __ZN4dyld4haltEPKc
2be01ebc push {r4,r5,r7,lr}
2be01ebe mov  r5, r0                     ; r5 = msg   (callee-saved, never reloaded)
2be01eca add  r0, pc  -> 0x2be1cd94      ; "dyld: %s\n"
2be01ece bl   0x2be017ec                 ; dyld::log  -> see §3
2be01ee0 add  r4, pc  -> 0x2be49564      ; r4 = _error_string (__common, 1024 B)
2be01edc mov.w r2, #1024
2be01ee4 bl   0x2be1824c                 ; _strlcpy(_error_string, msg, 0x400)
2be01ef0 add  r0, pc  -> 0x2be2195c      ; &dyld::gLinkContext
2be01ef2 ldrb.w r0, [r0, #0x97]          ; one bool field of LinkContext
2be01ef6 eor.w r1, r0, #1                ; r1 = !bool
2be01ef8 mov  r0, r4                     ; r0 = _error_string
2be01efc bl   0x2be08d04                 ; setAlImageInfosHalt(_error_string, !bool)
2be01f00 mov  r0, r4
2be01f02 blx  0x2be010c4                 ; _dyld_fatal_error   <-- only caller

__Z19setAlImageInfosHaltPKcm  (char const*, unsigned long)   0x2be08d04 (20 bytes)
2be08d04 movw r2, #0x8e0c ; movt r2, #1 ; add r2, pc   ; r2 = &gProcessInfo  0x2be21b1c
2be08d0e ldr  r3, [r2]                                 ; r3 = dyld_all_image_infos
2be08d10 str  r0, [r3, #0x20]   ; dyld_all_image_infos.errorMessage     = msg
2be08d12 ldr  r0, [r2]
2be08d14 str  r1, [r0, #0x24]   ; dyld_all_image_infos.terminationFlags = flags
2be08d16 bx   lr
```

`dyld::gProcessInfo` (`0x2be21b1c`) is initialised in the file to **`0x2be21c8c`**, the
address of the exported `_dyld_all_image_infos` (section `__DATA,__all_image_info`,
size `0xa4`, version `0x0e`). Its file image is zero at `+0x20`/`+0x24` — those two words
are written **only** by the path above, i.e. only at halt.

The fault registers are this code, still live at the `udf`:

| reg | value (this boot) | meaning |
|---|---|---|
| `pc` | `0x2be7e0c4` | `_dyld_fatal_error` (`0x2be010c4` + slide) |
| `r5` | `0x2be997fd` | **the `halt()` argument** (`mov r5,r0`, callee-saved, so it survives the two calls) |
| `r4` | `0x2bec6564` | `_error_string` (`0x2be49564` + slide) = `strlcpy` destination |
| `r0` | `0x2bec6564` | `mov r0,r4` before the trap — the message pointer |
| `r1` | `1` | the flags argument = `!gLinkContext[0x97]` (the bool was 0) |
| `r2` | `0x2be9eb1c` | `&gProcessInfo` (`0x2be21b1c` + slide) |
| `r3` | `0x2be9ec8c` | `dyld_all_image_infos` — **the exact address `trap.c` prints** |
| `r6` | `2` | C++ EH selector; only `dyld::_main`'s top-level catch compares its selector with 2 (see §7) |

## 2. The message itself: `"throwf, out of memory error"`

`r5 − slide = 0x2be997fd − 0x7D000 = 0x2be1c7fd`, and file offset `0x1c7fd` reads
`"throwf, out of memory error"`. It is a static `__cstring` emitted by exactly one place:

```
dyld::throwf(char const*, ...)                      0x2be01640  __ZN4dyld6throwfEPKcz
2be01652 bl   0x2be18dac        ; __simple_salloc()
2be01656 mov  r4, r0
2be01658 cbz  r4, 0x2be0167e    ; (a) salloc returned NULL
2be01664 bl   __simple_vsprintf(r4, fmt, va)
2be0166a bl   __simple_string(r4)
2be0166e bl   0x2be18144        ; _strdup()
2be01672 mov  r5, r0
2be0167a cmp  r5, #0
2be0167c bne  0x2be0169c        ; throw the strdup'ed string
2be0167e movs r0, #4 ; bl ___cxa_allocate_exception(4)
2be01696 add  r2, pc -> 0x2be1c7fd
2be01698 str  r2, [r0]          ; exception payload = "throwf, out of memory error"
```

`__simple_salloc()` (`0x2be18dac`) is not a bookkeeping call — it is a **memory
allocation**:

```
2be18db2 add r0, pc -> &_mach_task_self_;  r0 = [r0]     ; mach_task_self_
2be18dbc mov.w r2, #4096 ; movs r3, #1                   ; 4096, VM_FLAGS_ANYWHERE
2be18dc8 bl  0x2be1bb70                                  ; _vm_allocate(task,&addr,4096,1)
2be18dcc mov  r1, r0 ; movs r0, #0
2be18dd0 cbnz r1, 0x2be18df6                             ; kern_return != 0 -> return NULL
```

so `throwf` throws that string when **either** (a) `vm_allocate` of its 4 KB string-pool
page failed, **or** (b) `strdup` failed. Both are memory-service failures; (a) is a raw
`vm_allocate` Mach trap, (b) is dyld's own arena (`initialPoolContent`, `__bss` at
`0x2be231b0`) which refills from the same kernel services. Neither is a symbol/loader
problem — **the halt is dyld reporting that it could not get memory**, and that is all
the string can ever say. (Distinguishing (a) from (b) from the message alone is not
possible; it is decidable at runtime, see §6.)

`mach_task_self_` itself is set once, on every dyld start, by
`_mach_init` (`0x2be1b26c`, single caller `dyldbootstrap::start+0xf0` at `0x2be011c0`):
`_mach_init_doit` (`0x2be1b28c`) does `_mach_task_self_ = task_self_trap()` (via
`0x2be1b530`) and `__task_reply_port = mach_reply_port()`. So a bad task port would mean
a broken `task_self_trap`; a good port plus a non-zero `vm_allocate` return means the VM
path for pid 1.

## 3. Q1 — the actual sink of `dyld::log` (there is no `gLinkContext.log`)

`dyld::log(char const*, ...)` `0x2be017ec` = va_start + tail `bl dyld::vlog`
(`0x2be016b8`, the **only** caller of vlog; 114 callers of log). vlog picks one of three
sinks, in this order:

1. **libSystem's helper struct** — `2016c4 add r0,pc -> &gLibSystemHelpers` (`0x2be21a04`);
   `ldr r0,[r0]`; if `r0 == 0` → sink 2; `ldr r1,[r0,#0]` (version); `cmp r1,#11; bcc` →
   sink 2; `ldr r0,[r0,#0x48]; blx r0`; non-zero return → done.
2. **stderr** — `fstat64(2, &sb)` (`0x2be1c2fc`); if the return is `-1` → sink 3;
   otherwise `__simple_vdprintf(2, fmt, va)` (`0x2be18744`) and return.
3. **syslog over a UNIX datagram socket, created lazily** —
   `socket(AF_UNIX=1, SOCK_DGRAM=2, 0)` (`0x2be1c460`), `fcntl(fd, F_SETFD=2, FD_CLOEXEC=1)`
   (`0x2be1b904`), `sockaddr_un` at `sp+0x74` with `sa_family=1` at `sp+0x75`,
   `strncpy(sun_path, "/var/run/syslog" @0x2be1d882, 104)` (`0x2be18290`),
   `connect(fd,&sa,0x6a)` (`0x2be1bc98`); on `-1` → `close()` and give up; otherwise
   `__simple_salloc()`, `__simple_sprintf(s, "<%d>%s[%d]: " @0x2be1d892, pri, name, getpid())`
   (`0x2be18ed4`, `_getpid` = `0x2be1bd84`), `__simple_vsprintf(s, fmt, va)`,
   `__simple_string(s)`, `strlen`, `sendto(fd, buf, len, 0, NULL, 0)` (`0x2be1c1a4`),
   `__simple_sfree(s)`.

There is no direct `write(2)`, no `__dprintf`, no `gLinkContext.log` member. Sink 3 is
reached *through* `__simple_salloc` as well, so sink 3 inherits the same allocation
failure as §2: `201774 bl __simple_salloc; 20177a cbz r6, 2017d6` returns **silently**.

## 4. Q2 — the LibSystemHelpers contract: registered by libSystem, not by the kernel

- Type: `dyld::LibSystemHelpers` (mangled in the symbol table as
  `__ZL21registerThreadHelpersPKN4dyld16LibSystemHelpersE`).
- Registration entry point: the dyld export table `dyld_funcs` at `0x2be210c0`
  ({name, fn} pairs) maps **`__dyld_register_thread_helpers` → `0x2be088c9`**
  (= `registerThreadHelpers` `0x2be088c8 | 1`). Other entries: `__dyld_fast_stub_entry` →
  `fastBindLazySymbol`, `__dyld_get_all_image_infos`, `_dyld_register_image_state_change_handler`,
  `__dyld_moninit`, `__dyld_dlopen/dlclose/dladdr`, `__dyld_process_is_restricted`, …
- `registerThreadHelpers(const dyld::LibSystemHelpers* h)` `0x2be088c8`:
  `gLibSystemHelpers = r0` (`0x2be21a04`), `gProcessInfo->libSystemInitialized (+0x11) = 1`,
  and `if (h->version >= 5) h->[0x2c](&local, 0)`; then `bl 0x2be08a24`.
  Its only caller is libSystem (through the export above) — it has **no** in-image caller.
- Fields dyld consumes: `version` at `+0`, `+0x2c` (gated `>= 5`), `+0x48` (gated `>= 11`,
  the one vlog uses). `gLibSystemHelpers` lives in zero-fill `__DATA`, so it reads **NULL**
  until libSystem's initializer runs.
- Kernel side: `grep -rn "LibSystemHelpers|setLibSystemHelpers|libsystem_helpers"` over
  `external/` (all eight kernel trees) and `src/` returns nothing. The entire kernel-facing
  dyld surface is scalars: `osfmk/kern/task.c` `task_set_dyld_info` (two words),
  `bsd/kern/mach_loader.c:249-274` `note_all_image_info_section`, `bsd/kern/kern_exec.c:4126-4245`
  `exec_prefault_data` (copyin of the struct + `dyld_slide_amount`). There is no struct for
  the kernel to fill — see the companion doc for the full table.

## 5. Q3 — why the log produced nothing: all three sinks are dead

On this boot the halt happens **before libSystem's initializer**, so `gLibSystemHelpers == NULL`
and sink 1 is skipped (not a silent NULL-deref — the code checks, `2016e6 cbz`). Sink 2
needs a valid fd 2: the log shows no `dyld:` text at all, although `halt` itself calls
`log("dyld: %s\n", msg)` on the way in — so either pid 1 has no fd 2 (`fstat64` → `-1` →
fall through) or the write does not reach the ram console. Sink 3 then needs *both* a
listening syslogd (none in a bring-up boot) *and* `__simple_salloc`, which is the very
thing that failed. Hence: **nothing is written anywhere, and no amount of log-watching
will ever show a dyld message on this boot**. The message survives only in memory —
which is exactly what `setAlImageInfosHalt` writes.

## 6. Q4 — the payoff: `dyld_all_image_infos + 0x20`

`setAlImageInfosHalt` stores, in the struct the kernel already reads:

- **`+0x20` `errorMessage`** — `const char *` = pointer to the copied message
  (`_error_string`, `0x2be49564` + slide = `0x2bec6564` this boot);
- **`+0x24` `terminationFlags`** — `1` on this boot (`!gLinkContext[0x97]`).

Cheapest read on the next boot, in this order of effort:

1. **No new idea needed** — the tree already contains the reader.
   `osfmk/arm/trap.c:78-126` `ios7leo_dyld_matches`/`ios7leo_dyld_witness` matches this
   exact trap (`instruction == 0xfedeffe7`, pid 1, `proc_name == "launchd"`,
   `all_image_info_size == 0xa4`, `pc == A − 0x20bc8`, `r0 == A + 0x278d8`) and does
   `copyinstr(state->r[0], message, 512)` printing `dyld> …` lines, sanitised and bounded.
   It is compiled out on D13: the whole block is `#if defined(BOARD_CONFIG_QSD8250_LEO)`
   (HD2/Leo), and D13 is `BOARD_CONFIG_MSM8974_CANCRO`. Two other reasons it cannot fire
   as written: it needs `leo_instruction_error == 0` (the `copyin` of the opcode), and the
   declaration/definitions are inside the same `#if`. Its two magic constants are this
   dyld's layout, independently confirmed: `A − 0x20bc8 = 0x2be7e0c4` (the fault pc) and
   `A + 0x278d8 = 0x2bec6564` (the fault r0) for `A = 0x2be9ec8c`.
2. **By the field, not the register** (slide-free, and the documented ABI): with
   `A = task->all_image_info_addr` (already printed by `trap.c:919-920` and already in
   `src`'s witness record as `all_image_info_addr`), read `u = [A + 0x20]` (a 4-byte
   `copyin`), then `copyinstr(u, buf, 64)`. This works even when `r0` has been clobbered
   and does not depend on which `halt` site fired.
3. `osfmk/arm/ios7lab_fault_witness.c` already does `copyin(task->all_image_info_addr, …)`
   with a bounds check for the 24-byte prefix (`IOS7LAB_FAULT_WITNESS_DYLD_PREFIX_BYTES`);
   extending that prefix to `0x28` and adding one string `copyin` is the smallest diff, and
   it reuses the proven user-read path.

Note the field and the register hold the same pointer (`r0` is loaded from
`_error_string`, which is the pointer stored in the field), so either route yields the
same bytes.

## 7. Q5 — all 13 callers of `dyld::halt`, and what each passes

| site | enclosing function | argument |
|---|---|---|
| `0x2be01e34` | `dyld::runTerminators` catch | caught message (`bl ___cxa_begin_catch` `0x2be14950`, selector `== 1`) |
| `0x2be03554` | `dyld::bindLazySymbol` catch | caught message; logs `"dyld: lazy symbol binding failed: %s\n"` `0x2be1cdc5`; throws `"image not found for lazy pointer at %p"` `0x2be1cd9e` (throwf at `0x2be03524`) |
| `0x2be0365c` | `dyld::fastBindLazySymbol` | `"fast lazy binding from unknown image"` `0x2be1cdeb` (static) |
| `0x2be0369a` | `dyld::fastBindLazySymbol` catch | caught message; same `"lazy symbol binding failed"` log |
| `0x2be04ad6` | `dyld::_main` | `"libdyld.dylib support not present for LC_MAIN"` `0x2be1ceac` |
| `0x2be04b60` | `dyld::_main` | mkstringf `"could not load inserted library '%s' because %s\n"` `0x2be1cf15` |
| `0x2be04b7c` | `dyld::_main` | mkstringf `"could not load inserted library '%s'\n"` `0x2be1ceef` |
| `0x2be04be0` | `dyld::_main` top-level catch | caught message; selector compared with **2**; preceded by `log("dyld: launch failed\n" @0x2be1ceda)` at `0x2be04bcc` and `syncAllImages()` `0x2be0184c` |
| `0x2be07aae` | `unimplemented()` | `"unimplemented dyld function\n"` `0x2be1dc30` |
| `0x2be090f2` | `abort()` | `"dyld calling abort()\n"` `0x2be1df29` |
| `0x2be09166` | `exit()` | `"exit()"` `0x2be1dfa2` |
| `0x2be09216` | `___stack_chk_fail` | `"stack buffer overrun"` `0x2be1dfb6` |
| `0x2be09266` | `std::__vector_base_common<true>::__throw_length_error` | `"std::vector<>::_throw_length_error()"` `0x2be1dfde` |

Four sites hand on a *caught* message; the other nine pass a static/mkstringf'd string.
The `r6 == 2` in the dump fits `_main`'s top-level catch (the three lazy/terminator
catches compare their selector with 1 and keep it in `r0`, not `r6`), so the likely
chain is: some `throwf` inside `_main`'s `try` threw the OOM string → `_main`'s catch
logged `dyld: launch failed` → `halt(msg)` → `setAlImageInfosHalt(_error_string, 1)` →
`udf`. Not proof (r6 could be leftover), but the only site whose ordering matches.

## 8. What this does and does not establish

**Established by value:** the trap is `dyld::halt`→`_dyld_fatal_error`; the argument (r5)
is the file's static string `"throwf, out of memory error"`; the message is copied to
`_error_string` (`0x2be49564`+slide) and stored at `dyld_all_image_infos + 0x20` with
`+0x24 = 1`; every log sink is dead on this boot; the tree already contains a working
reader for this trap behind a board gate the D13 build does not define.

**Not established:** which `throwf` call site fired (all produce the same string once
allocation fails), and whether the failing allocation was the 4 KB `vm_allocate` of
`__simple_salloc` or the `strdup` behind it. Both reduce to the same question — *can
pid 1 get memory from the kernel?* — and that question, not the commpage and not the
shared cache, is now the head of the frontier.

Practical consequence for the next press: print `[A + 0x20]` (or `r0`) at the trap. Until
the allocation failure is fixed, **every** dyld diagnostic on this device will read
`"throwf, out of memory error"` — the message that would name the real fault is
unformattable by the same failure. The useful follow-ups are therefore (1) that read, to
confirm the string and the `halt` site, and (2) a decision about the allocation failure
itself (`task_self_trap` return value, and the `vm_allocate` path for pid 1).