#include <pmm.h>
#include <slub.h>
#include <stdio.h>
#include <string.h>

/*
 * Simplified SLUB allocator.
 *
 * Layer 1: page allocation through the current pmm_manager (alloc_pages).
 * Layer 2: each slab carves one 4KB page into fixed-size objects and keeps
 *          a free-object list.
 *
 * The slab header lives at the beginning of each page; a pointer can be
 * mapped back to its slab by clearing the low 12 bits of its address.
 */

#define SLUB_MAGIC      0x5C0B5C0BUL
#define SLUB_CACHE_NUM  8
#define SLUB_ALIGN      16

struct slub_free {
    struct slub_free *next;
};

struct slub_cache {
    size_t objsize;
    struct slub_free *free_list;
    size_t nr_free;
};

struct slub_slab {
    uint64_t magic;              /* SLUB_MAGIC marks a valid slab head */
    struct slub_cache *cache;    /* which size class this slab belongs to */
    uint32_t inuse;              /* number of allocated objects */
    uint32_t nr_objs;            /* objects carved from this page */
};

#define SLUB_HDR \
    ((sizeof(struct slub_slab) + SLUB_ALIGN - 1) & ~((size_t)SLUB_ALIGN - 1))

static struct slub_cache slub_caches[SLUB_CACHE_NUM];
static const size_t slub_sizes[SLUB_CACHE_NUM] = {
    16, 32, 64, 128, 256, 512, 1024, 2048
};
static int slub_ready = 0;

void
slub_init(void) {
    for (int i = 0; i < SLUB_CACHE_NUM; i ++) {
        slub_caches[i].objsize = slub_sizes[i];
        slub_caches[i].free_list = NULL;
        slub_caches[i].nr_free = 0;
    }
    slub_ready = 1;
}

static struct slub_cache *
slub_cache_for(size_t size) {
    for (int i = 0; i < SLUB_CACHE_NUM; i ++) {
        if (slub_sizes[i] >= size) {
            return &slub_caches[i];
        }
    }
    return NULL;
}

/* Allocate one page from the PMM and turn it into a slab of @cache. */
static struct slub_slab *
slub_new_slab(struct slub_cache *cache) {
    struct Page *page = alloc_pages(1);
    if (page == NULL) {
        return NULL;
    }

    uintptr_t slab_va = page2pa(page) + va_pa_offset;
    struct slub_slab *slab = (struct slub_slab *)slab_va;

    slab->magic = SLUB_MAGIC;
    slab->cache = cache;
    slab->inuse = 0;
    slab->nr_objs = (PGSIZE - SLUB_HDR) / cache->objsize;

    /* Thread every object in this page onto the cache free list. */
    cache->free_list = NULL;
    cache->nr_free = 0;
    uintptr_t obj = slab_va + SLUB_HDR;
    for (uint32_t i = 0; i < slab->nr_objs; i ++) {
        struct slub_free *f = (struct slub_free *)(obj + i * cache->objsize);
        f->next = cache->free_list;
        cache->free_list = f;
        cache->nr_free ++;
    }
    return slab;
}

void *
kmalloc(size_t size) {
    if (!slub_ready) {
        slub_init();
    }
    if (size == 0 || size > SLUB_MAX_OBJSIZE) {
        return NULL;
    }

    struct slub_cache *cache = slub_cache_for(size);
    if (cache == NULL) {
        return NULL;
    }

    if (cache->free_list == NULL) {
        if (slub_new_slab(cache) == NULL) {
            return NULL;
        }
    }

    struct slub_free *f = cache->free_list;
    cache->free_list = f->next;
    cache->nr_free --;

    struct slub_slab *slab =
        (struct slub_slab *)((uintptr_t)f & ~((uintptr_t)PGSIZE - 1));
    slab->inuse ++;
    return f;
}

void
kfree(void *ptr) {
    if (ptr == NULL) {
        return;
    }

    struct slub_slab *slab =
        (struct slub_slab *)((uintptr_t)ptr & ~((uintptr_t)PGSIZE - 1));
    if (slab->magic != SLUB_MAGIC) {
        /* Not an object from this allocator; leave memory untouched. */
        return;
    }

    struct slub_cache *cache = slab->cache;
    struct slub_free *f = (struct slub_free *)ptr;
    f->next = cache->free_list;
    cache->free_list = f;
    cache->nr_free ++;
    slab->inuse --;
}

/* ------------------------------------------------------------------ */
/* Self test                                                          */
/* ------------------------------------------------------------------ */

static int slub_pass, slub_fail;

static void
slub_result(int ok, const char *msg) {
    if (ok) {
        slub_pass ++;
        cprintf("[SLUB] PASS: %s\n", msg);
    } else {
        slub_fail ++;
        cprintf("[SLUB] FAIL: %s\n", msg);
    }
}

void
slub_test(void) {
    slub_pass = slub_fail = 0;
    cprintf("[SLUB] start self test\n");
    slub_init();

    slub_result(kmalloc(0) == NULL, "kmalloc(0) returns NULL");
    slub_result(kmalloc(SLUB_MAX_OBJSIZE + 1) == NULL,
                "oversized request returns NULL");

    void *a = kmalloc(8);
    void *b = kmalloc(20);
    void *c = kmalloc(100);
    slub_result(a && b && c && a != b && a != c && b != c,
                "basic allocation returns distinct objects");
    slub_result(((uintptr_t)a & (SLUB_ALIGN - 1)) == 0 &&
                ((uintptr_t)b & (SLUB_ALIGN - 1)) == 0 &&
                ((uintptr_t)c & (SLUB_ALIGN - 1)) == 0,
                "objects are 16-byte aligned");

    int data_ok = 1;
    if (a && b && c) {
        memset(a, 0xAA, 8);
        memset(b, 0xBB, 20);
        memset(c, 0xCC, 100);
        for (int i = 0; i < 8; i ++) {
            if (((unsigned char *)a)[i] != 0xAA) data_ok = 0;
        }
        for (int i = 0; i < 20; i ++) {
            if (((unsigned char *)b)[i] != 0xBB) data_ok = 0;
        }
        for (int i = 0; i < 100; i ++) {
            if (((unsigned char *)c)[i] != 0xCC) data_ok = 0;
        }
    } else {
        data_ok = 0;
    }
    slub_result(data_ok, "object contents are preserved");

    /* 300 objects of 16 bytes force more than one slab. */
    static void *many[300];
    int n = 0;
    for (; n < 300; n ++) {
        many[n] = kmalloc(16);
        if (many[n] == NULL) {
            break;
        }
    }
    int distinct = 1;
    for (int i = 0; i < n && distinct; i ++) {
        for (int j = i + 1; j < n; j ++) {
            if (many[i] == many[j]) {
                distinct = 0;
                break;
            }
        }
    }
    slub_result(n == 300 && distinct,
                "cross-slab allocation yields 300 distinct objects");

    int many_ok = (n == 300);
    for (int i = 0; i < n; i ++) {
        memset(many[i], i & 0xFF, 16);
    }
    for (int i = 0; i < n; i ++) {
        unsigned char *p = many[i];
        for (int k = 0; k < 16; k ++) {
            if (p[k] != (i & 0xFF)) many_ok = 0;
        }
    }
    slub_result(many_ok, "cross-slab objects do not overlap");

    for (int i = 0; i < n; i ++) {
        kfree(many[i]);
    }

    void *x = kmalloc(64);
    kfree(x);
    void *y = kmalloc(64);
    slub_result(x != NULL && x == y, "freed object is reused (LIFO)");
    kfree(y);

    void *m1 = kmalloc(64);
    void *m2 = kmalloc(128);
    void *m3 = kmalloc(512);
    int mixed = m1 && m2 && m3 && m1 != m2 && m2 != m3 && m1 != m3;
    kfree(m1);
    kfree(m2);
    kfree(m3);
    slub_result(mixed, "mixed size classes are independent");

    kfree(a);
    kfree(b);
    kfree(c);

    cprintf("[SLUB] done: %d passed, %d failed\n", slub_pass, slub_fail);
}
