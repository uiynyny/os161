#include <types.h>
#include <kern/errno.h>
#include <kern/unistd.h>
#include <kern/wait.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <copyinout.h>
#include <synch.h>
#include <array.h>

/* this implementation of sys__exit does not do anything with the exit code */
/* this needs to be fixed to get exit() and waitpid() working properly */

void sys__exit(int exitcode) {

	struct addrspace *as;
	struct proc *p = curproc;

	DEBUG(DB_SYSCALL,"Syscall: _exit(%d)\n",exitcode);

	KASSERT(curproc->p_addrspace != NULL);

	// Signal all the child processes that it is now OKAY to kill themselves
	for (unsigned int i = array_num(&p->p_children); i > 0 ; i--) {

		// This allows children to exit
		struct proc *cproc = array_get(&p->p_children, i - 1);
		lock_release(cproc->p_exit_lk);

		// Remove the child from the children array
		array_remove(&p->p_children, i - 1);
	}

	// All children must have been removed from the array
	KASSERT(array_num(&p->p_children) == 0);

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

	p->p_did_exit = true;
	p->p_exitcode = exitcode;

	// Let anyone waiting for this thread know that it has exited
	cv_broadcast(p->p_wait_cv, p->p_wait_lk);

	// Signal parent processes that we're finished!

	// Okay, at this point, we need to wait for the parent process to exit
	// before fully destroying ourselves. This way, the parent process can call
	// waitpid on its children at any time.
	lock_acquire(p->p_exit_lk);
	lock_release(p->p_exit_lk);

	/* if this is the last user process in the system, proc_destroy()
		 will wake up the kernel menu thread */
	proc_destroy(p);

	thread_exit();
	/* thread_exit() does not return, so we should never get here */
	panic("return from thread_exit in sys_exit\n");
}

/**
	The fork system call

	* ctf is the current trap frame
	* retval will be the PID of the child process
		* The child process will get a return value via `enter_forked_process`
*/
int sys_fork(struct trapframe *ctf, pid_t *retval) {

	// Create a new process from the current one
	struct proc *curp = curproc;
	struct proc *newp = proc_create_runprogram(curp->p_name);
	if (newp == NULL) {
		DEBUG(DB_SYSCALL, "sys_fork error: could not create a process.\n");
		return ENPROC; // too many processes in system?
	}
	DEBUG(DB_SYSCALL, "sys_fork: New process created.\n");

	// Create new address space for the new process
	as_copy(curproc_getas(), &(newp->p_addrspace));
	if (newp->p_addrspace == NULL) {
		DEBUG(DB_SYSCALL, "sys_fork error: Could not create address space for new process.\n");
		proc_destroy(newp);
		return ENOMEM; // could not create address space (out of memory?)
	}
	DEBUG(DB_SYSCALL, "sys_fork: New address space created.\n");

	// Duplicate the trap frame? Using the old trap frame's [virtual] pointer...
	// ... but how do we put it into the new address space?
	struct trapframe *ntf = kmalloc(sizeof(struct trapframe)); // leaks memory?
	if (ntf == NULL) {
		DEBUG(DB_SYSCALL, "sys_fork error: Could not create trap frame for new process.\n");
		proc_destroy(newp);
		return ENOMEM; // could not create trap frame (out of memory?)
	}
	memcpy(ntf, ctf, sizeof(struct trapframe));
	DEBUG(DB_SYSCALL, "sys_fork: New trap frame created.\n");

	// Fork the current thread into the new process and enter it
	// The current trap frame should have the same virtual address...?
	int thread_fork_err = thread_fork(curthread->t_name, newp, &enter_forked_process, ntf, 0);
	if (thread_fork_err) {
		DEBUG(DB_SYSCALL, "sys_fork error: Could not fork curren thread.\n");
		proc_destroy(newp); // removes address space as well
		kfree(ntf);
		ntf = NULL;
		return thread_fork_err; // err
	}
	DEBUG(DB_SYSCALL, "sys_fork: Current thread forked successfully.\n");

	// Add the child process to the current one's children array
	array_add(&curp->p_children, newp, NULL);

	// Grab the child's exit lock to prevent it from exiting
	lock_acquire(newp->p_exit_lk);

	// Return the new processes's ID
	*retval = newp->p_id;

	// No errors
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
