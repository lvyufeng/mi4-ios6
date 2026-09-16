/* A boot_args that satisfies the contract in XNU's own entry code.
 *
 * Why a *second* boot_args
 * ------------------------
 * The ladder already builds one (boot_args.c), and it is deliberately non-conforming:
 * `virtBase = 0`, `physBase = 0x00008000`, because the Stage-owned arm_init ladder is
 * an identity-mapped simulation and validates `physBase == STAGE90_BASE` on the way in.
 * That object is not something XNU could consume, and changing it in place would break
 * the ladder.
 *
 * This module produces the other one - the object a real kernel's `_start` would read
 * - behind STAGE90_XNU_BOOT_ARGS (default off, so it cannot affect a default run). The
 * contract it satisfies is worked out from `osfmk/arm/start.s` and recorded in
 * docs/reference/xnu-handoff-contract.md.
 *
 * The unaligned-physBase problem, and why it dissolves
 * ---------------------------------------------------
 * `start.s` ORs `physBase` straight into a 1 MB section descriptor, so `0x00008000` is
 * not merely inelegant - it sets AP[2] and produces a descriptor with the wrong
 * protection. The payload is loaded at PA 0x8000 (build.sh's `--kernel_offset`), which
 * looks like it forces either a new load address or a relocation pass.
 *
 * It does not. `physBase = 0x00000000` is 1 MB aligned, and the image at 0x8000 is
 * simply *inside* the region [physBase, physBase + memSize). That is exactly what the
 * Android boot image's kernel_offset means: an offset within the kernel region, not a
 * base. So reporting 0 is both conforming and true, and nothing has to move.
 *
 * It is also consistent with the pmap this project already builds: with
 * virtBase = 0x80000000 the correspondences are VA = 0x80000000 + PA, and full_pmap
 * maps VA 0x80000000..0x800fffff to PA 0..0xfffff through L2 pages. XNU's own section
 * mapping and this project's L2 mapping agree on the same translation, which is a
 * useful independent check on the choice.
 *
 * What XNU does with topOfKernelData
 * ---------------------------------
 * `start.s` writes `topOfKernelData | TTBR_SETUP` to TTBR0 and TTBR1, then clears a run
 * of translation table entries starting there. That run is 10240 entries - 40960 bytes,
 * which is 4 pages of trampoline L1 + 4 pages of CPU L1 + 1 page of L2 for a
 * non-1MB-aligned end + 1 page for the high exception vectors (start.s's own comment at
 * line 246 counts "4 + 4 + 1" pages and a separate high-vector table). So the region
 * must be writable RAM, 16 KB aligned (TTBR's low 14 bits are reserved for TTBR_SETUP),
 * above the image, and big enough for those 10 pages.
 */

#include "stage90.h"

/*
 * The ABI. These are not cosmetic: start.s loads these four fields by hand at fixed
 * offsets derived from `offsetof()` in its own tree (osfmk/arm/genassym.c), so a
 * mismatch is not a build error and not a fault - it is XNU silently using the wrong
 * 32-bit word as the physical base of memory. tools/check_boot_args_abi.py verifies the
 * full layout against pexpert/pexpert/arm/boot.h at build time; these asserts are the
 * in-payload half of the same check, so a drift cannot reach hardware even if the host
 * tool is skipped (external/ absent, ABI check silenced).
 */
_Static_assert(sizeof(struct boot_args) == 320u,
               "boot_args size must match XNU's ARM layout");
_Static_assert(offsetof(struct boot_args, virtBase) == 4u, "BA_VIRT_BASE");
_Static_assert(offsetof(struct boot_args, physBase) == 8u, "BA_PHYS_BASE");
_Static_assert(offsetof(struct boot_args, memSize) == 12u, "BA_MEM_SIZE");
_Static_assert(offsetof(struct boot_args, topOfKernelData) == 16u, "BA_TOP_OF_KERNEL_DATA");
_Static_assert(offsetof(struct boot_args, machineType) == 44u, "machineType offset");
_Static_assert(offsetof(struct boot_args, deviceTreeP) == 48u, "deviceTreeP offset");
_Static_assert(offsetof(struct boot_args, CommandLine) == 56u, "CommandLine offset");

/* virtBase: the kernel's link-time virtual base. Same value the project's own pmap
 * uses for its high alias (STAGE90_VIRT_BASE in xnu_arm_vm_init_full_pmap.c), so the
 * contract and the existing translation agree rather than describing different worlds. */
#define STAGE90_XNU_BA_VIRT_BASE     0x80000000u

/* physBase: 1 MB aligned, and the start of the region the image lives in. See the file
 * comment - reporting 0 is what makes the payload's 0x8000 load address conforming. */
#define STAGE90_XNU_BA_PHYS_BASE     0x00000000u

/*
 * memSize: how much contiguous physical memory the kernel may map with sections.
 *
 * Deliberately conservative. XNU maps physBase..physBase+memSize at virtBase in 1 MB
 * sections, so this is a claim about which physical addresses are real RAM - and below
 * 0x80000000 that is not a safe assumption for a large span, because MSM8974 puts
 * device registers and non-RAM windows down there. 2 MB is the region the payload has
 * actually exercised: mmu.c's identity table maps PA 0-2 MB and the payload has run
 * from it for ninety stages. Raising this is a later phase's job, and it should be
 * raised only with a memory map to justify it, not by guessing.
 */
#define STAGE90_XNU_BA_MEM_SIZE      0x00200000u

/* The run start.s clears at topOfKernelData: 10240 TTEs = 10 pages = 40960 bytes.
 * Derived from the invalidation loop (PGBYTES>>2, then *5, then *2) and cross-checked
 * against the page tally in its own comment at line 246. */
#define STAGE90_XNU_BA_TABLE_BYTES   0x0000a000u

/* TTBR's low 14 bits carry TTBR_SETUP, so the table base must be 16 KB aligned. */
#define STAGE90_XNU_BA_TABLE_ALIGN   0x00004000u

static struct boot_args g_stage90_xnu_boot_args;
static struct stage90_xnu_boot_args_result g_result;

static uint32_t xba_align_up(uint32_t value, uint32_t align)
{
    return (value + (align - 1u)) & ~(align - 1u);
}

static uint32_t xba_checksum(const struct stage90_xnu_boot_args_result *r)
{
    const uint32_t *words = (const uint32_t *)r;
    uint32_t count = (uint32_t)(offsetof(struct stage90_xnu_boot_args_result, checksum) / sizeof(uint32_t));
    uint32_t chk = 0u;

    for (uint32_t i = 0u; i < count; i++) {
        chk ^= words[i];
    }
    return chk;
}

static void xba_log(const struct stage90_xnu_boot_args_result *r)
{
    xnu_log_puts("stage90 xnu_boot_args result:\n");
    xnu_log_kv32("xnu_ba_virt_base", r->virt_base);
    xnu_log_kv32("xnu_ba_phys_base", r->phys_base);
    xnu_log_kv32("xnu_ba_mem_size", r->mem_size);
    xnu_log_kv32("xnu_ba_top_of_kernel_data", r->top_of_kernel_data);
    xnu_log_kv32("xnu_ba_image_base", r->image_base);
    xnu_log_kv32("xnu_ba_image_end", r->image_end);
    xnu_log_kv32("xnu_ba_table_bytes", r->table_bytes);
    xnu_log_kv32("xnu_ba_device_tree_ptr", r->device_tree_ptr);
    xnu_log_kv32("xnu_ba_device_tree_length", r->device_tree_length);
    xnu_log_kv32("xnu_ba_machine_type", r->machine_type);
    xnu_log_kv32("xnu_ba_command_line_len", r->command_line_len);
    xnu_log_kv32("xnu_ba_checks", r->checks);
    xnu_log_kv32("xnu_ba_failures", r->failures);
    xnu_log_kv32("xnu_ba_checksum", r->checksum);
}

int stage90_xnu_boot_args_prepare(void *dt, uint32_t dt_len)
{
    struct boot_args *a = &g_stage90_xnu_boot_args;
    struct stage90_xnu_boot_args_result *r = &g_result;
    uint32_t image_end;
    uint32_t checks = 0u;
    uint32_t failures = 0u;

    memset(a, 0, sizeof(*a));
    memset(r, 0, sizeof(*r));

    r->version = STAGE90_XNU_BOOT_ARGS_VERSION;
    r->size = sizeof(*r);

    image_end = (uint32_t)(uintptr_t)__stage90_image_end;

    a->Revision = BOOT_ARGS_REVISION;
    a->Version = BOOT_ARGS_VERSION;
    a->virtBase = STAGE90_XNU_BA_VIRT_BASE;
    a->physBase = STAGE90_XNU_BA_PHYS_BASE;
    a->memSize = STAGE90_XNU_BA_MEM_SIZE;
    /* Tables go above the image, so they cannot overlap it. */
    a->topOfKernelData = xba_align_up(image_end, STAGE90_XNU_BA_TABLE_ALIGN);
    a->machineType = MACHINE_TYPE_MSM8974;
    a->deviceTreeP = dt;
    a->deviceTreeLength = dt_len;
    a->bootFlags = 0;
    a->memSizeActual = STAGE90_XNU_BA_MEM_SIZE;

    /*
     * A command line XNU can parse: NUL-terminated well inside CommandLine, and
     * carrying the marker that says which flow produced it.
     */
    {
        static const char cmd[] = "xnu-handoff-contract mi4ios6.stage=90 debug=0x144";
        uint32_t n = (uint32_t)(sizeof(cmd) - 1u);
        if (n >= BOOT_LINE_LENGTH) {
            n = BOOT_LINE_LENGTH - 1u;
        }
        memcpy(a->CommandLine, cmd, n);
        a->CommandLine[n] = '\0';
        r->command_line_len = n;
    }

    /* --- the contract's invariants, each one explicit --- */

    /* start.s ORs physBase into a section descriptor. */
    checks++;
    if ((a->physBase & 0x000fffffu) != 0u) {
        failures |= STAGE90_XNU_BA_FAIL_PHYS_ALIGN;
    }
    checks++;
    if ((a->virtBase & 0x000fffffu) != 0u) {
        failures |= STAGE90_XNU_BA_FAIL_VIRT_ALIGN;
    }

    /* TTBR's low 14 bits are TTBR_SETUP, so the L1 base must be 16 KB aligned. */
    checks++;
    if ((a->topOfKernelData & (STAGE90_XNU_BA_TABLE_ALIGN - 1u)) != 0u) {
        failures |= STAGE90_XNU_BA_FAIL_TABLE_ALIGN;
    }

    /* Above the image: XNU writes its tables there and must not be inside the kernel. */
    checks++;
    if (a->topOfKernelData < image_end) {
        failures |= STAGE90_XNU_BA_FAIL_TABLE_BELOW_IMAGE;
    }

    /* The image has to be inside the region XNU will map. */
    checks++;
    if (image_end > (a->physBase + a->memSize)) {
        failures |= STAGE90_XNU_BA_FAIL_IMAGE_OUTSIDE;
    }

    /* And the tables have to be inside it too, with room for all ten pages. */
    checks++;
    if ((a->topOfKernelData + STAGE90_XNU_BA_TABLE_BYTES) > (a->physBase + a->memSize)) {
        failures |= STAGE90_XNU_BA_FAIL_TABLE_OUTSIDE;
    }

    /* Revision/Version: start.s does not check these, but PE_init_platform does. */
    checks++;
    if (a->Revision != BOOT_ARGS_REVISION || a->Version != BOOT_ARGS_VERSION) {
        failures |= STAGE90_XNU_BA_FAIL_REV_VERSION;
    }

    /* A device tree is not optional; start.s's callers read it immediately. */
    checks++;
    if (a->deviceTreeP == NULL || a->deviceTreeLength == 0u) {
        failures |= STAGE90_XNU_BA_FAIL_DEVICE_TREE;
    }

    /* The command line must be terminated within the array XNU will scan. */
    checks++;
    if (a->CommandLine[BOOT_LINE_LENGTH - 1u] != '\0') {
        failures |= STAGE90_XNU_BA_FAIL_COMMAND_LINE;
    }

    r->virt_base = a->virtBase;
    r->phys_base = a->physBase;
    r->mem_size = a->memSize;
    r->top_of_kernel_data = a->topOfKernelData;
    /* The image base is the linker script's load address (linker.ld: `. = 0x00008000`),
     * which is the same value STAGE90_BASE carries. There is no __stage90_image_start
     * symbol, and adding one would be a linker change for no gain. */
    r->image_base = STAGE90_BASE;
    r->image_end = image_end;
    r->table_bytes = STAGE90_XNU_BA_TABLE_BYTES;
    r->device_tree_ptr = (uint32_t)(uintptr_t)a->deviceTreeP;
    r->device_tree_length = a->deviceTreeLength;
    r->machine_type = a->machineType;
    r->checks = checks;
    r->failures = failures;
    r->status = (failures == 0u) ? STAGE90_STATUS_OK : STAGE90_STATUS_FAIL(failures);
    r->checksum = xba_checksum(r);

    xba_log(r);

    if (failures != 0u) {
        xnu_log_puts("stage90 xnu_boot_args: contract not satisfied\n");
        return -1;
    }
    xnu_log_puts("stage90 xnu_boot_args: conforms to the XNU entry contract\n");
    return 0;
}

const struct boot_args *stage90_xnu_boot_args(void)
{
    return &g_stage90_xnu_boot_args;
}

const struct stage90_xnu_boot_args_result *stage90_xnu_boot_args_result(void)
{
    return &g_result;
}
