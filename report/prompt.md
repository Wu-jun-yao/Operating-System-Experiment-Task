# lab2 提示词汇总

> 本文件按实验指导书要求的四段式结构（[PROMPT] / [RELY] / [GUARANTEE] / [SPECIFICATION]）记录 lab2 使用的提示词。

---

## 一、功能模块一 任务一：物理内存探测与 Page 描述符初始化

````markdown
[PROMPT]
**任务**：修改文件 kern/mm/pmm.c，实现静态函数 page_init()，完成"物理内存探测结果读取 → Page 描述符数组建立 → 已占用页面保留 → 空闲页区注册"这一整条初始化流程。

**操作要求**：你必须在项目中修改实际文件，而不是只展示代码。保留文件里已有的代码，只在 `//lab2 function1 YOUR CODE` 标记处补全实现。允许新增 static 辅助函数和模块内部使用的宏，但不要改动 pmm_init()、init_pmm_manager()、alloc_pages() 等已有接口的签名与逻辑。

**输出要求**：直接修改文件。实现必须与下面 [RELY]、[GUARANTEE]、[SPECIFICATION] 一致，不要引入与本任务无关的功能。

[RELY]
// 宏定义（kern/mm/memlayout.h、kern/mm/mmu.h、libs/defs.h）
#define KERNBASE                0xFFFFFFFFC0200000
#define PHYSICAL_MEMORY_END     0x88000000
#define PHYSICAL_MEMORY_OFFSET  0xFFFFFFFF40000000
#define KERNEL_BEGIN_PADDR      0x80200000
#define KERNEL_BEGIN_VADDR      0xFFFFFFFFC0200000
#define PGSIZE                  4096
#define PGSHIFT                 12
#define PPN(la)                 (((uintptr_t)(la)) >> PGSHIFT)
#define ROUNDUP(a, n)           // 向上对齐到 n 的整数倍
#define ROUNDDOWN(a, n)         // 向下对齐到 n 的整数倍
#define DRAM_BASE               0x80000000   // 定义在 libs/riscv.h

// 数据结构
struct Page {
    int ref;                 // 引用计数
    uint64_t flags;          // PG_reserved / PG_property 标志位
    unsigned int property;   // 空闲块包含的连续页数（块首页有效）
    list_entry_t page_link;  // 空闲链表节点
};

// 全局变量与符号
struct Page *pages;          // Page 描述符数组的首地址（虚拟地址）
size_t npage;                // 可管理物理页号的上界
uint64_t va_pa_offset;       // 虚拟地址与物理地址的偏移量
const size_t nbase;          // DRAM 起始地址对应的页号，DRAM_BASE / PGSIZE
extern char end[];           // 链接脚本导出的内核镜像结尾（虚拟地址）

// 可用函数
uint64_t get_memory_base(void);      // 设备树报告的物理内存起始地址
uint64_t get_memory_size(void);      // 设备树报告的物理内存大小
static void init_memmap(struct Page *base, size_t n);  // 注册空闲区
static inline struct Page *pa2page(uintptr_t pa);
#define PADDR(kva)  ((uintptr_t)(kva) - va_pa_offset)
void panic(const char *fmt, ...);
int cprintf(const char *fmt, ...);

// 已提供的宏
SetPageReserved(page) / ClearPageReserved(page) / PageReserved(page)
SetPageProperty(page) / ClearPageProperty(page) / PageProperty(page)
set_page_ref(page, val)

[GUARANTEE]
必须实现或修改的接口：
```c
static void page_init(void);
```

你可以根据需要添加：
- static 辅助函数（如打印内存布局、计算描述符数组范围）
- 模块内部使用的宏定义

[SPECIFICATION]
## page_init

**Pre-Condition**:
- init_pmm_manager() 已经执行，pmm_manager 指向当前选定的物理内存管理器；
- dtb_init() 已经执行，get_memory_base()/get_memory_size() 能返回设备树解析出的物理内存范围；
- va_pa_offset 尚未被设置，pages 与 npage 尚未被设置。

**Post-Condition**:
- va_pa_offset 等于 PHYSICAL_MEMORY_OFFSET；
- npage 等于 PPN(memory_base + memory_size)，且大于 nbase；
- pages 指向内核镜像结尾 end 之后、按 PGSIZE 向上对齐的地址，作为长度为 (npage - nbase) 的 Page 描述符数组的起点；
- 数组中每个描述符在空闲区建立之前都处于保留状态（PG_reserved 置位），ref 为 0、property 为 0、page_link 被初始化；
- 描述符数组之后第一个完整页到物理内存末端之前最后一个完整页之间的连续页面，通过 init_memmap() 交给当前 pmm_manager 管理；
- 屏幕上打印出设备树探测到的物理内存范围。

  **Case 1**:
  - 触发条件：设备树给出的大小为 0，或起始地址为 0，或起始地址低于 DRAM_BASE，或内存范围低于一页。
  - 执行行为：打印错误信息并调用 panic() 终止初始化；不得调用 init_memmap()，不得留下部分初始化状态。

  **Case 2**:
  - 触发条件：内存范围有效，且描述符数组之后的空闲区至少包含一个完整页。
  - 执行行为：按上述布局建立描述符数组，把描述符数组之后按页对齐的起点到内存末端按页向下对齐的终点之间的页面转换为 Page 指针，调用 init_memmap() 注册为初始空闲区，并打印内存范围。

  **Case 3**:
  - 触发条件：内存范围有效，但描述符数组之后的剩余空间不足一个完整页。
  - 执行行为：完成描述符数组的建立与保留，但不调用 init_memmap()，并打印提示信息。

**Requirements**:
- 必须把虚拟地址、物理地址、物理页号、Page 描述符地址区分清楚：pages 是虚拟地址，npage 是物理页号上界，PADDR() 只能用于内核虚拟地址；
- OpenSBI 占用区、内核镜像、Page 描述符数组、以及末尾不足一页的部分，都不得进入空闲区；
- 描述符数组的容量必须覆盖 [nbase, npage) 的全部页号；
- 不允许修改 pmm_init() 的既有调用顺序。
````

---

## 二、功能模块二 任务二：Best-Fit 连续物理内存分配器

````markdown
[PROMPT]
**任务**：修改文件 kern/mm/best_fit_pmm.c，实现完整的 Best-Fit 连续物理内存管理器；同时修改 kern/mm/pmm.c 的 init_pmm_manager()，把当前管理器从 default_pmm_manager 切换为 best_fit_pmm_manager。

**操作要求**：你必须在项目中修改实际文件，而不是只展示代码。kern/mm/best_fit_pmm.c 中已经给出 `basic_check()` 与 `best_fit_check()` 两个测试函数，**绝对不能修改它们**，只能补全 `//lab2 function2-2 YOUR CODE` 标记处的内容。

**输出要求**：直接修改文件。实现必须能通过文件中已有的两个测试函数，并保证 `make grade` 输出 `[Test 2] Best-Fit: PASS`。

[RELY]
// 数据结构（kern/mm/memlayout.h）
struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;   // 空闲块首页记录连续页数
    list_entry_t page_link;
};
typedef struct {
    list_entry_t free_list;
    unsigned int nr_free;
} free_area_t;

// 标志位与转换宏
PageProperty(page) / SetPageProperty(page) / ClearPageProperty(page)
PageReserved(page) / SetPageReserved(page) / ClearPageReserved(page)
set_page_ref(page, val)
le2page(le, page_link)
page2pa(page) / pa2page(pa) / npage / PGSIZE

// 链表接口（libs/list.h）
list_init(list)
list_empty(list)
list_add(listelm, elm)          // 插到 listelm 之后
list_add_before(listelm, elm)
list_del(elm)
list_next(le) / list_prev(le)

// 可参考的完整实现
kern/mm/default_pmm.c 中的 default_init / default_init_memmap /
default_alloc_pages / default_free_pages / default_nr_free_pages

// pmm_manager 接口（kern/mm/pmm.h）
struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};

[GUARANTEE]
必须实现或修改的接口：
```c
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);
static void best_fit_check(void);

const struct pmm_manager best_fit_pmm_manager;
```

同时在 kern/mm/pmm.c 的 init_pmm_manager() 中：
```c
pmm_manager = &best_fit_pmm_manager;
```

你可以根据需要添加：
- static 辅助函数
- 模块内部静态变量（必须提供名为 free_list 与 nr_free 的宏，因为测试函数直接使用这两个名字）

[SPECIFICATION]
## 模块内部状态

**Post-Condition**:
- 使用一个 static 的 free_area_t 变量保存全部空闲块；
- 通过 `#define free_list (free_area.free_list)` 与 `#define nr_free (free_area.nr_free)` 暴露给测试函数使用；
- 空闲链表按块起始 Page 的地址升序排列。

## best_fit_init
**Post-Condition**: 空闲链表被初始化为空，nr_free 为 0。

## best_fit_init_memmap
**Pre-Condition**: base 指向一段连续物理页中第一页的描述符，n > 0，且这些页当前都处于 PG_reserved 状态。
**Post-Condition**: 这 n 页的 flags 与 property 被清零、ref 置 0；base 的 property 置为 n 并设置 PG_property；该块按地址顺序插入空闲链表；nr_free 增加 n。

## best_fit_alloc_pages
**Pre-Condition**: n > 0。
**Post-Condition**: 成功时返回被分配块的首页描述符，返回的块至少有 n 页且已从空闲链表中摘除；失败时返回 NULL 且不改变任何空闲块。

  **Case 1**: n 大于 nr_free，直接返回 NULL。
  **Case 2**: 遍历空闲链表，在 property >= n 的块中选择 property 最小的一块；若有多个块大小相同，选择链表中靠前（地址较小）的一块。
  **Case 3**: 选中的块恰好为 n 页，整块摘除并清除 PG_property。
  **Case 4**: 选中的块大于 n 页，从块首分配 n 页，剩余部分以 base+n 为新的块首页，property 为原大小减 n，设置 PG_property 并按地址顺序插回链表；原块清除 PG_property。
  **Case 5**: 无论哪种成功路径，nr_free 都要减去 n。

## best_fit_free_pages
**Pre-Condition**: base 指向已分配连续 n 页的首页描述符，n > 0，这些页不是保留页、也不是空闲块首页。
**Post-Condition**: 这些页的 flags 与 property 清零、ref 置 0；base 的 property 置为 n 并设置 PG_property；按地址顺序插入空闲链表；nr_free 增加 n；随后尝试与地址相邻的前后空闲块合并，合并后只保留一个块首页并正确更新 property 与链表。

## best_fit_nr_free_pages
**Post-Condition**: 返回当前空闲页总数。

## best_fit_check
**Post-Condition**: 直接调用文件中已有的 `basic_check()` 与 `best_fit_check()` 相关逻辑，不修改测试函数本身。

**Requirements**:
- 不得修改 basic_check()、best_fit_check() 的任何一行；
- 所有分配出来的页面必须满足 PageProperty(p) 为假，空闲块首页必须满足 PageProperty(p) 为真；
- 空闲链表中所有块的 property 之和必须始终等于 nr_free；
- 允许 O(n) 线性扫描，不要求优化时间复杂度。
````

---

## 三、功能模块二 任务三：Buddy System 模块（第一份提示词）

````markdown
[PROMPT]
**任务**：新增文件 kern/mm/buddy_pmm.c 与 kern/mm/buddy_pmm.h，从零实现一个 Buddy System（伙伴系统）物理页分配模块，并按照现有 pmm_manager 框架注册为 buddy_pmm_manager，使其可以被 init_pmm_manager() 选用。

**操作要求**：你必须在项目中创建并修改实际文件，而不是只展示代码。不要修改 default_pmm.c、best_fit_pmm.c 以及它们对应的测试函数；不要破坏现有 Best-Fit 的评分路径。模块必须能被现有 Makefile 自动编译（kern/mm 目录下的 .c 会被自动收集）。

**输出要求**：直接创建文件并完成实现，保证 `make` 可以通过编译；提供的自检函数必须能够在 QEMU 中运行并输出明确的通过/失败信息。

[RELY]
// pmm_manager 接口（kern/mm/pmm.h）
struct pmm_manager {
    const char *name;
    void (*init)(void);
    void (*init_memmap)(struct Page *base, size_t n);
    struct Page *(*alloc_pages)(size_t n);
    void (*free_pages)(struct Page *base, size_t n);
    size_t (*nr_free_pages)(void);
    void (*check)(void);
};

// Page 结构（kern/mm/memlayout.h）
struct Page {
    int ref;
    uint64_t flags;
    unsigned int property;
    list_entry_t page_link;
};

// 可用宏与函数
PGSIZE / PGSHIFT / PPN / npage / nbase
page2pa(page) / pa2page(pa)
PageProperty / SetPageProperty / ClearPageProperty
PageReserved / SetPageReserved / ClearPageReserved
set_page_ref
le2page(le, page_link)
list_init / list_empty / list_add / list_add_before / list_del / list_next / list_prev
assert / panic / cprintf

[GUARANTEE]
必须实现或修改的接口：
```c
// kern/mm/buddy_pmm.h
extern const struct pmm_manager buddy_pmm_manager;
void buddy_pmm_test(void);

// kern/mm/buddy_pmm.c
static void buddy_init(void);
static void buddy_init_memmap(struct Page *base, size_t n);
static struct Page *buddy_alloc_pages(size_t n);
static void buddy_free_pages(struct Page *base, size_t n);
static size_t buddy_nr_free_pages(void);
static void buddy_check(void);
```

你可以根据需要添加：
- static 辅助函数（如阶数计算、伙伴地址计算、块拆分与合并）
- 模块内部宏与静态变量

[SPECIFICATION]
## 总体设计
**Post-Condition**:
- 以"阶"（order）为单位组织空闲块，阶为 k 的块包含 2^k 个连续物理页；
- 为每个阶维护一条空闲链表（例如 free_list[MAX_ORDER]），并记录每阶的空闲块数量；
- 所有空闲块首页设置 PG_property，非首页不设置；
- 保留页永远不进入任何空闲链表。

## buddy_init
**Post-Condition**: 所有阶的空闲链表初始化为空，空闲页计数为 0。

## buddy_init_memmap
**Pre-Condition**: base 指向一段连续 n 页的第一页描述符，n > 0，这些页当前为保留状态。
**Post-Condition**: 这 n 页的 ref 置 0、property 清零；把这段内存按 2 的幂拆成若干个对齐的块（从大到小）分别挂到对应阶的空闲链表上；nr_free 增加 n。

## buddy_alloc_pages
**Pre-Condition**: n > 0。
**Post-Condition**: 成功时返回分配块首页描述符，块大小为目标阶对应的 2^k 页（2^k >= n），并且该块与自身大小对齐；失败返回 NULL。

  **Case 1**: n 大于当前空闲页总数，返回 NULL。
  **Case 2**: 计算满足 2^k >= n 的最小阶 k；若该阶及更高阶都没有空闲块，返回 NULL。
  **Case 3**: 在最小的可用高阶块上不断二分，把后半块挂到低一阶的空闲链表，直到得到阶 k 的块；返回该块首页并清除 PG_property。
  **Case 4**: 成功分配后，nr_free 减去 2^k。

## buddy_free_pages
**Pre-Condition**: base 是之前由本分配器返回的块首页，n > 0。
**Post-Condition**: 释放的块被挂回对应阶的空闲链表，并尽可能与同阶的空闲伙伴块合并；合并后的块继续参与更高阶的合并；nr_free 增加实际释放的页数。

  **Case 1**: 伙伴块不存在、处于已分配状态、或阶数不同，则不做合并。
  **Case 2**: 伙伴块空闲且同阶，则把两块合并为高一阶的块，重复该过程直到无法继续。

## buddy_nr_free_pages
**Post-Condition**: 返回当前空闲页总数。

## buddy_pmm_test
**Post-Condition**: 在不影响 Best-Fit 正常评分的前提下，对伙伴系统做自检，至少覆盖：初始化后空闲页数正确、单页分配与释放、多页分配向上取整、块拆分后伙伴关系正确、释放后能合并回大块、空闲页耗尽时返回 NULL。每个检查项打印 PASS/FAIL。

**Requirements**:
- 块地址必须按自身大小对齐；
- 伙伴地址可用"块首页页号异或 (1 << order)"计算；
- 允许 O(n) 复杂度，不要求性能优化；
- 不允许修改 Best-Fit 模块及其测试。
````

---

## 四、功能模块二 任务三：Buddy System 测试（第二份提示词）

````markdown
[PROMPT]
**任务**：为上一条提示词生成的 Buddy System 分配器设计并实现一组测试函数，放在 kern/mm/buddy_pmm.c 中（或同目录的独立文件中），用于验证分配、释放、拆分、合并和边界行为是否正确。

**操作要求**：你必须在项目中修改实际文件，而不是只展示代码。测试只能调用 buddy_pmm_manager 暴露的接口或模块内部可测试的 static 函数，不得修改 default_pmm.c、best_fit_pmm.c 及其测试。

**输出要求**：测试必须能够编译运行，并在 QEMU 输出中给出每项检查的 PASS/FAIL，以及最终汇总。

[RELY]
// 被测模块接口
struct pmm_manager buddy_pmm_manager;
void buddy_pmm_test(void);

// 可观测状态
size_t nr_free_pages(void);
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
page2pa(page) / PPN(pa) / PGSIZE
PageProperty(page)

// 断言工具
assert / cprintf

[GUARANTEE]
必须实现或修改的接口：
```c
void buddy_pmm_test(void);
```

你可以根据需要添加：
- 若干 static 测试辅助函数
- 测试专用的统计变量

[SPECIFICATION]
## buddy_pmm_test

**Pre-Condition**: buddy_pmm_manager 已经注册，且初始空闲区已经通过 init_memmap 建立。

**Post-Condition**: 依次执行下列检查，每项打印结果，最后打印总体结果；测试结束后把分配出去的页全部释放，使空闲页数恢复到测试前的值。

  **Case 1（基本分配）**: 连续分配若干单页，检查返回非空、互不相同、页号在 [nbase, npage) 内、PG_property 为假。
  **Case 2（多页与对齐）**: 申请非 2 的幂的页数（例如 3 页），检查返回块的大小被向上取整、起始页号按块大小对齐。
  **Case 3（拆分）**: 申请一个较小块后，检查更高阶的空闲块数量是否按预期减少、低阶链表是否出现拆分出的伙伴块。
  **Case 4（释放与合并）**: 释放相邻的两个同阶伙伴块，检查它们是否合并成高一阶的块，且 nr_free 恢复正确。
  **Case 5（耗尽）**: 反复分配直到返回 NULL，检查此时 nr_free 为 0 或小于最小块大小，并且不会崩溃。
  **Case 6（恢复）**: 把 Case 5 中分配的块按逆序释放，检查 nr_free 回到测试前的值，且空闲链表结构仍然合法。

**Requirements**:
- 每个 Case 都要有独立的 PASS/FAIL 输出，便于定位问题；
- 测试不得依赖固定物理地址，必须通过 page2pa/PPN 计算；
- 测试结束后不得留下泄漏的页面；
- 测试函数不能被评分脚本误触发，必须由显式的调试入口调用。
````

---

## 五、扩展任务 Challenge：SLUB 任意大小内存分配

### 5.1 第一份提示词：从零生成 SLUB 模块并接入内核

````markdown
[PROMPT]
**任务**：新增文件 kern/mm/slub.c 与 kern/mm/slub.h，从零实现一个简化的 SLUB 内存对象分配器，并接入现有内核。

**操作要求**：你必须在项目中创建并修改实际文件，而不是只展示代码。不要修改 default_pmm.c、best_fit_pmm.c、buddy_pmm.c 及其测试函数；不要破坏 `make grade` 的评分路径。模块必须能被现有 Makefile 自动编译（kern/mm 下的 .c 会被自动收集）。

**输出要求**：直接创建文件并完成实现，保证 `make` 可以通过编译；提供的自检函数必须能够在 QEMU 中运行并输出明确的通过/失败信息。

[RELY]
// 第一层：页级分配器（kern/mm/pmm.h）
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *base, size_t n);
uintptr_t page2pa(struct Page *page);
extern uint64_t va_pa_offset;

// 常量
#define PGSIZE 4096

// 可用的库函数
void *memset(void *s, int c, size_t n);
int cprintf(const char *fmt, ...);
void panic(const char *fmt, ...);

// 内核虚拟地址 = 物理地址 + va_pa_offset

[GUARANTEE]
必须实现或修改的接口：
```c
// kern/mm/slub.h
void slub_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void slub_test(void);

// kern/mm/slub.c
static void slub_init_cache(struct slub_cache *cache, size_t objsize);
static struct slub_cache *slub_cache_for(size_t size);
static struct slub_slab *slub_new_slab(struct slub_cache *cache);
static void slub_test(void);
```

你可以根据需要添加：
- static 辅助函数
- 模块内部宏与静态变量

[SPECIFICATION]
## 总体设计
**Post-Condition**:
- 实现两层架构：第一层是页级分配（调用 `alloc_pages(1)` 获取 4KB 页），第二层是在页内切分为固定大小的对象；
- 维护一组 size class（建议 16、32、64、128、256、512、1024、2048 字节），每个 size class 对应一个 cache；
- 每个 slab 占一个 4KB 页，页首存放 slab 头（magic、所属 cache 指针、inuse、对象数），之后按对象大小切分；
- 每个 cache 维护一条空闲对象链表（利用对象本身的存储空间存放 next 指针）和空闲对象计数。

## slub_init
**Post-Condition**: 所有 cache 的 objsize 被设置，空闲链表为空，计数为 0；模块标记为已初始化。

## kmalloc
**Pre-Condition**: size 为请求的字节数。
**Post-Condition**: 成功时返回一个至少 size 字节、16 字节对齐的对象指针；失败返回 NULL。

  **Case 1**: size 为 0 或大于最大 size class，返回 NULL。
  **Case 2**: 找到第一个 objsize >= size 的 cache。
  **Case 3**: 该 cache 有空闲对象时，从空闲链表弹出一个对象并返回。
  **Case 4**: 该 cache 没有空闲对象时，调用 `alloc_pages(1)` 申请一个新页，初始化 slab 头并把页内对象全部串入空闲链表，再弹出一个返回。
  **Case 5**: 页分配失败时返回 NULL。

## kfree
**Pre-Condition**: ptr 是之前由 kmalloc 返回的指针，或为 NULL。
**Post-Condition**: 对象被放回其所属 cache 的空闲链表，slab 的 inuse 减一；传入 NULL 时直接返回。

  **Case 1**: 通过 ptr 所在页的首地址找到 slab 头，校验 magic。
  **Case 2**: magic 不匹配时说明不是本分配器的对象，直接返回，不破坏内存。
  **Case 3**: magic 匹配时，把对象插回对应 cache 的空闲链表头部，计数加一。

## slub_test
**Post-Condition**: 依次执行基本分配、尺寸取整、数据完整性、跨 slab 分配、释放复用、非法参数等检查，每项打印 PASS/FAIL，最后打印汇总。

**Requirements**:
- 不得修改现有 PMM 模块及其测试；
- 对象地址必须 16 字节对齐；
- 允许简化实现（不要求 NUMA、不要求空闲 slab 回收、不要求 per-CPU cache），但必须体现两层架构；
- 测试结束后不得留下泄漏的对象。
````

### 5.2 第二份提示词：为 SLUB 设计测试

````markdown
[PROMPT]
**任务**：为上一条提示词生成的 SLUB 分配器设计并实现一组测试函数，放在 kern/mm/slub.c 中，用于验证不同大小对象的分配、释放和复用是否正确。

**操作要求**：你必须在项目中修改实际文件，而不是只展示代码。测试只能调用 `kmalloc`、`kfree`、`slub_init` 等模块接口，不得修改其他内存管理模块。

**输出要求**：测试必须能够编译运行，并在 QEMU 输出中给出每项检查的 PASS/FAIL，以及最终汇总。

[RELY]
// 被测接口
void slub_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);

// 可观测手段
cprintf / memset
uintptr_t（判断对齐）

// size class
16 / 32 / 64 / 128 / 256 / 512 / 1024 / 2048

[GUARANTEE]
必须实现或修改的接口：
```c
void slub_test(void);
```

你可以根据需要添加：
- 若干 static 测试辅助函数
- 测试专用的静态数组与计数变量

[SPECIFICATION]
## slub_test

**Pre-Condition**: SLUB 模块已经初始化，当前 PMM 中有足够的空闲页。

**Post-Condition**: 依次执行下列检查，每项打印 PASS/FAIL，最后打印总体结果；测试结束后释放所有申请的对象。

  **Case 1（非法参数）**: `kmalloc(0)` 和 `kmalloc(2049)` 都应返回 NULL。
  **Case 2（基本分配）**: 申请 8、20、100 字节的对象，均应非空、互不相同、16 字节对齐。
  **Case 3（数据完整性）**: 向三个对象分别写入不同字节模式，再逐字节读回校验，确认对象之间互不覆盖。
  **Case 4（尺寸取整）**: 申请 8 字节应落在 16 字节 class，申请 20 字节应落在 32 字节 class；可通过对象间距或连续分配行为间接验证。
  **Case 5（跨 slab）**: 连续申请 300 个 16 字节对象，应超过单个 slab 的容量，从而触发至少两个 slab；检查全部非空、地址互不相同、数据各自保持。
  **Case 6（释放与复用）**: 释放一个对象后立即按同样大小重新申请，应复用同一个地址（空闲链表 LIFO）；释放全部对象后再次申请应成功。
  **Case 7（多尺寸混合）**: 交替申请 64、128、512 字节的对象，确认不同 size class 互不干扰，释放后各自可复用。

**Requirements**:
- 每个 Case 都要有独立的 PASS/FAIL 输出；
- 测试数组必须放在静态区，避免占用只有 8KB 的内核栈；
- 测试结束后不得留下未释放对象；
- 不得依赖固定物理地址，所有地址从 kmalloc 返回值获得。
````

---

---

## 六、调试阶段补充提示词

### 6.1 修复 `page_init()` 中 `end` 未声明

````markdown
[PROMPT]
**任务**：修复 kern/mm/pmm.c 中 page_init() 的编译错误 `'end' undeclared`。

**操作要求**：只在 page_init() 内部补充必要的声明，不要修改函数原有逻辑，不要改动其他文件。

**输出要求**：直接修改文件并保证 `make` 通过。

[RELY]
// 链接脚本 tools/kernel.ld 中导出了以下符号
PROVIDE(etext = .);
PROVIDE(edata = .);
PROVIDE(end = .);

// 内核中已有代码的写法（kern/init/init.c）
extern char edata[], end[];

[GUARANTEE]
```c
static void page_init(void);
```

[SPECIFICATION]
## page_init
**Pre-Condition**: 函数内使用了链接脚本导出的符号 `end`，但 C 编译器没有它的声明。
**Post-Condition**: 在函数作用域内补上 `extern char end[];`，使 `end` 可以被当作字符数组首地址参与指针运算；其余代码保持不变。
**Requirements**: 不得把 `end` 定义成新的变量，只能声明它是外部符号。
````

### 6.2 修复 Buddy System 的 `nr_free` 记账错误

````markdown
[PROMPT]
**任务**：修复 kern/mm/buddy_pmm.c 中 buddy_alloc_pages() 的空闲页计数错误。自检函数 buddy_pmm_test() 报告 "exhaust memory until allocation fails" 与 "free everything and merge back" 两项失败。

**操作要求**：修改实际文件；只调整 buddy_alloc_pages() 中与 nr_free 记账相关的语句，不改变拆分算法本身，不修改测试函数。

**输出要求**：修复后 `buddy_pmm_test()` 的 7 项检查应全部 PASS，并保证 `make grade` 仍然是 20/20。

[RELY]
// 模块状态
static list_entry_t free_list[MAX_ORDER];
static size_t nr_free;
static size_t free_blocks[MAX_ORDER];

// 辅助函数：把一个块挂回对应阶的空闲链表，并让 nr_free 增加 2^order
static void buddy_add_block(struct Page *base, size_t order);

// 分配流程
// 1. 从某个阶 found 的空闲链表中取出一个块
// 2. 不断二分，把后半块通过 buddy_add_block() 挂回低一阶
// 3. 返回阶 order 的块

[GUARANTEE]
```c
static struct Page *buddy_alloc_pages(size_t n);
```

[SPECIFICATION]
## buddy_alloc_pages
**Pre-Condition**: 已经从阶 found 的链表中取出一个大小为 2^found 页的块，即将把它拆分成 2^order 页。

**Post-Condition**: 分配完成后，nr_free 的净减少量必须恰好等于 2^order。

  **Case 1**: 取出整块时，nr_free 先减少 2^found（整块离开空闲池）。
  **Case 2**: 每次拆分出一半时，通过 buddy_add_block() 把这半块重新计入 nr_free。
  **Case 3**: 拆分结束后不再额外扣减 nr_free，避免重复扣减。

**Requirements**:
- 不得修改 buddy_add_block() 的语义；
- 修复后必须用 buddy_pmm_test() 验证耗尽与恢复两条路径；
- 不得影响 Best-Fit 的评分路径。
````

---

## 七、分支任务：用 GDB 调试 QEMU 的地址翻译过程

### 7.1 总体方案：三个终端与双重 GDB

````markdown
[PROMPT]
**任务**：我需要调试"正在运行我的 ucore 内核的 QEMU 源码"，观察 CPU 访问一个虚拟地址时，QEMU 是如何模拟 TLB 查找和 Sv39 页表遍历的。请帮我设计一套可执行的操作流程。

**操作要求**：请明确说明需要几个终端、每个终端执行什么、先后顺序是什么，以及两个 GDB 分别调试什么对象、为什么不能用同一个 GDB。

**输出要求**：给出按顺序执行的命令清单，并解释每一步的目的。

[RELY]
- 内核用 QEMU 4.1.1 模拟 RISC-V virt 机器；
- 内核对 QEMU 暴露 GDB stub（`-s -p 1234`），可用 `riscv64-unknown-elf-gdb` 调试；
- QEMU 本身是运行在 x86 上的普通 Linux 进程，可以用系统自带 `gdb` 调试；
- 内核已经开启 Sv39 分页，`satp` 中保存着三级页表基址。

[GUARANTEE]
需要产出一套流程，至少包含：
1. 启动 QEMU（暂停状态）；
2. 用宿主机 GDB 附加/启动 QEMU 进程；
3. 用 RISC-V GDB 连接 QEMU 的 GDB stub；
4. 在 QEMU 源码中设置断点并观察地址翻译。

[SPECIFICATION]
## 流程
**Pre-Condition**: 已编译好带调试信息（`-g`）的 QEMU，并有可运行的内核镜像。
**Post-Condition**: 能在一条访存指令执行时，同时在两个层面上停下来：RISC-V GDB 停在 ucore 的指令处，宿主机 GDB 停在 QEMU 的翻译函数处。

**Case 1（宿主机 GDB）**: 用于调试 QEMU 自身的 C 代码，必须能读到 `target/riscv/cpu_helper.c` 等源码。
**Case 2（RISC-V GDB）**: 用于调试被模拟的 ucore，地址是 RISC-V 虚拟地址。
**Case 3（顺序）**: 必须先让 QEMU 处于暂停或已附加状态，再让 RISC-V GDB 连接，否则 1234 端口无人监听。

**Requirements**:
- 说明为什么宿主机 GDB 不能用 `riscv64-unknown-elf-gdb`；
- 说明 `handle SIGPIPE nostop noprint` 的作用；
- 给出退出顺序，避免端口被占用。
````

### 7.2 编译带调试信息的 QEMU（不覆盖已安装版本）

````markdown
[PROMPT]
**任务**：我需要在 QEMU 4.1.1 的源码目录里重新编译一个"带调试信息"的 qemu-system-riscv64，用于 GDB 调试 QEMU 源码，但不希望覆盖系统里已经安装好的正式版。

**操作要求**：给出从清理、配置、编译到验证的完整命令，并说明每一步的作用；不要执行 `make install`。

**输出要求**：说明编译产物在哪里、如何让 ucore 的 Makefile 临时使用这个调试版。

[RELY]
- 源码目录：`~/qemu-4.1.1`
- 已安装版本：`/usr/local/bin/qemu-system-riscv64`
- 之前的配置命令：`./configure --target-list=riscv32-softmmu,riscv64-softmmu`
- ucore 的 Makefile 中有 `ifndef QEMU / QEMU := qemu-system-riscv64 / endif`，支持命令行覆盖

[GUARANTEE]
需要给出：
```bash
make distclean
./configure --target-list=riscv32-softmmu,riscv64-softmmu --enable-debug
make -j$(nproc)
```
以及产物路径 `riscv64-softmmu/qemu-system-riscv64`。

[SPECIFICATION]
## 编译
**Pre-Condition**: 源码目录中有旧的编译产物。
**Post-Condition**: 生成一个新的 `riscv64-softmmu/qemu-system-riscv64`，包含调试符号；`/usr/local/bin/qemu-system-riscv64` 保持不变。

**Case 1**: 执行 `make distclean` 清除旧产物，避免配置缓存影响。
**Case 2**: `--enable-debug` 会加上 `-g` 等调试选项，使 GDB 能显示源码行。
**Case 3**: 不执行 `make install`，因此系统中同时存在正式版和调试版两个 QEMU。

**Requirements**:
- 说明如何验证：`file riscv64-softmmu/qemu-system-riscv64` 应显示 not stripped；
- 说明用 `make debug QEMU=/绝对路径/qemu-system-riscv64` 覆盖，而不必修改 Makefile。
````

### 7.3 在 QEMU 源码中定位地址翻译代码

````markdown
[PROMPT]
**任务**：请在 QEMU 4.1.1 的源码中找到 RISC-V 地址翻译的关键代码，说明从"CPU 执行访存指令"到"完成虚拟地址到物理地址转换"的调用路径，并指出其中关键的分支语句。

**操作要求**：不要泛泛而谈，要给出具体文件、函数名和行号；重点说明 TLB 命中/未命中的分支，以及 Sv39 三级页表的循环。

**输出要求**：给出一张调用路径图（文字版即可），并标出适合下断点的函数入口。

[RELY]
- 源码目录：`~/qemu-4.1.1`
- 重点目录：`target/riscv/`、`accel/tcg/`
- 候选文件：`target/riscv/cpu_helper.c`、`target/riscv/cpu.c`、`accel/tcg/cputlb.c`、`accel/tcg/softmmu_template.h`
- 关键函数候选：`get_physical_address()`、`riscv_cpu_tlb_fill()`、`tlb_fill()`

[GUARANTEE]
需要说明：
```c
static int get_physical_address(CPURISCVState *env, hwaddr *physical,
                                int *prot, target_ulong address,
                                int access_type, int mmu_idx, bool two_stage);
bool riscv_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                        MMUAccessType access_type, int mmu_idx,
                        bool probe, uintptr_t retaddr);
```
以及 softmmu 的 `tlb_fill` 路径。

[SPECIFICATION]
## 调用路径
**Pre-Condition**: 内核已开启 Sv39，`satp` 的 MODE 字段为 8。
**Post-Condition**: 能说明一条访存指令如何触发 TLB 查找、未命中后如何进入页表遍历、遍历完成后如何填充 TLB。

**Case 1（TLB 命中）**: 由软件 TLB（QEMU 的 softmmu 缓存）直接给出物理地址，不再调用页表遍历。
**Case 2（TLB 未命中）**: 调用 `riscv_cpu_tlb_fill()`，再进入 `get_physical_address()`，按 Sv39 的三级索引逐级读取页表项。
**Case 3（页表项无效）**: 返回错误，触发缺页异常或访问异常。

**Requirements**:
- 指出 Sv39 中 VPN[2]、VPN[1]、VPN[0] 分别对应虚拟地址的哪几位；
- 指出页表项中 PPN 与权限位（V/R/W/X）如何被检查；
- 给出适合设置断点的函数名。
````

### 7.4 条件断点：避免每条访存都停下来

````markdown
[PROMPT]
**任务**：QEMU 的地址翻译函数对每一条访存指令都会调用，如果直接在函数入口下普通断点，会被打断无数次。请给出一种只在我们关心的虚拟地址上停下来的方案。

**操作要求**：说明条件断点的写法，以及如何确定"我们关心的地址"确实会被访问到。

**输出要求**：给出可以直接在 GDB 中执行的命令。

[RELY]
- `get_physical_address()` 的参数中包含 `target_ulong address`；
- 内核 C 入口 `kern_init` 的虚拟地址可以通过 `riscv64-unknown-elf-nm bin/kernel | grep kern_init` 得到；
- 该地址在启动时一定会被取指访问。

[GUARANTEE]
需要给出类似：
```gdb
break get_physical_address if address == 0xffffffffc02000d8
```
或 `break riscv_cpu_tlb_fill if address == ...` 的方案。

[SPECIFICATION]
## 条件断点
**Pre-Condition**: 已知目标虚拟地址，且该地址确实会被内核访问。
**Post-Condition**: 只有访问该地址时才中断，其余访存自动通过。

**Case 1**: 断在 `get_physical_address()`，可以观察完整的页表遍历。
**Case 2**: 断在 `riscv_cpu_tlb_fill()`，可以观察 TLB 未命中后如何进入翻译流程。
**Case 3**: 若断点始终不命中，说明该地址没有被访问，或访问走了另一条路径（例如取指与数据访问使用不同 mmu_idx）。

**Requirements**:
- 说明 GDB 条件断点的性能开销；
- 说明如何用 `info break`、`disable` 管理断点。
````

### 7.5 QEMU 模拟的 TLB 与真实 CPU TLB 的区别

````markdown
[PROMPT]
**任务**：请比较 QEMU 中软件模拟的 TLB 与真实 RISC-V CPU 中的 TLB，在结构、查找方式、失效机制和一致性维护上的区别。

**操作要求**：结合 QEMU 源码中的具体实现说明，不要只讲教科书概念。

**输出要求**：列出至少 4 条区别，并说明为什么 QEMU 可以这样简化。

[RELY]
- QEMU 的 TLB 实现在 `accel/tcg/cputlb.c`，是 softmmu 的一部分；
- 真实 CPU 的 TLB 由硬件维护，对软件透明；
- RISC-V 的 `sfence.vma` 用于刷新 TLB；
- QEMU 使用 `tlb_flush()` 系列函数在软件中模拟失效。

[GUARANTEE]
需要说明：
1. 实现介质：硬件 SRAM vs 软件哈希表/数组；
2. 容量与替换策略：硬件固定组相联 vs 软件可配置；
3. 查找时机：硬件在 MMU 内部自动完成 vs 软件在地址翻译 helper 中完成；
4. 失效方式：`sfence.vma` 触发硬件刷新 vs 调用 `tlb_flush()`；
5. 一致性：硬件需要维护 ASID/VMID，QEMU 用软件标记；
6. 性能模型：QEMU 的 TLB 只影响模拟速度，不影响程序语义。

[SPECIFICATION]
## 对比
**Pre-Condition**: 理解 QEMU 是"用软件模拟硬件"。
**Post-Condition**: 能说明在 QEMU 里"TLB 命中"只是一次软件缓存查找，而在真实 CPU 里是硬件并行比较。

**Case 1（未开启分页）**: 地址翻译走恒等映射，QEMU 仍会经过 softmmu 路径，但不会做三级页表遍历。
**Case 2（开启分页）**: 需要 `get_physical_address()` 完成页表遍历，并把结果写入软件 TLB。

**Requirements**:
- 指出 QEMU 的 TLB 不是"模拟一颗真实的 TLB 芯片"，而是"为了加速模拟而存在的缓存"；
- 说明这对实验观察意味着什么（例如断点可能因为 TLB 命中而不触发）。
````
