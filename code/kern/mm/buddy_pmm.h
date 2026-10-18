#ifndef __KERN_MM_BUDDY_PMM_H__
#define __KERN_MM_BUDDY_PMM_H__

#include <pmm.h>

extern const struct pmm_manager buddy_pmm_manager;

/* Run the buddy-system self test (prints PASS/FAIL lines). */
void buddy_pmm_test(void);

#endif /* !__KERN_MM_BUDDY_PMM_H__ */
