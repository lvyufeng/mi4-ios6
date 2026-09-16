# Stage82 Full Kernel Virtual Address Space Pmap Design

## Overview

Stage82 extends Stage81's minimal candidate pmap (10 L1 section mappings) to a complete kernel virtual address space with L1+L2 two-level translation, supporting 4KB page granularity and high-virtual kernel code/data execution.

## ARMv7 Short-Descriptor Format Reference

From `external/xnu-4570.1.46/osfmk/arm/proc_reg.h`:

### L1 Descriptor (Translation Table Entry - TTE)

**L1 Section (1MB granularity)**:
```c
ARM_TTE_TYPE_BLOCK         0x00000002  // section entry type
ARM_TTE_BLOCK_L1_MASK      0xFFF00000  // phys addr mask (bits [31:20])
ARM_TTE_BLOCK_L1_SHIFT     20          // 1MB section shift

// Stage81 used this conservative descriptor:
#define L1_DESC_SECTION_SO  0x00010c02  // Section, Strongly-Ordered
// Breakdown:
//   [31:20] = PA[31:20]     physical address top 12 bits
//   [19]    = 0             nG (not Global) = 0 (global)
//   [18]    = 0             S (Shareable) = 0
//   [17]    = 1             nG (TEX[2]) for Strongly-Ordered
//   [16]    = 0             AP[2]
//   [15]    = 0             TEX[1]
//   [14:12] = 0             TEX[0], C, B = 000 for Strongly-Ordered
//   [11:10] = 11            AP[1:0] = full access
//   [9]     = 0             reserved
//   [8:5]   = 0000          domain = 0
//   [4]     = 0             XN (execute-never) = 0
//   [3]     = 0             C (cacheable) = 0
//   [2]     = 0             B (bufferable) = 0
//   [1:0]   = 10            section type
```

**L1 Page Table Pointer** (points to L2 table):
```c
ARM_TTE_TYPE_TABLE         0x00000001  // page table type
ARM_TTE_TABLE_MASK         0xFFFFFC00  // L2 table base addr mask (bits [31:10])

// L1 page table descriptor:
#define L1_DESC_PAGE_TABLE  0x00000001  // simplest page table pointer
// Breakdown:
//   [31:10] = L2_table_PA[31:10]  // L2 table physical address (1KB aligned)
//   [9]     = 0                    // P (ECC enable, implementation defined)
//   [8:5]   = 0000                 // domain = 0
//   [4]     = 0                    // SBZ (should be zero)
//   [3]     = 0                    // NS (non-secure) = 0
//   [2]     = 0                    // SBZ
//   [1:0]   = 01                   // page table type
```

### L2 Descriptor (Page Table Entry - PTE)

**L2 Small Page (4KB granularity)**:
```c
ARM_PTE_TYPE               0x00000002  // small page type
ARM_PTE_PAGE_MASK          0xFFFFF000  // phys addr mask (bits [31:12])
ARM_PTE_PAGE_SHIFT         12          // 4KB page shift
ARM_SMALL_PAGE_SIZE        4096        // 4KB

// Conservative 4KB page descriptor:
#define L2_DESC_SMALL_PAGE  0x0000047e  // Small page, Normal Cacheable
// Breakdown:
//   [31:12] = PA[31:12]     physical address top 20 bits
//   [11:10] = 00            TEX[2:1]
//   [9]     = 0             AP[2] = 0
//   [8:6]   = 001           TEX[0], C, B = 001 for Normal Cacheable (example)
//   [5:4]   = 11            AP[1:0] = full access
//   [3]     = 0             C (cacheable bit, part of TEX|C|B encoding)
//   [2]     = 1             B (bufferable bit)
//   [1:0]   = 10            small page type (extended)
//
// Alternative Strongly-Ordered 4KB:
#define L2_DESC_SMALL_PAGE_SO  0x00000012
//   TEX|C|B = 000, AP=11, type=10 (small page with XN=0)
```

## Stage82 Virtual Address Space Layout

### Physical Memory Layout (cancro MSM8974)
```
0x00000000 - 0x00007fff  : reserved/boot ROM
0x00008000 - 0x0006ffff  : Stage82 image (text/data/bss) ~456KB
0x00070000 - 0x000dffff  : available low RAM
0x000e0000 - 0x000e3fff  : Stage81 candidate L1 (16KB)
0x000e4000 - 0x000effff  : available
0x000f0000 - 0x7fffffff  : unmapped/not present in 2GB cancro config
0x80000000 - 0xde4fffff  : main DRAM (1507 MB after subtracting console)
0xde500000 - 0xde6fffff  : RAM console (2MB)
0xde700000 - 0xf8ffffff  : unmapped
0xf9000000 - 0xf9ffffff  : GIC (distributor/CPU interface)
0xfa000000 - 0xfaffffff  : MSM IMEM / restart reason
0xfc400000 - 0xfc4fffff  : PS_HOLD / device registers
```

### Stage82 Proposed Virtual Address Space Layout

**Kernel virtual base (`virtBase`)**: `0x80000000`

Rationale:
- XNU typically uses high kernel virtual addresses (0x80000000 or higher on 32-bit ARM)
- Keeps user-space (0x00000000-0x7fffffff) separate
- Aligns with common ARM kernel convention
- Stage82 image at PA 0x00008000 maps to VA 0x80008000

**Virtual Address Map**:
```
0x00000000 - 0x7fffffff : (reserved for future user-space, unmapped in Stage82)
0x80000000 - 0x8006ffff : Stage82 kernel image (text/data/bss) HIGH ALIAS
                          -> PA 0x00008000 - 0x0006ffff
0x80070000 - 0x800dffff : available kernel heap/stack space (future)
0x800e0000 - 0x800e3fff : Stage81 legacy candidate L1 (preserved, unmapped in Stage82)
0x800e4000 - 0x801fffff : Stage82 L1 table (16KB) + L2 tables pool (~1MB)
                          -> PA 0x000e4000 - 0x001fffff
0x80200000 - 0xde4fffff : physical RAM direct map window
                          -> PA 0x80200000 - 0xde4fffff (identity after offset adjustment)
0xde500000 - 0xde6fffff : RAM console
                          -> PA 0xde500000 - 0xde6fffff (identity)
0xf9000000 - 0xf9ffffff : GIC
                          -> PA 0xf9000000 - 0xf9ffffff (identity)
0xfa000000 - 0xfaffffff : MSM IMEM
                          -> PA 0xfa000000 - 0xfaffffff (identity)
0xfc400000 - 0xfc4fffff : PS_HOLD / device
                          -> PA 0xfc400000 - 0xfc4fffff (identity)
```

**Key insight**: Stage82 kernel code at PA 0x00008000 needs to execute from VA 0x80008000. This requires position-independent code or careful relocation handling.

## Stage82 Candidate Pmap Structure

### L1 Table
- **Size**: 16 KB (4096 entries × 4 bytes)
- **Alignment**: 16 KB
- **Location**: PA 0x000e4000 (reuse Stage81's candidate L1 base, but rebuild content)
- **Coverage**: 4096 MB (entire 32-bit address space)
- **Entry format**: Mix of section descriptors (1MB) and page table pointers

### L2 Tables
- **Size per table**: 1 KB (256 entries × 4 bytes)
- **Alignment**: 1 KB (0x400)
- **Count needed**:
  - Kernel image: ~1 table (covers 1MB, Stage82 image is ~456KB)
  - Kernel data/tables: ~1-2 tables
  - Total allocation: ~128 tables (128KB) to be safe
- **Location**: PA 0x000e4000 + 16KB = PA 0x000e8000 onwards
- **Entry format**: Small page descriptors (4KB)

### Static Allocation
```c
// 16KB-aligned L1 table (4096 entries)
static uint32_t stage82_candidate_l1[4096] __attribute__((aligned(16384)));

// L2 tables pool (128 tables × 256 entries = 32768 entries)
static uint32_t stage82_candidate_l2_pool[32768] __attribute__((aligned(1024)));

// L2 table allocation cursor
static uint32_t stage82_l2_table_count;
```

## Mapping Plan

### Phase 1: Identity/low mappings (for safe transition)
```
VA 0x00000000 -> PA 0x00000000  (1MB section, Stage82 low image)
VA 0x00100000 -> PA 0x00100000  (1MB section, extra safety)
```

### Phase 2: High kernel image mapping (L2 fine-grained)
```
VA 0x80000000 - 0x8006ffff -> PA 0x00008000 - 0x0006ffff (L2 pages, kernel code/data)
VA 0x80070000 - 0x800fffff -> PA 0x00070000 - 0x000fffff (L2 pages, available RAM)
```

Use L2 tables for kernel image region to enable 4KB granularity, allowing future:
- Per-page execute/no-execute permissions
- Code vs data differentiation
- Precise memory protection

### Phase 3: Physical RAM direct map
```
VA 0x80200000 onwards -> PA 0x80200000 onwards (1MB sections for speed)
Up to VA 0xde4fffff -> PA 0xde4fffff
```

### Phase 4: Device MMIO (1MB sections)
```
VA 0xde500000 -> PA 0xde500000  (RAM console, 2MB = 2 sections)
VA 0xf9000000 -> PA 0xf9000000  (GIC)
VA 0xfa000000 -> PA 0xfa000000  (MSM IMEM)
VA 0xfc400000 -> PA 0xfc400000  (PS_HOLD)
```

### Phase 5: Self-mapping
```
VA of stage82_candidate_l1 base -> PA of stage82_candidate_l1 base
VA of stage82_candidate_l2_pool base -> PA of stage82_candidate_l2_pool base
```

## Implementation Steps

### Step 1: L2 Table Allocation Helper
```c
static uint32_t *stage82_alloc_l2_table(void) {
    if (stage82_l2_table_count >= 128) return NULL;
    uint32_t *l2 = &stage82_candidate_l2_pool[stage82_l2_table_count * 256];
    stage82_l2_table_count++;
    memset(l2, 0, 1024);
    return l2;
}
```

### Step 2: L2 Mapping Helper
```c
static void stage82_map_l2_page(uint32_t *l2, uint32_t va, uint32_t pa) {
    uint32_t l2_index = (va >> 12) & 0xff;  // bits [19:12]
    l2[l2_index] = (pa & 0xfffff000) | L2_DESC_SMALL_PAGE;
}
```

### Step 3: L1 Page Table Pointer Helper
```c
static void stage82_map_l1_page_table(uint32_t *l1, uint32_t va, uint32_t *l2_pa) {
    uint32_t l1_index = (va >> 20) & 0xfff;  // bits [31:20]
    l1[l1_index] = ((uint32_t)(uintptr_t)l2_pa & 0xfffffc00) | ARM_TTE_TYPE_TABLE;
}
```

### Step 4: Populate Candidate Pmap
```c
// Zero everything
memset(stage82_candidate_l1, 0, sizeof(stage82_candidate_l1));
memset(stage82_candidate_l2_pool, 0, sizeof(stage82_candidate_l2_pool));
stage82_l2_table_count = 0;

// Phase 1: Low identity (1MB sections for safety)
stage82_map_l1_section(stage82_candidate_l1, 0x00000000, 0x00000000);
stage82_map_l1_section(stage82_candidate_l1, 0x00100000, 0x00100000);

// Phase 2: High kernel image (L2 pages)
// VA 0x80000000 - 0x800fffff maps to PA 0x00000000 - 0x000fffff via L2
uint32_t *l2_kernel = stage82_alloc_l2_table();  // for VA 0x80000000-0x800fffff
stage82_map_l1_page_table(stage82_candidate_l1, 0x80000000, l2_kernel);
for (uint32_t offset = 0; offset < 0x00100000; offset += 0x1000) {
    uint32_t va = 0x80000000 + offset;
    uint32_t pa = 0x00000000 + offset;
    stage82_map_l2_page(l2_kernel, va, pa);
}

// Phase 3: Physical RAM direct map (1MB sections)
// Example: map 256MB starting at VA/PA 0x80200000
for (uint32_t offset = 0; offset < 256 * 1024 * 1024; offset += 0x00100000) {
    uint32_t va = 0x80200000 + offset;
    uint32_t pa = 0x80200000 + offset;
    stage82_map_l1_section(stage82_candidate_l1, va, pa);
}

// Phase 4: Device MMIO
stage82_map_l1_section(stage82_candidate_l1, 0xde500000, 0xde500000);  // RAM console
stage82_map_l1_section(stage82_candidate_l1, 0xde600000, 0xde600000);  // RAM console +1MB
stage82_map_l1_section(stage82_candidate_l1, 0xf9000000, 0xf9000000);  // GIC
stage82_map_l1_section(stage82_candidate_l1, 0xfa000000, 0xfa000000);  // MSM IMEM
stage82_map_l1_section(stage82_candidate_l1, 0xfc400000, 0xfc400000);  // PS_HOLD

// Phase 5: Self-mapping
uint32_t l1_pa = (uint32_t)(uintptr_t)stage82_candidate_l1;
uint32_t l2_pool_pa = (uint32_t)(uintptr_t)stage82_candidate_l2_pool;
stage82_map_l1_section(stage82_candidate_l1, l1_pa, l1_pa);
stage82_map_l1_section(stage82_candidate_l1, l2_pool_pa, l2_pool_pa);
```

## High-Virtual Execution Challenge

**Problem**: Stage82 code currently executes at PA 0x00008000 (identity mapped at VA 0x00008000). After switching to the candidate pmap, the code must execute from VA 0x80008000.

**Solutions**:

### Option A: Position-Independent Code + Dual Mapping (Recommended)
Keep both identity (VA 0x00000000) and high-virtual (VA 0x80000000) mappings active during the live window:
```c
// Phase 1: Low identity (for current PC)
stage82_map_l1_section(l1, 0x00000000, 0x00000000);

// Phase 2: High kernel (for high-virtual access tests)
// Map via L2 so we can test high-virtual without relocating PC
```

**Verification sequence**:
1. Install candidate pmap (PC still at low VA 0x0004xxxx)
2. Read/write test data through high VA 0x8000xxxx
3. Call a simple test function whose address is computed as high VA
4. Restore original pmap
5. Return (PC back to low VA)

### Option B: Explicit PC Relocation (Advanced, Stage83+)
Compute high-virtual PC, branch to it:
```c
dsb_isb();
write_ttbr0(candidate_l1_base);
invalidate_tlbs();
dsb_isb();

// Now at low VA, need to jump to high VA
uint32_t high_pc = 0x80000000 + ((uint32_t)&&high_label - 0x00008000);
goto *((void *)high_pc);

high_label:
// Now executing from high VA
// ... verification code ...
```

**Stage82 choice**: Option A (dual mapping) is safer and sufficient for validation.

## Verification Plan

### Static Verification
1. L1 table checksum
2. L2 tables count and checksums
3. Descriptor format validation (type bits, alignment)

### Live Verification (under candidate pmap)
1. Read Stage82 global variable through high VA
2. Write/read probe word through high VA
3. Verify RAM console signature through high VA
4. Verify GIC GICD_CTLR through high VA
5. Call simple test function at high VA (if safe)

### Safety
- TTBCR/DACR/SCTLR unchanged
- Fail-closed restore if any check fails
- Non-persistent (restore original TTBR0 before return)

## Next Steps

1. Implement `stage82_xnu_arm_vm_init_full_pmap_run()`
2. Extend `struct stage82_xnu_arm_vm_init_full_pmap_result` ABI
3. Update entry-stub to call new sequence
4. Local build validation
5. Hardware validation via `fastboot boot`
