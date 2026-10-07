/*
 * 910c (first arm): the bulk stream's constants - every one named beside the source that OWNS it.
 *
 * **Why a fourth header.** `entry_usb.h` is 910a's read-only offset map; `entry_usb_dev.h` is the values
 * 910a2 writes for the PHY/mode transition; `entry_usb_enum.h` is 910b's endpoint-control/descriptor set.
 * This arm writes a *different* pair of registers - the endpoint prime and complete semaphores for the one
 * bulk IN endpoint - and reads the RAM console ring the live channel publishes into. Those values live
 * here, one definition each ([[mi4-one-value-two-definitions]]), and the file includes the enum header and
 * re-uses every offset, qh/dTD field, and endpoint bit it already defines rather than restating one: a
 * second copy of `STAGE90_USB_ENUM_QH_OFF_TOKEN` would be the exact defect this project keeps paying for.
 *
 * **THE OWNER IS `external/android_kernel_xiaomi_cancro`** - the same files the two prior arms name:
 * `drivers/usb/gadget/ci13xxx_udc.c` + `ci13xxx_udc.h` for the dTD `token` layout and the completion path
 * (`_hardware_dequeue`), and `src/entry/entry_stubs.c` for the RAM console ring this arm streams.
 */
#ifndef STAGE90_ENTRY_USB_STREAM_H
#define STAGE90_ENTRY_USB_STREAM_H

#include <stdint.h>

#include "entry_usb_enum.h"   /* the qh/dTD graph, the endpoint bits, the one bulk IN endpoint. */

/* =================================================================================================
 * (1) The source: the RAM console ring the live channel publishes into.
 * =================================================================================================
 * Owner: `src/entry/entry_stubs.c`. The ring is `{ u32 sig; u32 start; u32 size; u8 data[]; }` at
 * `RAM_CONSOLE_BASE` (0xde500000) - sig `'DBGC' 0x43474244` at +0, `size` (the byte count, not an index)
 * at +8, the bytes at +12. `entry_write_kv` (`:2526`) appends ` key=0x<8 hex>\n` per record and hard-bounds
 * `size` at `0x00200000 - 12` (`:2532`). **The arm reads it through the ALIAS**
 * (`LIVE_CONSOLE_ALIAS_BASE = RAM_CONSOLE_BASE + 0x01000000`, `:2096`): 0xde500000 has no translation
 * while XNU's page tables are live, and `entry_live_map` installs a section aliasing the console's own MB
 * there (`:2360`). A stream that read the base VA with the live tables on would fault; the alias is the
 * path the live channel itself proves works (`:2377`).
 */
#define STAGE90_USB_STREAM_CONSOLE_VA    (STAGE90_USB_STREAM_RAM_CONSOLE_BASE + 0x01000000u)
#define STAGE90_USB_STREAM_RAM_CONSOLE_BASE 0xde500000u
#define STAGE90_USB_STREAM_CONSOLE_SIG   0x43474244u    /* 'DBGC', little-endian.                  */
#define STAGE90_USB_STREAM_OFF_SIG       0u
#define STAGE90_USB_STREAM_OFF_SIZE      8u
#define STAGE90_USB_STREAM_OFF_DATA      12u
#define STAGE90_USB_STREAM_SIZE_MAX      (0x00200000u - STAGE90_USB_STREAM_OFF_DATA)  /* entry_write_kv's bound. */

/*
 * The chunk: one page. The dTD's `page[0]` names the buffer, and its low 12 bits are a byte offset within
 * a 4 KiB page (`ci13xxx_udc.h`) - so a buffer whose base is page-aligned and whose length is <= one page
 * needs only `page[0]` and never `page[1..4]`. **A chunk larger than a page is NOT this arm** (the
 * multi-dTD / 20 KiB `page[]` spread the map §2.1 describes is a later refinement): one dTD, one page.
 */
#define STAGE90_USB_STREAM_CHUNK         4096u

/* =================================================================================================
 * (2) The completion accessor - the dTD `token`, NOT the qh `curr`.
 * =================================================================================================
 * Owner: `_hardware_dequeue` (`ci13xxx_udc.c:2176-2178`): `actual = length - ((token & TD_TOTAL_BYTES) >>
 * 16)`. **The byte count a completed transfer left is in the dTD `token`'s `TOTAL_BYTES` field, never in
 * `qh.curr`** - the vendor writes `qh.curr` for diagnostics only and never reads it for a count. This is
 * the first fact a first build gets wrong. `TD_STATUS_ACTIVE` (bit 7) must be clear before the count is
 * read; a still-active dTD is the vendor's `-EBUSY` retry (`:2134`).
 */
#define STAGE90_USB_STREAM_TD_LEN        28u   /* the whole `struct ci13xxx_td`, for the invalidate. */

/* The one bulk IN endpoint, re-stated by name so this file reads without the enum header open. */
#define STAGE90_USB_STREAM_QH_IN1        STAGE90_USB_ENUM_QH_IN1

/*
 * The entry point. Called from `__wrap_Idle_load_context` beside `entry_usb_enum_poll` (the one wrapper
 * every idle pass reaches, 910b design §9.6), AFTER it, so the endpoint the enum arm armed on
 * `SET_CONFIGURATION` is streamed only once the control machine has configured the device. It must NOT
 * write the endpoint-control register, the device address, or `USBCMD` - the enum arm owns those and a
 * build refusal guards the split - and it must NOT write at all unless the core is already a device and
 * the endpoint is already armed (the 910a2 rule, extended one rung).
 */
void entry_usb_stream_poll(void);

#endif /* STAGE90_ENTRY_USB_STREAM_H */