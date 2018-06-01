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

int hpt_hash(struct addrspace *as, vaddr_t faultaddr)
{
        uint32_t index;

        index = (((uint32_t )as) ^ (faultaddr >> PAGE_BITS)) % h_pt -> hash_frame_num;
        // kprintf("hash table address is %p\n", (void *)h_pt);
        return index;
}

int hpt_check(struct addrspace *as, vaddr_t faultaddr) {
        int h_index = hpt_hash(as, faultaddr);
        // int next = h_pt -> hash_pt[h_index].next;
        // kprintf("hpt_check: check next value is %d\n", next);
        int spl = splhigh();
        faultaddr &= PAGE_FRAME;
        while (h_index != -1) {
                if (h_pt -> hash_pt[h_index].process_ID == (uint32_t)as &&
                    h_pt -> hash_pt[h_index].v_page_num == faultaddr) {
                        //     kprintf("faultaddr %d exits, value is %d!\n", faultaddr, h_index);
                            splx(spl);
                            return h_index;
                    }
                h_index = h_pt -> hash_pt[h_index].next;
        }
        splx(spl);
        return -1;
}

int 
hpt_load(struct addrspace *as, vaddr_t faultaddr, vaddr_t frame_num, int permission) {
        int h_index = hpt_hash(as, faultaddr);
        int temp = 0;
        faultaddr &= PAGE_FRAME;
        int spl = splhigh();
        if (h_pt -> hash_pt[h_index].process_ID == 0) {
                temp = h_index;
                h_pt -> hash_pt[h_index].next = -1;
        } else {
                while (h_pt -> hash_pt[h_index].next != -1) {
                        h_index = h_pt -> hash_pt[h_index].next;
                }
                temp = 0;
                while ((! h_pt -> hash_pt[temp].process_ID) && temp < h_pt -> hash_frame_num) {
                        temp += 1;
                }
                // no sapce left 
                if (temp == h_pt -> hash_frame_num) {
                        // kprintf("hpt_load: no space!\n");
                        return -1;
                }
                 h_pt -> hash_pt[h_index].next = temp;
        }
        h_pt -> hash_pt[temp].process_ID = (uint32_t)as;
        h_pt -> hash_pt[temp].v_page_num = faultaddr;
        h_pt -> hash_pt[temp].frame_num = frame_num;
        h_pt -> hash_pt[temp].permission = permission;
        splx(spl);
        // kprintf("hpt_load: load in %d, next")
        return 0;
}

int check_region(struct addrspace *as, vaddr_t faultaddr) {
        p_memory_address *tmp = as -> head;
        int i = 0;
        while (tmp) {
                if (faultaddr >= tmp -> p_vaddr && faultaddr <= tmp -> p_upper) {
                        // kprintf("return value is %d\n", i);
                        return i;
                }
                i += 1;
                tmp = tmp -> next;
        }

        return -1;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{       
        if (faulttype == VM_FAULT_READONLY) {
                return EFAULT;
        }
        struct addrspace *as;
        as = proc_getas();
        int result;
        int write_bit = 2;
        uint32_t entryhi, entrylo;

        // address in HPT
        faultaddress &= PAGE_FRAME;
        result = hpt_check(as, faultaddress);
        if (result != -1) {
                spinlock_acquire(&stealmem_lock);
                entryhi = TLBHI_VPAGE & faultaddress;
                entrylo = h_pt -> hash_pt[result].frame_num & TLBLO_PPAGE & TLBLO_VALID;
                if (h_pt -> hash_pt[result].permission & write_bit) {
                      entrylo &= TLBLO_DIRTY; 
                }
                tlb_random(entryhi, entrylo);
                spinlock_release(&stealmem_lock);
                return 0;
        } else {
                result = check_region(as, faultaddress);
                if (result != -1) {
                // address in region
                        p_memory_address *tmp = as -> head;
                        for (int i = 0; i < result; i++) {
                                tmp = tmp -> next;
                        }
                        vaddr_t frame_add = alloc_kpages(1);
                        spinlock_acquire(&stealmem_lock);
                        if (! tmp -> dirty) {
                                memset((void *)frame_add, 0, PAGE_SIZE);
                        } else {
                                memcpy((void *)frame_add, (const void *)tmp -> frame_table_num, PAGE_SIZE);
                        }
                        tmp -> frame_table_num = frame_add;
                        hpt_load(as, faultaddress, frame_add, 7);
                        entryhi = TLBHI_VPAGE & faultaddress;
                        entrylo = frame_add & TLBLO_PPAGE & TLBLO_VALID & TLBLO_DIRTY;
                        tlb_random(entryhi, entrylo);
                        spinlock_release(&stealmem_lock);
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

