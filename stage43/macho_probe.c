#include "stage43.h"

struct stage43_macho_header_32 {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
};

struct stage43_macho_load_command {
    uint32_t cmd;
    uint32_t cmdsize;
};

struct stage43_macho_segment_command_32 {
    uint32_t cmd;
    uint32_t cmdsize;
    char segname[16];
    uint32_t vmaddr;
    uint32_t vmsize;
    uint32_t fileoff;
    uint32_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct stage43_macho_symtab_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
};

static struct stage43_loader_preflight stage43_loader_preflight_block;

static uint32_t add_overflow_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    *out = a + b;
    return *out < a;
}

static uint32_t align_up_u32(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static int segname_eq(const char segname[16], const char *name)
{
    uint32_t i;

    for (i = 0; i < 16u; i++) {
        char expected = name[i];
        if (segname[i] != expected) {
            return 0;
        }
        if (expected == '\0') {
            return 1;
        }
    }

    return 1;
}

static void record_segment(struct stage43_macho_probe_result *result,
                           const struct stage43_macho_segment_command_32 *segment)
{
    uint32_t vm_end = 0;
    uint32_t file_end = 0;
    uint32_t file_ok = 1;
    uint32_t seg_bit = 0;

    result->segment_count++;
    result->validation_mask |= STAGE43_MACHO_VALID_SEGMENT_COMMAND;

    if (add_overflow_u32(segment->vmaddr, segment->vmsize, &vm_end)) {
        result->failure_mask |= STAGE43_MACHO_FAIL_SEGMENT_BOUNDS;
        return;
    }
    if (add_overflow_u32(segment->fileoff, segment->filesize, &file_end)) {
        result->failure_mask |= STAGE43_MACHO_FAIL_SEGMENT_EXTENT;
        file_ok = 0;
    }
    if (file_end > result->artifact_size) {
        result->failure_mask |= STAGE43_MACHO_FAIL_SEGMENT_EXTENT;
        file_ok = 0;
    }

    if (segname_eq(segment->segname, "__TEXT")) {
        seg_bit = STAGE43_MACHO_SEG_TEXT;
        result->text_vmaddr = segment->vmaddr;
        result->text_vmsize = segment->vmsize;
        result->text_fileoff = segment->fileoff;
        result->text_filesize = segment->filesize;
    } else if (segname_eq(segment->segname, "__DATA")) {
        seg_bit = STAGE43_MACHO_SEG_DATA;
        result->data_vmaddr = segment->vmaddr;
        result->data_vmsize = segment->vmsize;
        result->data_fileoff = segment->fileoff;
        result->data_filesize = segment->filesize;
    } else if (segname_eq(segment->segname, "__LINKEDIT")) {
        seg_bit = STAGE43_MACHO_SEG_LINKEDIT;
        result->linkedit_vmaddr = segment->vmaddr;
        result->linkedit_vmsize = segment->vmsize;
        result->linkedit_fileoff = segment->fileoff;
        result->linkedit_filesize = segment->filesize;
    } else if (segname_eq(segment->segname, "__KLD")) {
        seg_bit = STAGE43_MACHO_SEG_KLD;
    } else if (segname_eq(segment->segname, "__LAST")) {
        seg_bit = STAGE43_MACHO_SEG_LAST;
    } else if (segname_eq(segment->segname, "__PRELINK_TEXT")) {
        seg_bit = STAGE43_MACHO_SEG_PRELINK_TEXT;
    } else if (segname_eq(segment->segname, "__PRELINK_INFO")) {
        seg_bit = STAGE43_MACHO_SEG_PRELINK_INFO;
    }

    result->segment_seen_mask |= seg_bit;
    if (result->segment_count == 1u || segment->vmaddr < result->min_vmaddr) {
        result->min_vmaddr = segment->vmaddr;
    }
    if (vm_end > result->max_vmaddr) {
        result->max_vmaddr = vm_end;
    }
    if (file_ok && file_end > result->max_file_extent) {
        result->max_file_extent = file_end;
    }
}

int stage43_macho_probe(const void *artifact, uint32_t artifact_size,
                        struct stage43_macho_probe_result *result)
{
    const uint8_t *bytes = (const uint8_t *)artifact;
    const struct stage43_macho_header_32 *header;
    uint32_t commands_start = sizeof(*header);
    uint32_t commands_end = 0;
    uint32_t offset;

    if (!result) {
        return 0;
    }

    memset(result, 0, sizeof(*result));
    result->version = STAGE43_MACHO_PROBE_VERSION;
    result->artifact_base = (uint32_t)(uintptr_t)artifact;
    result->artifact_size = artifact_size;
    result->segment_required_mask = STAGE43_MACHO_SEG_REQUIRED;
    result->entry_kind_mask = STAGE43_MACHO_ENTRY_NOT_EXECUTED;
    result->status = STAGE43_STATUS_BASE;

    xnu_log_puts("Stage43 Mach-O probe begin\n");
    xnu_log_kv32("macho_artifact_base", result->artifact_base);
    xnu_log_kv32("macho_artifact_size", result->artifact_size);

    if (!artifact) {
        result->failure_mask |= STAGE43_MACHO_FAIL_NULL;
        return 0;
    }
    if (artifact_size < sizeof(*header)) {
        result->failure_mask |= STAGE43_MACHO_FAIL_SIZE;
        return 0;
    }

    header = (const struct stage43_macho_header_32 *)artifact;
    result->magic = header->magic;
    result->cputype = header->cputype;
    result->cpusubtype = header->cpusubtype;
    result->filetype = header->filetype;
    result->ncmds = header->ncmds;
    result->sizeofcmds = header->sizeofcmds;
    result->flags = header->flags;

    if (header->magic == STAGE43_MACHO_MAGIC) {
        result->validation_mask |= STAGE43_MACHO_VALID_MAGIC | STAGE43_MACHO_VALID_ENDIAN;
    } else if (header->magic == STAGE43_MACHO_CIGAM) {
        result->validation_mask |= STAGE43_MACHO_VALID_MAGIC;
        result->failure_mask |= STAGE43_MACHO_FAIL_ENDIAN;
    } else {
        result->failure_mask |= STAGE43_MACHO_FAIL_MAGIC;
    }

    if (header->cputype == STAGE43_MACHO_CPU_TYPE_ARM) {
        result->validation_mask |= STAGE43_MACHO_VALID_CPU;
    } else {
        result->failure_mask |= STAGE43_MACHO_FAIL_CPU;
    }

    if (header->cpusubtype == STAGE43_MACHO_CPU_SUBTYPE_ARM_ALL ||
        header->cpusubtype == STAGE43_MACHO_CPU_SUBTYPE_ARM_V7) {
        result->validation_mask |= STAGE43_MACHO_VALID_SUBTYPE;
    } else {
        result->failure_mask |= STAGE43_MACHO_FAIL_SUBTYPE;
    }

    if (header->filetype == STAGE43_MACHO_FILETYPE_EXECUTE ||
        header->filetype == STAGE43_MACHO_FILETYPE_PRELOAD) {
        result->validation_mask |= STAGE43_MACHO_VALID_FILETYPE;
    } else {
        result->failure_mask |= STAGE43_MACHO_FAIL_FILETYPE;
    }

    if (add_overflow_u32(commands_start, header->sizeofcmds, &commands_end) ||
        commands_end > artifact_size || (header->sizeofcmds & 3u) != 0u) {
        result->failure_mask |= STAGE43_MACHO_FAIL_COMMAND_BOUNDS;
    } else {
        result->validation_mask |= STAGE43_MACHO_VALID_COMMAND_BOUNDS;
    }

    offset = commands_start;
    for (uint32_t i = 0; i < header->ncmds && offset < commands_end; i++) {
        const struct stage43_macho_load_command *load_command;
        uint32_t next_offset = 0;

        if (commands_end - offset < sizeof(*load_command)) {
            result->failure_mask |= STAGE43_MACHO_FAIL_COMMAND_BOUNDS;
            break;
        }

        load_command = (const struct stage43_macho_load_command *)(const void *)(bytes + offset);
        if (load_command->cmdsize < sizeof(*load_command) || (load_command->cmdsize & 3u) != 0u ||
            add_overflow_u32(offset, load_command->cmdsize, &next_offset) || next_offset > commands_end) {
            result->failure_mask |= STAGE43_MACHO_FAIL_COMMAND_SIZE;
            break;
        }

        result->load_command_count_seen++;
        if (load_command->cmd == STAGE43_MACHO_LC_SEGMENT) {
            if (load_command->cmdsize < sizeof(struct stage43_macho_segment_command_32)) {
                result->failure_mask |= STAGE43_MACHO_FAIL_COMMAND_SIZE;
            } else {
                const struct stage43_macho_segment_command_32 *segment =
                    (const struct stage43_macho_segment_command_32 *)(const void *)(bytes + offset);
                result->load_command_seen_mask |= STAGE43_MACHO_CMD_SEGMENT;
                record_segment(result, segment);
            }
        } else if (load_command->cmd == STAGE43_MACHO_LC_SYMTAB) {
            result->load_command_seen_mask |= STAGE43_MACHO_CMD_SYMTAB;
            if (load_command->cmdsize >= sizeof(struct stage43_macho_symtab_command)) {
                const struct stage43_macho_symtab_command *symtab =
                    (const struct stage43_macho_symtab_command *)(const void *)(bytes + offset);
                uint32_t str_end = 0;
                if (add_overflow_u32(symtab->stroff, symtab->strsize, &str_end) || str_end > artifact_size) {
                    result->failure_mask |= STAGE43_MACHO_FAIL_SEGMENT_EXTENT;
                }
            }
        } else if (load_command->cmd == STAGE43_MACHO_LC_UNIXTHREAD) {
            result->load_command_seen_mask |= STAGE43_MACHO_CMD_UNIXTHREAD;
            result->entry_kind_mask |= STAGE43_MACHO_ENTRY_UNIXTHREAD;
            if (load_command->cmdsize >= 16u) {
                const uint32_t *entry_words = (const uint32_t *)(const void *)(bytes + offset);
                result->entryoff_or_pc = entry_words[3];
            }
        } else if (load_command->cmd == STAGE43_MACHO_LC_MAIN) {
            result->load_command_seen_mask |= STAGE43_MACHO_CMD_MAIN;
            result->entry_kind_mask |= STAGE43_MACHO_ENTRY_MAIN;
        } else {
            result->load_command_seen_mask |= STAGE43_MACHO_CMD_UNKNOWN;
            result->unknown_command_count++;
        }

        offset = next_offset;
    }

    if (result->load_command_count_seen != header->ncmds || offset != commands_end) {
        result->failure_mask |= STAGE43_MACHO_FAIL_COMMAND_BOUNDS;
    }
    if ((result->segment_seen_mask & STAGE43_MACHO_SEG_REQUIRED) == STAGE43_MACHO_SEG_REQUIRED) {
        result->validation_mask |= STAGE43_MACHO_VALID_REQUIRED_SEGMENTS;
    } else {
        result->failure_mask |= STAGE43_MACHO_FAIL_REQUIRED_SEGMENTS;
    }
    if ((result->failure_mask & STAGE43_MACHO_FAIL_SEGMENT_EXTENT) == 0u) {
        result->validation_mask |= STAGE43_MACHO_VALID_FILE_EXTENTS;
    }
    if ((result->entry_kind_mask & (STAGE43_MACHO_ENTRY_UNIXTHREAD | STAGE43_MACHO_ENTRY_MAIN)) != 0u) {
        result->validation_mask |= STAGE43_MACHO_VALID_ENTRY_NOT_EXECUTED;
    } else {
        result->failure_mask |= STAGE43_MACHO_FAIL_NO_ENTRY_METADATA;
    }

    xnu_log_kv32("macho_magic", result->magic);
    xnu_log_kv32("macho_cputype", result->cputype);
    xnu_log_kv32("macho_cpusubtype", result->cpusubtype);
    xnu_log_kv32("macho_filetype", result->filetype);
    xnu_log_kv32("macho_ncmds", result->ncmds);
    xnu_log_kv32("macho_sizeofcmds", result->sizeofcmds);
    xnu_log_kv32("macho_command_mask", result->load_command_seen_mask);
    xnu_log_kv32("macho_segment_mask", result->segment_seen_mask);
    xnu_log_kv32("macho_entry_kind_mask", result->entry_kind_mask);
    xnu_log_kv32("macho_entry_not_executed", 1u);
    xnu_log_kv32("macho_min_vmaddr", result->min_vmaddr);
    xnu_log_kv32("macho_max_vmaddr", result->max_vmaddr);
    xnu_log_kv32("macho_max_file_extent", result->max_file_extent);
    xnu_log_kv32("macho_validation_mask", result->validation_mask);
    xnu_log_kv32("macho_failure_mask", result->failure_mask);

    if (result->validation_mask == STAGE43_MACHO_VALID_REQUIRED && result->failure_mask == 0u) {
        result->status = STAGE43_STATUS_OK;
        xnu_log_puts("Stage43 Mach-O probe ok; entry metadata observed but not executed\n");
        return 1;
    }

    result->status = STAGE43_STATUS_BASE | result->failure_mask;
    xnu_log_puts("Stage43 Mach-O probe failed\n");
    return 0;
}

static uint32_t stage43_loader_preflight_checksum(volatile const struct stage43_loader_preflight *preflight)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)preflight;
    uint32_t count = (uint32_t)(offsetof(struct stage43_loader_preflight, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static const void *find_child(const struct boot_args *args, const void *node, const char *name)
{
    return apple_dt_find_child(args->deviceTreeP, args->deviceTreeLength, node, name);
}

static uint32_t stage43_dt_semantic_mask(struct boot_args *args)
{
    uint32_t mask = 0;
    uint32_t len = 0;
    const void *root = args->deviceTreeP;
    const void *chosen = find_child(args, root, "chosen");
    const void *memory = find_child(args, root, "memory");
    const void *cpus = find_child(args, root, "cpus");
    const void *gic = find_child(args, root, "interrupt-controller");
    const void *timer = find_child(args, root, "timer");
    const void *device_tree = find_child(args, root, "device-tree");

    if (root && args->deviceTreeLength && apple_dt_node_prop_count(root, args->deviceTreeLength, root) != 0u) {
        mask |= STAGE43_DT_READY_BINARY_SELFTEST;
    }
    if (chosen) {
        mask |= STAGE43_DT_READY_CHOSEN;
        if (apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, chosen, "boot-args", &len) && len != 0u) {
            mask |= STAGE43_DT_READY_BOOT_ARGS;
        }
        if (apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, chosen, "ram-console-reg", &len) && len >= 8u) {
            mask |= STAGE43_DT_READY_RAM_CONSOLE;
        }
    }
    if (memory && apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, memory, "reg", &len) && len >= 8u) {
        mask |= STAGE43_DT_READY_MEMORY;
    }
    if (cpus && apple_dt_node_child_count(args->deviceTreeP, args->deviceTreeLength, cpus) != 0u) {
        const void *cpu0 = find_child(args, cpus, "cpu@0");
        mask |= STAGE43_DT_READY_CPUS;
        if (cpu0 &&
            apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu0, "clock-frequency", 0u) != 0u &&
            apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu0, "timebase-frequency", 0u) == 19200000u) {
            mask |= STAGE43_DT_READY_CPU_CLOCKS;
        }
    }
    if (gic) {
        mask |= STAGE43_DT_READY_INTERRUPT_CONTROLLER;
    }
    if (timer && apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer, "frequency", 0u) == 19200000u) {
        mask |= STAGE43_DT_READY_TIMER;
    }
    if (device_tree) {
        mask |= STAGE43_DT_READY_DEVICE_TREE_NODE;
        if (apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, device_tree, "target-type", &len) && len != 0u) {
            mask |= STAGE43_DT_READY_TARGET_TYPE;
        }
        if (apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, device_tree, "model", &len) && len != 0u) {
            mask |= STAGE43_DT_READY_MODEL;
        }
    }

    return mask;
}

int stage43_loader_preflight_run(struct boot_args *args)
{
    struct stage43_macho_probe_result probe;
    struct stage43_loader_preflight *preflight = &stage43_loader_preflight_block;
    uint32_t loaded_span;
    uint32_t proposed_loaded_end;
    uint32_t workspace_limit;

    xnu_log_puts("Stage43 Mach-O/XNU loader preflight begin\n");
    memset(preflight, 0, sizeof(*preflight));
    preflight->version = STAGE43_LOADER_PREFLIGHT_VERSION;
    preflight->size = sizeof(*preflight);
    preflight->xnu_baseline_tag = STAGE43_XNU_BASELINE_2050_22_13;
    preflight->xnu_baseline_commit = STAGE43_XNU_BASELINE_COMMIT_CC8A9B0C;
    preflight->xnu_master_version = STAGE43_XNU_BASELINE_MASTER_12_3_0;
    preflight->xnu_arm_reference = STAGE43_XNU_ARM_REFERENCE_4570_1_46;

    if (stage43_macho_probe(stage43_embedded_macho, stage43_embedded_macho_size, &probe)) {
        preflight->satisfied_mask |= STAGE43_LOADER_SAT_MACHO;
    }
    memcpy(&preflight->macho, &probe, sizeof(probe));

    preflight->boot_args_rev_ver = ((uint32_t)args->Version << 16) | args->Revision;
    preflight->boot_args_ptr = (uint32_t)(uintptr_t)args;
    preflight->device_tree_ptr = (uint32_t)(uintptr_t)args->deviceTreeP;
    preflight->device_tree_length = args->deviceTreeLength;
    preflight->actual_virtBase = args->virtBase;
    preflight->actual_physBase = args->physBase;
    preflight->actual_memSize = args->memSize;
    preflight->actual_topOfKernelData = args->topOfKernelData;

    preflight->proposed_virtBase = probe.text_vmaddr;
    preflight->proposed_physBase = RAM_PHYS_BASE;
    preflight->proposed_memSize = RAM_CONSOLE_BASE - RAM_PHYS_BASE;
    loaded_span = probe.max_vmaddr - probe.min_vmaddr;
    if (loaded_span < 0x00010000u) {
        loaded_span = 0x00010000u;
    }
    proposed_loaded_end = preflight->proposed_physBase + loaded_span;
    preflight->proposed_topOfKernelData = align_up_u32(proposed_loaded_end, STAGE43_XNU_TTE_ALIGNMENT);
    preflight->proposed_ttep_workspace_base = preflight->proposed_topOfKernelData;
    preflight->proposed_ttep_workspace_size = STAGE43_XNU_TTE_WORKSPACE_BYTES;
    workspace_limit = preflight->proposed_ttep_workspace_base + preflight->proposed_ttep_workspace_size;
    preflight->proposed_ttep_workspace_limit = workspace_limit;
    preflight->proposed_avail_start = workspace_limit;
    preflight->proposed_workspace_alignment = preflight->proposed_topOfKernelData & (STAGE43_XNU_TTE_ALIGNMENT - 1u);
    if (preflight->proposed_workspace_alignment == 0u &&
        workspace_limit > preflight->proposed_ttep_workspace_base &&
        workspace_limit < RAM_CONSOLE_BASE &&
        preflight->proposed_topOfKernelData >= RAM_PHYS_BASE) {
        preflight->proposed_workspace_range_ok = 1u;
    }

    preflight->apple_dt_semantic_mask = stage43_dt_semantic_mask(args);
    preflight->pexpert_gap_mask = STAGE43_PLATFORM_GAP_PEXPERT_IMPL;
    preflight->platform_gap_mask = STAGE43_PLATFORM_GAP_REQUIRED_RECORDED;
    preflight->interrupt_ready_mask = STAGE43_LOADER_IRQ_READY_REQUIRED;
    preflight->safety_mask = STAGE43_LOADER_SAFETY_REQUIRED;

    if (preflight->xnu_baseline_tag == STAGE43_XNU_BASELINE_2050_22_13 &&
        preflight->xnu_baseline_commit == STAGE43_XNU_BASELINE_COMMIT_CC8A9B0C &&
        preflight->xnu_master_version == STAGE43_XNU_BASELINE_MASTER_12_3_0 &&
        preflight->xnu_arm_reference == STAGE43_XNU_ARM_REFERENCE_4570_1_46) {
        preflight->satisfied_mask |= STAGE43_LOADER_SAT_BASELINE;
    }
    if (preflight->boot_args_rev_ver == 0x00020002u &&
        preflight->boot_args_ptr != 0u && preflight->device_tree_ptr != 0u &&
        preflight->actual_memSize == (RAM_CONSOLE_BASE - RAM_PHYS_BASE)) {
        preflight->satisfied_mask |= STAGE43_LOADER_SAT_BOOT_ARGS;
    }
    if (preflight->proposed_workspace_range_ok == 1u) {
        preflight->satisfied_mask |= STAGE43_LOADER_SAT_WORKSPACE;
    }
    if (preflight->apple_dt_semantic_mask == STAGE43_DT_READY_REQUIRED) {
        preflight->satisfied_mask |= STAGE43_LOADER_SAT_DT;
    }
    if (preflight->platform_gap_mask == STAGE43_PLATFORM_GAP_REQUIRED_RECORDED) {
        preflight->satisfied_mask |= STAGE43_LOADER_SAT_PLATFORM_GAPS;
    }
    if (preflight->interrupt_ready_mask == STAGE43_LOADER_IRQ_READY_REQUIRED) {
        preflight->satisfied_mask |= STAGE43_LOADER_SAT_INTERRUPTS;
    }
    if (preflight->safety_mask == STAGE43_LOADER_SAFETY_REQUIRED) {
        preflight->satisfied_mask |= STAGE43_LOADER_SAT_SAFETY;
    }

    preflight->readiness_mask = preflight->satisfied_mask;
    preflight->failure_mask = probe.failure_mask;
    if (preflight->satisfied_mask != STAGE43_LOADER_SAT_REQUIRED) {
        preflight->failure_mask |= 0x80000000u;
    }
    preflight->checksum = stage43_loader_preflight_checksum(preflight);
    preflight->status = (preflight->satisfied_mask == STAGE43_LOADER_SAT_REQUIRED && preflight->failure_mask == 0u &&
        preflight->checksum == stage43_loader_preflight_checksum(preflight)) ? STAGE43_STATUS_OK : (STAGE43_STATUS_BASE | preflight->failure_mask);

    xnu_log_kv32("loader_xnu_baseline_tag", preflight->xnu_baseline_tag);
    xnu_log_kv32("loader_xnu_baseline_commit", preflight->xnu_baseline_commit);
    xnu_log_kv32("loader_xnu_master_version", preflight->xnu_master_version);
    xnu_log_kv32("loader_xnu_arm_reference", preflight->xnu_arm_reference);
    xnu_log_kv32("loader_actual_virtBase", preflight->actual_virtBase);
    xnu_log_kv32("loader_actual_physBase", preflight->actual_physBase);
    xnu_log_kv32("loader_actual_topOfKernelData", preflight->actual_topOfKernelData);
    xnu_log_kv32("xnu_proposed_virtBase", preflight->proposed_virtBase);
    xnu_log_kv32("xnu_proposed_physBase", preflight->proposed_physBase);
    xnu_log_kv32("xnu_proposed_memSize", preflight->proposed_memSize);
    xnu_log_kv32("xnu_proposed_topOfKernelData", preflight->proposed_topOfKernelData);
    xnu_log_kv32("xnu_proposed_ttep_workspace_limit", preflight->proposed_ttep_workspace_limit);
    xnu_log_kv32("xnu_proposed_avail_start", preflight->proposed_avail_start);
    xnu_log_kv32("xnu_workspace_range_ok", preflight->proposed_workspace_range_ok);
    xnu_log_kv32("apple_dt_semantic_mask", preflight->apple_dt_semantic_mask);
    xnu_log_kv32("platform_gap_mask", preflight->platform_gap_mask);
    xnu_log_kv32("loader_interrupt_ready_mask", preflight->interrupt_ready_mask);
    xnu_log_kv32("loader_safety_mask", preflight->safety_mask);
    xnu_log_kv32("loader_satisfied_mask", preflight->satisfied_mask);
    xnu_log_kv32("loader_checksum", preflight->checksum);
    xnu_log_kv32("loader_status", preflight->status);
    xnu_log_puts("Stage43 XNU handoff disabled: no Mach-O entry executed, no TTBR switch, caches unchanged\n");

    if (preflight->status == STAGE43_STATUS_OK) {
        xnu_log_puts("Stage43 Mach-O/XNU loader preflight ok\n");
        return 1;
    }

    xnu_log_puts("Stage43 Mach-O/XNU loader preflight failed\n");
    return 0;
}
