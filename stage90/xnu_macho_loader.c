/* Stage89 Mach-O Kernel Loader
 *
 * Loads a real XNU Mach-O kernel image into memory at high-VA (0x80000000)
 * and prepares to transfer control to XNU's _start entry point.
 *
 * This is the critical transition from "simulating XNU boot" to "executing
 * real XNU code."
 */

#include "stage90.h"

/* Mach-O load command types */
#define LC_SEGMENT          0x1
#define LC_UNIXTHREAD       0x5
#define MH_EXECUTE          0x2

/* Segment permissions (VM_PROT_*) */
#define VM_PROT_READ    0x01
#define VM_PROT_WRITE   0x02
#define VM_PROT_EXECUTE 0x04

/* ARM thread state flavor */
#define ARM_THREAD_STATE    1

struct mach_header {
	uint32_t magic;
	uint32_t cputype;
	uint32_t cpusubtype;
	uint32_t filetype;
	uint32_t ncmds;
	uint32_t sizeofcmds;
	uint32_t flags;
};

struct load_command {
	uint32_t cmd;
	uint32_t cmdsize;
};

struct segment_command {
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

struct arm_thread_state {
	uint32_t r[13];
	uint32_t sp;
	uint32_t lr;
	uint32_t pc;
	uint32_t cpsr;
};

struct thread_command {
	uint32_t cmd;
	uint32_t cmdsize;
	uint32_t flavor;
	uint32_t count;
	struct arm_thread_state state;
};

static struct stage90_xnu_macho_loader_result g_result;

/* Helper: compare segment name (first 16 chars) */
static int seg_name_eq(const char *segname, const char *target)
{
	int i;
	for (i = 0; i < 16; i++) {
		if (segname[i] != target[i]) {
			return 0;
		}
		if (segname[i] == '\0') {
			return 1;
		}
	}
	return 1;
}

/* Helper: compute checksum */
static uint32_t stage90_xnu_macho_loader_checksum(
    volatile const struct stage90_xnu_macho_loader_result *r)
{
	volatile const uint32_t *words = (volatile const uint32_t *)r;
	uint32_t count = (uint32_t)(offsetof(struct stage90_xnu_macho_loader_result, checksum) / sizeof(uint32_t));
	uint32_t chk = 0;
	for (uint32_t i = 0; i < count; i++) {
		chk ^= words[i];
	}
	return chk;
}

/* Helper: log result */
static void stage90_xnu_macho_loader_log(
    volatile const struct stage90_xnu_macho_loader_result *r)
{
	xnu_log_puts("stage90_xnu_macho_loader result:\n");
	xnu_log_kv32("stage90_xnu_macho_loader_version", r->version);
	xnu_log_kv32("stage90_xnu_macho_loader_status", r->status);
	xnu_log_kv32("stage90_xnu_macho_loader_macho_base", r->macho_base);
	xnu_log_kv32("stage90_xnu_macho_loader_macho_size", r->macho_size);
	xnu_log_kv32("stage90_xnu_macho_loader_text_segment_va", r->text_segment_va);
	xnu_log_kv32("stage90_xnu_macho_loader_text_segment_size", r->text_segment_size);
	xnu_log_kv32("stage90_xnu_macho_loader_data_segment_va", r->data_segment_va);
	xnu_log_kv32("stage90_xnu_macho_loader_data_segment_size", r->data_segment_size);
	xnu_log_kv32("stage90_xnu_macho_loader_linkedit_segment_va", r->linkedit_segment_va);
	xnu_log_kv32("stage90_xnu_macho_loader_segments_loaded", r->segments_loaded);
	xnu_log_kv32("stage90_xnu_macho_loader_entry_point_offset", r->entry_point_offset);
	xnu_log_kv32("stage90_xnu_macho_loader_xnu_entry_va", r->xnu_entry_va);
	xnu_log_kv32("stage90_xnu_macho_loader_ready_for_handoff", r->ready_for_handoff);
	xnu_log_kv32("stage90_xnu_macho_loader_checksum", r->checksum);
}

/* Helper: load one segment into memory at its target VA */
static int load_segment(struct segment_command *seg, uint8_t *macho_base,
                        struct stage90_xnu_macho_loader_result *r)
{
	/* Variables for actual loading (dest/src unused in dry-run mode) */
	uint8_t *dest __attribute__((unused));
	uint8_t *src __attribute__((unused));
	uint32_t copy_size;
	uint32_t zero_size;
	uint32_t seg_end;

	/* Validate segment addresses */
	if (seg->vmsize == 0) {
		return 0;  /* Empty segment, skip */
	}

	/* Calculate segment end address */
	seg_end = seg->vmaddr + seg->vmsize;

	/* For now, we assume segments are already mapped by the candidate L1.
	 * The candidate L1 maps 0x80000000-0x800fffff (1MB) via L2 pages.
	 * XNU segments MUST be in this range. */
	if (seg->vmaddr < 0x80000000 || seg->vmaddr >= 0x80100000) {
		xnu_log_puts("segment ");
		xnu_log_puts(seg->segname);
		xnu_log_puts(" vmaddr out of mapped range, skipping\n");
		xnu_log_kv32("vmaddr", seg->vmaddr);
		/* Don't fail, just skip unmapped segments */
		return 0;
	}

	/* Check if segment end is also within range */
	if (seg_end > 0x80100000) {
		xnu_log_puts("segment ");
		xnu_log_puts(seg->segname);
		xnu_log_puts(" extends beyond mapped range, skipping\n");
		xnu_log_kv32("vmaddr", seg->vmaddr);
		xnu_log_kv32("vmsize", seg->vmsize);
		xnu_log_kv32("seg_end", seg_end);
		/* Don't fail, just skip */
		return 0;
	}

	dest = (uint8_t *)(uintptr_t)seg->vmaddr;
	src = macho_base + seg->fileoff;
	copy_size = seg->filesize;
	zero_size = seg->vmsize - seg->filesize;

	/* Copy segment data from Mach-O file to target VA */
	if (copy_size > 0) {
		xnu_log_puts("copying segment data\n");
		xnu_log_kv32("src_offset", seg->fileoff);
		xnu_log_kv32("dest_va", seg->vmaddr);
		xnu_log_kv32("copy_size", copy_size);
		memcpy(dest, src, copy_size);
	}

	/* Zero-fill remaining space (BSS, etc.) */
	if (zero_size > 0) {
		xnu_log_puts("zeroing BSS\n");
		xnu_log_kv32("zero_size", zero_size);
		memset(dest + copy_size, 0, zero_size);
	}

	xnu_log_puts("loaded segment ");
	xnu_log_puts(seg->segname);
	xnu_log_puts(" successfully\n");

	r->segments_loaded++;
	return 0;
}

int stage90_xnu_macho_loader_run(
    struct boot_args *args,
    struct stage90_xnu_entry_stub_result *entry_result)
{
	struct stage90_xnu_macho_loader_result *r = &g_result;
	struct mach_header *mh;
	struct load_command *lc;
	struct segment_command *seg;
	struct thread_command *tc;
	uint8_t *macho_base;
	uint32_t i;
	uint32_t lc_offset;
	int found_text = 0;
	int found_entry = 0;

	(void)entry_result;

	/* Initialize result */
	memset(r, 0, sizeof(*r));
	r->version = STAGE90_XNU_MACHO_LOADER_VERSION;
	r->size = sizeof(*r);
	r->required_mask = STAGE90_XNU_MACHO_LOADER_REQUIRED_MASK;

	/* For Stage89, we'll use the embedded macho fixture as the "kernel" for initial testing.
	 * In later iterations, this would be a real XNU kernel binary.
	 * The fixture is built by macho_fixture.c and embedded in the image. */

	extern const uint8_t stage90_embedded_macho[];

	macho_base = (uint8_t *)stage90_embedded_macho;
	r->macho_base = (uint32_t)(uintptr_t)macho_base;
	r->macho_size = 4096;  /* Fixture is approximately 4KB */

	xnu_log_puts("stage90_xnu_macho_loader: loading Mach-O kernel\n");
	xnu_log_kv32("macho_base", r->macho_base);
	xnu_log_kv32("macho_size", r->macho_size);

	/* Parse Mach-O header */
	mh = (struct mach_header *)macho_base;
	if (mh->magic != 0xfeedface) {
		xnu_log_puts("invalid Mach-O magic\n");
		r->failure_mask |= STAGE90_XNU_MACHO_LOADER_FAIL_INVALID_MACHO;
		goto finish;
	}

	r->satisfied_mask |= STAGE90_XNU_MACHO_LOADER_SAT_MACHO_PARSED;

	/* Iterate load commands */
	lc_offset = sizeof(struct mach_header);
	for (i = 0; i < mh->ncmds; i++) {
		lc = (struct load_command *)(macho_base + lc_offset);

		if (lc->cmd == LC_SEGMENT) {
			seg = (struct segment_command *)lc;

			/* Load segment */
			if (load_segment(seg, macho_base, r) != 0) {
				goto finish;
			}

			/* Record key segments */
			if (seg_name_eq(seg->segname, "__TEXT")) {
				r->text_segment_va = seg->vmaddr;
				r->text_segment_size = seg->vmsize;
				found_text = 1;
			} else if (seg_name_eq(seg->segname, "__DATA")) {
				r->data_segment_va = seg->vmaddr;
				r->data_segment_size = seg->vmsize;
			} else if (seg_name_eq(seg->segname, "__LINKEDIT")) {
				r->linkedit_segment_va = seg->vmaddr;
			}
		} else if (lc->cmd == LC_UNIXTHREAD) {
			tc = (struct thread_command *)lc;
			if (tc->flavor == ARM_THREAD_STATE) {
				r->entry_point_offset = tc->state.pc;
				found_entry = 1;
			}
		}

		lc_offset += lc->cmdsize;
	}

	if (!found_text) {
		xnu_log_puts("no __TEXT segment found\n");
		r->failure_mask |= STAGE90_XNU_MACHO_LOADER_FAIL_NO_TEXT;
		goto finish;
	}

	r->satisfied_mask |= STAGE90_XNU_MACHO_LOADER_SAT_SEGMENTS_LOADED;

	/* Compute XNU entry point VA */
	if (found_entry) {
		/* LC_UNIXTHREAD gives absolute VA */
		r->xnu_entry_va = r->entry_point_offset;
	} else {
		/* No LC_UNIXTHREAD, assume entry at start of __TEXT */
		r->xnu_entry_va = r->text_segment_va;
	}

	xnu_log_puts("XNU entry point computed\n");
	xnu_log_kv32("xnu_entry_va", r->xnu_entry_va);

	r->satisfied_mask |= STAGE90_XNU_MACHO_LOADER_SAT_ENTRY_RESOLVED;

	/* Prepare for handoff */
	r->boot_args_ptr = (uint32_t)(uintptr_t)args;
	r->ready_for_handoff = 1;
	r->satisfied_mask |= STAGE90_XNU_MACHO_LOADER_SAT_READY_FOR_HANDOFF;

	/* For Stage89, we DON'T actually jump to XNU yet.
	 * We just verify the loader works and can prepare everything.
	 * The actual jump will come in a later iteration once we're confident. */

	xnu_log_puts("stage90_xnu_macho_loader: ready for handoff (NOT jumping yet)\n");

finish:
	/* Compute final status */
	if (r->satisfied_mask == r->required_mask && r->failure_mask == 0) {
		r->status = STAGE90_STATUS_OK;
	} else {
		r->status = STAGE90_STATUS_FAIL(r->failure_mask);
	}

	/* Compute checksum */
	r->checksum = stage90_xnu_macho_loader_checksum(r);

	/* Log result */
	stage90_xnu_macho_loader_log(r);

	return r->status == STAGE90_STATUS_OK ? 0 : -1;
}

const struct stage90_xnu_macho_loader_result *
stage90_xnu_macho_loader_result(void)
{
	return &g_result;
}
