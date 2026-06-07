#include "stage53.h"
#include "xnu_object_subset_generated.h"

static struct stage53_xnu_object_subset g_stage53_xnu_object_subset;

static uint32_t stage53_xnu_object_subset_checksum(volatile const struct stage53_xnu_object_subset *obj)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)obj;
    uint32_t count = (uint32_t)(offsetof(struct stage53_xnu_object_subset, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static void stage53_xnu_object_subset_log(const struct stage53_xnu_object_subset *obj)
{
    xnu_log_kv32("stage53_xnu_object_subset_status", obj->status);
    xnu_log_kv32("stage53_xnu_object_subset_required_mask", obj->required_mask);
    xnu_log_kv32("stage53_xnu_object_subset_satisfied_mask", obj->satisfied_mask);
    xnu_log_kv32("stage53_xnu_object_subset_failure_mask", obj->failure_mask);
    xnu_log_kv32("stage53_xnu_object_subset_checksum", obj->checksum);
    xnu_log_kv32("stage53_xnu_object_source_mask", obj->source_mask);
    xnu_log_kv32("stage53_xnu_object_shim_mask", obj->shim_mask);
    xnu_log_kv32("stage53_xnu_object_count", obj->object_count);
    xnu_log_kv32("stage53_xnu_object_baseline_commit32", obj->baseline_commit32);
    xnu_log_kv32("stage53_xnu_object_master_version", obj->baseline_master_version);
    xnu_log_kv32("stage53_xnu_object_device_tree_bytes", obj->device_tree_object_bytes);
    xnu_log_kv32("stage53_xnu_object_bootargs_bytes", obj->bootargs_object_bytes);
    xnu_log_kv32("stage53_xnu_object_pe_gen_bytes", obj->pe_gen_object_bytes);
    xnu_log_kv32("stage53_xnu_object_device_tree_sha32", obj->device_tree_object_sha32);
    xnu_log_kv32("stage53_xnu_object_bootargs_sha32", obj->bootargs_object_sha32);
    xnu_log_kv32("stage53_xnu_object_pe_gen_sha32", obj->pe_gen_object_sha32);
    xnu_log_kv32("stage53_xnu_object_public_2050_baseline", obj->public_2050_baseline);
    xnu_log_kv32("stage53_xnu_object_public_only", obj->public_only);
    xnu_log_kv32("stage53_xnu_object_no_full_xnu_build", obj->no_full_xnu_build);
    xnu_log_kv32("stage53_xnu_object_no_macho_link", obj->no_macho_link);
    xnu_log_kv32("stage53_xnu_object_no_public_xnu_exec", obj->no_public_xnu_exec);
    xnu_log_kv32("stage53_xnu_object_no_external_mutation", obj->no_external_mutation);
    xnu_log_kv32("stage53_xnu_object_outputs_ignored", obj->outputs_ignored);
    xnu_log_kv32("stage53_xnu_object_fail_closed", obj->fail_closed);
}

int stage53_xnu_object_subset_selftest(void)
{
    struct stage53_xnu_object_subset *obj = &g_stage53_xnu_object_subset;

    memset(obj, 0, sizeof(*obj));
    obj->version = STAGE53_XNU_OBJECT_SUBSET_VERSION;
    obj->size = sizeof(*obj);
    obj->required_mask = STAGE53_XNU_OBJ_REQUIRED_MASK;
    obj->satisfied_mask = STAGE53_XNU_OBJ_HOST_SATISFIED_MASK;
    obj->failure_mask = STAGE53_XNU_OBJ_HOST_FAILURE_MASK;
    obj->source_mask = STAGE53_XNU_OBJ_HOST_SOURCE_MASK;
    obj->shim_mask = STAGE53_XNU_OBJ_HOST_SHIM_MASK;
    obj->object_count = STAGE53_XNU_OBJ_HOST_OBJECT_COUNT;
    obj->baseline_commit32 = STAGE53_XNU_OBJ_HOST_BASELINE_COMMIT32;
    obj->baseline_master_version = STAGE53_XNU_OBJ_HOST_MASTER_VERSION_12_3_0;
    obj->device_tree_object_bytes = STAGE53_XNU_OBJ_HOST_DEVICE_TREE_BYTES;
    obj->bootargs_object_bytes = STAGE53_XNU_OBJ_HOST_BOOTARGS_BYTES;
    obj->pe_gen_object_bytes = STAGE53_XNU_OBJ_HOST_PE_GEN_BYTES;
    obj->device_tree_object_sha32 = STAGE53_XNU_OBJ_HOST_DEVICE_TREE_SHA32;
    obj->bootargs_object_sha32 = STAGE53_XNU_OBJ_HOST_BOOTARGS_SHA32;
    obj->pe_gen_object_sha32 = STAGE53_XNU_OBJ_HOST_PE_GEN_SHA32;
    obj->public_2050_baseline = STAGE53_XNU_OBJ_HOST_PUBLIC_2050_BASELINE;
    obj->public_only = STAGE53_XNU_OBJ_HOST_PUBLIC_ONLY;
    obj->no_full_xnu_build = STAGE53_XNU_OBJ_HOST_NO_FULL_XNU_BUILD;
    obj->no_macho_link = STAGE53_XNU_OBJ_HOST_NO_MACHO_LINK;
    obj->no_public_xnu_exec = STAGE53_XNU_OBJ_HOST_NO_PUBLIC_XNU_EXEC;
    obj->no_external_mutation = STAGE53_XNU_OBJ_HOST_NO_EXTERNAL_MUTATION;
    obj->outputs_ignored = STAGE53_XNU_OBJ_HOST_OUTPUTS_IGNORED;
    obj->fail_closed = STAGE53_XNU_OBJ_HOST_FAIL_CLOSED;

    if (STAGE53_XNU_OBJ_HOST_STATUS != STAGE53_STATUS_OK ||
        obj->required_mask != STAGE53_XNU_OBJ_HOST_REQUIRED_MASK ||
        obj->satisfied_mask != obj->required_mask || obj->failure_mask != 0u) {
        obj->failure_mask |= STAGE53_XNU_OBJ_FAIL_SAFETY_BOUNDARY;
    }
    if ((obj->source_mask & STAGE53_XNU_OBJ_SOURCE_MASK_REQUIRED) != STAGE53_XNU_OBJ_SOURCE_MASK_REQUIRED) {
        obj->failure_mask |= STAGE53_XNU_OBJ_FAIL_DEVICE_TREE_SOURCE |
            STAGE53_XNU_OBJ_FAIL_BOOTARGS_SOURCE | STAGE53_XNU_OBJ_FAIL_PE_GEN_SOURCE;
    }
    if ((obj->shim_mask & STAGE53_XNU_OBJ_SHIM_REQUIRED) != STAGE53_XNU_OBJ_SHIM_REQUIRED) {
        obj->failure_mask |= STAGE53_XNU_OBJ_FAIL_SHIMS;
    }
    if (obj->object_count != 4u || obj->device_tree_object_bytes == 0u ||
        obj->bootargs_object_bytes == 0u || obj->pe_gen_object_bytes == 0u) {
        obj->failure_mask |= STAGE53_XNU_OBJ_FAIL_OBJECTS;
    }
    if (obj->baseline_commit32 != STAGE53_XNU_BASELINE_COMMIT_CC8A9B0C ||
        obj->baseline_master_version != STAGE53_XNU_BASELINE_MASTER_12_3_0 ||
        obj->public_2050_baseline != 1u) {
        obj->failure_mask |= STAGE53_XNU_OBJ_FAIL_PUBLIC_BASELINE;
    }
    if (obj->public_only != 1u || obj->no_full_xnu_build != 1u || obj->no_macho_link != 1u ||
        obj->no_public_xnu_exec != 1u || obj->no_external_mutation != 1u ||
        obj->outputs_ignored != 1u || obj->fail_closed != 1u) {
        obj->failure_mask |= STAGE53_XNU_OBJ_FAIL_SAFETY_BOUNDARY;
    }

    obj->status = (obj->satisfied_mask == obj->required_mask && obj->failure_mask == 0u) ?
        STAGE53_STATUS_OK : (STAGE53_STATUS_BASE | obj->failure_mask);
    obj->checksum = stage53_xnu_object_subset_checksum(obj);

    stage53_xnu_object_subset_log(obj);
    return obj->status == STAGE53_STATUS_OK;
}

const struct stage53_xnu_object_subset *stage53_xnu_object_subset_result(void)
{
    return &g_stage53_xnu_object_subset;
}
