# Stage87: High-Virtual Data Abort Handler

## Goal

Prove that **MMU fault handling works at high-VA** by intentionally triggering a
data abort (accessing an unmapped address) with VBAR pointing to the high-VA
exception vectors, and verifying the data abort handler executes correctly from
the L2-page-mapped region at `0x80000000+offset`.

## What Stage86 Proved

Stage86 successfully demonstrated:
- ✅ Exception vector table at high-VA (VBAR → `0x800080a0`)
- ✅ IRQ handler execution from high-VA vector base
- ✅ PC-relative branch from high-VA vector to high-VA C handler works
- ✅ IRQ stack access under high-VA candidate L1 works

## What Stage87 Will Prove

Stage87 extends high-VA exception handling to **synchronous exceptions** (data aborts):
- Exception vectors remain at high-VA (reuse Stage86 VBAR setup)
- **Data abort handler** executes from high-VA vector (new)
- PC-relative branch from high-VA `vector_data_abort` to C handler works
- Abort stack (same as IRQ stack in our simple model) access works
- **Fault address and status registers** readable in high-VA abort handler
- Handler can identify fault type (unmapped address) and return gracefully

This completes **synchronous exception handling** validation under high-VA pmap.
