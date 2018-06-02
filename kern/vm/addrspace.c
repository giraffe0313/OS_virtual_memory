/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *        The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */

struct addrspace *
as_create(void)
{
        struct addrspace *as;

        // as = (struct addrspace *)kmalloc(sizeof(struct addrspace));
        // kprintf("as_create: create a addrspace, pid is %p\n",(void *)as);

        as = (struct addrspace *)alloc_kpages(1);

        if (as == NULL) {
                return NULL;
        }
        as -> head = (p_memory_address *)alloc_kpages(1);
        // as -> head -> vertual_page_num = 0;
        as -> head -> next = NULL;
        as -> head -> p_vaddr = 0;
        as -> head -> p_upper = 0;

        /*
         * Initialize as needed.
         */
        
        return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
        struct addrspace *newas;

        newas = as_create();
        if (newas==NULL) {
                return ENOMEM;
        }
        kprintf("as_copy: start\n");
        int spl = splhigh();
        p_memory_address *old_pt = old -> head -> next;
        p_memory_address *newas_pt = newas -> head;
        p_memory_address *temp = 0;
        while (old_pt) {
                temp = (p_memory_address *)alloc_kpages(1);
                temp -> permission = old_pt -> permission;
                temp -> p_vaddr = old_pt -> p_vaddr;
                temp -> p_upper = old_pt -> p_upper;
                temp -> old = old;
                temp -> next = NULL;
                newas_pt -> next = temp;
                newas_pt = temp;
                old_pt = old_pt -> next;
        }
        old_pt = old -> head -> next;
        newas_pt = newas -> head -> next;
        while (old_pt) {
                kprintf("old_vaddr is %d, new_vaddr is %d\n", old_pt -> p_vaddr, newas_pt -> p_vaddr);
                kprintf("old_permission is %d, new_permission is %d\n", old_pt -> permission, newas_pt -> permission);
                old_pt = old_pt -> next;
                newas_pt = newas_pt -> next;
        }

        splx(spl);





        (void)old;

        *ret = newas;
        return 0;
}

void
as_destroy(struct addrspace *as)
{
        /*
         * Clean up as needed.
         */

        kfree(as);
}

void
as_activate(void)
{
        struct addrspace *as;

        as = proc_getas();
        if (as == NULL) {
                /*
                 * Kernel thread without an address space; leave the
                 * prior address space in place.
                 */
                return;
        }

        int i, spl;
	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
        /*
         * Write this. For many designs it won't need to actually do
         * anything. See proc.c for an explanation of why it (might)
         * be needed.
         */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
                 int readable, int writeable, int executable)
{
        int x = hpt_hash(as, vaddr);
        kprintf("hash table is : %d\n", x);

        kprintf("as_define_region: vaddr_t is %d, size_t is %d\n", vaddr, memsize);
        size_t npages;
        memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
        vaddr &= PAGE_FRAME;
        kprintf("as_define_region:  changed vaddr is %p\n", (void *)vaddr);
        memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;
        kprintf("as_define_region: changed memsize is %d\n", memsize);
        npages = memsize / PAGE_SIZE;
        kprintf("as_define_region: npages is %d\n", npages);

        // kprintf("read %d write %d exe %d %d\n", readable, writeable, executable, 
        //                                         readable | writeable | executable);
        kprintf("as_define_region: vertual page number is %d\n", vaddr / PAGE_SIZE);

        p_memory_address *temp = (p_memory_address *)alloc_kpages(1);
        // temp -> frame_table_num = 0;
        // temp -> vertual_page_num = vaddr / PAGE_SIZE;
        // temp -> need_page_num = npages;
        temp -> permission = readable | writeable | executable;
        temp -> p_vaddr = vaddr;
        temp -> p_upper = vaddr + memsize;
        // temp -> dirty = 0;
        temp -> next = 0;
        temp -> old = NULL;
        kprintf("as_define_region: p_vaddr is %d\n", vaddr);

        p_memory_address *tmp = as -> head;
        while (tmp -> next) {
                tmp = tmp -> next;
        }
        tmp -> next = temp;


        tmp = as -> head -> next;
        while (tmp) {
                kprintf("as_define_region: test p_vaddr is %d\n", tmp -> p_vaddr);
                tmp = tmp -> next;
        }        
        // test hashed table
        // x = hpt_hash(as, vaddr);
        // kprintf("hash table is : %d\n", x);
        
        // vaddr_t z = alloc_kpages(1);
        // hpt_load(as, vaddr, z, 6);
        // hpt_check(as, vaddr);
        // check_region(as, vaddr);

        

        // p_memory_address *tmp1 = as->head;
        // while (tmp1 != NULL) {
        //         kprintf("page num is %d\n", tmp1 -> vertual_page_num);
        //         tmp1 = tmp1 -> next;
        // }

        (void)as;
        (void)vaddr;
        (void)memsize;
        (void)readable;
        (void)writeable;
        (void)executable;
        kprintf("\n");
        return 0;
        
        // return ENOSYS; /* Unimplemented */
}

int
as_prepare_load(struct addrspace *as)
{

        // left shift 3bits and allocte readwrite permision
        int bit = 7;
        kprintf("as_prepare_load: start load\n");
        p_memory_address *temp = as -> head -> next;
        while (temp) {
                kprintf("as_prepare_load: permission is %d\n", temp -> permission);
                temp -> permission = temp -> permission << 3;
                temp -> permission = temp -> permission | bit;
                kprintf("as_prepare_load: changed permission is %d\n", temp -> permission);

                temp = temp -> next;
        }

        (void)as;
        return 0;
}

int
as_complete_load(struct addrspace *as)
{
        // right shift 3 bits
        kprintf("as_complete_load: start load\n");
        p_memory_address *temp = as -> head -> next;
        while (temp) {
                kprintf("as_complete_load: permission is %d\n", temp -> permission);
                temp -> permission = temp -> permission >> 3;
                kprintf("as_complete_load: changed permission is %d\n", temp -> permission);
                temp = temp -> next;
        }
        (void)as;
        return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
        // kprintf("as_define_stack: start\n");

        p_memory_address *temp = (p_memory_address *)alloc_kpages(1);
        // temp -> frame_table_num = 0;
        // temp -> vertual_page_num = (USERSTACK - 16 * PAGE_SIZE) / PAGE_SIZE;
        // temp -> need_page_num = 16;
        temp -> permission = 6;
        temp -> p_vaddr = USERSTACK - 16 * PAGE_SIZE;
        temp -> p_upper = USERSTACK;
        // temp -> dirty = 0;
        temp -> next = NULL;


        p_memory_address *tmp = as -> head;
        while (tmp -> next != NULL) {
                tmp = tmp -> next;
        }
        tmp -> next = temp;

        (void)as;

        /* Initial user-level stack pointer */
        *stackptr = USERSTACK;

        return 0;
}

