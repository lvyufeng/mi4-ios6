# Stage1 Boot Wrapper Plan

Goal: evolve Stage0 from a one-shot marker payload into a minimal boot-wrapper contract for XNU/Darwin bring-up.

Stage1 is still not XNU. It is a self-contained ARMv7 payload loaded as the Android boot image `kernel` through non-persistent `fastboot boot`. It proves that we can build the early data structures that an XNU-style ARM entry path expects, and that we can log those structures through the Android ram_console path before rebooting.

## Stage0 result carried forward

Stage0 proved on real cancro hardware:

- the stock bootloader executes our raw ARMv7 payload at `0x00008000`,
- the bootloader requires QCDT in the legacy Android v0 `dt_size` field,
- `RAM_CONSOLE_BASE = 0xde500000` is correct,
- Android persistent RAM format with signature `DBGC` is accepted,
- `MSM8974_PSHOLD = 0xfc4ab000` reboots the device,
- `/proc/last_kmsg` recovers the marker over USB after reboot.

## Stage1 responsibilities

Stage1 should add:

1. A reusable persistent log writer instead of a single hard-coded copy loop.
2. A reserved boot-wrapper memory layout after the payload image.
3. A synthetic XNU ARM `boot_args` structure modeled on public XNU ARM headers.
4. A minimal Apple-format flattened device tree stub.
5. A test-kernel handoff stub that receives the `boot_args` pointer, validates key fields, logs them, and returns.
6. A controlled reboot through PS_HOLD.

This keeps the test safe and non-persistent while exercising the next kernel handoff boundary.

## Source reference

Public XNU ARM boot argument definition:

```text
external/xnu-4570.1.46/pexpert/pexpert/arm/boot.h
```

Relevant fields:

```c
typedef struct boot_args {
    uint16_t Revision;
    uint16_t Version;
    uint32_t virtBase;
    uint32_t physBase;
    uint32_t memSize;
    uint32_t topOfKernelData;
    Boot_Video Video;
    uint32_t machineType;
    void *deviceTreeP;
    uint32_t deviceTreeLength;
    char CommandLine[256];
    uint32_t bootFlags;
    uint32_t memSizeActual;
} boot_args;
```

Stage1 will use revision/version 2, because public XNU ARM code has fields for `bootFlags` and `memSizeActual`.

## Proposed memory layout

Stage1 remains linked at the Android kernel entry/load address:

```text
0x00008000  stage1 code/rodata/data
```

Static payload-local data symbols hold the Stage1 test structures:

```text
boot_args_struct      XNU-style boot_args
apple_dt_blob         minimal Apple flattened tree stub
test_state           values written by handoff stub
```

The first version does not attempt dynamic allocation or MMU setup. Pointers in `boot_args` are physical identity pointers, matching the pre-MMU test environment.

## Initial boot_args values

```text
Revision         = 2
Version          = 2
virtBase         = 0x00000000   # placeholder for identity/no-MMU test
physBase         = 0x00008000   # payload/kernel physical base
memSize          = 0x5e4f8000   # usable RAM excluding top 2 MiB ram_console (0xde500000 - 0x80008000 approx)
topOfKernelData  = end of stage1 image, 4 KiB aligned
Video            = zeroed/headless
machineType      = 0x8974       # experimental MSM8974 tag, not an Apple machine ID
deviceTreeP      = &apple_dt_blob
deviceTreeLength = sizeof(apple_dt_blob)
CommandLine      = "debug=0x144 serial=0x1 mi4ios6.stage=1 msm8974=cancro"
bootFlags        = 0
memSizeActual    = 0x5e700000   # observed System RAM span from 0x80000000 to 0xde6fffff
```

The exact memory-size values are placeholders for the early wrapper contract. A later wrapper must derive them from the bootloader-provided tags/FDT/QCDT or from a hard-coded board profile with reserved ranges.

## Minimal Apple device tree stub

Apple flattened device tree nodes are not Linux FDT blobs. The public header uses:

```c
typedef struct OpaqueDTEntry {
    uint32_t nProperties;
    uint32_t nChildren;
} DeviceTreeNode;

typedef struct DeviceTreeNodeProperty {
    char name[32];
    uint32_t length;
    // value bytes padded to 4-byte boundary
} DeviceTreeNodeProperty;
```

Stage1 will build a single root node with a few string properties:

```text
root node:
  name        = "/"
  compatible  = "qcom,msm8974-xnu-stage1"
  model       = "Xiaomi Mi 4 cancro Stage1"
  target-type = "cancro"
```

This is not yet enough for XNU platform expert, but it proves that the wrapper can produce a correctly shaped Apple-DT pointer and length inside `boot_args`.

## Success criteria

1. `./stage1/build.sh` produces `out/stage1/stage1-qcdt.img` with `dt_size != 0`.
2. `sudo fastboot boot out/stage1/stage1-qcdt.img` is accepted.
3. The payload logs Stage1 start, boot_args pointer, device tree pointer/length, and handoff-stub validation.
4. The device automatically reboots.
5. Android `/proc/last_kmsg` contains `MI4IOS6_STAGE1` and `handoff ok` markers.

## Next stage after Stage1

If Stage1 succeeds, Stage2 should add enough early runtime support to call C code or a small XNU-adjacent entry stub:

- stack setup,
- `.bss` zeroing,
- simple `memcpy`/`memset` primitives,
- a real append-only ram_console writer,
- generated Apple-DT nodes for CPUs, memory, GIC, timer, and chosen,
- possibly a tiny MMU identity-map setup before attempting public XNU ARM code paths.
