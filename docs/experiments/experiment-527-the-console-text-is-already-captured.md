# 527: the console's text is already captured - the sink that is off is the DCC

**A host-side reading: no device, no build, no switch.** It re-reads 520's own log and the two objects
that log's run executed, and it settles the console item 523 section 2 named as the first thing the
drivers milestone needs. The answer is not the one 523 and 524 gave it:

* **The OS's console text is already captured, and already read by the host.** 458's and 459's taps sit
  in front of *both* sinks, so the characters that the sinks discard in silence are written into a
  reserved 128 KB block of the RAM console, and 520's log carries XNU's own boot output verbatim -
  `Darwin Kernel Version`, `BSD root: md0, major 2, minor 0`, `load_init_program: attempting to load
  /sbin/launchd`, and the register dump and `MACH Reboot` that ended the run. The console is not inert.
  The *log* channel works.
* **What is inert is Apple's hardware sink, and it is not the MSM8974 UART.** `uart_putc`'s body in this
  image is the ARM **DCC** - a CP14 debug-channel write - and 524 section 4's plan for it ("two global
  writes") cannot be carried out at all, for three independent reasons, one of which is that the pointer
  to write does not exist in the image.
* **The seam for a real physical console already exists and the payload already occupies it.**
  `__wrap_uart_putc` calls `entry_os_console_char` and then tail-calls Apple's sink, so a console that
  drives the MSM8974 UART is a change inside the payload's own wrapper - not a change to Apple's code.

The load-bearing consequence for the goal is the third one plus the first: the boot can already be read
step by step from its own words, and what the console item is short of is a *wire out of the SoC*, which
the goal's "把基础驱动跑起来" does not need. 524's pending arm for it is retired rather than done.

## 1. What was believed, and the reading that replaces it

523 section 2 put the console first among the missing drivers, on a chain it read out of the image:

> the console chain is complete in 522's image and its last link is off (`PE_kputc` -> `serial_putc` ->
> `__wrap_uart_putc` + Apple's `uart_putc`, inert because `uart_initted` is zero - **the payload tapped
> the UART, it did not replace it**)

That chain is right, and the last clause is right, and both are now visible instruction by instruction
(section 4). What the sentence implies is not: "inert" is true of Apple's sink and false of the console,
because the tap 523 names is **in front of** the gate, not behind it, so every character the gate refuses
has already been written down. 523 read the chain as one series of links in which the last one off means
the news does not arrive. It arrives - it just does not leave the chip.

524 section 4 then costed the repair:

> `uart_putc` needs no change at all ... "the driver is running" is exactly two global writes:
> `gPESF = &<table>` and `uart_initted = 1`

Each of those two writes is refused by the image, and section 5 shows the refusal at the object level.

## 2. The text, from 520's log

520's log is the last run that came back (522's produced none). It contains, at byte offset **296022**:

```
Darwin Kernel Version ###not-built-by-apple###
vm_page_bootstrap: 2218 free pages and 1878 wired pages
zalloc: allocating memory for zone names buffer
"vm_compressor_mode" is 0
multiq scheduler config: deep-drain 0, ceiling 47, depth limit 4, band limit 127, sanity check 0
standard timeslicing quantum is 10000 us
standard background quantum is 2500 us
mig_table_max_displ = 1
debug_log_init: Error!! gPanicBase is still not initialized
iBoot version:
Copyright (c) 1982, 1986, 1989, 1991, 1993
The Regents of the University of California. All rights reserved.
MAC Framework successfully initialized
using 80 buffer headers and 80 cluster IO buffer headers
mcache: 1 CPU(s), 128 bytes CPU cache line size
mbinit: done [0 MB total pool size, (0/0) split]
Added memory device md0/rmd0 (02000000/0D000000) at 0000000080511000 for 0000000000002000
BSD root: md0, major 2, minor 0
VM_TEST_DEVICE_PAGER_TRANSPOSE: PASS
load_init_program: attempting to load /usr/local/sbin/launchd.development
load_init_program: failed loading /usr/local/sbin/launchd.development: errno 2
load_init_program: attempting to load /sbin/launchd
```

and, at the end of the same block, the run's own ending - the fault state `...fault in kernel mode:
fault_addr=0x4b79074`, the register dump, `Attempting system restart...MACH Reboot`.

**Two independent facts say this is the instrument's block and not a coincidence of the dump.**

1. **The offset is the reading.** The block's base is published as `xnu_live_ostext_at=0x00048456`, and
   `0x48456` is **296022** - the marker's own byte offset in the log file. The host dump begins the RAM
   console's data area at file offset 0, so the number in the log and the offset in the file are the
   same number, and a block that had been reserved anywhere else could not satisfy that.
2. **The marker is the instrument's.** `entry_os_reserve` writes `"\n[os-console-459]\n"` at the block's
   first bytes (`entry_stubs.c:2625`), and that string is in the log immediately before the text. The
   lines above are XNU's; nothing in the payload prints `load_init_program`.

**The counts, and the arithmetic that closes.** The block's own bytes are **1995** characters of text
(to the first run of sixteen spaces, which is where the reservation's space-fill begins). The log's
counters say `xnu_live_ostext_chars=0x000003ee` (1006), `_total=0x000003ee` (all 1006 stored),
`_tank=0x00000000`, `_heals=0x00000001`, `_block=0x00020000`.

1006 is not the total and is not meant to be: the counters ride along with the heal, which fires every
`ENTRY_OS_HEAL` (1024) bytes of block (`entry_stubs.c:2737-2748`, 459's own addition), so the published
value is a **snapshot at the last heal**. The check is exact - the marker is **18** bytes
(`\n` + `[os-console-459]` (16) + `\n`), and 18 + 1006 = **1024**, the threshold itself. One heal is
also what 1995 characters allow: the second would need 2048. (`ostext_lines` and `ostext_limited` are
absent from the log, which is the same statement from the other side - they are only published if the
block *fills*, and a 128 KB block receiving 2 KB never does.)

**So the volume reading 459 wanted is answered: a whole XNU boot, to `MACH Reboot`, prints about 2 KB
of console text - sixty-four times under the block's capacity.**

## 3. Which route the text took, read rather than assumed

The instrument publishes the console's own state (`entry_os_state_record`, `entry_stubs.c:2607`), and
520's log carries it:

| key | value | what it is |
| --- | --- | --- |
| `xnu_live_console_kputc` | `0x800ad3cc` | `PE_kputc`, and `0x800ad3cc` is **`cnputc`** - **not** `serial_putc` (`0x8003d978`) |
| `xnu_live_console_noserial` | `0x00000001` | `disable_serial_output`, still at its default `TRUE` (`pe_kprintf.c:19`) |
| `xnu_live_console_noconout` | `0x00000000` | `disableConsoleOutput` |
| `xnu_live_console_opsidx` | `0x00000000` | `cons_ops_index` (`D`, `0x80527928`) |
| `xnu_live_console_dbgcnt` | `0x00000000` | `kernel_debugger_entry_count` - **no panic was entered in this run** |

`PE_kputc` is set exactly once, in `PE_init_kprintf` (`pe_kprintf.c:24-42`): `serial_putc` if
`serial_init()` returns non-zero, else `cnputc`. The log says `cnputc`, so **`serial_init()` returned 0
in this boot** - a reading, not an inference, and one that agrees with everything else here: the serial
route is not even the one installed, and `disable_serial_output` is still `TRUE`. The text went

```
cnputc (0x800ad3cc) -> _cnputs (0x800acf6c) -> vcputc (0x8003dc20) -> __wrap_vcputc (0x8047d7c4)
                                                                       -> entry_os_console_char(c, 1)
```

and `vcputc`'s own body gates on `gc_initialized` (`b`, `0x8055a114`) being 1 - reading it with
`ldrb` / `cmp #1` / `bne` out of the function. That global is 0 at boot, so Apple's video console prints
nothing either; the characters survive because the wrapper is **in front of** the gate. That is the
whole mechanism of 458, and it is why the console could be read before either sink worked.

(The two gate globals `uart_initted` and `gc_initialized` are `b` - local to their objects - so the
instrument cannot read them by name. `entry_stubs.c:2596-2600` records that as a decision and gives the
counters as the substitute. This section is the first time that substitute has been filled in with
values.)

## 4. The sink that is off, read off the image

`uart_putc` in the entry image, `0x8003dbbc`, complete (48 bytes):

```
8003dbbc  push {r4, lr}
8003dbc0  mov  r4, r0
8003dbc4  movw r0, #0xa104
8003dbc8  movt r0, #0x8055        ; r0 = &uart_initted (0x8055a104)
8003dbcc  ldrb r0, [r0]
8003dbd0  cmp  r0, #1
8003dbd4  popne {r4, pc}          ; the gate 523 named: uart_initted must be 1
8003dbd8  bl   80017a2c <arm_debug_read_dscr>
8003dbdc  tst  r0, #0x20000000    ; ARM_DBGDSCR_TXFULL (1 << 29)
8003dbe0  bne  8003dbd8           ; wait until the DCC's transmit register is not full
8003dbe4  mcr  p14, 0, r4, c0, c5 ; write_dtr(c)
8003dbe8  pop  {r4, pc}
```

`uart_getc` (`0x8003dbec`) is symmetric: `arm_debug_read_dscr`, `tst r0, #0x40000000`
(`ARM_DBGDSCR_RXFULL`, 1 << 30), then `mrc 14, 0, r0, cr0, cr5, {0}` - `read_dtr`.

This is `pe_serial.c`'s **dcc** backend, with its indirection gone. Source for comparison - the four
table entries at `pe_serial.c:212` (`dcc_tr0, dcc_td0, dcc_rr0, dcc_rd0`), `write_dtr`
(`:162-170`, `mcr p14, 0, %0, c0, c5`), `dcc_tr0` (`:173-181`, `!(arm_debug_read_dscr() &
ARM_DBGDSCR_TXFULL)`), and the generic reader at `:814-819`:

```c
void uart_putc(char c)
{
        if (uart_initted) {
                while (!gPESF->tr0());  /* Wait until THR is empty. */
                gPESF->td0(c);
        }
}
```

**The image has no such indirection.** `nm out/xnu_kernel_obj/pexpert_arm_pe_serial.o` lists every
symbol the object defines:

```
00000000 T serial_init
00000110 T uart_putc
00000140 T uart_getc
00000000 b uart_initted
```

`text 421, data 0, bss 1` - and that is all. There is **no `gPESF`**, no `dcc_serial_functions`, no
`dcc_tr0`/`dcc_td0`/`dcc_rr0`/`dcc_rd0`, and no `serial_putc` (that is `pe_kprintf.c:131`'s, in another
object). The object's only undefined reference to the sink is `U arm_debug_read_dscr`: the compiler
proved `gPESF` can hold exactly one value, `&dcc_serial_functions`, folded the pointer away, and inlined
the four accessors at their call sites. The static pointer declared at `pe_serial.c:39` has **no
storage**, and neither does the table - which is what `text == 421` for four functions including a
`serial_init` of `0x140` bytes says.

Why the fold is sound: `gPESF` is assigned in precisely two places, and in this configuration both other
arms are compiled out - the `SHMCON` branch and the `uart16x50,mmio` branch. `serial_init`'s own
disassembly shows the surviving branch storing **only** `uart_initted`:

```
8003dae0  movw r0, #0xa104
8003dae4  mov  r5, #1
8003dae8  movt r0, #0x8055
8003daec  strb r5, [r0]          ; uart_initted = 1
8003daf0  b    8003db64          ; return 1
```

with no store to any table pointer beside it. So the `gPESF = &uart16x50_serial_functions` line that
`pe_serial.c` prints is not in this image - and the same fact is a latent wedge worth naming: this branch
sets `uart_initted = 1` **without installing a table**, so a boot arg that reaches it arms section 5's
spin.

## 5. Why 524 section 4's two writes cannot be performed

Independently, three times:

1. **There is no `gPESF` to write.** Section 4: the symbol does not exist in the object or the image.
   `gPESF = &<table>` is not a store that can be placed, because the compiler removed the variable and
   with it the read that the store was meant to change. Even a correct table would not be consulted.
2. **`uart_initted` is not writable from outside.** `nm` says `b` - local, and `0x8055a104` is an
   address rather than a symbol another object can reference. (The payload could store to the address
   by name-of-address, as it does for other locals; the instrument deliberately does not, and
   `entry_stubs.c:2598` says why. That is a separate decision from whether writing it would help.)
3. **Writing it would arm the DCC, not the UART - and would likely hang the boot.** With
   `uart_initted = 1`, the next console character enters `while (arm_debug_read_dscr() & 0x20000000)`
   where `arm_debug_read_dscr` is `mrc p14, 0, r0, cr0, cr1, {0}` (`0x80017a2c`, two instructions). On
   this CPU the value of that read is **unmeasured** - no run has ever reached the instruction, because
   `uart_initted` has been zero in every one of them and the gate short-circuits first - and DBGDSCR is
   not architecturally a CP14 register on ARMv7-A. If bit 29 reads clear, the loop never terminates: a
   character on the console path becomes an unbounded spin inside `kprintf`. That trades a silent
   console for a wedged boot, on the one path every later reading depends on.

So 524 section 4's conclusion - "`uart_putc` needs no change at all" - is right about Apple's function
and wrong about what to do with it, and 524's own vendored `debug_putc` is still the right *writer*; it
is the *sink* that was misidentified. This is the project's oldest defect class in a new place: one
decision answered in several files, and the answer read off the source's shape rather than off the
compiled definition. `uart_putc`'s source says `gPESF->td0(c)`; the image says `mcr p14, 0, r4, c0, c5`.
Only the second runs.

## 6. Where a real console goes, and the evidence that the seam is there

`__wrap_uart_putc` (`0x8047d808`), complete:

```
8047d808  mov  r1, #2
8047d80c  str  r4, [sp, #-8]!
8047d810  mov  r4, r0
8047d814  str  lr, [sp, #4]
8047d818  bl   80004560 <entry_os_console_char>   ; the tap: store the character, count it
8047d81c  mov  r0, r4
8047d820  ldr  r4, [sp]
8047d824  ldr  lr, [sp, #4]
8047d828  add  sp, sp, #8
8047d82c  b    8003dbbc <uart_putc>               ; tail call: Apple's sink, deliberately inert
```

and `serial_putc` (`0x8003d978`, `pe_kprintf.c:131`) is a four-byte `b 8047d808` - the `--wrap` rewrite
of its `uart_putc(c)`.

**The payload is already at the exact point where a byte would be handed to hardware**, and the
character is already in a register there (`r4`). So the console item's implementation is one edit in the
payload: have the wrapper write the character to the MSM8974's low-speed UART, using the polled putc that
is already vendored in this tree -

```c
while (!(read(SR) & TX_READY)); write(c, TF);      /* msm_serial_debugger.c:103, SR 0x0008 / TF 0x000C */
```

- and leave Apple's sink alone, inert, exactly as it is. 524's section 3 register map and this seam are
  the two halves of the same job, and 524's section 4 is the half that does not fit.

Two limits on that edit, recorded so the next step does not re-derive them: the wrapper is called from
`kprintf`'s route and from the console route, so a polled write with a bounded retry is the safe shape
(an unbounded `while` in a console path is section 5's failure mode); and the write must not be enabled
until the UART's clock and pinmux are actually up, which is a separate question - the payload has not
brought up the GSBI/GPIO for the console, and 524's map is the register side only.

## 7. Two smaller readings

**A counter published on a schedule is not a total.** `xnu_live_ostext_chars` reads 1006 in a log whose
block holds 1995 characters, and both numbers are correct: the counter is written when the heal runs, and
459 chose the heal because it "already has to write a record every kilobyte of text" (`entry_stubs.c:2742`).
Anyone reading that key as a volume will undercount by whatever arrived after the last heal - here, by
half. The block's own bytes are the total. Recorded because it is this repository's recurring shape: a
number whose meaning depends on *when* it was taken, compared against one taken at a different time.

**A comment cites the wrong file, and a comment is what the next reader trusts.**
`entry_stubs.c:2550` says "`serial_putc` is a tail call into `uart_putc` (`pe_serial.c:813`)".
`serial_putc` is defined at `pexpert/arm/pe_kprintf.c:131`; `pe_serial.c:813` is a blank line inside
`serial_init`. The claim is true and the citation is not, and 525 spent a document on the cost of exactly
this - a sentence that survived because nothing compares it with the file it names. No check is added
here (a source citation is not something this build can assert); the correction is the reading.

## 8. What this changes, and what it does not

**It changes the console item's category.** Per 523 section 2 the console was the first thing the drivers
milestone needed, because a boot that hangs takes its log with it. Half of that is already true: the log
survives - in the RAM console block, captured by the instrument, read by `run_and_capture.sh` on every
run that comes back, and worth ~2 KB of text for a whole boot. What does not survive a hang is the
*reading*, and that is a property of the payload's own records being live-only (455's lesson), not of the
console.

**It does not give a hung run a voice.** Everything in 520's log is a live record or the block's content
as of the last return; a non-return still produces no log at all, and 526's run - whenever the phone is
powered again - is still judged host-side from the USB signature. The physical UART would be the one
channel that outlives a hang, and it is the item 524 was reaching for; this document only says where it
goes and what cannot be written to get it.

**It does not begin 「挂载存储」.** Unchanged from 523: the root is an 8 KB Mach-O stub with a mockfs
fixture, the only loadable pid 1 is a static `MH_EXECUTE`, and there is no HFS in this tree.

## 9. Safety

Nothing was touched. This experiment is a reading: no build, no switch, no device, no edit to any running
script, and nothing is ever flashed in this project. The phone is off the bus and owes a power press
before 526 can run.
