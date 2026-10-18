#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>

/* Largest object size supported by the simplified SLUB allocator. */
#define SLUB_MAX_OBJSIZE 2048

void slub_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void slub_test(void);

#endif /* !__KERN_MM_SLUB_H__ */
