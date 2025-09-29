#include "mem/vmem.h"      
#include "mem/pmem.h"      // pmem_alloc / pmem_free




pgtbl_t kernel_pgtbl = 0;

// ---------- vm_print ----------

void vm_print(pgtbl_t pgtbl) {
    uint64 scan_limit = 1ul << 38;
    for (uint64 va = 0; va < scan_limit; va += PGSIZE) {
        pte_t* pte = vm_getpte(pgtbl, va, false);
        if (pte && (*pte & PTE_V)) {
            uint64 pa = PTE_TO_PA(*pte);
            int flags = PTE_FLAGS(*pte);
            // printf
            printf("VA 0x%p -> PA 0x%p flags 0x%x\n", va, pa, flags);
        }
    }
}

/* ---------- vm_getpte ----------
 */
pte_t* vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc) {
    if (va >= VA_MAX) {
        panic("vm_getpte: va out of range");
        return NULL;
    }

    for (int level = 2; level > 0; level--) {
        uint64 idx = VA_TO_VPN(va, level);
        pte_t* pte = &pgtbl[idx];

        if (*pte & PTE_V) {
            uint64 child_pa = PTE_TO_PA(*pte);
            pgtbl = (pgtbl_t)child_pa;
        } else {
            if (!alloc) return NULL;
            void* newpage = pmem_alloc(true);
            if (!newpage) {
                printf("vm_getpte: pmem_alloc failed for VA=0x%lx\n", va);
                return NULL;
            }
            memset(newpage, 0, PGSIZE);
            *pte = PA_TO_PTE((uint64)newpage) | PTE_V;
            pgtbl = (pgtbl_t)newpage;
        }
    }

    return &pgtbl[VA_TO_VPN(va, 0)];
}


/* ---------- vm_mappages ----------
 */
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm) {
    uint64 start = va;
    uint64 end = va + len;

    while (start < end) {
        uint64 a = PG_ROUND_DOWN(start);
        pte_t* pte = vm_getpte(pgtbl, a, true);
        if (!pte) {
            panic("vm_mappages: vm_getpte failed");
        }

        if (*pte & PTE_V) {
            // 改进：允许覆盖已有映射
        }

        *pte = PA_TO_PTE(pa) | (perm & 0x3FF) | PTE_V;

        start = a + PGSIZE;
        pa += PGSIZE;
    }
}



/* ---------- vm_unmappages ----------*/
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit) {
    uint64 a = PG_ROUND_DOWN(va);
    uint64 last = PG_ROUND_DOWN(va + len - 1);

    for (;;) {
        pte_t* pte = vm_getpte(pgtbl, a, false); 
        if (pte == NULL || (*pte & PTE_V) == 0) {
            panic("vm_unmappages: not mapped");
        }
        if (PTE_CHECK(*pte)) {
            panic("vm_unmappages: not a leaf pte");
        }
        if (freeit) {
            uint64 pa = PTE_TO_PA(*pte);
            pmem_free(pa, true);
        }

        *pte = 0;

        if (a == last)
            break;

        a += PGSIZE;
    }
}

/* ---------- kvm_init / kvm_inithart ----------

 */

#define REG_BASE   0x10000000UL               
#define REG_SIZE   0x10000000UL             
#define MEM_BASE   0x80000000UL                
#define MEM_SIZE   (128UL * 1024 * 1024)       // 128MB

void kvm_init() {
    kernel_pgtbl = (pgtbl_t)pmem_alloc(true);
    if (!kernel_pgtbl) {
        panic("kvm_init: pmem_alloc failed");
    }
   
    memset(kernel_pgtbl, 0, PGSIZE);

   
    vm_mappages(kernel_pgtbl, REG_BASE, REG_BASE, REG_SIZE, PTE_R | PTE_W);


    vm_mappages(kernel_pgtbl, MEM_BASE, MEM_BASE, MEM_SIZE, PTE_R | PTE_W | PTE_X);
}

void kvm_inithart() {
    w_satp(MAKE_SATP(kernel_pgtbl));
    // TLB flush
    sfence_vma();
}
