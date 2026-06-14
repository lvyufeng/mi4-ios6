#include "stage83.h"
#include "xnu_link_generated.h"

static struct stage83_xnu_link g_stage83_xnu_link;

static uint32_t stage83_xnu_link_checksum(volatile const struct stage83_xnu_link *link)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)link;
    uint32_t count = (uint32_t)(offsetof(struct stage83_xnu_link, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static void stage83_xnu_link_log(const struct stage83_xnu_link *link)
{
    xnu_log_kv32("stage83_xnu_link_status", link->status);
    xnu_log_kv32("stage83_xnu_link_required_mask", link->required_mask);
    xnu_log_kv32("stage83_xnu_link_satisfied_mask", link->satisfied_mask);
    xnu_log_kv32("stage83_xnu_link_failure_mask", link->failure_mask);
    xnu_log_kv32("stage83_xnu_link_checksum", link->checksum);
    xnu_log_kv32("stage83_xnu_link_object_count", link->linked_object_count);
    xnu_log_kv32("stage83_xnu_link_support_object_count", link->support_object_count);
    xnu_log_kv32("stage83_xnu_link_undefined_symbol_count", link->undefined_symbol_count);
    xnu_log_kv32("stage83_xnu_link_global_symbol_count", link->global_symbol_count);
    xnu_log_kv32("stage83_xnu_link_elf_bytes", link->elf_bytes);
    xnu_log_kv32("stage83_xnu_link_elf_sha32", link->elf_sha32);
    xnu_log_kv32("stage83_xnu_link_text_addr", link->text_addr);
    xnu_log_kv32("stage83_xnu_link_text_size", link->text_size);
    xnu_log_kv32("stage83_xnu_link_data_addr", link->data_addr);
    xnu_log_kv32("stage83_xnu_link_data_size", link->data_size);
    xnu_log_kv32("stage83_xnu_link_bss_addr", link->bss_addr);
    xnu_log_kv32("stage83_xnu_link_bss_size", link->bss_size);
    xnu_log_kv32("stage83_xnu_link_baseline_commit32", link->baseline_commit32);
    xnu_log_kv32("stage83_xnu_link_master_version", link->baseline_master_version);
    xnu_log_kv32("stage83_xnu_link_public_only", link->public_only);
    xnu_log_kv32("stage83_xnu_link_no_full_xnu_build", link->no_full_xnu_build);
    xnu_log_kv32("stage83_xnu_link_no_public_xnu_exec", link->no_public_xnu_exec);
    xnu_log_kv32("stage83_xnu_link_no_macho_exec", link->no_macho_exec);
    xnu_log_kv32("stage83_xnu_link_no_platform_runtime_exec", link->no_platform_runtime_exec);
    xnu_log_kv32("stage83_xnu_link_no_external_mutation", link->no_external_mutation);
    xnu_log_kv32("stage83_xnu_link_outputs_ignored", link->outputs_ignored);
    xnu_log_kv32("stage83_xnu_link_fail_closed", link->fail_closed);
}

int stage83_xnu_link_selftest(void)
{
    struct stage83_xnu_link *link = &g_stage83_xnu_link;

    memset(link, 0, sizeof(*link));
    link->version = STAGE83_XNU_LINK_VERSION;
    link->size = sizeof(*link);
    link->required_mask = STAGE83_XNU_LINK_REQUIRED_MASK;
    link->satisfied_mask = STAGE83_XNU_LINK_HOST_SATISFIED_MASK;
    link->failure_mask = STAGE83_XNU_LINK_HOST_FAILURE_MASK;
    link->linked_object_count = STAGE83_XNU_LINK_HOST_LINKED_OBJECT_COUNT;
    link->support_object_count = STAGE83_XNU_LINK_HOST_SUPPORT_OBJECT_COUNT;
    link->undefined_symbol_count = STAGE83_XNU_LINK_HOST_UNDEFINED_SYMBOL_COUNT;
    link->global_symbol_count = STAGE83_XNU_LINK_HOST_GLOBAL_SYMBOL_COUNT;
    link->elf_bytes = STAGE83_XNU_LINK_HOST_ELF_BYTES;
    link->elf_sha32 = STAGE83_XNU_LINK_HOST_ELF_SHA32;
    link->text_addr = STAGE83_XNU_LINK_HOST_TEXT_ADDR;
    link->text_size = STAGE83_XNU_LINK_HOST_TEXT_SIZE;
    link->data_addr = STAGE83_XNU_LINK_HOST_DATA_ADDR;
    link->data_size = STAGE83_XNU_LINK_HOST_DATA_SIZE;
    link->bss_addr = STAGE83_XNU_LINK_HOST_BSS_ADDR;
    link->bss_size = STAGE83_XNU_LINK_HOST_BSS_SIZE;
    link->baseline_commit32 = STAGE83_XNU_LINK_HOST_BASELINE_COMMIT32;
    link->baseline_master_version = STAGE83_XNU_LINK_HOST_MASTER_VERSION_12_3_0;
    link->public_only = STAGE83_XNU_LINK_HOST_PUBLIC_ONLY;
    link->no_full_xnu_build = STAGE83_XNU_LINK_HOST_NO_FULL_XNU_BUILD;
    link->no_public_xnu_exec = STAGE83_XNU_LINK_HOST_NO_PUBLIC_XNU_EXEC;
    link->no_macho_exec = STAGE83_XNU_LINK_HOST_NO_MACHO_EXEC;
    link->no_platform_runtime_exec = STAGE83_XNU_LINK_HOST_NO_PLATFORM_RUNTIME_EXEC;
    link->no_external_mutation = STAGE83_XNU_LINK_HOST_NO_EXTERNAL_MUTATION;
    link->outputs_ignored = STAGE83_XNU_LINK_HOST_OUTPUTS_IGNORED;
    link->fail_closed = STAGE83_XNU_LINK_HOST_FAIL_CLOSED;

    if (STAGE83_XNU_LINK_HOST_STATUS != STAGE83_STATUS_OK ||
        link->required_mask != STAGE83_XNU_LINK_HOST_REQUIRED_MASK ||
        link->satisfied_mask != link->required_mask || link->failure_mask != 0u) {
        link->failure_mask |= STAGE83_XNU_LINK_FAIL_SAFETY_BOUNDARY;
    }
    if (link->linked_object_count != 6u) {
        link->failure_mask |= STAGE83_XNU_LINK_FAIL_OBJECTS;
    }
    if (link->support_object_count != 2u) {
        link->failure_mask |= STAGE83_XNU_LINK_FAIL_SUPPORT;
    }
    if (link->undefined_symbol_count != 0u) {
        link->failure_mask |= STAGE83_XNU_LINK_FAIL_UNDEFINEDS;
    }
    if (link->elf_bytes == 0u || link->global_symbol_count == 0u || link->text_size == 0u) {
        link->failure_mask |= STAGE83_XNU_LINK_FAIL_LINK | STAGE83_XNU_LINK_FAIL_LAYOUT;
    }
    if (link->baseline_commit32 != STAGE83_XNU_BASELINE_COMMIT_CC8A9B0C ||
        link->baseline_master_version != STAGE83_XNU_BASELINE_MASTER_12_3_0) {
        link->failure_mask |= STAGE83_XNU_LINK_FAIL_PUBLIC_BASELINE;
    }
    if (link->public_only != 1u || link->no_full_xnu_build != 1u ||
        link->no_public_xnu_exec != 1u || link->no_macho_exec != 1u ||
        link->no_platform_runtime_exec != 1u || link->no_external_mutation != 1u ||
        link->outputs_ignored != 1u || link->fail_closed != 1u) {
        link->failure_mask |= STAGE83_XNU_LINK_FAIL_SAFETY_BOUNDARY;
    }

    link->status = (link->satisfied_mask == link->required_mask && link->failure_mask == 0u) ?
        STAGE83_STATUS_OK : STAGE83_STATUS_FAIL(link->failure_mask);
    link->checksum = stage83_xnu_link_checksum(link);

    stage83_xnu_link_log(link);
    return link->status == STAGE83_STATUS_OK;
}

const struct stage83_xnu_link *stage83_xnu_link_result(void)
{
    return &g_stage83_xnu_link;
}
