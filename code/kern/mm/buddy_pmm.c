#include <pmm.h>
#include <list.h>
#include <buddy_pmm.h>
#include <stdio.h>

/*
 * Buddy System allocator.
 *
 * All free memory is organised into blocks whose size is a power of two
 * pages: order k means 2^k contiguous pages.  For every order there is one
 * free list.  Allocation rounds the request up to the next power of two,
 * takes the smallest available block, and splits it down.  Freeing a block
 * merges it with its buddy whenever the buddy is free and of the same order.
 */

#define MAX_ORDER 16          /* 2^16 pages = 256 MB, enough for the 128 MB DRAM */

static list_entry_t free_list[MAX_ORDER];
static size_t nr_free;
static size_t free_blocks[MAX_ORDER];

/* Smallest k such that 2^k >= n. */
static size_t
buddy_order_for(size_t n) {
    size_t order = 0;
    while (((size_t)1 << order) < n) {
        order ++;
    }
    return order;
}

static void
buddy_init(void) {
    for (int i = 0; i < MAX_ORDER; i ++) {
        list_init(&free_list[i]);
        free_blocks[i] = 0;
    }
    nr_free = 0;
}

/* Put one block of 2^order pages into the corresponding free list. */
static void
buddy_add_block(struct Page *base, size_t order) {
    base->property = (unsigned int)((size_t)1 << order);
    SetPageProperty(base);
    list_add(&free_list[order], &(base->page_link));
    free_blocks[order] ++;
    nr_free += ((size_t)1 << order);
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);

    /* All pages must currently be reserved. */
    struct Page *p = base;
    for (size_t i = 0; i < n; i ++, p ++) {
        assert(PageReserved(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }

    /*
     * Split [base, base + n) into aligned power-of-two blocks, largest
     * first.  A block of order k must start at a multiple of 2^k pages,
     * otherwise its buddy computation would be wrong.
     */
    uintptr_t ppn = page2ppn(base);
    while (n > 0) {
        size_t order = MAX_ORDER - 1;
        while (order > 0 &&
               ((((size_t)1 << order) > n) ||
                ((ppn & (((size_t)1 << order) - 1)) != 0))) {
            order --;
        }
        buddy_add_block(pa2page(ppn << PGSHIFT), order);
        ppn += ((size_t)1 << order);
        n -= ((size_t)1 << order);
    }
}

static struct Page *
buddy_alloc_pages(size_t n) {
    assert(n > 0);
    if (n > nr_free) {
        return NULL;
    }

    size_t order = buddy_order_for(n);

    /* Find the smallest order that can satisfy the request. */
    size_t found = order;
    while (found < MAX_ORDER && list_empty(&free_list[found])) {
        found ++;
    }
    if (found >= MAX_ORDER) {
        return NULL;
    }

    struct Page *block = le2page(list_next(&free_list[found]), page_link);
    list_del(&(block->page_link));
    free_blocks[found] --;
    ClearPageProperty(block);

    /* The whole block leaves the free pool; the split halves below are
     * put back immediately, so the net decrease is 2^order pages. */
    nr_free -= ((size_t)1 << found);

    /* Split the block down to the requested order. */
    while (found > order) {
        found --;
        struct Page *buddy = block + ((size_t)1 << found);
        buddy_add_block(buddy, found);
    }

    block->property = 0;
    return block;
}

/* Find a free block of the given order whose first page number is ppn. */
static struct Page *
buddy_find_block(uintptr_t ppn, size_t order) {
    list_entry_t *le = &free_list[order];
    while ((le = list_next(le)) != &free_list[order]) {
        struct Page *p = le2page(le, page_link);
        if (page2ppn(p) == ppn) {
            return p;
        }
    }
    return NULL;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    size_t order = buddy_order_for(n);
    size_t pages = (size_t)1 << order;

    /* The caller frees the whole block that was allocated. */
    struct Page *p = base;
    for (size_t i = 0; i < pages; i ++, p ++) {
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        p->property = 0;
        set_page_ref(p, 0);
    }

    nr_free += pages;

    /* Try to merge with the buddy at each level. */
    while (order < MAX_ORDER - 1) {
        uintptr_t ppn = page2ppn(base);
        uintptr_t buddy_ppn = ppn ^ ((size_t)1 << order);
        struct Page *buddy = buddy_find_block(buddy_ppn, order);
        if (buddy == NULL) {
            break;
        }
        list_del(&(buddy->page_link));
        ClearPageProperty(buddy);
        free_blocks[order] --;
        if (buddy < base) {
            base = buddy;
        }
        order ++;
    }

    base->property = (unsigned int)((size_t)1 << order);
    SetPageProperty(base);
    list_add(&free_list[order], &(base->page_link));
    free_blocks[order] ++;
}

static size_t
buddy_nr_free_pages(void) {
    return nr_free;
}

static void
buddy_check(void) {
    /* The real verification lives in buddy_pmm_test(). */
    assert(nr_free <= npage - nbase);
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};

/* ------------------------------------------------------------------ */
/* Self test                                                          */
/* ------------------------------------------------------------------ */

static int buddy_pass, buddy_fail;

static void
buddy_result(int ok, const char *msg) {
    if (ok) {
        buddy_pass ++;
        cprintf("[Buddy] PASS: %s\n", msg);
    } else {
        buddy_fail ++;
        cprintf("[Buddy] FAIL: %s\n", msg);
    }
}

void
buddy_pmm_test(void) {
    const size_t total = 1024;          /* 4 MB test region */
    const struct pmm_manager *saved = pmm_manager;

    buddy_pass = buddy_fail = 0;
    cprintf("[Buddy] start self test (%lu pages)\n", (unsigned long)total);

    struct Page *base = alloc_pages(total);
    if (base == NULL) {
        cprintf("[Buddy] SKIP: cannot reserve a test region\n");
        return;
    }

    for (size_t i = 0; i < total; i ++) {
        SetPageReserved(base + i);
        ClearPageProperty(base + i);
        base[i].property = 0;
        set_page_ref(base + i, 0);
    }

    /* Switch to the buddy manager for the duration of the test. */
    pmm_manager = &buddy_pmm_manager;
    buddy_init();
    buddy_init_memmap(base, total);

    buddy_result(nr_free_pages() == total, "init registers all pages as free");

    struct Page *p1 = alloc_pages(1);
    buddy_result(p1 != NULL && !PageProperty(p1), "allocate one page");

    struct Page *p3 = alloc_pages(3);
    int aligned4 = (p3 != NULL) &&
                   ((page2ppn(p3) & 3) == 0);
    buddy_result(p3 != NULL && aligned4, "allocate 3 pages -> 4-page aligned block");

    buddy_result(nr_free_pages() == total - 5, "free page counter after allocations");

    if (p3 != NULL) {
        free_pages(p3, 3);
    }
    if (p1 != NULL) {
        free_pages(p1, 1);
    }
    buddy_result(nr_free_pages() == total, "free restores the free page counter");

    /* Exhaust all memory, then give every page back. */
    size_t got = 0;
    /* static: the kernel stack is only 8 KB, keep this array in .bss */
    static struct Page *list[2048];
    while (got < 2048) {
        struct Page *q = alloc_pages(1);
        if (q == NULL) {
            break;
        }
        list[got ++] = q;
    }
    buddy_result(got == total && nr_free_pages() == 0, "exhaust memory until allocation fails");

    for (size_t i = 0; i < got; i ++) {
        free_pages(list[i], 1);
    }
    buddy_result(nr_free_pages() == total, "free everything and merge back");

    /* Restore the original manager and return the region to it. */
    pmm_manager = saved;
    for (size_t i = 0; i < total; i ++) {
        ClearPageProperty(base + i);
        ClearPageReserved(base + i);
        base[i].property = 0;
        set_page_ref(base + i, 0);
    }
    free_pages(base, total);

    cprintf("[Buddy] done: %d passed, %d failed\n", buddy_pass, buddy_fail);
}
