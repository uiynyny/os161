/*
 * Copyright (c) 2013
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

#ifndef _PROC_H_
#define _PROC_H_

/*
 * Definition of a process.
 *
 * Note: curproc is defined by <current.h>.
 */

#include <types.h>
#include <array.h>
#include <spinlock.h>
#include <thread.h> /* required for struct threadarray */

struct addrspace;
struct vnode;
#ifdef UW
struct semaphore;
#endif // UW

struct proc;

/*
	Array of processes.
	NOTE: This is stupid and doesn't work
	I tried this and kept getting weird errors that look like this

		../../proc/proc.c:(.text+0x20): undefined reference to `procarray_num'
		../../proc/proc.c:(.text+0x20): relocation truncated to fit: R_MIPS_26 against `procarray_num'

	What to do about this?
*/
// #ifndef PROCINLINE
// #define PROCINLINE INLINE
// #endif

// DECLARRAY(proc);
// DEFARRAY(proc, PROCINLINE);

/*
 * Process structure.
 */
struct proc {
	char *p_name;					/* Name of this process */
	struct spinlock p_lock;			/* Lock for this structure */
	struct threadarray p_threads;	/* Threads in this process */

	/* VM */
	struct addrspace *p_addrspace;	/* virtual address space */

	/* VFS */ // forked processes can have the same one
	struct vnode *p_cwd;		/* current working directory */

#ifdef UW
	/* a vnode to refer to the console device */
	/* this is a quick-and-dirty way to get console writes working */
	/* you will probably need to change this when implementing file-related
	 system calls, since each process will need to keep track of all files
	 it has opened, not just the console. */
	struct vnode *console;			/* a vnode for the console device */

#endif

 	pid_t p_id;						/* process ID */
	struct array p_children;/* Child processes for this process */

	bool p_did_exit;				/* Did the thread exit yet? */
	int p_exitcode;					/* Exit code for this process */

	struct lock *p_exit_lk;			/* Grabbing this lock prevents this thread from exiting */
	struct lock *p_wait_lk;			/* Use with p_wait_cv to check when lock exists */
	struct cv *p_wait_cv;			/* Conditional variable for checking whether we've existed */

};

/* This is the process structure for the kernel and for kernel-only threads. */
extern struct proc *kproc;

/* Semaphore used to signal when there are no more processes */
#ifdef UW
extern struct semaphore *no_proc_sem;
#endif // UW

/* Call once during system startup to allocate data structures. */
void proc_bootstrap(void);

/* Create a fresh process for use by runprogram(). */
struct proc *proc_create_runprogram(const char *name);

/* Destroy a process. */
void proc_destroy(struct proc *proc);

/* Attach a thread to a process. Must not already have a process. */
int proc_addthread(struct proc *proc, struct thread *t);

/* Detach a thread from its process. */
void proc_remthread(struct thread *t);

/* Fetch the address space of the current process. */
struct addrspace *curproc_getas(void);

/* Change the address space of the current process, and return the old one. */
struct addrspace *curproc_setas(struct addrspace *);

// PID and processes helpers

/**
	A list of processes is a dynamic array ordered by PID
*/

/**
	Returns the process for the given process ID
*/
unsigned procarray_proc_index_by_pid(struct array *procs, pid_t pid); // returns index for process (or -1 if fail)
struct proc * procarray_proc_by_pid(struct array *procs, pid_t pid); // returns actual process

/**
	Add a process to the complete list of processes.
	At this point it can be looked up with proc_by_pid
*/
void procarray_add_proc(struct array *procs, struct proc *p);
void procarray_allprocs_add_proc(struct proc *p);

/**
	Remove a process from the complete list of processes
	This should only be called by proc_destroy
*/
void procarray_remove_proc(struct array *procs, pid_t pid);
void procarray_allprocs_remove_proc(pid_t pid);

// Generates a unique process ID
pid_t gen_pid(void);

#endif /* _PROC_H_ */
