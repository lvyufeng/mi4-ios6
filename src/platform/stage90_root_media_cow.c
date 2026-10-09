/*
 * stage90_root_media_cow.c - a copy-on-write page shadow for the mi4 root medium.
 *
 * WHAT IT IS. The 965c arm mounts the real iOS 7.1.2 HFSX rootfs **read-only**: the strategy serves the
 * card unit's blocks and refuses every `B_WRITE` with `EROFS`. But iOS userspace writes `/private/var`
 * from its first seconds, so a read-only root cannot carry it. The HD2 iOS7 lab solved exactly this
 * (`leo_cow.c`, 82 lines, in this same `xnu-hd2-darwin13` tree) with a **page-granular RAM shadow over a
 * read-only base** - it never writes the base; a write copies the whole original page into a RAM arena
 * first, then modifies only the shadow. `IOS7LeoHFSFile` wraps its read-only SD file in this, and
 * `hfs_mountfs` sees a writable device and mounts rw (`Leo boot: root mount readonly=0`).
 *
 * This file is the SAME mechanism, for OUR base. The differences from `leo_cow.c` are deliberate and
 * named:
 *
 *   1. **The page geometry is parameterized, not hardwired.** `leo_cow` assumes 8x512-byte sectors per
 *      4096-byte page (`LEO_COW_PAGE_BYTES / LEO_COW_SECTOR_BYTES`). We pass `sectors_per_page` so the
 *      page size is a parameter - a hardwired 8 is a hidden 4096, and this project pays for hidden
 *      constants.
 *   2. **The base read is a callback** (`st_cow_read512_fn`, `read512(cookie, lba, out)`), exactly as
 *      `leo_cow`'s - so the module knows nothing of the SD card or of the mi4 ladder. The caller supplies
 *      the base (for the mi4, `entry_storage_driver_read`).
 *   3. **No allocator.** `leo_cow` takes its arena from `IOMalloc` at probe time. Here the arena, the
 *      hash table and the pending list are handed in by the caller (the strategy owns a static `.bss`
 *      arena), because this module is compiled into the entry image whose `.bss` is a fixed window, and
 *      because a device with no allocator to ask must not pretend to ask one.
 *
 * THE ONE SAFETY PROPERTY, AND IT IS A CHECK AND NOT A COMMENT. The base is **never written**: every path
 * that changes bytes writes into `arena`, and `read512` is the only function that touches the base and it
 * only reads. The host known-answer test below (`STAGE90_COW_SELFTEST`) proves this by calling the API
 * against a base array in memory and asserting the base is byte-identical afterward - the same "run the
 * very same source natively" shape `stage90_aes.c`'s `STAGE90_AES_SELFTEST` uses. So the property is
 * grounded on the artifact, not asserted in prose ([[mi4-a-claim-in-a-comment-is-not-a-check]]).
 *
 * PORTABILITY / LINKAGE. This is plain C with no XNU header and no tree include. It is `#include`d by
 * `src/platform/stage90_root_media.c` (so one object carries it, as the platform block builds one `.c`)
 * and compiled *standalone* by the host selftest. `memcpy` is the only library call; on the host it is
 * the C library's, in the image it is the kernel's.
 */

/* The two fixed-width headers are clang's OWN freestanding headers, so they resolve whether this file is
 * compiled standalone (the host known-answer test) or `#include`d into `src/platform/stage90_root_media.c`
 * (the kernel build). */
#include <stdint.h>
#include <stddef.h>
#ifdef STAGE90_COW_SELFTEST
/* The host build: a normal C program with libc. */
#include <stdio.h>
#include <string.h>
#else
/* The kernel build: the two library calls are DECLARED here rather than pulled from `<string.h>`, which is
 * not guaranteed on the platform block's component-specific include path (it compiles under the **bsd**
 * define set). Both are libkern in the image and libc on the host. */
extern void *memcpy(void *to, const void *from, size_t len);
extern void *memset(void *to, int c, size_t len);
#endif

#define ST_COW_OK        0
#define ST_COW_RANGE     1
#define ST_COW_NO_SPACE  2
#define ST_COW_IO        3
#define ST_COW_STATE     4

#define ST_COW_OCCUPIED  1u
#define ST_COW_DIRTY     2u

typedef int (*st_cow_read512_fn)(void *cookie, uint64_t lba, uint8_t out[512]);

typedef struct {
    uint32_t page;      /* base page number this slot shadows */
    uint32_t slot;      /* arena page holding the shadow       */
    uint32_t flags;     /* OCCUPIED | DIRTY                    */
} st_cow_entry;

typedef struct {
    uint64_t      sectors;        /* base length in 512-byte sectors                 */
    uint32_t      sectors_per_page; /* 512-byte sectors per page (8 => 4096-byte page) */
    uint32_t      page_bytes;     /* sectors_per_page * 512                          */
    uint32_t      capacity;       /* arena pages (max distinct shadowed pages)       */
    uint32_t      hash_slots;     /* power of two, >= 2*capacity                     */
    uint32_t      used;           /* arena pages handed out                          */
    uint32_t      dirty;
    uint8_t      *arena;          /* capacity * page_bytes                           */
    st_cow_entry *entries;        /* hash_slots entries                              */
    uint32_t     *pending;        /* capacity scratch                                */
    void         *cookie;
    st_cow_read512_fn read512;
    int           backend_error;
} st_cow;

/*
 * Validate and bind the shadow. Returns ST_COW_RANGE for any geometry the arithmetic below cannot hold
 * in 32 bits (the sector count is divided into pages by a shift, so an unaligned length or a page size
 * that is not a power of two would silently drop a tail page - refused, not rounded).
 */
static int
st_cow_init(st_cow *c, uint64_t sectors, uint32_t sectors_per_page,
            uint32_t capacity, uint32_t hash_slots, uint8_t *arena,
            st_cow_entry *entries, uint32_t *pending, void *cookie,
            st_cow_read512_fn read512)
{
    uint32_t spp = sectors_per_page;
    if (!c || !arena || !entries || !pending || !read512 || !sectors || !spp || !capacity)
        return ST_COW_RANGE;
    if (spp & (spp - 1u))                       /* page size must be a power of two */
        return ST_COW_RANGE;
    /* A base whose length is not a whole number of pages is ACCEPTED and its tail page is partial: the
     * last page holds `sectors % spp` valid sectors, and only those are ever read from the base or served
     * (see prepare_write). This is not a corner case - the mi4's `userdata` is 26,582,225 sectors, not a
     * multiple of 8 - so refusing it would refuse the very medium this arm exists for. */
    if (((sectors + spp - 1u) / spp) > 0xffffffffull)   /* page count must fit 32 bits (the hash key) */
        return ST_COW_RANGE;
    if (!hash_slots || (hash_slots & (hash_slots - 1u)) || hash_slots < capacity * 2u)
        return ST_COW_RANGE;
    if (capacity > (0xffffffffu / spp))         /* page_bytes must fit 32 bits */
        return ST_COW_RANGE;
    memset(c, 0, sizeof(*c));
    memset(entries, 0, (size_t)hash_slots * sizeof(*entries));
    c->sectors = sectors;
    c->sectors_per_page = spp;
    c->page_bytes = spp * 512u;
    c->capacity = capacity;
    c->hash_slots = hash_slots;
    c->arena = arena;
    c->entries = entries;
    c->pending = pending;
    c->cookie = cookie;
    c->read512 = read512;
    return ST_COW_OK;
}

static st_cow_entry *
st_cow_lookup(st_cow *c, uint32_t page, int want_empty)
{
    /* Fibonacci hash, linear probe - the same shape leo_cow uses, and the table is >= 2*capacity so a
     * full table cannot spin: at most `capacity` slots are occupied. */
    uint32_t i, slot = (page * 2654435761u) & (c->hash_slots - 1u);
    for (i = 0; i < c->hash_slots; i++, slot = (slot + 1u) & (c->hash_slots - 1u)) {
        st_cow_entry *e = c->entries + slot;
        if (!(e->flags & ST_COW_OCCUPIED))
            return want_empty ? e : 0;
        if (e->page == page)
            return e;
    }
    return 0;
}

/*
 * Read one sector: from the shadow if its page is resident, else from the base through the callback.
 */
static int
st_cow_read_sector(st_cow *c, uint64_t sector, uint8_t out[512])
{
    st_cow_entry *e;
    uint32_t in_page;
    if (!c || !out || sector >= c->sectors)
        return ST_COW_RANGE;
    e = st_cow_lookup(c, (uint32_t)(sector / c->sectors_per_page), 0);
    if (e) {
        in_page = (uint32_t)(sector % c->sectors_per_page);
        memcpy(out, c->arena + ((size_t)e->slot * c->page_bytes) + ((size_t)in_page << 9), 512);
        return ST_COW_OK;
    }
    {
        int error = c->read512(c->cookie, sector, out);
        if (error) { c->backend_error = error; return ST_COW_IO; }
    }
    return ST_COW_OK;
}

/*
 * Make every page in [first, first+count) resident BEFORE any of it is modified: capacity is proved
 * first, then each not-yet-resident page's original bytes are read from the base into a free arena slot,
 * then all of them are published. The order is the safety property - a page is never half-copied, and
 * nothing is visible until every original page has been read, so a mid-copy failure leaves the shadow
 * unchanged.
 */
static int
st_cow_prepare_write(st_cow *c, uint64_t first, uint64_t count)
{
    uint32_t first_page, last_page, page, needed = 0, i, j;
    if (!c || !count || first >= c->sectors || count > c->sectors - first)
        return ST_COW_RANGE;
    first_page = (uint32_t)(first / c->sectors_per_page);
    last_page  = (uint32_t)((first + count - 1u) / c->sectors_per_page);
    if ((uint64_t)last_page - first_page + 1u > c->capacity)
        return ST_COW_NO_SPACE;
    for (page = first_page; ; page++) {
        if (!st_cow_lookup(c, page, 0)) {
            if (needed >= c->capacity - c->used)
                return ST_COW_NO_SPACE;         /* proved before any base read */
            c->pending[needed++] = page;
        }
        if (page == last_page)
            break;
    }
    for (i = 0; i < needed; i++) {
        /* The tail page of a base whose length is not a whole number of pages holds fewer valid sectors;
         * read only those, so the copy never asks the base read for an LBA past the medium's end (which
         * the ladder would answer with an EOF and a stale buffer). */
        uint32_t ns = c->sectors_per_page;
        uint64_t remain = c->sectors - ((uint64_t)c->pending[i] * c->sectors_per_page);
        if (remain < ns) ns = (uint32_t)remain;
        for (j = 0; j < ns; j++) {
            int error = c->read512(c->cookie,
                                   ((uint64_t)c->pending[i] * c->sectors_per_page) + j,
                                   c->arena + ((size_t)(c->used + i) * c->page_bytes) + ((size_t)j << 9));
            if (error) { c->backend_error = error; return ST_COW_IO; }
        }
    }
    for (i = 0; i < needed; i++) {
        st_cow_entry *e = st_cow_lookup(c, c->pending[i], 1);
        if (!e) return ST_COW_STATE;
        e->page = c->pending[i];
        e->slot = c->used + i;
        e->flags = ST_COW_OCCUPIED;
    }
    c->used += needed;
    return ST_COW_OK;
}

static int
st_cow_write_prepared_sector(st_cow *c, uint64_t sector, const uint8_t bytes[512])
{
    st_cow_entry *e;
    uint32_t in_page;
    if (!c || !bytes || sector >= c->sectors)
        return ST_COW_RANGE;
    e = st_cow_lookup(c, (uint32_t)(sector / c->sectors_per_page), 0);
    if (!e) return ST_COW_STATE;                /* the caller must prepare_write first */
    in_page = (uint32_t)(sector % c->sectors_per_page);
    memcpy(c->arena + ((size_t)e->slot * c->page_bytes) + ((size_t)in_page << 9), bytes, 512);
    if (!(e->flags & ST_COW_DIRTY)) { e->flags |= ST_COW_DIRTY; c->dirty++; }
    return ST_COW_OK;
}

/* ------------------------------------------------------------------------------------- host selftest */

#ifdef STAGE90_COW_SELFTEST
/* A base of `pages` pages, each filled so every byte is (page_seed ^ offset); a read callback that
 * serves it; and records the base's contents at bind time so the test can prove the base never changed. */
static uint8_t  g_base[8 * 4096];
static uint8_t  g_base_copy[8 * 4096];
static uint8_t  g_arena[4 * 4096];
static st_cow_entry g_entries[8];
static uint32_t g_pending[4];
static int      g_read_calls;

static void
fill_base(uint32_t pages)
{
    size_t n = (size_t)pages * 4096u, i;
    for (i = 0; i < n; i++)
        g_base[i] = (uint8_t)(((i / 4096u) * 131u + (i % 4096u)) & 0xffu);
    memcpy(g_base_copy, g_base, n);
}

static int
base_read(void *cookie, uint64_t lba, uint8_t out[512])
{
    (void)cookie;
    g_read_calls++;
    memcpy(out, g_base + (size_t)lba * 512u, 512u);
    return 0;
}

static int failures;
static void
expect(int cond, const char *what)
{
    if (!cond) { printf("COW KAT FAIL: %s\n", what); failures++; }
    else       { printf("COW KAT  ok : %s\n", what); }
}

int
main(void)
{
    st_cow c;
    uint8_t buf[512];

    /* 2 pages of base, arena of 4 pages, hash 8 slots. */
    fill_base(2);
    expect(st_cow_init(&c, 2ull * 8u, 8u, 4u, 8u, g_arena, g_entries, g_pending,
                       (void *)0, base_read) == ST_COW_OK, "init accepts a clean geometry");

    /* (1) an unwritten sector reads the base. */
    g_read_calls = 0;
    expect(st_cow_read_sector(&c, 3u, buf) == ST_COW_OK, "read unwritten sector returns OK");
    expect(memcmp(buf, g_base + 3u * 512u, 512u) == 0, "read unwritten sector == base");
    expect(g_read_calls == 1, "read unwritten sector went to the base");

    /* (2) write sector 3 (page 0). Its neighbours in page 0 must keep the base's bytes. */
    expect(st_cow_prepare_write(&c, 3u, 1u) == ST_COW_OK, "prepare_write(sector 3) OK");
    memset(buf, 0xAB, 512u);
    expect(st_cow_write_prepared_sector(&c, 3u, buf) == ST_COW_OK, "write_prepared(sector 3) OK");
    expect(st_cow_read_sector(&c, 3u, buf) == ST_COW_OK && buf[0] == 0xAB && buf[511] == 0xAB,
           "sector 3 now reads the written bytes");
    expect(st_cow_read_sector(&c, 0u, buf) == ST_COW_OK &&
           memcmp(buf, g_base, 512u) == 0, "sector 0 (same page) still reads the base");
    expect(st_cow_read_sector(&c, 7u, buf) == ST_COW_OK &&
           memcmp(buf, g_base + 7u * 512u, 512u) == 0, "sector 7 (same page) still reads the base");

    /* (3) writes to a second, independent page. */
    expect(st_cow_prepare_write(&c, 15u, 1u) == ST_COW_OK, "prepare_write(sector 15) OK");
    memset(buf, 0xCD, 512u);
    expect(st_cow_write_prepared_sector(&c, 15u, buf) == ST_COW_OK, "write_prepared(sector 15) OK");
    expect(st_cow_read_sector(&c, 15u, buf) == ST_COW_OK && buf[0] == 0xCD, "sector 15 reads its write");
    expect(c.dirty == 2u && c.used == 2u, "two pages resident, both dirty");

    /* (4) THE SAFETY PROPERTY: the base was never modified. */
    expect(memcmp(g_base, g_base_copy, sizeof(g_base)) == 0, "the base array is byte-identical (never written)");

    /* (5) a write needs its page prepared first. Fresh shadow: pages 0/1 unmodified, so page 1 (sector
     * 8) is not resident. */
    {
        st_cow c3;
        fill_base(2);
        expect(st_cow_init(&c3, 2ull * 8u, 8u, 4u, 8u, g_arena, g_entries, g_pending,
                           (void *)0, base_read) == ST_COW_OK, "init a fresh shadow");
        expect(st_cow_write_prepared_sector(&c3, 8u, buf) == ST_COW_STATE,
               "write without prepare is refused (ST_COW_STATE)");
    }

    /* (6) exhaustion: a 4-page base, a 2-page arena - the third distinct page cannot be prepared. */
    {
        st_cow c2;
        fill_base(4);
        expect(st_cow_init(&c2, 4ull * 8u, 8u, 2u, 8u, g_arena, g_entries, g_pending,
                           (void *)0, base_read) == ST_COW_OK, "init with a 2-page arena");
        expect(st_cow_prepare_write(&c2, 0u, 8u) == ST_COW_OK, "prepare page 0");
        expect(st_cow_prepare_write(&c2, 8u, 8u) == ST_COW_OK, "prepare page 1");
        expect(st_cow_prepare_write(&c2, 8u, 8u) == ST_COW_OK, "re-prepare a resident page is free");
        expect(st_cow_prepare_write(&c2, 16u, 8u) == ST_COW_NO_SPACE,
               "a third distinct page is refused (ST_COW_NO_SPACE)");
    }

    /* (7) a PARTIAL TAIL page: 9 sectors = a full page 0 + 1 valid sector of page 1. A prepare of the
     * tail page must read ONLY the one valid sector from the base, never an LBA past the medium. */
    {
        st_cow c4;
        fill_base(2);                     /* 16 sectors of base; only 9 are in this medium */
        expect(st_cow_init(&c4, 9ull, 8u, 4u, 8u, g_arena, g_entries, g_pending,
                           (void *)0, base_read) == ST_COW_OK,
               "a length that is not a whole number of pages is accepted (a partial tail)");
        g_read_calls = 0;
        expect(st_cow_prepare_write(&c4, 8u, 1u) == ST_COW_OK, "prepare the tail page (sector 8)");
        expect(g_read_calls == 1, "the tail page read exactly one sector from the base (not a full page)");
        memset(buf, 0x5A, 512u);
        expect(st_cow_write_prepared_sector(&c4, 8u, buf) == ST_COW_OK, "write the tail sector");
        expect(st_cow_read_sector(&c4, 8u, buf) == ST_COW_OK && buf[0] == 0x5A, "the tail sector reads its write");
        expect(st_cow_read_sector(&c4, 9u, buf) == ST_COW_RANGE, "a sector past the medium is out of range");
        expect(st_cow_prepare_write(&c4, 9u, 1u) == ST_COW_RANGE, "a write past the medium is out of range");
    }

    /* (8) geometry refusals. */
    expect(st_cow_init(&c, 16u, 12u, 4u, 8u, g_arena, g_entries, g_pending, 0, base_read) == ST_COW_RANGE,
           "a non-power-of-two page size is refused");
    expect(st_cow_init(&c, 16u, 8u, 4u, 4u, g_arena, g_entries, g_pending, 0, base_read) == ST_COW_RANGE,
           "a hash table smaller than 2*capacity is refused");

    if (failures) { printf("COW KAT: %d FAILURE(S)\n", failures); return 1; }
    printf("COW KAT: all checks passed\n");
    return 0;
}
#endif /* STAGE90_COW_SELFTEST */