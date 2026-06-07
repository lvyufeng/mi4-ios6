#include <stdint.h>
#include <stddef.h>
#include <mach/boolean.h>
#include <mach/machine/vm_types.h>
#include <pexpert/device_tree.h>
#include <pexpert/pexpert.h>

#define STAGE54_XNU_SHIM_KALLOC_BYTES   4096u

void *memset(void *dst, int c, size_t n);

static boot_args g_stage54_xnu_public_arm_boot_args = {
    .Revision = kBootArgsRevision2,
    .Version = kBootArgsVersion2,
    .virtBase = 0u,
    .physBase = 0x00008000u,
    .memSize = 0x5e500000u,
    .topOfKernelData = 0u,
    .machineType = 0x8974u,
    .deviceTreeP = NULL,
    .deviceTreeLength = 0u,
    .CommandLine = "debug=0x144 serial=0x1 mi4ios6.stage=54 msm8974=cancro public-xnu-object-subset xnu-compile-graph xnu-platform-graph arm-pe-bootargs xnu-link-proof stage54dt=0x54",
    .bootFlags = 0u,
    .memSizeActual = 0x5e700000u,
};

PE_state_t PE_state = {
    .initialized = TRUE,
    .video = {0},
    .deviceTreeHead = NULL,
    .bootArgs = &g_stage54_xnu_public_arm_boot_args,
};

static uint8_t g_stage54_xnu_shim_kalloc[STAGE54_XNU_SHIM_KALLOC_BYTES];
static vm_size_t g_stage54_xnu_shim_kalloc_used;

void *kalloc(vm_size_t size)
{
    vm_size_t aligned_size = (size + 7u) & ~(vm_size_t)7u;
    void *ptr;

    if (aligned_size == 0u || aligned_size > (STAGE54_XNU_SHIM_KALLOC_BYTES - g_stage54_xnu_shim_kalloc_used)) {
        return NULL;
    }

    ptr = &g_stage54_xnu_shim_kalloc[g_stage54_xnu_shim_kalloc_used];
    g_stage54_xnu_shim_kalloc_used += aligned_size;
    memset(ptr, 0, size);
    return ptr;
}

void kfree(void *data, vm_size_t size)
{
    (void)data;
    (void)size;
    /* Compile-only monotonic shim; no object-subset runtime is executed on hardware. */
}

int IODTGetDefault(const char *key, void *infoAddr, unsigned int infoSize)
{
    (void)key;
    (void)infoAddr;
    (void)infoSize;
    return 1;
}

void Debugger(const char *reason)
{
    (void)reason;
    /* Stage54 compile/link support only; no debugger runtime is entered. */
}

void cnputc(char c)
{
    (void)c;
    /* Stage54 compile/link support only; public-XNU PE_putc is never called on hardware. */
}

void vcattach(void)
{
    /* Stage54 compile/link support only; no console device is attached. */
}
