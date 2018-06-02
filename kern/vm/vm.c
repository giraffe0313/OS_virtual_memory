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
        /* Initialise VM sub-system.  You probably want to initialise your 
           frame table here as well.
        */
        vaddr_t tmp = create_frame_table();
        h_pt = (struct hash_table_v *)tmp;
        kprintf("hash pointer is %p\n", h_pt -> hash_pt);

        
}

// static
// void
// as_zero_region(paddr_t paddr, unsigned npages)
// {
// 	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
// }

int hpt_hash(struct addrspace *as, vaddr_t faultaddr)
{
        uint32_t index;

        index = (((uint32_t )as) ^ (faultaddr >> PAGE_BITS)) % h_pt -> hash_frame_num;
        // kprintf("hash table address is %p\n", (void *)h_pt);
        return index;
}

vaddr_t hpt_check(struct addrspace *as, vaddr_t faultaddr) {
        int h_index = hpt_hash(as, faultaddr);
        hashed_page_table *tmp = &h_pt -> hash_pt[h_index];

        int spl = splhigh();
        while (tmp) {
                if (tmp -> process_ID == (uint32_t)as &&
                    tmp -> v_page_num == faultaddr) {
                            splx(spl);
                        //     kprintf("find this page\n!");
                            return (vaddr_t)tmp;
                    }
                tmp = tmp -> next;
        }
        splx(spl);
        return 0;
}

void 
hpt_load(struct addrspace *as, vaddr_t faultaddr, vaddr_t frame_num, int permission) {
        int h_index = hpt_hash(as, faultaddr);
        int spl = splhigh();
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
                tmp -> next = new_page;
        }

        splx(spl);
        // kprintf("hpt_load: load in %d, next")
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
        result = hpt_check(as, faultaddress);
        if (result) {
                spinlock_acquire(&stealmem_lock);
                entryhi = TLBHI_VPAGE & faultaddress;
                hashed_page_table *tmp = (hashed_page_table *) result;
                entrylo = (KVADDR_TO_PADDR(tmp -> frame_num) & TLBLO_PPAGE) | TLBLO_VALID;
                if (tmp -> permission & write_bit) {
                      entrylo |= TLBLO_DIRTY; 
                }
                tlb_random(entryhi, entrylo);
                spinlock_release(&stealmem_lock);
                return 0;
        } else {
                result = check_region(as, faultaddress);
                if (result) {
                // address in region
                        vaddr_t frame_add = KVADDR_TO_PADDR(alloc_kpages(1));
                        p_memory_address *tmp = (p_memory_address *)result;
                        if (!tmp -> old) {
                                // as_zero_region(frame_add, 1);
                                memset((void *)PADDR_TO_KVADDR(frame_add), 0, PAGE_SIZE);
                                // panic("error test");
                        } else {
                                result = hpt_check(tmp -> old, faultaddress);
                                tmp -> old = NULL;
                                if (result) {
                                        hashed_page_table *tmp1 = (hashed_page_table *) result;
                                        memcpy((void *)PADDR_TO_KVADDR(frame_add), (const void *)PADDR_TO_KVADDR(tmp1 -> frame_num), PAGE_SIZE);
                                } else {
                                        panic("error HPT");
                                
                                }
                        }
                        hpt_load(as, faultaddress, PADDR_TO_KVADDR(frame_add), 7);
                        entryhi = TLBHI_VPAGE & faultaddress;
                        entrylo = (frame_add & TLBLO_PPAGE) | TLBLO_VALID;
                        if (tmp -> permission & write_bit) {
                                entrylo |= TLBLO_DIRTY; 
                        }
                        tlb_random(entryhi, entrylo);
                        // spinlock_release(&stealmem_lock);
                        return 0;
                }
        }
        

        
        // return 0;

        // panic("vm_fault hasn't been written yet\n");

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

