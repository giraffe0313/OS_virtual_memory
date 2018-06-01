#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>

/* Place your page table functions here */
#define PAGE_BITS  12
struct hash_table_v *h_pt = 0; 

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
        kprintf("hash table address is %p\n", (void *)h_pt);
        return index;
}

// int hpt_check(struct addrspace *as, vaddr_t faultaddr) {

// }

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
        (void) faulttype;
        (void) faultaddress;
        return 0;

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

