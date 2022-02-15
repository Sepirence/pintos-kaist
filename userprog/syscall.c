#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address(const uint64_t *addr);
static struct file *find_file_by_fd(int fd);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	// lock_init(&fs_lock);

}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	//printf("%x\n",f->R.rax);
	//printf ("system call!\n");
 
	switch(f->R.rax)
	{
		case SYS_HALT:
			printf("HALT\n");
			halt();
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			break;
		case SYS_EXEC:
			break;
		case SYS_WAIT:
			break;
		case SYS_CREATE:
			break;
		case SYS_REMOVE:
			break;
		case SYS_OPEN:
			break;
		case SYS_FILESIZE:
			break;
		case SYS_READ:
			printf("READ\n");
			break;
		case SYS_WRITE:
			//printf("WRITE\n");
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			break;
		case SYS_TELL:
			break;
		case SYS_CLOSE:
			break;
	}

	//thread_exit ();
}

void halt(void)
{
	power_off();
}

void exit(int status) {
	struct thread *cur = thread_current();
    // cur->exit_status = status;

	printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();
}

// pid_t exec(const char *cmd_line) {
	

// }

// int wait(pid_t pid) {


// }

int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	int write_result;

	// lock_acquire(&fs_lock);

	if(fd == 1)
	{	
		putbuf(buffer, size);
		write_result = size;
	}
	else
	{

		if(find_file_by_fd(fd) != NULL)
			write_result = file_write(find_file_by_fd(fd),buffer,size);
		else
			write_result = -1;
	}
	// lock_release(&fs_lock);
	return write_result;
}



void check_address(const uint64_t *addr)	
{
	struct thread *cur = thread_current();
	if (addr == NULL || !(is_user_vaddr(addr)) || pml4_get_page(cur->pml4, addr) == NULL) {
		exit(-1);
	}
}

static struct file *find_file_by_fd(int fd) {
	struct thread *cur = thread_current();

	if (fd < 0 || fd >= FDCOUNT_LIMIT) {
		return NULL;
	}
	return cur->fd_table[fd];
}