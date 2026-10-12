# 995 - the dyld halt-message witness (`STAGE90_XNU_DYLD_WITNESS`)

**Status: spec + tools only. NOTHING IS STAGED.** The tree edit is written by
`tools/patch_d13_dyld_witness.py` and is proven OFF-byte-identical before it writes, but no rung has
run it against `external/xnu-hd2-darwin13` yet, no image has been built, no device has been touched
and no commit has been made. What follows is the recipe 995's build should follow, with every
load-bearing value re-measured from the artifacts that exist today.

**Revision 2 (2026-10-12, team-lead's dyld-log-sink ruling, `d13-dyld-halt-message-recovery.md` §9).**
Two additions to the witness, both in the same trap and the same two print slots: (a) the message is
read from **both** live pointers - `A + 0x20` and `state->r[5]` - and the witness states whether they
are equal (unequal ⇒ another error; never silent); (b) `state->r[8]` is classified by the exception
block's signature pair. The tool was not frozen (nothing had been staged), so rev 2 is in the patcher
itself, not a delta on top. Everything below is the rev-2 spec.

## Why 995 at all

994's press (arm `armed-d13-8b4c9a3e`, commit `a4a2532`, evidence
`out/stage90/captures/cancro-last_kmsg-994-armed-d13-8b4c9a3e.txt`) confirmed the trap-10 wire fix
(`IOS7LAB vm_allocate64 size=00000000:00001000 flags=1 result=0 address=000e1000`, log:4101) and moved
the frontier one step further than expected: dyld still halts, but now at its **real** error.

* `:4103` onward, 235x: `usermode undefined instruction, EXC_BAD_INSTRUCTION at 0x2beb90c4`, slide
  `0xB8000`, i.e. pc `0x2be010c4` = `_dyld_fatal_error` = `fe de ff e7` = `udf #0xdefe`.
* the register block's `r5` is `0x2bedc390` = `__ZL18initialPoolContent + 0x11e0` (pool symbol
  `0x2be231b0`) - a **heap string in dyld's malloc pool**, i.e. a message dyld formatted, not 993's
  static out-of-memory literal.
* the chain is `throwf` -> `__simple_salloc` = `vm_allocate(..., 4096, VM_FLAGS_ANYWHERE)` **succeeds**
  (`:4101`) -> `vsprintf` formats -> `strdup` into the pool (`r5`) -> `__simple_sfree` releases the
  salloc buffer (`:4102`) -> `__cxa_throw` -> `_main`'s `catch (const char*)` fatal pad (`r6 = 2`) ->
  `halt(r5)` -> `_dyld_fatal_error`.

So dyld's error reporting works again; the message is simply **unreadable**, because the tree's only
reader for it is compiled out on CANCRO. 995 opens a reader.

## The read, proven by value

Everything below was measured, not assumed. dyld is **Thumb**: `llvm-objdump-14 -d` decodes it as ARM
and returns nonsense; the decode needs `--triple=thumbv7-apple-ios`.

| what | value | how it was measured |
|---|---|---|
| `__DATA,__all_image_info` | `0x2be21c8c`, size `0xa4`, version `0x0e` | Mach-O section headers of `/tmp/dyld_ios7` |
| `gProcessInfo` (the pointer variable) | `0x2be21b1c`, holds `0x2be21c8c` | file bytes at `0x21b1c`: `8c 1c e2 2b` |
| `setAlImageInfosHalt` | `0x2be08d04` | `llvm-objdump-14 --syms`; 6 instructions: `ldr r3,[r2]` (r2 = `0x2be08d10 + 0x18e0c` = `0x2be21b1c`), `str r0,[r3,#32]`, `str r1,[r0,#36]` |
| `dyld::halt` | `0x2be01ebc` | `push {r4,r5,r7,lr}` / `mov r5, r0` (`:0x2be01ebe` - **r5 is the message, callee-saved**), `dyld::log("dyld: %s\n", msg)` (r0 = `0x2be01ece + 0x1aec6` = `0x2be1cd94`), `strlcpy(_error_string, msg, 1024)` (r4 = `0x2be01ee4 + 0x47680` = `0x2be49564`), `setAlImageInfosHalt(_error_string, byte ^ 1)` with byte@`0x2be219f3` = `0x00`, then `blx _dyld_fatal_error` |
| `_error_string` | `0x2be49564`, `__DATA,__common`, `0x428` bytes | `--syms`; the `strlcpy` destination |
| `_dyld_fatal_error` | `0x2be010c4` = `fe de ff e7` = `udf #0xdefe` | raw bytes at file offset `0x10c4` |
| the exception signature pair | `0x432b2b00` at `object-0x20`, `0x434c4e47` at `object-0x1C` | `___cxa_throw` `0x2be148e4` `movw r0,#20039; movt r0,#17228` (0x434c4e47) `str r0,[r4,#-28]`; `0x2be148fa` `movw r0,#11008; movt r0,#17195` (0x432b2b00) `str r0,[r6,#-32]!` with r6 = r4 - 32 |

Consequences, each of which the witness depends on:

1. **`task->all_image_info_addr` is dyld's own `gProcessInfo` struct.** The kernel sets it in
   `bsd/kern/mach_loader.c:249` `note_all_image_info_section()` (`result->all_image_info_addr =
   <section addr>` at `:266`, `+= slide` at `:268`), and dyld's `gProcessInfo` holds the same address,
   so the value `trap.c:919-920` prints (`dyld_all_image_info_addr`) IS the struct the witness reads.
   994's arithmetic closes: `0x2be21c8c + 0xB8000 = 0x2bed9c8c`.
2. **`A + 0x20` is the message pointer.** `setAlImageInfosHalt` writes it there (`str r0, [r3, #32]`,
   with `A + 0x24` = the flags word, `str r1, [r0, #36]`). 994's `r0 = r4 = 0x2be49564` and `r1 = 1`
   are exactly this call's arguments, seen in the dump.
3. **`A + 0x20` points at `_error_string`** (a 1024-byte private RW `__common` buffer), because
   `dyld::halt` `strlcpy`s the message into it first. **`r5` is the same text under the other
   pointer** - the `strdup`'d pool string `halt()` was handed - still live at the `udf` because
   `dyld::halt` keeps it in a callee-saved register and `_dyld_fatal_error` is a bare `udf` that
   clobbers nothing. Both reads are used, and compared.
4. **Reaching the witness already proves the trap is dyld's fatal `udf`**: the only path past
   `trap.c:877` is `instruction == 0xfedeffe7`, i.e. `udf #0xdefe`, the encoding dyld uses for
   `_dyld_fatal_error` and nowhere else on this path. (Doc §1: `dyld::halt` is its **only** caller.)

## Wiring point

**`external/xnu-hd2-darwin13/xnu/osfmk/arm/trap.c`, inside `sleh_undef` (function at `:802`), inserted
immediately after the `dyld_all_image_info_addr` printf's continuation line (`:920`) and before the LEO
marker at `:921`.** That is the same path that prints A, so the injected call's first argument and the
printed value are one expression (`thread->task->all_image_info_addr`), and its second argument,
`arm_ctx`, is the same saved state the register block above printed (`r5`/`r8` are the dump's own
numbers). The witness function itself goes after the LEO block's `/* IOS7LEO_DYLD_END */` (`:126`),
before `typedef enum {` (`:127`).

Staged, the file is 1113 lines (was 955): the block occupies `:127-279` (153 lines), the call
`:1074-1078` (5 lines), and the diff is exactly those two insertions.

### Gate line style

```c
/* IOS7WITNESS_BEGIN */
#if defined(STAGE90_XNU_DYLD_WITNESS) && STAGE90_XNU_DYLD_WITNESS
        ios7wit_dyld_message(thread->task, arm_ctx);
#endif
/* IOS7WITNESS_END */
```

Two notes on this, both deliberate:

* **`|| (defined(...) && ...)` does not apply here.** That spelling is for *opening an existing gate*
  (994's `mach_kernelrpc.c:39` is `#if IOS7_COMPAT_PLATFORM || (defined(STAGE90_XNU_IOS7_TRAPS) &&
  STAGE90_XNU_IOS7_TRAPS)`). The witness is a *new* block with no gate to open, so it uses the
  fresh-block spelling the same lane already uses (`ios7_compat.h:35`, `mach_kernelrpc.c:279`). The
  property the house style protects - `defined()` evaluated FIRST, so an unset switch never evaluates
  the identifier (`mi4-off-option-two-spellings`) - is preserved verbatim.
* **The LEO gate is NOT reused.** Waking `#if defined(BOARD_CONFIG_QSD8250_LEO)` on CANCRO would drag
  in `ios7leo_*`, its observation counters and its `#include "ios7lab_fault_witness.h"`, which 994's
  own hard requirement forbids on this board.

### What is inserted

The full text is `BLOCK`/`CALL` in `tools/patch_d13_dyld_witness.py` (153 + 5 lines). In outline:

```
#define IOS7WIT_ERROR_MESSAGE_OFF   0x20U   /* dyld_all_image_infos.errorMessage, v0x0e */
#define IOS7WIT_MIN_INFO_SIZE       0x24U
#define IOS7WIT_MAX_PRINTS          2U
#define IOS7WIT_MAX_TEXT            256U
#define IOS7WIT_LINE                48U
#define IOS7WIT_EH_W0               0x432b2b00U   /* the exception signature pair, see the table */
#define IOS7WIT_EH_W1               0x434c4e47U
#define IOS7WIT_OBJ_BACK            0x20U
#define IOS7WIT_BASE_FWD            0x30U

static volatile UInt32 ios7wit_prints;

__attribute__((noinline)) static void ios7wit_dyld_message(task_t task, arm_saved_state_t * state)
{
    ...
    if (!task || !state || !task->bsd_info) return;
    if (task->all_image_info_addr == 0) return;
    if (task->all_image_info_size < IOS7WIT_MIN_INFO_SIZE) return;
    if (ios7wit_prints >= IOS7WIT_MAX_PRINTS) return;      /* claimed BEFORE any I/O */
    ++ios7wit_prints;

    /* (1) A + 0x20 -> the strlcpy'd _error_string copy               */
    kr = copyin((user_addr_t)(task->all_image_info_addr + IOS7WIT_ERROR_MESSAGE_OFF), &msgp, 4);
    ...  err = copyinstr(msgp, field, 256, &copied);
    /* (2) r5 -> the strdup'd pool string, same text                    */
    err5 = copyinstr((user_addr_t)state->r[5], other, 256, &copied5);
    /* (3) the verdict, printed on every path                           */
    verdict = (ok && ok5) ? (equal ? "matches errorMessage" : "DIFFERS from errorMessage")
                          : "unreadable, no comparison";
    /* (4) the r8 classification                                        */
    ...  16-byte copyin at r8-0x20 == the pair -> "exception object (signature at r8-0x20)"
    ...  16-byte copyin at r8+0x30 == the pair -> "exception block base (signature at r8+0x30)"
    ...  else copyinstr(r8) readable           -> "message interior len=<n>" + dyld8> lines
    ...  else                                  -> "stale probe=0x.... copyin=..,.. err=.."
}
```

Print shape (exact literals, so a checker can find them by value):

```
IOS7WITNESS dyld msg=0x2bf01564 copyinstr=0 len=63
IOS7WITNESS r5 msg=0x2bedc390 copyinstr=0 len=63
IOS7WITNESS r5 matches errorMessage
dyld> Library not loaded: /System/Library/Frameworks/...
dyld5> Library not loaded: /System/Library/Frameworks/...
IOS7WITNESS r8=0x2bedc428 exception object (signature at r8-0x20)
```

Every failure branch prints its reason: `dyld> <no message pointer>`, `dyld> <copyinstr failed>`,
`dyld> <empty>`, `dyld5> <copyinstr failed>`, `dyld5> <empty>`, and the `stale` line. The three
verdicts are `matches errorMessage`, `DIFFERS from errorMessage`, `unreadable, no comparison`.

### Rulings

* **The LEO magic checks are DROPPED** (`info - 0x20bc8`, `info + 0x278d8`, `all_image_info_size ==
  0xa4`, `proc_pid == 1`, `strcmp(name, "launchd")`). By value they are *addresses* of two symbols in
  one dyld build: `A - 0x20bc8` = `_dyld_fatal_error` and `A + 0x278d8` = `_error_string`. Their only
  semantic content - "this pc is dyld's fatal udf, and r0 is where the message lives" - is already
  guaranteed (the `udf #0xdefe` gate) or replaced by the read itself (the pointer comes *out of*
  `A + 0x20`). Keeping them would add a silent-skip mode whenever dyld moves, which is exactly
  `mi4-silence-is-a-reading-only-if-success-is-silent`. The size gate becomes the field's true
  requirement, `>= 0x24`.
* **Two pointers, one text, compared in the trap.** `A + 0x20` is the ABI route (register-free) and
  `r5` is the register route; at this trap they must describe the same halt. The witness prints both
  strings and a verdict line on every claimed slot, so "the field and the register disagree" is itself
  a reading, not a silence.
* **The r8 probe is a classification, not a dereference.** `r8` is inherited from an outer frame
  (`d13-dyld-halt-message-recovery.md` §9.3), and the only three readings that land on something have
  distinct, testable signatures; a fourth outcome - all-zero or all-failing - is reported as the stale
  value it then is. Nothing in the probe assumes the value is meaningful.
* **Rate limit = 2, claimed before any I/O.** The halt re-executes forever (235 identical dumps in
  994) and the OS console block is 128 KiB and drops silently past the end
  (`src/entry/entry_stubs.c:2506`, drop at `:2762-2770`). After two slots, the remaining ~233 traps
  cost one load and one compare each. D13 starts only the boot CPU, so the counter cannot race.
* **Fail-soft, absolutely.** No direct user dereference anywhere (every user read is `copyin` /
  `copyinstr`; `copyin` runs on the current task's map, so a kernel VA or an unmapped word can only
  return an error), no panic, no allocation, no sleep, no lock, no loop that is not bounded by the
  buffer. `copyin(nonzero) != 0 || msgp == 0` short-circuits before `copyinstr`; `r8 - 0x20` wraps
  harmlessly on a bogus `r8` and the wrapped address simply fails its copyin.
* **Print budget**: per claimed slot at most `2 headers + 6 + 6 text lines + 1 verdict + 7 r8 lines =
  22` lines (the text cap is 255 bytes at 48 bytes/line), so 2 slots ≤ 44 lines ≈ 4.4 KiB out of
  128 KiB, arriving at the frontier (log:4103), i.e. far from the block's end.

## Build wiring (owed - not in this deliverable)

`osfmk/arm/trap.c` is a **kernel-pool** file; the linked image only LINKS
`out/xnu_kernel_obj_d13/osfmk_arm_trap.o`, so a `-D` is required:

1. `tools/build_xnu_arm_kernel.sh` - add, after the 994 line at `:806`:
   `[[ ${STAGE90_XNU_DYLD_WITNESS:-0} -eq 1 ]] && DEFINES+=(-DSTAGE90_XNU_DYLD_WITNESS=1)`
   (the `DEFINES` array, not `EXTRA_DEFINES` - same as SP_ALIGN `:772`, VMHINT `:780`, COMMPAGE
   `:790`, IOS7_TRAPS `:806`).
2. `scripts/preflight_boot_check.sh` - add `STAGE90_XNU_DYLD_WITNESS` to `ENTRY_CFG_KEYS`
   (`:716-740`) **and** a back-compat clause for a record that predates it, copying the shape at
   `:954-1022` ("an absent key is not a missing value but the value - the switch did not exist"):
   every parked arm's record lacks the key, and demanding it would make each of them unpressable.
3. `scripts/build_995.sh` / `press_995.sh` / `park_995.sh` - 994's switch set verbatim plus
   `STAGE90_XNU_DYLD_WITNESS=1`, exported as an environment variable **and** passed in
   `XNU_KERNEL_EXTRA_DEFINES` (two spellings of one value, `mi4-one-value-two-definitions`).
4. `tools/stage_d13_dyld_witness.sh` - a driver mirroring `tools/stage_d13_ios7_traps.sh`, calling
   `tools/patch_d13_dyld_witness.py`; the tree is a re-provisionable checkout, so a hand edit would be
   lost.
5. **Freeze the 994 elf before the rebuild**: `cp out/stage90/xnu_arm_entry.elf
   out/stage90/captures/994-xnu_arm_entry.elf` - it is the only valid OFF control (see below).

## Tooling

### `tools/patch_d13_dyld_witness.py <xnu-tree-root> [--dry-run|--verify-off]`

Idempotent and refusing-by-construction, in `patch_d13_ios7_traps.py`'s discipline: both anchors are
exact two-line windows with an asserted count of 1 (`/* IOS7LEO_DYLD_END */\ntypedef enum {` and
`...all_image_info_size);\n/* IOS7LEO_DYLD_BEGIN */` - neither single line is unique in the file), an
identity check on `sleh_undef` and the aii-format string, 0 markers = stage / 2 markers = "nothing to
do" / anything else = refuse with the file untouched, and a partial or foreign edit is refused rather
than completed.

**The OFF byte-identity proof runs BEFORE the write**, with the real preprocessor over the whole file:
four `clang -E -P` runs (HEAD, staged with the switch undefined, staged `=0`, staged `=1`) must give
three byte-identical outputs and a fourth, different one that is the only one carrying the witness.
`#include`s resolve through empty placeholder headers; that is faithful because the script *scans*
trap.c's own directives and refuses the proof if they ever evaluate anything outside
`{BOARD_CONFIG_QSD8250_LEO, BOARD_CONFIG_ARMPBA8, STAGE90_XNU_DYLD_WITNESS}` (today's set, printed on
every run). Comments cannot be used as fingerprints here - `clang -E` strips them, so the ON/OFF
detection uses code and string-literal tokens.

Measured self-test (sandbox copy in `/tmp/wit_sandbox2`, the tree untouched):

```
$ python3 tools/patch_d13_dyld_witness.py /tmp/wit_sandbox2 --dry-run
  conditional identifiers in the file: BOARD_CONFIG_ARMPBA8, BOARD_CONFIG_QSD8250_LEO, STAGE90_XNU_DYLD_WITNESS
  OFF (STAGE90_XNU_DYLD_WITNESS undefined): preprocessed output is IDENTICAL to HEAD (18021 chars)
  OFF (STAGE90_XNU_DYLD_WITNESS = 0): preprocessed output is IDENTICAL to HEAD (18021 chars)
  ON  (STAGE90_XNU_DYLD_WITNESS = 1): preprocessed output carries the witness = True; the three OFF outputs carry none = True
patch_d13_dyld_witness: OFF byte-identity proof PASS
  --dry-run: would insert 153 witness lines + 5 call lines at the two anchors; nothing written
```

applied: 955 -> 1113 lines, the diff is **insertion-only** (0 deleted lines), block `:127-279`, call
`:1074-1078`, braces balanced, `--verify-off` PASS on the staged text, and a second run prints
`nothing to do`.

### `tools/check_d13_dyld_witness.py [<elf>] [--expect on|off] [--control ELF]...`

By value, decoding the ARM encodings itself (`clang -marm` builds this pool), never a comment:
`nm` for symbols, `objdump -d` for code, the file's bytes for the literals.

* ON asserts: `ios7wit_dyld_message` exists; `sleh_undef`'s region (bounded by the next GLOBAL text
  symbol) calls it; that call follows a `bl printf` in the same region (the dump path); the witness's
  own region calls `copyin` **at least 3 times** (the A field read and the two r8 signature windows)
  and `copyinstr` **at least 3 times** (the A text, the r5 text, the r8 probe) - a build that dropped
  one of the reads FAILs here; the region names `#32` (the A field offset / r8 back-offset) and `#48`
  (the r8 base-forward offset / the 48-byte stride) as exact immediates - necessary, not sufficient;
  and the bytes carry the whole vocabulary: `IOS7WITNESS dyld msg=`, `IOS7WITNESS r5 msg=`, the three
  verdicts, the three line tags, the three r8 classification lines and the `stale probe=` fallback.
* OFF asserts the mirror **positively**: no witness symbol, none of `IOS7WITNESS`/`dyld> `/`dyld5> `/
  `dyld8> ` bytes, `sleh_undef` still present and still calling `printf` exactly 3 times (the fault
  header, the register block and `dyld_all_image_info_addr`), and the aii format string still in the
  image.
* `--control ELF` demands that each control FAIL ON and PASS OFF, so a control the checker cannot tell
  apart is reported as a failure instead of a pass.

Measured:

```
$ ... out/stage90/xnu_arm_entry.elf --expect off   -> PASS (7/7; "sleh_undef (0x8046b72c) still calls printf 3 time(s)")
$ ... out/stage90/xnu_arm_entry.elf --expect on    -> FAIL (no witness symbol)
$ ... --expect off --control out/stage90/xnu_arm_entry.elf -> control discriminates (on FAILs, off PASSes)
$ ... /tmp/wit_on_probe/probe.elf --expect on      -> PASS (25/25)
$ ... /tmp/wit_on_probe/probe.elf --expect off     -> FAIL
```

The ON direction is exercised on a **linked synthetic probe** built in `/tmp/wit_on_probe` from the
patcher's own `BLOCK` text with the pool's exact compile line
(`clang --target=armv7-unknown-netbsd-eabi -mcpu=cortex-a15 -marm -mfpu=neon-vfpv4
-mfloat-abi=softfp -ffreestanding -fno-builtin -fno-common -fno-pic -O2`) and `arm-none-eabi-ld`.
That is self-test evidence for the checker and a codegen smoke test for the block, **not** evidence
about 995's image: among other things it measured that clang keeps all three `copyin` and all three
`copyinstr` call sites and materialises `add r0, r1, #32` for the field offset. It also found and
fixed a checker bug: objdump left-pads short addresses (`   4:`), which the instruction regex did not
tolerate - the 994 image masked it because every address there is 8 digits wide.

**The older frozen images are NOT valid controls**: `out/stage90/captures/594-xnu_arm_entry.elf` and
`535-xnu_arm_entry.elf` have `sleh_undef` but it calls `printf` 0 times and they carry no
`dyld_all_image_info_addr` format string (they predate the dump). The checker refuses them as
controls, which is the correct behaviour.

## Pre-registered reading for 995's press

On the first two `udf #0xdefe` traps the log should carry, between the register dump and the next
dump: two `IOS7WITNESS ... msg=` headers, `dyld> `/`dyld5> ` text lines, a verdict line, and an r8
line. Expected text: a real dyld failure - the by-value candidates are `recursiveLoadLibraries`'s
`Library not loaded: %s` (`dyld` 0x2be0a35e) or a `mach_error_string` (`0x2be09220`).

The r8 line maps straight onto `d13-dyld-halt-message-recovery.md` §9.3's four hypotheses:

| line | hypothesis | consequence |
|---|---|---|
| `exception object (signature at r8-0x20)` | #2, r8 = `base+0x50` | `strlen(msg)` should be 68..71 |
| `exception block base (signature at r8+0x30)` | #1, r8 = the block base | `strlen(msg)` should be 148..151 |
| `message interior len=<n>` | #3, r8 is inside the message | `strlen(msg)` >= 148; the bytes name the part |
| `stale probe=0x00000000 copyin=..,.. err=..` | #4 | r8 is a pool tail/stale value - drop it from any further reasoning |

FALSIFICATION: no `IOS7WITNESS` line at all ⇒ the witness was never called (wrong pc path, or the
switch did not reach `osfmk_arm_trap.o` - check with the checker); a `DIFFERS from errorMessage`
verdict ⇒ the field and the register do not describe the same halt, i.e. something else wrote
`A + 0x20` or `r5` is not halt's argument - report it rather than trusting either string; a header
with `copyinstr=<nonzero>` on both reads ⇒ `A + 0x20` is not the message pointer in this dyld.

## Open points

1. **The checker's ON direction has no real built image yet.** The synthetic probe above exercises
   its code paths and the block's codegen, but 995's build owes the first `--expect on` on an image
   that also passed `build_xnu_arm_kernel.sh`.
2. **Budget ordering.** If the first two `udf #0xdefe` traps came from a process whose `A + 0x20` is
   empty or unrelated, the budget would be spent before launchd's message. Every claimed slot prints
   its headers and its verdict, so that outcome is *visible* rather than silent; the cheap fixes if it
   happens are raising `IOS7WIT_MAX_PRINTS` or only claiming a slot once a read succeeded.
3. **The `#32`/`#48` assertions are necessary, not sufficient** - they prove the constants are in the
   code, not that they are added to the right addresses. The call-site, call-count and string checks
   are what pin the reads. A build that materialises 0x20/0x30 differently would FAIL loudly (the safe
   direction).
4. **Nothing here changes why the process never dies.** `_dyld_fatal_error` re-executes forever
   because the Mach exception path cannot terminate a task with `bsd_info`; see
   `docs/reference/d13-user-udf-retry-mechanism.md`. 995 makes the message readable, not the halt
   survivable.