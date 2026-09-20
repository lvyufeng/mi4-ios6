# Experiment 468 — the RAM disk is a Mach-O, and the ARM layer was a configuration behind

**Two findings in one step, and the second one cost the run.** The step's content is
`stages/stage90/xnu_arm_boot/entry_ramdisk.s`: the bytes `g_stage90_ramdisk` holds *are* `/sbin/launchd`,
because the memory device `/dev/md0` is created over exactly those pages and `mockfs` maps its one file
node onto them without a filesystem in between. 467's run had reached the three image activators for the
first time and every one of them declined the file, for the reason its first four bytes were zero
(`errno 8`, `ENOEXEC`). So the object is a **static ARMv7 `MH_EXECUTE` Mach-O**: seven header words, two
`LC_SEGMENT`s (`__PAGEZERO`, `__TEXT`) and one `LC_UNIXTHREAD`, with the entry point one `udf #0` at
virtual address `0x10e0`.

**Its shape is asserted twice, by two independent routes.** In the assembler, three `.if/.error` pairs
over label differences: `sizeofcmds` must be `0xC4`, the instruction must be inside `__TEXT`'s
`filesize`, and `pc` must be inside the segment it is loaded from. On the host,
`tools/host_ramdisk_macho_check.py` decodes the emitted bytes out of the object and the linked image and
asserts them against the rules `parse_machfile`, `load_segment` and `load_threadstate` apply — with
Apple's constants read out of `mach-o/loader.h` and `mach/machine.h` rather than transcribed, so a number
in the `.s` file that disagrees with the headers fails the build rather than the device run. Measured:
`g_stage90_ramdisk 0x2000`, `__TEXT` at `[0x1000, 0x2000)`, `pc = 0x10e0` = file offset `0xe0` and the
word there is `udf #0`, `sizeofcmds 0xC4`, `cpsr 0x10`. `--selftest` mutates a copy one field at a time
and requires **all 22 mutations** to be refused; the negative control is 467's own `.bss` image, which the
section check refuses by name rather than by reading a NOBITS section's garbage.

**And the run it was built for never reached it.** `machine_load_context` faulted on every thread switch:
five `sleh_abort` panics, `pc = 0x800f6d10` = the `ldm r3!, {r4-r14}` in `machine_load_context`,
`lr = 0x80016310` = `slave_main`, `far = 0x11`, `fsr = 0x1`, `cpsr = 0x60000093`, and the OS console
stopped at `mbinit: done` — twelve lines, where the previous step's run had printed twenty-three.

**The cause is that `assym.s` is a per-configuration artifact and the ARM layer was assembled against
another configuration's.** `osfmk/arm/cswitch.s`'s `machine_load_context` reads `struct thread` at three
literal offsets — `TH_CTH_SELF`, `TH_CTH_DATA`, `TH_KSTACKPTR` — and those offsets are **not** the same in
`RELEASE` and in the 468 configuration: `DEVELOPMENT`-sized `struct thread` moves them by 16 bytes
(`RELEASE` `TH_KSTACKPTR 1464 / TH_CTH_SELF 1480 / TH_CTH_DATA 1488`; this configuration
`1480 / 1496 / 1504`). `tools/gen_assym.sh` defaults `CONFIG=${XNU_KERNEL_CONFIG:-RELEASE}` and writes
`out/xnu_assym/$CONFIG/assym.s`; `tools/assemble_arm_layer.sh` and
`stages/stage90/xnu_arm_assemble.sh` both read that path. **Neither script was run from anywhere**, so a
configuration change moved every C object and left the ARM layer where it was.

The first two loads then read `TH_KSTACKPTR`'s field and its neighbour into TPIDRURO and TPIDRURW, the
third read something else as the stack pointer, and the `ldm` walked off the end of the stack. It was
found by disassembly rather than by guessing: the *linked image*'s `[r0,#1464]` was compared against the
freshly generated `STAGE90_XNU/assym.s` (`TH_CTH_SELF #1496`), and the case was closed by re-assembling
and re-disassembling `cswitch.o` (`ldr r1, [r0, #1496]`, `ldr r1, [r0, #1504]`, `ldr r3, [r0, #1480]`).

**Fixed as a check, in 471**, because the re-assembly is a hand-run step and a stale object is exactly
what the entry build links: `tools/check_assym_cswitch.py` reads the three `#define`s out of the
configuration's `assym.s` and the three `ldr rX, [r0, #N]` immediates out of the assembled
`out/xnu_asm_obj/cswitch.o`, **in source order** — because a set comparison cannot see two equal-sized
fields swapped, which is what a wrong `assym.s` produces. It fails the entry build by name, refuses all
three of its own mutations under `--selftest`, and its negative control is the real defect: run against
`out/xnu_assym/RELEASE/assym.s` it reports all three offsets wrong.

**Measured:** device run with exit 0, no `MACH Reboot` loop, device back on Android on its own; log
`/tmp/cancro-468-last_kmsg.txt`, 557941 bytes. Entry image `.text` 5208960, image 5421236, `.bss`
`0x8052b8c0..0x805831d8` (358680 bytes), headroom 1560104, payload `stage90-qcdt.img` 8439808 bytes,
`xnu_arm_entry.bin` md5 `5fe14a8703500dbbe289ee1f52be5152`.
