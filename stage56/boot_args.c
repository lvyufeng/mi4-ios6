#include "stage56.h"

void build_boot_args(struct boot_args *args, void *dt, uint32_t dt_len)
{
    memset(args, 0, sizeof(*args));

    args->Revision = BOOT_ARGS_REVISION;
    args->Version = BOOT_ARGS_VERSION;
    args->virtBase = 0;                  /* Stage56 boot args are still identity-based before MMU handoff. */
    args->physBase = STAGE56_BASE;
    args->memSize = RAM_CONSOLE_BASE - RAM_PHYS_BASE;
    args->topOfKernelData = (uint32_t)(uintptr_t)__stage56_image_end;

    args->machineType = MACHINE_TYPE_MSM8974;
    args->deviceTreeP = dt;
    args->deviceTreeLength = dt_len;

    const char cmd[] = "debug=0x144 serial=0x1 mi4ios6.stage=56 msm8974=cancro xnu-workspace xnu-platform-graph xnu-obj-subset arm-pe-bootargs arm-consistent-debug xnu-bootstrap-contract xnu-link-proof no-xnu-build no-xnu-exec no-macho-exec no-platform-runtime-exec no-persist-write";
    memcpy(args->CommandLine, cmd, sizeof(cmd));

    args->bootFlags = 0;
    args->memSizeActual = RAM_TOP - RAM_PHYS_BASE;
}
