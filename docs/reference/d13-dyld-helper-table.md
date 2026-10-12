# Is there a kernel-populated dyld helper table? — NO (D13, Darwin 13 / iOS 7)

Method: read-only recon of the D13 build tree `external/xnu-hd2-darwin13/xnu` (paths relative to it unless absolute), compared against `external/xnu-4570.1.46/`, the HTC-Leo upstream `external/xnu-upstream/`, and `external/xnu-2050.18.24/`; plus a by-value dissection of the actual dylinker `/tmp/dyld_ios7` (`llvm-objdump-14 --macho`, `struct.unpack_from`, `arm-none-eabi-objdump -D -b binary`). No build, no git, no device; exactly one file written (this one). sha256 (first 12): `osfmk/arm/trap.c` `601a463933c0`, `bsd/kern/mach_loader.c` `0d8cee69d0fd`, `bsd/kern/kern_exec.c` `f404e5bf467c`, `osfmk/arm/cpu_capabilities.h` `9fd54db9d5ef`, `osfmk/arm/PlatformConfigs.h` `a01d18efc139`, `osfmk/arm/ios7_compat.h` `420d9d9e9a50`, `libsyscall/custom/__fork.s` `3411d1e0fbe3`; references `external/xnu-4570.1.46/bsd/kern/mach_loader.c` `06cbd4ff16da`, `external/xnu-upstream/bsd/kern/mach_loader.c` `4ca15d1a46c0`, `/tmp/dyld_ios7` `408061f0ec0a`.

**Verdict up front: no kernel-populated dyld table exists, and none is missing.** The `ldr pc,[pc,ip]` at dyld `0x2be010b8` dispatches into dyld's *own* `__DATA` table, which is non-NULL in the file and slid by dyld itself. The `udf #65006` the run dies on is dyld's `_dyld_fatal_error`, whose only caller is `dyld::halt()` — dyld deliberately aborting, with the message text recoverable from the registers (`r0` = the `_error_string` copy, `r5` = the `halt()` argument). The tree already contains a purpose-built witness for exactly this trap, on another board.

## 1. Grep: no `g_dyld`, no `dyld_func_lookup`, no dyld table in any kernel source

D13 tree, `grep -rn "dyld_func_lookup|__dyld_func_lookup|dyld_helper|g_dyld|set_dyld|gDyld|dylinker|MH_DYLINKER|__dyld"` over `*.c *.h *.s *.cpp`:

- `libsyscall/custom/__fork.s:126-144` — the **only** `__dyld_func_lookup` reference in the whole tree, and it is a *client*, inside `#if 0`:
  `// do this by using the private extern __dyld_func_lookup() function to get the` / `bl __dyld_func_lookup`. libsyscall is the userspace side; this does not put a table in the kernel.
- Everything else is `MH_DYLINKER` / `load_dylinker` mechanics in `bsd/kern/mach_loader.c` (:208, :532, :676, :762, :1044, :1324) and `task_set_dyld_info` / `all_image_info` bookkeeping (below).
- `g_dyld`, `dyld_funcs`, `dyld_helper`, `set_dyld`: **zero hits** in D13. Zero in 4570. Zero in `external/xnu-upstream` (HTC-Leo). Zero in `external/xnu-2050.18.24` and `external/apple-xnu-rel-2050`.

## 2. The kernel's entire dyld-facing surface — five data items, no function table

| # | Site | What it sets |
|---|---|---|
| 1 | `bsd/kern/mach_loader.c:676-681` `case LC_LOAD_DYLINKER:` | records `dlp`, `dlarchbits`; at `:762-764` calls `load_dylinker(...)`, which maps the file named by the command (dyld's own Mach-O) |
| 2 | `bsd/kern/mach_loader.c:249-274` `note_all_image_info_section()` | scans the `__DATA,__all_image_info` **section header** and copies `addr`+`slide` / `size` into `result->all_image_info_addr/size`. Reads metadata only |
| 3 | `bsd/kern/mach_loader.c:1044-1048` | calls #2 only for `filetype == MH_DYLINKER` — plus `:585-587` `slide = aslr_offset` for `MH_PIE || MH_DYLINKER` |
| 4 | `bsd/kern/kern_exec.c:987` | `task_set_dyld_info(task, load_result.all_image_info_addr, load_result.all_image_info_size);` → `osfmk/kern/task.c:258` stores two scalars in the task (`:861` resets them to `MACH_VM_MIN_ADDRESS,0`) |
| 5 | `bsd/kern/kern_exec.c:4126-4245` `exec_prefault_data()` | **reads** dyld's `__all_image_info` with `copyin`, computes `dyld_slide_amount = load_result->all_image_info_addr - dyld_all_image_infos_address` (`:4209`), then `vm_fault()`s four dyld pages so a later debugger access does not re-enter the fault path |

The kernel **never writes into the dyld image**. There is no slot for it to fill: XNU does not apply user-image relocations, and #2/#4/#5 are address/size scalars, not function pointers.

The comparison trees are the same shape, not a different one: 4570 `mach_loader.c:236/:640/:995/:1164/:1872/:2229` and `note_all_image_info_section` at `:278`; HTC-Leo upstream `mach_loader.c:207`, `case MH_DYLINKER:` at `:532`, `case LC_LOAD_DYLINKER:` at `:671`, `load_dylinker(...)` call at `:759`, body at `:1315`, `note_all_image_info_section` at `:248`/`:1037`; `xnu-2050.18.24` identical to the HTC-Leo layout (`:532`, `:671`). Three generations, one design.

**Nearest true analogue** (and the one the 992 work already touched): the commpage. `osfmk/arm/cpu_capabilities.h:12-52` — `_COMM_PAGE32_BASE_ADDRESS 0x40000000`, `_COMM_PAGE32_AREA_LENGTH 1*4096`, `_COMM_PAGE_TEXT_START = START+0x1000`, and the field offsets `_COMM_PAGE_CPUFAMILY 0x40000080`, `_COMM_PAGE_TIMEOFDAY_DATA 0x40000040`, `_COMM_PAGE_MEMORY_SIZE 0x40000038`, `_COMM_PAGE_NCPUS 0x40000022`, `_COMM_PAGE_CPU_CAPABILITIES 0x40000020`, `_COMM_PAGE_MYSTERY_VALUE 0x4000001e`. That **is** a kernel-populated page that userspace reads, and `commpage_populate()` (`osfmk/arm/commpage/commpage.c:70-115`) is its writer. But it is a *data* page and there is **no `_COMM_PAGE_DYLD*` field in this ABI**. dyld does not read it either: in `/tmp/dyld_ios7`, `__init_cpu_capabilities` at `0x2be0921c` is a bare `bx lr` (`70 47`), the only commpage consumer is the libSystem import `___commpage_gettimeofday` (`0x2be181aa`), and the byte string `00 40 ff ff` (0xffff4000) does not occur in the file at all.

## 3. Does our port set up or stub anything dyld needs? No

`grep -rn "dyld|dylinker|all_image_info|TASK_DYLD" src/` returns only prose in `src/entry/build_entry.sh` (comments at `:1103`, `:30482-30516` describing the 992 commpage alias) and one firehose tracepoint enum. No port file names, defines, or stubs a dyld symbol; no dyld-facing table is left zero by the port. The only userspace-visible table the port populates is the 992 commpage alias, and per §2 dyld itself never touches the commpage.

## 4. What `ldr pc,[pc,ip]` actually dispatches to (by value, from `/tmp/dyld_ios7`)

Raw words (`python3 struct.unpack_from('<I')` at the file offsets, cross-checked with `arm-none-eabi-objdump -b binary -m arm`):

```
2be01008  eaff0029  b   _branch_to_lookupDyldFunction      @ _dyld_func_lookup
2be010b4  e59fc000  ldr ip, [pc]        ; ip = [0x2be010bc] = 0x00020a2c
2be010b8  e79ff00c  ldr pc, [pc, ip]    ; pc = [0x2be010b8+8+0x20a2c] = [0x2be21aec]
2be010bc  00020a2c  .word 0x00020a2c
2be010c0  e7ffdefe  udf #65006          @ dyld_stub_binding_helper
2be010c4  e7ffdefe  udf #65006          @ _dyld_fatal_error   <-- the crash pc
```

**Arithmetic correction.** The ARM PC bias is `instr_addr+8`, and the instruction is at `0x2be010b8`, so the base is `0x2be010c0` — not `0x2be010bc+8 = 0x2be010c4` as the working note had it. The slot read is therefore `0x2be010c0 + 0x20a2c = 0x2be21aec`, **not** `0x2be21af0`. `0x2be21aec` is a symbol, and the off-by-4 matters because `0x2be21af0` is the *next* word:

```
$ llvm-objdump-14 --macho --syms /tmp/dyld_ios7
2be21aec l O __DATA,__data _lookupDyldFunction_ptr
```

File content at `0x2be21aec` = `3d 7a e0 2b` = **`0x2be07a3d`** — a valid THUMB pointer to `__Z18lookupDyldFunctionPKcPm` at `0x2be07a3c`. **The slot is not NULL and cannot be NULL**: it is an ordinary initialized static.

What it points at (`0x2be07a3c-0x2be07a9d`) is a small `strcmp` loop:

```
2be07a50  add r4, pc                ; r4 -> __ZL10dyld_funcs
2be07a52  ldr r5, [r4]              ; name
2be07a54  cmp r5, #0 / beq 0x2be07a90   ; NULL terminator -> return 0, *result = NULL
2be07a5c  blx __platform_strcmp
2be07a60  adds r4, #8               ; next (name, func) pair
...
2be07a6e  ldr r1, [r4, #-4]         ; func
2be07a94  str.w r1, [r8]            ; *result = func ; return 1
```

The table is `__DATA,__const:__ZL10dyld_funcs` at `0x2be210c0`, 26 entries + NULL, read from the file:

| i | name | func | i | name | func |
|---|---|---|---|---|---|
| 0 | `__dyld_register_func_for_add_image` | `0x2be07921` | 13 | `__dyld__NSGetExecutablePath` | `0x2be0776d` |
| 1 | `__dyld_register_func_for_remove_image` | `0x2be0795d` | 14 | `__dyld_dyld_register_image_state_change_handler` | `0x2be08819` |
| 2 | `__dyld_dladdr` | `0x2be084d1` | 15 | `__dyld_register_thread_helpers` | `0x2be088c9` |
| 3 | `__dyld_dlclose` | `0x2be08441` | 16 | `__dyld_fork_child` | `0x2be07999` |
| 4 | `__dyld_dlerror` | `0x2be08579` | 17 | `__dyld_moninit` | `0x2be079f1` |
| 5 | `__dyld_dlopen` | `0x2be07e71` | 18 | `__dyld_make_delayed_module_initializer_calls` | `0x2be08909` |
| 6 | `__dyld_dlsym` | `0x2be085e5` | 19 | `__dyld_set_variable` | `0x2be0893d` |
| 7 | `__dyld_dlopen_preflight` | `0x2be07ab5` | 20 | `__dyld_add_image_reexport` | `0x2be08969` |
| 8 | `__dyld_image_count` | `0x2be077cd` | 21 | `__dyld_get_all_image_infos` | `0x2be08809` |
| 9 | `__dyld_get_image_header` | `0x2be07801` | 22 | `__dyld_fast_stub_entry` | `0x2be03589` |
| 10 | `__dyld_get_image_vmaddr_slide` | `0x2be07849` | 23 | `__dyld_image_path_containing_address` | `0x2be08871` |
| 11 | `__dyld_get_image_name` | `0x2be078d9` | 24 | `__dyld_shared_cache_some_image_overridden` | `0x2be088b9` |
| 12 | `__dyld_get_image_slide` | `0x2be07891` | 25 | `__dyld_process_is_restricted` | `0x2be043a5` |

Both the slot and the table are covered by dyld's **own** local relocations (LC_DYSYMTAB `locreloff 139264`, `nlocrel 514`): `0x21aec` is present with `r_extern=0, r_symbolnum=1 (__TEXT,__text)`, and 52 relocations fall inside `0x210c0..0x211a0` (the table). dyld slides these itself during `dyldbootstrap::start`. The kernel is not in this path at any point — and even a total kernel failure to act here would leave the slot *valid but unslid*, never NULL.

So the hypothesis "dyld dispatches through a kernel-populated table whose slot is NULL" is answered by case 4 of the brief: it is dyld's own `__dyld_func_lookup` self-table, populated by dyld from its own Mach-O, and the kernel is not the culprit.

## 5. What the `udf` at `0x2be010c4` actually is — dyld's own abort, not a bad dispatch

`0x2be010c4` = `_dyld_fatal_error` (`--macho --syms`: `2be010c4 l F __TEXT,__text _dyld_fatal_error`). Every in-image reference to it goes through `dyld::halt(char const*)`:

```
2be01ebc  push {r4,r5,r7,lr}              @ __ZN4dyld4haltEPKc
2be01ebe  mov  r5, r0                     ; r5 = the halt() ARGUMENT (the ONLY write to r5 in the body)
2be01ece  bl   __ZN4dyld3logEPKcz          ; logs the "dyld: %s\n" family string (r1 = r5)
2be01ee0  add  r4, pc                     ; r4 = _error_string (__common @0x2be49564)
2be01ee4  bl   _strlcpy                    ; COPIES the message into r4 ; r2 = 1024
2be01efc  bl   __Z19setAlImageInfosHaltPKcm ; stores the COPY's pointer at __all_image_infos + 0x20
2be01f00  mov  r0, r4                      ; r0 = r4 = the COPY
2be01f02  blx  _dyld_fatal_error           ; <-- the only caller; at the udf r0 = the COPY, r5 = the argument
```

and the message handed to `halt()` at each of the 13 sites is decodable from the file (movw/movt + `add r0,pc`):

| halt caller | message |
|---|---|
| `dyld::_main` `2be04ad6` | `libdyld.dylib support not present for LC_MAIN` |
| `dyld::_main` `2be04b60` / `2be04b7c` | `could not load inserted library '%s' because %s\n` / `...'%s'\n` |
| `dyld::_main` `2be04be0` | `dyld: launch failed\n` |
| `dyld::bindLazySymbol` `2be03554`, `fastBindLazySymbol` `2be0369a` | `dyld: lazy symbol binding failed: %s\n` |
| `fastBindLazySymbol` `2be0365c` | `dyld: %s\n` |
| `unimplemented()` `2be07aae` | `unimplemented dyld function\n` |
| `_abort` / `_exit` / `__stack_chk_fail` / `std::vector::_throw_length_error` | `dyld calling abort()\n`, `exit()`, `stack buffer overrun`, `std::vector<>::_throw_length_error()` |

`setAlImageInfosHalt` (`2be08d04-2be08d17`) is three instructions: `gAllImageInfos->errorMessage = msg` (`str r0,[r3,#32]`) and `->terminationVersion = 1` (`str r1,[r0,#36]`); `dyld::halt` then re-loads r4 into r0 (`2be01f00: mov r0, r4`) and traps. **Therefore at the `udf`, `r0`/`r4` = `_error_string`** — the `strlcpy` **copy** of the message (reading r0 recovers the same text) — **while the `halt()` argument itself stays in `r5`** (`2be01ebe: mov r5, r0`, callee-saved, written once and never reloaded; the only other mentions, `2be01ec8`/`2be01ed6: mov r1, r5`, are reads), and it is the **copy's** pointer that `setAlImageInfosHalt` stores at `__all_image_infos + 0x20`.

The udf is a *sequential* abort, not a mis-dispatch: nothing in the file branches to `0x2be010c4` except `dyld::halt`, and the `ldr pc,[pc,ip]` path provably lands in `lookupDyldFunction` whose miss path is benign (`0x2be07a90: movs r1,#0; movs r0,#0; str.w r1,[r8]` — returns 0 with a NULL out-param, no trap).

Note the repeat count is a *kernel* artifact, not 236 crashes: `osfmk/arm/trap.c:906` prints the `usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x%08x` dump and `doexception()` at `:948` re-delivers the same state, so the log shows the same pc/frame N times (the same shape as 991's 231× repeated `0xffff4090` data abort).

## 6. The tree already answers "which halt" — on another board

`osfmk/arm/trap.c:71-126` (pinned `601a463933c0`) is a purpose-built probe, `IOS7LEO_DYLD_BEGIN`..`_END`, gated by `#if defined(BOARD_CONFIG_QSD8250_LEO)` (the HD2/Leo board, `PlatformConfigs.h:48`; D13/CANCRO is `BOARD_CONFIG_MSM8974_CANCRO`, `:42`):

```c
static boolean_t ios7leo_dyld_matches(thread_t thread,arm_saved_state_t *state,uint32_t instruction)
{
    uint64_t info;
    if(!thread || !thread->task || !thread->task->bsd_info ||
       (state->cpsr & 0x3fU)!=0x10U || instruction!=0xfedeffe7U ||
       proc_pid(thread->task->bsd_info)!=1 ||
       strcmp(proc_name_address(thread->task->bsd_info),"launchd")!=0 ||
       thread->task->all_image_info_size!=0xa4U) return FALSE;
    info=thread->task->all_image_info_addr;
    /* Current pinned dyld offsets; no assumed ASLR slide or direct user dereference. */
    if(info<0x20bc8U+0x1000U || info>=0x40000000ULL ||
       info+0xa4U>0x40000000ULL || info+0x278d8U+512U>0x40000000ULL) return FALSE;
    return state->pc==info-0x20bc8U && state->r[0]==info+0x278d8U;
}
```

`instruction != 0xfedeffe7U` is the little-endian byte string of `e7ffdefe` — **the same `udf #65006`**. The witness, when it matches, `copyinstr(state->r[0], ...)` and prints `dyld> ...` lines (`:101-124`), i.e. it exists precisely to lift the `dyld::halt` message out of `r0`.

Its two magic offsets are exactly this dyld's layout, verified independently in the file:

- `all_image_info` section `addr 0x2be21c8c` (size `0xa4`, section table above) → `info - 0x20bc8 = 0x2be010c4` = `_dyld_fatal_error`, and `info + 0x278d8 = 0x2be49564` = `__common`, the `strlcpy` buffer (`r4 = pc(0x2be01ee4) + 0x47680 = 0x2be49564`).
- Our own log corroborates the third precondition: `dyld_all_image_info_addr: 0x2be70c8c   dyld_all_image_info_size: 0x000000a4` (`/tmp/cancro-last_kmsg-991.txt:4102`), i.e. slide `0x4F000` and the section grew by exactly the slide.

So the crash is **the same event the HD2 board already instrumented**: pid 1 `launchd` executing the `_dyld_fatal_error` trap after `dyld::halt()`. On D13 the LEO block compiles out, so the tree prints the raw register dump (`trap.c:906-930`) and drops the message.

**Predicted next reading (slide-invariant, from the file alone).** With `A = task->all_image_info_addr` (printed by `trap.c:919`): the crashing pc is `A - 0x20bc8`, and `r0 = A + 0x278d8` is the `_error_string` copy (the `strlcpy` buffer holding the message — the same pointer `setAlImageInfosHalt` records at `__all_image_infos + 0x20`); the `halt()` argument's own pointer is `r5`. For the 991-era slide that is pc `0x2be500c4` and r0 `0x2be98564`. 993's press measured the formula directly at its own slide: `A = 0x2be9ec8c` ⇒ r0 `0x2bec6564` (`_error_string`, reading `throwf, out of memory error`) and r5 `0x2be997fd` = static `0x2be1c7fd` + slide. Reading the string is a `copyinstr` of r0 — a D13 equivalent of the LEO witness (`trap.c:101-124`) is the minimal way to get it; nothing in the kernel needs to be *fixed*, only asked.

Corollary: the "`pc = [0x2be21af0]` = a NULL slot" reading of the crash is not the failure. If a future run really did dispatch through a bad slot, the symptom would be a fault at the *loaded* address (or at `_lookupDyldFunction_ptr`), never an `EXC_BAD_INSTRUCTION` at `0x2be010c4` with `r0` pointing at a message buffer.