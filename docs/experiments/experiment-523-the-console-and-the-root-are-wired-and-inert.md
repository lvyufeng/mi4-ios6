# 523: the console and the root are wired and inert

**This is not a run and it is not a step's image.** 522 is built, gated and frozen, and its hardware half
is still owed - the device has not been on the bus since 521's non-return. So 522's four readings remain
unwritten, and nothing here depends on how that run turns out: the two subjects below are the ones 522's
section 3.2 opened (*what the OS would be running, and where from*) and neither is a cache question.

Everything in this log is a reading of the tree at `external/xnu-4570.1.46` or a disassembly of the image
built for 522 (`out/stage90/xnu_arm_entry.elf`, whose `.text` is 5,306,504 bytes and whose payload is
`13d771938336fe65ccaf3dee833c14c0a9280d5365eda3f5c1e74cc6e6948825`). There is no hardware result in it,
and it is written now only because the hardware track is blocked on a power press and this is the one
piece of the drivers milestone that can be settled without the device.

## 1. The finding, stated once

The kernel's console chain is **complete and inert**: every link exists in the image, and the last one -
the only one that would put a byte on a wire - is disabled by a single `.bss` byte that nothing sets. The
same shape holds for the root: the filesystem is mounted, the program is exec'd, and there is no
filesystem that can read the device's storage and no program that is a program.

Both of these are *supply* problems with a known, small extension point rather than missing subsystems.
That is the useful part, and it is the reason to write this down before the drivers phase starts rather
than during it.

## 2. The console

### 2.1 The chain, by address

Read off the image rather than off the source, because the payload has wrapped three of the links and
the source alone does not show which of the two spellings of each call survived the build:

| Link | Address | What it is |
| --- | --- | --- |
| `cons_ops` | `0x8049878c` | the two-entry table, `serial_console.c:117-126` |
| `_serial_putc` | `0x800acd34` | `cons_ops[0].putc`, tail into `serial_putc` (`serial_console.c:620-622`) |
| `vcputc` | `0x8003dc20` | `cons_ops[1].putc` |
| `cons_ops_index` | `0x80527928` | `.data`, `= VC_CONS_OPS` (`serial_console.c:128`) - **entry 1, the video console** |
| `PE_kputc` | `0x8055a0d4` | `.bss`, the sink `kprintf` and `panic` print through |
| `serial_putc` | `0x8003d978` | `b __wrap_uart_putc` - the payload's wrapper, not Apple's body |
| `__wrap_uart_putc` | `0x8047d850` | calls `entry_os_console_char` (`0x80004560`), then `b uart_putc` |
| `uart_putc` | `0x8003dbbc` | Apple's polled-mode writer, `if (uart_initted) { wait; write; }` |
| `uart_initted` | `0x8055a104` | `.bss`, zero, and nothing in this boot sets it |
| `serial_init` | `0x8003daac` | decides `PE_kputc` |

So a `printf` in XNU reaches `ram_console` through **the payload's** `__wrap_uart_putc`
(`entry_stubs.c`'s `[os-console-459]` sink, `entry_os_console_char`), and the one call that would reach
hardware - `uart_putc` - returns immediately because `uart_initted` is 0. The payload did not replace the
UART; it *tapped* it. `PE_init_kprintf` (`pexpert/arm/pe_kprintf.c:38-41`) is the fork in the road:

```c
if (serial_init())
        PE_kputc = serial_putc;
else
        PE_kputc = cnputc;
```

and `cnputc` is itself the payload's (`xnu_object_shims.c:93`), so both branches of that fork currently
end in `ram_console`.

### 2.2 The one byte that decides it

`serial_init` (`0x8003daac`) has exactly two ways to reach `mov r5, #1`:

* **`PE_parse_boot_argn("dcc", ...)`** (`0x8003dad4`): on success the image does
  `mov r5, #1; strb r5, [0x8055a104]` and returns 1 (`0x8003dae0`-`0x8003daf0`). This is the Debug
  Communication Channel - a CoreSight/JTAG block, not the UART, and the only path that can succeed in
  *this* build.
* **A device-tree node**: `DTFindEntry("boot-console", ...)`, else `name = "uart0"`, else `name =
  "uart1"` (`0x8003db04`-`0x8003db60`), then `reg` -> `ml_io_map(soc_base + reg[0], reg[1])` and
  `compatible` (`0x8003db70`-`0x8003dbb8`) - the source's `pe_serial.c:758-778` exactly. **The payload's
  device tree declares no such node**: `xnu_real_dt.c` has no `serial`, `uart` or `compatible` string in
  it at all.

### 2.3 Why a device-tree node alone would not be enough

Because there is no driver in this image for the node to select. `serial_init` picks a
`struct pe_serial_functions` (`pe_serial.c:30-37`: `uart_init`, `uart_set_baud_rate`, `tr0`, `td0`,
`rr0`, `rd0`) by comparing `compatible` against a fixed list, and the list's strings are **absent from the
image**:

| compatible | in the image? | table it would select |
| --- | --- | --- |
| `uart,16550`, `uart-16550`, `uart,s5i3000`, `uart-1,samsung` | no | `ln2410_serial_functions` |
| `uart16x50,mmio` | no | `uart16x50_serial_functions` (MV88F6710) |
| `boot-console`, `uart0`, `uart1`, `compatible`, `dcc` | yes | - the lookup keys, not the drivers |

`S3CUART` is defined unconditionally by `pexpert/pexpert/arm/S3cUART.h:7`, but nothing force-includes it
into this build, and `ARM_BOARD_CONFIG_MV88F6710` is a reference-board configuration; so the `strcmp`
chain is not in the image and neither are `ln2410_serial_functions` or `uart16x50_serial_functions`
(confirmed by `nm`). That leaves `pe_serial.c`'s `#else return 0` as what the node-found path compiles
to, and the file's own header line says what the tables were written for: *"Polled-mode UART0 driver for
S3c2410 and PL011"*.

**MSM8974 is neither.** Its UARTs are the Qualcomm UARTDM blocks - a command/data register split with a
`CR`-guarded `TF`/`RF` FIFO interface, not a 16550 register window and not a PL011 one. Whether the
`ln2410` accessors can be bent to drive it is an open question, but it is an *engineering* question with
a testable answer, which is more than the phase had before this reading.

### 2.4 What the console item actually is

The I/O Kit side is not the blocker. `/dev/console` already exists - `devfs_init` creates it as
`makedev(0, 0)`, `0622` (`bsd/miscfs/devfs/devfs_vfsops.c:113-115`), alongside `tty` (2,0), `null` (3,2)
and `zero` (3,3) - and `bsd_init` mounts devfs at `/dev` (`bsd/kern/bsd_init.c:1019-1026`,
`devfs_kernel_mount("/dev")`). What is missing is the character-device switch behind major 0 and the
`cons_ops` entry behind it:

1. A `pe_serial_functions` for MSM8974's UARTDM, or a demonstration that `ln2410`'s accessors drive it -
   plus the `compatible` string, plus the `S3CUART` (or a new) `#ifdef` so the branch is in the image.
2. A `boot-console` node in the payload's device tree carrying `reg` (the UART's offset within
   `soc_base`) and that `compatible`. The address itself has to come off the device tree or the MSM8974
   documentation; the payload already builds the tree (`xnu_real_dt.c`) and already knows `soc_base`
   (`pe_arm_get_soc_base_phys`, `0x8000fde4`).
3. Then `serial_init()` returns 1, `PE_kputc = serial_putc`, and the payload's two wrappers become
   removable rather than load-bearing - which is the definition of the driver being *running*.

Order matters and this is first, for one reason: every later driver's evidence is printed through this
path, and today that path is the payload's buffer rather than the kernel's own. A driver whose output
goes through the thing being measured is not an instrument.

## 3. The root, and the storage clause

Two facts, both from the tree, and together they answer the goal's conditional TWRP clause in the only
honest way available:

* **mockfs is the only filesystem that can be root here.** In `bsd/vfs/vfs_conf.c:123-158`'s
  `vfstbllist`, the mountroot field is non-NULL for exactly one entry - `mockfs_mountroot` - and devfs,
  routefs and nullfs carry NULL; `NFSCLIENT` is off in this configuration. That is why the OS mounts at
  all, and it is why it mounts a fixture: three nodes with one regular file, `sbin/launchd`, whose own
  comment says it is "assumed to be an executable".
* **There is no HFS in this tree.** `find` over `external/xnu-4570.1.46` returns no HFS source and
  `bsd/conf/files` has no HFS entry; Apple's HFS lives in a separate driver component. So the
  configuration has devfs, routefs, fifo, specfs and mockfs - and nothing that can read a partition.

The consequence for "让os可以正常启动并且挂载存储" is that **a storage driver is not sufficient**. The
phone's userdata is ext4 or f2fs; neither is in XNU at any copyright date, and the tree that would mount
it does not exist here. Mounting the device's own storage therefore needs *two* new things - an
MMC/BlockStorage path for MSM8974 **and** a filesystem that can read what is on the partition - and the
second is the larger of the two. The smaller honest alternative is our own read-only filesystem over our
own container, which is a project of its own and not a step.

This is recorded here so that the clause is not attempted on a false premise later: with the OS up and
running, `twrp`-style writing to storage would still need something to write *with*.

## 4. What pid 1 can run

The same "supply, not subsystem" shape, and here the tree is more specific than it looks.

A process-1 image is refused unless one of two conditions holds (`bsd/kern/mach_loader.c:623-636`):

* **Static** (`MH_EXECUTE` without `MH_DYLDLINK`): refused outright unless
  `#if !(DEVELOPMENT || DEBUG)` is false - Apple's own comment is *"Check properties of static
  executables (disallowed except for development)"*.
* **Dynamic** (`MH_DYLDLINK`): needs a dylinker vnode *and* `CS_DYLD_PLATFORM` in its code-signing flags
  (`mach_loader.c:1155`, `:1173`), which is set only for a platform binary (`:2326-2327`) - i.e. it needs
  Apple's signature, which is not obtainable.

**The configuration already takes the first branch on purpose.** `STAGE90_XNU` is
`[ RELEASE mockfs development ]` and `tools/xnu_config/boot/STAGE90_XNU.local`'s header says why:
`development` is the narrow, lower-case attribute that adds exactly `-DDEVELOPMENT=1` and is what makes
a static executable legal. So the infrastructure for a real pid 1 is already in the image and is not
being used - what is on the RAM disk is an 8 KB Mach-O **header stub** (`g_stage90_ramdisk`,
`0x80511000`) whose `LC_UNIXTHREAD pc = 0x10e0` and whose `__TEXT` has fileoff 0, so pid 1 executes its
own header and faults at `0x1118`/`0x1124`/`0x11a4`, ending at `far 0x102000` - the page past `__TEXT`.

The arm that follows from this is small and testable: **a real static ARMv7 Mach-O program as `/sbin/launchd`**,
built by this tree's own toolchain, whose first act is a `write(1, ...)` to the console from section 2.
That turns "pid 1 faulted inside its own Mach-O header" into "pid 1 executed our instructions and made a
syscall", which is the smallest reading that means *the OS is running something*.

## 5. Two obligations this phase inherits from the cache walk

Neither is a cache question any more, and both are recorded so they are not lost with the step that
found them:

* **`ml_arm_sleep` stores with the cache off** (`0x8000dfc0`: `platform_cache_disable`, then
  `bcopy(suspend_signature, IOS_STATE, 8)` at `0x8000e090`, then an infinite spin), and
  `platform_cache_disable` has three call sites - the idle enter's and two inside `ml_arm_sleep`. It is
  unreachable today (no caller, no pointer in data, the name only in an uncopied `.strtab`) and becomes
  reachable as soon as a platform driver that can suspend exists. It is the same question 522 answers for
  the idle path, in a path where the answer cannot be "the window is 140 bytes and all of them are
  Apple's".
* **522 re-enables the D-cache while the CPU is still out of the ACTLR coherence domain** - Apple rejoins
  the domain before setting `SCTLR.C`, 522 does it after. On one CPU with no DMA this is unobservable,
  which is exactly why it is worth writing down: the moment a second CPU or a live DMA master exists, an
  ordering that was free becomes a correctness property.

## 6. What this does not decide

Which of section 4 or a filesystem comes first. If 522's run passes, the next arm is more likely to be
the static pid 1 - it is smaller, it produces a reading through the console, and it does not need
storage. If 522's run hangs, the next arm is the null instrument that separates the readings' cost from
the state change's (522's section 3.1), and this log is the scope of the phase that follows *that*, not
of the next step.

## 7. Safety

Unchanged, and this log changes nothing about it: `fastboot boot` only, never flash, gate first, log
captured before anything else touches the device. Nothing in this section is built, and no image or
switch is proposed here that would be exercised before 522's own run is read.
