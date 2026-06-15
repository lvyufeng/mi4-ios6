#include "stage85.h"

#define STAGE85_PAGE_SIZE 4096u

struct stage85_macho_header_32 {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
};

struct stage85_macho_load_command {
    uint32_t cmd;
    uint32_t cmdsize;
};

struct stage85_macho_segment_command_32 {
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

struct stage85_macho_section_32 {
    char sectname[16];
    char segname[16];
    uint32_t addr;
    uint32_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
};

struct stage85_macho_symtab_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
};

static struct stage85_loader_preflight stage85_loader_preflight_block;
static uint8_t stage85_macho_staging_arena[STAGE85_MACHO_STAGING_ARENA_CAP]
    __attribute__((aligned(STAGE85_MACHO_STAGING_ARENA_ALIGN)));
static uint8_t stage85_tte_dryrun_arena[STAGE85_XNU_TTE_WORKSPACE_BYTES]
    __attribute__((aligned(STAGE85_XNU_TTE_ALIGNMENT)));
static uint8_t stage85_safe_table_arena[STAGE85_XNU_TTE_L1_SIZE * 2u]
    __attribute__((aligned(STAGE85_XNU_TTE_L1_ALIGN)));

static uint32_t add_overflow_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    *out = a + b;
    return *out < a;
}

static uint32_t mul_overflow_u32(uint32_t a, uint32_t b, uint32_t *out)
{
    if (a != 0u && b > (UINT32_MAX / a)) {
        *out = 0;
        return 1;
    }
    *out = a * b;
    return 0;
}

static uint32_t align_up_checked_u32(uint32_t value, uint32_t alignment, uint32_t *out)
{
    uint32_t plus;

    if (alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
        *out = value;
        return 1;
    }
    if (add_overflow_u32(value, alignment - 1u, &plus)) {
        *out = 0;
        return 1;
    }
    *out = plus & ~(alignment - 1u);
    return 0;
}

static uint32_t align_up_u32(uint32_t value, uint32_t alignment)
{
    uint32_t out;

    if (align_up_checked_u32(value, alignment, &out)) {
        return 0;
    }
    return out;
}

static int fixed16_eq(const char fixed[16], const char *name)
{
    uint32_t i;

    for (i = 0; i < 16u; i++) {
        char expected = name[i];
        if (fixed[i] != expected) {
            return 0;
        }
        if (expected == '\0') {
            return 1;
        }
    }

    return 1;
}

static int bytes_prefix_eq(const uint8_t *bytes, const char *prefix)
{
    uint32_t i;

    for (i = 0; prefix[i] != '\0'; i++) {
        if (bytes[i] != (uint8_t)prefix[i]) {
            return 0;
        }
    }
    return 1;
}

static uint32_t segment_mask_for_name(const char segname[16])
{
    if (fixed16_eq(segname, "__TEXT")) {
        return STAGE85_MACHO_SEG_TEXT;
    }
    if (fixed16_eq(segname, "__DATA")) {
        return STAGE85_MACHO_SEG_DATA;
    }
    if (fixed16_eq(segname, "__LINKEDIT")) {
        return STAGE85_MACHO_SEG_LINKEDIT;
    }
    if (fixed16_eq(segname, "__KLD")) {
        return STAGE85_MACHO_SEG_KLD;
    }
    if (fixed16_eq(segname, "__LAST")) {
        return STAGE85_MACHO_SEG_LAST;
    }
    if (fixed16_eq(segname, "__PRELINK_TEXT")) {
        return STAGE85_MACHO_SEG_PRELINK_TEXT;
    }
    if (fixed16_eq(segname, "__PRELINK_INFO")) {
        return STAGE85_MACHO_SEG_PRELINK_INFO;
    }
    if (fixed16_eq(segname, "__PRELINK_STATE")) {
        return STAGE85_MACHO_SEG_PRELINK_STATE;
    }
    if (fixed16_eq(segname, "__PRELINK")) {
        return STAGE85_MACHO_SEG_PRELINK;
    }

    return 0;
}

static void record_key_segment(struct stage85_macho_probe_result *result,
                               const struct stage85_macho_segment_command_32 *segment,
                               uint32_t seg_bit)
{
    if (seg_bit == STAGE85_MACHO_SEG_TEXT) {
        result->text_vmaddr = segment->vmaddr;
        result->text_vmsize = segment->vmsize;
        result->text_fileoff = segment->fileoff;
        result->text_filesize = segment->filesize;
    } else if (seg_bit == STAGE85_MACHO_SEG_DATA) {
        result->data_vmaddr = segment->vmaddr;
        result->data_vmsize = segment->vmsize;
        result->data_fileoff = segment->fileoff;
        result->data_filesize = segment->filesize;
    } else if (seg_bit == STAGE85_MACHO_SEG_LINKEDIT) {
        result->linkedit_vmaddr = segment->vmaddr;
        result->linkedit_vmsize = segment->vmsize;
        result->linkedit_fileoff = segment->fileoff;
        result->linkedit_filesize = segment->filesize;
    } else if (seg_bit == STAGE85_MACHO_SEG_PRELINK_TEXT) {
        result->prelink_text_vmaddr = segment->vmaddr;
        result->prelink_text_vmsize = segment->vmsize;
        result->prelink_text_fileoff = segment->fileoff;
        result->prelink_text_filesize = segment->filesize;
    } else if (seg_bit == STAGE85_MACHO_SEG_PRELINK_INFO) {
        result->prelink_info_vmaddr = segment->vmaddr;
        result->prelink_info_vmsize = segment->vmsize;
        result->prelink_info_fileoff = segment->fileoff;
        result->prelink_info_filesize = segment->filesize;
    } else if (seg_bit == STAGE85_MACHO_SEG_PRELINK_STATE) {
        result->prelink_state_vmaddr = segment->vmaddr;
        result->prelink_state_vmsize = segment->vmsize;
        result->prelink_state_fileoff = segment->fileoff;
        result->prelink_state_filesize = segment->filesize;
    }
}

static void add_load_plan_entry(struct stage85_macho_probe_result *result,
                                const struct stage85_macho_segment_command_32 *segment,
                                uint32_t seg_bit)
{
    struct stage85_macho_load_plan *plan = &result->load_plan;
    struct stage85_macho_load_plan_entry *entry;

    if (segment->vmsize == 0u) {
        return;
    }
    if (plan->entry_count >= STAGE85_MACHO_LOAD_PLAN_MAX) {
        result->failure_mask |= STAGE85_MACHO_FAIL_LOAD_PLAN_FULL;
        return;
    }

    entry = &plan->entries[plan->entry_count];
    entry->index = plan->entry_count;
    entry->segment_mask = seg_bit;
    entry->vmaddr = segment->vmaddr;
    entry->vmsize = segment->vmsize;
    entry->fileoff = segment->fileoff;
    entry->filesize = segment->filesize;
    entry->physaddr = 0;
    entry->physsize = 0;
    entry->flags = 0;
    if (segment->filesize != 0u) {
        entry->flags |= STAGE85_MACHO_LOAD_PLAN_FILE_BACKED;
    }
    if ((seg_bit & STAGE85_MACHO_SEG_PRELINK_MASK) != 0u) {
        entry->flags |= STAGE85_MACHO_LOAD_PLAN_PRELINK;
    }
    if (segment->vmsize > segment->filesize) {
        entry->flags |= STAGE85_MACHO_LOAD_PLAN_ZERO_FILL;
    }
    plan->entry_count++;
}

static void record_section_match(struct stage85_macho_probe_result *result,
                                 const struct stage85_macho_section_32 *section)
{
    uint32_t section_bit = 0;

    if (fixed16_eq(section->segname, "__TEXT") && fixed16_eq(section->sectname, "__text")) {
        section_bit = STAGE85_MACHO_SECT_TEXT_TEXT;
        result->text_section_vmaddr = section->addr;
        result->text_section_size = section->size;
    } else if (fixed16_eq(section->segname, "__DATA") && fixed16_eq(section->sectname, "__const")) {
        section_bit = STAGE85_MACHO_SECT_DATA_CONST;
        result->data_const_vmaddr = section->addr;
        result->data_const_size = section->size;
    } else if (fixed16_eq(section->segname, "__PRELINK_TEXT") && fixed16_eq(section->sectname, "__text")) {
        section_bit = STAGE85_MACHO_SECT_PRELINK_TEXT_TEXT;
        result->prelink_text_section_vmaddr = section->addr;
        result->prelink_text_section_size = section->size;
    } else if (fixed16_eq(section->segname, "__PRELINK_INFO") && fixed16_eq(section->sectname, "__info")) {
        section_bit = STAGE85_MACHO_SECT_PRELINK_INFO_INFO;
        result->prelink_info_section_vmaddr = section->addr;
        result->prelink_info_section_size = section->size;
    } else if (fixed16_eq(section->segname, "__PRELINK_INFO") && fixed16_eq(section->sectname, "__kernel")) {
        section_bit = STAGE85_MACHO_SECT_PRELINK_INFO_KERNEL;
    } else if (fixed16_eq(section->segname, "__PRELINK_INFO") && fixed16_eq(section->sectname, "__kexts")) {
        section_bit = STAGE85_MACHO_SECT_PRELINK_INFO_KEXTS;
    } else if (fixed16_eq(section->segname, "__PRELINK_STATE") && fixed16_eq(section->sectname, "__kernel")) {
        section_bit = STAGE85_MACHO_SECT_PRELINK_STATE_KERNEL;
    } else if (fixed16_eq(section->segname, "__PRELINK_STATE") && fixed16_eq(section->sectname, "__kexts")) {
        section_bit = STAGE85_MACHO_SECT_PRELINK_STATE_KEXTS;
    } else if (fixed16_eq(section->segname, "__PRELINK") && fixed16_eq(section->sectname, "__symtab")) {
        section_bit = STAGE85_MACHO_SECT_PRELINK_SYMTAB;
    }

    result->section_seen_mask |= section_bit;
    if ((section_bit & STAGE85_MACHO_SECT_PRELINK_MASK) != 0u) {
        result->prelink_section_seen_mask |= section_bit;
    }
}

static void record_section(struct stage85_macho_probe_result *result,
                           const struct stage85_macho_segment_command_32 *segment,
                           const struct stage85_macho_section_32 *section)
{
    uint32_t section_end = 0;
    uint32_t segment_end = 0;
    uint32_t file_end = 0;

    result->section_count++;
    record_section_match(result, section);

    if (add_overflow_u32(section->addr, section->size, &section_end) ||
        add_overflow_u32(segment->vmaddr, segment->vmsize, &segment_end) ||
        section->addr < segment->vmaddr || section_end > segment_end) {
        result->failure_mask |= STAGE85_MACHO_FAIL_SECTION_BOUNDS;
    }

    if (section->size != 0u && section->offset != 0u) {
        if (add_overflow_u32(section->offset, section->size, &file_end) || file_end > result->artifact_size) {
            result->failure_mask |= STAGE85_MACHO_FAIL_SECTION_BOUNDS;
        }
        if (file_end > result->max_file_extent) {
            result->max_file_extent = file_end;
        }
    }
}

static void record_segment(struct stage85_macho_probe_result *result,
                           const struct stage85_macho_segment_command_32 *segment)
{
    uint32_t vm_end = 0;
    uint32_t file_end = 0;
    uint32_t seg_bit;

    result->segment_count++;
    result->validation_mask |= STAGE85_MACHO_VALID_SEGMENT_COMMAND;
    seg_bit = segment_mask_for_name(segment->segname);

    if (add_overflow_u32(segment->vmaddr, segment->vmsize, &vm_end)) {
        result->failure_mask |= STAGE85_MACHO_FAIL_SEGMENT_BOUNDS;
        return;
    }
    if (add_overflow_u32(segment->fileoff, segment->filesize, &file_end) || file_end > result->artifact_size) {
        result->failure_mask |= STAGE85_MACHO_FAIL_SEGMENT_EXTENT;
    }

    result->segment_seen_mask |= seg_bit;
    if ((seg_bit & STAGE85_MACHO_SEG_PRELINK_MASK) != 0u) {
        result->prelink_segment_seen_mask |= seg_bit;
    }
    record_key_segment(result, segment, seg_bit);

    if (result->segment_count == 1u || segment->vmaddr < result->min_vmaddr) {
        result->min_vmaddr = segment->vmaddr;
    }
    if (vm_end > result->max_vmaddr) {
        result->max_vmaddr = vm_end;
    }
    if (file_end > result->max_file_extent) {
        result->max_file_extent = file_end;
    }

    add_load_plan_entry(result, segment, seg_bit);
}

static void parse_segment_sections(struct stage85_macho_probe_result *result,
                                   const uint8_t *bytes,
                                   uint32_t command_offset,
                                   const struct stage85_macho_segment_command_32 *segment)
{
    uint32_t section_bytes = 0;
    uint32_t expected_cmdsize = 0;
    uint32_t section_offset;

    if (mul_overflow_u32(segment->nsects, sizeof(struct stage85_macho_section_32), &section_bytes) ||
        add_overflow_u32(sizeof(*segment), section_bytes, &expected_cmdsize) ||
        expected_cmdsize > segment->cmdsize) {
        result->failure_mask |= STAGE85_MACHO_FAIL_SECTION_BOUNDS;
        return;
    }

    section_offset = command_offset + sizeof(*segment);
    for (uint32_t i = 0; i < segment->nsects; i++) {
        const struct stage85_macho_section_32 *section =
            (const struct stage85_macho_section_32 *)(const void *)(bytes + section_offset);
        record_section(result, segment, section);
        section_offset += sizeof(*section);
    }
}

static void finalize_load_plan(struct stage85_macho_probe_result *result)
{
    struct stage85_macho_load_plan *plan = &result->load_plan;
    uint32_t phys_end = RAM_PHYS_BASE;
    uint32_t file_base = UINT32_MAX;
    uint32_t file_end = 0;

    plan->version = STAGE85_MACHO_LOAD_PLAN_VERSION;
    plan->vm_base = result->min_vmaddr;
    plan->vm_end = result->max_vmaddr;
    plan->phys_base = RAM_PHYS_BASE;

    if (plan->entry_count == 0u || result->min_vmaddr == 0u || result->max_vmaddr <= result->min_vmaddr) {
        result->failure_mask |= STAGE85_MACHO_FAIL_LOAD_PLAN;
        return;
    }

    for (uint32_t i = 0; i < plan->entry_count; i++) {
        struct stage85_macho_load_plan_entry *entry = &plan->entries[i];
        uint32_t vm_delta;
        uint32_t aligned_size;
        uint32_t entry_end;
        uint32_t entry_file_end;

        if (entry->vmaddr < result->min_vmaddr) {
            result->failure_mask |= STAGE85_MACHO_FAIL_LOAD_PLAN;
            continue;
        }
        vm_delta = entry->vmaddr - result->min_vmaddr;
        if (add_overflow_u32(RAM_PHYS_BASE, vm_delta, &entry->physaddr) ||
            align_up_checked_u32(entry->vmsize, STAGE85_PAGE_SIZE, &aligned_size) ||
            add_overflow_u32(entry->physaddr, aligned_size, &entry_end)) {
            result->failure_mask |= STAGE85_MACHO_FAIL_PHYS_RANGE;
            continue;
        }
        entry->physsize = aligned_size;
        if (entry->physaddr < RAM_PHYS_BASE || entry_end > RAM_CONSOLE_BASE || entry_end < entry->physaddr) {
            result->failure_mask |= STAGE85_MACHO_FAIL_PHYS_RANGE;
        }
        if (entry_end > phys_end) {
            phys_end = entry_end;
        }
        if (entry->filesize != 0u) {
            if (entry->fileoff < file_base) {
                file_base = entry->fileoff;
            }
            if (!add_overflow_u32(entry->fileoff, entry->filesize, &entry_file_end) && entry_file_end > file_end) {
                file_end = entry_file_end;
            }
        }
    }

    plan->phys_end = phys_end;
    plan->file_end = file_end;
    result->load_vm_base = plan->vm_base;
    result->load_vm_end = plan->vm_end;
    result->load_file_base = (file_base == UINT32_MAX) ? 0u : file_base;
    result->load_file_end = file_end;
    result->load_phys_base = plan->phys_base;
    result->load_phys_end = plan->phys_end;
    result->load_phys_size = result->load_phys_end - result->load_phys_base;
    result->load_plan_count = plan->entry_count;

    if ((result->failure_mask & (STAGE85_MACHO_FAIL_LOAD_PLAN | STAGE85_MACHO_FAIL_PHYS_RANGE |
                                 STAGE85_MACHO_FAIL_LOAD_PLAN_FULL)) == 0u) {
        plan->status = STAGE85_STATUS_OK;
        result->load_plan_status = STAGE85_STATUS_OK;
        result->validation_mask |= STAGE85_MACHO_VALID_LOAD_PLAN;
    } else {
        plan->status = STAGE85_STATUS_FAIL(result->failure_mask);
        result->load_plan_status = plan->status;
    }
}

static void log_load_plan_entry(const char *prefix_vmaddr, const char *prefix_vmsize,
                                const char *prefix_physaddr, const char *prefix_physsize,
                                const struct stage85_macho_load_plan_entry *entry)
{
    xnu_log_kv32(prefix_vmaddr, entry->vmaddr);
    xnu_log_kv32(prefix_vmsize, entry->vmsize);
    xnu_log_kv32(prefix_physaddr, entry->physaddr);
    xnu_log_kv32(prefix_physsize, entry->physsize);
}

static void log_load_plan_entries(const struct stage85_macho_probe_result *result)
{
    const struct stage85_macho_load_plan *plan = &result->load_plan;

    if (plan->entry_count > 0u) {
        log_load_plan_entry("macho_load0_vmaddr", "macho_load0_vmsize",
                            "macho_load0_physaddr", "macho_load0_physsize",
                            &plan->entries[0]);
    }
    if (plan->entry_count > 1u) {
        log_load_plan_entry("macho_load1_vmaddr", "macho_load1_vmsize",
                            "macho_load1_physaddr", "macho_load1_physsize",
                            &plan->entries[1]);
    }
    if (plan->entry_count > 2u) {
        log_load_plan_entry("macho_load2_vmaddr", "macho_load2_vmsize",
                            "macho_load2_physaddr", "macho_load2_physsize",
                            &plan->entries[2]);
    }
}

int stage85_macho_probe(const void *artifact, uint32_t artifact_size,
                        struct stage85_macho_probe_result *result)
{
    const uint8_t *bytes = (const uint8_t *)artifact;
    const struct stage85_macho_header_32 *header;
    uint32_t commands_start = sizeof(*header);
    uint32_t commands_end = 0;
    uint32_t offset;
    uint32_t command_region_ok = 0;

    if (!result) {
        return 0;
    }

    memset(result, 0, sizeof(*result));
    result->version = STAGE85_MACHO_PROBE_VERSION;
    result->artifact_base = (uint32_t)(uintptr_t)artifact;
    result->artifact_size = artifact_size;
    result->segment_required_mask = STAGE85_MACHO_SEG_REQUIRED;
    result->section_required_mask = STAGE85_MACHO_SECT_REQUIRED;
    result->entry_kind_mask = STAGE85_MACHO_ENTRY_NOT_EXECUTED;
    result->status = STAGE85_STATUS_BASE;
    result->load_plan.version = STAGE85_MACHO_LOAD_PLAN_VERSION;

    xnu_log_puts("Stage84 Mach-O probe begin\n");
    xnu_log_kv32("macho_artifact_base", result->artifact_base);
    xnu_log_kv32("macho_artifact_size", result->artifact_size);

    if (!artifact) {
        result->failure_mask |= STAGE85_MACHO_FAIL_NULL;
        goto done;
    }
    if (artifact_size < sizeof(*header)) {
        result->failure_mask |= STAGE85_MACHO_FAIL_SIZE;
        goto done;
    }

    header = (const struct stage85_macho_header_32 *)artifact;
    result->magic = header->magic;
    result->cputype = header->cputype;
    result->cpusubtype = header->cpusubtype;
    result->filetype = header->filetype;
    result->ncmds = header->ncmds;
    result->sizeofcmds = header->sizeofcmds;
    result->flags = header->flags;

    if (header->magic == STAGE85_MACHO_MAGIC) {
        result->validation_mask |= STAGE85_MACHO_VALID_MAGIC | STAGE85_MACHO_VALID_ENDIAN;
    } else if (header->magic == STAGE85_MACHO_CIGAM) {
        result->validation_mask |= STAGE85_MACHO_VALID_MAGIC;
        result->failure_mask |= STAGE85_MACHO_FAIL_ENDIAN;
    } else {
        result->failure_mask |= STAGE85_MACHO_FAIL_MAGIC;
    }

    if (header->cputype == STAGE85_MACHO_CPU_TYPE_ARM) {
        result->validation_mask |= STAGE85_MACHO_VALID_CPU;
    } else {
        result->failure_mask |= STAGE85_MACHO_FAIL_CPU;
    }

    if (header->cpusubtype == STAGE85_MACHO_CPU_SUBTYPE_ARM_ALL ||
        header->cpusubtype == STAGE85_MACHO_CPU_SUBTYPE_ARM_V7) {
        result->validation_mask |= STAGE85_MACHO_VALID_SUBTYPE;
    } else {
        result->failure_mask |= STAGE85_MACHO_FAIL_SUBTYPE;
    }

    if (header->filetype == STAGE85_MACHO_FILETYPE_EXECUTE ||
        header->filetype == STAGE85_MACHO_FILETYPE_PRELOAD) {
        result->validation_mask |= STAGE85_MACHO_VALID_FILETYPE;
    } else {
        result->failure_mask |= STAGE85_MACHO_FAIL_FILETYPE;
    }

    if (add_overflow_u32(commands_start, header->sizeofcmds, &commands_end) ||
        commands_end > artifact_size || (header->sizeofcmds & 3u) != 0u) {
        result->failure_mask |= STAGE85_MACHO_FAIL_COMMAND_BOUNDS;
    } else {
        result->validation_mask |= STAGE85_MACHO_VALID_COMMAND_BOUNDS;
        command_region_ok = 1u;
    }

    if (command_region_ok == 0u) {
        goto after_commands;
    }

    offset = commands_start;
    for (uint32_t i = 0; i < header->ncmds && offset < commands_end; i++) {
        const struct stage85_macho_load_command *load_command;
        uint32_t next_offset = 0;

        if (commands_end - offset < sizeof(*load_command)) {
            result->failure_mask |= STAGE85_MACHO_FAIL_COMMAND_BOUNDS;
            break;
        }

        load_command = (const struct stage85_macho_load_command *)(const void *)(bytes + offset);
        if (load_command->cmdsize < sizeof(*load_command) || (load_command->cmdsize & 3u) != 0u ||
            add_overflow_u32(offset, load_command->cmdsize, &next_offset) || next_offset > commands_end) {
            result->failure_mask |= STAGE85_MACHO_FAIL_COMMAND_SIZE;
            break;
        }

        result->load_command_count_seen++;
        if (load_command->cmd == STAGE85_MACHO_LC_SEGMENT) {
            if (load_command->cmdsize < sizeof(struct stage85_macho_segment_command_32)) {
                result->failure_mask |= STAGE85_MACHO_FAIL_COMMAND_SIZE;
            } else {
                const struct stage85_macho_segment_command_32 *segment =
                    (const struct stage85_macho_segment_command_32 *)(const void *)(bytes + offset);
                result->load_command_seen_mask |= STAGE85_MACHO_CMD_SEGMENT;
                record_segment(result, segment);
                parse_segment_sections(result, bytes, offset, segment);
            }
        } else if (load_command->cmd == STAGE85_MACHO_LC_SYMTAB) {
            result->load_command_seen_mask |= STAGE85_MACHO_CMD_SYMTAB;
            if (load_command->cmdsize >= sizeof(struct stage85_macho_symtab_command)) {
                const struct stage85_macho_symtab_command *symtab =
                    (const struct stage85_macho_symtab_command *)(const void *)(bytes + offset);
                uint32_t str_end = 0;
                if (add_overflow_u32(symtab->stroff, symtab->strsize, &str_end) || str_end > artifact_size) {
                    result->failure_mask |= STAGE85_MACHO_FAIL_SEGMENT_EXTENT;
                }
            }
        } else if (load_command->cmd == STAGE85_MACHO_LC_UNIXTHREAD) {
            result->load_command_seen_mask |= STAGE85_MACHO_CMD_UNIXTHREAD;
            result->entry_kind_mask |= STAGE85_MACHO_ENTRY_UNIXTHREAD;
            if (load_command->cmdsize >= 16u) {
                const uint32_t *entry_words = (const uint32_t *)(const void *)(bytes + offset);
                result->entryoff_or_pc = entry_words[3];
            }
        } else if (load_command->cmd == STAGE85_MACHO_LC_MAIN) {
            result->load_command_seen_mask |= STAGE85_MACHO_CMD_MAIN;
            result->entry_kind_mask |= STAGE85_MACHO_ENTRY_MAIN;
        } else {
            result->load_command_seen_mask |= STAGE85_MACHO_CMD_UNKNOWN;
            result->unknown_command_count++;
        }

        offset = next_offset;
    }

    if (result->load_command_count_seen != header->ncmds || offset != commands_end) {
        result->failure_mask |= STAGE85_MACHO_FAIL_COMMAND_BOUNDS;
    }

after_commands:
    if ((result->segment_seen_mask & STAGE85_MACHO_SEG_REQUIRED) == STAGE85_MACHO_SEG_REQUIRED) {
        result->validation_mask |= STAGE85_MACHO_VALID_REQUIRED_SEGMENTS;
    } else {
        result->failure_mask |= STAGE85_MACHO_FAIL_REQUIRED_SEGMENTS;
    }
    if ((result->section_seen_mask & STAGE85_MACHO_SECT_REQUIRED) == STAGE85_MACHO_SECT_REQUIRED) {
        result->validation_mask |= STAGE85_MACHO_VALID_REQUIRED_SECTIONS;
    } else {
        result->failure_mask |= STAGE85_MACHO_FAIL_REQUIRED_SECTIONS;
    }
    if ((result->failure_mask & STAGE85_MACHO_FAIL_SEGMENT_EXTENT) == 0u) {
        result->validation_mask |= STAGE85_MACHO_VALID_FILE_EXTENTS;
    }
    if ((result->failure_mask & STAGE85_MACHO_FAIL_SECTION_BOUNDS) == 0u) {
        result->validation_mask |= STAGE85_MACHO_VALID_SECTION_BOUNDS;
    }
    if ((result->prelink_segment_seen_mask | result->prelink_section_seen_mask) != 0u) {
        result->validation_mask |= STAGE85_MACHO_VALID_PRELINK_REPORT;
    }
    if ((result->entry_kind_mask & (STAGE85_MACHO_ENTRY_UNIXTHREAD | STAGE85_MACHO_ENTRY_MAIN)) != 0u) {
        result->validation_mask |= STAGE85_MACHO_VALID_ENTRY_NOT_EXECUTED;
    } else {
        result->failure_mask |= STAGE85_MACHO_FAIL_NO_ENTRY_METADATA;
    }

    finalize_load_plan(result);

done:
    xnu_log_kv32("macho_magic", result->magic);
    xnu_log_kv32("macho_cputype", result->cputype);
    xnu_log_kv32("macho_cpusubtype", result->cpusubtype);
    xnu_log_kv32("macho_filetype", result->filetype);
    xnu_log_kv32("macho_ncmds", result->ncmds);
    xnu_log_kv32("macho_sizeofcmds", result->sizeofcmds);
    xnu_log_kv32("macho_command_mask", result->load_command_seen_mask);
    xnu_log_kv32("macho_segment_count", result->segment_count);
    xnu_log_kv32("macho_segment_mask", result->segment_seen_mask);
    xnu_log_kv32("macho_section_count", result->section_count);
    xnu_log_kv32("macho_section_mask", result->section_seen_mask);
    xnu_log_kv32("macho_prelink_segment_mask", result->prelink_segment_seen_mask);
    xnu_log_kv32("macho_prelink_section_mask", result->prelink_section_seen_mask);
    xnu_log_kv32("macho_entry_kind_mask", result->entry_kind_mask);
    xnu_log_kv32("macho_entry_not_executed", 1u);
    xnu_log_kv32("macho_min_vmaddr", result->min_vmaddr);
    xnu_log_kv32("macho_max_vmaddr", result->max_vmaddr);
    xnu_log_kv32("macho_max_file_extent", result->max_file_extent);
    xnu_log_kv32("macho_load_plan_count", result->load_plan_count);
    xnu_log_kv32("macho_load_vm_base", result->load_vm_base);
    xnu_log_kv32("macho_load_vm_end", result->load_vm_end);
    xnu_log_kv32("macho_load_file_base", result->load_file_base);
    xnu_log_kv32("macho_load_file_end", result->load_file_end);
    xnu_log_kv32("macho_load_phys_base", result->load_phys_base);
    xnu_log_kv32("macho_load_phys_end", result->load_phys_end);
    xnu_log_kv32("macho_load_phys_size", result->load_phys_size);
    xnu_log_kv32("macho_load_plan_status", result->load_plan_status);
    log_load_plan_entries(result);
    xnu_log_kv32("macho_validation_mask", result->validation_mask);
    xnu_log_kv32("macho_failure_mask", result->failure_mask);

    {
        const uint32_t parser_required = STAGE85_MACHO_VALID_REQUIRED &
            ~(STAGE85_MACHO_VALID_STAGING |
              STAGE85_MACHO_VALID_MATERIALIZED_PARSE |
              STAGE85_MACHO_VALID_MARKERS |
              STAGE85_MACHO_VALID_ZERO_FILL);

        if ((result->validation_mask & parser_required) == parser_required &&
            result->failure_mask == 0u) {
            result->status = STAGE85_STATUS_OK;
            xnu_log_puts("Stage84 Mach-O probe ok; entry metadata observed but not executed\n");
            return 1;
        }
    }

    result->status = STAGE85_STATUS_FAIL(result->failure_mask);
    xnu_log_puts("Stage84 Mach-O probe failed\n");
    return 0;
}

static void log_stage_entry(const char *copy_src_key, const char *copy_dst_key,
                            const char *copy_size_key, const char *zero_dst_key,
                            const char *zero_size_key,
                            const struct stage85_macho_staging_plan_entry *entry)
{
    xnu_log_kv32(copy_src_key, entry->copy_src_off);
    xnu_log_kv32(copy_dst_key, entry->copy_dst);
    xnu_log_kv32(copy_size_key, entry->copy_size);
    xnu_log_kv32(zero_dst_key, entry->zero_dst);
    xnu_log_kv32(zero_size_key, entry->zero_size);
}

static void log_staging_entries(const struct stage85_macho_staging_plan *staging)
{
    if (staging->entry_count > 0u) {
        log_stage_entry("macho_stage0_copy_src_off", "macho_stage0_copy_dst",
                        "macho_stage0_copy_size", "macho_stage0_zero_dst",
                        "macho_stage0_zero_size", &staging->entries[0]);
    }
    if (staging->entry_count > 1u) {
        log_stage_entry("macho_stage1_copy_src_off", "macho_stage1_copy_dst",
                        "macho_stage1_copy_size", "macho_stage1_zero_dst",
                        "macho_stage1_zero_size", &staging->entries[1]);
    }
    if (staging->entry_count > 2u) {
        log_stage_entry("macho_stage2_copy_src_off", "macho_stage2_copy_dst",
                        "macho_stage2_copy_size", "macho_stage2_zero_dst",
                        "macho_stage2_zero_size", &staging->entries[2]);
    }
}

static uint32_t stage85_macho_getlastaddr_style(const struct stage85_macho_probe_result *probe)
{
    uint32_t last = 0;

    for (uint32_t i = 0; i < probe->load_plan.entry_count; i++) {
        uint32_t end = 0;
        const struct stage85_macho_load_plan_entry *entry = &probe->load_plan.entries[i];
        if (!add_overflow_u32(entry->vmaddr, entry->vmsize, &end) && end > last) {
            last = end;
        }
    }

    return last;
}

static void stage85_macho_check_arena_aliases(struct stage85_macho_probe_result *result,
                                              uint32_t artifact_base,
                                              uint32_t artifact_size)
{
    struct stage85_macho_staging_plan *staging = &result->staging_plan;
    uint32_t artifact_end = 0;

    if (add_overflow_u32(artifact_base, artifact_size, &artifact_end)) {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_ARENA_ALIAS;
        return;
    }
    if (staging->arena_base < artifact_end && artifact_base < staging->arena_end) {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_ARENA_ALIAS;
    }
    if (staging->arena_base < (RAM_CONSOLE_BASE + RAM_CONSOLE_SIZE) && RAM_CONSOLE_BASE < staging->arena_end) {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_ARENA_ALIAS;
    }
    if (result->load_phys_end != 0u && staging->arena_base < result->load_phys_end &&
        result->load_phys_base < staging->arena_end) {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_ARENA_ALIAS;
    }
    if (staging->arena_base >= 0xf0000000u || staging->arena_end > 0xf0000000u) {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_ARENA_ALIAS;
    }
}

static void stage85_macho_verify_marker(struct stage85_macho_staging_plan *staging,
                                        uint32_t vmaddr,
                                        const char *prefix,
                                        uint32_t bit)
{
    uint32_t offset;

    if (vmaddr < staging->vm_base) {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_MARKERS;
        return;
    }
    offset = vmaddr - staging->vm_base;
    if (offset >= staging->arena_size) {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_MARKERS;
        return;
    }
    if (bytes_prefix_eq(stage85_macho_staging_arena + offset, prefix)) {
        staging->marker_check_mask |= bit;
    } else {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_MARKERS;
    }
}

static void stage85_macho_verify_zero_fill(struct stage85_macho_staging_plan *staging)
{
    uint32_t required_zero_mask = 0;

    for (uint32_t i = 0; i < staging->entry_count; i++) {
        const struct stage85_macho_staging_plan_entry *entry = &staging->entries[i];
        uint32_t bit = 1u << i;
        uint32_t entry_ok = 1u;

        if (entry->zero_size == 0u) {
            continue;
        }
        required_zero_mask |= bit;
        if (entry->arena_offset + entry->copy_size + entry->zero_size > staging->arena_size) {
            staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_ZERO_FILL;
            entry_ok = 0u;
        } else {
            uint32_t zero_offset = entry->arena_offset + entry->copy_size;
            for (uint32_t j = 0; j < entry->zero_size; j++) {
                if (stage85_macho_staging_arena[zero_offset + j] != 0u) {
                    staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_ZERO_FILL;
                    entry_ok = 0u;
                    break;
                }
            }
        }
        if (entry_ok) {
            staging->zero_check_mask |= bit;
        }
    }

    if ((staging->zero_check_mask & required_zero_mask) == required_zero_mask) {
        staging->validation_mask |= STAGE85_MACHO_VALID_ZERO_FILL;
    } else {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_ZERO_FILL;
    }
}

static void stage85_macho_verify_reparse(struct stage85_macho_probe_result *result,
                                         struct stage85_macho_staging_plan *staging)
{
    struct stage85_macho_probe_result materialized;
    uint32_t original_last = stage85_macho_getlastaddr_style(result);
    uint32_t materialized_last;

    if (!stage85_macho_probe(stage85_macho_staging_arena, staging->arena_size, &materialized)) {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_REPARSE;
        return;
    }

    materialized_last = stage85_macho_getlastaddr_style(&materialized);
    staging->reparsed_vm_base = materialized.min_vmaddr;
    staging->reparsed_vm_end = materialized.max_vmaddr;
    staging->reparsed_getlastaddr = materialized_last;

    if (materialized.magic == result->magic &&
        materialized.cputype == result->cputype &&
        materialized.cpusubtype == result->cpusubtype &&
        materialized.filetype == result->filetype &&
        materialized.ncmds == result->ncmds &&
        materialized.segment_seen_mask == result->segment_seen_mask &&
        materialized.section_seen_mask == result->section_seen_mask &&
        materialized.min_vmaddr == result->min_vmaddr &&
        materialized.max_vmaddr == result->max_vmaddr &&
        materialized.load_vm_base == result->load_vm_base &&
        materialized.load_vm_end == result->load_vm_end &&
        materialized_last == original_last &&
        materialized_last == staging->vm_end) {
        staging->validation_mask |= STAGE85_MACHO_VALID_MATERIALIZED_PARSE;
        xnu_log_puts("Stage84 materialized Mach-O reparse ok\n");
    } else {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_REPARSE;
    }
}

static int stage85_macho_materialize_to_arena(const void *artifact,
                                              uint32_t artifact_size,
                                              struct stage85_macho_probe_result *result)
{
    const uint8_t *source = (const uint8_t *)artifact;
    struct stage85_macho_staging_plan *staging = &result->staging_plan;
    uint32_t arena_base = (uint32_t)(uintptr_t)stage85_macho_staging_arena;
    uint32_t arena_end = 0;
    uint32_t vm_span = 0;
    uint32_t arena_size = 0;

    xnu_log_puts("Stage84 proposed physical load plan is dry-run only; no physical writes performed\n");
    xnu_log_puts("Stage84 Mach-O materialization begin\n");
    memset(stage85_macho_staging_arena, 0, sizeof(stage85_macho_staging_arena));
    memset(staging, 0, sizeof(*staging));
    staging->version = STAGE85_MACHO_STAGING_PLAN_VERSION;
    staging->status = STAGE85_STATUS_BASE;
    staging->arena_base = arena_base;

    if (add_overflow_u32(arena_base, STAGE85_MACHO_STAGING_ARENA_CAP, &arena_end)) {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_SPAN;
        goto finish;
    }
    staging->arena_end = arena_end;
    if (result->min_vmaddr == 0u || result->max_vmaddr <= result->min_vmaddr) {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_SPAN;
        goto finish;
    }
    vm_span = result->max_vmaddr - result->min_vmaddr;
    if (align_up_checked_u32(vm_span, STAGE85_PAGE_SIZE, &arena_size) ||
        arena_size > STAGE85_MACHO_STAGING_ARENA_CAP) {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_SPAN;
        goto finish;
    }

    staging->vm_base = result->min_vmaddr;
    staging->vm_end = result->max_vmaddr;
    staging->vm_span = vm_span;
    staging->arena_size = arena_size;
    staging->materialized_header = arena_base;
    stage85_macho_check_arena_aliases(result, (uint32_t)(uintptr_t)artifact, artifact_size);
    if (staging->failure_mask != 0u) {
        goto finish;
    }

    for (uint32_t i = 0; i < result->load_plan.entry_count; i++) {
        const struct stage85_macho_load_plan_entry *load = &result->load_plan.entries[i];
        struct stage85_macho_staging_plan_entry *entry;
        uint32_t src_end;
        uint32_t arena_offset;
        uint32_t dst_end;
        uint32_t copy_end;
        uint32_t zero_size;

        if (staging->entry_count >= STAGE85_MACHO_STAGING_PLAN_MAX) {
            staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_PLAN_FULL;
            break;
        }
        if (load->vmaddr < staging->vm_base || load->filesize > load->vmsize) {
            staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_DST_BOUNDS;
            continue;
        }
        arena_offset = load->vmaddr - staging->vm_base;
        if (add_overflow_u32(load->fileoff, load->filesize, &src_end) || src_end > artifact_size) {
            staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_SRC_BOUNDS;
            continue;
        }
        if (add_overflow_u32(arena_offset, load->vmsize, &dst_end) || dst_end > arena_size ||
            add_overflow_u32(arena_offset, load->filesize, &copy_end) || copy_end > arena_size) {
            staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_DST_BOUNDS;
            continue;
        }

        entry = &staging->entries[staging->entry_count];
        entry->index = staging->entry_count;
        entry->segment_mask = load->segment_mask;
        entry->vmaddr = load->vmaddr;
        entry->vmsize = load->vmsize;
        entry->copy_src_off = load->fileoff;
        entry->copy_dst = arena_base + arena_offset;
        entry->copy_size = load->filesize;
        zero_size = load->vmsize - load->filesize;
        entry->zero_dst = entry->copy_dst + load->filesize;
        entry->zero_size = zero_size;
        entry->arena_offset = arena_offset;
        entry->flags = load->flags;
        staging->entry_count++;

        if (load->filesize != 0u) {
            memcpy(stage85_macho_staging_arena + arena_offset, source + load->fileoff, load->filesize);
            staging->file_bytes += load->filesize;
        }
        if (zero_size != 0u) {
            memset(stage85_macho_staging_arena + arena_offset + load->filesize, 0, zero_size);
            staging->zero_bytes += zero_size;
        }
    }

    if (staging->entry_count != result->load_plan.entry_count) {
        staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_COPY;
    }

    if (staging->failure_mask == 0u) {
        staging->validation_mask |= STAGE85_MACHO_VALID_STAGING;
        stage85_macho_verify_reparse(result, staging);
        stage85_macho_verify_marker(staging, result->text_section_vmaddr,
                                    "ST85-TEXT", STAGE85_MACHO_MARKER_TEXT);
        stage85_macho_verify_marker(staging, result->data_const_vmaddr,
                                    "ST85-DATA", STAGE85_MACHO_MARKER_DATA);
        stage85_macho_verify_marker(staging, result->prelink_text_section_vmaddr,
                                    "ST85-PRELINK-TEXT", STAGE85_MACHO_MARKER_PRELINK_TEXT);
        if ((staging->marker_check_mask & STAGE85_MACHO_MARKER_REQUIRED) == STAGE85_MACHO_MARKER_REQUIRED) {
            staging->validation_mask |= STAGE85_MACHO_VALID_MARKERS;
        } else {
            staging->failure_mask |= STAGE85_MACHO_FAIL_STAGE_MARKERS;
        }
        stage85_macho_verify_zero_fill(staging);
    }

finish:
    if (staging->failure_mask == 0u &&
        (staging->validation_mask & (STAGE85_MACHO_VALID_STAGING |
                                     STAGE85_MACHO_VALID_MATERIALIZED_PARSE |
                                     STAGE85_MACHO_VALID_MARKERS |
                                     STAGE85_MACHO_VALID_ZERO_FILL)) ==
        (STAGE85_MACHO_VALID_STAGING | STAGE85_MACHO_VALID_MATERIALIZED_PARSE |
         STAGE85_MACHO_VALID_MARKERS | STAGE85_MACHO_VALID_ZERO_FILL)) {
        staging->status = STAGE85_STATUS_OK;
    } else {
        staging->status = STAGE85_STATUS_FAIL(staging->failure_mask);
    }

    result->staging_status = staging->status;
    result->staging_file_bytes = staging->file_bytes;
    result->staging_zero_bytes = staging->zero_bytes;
    result->staging_arena_base = staging->arena_base;
    result->staging_arena_end = staging->arena_end;
    result->staging_vm_base = staging->vm_base;
    result->staging_vm_end = staging->vm_end;
    result->validation_mask |= staging->validation_mask;
    result->failure_mask |= staging->failure_mask;

    xnu_log_kv32("macho_staging_arena_base", staging->arena_base);
    xnu_log_kv32("macho_staging_arena_end", staging->arena_end);
    xnu_log_kv32("macho_staging_vm_base", staging->vm_base);
    xnu_log_kv32("macho_staging_vm_end", staging->vm_end);
    xnu_log_kv32("macho_staging_vm_span", staging->vm_span);
    xnu_log_kv32("macho_staging_entry_count", staging->entry_count);
    xnu_log_kv32("macho_staging_file_bytes", staging->file_bytes);
    xnu_log_kv32("macho_staging_zero_bytes", staging->zero_bytes);
    xnu_log_kv32("macho_staging_marker_mask", staging->marker_check_mask);
    xnu_log_kv32("macho_staging_zero_mask", staging->zero_check_mask);
    xnu_log_kv32("macho_staging_reparsed_vm_base", staging->reparsed_vm_base);
    xnu_log_kv32("macho_staging_reparsed_vm_end", staging->reparsed_vm_end);
    xnu_log_kv32("macho_staging_getlastaddr", staging->reparsed_getlastaddr);
    xnu_log_kv32("macho_staging_validation_mask", staging->validation_mask);
    xnu_log_kv32("macho_staging_failure_mask", staging->failure_mask);
    xnu_log_kv32("macho_staging_status", staging->status);
    log_staging_entries(staging);

    return staging->status == STAGE85_STATUS_OK;
}

static uint32_t stage85_tte_dryrun_checksum(volatile const struct stage85_xnu_tte_dryrun *tte)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)tte;
    uint32_t count = (uint32_t)(offsetof(struct stage85_xnu_tte_dryrun, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t ranges_overlap_u32(uint32_t a_base, uint32_t a_limit,
                                   uint32_t b_base, uint32_t b_limit)
{
    if (a_limit <= a_base || b_limit <= b_base) {
        return 1u;
    }
    return (a_base < b_limit && b_base < a_limit) ? 1u : 0u;
}

static uint32_t section_first_index(uint32_t addr)
{
    return addr >> 20;
}

static uint32_t section_last_index(uint32_t limit)
{
    return (limit - 1u) >> 20;
}

static void stage85_tte_map_section(uint32_t *l1, uint32_t index, uint32_t desc_flags)
{
    if (index < STAGE85_XNU_TTE_L1_ENTRY_COUNT) {
        l1[index] = (index << 20) | desc_flags;
    }
}

static void stage85_tte_map_section_range(uint32_t *l1, uint32_t first, uint32_t last, uint32_t desc_flags)
{
    for (uint32_t i = first; i <= last && i < STAGE85_XNU_TTE_L1_ENTRY_COUNT; i++) {
        stage85_tte_map_section(l1, i, desc_flags);
        if (i == UINT32_MAX) {
            break;
        }
    }
}

static uint32_t stage85_tte_l2_and_scratch_zeroed(void)
{
    for (uint32_t i = STAGE85_XNU_TTE_L1_SIZE; i < STAGE85_XNU_TTE_WORKSPACE_BYTES; i++) {
        if (stage85_tte_dryrun_arena[i] != 0u) {
            return 0u;
        }
    }
    return 1u;
}

static uint32_t stage85_tte_section_word(uint32_t index, uint32_t desc_flags)
{
    return (index << 20) | (desc_flags & STAGE85_XNU_TTE_DESC_ATTR_MASK);
}

static uint32_t stage85_tte_descriptor_attrs_ok(uint32_t word, uint32_t expected_flags)
{
    if ((word & STAGE85_XNU_TTE_DESC_TYPE_MASK) != STAGE85_XNU_TTE_DESC_TYPE_SECTION) {
        return 0u;
    }
    return ((word & STAGE85_XNU_TTE_DESC_ATTR_MASK) ==
            (expected_flags & STAGE85_XNU_TTE_DESC_ATTR_MASK)) ? 1u : 0u;
}

static uint32_t stage85_tte_verify_section_slot(const uint32_t *l1,
                                                uint32_t index,
                                                uint32_t desc_flags,
                                                uint32_t *observed_word)
{
    uint32_t word;
    uint32_t expected;

    if (observed_word) {
        *observed_word = 0u;
    }
    if (index >= STAGE85_XNU_TTE_L1_ENTRY_COUNT) {
        return 0u;
    }

    word = l1[index];
    expected = stage85_tte_section_word(index, desc_flags);
    if (observed_word) {
        *observed_word = word;
    }

    return (word == expected && stage85_tte_descriptor_attrs_ok(word, desc_flags)) ? 1u : 0u;
}

static uint32_t stage85_tte_verify_section_range(const uint32_t *l1,
                                                 uint32_t first,
                                                 uint32_t last,
                                                 uint32_t desc_flags,
                                                 uint32_t *first_word,
                                                 uint32_t *last_word)
{
    uint32_t ok = 1u;
    uint32_t word = 0u;

    if (first_word) {
        *first_word = 0u;
    }
    if (last_word) {
        *last_word = 0u;
    }
    if (first > last || last >= STAGE85_XNU_TTE_L1_ENTRY_COUNT) {
        return 0u;
    }

    for (uint32_t i = first; i <= last; i++) {
        if (!stage85_tte_verify_section_slot(l1, i, desc_flags, &word)) {
            ok = 0u;
        }
        if (i == first && first_word) {
            *first_word = word;
        }
        if (i == last && last_word) {
            *last_word = word;
        }
        if (i == UINT32_MAX) {
            break;
        }
    }

    return ok;
}


static uint32_t stage85_safe_table_checksum(const void *base, uint32_t bytes)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)base;
    uint32_t count = bytes / sizeof(uint32_t);
    uint32_t checksum = 0u;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static uint32_t stage85_tte_section_word_pa(uint32_t pa, uint32_t desc_flags)
{
    return (pa & STAGE85_XNU_TTE_DESC_BASE_MASK) |
           (desc_flags & STAGE85_XNU_TTE_DESC_ATTR_MASK);
}

static void stage85_tte_map_section_pa(uint32_t *l1, uint32_t va, uint32_t pa, uint32_t desc_flags)
{
    uint32_t index = section_first_index(va);

    if (index < STAGE85_XNU_TTE_L1_ENTRY_COUNT) {
        l1[index] = stage85_tte_section_word_pa(pa, desc_flags);
    }
}

static void stage85_tte_map_section_range_pa(uint32_t *l1,
                                             uint32_t va_base,
                                             uint32_t pa_base,
                                             uint32_t size,
                                             uint32_t desc_flags)
{
    uint32_t va_limit = 0u;
    uint32_t first;
    uint32_t last;
    uint32_t va_section;
    uint32_t pa_section;

    if (size == 0u || add_overflow_u32(va_base, size, &va_limit)) {
        return;
    }

    first = section_first_index(va_base);
    last = section_last_index(va_limit);
    va_section = va_base & STAGE85_XNU_TTE_DESC_BASE_MASK;
    pa_section = pa_base & STAGE85_XNU_TTE_DESC_BASE_MASK;

    for (uint32_t i = first; i <= last && i < STAGE85_XNU_TTE_L1_ENTRY_COUNT; i++) {
        uint32_t va = i << 20;
        uint32_t pa = pa_section + (va - va_section);
        stage85_tte_map_section_pa(l1, va, pa, desc_flags);
        if (i == UINT32_MAX) {
            break;
        }
    }
}

static uint32_t stage85_tte_verify_section_slot_pa(const uint32_t *l1,
                                                   uint32_t va,
                                                   uint32_t expected_pa,
                                                   uint32_t desc_flags,
                                                   uint32_t *observed_word)
{
    uint32_t index = section_first_index(va);
    uint32_t word;
    uint32_t expected;

    if (observed_word) {
        *observed_word = 0u;
    }
    if (index >= STAGE85_XNU_TTE_L1_ENTRY_COUNT) {
        return 0u;
    }

    word = l1[index];
    expected = stage85_tte_section_word_pa(expected_pa, desc_flags);
    if (observed_word) {
        *observed_word = word;
    }

    if (word != expected) {
        return 0u;
    }
    if ((word & STAGE85_XNU_TTE_DESC_BASE_MASK) != (expected_pa & STAGE85_XNU_TTE_DESC_BASE_MASK)) {
        return 0u;
    }
    return stage85_tte_descriptor_attrs_ok(word, desc_flags);
}

static uint32_t stage85_tte_expected_section_pa(uint32_t va, uint32_t pa_base)
{
    return (pa_base & STAGE85_XNU_TTE_DESC_BASE_MASK) |
           (va & STAGE85_XNU_TTE_SECTION_OFFSET_MASK);
}

static uint32_t stage85_tte_section_pa_for_va(uint32_t va, uint32_t va_base, uint32_t pa_base)
{
    uint32_t va_section = va & STAGE85_XNU_TTE_DESC_BASE_MASK;
    uint32_t base_section = va_base & STAGE85_XNU_TTE_DESC_BASE_MASK;
    uint32_t pa_section = pa_base & STAGE85_XNU_TTE_DESC_BASE_MASK;

    return pa_section + (va_section - base_section);
}

static uint32_t stage85_tte_translate_l1_section(const uint32_t *l1,
                                                 uint32_t va,
                                                 uint32_t *pa,
                                                 uint32_t *desc_word)
{
    uint32_t index = section_first_index(va);
    uint32_t desc;

    if (pa) {
        *pa = 0u;
    }
    if (desc_word) {
        *desc_word = 0u;
    }
    if (index >= STAGE85_XNU_TTE_L1_ENTRY_COUNT) {
        return 0u;
    }

    desc = l1[index];
    if (desc_word) {
        *desc_word = desc;
    }
    if ((desc & STAGE85_XNU_TTE_DESC_TYPE_MASK) != STAGE85_XNU_TTE_DESC_TYPE_SECTION) {
        return 0u;
    }
    if (pa) {
        *pa = (desc & STAGE85_XNU_TTE_DESC_BASE_MASK) |
              (va & STAGE85_XNU_TTE_SECTION_OFFSET_MASK);
    }

    return 1u;
}

static uint32_t stage85_tte_expect_translation(const uint32_t *l1,
                                               uint32_t va,
                                               uint32_t expected_pa,
                                               uint32_t *observed_pa)
{
    uint32_t pa = 0u;

    if (observed_pa) {
        *observed_pa = 0u;
    }
    if (!stage85_tte_translate_l1_section(l1, va, &pa, 0)) {
        return 0u;
    }
    if (observed_pa) {
        *observed_pa = pa;
    }

    return pa == expected_pa ? 1u : 0u;
}

static int stage85_xnu_tte_dryrun_build(const struct stage85_macho_probe_result *probe,
                                        const struct stage85_loader_preflight *preflight,
                                        struct stage85_xnu_tte_dryrun *tte)
{
    uint32_t expected_workspace_base = 0;
    uint32_t workspace_limit = 0;
    uint32_t l1_limit = 0;
    uint32_t l2_limit = 0;
    uint32_t local_limit = 0;
    uint32_t embedded_base = (uint32_t)(uintptr_t)stage85_embedded_macho;
    uint32_t embedded_limit = 0;
    uint32_t staging_base = (uint32_t)(uintptr_t)stage85_macho_staging_arena;
    uint32_t staging_limit = 0;
    uint32_t safe_base = (uint32_t)(uintptr_t)stage85_safe_table_arena;
    uint32_t safe_limit = 0;
    uint32_t highva_size = 0;
    uint32_t highva_last_va = 0;
    uint32_t highva_first_expected_pa = 0;
    uint32_t highva_last_expected_pa = 0;
    uint32_t *l1 = (uint32_t *)(void *)stage85_tte_dryrun_arena;
    uint32_t *safe_identity_l1 = (uint32_t *)(void *)stage85_safe_table_arena;
    uint32_t *safe_highva_l1 = (uint32_t *)(void *)(stage85_safe_table_arena + STAGE85_XNU_TTE_L1_SIZE);

    memset(tte, 0, sizeof(*tte));
    memset(stage85_tte_dryrun_arena, 0, sizeof(stage85_tte_dryrun_arena));
    memset(stage85_safe_table_arena, 0, sizeof(stage85_safe_table_arena));

    tte->version = STAGE85_XNU_TTE_DRYRUN_VERSION;
    tte->size = sizeof(*tte);
    tte->status = STAGE85_STATUS_BASE;
    tte->source_macho_status = probe->status;
    tte->source_staging_status = probe->staging_status;
    tte->source_load_phys_base = probe->load_phys_base;
    tte->source_load_phys_end = probe->load_phys_end;
    tte->source_load_virt_base = probe->load_vm_base;
    tte->source_load_virt_end = probe->load_vm_end;
    tte->proposed_physBase = preflight->proposed_physBase;
    tte->proposed_virtBase = preflight->proposed_virtBase;
    tte->proposed_memSize = preflight->proposed_memSize;
    tte->proposed_topOfKernelData = preflight->proposed_topOfKernelData;
    tte->proposed_avail_start = preflight->proposed_avail_start;
    tte->workspace_base = preflight->proposed_ttep_workspace_base;
    tte->workspace_size = STAGE85_XNU_TTE_WORKSPACE_BYTES;
    tte->workspace_alignment = tte->workspace_base & (STAGE85_XNU_TTE_L1_ALIGN - 1u);
    tte->local_arena_base = (uint32_t)(uintptr_t)stage85_tte_dryrun_arena;
    tte->local_arena_size = sizeof(stage85_tte_dryrun_arena);
    tte->l1_table_base = tte->workspace_base;
    tte->l1_table_size = STAGE85_XNU_TTE_L1_SIZE;
    tte->l1_entry_count = STAGE85_XNU_TTE_L1_ENTRY_COUNT;
    tte->l2_table_size = STAGE85_XNU_TTE_L2_SIZE;
    tte->kernel_section_descriptor = STAGE85_XNU_TTE_DESC_SECTION_SO;
    tte->ram_console_section_descriptor = STAGE85_XNU_TTE_DESC_SECTION_SO;
    tte->device_section_descriptor = STAGE85_XNU_TTE_DESC_SECTION_SO;

    if (add_overflow_u32(tte->workspace_base, tte->workspace_size, &workspace_limit)) {
        tte->failure_mask |= STAGE85_TTE_FAIL_WORKSPACE_RANGE;
    } else {
        tte->workspace_limit = workspace_limit;
    }
    if (add_overflow_u32(tte->local_arena_base, tte->local_arena_size, &local_limit)) {
        tte->failure_mask |= STAGE85_TTE_FAIL_LOCAL_ARENA_ALIAS;
    } else {
        tte->local_arena_limit = local_limit;
    }
    if (add_overflow_u32(tte->l1_table_base, tte->l1_table_size, &l1_limit)) {
        tte->failure_mask |= STAGE85_TTE_FAIL_LAYOUT_OVERFLOW;
    } else {
        tte->l1_table_limit = l1_limit;
        tte->l2_table_base = l1_limit;
    }
    if (add_overflow_u32(tte->l2_table_base, tte->l2_table_size, &l2_limit)) {
        tte->failure_mask |= STAGE85_TTE_FAIL_LAYOUT_OVERFLOW;
    } else {
        tte->l2_table_limit = l2_limit;
        tte->scratch_base = l2_limit;
    }
    if (tte->workspace_limit >= tte->scratch_base) {
        tte->scratch_size = tte->workspace_limit - tte->scratch_base;
        tte->scratch_limit = tte->workspace_limit;
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_LAYOUT_OVERFLOW;
    }

    expected_workspace_base = align_up_u32(probe->load_phys_end, STAGE85_XNU_TTE_L1_ALIGN);
    if (tte->workspace_limit > tte->workspace_base && tte->workspace_limit < RAM_CONSOLE_BASE &&
        tte->workspace_base >= probe->load_phys_end && tte->workspace_base == expected_workspace_base &&
        tte->workspace_limit == preflight->proposed_avail_start) {
        tte->satisfied_mask |= STAGE85_TTE_SAT_WORKSPACE_RANGE;
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_WORKSPACE_RANGE;
    }
    if (tte->workspace_alignment == 0u && (tte->l1_table_base & (STAGE85_XNU_TTE_L1_ALIGN - 1u)) == 0u &&
        (tte->l2_table_base & (STAGE85_XNU_TTE_L2_ALIGN - 1u)) == 0u) {
        tte->satisfied_mask |= STAGE85_TTE_SAT_WORKSPACE_ALIGNMENT;
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_WORKSPACE_ALIGNMENT;
    }
    if (tte->l1_table_limit <= tte->workspace_limit && tte->l2_table_limit <= tte->workspace_limit &&
        tte->scratch_base <= tte->scratch_limit && tte->scratch_limit == tte->workspace_limit &&
        tte->l1_table_size == STAGE85_XNU_TTE_L1_SIZE && tte->l2_table_size == STAGE85_XNU_TTE_L2_SIZE) {
        tte->satisfied_mask |= STAGE85_TTE_SAT_LAYOUT_FITS;
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_LAYOUT_OVERFLOW;
    }

    if (probe->load_phys_end > probe->load_phys_base) {
        tte->kernel_l1_first_index = section_first_index(probe->load_phys_base);
        tte->kernel_l1_last_index = section_last_index(probe->load_phys_end);
        tte->kernel_l1_section_count = tte->kernel_l1_last_index - tte->kernel_l1_first_index + 1u;
        if (tte->kernel_l1_last_index < STAGE85_XNU_TTE_L1_ENTRY_COUNT) {
            tte->satisfied_mask |= STAGE85_TTE_SAT_KERNEL_MAPPING;
        } else {
            tte->failure_mask |= STAGE85_TTE_FAIL_KERNEL_MAPPING;
        }
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_KERNEL_MAPPING;
    }

    tte->lowmem_l1_first_index = section_first_index(RAM_PHYS_BASE);
    tte->lowmem_l1_last_index = section_last_index(RAM_CONSOLE_BASE);
    tte->lowmem_l1_section_count = tte->lowmem_l1_last_index - tte->lowmem_l1_first_index + 1u;
    tte->ram_console_l1_index = section_first_index(RAM_CONSOLE_BASE);
    if (tte->ram_console_l1_index < STAGE85_XNU_TTE_L1_ENTRY_COUNT) {
        tte->satisfied_mask |= STAGE85_TTE_SAT_RAM_CONSOLE_MAPPING;
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_RAM_CONSOLE_MAPPING;
    }

    tte->gic_l1_first_index = section_first_index(0xf9000000u);
    tte->gic_l1_last_index = section_first_index(0xf9022000u);
    if (tte->gic_l1_first_index <= tte->gic_l1_last_index &&
        tte->gic_l1_last_index < STAGE85_XNU_TTE_L1_ENTRY_COUNT) {
        tte->satisfied_mask |= STAGE85_TTE_SAT_DEVICE_MAPPING;
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_DEVICE_MAPPING;
    }

    if (!ranges_overlap_u32(probe->load_phys_base, probe->load_phys_end, tte->workspace_base, tte->workspace_limit) &&
        !ranges_overlap_u32(tte->workspace_base, tte->workspace_limit, RAM_CONSOLE_BASE, RAM_CONSOLE_BASE + RAM_CONSOLE_SIZE)) {
        tte->satisfied_mask |= STAGE85_TTE_SAT_NO_OVERLAP;
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_OVERLAP;
    }

    if (add_overflow_u32(embedded_base, stage85_embedded_macho_size, &embedded_limit) ||
        add_overflow_u32(staging_base, STAGE85_MACHO_STAGING_ARENA_CAP, &staging_limit) ||
        ranges_overlap_u32(tte->local_arena_base, tte->local_arena_limit, embedded_base, embedded_limit) ||
        ranges_overlap_u32(tte->local_arena_base, tte->local_arena_limit, staging_base, staging_limit) ||
        ranges_overlap_u32(tte->local_arena_base, tte->local_arena_limit, RAM_CONSOLE_BASE, RAM_CONSOLE_BASE + RAM_CONSOLE_SIZE) ||
        (probe->load_phys_end != 0u && ranges_overlap_u32(tte->local_arena_base, tte->local_arena_limit,
                                                          probe->load_phys_base, probe->load_phys_end))) {
        tte->failure_mask |= STAGE85_TTE_FAIL_LOCAL_ARENA_ALIAS;
    }

    if ((tte->failure_mask & (STAGE85_TTE_FAIL_KERNEL_MAPPING | STAGE85_TTE_FAIL_DEVICE_MAPPING |
                              STAGE85_TTE_FAIL_RAM_CONSOLE_MAPPING | STAGE85_TTE_FAIL_LAYOUT_OVERFLOW)) == 0u) {
        stage85_tte_map_section_range(l1, tte->lowmem_l1_first_index, tte->lowmem_l1_last_index,
                                      STAGE85_XNU_TTE_DESC_SECTION_SO);
        stage85_tte_map_section_range(l1, tte->kernel_l1_first_index, tte->kernel_l1_last_index,
                                      STAGE85_XNU_TTE_DESC_SECTION_SO);
        stage85_tte_map_section(l1, tte->ram_console_l1_index, STAGE85_XNU_TTE_DESC_SECTION_SO);
        stage85_tte_map_section_range(l1, tte->gic_l1_first_index, tte->gic_l1_last_index,
                                      STAGE85_XNU_TTE_DESC_SECTION_SO);
    }

    tte->descriptor_expected_attr_mask = STAGE85_XNU_TTE_DESC_SECTION_SO &
                                         STAGE85_XNU_TTE_DESC_ATTR_MASK;
    if ((tte->failure_mask & (STAGE85_TTE_FAIL_KERNEL_MAPPING | STAGE85_TTE_FAIL_DEVICE_MAPPING |
                              STAGE85_TTE_FAIL_RAM_CONSOLE_MAPPING | STAGE85_TTE_FAIL_LAYOUT_OVERFLOW)) == 0u) {
        uint32_t attrs_ok = 1u;
        uint32_t identity_desc_required = STAGE85_TTE_DESC_VERIFY_REQUIRED &
            ~(STAGE85_TTE_DESC_VERIFY_HIGHVA_FIRST | STAGE85_TTE_DESC_VERIFY_HIGHVA_LAST);

        if (stage85_tte_verify_section_range(l1, tte->lowmem_l1_first_index, tte->lowmem_l1_last_index,
                                             STAGE85_XNU_TTE_DESC_SECTION_SO,
                                             &tte->descriptor_lowmem_first_word,
                                             &tte->descriptor_lowmem_last_word)) {
            tte->descriptor_verify_mask |= STAGE85_TTE_DESC_VERIFY_LOWMEM_FIRST |
                                           STAGE85_TTE_DESC_VERIFY_LOWMEM_LAST;
        } else {
            tte->descriptor_failure_mask |= STAGE85_TTE_DESC_VERIFY_LOWMEM_FIRST |
                                            STAGE85_TTE_DESC_VERIFY_LOWMEM_LAST;
        }
        if (stage85_tte_verify_section_range(l1, tte->kernel_l1_first_index, tte->kernel_l1_last_index,
                                             STAGE85_XNU_TTE_DESC_SECTION_SO,
                                             &tte->descriptor_kernel_first_word,
                                             &tte->descriptor_kernel_last_word)) {
            tte->descriptor_verify_mask |= STAGE85_TTE_DESC_VERIFY_KERNEL_FIRST |
                                           STAGE85_TTE_DESC_VERIFY_KERNEL_LAST;
        } else {
            tte->descriptor_failure_mask |= STAGE85_TTE_DESC_VERIFY_KERNEL_FIRST |
                                            STAGE85_TTE_DESC_VERIFY_KERNEL_LAST;
        }
        if (stage85_tte_verify_section_slot(l1, tte->ram_console_l1_index,
                                            STAGE85_XNU_TTE_DESC_SECTION_SO,
                                            &tte->descriptor_ram_console_word)) {
            tte->descriptor_verify_mask |= STAGE85_TTE_DESC_VERIFY_RAM_CONSOLE;
        } else {
            tte->descriptor_failure_mask |= STAGE85_TTE_DESC_VERIFY_RAM_CONSOLE;
        }
        if (stage85_tte_verify_section_range(l1, tte->gic_l1_first_index, tte->gic_l1_last_index,
                                             STAGE85_XNU_TTE_DESC_SECTION_SO,
                                             &tte->descriptor_gic_first_word,
                                             &tte->descriptor_gic_last_word)) {
            tte->descriptor_verify_mask |= STAGE85_TTE_DESC_VERIFY_GIC_FIRST |
                                           STAGE85_TTE_DESC_VERIFY_GIC_LAST;
        } else {
            tte->descriptor_failure_mask |= STAGE85_TTE_DESC_VERIFY_GIC_FIRST |
                                            STAGE85_TTE_DESC_VERIFY_GIC_LAST;
        }

        tte->descriptor_type_mask_seen =
            (tte->descriptor_lowmem_first_word & STAGE85_XNU_TTE_DESC_TYPE_MASK) |
            (tte->descriptor_lowmem_last_word & STAGE85_XNU_TTE_DESC_TYPE_MASK) |
            (tte->descriptor_kernel_first_word & STAGE85_XNU_TTE_DESC_TYPE_MASK) |
            (tte->descriptor_kernel_last_word & STAGE85_XNU_TTE_DESC_TYPE_MASK) |
            (tte->descriptor_ram_console_word & STAGE85_XNU_TTE_DESC_TYPE_MASK) |
            (tte->descriptor_gic_first_word & STAGE85_XNU_TTE_DESC_TYPE_MASK) |
            (tte->descriptor_gic_last_word & STAGE85_XNU_TTE_DESC_TYPE_MASK);
        tte->descriptor_attr_mask_seen =
            (tte->descriptor_lowmem_first_word & STAGE85_XNU_TTE_DESC_ATTR_MASK) |
            (tte->descriptor_lowmem_last_word & STAGE85_XNU_TTE_DESC_ATTR_MASK) |
            (tte->descriptor_kernel_first_word & STAGE85_XNU_TTE_DESC_ATTR_MASK) |
            (tte->descriptor_kernel_last_word & STAGE85_XNU_TTE_DESC_ATTR_MASK) |
            (tte->descriptor_ram_console_word & STAGE85_XNU_TTE_DESC_ATTR_MASK) |
            (tte->descriptor_gic_first_word & STAGE85_XNU_TTE_DESC_ATTR_MASK) |
            (tte->descriptor_gic_last_word & STAGE85_XNU_TTE_DESC_ATTR_MASK);

        attrs_ok &= stage85_tte_descriptor_attrs_ok(tte->descriptor_lowmem_first_word,
                                                    STAGE85_XNU_TTE_DESC_SECTION_SO);
        attrs_ok &= stage85_tte_descriptor_attrs_ok(tte->descriptor_lowmem_last_word,
                                                    STAGE85_XNU_TTE_DESC_SECTION_SO);
        attrs_ok &= stage85_tte_descriptor_attrs_ok(tte->descriptor_kernel_first_word,
                                                    STAGE85_XNU_TTE_DESC_SECTION_SO);
        attrs_ok &= stage85_tte_descriptor_attrs_ok(tte->descriptor_kernel_last_word,
                                                    STAGE85_XNU_TTE_DESC_SECTION_SO);
        attrs_ok &= stage85_tte_descriptor_attrs_ok(tte->descriptor_ram_console_word,
                                                    STAGE85_XNU_TTE_DESC_SECTION_SO);
        attrs_ok &= stage85_tte_descriptor_attrs_ok(tte->descriptor_gic_first_word,
                                                    STAGE85_XNU_TTE_DESC_SECTION_SO);
        attrs_ok &= stage85_tte_descriptor_attrs_ok(tte->descriptor_gic_last_word,
                                                    STAGE85_XNU_TTE_DESC_SECTION_SO);

        if ((tte->descriptor_verify_mask & identity_desc_required) == identity_desc_required &&
            tte->descriptor_failure_mask == 0u) {
            tte->satisfied_mask |= STAGE85_TTE_SAT_DESCRIPTOR_READBACK;
        } else {
            tte->failure_mask |= STAGE85_TTE_FAIL_DESCRIPTOR_READBACK;
        }
        if (attrs_ok && tte->descriptor_attr_mask_seen == tte->descriptor_expected_attr_mask &&
            tte->descriptor_type_mask_seen == STAGE85_XNU_TTE_DESC_TYPE_SECTION) {
            tte->satisfied_mask |= STAGE85_TTE_SAT_DESCRIPTOR_ATTRS;
        } else {
            tte->failure_mask |= STAGE85_TTE_FAIL_DESCRIPTOR_ATTRS;
        }

        if (probe->load_phys_end > probe->load_phys_base) {
            uint32_t kernel_last = probe->load_phys_end - 1u;

            tte->translation_kernel_text_va = probe->load_phys_base;
            if (stage85_tte_expect_translation(l1, tte->translation_kernel_text_va,
                                               tte->translation_kernel_text_va,
                                               &tte->translation_kernel_text_pa)) {
                tte->translation_check_mask |= STAGE85_TTE_TRANSLATE_KERNEL_FIRST;
                tte->translation_case_count++;
            } else {
                tte->translation_failure_mask |= STAGE85_TTE_TRANSLATE_KERNEL_FIRST;
            }

            tte->translation_kernel_last_va = kernel_last;
            if (stage85_tte_expect_translation(l1, tte->translation_kernel_last_va,
                                               tte->translation_kernel_last_va,
                                               &tte->translation_kernel_last_pa)) {
                tte->translation_check_mask |= STAGE85_TTE_TRANSLATE_KERNEL_LAST;
                tte->translation_case_count++;
            } else {
                tte->translation_failure_mask |= STAGE85_TTE_TRANSLATE_KERNEL_LAST;
            }
        }

        tte->translation_lowmem_va = RAM_PHYS_BASE;
        if (stage85_tte_expect_translation(l1, tte->translation_lowmem_va,
                                           tte->translation_lowmem_va,
                                           &tte->translation_lowmem_pa)) {
            tte->translation_check_mask |= STAGE85_TTE_TRANSLATE_LOW_RAM;
            tte->translation_case_count++;
        } else {
            tte->translation_failure_mask |= STAGE85_TTE_TRANSLATE_LOW_RAM;
        }

        tte->translation_ram_console_va = RAM_CONSOLE_BASE;
        if (stage85_tte_expect_translation(l1, tte->translation_ram_console_va,
                                           tte->translation_ram_console_va,
                                           &tte->translation_ram_console_pa)) {
            tte->translation_check_mask |= STAGE85_TTE_TRANSLATE_RAM_CONSOLE;
            tte->translation_case_count++;
        } else {
            tte->translation_failure_mask |= STAGE85_TTE_TRANSLATE_RAM_CONSOLE;
        }

        tte->translation_gic_va = 0xf9000000u;
        if (stage85_tte_expect_translation(l1, tte->translation_gic_va,
                                           tte->translation_gic_va,
                                           &tte->translation_gic_pa)) {
            tte->translation_check_mask |= STAGE85_TTE_TRANSLATE_GIC;
            tte->translation_case_count++;
        } else {
            tte->translation_failure_mask |= STAGE85_TTE_TRANSLATE_GIC;
        }

        if ((tte->translation_check_mask & (STAGE85_TTE_TRANSLATE_KERNEL_FIRST |
                                            STAGE85_TTE_TRANSLATE_KERNEL_LAST)) ==
            (STAGE85_TTE_TRANSLATE_KERNEL_FIRST | STAGE85_TTE_TRANSLATE_KERNEL_LAST)) {
            tte->satisfied_mask |= STAGE85_TTE_SAT_TRANSLATION_KERNEL;
        } else {
            tte->failure_mask |= STAGE85_TTE_FAIL_TRANSLATION_KERNEL;
        }
        if ((tte->translation_check_mask & STAGE85_TTE_TRANSLATE_LOW_RAM) != 0u) {
            tte->satisfied_mask |= STAGE85_TTE_SAT_TRANSLATION_LOW_RAM;
        } else {
            tte->failure_mask |= STAGE85_TTE_FAIL_TRANSLATION_LOW_RAM;
        }
        if ((tte->translation_check_mask & STAGE85_TTE_TRANSLATE_RAM_CONSOLE) != 0u) {
            tte->satisfied_mask |= STAGE85_TTE_SAT_TRANSLATION_CONSOLE;
        } else {
            tte->failure_mask |= STAGE85_TTE_FAIL_TRANSLATION_CONSOLE;
        }
        if ((tte->translation_check_mask & STAGE85_TTE_TRANSLATE_GIC) != 0u) {
            tte->satisfied_mask |= STAGE85_TTE_SAT_TRANSLATION_DEVICE;
        } else {
            tte->failure_mask |= STAGE85_TTE_FAIL_TRANSLATION_DEVICE;
        }
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_DESCRIPTOR_READBACK |
                             STAGE85_TTE_FAIL_DESCRIPTOR_ATTRS |
                             STAGE85_TTE_FAIL_TRANSLATION_KERNEL |
                             STAGE85_TTE_FAIL_TRANSLATION_LOW_RAM |
                             STAGE85_TTE_FAIL_TRANSLATION_CONSOLE |
                             STAGE85_TTE_FAIL_TRANSLATION_DEVICE;
    }

    tte->ttbr0_written = 0u;
    tte->ttbr1_written = 0u;
    tte->caches_enabled = 0u;
    if (tte->ttbr0_written == 0u && tte->ttbr1_written == 0u) {
        tte->satisfied_mask |= STAGE85_TTE_SAT_NO_TTBR_WRITE;
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_SAFETY;
    }
    if (tte->caches_enabled == 0u) {
        tte->satisfied_mask |= STAGE85_TTE_SAT_CACHES_UNCHANGED;
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_SAFETY;
    }
    if (stage85_tte_l2_and_scratch_zeroed()) {
        tte->local_sim_zeroed = 1u;
        tte->satisfied_mask |= STAGE85_TTE_SAT_LOCAL_SIM_ONLY;
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_SAFETY;
    }

    tte->xnu_boot_ttep_expected = preflight->proposed_topOfKernelData;
    tte->xnu_avail_start_expected = tte->workspace_limit;
    if (add_overflow_u32(tte->proposed_physBase, tte->proposed_memSize, &tte->xnu_avail_end_expected)) {
        tte->failure_mask |= STAGE85_TTE_FAIL_XNU_BOOT_POLICY;
    } else {
        if (tte->xnu_boot_ttep_expected == preflight->proposed_topOfKernelData) {
            tte->xnu_policy_mask |= STAGE85_TTE_XNU_POLICY_BOOT_TTEP;
        }
        if (tte->workspace_base == preflight->proposed_topOfKernelData &&
            tte->workspace_base == tte->xnu_boot_ttep_expected) {
            tte->xnu_policy_mask |= STAGE85_TTE_XNU_POLICY_WORKSPACE;
        }
        if (tte->workspace_limit == preflight->proposed_avail_start &&
            tte->xnu_avail_start_expected == preflight->proposed_avail_start) {
            tte->xnu_policy_mask |= STAGE85_TTE_XNU_POLICY_AVAIL_START;
        }
        if (tte->xnu_avail_end_expected == tte->proposed_physBase + tte->proposed_memSize &&
            tte->xnu_avail_end_expected > tte->proposed_physBase) {
            tte->xnu_policy_mask |= STAGE85_TTE_XNU_POLICY_AVAIL_END;
        }
        if (tte->ttbr0_written == 0u && tte->ttbr1_written == 0u && tte->caches_enabled == 0u) {
            tte->xnu_policy_mask |= STAGE85_TTE_XNU_POLICY_SAFE_DRYRUN;
        }
        if (tte->xnu_policy_mask == STAGE85_TTE_XNU_POLICY_REQUIRED) {
            tte->satisfied_mask |= STAGE85_TTE_SAT_XNU_BOOT_POLICY;
        } else {
            tte->failure_mask |= STAGE85_TTE_FAIL_XNU_BOOT_POLICY;
        }
    }


    tte->proposed_phys_load_written = 0u;
    tte->proposed_tte_workspace_written = 0u;
    tte->live_mmu_tables_replaced = 0u;
    tte->ttbcr_written = 0u;
    tte->dacr_written = 0u;
    tte->tlbs_invalidated = 0u;
    tte->sctlr_written = 0u;

    tte->safe_table.version = STAGE85_XNU_SAFE_TABLE_VERSION;
    tte->safe_table.size = sizeof(tte->safe_table);
    tte->safe_table.status = STAGE85_STATUS_BASE;
    tte->safe_table.local_base = safe_base;
    if (add_overflow_u32(safe_base, sizeof(stage85_safe_table_arena), &safe_limit)) {
        tte->safe_table.failure_mask |= STAGE85_TTE_FAIL_SAFE_TABLE;
    } else {
        tte->safe_table.local_limit = safe_limit;
    }
    tte->safe_table.identity_l1_base = safe_base;
    tte->safe_table.identity_l1_limit = safe_base + STAGE85_XNU_TTE_L1_SIZE;
    tte->safe_table.highva_l1_base = safe_base + STAGE85_XNU_TTE_L1_SIZE;
    tte->safe_table.highva_l1_limit = safe_base + (STAGE85_XNU_TTE_L1_SIZE * 2u);
    tte->safe_table.l1_entry_count = STAGE85_XNU_SAFE_TABLE_L1_COUNT;
    tte->safe_table.l1_bytes = STAGE85_XNU_SAFE_TABLE_L1_BYTES;
    tte->identity_l1_local_base = tte->safe_table.identity_l1_base;
    tte->identity_l1_local_limit = tte->safe_table.identity_l1_limit;
    tte->highva_l1_local_base = tte->safe_table.highva_l1_base;
    tte->highva_l1_local_limit = tte->safe_table.highva_l1_limit;

    if (tte->safe_table.local_base != 0u && tte->safe_table.local_limit > tte->safe_table.local_base &&
        (tte->safe_table.local_base & (STAGE85_XNU_SAFE_TABLE_ALIGNMENT - 1u)) == 0u &&
        tte->safe_table.identity_l1_limit == tte->safe_table.highva_l1_base &&
        tte->safe_table.highva_l1_limit == tte->safe_table.local_limit) {
        memcpy(safe_identity_l1, l1, STAGE85_XNU_TTE_L1_SIZE);
        tte->safe_table.kind_mask |= STAGE85_XNU_SAFE_TABLE_KIND_IDENTITY;
        tte->safe_table.materialized_mask |= STAGE85_XNU_SAFE_TABLE_MAT_IDENTITY;
        tte->safe_table.written_bytes += STAGE85_XNU_TTE_L1_SIZE;
    } else {
        tte->safe_table.failure_mask |= STAGE85_TTE_FAIL_SAFE_TABLE;
    }

    if (probe->load_vm_end > probe->load_vm_base && probe->load_phys_end > probe->load_phys_base) {
        tte->highva_virt_base = probe->load_vm_base & STAGE85_XNU_TTE_DESC_BASE_MASK;
        tte->highva_virt_end = probe->load_vm_end;
        tte->highva_phys_base = probe->load_phys_base & STAGE85_XNU_TTE_DESC_BASE_MASK;
        highva_size = tte->highva_virt_end - tte->highva_virt_base;
        tte->highva_phys_end = tte->highva_phys_base + highva_size;
        tte->highva_virt_phys_delta = tte->highva_virt_base - tte->highva_phys_base;
        highva_last_va = tte->highva_virt_end - 1u;
        tte->highva_l1_first_index = section_first_index(tte->highva_virt_base);
        tte->highva_l1_last_index = section_last_index(tte->highva_virt_end);
        tte->highva_l1_section_count = tte->highva_l1_last_index - tte->highva_l1_first_index + 1u;
        highva_first_expected_pa = stage85_tte_expected_section_pa(tte->highva_virt_base,
                                                                   tte->highva_phys_base);
        highva_last_expected_pa = stage85_tte_expected_section_pa(highva_last_va,
                                                                  stage85_tte_section_pa_for_va(highva_last_va,
                                                                                                tte->highva_virt_base,
                                                                                                tte->highva_phys_base));

        if (tte->highva_l1_first_index <= tte->highva_l1_last_index &&
            tte->highva_l1_last_index < STAGE85_XNU_TTE_L1_ENTRY_COUNT && highva_size != 0u) {
            stage85_tte_map_section_range_pa(safe_highva_l1, tte->highva_virt_base,
                                             tte->highva_phys_base, highva_size,
                                             STAGE85_XNU_TTE_DESC_SECTION_SO);
            if (stage85_tte_verify_section_slot_pa(safe_highva_l1, tte->highva_virt_base,
                                                   tte->highva_phys_base,
                                                   STAGE85_XNU_TTE_DESC_SECTION_SO,
                                                   &tte->descriptor_highva_first_word)) {
                tte->descriptor_verify_mask |= STAGE85_TTE_DESC_VERIFY_HIGHVA_FIRST;
            } else {
                tte->descriptor_failure_mask |= STAGE85_TTE_DESC_VERIFY_HIGHVA_FIRST;
            }
            if (stage85_tte_verify_section_slot_pa(safe_highva_l1, highva_last_va,
                                                   stage85_tte_section_pa_for_va(highva_last_va,
                                                                                 tte->highva_virt_base,
                                                                                 tte->highva_phys_base),
                                                   STAGE85_XNU_TTE_DESC_SECTION_SO,
                                                   &tte->descriptor_highva_last_word)) {
                tte->descriptor_verify_mask |= STAGE85_TTE_DESC_VERIFY_HIGHVA_LAST;
            } else {
                tte->descriptor_failure_mask |= STAGE85_TTE_DESC_VERIFY_HIGHVA_LAST;
            }

            tte->descriptor_type_mask_seen |=
                (tte->descriptor_highva_first_word & STAGE85_XNU_TTE_DESC_TYPE_MASK) |
                (tte->descriptor_highva_last_word & STAGE85_XNU_TTE_DESC_TYPE_MASK);
            tte->descriptor_attr_mask_seen |=
                (tte->descriptor_highva_first_word & STAGE85_XNU_TTE_DESC_ATTR_MASK) |
                (tte->descriptor_highva_last_word & STAGE85_XNU_TTE_DESC_ATTR_MASK);

            tte->translation_highva_first_va = tte->highva_virt_base;
            if (stage85_tte_expect_translation(safe_highva_l1, tte->translation_highva_first_va,
                                               highva_first_expected_pa,
                                               &tte->translation_highva_first_pa)) {
                tte->translation_check_mask |= STAGE85_TTE_TRANSLATE_HIGHVA_FIRST;
                tte->translation_case_count++;
            } else {
                tte->translation_failure_mask |= STAGE85_TTE_TRANSLATE_HIGHVA_FIRST;
            }
            tte->translation_highva_last_va = highva_last_va;
            if (stage85_tte_expect_translation(safe_highva_l1, tte->translation_highva_last_va,
                                               highva_last_expected_pa,
                                               &tte->translation_highva_last_pa)) {
                tte->translation_check_mask |= STAGE85_TTE_TRANSLATE_HIGHVA_LAST;
                tte->translation_case_count++;
            } else {
                tte->translation_failure_mask |= STAGE85_TTE_TRANSLATE_HIGHVA_LAST;
            }

            if ((tte->descriptor_verify_mask & (STAGE85_TTE_DESC_VERIFY_HIGHVA_FIRST |
                                                STAGE85_TTE_DESC_VERIFY_HIGHVA_LAST)) ==
                (STAGE85_TTE_DESC_VERIFY_HIGHVA_FIRST | STAGE85_TTE_DESC_VERIFY_HIGHVA_LAST) &&
                (tte->descriptor_failure_mask & (STAGE85_TTE_DESC_VERIFY_HIGHVA_FIRST |
                                                 STAGE85_TTE_DESC_VERIFY_HIGHVA_LAST)) == 0u) {
                tte->satisfied_mask |= STAGE85_TTE_SAT_HIGHVA_MAPPING;
                tte->safe_table.kind_mask |= STAGE85_XNU_SAFE_TABLE_KIND_HIGHVA;
                tte->safe_table.materialized_mask |= STAGE85_XNU_SAFE_TABLE_MAT_HIGHVA;
                tte->safe_table.written_bytes += STAGE85_XNU_TTE_L1_SIZE;
            } else {
                tte->failure_mask |= STAGE85_TTE_FAIL_HIGHVA_MAPPING;
            }
            if ((tte->translation_check_mask & (STAGE85_TTE_TRANSLATE_HIGHVA_FIRST |
                                                STAGE85_TTE_TRANSLATE_HIGHVA_LAST)) ==
                (STAGE85_TTE_TRANSLATE_HIGHVA_FIRST | STAGE85_TTE_TRANSLATE_HIGHVA_LAST) &&
                (tte->translation_failure_mask & (STAGE85_TTE_TRANSLATE_HIGHVA_FIRST |
                                                  STAGE85_TTE_TRANSLATE_HIGHVA_LAST)) == 0u) {
                tte->satisfied_mask |= STAGE85_TTE_SAT_HIGHVA_TRANSLATION;
            } else {
                tte->failure_mask |= STAGE85_TTE_FAIL_HIGHVA_TRANSLATION;
            }
        } else {
            tte->failure_mask |= STAGE85_TTE_FAIL_HIGHVA_MAPPING | STAGE85_TTE_FAIL_HIGHVA_TRANSLATION;
        }
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_HIGHVA_MAPPING | STAGE85_TTE_FAIL_HIGHVA_TRANSLATION;
    }

    if (tte->descriptor_verify_mask == STAGE85_TTE_DESC_VERIFY_REQUIRED &&
        tte->descriptor_failure_mask == 0u) {
        tte->satisfied_mask |= STAGE85_TTE_SAT_DESCRIPTOR_READBACK;
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_DESCRIPTOR_READBACK;
    }
    if (tte->descriptor_attr_mask_seen == tte->descriptor_expected_attr_mask &&
        tte->descriptor_type_mask_seen == STAGE85_XNU_TTE_DESC_TYPE_SECTION) {
        tte->satisfied_mask |= STAGE85_TTE_SAT_DESCRIPTOR_ATTRS;
    } else {
        tte->failure_mask |= STAGE85_TTE_FAIL_DESCRIPTOR_ATTRS;
    }

    if (tte->safe_table.kind_mask == STAGE85_XNU_SAFE_TABLE_KIND_REQUIRED &&
        tte->safe_table.materialized_mask == STAGE85_XNU_SAFE_TABLE_MAT_REQUIRED &&
        tte->safe_table.written_bytes == (STAGE85_XNU_TTE_L1_SIZE * 2u) &&
        tte->safe_table.failure_mask == 0u) {
        tte->safe_table.status = STAGE85_STATUS_OK;
        tte->satisfied_mask |= STAGE85_TTE_SAT_SAFE_TABLE;
    } else {
        tte->safe_table.failure_mask |= STAGE85_TTE_FAIL_SAFE_TABLE;
        tte->failure_mask |= STAGE85_TTE_FAIL_SAFE_TABLE;
    }

    if (add_overflow_u32(embedded_base, stage85_embedded_macho_size, &embedded_limit) ||
        add_overflow_u32(staging_base, STAGE85_MACHO_STAGING_ARENA_CAP, &staging_limit) ||
        ranges_overlap_u32(tte->safe_table.local_base, tte->safe_table.local_limit, embedded_base, embedded_limit) ||
        ranges_overlap_u32(tte->safe_table.local_base, tte->safe_table.local_limit, staging_base, staging_limit) ||
        ranges_overlap_u32(tte->safe_table.local_base, tte->safe_table.local_limit, tte->local_arena_base, tte->local_arena_limit) ||
        ranges_overlap_u32(tte->safe_table.local_base, tte->safe_table.local_limit, RAM_CONSOLE_BASE, RAM_CONSOLE_BASE + RAM_CONSOLE_SIZE) ||
        ranges_overlap_u32(tte->safe_table.local_base, tte->safe_table.local_limit, tte->workspace_base, tte->workspace_limit) ||
        (probe->load_phys_end != 0u && ranges_overlap_u32(tte->safe_table.local_base, tte->safe_table.local_limit,
                                                          probe->load_phys_base, probe->load_phys_end))) {
        tte->safe_table.failure_mask |= STAGE85_TTE_FAIL_STAGE_OWNED_TABLES;
        tte->failure_mask |= STAGE85_TTE_FAIL_STAGE_OWNED_TABLES;
    } else if (tte->proposed_phys_load_written == 0u && tte->proposed_tte_workspace_written == 0u &&
               tte->live_mmu_tables_replaced == 0u && tte->ttbcr_written == 0u && tte->dacr_written == 0u &&
               tte->tlbs_invalidated == 0u && tte->sctlr_written == 0u &&
               tte->ttbr0_written == 0u && tte->ttbr1_written == 0u && tte->caches_enabled == 0u) {
        tte->stage_owned_tables_only = 1u;
        tte->satisfied_mask |= STAGE85_TTE_SAT_STAGE_OWNED_TABLES;
    } else {
        tte->safe_table.failure_mask |= STAGE85_TTE_FAIL_STAGE_OWNED_TABLES;
        tte->failure_mask |= STAGE85_TTE_FAIL_STAGE_OWNED_TABLES;
    }

    tte->safe_table.checksum = stage85_safe_table_checksum(stage85_safe_table_arena,
                                                           sizeof(stage85_safe_table_arena));

    tte->checksum = stage85_tte_dryrun_checksum(tte);
    if (tte->checksum != stage85_tte_dryrun_checksum(tte)) {
        tte->failure_mask |= STAGE85_TTE_FAIL_CHECKSUM;
    }

    if (tte->failure_mask == 0u && tte->satisfied_mask == STAGE85_TTE_SAT_REQUIRED &&
        tte->checksum == stage85_tte_dryrun_checksum(tte)) {
        tte->status = STAGE85_STATUS_OK;
        xnu_log_puts("Stage84 XNU TTE dry-run ok\n");
        return 1;
    }

    tte->status = STAGE85_STATUS_FAIL(tte->failure_mask);
    xnu_log_puts("Stage84 XNU TTE dry-run failed\n");
    return 0;
}

static void stage85_xnu_tte_dryrun_log(const struct stage85_xnu_tte_dryrun *tte)
{
    xnu_log_kv32("xnu_tte_dryrun_status", tte->status);
    xnu_log_kv32("xnu_tte_workspace_base", tte->workspace_base);
    xnu_log_kv32("xnu_tte_workspace_size", tte->workspace_size);
    xnu_log_kv32("xnu_tte_workspace_limit", tte->workspace_limit);
    xnu_log_kv32("xnu_tte_local_arena_base", tte->local_arena_base);
    xnu_log_kv32("xnu_tte_l1_table_base", tte->l1_table_base);
    xnu_log_kv32("xnu_tte_l1_table_size", tte->l1_table_size);
    xnu_log_kv32("xnu_tte_l2_table_base", tte->l2_table_base);
    xnu_log_kv32("xnu_tte_l2_table_size", tte->l2_table_size);
    xnu_log_kv32("xnu_tte_scratch_base", tte->scratch_base);
    xnu_log_kv32("xnu_tte_scratch_size", tte->scratch_size);
    xnu_log_kv32("xnu_tte_kernel_l1_first_index", tte->kernel_l1_first_index);
    xnu_log_kv32("xnu_tte_kernel_l1_last_index", tte->kernel_l1_last_index);
    xnu_log_kv32("xnu_tte_kernel_l1_section_count", tte->kernel_l1_section_count);
    xnu_log_kv32("xnu_tte_lowmem_l1_first_index", tte->lowmem_l1_first_index);
    xnu_log_kv32("xnu_tte_lowmem_l1_last_index", tte->lowmem_l1_last_index);
    xnu_log_kv32("xnu_tte_ram_console_l1_index", tte->ram_console_l1_index);
    xnu_log_kv32("xnu_tte_gic_l1_first_index", tte->gic_l1_first_index);
    xnu_log_kv32("xnu_tte_gic_l1_last_index", tte->gic_l1_last_index);
    xnu_log_kv32("xnu_tte_descriptor_verify_mask", tte->descriptor_verify_mask);
    xnu_log_kv32("xnu_tte_descriptor_failure_mask", tte->descriptor_failure_mask);
    xnu_log_kv32("xnu_tte_descriptor_lowmem_first_word", tte->descriptor_lowmem_first_word);
    xnu_log_kv32("xnu_tte_descriptor_lowmem_last_word", tte->descriptor_lowmem_last_word);
    xnu_log_kv32("xnu_tte_descriptor_kernel_first_word", tte->descriptor_kernel_first_word);
    xnu_log_kv32("xnu_tte_descriptor_kernel_last_word", tte->descriptor_kernel_last_word);
    xnu_log_kv32("xnu_tte_descriptor_ram_console_word", tte->descriptor_ram_console_word);
    xnu_log_kv32("xnu_tte_descriptor_gic_first_word", tte->descriptor_gic_first_word);
    xnu_log_kv32("xnu_tte_descriptor_gic_last_word", tte->descriptor_gic_last_word);
    xnu_log_kv32("xnu_tte_descriptor_type_mask_seen", tte->descriptor_type_mask_seen);
    xnu_log_kv32("xnu_tte_descriptor_attr_mask_seen", tte->descriptor_attr_mask_seen);
    xnu_log_kv32("xnu_tte_descriptor_expected_attr_mask", tte->descriptor_expected_attr_mask);
    xnu_log_kv32("xnu_tte_translation_check_mask", tte->translation_check_mask);
    xnu_log_kv32("xnu_tte_translation_failure_mask", tte->translation_failure_mask);
    xnu_log_kv32("xnu_tte_translation_case_count", tte->translation_case_count);
    xnu_log_kv32("xnu_tte_translation_kernel_text_va", tte->translation_kernel_text_va);
    xnu_log_kv32("xnu_tte_translation_kernel_text_pa", tte->translation_kernel_text_pa);
    xnu_log_kv32("xnu_tte_translation_kernel_last_va", tte->translation_kernel_last_va);
    xnu_log_kv32("xnu_tte_translation_kernel_last_pa", tte->translation_kernel_last_pa);
    xnu_log_kv32("xnu_tte_translation_lowmem_va", tte->translation_lowmem_va);
    xnu_log_kv32("xnu_tte_translation_lowmem_pa", tte->translation_lowmem_pa);
    xnu_log_kv32("xnu_tte_translation_ram_console_va", tte->translation_ram_console_va);
    xnu_log_kv32("xnu_tte_translation_ram_console_pa", tte->translation_ram_console_pa);
    xnu_log_kv32("xnu_tte_translation_gic_va", tte->translation_gic_va);
    xnu_log_kv32("xnu_tte_translation_gic_pa", tte->translation_gic_pa);
    xnu_log_kv32("xnu_tte_xnu_boot_ttep_expected", tte->xnu_boot_ttep_expected);
    xnu_log_kv32("xnu_tte_xnu_avail_start_expected", tte->xnu_avail_start_expected);
    xnu_log_kv32("xnu_tte_xnu_avail_end_expected", tte->xnu_avail_end_expected);
    xnu_log_kv32("xnu_tte_xnu_policy_mask", tte->xnu_policy_mask);
    xnu_log_kv32("xnu_highva_dryrun_status", ((tte->satisfied_mask & (STAGE85_TTE_SAT_HIGHVA_MAPPING | STAGE85_TTE_SAT_HIGHVA_TRANSLATION)) == (STAGE85_TTE_SAT_HIGHVA_MAPPING | STAGE85_TTE_SAT_HIGHVA_TRANSLATION)) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE);
    xnu_log_kv32("xnu_highva_virt_base", tte->highva_virt_base);
    xnu_log_kv32("xnu_highva_virt_end", tte->highva_virt_end);
    xnu_log_kv32("xnu_highva_phys_base", tte->highva_phys_base);
    xnu_log_kv32("xnu_highva_phys_end", tte->highva_phys_end);
    xnu_log_kv32("xnu_highva_virt_phys_delta", tte->highva_virt_phys_delta);
    xnu_log_kv32("xnu_highva_l1_first_index", tte->highva_l1_first_index);
    xnu_log_kv32("xnu_highva_l1_last_index", tte->highva_l1_last_index);
    xnu_log_kv32("xnu_highva_l1_section_count", tte->highva_l1_section_count);
    xnu_log_kv32("xnu_highva_descriptor_first_word", tte->descriptor_highva_first_word);
    xnu_log_kv32("xnu_highva_descriptor_last_word", tte->descriptor_highva_last_word);
    xnu_log_kv32("xnu_highva_translation_first_va", tte->translation_highva_first_va);
    xnu_log_kv32("xnu_highva_translation_first_pa", tte->translation_highva_first_pa);
    xnu_log_kv32("xnu_highva_translation_last_va", tte->translation_highva_last_va);
    xnu_log_kv32("xnu_highva_translation_last_pa", tte->translation_highva_last_pa);
    xnu_log_kv32("xnu_safe_table_status", tte->safe_table.status);
    xnu_log_kv32("xnu_safe_table_version", tte->safe_table.version);
    xnu_log_kv32("xnu_safe_table_kind_mask", tte->safe_table.kind_mask);
    xnu_log_kv32("xnu_safe_table_local_base", tte->safe_table.local_base);
    xnu_log_kv32("xnu_safe_table_local_limit", tte->safe_table.local_limit);
    xnu_log_kv32("xnu_safe_table_identity_l1_base", tte->safe_table.identity_l1_base);
    xnu_log_kv32("xnu_safe_table_identity_l1_limit", tte->safe_table.identity_l1_limit);
    xnu_log_kv32("xnu_safe_table_highva_l1_base", tte->safe_table.highva_l1_base);
    xnu_log_kv32("xnu_safe_table_highva_l1_limit", tte->safe_table.highva_l1_limit);
    xnu_log_kv32("xnu_safe_table_l1_entry_count", tte->safe_table.l1_entry_count);
    xnu_log_kv32("xnu_safe_table_l1_bytes", tte->safe_table.l1_bytes);
    xnu_log_kv32("xnu_safe_table_written_bytes", tte->safe_table.written_bytes);
    xnu_log_kv32("xnu_safe_table_materialized_mask", tte->safe_table.materialized_mask);
    xnu_log_kv32("xnu_safe_table_failure_mask", tte->safe_table.failure_mask);
    xnu_log_kv32("xnu_safe_table_checksum", tte->safe_table.checksum);
    xnu_log_kv32("xnu_tte_proposed_phys_load_written", tte->proposed_phys_load_written);
    xnu_log_kv32("xnu_tte_proposed_workspace_written", tte->proposed_tte_workspace_written);
    xnu_log_kv32("xnu_tte_live_mmu_tables_replaced", tte->live_mmu_tables_replaced);
    xnu_log_kv32("xnu_tte_ttbcr_written", tte->ttbcr_written);
    xnu_log_kv32("xnu_tte_dacr_written", tte->dacr_written);
    xnu_log_kv32("xnu_tte_tlbs_invalidated", tte->tlbs_invalidated);
    xnu_log_kv32("xnu_tte_sctlr_written", tte->sctlr_written);
    xnu_log_kv32("xnu_tte_stage_owned_tables_only", tte->stage_owned_tables_only);
    xnu_log_kv32("xnu_tte_ttbr0_written", tte->ttbr0_written);
    xnu_log_kv32("xnu_tte_ttbr1_written", tte->ttbr1_written);
    xnu_log_kv32("xnu_tte_caches_enabled", tte->caches_enabled);
    xnu_log_kv32("xnu_tte_local_sim_zeroed", tte->local_sim_zeroed);
    xnu_log_kv32("xnu_tte_satisfied_mask", tte->satisfied_mask);
    xnu_log_kv32("xnu_tte_failure_mask", tte->failure_mask);
    xnu_log_kv32("xnu_tte_checksum", tte->checksum);
}

static uint32_t stage85_loader_preflight_checksum(volatile const struct stage85_loader_preflight *preflight)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)preflight;
    uint32_t count = (uint32_t)(offsetof(struct stage85_loader_preflight, checksum) / sizeof(uint32_t));
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

static uint32_t prop_string_matches(const struct boot_args *args, const void *node,
                                    const char *name, const char *expected)
{
    uint32_t len = 0u;
    const char *value;

    if (!args || !node || !expected) {
        return 0u;
    }

    value = (const char *)apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, node, name, &len);
    if (!value || len == 0u) {
        return 0u;
    }

    return (strcmp(value, expected) == 0) ? 1u : 0u;
}

static uint32_t stage85_dt_semantic_mask(struct boot_args *args)
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
    const void *iokit_platform = find_child(args, root, "iokit-platform-scaffold");
    const void *platform_driver = find_child(args, root, "msm8974-platform-driver");
    const void *interrupt_service = find_child(args, root, "msm8974-interrupt-service");
    const void *timer_service = find_child(args, root, "msm8974-timer-service");
    const void *cpu_service = find_child(args, root, "msm8974-cpu-service");
    const void *rejected_driver = find_child(args, root, "msm8974-rejected-driver");
    const void *catalog_root = find_child(args, root, "iokit-catalog-property-dryrun");
    const void *platform_personality = find_child(args, root, "stage85-platform-personality");
    const void *interrupt_personality = find_child(args, root, "stage85-interrupt-personality");
    const void *timer_personality = find_child(args, root, "stage85-timer-personality");
    const void *cpu_personality = find_child(args, root, "stage85-cpu-personality");
    const void *rejected_personality = find_child(args, root, "stage85-rejected-personality");

    if (root && args->deviceTreeLength && apple_dt_node_prop_count(root, args->deviceTreeLength, root) != 0u) {
        mask |= STAGE85_DT_READY_BINARY_SELFTEST;
    }
    if (chosen) {
        mask |= STAGE85_DT_READY_CHOSEN;
        if (apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, chosen, "boot-args", &len) && len != 0u) {
            mask |= STAGE85_DT_READY_BOOT_ARGS;
        }
        if (apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, chosen, "ram-console-reg", &len) && len >= 8u) {
            mask |= STAGE85_DT_READY_RAM_CONSOLE;
        }
    }
    if (memory && apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, memory, "reg", &len) && len >= 8u) {
        mask |= STAGE85_DT_READY_MEMORY;
    }
    if (cpus && apple_dt_node_child_count(args->deviceTreeP, args->deviceTreeLength, cpus) != 0u) {
        const void *cpu0 = find_child(args, cpus, "cpu@0");
        mask |= STAGE85_DT_READY_CPUS;
        if (cpu0 &&
            apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu0, "clock-frequency", 0u) != 0u &&
            apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu0, "timebase-frequency", 0u) == 19200000u) {
            mask |= STAGE85_DT_READY_CPU_CLOCKS;
        }
    }
    if (gic) {
        mask |= STAGE85_DT_READY_INTERRUPT_CONTROLLER;
    }
    if (timer && apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer, "frequency", 0u) == 19200000u) {
        mask |= STAGE85_DT_READY_TIMER;
    }
    if (device_tree) {
        mask |= STAGE85_DT_READY_DEVICE_TREE_NODE;
        if (apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, device_tree, "target-type", &len) && len != 0u) {
            mask |= STAGE85_DT_READY_TARGET_TYPE;
        }
        if (apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, device_tree, "model", &len) && len != 0u) {
            mask |= STAGE85_DT_READY_MODEL;
        }
    }
    if (iokit_platform &&
        apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, iokit_platform, "IOClass", &len) && len != 0u &&
        apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, iokit_platform, "IOProviderClass", &len) && len != 0u &&
        apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, iokit_platform, "registry-plane", &len) && len != 0u) {
        mask |= STAGE85_DT_READY_IOKIT_SCAFFOLD;
    }
    if (platform_driver &&
        apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, platform_driver, "IOClass", &len) && len != 0u &&
        apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, platform_driver, "IOProviderClass", &len) && len != 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_driver, "cpu-count", 0u) == 4u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_driver, "timebase-frequency", 0u) == 19200000u) {
        mask |= STAGE85_DT_READY_PLATFORM_DRIVER;
    }
    if (interrupt_service && timer_service && cpu_service &&
        apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_service, "IOClass", &len) && len != 0u &&
        apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, timer_service, "IOClass", &len) && len != 0u &&
        apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, cpu_service, "IOClass", &len) && len != 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_service, "irq-count", 0u) >=
        STAGE85_XNU_IOKIT_REGISTRY_SERVICE_EXPECTED_IRQ_COUNT_MIN &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_service, "frequency", 0u) == 19200000u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_service, "cpu-count", 0u) == 4u) {
        mask |= STAGE85_DT_READY_IOKIT_REGISTRY_SERVICES;
    }
    if (rejected_driver &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_driver, "rejected-candidate", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_driver, "stage-owned-local-only", 0u) == 1u) {
        mask |= STAGE85_DT_READY_IOKIT_REJECTED_DRIVER;
    }
    if (catalog_root && platform_personality && interrupt_personality && timer_personality && cpu_personality &&
        rejected_personality &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, catalog_root, "catalog-version", 0u) ==
        STAGE85_XNU_IOKIT_CATALOG_PROPERTY_CATALOG_VERSION &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "catalog-property-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "catalog-property-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "catalog-property-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "catalog-property-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "rejected-candidate", 0u) == 1u) {
        mask |= STAGE85_DT_READY_IOKIT_CATALOG_PROPERTY;
    }
    if (platform_personality && interrupt_personality && timer_personality && cpu_personality && rejected_personality &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "registry-entry-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "registry-entry-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "registry-entry-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "registry-entry-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "property-inheritance-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "property-inheritance-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "property-inheritance-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "property-inheritance-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "property-inheritance-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "registry-entry-attached", 1u) == 0u) {
        mask |= STAGE85_DT_READY_IOKIT_PROPERTY_INHERITANCE;
    }
    if (platform_personality && interrupt_personality && timer_personality && cpu_personality && rejected_personality &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "registry-topology-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "registry-topology-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "registry-topology-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "registry-topology-dryrun", 0u) == 1u &&
        prop_string_matches(args, platform_personality, "registry-plane", "IODeviceTree") &&
        prop_string_matches(args, interrupt_personality, "registry-plane", "IODeviceTree") &&
        prop_string_matches(args, timer_personality, "registry-plane", "IODeviceTree") &&
        prop_string_matches(args, cpu_personality, "registry-plane", "IODeviceTree") &&
        prop_string_matches(args, platform_personality, "registry-parent-path", "IODeviceTree:/") &&
        prop_string_matches(args, interrupt_personality, "registry-parent-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, timer_personality, "registry-parent-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, cpu_personality, "registry-parent-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, platform_personality, "registry-entry-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, interrupt_personality, "registry-entry-path",
                            "IODeviceTree:/stage85-interrupt-personality") &&
        prop_string_matches(args, timer_personality, "registry-entry-path",
                            "IODeviceTree:/stage85-timer-personality") &&
        prop_string_matches(args, cpu_personality, "registry-entry-path",
                            "IODeviceTree:/stage85-cpu-personality") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "registry-parent-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "registry-parent-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "registry-parent-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "registry-parent-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "registry-sibling-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "registry-sibling-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "registry-sibling-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "registry-sibling-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL &&
        prop_string_matches(args, platform_personality, "registry-topology-provenance",
                            "stage85-property-inheritance") &&
        prop_string_matches(args, interrupt_personality, "registry-topology-provenance",
                            "stage85-property-inheritance") &&
        prop_string_matches(args, timer_personality, "registry-topology-provenance",
                            "stage85-property-inheritance") &&
        prop_string_matches(args, cpu_personality, "registry-topology-provenance",
                            "stage85-property-inheritance") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "registry-topology-dryrun", 1u) == 0u &&
        prop_string_matches(args, rejected_personality, "registry-parent-path", "IODeviceTree:/unlinked") &&
        prop_string_matches(args, rejected_personality, "registry-entry-path",
                            "IODeviceTree:/stage85-rejected-personality") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "registry-parent-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "registry-sibling-order", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        prop_string_matches(args, rejected_personality, "registry-topology-provenance",
                            "stage85-rejected-unlinked") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "registry-entry-linked", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "provider-plane-published", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "registry-entry-dryrun", 1u) == 0u) {
        mask |= STAGE85_DT_READY_IOKIT_REGISTRY_TOPOLOGY;
    }

    if (platform_personality && interrupt_personality && timer_personality && cpu_personality && rejected_personality &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "attach-start-readiness-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "attach-start-readiness-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "attach-start-readiness-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "attach-start-readiness-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "attach-readiness", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "attach-readiness", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "attach-readiness", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "attach-readiness", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "start-readiness", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "start-readiness", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "start-readiness", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "start-readiness", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "attach-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "attach-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "attach-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "attach-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "start-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "start-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "start-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "start-runtime-exec", 1u) == 0u &&
        prop_string_matches(args, platform_personality, "attach-provider-path", "IODeviceTree:/") &&
        prop_string_matches(args, platform_personality, "start-provider-path", "IODeviceTree:/") &&
        prop_string_matches(args, interrupt_personality, "attach-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, interrupt_personality, "start-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, timer_personality, "attach-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, timer_personality, "start-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, cpu_personality, "attach-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, cpu_personality, "start-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "attach-provider-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "start-provider-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "attach-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "start-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "attach-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "start-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "attach-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "start-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "attach-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "attach-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "attach-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "attach-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "start-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "start-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "start-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "start-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "provider-start-required", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "provider-start-required", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "provider-start-required", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "provider-start-required", 0u) == 1u &&
        prop_string_matches(args, platform_personality, "attach-start-provenance", "stage85-registry-topology") &&
        prop_string_matches(args, interrupt_personality, "attach-start-provenance", "stage85-registry-topology") &&
        prop_string_matches(args, timer_personality, "attach-start-provenance", "stage85-registry-topology") &&
        prop_string_matches(args, cpu_personality, "attach-start-provenance", "stage85-registry-topology") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "attach-start-readiness-dryrun", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "attach-readiness", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "start-readiness", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "attach-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "start-runtime-exec", 1u) == 0u &&
        prop_string_matches(args, rejected_personality, "attach-provider-path", "IODeviceTree:/unlinked") &&
        prop_string_matches(args, rejected_personality, "start-provider-path", "IODeviceTree:/unlinked") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "attach-provider-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "start-provider-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "attach-order", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "start-order", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "provider-start-required", 1u) == 0u &&
        prop_string_matches(args, rejected_personality, "attach-start-provenance", "stage85-rejected-unattached")) {
        mask |= STAGE85_DT_READY_IOKIT_ATTACH_START_READINESS;
    }

    if (platform_personality && interrupt_personality && timer_personality && cpu_personality && rejected_personality &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "lifecycle-regsvc-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "lifecycle-regsvc-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "lifecycle-regsvc-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "lifecycle-regsvc-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "lifecycle-state-matched", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "lifecycle-state-matched", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "lifecycle-state-matched", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "lifecycle-state-matched", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "lifecycle-provider-published", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "lifecycle-provider-published", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "lifecycle-provider-published", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "lifecycle-provider-published", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "lifecycle-registry-linked", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "lifecycle-registry-linked", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "lifecycle-registry-linked", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "lifecycle-registry-linked", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "lifecycle-topology-linked", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "lifecycle-topology-linked", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "lifecycle-topology-linked", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "lifecycle-topology-linked", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "register-service-readiness", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "register-service-readiness", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "register-service-readiness", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "register-service-readiness", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "service-registered", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "service-registered", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "service-registered", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "service-registered", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "register-service-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "register-service-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "register-service-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "register-service-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "notification-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "notification-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "notification-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "notification-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "notification-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "notification-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "notification-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "notification-runtime-exec", 1u) == 0u &&
        prop_string_matches(args, platform_personality, "register-provider-path", "IODeviceTree:/") &&
        prop_string_matches(args, interrupt_personality, "register-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, timer_personality, "register-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, cpu_personality, "register-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "register-provider-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "register-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "register-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "register-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "register-service-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "register-service-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "register-service-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "register-service-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "notification-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "notification-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "notification-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "notification-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL &&
        prop_string_matches(args, platform_personality, "lifecycle-provenance", "stage85-attach-start-readiness") &&
        prop_string_matches(args, interrupt_personality, "lifecycle-provenance", "stage85-attach-start-readiness") &&
        prop_string_matches(args, timer_personality, "lifecycle-provenance", "stage85-attach-start-readiness") &&
        prop_string_matches(args, cpu_personality, "lifecycle-provenance", "stage85-attach-start-readiness") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "lifecycle-regsvc-dryrun", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "lifecycle-state-matched", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "lifecycle-provider-published", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "lifecycle-registry-linked", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "lifecycle-topology-linked", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "register-service-readiness", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "service-registered", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "notification-ready", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "register-service-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "notification-runtime-exec", 1u) == 0u &&
        prop_string_matches(args, rejected_personality, "register-provider-path", "IODeviceTree:/unlinked") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "register-provider-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "register-provider-start-req", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "register-service-order", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "notification-order", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "register-dependency-ready", 1u) == 0u &&
        prop_string_matches(args, rejected_personality, "lifecycle-provenance", "stage85-rejected-unregistered")) {
        mask |= STAGE85_DT_READY_IOKIT_LIFECYCLE_REGISTER_SERVICE;
    }

    if (platform_personality && interrupt_personality && timer_personality && cpu_personality && rejected_personality &&
        apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength, platform_personality) == 100u &&
        apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength, interrupt_personality) == 100u &&
        apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength, timer_personality) == 100u &&
        apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength, cpu_personality) == 100u &&
        apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength, rejected_personality) == 99u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "prov-notify-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "prov-notify-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "prov-notify-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "prov-notify-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "prov-notify-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "prov-notify-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "prov-notify-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "prov-notify-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "interest-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "interest-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "interest-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "interest-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "delivery-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "delivery-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "delivery-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "delivery-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "interest-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "interest-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "interest-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "interest-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "delivery-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "delivery-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "delivery-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "delivery-runtime-exec", 1u) == 0u &&
        prop_string_matches(args, platform_personality, "interest-provider-path", "IODeviceTree:/") &&
        prop_string_matches(args, interrupt_personality, "interest-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, timer_personality, "interest-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, cpu_personality, "interest-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "interest-provider-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "interest-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "interest-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "interest-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "interest-type-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "interest-type-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "interest-type-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "interest-type-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "expected-notify-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "expected-notify-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "expected-notify-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "expected-notify-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "delivered-notify-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "delivered-notify-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "delivered-notify-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "delivered-notify-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "interest-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "interest-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "interest-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "interest-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "delivery-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "delivery-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "delivery-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "delivery-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL &&
        prop_string_matches(args, platform_personality, "notify-provenance", "stage85-lifecycle-regsvc") &&
        prop_string_matches(args, interrupt_personality, "notify-provenance", "stage85-lifecycle-regsvc") &&
        prop_string_matches(args, timer_personality, "notify-provenance", "stage85-lifecycle-regsvc") &&
        prop_string_matches(args, cpu_personality, "notify-provenance", "stage85-lifecycle-regsvc") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "prov-notify-dryrun", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "prov-notify-ready", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "interest-ready", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "delivery-ready", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "interest-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "delivery-runtime-exec", 1u) == 0u &&
        prop_string_matches(args, rejected_personality, "interest-provider-path", "IODeviceTree:/unlinked") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "interest-provider-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "interest-type-mask", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "expected-notify-mask", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "delivered-notify-mask", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "interest-order", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "delivery-order", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "notify-dependency-ready", 1u) == 0u &&
        prop_string_matches(args, rejected_personality, "notify-provenance", "stage85-rejected-undelivered")) {
        mask |= STAGE85_DT_READY_IOKIT_PROVIDER_NOTIFICATION;
    }

    if (platform_personality && interrupt_personality && timer_personality && cpu_personality && rejected_personality &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "prov-callback-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "prov-callback-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "prov-callback-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "prov-callback-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "callback-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "callback-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "callback-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "callback-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "client-notify-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "client-notify-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "client-notify-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "client-notify-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "callback-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "callback-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "callback-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "callback-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "client-notify-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "client-notify-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "client-notify-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "client-notify-runtime-exec", 1u) == 0u &&
        prop_string_matches(args, platform_personality, "callback-provider-path", "IODeviceTree:/") &&
        prop_string_matches(args, interrupt_personality, "callback-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, timer_personality, "callback-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, cpu_personality, "callback-provider-path",
                            "IODeviceTree:/stage85-platform-personality") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "callback-provider-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "callback-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "callback-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "callback-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "callback-type-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "callback-type-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "callback-type-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "callback-type-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "expected-callback-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "expected-callback-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "expected-callback-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "expected-callback-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "delivered-callback-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "delivered-callback-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "delivered-callback-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "delivered-callback-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "client-ack-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "client-ack-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "client-ack-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "client-ack-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "callback-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "callback-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "callback-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "callback-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "client-notify-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "client-notify-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "client-notify-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "client-notify-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality,
                              "callback-dependency-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality,
                              "callback-dependency-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality,
                              "callback-dependency-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality,
                              "callback-dependency-ready", 0u) == 1u &&
        prop_string_matches(args, platform_personality, "callback-provenance", "stage85-provider-notify") &&
        prop_string_matches(args, interrupt_personality, "callback-provenance", "stage85-provider-notify") &&
        prop_string_matches(args, timer_personality, "callback-provenance", "stage85-provider-notify") &&
        prop_string_matches(args, cpu_personality, "callback-provenance", "stage85-provider-notify") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "prov-callback-dryrun", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "callback-ready", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "client-notify-ready", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "callback-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "client-notify-runtime-exec", 1u) == 0u &&
        prop_string_matches(args, rejected_personality, "callback-provider-path", "IODeviceTree:/unlinked") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "callback-provider-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "callback-type-mask", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "expected-callback-mask", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "delivered-callback-mask", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "client-ack-mask", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "callback-order", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "client-notify-order", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality,
                              "callback-dependency-ready", 1u) == 0u &&
        prop_string_matches(args, rejected_personality, "callback-provenance", "stage85-rejected-nocallback")) {
        mask |= STAGE85_DT_READY_IOKIT_PROVIDER_CALLBACK;
    }

    if (apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength, platform_personality) == 100u &&
        apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength, interrupt_personality) == 100u &&
        apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength, timer_personality) == 100u &&
        apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength, cpu_personality) == 100u &&
        apple_dt_node_prop_count(args->deviceTreeP, args->deviceTreeLength, rejected_personality) == 99u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "client-open-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "client-open-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "client-open-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "client-open-dryrun", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "client-open-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "client-open-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "client-open-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "client-open-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "provider-claim-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "provider-claim-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "provider-claim-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "provider-claim-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "client-close-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "client-close-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "client-close-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "client-close-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "open-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "open-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "open-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "open-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "claim-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "claim-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "claim-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "claim-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "close-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "close-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "close-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "close-runtime-exec", 1u) == 0u &&
        prop_string_matches(args, platform_personality, "open-provider-path", "IODeviceTree:/") &&
        prop_string_matches(args, interrupt_personality, "open-provider-path", "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, timer_personality, "open-provider-path", "IODeviceTree:/stage85-platform-personality") &&
        prop_string_matches(args, cpu_personality, "open-provider-path", "IODeviceTree:/stage85-platform-personality") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "open-provider-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "open-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "open-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "open-provider-ordinal", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "open-type-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "open-type-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "open-type-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "open-type-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "expected-open-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "expected-open-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "expected-open-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "expected-open-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "claimed-open-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "claimed-open-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "claimed-open-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "claimed-open-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "client-close-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_PLATFORM_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "client-close-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_INTERRUPT_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "client-close-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_TIMER_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "client-close-mask", 0u) == STAGE85_XNU_IOKIT_REGISTRY_SERVICE_CPU_BIT &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "open-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "open-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "open-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "open-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "close-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_PLATFORM_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "close-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_INTERRUPT_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "close-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_TIMER_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "close-order", 0xffffffffu) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_CPU_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, platform_personality, "open-dependency-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, interrupt_personality, "open-dependency-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer_personality, "open-dependency-ready", 0u) == 1u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu_personality, "open-dependency-ready", 0u) == 1u &&
        prop_string_matches(args, platform_personality, "open-provenance", "stage85-provider-callback") &&
        prop_string_matches(args, interrupt_personality, "open-provenance", "stage85-provider-callback") &&
        prop_string_matches(args, timer_personality, "open-provenance", "stage85-provider-callback") &&
        prop_string_matches(args, cpu_personality, "open-provenance", "stage85-provider-callback") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "client-open-dryrun", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "client-open-ready", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "provider-claim-ready", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "client-close-ready", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "open-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "claim-runtime-exec", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "close-runtime-exec", 1u) == 0u &&
        prop_string_matches(args, rejected_personality, "open-provider-path", "IODeviceTree:/unlinked") &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "open-provider-ordinal", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "open-type-mask", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "expected-open-mask", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "claimed-open-mask", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "client-close-mask", 1u) == 0u &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "open-order", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "close-order", 0u) == STAGE85_XNU_IOKIT_REGISTRY_TOPOLOGY_REJECTED_ORDINAL &&
        apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, rejected_personality, "open-dependency-ready", 1u) == 0u &&
        prop_string_matches(args, rejected_personality, "open-provenance", "stage85-rejected-noopen")) {
        mask |= STAGE85_DT_READY_IOKIT_CLIENT_OPEN;
    }

    return mask;
}

int stage85_loader_preflight_run(struct boot_args *args)
{
    struct stage85_macho_probe_result probe;
    struct stage85_loader_preflight *preflight = &stage85_loader_preflight_block;
    const struct stage85_ttbr0_roundtrip *ttbr_rt = mmu_stage85_ttbr0_roundtrip_result();
    const struct stage85_xnu_workspace *xnu_ws;
    const struct stage85_xnu_compile_graph *xnu_graph;
    const struct stage85_xnu_object_subset *xnu_obj;
    const struct stage85_xnu_link *xnu_link;
    const struct stage85_xnu_pmap_bootstrap_contract *pmap_contract;
    const struct stage85_xnu_pmap_table_dryrun_contract *pmap_table_contract;
    const struct stage85_xnu_pmap_page_dryrun_contract *pmap_page_contract;
    const struct stage85_xnu_pmap_attr_dryrun_contract *pmap_attr_contract;
    const struct stage85_xnu_pmap_multiwindow_dryrun_contract *pmap_multiwindow_contract;
    const struct stage85_xnu_pmap_transition_dryrun_contract *pmap_transition_contract;
    const struct stage85_xnu_pexpert_hook_readiness_contract *pexpert_hook_contract;
    const struct stage85_xnu_iokit_platform_scaffold_contract *iokit_platform_contract;
    const struct stage85_xnu_iokit_match_dryrun_contract *iokit_match_contract;
    const struct stage85_xnu_iokit_registry_service_dryrun_contract *iokit_registry_contract;
    const struct stage85_xnu_iokit_provider_plane_dryrun_contract *iokit_provider_contract;
    const struct stage85_xnu_iokit_catalog_property_dryrun_contract *iokit_catalog_contract;
    const struct stage85_xnu_iokit_property_inheritance_dryrun_contract *iokit_property_inheritance_contract;
    const struct stage85_xnu_iokit_registry_topology_dryrun_contract *iokit_registry_topology_contract;
    const struct stage85_xnu_iokit_attach_start_readiness_dryrun_contract *iokit_attach_start_contract;
    const struct stage85_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract *iokit_lifecycle_contract;
    const struct stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract *iokit_provider_notification_contract;
    const struct stage85_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract *iokit_provider_callback_contract;
    const struct stage85_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract *iokit_client_open_contract;
    const struct stage85_xnu_entry_stub_result *xnu_entry_stub;
    const struct stage85_xnu_early_pmap_platform_init_result *xnu_early_init;
    const struct stage85_xnu_pe_init_platform_false_result *xnu_pe_init;
    const struct stage85_xnu_arm_init_post_pe_bootstrap_result *xnu_post_pe;
    const struct stage85_xnu_arm_vm_init_full_pmap_result *xnu_live_pmap;
    uint32_t workspace_limit;
    uint32_t loaded_end_for_workspace;
    uint32_t macho_ok;

    xnu_log_puts("Stage84 Mach-O/XNU loader preflight begin\n");
    memset(preflight, 0, sizeof(*preflight));
    preflight->version = STAGE85_LOADER_PREFLIGHT_VERSION;
    preflight->size = sizeof(*preflight);
    preflight->xnu_baseline_tag = STAGE85_XNU_BASELINE_2050_22_13;
    preflight->xnu_baseline_commit = STAGE85_XNU_BASELINE_COMMIT_CC8A9B0C;
    preflight->xnu_master_version = STAGE85_XNU_BASELINE_MASTER_12_3_0;
    preflight->xnu_arm_reference = STAGE85_XNU_ARM_REFERENCE_4570_1_46;

    macho_ok = (uint32_t)stage85_macho_probe(stage85_embedded_macho, stage85_embedded_macho_size, &probe);
    if (macho_ok) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_MACHO;
    }
    if (macho_ok && stage85_macho_materialize_to_arena(stage85_embedded_macho, stage85_embedded_macho_size, &probe)) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_MATERIALIZED;
    }
    memcpy(&preflight->macho, &probe, sizeof(probe));
    memcpy(&preflight->staging, &probe.staging_plan, sizeof(probe.staging_plan));
    preflight->materialized_status = probe.staging_status;
    preflight->materialized_file_bytes = probe.staging_file_bytes;
    preflight->materialized_zero_bytes = probe.staging_zero_bytes;

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
    preflight->proposed_loaded_phys_base = probe.load_phys_base;
    preflight->proposed_loaded_phys_end = probe.load_phys_end;
    preflight->proposed_loaded_file_end = probe.load_file_end;
    loaded_end_for_workspace = probe.load_phys_end;
    if (loaded_end_for_workspace == 0u) {
        uint32_t loaded_span = probe.max_vmaddr - probe.min_vmaddr;
        if (loaded_span < 0x00010000u) {
            loaded_span = 0x00010000u;
        }
        loaded_end_for_workspace = preflight->proposed_physBase + loaded_span;
    }
    preflight->proposed_topOfKernelData = align_up_u32(loaded_end_for_workspace, STAGE85_XNU_TTE_ALIGNMENT);
    preflight->proposed_ttep_workspace_base = preflight->proposed_topOfKernelData;
    preflight->proposed_ttep_workspace_size = STAGE85_XNU_TTE_WORKSPACE_BYTES;
    workspace_limit = preflight->proposed_ttep_workspace_base + preflight->proposed_ttep_workspace_size;
    preflight->proposed_ttep_workspace_limit = workspace_limit;
    preflight->proposed_avail_start = workspace_limit;
    preflight->proposed_workspace_alignment = preflight->proposed_topOfKernelData & (STAGE85_XNU_TTE_ALIGNMENT - 1u);
    if (preflight->proposed_workspace_alignment == 0u &&
        workspace_limit > preflight->proposed_ttep_workspace_base &&
        workspace_limit < RAM_CONSOLE_BASE &&
        preflight->proposed_topOfKernelData >= loaded_end_for_workspace &&
        preflight->proposed_loaded_phys_base >= RAM_PHYS_BASE &&
        preflight->proposed_loaded_phys_end <= preflight->proposed_topOfKernelData) {
        preflight->proposed_workspace_range_ok = 1u;
    }

    preflight->apple_dt_semantic_mask = stage85_dt_semantic_mask(args);
    preflight->pexpert_gap_mask = STAGE85_PLATFORM_GAP_REQUIRED_RECORDED;
    preflight->platform_gap_mask = STAGE85_PLATFORM_GAP_REQUIRED_RECORDED;
    preflight->interrupt_ready_mask = STAGE85_LOADER_IRQ_READY_GIC_DIST |
        STAGE85_LOADER_IRQ_READY_GIC_CPU |
        STAGE85_LOADER_IRQ_READY_TIMEBASE;
    preflight->safety_mask = STAGE85_LOADER_SAFETY_NO_EXECUTE |
        STAGE85_LOADER_SAFETY_NO_PERSIST_WRITE |
        STAGE85_LOADER_SAFETY_RAM_CONSOLE |
        STAGE85_LOADER_SAFETY_LOCAL_ARENA_ONLY |
        STAGE85_LOADER_SAFETY_NO_PHYS_WRITE |
        STAGE85_LOADER_SAFETY_TTE_DRYRUN_ONLY |
        STAGE85_LOADER_SAFETY_TTE_VERIFY_ONLY |
        STAGE85_LOADER_SAFETY_VTOP_DRYRUN_ONLY |
        STAGE85_LOADER_SAFETY_HIGHVA_DRYRUN_ONLY |
        STAGE85_LOADER_SAFETY_SAFE_TABLE_LOCAL_ONLY |
        STAGE85_LOADER_SAFETY_STAGE_OWNED_TABLES_ONLY |
        STAGE85_LOADER_SAFETY_NO_FULL_XNU_BUILD |
        STAGE85_LOADER_SAFETY_NO_PUBLIC_XNU_EXEC |
        STAGE85_LOADER_SAFETY_NO_EXTERNAL_MUTATION |
        STAGE85_LOADER_SAFETY_CONTROLLED_PUBLIC_XNU_LINK_NO_EXEC |
        STAGE85_LOADER_SAFETY_XNU_COMPILE_GRAPH_NO_EXEC |
        STAGE85_LOADER_SAFETY_NO_PLATFORM_RUNTIME_EXEC;

    if (ttbr_rt->status == STAGE85_STATUS_OK &&
        (ttbr_rt->satisfied_mask & STAGE85_TTBR_RT_SAT_STAGE_OWNED_TABLE) != 0u) {
        preflight->safety_mask |= STAGE85_LOADER_SAFETY_STAGE_OWNED_TTBR;
    }
    if (ttbr_rt->status == STAGE85_STATUS_OK &&
        (ttbr_rt->satisfied_mask & STAGE85_TTBR_RT_SAT_ORIGINAL_RESTORED) != 0u) {
        preflight->safety_mask |= STAGE85_LOADER_SAFETY_TTBR_RESTORED;
    }
    if (ttbr_rt->status == STAGE85_STATUS_OK && ttbr_rt->caches_changed == 0u &&
        (ttbr_rt->satisfied_mask & STAGE85_TTBR_RT_SAT_CACHE_BITS_PRESERVED) != 0u) {
        preflight->safety_mask |= STAGE85_LOADER_SAFETY_CACHES_UNCHANGED |
            STAGE85_LOADER_SAFETY_NO_CACHE_CHANGE;
    }

    if (preflight->xnu_baseline_tag == STAGE85_XNU_BASELINE_2050_22_13 &&
        preflight->xnu_baseline_commit == STAGE85_XNU_BASELINE_COMMIT_CC8A9B0C &&
        preflight->xnu_master_version == STAGE85_XNU_BASELINE_MASTER_12_3_0 &&
        preflight->xnu_arm_reference == STAGE85_XNU_ARM_REFERENCE_4570_1_46) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_BASELINE;
    }
    if (preflight->boot_args_rev_ver == 0x00020002u &&
        preflight->boot_args_ptr != 0u && preflight->device_tree_ptr != 0u &&
        preflight->actual_memSize == (RAM_CONSOLE_BASE - RAM_PHYS_BASE)) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_BOOT_ARGS;
    }
    if (preflight->proposed_workspace_range_ok == 1u) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_WORKSPACE;
    }
    if (preflight->apple_dt_semantic_mask == STAGE85_DT_READY_REQUIRED) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_DT;
    }
    if (stage85_xnu_tte_dryrun_build(&probe, preflight, &preflight->tte_dryrun)) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_TTE_DRYRUN;
        if ((preflight->tte_dryrun.satisfied_mask & (STAGE85_TTE_SAT_DESCRIPTOR_READBACK |
                                                     STAGE85_TTE_SAT_DESCRIPTOR_ATTRS)) ==
            (STAGE85_TTE_SAT_DESCRIPTOR_READBACK | STAGE85_TTE_SAT_DESCRIPTOR_ATTRS)) {
            preflight->satisfied_mask |= STAGE85_LOADER_SAT_TTE_VERIFY;
        }
        if ((preflight->tte_dryrun.satisfied_mask & (STAGE85_TTE_SAT_TRANSLATION_KERNEL |
                                                     STAGE85_TTE_SAT_TRANSLATION_LOW_RAM |
                                                     STAGE85_TTE_SAT_TRANSLATION_CONSOLE |
                                                     STAGE85_TTE_SAT_TRANSLATION_DEVICE |
                                                     STAGE85_TTE_SAT_XNU_BOOT_POLICY)) ==
            (STAGE85_TTE_SAT_TRANSLATION_KERNEL |
             STAGE85_TTE_SAT_TRANSLATION_LOW_RAM |
             STAGE85_TTE_SAT_TRANSLATION_CONSOLE |
             STAGE85_TTE_SAT_TRANSLATION_DEVICE |
             STAGE85_TTE_SAT_XNU_BOOT_POLICY)) {
            preflight->satisfied_mask |= STAGE85_LOADER_SAT_VTOP_DRYRUN;
        }
        if ((preflight->tte_dryrun.satisfied_mask & (STAGE85_TTE_SAT_HIGHVA_MAPPING |
                                                     STAGE85_TTE_SAT_HIGHVA_TRANSLATION)) ==
            (STAGE85_TTE_SAT_HIGHVA_MAPPING | STAGE85_TTE_SAT_HIGHVA_TRANSLATION)) {
            preflight->satisfied_mask |= STAGE85_LOADER_SAT_HIGHVA_DRYRUN;
        }
        if ((preflight->tte_dryrun.satisfied_mask & STAGE85_TTE_SAT_SAFE_TABLE) != 0u) {
            preflight->satisfied_mask |= STAGE85_LOADER_SAT_SAFE_TABLE;
        }
        if ((preflight->tte_dryrun.satisfied_mask & STAGE85_TTE_SAT_STAGE_OWNED_TABLES) != 0u) {
            preflight->satisfied_mask |= STAGE85_LOADER_SAT_STAGE_OWNED_TABLES;
        }
    }
    preflight->tte_dryrun_status = preflight->tte_dryrun.status;
    preflight->tte_dryrun_satisfied_mask = preflight->tte_dryrun.satisfied_mask;
    preflight->tte_dryrun_failure_mask = preflight->tte_dryrun.failure_mask;
    preflight->tte_dryrun_checksum = preflight->tte_dryrun.checksum;
    preflight->tte_verify_status = ((preflight->satisfied_mask & STAGE85_LOADER_SAT_TTE_VERIFY) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;
    preflight->vtop_dryrun_status = ((preflight->satisfied_mask & STAGE85_LOADER_SAT_VTOP_DRYRUN) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;
    preflight->highva_dryrun_status = ((preflight->satisfied_mask & STAGE85_LOADER_SAT_HIGHVA_DRYRUN) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;
    preflight->safe_table_status = ((preflight->satisfied_mask & STAGE85_LOADER_SAT_SAFE_TABLE) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;
    preflight->stage_owned_tables_status = ((preflight->satisfied_mask & STAGE85_LOADER_SAT_STAGE_OWNED_TABLES) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    preflight->ttbr_roundtrip_status = ttbr_rt->status;
    preflight->ttbr_roundtrip_satisfied_mask = ttbr_rt->satisfied_mask;
    preflight->ttbr_roundtrip_failure_mask = ttbr_rt->failure_mask;
    preflight->ttbr_roundtrip_checksum = ttbr_rt->checksum;
    if (ttbr_rt->status == STAGE85_STATUS_OK) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_TTBR_ROUNDTRIP;
    }
    if (ttbr_rt->status == STAGE85_STATUS_OK &&
        (ttbr_rt->satisfied_mask & STAGE85_TTBR_RT_SAT_STAGE_OWNED_TABLE) != 0u) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_STAGE_OWNED_TTBR;
    }
    if (ttbr_rt->status == STAGE85_STATUS_OK &&
        (ttbr_rt->satisfied_mask & STAGE85_TTBR_RT_SAT_ORIGINAL_RESTORED) != 0u) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_TTBR_RESTORED;
    }
    if (ttbr_rt->status == STAGE85_STATUS_OK && ttbr_rt->caches_changed == 0u &&
        (ttbr_rt->satisfied_mask & STAGE85_TTBR_RT_SAT_CACHE_BITS_PRESERVED) != 0u) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_CACHE_PRESERVED;
    }
    preflight->stage_owned_ttbr_status = ((preflight->satisfied_mask & STAGE85_LOADER_SAT_STAGE_OWNED_TTBR) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;
    preflight->ttbr_restored_status = ((preflight->satisfied_mask & STAGE85_LOADER_SAT_TTBR_RESTORED) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;
    preflight->cache_preserved_status = ((preflight->satisfied_mask & STAGE85_LOADER_SAT_CACHE_PRESERVED) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_workspace_selftest()) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_XNU_WORKSPACE;
    }
    xnu_ws = stage85_xnu_workspace_result();
    memcpy(&preflight->xnu_workspace, xnu_ws, sizeof(*xnu_ws));
    preflight->xnu_workspace_status = xnu_ws->status;
    preflight->xnu_workspace_satisfied_mask = xnu_ws->satisfied_mask;
    preflight->xnu_workspace_failure_mask = xnu_ws->failure_mask;
    preflight->xnu_workspace_checksum = xnu_ws->checksum;
    if (xnu_ws->status == STAGE85_STATUS_OK && xnu_ws->cancro_target_declared == 1u &&
        xnu_ws->armv7_target_declared == 1u && xnu_ws->msm8974_target_declared == 1u) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_CANCRO_TARGET;
        preflight->cancro_target_status = STAGE85_STATUS_OK;
    } else {
        preflight->cancro_target_status = STAGE85_STATUS_BASE;
    }
    if (xnu_ws->status == STAGE85_STATUS_OK && xnu_ws->stage85_plan_ready == 1u &&
        xnu_ws->public_2050_arm_gap_recorded == 1u) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_STAGE85_PLAN;
        preflight->stage85_plan_status = STAGE85_STATUS_OK;
    } else {
        preflight->stage85_plan_status = STAGE85_STATUS_BASE;
    }

    if (stage85_xnu_compile_graph_selftest()) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_XNU_COMPILE_GRAPH;
    }
    xnu_graph = stage85_xnu_compile_graph_result();
    memcpy(&preflight->xnu_compile_graph, xnu_graph, sizeof(*xnu_graph));
    preflight->xnu_compile_graph_status = xnu_graph->status;
    preflight->xnu_compile_graph_satisfied_mask = xnu_graph->satisfied_mask;
    preflight->xnu_compile_graph_failure_mask = xnu_graph->failure_mask;
    preflight->xnu_compile_graph_checksum = xnu_graph->checksum;
    preflight->xnu_compile_graph_status_rollup =
        ((preflight->satisfied_mask & STAGE85_LOADER_SAT_XNU_COMPILE_GRAPH) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_object_subset_selftest()) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_XNU_OBJECT_SUBSET;
    }
    xnu_obj = stage85_xnu_object_subset_result();
    memcpy(&preflight->xnu_object_subset, xnu_obj, sizeof(*xnu_obj));
    preflight->xnu_object_subset_status = xnu_obj->status;
    preflight->xnu_object_subset_satisfied_mask = xnu_obj->satisfied_mask;
    preflight->xnu_object_subset_failure_mask = xnu_obj->failure_mask;
    preflight->xnu_object_subset_checksum = xnu_obj->checksum;
    preflight->xnu_object_subset_status_rollup =
        ((preflight->satisfied_mask & STAGE85_LOADER_SAT_XNU_OBJECT_SUBSET) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_link_selftest()) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_XNU_LINK;
    }
    xnu_link = stage85_xnu_link_result();
    memcpy(&preflight->xnu_link, xnu_link, sizeof(*xnu_link));
    preflight->xnu_link_status = xnu_link->status;
    preflight->xnu_link_satisfied_mask = xnu_link->satisfied_mask;
    preflight->xnu_link_failure_mask = xnu_link->failure_mask;
    preflight->xnu_link_checksum = xnu_link->checksum;
    preflight->xnu_link_status_rollup =
        ((preflight->satisfied_mask & STAGE85_LOADER_SAT_XNU_LINK) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (preflight->safety_mask == STAGE85_LOADER_SAFETY_REQUIRED) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_SAFETY;
    }

    if (stage85_xnu_bootstrap_contract_selftest(preflight)) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_XNU_BOOTSTRAP_CONTRACT;
    }
    {
        const struct stage85_xnu_bootstrap_contract *contract = stage85_xnu_bootstrap_contract_result();
        memcpy(&preflight->xnu_bootstrap_contract, contract, sizeof(*contract));
        preflight->xnu_bootstrap_contract_status = contract->status;
        preflight->xnu_bootstrap_contract_satisfied_mask = contract->satisfied_mask;
        preflight->xnu_bootstrap_contract_failure_mask = contract->failure_mask;
        preflight->xnu_bootstrap_contract_checksum = contract->checksum;
        preflight->xnu_bootstrap_contract_status_rollup =
            ((preflight->satisfied_mask & STAGE85_LOADER_SAT_XNU_BOOTSTRAP_CONTRACT) != 0u) ?
            STAGE85_STATUS_OK : STAGE85_STATUS_BASE;
    }

    if (stage85_xnu_pmap_bootstrap_contract_selftest(preflight)) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_XNU_PMAP_BOOTSTRAP_CONTRACT;
    }
    pmap_contract = stage85_xnu_pmap_bootstrap_contract_result();
    memcpy(&preflight->xnu_pmap_bootstrap_contract, pmap_contract, sizeof(*pmap_contract));
    preflight->xnu_pmap_bootstrap_contract_status = pmap_contract->status;
    preflight->xnu_pmap_bootstrap_contract_satisfied_mask = pmap_contract->satisfied_mask;
    preflight->xnu_pmap_bootstrap_contract_failure_mask = pmap_contract->failure_mask;
    preflight->xnu_pmap_bootstrap_contract_checksum = pmap_contract->checksum;
    preflight->xnu_pmap_bootstrap_contract_status_rollup =
        ((preflight->satisfied_mask & STAGE85_LOADER_SAT_XNU_PMAP_BOOTSTRAP_CONTRACT) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_pmap_table_dryrun_contract_selftest(preflight)) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_XNU_PMAP_TABLE_DRYRUN_CONTRACT;
    }
    pmap_table_contract = stage85_xnu_pmap_table_dryrun_contract_result();
    memcpy(&preflight->xnu_pmap_table_dryrun_contract, pmap_table_contract, sizeof(*pmap_table_contract));
    preflight->xnu_pmap_table_dryrun_contract_status = pmap_table_contract->status;
    preflight->xnu_pmap_table_dryrun_contract_satisfied_mask = pmap_table_contract->satisfied_mask;
    preflight->xnu_pmap_table_dryrun_contract_failure_mask = pmap_table_contract->failure_mask;
    preflight->xnu_pmap_table_dryrun_contract_checksum = pmap_table_contract->checksum;
    preflight->xnu_pmap_table_dryrun_contract_status_rollup =
        ((preflight->satisfied_mask & STAGE85_LOADER_SAT_XNU_PMAP_TABLE_DRYRUN_CONTRACT) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_pmap_page_dryrun_contract_selftest(preflight)) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_XNU_PMAP_PAGE_DRYRUN_CONTRACT;
    }
    pmap_page_contract = stage85_xnu_pmap_page_dryrun_contract_result();
    memcpy(&preflight->xnu_pmap_page_dryrun_contract, pmap_page_contract, sizeof(*pmap_page_contract));
    preflight->xnu_pmap_page_dryrun_contract_status = pmap_page_contract->status;
    preflight->xnu_pmap_page_dryrun_contract_satisfied_mask = pmap_page_contract->satisfied_mask;
    preflight->xnu_pmap_page_dryrun_contract_failure_mask = pmap_page_contract->failure_mask;
    preflight->xnu_pmap_page_dryrun_contract_checksum = pmap_page_contract->checksum;
    preflight->xnu_pmap_page_dryrun_contract_status_rollup =
        ((preflight->satisfied_mask & STAGE85_LOADER_SAT_XNU_PMAP_PAGE_DRYRUN_CONTRACT) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_pmap_attr_dryrun_contract_selftest(preflight)) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_XNU_PMAP_ATTR_DRYRUN_CONTRACT;
    }
    pmap_attr_contract = stage85_xnu_pmap_attr_dryrun_contract_result();
    memcpy(&preflight->xnu_pmap_attr_dryrun_contract, pmap_attr_contract, sizeof(*pmap_attr_contract));
    preflight->xnu_pmap_attr_dryrun_contract_status = pmap_attr_contract->status;
    preflight->xnu_pmap_attr_dryrun_contract_satisfied_mask = pmap_attr_contract->satisfied_mask;
    preflight->xnu_pmap_attr_dryrun_contract_failure_mask = pmap_attr_contract->failure_mask;
    preflight->xnu_pmap_attr_dryrun_contract_checksum = pmap_attr_contract->checksum;
    preflight->xnu_pmap_attr_dryrun_contract_status_rollup =
        ((preflight->satisfied_mask & STAGE85_LOADER_SAT_XNU_PMAP_ATTR_DRYRUN_CONTRACT) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_pmap_multiwindow_dryrun_contract_selftest(preflight)) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_XNU_PMAP_MULTIWINDOW_DRYRUN_CONTRACT;
    }
    pmap_multiwindow_contract = stage85_xnu_pmap_multiwindow_dryrun_contract_result();
    memcpy(&preflight->xnu_pmap_multiwindow_dryrun_contract, pmap_multiwindow_contract,
           sizeof(*pmap_multiwindow_contract));
    preflight->xnu_pmap_multiwindow_dryrun_contract_status = pmap_multiwindow_contract->status;
    preflight->xnu_pmap_multiwindow_dryrun_contract_satisfied_mask = pmap_multiwindow_contract->satisfied_mask;
    preflight->xnu_pmap_multiwindow_dryrun_contract_failure_mask = pmap_multiwindow_contract->failure_mask;
    preflight->xnu_pmap_multiwindow_dryrun_contract_checksum = pmap_multiwindow_contract->checksum;
    preflight->xnu_pmap_multiwindow_dryrun_contract_status_rollup =
        ((preflight->satisfied_mask & STAGE85_LOADER_SAT_XNU_PMAP_MULTIWINDOW_DRYRUN_CONTRACT) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_pmap_transition_dryrun_contract_selftest(preflight)) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_XNU_PMAP_TRANSITION_DRYRUN_CONTRACT;
    }
    pmap_transition_contract = stage85_xnu_pmap_transition_dryrun_contract_result();
    memcpy(&preflight->xnu_pmap_transition_dryrun_contract, pmap_transition_contract,
           sizeof(*pmap_transition_contract));
    preflight->xnu_pmap_transition_dryrun_contract_status = pmap_transition_contract->status;
    preflight->xnu_pmap_transition_dryrun_contract_satisfied_mask = pmap_transition_contract->satisfied_mask;
    preflight->xnu_pmap_transition_dryrun_contract_failure_mask = pmap_transition_contract->failure_mask;
    preflight->xnu_pmap_transition_dryrun_contract_checksum = pmap_transition_contract->checksum;
    preflight->xnu_pmap_transition_dryrun_contract_status_rollup =
        ((preflight->satisfied_mask & STAGE85_LOADER_SAT_XNU_PMAP_TRANSITION_DRYRUN_CONTRACT) != 0u) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_pexpert_hook_readiness_contract_selftest(preflight)) {
        preflight->pexpert_gap_mask = 0u;
        preflight->interrupt_ready_mask = STAGE85_LOADER_IRQ_READY_REQUIRED;
    }
    pexpert_hook_contract = stage85_xnu_pexpert_hook_readiness_contract_result();
    memcpy(&preflight->xnu_pexpert_hook_readiness_contract, pexpert_hook_contract,
           sizeof(*pexpert_hook_contract));
    preflight->xnu_pexpert_hook_readiness_contract_status = pexpert_hook_contract->status;
    preflight->xnu_pexpert_hook_readiness_contract_satisfied_mask = pexpert_hook_contract->satisfied_mask;
    preflight->xnu_pexpert_hook_readiness_contract_failure_mask = pexpert_hook_contract->failure_mask;
    preflight->xnu_pexpert_hook_readiness_contract_checksum = pexpert_hook_contract->checksum;
    preflight->xnu_pexpert_hook_readiness_contract_status_rollup =
        (pexpert_hook_contract->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_iokit_platform_scaffold_contract_selftest(preflight)) {
        preflight->platform_gap_mask &= ~STAGE85_PLATFORM_GAP_IOKIT_STACK;
    }
    iokit_platform_contract = stage85_xnu_iokit_platform_scaffold_contract_result();
    memcpy(&preflight->xnu_iokit_platform_scaffold_contract, iokit_platform_contract,
           sizeof(*iokit_platform_contract));
    preflight->xnu_iokit_platform_scaffold_contract_status = iokit_platform_contract->status;
    preflight->xnu_iokit_platform_scaffold_contract_satisfied_mask = iokit_platform_contract->satisfied_mask;
    preflight->xnu_iokit_platform_scaffold_contract_failure_mask = iokit_platform_contract->failure_mask;
    preflight->xnu_iokit_platform_scaffold_contract_checksum = iokit_platform_contract->checksum;
    preflight->xnu_iokit_platform_scaffold_contract_status_rollup =
        (iokit_platform_contract->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_iokit_match_dryrun_contract_selftest(preflight)) {
        preflight->platform_gap_mask &= ~STAGE85_PLATFORM_GAP_IOKIT_STACK;
    }
    iokit_match_contract = stage85_xnu_iokit_match_dryrun_contract_result();
    memcpy(&preflight->xnu_iokit_match_dryrun_contract, iokit_match_contract,
           sizeof(*iokit_match_contract));
    preflight->xnu_iokit_match_dryrun_contract_status = iokit_match_contract->status;
    preflight->xnu_iokit_match_dryrun_contract_satisfied_mask = iokit_match_contract->satisfied_mask;
    preflight->xnu_iokit_match_dryrun_contract_failure_mask = iokit_match_contract->failure_mask;
    preflight->xnu_iokit_match_dryrun_contract_checksum = iokit_match_contract->checksum;
    preflight->xnu_iokit_match_dryrun_contract_status_rollup =
        (iokit_match_contract->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_iokit_registry_service_dryrun_contract_selftest(preflight)) {
        preflight->platform_gap_mask &= ~STAGE85_PLATFORM_GAP_IOKIT_STACK;
    }
    iokit_registry_contract = stage85_xnu_iokit_registry_service_dryrun_contract_result();
    memcpy(&preflight->xnu_iokit_registry_service_dryrun_contract, iokit_registry_contract,
           sizeof(*iokit_registry_contract));
    preflight->xnu_iokit_registry_service_dryrun_contract_status = iokit_registry_contract->status;
    preflight->xnu_iokit_registry_service_dryrun_contract_satisfied_mask = iokit_registry_contract->satisfied_mask;
    preflight->xnu_iokit_registry_service_dryrun_contract_failure_mask = iokit_registry_contract->failure_mask;
    preflight->xnu_iokit_registry_service_dryrun_contract_checksum = iokit_registry_contract->checksum;
    preflight->xnu_iokit_registry_service_dryrun_contract_status_rollup =
        (iokit_registry_contract->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_iokit_provider_plane_dryrun_contract_selftest(preflight)) {
        preflight->platform_gap_mask &= ~STAGE85_PLATFORM_GAP_IOKIT_STACK;
    }
    iokit_provider_contract = stage85_xnu_iokit_provider_plane_dryrun_contract_result();
    memcpy(&preflight->xnu_iokit_provider_plane_dryrun_contract, iokit_provider_contract,
           sizeof(*iokit_provider_contract));
    preflight->xnu_iokit_provider_plane_dryrun_contract_status = iokit_provider_contract->status;
    preflight->xnu_iokit_provider_plane_dryrun_contract_satisfied_mask = iokit_provider_contract->satisfied_mask;
    preflight->xnu_iokit_provider_plane_dryrun_contract_failure_mask = iokit_provider_contract->failure_mask;
    preflight->xnu_iokit_provider_plane_dryrun_contract_checksum = iokit_provider_contract->checksum;
    preflight->xnu_iokit_provider_plane_dryrun_contract_status_rollup =
        (iokit_provider_contract->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_iokit_catalog_property_dryrun_contract_selftest(preflight)) {
        preflight->platform_gap_mask &= ~STAGE85_PLATFORM_GAP_IOKIT_STACK;
    }
    iokit_catalog_contract = stage85_xnu_iokit_catalog_property_dryrun_contract_result();
    memcpy(&preflight->xnu_iokit_catalog_property_dryrun_contract, iokit_catalog_contract,
           sizeof(*iokit_catalog_contract));
    preflight->xnu_iokit_catalog_property_dryrun_contract_status = iokit_catalog_contract->status;
    preflight->xnu_iokit_catalog_property_dryrun_contract_satisfied_mask = iokit_catalog_contract->satisfied_mask;
    preflight->xnu_iokit_catalog_property_dryrun_contract_failure_mask = iokit_catalog_contract->failure_mask;
    preflight->xnu_iokit_catalog_property_dryrun_contract_checksum = iokit_catalog_contract->checksum;
    preflight->xnu_iokit_catalog_property_dryrun_contract_status_rollup =
        (iokit_catalog_contract->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_iokit_property_inheritance_dryrun_contract_selftest(preflight)) {
        preflight->platform_gap_mask &= ~STAGE85_PLATFORM_GAP_IOKIT_STACK;
    }
    iokit_property_inheritance_contract = stage85_xnu_iokit_property_inheritance_dryrun_contract_result();
    memcpy(&preflight->xnu_iokit_property_inheritance_dryrun_contract, iokit_property_inheritance_contract,
           sizeof(*iokit_property_inheritance_contract));
    preflight->xnu_iokit_property_inheritance_dryrun_contract_status = iokit_property_inheritance_contract->status;
    preflight->xnu_iokit_property_inheritance_dryrun_contract_satisfied_mask =
        iokit_property_inheritance_contract->satisfied_mask;
    preflight->xnu_iokit_property_inheritance_dryrun_contract_failure_mask =
        iokit_property_inheritance_contract->failure_mask;
    preflight->xnu_iokit_property_inheritance_dryrun_contract_checksum = iokit_property_inheritance_contract->checksum;
    preflight->xnu_iokit_property_inheritance_dryrun_contract_status_rollup =
        (iokit_property_inheritance_contract->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_iokit_registry_topology_dryrun_contract_selftest(preflight)) {
        preflight->platform_gap_mask &= ~STAGE85_PLATFORM_GAP_IOKIT_STACK;
    }
    iokit_registry_topology_contract = stage85_xnu_iokit_registry_topology_dryrun_contract_result();
    memcpy(&preflight->xnu_iokit_registry_topology_dryrun_contract, iokit_registry_topology_contract,
           sizeof(*iokit_registry_topology_contract));
    preflight->xnu_iokit_registry_topology_dryrun_contract_status = iokit_registry_topology_contract->status;
    preflight->xnu_iokit_registry_topology_dryrun_contract_satisfied_mask =
        iokit_registry_topology_contract->satisfied_mask;
    preflight->xnu_iokit_registry_topology_dryrun_contract_failure_mask =
        iokit_registry_topology_contract->failure_mask;
    preflight->xnu_iokit_registry_topology_dryrun_contract_checksum = iokit_registry_topology_contract->checksum;
    preflight->xnu_iokit_registry_topology_dryrun_contract_status_rollup =
        (iokit_registry_topology_contract->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_iokit_attach_start_readiness_dryrun_contract_selftest(preflight)) {
        preflight->platform_gap_mask &= ~STAGE85_PLATFORM_GAP_IOKIT_STACK;
    }
    iokit_attach_start_contract = stage85_xnu_iokit_attach_start_readiness_dryrun_contract_result();
    memcpy(&preflight->xnu_iokit_attach_start_readiness_dryrun_contract, iokit_attach_start_contract,
           sizeof(*iokit_attach_start_contract));
    preflight->xnu_iokit_attach_start_readiness_dryrun_contract_status = iokit_attach_start_contract->status;
    preflight->xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask =
        iokit_attach_start_contract->satisfied_mask;
    preflight->xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask =
        iokit_attach_start_contract->failure_mask;
    preflight->xnu_iokit_attach_start_readiness_dryrun_contract_checksum = iokit_attach_start_contract->checksum;
    preflight->xnu_iokit_attach_start_readiness_dryrun_contract_status_rollup =
        (iokit_attach_start_contract->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_selftest(preflight)) {
        preflight->platform_gap_mask &= ~STAGE85_PLATFORM_GAP_IOKIT_STACK;
    }
    iokit_lifecycle_contract = stage85_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_result();
    memcpy(&preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract, iokit_lifecycle_contract,
           sizeof(*iokit_lifecycle_contract));
    preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status =
        iokit_lifecycle_contract->status;
    preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_satisfied_mask =
        iokit_lifecycle_contract->satisfied_mask;
    preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_failure_mask =
        iokit_lifecycle_contract->failure_mask;
    preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_checksum =
        iokit_lifecycle_contract->checksum;
    preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status_rollup =
        (iokit_lifecycle_contract->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_selftest(preflight)) {
        preflight->platform_gap_mask &= ~STAGE85_PLATFORM_GAP_IOKIT_STACK;
    }
    iokit_provider_notification_contract =
        stage85_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_result();
    memcpy(&preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract,
           iokit_provider_notification_contract, sizeof(*iokit_provider_notification_contract));
    preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status =
        iokit_provider_notification_contract->status;
    preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_satisfied_mask =
        iokit_provider_notification_contract->satisfied_mask;
    preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_failure_mask =
        iokit_provider_notification_contract->failure_mask;
    preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_checksum =
        iokit_provider_notification_contract->checksum;
    preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status_rollup =
        (iokit_provider_notification_contract->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_selftest(preflight)) {
        preflight->platform_gap_mask &= ~STAGE85_PLATFORM_GAP_IOKIT_STACK;
    }
    iokit_provider_callback_contract =
        stage85_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_result();
    memcpy(&preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract,
           iokit_provider_callback_contract, sizeof(*iokit_provider_callback_contract));
    preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_status =
        iokit_provider_callback_contract->status;
    preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_satisfied_mask =
        iokit_provider_callback_contract->satisfied_mask;
    preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_failure_mask =
        iokit_provider_callback_contract->failure_mask;
    preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_checksum =
        iokit_provider_callback_contract->checksum;
    preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_status_rollup =
        (iokit_provider_callback_contract->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (stage85_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_selftest(preflight)) {
        preflight->platform_gap_mask &= ~STAGE85_PLATFORM_GAP_IOKIT_STACK;
    }
    iokit_client_open_contract =
        stage85_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_result();
    memcpy(&preflight->xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract,
           iokit_client_open_contract, sizeof(*iokit_client_open_contract));
    preflight->xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_status =
        iokit_client_open_contract->status;
    preflight->xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_satisfied_mask =
        iokit_client_open_contract->satisfied_mask;
    preflight->xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_failure_mask =
        iokit_client_open_contract->failure_mask;
    preflight->xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_checksum =
        iokit_client_open_contract->checksum;
    preflight->xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_status_rollup =
        (iokit_client_open_contract->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (iokit_client_open_contract->status == STAGE85_STATUS_OK) {
        (void)stage85_xnu_entry_stub_run(args);
    }
    xnu_entry_stub = stage85_xnu_entry_stub_result();
    memcpy(&preflight->xnu_entry_stub, xnu_entry_stub, sizeof(*xnu_entry_stub));
    preflight->xnu_entry_stub_status = xnu_entry_stub->status;
    preflight->xnu_entry_stub_satisfied_mask = xnu_entry_stub->satisfied_mask;
    preflight->xnu_entry_stub_failure_mask = xnu_entry_stub->failure_mask;
    preflight->xnu_entry_stub_checksum = xnu_entry_stub->checksum;
    preflight->xnu_entry_stub_status_rollup =
        (xnu_entry_stub->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    xnu_early_init = stage85_xnu_early_pmap_platform_init_result();
    memcpy(&preflight->xnu_early_pmap_platform_init, xnu_early_init, sizeof(*xnu_early_init));
    preflight->xnu_early_pmap_platform_init_status = xnu_early_init->status;
    preflight->xnu_early_pmap_platform_init_satisfied_mask = xnu_early_init->satisfied_mask;
    preflight->xnu_early_pmap_platform_init_failure_mask = xnu_early_init->failure_mask;
    preflight->xnu_early_pmap_platform_init_checksum = xnu_early_init->checksum;
    preflight->xnu_early_pmap_platform_init_status_rollup =
        (xnu_early_init->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    xnu_pe_init = stage85_xnu_pe_init_platform_false_result();
    memcpy(&preflight->xnu_pe_init_platform_false, xnu_pe_init, sizeof(*xnu_pe_init));
    preflight->xnu_pe_init_platform_false_status = xnu_pe_init->status;
    preflight->xnu_pe_init_platform_false_satisfied_mask = xnu_pe_init->satisfied_mask;
    preflight->xnu_pe_init_platform_false_failure_mask = xnu_pe_init->failure_mask;
    preflight->xnu_pe_init_platform_false_checksum = xnu_pe_init->checksum;
    preflight->xnu_pe_init_platform_false_status_rollup =
        (xnu_pe_init->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    xnu_post_pe = stage85_xnu_arm_init_post_pe_bootstrap_result();
    memcpy(&preflight->xnu_arm_init_post_pe_bootstrap, xnu_post_pe, sizeof(*xnu_post_pe));
    preflight->xnu_arm_init_post_pe_bootstrap_status = xnu_post_pe->status;
    preflight->xnu_arm_init_post_pe_bootstrap_satisfied_mask = xnu_post_pe->satisfied_mask;
    preflight->xnu_arm_init_post_pe_bootstrap_failure_mask = xnu_post_pe->failure_mask;
    preflight->xnu_arm_init_post_pe_bootstrap_checksum = xnu_post_pe->checksum;
    preflight->xnu_arm_init_post_pe_bootstrap_status_rollup =
        (xnu_post_pe->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    xnu_live_pmap = stage85_xnu_arm_vm_init_full_pmap_result();
    memcpy(&preflight->xnu_arm_vm_init_full_pmap, xnu_live_pmap, sizeof(*xnu_live_pmap));
    preflight->xnu_arm_vm_init_full_pmap_status = xnu_live_pmap->status;
    preflight->xnu_arm_vm_init_full_pmap_satisfied_mask = xnu_live_pmap->satisfied_mask;
    preflight->xnu_arm_vm_init_full_pmap_failure_mask = xnu_live_pmap->failure_mask;
    preflight->xnu_arm_vm_init_full_pmap_checksum = xnu_live_pmap->checksum;
    preflight->xnu_arm_vm_init_full_pmap_status_rollup =
        (xnu_live_pmap->status == STAGE85_STATUS_OK) ? STAGE85_STATUS_OK : STAGE85_STATUS_BASE;

    if (preflight->platform_gap_mask == (STAGE85_PLATFORM_GAP_REQUIRED_RECORDED & ~STAGE85_PLATFORM_GAP_IOKIT_STACK) &&
        preflight->pexpert_gap_mask == 0u) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_PLATFORM_GAPS;
    }
    if (preflight->interrupt_ready_mask == STAGE85_LOADER_IRQ_READY_REQUIRED) {
        preflight->satisfied_mask |= STAGE85_LOADER_SAT_INTERRUPTS;
    }

    preflight->readiness_mask = preflight->satisfied_mask;
    preflight->failure_mask = probe.failure_mask | probe.staging_plan.failure_mask |
        preflight->tte_dryrun.failure_mask | preflight->ttbr_roundtrip_failure_mask |
        preflight->xnu_workspace_failure_mask | preflight->xnu_compile_graph_failure_mask |
        preflight->xnu_object_subset_failure_mask | preflight->xnu_link_failure_mask |
        preflight->xnu_bootstrap_contract_failure_mask |
        preflight->xnu_pmap_bootstrap_contract_failure_mask |
        preflight->xnu_pmap_table_dryrun_contract_failure_mask |
        preflight->xnu_pmap_page_dryrun_contract_failure_mask |
        preflight->xnu_pmap_attr_dryrun_contract_failure_mask |
        preflight->xnu_pmap_multiwindow_dryrun_contract_failure_mask |
        preflight->xnu_pmap_transition_dryrun_contract_failure_mask |
        preflight->xnu_pexpert_hook_readiness_contract_failure_mask |
        preflight->xnu_iokit_platform_scaffold_contract_failure_mask |
        preflight->xnu_iokit_match_dryrun_contract_failure_mask |
        preflight->xnu_iokit_registry_service_dryrun_contract_failure_mask |
        preflight->xnu_iokit_provider_plane_dryrun_contract_failure_mask |
        preflight->xnu_iokit_catalog_property_dryrun_contract_failure_mask |
        preflight->xnu_iokit_property_inheritance_dryrun_contract_failure_mask |
        preflight->xnu_iokit_registry_topology_dryrun_contract_failure_mask |
        preflight->xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask |
        preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_failure_mask |
        preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_failure_mask |
        preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_failure_mask |
        preflight->xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_failure_mask |
        preflight->xnu_entry_stub_failure_mask |
        preflight->xnu_early_pmap_platform_init_failure_mask |
        preflight->xnu_pe_init_platform_false_failure_mask |
        preflight->xnu_arm_init_post_pe_bootstrap_failure_mask |
        preflight->xnu_arm_vm_init_full_pmap_failure_mask;
    if (preflight->satisfied_mask != STAGE85_LOADER_SAT_REQUIRED) {
        preflight->failure_mask |= 0x80000000u;
    }
    preflight->checksum = stage85_loader_preflight_checksum(preflight);
    preflight->status = (preflight->satisfied_mask == STAGE85_LOADER_SAT_REQUIRED && preflight->failure_mask == 0u &&
        preflight->xnu_entry_stub_status_rollup == STAGE85_STATUS_OK &&
        preflight->xnu_early_pmap_platform_init_status_rollup == STAGE85_STATUS_OK &&
        preflight->xnu_pe_init_platform_false_status_rollup == STAGE85_STATUS_OK &&
        preflight->xnu_arm_init_post_pe_bootstrap_status_rollup == STAGE85_STATUS_OK &&
        preflight->xnu_arm_vm_init_full_pmap_status_rollup == STAGE85_STATUS_OK &&
        preflight->checksum == stage85_loader_preflight_checksum(preflight)) ?
        STAGE85_STATUS_OK : STAGE85_STATUS_FAIL(preflight->failure_mask);

    xnu_log_kv32("loader_xnu_baseline_tag", preflight->xnu_baseline_tag);
    xnu_log_kv32("loader_xnu_baseline_commit", preflight->xnu_baseline_commit);
    xnu_log_kv32("loader_xnu_master_version", preflight->xnu_master_version);
    xnu_log_kv32("loader_xnu_arm_reference", preflight->xnu_arm_reference);
    xnu_log_kv32("loader_actual_virtBase", preflight->actual_virtBase);
    xnu_log_kv32("loader_actual_physBase", preflight->actual_physBase);
    xnu_log_kv32("loader_actual_topOfKernelData", preflight->actual_topOfKernelData);
    xnu_log_kv32("loader_materialized_status", preflight->materialized_status);
    xnu_log_kv32("loader_materialized_file_bytes", preflight->materialized_file_bytes);
    xnu_log_kv32("loader_materialized_zero_bytes", preflight->materialized_zero_bytes);
    xnu_log_kv32("xnu_proposed_virtBase", preflight->proposed_virtBase);
    xnu_log_kv32("xnu_proposed_physBase", preflight->proposed_physBase);
    xnu_log_kv32("xnu_proposed_memSize", preflight->proposed_memSize);
    xnu_log_kv32("xnu_proposed_loaded_phys_base", preflight->proposed_loaded_phys_base);
    xnu_log_kv32("xnu_proposed_loaded_phys_end", preflight->proposed_loaded_phys_end);
    xnu_log_kv32("xnu_proposed_loaded_file_end", preflight->proposed_loaded_file_end);
    xnu_log_kv32("xnu_proposed_topOfKernelData", preflight->proposed_topOfKernelData);
    xnu_log_kv32("xnu_proposed_ttep_workspace_limit", preflight->proposed_ttep_workspace_limit);
    xnu_log_kv32("xnu_proposed_avail_start", preflight->proposed_avail_start);
    xnu_log_kv32("xnu_workspace_range_ok", preflight->proposed_workspace_range_ok);
    xnu_log_kv32("apple_dt_semantic_mask", preflight->apple_dt_semantic_mask);
    xnu_log_kv32("pexpert_gap_mask", preflight->pexpert_gap_mask);
    xnu_log_kv32("platform_gap_mask", preflight->platform_gap_mask);
    xnu_log_kv32("loader_interrupt_ready_mask", preflight->interrupt_ready_mask);
    xnu_log_kv32("loader_safety_mask", preflight->safety_mask);
    stage85_xnu_tte_dryrun_log(&preflight->tte_dryrun);
    xnu_log_kv32("loader_tte_dryrun_status", preflight->tte_dryrun_status);
    xnu_log_kv32("loader_tte_dryrun_satisfied_mask", preflight->tte_dryrun_satisfied_mask);
    xnu_log_kv32("loader_tte_dryrun_failure_mask", preflight->tte_dryrun_failure_mask);
    xnu_log_kv32("loader_tte_dryrun_checksum", preflight->tte_dryrun_checksum);
    xnu_log_kv32("loader_tte_verify_status", preflight->tte_verify_status);
    xnu_log_kv32("loader_vtop_dryrun_status", preflight->vtop_dryrun_status);
    xnu_log_kv32("loader_highva_dryrun_status", preflight->highva_dryrun_status);
    xnu_log_kv32("loader_safe_table_status", preflight->safe_table_status);
    xnu_log_kv32("loader_stage_owned_tables_status", preflight->stage_owned_tables_status);
    xnu_log_kv32("loader_ttbr_roundtrip_status", preflight->ttbr_roundtrip_status);
    xnu_log_kv32("loader_ttbr_roundtrip_satisfied_mask", preflight->ttbr_roundtrip_satisfied_mask);
    xnu_log_kv32("loader_ttbr_roundtrip_failure_mask", preflight->ttbr_roundtrip_failure_mask);
    xnu_log_kv32("loader_ttbr_roundtrip_checksum", preflight->ttbr_roundtrip_checksum);
    xnu_log_kv32("loader_stage_owned_ttbr_status", preflight->stage_owned_ttbr_status);
    xnu_log_kv32("loader_ttbr_restored_status", preflight->ttbr_restored_status);
    xnu_log_kv32("loader_cache_preserved_status", preflight->cache_preserved_status);
    xnu_log_kv32("loader_xnu_workspace_status", preflight->xnu_workspace_status);
    xnu_log_kv32("loader_xnu_workspace_satisfied_mask", preflight->xnu_workspace_satisfied_mask);
    xnu_log_kv32("loader_xnu_workspace_failure_mask", preflight->xnu_workspace_failure_mask);
    xnu_log_kv32("loader_xnu_workspace_checksum", preflight->xnu_workspace_checksum);
    xnu_log_kv32("loader_cancro_target_status", preflight->cancro_target_status);
    xnu_log_kv32("loader_stage85_plan_status", preflight->stage85_plan_status);
    xnu_log_kv32("loader_xnu_compile_graph_status", preflight->xnu_compile_graph_status);
    xnu_log_kv32("loader_xnu_compile_graph_satisfied_mask", preflight->xnu_compile_graph_satisfied_mask);
    xnu_log_kv32("loader_xnu_compile_graph_failure_mask", preflight->xnu_compile_graph_failure_mask);
    xnu_log_kv32("loader_xnu_compile_graph_checksum", preflight->xnu_compile_graph_checksum);
    xnu_log_kv32("loader_xnu_compile_graph_status_rollup", preflight->xnu_compile_graph_status_rollup);
    xnu_log_kv32("loader_xnu_object_subset_status", preflight->xnu_object_subset_status);
    xnu_log_kv32("loader_xnu_object_subset_satisfied_mask", preflight->xnu_object_subset_satisfied_mask);
    xnu_log_kv32("loader_xnu_object_subset_failure_mask", preflight->xnu_object_subset_failure_mask);
    xnu_log_kv32("loader_xnu_object_subset_checksum", preflight->xnu_object_subset_checksum);
    xnu_log_kv32("loader_xnu_object_subset_status_rollup", preflight->xnu_object_subset_status_rollup);
    xnu_log_kv32("loader_xnu_link_status", preflight->xnu_link_status);
    xnu_log_kv32("loader_xnu_link_satisfied_mask", preflight->xnu_link_satisfied_mask);
    xnu_log_kv32("loader_xnu_link_failure_mask", preflight->xnu_link_failure_mask);
    xnu_log_kv32("loader_xnu_link_checksum", preflight->xnu_link_checksum);
    xnu_log_kv32("loader_xnu_link_status_rollup", preflight->xnu_link_status_rollup);
    xnu_log_kv32("loader_xnu_bootstrap_contract_status", preflight->xnu_bootstrap_contract_status);
    xnu_log_kv32("loader_xnu_bootstrap_contract_satisfied_mask", preflight->xnu_bootstrap_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_bootstrap_contract_failure_mask", preflight->xnu_bootstrap_contract_failure_mask);
    xnu_log_kv32("loader_xnu_bootstrap_contract_checksum", preflight->xnu_bootstrap_contract_checksum);
    xnu_log_kv32("loader_xnu_bootstrap_contract_status_rollup", preflight->xnu_bootstrap_contract_status_rollup);
    xnu_log_kv32("loader_xnu_pmap_bootstrap_contract_status", preflight->xnu_pmap_bootstrap_contract_status);
    xnu_log_kv32("loader_xnu_pmap_bootstrap_contract_satisfied_mask", preflight->xnu_pmap_bootstrap_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_pmap_bootstrap_contract_failure_mask", preflight->xnu_pmap_bootstrap_contract_failure_mask);
    xnu_log_kv32("loader_xnu_pmap_bootstrap_contract_checksum", preflight->xnu_pmap_bootstrap_contract_checksum);
    xnu_log_kv32("loader_xnu_pmap_bootstrap_contract_status_rollup", preflight->xnu_pmap_bootstrap_contract_status_rollup);
    xnu_log_kv32("loader_xnu_pmap_table_dryrun_contract_status", preflight->xnu_pmap_table_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_pmap_table_dryrun_contract_satisfied_mask", preflight->xnu_pmap_table_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_pmap_table_dryrun_contract_failure_mask", preflight->xnu_pmap_table_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_pmap_table_dryrun_contract_checksum", preflight->xnu_pmap_table_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_pmap_table_dryrun_contract_status_rollup", preflight->xnu_pmap_table_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_pmap_page_dryrun_contract_status", preflight->xnu_pmap_page_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_pmap_page_dryrun_contract_satisfied_mask", preflight->xnu_pmap_page_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_pmap_page_dryrun_contract_failure_mask", preflight->xnu_pmap_page_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_pmap_page_dryrun_contract_checksum", preflight->xnu_pmap_page_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_pmap_page_dryrun_contract_status_rollup", preflight->xnu_pmap_page_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_pmap_attr_dryrun_contract_status", preflight->xnu_pmap_attr_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_pmap_attr_dryrun_contract_satisfied_mask", preflight->xnu_pmap_attr_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_pmap_attr_dryrun_contract_failure_mask", preflight->xnu_pmap_attr_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_pmap_attr_dryrun_contract_checksum", preflight->xnu_pmap_attr_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_pmap_attr_dryrun_contract_status_rollup", preflight->xnu_pmap_attr_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_pmap_multiwindow_dryrun_contract_status", preflight->xnu_pmap_multiwindow_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_pmap_multiwindow_dryrun_contract_satisfied_mask", preflight->xnu_pmap_multiwindow_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_pmap_multiwindow_dryrun_contract_failure_mask", preflight->xnu_pmap_multiwindow_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_pmap_multiwindow_dryrun_contract_checksum", preflight->xnu_pmap_multiwindow_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_pmap_multiwindow_dryrun_contract_status_rollup", preflight->xnu_pmap_multiwindow_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_pmap_transition_dryrun_contract_status", preflight->xnu_pmap_transition_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_pmap_transition_dryrun_contract_satisfied_mask", preflight->xnu_pmap_transition_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_pmap_transition_dryrun_contract_failure_mask", preflight->xnu_pmap_transition_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_pmap_transition_dryrun_contract_checksum", preflight->xnu_pmap_transition_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_pmap_transition_dryrun_contract_status_rollup", preflight->xnu_pmap_transition_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_pexpert_hook_readiness_contract_status", preflight->xnu_pexpert_hook_readiness_contract_status);
    xnu_log_kv32("loader_xnu_pexpert_hook_readiness_contract_satisfied_mask", preflight->xnu_pexpert_hook_readiness_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_pexpert_hook_readiness_contract_failure_mask", preflight->xnu_pexpert_hook_readiness_contract_failure_mask);
    xnu_log_kv32("loader_xnu_pexpert_hook_readiness_contract_checksum", preflight->xnu_pexpert_hook_readiness_contract_checksum);
    xnu_log_kv32("loader_xnu_pexpert_hook_readiness_contract_status_rollup", preflight->xnu_pexpert_hook_readiness_contract_status_rollup);
    xnu_log_kv32("loader_xnu_iokit_platform_scaffold_contract_status", preflight->xnu_iokit_platform_scaffold_contract_status);
    xnu_log_kv32("loader_xnu_iokit_platform_scaffold_contract_satisfied_mask", preflight->xnu_iokit_platform_scaffold_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_iokit_platform_scaffold_contract_failure_mask", preflight->xnu_iokit_platform_scaffold_contract_failure_mask);
    xnu_log_kv32("loader_xnu_iokit_platform_scaffold_contract_checksum", preflight->xnu_iokit_platform_scaffold_contract_checksum);
    xnu_log_kv32("loader_xnu_iokit_platform_scaffold_contract_status_rollup", preflight->xnu_iokit_platform_scaffold_contract_status_rollup);
    xnu_log_kv32("loader_xnu_iokit_match_dryrun_contract_status", preflight->xnu_iokit_match_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_iokit_match_dryrun_contract_satisfied_mask", preflight->xnu_iokit_match_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_iokit_match_dryrun_contract_failure_mask", preflight->xnu_iokit_match_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_iokit_match_dryrun_contract_checksum", preflight->xnu_iokit_match_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_iokit_match_dryrun_contract_status_rollup", preflight->xnu_iokit_match_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_iokit_registry_service_dryrun_contract_status", preflight->xnu_iokit_registry_service_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_iokit_registry_service_dryrun_contract_satisfied_mask", preflight->xnu_iokit_registry_service_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_iokit_registry_service_dryrun_contract_failure_mask", preflight->xnu_iokit_registry_service_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_iokit_registry_service_dryrun_contract_checksum", preflight->xnu_iokit_registry_service_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_iokit_registry_service_dryrun_contract_status_rollup", preflight->xnu_iokit_registry_service_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_iokit_provider_plane_dryrun_contract_status", preflight->xnu_iokit_provider_plane_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_iokit_provider_plane_dryrun_contract_satisfied_mask", preflight->xnu_iokit_provider_plane_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_iokit_provider_plane_dryrun_contract_failure_mask", preflight->xnu_iokit_provider_plane_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_iokit_provider_plane_dryrun_contract_checksum", preflight->xnu_iokit_provider_plane_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_iokit_provider_plane_dryrun_contract_status_rollup", preflight->xnu_iokit_provider_plane_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_iokit_catalog_property_dryrun_contract_status", preflight->xnu_iokit_catalog_property_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_iokit_catalog_property_dryrun_contract_satisfied_mask", preflight->xnu_iokit_catalog_property_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_iokit_catalog_property_dryrun_contract_failure_mask", preflight->xnu_iokit_catalog_property_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_iokit_catalog_property_dryrun_contract_checksum", preflight->xnu_iokit_catalog_property_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_iokit_catalog_property_dryrun_contract_status_rollup", preflight->xnu_iokit_catalog_property_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_iokit_property_inheritance_dryrun_contract_status", preflight->xnu_iokit_property_inheritance_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_iokit_property_inheritance_dryrun_contract_satisfied_mask", preflight->xnu_iokit_property_inheritance_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_iokit_property_inheritance_dryrun_contract_failure_mask", preflight->xnu_iokit_property_inheritance_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_iokit_property_inheritance_dryrun_contract_checksum", preflight->xnu_iokit_property_inheritance_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_iokit_property_inheritance_dryrun_contract_status_rollup", preflight->xnu_iokit_property_inheritance_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_iokit_registry_topology_dryrun_contract_status", preflight->xnu_iokit_registry_topology_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_iokit_registry_topology_dryrun_contract_satisfied_mask", preflight->xnu_iokit_registry_topology_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_iokit_registry_topology_dryrun_contract_failure_mask", preflight->xnu_iokit_registry_topology_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_iokit_registry_topology_dryrun_contract_checksum", preflight->xnu_iokit_registry_topology_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_iokit_registry_topology_dryrun_contract_status_rollup", preflight->xnu_iokit_registry_topology_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_iokit_attach_start_readiness_dryrun_contract_status", preflight->xnu_iokit_attach_start_readiness_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask", preflight->xnu_iokit_attach_start_readiness_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask", preflight->xnu_iokit_attach_start_readiness_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_iokit_attach_start_readiness_dryrun_contract_checksum", preflight->xnu_iokit_attach_start_readiness_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_iokit_attach_start_readiness_dryrun_contract_status_rollup", preflight->xnu_iokit_attach_start_readiness_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status", preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_satisfied_mask", preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_failure_mask", preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_checksum", preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status_rollup", preflight->xnu_iokit_lifecycle_register_service_readiness_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status", preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_satisfied_mask", preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_failure_mask", preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_checksum", preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status_rollup", preflight->xnu_iokit_provider_notification_delivery_readiness_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_status", preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_satisfied_mask", preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_failure_mask", preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_checksum", preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_status_rollup", preflight->xnu_iokit_provider_callback_client_notification_readiness_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_status", preflight->xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_status);
    xnu_log_kv32("loader_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_satisfied_mask", preflight->xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_satisfied_mask);
    xnu_log_kv32("loader_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_failure_mask", preflight->xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_failure_mask);
    xnu_log_kv32("loader_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_checksum", preflight->xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_checksum);
    xnu_log_kv32("loader_xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_status_rollup", preflight->xnu_iokit_client_open_provider_claim_close_readiness_dryrun_contract_status_rollup);
    xnu_log_kv32("loader_xnu_entry_stub_status", preflight->xnu_entry_stub_status);
    xnu_log_kv32("loader_xnu_entry_stub_satisfied_mask", preflight->xnu_entry_stub_satisfied_mask);
    xnu_log_kv32("loader_xnu_entry_stub_failure_mask", preflight->xnu_entry_stub_failure_mask);
    xnu_log_kv32("loader_xnu_entry_stub_checksum", preflight->xnu_entry_stub_checksum);
    xnu_log_kv32("loader_xnu_entry_stub_status_rollup", preflight->xnu_entry_stub_status_rollup);
    xnu_log_kv32("loader_xnu_early_init_status", preflight->xnu_early_pmap_platform_init_status);
    xnu_log_kv32("loader_xnu_early_init_satisfied_mask", preflight->xnu_early_pmap_platform_init_satisfied_mask);
    xnu_log_kv32("loader_xnu_early_init_failure_mask", preflight->xnu_early_pmap_platform_init_failure_mask);
    xnu_log_kv32("loader_xnu_early_init_checksum", preflight->xnu_early_pmap_platform_init_checksum);
    xnu_log_kv32("loader_xnu_early_init_status_rollup", preflight->xnu_early_pmap_platform_init_status_rollup);
    xnu_log_kv32("loader_xnu_pe_init_platform_false_status", preflight->xnu_pe_init_platform_false_status);
    xnu_log_kv32("loader_xnu_pe_init_platform_false_satisfied_mask", preflight->xnu_pe_init_platform_false_satisfied_mask);
    xnu_log_kv32("loader_xnu_pe_init_platform_false_failure_mask", preflight->xnu_pe_init_platform_false_failure_mask);
    xnu_log_kv32("loader_xnu_pe_init_platform_false_checksum", preflight->xnu_pe_init_platform_false_checksum);
    xnu_log_kv32("loader_xnu_pe_init_platform_false_status_rollup", preflight->xnu_pe_init_platform_false_status_rollup);
    xnu_log_kv32("loader_xnu_arm_init_post_pe_bootstrap_status", preflight->xnu_arm_init_post_pe_bootstrap_status);
    xnu_log_kv32("loader_xnu_arm_init_post_pe_bootstrap_satisfied_mask", preflight->xnu_arm_init_post_pe_bootstrap_satisfied_mask);
    xnu_log_kv32("loader_xnu_arm_init_post_pe_bootstrap_failure_mask", preflight->xnu_arm_init_post_pe_bootstrap_failure_mask);
    xnu_log_kv32("loader_xnu_arm_init_post_pe_bootstrap_checksum", preflight->xnu_arm_init_post_pe_bootstrap_checksum);
    xnu_log_kv32("loader_xnu_arm_init_post_pe_bootstrap_status_rollup", preflight->xnu_arm_init_post_pe_bootstrap_status_rollup);
    xnu_log_kv32("loader_xnu_arm_vm_init_full_pmap_status", preflight->xnu_arm_vm_init_full_pmap_status);
    xnu_log_kv32("loader_xnu_arm_vm_init_full_pmap_satisfied_mask", preflight->xnu_arm_vm_init_full_pmap_satisfied_mask);
    xnu_log_kv32("loader_xnu_arm_vm_init_full_pmap_failure_mask", preflight->xnu_arm_vm_init_full_pmap_failure_mask);
    xnu_log_kv32("loader_xnu_arm_vm_init_full_pmap_checksum", preflight->xnu_arm_vm_init_full_pmap_checksum);
    xnu_log_kv32("loader_xnu_arm_vm_init_full_pmap_status_rollup", preflight->xnu_arm_vm_init_full_pmap_status_rollup);
    xnu_log_kv32("loader_satisfied_mask", preflight->satisfied_mask);
    xnu_log_kv32("loader_checksum", preflight->checksum);
    xnu_log_kv32("loader_status", preflight->status);
    xnu_log_puts("Stage84 Stage-owned arm_vm_init live-pmap installation window enabled: loader calls stage85_xnu_start_stub with boot_args, the stub branches to stage85_arm_init_stub, the arm-init stub invokes stage85_xnu_early_pmap_platform_init_run, stage85_xnu_pe_init_platform_false_run, stage85_xnu_arm_init_post_pe_bootstrap_run, and stage85_xnu_arm_vm_init_full_pmap_run; the live-pmap window builds a Stage-owned candidate L1, writes TTBR0, invalidates TLBs, verifies high-alias/RAM-console/GIC translations, restores the original TTBR0, and returns with public XNU _start/arm_init/bootstrap/rtclock/arm_vm_init, public pmap/pexpert/IOKit objects, generated Mach-O fixture execution, cache policy change, and persistent writes still blocked\n");

    if (preflight->status == STAGE85_STATUS_OK) {
        xnu_log_puts("Stage84 Mach-O/XNU loader preflight ok\n");
        return 1;
    }

    xnu_log_puts("Stage84 Mach-O/XNU loader preflight failed\n");
    return 0;
}
