# Experiment 07 — Stage4 Timebase + Exception Vectors

Date: 2026-06-04

Goal: build on Stage3 by installing our own ARMv7 exception vector table and using the ARM generic timer as an early timebase from the freestanding C runtime.

Stage4 still does **not** run XNU or iOS. It provides two pieces needed before attempting more fragile XNU-adjacent code:

1. a minimal exception safety net (VBAR + handlers that log and reboot),
2. a monotonic timebase and `delay_us()` based on `CNTFRQ/CNTPCT`.

## Safety model

- Non-persistent `fastboot boot` only.
- No partition flash/erase/write.
- IRQ/FIQ remain masked.
- The Stage4 GIC code remains read-only.
- Stage4 deliberately triggers an undefined-instruction exception at the end of the run to prove the vector table and handler are active. The handler logs through ram_console and reboots through PS_HOLD.

## New source files

Stage4 is based on Stage3 and adds:

```text
stage4/vectors.S   # ARMv7 vector table + exception logger + deliberate undef trigger
stage4/timebase.c  # CNTFRQ/CNTPCT timebase and delay_us self-test
```

Other Stage4 files are copied/evolved from Stage3.

## Built image

```bash
./stage4/build.sh
```

Final image before hardware run:

```text
out/stage4/stage4-qcdt.img
sha256=50c97c02dac194b2e11e58176de7f2e2c30bfe398a3af73adb2853b2526294b9
```

Boot image parse:

```text
magic=ANDROID!
page_size=2048
kernel_size=10184 (0x27c8)
kernel_addr=32768 (0x8000)
dt_size=2521088 (0x267800)
cmdline=stage4 mi4ios6=stage4 c-runtime apple-dt
```

Important symbols:

```text
00008000 T _start
000080a0 T stage4_vectors
00008104 T stage4_exception_common
000081b8 T trigger_stage4_undef_test
000092fc T run_timebase_selftest
000095cc T stage4_main
00011000 B __stage4_image_end
```

The vector table is 32-byte aligned and installed into VBAR by `start.S` after stack and `.bss` setup.

## Expected log markers

A successful run should include all prior Stage3 markers plus:

```text
MI4IOS6_STAGE4 vector base installed at 0x000080a0
MI4IOS6_STAGE4 timebase_cntfrq=0x0124f800
MI4IOS6_STAGE4 timebase selftest begin
MI4IOS6_STAGE4 delay_1000us_measured_us=...
MI4IOS6_STAGE4 delay_5000us_measured_us=...
MI4IOS6_STAGE4 timebase selftest ok
MI4IOS6_STAGE4 triggering deliberate undefined-instruction exception
MI4IOS6_STAGE4 exception: undefined lr=... spsr=...
MI4IOS6_STAGE4 attempting MSM8974 PS_HOLD reset
```

## Hardware run result

Hardware run succeeded.

Command:

```bash
sudo adb reboot bootloader
sudo fastboot boot /mnt/data/mi4-ios6/out/stage4/stage4-qcdt.img
```

Fastboot output:

```text
Sending 'boot.img' (2474 KB)                       OKAY [  0.079s]
Booting                                            OKAY [  0.005s]
Finished. Total time: 0.089s
```

The phone returned to Android automatically after ~30 seconds; ADB transport id changed from 25 to 26.

Recovered persistent log:

```bash
sudo adb exec-out 'cat /proc/last_kmsg' > /tmp/cancro-stage4-last_kmsg.txt
```

The recovered log was 3422 bytes and contained 80 `MI4IOS6_STAGE4` markers.

## Key results

Stage4 repeated the Stage3 boot_args, Apple-DT, CP15, timer, and GIC probes successfully, then added the new vector/timebase tests.

### VBAR / vector installation

```text
MI4IOS6_STAGE4 cp15_vbar=0x000080a0
MI4IOS6_STAGE4 vector base installed at 0x000080a0
```

This confirms that `start.S` installed `stage4_vectors` into the ARM VBAR and that later C code read back the expected vector base.

### Timebase / delay_us

```text
MI4IOS6_STAGE4 timebase_cntfrq=0x0124f800
MI4IOS6_STAGE4 timebase_boot_ticks_lo=0x054fc506
MI4IOS6_STAGE4 timebase_boot_ticks_hi=0x00000000
MI4IOS6_STAGE4 timebase selftest begin
MI4IOS6_STAGE4 timebase_freq_hz=0x0124f800
MI4IOS6_STAGE4 timebase_a_lo=0x054fc95c
MI4IOS6_STAGE4 timebase_b_lo=0x0550146a
MI4IOS6_STAGE4 timebase_c_lo=0x05518b79
MI4IOS6_STAGE4 delay_1000us_measured_us=0x000003e8
MI4IOS6_STAGE4 delay_5000us_measured_us=0x00001388
MI4IOS6_STAGE4 timebase selftest ok
```

Interpretation:

- `CNTFRQ=0x0124f800 = 19,200,000 Hz`.
- `delay_us(1000)` measured as exactly `1000` microseconds by the same timebase.
- `delay_us(5000)` measured as exactly `5000` microseconds.
- The generic timer is suitable as the early Stage4 timebase and maps cleanly to the future XNU `ml_get_timebase`/`ml_init_timebase` concept.

### Deliberate undefined-instruction exception

```text
MI4IOS6_STAGE4 triggering deliberate undefined-instruction exception
MI4IOS6_STAGE4 exception: undefined lr=0x000081bc spsr=0x60000193
MI4IOS6_STAGE4 attempting MSM8974 PS_HOLD reset
```

Interpretation:

- The deliberate undefined instruction reached our custom vector table.
- The undefined-instruction handler had a valid banked stack, called the ram_console logger, printed LR/SPSR, and rebooted through PS_HOLD.
- This proves the Stage4 exception safety net works for at least undefined-instruction exceptions.

## Success criteria — met

1. bootloader accepted `stage4-qcdt.img`: yes
2. Stage3 boot_args/Apple-DT/hardware probes still passed: yes
3. VBAR was set to our vector table and read back: yes (`0x000080a0`)
4. generic timer delay self-test passed: yes
5. deliberate exception was caught and logged: yes
6. exception handler rebooted the phone through PS_HOLD: yes

## Next stage

Stage5 should start using this safety net for a small XNU-adjacent kernel skeleton:

- define a minimal `kernel_entry(struct boot_args *)` compiled as C,
- move more bring-up code behind that entry shape,
- add pexpert-like discovery functions that consume the Apple-DT nodes,
- map the Stage4 timebase into XNU-like `ml_get_timebase` routines,
- optionally add data-abort/prefetch-abort self-tests one at a time,
- only after that attempt a small subset of public XNU ARM early initialization concepts.
