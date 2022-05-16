#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"

struct page;
enum vm_type;

struct file_page {
	struct file* file;
	off_t file_offset;
	size_t length;
	void* mmap_addr;
	size_t number_of_pages;

};

/* An open file. */
struct file {
	struct inode *inode;        /* File's inode. */
	off_t pos;                  /* Current position. */
	bool deny_write;            /* Has file_deny_write() been called? */
};

// user addition for lazy loading
struct Inform_mmap_file {
	struct file *file;
    off_t ofs;
    off_t page_read_bytes;
    off_t page_zero_bytes;
	void *mmap_addr;
	int number_of_page;
	
};

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset);
void do_munmap (void *va);
// user addition
bool lazy_load_segment_file (struct page *page, void *aux);
//
#endif
