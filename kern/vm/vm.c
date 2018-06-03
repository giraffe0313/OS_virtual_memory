#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <spl.h>

#include <spinlock.h>
#include <proc.h>

/* Place your page table functions here */
#define PAGE_BITS  12
struct hash_table_v *h_pt = 0; 
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;


void vm_bootstrap(void)
{
        vaddr_t tmp = create_frame_table();
        h_pt = (struct hash_table_v *)tmp;

}


int hpt_hash(struct addrspace *as, vaddr_t faultaddr)
{
        uint32_t index;
        index = (((uint32_t )as) ^ (faultaddr >> PAGE_BITS)) % h_pt -> hash_frame_num;
        return index;
}

vaddr_t hpt_check(struct addrspace *as, vaddr_t faultaddr) {
        int h_index = hpt_hash(as, faultaddr);
        hashed_page_table *tmp = &h_pt -> hash_pt[h_index];
        while (tmp) {
                if (tmp -> process_ID == (uint32_t)as &&
                    tmp -> v_page_num == faultaddr) {
                            return (vaddr_t)tmp;
                    }
                tmp = tmp -> next;
        }
        return 0;
}

void delete_HPT(paddr_t as) {
        for (int i = 0; i < h_pt -> hash_frame_num; i++) {
                if ( h_pt -> hash_pt[i].process_ID == 0) {
                        continue;
                }
                if ( h_pt -> hash_pt[i].process_ID == (uint32_t)as) {
                        h_pt -> hash_pt[i].process_ID = 0;
                }
                hashed_page_table *pre_tmp = &h_pt -> hash_pt[i];
                if (!h_pt -> hash_pt[i].next) {
                        continue;
                }

                hashed_page_table *tmp = h_pt -> hash_pt[i].next;

                while (tmp) {
                        if (tmp -> process_ID == (uint32_t)as) {
                                hashed_page_table *tmp_next = tmp -> next;
                        
                                free_kpages((vaddr_t)tmp -> frame_num);
                                free_kpages((vaddr_t)tmp);
                                tmp = tmp_next;
                                pre_tmp -> next = tmp;
                        } else {
                                tmp = tmp -> next;
                                pre_tmp = pre_tmp -> next;
                        }
                }
        }
}

void add_HPT(struct addrspace *old, struct addrspace *new) {
        hashed_page_table *tmp = 0;
        vaddr_t frame_add = 0;
        for (int i = 0; i < h_pt -> hash_frame_num; i++) {
                if ( h_pt -> hash_pt[i].process_ID == 0) {
                        continue;
                }
                tmp = &h_pt -> hash_pt[i];
                while (tmp) {
                        if (tmp -> process_ID == (uint32_t)old) {
                                frame_add = alloc_kpages(1);
                                memcpy((void *)frame_add, (void *)tmp -> frame_num, PAGE_SIZE);
                                hpt_load(new, tmp -> v_page_num, frame_add, tmp->permission);
                        }
                        
                        tmp = tmp -> next;
                }
        }
}



void 
hpt_load(struct addrspace *as, vaddr_t faultaddr, vaddr_t frame_num, int permission) {
        int h_index = hpt_hash(as, faultaddr);

        if (h_pt -> hash_pt[h_index].process_ID == 0) {
                h_pt -> hash_pt[h_index].process_ID = (uint32_t)as;
                h_pt -> hash_pt[h_index].v_page_num = faultaddr;
                h_pt -> hash_pt[h_index].frame_num = frame_num;
                h_pt -> hash_pt[h_index].permission = permission;

        } else {
                hashed_page_table *tmp = & h_pt -> hash_pt[h_index];
    
                while (tmp -> next) {
                        tmp = tmp -> next;
                }
                hashed_page_table *new_page = (hashed_page_table *)alloc_kpages(1);

                new_page -> process_ID = (uint32_t)as;
                new_page -> v_page_num = faultaddr;
                new_page -> frame_num = frame_num;
                new_page -> permission = permission;
                new_page -> next = NULL;
                tmp -> next = new_page;
        }

}

vaddr_t check_region(struct addrspace *as, vaddr_t faultaddr) {
        p_memory_address *tmp = as -> head;
        while (tmp) {
                if (faultaddr >= tmp -> p_vaddr && faultaddr <= tmp -> p_upper) {
                        // kprintf("return value is %d\n", i);
                        return (vaddr_t)tmp; 
                }
                tmp = tmp -> next;
        }

        return 0;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{       
        if (faulttype == VM_FAULT_READONLY) {
                return EFAULT;
        }
        struct addrspace *as;
        as = proc_getas();
        vaddr_t result;
        int write_bit = 2;
        uint32_t entryhi, entrylo;
        // address in HPT
        faultaddress &= PAGE_FRAME;
        spinlock_acquire(&stealmem_lock);
        result = hpt_check(as, faultaddress);
        if (result) {
                entryhi = TLBHI_VPAGE & faultaddress;
                hashed_page_table *tmp = (hashed_page_table *) result;
                entrylo = (KVADDR_TO_PADDR(tmp -> frame_num) & TLBLO_PPAGE) | TLBLO_VALID;
                if (tmp -> permission & write_bit) {
                      entrylo |= TLBLO_DIRTY; 
                }

                int spl = splhigh();
                tlb_random(entryhi, entrylo);
                splx(spl);
                spinlock_release(&stealmem_lock);
                return 0;
        } else {
                result = check_region(as, faultaddress);
                if (result) {
                // address in region
                        vaddr_t frame_add = KVADDR_TO_PADDR(alloc_kpages(1));
                        p_memory_address *tmp = (p_memory_address *)result;
                        if (!tmp -> old) {
                                memset((void *)PADDR_TO_KVADDR(frame_add), 0, PAGE_SIZE);
                        } else {
                                
                                result = hpt_check(tmp -> old, faultaddress);
                                if (result) {
                                        hashed_page_table *tmp1 = (hashed_page_table *) result;
                                        memcpy((void *)PADDR_TO_KVADDR(frame_add), (void *)(tmp1 -> frame_num), PAGE_SIZE);
                                } 
                        }
                        // spinlock_release(&stealmem_lock);
                        hpt_load(as, faultaddress, PADDR_TO_KVADDR(frame_add), tmp -> permission);
                        entryhi = TLBHI_VPAGE & faultaddress;
                        entrylo = (frame_add & TLBLO_PPAGE) | TLBLO_VALID;
                        if (tmp -> permission & write_bit) {
                                entrylo |= TLBLO_DIRTY; 
                        }
                        int spl = splhigh();
                        tlb_random(entryhi, entrylo);
                        splx(spl);
                        spinlock_release(&stealmem_lock);
                        return 0;
                }
                if (as -> head -> old) {
                        vaddr_t frame_add = alloc_kpages(1);
                        if (!frame_add) {
                                spinlock_release(&stealmem_lock);
                                return ENOMEM;
                        }
                        
                        memset((void *)frame_add, 0, PAGE_SIZE);
                        hpt_load(as, faultaddress, frame_add, 7);
                        entryhi = TLBHI_VPAGE & faultaddress;
                        entrylo = (KVADDR_TO_PADDR(frame_add) & TLBLO_PPAGE) | TLBLO_VALID | TLBLO_DIRTY;
                        tlb_random(entryhi, entrylo);
                        int spl = splhigh();
                        splx(spl);
                        spinlock_release(&stealmem_lock);
                        return 0;
                }
                
        }
        
        spinlock_release(&stealmem_lock);
        panic("vm_fault hasn't been written yet\n");

        return EFAULT;
}

/*
 *
 * SMP-specific functions.  Unused in our configuration.
 */

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
        (void)ts;
        panic("vm tried to do tlb shootdown?!\n");
}

