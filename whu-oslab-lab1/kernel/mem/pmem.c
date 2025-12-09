#include "mem/pmem.h"
#include "lib/print.h"
#include "lib/lock.h"
#include "lib/str.h"



#define PGROUNDUP(x)   (((x) + PGSIZE - 1) & ~(PGSIZE - 1))
#define PGROUNDDOWN(x) ((x) & ~(PGSIZE - 1))
#define PGSIZE 4096


// 物理页节点
typedef struct page_node {
    struct page_node* next;
} page_node_t;

// 许多物理页构成一个可分配的区域
typedef struct alloc_region {
    uint64 begin;          // 起始物理地址
    uint64 end;            // 终止物理地址
    spinlock_t lk;         // 自旋锁(保护下面两个变量)
    uint32 allocable;      // 可分配页面数    
    page_node_t list_head; // 可分配链的链头节点
} alloc_region_t;

// 内核和用户可分配的物理页分开
static alloc_region_t kern_region, user_region;



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

// 物理内存初始化
void pmem_init() {
 

    uint64 free_lo = PGROUNDUP( (uint64)KERNEL_DATA >  (uint64)ALLOC_BEGIN ? (uint64)KERNEL_DATA : (uint64)ALLOC_BEGIN);
    uint64 free_hi = PGROUNDDOWN((uint64)ALLOC_END);
    uint64 mid = free_lo + (free_hi - free_lo) / 3;
    // 内核页区
    kern_region.begin = (uint64)ALLOC_BEGIN;
    kern_region.end   = (uint64)mid;
    spinlock_init(&kern_region.lk, "kern_pmem");
    build_free_list(&kern_region, kern_region.begin, kern_region.end);

    // 用户页区
    user_region.begin = mid;
    user_region.end   = (uint64)ALLOC_END;
    spinlock_init(&user_region.lk, "user_pmem");
    build_free_list(&user_region, user_region.begin, user_region.end);
}

// 返回一个可分配的干净物理页
// 失败则panic锁死
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

// 释放物理页
// 失败则panic锁死
void pmem_free(uint64 page, bool in_kernel) {
    alloc_region_t* r = in_kernel ? &kern_region : &user_region;

    page_node_t* node = (page_node_t*)page;
    spinlock_acquire(&r->lk);
    node->next = r->list_head.next;
    r->list_head.next = node;
    r->allocable++;
    spinlock_release(&r->lk);
}