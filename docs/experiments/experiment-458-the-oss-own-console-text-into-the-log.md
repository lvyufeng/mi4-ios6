# Experiment 458 — the OS's own console text into the log, and why its console had never been visible

**Status: built, gated, run on hardware — the OS's own words are in the log, and the two reasons they
never were are now measured.** Six experiments have
read the boot through wrappers this project wrote: `xnu_live_*` records, counters, state words. Every
one of those words is *this* instrument's. The OS's own `printf`/`IOLog` output — the text XNU
produced about itself — has gone somewhere this image could not see, and 457's run is the proof: a
60-second wait for a device that does not exist, and not one character of the OS's own text anywhere
in 306521 bytes of log. This step puts the two sinks that text leaves through behind wrappers, so
that what XNU says about itself lands in the ram console beside the records.

## What the step adds, and where it came from

| piece | file | why it is that and not something else |
| --- | --- | --- |
| the derivation | `entry_stubs.c`'s 458 block | every character the OS's console prints ends in `cons_ops[cons_ops_index].putc` (`osfmk/console/serial_console.c:294-296`), whose two entries are `_serial_putc` -> `serial_putc` -> `uart_putc` and `vcputc` — so the sinks are those two functions, and nothing else needs wrapping |
| the video sink | `entry_trace.c`'s `__wrap_vcputc` | reached **only** through the ops table, i.e. through a reference that is an *address*, which `--wrap` rewrites like a call — and `build_entry.sh` reads `cons_ops[1].putc` out of the linked image and fails the build unless it holds `__wrap_vcputc` |
| the serial sink | `entry_trace.c`'s `__wrap_uart_putc` | **not** `serial_putc`, and the image is why: `PE_init_kprintf` stores `serial_putc` into `PE_kputc` with a `movw`/`movt` pair against a symbol its own object defines (`pexpert/arm/pe_kprintf.c:36-40`), which `--wrap` cannot reach, so a wrapper on `serial_putc` would miss the one sink `kprintf` and `panic` print through. `serial_putc` is a tail call into `uart_putc` in another object, so wrapping `uart_putc` catches the console route, kdp's two `pal_serial_*` and the stored pointer alike, and `build_entry.sh` reads that tail call out of the image too |
| the capture | `entry_stubs.c`'s `entry_os_console_char`, `entry_os_reserve` | it takes one 128 KB **block** out of the ram console's cursor on the first character and space-fills it, so the records — which append at that same cursor — are above the block from then on and neither writer can reach the other's bytes |
| the tank | `entry_stubs.c`'s `g_os_tank` | 8 KB of `.bss` for text that arrives before the live channel's page tables are installed; `xnu_live_ostext_tank`/`_dropped` report whether it was needed |

## The prediction, written before the build and before the run

1. **The state record appears, with the run's own answer to "why was none of this visible".**
   `xnu_live_console_opsidx = 0x00000001` — `cons_ops_index` is initialised to `VC_CONS_OPS` and
   `switch_to_serial_console()` is called only when the `serial` boot-arg sets `SERIALMODE_OUTPUT`,
   which the stage90 boot-args do not carry. `xnu_live_console_noserial = 0x00000001` —
   `disable_serial_output` is initialised `TRUE` (`pexpert/arm/pe_kprintf.c:20`) and cleared only for
   `debug & DB_KPRT`, and `debug=0x144` is `DB_NMI|DB_ARP|0x100`, no `0x8`. That one word is also the
   reason prediction 3 is what it is. `xnu_live_console_noconout` is the one reading this document
   does not know in advance: `disableConsoleOutput` starts 0 and is set `TRUE` only by `gc_enable
   (FALSE)` / `gc_pause(TRUE, ...)` when `console_is_serial()` is false, which depends on whether the
   video console was ever touched. If it reads `0x00000000` the banner below must be captured; if
   `0x00000001`, `printf` was silenced earlier and the text that survives is `IOLog`'s, which is
   *not* gated by it (`_IOLogv` never tests it, `iokit/Kernel/IOLib.cpp:1170-1185`) — either way the
   route is proven, and the two readings are distinguished in the log rather than argued about.
2. **`xnu_live_console_kputc` is `serial_putc`'s address in this build**, not `cnputc`'s: it is the
   pointer `PE_init_kprintf` stored, so it says whether `serial_init()` found the uart. The value is
   a per-build address; the *claim* is which of the two functions it names.
3. **`xnu_live_ostext_ser = 0x00000000`.** The serial sink is installed and idle: `kprintf` is gated
   off by prediction 1's `noserial = 1`, kdp is not attached, and `_serial_putc` is reached only if
   `cons_ops_index == 0`. This is the negative the counter exists for — it says the wrapper was
   reachable and had nothing to carry, which is a different statement from "the wrapper did not run".
4. **`xnu_live_ostext_vc > 0`, `xnu_live_ostext_chars` in the hundreds at least, and the block
   contains the copyright banner** — `printf(copyright)` is the first statement of `bsd_init`
   (`bsd/kern/bsd_init.c:452`), the string at `:209-212` being
   `Copyright (c) 1982, 1986, 1989, 1991, 1993\n\tThe Regents of the University of California. All
   rights reserved.\n\n` — provided prediction 1's `noconout` is 0. And a detail that is a *fingerprint
   of the route*: the drain writes a `'\r'` after every `'\n'` (`serial_console.c:295-297`), so the
   captured text is CRLF, and no other writer in this project produces that.
5. **The block is self-describing**: `xnu_live_ostext_at` = the size field at the first console
   character (a few hundred KB, the payload's own lines plus the records so far) and
   `xnu_live_ostext_block = 0x00020000`; the last record before the block's text is
   `xnu_live_ostext_tank`.
6. **`xnu_live_ostext_tank = 0` and `xnu_live_ostext_limited = 0`.** The console prints long after the
   entry's tables are live, so nothing waits in `.bss`; and 128 KB is far more than XNU says about
   itself before the frontier, so the block never fills. A nonzero tank would be a real finding: text
   produced during the entry's own window, before XNU's first instruction.
7. **The frontier does not move.** The log still ends inside `IOFindBSDRoot`'s `IOMedia`/`Apple_HFS`
   loop, with the same 60-second deadline record and no new stub hit, no `exception:` and no
   `panic:`. The instrument's job is to be invisible to the boot; if the captured text shows the
   boot reaching somewhere new, that is this step's defect and not a step forward.

Falsifiers, named in advance: (a) `ostext_vc = 0` **and** `ostext_chars = 0` — the console never
printed, and `noconout`/`noserial` say which gate did it (both 0 would mean the wrapper is not on the
console's path at all, i.e. this document's derivation is wrong); (b) `ostext_chars > 0` with
`ostext_total = 0` — the block was never taken, and `ostext_limited` says which refusal (2 = wrong sig
or no room); (c) the banner absent while `IOLog` text is present — `noconout = 1`, the capture still
proves the route, and the missing banner is a *reading* about `gc_enable`, not a failure; (d) a
`stub_hit=` or `exception:` — the console path faulted, which the entry's own fault handler reports
and the watchdog returns; (e) text captured but garbled — the block text is space-filled and written
by one writer at a time under `_cnputs`'s lock, so a garble would mean the records reached into the
block, i.e. the reservation did not hold and the two-writer reasoning above it is wrong.

## The build, and the numbers it made

    xnu_entry_455: 36 --wrap'd symbols: 32 reached by a branch in this image,
                   2 same-object-only (copyExistingServices, matchPassive),
                   1 never called here (sleep), 1 by address only (vcputc)
    xnu_entry_458: the console ops table reads exactly as built -
                   [0].putc=0x8009ab0c (_serial_putc), [0].getc=0x8009ab14,
                   [1].putc=0x8045669c (__wrap_vcputc), [1].getc=0x8009abd0, nconsops at 0x80468570
    xnu_entry_458: serial_putc branches to __wrap_uart_putc, so the pointer PE_init_kprintf
                   stores in PE_kputc reaches the capture

The third line is a check this step needed and did not exist when it was designed: the census asks
whether *some* branch reaches a wrapper, which was true of `serial_putc` and said nothing about the
pointer `kprintf` prints through. `serial_putc` in this image is one instruction — `b __wrap_uart_putc`
at `0x80031ab8`, and that is the image's **only** branch to that wrapper — and `vcputc` is reached by
no branch at all: its wrapper has exactly one reference in the whole image, the ops-table word the
second line reads. The wrapper addresses are `__wrap_vcputc` 0x8045669c (0x44) and `__wrap_uart_putc`
0x804566e0 (0x28); `entry_os_console_char` is at 0x80002e8c.

`.text` 5018624 -> **5020000** (+0x560: `entry_os_console_char` 0x3C8 with `entry_os_reserve` inlined,
the two wrappers 0x6C, the marker and the ten new key strings). `.bss` **0x804fba40 .. 0x8054edd8**,
340888 bytes, of which 0x2024 is this step's — the 8 KB tank at `0x804fceb4` and the counters at
`0x804fcb60`. The copied image still ends at `0x804fba18` and is still **5224984 bytes**: the `.text`
growth is absorbed by the 16 KB alignment gap before `.data`, exactly as in 457. Payload
`e5562bdd...`, 8245248 bytes.

## The run (2026-09-20): the OS names its own frontier, and the text it will never print

    LOGFILE=/tmp/cancro-458-last_kmsg.txt ./run_and_capture.sh --allow-xnu-entry
    wrote 437874 bytes, exit 0, device back on its own

The log grew by 131353 bytes over 457's, which is the 128 KB block. The block was reserved at data
offset **`0x4a9ec`** (`xnu_live_ostext_at`) with `xnu_live_ostext_block = 0x00020000`, and it holds
**176 bytes of text, 169 of them not spaces** — the whole of what XNU said about itself before the
frontier:

    \n[os-console-458]\n
    iBoot version: \n\r
    Waiting on <dict ID="0"><key>IOProviderClass</key><string ID="1">IOMedia</string><key>Content</key><string ID="2">Apple_HFS</string></dict>\n\r

**The second line is the OS naming the frontier 457 could only infer.** It is
`IOLog("Waiting on %s\n", s->text())` at `iokit/bsddev/IOKitBSDInit.cpp:547`, three lines above the
`do { service = IOService::waitForService(matching, &t); } while (!service)` loop — the same
dictionary XNU itself is waiting for, in XNU's own words. And the first line is a real `printf` from
`PE_init_iokit` (`pexpert/arm/pe_init.c:187`, `printf("iBoot version: %s\n", firmware_version)`) with
an **empty** firmware version, which this project's tree supplies none of. It is in the tank because
it arrived before the live channel's first record; the `\n\r` pairs are `_cnputs`'s own fingerprint
(`osfmk/console/serial_console.c:294-297` writes a `'\r'` after every `'\n'`), so the route the text
took is readable in the text itself.

**And the state record answers why none of this was ever visible, with two reasons, both measured.**
`xnu_live_console_opsidx = 0x00000000`: the console is the **serial** one. `xnu_live_console_kputc =
0x8009b1a4` = **`cnputc`**, not `serial_putc` — so `serial_init()` returned 0,
`uart_initted` is 0, and `uart_putc` returns on its first line (`pexpert/arm/pe_serial.c:813-819`):
**the serial sink is a function that does nothing.** `xnu_live_console_noserial = 1` (kprintf is
gated off) and `xnu_live_console_noconout = 0` (printf is not). So XNU's console was wired to the one
sink that cannot print, and the video sink — `vcputc` — returns early on `gc_initialized`, which the
no-framebuffer path never sets. The source of the switch is `initialize_screen`'s no-video branch
(`osfmk/console/video_console.c:2846-2852`): `switch_to_serial_console()`, `gc_graphics_boot =
FALSE`, `disableConsoleOutput = FALSE`, reached from `PE_create_console` at `arm_init.c:365` — one
block that accounts for *both* of the state words this document did not know in advance.

**The copyright banner is not missing because a gate ate it. It was never printed.** In the image,
`bsd_init`'s first statement is `mov r0, #0; bl _consume_printf_args` (`8003d2dc`, `8003d2e0`) — the
`printf` is compiled out: `CONFIG_NO_PRINTF_STRINGS` (`config/MASTER:302`) makes `printf` a no-op in
every file that includes `bsd/libkern/libkern.h` (`:171-173`) or `osfmk/kern/misc_protos.h`
(`:150-152`), which is where `bsd/kern` and most of BSD/OSFMK live, while `pexpert/arm/pe_init.c`
includes neither and calls the real `printf` (`bl printf` at `80006fec`, format string `0x804619d7`).
**What that means for the next steps is the sharp end of this run**: at the frontier the OS's own
most useful messages — `printf("cannot mount root, errno = %d\n")` (`bsd_init.c:962`),
`printf("We are hanging here...\n")` (`:956`), `printf("bsd_init: failed to mount network root,
error %d, %s\n")` (`:954`) — are all in the compiled-out class, while every `IOLog` is not. The
strings are in the image (`iokit/bsddev/IOKitBSDInit.cpp`'s table around `0x8048294d` holds
`IOMedia`, `Apple_HFS`, `Content`, `Waiting for matching to complete`, `Waiting on %s`, `Still
waiting for root device`, `Got boot device = %s`, `Wait for root failed`, `BSD root: %s`); the
`printf` class will not reach either sink until the build is changed.

**The frontier did not move** (prediction 7). Three `xnu_live_iolock` records, all at
**`waitForMatchingService+0xE0`** again — site `0x801365d4` against the symbol at `0x801364f4`, the
durable form 457 named, in a build whose `waitForMatchingService` moved by 0x3C0 from 457's — with
`deadline − now` of 29.9966 s, 29.9936 s and 59.9943 s, and `finddrv` 2, `rootdrv` 1, 38 `block_seq`
records: the same `IOMedia`/`Apple_HFS` wait as 457, unchanged. The capture adds one fact to it:
`IOFindBSDRoot` prints `IOLog("Still waiting for root device\n")` when the 60-second wait **times
out** (`IOKitBSDInit.cpp:559`), and that line is not in the log — so the hardware watchdog returned
the device at ~25 s, comfortably inside the deadline the boot was still serving.

### Where the prediction was wrong, and what the errors were

1. **`opsidx` is 0, not 1** (prediction 1's first item). The document argued from `cons_ops_index`'s
   initialiser and the missing `serial=` boot-arg — and missed `initialize_screen`'s no-video branch,
   which is a *third* caller of `switch_to_serial_console()` the grep for callers would have shown
   (`osfmk/console/video_console.c:2848`). The prediction named the right word to read and the wrong
   value; the source it quoted was true and incomplete, which is the failure mode this project has
   recorded most often.
2. **`kputc` is `cnputc`, not `serial_putc`**: `serial_init()` fails on this device, so
   `PE_init_kprintf` stores the *other* branch (`pexpert/arm/pe_kprintf.c:38-41`) — and the
   `movw`/`movt` same-object reference that justified wrapping `uart_putc` therefore handed over
   `serial_putc`'s address in a build where it is never stored at all. The image check that found the
   fact was right; the run says the belt was not needed *here*, because with no uart the only route
   to the serial sink would have been `_serial_putc` from the ops table — which the `serial_putc`
   wrapper would have caught. It stays, as the instrument that is correct in the configuration where
   `serial_init()` succeeds, and the reason is on record rather than in a comment.
3. **The tank held 17 characters, not 0** (prediction 6). The entry's live channel is established
   lazily by the first `entry_live_write`, and `PE_init_iokit` prints before any wrapper has written
   a record — so the tank is not a nicety, it is the only reason the first line of the captured text
   exists. The 8 KB of `.bss` paid for itself in the first run.
4. **The banner's absence had a fourth cause, and the document only knew three.** The predictions
   offered "gated by `disableConsoleOutput`" or "not printed"; the answer is a *build configuration*
   that turns `printf` into a call that discards its arguments. This project's catalogue of what
   stops the boot has three kinds (a missing symbol, an invented zero, a boot-arg or DT string —
   `mi4-stub-walk-frontier-kinds`); what stops *output* has now one more, and it is the first one
   that is settled at compile time rather than at run time.
5. **The counters are never published.** `ostext_chars`/`ostext_total`/`ostext_heals` exist but
   nothing writes them unless the block fills or a kilobyte of text arrives, so this run's log has no
   `ostext_chars` at all — the count had to be taken from the block's own bytes. A step whose whole
   product is a count should publish it, and the next one will.
6. Safety as measured: 437874 bytes of log, 25 × `persistent_write_attempted=0x00000000`, 87 ×
   `failure_mask=0x00000000`, no `exception:`, no `panic:`, no `stub_hit=`, one
   `xnu_live_console=0x00000001` with `attempts=0x00000001`, and the device back on its own.

**Next:** the root device, with two instruments now in hand and one configuration change named: the
`IOMedia`/`Apple_HFS` wait needs a storage driver, and the messages that a failure there would print
are the compiled-out `printf` class, so the step that adds the driver is also the step that turns
`CONFIG_NO_PRINTF_STRINGS` off — one variable each, and the string size is the price.

## Safety, unchanged and re-stated for a step that runs new code

`fastboot boot` only, never flash; every touch through `stages/stage90/preflight_boot_check.sh
--allow-xnu-entry` and `stages/stage90/run_and_capture.sh --allow-xnu-entry`; the hardware watchdog is
armed by the payload before the jump and XNU never touches it, and it is the net that returns the
device (the software dead-man is disarmed before the entry by design, 308). What is new here is that
this step's code runs on the console path — `_cnputs` with preemption disabled, under a hardware lock
whose timeout panics — so the wrapper allocates nothing, takes no lock, calls nothing in XNU, and
writes plain stores into the ram console. The one expensive thing it does is space-fill the 128 KB
block once, on the first character, which is bounded work on a path that is already printing. The two
writers share the buffer's size field, and the separation is by construction rather than by luck: the
block is published before it is filled, so the records are above it from that instant, and the text
never writes outside it. If the console path faulted, the entry's own `fleh_dataabt` reports it and
the watchdog returns the device.

