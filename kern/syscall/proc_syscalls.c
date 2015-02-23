#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>

/* this implementation of sys__exit does not do anything with the exit code */
/* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

	struct addrspace *as;
	struct proc *p = curproc;
	/* for now, just include this to keep the compiler from complaining about
		 an unused variable */
	(void)exitcode;

	DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

	KASSERT(curproc->p_addrspace != NULL);
	as_deactivate();
	/*
	 * clear p_addrspace before calling as_destroy. Otherwise if
	 * as_destroy sleeps (which is quite possible) when we
	 * come back we'll be calling as_activate on a
	 * half-destroyed address space. This tends to be
	 * messily fatal.
	 */
	as = curproc_setas(NULL);
	as_destroy(as);

	/* detach this thread from its process */
	/* note: curproc cannot be used after this call */
	proc_remthread(curthread);

	/* if this is the last user process in the system, proc_destroy()
		 will wake up the kernel menu thread */
	proc_destroy(p);

	thread_exit();
	/* thread_exit() does not return, so we should never get here */
	panic("return from thread_exit in sys_exit\n");
}

/**
	The fork system call
*/
int sys_fork(pid_t *retval) {
	(void)retval;

	int i; // iterator

	struct proc *curp = curproc;
	struct proc *newp;
	struct addrspace *newp_addrspace;

	// Allocate memory for the new process
	newp = kmalloc(sizeof(struct proc));
	if (newp == NULL) {
		return ENOMEM; // could not allocate, out of memory?
	}

	// Copy over the process name
	newp->p_name = kstrdup(curp->p_name);
	if (newp->p_name == NULL) {
		kfree(newp);
		return ENOMEM; // could not allocate, out of memory?
	}

	// Duplicate the address space
	as_copy(curp->p_addrspace, &newp->p_addrspace);

	// Initialize this process's spinlock
	spinlock_init(&newp->plock);

	// Add the cwd to the new process
	newp->p_cwd = curp->p_cwd;
	vnode_incref(newp->p_cwd); // increments references to this directory

	// Fork all the threads
	int numthreads = threadarray_num(curp->p_threads);
	for (int i = 0; i < numthreads; i++) {
		struct thread * curp_thread = threadarray_get(&curp->p_threads, i);
		struct thread * newp_thread;

		thread_fork(const char *name, struct proc *proc,
                void (*func)(void *, unsigned long),
                void *data1, unsigned long data2);
		if () {
			threadarray_remove(&proc->p_threads, i);
			spinlock_release(&proc->p_lock);
			t->t_proc = NULL;
			return;
		}
	}
	// Copy the current process
	// Make a new copy of each thread and fork each thread
	// Copy the stack frame

	// How do we select the process that will run?

	return 0;
}

/* stub handler for getpid() system call                */
int sys_getpid(pid_t *retval) {
	/* for now, this is just a stub that always returns a PID of 1 */
	/* you need to fix this to make it work properly */
	*retval = 1;
	return(0);
}

/* stub handler for waitpid() system call                */

int sys_waitpid(pid_t pid, userptr_t status, int options, pid_t *retval) {
	int exitstatus;
	int result;

	/* this is just a stub implementation that always reports an
		 exit status of 0, regardless of the actual exit status of
		 the specified process.
		 In fact, this will return 0 even if the specified process
		 is still running, and even if it never existed in the first place.

		 Fix this!
	*/

	if (options != 0) {
		return(EINVAL);
	}
	/* for now, just pretend the exitstatus is 0 */
	exitstatus = 0;
	result = copyout((void *)&exitstatus,status,sizeof(int));
	if (result) {
		return(result);
	}
	*retval = pid;
	return(0);
}
