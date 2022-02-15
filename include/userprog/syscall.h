#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

struct lock *fs_lock;

#endif /* userprog/syscall.h */
