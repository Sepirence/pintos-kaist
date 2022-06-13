#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "filesys/off_t.h"
tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

// user addition for lazy loading
struct Inform_load_file {
	struct file *file;
    off_t ofs;
    off_t page_read_bytes;
    off_t page_zero_bytes;
};
//
#endif /* userprog/process.h */
