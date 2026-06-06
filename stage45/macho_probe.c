#include "stage45.h"

#define STAGE45_PAGE_SIZE 4096u

struct stage45_macho_header_32 {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
};

struct stage45_macho_load_command {
    uint32_t cmd;
    uint32_t cmdsize;
};

struct stage45_macho_segment_command_32 {
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

struct stage45_macho_section_32 {
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

struct stage45_macho_symtab_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
};

static struct stage45_loader_preflight stage45_loader_preflight_block;
static uint8_t stage45_macho_staging_arena[STAGE45_MACHO_STAGING_ARENA_CAP]
    __attribute__((aligned(STAGE45_MACHO_STAGING_ARENA_ALIGN)));

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
        return STAGE45_MACHO_SEG_TEXT;
    }
    if (fixed16_eq(segname, "__DATA")) {
        return STAGE45_MACHO_SEG_DATA;
    }
    if (fixed16_eq(segname, "__LINKEDIT")) {
        return STAGE45_MACHO_SEG_LINKEDIT;
    }
    if (fixed16_eq(segname, "__KLD")) {
        return STAGE45_MACHO_SEG_KLD;
    }
    if (fixed16_eq(segname, "__LAST")) {
        return STAGE45_MACHO_SEG_LAST;
    }
    if (fixed16_eq(segname, "__PRELINK_TEXT")) {
        return STAGE45_MACHO_SEG_PRELINK_TEXT;
    }
    if (fixed16_eq(segname, "__PRELINK_INFO")) {
        return STAGE45_MACHO_SEG_PRELINK_INFO;
    }
    if (fixed16_eq(segname, "__PRELINK_STATE")) {
        return STAGE45_MACHO_SEG_PRELINK_STATE;
    }
    if (fixed16_eq(segname, "__PRELINK")) {
        return STAGE45_MACHO_SEG_PRELINK;
    }

    return 0;
}

static void record_key_segment(struct stage45_macho_probe_result *result,
                               const struct stage45_macho_segment_command_32 *segment,
                               uint32_t seg_bit)
{
    if (seg_bit == STAGE45_MACHO_SEG_TEXT) {
        result->text_vmaddr = segment->vmaddr;
        result->text_vmsize = segment->vmsize;
        result->text_fileoff = segment->fileoff;
        result->text_filesize = segment->filesize;
    } else if (seg_bit == STAGE45_MACHO_SEG_DATA) {
        result->data_vmaddr = segment->vmaddr;
        result->data_vmsize = segment->vmsize;
        result->data_fileoff = segment->fileoff;
        result->data_filesize = segment->filesize;
    } else if (seg_bit == STAGE45_MACHO_SEG_LINKEDIT) {
        result->linkedit_vmaddr = segment->vmaddr;
        result->linkedit_vmsize = segment->vmsize;
        result->linkedit_fileoff = segment->fileoff;
        result->linkedit_filesize = segment->filesize;
    } else if (seg_bit == STAGE45_MACHO_SEG_PRELINK_TEXT) {
        result->prelink_text_vmaddr = segment->vmaddr;
        result->prelink_text_vmsize = segment->vmsize;
        result->prelink_text_fileoff = segment->fileoff;
        result->prelink_text_filesize = segment->filesize;
    } else if (seg_bit == STAGE45_MACHO_SEG_PRELINK_INFO) {
        result->prelink_info_vmaddr = segment->vmaddr;
        result->prelink_info_vmsize = segment->vmsize;
        result->prelink_info_fileoff = segment->fileoff;
        result->prelink_info_filesize = segment->filesize;
    } else if (seg_bit == STAGE45_MACHO_SEG_PRELINK_STATE) {
        result->prelink_state_vmaddr = segment->vmaddr;
        result->prelink_state_vmsize = segment->vmsize;
        result->prelink_state_fileoff = segment->fileoff;
        result->prelink_state_filesize = segment->filesize;
    }
}

static void add_load_plan_entry(struct stage45_macho_probe_result *result,
                                const struct stage45_macho_segment_command_32 *segment,
                                uint32_t seg_bit)
{
    struct stage45_macho_load_plan *plan = &result->load_plan;
    struct stage45_macho_load_plan_entry *entry;

    if (segment->vmsize == 0u) {
        return;
    }
    if (plan->entry_count >= STAGE45_MACHO_LOAD_PLAN_MAX) {
        result->failure_mask |= STAGE45_MACHO_FAIL_LOAD_PLAN_FULL;
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
        entry->flags |= STAGE45_MACHO_LOAD_PLAN_FILE_BACKED;
    }
    if ((seg_bit & STAGE45_MACHO_SEG_PRELINK_MASK) != 0u) {
        entry->flags |= STAGE45_MACHO_LOAD_PLAN_PRELINK;
    }
    if (segment->vmsize > segment->filesize) {
        entry->flags |= STAGE45_MACHO_LOAD_PLAN_ZERO_FILL;
    }
    plan->entry_count++;
}

static void record_section_match(struct stage45_macho_probe_result *result,
                                 const struct stage45_macho_section_32 *section)
{
    uint32_t section_bit = 0;

    if (fixed16_eq(section->segname, "__TEXT") && fixed16_eq(section->sectname, "__text")) {
        section_bit = STAGE45_MACHO_SECT_TEXT_TEXT;
        result->text_section_vmaddr = section->addr;
        result->text_section_size = section->size;
    } else if (fixed16_eq(section->segname, "__DATA") && fixed16_eq(section->sectname, "__const")) {
        section_bit = STAGE45_MACHO_SECT_DATA_CONST;
        result->data_const_vmaddr = section->addr;
        result->data_const_size = section->size;
    } else if (fixed16_eq(section->segname, "__PRELINK_TEXT") && fixed16_eq(section->sectname, "__text")) {
        section_bit = STAGE45_MACHO_SECT_PRELINK_TEXT_TEXT;
        result->prelink_text_section_vmaddr = section->addr;
        result->prelink_text_section_size = section->size;
    } else if (fixed16_eq(section->segname, "__PRELINK_INFO") && fixed16_eq(section->sectname, "__info")) {
        section_bit = STAGE45_MACHO_SECT_PRELINK_INFO_INFO;
        result->prelink_info_section_vmaddr = section->addr;
        result->prelink_info_section_size = section->size;
    } else if (fixed16_eq(section->segname, "__PRELINK_INFO") && fixed16_eq(section->sectname, "__kernel")) {
        section_bit = STAGE45_MACHO_SECT_PRELINK_INFO_KERNEL;
    } else if (fixed16_eq(section->segname, "__PRELINK_INFO") && fixed16_eq(section->sectname, "__kexts")) {
        section_bit = STAGE45_MACHO_SECT_PRELINK_INFO_KEXTS;
    } else if (fixed16_eq(section->segname, "__PRELINK_STATE") && fixed16_eq(section->sectname, "__kernel")) {
        section_bit = STAGE45_MACHO_SECT_PRELINK_STATE_KERNEL;
    } else if (fixed16_eq(section->segname, "__PRELINK_STATE") && fixed16_eq(section->sectname, "__kexts")) {
        section_bit = STAGE45_MACHO_SECT_PRELINK_STATE_KEXTS;
    } else if (fixed16_eq(section->segname, "__PRELINK") && fixed16_eq(section->sectname, "__symtab")) {
        section_bit = STAGE45_MACHO_SECT_PRELINK_SYMTAB;
    }

    result->section_seen_mask |= section_bit;
    if ((section_bit & STAGE45_MACHO_SECT_PRELINK_MASK) != 0u) {
        result->prelink_section_seen_mask |= section_bit;
    }
}

static void record_section(struct stage45_macho_probe_result *result,
                           const struct stage45_macho_segment_command_32 *segment,
                           const struct stage45_macho_section_32 *section)
{
    uint32_t section_end = 0;
    uint32_t segment_end = 0;
    uint32_t file_end = 0;

    result->section_count++;
    record_section_match(result, section);

    if (add_overflow_u32(section->addr, section->size, &section_end) ||
        add_overflow_u32(segment->vmaddr, segment->vmsize, &segment_end) ||
        section->addr < segment->vmaddr || section_end > segment_end) {
        result->failure_mask |= STAGE45_MACHO_FAIL_SECTION_BOUNDS;
    }

    if (section->size != 0u && section->offset != 0u) {
        if (add_overflow_u32(section->offset, section->size, &file_end) || file_end > result->artifact_size) {
            result->failure_mask |= STAGE45_MACHO_FAIL_SECTION_BOUNDS;
        }
        if (file_end > result->max_file_extent) {
            result->max_file_extent = file_end;
        }
    }
}

static void record_segment(struct stage45_macho_probe_result *result,
                           const struct stage45_macho_segment_command_32 *segment)
{
    uint32_t vm_end = 0;
    uint32_t file_end = 0;
    uint32_t seg_bit;

    result->segment_count++;
    result->validation_mask |= STAGE45_MACHO_VALID_SEGMENT_COMMAND;
    seg_bit = segment_mask_for_name(segment->segname);

    if (add_overflow_u32(segment->vmaddr, segment->vmsize, &vm_end)) {
        result->failure_mask |= STAGE45_MACHO_FAIL_SEGMENT_BOUNDS;
        return;
    }
    if (add_overflow_u32(segment->fileoff, segment->filesize, &file_end) || file_end > result->artifact_size) {
        result->failure_mask |= STAGE45_MACHO_FAIL_SEGMENT_EXTENT;
    }

    result->segment_seen_mask |= seg_bit;
    if ((seg_bit & STAGE45_MACHO_SEG_PRELINK_MASK) != 0u) {
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

static void parse_segment_sections(struct stage45_macho_probe_result *result,
                                   const uint8_t *bytes,
                                   uint32_t command_offset,
                                   const struct stage45_macho_segment_command_32 *segment)
{
    uint32_t section_bytes = 0;
    uint32_t expected_cmdsize = 0;
    uint32_t section_offset;

    if (mul_overflow_u32(segment->nsects, sizeof(struct stage45_macho_section_32), &section_bytes) ||
        add_overflow_u32(sizeof(*segment), section_bytes, &expected_cmdsize) ||
        expected_cmdsize > segment->cmdsize) {
        result->failure_mask |= STAGE45_MACHO_FAIL_SECTION_BOUNDS;
        return;
    }

    section_offset = command_offset + sizeof(*segment);
    for (uint32_t i = 0; i < segment->nsects; i++) {
        const struct stage45_macho_section_32 *section =
            (const struct stage45_macho_section_32 *)(const void *)(bytes + section_offset);
        record_section(result, segment, section);
        section_offset += sizeof(*section);
    }
}

static void finalize_load_plan(struct stage45_macho_probe_result *result)
{
    struct stage45_macho_load_plan *plan = &result->load_plan;
    uint32_t phys_end = RAM_PHYS_BASE;
    uint32_t file_base = UINT32_MAX;
    uint32_t file_end = 0;

    plan->version = STAGE45_MACHO_LOAD_PLAN_VERSION;
    plan->vm_base = result->min_vmaddr;
    plan->vm_end = result->max_vmaddr;
    plan->phys_base = RAM_PHYS_BASE;

    if (plan->entry_count == 0u || result->min_vmaddr == 0u || result->max_vmaddr <= result->min_vmaddr) {
        result->failure_mask |= STAGE45_MACHO_FAIL_LOAD_PLAN;
        return;
    }

    for (uint32_t i = 0; i < plan->entry_count; i++) {
        struct stage45_macho_load_plan_entry *entry = &plan->entries[i];
        uint32_t vm_delta;
        uint32_t aligned_size;
        uint32_t entry_end;
        uint32_t entry_file_end;

        if (entry->vmaddr < result->min_vmaddr) {
            result->failure_mask |= STAGE45_MACHO_FAIL_LOAD_PLAN;
            continue;
        }
        vm_delta = entry->vmaddr - result->min_vmaddr;
        if (add_overflow_u32(RAM_PHYS_BASE, vm_delta, &entry->physaddr) ||
            align_up_checked_u32(entry->vmsize, STAGE45_PAGE_SIZE, &aligned_size) ||
            add_overflow_u32(entry->physaddr, aligned_size, &entry_end)) {
            result->failure_mask |= STAGE45_MACHO_FAIL_PHYS_RANGE;
            continue;
        }
        entry->physsize = aligned_size;
        if (entry->physaddr < RAM_PHYS_BASE || entry_end > RAM_CONSOLE_BASE || entry_end < entry->physaddr) {
            result->failure_mask |= STAGE45_MACHO_FAIL_PHYS_RANGE;
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

    if ((result->failure_mask & (STAGE45_MACHO_FAIL_LOAD_PLAN | STAGE45_MACHO_FAIL_PHYS_RANGE |
                                 STAGE45_MACHO_FAIL_LOAD_PLAN_FULL)) == 0u) {
        plan->status = STAGE45_STATUS_OK;
        result->load_plan_status = STAGE45_STATUS_OK;
        result->validation_mask |= STAGE45_MACHO_VALID_LOAD_PLAN;
    } else {
        plan->status = STAGE45_STATUS_BASE | result->failure_mask;
        result->load_plan_status = plan->status;
    }
}

static void log_load_plan_entry(const char *prefix_vmaddr, const char *prefix_vmsize,
                                const char *prefix_physaddr, const char *prefix_physsize,
                                const struct stage45_macho_load_plan_entry *entry)
{
    xnu_log_kv32(prefix_vmaddr, entry->vmaddr);
    xnu_log_kv32(prefix_vmsize, entry->vmsize);
    xnu_log_kv32(prefix_physaddr, entry->physaddr);
    xnu_log_kv32(prefix_physsize, entry->physsize);
}

static void log_load_plan_entries(const struct stage45_macho_probe_result *result)
{
    const struct stage45_macho_load_plan *plan = &result->load_plan;

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

int stage45_macho_probe(const void *artifact, uint32_t artifact_size,
                        struct stage45_macho_probe_result *result)
{
    const uint8_t *bytes = (const uint8_t *)artifact;
    const struct stage45_macho_header_32 *header;
    uint32_t commands_start = sizeof(*header);
    uint32_t commands_end = 0;
    uint32_t offset;
    uint32_t command_region_ok = 0;

    if (!result) {
        return 0;
    }

    memset(result, 0, sizeof(*result));
    result->version = STAGE45_MACHO_PROBE_VERSION;
    result->artifact_base = (uint32_t)(uintptr_t)artifact;
    result->artifact_size = artifact_size;
    result->segment_required_mask = STAGE45_MACHO_SEG_REQUIRED;
    result->section_required_mask = STAGE45_MACHO_SECT_REQUIRED;
    result->entry_kind_mask = STAGE45_MACHO_ENTRY_NOT_EXECUTED;
    result->status = STAGE45_STATUS_BASE;
    result->load_plan.version = STAGE45_MACHO_LOAD_PLAN_VERSION;

    xnu_log_puts("Stage45 Mach-O probe begin\n");
    xnu_log_kv32("macho_artifact_base", result->artifact_base);
    xnu_log_kv32("macho_artifact_size", result->artifact_size);

    if (!artifact) {
        result->failure_mask |= STAGE45_MACHO_FAIL_NULL;
        goto done;
    }
    if (artifact_size < sizeof(*header)) {
        result->failure_mask |= STAGE45_MACHO_FAIL_SIZE;
        goto done;
    }

    header = (const struct stage45_macho_header_32 *)artifact;
    result->magic = header->magic;
    result->cputype = header->cputype;
    result->cpusubtype = header->cpusubtype;
    result->filetype = header->filetype;
    result->ncmds = header->ncmds;
    result->sizeofcmds = header->sizeofcmds;
    result->flags = header->flags;

    if (header->magic == STAGE45_MACHO_MAGIC) {
        result->validation_mask |= STAGE45_MACHO_VALID_MAGIC | STAGE45_MACHO_VALID_ENDIAN;
    } else if (header->magic == STAGE45_MACHO_CIGAM) {
        result->validation_mask |= STAGE45_MACHO_VALID_MAGIC;
        result->failure_mask |= STAGE45_MACHO_FAIL_ENDIAN;
    } else {
        result->failure_mask |= STAGE45_MACHO_FAIL_MAGIC;
    }

    if (header->cputype == STAGE45_MACHO_CPU_TYPE_ARM) {
        result->validation_mask |= STAGE45_MACHO_VALID_CPU;
    } else {
        result->failure_mask |= STAGE45_MACHO_FAIL_CPU;
    }

    if (header->cpusubtype == STAGE45_MACHO_CPU_SUBTYPE_ARM_ALL ||
        header->cpusubtype == STAGE45_MACHO_CPU_SUBTYPE_ARM_V7) {
        result->validation_mask |= STAGE45_MACHO_VALID_SUBTYPE;
    } else {
        result->failure_mask |= STAGE45_MACHO_FAIL_SUBTYPE;
    }

    if (header->filetype == STAGE45_MACHO_FILETYPE_EXECUTE ||
        header->filetype == STAGE45_MACHO_FILETYPE_PRELOAD) {
        result->validation_mask |= STAGE45_MACHO_VALID_FILETYPE;
    } else {
        result->failure_mask |= STAGE45_MACHO_FAIL_FILETYPE;
    }

    if (add_overflow_u32(commands_start, header->sizeofcmds, &commands_end) ||
        commands_end > artifact_size || (header->sizeofcmds & 3u) != 0u) {
        result->failure_mask |= STAGE45_MACHO_FAIL_COMMAND_BOUNDS;
    } else {
        result->validation_mask |= STAGE45_MACHO_VALID_COMMAND_BOUNDS;
        command_region_ok = 1u;
    }

    if (command_region_ok == 0u) {
        goto after_commands;
    }

    offset = commands_start;
    for (uint32_t i = 0; i < header->ncmds && offset < commands_end; i++) {
        const struct stage45_macho_load_command *load_command;
        uint32_t next_offset = 0;

        if (commands_end - offset < sizeof(*load_command)) {
            result->failure_mask |= STAGE45_MACHO_FAIL_COMMAND_BOUNDS;
            break;
        }

        load_command = (const struct stage45_macho_load_command *)(const void *)(bytes + offset);
        if (load_command->cmdsize < sizeof(*load_command) || (load_command->cmdsize & 3u) != 0u ||
            add_overflow_u32(offset, load_command->cmdsize, &next_offset) || next_offset > commands_end) {
            result->failure_mask |= STAGE45_MACHO_FAIL_COMMAND_SIZE;
            break;
        }

        result->load_command_count_seen++;
        if (load_command->cmd == STAGE45_MACHO_LC_SEGMENT) {
            if (load_command->cmdsize < sizeof(struct stage45_macho_segment_command_32)) {
                result->failure_mask |= STAGE45_MACHO_FAIL_COMMAND_SIZE;
            } else {
                const struct stage45_macho_segment_command_32 *segment =
                    (const struct stage45_macho_segment_command_32 *)(const void *)(bytes + offset);
                result->load_command_seen_mask |= STAGE45_MACHO_CMD_SEGMENT;
                record_segment(result, segment);
                parse_segment_sections(result, bytes, offset, segment);
            }
        } else if (load_command->cmd == STAGE45_MACHO_LC_SYMTAB) {
            result->load_command_seen_mask |= STAGE45_MACHO_CMD_SYMTAB;
            if (load_command->cmdsize >= sizeof(struct stage45_macho_symtab_command)) {
                const struct stage45_macho_symtab_command *symtab =
                    (const struct stage45_macho_symtab_command *)(const void *)(bytes + offset);
                uint32_t str_end = 0;
                if (add_overflow_u32(symtab->stroff, symtab->strsize, &str_end) || str_end > artifact_size) {
                    result->failure_mask |= STAGE45_MACHO_FAIL_SEGMENT_EXTENT;
                }
            }
        } else if (load_command->cmd == STAGE45_MACHO_LC_UNIXTHREAD) {
            result->load_command_seen_mask |= STAGE45_MACHO_CMD_UNIXTHREAD;
            result->entry_kind_mask |= STAGE45_MACHO_ENTRY_UNIXTHREAD;
            if (load_command->cmdsize >= 16u) {
                const uint32_t *entry_words = (const uint32_t *)(const void *)(bytes + offset);
                result->entryoff_or_pc = entry_words[3];
            }
        } else if (load_command->cmd == STAGE45_MACHO_LC_MAIN) {
            result->load_command_seen_mask |= STAGE45_MACHO_CMD_MAIN;
            result->entry_kind_mask |= STAGE45_MACHO_ENTRY_MAIN;
        } else {
            result->load_command_seen_mask |= STAGE45_MACHO_CMD_UNKNOWN;
            result->unknown_command_count++;
        }

        offset = next_offset;
    }

    if (result->load_command_count_seen != header->ncmds || offset != commands_end) {
        result->failure_mask |= STAGE45_MACHO_FAIL_COMMAND_BOUNDS;
    }

after_commands:
    if ((result->segment_seen_mask & STAGE45_MACHO_SEG_REQUIRED) == STAGE45_MACHO_SEG_REQUIRED) {
        result->validation_mask |= STAGE45_MACHO_VALID_REQUIRED_SEGMENTS;
    } else {
        result->failure_mask |= STAGE45_MACHO_FAIL_REQUIRED_SEGMENTS;
    }
    if ((result->section_seen_mask & STAGE45_MACHO_SECT_REQUIRED) == STAGE45_MACHO_SECT_REQUIRED) {
        result->validation_mask |= STAGE45_MACHO_VALID_REQUIRED_SECTIONS;
    } else {
        result->failure_mask |= STAGE45_MACHO_FAIL_REQUIRED_SECTIONS;
    }
    if ((result->failure_mask & STAGE45_MACHO_FAIL_SEGMENT_EXTENT) == 0u) {
        result->validation_mask |= STAGE45_MACHO_VALID_FILE_EXTENTS;
    }
    if ((result->failure_mask & STAGE45_MACHO_FAIL_SECTION_BOUNDS) == 0u) {
        result->validation_mask |= STAGE45_MACHO_VALID_SECTION_BOUNDS;
    }
    if ((result->prelink_segment_seen_mask | result->prelink_section_seen_mask) != 0u) {
        result->validation_mask |= STAGE45_MACHO_VALID_PRELINK_REPORT;
    }
    if ((result->entry_kind_mask & (STAGE45_MACHO_ENTRY_UNIXTHREAD | STAGE45_MACHO_ENTRY_MAIN)) != 0u) {
        result->validation_mask |= STAGE45_MACHO_VALID_ENTRY_NOT_EXECUTED;
    } else {
        result->failure_mask |= STAGE45_MACHO_FAIL_NO_ENTRY_METADATA;
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
        const uint32_t parser_required = STAGE45_MACHO_VALID_REQUIRED &
            ~(STAGE45_MACHO_VALID_STAGING |
              STAGE45_MACHO_VALID_MATERIALIZED_PARSE |
              STAGE45_MACHO_VALID_MARKERS |
              STAGE45_MACHO_VALID_ZERO_FILL);

        if ((result->validation_mask & parser_required) == parser_required &&
            result->failure_mask == 0u) {
            result->status = STAGE45_STATUS_OK;
            xnu_log_puts("Stage45 Mach-O probe ok; entry metadata observed but not executed\n");
            return 1;
        }
    }

    result->status = STAGE45_STATUS_BASE | result->failure_mask;
    xnu_log_puts("Stage45 Mach-O probe failed\n");
    return 0;
}

static void log_stage_entry(const char *copy_src_key, const char *copy_dst_key,
                            const char *copy_size_key, const char *zero_dst_key,
                            const char *zero_size_key,
                            const struct stage45_macho_staging_plan_entry *entry)
{
    xnu_log_kv32(copy_src_key, entry->copy_src_off);
    xnu_log_kv32(copy_dst_key, entry->copy_dst);
    xnu_log_kv32(copy_size_key, entry->copy_size);
    xnu_log_kv32(zero_dst_key, entry->zero_dst);
    xnu_log_kv32(zero_size_key, entry->zero_size);
}

static void log_staging_entries(const struct stage45_macho_staging_plan *staging)
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

static uint32_t stage45_macho_getlastaddr_style(const struct stage45_macho_probe_result *probe)
{
    uint32_t last = 0;

    for (uint32_t i = 0; i < probe->load_plan.entry_count; i++) {
        uint32_t end = 0;
        const struct stage45_macho_load_plan_entry *entry = &probe->load_plan.entries[i];
        if (!add_overflow_u32(entry->vmaddr, entry->vmsize, &end) && end > last) {
            last = end;
        }
    }

    return last;
}

static void stage45_macho_check_arena_aliases(struct stage45_macho_probe_result *result,
                                              uint32_t artifact_base,
                                              uint32_t artifact_size)
{
    struct stage45_macho_staging_plan *staging = &result->staging_plan;
    uint32_t artifact_end = 0;

    if (add_overflow_u32(artifact_base, artifact_size, &artifact_end)) {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_ARENA_ALIAS;
        return;
    }
    if (staging->arena_base < artifact_end && artifact_base < staging->arena_end) {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_ARENA_ALIAS;
    }
    if (staging->arena_base < (RAM_CONSOLE_BASE + RAM_CONSOLE_SIZE) && RAM_CONSOLE_BASE < staging->arena_end) {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_ARENA_ALIAS;
    }
    if (result->load_phys_end != 0u && staging->arena_base < result->load_phys_end &&
        result->load_phys_base < staging->arena_end) {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_ARENA_ALIAS;
    }
    if (staging->arena_base >= 0xf0000000u || staging->arena_end > 0xf0000000u) {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_ARENA_ALIAS;
    }
}

static void stage45_macho_verify_marker(struct stage45_macho_staging_plan *staging,
                                        uint32_t vmaddr,
                                        const char *prefix,
                                        uint32_t bit)
{
    uint32_t offset;

    if (vmaddr < staging->vm_base) {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_MARKERS;
        return;
    }
    offset = vmaddr - staging->vm_base;
    if (offset >= staging->arena_size) {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_MARKERS;
        return;
    }
    if (bytes_prefix_eq(stage45_macho_staging_arena + offset, prefix)) {
        staging->marker_check_mask |= bit;
    } else {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_MARKERS;
    }
}

static void stage45_macho_verify_zero_fill(struct stage45_macho_staging_plan *staging)
{
    uint32_t required_zero_mask = 0;

    for (uint32_t i = 0; i < staging->entry_count; i++) {
        const struct stage45_macho_staging_plan_entry *entry = &staging->entries[i];
        uint32_t bit = 1u << i;
        uint32_t entry_ok = 1u;

        if (entry->zero_size == 0u) {
            continue;
        }
        required_zero_mask |= bit;
        if (entry->arena_offset + entry->copy_size + entry->zero_size > staging->arena_size) {
            staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_ZERO_FILL;
            entry_ok = 0u;
        } else {
            uint32_t zero_offset = entry->arena_offset + entry->copy_size;
            for (uint32_t j = 0; j < entry->zero_size; j++) {
                if (stage45_macho_staging_arena[zero_offset + j] != 0u) {
                    staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_ZERO_FILL;
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
        staging->validation_mask |= STAGE45_MACHO_VALID_ZERO_FILL;
    } else {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_ZERO_FILL;
    }
}

static void stage45_macho_verify_reparse(struct stage45_macho_probe_result *result,
                                         struct stage45_macho_staging_plan *staging)
{
    struct stage45_macho_probe_result materialized;
    uint32_t original_last = stage45_macho_getlastaddr_style(result);
    uint32_t materialized_last;

    if (!stage45_macho_probe(stage45_macho_staging_arena, staging->arena_size, &materialized)) {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_REPARSE;
        return;
    }

    materialized_last = stage45_macho_getlastaddr_style(&materialized);
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
        staging->validation_mask |= STAGE45_MACHO_VALID_MATERIALIZED_PARSE;
        xnu_log_puts("Stage45 materialized Mach-O reparse ok\n");
    } else {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_REPARSE;
    }
}

static int stage45_macho_materialize_to_arena(const void *artifact,
                                              uint32_t artifact_size,
                                              struct stage45_macho_probe_result *result)
{
    const uint8_t *source = (const uint8_t *)artifact;
    struct stage45_macho_staging_plan *staging = &result->staging_plan;
    uint32_t arena_base = (uint32_t)(uintptr_t)stage45_macho_staging_arena;
    uint32_t arena_end = 0;
    uint32_t vm_span = 0;
    uint32_t arena_size = 0;

    xnu_log_puts("Stage45 proposed physical load plan is dry-run only; no physical writes performed\n");
    xnu_log_puts("Stage45 Mach-O materialization begin\n");
    memset(stage45_macho_staging_arena, 0, sizeof(stage45_macho_staging_arena));
    memset(staging, 0, sizeof(*staging));
    staging->version = STAGE45_MACHO_STAGING_PLAN_VERSION;
    staging->status = STAGE45_STATUS_BASE;
    staging->arena_base = arena_base;

    if (add_overflow_u32(arena_base, STAGE45_MACHO_STAGING_ARENA_CAP, &arena_end)) {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_SPAN;
        goto finish;
    }
    staging->arena_end = arena_end;
    if (result->min_vmaddr == 0u || result->max_vmaddr <= result->min_vmaddr) {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_SPAN;
        goto finish;
    }
    vm_span = result->max_vmaddr - result->min_vmaddr;
    if (align_up_checked_u32(vm_span, STAGE45_PAGE_SIZE, &arena_size) ||
        arena_size > STAGE45_MACHO_STAGING_ARENA_CAP) {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_SPAN;
        goto finish;
    }

    staging->vm_base = result->min_vmaddr;
    staging->vm_end = result->max_vmaddr;
    staging->vm_span = vm_span;
    staging->arena_size = arena_size;
    staging->materialized_header = arena_base;
    stage45_macho_check_arena_aliases(result, (uint32_t)(uintptr_t)artifact, artifact_size);
    if (staging->failure_mask != 0u) {
        goto finish;
    }

    for (uint32_t i = 0; i < result->load_plan.entry_count; i++) {
        const struct stage45_macho_load_plan_entry *load = &result->load_plan.entries[i];
        struct stage45_macho_staging_plan_entry *entry;
        uint32_t src_end;
        uint32_t arena_offset;
        uint32_t dst_end;
        uint32_t copy_end;
        uint32_t zero_size;

        if (staging->entry_count >= STAGE45_MACHO_STAGING_PLAN_MAX) {
            staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_PLAN_FULL;
            break;
        }
        if (load->vmaddr < staging->vm_base || load->filesize > load->vmsize) {
            staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_DST_BOUNDS;
            continue;
        }
        arena_offset = load->vmaddr - staging->vm_base;
        if (add_overflow_u32(load->fileoff, load->filesize, &src_end) || src_end > artifact_size) {
            staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_SRC_BOUNDS;
            continue;
        }
        if (add_overflow_u32(arena_offset, load->vmsize, &dst_end) || dst_end > arena_size ||
            add_overflow_u32(arena_offset, load->filesize, &copy_end) || copy_end > arena_size) {
            staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_DST_BOUNDS;
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
            memcpy(stage45_macho_staging_arena + arena_offset, source + load->fileoff, load->filesize);
            staging->file_bytes += load->filesize;
        }
        if (zero_size != 0u) {
            memset(stage45_macho_staging_arena + arena_offset + load->filesize, 0, zero_size);
            staging->zero_bytes += zero_size;
        }
    }

    if (staging->entry_count != result->load_plan.entry_count) {
        staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_COPY;
    }

    if (staging->failure_mask == 0u) {
        staging->validation_mask |= STAGE45_MACHO_VALID_STAGING;
        stage45_macho_verify_reparse(result, staging);
        stage45_macho_verify_marker(staging, result->text_section_vmaddr,
                                    "ST45-TEXT", STAGE45_MACHO_MARKER_TEXT);
        stage45_macho_verify_marker(staging, result->data_const_vmaddr,
                                    "ST45-DATA", STAGE45_MACHO_MARKER_DATA);
        stage45_macho_verify_marker(staging, result->prelink_text_section_vmaddr,
                                    "ST45-PRELINK-TEXT", STAGE45_MACHO_MARKER_PRELINK_TEXT);
        if ((staging->marker_check_mask & STAGE45_MACHO_MARKER_REQUIRED) == STAGE45_MACHO_MARKER_REQUIRED) {
            staging->validation_mask |= STAGE45_MACHO_VALID_MARKERS;
        } else {
            staging->failure_mask |= STAGE45_MACHO_FAIL_STAGE_MARKERS;
        }
        stage45_macho_verify_zero_fill(staging);
    }

finish:
    if (staging->failure_mask == 0u &&
        (staging->validation_mask & (STAGE45_MACHO_VALID_STAGING |
                                     STAGE45_MACHO_VALID_MATERIALIZED_PARSE |
                                     STAGE45_MACHO_VALID_MARKERS |
                                     STAGE45_MACHO_VALID_ZERO_FILL)) ==
        (STAGE45_MACHO_VALID_STAGING | STAGE45_MACHO_VALID_MATERIALIZED_PARSE |
         STAGE45_MACHO_VALID_MARKERS | STAGE45_MACHO_VALID_ZERO_FILL)) {
        staging->status = STAGE45_STATUS_OK;
    } else {
        staging->status = STAGE45_STATUS_BASE | staging->failure_mask;
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

    return staging->status == STAGE45_STATUS_OK;
}

static uint32_t stage45_loader_preflight_checksum(volatile const struct stage45_loader_preflight *preflight)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)preflight;
    uint32_t count = (uint32_t)(offsetof(struct stage45_loader_preflight, checksum) / sizeof(uint32_t));
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

static uint32_t stage45_dt_semantic_mask(struct boot_args *args)
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
        mask |= STAGE45_DT_READY_BINARY_SELFTEST;
    }
    if (chosen) {
        mask |= STAGE45_DT_READY_CHOSEN;
        if (apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, chosen, "boot-args", &len) && len != 0u) {
            mask |= STAGE45_DT_READY_BOOT_ARGS;
        }
        if (apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, chosen, "ram-console-reg", &len) && len >= 8u) {
            mask |= STAGE45_DT_READY_RAM_CONSOLE;
        }
    }
    if (memory && apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, memory, "reg", &len) && len >= 8u) {
        mask |= STAGE45_DT_READY_MEMORY;
    }
    if (cpus && apple_dt_node_child_count(args->deviceTreeP, args->deviceTreeLength, cpus) != 0u) {
        const void *cpu0 = find_child(args, cpus, "cpu@0");
        mask |= STAGE45_DT_READY_CPUS;
        if (cpu0 &&
            apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu0, "clock-frequency", 0u) != 0u &&
            apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, cpu0, "timebase-frequency", 0u) == 19200000u) {
            mask |= STAGE45_DT_READY_CPU_CLOCKS;
        }
    }
    if (gic) {
        mask |= STAGE45_DT_READY_INTERRUPT_CONTROLLER;
    }
    if (timer && apple_dt_get_u32_prop(args->deviceTreeP, args->deviceTreeLength, timer, "frequency", 0u) == 19200000u) {
        mask |= STAGE45_DT_READY_TIMER;
    }
    if (device_tree) {
        mask |= STAGE45_DT_READY_DEVICE_TREE_NODE;
        if (apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, device_tree, "target-type", &len) && len != 0u) {
            mask |= STAGE45_DT_READY_TARGET_TYPE;
        }
        if (apple_dt_get_prop(args->deviceTreeP, args->deviceTreeLength, device_tree, "model", &len) && len != 0u) {
            mask |= STAGE45_DT_READY_MODEL;
        }
    }

    return mask;
}

int stage45_loader_preflight_run(struct boot_args *args)
{
    struct stage45_macho_probe_result probe;
    struct stage45_loader_preflight *preflight = &stage45_loader_preflight_block;
    uint32_t workspace_limit;
    uint32_t loaded_end_for_workspace;
    uint32_t macho_ok;

    xnu_log_puts("Stage45 Mach-O/XNU loader preflight begin\n");
    memset(preflight, 0, sizeof(*preflight));
    preflight->version = STAGE45_LOADER_PREFLIGHT_VERSION;
    preflight->size = sizeof(*preflight);
    preflight->xnu_baseline_tag = STAGE45_XNU_BASELINE_2050_22_13;
    preflight->xnu_baseline_commit = STAGE45_XNU_BASELINE_COMMIT_CC8A9B0C;
    preflight->xnu_master_version = STAGE45_XNU_BASELINE_MASTER_12_3_0;
    preflight->xnu_arm_reference = STAGE45_XNU_ARM_REFERENCE_4570_1_46;

    macho_ok = (uint32_t)stage45_macho_probe(stage45_embedded_macho, stage45_embedded_macho_size, &probe);
    if (macho_ok) {
        preflight->satisfied_mask |= STAGE45_LOADER_SAT_MACHO;
    }
    if (macho_ok && stage45_macho_materialize_to_arena(stage45_embedded_macho, stage45_embedded_macho_size, &probe)) {
        preflight->satisfied_mask |= STAGE45_LOADER_SAT_MATERIALIZED;
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
    preflight->proposed_topOfKernelData = align_up_u32(loaded_end_for_workspace, STAGE45_XNU_TTE_ALIGNMENT);
    preflight->proposed_ttep_workspace_base = preflight->proposed_topOfKernelData;
    preflight->proposed_ttep_workspace_size = STAGE45_XNU_TTE_WORKSPACE_BYTES;
    workspace_limit = preflight->proposed_ttep_workspace_base + preflight->proposed_ttep_workspace_size;
    preflight->proposed_ttep_workspace_limit = workspace_limit;
    preflight->proposed_avail_start = workspace_limit;
    preflight->proposed_workspace_alignment = preflight->proposed_topOfKernelData & (STAGE45_XNU_TTE_ALIGNMENT - 1u);
    if (preflight->proposed_workspace_alignment == 0u &&
        workspace_limit > preflight->proposed_ttep_workspace_base &&
        workspace_limit < RAM_CONSOLE_BASE &&
        preflight->proposed_topOfKernelData >= loaded_end_for_workspace &&
        preflight->proposed_loaded_phys_base >= RAM_PHYS_BASE &&
        preflight->proposed_loaded_phys_end <= preflight->proposed_topOfKernelData) {
        preflight->proposed_workspace_range_ok = 1u;
    }

    preflight->apple_dt_semantic_mask = stage45_dt_semantic_mask(args);
    preflight->pexpert_gap_mask = STAGE45_PLATFORM_GAP_PEXPERT_IMPL;
    preflight->platform_gap_mask = STAGE45_PLATFORM_GAP_REQUIRED_RECORDED;
    preflight->interrupt_ready_mask = STAGE45_LOADER_IRQ_READY_REQUIRED;
    preflight->safety_mask = STAGE45_LOADER_SAFETY_REQUIRED;

    if (preflight->xnu_baseline_tag == STAGE45_XNU_BASELINE_2050_22_13 &&
        preflight->xnu_baseline_commit == STAGE45_XNU_BASELINE_COMMIT_CC8A9B0C &&
        preflight->xnu_master_version == STAGE45_XNU_BASELINE_MASTER_12_3_0 &&
        preflight->xnu_arm_reference == STAGE45_XNU_ARM_REFERENCE_4570_1_46) {
        preflight->satisfied_mask |= STAGE45_LOADER_SAT_BASELINE;
    }
    if (preflight->boot_args_rev_ver == 0x00020002u &&
        preflight->boot_args_ptr != 0u && preflight->device_tree_ptr != 0u &&
        preflight->actual_memSize == (RAM_CONSOLE_BASE - RAM_PHYS_BASE)) {
        preflight->satisfied_mask |= STAGE45_LOADER_SAT_BOOT_ARGS;
    }
    if (preflight->proposed_workspace_range_ok == 1u) {
        preflight->satisfied_mask |= STAGE45_LOADER_SAT_WORKSPACE;
    }
    if (preflight->apple_dt_semantic_mask == STAGE45_DT_READY_REQUIRED) {
        preflight->satisfied_mask |= STAGE45_LOADER_SAT_DT;
    }
    if (preflight->platform_gap_mask == STAGE45_PLATFORM_GAP_REQUIRED_RECORDED) {
        preflight->satisfied_mask |= STAGE45_LOADER_SAT_PLATFORM_GAPS;
    }
    if (preflight->interrupt_ready_mask == STAGE45_LOADER_IRQ_READY_REQUIRED) {
        preflight->satisfied_mask |= STAGE45_LOADER_SAT_INTERRUPTS;
    }
    if (preflight->safety_mask == STAGE45_LOADER_SAFETY_REQUIRED) {
        preflight->satisfied_mask |= STAGE45_LOADER_SAT_SAFETY;
    }

    preflight->readiness_mask = preflight->satisfied_mask;
    preflight->failure_mask = probe.failure_mask | probe.staging_plan.failure_mask;
    if (preflight->satisfied_mask != STAGE45_LOADER_SAT_REQUIRED) {
        preflight->failure_mask |= 0x80000000u;
    }
    preflight->checksum = stage45_loader_preflight_checksum(preflight);
    preflight->status = (preflight->satisfied_mask == STAGE45_LOADER_SAT_REQUIRED && preflight->failure_mask == 0u &&
        preflight->checksum == stage45_loader_preflight_checksum(preflight)) ? STAGE45_STATUS_OK : (STAGE45_STATUS_BASE | preflight->failure_mask);

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
    xnu_log_kv32("platform_gap_mask", preflight->platform_gap_mask);
    xnu_log_kv32("loader_interrupt_ready_mask", preflight->interrupt_ready_mask);
    xnu_log_kv32("loader_safety_mask", preflight->safety_mask);
    xnu_log_kv32("loader_satisfied_mask", preflight->satisfied_mask);
    xnu_log_kv32("loader_checksum", preflight->checksum);
    xnu_log_kv32("loader_status", preflight->status);
    xnu_log_puts("Stage45 XNU handoff disabled: materialized only into local BSS arena; no Mach-O entry executed, no physical writes, no TTBR switch, caches unchanged\n");

    if (preflight->status == STAGE45_STATUS_OK) {
        xnu_log_puts("Stage45 Mach-O/XNU loader preflight ok\n");
        return 1;
    }

    xnu_log_puts("Stage45 Mach-O/XNU loader preflight failed\n");
    return 0;
}
