#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/synch.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "lib/string.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void 	 halt(void);
void 	 exit(int status);
//tid_t 	 fork(const char *thread_name);
int 	 exec(const char *cmd_line);
// int	 wait(pid_t pid); on process.c
bool	 create(const char *file, unsigned initial_size);
bool	 remove(const char *file);
int 	 open(const char *file);
int		 filesize(int fd);
int 	 read(int fd, void *buffer, unsigned size);
int 	 write(int fd, const void *buffer, unsigned size);
void	 seek(int fd, unsigned position);
unsigned tell(int fd);
void	 close(int fd);



void check_address(const uint64_t *addr);
static struct file *find_file_by_fd(int fd);
static int add_file_to_fdt(struct file *f);
static void remove_file_from_fdt(int fd);


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
	
	lock_init(&file_rw_lock);

}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	//  printf("!!!%x\n",f->R.rax);
	//  printf ("system call!\n");
 
	switch(f->R.rax)
	{
		case SYS_HALT:
			break;
		case SYS_EXIT:
			exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = process_fork(f->R.rdi,f);
			break;
		case SYS_EXEC:
			if(exec(f->R.rdi) == -1)
				exit(-1);
			break;
		case SYS_WAIT:
			f->R.rax = process_wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			// f->R.rax = remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			// f->R.rax = tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			close(f->R.rdi);
			break;
		default:
			exit(-1);
			break;
	}
}

void halt(void)
{
	power_off();
}

void exit(int status) {
	struct thread *curr = thread_current();
    curr->exit_status = status;

	printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();
}

int exec(const char *cmd_line) {
	check_address(cmd_line);

	int file_size = strlen(cmd_line) + 1;
	char *fn_copy;

	if((fn_copy = palloc_get_page(PAL_ZERO)) == NULL)
		exit(-1);

	strlcpy(fn_copy,cmd_line,file_size);

	if(process_exec(fn_copy) == -1)
		return -1;

	NOT_REACHED();
	return 0;

}

bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	return filesys_create(file, initial_size);
}

int open(const char *file)
{
	check_address(file);
	struct file *f = filesys_open(file);
	if(f == NULL)
		return -1;
	int fd = add_file_to_fdt(f);
	
	if(strcmp(file, thread_current()->name) == 0)
		file_deny_write(f);

	if(fd == -1)
		return -1;
	return fd;
}

int filesize(int fd)
{
	struct file *f = find_file_by_fd(fd);

	if (f == NULL)
		return -1;

	return file_length(f);
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	int read_result;
	if(find_file_by_fd(fd) == NULL)
		return -1;
	if(fd == 1)
	{
		int i;
		unsigned char *buf = buffer;
		for(i=0; i< size; i++)
		{
			char c = input_getc();
			*buf++ = c;
			if(c=='\0')
				break;
		}
		read_result = i;
	}
	else
	{
		lock_acquire(&file_rw_lock);
		read_result = file_read(find_file_by_fd(fd),buffer,size);
		lock_release(&file_rw_lock);
	}

	return read_result;

}

int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);

	int write_result;
	lock_acquire(&file_rw_lock);

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
	lock_release(&file_rw_lock);
	return write_result;
}

struct file {
	struct inode *inode;        /* File's inode. */
	off_t pos;                  /* Current position. */
	bool deny_write;            /* Has file_deny_write() been called? */
};

void seek(int fd, unsigned position)
{
	struct file *f = find_file_by_fd(fd);
	if(f == NULL)
		return;
	
	f->pos = position; 
}


void close(int fd)
{
	struct file *f;
		
	if((f= find_file_by_fd(fd)) == NULL)
		return;

	file_allow_write(f);

	remove_file_from_fdt(fd);
}


void check_address(const uint64_t *addr)	
{
	struct thread *cur = thread_current();
	if (addr == NULL || !(is_user_vaddr(addr)) || pml4_get_page(cur->pml4, addr) == NULL) 
	{
		exit(-1);
	}
	if(is_kernel_vaddr(addr))
		exit(-1);
}

static struct file *find_file_by_fd(int fd) 
{
	struct thread *curr = thread_current();
	if (fd < 2 || fd >= FDCOUNT_LIMIT) {
		return NULL;
	}
	return curr->fd_table[fd];

}

static int add_file_to_fdt(struct file *f)
{
	struct thread *curr = thread_current();
	struct file **fdt = curr->fd_table;

	while(curr->fd_idx < FDCOUNT_LIMIT && fdt[curr->fd_idx] != NULL)
		curr->fd_idx++;

	if(curr->fd_idx >= FDCOUNT_LIMIT)
		return -1;
	
	fdt[curr->fd_idx] = f;
	return curr->fd_idx;
}

static void remove_file_from_fdt(int fd)
{
	struct thread *curr = thread_current();
	if(fd < 0 || fd >= FDCOUNT_LIMIT)
		return;
	curr->fd_table[fd] = NULL;
}