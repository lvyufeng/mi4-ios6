#include "stage77.h"
#include "xnu_object_subset_generated.h"

static struct stage77_xnu_object_subset g_stage77_xnu_object_subset;

static uint32_t stage77_xnu_object_subset_checksum(volatile const struct stage77_xnu_object_subset *obj)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)obj;
    uint32_t count = (uint32_t)(offsetof(struct stage77_xnu_object_subset, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static void stage77_xnu_object_subset_log(const struct stage77_xnu_object_subset *obj)
{
    xnu_log_kv32("stage77_xnu_object_subset_status", obj->status);
    xnu_log_kv32("stage77_xnu_object_subset_required_mask", obj->required_mask);
    xnu_log_kv32("stage77_xnu_object_subset_satisfied_mask", obj->satisfied_mask);
    xnu_log_kv32("stage77_xnu_object_subset_failure_mask", obj->failure_mask);
    xnu_log_kv32("stage77_xnu_object_subset_checksum", obj->checksum);
    xnu_log_kv32("stage77_xnu_object_source_mask", obj->source_mask);
    xnu_log_kv32("stage77_xnu_object_shim_mask", obj->shim_mask);
    xnu_log_kv32("stage77_xnu_object_count", obj->object_count);
    xnu_log_kv32("stage77_xnu_object_public_2050_count", obj->public_2050_object_count);
    xnu_log_kv32("stage77_xnu_object_public_arm_pexpert_count", obj->public_arm_pexpert_object_count);
    xnu_log_kv32("stage77_xnu_object_stage_owned_shim_count", obj->stage_owned_shim_object_count);
    xnu_log_kv32("stage77_xnu_object_duplicate_symbol_count", obj->duplicate_symbol_count);
    xnu_log_kv32("stage77_xnu_object_pe_state_abi_shim_ready", obj->pe_state_abi_shim_ready);
    xnu_log_kv32("stage77_xnu_object_consistent_debug_abi_shim_ready", obj->consistent_debug_abi_shim_ready);
    xnu_log_kv32("stage77_xnu_object_baseline_commit32", obj->baseline_commit32);
    xnu_log_kv32("stage77_xnu_object_master_version", obj->baseline_master_version);
    xnu_log_kv32("stage77_xnu_object_device_tree_bytes", obj->device_tree_object_bytes);
    xnu_log_kv32("stage77_xnu_object_bootargs_bytes", obj->bootargs_object_bytes);
    xnu_log_kv32("stage77_xnu_object_pe_gen_bytes", obj->pe_gen_object_bytes);
    xnu_log_kv32("stage77_xnu_object_arm_pe_bootargs_bytes", obj->arm_pe_bootargs_object_bytes);
    xnu_log_kv32("stage77_xnu_object_arm_pe_consistent_debug_bytes", obj->arm_pe_consistent_debug_object_bytes);
    xnu_log_kv32("stage77_xnu_object_device_tree_sha32", obj->device_tree_object_sha32);
    xnu_log_kv32("stage77_xnu_object_bootargs_sha32", obj->bootargs_object_sha32);
    xnu_log_kv32("stage77_xnu_object_pe_gen_sha32", obj->pe_gen_object_sha32);
    xnu_log_kv32("stage77_xnu_object_arm_pe_bootargs_sha32", obj->arm_pe_bootargs_object_sha32);
    xnu_log_kv32("stage77_xnu_object_arm_pe_consistent_debug_sha32", obj->arm_pe_consistent_debug_object_sha32);
    xnu_log_kv32("stage77_xnu_object_public_2050_baseline", obj->public_2050_baseline);
    xnu_log_kv32("stage77_xnu_object_public_only", obj->public_only);
    xnu_log_kv32("stage77_xnu_object_no_full_xnu_build", obj->no_full_xnu_build);
    xnu_log_kv32("stage77_xnu_object_no_macho_link", obj->no_macho_link);
    xnu_log_kv32("stage77_xnu_object_no_public_xnu_exec", obj->no_public_xnu_exec);
    xnu_log_kv32("stage77_xnu_object_no_platform_runtime_exec", obj->no_platform_runtime_exec);
    xnu_log_kv32("stage77_xnu_object_no_external_mutation", obj->no_external_mutation);
    xnu_log_kv32("stage77_xnu_object_outputs_ignored", obj->outputs_ignored);
    xnu_log_kv32("stage77_xnu_object_fail_closed", obj->fail_closed);
}

int stage77_xnu_object_subset_selftest(void)
{
    struct stage77_xnu_object_subset *obj = &g_stage77_xnu_object_subset;

    memset(obj, 0, sizeof(*obj));
    obj->version = STAGE77_XNU_OBJECT_SUBSET_VERSION;
    obj->size = sizeof(*obj);
    obj->required_mask = STAGE77_XNU_OBJ_REQUIRED_MASK;
    obj->satisfied_mask = STAGE77_XNU_OBJ_HOST_SATISFIED_MASK;
    obj->failure_mask = STAGE77_XNU_OBJ_HOST_FAILURE_MASK;
    obj->source_mask = STAGE77_XNU_OBJ_HOST_SOURCE_MASK;
    obj->shim_mask = STAGE77_XNU_OBJ_HOST_SHIM_MASK;
    obj->object_count = STAGE77_XNU_OBJ_HOST_OBJECT_COUNT;
    obj->public_2050_object_count = STAGE77_XNU_OBJ_HOST_PUBLIC_2050_OBJECT_COUNT;
    obj->public_arm_pexpert_object_count = STAGE77_XNU_OBJ_HOST_PUBLIC_ARM_PEXPERT_OBJECT_COUNT;
    obj->stage_owned_shim_object_count = STAGE77_XNU_OBJ_HOST_STAGE_OWNED_SHIM_OBJECT_COUNT;
    obj->duplicate_symbol_count = STAGE77_XNU_OBJ_HOST_DUPLICATE_SYMBOL_COUNT;
    obj->pe_state_abi_shim_ready = STAGE77_XNU_OBJ_HOST_PE_STATE_ABI_SHIM_READY;
    obj->consistent_debug_abi_shim_ready = STAGE77_XNU_OBJ_HOST_CONSISTENT_DEBUG_ABI_SHIM_READY;
    obj->baseline_commit32 = STAGE77_XNU_OBJ_HOST_BASELINE_COMMIT32;
    obj->baseline_master_version = STAGE77_XNU_OBJ_HOST_MASTER_VERSION_12_3_0;
    obj->device_tree_object_bytes = STAGE77_XNU_OBJ_HOST_DEVICE_TREE_BYTES;
    obj->bootargs_object_bytes = STAGE77_XNU_OBJ_HOST_BOOTARGS_BYTES;
    obj->pe_gen_object_bytes = STAGE77_XNU_OBJ_HOST_PE_GEN_BYTES;
    obj->arm_pe_bootargs_object_bytes = STAGE77_XNU_OBJ_HOST_ARM_PE_BOOTARGS_BYTES;
    obj->arm_pe_consistent_debug_object_bytes = STAGE77_XNU_OBJ_HOST_ARM_PE_CONSISTENT_DEBUG_BYTES;
    obj->device_tree_object_sha32 = STAGE77_XNU_OBJ_HOST_DEVICE_TREE_SHA32;
    obj->bootargs_object_sha32 = STAGE77_XNU_OBJ_HOST_BOOTARGS_SHA32;
    obj->pe_gen_object_sha32 = STAGE77_XNU_OBJ_HOST_PE_GEN_SHA32;
    obj->arm_pe_bootargs_object_sha32 = STAGE77_XNU_OBJ_HOST_ARM_PE_BOOTARGS_SHA32;
    obj->arm_pe_consistent_debug_object_sha32 = STAGE77_XNU_OBJ_HOST_ARM_PE_CONSISTENT_DEBUG_SHA32;
    obj->public_2050_baseline = STAGE77_XNU_OBJ_HOST_PUBLIC_2050_BASELINE;
    obj->public_only = STAGE77_XNU_OBJ_HOST_PUBLIC_ONLY;
    obj->no_full_xnu_build = STAGE77_XNU_OBJ_HOST_NO_FULL_XNU_BUILD;
    obj->no_macho_link = STAGE77_XNU_OBJ_HOST_NO_MACHO_LINK;
    obj->no_public_xnu_exec = STAGE77_XNU_OBJ_HOST_NO_PUBLIC_XNU_EXEC;
    obj->no_platform_runtime_exec = STAGE77_XNU_OBJ_HOST_NO_PLATFORM_RUNTIME_EXEC;
    obj->no_external_mutation = STAGE77_XNU_OBJ_HOST_NO_EXTERNAL_MUTATION;
    obj->outputs_ignored = STAGE77_XNU_OBJ_HOST_OUTPUTS_IGNORED;
    obj->fail_closed = STAGE77_XNU_OBJ_HOST_FAIL_CLOSED;

    if (STAGE77_XNU_OBJ_HOST_STATUS != STAGE77_STATUS_OK ||
        obj->required_mask != STAGE77_XNU_OBJ_HOST_REQUIRED_MASK ||
        obj->satisfied_mask != obj->required_mask || obj->failure_mask != 0u) {
        obj->failure_mask |= STAGE77_XNU_OBJ_FAIL_SAFETY_BOUNDARY;
    }
    if ((obj->source_mask & STAGE77_XNU_OBJ_SOURCE_MASK_REQUIRED) != STAGE77_XNU_OBJ_SOURCE_MASK_REQUIRED) {
        obj->failure_mask |= STAGE77_XNU_OBJ_FAIL_DEVICE_TREE_SOURCE |
            STAGE77_XNU_OBJ_FAIL_BOOTARGS_SOURCE | STAGE77_XNU_OBJ_FAIL_PE_GEN_SOURCE |
            STAGE77_XNU_OBJ_FAIL_ARM_PE_BOOTARGS_SOURCE |
            STAGE77_XNU_OBJ_FAIL_ARM_PE_CONSISTENT_DEBUG_SOURCE;
    }
    if ((obj->shim_mask & STAGE77_XNU_OBJ_SHIM_REQUIRED) != STAGE77_XNU_OBJ_SHIM_REQUIRED) {
        obj->failure_mask |= STAGE77_XNU_OBJ_FAIL_SHIMS;
    }
    if (obj->object_count != 6u || obj->public_2050_object_count != 3u ||
        obj->public_arm_pexpert_object_count != 2u || obj->stage_owned_shim_object_count != 1u ||
        obj->device_tree_object_bytes == 0u || obj->bootargs_object_bytes == 0u ||
        obj->pe_gen_object_bytes == 0u || obj->arm_pe_bootargs_object_bytes == 0u ||
        obj->arm_pe_consistent_debug_object_bytes == 0u) {
        obj->failure_mask |= STAGE77_XNU_OBJ_FAIL_OBJECTS;
    }
    if (obj->duplicate_symbol_count != 0u) {
        obj->failure_mask |= STAGE77_XNU_OBJ_FAIL_DUPLICATE_SYMBOLS;
    }
    if (obj->pe_state_abi_shim_ready != 1u) {
        obj->failure_mask |= STAGE77_XNU_OBJ_FAIL_PE_STATE_ABI;
    }
    if (obj->consistent_debug_abi_shim_ready != 1u) {
        obj->failure_mask |= STAGE77_XNU_OBJ_FAIL_CONSISTENT_DEBUG_ABI;
    }
    if (obj->baseline_commit32 != STAGE77_XNU_BASELINE_COMMIT_CC8A9B0C ||
        obj->baseline_master_version != STAGE77_XNU_BASELINE_MASTER_12_3_0 ||
        obj->public_2050_baseline != 1u) {
        obj->failure_mask |= STAGE77_XNU_OBJ_FAIL_PUBLIC_BASELINE;
    }
    if (obj->public_only != 1u || obj->no_full_xnu_build != 1u || obj->no_macho_link != 1u ||
        obj->no_public_xnu_exec != 1u || obj->no_platform_runtime_exec != 1u ||
        obj->no_external_mutation != 1u || obj->outputs_ignored != 1u || obj->fail_closed != 1u) {
        obj->failure_mask |= STAGE77_XNU_OBJ_FAIL_SAFETY_BOUNDARY;
    }

    obj->status = (obj->satisfied_mask == obj->required_mask && obj->failure_mask == 0u) ?
        STAGE77_STATUS_OK : (STAGE77_STATUS_BASE | obj->failure_mask);
    obj->checksum = stage77_xnu_object_subset_checksum(obj);

    stage77_xnu_object_subset_log(obj);
    return obj->status == STAGE77_STATUS_OK;
}

const struct stage77_xnu_object_subset *stage77_xnu_object_subset_result(void)
{
    return &g_stage77_xnu_object_subset;
}
