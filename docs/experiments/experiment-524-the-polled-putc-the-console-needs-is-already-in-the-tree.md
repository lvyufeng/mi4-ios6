# 524: the polled putc the console needs is already in the tree

**This is a reading, not a step.** No image, no switch, no device. It exists because 523 left one
question explicitly open and this repository already contains the answer to it.

523 §2.3, verbatim: *"MSM8974 is neither \[S3c2410 nor PL011\]. Its UARTs are the Qualcomm UARTDM
blocks ... Whether the `ln2410` accessors can be bent to drive it is an open question, but it is an
*engineering* question with a testable answer, which is more than the phase had before this
reading."*

The question turns out not to need bending. **This repository has vendored the MSM8974 UART
register map and a polled character-at-a-time writer for exactly this chip**, in the Android kernel
tree at `external/android_kernel_xiaomi_cancro/`, and that writer is a `pe_serial_functions` table
with three of its six entries already written.

## 1. The finding, stated once

The console's remaining work is smaller and more specific than 523 could see, and the reason is
that the tree has two Qualcomm UART register windows, not one:

* the **low-speed window** (`UART_SR 0x0008`, `UART_TF 0x000C`, `UART_CR 0x0010`), which is what
  the *debug console* on this SoC family uses and whose write path is a two-line poll; and
* the **UARTDM window** (`UARTDM_TF 0x0070`) with the command/data split 523 described, used by the
  high-speed driver.

523 described only the second. The first is the one a polled `pexpert` console wants, and its
accessor body is already written down in this tree - as `debug_putc`, a function whose entire
content is the body of `pe_serial_functions.td0`.

## 2. The register map, in-tree

`external/android_kernel_xiaomi_cancro/drivers/tty/serial/msm_serial.h`:

| Offset | Name | Note |
| --- | --- | --- |
| `0x0000` | `UART_MR1` | mode register 1; `UART_MR1_CTS_CTL` is `1 << 6` (`:24`) |
| `0x0004` | `UART_MR2` | 8N1 lives here: `BITS_PER_CHAR_8` `3 << 4`, `STOP_BIT_LEN_ONE` `1 << 2` |
| `0x0008` | `UART_SR` (read) **and** `UART_CSR` (write) | `:120` / `:41` - **one offset, two registers**, decided by direction. `CSR` is the baud clock select, `SR` the status |
| `0x000C` | `UART_TF` (write) **and** `UART_RF` (read) | `:58` / `:130` - the same trick again |
| `0x0010` | `UART_CR` | command register; every command is `(n << 4)` - reset RX/TX/ERR/BREAK/CTS, `SET_RFR`, and `RX_ENABLE`/`TX_ENABLE` as `1 << 0` / `1 << 2` |
| `0x0014` | `UART_IMR` (write) / `UART_ISR` (read) | `:82` / `:133`, again one offset |
| `0x0018` | `UART_IPR` | stale-interrupt pacing |
| `0x0028`..`0x0034` | `UART_MREG`, `UART_NREG`, `UART_DREG`, `UART_MNDREG` | the fractional baud divisor |
| `0x0070` | `UARTDM_TF` | `:59` - the high-speed window's data register, **not** the one the console uses |

Status bits (`:120-128`): `RX_READY` `1 << 0`, `RX_FULL` `1 << 1`, **`TX_READY` `1 << 2`**,
`TX_EMPTY` `1 << 3`, then overrun/parity/break/hunt.

## 3. The putc is already written

`external/android_kernel_xiaomi_cancro/drivers/tty/serial/msm_serial_debugger.c` - the polled
debugger console for this SoC family - is 400 lines whose useful part is two:

```c
static inline void debug_putc(unsigned int c)          /* :103 */
{
	while (!(msm_read(UART_SR) & UART_SR_TX_READY)) ;
	msm_write(c, UART_TF);
}

static inline void debug_flush(void)                   /* :109 */
{
	while (!(msm_read(UART_SR) & UART_SR_TX_EMPTY)) ;
}
```

Read against Apple's interface (`pexpert/arm/pe_serial.c:30-37`), that is not an analogy, it is the
table:

| `pe_serial_functions` | what it is | UARTDM body, already in the tree |
| --- | --- | --- |
| `uart_init` | one-shot bring-up | `debug_port_init` (`:57-91`): `CR` resets, the four divisor registers, `CSR = 0xFF` for 115200, `RFWR = 0`, `CR = 0x05` (RX enable + TX enable), `IMR = RXLEV` |
| `uart_set_baud_rate` | divisor | the same four registers, selected by `clk_get_rate` (`:74-82`) |
| `tr0` | "transmitter ready?" | `(read(SR) & TX_READY) != 0` |
| `td0` | "write a byte" | `write(TF, c)` - plus `debug_putc`'s wait in front of it |
| `rr0` / `rd0` | receive | `debug_getc` (`:93-99`), `SR & RX_READY` then `read(RF)` |

Note the sense of the bit, because it inverts between the two files and both are correct:
`debug_putc` waits **until** `TX_READY` is set, while `handle_tx` (`msm_serial.c:311`) loops while
`TX_READY` is set, writing bytes. `TX_READY` set means "the FIFO has room", so both are busy-wait
loops from opposite sides of the same condition. Apple's `tr0` is the first spelling.

And `ln2410_uart_init`'s own comment (`pe_serial.c:56`) - *"NCLK, No interrupts, No DMA - just
polled"* - is the same mode `debug_port_init` programs. Nothing about the required operating mode
differs; only the addresses do.

## 4. Where it plugs in, and what is *not* missing

The image's `serial_init` (`0x8003daac`) was re-disassembled for this reading, and its literals were
read out of the ELF by address rather than by string search:

| instruction | literal | meaning |
| --- | --- | --- |
| `0x8003dab4` | `0x80492399` = `"dcc"` | `PE_parse_boot_argn` key |
| `0x8003dae0` | `0x8055a104` | `uart_initted = 1`, `return 1` - **no store to `gPESF`** |
| `0x8003db04` | `0x8049239d` = `"boot-console"`, `NULL` | `DTFindEntry` |
| `0x8003db20/24` | `0x804b1e3e` = `"name"`, `0x804923b5` = `"uart0"` | then `"uart1"` (`0x804923bb`) |
| `0x8003db74` | `0x804c7c59` = `"reg"` | read **unconditionally**, then `ml_io_map(soc_base + reg[0], reg[1])` at `0x8003db98` |
| `0x8003dba4` | `0x804923aa` = `"compatible"` | fetched, then the function **ends** |

There is no `strcmp` against a driver list, no `gPESF` assignment and no `gPESF->uart_init()` call
anywhere between `0x8003dba4` and `uart_putc` at `0x8003dbbc`. That confirms 523's conclusion by
disassembly rather than by `nm`: the `#ifdef S3CUART` block (`pe_serial.c:778-807`) is compiled
out - `pclk`, `ubrdiv` and `uart,16550` are all absent from the image - and the node-found path
returns 0.

Two consequences that make the remaining work smaller:

* **`uart_putc` needs no change at all.** Its whole body
  (`pe_serial.c:813-819`) is `if (uart_initted) { while (!gPESF->tr0()); gPESF->td0(c); }`,
  which is already the generic form. So after a table exists, "the driver is running" is exactly
  two global writes: `gPESF = &<table>` and `uart_initted = 1`.
* **The `dcc` branch proves the shape works**: it is the one path in the image that sets
  `uart_initted` and returns 1, and its cost is one boot-arg. A temporary arm can therefore obtain
  *the console* before a driver exists - by pointing `gPESF` at a table whose `td0` is the payload's
  own console sink - which would make the `PE_kputc` fork take the `serial_putc` branch without
  claiming a driver. That is an instrument, not the milestone, and it is recorded here only so it is
  not mistaken for one.

## 5. The address

523 §2.4 item 2 said *"the address itself has to come off the device tree or the MSM8974
documentation."* Both are in the tree:

`arch/arm/boot/dts/msm8974.dtsi`: `serial@f991f000` (`:239`), `blsp1_uart1: serial@f991e000`
(`:253`), `uart7: uart@f995d000` (`:2394`); the low-speed UARTs are `compatible =
"qcom,msm-lsuart-v14"` (`:240`, `:247`, `:254`) and the high-speed one `"qcom,msm-hsuart-v14"`
(`:2395`).

`serial_init` already computes the mapped base as `ml_io_map(soc_base + reg[0], reg[1])`, so the
value the node needs is the UART's offset **within** `soc_base`, and the payload's own replayed tree
is the authoritative source for it - the same tree whose bytes experiment 443 has the device hash
back. Nothing here has to be guessed from documentation.

Do not read `"qcom,msm-lsuart-v14"` as a `compatible` the image could be made to match: it is
Qualcomm's string, and the `strcmp` chain that consumes `serial_compat` is Apple's, compiled out.
The string the node needs is one a new `#if` in `pe_serial.c` compares against.

## 6. A measurement defect this reading made, and caught

The first pass at §4 used `strings -a img | grep -F dcc` and got **0** - which would have
contradicted 523's table and, worse, would have read as "the `dcc` path is not in the image" for the
one branch that is. `strings` defaults to a **minimum length of 4**, so a 3-character literal is
invisible to it. Re-reading the same addresses out of the ELF's program headers gave `"dcc"`,
`"boot-console"`, `"compatible"`, `"uart0"`, `"uart1"`, `"name"`, `"reg"` - 523's table, correct.

This is the `mi4-measurement-defects` family and it has a shape worth naming next to the ones
already there: **before concluding a value is absent, establish that the extractor could have seen
it.** `strings`' minimum length, `grep -q`'s exit status, a decimal pattern against `0x%08x`, and a
`set -e` pipeline are four spellings of the same mistake. The literals in §4 were read by address
precisely because the first method could not have reported a positive.

## 7. What this does not decide

It does not decide that the low-speed window is wired to the Mi 4's debug connector, nor that the
phone exposes any UART at all on a port reachable without a special cable. `debug_port_init`
programs for a 19.2 MHz TCXO and `pe_arm_get_soc_base_phys` gives the offset's base; whether those
agree with this board is a measurement, not a reading, and it needs the device.

It also does not decide the ordering question 523 §6 left open - which of a static pid 1 and a
filesystem comes first once 522's run is read. This reading removes an unknown from the console
item; it does not reorder the phase.

And it changes nothing about 522. 522 is built, gated and frozen, its run is still owed, and
nothing in this log is a reason to touch the device differently when it appears.

## 8. Safety

Unchanged, and this log touches none of it: `fastboot boot` only, never flash, gate first, log
captured before anything else touches the device. Nothing here is an image, a switch or a build -
it is a reading of files already in the tree, and it was done without the device present.
