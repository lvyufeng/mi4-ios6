#ifndef MI4IOS6_STAGE84_SHIM_PEXPERT_ARM_CONSISTENT_DEBUG_H
#define MI4IOS6_STAGE84_SHIM_PEXPERT_ARM_CONSISTENT_DEBUG_H

#include <stdint.h>

#define DEBUG_RECORD_ID_LONG(a, b, c, d, e, f, g, h) \
    ((((uint64_t)((((h) << 24) & 0xff000000u) | \
                  (((g) << 16) & 0x00ff0000u) | \
                  (((f) <<  8) & 0x0000ff00u) | \
                  ((e)         & 0x000000ffu))) << 32) | \
     ((uint64_t)((((d) << 24) & 0xff000000u) | \
                 (((c) << 16) & 0x00ff0000u) | \
                 (((b) <<  8) & 0x0000ff00u) | \
                 ((a)         & 0x000000ffu))))
#define DEBUG_RECORD_ID_SHORT(a, b, c, d) DEBUG_RECORD_ID_LONG(a, b, c, d, 0, 0, 0, 0)

#define kDbgIdUnusedEntry   0x0ULL
#define kDbgIdReservedEntry DEBUG_RECORD_ID_LONG('R', 'E', 'S', 'E', 'R', 'V', 'E', 'D')

#define DEBUG_REGISTRY_MAX_RECORDS 512u
#define CPR_MAX_STATE_ENTRIES      16u

typedef struct {
    uint64_t record_id;
    uint32_t num_records;
    uint32_t record_size_bytes;
} dbg_top_level_header_t;

typedef struct {
    uint64_t record_id;
    uint64_t length;
    uint64_t physaddr;
} dbg_record_header_t;

typedef struct {
    uint64_t timestamp;
    uint32_t cp_state;
    uint32_t cp_state_arg;
} dbg_cpr_state_entry_t;

typedef struct {
    uint32_t rdptr;
    uint32_t wrptr;
    uint32_t num_cp_state_entries;
    uint32_t checksum;
    dbg_cpr_state_entry_t cp_state_entries[CPR_MAX_STATE_ENTRIES];
} dbg_cpr_t;

typedef struct {
    dbg_top_level_header_t top_level_header;
    dbg_record_header_t records[DEBUG_REGISTRY_MAX_RECORDS];
    dbg_cpr_t ap_cpr_region;
} dbg_registry_t;

int PE_consistent_debug_inherit(void);
int PE_consistent_debug_register(uint64_t record_id, uint64_t physaddr, uint64_t length);
int PE_consistent_debug_enabled(void);

#endif
