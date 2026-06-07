#ifndef MI4IOS6_STAGE54_XNU_WORKSPACE_H
#define MI4IOS6_STAGE54_XNU_WORKSPACE_H

#include <stdint.h>

#define STAGE54_XNU_WORKSPACE_VERSION              1u

#define STAGE54_XNU_WS_2050_PRESENT                0x00000001u
#define STAGE54_XNU_WS_2050_REF_MATCH              0x00000002u
#define STAGE54_XNU_WS_2050_MASTER_VERSION         0x00000004u
#define STAGE54_XNU_WS_2050_DARWIN12_DIRS          0x00000008u
#define STAGE54_XNU_WS_2050_ARM_GAP_RECORDED       0x00000010u
#define STAGE54_XNU_WS_4570_PRESENT                0x00000020u
#define STAGE54_XNU_WS_4570_ARM_REFS_PRESENT       0x00000040u
#define STAGE54_XNU_WS_CANCRO_TARGET_DECLARED      0x00000080u
#define STAGE54_XNU_WS_ARMV7_TARGET_DECLARED       0x00000100u
#define STAGE54_XNU_WS_MSM8974_TARGET_DECLARED     0x00000200u
#define STAGE54_XNU_WS_STAGE54_PLAN_READY          0x00000400u
#define STAGE54_XNU_WS_PUBLIC_ONLY                 0x00000800u
#define STAGE54_XNU_WS_NO_FULL_XNU_BUILD           0x00001000u
#define STAGE54_XNU_WS_NO_XNU_EXEC                 0x00002000u
#define STAGE54_XNU_WS_NO_MACHO_EXEC               0x00004000u
#define STAGE54_XNU_WS_NO_PROPOSED_PHYS_WRITE      0x00008000u
#define STAGE54_XNU_WS_NO_PROPOSED_TTE_WRITE       0x00010000u
#define STAGE54_XNU_WS_NO_CACHE_CHANGE             0x00020000u
#define STAGE54_XNU_WS_NO_PERSIST_WRITE            0x00040000u
#define STAGE54_XNU_WS_REQUIRED_MASK               0x0007ffffu

#define STAGE54_XNU_WS_FAIL_2050_MISSING           0x00000001u
#define STAGE54_XNU_WS_FAIL_2050_REF               0x00000002u
#define STAGE54_XNU_WS_FAIL_MASTER_VERSION         0x00000004u
#define STAGE54_XNU_WS_FAIL_2050_DIRS              0x00000008u
#define STAGE54_XNU_WS_FAIL_ARM_GAP_UNRECORDED     0x00000010u
#define STAGE54_XNU_WS_FAIL_4570_MISSING           0x00000020u
#define STAGE54_XNU_WS_FAIL_4570_ARM_REFS          0x00000040u
#define STAGE54_XNU_WS_FAIL_CANCRO_TARGET          0x00000080u
#define STAGE54_XNU_WS_FAIL_STAGE54_PLAN           0x00000100u
#define STAGE54_XNU_WS_FAIL_SAFETY_BOUNDARY        0x80000000u

struct stage54_xnu_workspace {
    uint32_t version;
    uint32_t size;
    uint32_t status;
    uint32_t required_mask;
    uint32_t satisfied_mask;
    uint32_t failure_mask;
    uint32_t baseline_commit32;
    uint32_t baseline_master_version;
    uint32_t arm_reference_4570_1_46;
    uint32_t cancro_target_declared;
    uint32_t armv7_target_declared;
    uint32_t msm8974_target_declared;
    uint32_t public_only;
    uint32_t no_full_xnu_build;
    uint32_t no_xnu_exec;
    uint32_t no_macho_exec;
    uint32_t no_proposed_phys_write;
    uint32_t no_proposed_tte_write;
    uint32_t no_cache_change;
    uint32_t no_persist_write;
    uint32_t stage54_plan_ready;
    uint32_t public_2050_arm_gap_recorded;
    uint32_t external_checkout_mutated;
    uint32_t checksum;
};

int stage54_xnu_workspace_selftest(void);
const struct stage54_xnu_workspace *stage54_xnu_workspace_result(void);

#endif
