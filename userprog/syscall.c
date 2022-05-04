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

// for vm user addition
#include "vm/vm.h"
//


void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void 	 syscall_halt(void);
void 	 syscall_exit(int status);
tid_t  	 syscall_fork(const char *thread_name, struct intr_frame *f);
int 	 syscall_exec(const char *cmd_line);
int	 	 syscall_wait(tid_t pid);
bool	 syscall_create(const char *file_name, unsigned initial_size);
bool	 syscall_remove(const char *file_name);
int 	 syscall_open(const char *file_name);
int		 syscall_filesize(int fd);
int 	 syscall_read(int fd, void *buffer, unsigned size);
int 	 syscall_write(int fd, const void *buffer, unsigned size);
void	 syscall_seek(int fd, unsigned position);
unsigned syscall_tell(int fd);
void	 syscall_close(int fd);

// extra
int syscall_dup2(int oldfd, int newfd);
static bool is_valid_file_descriptor(int fd);
// extra

void is_valid_addr(const uint64_t *addr);
static struct file *find_file_by_fd(int fd);
static int add_file_to_fdt(struct file *f);
static void remove_file_from_fdt(int fd);
static bool is_stdin(int fd);
static bool is_stdout(int fd);


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
	
	// DEBUG
	// printf ("system call! %d\n",f->R.rax);

	thread_current()->stack_rsp = f->rsp;
 
	switch(f->R.rax)
	{
		case SYS_HALT:
			syscall_halt();
			break;
		case SYS_EXIT:
			syscall_exit(f->R.rdi);
			break;
		case SYS_FORK:
			f->R.rax = syscall_fork(f->R.rdi,f);
			break;
		case SYS_EXEC:
			if(syscall_exec(f->R.rdi) == -1)
				syscall_exit(-1);
			break;
		case SYS_WAIT:
			f->R.rax = syscall_wait(f->R.rdi);
			break;
		case SYS_CREATE:
			f->R.rax = syscall_create(f->R.rdi, f->R.rsi);
			break;
		case SYS_REMOVE:
			f->R.rax = syscall_remove(f->R.rdi);
			break;
		case SYS_OPEN:
			f->R.rax = syscall_open(f->R.rdi);
			break;
		case SYS_FILESIZE:
			f->R.rax = syscall_filesize(f->R.rdi);
			break;
		case SYS_READ:
			f->R.rax = syscall_read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_WRITE:
			f->R.rax = syscall_write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		case SYS_SEEK:
			syscall_seek(f->R.rdi, f->R.rsi);
			break;
		case SYS_TELL:
			f->R.rax = syscall_tell(f->R.rdi);
			break;
		case SYS_CLOSE:
			syscall_close(f->R.rdi);
			break;
		case SYS_DUP2:
			f->R.rax = syscall_dup2(f->R.rdi, f->R.rsi);
			break;
		default:
			NOT_REACHED();
			syscall_exit(-1);
			break;
	}
}

void syscall_halt(void)
{
	power_off();
}

void syscall_exit(int status) {
	struct thread *curr = thread_current();
    curr->exit_status = status;

	printf("%s: exit(%d)\n", thread_name(), status);
	
	thread_exit();
}

tid_t syscall_fork(const char *thread_name, struct intr_frame *f)
{
	return process_fork(thread_name,f);
}

int syscall_exec(const char *cmd_line) {
	is_valid_addr(cmd_line);

	int file_size = strlen(cmd_line) + 1;
	// const cannot be modified
	char *cmd_copy;
	
	// PALLOC_SYS_EXEC
	if((cmd_copy = palloc_get_page(PAL_ZERO)) == NULL){
		// PALLOC_SYS_EXEC is not allocated, so does not need to free		
		syscall_exit(-1);
		//
	}
	strlcpy(cmd_copy,cmd_line,file_size);

	if(process_exec(cmd_copy) == -1){
		// in exec, cmd_copy is freed
		return -1;
	}
}

int	syscall_wait(tid_t pid)
{
	return process_wait(pid);
}

bool syscall_create(const char *file_name, unsigned initial_size)
{
	is_valid_addr(file_name);
	return filesys_create(file_name, initial_size);
}

bool syscall_remove(const char *file_name)
{	
	is_valid_addr(file_name);
	return filesys_remove(file_name);
}

int syscall_open(const char *file_name)
{
	is_valid_addr(file_name);
	struct file *f = filesys_open(file_name);

	if(f == NULL)
		return -1;

	int fd = add_file_to_fdt(f);

	if(fd == -1)
	{
		filesys_remove(file_name);

		return -1;
	}
	
	if(strcmp(file_name, thread_current()->name) == 0)
		file_deny_write(f);
	
	return fd;
}

int syscall_filesize(int fd)
{
	struct file *f = find_file_by_fd(fd);

	if (f == NULL)
		return -1;

	return file_length(f);
}

int syscall_read(int fd, void *buffer, unsigned size)
{	
	// fail => syscall exit
	is_valid_addr(buffer);
	
	// checking buffer
	// buffer should not be in code segment
	struct page *buffer_page = spt_find_page(&thread_current()->spt, buffer);
	if(!buffer_page->writable)
		syscall_exit(-1);

	int read_result;
	

	// invalid fd
	if (is_valid_file_descriptor(fd) == false) return -1;
	
	
	// stdin
	if(is_stdin(fd))
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
		struct thread *curr = thread_current();
		if(curr->fd_table[fd] == NULL)
		{
			return -1;
		}

		lock_acquire(&file_rw_lock);
		read_result = file_read(find_file_by_fd(fd),buffer,size);
		lock_release(&file_rw_lock);
	}

	return read_result;
}

int syscall_write(int fd, const void *buffer, unsigned size)
{
	is_valid_addr(buffer);
	
	//invalid fd
	if (is_valid_file_descriptor(fd) == false) return -1;

	int write_result;
	lock_acquire(&file_rw_lock);

	if(is_stdout(fd))
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

void syscall_seek(int fd, unsigned position)
{
	struct file *file = find_file_by_fd(fd);
	if(file == NULL)
		return;
	
	file_seek(file, position);
}

unsigned syscall_tell(int fd)
{	
	struct file *file = find_file_by_fd(fd);
	if(file == NULL)
		return -1;
		
	return file_tell(file);
}

void syscall_close(int fd_idx)
{	
	// closing stdin, stdout
	if (is_stdin(fd_idx) || is_stdout(fd_idx)) {
		thread_current()->fd_table[fd_idx] = false;
		return;
	}

	struct file *file;
		
	if((file = find_file_by_fd(fd_idx)) == NULL)
	{	
		return;
	}
	
	// syscall_dup2 fd_idx NULL로 초기화
	struct thread *curr = thread_current();

	curr->fd_table[fd_idx] = NULL;

	// 만약 다른 fd_table[i] 중 같은 파일을 가리키는 것이 없으면 파일 close
	// if no fd_table[i] points file, then close that file
	int no_fd_points_file = true;

	for (int i = 0; i < FDCOUNT_LIMIT; i++)
	{
		if (curr->fd_table[i] == file)
		{	
			no_fd_points_file = false;
		}
	}
	if (no_fd_points_file) file_close(file);
	
}

void is_valid_addr(const uint64_t *addr)	
{
	struct thread *curr = thread_current();
	// printf("addr: %p\n", addr);
	// check that address is NULL

	if (addr == NULL 
		|| is_kernel_vaddr(addr)
		|| spt_find_page(&curr->spt, addr) == NULL)
		
		syscall_exit(-1);

}

// return file or null
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

	while(curr->fd_idx < FDCOUNT_LIMIT && fdt[curr->fd_idx])
		curr->fd_idx++;

	if(curr->fd_idx >= FDCOUNT_LIMIT)
		return -1;
	
	fdt[curr->fd_idx] = f;
	curr->fd_idx++;
	return curr->fd_idx - 1;
}


static void remove_file_from_fdt(int fd)
{
	struct thread *curr = thread_current();
	if(fd < 0 || fd >= FDCOUNT_LIMIT)
		return;

	file_close(curr->fd_table[fd]);
	curr->fd_table[fd] = NULL;
}

// extra
int syscall_dup2(int oldfd, int newfd)
{	
	struct thread *curr = thread_current();
	struct file *oldfd_file;

	// if oldfd or newfd invalid file descriptor
	if (is_valid_file_descriptor(oldfd) == false 
		|| is_valid_file_descriptor(newfd) == false) 
	{
		return -1;
	}
	/////////////////////////
	// valid file descriptor
	/////////////////////////

	// if oldfd is stdin or stdout
	if(is_stdin(oldfd))
	{
		curr->fd_table[newfd] = STDIN;
		return newfd;
	}
	if(is_stdout(oldfd))
	{
		curr->fd_table[newfd] = STDOUT;
		return newfd;
	}

	// oldfd doen not point certain file
	if ((oldfd_file = find_file_by_fd(oldfd)) == NULL)
	{	
		return -1;
	}
	
	struct file *newfd_file;
	// newfd does not point file
	if ((newfd_file = find_file_by_fd(newfd)) == NULL) 
	{
		curr->fd_table[newfd] = oldfd_file;
	}
	// newfd does point file
	else 
	{	
		// both point same file
		if (oldfd_file == newfd_file)
		{
			return newfd;
		}

		// different file
		// close newfd_file
		syscall_close(newfd);
		curr->fd_table[newfd] = oldfd_file;
	}
	

	return newfd;

}

static bool is_valid_file_descriptor(int fd)
{

	if (fd < 0 || fd >= FDCOUNT_LIMIT) 
		return false;
	
	else 
		return true;

}

static bool is_stdin(int fd)
{	
	
	struct thread *curr = thread_current();
	if(curr->fd_table[fd] == STDIN)
		return true;
	else
		return false;
}

static bool is_stdout(int fd)
{
	struct thread *curr = thread_current();
	if(curr->fd_table[fd] == STDOUT)
		return true;
	else
		return false;
}