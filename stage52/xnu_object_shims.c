#include <stdint.h>
#include <stddef.h>
#include <mach/boolean.h>
#include <mach/machine/vm_types.h>
#include <pexpert/device_tree.h>

#define STAGE52_XNU_SHIM_BOOT_ARGS_SIZE 256u
#define STAGE52_XNU_SHIM_KALLOC_BYTES   4096u

void *memset(void *dst, int c, size_t n);

static char g_stage52_xnu_shim_boot_args[STAGE52_XNU_SHIM_BOOT_ARGS_SIZE] =
    "debug=0x144 serial=0x1 mi4ios6.stage=52 msm8974=cancro public-xnu-object-subset xnu-link-proof stage52dt=0x52";
static uint64_t g_stage52_xnu_shim_kalloc[(STAGE52_XNU_SHIM_KALLOC_BYTES + sizeof(uint64_t) - 1u) / sizeof(uint64_t)];
static vm_size_t g_stage52_xnu_shim_kalloc_used;

char *PE_boot_args(void)
{
    return g_stage52_xnu_shim_boot_args;
}

void *kalloc(vm_size_t size)
{
    vm_size_t aligned_size = (size + 7u) & ~(vm_size_t)7u;
    void *ptr;

    if (aligned_size == 0u || aligned_size > (STAGE52_XNU_SHIM_KALLOC_BYTES - g_stage52_xnu_shim_kalloc_used)) {
        return NULL;
    }

    ptr = &g_stage52_xnu_shim_kalloc[g_stage52_xnu_shim_kalloc_used];
    g_stage52_xnu_shim_kalloc_used += aligned_size;
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
