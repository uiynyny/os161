/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
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
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

/*
 * Dumb MIPS-only "VM system" that is intended to only be just barely
 * enough to struggle off the ground. You should replace all of this
 * code while doing the VM assignment. In fact, starting in that
 * assignment, this file is not included in your kernel!
 */

/* under smartvm, always have 48k of user stack */
#define SMARTVM_STACKPAGES    12

/**
	A coremap is an array of coremapentry instances
	If memory is n pages large, and the ith page is being used, the ith
	element of coremap will have used == true

	This array is contiguous in memory
*/
static struct coremapentry *coremap = NULL;

static bool coremapsetup = false;

// Is the TLB currently full?
static bool tlbfull = false;

/*
 * Wrap rma_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/**
	TODO: Rename to not-so-smartvm.c
	TODO: Dynamic segments for processes using Segmentation and Paging translation
*/
void vm_bootstrap(void) {

	paddr_t lo, hi;
	ram_getsize(&lo, &hi);

	// How many pages do we need for this?
	totalpagecount = (hi - lo) / PAGE_SIZE;

	int coremapsize = sizeof(struct coremapentry) * totalpagecount;
	int pagesforcoremap = (coremapsize + PAGE_SIZE - 1)/PAGE_SIZE;


	paddr_t coremapploc = ram_stealmem(pagesforcoremap);
	totalpagecount -= pagesforcoremap; // a few pages are now unavailable
	coremap = (struct coremapentry *)PADDR_TO_KVADDR(coremapploc);

	// Zero out all the core map entries
	for (int i = 0; i < totalpagecount; i++) {
		(coremap + i)->used = false;
		(coremap + i)->nextentry = -1;
	}

	// Recalc ramsize again
	ram_getsize(&lo, &hi);

	// makes sure ram_getsize or ram_stealmem can't be called again
	unset_ramsize(); // (used to be called at the ned of ram_getsize)

	pmemstart = lo;
	pmemend = hi;
	coremapsetup = true;
}

/**
	Get the address of a free physical page, allocating as we go
	When more than one page is required, the coremap entry will have the index
	of the next page (the last page will have an index of -1).
*/
int getppageid(unsigned long npages) {

	int result = -1;
	struct coremapentry* previousrow = NULL;
	int i;

	for (i = 0; i < totalpagecount && npages > 0; i++) {
		struct coremapentry* row = (coremap + i);
		if (row->used) continue;
		row->used = true;

		if (previousrow == NULL) {
			result = i;
		} else {
			previousrow->nextentry = i;
		}
		previousrow = row;

		if (--npages == 0) break;
	}

	if (i == totalpagecount) {
		// Did not break before finding a page
		// TODO: Evict page from memory into swap file
		panic("Out of memory. No more pages to allocate\n");
	}

	return result;
}

static paddr_t getppages(unsigned long npages) {
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);

	if (coremapsetup) {
		addr = (paddr_t)(pmemstart + getppageid(npages) * PAGE_SIZE);
	} else {
		addr = ram_stealmem(npages);
	}

	spinlock_release(&stealmem_lock);
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t alloc_kpages(int npages) {
	paddr_t pa;
	pa = getppages(npages);
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void free_kpages(vaddr_t addr) {

	// (void)addr;
	// TODO: check if physical address is in the right bounds?

	paddr_t paddr = KVADDR_TO_PADDR(addr);
	KASSERT(paddr % PAGE_SIZE == 0); // must be the address of a page

	// convert physical address to page number
	int pagenumber = (paddr - pmemstart) / PAGE_SIZE;

	// Keep grabbing the next page and free it
	do {
		// Free the page
		struct coremapentry * kpage = (coremap + pagenumber);
		KASSERT(kpage->used); // Crash if this is an unused page
		kpage->used = false;
		pagenumber = kpage->nextentry;
		kpage->nextentry = -1;
	} while (pagenumber >= 0);
}

void vm_tlbshootdown_all(void) {
	panic("smartvm tried to do tlb shootdown?!\n");
}

void vm_tlbshootdown(const struct tlbshootdown *ts) {
	(void)ts;
	panic("smartvm tried to do tlb shootdown?!\n");
}

int vm_fault(int faulttype, vaddr_t faultaddress) {
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "smartvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("smartvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = curproc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - SMARTVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;
	bool dirtiable;

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = vaddr_to_paddr(faultaddress,  vbase1, as->as_pbase1);
		dirtiable = as->as_dirtiable1;
	} else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = vaddr_to_paddr(faultaddress,  vbase2, as->as_pbase2);
		dirtiable = as->as_dirtiable2;
	} else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = vaddr_to_paddr(faultaddress, stackbase, as->as_stackpbase);
		dirtiable = true;
	} else {
		return EFAULT;
	}

	// if not yet ready, we're still loading segments
	// Will always be dirtiable in this case
	if (!as->as_ready) dirtiable = true;

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; !tlbfull && i < NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | (dirtiable ? TLBLO_DIRTY : 0) | TLBLO_VALID;
		DEBUG(DB_VM, "smartvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	tlbfull = true;

	// If we reached this point the TLB is full
	// Evict and write to a random page for now
	ehi = faultaddress;
	elo = paddr | (dirtiable ? TLBLO_DIRTY : 0) | TLBLO_VALID;
	tlb_random(ehi, elo);
	splx(spl);
	return 0;
}

/**
	Follows the coremap to find the correct physical address
*/
paddr_t vaddr_to_paddr(vaddr_t addr, vaddr_t vbase, paddr_t pbase) {
	int pagenumber = (pbase - pmemstart) / PAGE_SIZE;
	struct coremapentry * entry = coremap + pagenumber;
	for (size_t i = 0; i < (addr - vbase) / PAGE_SIZE; i++) {
		pagenumber = entry->nextentry;
		entry = coremap + pagenumber;
	}
	return (paddr_t)(pmemstart + (pagenumber * PAGE_SIZE) + (addr % PAGE_SIZE));
}

struct addrspace * as_create(void) {
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_dirtiable1 = false;

	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_dirtiable2 = false;

	as->as_stackpbase = 0;

	as->as_ready = false;

	return as;
}

void as_destroy(struct addrspace *as) {
	// Delete all allocated segments
	// quick way of cleaning by passing in physical page
	// Should probably have a separate function for this.
	free_kpages(PADDR_TO_KVADDR(as->as_pbase1));
	free_kpages(PADDR_TO_KVADDR(as->as_pbase2));
	free_kpages(PADDR_TO_KVADDR(as->as_stackpbase));

	// Finally, free up the actual address space structure
	kfree(as);
}

void as_activate(void) {
	int i, spl;
	struct addrspace *as;

	as = curproc_getas();
#ifdef UW
        /* Kernel threads don't have an address spaces to activate */
#endif
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	tlbfull = false;

	splx(spl);
}

void as_deactivate(void) {
	/* nothing */
}

int as_define_region(
	struct addrspace *as, vaddr_t vaddr, size_t sz,
	int readable, int writeable, int executable
) {
	size_t npages;

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	bool dirtiable = (bool)(writeable >> 1);
	(void)readable;
	// (void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		as->as_dirtiable1 = dirtiable;
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		as->as_dirtiable2 = dirtiable;
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("smartvm: Warning: too many regions\n");
	return EUNIMP;
}

static void as_zero_region(paddr_t paddr, unsigned npages) {
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}

int as_prepare_load(struct addrspace *as) {
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);

	as->as_pbase1 = getppages(as->as_npages1);
	if (as->as_pbase1 == 0) {
		return ENOMEM;
	}

	as->as_pbase2 = getppages(as->as_npages2);
	if (as->as_pbase2 == 0) {
		return ENOMEM;
	}

	as->as_stackpbase = getppages(SMARTVM_STACKPAGES);
	if (as->as_stackpbase == 0) {
		return ENOMEM;
	}

	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, SMARTVM_STACKPAGES);

	return 0;
}

int as_complete_load(struct addrspace *as) {
	as->as_ready = true;
	return 0;
}

int as_define_stack(struct addrspace *as, vaddr_t *stackptr) {
	KASSERT(as->as_stackpbase != 0);

	*stackptr = USERSTACK;
	return 0;
}

int as_copy(struct addrspace *old, struct addrspace **ret) {
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		SMARTVM_STACKPAGES*PAGE_SIZE);

	*ret = new;
	return 0;
}
