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

## 9. 994 confirmations — the three arithmetic checks, recomputed by value

(§9 also **supersedes §8's closing paragraph** and §2's reading of the message: on the 994
press the allocation failure is fixed, the formatted message is a *real* dyld error, and it
lives in dyld's own malloc pool. Nothing in §1/§6 is invalidated — they are the same code
and the same field.)

Input: `out/stage90/captures/cancro-last_kmsg-994-armed-d13-8b4c9a3e.txt` (the path the task
named, `/tmp/cancro-last_kmsg-994.txt`, does not exist; this is the archived capture, 22748
lines). It is a **third** log: §0's table is the 991/993 pair. Slide this boot = **0xB8000**
(`0x2bed9c8c − 0x2be21c8c` = `0x2beb90c4 − 0x2be010c4` = `0x2bedc390 − 0x2be24390` =
`0x2bedc428 − 0x2be24428` = `0x2beb9f07 − 0x2be01f07`), so every register the dump prints
reduces to a link-time address — **the task's numbers are the link-time ones**:

| reg | dump (`:4103-4109`) | − slide | link | meaning (same as §1) |
|---|---|---|---|---|
| `pc` | `0x2beb90c4` | `0xB8000` | `0x2be010c4` | `_dyld_fatal_error` (`udf #0xdefe`) |
| `lr` | `0x2beb9f07` | `0xB8000` | `0x2be01f07` | inside `dyld::halt`, the `blx` at `0x2be01f02` |
| `r5` | `0x2bedc390` | `0xB8000` | `0x2be24390` | **the `halt()` argument** = `initialPoolContent+0x11E0` |
| `r4`/`r0` | `0x2bf01564` | `0xB8000` | `0x2be49564` | `_error_string` (the `strlcpy` destination) |
| `r1` | `1` | — | — | `!gLinkContext[0x97]`, the stored flags |
| `r2` | `0x2bed9b1c` | `0xB8000` | `0x2be21b1c` | `&gProcessInfo` (left over from `setAlImageInfosHalt`) |
| `r3` | `0x2bed9c8c` | `0xB8000` | `0x2be21c8c` | `dyld_all_image_infos` — the address the kernel prints |
| `r6` | `2` | — | — | the catch pad's selector (§9.2, now by value) |
| `r8` | `0x2bedc428` | `0xB8000` | `0x2be24428` | `initialPoolContent+0x1278` — see §9.3 |

235 identical traps and **exactly two `IOS7LAB vm_*64` lines in the whole log** —
`vm_allocate64 size=00000000:00001000 flags=1 result=0 address=000e1000` (`:4101`) and its
`vm_deallocate64` twin (`:4102`). That pair is `__simple_salloc`/`__simple_sfree` **inside
`throwf`** (§2): one 4 KB page, allocated and released *before* the first trap. So in this
run exactly one dyld message was ever *formatted*, the formatting **succeeded**, and the
`strdup`'d copy is the one that survives — there is no third copy, the salloc page is gone.

### 9.1 (a) `r5 = initialPoolContent+0x11E0`, `r8 = +0x1278`, and the pool is `0x25800` bytes

`r5` and `r8` are both inside `__ZL18initialPoolContent` (the local symbol `0x2be231b0`, in
zero-fill `__DATA,__bss`), and the pool's **total size is `0x25800` = 153,600 B** — proven
three independent ways, all by value:

1. **Symbol-to-symbol**: the next symbol above it, `__ZZN23ImageLoaderMachOClassic26bindIndirectSymbolPointersE…13alreadyWarned`,
   is at `0x2be489b0` = pool `+0x25800`. (`nlist` entries carry no size, so the distance to
   the next symbol *is* the layout bound.) `__bss` itself is `0x2be21d30`+`0x27831` ending at
   `0x2be49561`, so the pool is the last `0x25800` bytes of it — the symbol bound and the
   section bound agree.
2. **The allocator's own constant**: `_malloc` (`0x2be0dfc0`) refills by
   `0x2be0e010 mov.w r2,#153600` and then writes a header `{next, available = p+12,
   end = p+0x25800}` (`0x2be0e022-0x2be0e030`); `_free`'s "is this ours" test is
   `0x2be0e09c add r2, pc → 0x2be231b0` … `0x2be0e09e add.w r2, r2, #153600` …
   `0x2be0e0a2 cmp r0, r2`. (This is the same 153600 the earlier trace of the first
   `vm_allocate` on this path reported — it is the *pool refill*, not a dyld message buffer.)
3. **The initialiser in file-backed `__data`**: `[0x2be21b30] = 0x2be21b34` (the
   `sCurrentPool` merged-global holds the header address), and the header words read
   `{next = 0, available = 0x2be231b0, end = 0x2be489b0}` — i.e. the first allocation starts
   at the pool's first byte and the end is exactly pool `+0x25800`.

⇒ `+0x11E0` (4576) and `+0x1278` (4728) are far inside (the pool is 153,600 B): the task's
parse is correct and **+0x1278 does not fall outside. No counterexample found.**

### 9.2 (b) `aii+0x20`/`+0x24`: confirmed, and the ordering that makes the 995 read safe

`setAlImageInfosHalt` `0x2be08d04` reads, in full (20 bytes):

```
2be08d04 movw r2,#0x8e0c ; movt r2,#1 ; add r2,pc   ; r2 = &dyld::gProcessInfo 0x2be21b1c
2be08d0e ldr  r3,[r2]                                ; r3 = dyld_all_image_infos
2be08d10 str  r0,[r3,#0x20]                          ; aii.errorMessage      = r0
2be08d12 ldr  r0,[r2]
2be08d14 str  r1,[r0,#0x24]                          ; aii.terminationFlags  = r1
2be08d16 bx   lr
```

Confirmed: `r0 → +0x20`, `r1 → +0x24`. The dump's `r2`/`r3` are literally this function's
exit state (its two loads), which is why they read `&gProcessInfo` / the aii at the `udf`.

Field name, against the tree: `osfmk/kdp/kdp_dyld.h`'s `user32_dyld_all_image_infos` lists
`version, infoArrayCount, infoArray, notification, processDetachedFromSharedRegion,
libSystemInitialized, dyldImageLoadAddress, jitInfo, dyldVersion, **errorMessage**,
**terminationFlags**, …`. With the two `bool`s at `+0x10`/`+0x11` and 4-byte alignment, that
is `errorMessage = +0x20`, `terminationFlags = +0x24`, and three neighbours are pinned
independently in this doc: `+0x0c notification = 0x2be010cc`, `+0x14
dyldImageLoadAddress = 0x2be00000`, `+0x1c dyldVersion = 0x2be1df23` ("324.1"),
`+0x38 dyldAllImageInfosAddress = 0x2be21c8c`; the kernel's printed size `0xa4` matches v14.

**The ordering (this is the load-bearing part for 995)**: `dyld::halt` runs
`log("dyld: %s\n", msg)` **first**, then `strlcpy(_error_string, msg, 0x400)`, then
`setAlImageInfosHalt(_error_string, …)`, and only then `blx _dyld_fatal_error`. So the field
is always written *before* the trap that wakes the witness — the log path's failure cannot
prevent it — and `r5` (halt's argument) and `r0`/`r4` (`_error_string`) hold **the same text
but different pointers**: `r5` is the `strdup`'d pool string, `r0` is the 1024-byte copy the
field points at. `copyinstr(A+0x20)` therefore recovers the identical bytes, **truncated to
1023 chars + NUL** (`_error_string`'s extent is the next `__common` symbol,
`dyld::gSharedCacheOverridden` at `+0x400`); at *this* trap `r5` is also live and untruncated,
so either read works — `A+0x20` is the register-free, ABI one.

Bonus, by value: the catch is now *proven* to be `dyld::_main`'s top-level catch (retiring
§7's "not proof" hedge, which rested on `r6 == 2` alone):

```
2be04baa ldr r0,[sp,#112]      ; the exception object
2be04bb2 bl  ___cxa_begin_catch ; r0 = the thrown payload
2be04bb6 str r0,[sp,#20]        ; [sp+20] = the char* message
2be04bb0 movs r4,#2  /  2be04bb8 cmp r6,r4   ; r6 (=[sp+116]) compared with 2  <-- r6 = 2
2be04bd2 bl  0x2be0184c         ; syncAllImages()
2be04bde ldr r0,[sp,#20]        ; r0 = the message  (= r5 at the trap)
2be04be0 bl  0x2be01ebc         ; dyld::halt(msg)
```

### 9.3 (c) Is `r8` the message's arena neighbour? What the allocator says

The allocator, by value (`_malloc` `0x2be0dfc0`, `_free` `0x2be0e074`, `_strdup` `0x2be18144`):

```
_malloc(size):  size = (size+3) & ~3                       ; 4-byte round-up
                pool = *sCurrentPool   (global 0x2be21b30)
                p = pool->available ;  pool->available += size
                if (pool->available <= pool->end) return p          ; strict bump, no free lists
                else: vm_allocate(&q, 153600, VM_FLAGS_ANYWHERE)    ; 0x2be0e010-0x2be0e01c
                      on failure: log("out of address space for dyld memory pool\n") + _exit(1)
                      q = {next = pool, available = q+12, end = q+0x25800}; sCurrentPool = q
_free(p):       if (gLibSystemHelpers == NULL) return       ; 0x2be0e082 — pre-libSystem: NO-OP
                if (p in [0x2be00000, initialPoolContent+0x25800)) return   ; NO-OP
                walk the pool list; inside any pool → return              ; NO-OP
                else helpers->free(p)
_strdup(s):     n = strlen(s)+1 ; p = _malloc(n) ; memcpy(p, s, n) ; return p
```

Three consequences, and they settle the premise: (i) the pool hands out memory **strictly
monotonically** from `initialPoolContent+0`; (ii) `_free` **never** gives memory back to a
pool (below `gLibSystemHelpers != NULL` it does nothing at all, and the pool test returns
early regardless), so an address's offset is pure allocation order; (iii) `_strdup` returns
the block's **first byte**, so `r5` *is* the string, and the bump pointer when it was
allocated was exactly `pool+0x11e0` — the earlier allocations sum to `0x11e0` = 4576 B, each
rounded up to 4. So the premise "~4576 B consumed before +0x11e0" is exact, not approximate.

`r8 − r5 = 0x98`. Since allocation order is monotone, **whatever `r8` points at, if it is a
pool object, was allocated *after* the message.** The throw path has exactly **one** pool
allocation after the message:

```
throwf 0x2be0166e bl _strdup                      ; <-- the message, at pool+0x11e0
       0x2be01676 bl __simple_sfree               ; frees the 4 KB salloc page (vm_deallocate: log :4102)
       0x2be0169c movs r0,#4 ; bl ___cxa_allocate_exception
___cxa_allocate_exception 0x2be14584: r0 = size+80 ; bl do_malloc     (0x2be1462c)
__cxxabiv1::do_malloc     0x2be1462c: 0x2be1464a bl _malloc          ; <-- the POOL
                                     then memset(base,0,84); returns base+0x50
__cxa_throw  0x2be148c0:             fills the 80-byte header below that pointer
```

(the pad's other work, `syncAllImages()` `0x2be0184c`, contains **no** allocation — it only
walks `sAllImages` and calls a notify — and the log path's only allocation is the 4 KB
`vm_allocate` page, which is outside the pool). So the only readings of `r8 = +0x1278` are:

| # | hypothesis for `r8` | block size of the message | implied `strlen(msg)` | signature bytes at `r8` |
|---|---|---|---|---|
| 1 | the exception **block base** (= `do_malloc`'s return, the *next* free offset) | `0x98` | **148…151** | `0x432b2b00` at `+0x30`, `0x434c4e47` at `+0x34` |
| 2 | the exception **object pointer** (`base+0x50`, the value `throwf` is given) | `0x48` | **68…71** | `0x432b2b00` at `−0x20`, `0x434c4e47` at `−0x1C` |
| 3 | **interior** of the message block itself | `≥ 0x98` | **≥ 148** | printable ASCII |
| 4 | none of the above | — | `strlen < 148` (and not #2) | `r8` is then in the pool's **not-yet-handed-out tail** ⇒ zeroed `__bss`, a stale leftover |

A neighbour that is *another string* is ruled out: nothing formats a string between the
message's `strdup` and the trap, and any "Referenced from …" text was already an argument to
the `throwf` that built the message, so it was allocated **below** `0x11e0`.

**What cannot be proven from this dump**: that `r8` designates an object at all. No code on
the halt path writes `r8` (`dyld::halt` pushes only `{r4,r5,r7,lr}`; `_dyld_fatal_error` is a
bare `udf`; the pad `0x2be04baa-0x2be04be4` never touches it; every callee preserves it), so
its value is *inherited* — the unwinder's register context for `_main`'s frame at the pad.
`_main` does use `r8` as a live register earlier (`0x2be04590 ldr.w r8,[r4]`,
`0x2be045da ldr.w r8,[sl],#4` — pointer elements of a vector; `0x2be04660 add r8,pc` → a
`__cstring`), but none of those is `0x2be24428`, so the value came from outside `_main`'s own
code and a register dump cannot name it — and it cannot *dereference* it either, since the
pool is zero-fill `__bss` and the log has no memory.

**Cheap decisive test for 995** (no new machinery: the same trap carries both registers):
print the `copyinstr` at `r5` (or `A+0x20`) *and* a 48–64 B `copyin` at **`r8`**. Printable
ASCII ⇒ #3 (the bytes name which part of the message); the `0x432b2b00`/`0x434c4e47` pair at
one of the two offsets above ⇒ #1 or #2 with the offset saying which; all zero ⇒ #4, and `r8`
should then be dropped from any further reasoning. The recovered `strlen(msg)` picks between
148…151 and 68…71 and so decides between #1 and #2 independently.

### 9.4 995 outcome — the witness read the message (measured)

§9.2 was written as a prediction and the 995 press confirmed it. The evidence: log
`out/stage90/captures/cancro-last_kmsg-995-armed-d13-4efcabae.txt` (arm `armed-d13-4efcabae`,
slide **`0xD0000`**), lines 4103–4138 (the witness's rate limit prints the first two traps).

```
4105     r0: 0x2bf19564  r1: 0x00000001  r2: 0x2bef1b1c  r3: 0x2bef1c8c
4106     r4: 0x2bf19564  r5: 0x2bef4390  r6: 0x00000002
4107     r8: 0x2bef4428
4108    r12: 0x00000000  sp: 0x2fd2f9f0  lr: 0x2bed1f07  pc: 0x2bed10c4
4110 dyld_all_image_info_addr: 0x2bef1c8c   dyld_all_image_info_size: 0x000000a4
4111 IOS7WITNESS dyld msg=0x2bf19564 copyinstr=0 len=103
4112 IOS7WITNESS r5 msg=0x2bef4390 copyinstr=0 len=103
4113 IOS7WITNESS r5 matches errorMessage
4114 dyld> Library not loaded: /usr/lib/libbsm.0.dylib?  Re
4120 IOS7WITNESS r8=0x2bef4428 message interior len=0
```

Slide check (every value minus `0xD0000`, and equal to 994's): `pc` → `0x2be010c4` =
`_dyld_fatal_error`; `lr` → `0x2be01f07` = `halt+0x4b`; the word read *from* `A+0x20` →
`0x2be49564` = `_error_string`; `r5` → `0x2be24390` = `initialPoolContent+0x11E0`; `r8` →
`0x2be24428` = `+0x1278`; `A` = `0x2be21c8c`. **The pool geometry is byte-for-byte 994's**
(only the slide moved), so §9.1/§9.3's arithmetic transfers without change.

**§9.2 CONFIRMED by measurement.** The ABI read (`copyin` u32 at `A+0x20`, then `copyinstr`)
and the register read (`copyinstr(r5)`) return the **same 103 bytes**, and the witness's own
verdict line says so ("matches errorMessage", a `bcmp` over the cleaned buffers). Two
pointers, one text — exactly what §9.2 predicted from `halt`'s order.

**The message.** The first 48 bytes are `Library not loaded: /usr/lib/libbsm.0.dylib?  Re`;
the `?` is the witness's cleaner replacing the message's `\n` at **index 43** (any byte
`< 0x20` or `> 0x7e` → `'?'`, by design), i.e. the path ends at index 42 and byte 43 is the
format's first newline ⇒ the "not loaded" path is the 23-char `/usr/lib/libbsm.0.dylib` and
the 48-byte chunk is exactly one `IOS7WIT_LINE` worth of it.

Format and call site, both by value: the `__cstring` at `0x2be1e0df` reads
`Library not loaded: %s\n  Referenced from: %s\n  Reason: %s` (and "Referenced from" occurs
nowhere else in the image), and its only use is `0x2be0a350-0x2be0a35e` in
`ImageLoader::recursiveLoadLibraries`
(`__ZN11ImageLoader22recursiveLoadLibrariesERKNS_11LinkContextEbRKNS_10RPathChainE`):
`movw r0,#0x3d7f; movt r0,#1; add r0,pc` → `0x13d7f + 0x2be0a360 = 0x2be1e0df`, then
`bl 0x2be01640` = `dyld::throwf`. The three arguments at that call:

- `%s#1` (Library not loaded) = `[[r7,#-156]]+0` — the required dylib's record;
- `%s#2` (Referenced from) = `[[r7,#-184]+8]`, else `+4` if `+8` is 0 — and `[r7,#-184]` is
  `r0` on entry to the function (stored at `0x2be09d28`), i.e. the **referencing `ImageLoader*`
  (`this`)** ⇒ this string is *its own* path;
- `%s#3` (Reason) = `[r7,#-220]` = the **caught message**. The site is a
  `catch (const char*)` — `bl 0x2be14950` = `___cxa_begin_catch` at `0x2be0a2a2` — which
  classifies the caught text with `_strstr` (`0x2be15db8`) against `"Incompatible"`
  (`0x2be1e0c5`, inside the 101-char "Incompatible library version…" reason) and
  `"architecture"` (`0x2be1e0d2`), sets `errorKind` (7 / 14 / 15, then 16 immediately before
  the throw), and **re-throws the loader's own failure text wrapped in this message**.

Length arithmetic (what the 48-byte chunk makes checkable): literals 51 + path 23 ⇒
**`|X| + |R| = 29`**. The load-failure reasons this dyld can throw, from its own `__cstring`:
`image not found` (15), `no suitable image found.  Did find:` (35), `mach-o, but wrong
architecture` (30), `no matching architecture in universal wrapper` (45), `unknown file
type, …` (93, composed ≈85), `Incompatible library version: …` (101, composed ≥101). Only
**`image not found` (15)** fits a plausible referencing path, forcing `|X| = 14`; the only
other literal that fits the arithmetic at all is `symbol not found` (16 — a *binding* reason,
not a load reason) with `|X| = 13` = `/sbin/launchd` (the exec path the kernel logs at
`:4095`). **So the natural guess `/sbin/launchd` + `image not found` (= 102) is one character
short of the measured 103** — either the referencing path is 14 chars or the reason is not
`image not found`. 996's full print (48+48+7) decides it; the 48-byte chunk cannot.

The chunker: the 995 witness printed only the first 48-byte line of each copy (the two
following lines came out empty), but `copyinstr` returned the full `len=103` at **both**
pointers — the text is intact and complete, and the truncation is the witness's own bug (the
lead's note; fixed for 996), not dyld's and not `copyin`'s.

**§9.3's table, resolved as far as measurement goes.** The witness implemented §9.3's two
probes exactly — `r8-0x20` (row #2's position) and `r8+0x30` (row #1's position) — and **both
MISSED**: neither offset holds the `__cxa_exception` signature pair. So the two exception
readings are **retired by measurement**, not merely unproven. The fallback then ran
`copyinstr(r8)` and **succeeded with `len=0`** ⇒ the byte at `r8` is `0x00` (a copy failure
would have printed the `stale probe=…` branch instead; and "message interior" is only the
witness's label for this branch — `r8 - r5 = 0x98 = 152 > 103`, so `r8` is *not* inside the
message text).

That length is the input §9.3 was missing. `strlen(msg) = 103` ⇒ the message block is
`roundup(104,4) = 104 = 0x68` at `pool+0x11E0`, ending at `pool+0x1248`; the only pool
allocation after it is the exception block (`__cxa_allocate_exception(4)` → `do_malloc(84)`),
i.e. `0x1248…0x129C`, object at `base+0x50 = 0x1298`, and the `__cxa_exception` signature pair
`0x432b2B00` / `0x434C4E47` sits at `obj-0x20 = **0x1278 = r8**` — whose first byte in memory
is `0x00`, exactly the measured len-0 read. So the first reading of `r8` is **the address of
the exception header's signature word**, a fifth reading §9.3's table did not list and the
one that explains both misses (each probe was aimed one object-interpretation away from it).
The remaining alternative is row #4 (zero-filled unallocated tail) — but with `strlen = 103`
the tail begins at `0x129C`, *after* `r8`, so #4 now requires the exception block not to be
where the arithmetic puts it. Note what this does and does not do: it **identifies** the
address if the probe hits, it does not explain the provenance (r8 is still an inherited
callee-saved register).

**Pre-registered for 996 (one word, no new machinery).** `copyin` the u32 at **`r8`** — and
at `r8+4`: `0x432b2B00` / `0x434C4E47` ⇒ `r8` is the signature address and the register can be
closed out; `0` (or anything else) ⇒ row #4 holds and `r8` should be dropped.

**Run state (measured).** 234 × `EXC_BAD_INSTRUCTION at 0x2bed10c4` (= `_dyld_fatal_error`) —
the 995 log's count — with no panic anywhere; `xnu_live_exec_seq=0x00000001` (`:21255`) ⇒ the
kernel is alive behind the looping user fault; `xnu_live_ostext_limited=0x00000001`
(`:22753`) ⇒ the run is bounded by the console block, as designed; exactly **2**
`IOS7WITNESS r5 matches errorMessage` lines (the witness's own rate limit). End to end the
run reads: the kernel exec'd launchd (`:4095 attempting to start init of /sbin/launchd`),
dyld parsed it, its **first** `LC_LOAD_DYLIB` (`/usr/lib/libbsm.0.dylib`; `libSystem.B.dylib`
is second — launchd's load commands, by value, dyldtrace lane) failed to load, the loader's
own failure text was re-thrown as this message, and `_main`'s catch halted.

**The next question — a working hypothesis, NOT proven.** `libbsm.0.dylib` has no file on the
rootfs (`usr/lib` holds only `dyld` + the two `ios7lab` dylibs) but *is* in the shared cache
(marker `0x37454000`; 315,952,666-byte cache, 696 images) ⇒ the minimal explanation is that
**the shared cache was never mapped**, and every cached dylib degraded to a file lookup. 996
tests it with three independent readings: (i) the full 103-byte text (the `%s#2`/`%s#3`
above), (ii) dyld's own `sharedCacheBaseAddress` word at **`A+0x64`** — *offset not confirmed
here: this tree's `kdp_dyld.h` copy ends at `+0x38`, so it cannot corroborate it; the
dyldtrace lane is confirming it against dyld's own struct*, (iii) a bounded kernel probe on
syscalls 294/438.

**dyld's syscall surface (by value, for the 996 probe).** Both shared-region entry points are
BSD syscalls through dyld's own wrappers, and both are table-present in the D13 kernel:

- `_shared_region_check_np` (`0x2be05774`, called from `0x2be04fe0`): `mov.w r0,#294 ; blx
  0x2be1c200` (`_syscall`).
- `_shared_region_map_and_slide_np` (`0x2be0579c`, called from `0x2be054b2`): on its mode
  word `== 0` path it reaches `mov.w r0,#438` at `0x2be058d6` (the flag word is loaded from
  `0x2be1195C+0x8C`, at `0x2be05804/0x2be05806`); the other path deallocates
  `0x2C000000`/`0x14000000` (`_vm_deallocate` `0x2be1bba0`) and maps the segments itself.
- The image holds exactly **four** Thumb `svc #0x80` encodings (bytes `80 df`) — whole-file
  byte scan, at VAs `0x2be08351` / `0x2be0a263` / `0x2be114f5` / `0x2be188f7`. The BSD-syscall
  surface itself is ARM-mode: `_syscall` at `0x2be1c200` (`mov r12,sp; push {r4,r5,r6,r8};
  …`) with its `svc #0x80` at `0x2be1c210`, and ~100 ARM `svc #0x80` encodings in VA
  `0x2be1ac0c`–`0x2be1c4d8` (the per-syscall trampolines the wrappers `blx` into).
- Kernel side, by value in `out/stage90/xnu_arm_entry.elf`: `shared_region_check_np` is at
  `0x8041fd84` and `shared_region_map_and_slide_np` at `0x804201c0` — both linked in — and
  `bsd/kern/syscalls.master` tables the latter (the generated `bsd/kern/init_sysent.c` is not
  part of the checkout; XNU builds it from the master). "Syscall missing" is therefore
  excluded at the table level; what 996 measures is the **runtime return value**.

### 9.5 996 rev 3 — the tooling landed; the chunker bug, by mechanism

The 48-byte truncation is now explained mechanically, not just observed. `ios7wit_lines` prints
`used` bytes as `IOS7WIT_LINE`-byte lines by writing `text[index + count] = 0` to terminate each
chunk — but that store ran for **every** chunk, and for every chunk except the last,
`index + count` **is the next chunk's first byte**. So the 995 log's
`dyld> Library not loaded: /usr/lib/libbsm.0.dylib?  Re` is chunk 1 whose terminator NULed
chunk 2's first byte, and the two empty `dyld>` lines are the chunks that were NULed. The text
itself was never truncated (`copyinstr` returned `len=103` at **both** pointers), and the
two-pointer `bcmp` runs **before** the printing — which is why `matches errorMessage` was still
correct. The fix (landed in `tools/patch_d13_dyld_witness.py`, sha
`1a3f0aee0bdd8f9f8c671f205a73fa1a9a45969b229c3f7430f9d5d51faee01a`) saves the byte before the NUL
and restores it after, so `text` comes back intact. `ios7wit_clean` is untouched (the `?`
sanitization is by design).

The 996 field reads landed as named constants, single-sourced next to
`IOS7WIT_ERROR_MESSAGE_OFF`: `IOS7WIT_AI_ERROR_KIND 0x40`, `IOS7WIT_AI_CLIENT_PATH 0x44`,
`IOS7WIT_AI_TARGET_PATH 0x48`, `IOS7WIT_AI_ERROR_SYMBOL 0x4c`, `IOS7WIT_AI_CACHE_SLIDE 0x50`,
`IOS7WIT_AI_CACHE_BASE 0x64`. Every read is fail-soft and every failure branch prints: the
`IOS7WITNESS info cacheBase=0x%08x slide=0x%08x errKind=0x%08x` line with its
`cacheBase/slide/errKind copyin=%d,%d,%d` fallback, then `client=` / `target=` / `symbol=` per
field, each with its own `copyin=%d` / `copyinstr=%d` failure form and `"<null>"` for a zero (or
empty) pointer. `IOS7WIT_MIN_INFO_SIZE` rose 0x24 → 0x68; the gate (unchanged in position) sits
**before** the print-slot claim, so a struct smaller than the reads goes silent without burning
one of the two firings. Measured size is 0xa4 — 0x3c of margin. The `+0x64` offset is still the
one this tree's headers cannot corroborate, which is exactly why it lives in one named constant
and why the read is fail-soft: a moved offset prints a wrong value, it cannot fault one in.

Staging is now re-stage-capable, so the driver (`tools/stage_d13_dyld_witness.sh`) re-runs
unedited: the patcher removes its own marker-bounded BEGIN..END regions (version-agnostic),
re-checks the file's identity strings and both anchors on the stripped text (a partial or foreign
edit is still refused), then re-inserts. Verified byte-for-byte: `strip(staged rev 2)` equals
`git HEAD:osfmk/arm/trap.c` exactly (955 lines; 158 removed in 2 regions), and staging that HEAD
text reproduces the staged file. The staged tree `osfmk/arm/trap.c` is sha256
`b9fa138368d2457c6ed12d6246bc65c81da2ce3281398c6826340c91653bd831` — 1191 lines = 955 + 231
witness + 5 call — equal to the patcher's output three independent ways (sandbox cmp,
second-driver-run no-op, `stage(git HEAD)`).

Checker (sha `fd8eb93d58dcb066d13d1fdcff13958b8dce4a6f7bbbd31dc2f7015ebe30adca`) asserts, off the
linked image, `copyin` ≥ 9 / `copyinstr` ≥ 6 and the immediates `#32` / `#48` / `#100` / `#80`; a
synthetic probe linked from the rev-3 block passes 33/33 with `#100`/`#80` materialised by value
(`add r0, r0, #100` @ `0x80000670`, `#80` @ `0x80000658`). The pre-rebuild
`out/stage90/xnu_arm_entry.elf` (`7cd89e81…`, still rev 2) FAILs ON on exactly the ten rev-3
checks — the clean stale signal; the frozen 994 control still PASSes OFF and FAILs ON.

### 9.6 Tasks A–D, by value (dyldtrace lane, 2026-10-12, post-995; §9.6.A revised post-996 — the setErrorStrings dispatch is INDIRECT)

Read-only disassembly of the pinned `/tmp/dyld_ios7`; whole-file Thumb BL/BLX census (2,237 call sites) and
a `movw`/`movt`/`add pc` resolver over the annotated dump (`/tmp/d996_*.txt`). **Segment map first, because it
bites:** `__TEXT` vmaddr `0x2be00000` fileoff `0` filesize `0x21000`; `__DATA` vmaddr `0x2be21000` fileoff
`0x21000` **filesize `0x1000` only**; `__LINKEDIT` fileoff `0x22000`. So `VA = 0x2be00000 + file_offset`
holds **only below `0x2be22000`**; `0x2be22000..0x2be4a000` is zero-fill, and every aii / cache-global field
starts **0** at load. (Reading those words at their "file offset" returns `__LINKEDIT` symbol-table bytes —
a mistake this lane caught itself making.) 【测】

**(A) aii `+0x40..+0x4C` and `+0x64`: writers found by value — one indirect dispatch; `+0x64` has none.**

| field | writer (by value) | reachable? |
|---|---|---|
| `+0x20` errorMessage · `+0x24` terminationFlags | `setAlImageInfosHalt` `0x2be08d04` (doc §1/§9.2) | yes — sole caller `dyld::halt` `0x2be01efc` |
| `+0x40` errorKind | `setErrorStrings` `0x2be05d66`: `strd r4,r5,[r1,#64]` (`r4` = arg0) | **yes — indirect; measured 1** |
| `+0x44` clientPath | same `strd` (`r5` = arg1) | yes — measured `/sbin/launchd` |
| `+0x48` targetPath | `0x2be05d6c`: `str r2,[r1,#72]` | yes — measured ≠ 0 (faulting ptr) |
| `+0x4C` errorSymbol | `0x2be05d70`: `str r3,[r0,#76]` (`r0` reloaded = aii at `0x5d6e`) | yes — measured 0 (= the `NULL` arg) |
| `+0x50` sharedCacheSlide | `0x2be050cc` (region accepted) · `0x2be054fc` (post-map success) | yes — **the probe's best word** |
| `+0x54..+0x63` sharedCacheUUID | `0x2be050ec` · `0x2be05512` (`vst1.8 {d16-d17}`) | yes, when the header's `mappingOffset ≥ 0x68` |
| `+0x64` (v0x0E: `reserved[0]`, not a named field — see below) | **no writer anywhere in the image** | — |

**`setErrorStrings` `0x2be05d50` IS reached — through the `LinkContext` hook table.** The direct-branch
census's 0 is correct and irrelevant: the only in-file occurrence of the address outside the code is a
symbol-table word in `__LINKEDIT` (`0x2be241d8`), because dyld dispatches every context hook INDIRECTLY.
`dyld::setContext` materializes the Thumb address `0x2be05d51` at `0x2be04d78` (`add r5,pc`) and stores it
to `gLinkContext` (`0x2be2195c`, symbol `_ZN4dyld12gLinkContextE`) `+0x44`: `str.w r5,[r9,#68]` @`0x2be04d80`.
The context-hook slots carry 32 `ldr rX,[…,#68|#72]; blx rX` call sites in the image (21 on `+0x44`, 11 on
`+0x48`) — e.g. `0x2be0a2e6/0x2be0a300`, `0x2be0a30c/0x2be0a324`, `0x2be0cf4a/0x2be0cf56` — and the
`const LinkContext&` in every `ImageLoader::…` symbol is that same struct. ⇒ **the 996 press's four values ARE this dispatch**: `+0x40 = 1`,
`+0x44 = "/sbin/launchd"`, `+0x48 ≠ 0`, `+0x4C = 0` are exactly the four arguments of the call at
`ImageLoader::recursiveLoadLibraries+0x60c` (`0x2be0a300`: `movs r0,#1` @`0x2be0a2fc`, `movs r3,#0`
@`0x2be0a2fe`; `r6 = [LinkContext+0x44]` @`0x2be0a2e6`; `r1 = [this+4]` (client), `r2 = *[r7,#-156]`
(target) @`0x2be0a2e8..f2`). Two of the 21 `+0x44` sites instead ZERO all four fields — `ImageLoader::link`
@`0x2be09a96` and `0x2be09bdc` (`r0..r3 = 0` = `dyld_error_kind_none`, i.e. state clears) — so the last
writer on the failing run is the `+0x60c` call. The earlier prediction "`errKind=` / `client=` / `target=`
will read `0` / `<null>` on every path, including a successful boot" is **RETIRED by measurement**. 【测】

**Which arm, and what the numbers are called.** The same catch block (`__cxa_begin_catch` `0x2be14950`
@`0x2be0a2a2`, `mov r4,r0`; required-guard `ldrb;cbz 0x2be0a362` @`0x2be0a2a8`) classifies the caught
message: `strstr(msg,"Incompatible")` (`0x2be1e0c5`, args @`0x2be0a2b2..bc`) ≠ 0 ⇒ arm `0x2be0a304` ⇒ kind
**3**; else `strstr(msg,"architecture")` (`0x2be1e0d2`, @`0x2be0a2cc..da`) ≠ 0 ⇒ arm `0x2be0a328` ⇒ kind
**2**; else fall-through `0x2be0a2f6` ⇒ kind **1**. The 995 message ("Reason: image not found") contains
neither needle ⇒ kind 1 — what was measured. `ImageLoaderMachO::throwSymbolNotFound` (`0x2be0cf3c`) is the
kind-**4** side (`movs r0,#4` @`0x2be0cf4a-56`), formatting
`"Symbol not found: %s\n  Referenced from: %s\n  Expected in: %s\n"` (`0x2be1ea98`, `add pc` @`0x2be0cf64`).
Apple's header for the identical struct era, dyld-353.2.3 `include/mach-o/dyld_images.h` (its version
comments are exactly the v11–v14 field list this pin's layout obeys to the byte, see the `+0x64` note
below), names them `dyld_error_kind_none=0, dylib_missing=1, dylib_wrong_arch=2, dylib_version=3,
symbol_missing=4` — measured 1/2/3/4, zero mismatches. ⇒ 996's `errKind=1` =
**`dyld_error_kind_dylib_missing`**. 【测】+【引】

**#4 closes: the write happens, and the string is the one in the message.** The joint tail
`0x2be0a334..5e` calls `dyld::throwf` (`0x2be01640`) with r0 =
`"Library not loaded: %s\n  Referenced from: %s\n  Reason: %s"` (`0x2be1e0df`, `add pc` @`0x2be0a35c`),
r1 = `*[r7,#-156] → [#0]` (@`0x2be0a33a`), r2 = this-image path (`[r7,#-184]`: `[#8] ?: [#4]`), r3 = the
caught message (`r4` via `[r7,#-220]`, §9.6.D). The setErrorStrings arms load their target from the SAME
local: `ldr.w r2,[r7,#-156]; ldr r2,[r2]` @`0x2be0a2ee/f2`. ⇒ the image-not-found path **does** write
`errorTargetDylibPath`, from the very string that then fills the message's first `%s`. The witness's
`target copyinstr=996748` = `0xF358C` is the **faulting user address**, not a reading — `copyinstr`'s fault
stub returns the faulting user VA and does not set EFAULT (`bcopyinout.s:1014-1018`) — and at that same
witness moment the malloc'd message copy read fine (995's 103 bytes carry `/usr/lib/libbsm.0.dylib`). ⇒ the
stored pointer's page was unmapped by witness time (a transient heap buffer); it was written, not stale. 【测】+【推】

`+0x64` likewise has no writer: the **only** non-stack `[rX,#0x64]` store in the image is `dyld::setContext`
`0x2be04dbc` `str.w lr,[r0,#100]!`, and that `r0 = r9 = gLinkContext`, not the aii. ⇒ **996's `cacheBase=`
read at `A+0x64` is vacuous (prints 0 always)** — replace it with `A+0x50` (slide-free; written by dyld
**only once the cache is mapped**, see B — `0 == never mapped`) plus `A+0x54` (UUID), and optionally
`[0x2be2301C]` (cache-global `base`, needs `slide = A − 0x2be21c8c`; nonzero means *a region existed*). 【测】

**And the `+0x64` NAME is retired with it: a v0x0E aii has no `sharedCacheBaseAddress`.** Apple's field
history (same dyld-353.2.3 header) lists, after `errorSymbol`, in order: `sharedCacheSlide` (v12, iOS 4.3)
→ `sharedCacheUUID[16]` (v13, iOS 7.0) → **`reserved[16]`** (v14, iOS 7.0). The offsets obey it bit for
bit: v11 = `0x40..0x4C` (measured above), v12 = `0x50` (measured: `0x2be050cc` · `0x2be054fc`),
v13 = `0x54..0x63`, v14 = `0x64..0xA3` — and `__DATA,__all_image_info` is exactly `0xa4` = `0x64 + 16×4`
bytes (`_dyld_all_image_infos` = `0x2be21c8c`, section size 0xa4; the pin's own `dyldVersion` string at
`aii+0x1C` = `0x2be1df23` reads `"324.1"`). The pin's file leaves `+0x64 = 0`. ⇒ the 996 `cacheBase` word
read `reserved[0]`; the name `sharedCacheBaseAddress` came from LATER dylds, which turned that reserved slot
into a real field. Keep the recommended replacement reads. 【测】+【引】

**(B) `mapSharedCache` `0x2be04fa0` — the complete exit catalog; none of them throws.**

Call targets over `0x2be04fa0..0x2be0596e`, all 17, contain **no `___cxa_throw`, no `dyld::throwf`, no
`dyld::halt`** — every failure is log-and-continue and the caller just sees "no cache". 【测】

- **Mode gate in the check wrapper `0x2be05774`:** `ctx = 0x2BE2195C`; `ldr r0,[ctx+0x8C]; cmp #0;
  itt ne {movne r0,#−1; popne}`; only when `[ctx+0x8C] == 0` does `mov.w r0,#294 ; blx _syscall`
  (`0x2be05792`) run. Non-zero ⇒ **no syscall 294 at all** and return −1 (feeds the fd path). Writers of
  `[ctx+0x8C]`: the env parser `0x2be0253c`(=1) / `0x2be0254e`(=2) / `0x2be02560`(=0) / `0x2be0256a`(=3),
  plus the map wrapper's own `0x58a0`(=2) / `0x5960`(=1). XNU's init does not set `DYLD_SHARED_REGION`, so a
  plain boot runs at 0 and the syscall **is** issued. `0` from the kernel = "a region exists" → region path;
  non-zero → `openSharedCacheFile` (`bl 0x2be02e24` @`0x4fe8`). 【测】
- **fd == −1** (`cmp r5,#−1` @`0x4fee`) ⇒ verbose-gated log
  `'dyld: shared cached file cannot be opened\n'` (`0x2be1d0f1`, len 42) @`0x5012` → `0x5522`. 【测】
- Region path `0x5018`: `r5 = 0x2be23010`; **`str base,[r5,#12]` @`0x5032` sets `[0x2be2301C] = the kernel
  region base` before the magic check**; `strcmp(base, 'dyld_v1   armv7' 0x2be1cf46)` @`0x5034`; mismatch ⇒
  **`[0x2be2301C] := 0` @`0x5044`** + verbose-gated log
  `'dyld: existing shared cached in memory is not compatible\n'` (`0x2be1cf56`, 57) @`0x5058` → `0x5754`.
  (By-value oddity: the *non-verbose* mismatch branch `cbz r0,0x50a8` @`0x504c` rejoins the header parse with
  `r4 = 0`, i.e. would `ldr r0,[r4,#16]` @`0x50a8` from address `0x10` — a latent fault that Apple never hits
  because a kernel region always carries the magic if it exists at all.) 【测】
- Header accepted (`0x50a8`, both routes): `[r4+0x10] < 0x48` ⇒ `0x552e`; if the 64-bit `[r4+0x40]` (slide
  info) ≠ 0 ⇒ `slide = r4 − mappings[0].address`, stored at cache-global `+0x20` @`0x50c6` and
  **aii+0x50 @`0x50cc`**; `mappingOffset ≥ 0x68` ⇒ UUID copied from region`+0x58` to **aii+0x54** @`0x50ec. 【测】
- fd route `0x505e`: `_read(fd, buf, 0x2000)` @`0x5068`; short read ⇒ verbose-gated
  `'dyld: shared cached file cannot be read\n'` (`0x2be1d0c8`, 40) @`0x5110`; bad magic ⇒ verbose-gated
  `'dyld: shared cached file is invalid\n'` (`0x2be1d0a3`, 36) @`0x5110` → `_close` `0x551c`; then
  `_fstat` @`0x513a` (**failure is silent**), the 32-byte mapping entries are validated — a mapping outside
  the file ⇒ **unconditional** log `'dyld: shared cached file is corrupt: %sdyld_shared_cache_armv7\n'`
  (`0x2be1cf90`; the `%s` is the 38-byte dir `'/System/Library/Caches/com.apple.dyld/'` from
  `[0x2be21af0] = 0x2be1d7b7`, and the file name is inside the same literal) @`0x5206`, bad flag set, loop
  continues; missing full coverage ⇒ **unconditional** `'…missing read-only mapping: %sdyld_shared_cache_armv7\n'`
  (`0x2be1d023`, 81) @`0x5314` or `'…missing read/write mapping: %sdyld_shared_cache_armv7\n'`
  (`0x2be1cfd0`, 82) @`0x5330` → `0x5516` (**nothing stored**). 【测】
- Map step: `bl 0x2be0579c` @`0x54b2`; **non-zero ⇒ verbose-gated log
  `'dyld: shared cached file could not be mapped\n'` (`0x2be1d075`, 45) @`0x54d6`, nothing stored**;
  zero ⇒ the fd route's only stores: cache-global`+0xC` = `mappings[0].address` @`0x54f6` (write **2** of
  the 0x32/0x44 question), **aii+0x50 = slide @`0x54fc`**, aii+0x54 UUID @`0x5512`. Every exit then passes
  `0x5522` (`[0x2be2301C]==0 ⇒ return NULL`) — so `[0x2be2301C]` is set exactly when a cache region exists. 【测】
- Map wrapper `0x2be0579c`, three paths: (a) `r3 != −1` ⇒ `_fcntl(fd, 61 /*F_ADDFILESIGS*/, &{off,size of
  mappings[r3]})` @`0x57ca`; on −1 + verbose, `'dyld: code signature registration for shared cache failed
  with errno=%d\n'` (`0x2be1d297`, 72) @`0x57f4`; (b) **`[ctx+0x8C] == 0` (default) ⇒
  `mov.w r0,#438 ; blx _syscall` @`0x58d6/0x58da`** = `shared_region_map_and_slide_np(fd, count, mappings,
  slide, flags, addr, size)`, its return value passed back untouched, `[ctx+0x8C]` left 0; (c) non-zero ⇒
  dyld maps the cache itself: `vm_deallocate(0x2C000000, 0x14000000)` @`0x5826`, `strb #1,[aii+0x10]`
  @`0x5842`, `mmap` per mapping @`0x5878`, any mismatch ⇒ deallocate again + **`[ctx+0x8C] := 2` @`0x58a0`**
  + verbose-gated `'dyld: shared cached region cannot be mapped at address %p with size 0x%08lX\n'`
  (`0x2be1d2e0`, 76) @`0x58be` + return −1; all mapped ⇒ slide fixups (128×32-byte loop @`0x5908`) then
  **`[ctx+0x8C] := 1` @`0x5960`, return 0**. 【测】

**⇒ what the 995 run's silence means.** With the verbose flag `[gLinkContext+0x9B] == 0` (gates read at
`0x4ffe/0x5048/0x50fc/0x5092/0x54c6`), every exit is silent except the two unconditional
"corrupt"/"missing mapping" logs — which cannot fire without the fd route having opened a file. The 995 log
has **no dyld I/O line at all**, so `mapSharedCache` returned NULL leaving the aii untouched. The routes
consistent with the capture: `check_np != 0` (no kernel region) → open fails or read/magic/fstat/map fails,
all silent → NULL. The `check_np == 0` route is excluded by outcome (a magic-matching region would have made
libbsm reachable; a mismatching one would have crashed on the `0x504c` oddity, not produced this clean
`throwf`). The `[ctx+0x8C] != 0` route would look identical, but nothing writes that word on a stock boot. 【推】

**(C) The message closes exactly: L = 102, `Reason` = the static 15-byte `image not found`.**

- Parsed from the card image (`/tmp/c_launchd.txt`): launchd's load commands list
  **`LC_LOAD_DYLIB /usr/lib/libbsm.0.dylib` first**, then `/usr/lib/libSystem.B.dylib` — both cache-resident
  (§10.7). The first dependency pid 1 resolves is the first one a cache-less dyld cannot satisfy. 【测】
- 995's `copyinstr len=103` counts the terminating NUL (§9.4's "one character short") ⇒ **strlen = 102**.
  `'Library not loaded: %s\n  Referenced from: %s\n  Reason: %s'` (`0x2be1e0df`, 57) with 23 + 13 + **15** =
  102 exactly. A `'no suitable image found.  Did find:'` `Reason` (35) forces ≥ 122; the heap sibling throw
  at `0x2be02cb6` builds a `\n\t`-path list and cannot fit either. ⇒ **S = `'image not found'`
  (`0x2be1cc9d`, 15), thrown by `dyld::load` at `0x2be02d24`** (`add pc` → `0x2BE21000` = the `const char*`
  typeinfo slot; `r2 = 0x2be1cc9d`; SjLj selector 3), reached **because `byte[LoadContext+5] == 0`**
  (`ldrb;cmp;beq 0x2be02d00` @`0x2be02cc0`; the non-zero side unwinds and returns NULL without throwing). 【测】
- The rewrap then fired from `recursiveLoadLibraries+0x66a` `0x2be0a35e` — **§10.5's "EXCLUDED" verdict is
  falsified by measurement**: its gate byte `*[r7,#-164]` (`ldrb;cmp;beq 0x2be0a362` @`0x2be0a29e`) was
  **non-zero** in 995. The allocation arithmetic also closes: `r8 = obj − 0x20 = block + 0x30`, and
  `block − r5 = align4(102+1) = 0x68` ⇒ `r8 − r5 = 0x68 + 0x30 = 0x98` — exactly the measured
  `0x2bef4428 − 0x2bef4390`. 【测】

**(D) `0x2be14950 = ___cxa_begin_catch`** — the annotated dump's label sits immediately above it
(`__cxa_begin_catch: 2be14950 push {r4,r5,r6,r7,lr}`). Its return is used **directly as the `Reason`
string**: `bl ___cxa_begin_catch; mov r4,r0` @`0x2be0a2a2` in the rewrap pad, `r4` → the third `%s`. 【测】

Two citation repairs for §9.4/§9.5: the mode-word base is **`0x2BE2195C` = gLinkContext** (the "`0x2be1195C`"
in §9.4 is a typo; `add r0,pc` @`0x2be05804` resolves to `0x2BE2195C`), and the `+0x64` "uncorroborated
offset" line is now moot twice over — no writer exists, and v0x0E has no such field (`+0x64` = `reserved[0]`,
§9.6.A second block) — so the offset question is retired rather than open.

**Provenance, for the record (ASK 2).** The kernel's own dylinker path is a FILE load, with no shared-cache
participation: `load_machfile`'s `needs_dynlinker` gate (`mach_loader.c:756-764`) → `load_dylinker`
(`:1324+`) opens the name taken from the main executable's `LC_LOAD_DYLINKER` via `get_macho_vnode()`, and
`kern_exec.c:987` publishes the witness anchor from `load_result.all_image_info_addr` — which
`note_all_image_info_section` (`mach_loader.c:249-269`, called for `MH_DYLINKER` only, `:1044-1048`) computed
as section addr + slide. Neither file references `shared_region`/`shared_cache`. ⇒ the running dyld IS the
file at `/usr/lib/dyld` = the pin (byte-identical over the pin's 196,576 bytes; the card's file carries 32
more trailing bytes). The `src/entry/build_entry.sh` note "dyld ran from the mapped shared cache" describes
the design (the file ≡ the cache's dyld) but not the mechanism — that other-lane comment is flagged for a
wording fix, not edited here. 【测】+【引】

### 9.7 997 closure — the census verdict, Q1–Q4, and dyld's own draw budget (dyldtrace lane, 2026-10-12, post-997)

**(E1) Store-pattern census: exactly ONE aii-relative store group exists — `setErrorStrings`; no inlined copy, and none is needed.** Whole-image census over `/tmp/dyld_full.asm`: 1,146 stores at immediate offsets {64,68,72,76} (every decoded `str*`/`strd` form, any base, whole-file) + 944 `stm*`/`vst*` multi-register stores. The groups covering three or more of the four offsets, exhaustively: `0x2be04d7a/80/84/88` base `r9` = **gLinkContext itself** (the `setContext` hook-table writes — this is where `+0x44 = setErrorStrings|1` is stored, §9.6.A); `0x2be05d66` `strd r4,r5,[r1,#64]` · `0x2be05d6c` `str r2,[r1,#72]` · `0x2be05d70` `str r3,[r0,#76]` — **the only aii-relative group** (base = the double-deref of literal slot `0x2be21008` = `&gProcessInfo` → aii `0x2be21c8c`); `0x2be07b14/1a` and `0x2be07ed0/ed4/ed8` base `r5` (`_dlopen`/`_dlopen_preflight` locals); `0x2be25430/3c/48/54` (`__LINKEDIT` bytes misdecoded as code). No multi-register store touches the aii offsets. ⇒ the ultimatum's two horns reconcile on the third path: the out-of-line `setErrorStrings` `0x2be05d50` ran, reached through the indirect `gLinkContext+0x44` dispatch (Thumb `0x2be05d51` = `setErrorStrings|1`; materialized `0x2be04d78`, stored `0x2be04d80`; 21 dispatch sites), and its four arguments ARE 996's measured `+0x40 = 1`, `+0x44 = "/sbin/launchd"`, `+0x48 ≠ 0`, `+0x4C = 0`. Neither an inlined copy nor a leftover contradiction remains. 【测】

**(E2) Q1 — count = the cache FILE header's `mappingCount`; the array is a runtime stack copy of the header's mapping table.** `mapSharedCache` `0x2be04fa0` reads the header into `frame+0x114` (`_read(fd, .., 0x2000)` @`0x2be05068`, == 0x2000 checked), then `ldrd sl,fp,[r4,#184]` @`0x2be05116` = header `+0x10` `mappingOffset` / `+0x14` `mappingCount`; the array is **`sp − (count+1)×32`**, stored to `[r6+0x18]` @`0x2be05122/26`. No static table exists (and the by-value copy rules one out: entries are copied field-by-field, 32 B each, from `header+mappingOffset` @`0x2be0517e..0x2be051ac`). Count provenance by value: `[r4+0xBC]` (header+0x14) → `[r6+0x10]` @`0x2be0512e/32` → `r5` @`0x2be052e8` → `[r6+0x20]` @`0x2be05388` → syscall `r1` @`0x2be054ae`; the add-loop guard and the draw's scan read the same word. Per-entry indices: `[r6+0x24]` = the last entry with `init_prot == 1` (`0x2be05212-18`); `[r6+0x28]` = the last with `(init_prot & 3) == 3` (`0x2be05220-2c`). ⇒ **the 4th entry (m3) is NOT the slide-info mapping**: m3 is the last `init==1` entry (`[r6+0x24]`, fed into `slide_start` below), while `[r6+0x28]` — the entry whose max_prot and init_prot get `VM_PROT_SLIDE` OR'd in @`0x2be05488/0x2be0549a` — is m1, the only `(init&3)==3` entry (measured `0x23`). The `(count+1)`-th slot is the synthetic code-signature segment: address = cache end VA (last entry's addr+size), size = page-rounded (`st_size − codeSignatureOffset`, the `_fstat` buffer's `st_size` at `[r6+0xD8]`), file offset = `codeSignatureOffset`, prot 1/1 — built @`0x2be05284..0x2be052d2`; its index ( = count) is the wrapper's `r3`, or −1 when the cache has no signature region (`0x2be05278`), in which case the wrapper's `cmp r3,#-1` @`0x2be057ac` skips `F_ADDFILESIGS`. The `fcntl` payload takes that entry's `+0x10`/`+0x08` (file offset, size) @`0x2be057be/c4`. 【测】

**(E3) Q2 — the slide is dyld's own draw, and the mapping array IS rewritten to slid addresses before the syscall.** The 438 wrapper's only caller is `0x2be054b2`; args: fd = `[r6+0x14]`, count = `[r6+0x20]`, array = `[r6+0x18]`, index = `[r6+0x1C]`, stack slide/slide_start/slide_size @`0x2be054a2-b0`. The draw @`0x2be05416..0x2be05446`: a 32-bit value from the static-state generator `0x2be15f48` (`r1` is zeroed after the call @`0x2be05430`, so only `r0` is used), reduced mod `(0x40000000 − max_end)` by the 64-bit helper `0x2be1868c` (the same helper serves /10 decimal conversion @`0x2be18fba`/`0x2be19444`; the measured magnitudes 0xd9c000 / 0x3811000 force the remainder-in-`r0` reading over any quotient reading), then rounded up to 4 KiB (`addw #0xfff` @`0x2be0543a`, `bfc #0,#12` @`0x2be05446`). `max_end` comes from the 64-bit min/end scan over the array's address fields @`0x2be05374..0x2be053fc` — and that scan runs on **unslid, base-based** addresses: 997 fixes it by value, `m0.va − slide = 0x2cd9c000 − 0xd9c000 = 0x2C000000` exactly (the copy @`0x2be05184..` is verbatim from the header; the add loop below is the only intervening writer). Then the loop @`0x2be05450..0x2be05466` (`ldmia {r3,r5}; adds r3,r3,r4; adc r5,r5,r0; stmia`, stride 32) adds the drawn slide to **every** entry's 64-bit address field, immediately before `bl 0x2be0579c`. ⇒ **the va fields handed to syscall 438 are already slid** across all entries, so the kernel may only translate (`target = va − sr_base`) and must not re-add the slide — the shreg-g1 (a)/(b) split is a dyld-side invariant, not an inference. The same `r4` is the syscall's `slide` argument (it can only drive the fixups inside the `VM_PROT_SLIDE` segment) and, on success only, `aii+0x50` (`0x2be054fc`). `slide_start` = `[r6+0x14C] + mappings[[r6+0x24]].address − mappings[[r6+0x24]].file_offset` @`0x2be05494..0x2be054a0`, i.e. **slid m3.va − slide_size**, verified on both runs: 996 `0x3fa46000 − 0x89000 = 0x3f9bd000` ✓; 997 `0x3cfd1000 − 0x89000 = 0x3cf48000` ✓ (with `[r6+0x14C] = m3.file_offset − slide_size = 0x12B51000`). 【测】+【推】

**The draw budget, and why 998's `kr=0` is draw-independent.** `0x40000000 − max_end = 0x40000000 − 0x3C3AC000 = 0x3C54000`, and `0x14000000 − span = 0x14000000 − 0x103AC000 = 0x3C54000` — **identical**. G1's window size equals dyld's draw budget plus the cache, so every slide the generator can emit keeps the slid cache inside `[0x2C000000, 0x40000000)`: `target_max = slide + span ≤ 0x14000000` (equality only at the supremum of the draw — still a legal window end). Both measured draws satisfy it (997 `0x11148000`; 996 `0x3811000 + span = 0x13BBD000`). ⇒ under G1 no draw reproduces 997's `kr=1`, and 996's larger draw would also have fit. (This identity belongs to the G1 audit / `docs/reference/d13-shreg-frontier.md`, other lane — recorded here only.) 【测】+【推】

**(E4) Q3 — withdrawn as an offset.** 996 `0xF34CC` vs 997 `0xD356C` for the same string ⇒ per-run; it is the `copyinstr` faulting-address echo (§9.6.A #4), not a cache-file offset for libbsm.

**(E5) Q4 — no `+0x50` writer precedes a successful map.** Writers: `0x2be050cc` (region path — requires a magic-matching region already resident; stores `r2 = r4 − [r4+r0]`, the resident-bias value, @`0x2be050c4/c6/cc`) and `0x2be054fc` (fd path success branch: `cmp r0,#0; beq 0x2be054dc`, stores the drawn slide). A failed kernel map reaches neither ⇒ `+0x50 == 0` is a sound "never mapped" witness; under 998 a nonzero `+0x50` printing the drawn slide (the same value the `IOS7SHREG` line prints) is a positive first. 【测】
