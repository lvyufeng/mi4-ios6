#include "stage68.h"

void build_boot_args(struct boot_args *args, void *dt, uint32_t dt_len)
{
    memset(args, 0, sizeof(*args));

    args->Revision = BOOT_ARGS_REVISION;
    args->Version = BOOT_ARGS_VERSION;
    args->virtBase = 0;                  /* Stage68 boot args are still identity-based before MMU handoff. */
    args->physBase = STAGE68_BASE;
    args->memSize = RAM_CONSOLE_BASE - RAM_PHYS_BASE;
    args->topOfKernelData = (uint32_t)(uintptr_t)__stage68_image_end;

    args->machineType = MACHINE_TYPE_MSM8974;
    args->deviceTreeP = dt;
    args->deviceTreeLength = dt_len;

    const char cmd[] = "debug=0x144 serial=0x1 mi4ios6.stage=68 xnu-cg xnu-bs xnu-pmap-bs pexpert-hook-ready iokit-platform-scaffold iokit-match-dryrun iokit-registry-service-dryrun iokit-provider-plane-dryrun iokit-catalog-property-dryrun pmap-ref-only st68dt=0x68";
    const uint32_t cmd_len = (sizeof(cmd) < sizeof(args->CommandLine)) ?
        (uint32_t)sizeof(cmd) : (uint32_t)sizeof(args->CommandLine);
    memcpy(args->CommandLine, cmd, cmd_len);
    args->CommandLine[BOOT_LINE_LENGTH - 1u] = '\0';

    args->bootFlags = 0;
    args->memSizeActual = RAM_TOP - RAM_PHYS_BASE;
}
