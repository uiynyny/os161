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

	* ctf is the current trap frame
	* retval will be the PID of the child process
		* The child process will get a return value via `enter_forked_process`
*/
int sys_fork(struct trapframe *ctf, pid_t *retval) {

	struct proc *curp = curproc;
	struct proc *newp = proc_create_runprogram(curp->p_name);

	if (newp == NULL) {
		return ENPROC; // too many processes in system
	}

	// Create new address space for the new process
	as_copy(curproc_getas(), &(newp->p_addrspace));

	if (newp->p_addrspace == NULL) {
		return ENOMEM; // could not create address space (out of memory?)
	}

	// Duplicate the trap frame
	struct trapframe *ntf;

	// ctf is a plain-old data structure (PODS). This makes a copy
	// Will be copied to this function's stack on the kernel
	*ntf = *ctf;

	// Find the differences between the address spaces
	int vbase1diff = newp->p_addrspace->as_vbase1 - curp->p_addrspace->as_vbase1; // code
	int vbase2diff = newp->p_addrspace->as_vbase2 - curp->p_addrspace->as_vbase2; // data

	ntf->tf_gp += vbase2diff; // global pointer
	ntf->tf_sp += vbase2diff; // stack pointer
	// ntf->tf_s8 += vbase2diff; // Frame pointer?
	ntf->tf_epc += vbase1diff; // PC

	// Fork the current thread into the new process, enter it where required
	int thread_fork_err = thread_fork(curthread->t_name, newp, &enter_forked_process, ntf, 0);

	if (thread_fork_err) {
		return thread_fork_err; // err
	}

	// Return the new processes's ID
	*retval = newp->id;

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
