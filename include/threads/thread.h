#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H
// #define FDT_PAGES 3
#define FDCOUNT_LIMIT (1 << 9)

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

typedef int64_t fp;

#define F 1<<14

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */
	
	
	/// user_addition in project 1

	int64_t sleep_ticks;				/* */
	int original_priority;				/* Store original priority if priority donation occurs*/

	struct list donate_list;			/* list of thread have donated priority */
	struct list_elem donate_elem;		/* list_elem structure for donate_list */
	struct lock *waiting_lock;			/* indicate current waiting lock */

	// Advanced Scheduler
	int nice;
	fp recent_cpu;
	///

	struct file *load_file;

	
// #ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */

	/// user_addition in project 2

	struct file **fd_table;				/* list of fd that thread currently occupied */
	int fd_idx;							/* index of fd_table */
	
	int exit_status;					/* store */	

	// Fork Status
	struct intr_frame p_if;				/* deliver parent's intr_frame to child on fork */

	struct list child_list;				
	struct list_elem child_elem;


	struct semaphore fork_sema;
	struct semaphore wait_sema;
	struct semaphore synch_sema;
	///
	///
	///

// #endif
// #ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
	void* stack_bottom;
	void* stack_rsp;
// #endif

// #ifdef EFILESYS
	struct dir *current_dir;
	// char *current_dir;
	// char *previous_dir;
	// char current_dir[50];
// #endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

/* States in a thread's life cycle. */
enum cmp_fun_num {
	PRIORITY,
	SEMA,
	DONATE
};

void user_timer_sleep(int64_t ticks);
void user_timer_wakeup(int64_t ticks);
bool compare_function(const struct list_elem *a, const struct list_elem *b, void *aux);

void cpu_increment (void);
void cpu_recalculation(void);
void calculating_load_avg(void);
void priority_calculation(void);

fp int_to_fp(int n);
int fp_to_int_zero(fp x);
int fp_to_int_near(fp x);
fp add_fp(fp x, fp y);

fp sub_fp(fp x, fp y);
fp add_int(fp x, int n);
fp sub_int(fp x, int n);
fp mul_fp(fp x, fp y);
fp mul_int(fp x, int n);
fp div_fp(fp x, fp y);
fp div_int(fp x, int n);

bool check_preemption(void);









#endif /* threads/thread.h */
