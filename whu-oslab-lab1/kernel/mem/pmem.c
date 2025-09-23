#include "mem/pmem.h"
#include "lib/lock.h"

#define PGROUNDUP(x)   (((x) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(x) ((x) & ~(PGSIZE - 1))
#define PGSIZE 4096
#define KERNEL_PAGES 2048

// 物理页节点：占据每个物理页的前8字节
typedef struct page_node {
    struct page_node* next;
} page_node_t;

// 物理页分配区域
typedef struct alloc_region {
    uint64 begin;         // 起始物理地址
    uint64 end;           // 终止物理地址
    spinlock_t lk;        // 自旋锁
    uint32 allocable;     // 可分配页面数
    page_node_t list_head;// 链表头
} alloc_region_t;

// 内核和用户物理页区域
static alloc_region_t kern_region, user_region;

// 辅助函数：把[lo, hi)范围内的物理页挂到链表
static void build_free_list(alloc_region_t* r, uint64 lo, uint64 hi) {
    r->allocable = 0;
    r->list_head.next = NULL;

    for (uint64 p = PGROUNDUP(lo); p + PGSIZE <= PGROUNDDOWN(hi); p += PGSIZE) {
        page_node_t* node = (page_node_t*)p;
        node->next = r->list_head.next;
        r->list_head.next = node;
        r->allocable++;
    }
}

// 初始化物理内存
void pmem_init() {
 
    // 内核页区
    kern_region.begin = (uint64)ALLOC_BEGIN;
    kern_region.end   = (uint64)ALLOC_BEGIN + KERNEL_PAGES * PGSIZE;
    spinlock_init(&kern_region.lk, "kern_pmem");
    build_free_list(&kern_region, kern_region.begin, kern_region.end);

    // 用户页区
    user_region.begin = kern_region.end;
    user_region.end   = (uint64)ALLOC_END;
    spinlock_init(&user_region.lk, "user_pmem");
    build_free_list(&user_region, user_region.begin, user_region.end);
}

// 分配一个物理页
void* pmem_alloc(bool in_kernel) {
    alloc_region_t* r = in_kernel ? &kern_region : &user_region;

    spinlock_acquire(&r->lk);
    page_node_t* node = r->list_head.next;
    if (node) {
        r->list_head.next = node->next;
        r->allocable--;
    }
    spinlock_release(&r->lk);

    return node ? (void*)node : NULL;
}

// 释放一个物理页
void pmem_free(uint64 page, bool in_kernel) {
    alloc_region_t* r = in_kernel ? &kern_region : &user_region;

    page_node_t* node = (page_node_t*)page;
    spinlock_acquire(&r->lk);
    node->next = r->list_head.next;
    r->list_head.next = node;
    r->allocable++;
    spinlock_release(&r->lk);
}