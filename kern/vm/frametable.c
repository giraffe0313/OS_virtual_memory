#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>

/* Place your frametable data-structures here 
 * You probably also want to write a frametable initialisation
 * function and call it from vm_bootstrap
 */


struct frame_table_entry {
        int next_free_frame;
};

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

struct frame_table_entry *ft = 0;

vaddr_t create_frame_table(void) {
        paddr_t pt, pt1, top;
        hashed_page_table * hash_pt; 
        // calculate need space
        top = paddr_to_kvaddr(ram_getsize());
        int indicate_frame_number = (top - MIPS_KSEG0)/PAGE_SIZE;
        int require_space = sizeof(struct frame_table_entry) * indicate_frame_number;
        int require_frame_number = require_space/PAGE_SIZE;
        // kprintf("create_frame_table: require space is %d, frame number is %d\n", require_space, require_frame_number);
        // kprintf("indicate_frame_number is %d\n", indicate_frame_number * 2);

        // init frame table
        spinlock_acquire(&stealmem_lock);

        pt1 = ram_stealmem(require_frame_number);
        pt = paddr_to_kvaddr(pt1);
        ft = (struct frame_table_entry*) pt;

        // init HPT
        int hashed_pt_num =  (2 * indicate_frame_number * sizeof(struct hashed_page_table))/PAGE_SIZE + 1;
        // kprintf("create_frame_table: need %d page to store HPT\n", hashed_pt_num);

        hash_pt = (hashed_page_table *)paddr_to_kvaddr(ram_stealmem(hashed_pt_num));
        kprintf("create_frame_table: hash pointer is %p\n", hash_pt);
        kprintf("hashed_pt_num is %d\n", hashed_pt_num);
        
        for (int i = 0; i < 2 * indicate_frame_number; i++) {
                hash_pt[i].process_ID = 0;
                hash_pt[i].permission = 5;
                hash_pt[i].next = 0;
        }

        int frame = (int )((void *)pt - MIPS_KSEG0)/PAGE_SIZE + hashed_pt_num + require_frame_number;



        struct hash_table_v* r_pt = (struct hash_table_v*)paddr_to_kvaddr(ram_getfirstfree());
        frame = frame + 1;
        for (int i = 0; i < frame; i++) {
                ft[i].next_free_frame = frame;
        }
        for (int i = frame; i < indicate_frame_number - 1; i++) {
                ft[i].next_free_frame = i + 1;
        }
        ft[indicate_frame_number - 1].next_free_frame = 0;
        spinlock_release(&stealmem_lock);

        // kprintf("create_frame_table: used space is %d\n", frame);
        // kprintf("create_frame_table: orgin address is %p, ft address is %p\n",(void *)pt, ft);
        
        r_pt -> hash_pt = hash_pt;
        r_pt -> hash_frame_num = 2 * indicate_frame_number;

        // paddr_t tmp_first = paddr_to_kvaddr(ram_getfirstfree());
        // kprintf("tmp_first is %p, hash_table is %p\n", (void*)tmp_first, r_pt);
        return (vaddr_t)r_pt;
}




/* Note that this function returns a VIRTUAL address, not a physical 
 * address
 * WARNING: this function gets called very early, before
 * vm_bootstrap().  You may wish to modify main.c to call your
 * frame table initialisation function, or check to see if the
 * frame table has been initialised and call ram_stealmem() otherwise.
 */

vaddr_t alloc_kpages(unsigned int npages)
{
        /*
         * IMPLEMENT ME.  You should replace this code with a proper
         *                implementation.
         */
        // test();
        if (npages != 1) {
                panic("more than one page");
        }
        if (ft == 0) {
                // kprintf("alloc_kpages: no frame table\n");
                paddr_t addr;

                spinlock_acquire(&stealmem_lock);
                addr = ram_stealmem(npages);
                spinlock_release(&stealmem_lock);

                if(addr == 0)
                        return 0;

                return PADDR_TO_KVADDR(addr);
        } else {
                // kprintf("alloc_kpages: try to allocate %d pages\n", npages);
                KASSERT(npages == 1);
                // there is no free space
                spinlock_acquire(&stealmem_lock);
                if (ft[ft[0].next_free_frame].next_free_frame == 0) {
                        spinlock_release(&stealmem_lock);
                        return 0;
                }
                int result = ft[0].next_free_frame;
                // kprintf("alloc_kpages: result is %d\n", result);
                for (int i = 0; i < ft[0].next_free_frame; i++) {
                        if (ft[i].next_free_frame == result) {
                                ft[i].next_free_frame = ft[result].next_free_frame;
                        }
                }
                spinlock_release(&stealmem_lock);
                // kprintf("alloc_kpages: reture result is %p\n", (void *)(MIPS_KSEG0 + PAGE_SIZE * result));
                return (MIPS_KSEG0 + PAGE_SIZE * result);

        }
}

void free_kpages(vaddr_t addr)
{       
        // kprintf("free_kpages: %d!!\n", addr);
        int frame_number = (addr - MIPS_KSEG0) / PAGE_SIZE;
        for (int i = 0; i < frame_number; i++) {
                if (ft[i].next_free_frame > frame_number) {
                        ft[i].next_free_frame = frame_number;
                }
        }
}

