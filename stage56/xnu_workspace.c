#include "stage56.h"
#include "xnu_workspace_generated.h"

static struct stage56_xnu_workspace g_stage56_xnu_workspace;

static uint32_t stage56_xnu_workspace_checksum(volatile const struct stage56_xnu_workspace *ws)
{
    volatile const uint32_t *words = (volatile const uint32_t *)(uintptr_t)ws;
    uint32_t count = (uint32_t)(offsetof(struct stage56_xnu_workspace, checksum) / sizeof(uint32_t));
    uint32_t checksum = 0;

    for (uint32_t i = 0; i < count; i++) {
        checksum ^= words[i];
    }

    return checksum;
}

static void stage56_xnu_workspace_log(const struct stage56_xnu_workspace *ws)
{
    xnu_log_kv32("stage56_xnu_workspace_status", ws->status);
    xnu_log_kv32("stage56_xnu_workspace_required_mask", ws->required_mask);
    xnu_log_kv32("stage56_xnu_workspace_satisfied_mask", ws->satisfied_mask);
    xnu_log_kv32("stage56_xnu_workspace_failure_mask", ws->failure_mask);
    xnu_log_kv32("stage56_xnu_workspace_checksum", ws->checksum);
    xnu_log_kv32("stage56_xnu_baseline_commit32", ws->baseline_commit32);
    xnu_log_kv32("stage56_xnu_master_version", ws->baseline_master_version);
    xnu_log_kv32("stage56_xnu_arm_reference", ws->arm_reference_4570_1_46);
    xnu_log_kv32("stage56_target_cancro", ws->cancro_target_declared);
    xnu_log_kv32("stage56_target_armv7", ws->armv7_target_declared);
    xnu_log_kv32("stage56_target_msm8974", ws->msm8974_target_declared);
    xnu_log_kv32("stage56_public_only", ws->public_only);
    xnu_log_kv32("stage56_public_2050_arm_gap_recorded", ws->public_2050_arm_gap_recorded);
    xnu_log_kv32("stage56_stage56_plan_ready", ws->stage56_plan_ready);
    xnu_log_kv32("stage56_no_full_xnu_build", ws->no_full_xnu_build);
    xnu_log_kv32("stage56_no_xnu_exec", ws->no_xnu_exec);
    xnu_log_kv32("stage56_no_macho_exec", ws->no_macho_exec);
    xnu_log_kv32("stage56_no_proposed_phys_write", ws->no_proposed_phys_write);
    xnu_log_kv32("stage56_no_proposed_tte_write", ws->no_proposed_tte_write);
    xnu_log_kv32("stage56_no_cache_change", ws->no_cache_change);
    xnu_log_kv32("stage56_no_persist_write", ws->no_persist_write);
    xnu_log_kv32("stage56_external_checkout_mutated", ws->external_checkout_mutated);
}

int stage56_xnu_workspace_selftest(void)
{
    struct stage56_xnu_workspace *ws = &g_stage56_xnu_workspace;

    memset(ws, 0, sizeof(*ws));
    ws->version = STAGE56_XNU_WORKSPACE_VERSION;
    ws->size = sizeof(*ws);
    ws->required_mask = STAGE56_XNU_WS_REQUIRED_MASK;
    ws->satisfied_mask = STAGE56_XNU_WS_HOST_SATISFIED_MASK;
    ws->failure_mask = STAGE56_XNU_WS_HOST_FAILURE_MASK;
    ws->baseline_commit32 = STAGE56_XNU_WS_BASELINE_COMMIT32;
    ws->baseline_master_version = STAGE56_XNU_WS_MASTER_VERSION_12_3_0;
    ws->arm_reference_4570_1_46 = STAGE56_XNU_WS_ARM_REFERENCE_4570_1_46;
    ws->cancro_target_declared = STAGE56_XNU_WS_HOST_TARGET_CANCRO;
    ws->armv7_target_declared = STAGE56_XNU_WS_HOST_TARGET_ARMV7;
    ws->msm8974_target_declared = STAGE56_XNU_WS_HOST_TARGET_MSM8974;
    ws->public_only = STAGE56_XNU_WS_HOST_PUBLIC_ONLY;
    ws->no_full_xnu_build = STAGE56_XNU_WS_HOST_NO_FULL_XNU_BUILD;
    ws->no_xnu_exec = STAGE56_XNU_WS_HOST_NO_XNU_EXEC;
    ws->no_macho_exec = STAGE56_XNU_WS_HOST_NO_MACHO_EXEC;
    ws->no_proposed_phys_write = STAGE56_XNU_WS_HOST_NO_PROPOSED_PHYS_WRITE;
    ws->no_proposed_tte_write = STAGE56_XNU_WS_HOST_NO_PROPOSED_TTE_WRITE;
    ws->no_cache_change = STAGE56_XNU_WS_HOST_NO_CACHE_CHANGE;
    ws->no_persist_write = STAGE56_XNU_WS_HOST_NO_PERSIST_WRITE;
    ws->stage56_plan_ready = STAGE56_XNU_WS_HOST_STAGE56_PLAN_READY;
    ws->public_2050_arm_gap_recorded = STAGE56_XNU_WS_HOST_ARM_GAP_RECORDED;
    ws->external_checkout_mutated = STAGE56_XNU_WS_HOST_EXTERNAL_MUTATED;

    if (STAGE56_XNU_WS_HOST_STATUS != STAGE56_STATUS_OK ||
        ws->required_mask != STAGE56_XNU_WS_HOST_REQUIRED_MASK ||
        ws->satisfied_mask != ws->required_mask || ws->failure_mask != 0u) {
        ws->failure_mask |= STAGE56_XNU_WS_FAIL_SAFETY_BOUNDARY;
    }
    if (ws->baseline_commit32 != STAGE56_XNU_BASELINE_COMMIT_CC8A9B0C) {
        ws->failure_mask |= STAGE56_XNU_WS_FAIL_2050_REF;
    }
    if (ws->baseline_master_version != STAGE56_XNU_BASELINE_MASTER_12_3_0) {
        ws->failure_mask |= STAGE56_XNU_WS_FAIL_MASTER_VERSION;
    }
    if (ws->arm_reference_4570_1_46 != STAGE56_XNU_ARM_REFERENCE_4570_1_46) {
        ws->failure_mask |= STAGE56_XNU_WS_FAIL_4570_ARM_REFS;
    }
    if (ws->cancro_target_declared != 1u || ws->armv7_target_declared != 1u ||
        ws->msm8974_target_declared != 1u) {
        ws->failure_mask |= STAGE56_XNU_WS_FAIL_CANCRO_TARGET;
    }
    if (ws->stage56_plan_ready != 1u || ws->public_2050_arm_gap_recorded != 1u) {
        ws->failure_mask |= STAGE56_XNU_WS_FAIL_STAGE56_PLAN;
    }
    if (ws->public_only != 1u || ws->no_full_xnu_build != 1u || ws->no_xnu_exec != 1u ||
        ws->no_macho_exec != 1u || ws->no_proposed_phys_write != 1u ||
        ws->no_proposed_tte_write != 1u || ws->no_cache_change != 1u ||
        ws->no_persist_write != 1u || ws->external_checkout_mutated != 0u) {
        ws->failure_mask |= STAGE56_XNU_WS_FAIL_SAFETY_BOUNDARY;
    }

    ws->status = (ws->satisfied_mask == ws->required_mask && ws->failure_mask == 0u) ?
        STAGE56_STATUS_OK : (STAGE56_STATUS_BASE | ws->failure_mask);
    ws->checksum = stage56_xnu_workspace_checksum(ws);

    stage56_xnu_workspace_log(ws);
    return ws->status == STAGE56_STATUS_OK;
}

const struct stage56_xnu_workspace *stage56_xnu_workspace_result(void)
{
    return &g_stage56_xnu_workspace;
}
