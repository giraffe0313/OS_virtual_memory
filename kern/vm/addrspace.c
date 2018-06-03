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

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

struct addrspace *
as_create(void)
{
        struct addrspace *as;
        as = (struct addrspace *)alloc_kpages(1);
        if (as == NULL) {
                return NULL;
        }
        as -> head = (p_memory_address *)alloc_kpages(1);
        as -> head -> next = NULL;
        as -> head -> p_vaddr = 0;
        as -> head -> p_upper = 0;
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
        spinlock_acquire(&stealmem_lock);
        // old -> head -> old = old;
        p_memory_address *old_pt = old -> head -> next;
        p_memory_address *newas_pt = newas -> head;
        newas -> head -> old = old;
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
        add_HPT(old, newas);

        spinlock_release(&stealmem_lock);

        (void)old;

        *ret = newas;
        return 0;
}

void
as_destroy(struct addrspace *as)
{

        spinlock_acquire(&stealmem_lock);
        // delete HPT entries
        delete_HPT((paddr_t)as);

        // delete region linked list
        p_memory_address *tmp = as -> head;
        while (tmp) {
                p_memory_address *tmp_next = tmp -> next;
                free_kpages((paddr_t)tmp);
                tmp = tmp_next;
        }
        free_kpages((paddr_t)as);

        spinlock_release(&stealmem_lock);        
        (void) as;
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
        memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
        vaddr &= PAGE_FRAME;
        memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;
        p_memory_address *temp = (p_memory_address *)alloc_kpages(1);
        temp -> permission = readable | writeable | executable;
        temp -> p_vaddr = vaddr;
        temp -> p_upper = vaddr + memsize;
        temp -> next = 0;
        temp -> old = NULL;

        p_memory_address *tmp = as -> head;
        while (tmp -> next) {
                tmp = tmp -> next;
        }
        tmp -> next = temp;

        (void)as;
        (void)vaddr;
        (void)memsize;
        (void)readable;
        (void)writeable;
        (void)executable;
        return 0;

}

int
as_prepare_load(struct addrspace *as)
{

        // left shift 3bits and allocte readwrite permision
        spinlock_acquire(&stealmem_lock);
        int bit = 7;
        p_memory_address *temp = as -> head -> next;
        while (temp) {
                temp -> permission = temp -> permission << 3;
                temp -> permission = temp -> permission | bit;
                temp = temp -> next;
        }

        (void)as;
        spinlock_release(&stealmem_lock);
        return 0;
}

int
as_complete_load(struct addrspace *as)
{
        // right shift 3 bits
        spinlock_acquire(&stealmem_lock);  
        p_memory_address *temp = as -> head -> next;
        while (temp) {
                temp -> permission = temp -> permission >> 3;
                temp = temp -> next;
        }
        (void)as;
        spinlock_release(&stealmem_lock);
        return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
        spinlock_acquire(&stealmem_lock);
        p_memory_address *temp = (p_memory_address *)alloc_kpages(1);
        temp -> permission = 7;
        temp -> p_vaddr = USERSTACK - 16 * PAGE_SIZE;
        temp -> p_upper = USERSTACK;
        temp -> next = NULL;


        p_memory_address *tmp = as -> head;
        while (tmp -> next != NULL) {
                tmp = tmp -> next;
        }
        tmp -> next = temp;

        (void)as;

        /* Initial user-level stack pointer */
        *stackptr = USERSTACK;
        spinlock_release(&stealmem_lock);
        return 0;
}

