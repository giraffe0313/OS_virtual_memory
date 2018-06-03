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
        // kprintf("hash pointer is %p\n", h_pt -> hash_pt);
        // for (int i = 0; i < h_pt -> hash_frame_num; i++) {
        //         kprintf("i5 is %d\n", h_pt -> hash_pt[i].permission );
        // }
        
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
        // kprintf("hash table address is %d\n", h_pt -> hash_frame_num);
        return index;
}

vaddr_t hpt_check(struct addrspace *as, vaddr_t faultaddr) {
        int h_index = hpt_hash(as, faultaddr);
        hashed_page_table *tmp = &h_pt -> hash_pt[h_index];

        // int spl = splhigh();
        while (tmp) {
                if (tmp -> process_ID == (uint32_t)as &&
                    tmp -> v_page_num == faultaddr) {
                        //     splx(spl);
                        //     kprintf("find this page, permision is %d\n!", tmp -> permission);
                            return (vaddr_t)tmp;
                    }
                tmp = tmp -> next;
        }
        // splx(spl);
        return 0;
}

void delete_HPT(paddr_t as) {
        // hashed_page_table *tmp;
        // hashed_page_table *tmp_next;
        // int spl = splhigh();
        // kprintf("h_pt -> hash_frame_num is %d\n", 2);
        // kprintf("as is %p\n", as);
        // kprintf("delete_HPT pointer: %p\n", h_pt -> hash_pt);
        for (int i = 0; i < h_pt -> hash_frame_num; i++) {
                if ( h_pt -> hash_pt[i].process_ID == 0) {
                        continue;
                }
                if ( h_pt -> hash_pt[i].process_ID == (uint32_t)as) {
                        h_pt -> hash_pt[i].process_ID = 0;
                }
                kprintf("i is %p\n", (void *)h_pt -> hash_pt[i].next);
                // kprintf("find i:%d\n", h_pt -> hash_pt[i].process_ID);
                // hashed_page_table *tmp = &h_pt -> hash_pt[i];
                // while (tmp) {
                //         if (tmp -> process_ID == (uint32_t)as) {
                //                 hashed_page_table *tmp_next = tmp -> next;
                //                 // free_kpages((vaddr_t)tmp);
                //                 tmp = tmp_next;
                //         } else {
                //                 tmp = tmp -> next;
                //         }
                        // tmp = tmp -> next;
                // }
                // ;
        }
        // // splx(spl);
        kprintf("wired%d!!\n", as);
        ;
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
                                kprintf("add_HPT: old is %p, new is %p, new old is %p, old old is %p\n", 
                                        old, new, (void *)new -> head -> old, old -> head -> old);
                        }
                        
                        tmp = tmp -> next;
                }
        }
}



void 
hpt_load(struct addrspace *as, vaddr_t faultaddr, vaddr_t frame_num, int permission) {
        int h_index = hpt_hash(as, faultaddr);
        // int spl = splhigh();
        if (h_pt -> hash_pt[h_index].process_ID == 0) {
                h_pt -> hash_pt[h_index].process_ID = (uint32_t)as;
                h_pt -> hash_pt[h_index].v_page_num = faultaddr;
                h_pt -> hash_pt[h_index].frame_num = frame_num;
                h_pt -> hash_pt[h_index].permission = permission;
                // kprintf("hpt_load permission is %d\n", permission);
        } else {
                hashed_page_table *tmp = & h_pt -> hash_pt[h_index];
                // kprintf("tmp -> permsion is %d\n", tmp -> permission);
                while (tmp -> next) {
                        tmp = tmp -> next;
                }
                hashed_page_table *new_page = (hashed_page_table *)alloc_kpages(1);
                // kprintf("hpt_load allocate address is %p\n", new_page);
                new_page -> process_ID = (uint32_t)as;
                new_page -> v_page_num = faultaddr;
                new_page -> frame_num = frame_num;
                new_page -> permission = permission;
                tmp -> next = new_page;
        }


        // splx(spl);
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

        kprintf("check_region: as is %p\n", as);
        kprintf("check_region: old is %p\n", as -> head -> next ->  old);
        kprintf("check_region: first vaddr is %d, uper is %d\n", 
                as -> head -> next -> p_vaddr, as -> head -> next ->p_upper);
        kprintf("faultaddr is %d\n", faultaddr);
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
                // splx(spl);
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

                        // kprintf("vm_falut : load address is %p\n", (void *)frame_add);
                        // spinlock_acquire(&stealmem_lock);
                        p_memory_address *tmp = (p_memory_address *)result;
                        if (!tmp -> old) {
                                // as_zero_region(frame_add, 1);
                                memset((void *)PADDR_TO_KVADDR(frame_add), 0, PAGE_SIZE);
                                // panic("error test");
                        } else {
                                
                                result = hpt_check(tmp -> old, faultaddress);
                                // tmp -> old = NULL;
                                if (result) {
                                        // panic("test HPT");
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
                                // splx(spl);
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
        

        
        // return 0;
        // kprintf("check_region: as is %p\n", as);
        spinlock_release(&stealmem_lock);
        // splx(spl);
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

