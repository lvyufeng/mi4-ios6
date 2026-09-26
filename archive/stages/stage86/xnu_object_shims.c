#include <stdint.h>
#include <stddef.h>
#include <mach/boolean.h>
#include <mach/machine/vm_types.h>
#include <pexpert/device_tree.h>
#include <pexpert/pexpert.h>
#include <libkern/OSAtomic.h>
#include <machine/machine_routines.h>

#define STAGE86_XNU_SHIM_KALLOC_BYTES   4096u
#define STAGE86_XNU_CONSISTENT_DEBUG_WINDOW_BYTES 0x4000u

void *memset(void *dst, int c, size_t n);

static boot_args g_stage86_xnu_public_arm_boot_args = {
    .Revision = kBootArgsRevision2,
    .Version = kBootArgsVersion2,
    .virtBase = 0u,
    .physBase = 0x00008000u,
    .memSize = 0x5e500000u,
    .topOfKernelData = 0u,
    .machineType = 0x8974u,
    .deviceTreeP = NULL,
    .deviceTreeLength = 0u,
    .CommandLine = "debug=0x144 mi4ios6.stage=83 xnu-pe-init-false xnu-postpe cpu-topo bootcpu rtclock xnu-armvm live-pmap tlb-live pmap-restore prevm-pexpert dtinit-facts peid-machine no-pub-peinit no-pub-dtinit no-pub-peid no-pub-thread no-pub-cpuboot no-pub-rtclock",
    .bootFlags = 0u,
    .memSizeActual = 0x5e700000u,
};

PE_state_t PE_state = {
    .initialized = TRUE,
    .video = {0},
    .deviceTreeHead = NULL,
    .bootArgs = &g_stage86_xnu_public_arm_boot_args,
};

static uint8_t g_stage86_xnu_shim_kalloc[STAGE86_XNU_SHIM_KALLOC_BYTES];
static vm_size_t g_stage86_xnu_shim_kalloc_used;
static uint8_t g_stage86_xnu_consistent_debug_window[STAGE86_XNU_CONSISTENT_DEBUG_WINDOW_BYTES] __attribute__((aligned(8)));

void *kalloc(vm_size_t size)
{
    vm_size_t aligned_size = (size + 7u) & ~(vm_size_t)7u;
    void *ptr;

    if (aligned_size == 0u || aligned_size > (STAGE86_XNU_SHIM_KALLOC_BYTES - g_stage86_xnu_shim_kalloc_used)) {
        return NULL;
    }

    ptr = &g_stage86_xnu_shim_kalloc[g_stage86_xnu_shim_kalloc_used];
    g_stage86_xnu_shim_kalloc_used += aligned_size;
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
    /* Stage84 compile/link support only; no debugger runtime is entered. */
}

void cnputc(char c)
{
    (void)c;
    /* Stage84 compile/link support only; public-XNU PE_putc is never called on hardware. */
}

void vcattach(void)
{
    /* Stage84 compile/link support only; no console device is attached. */
}

Boolean OSCompareAndSwap64(UInt64 oldValue, UInt64 newValue, volatile UInt64 *address)
{
    if (!address) {
        return FALSE;
    }
    if (*address != oldValue) {
        return FALSE;
    }
    *address = newValue;
    return TRUE;
}

vm_map_address_t ml_map_high_window(vm_offset_t phys_addr, vm_size_t len)
{
    (void)phys_addr;
    /* Stage84 host-proof support only; public consistent-debug code is never executed on hardware. */
    if (len == 0u || len > sizeof(g_stage86_xnu_consistent_debug_window)) {
        return 0u;
    }
    return (vm_map_address_t)(uintptr_t)g_stage86_xnu_consistent_debug_window;
}
