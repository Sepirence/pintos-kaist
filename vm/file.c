/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
// user addition
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "threads/mmu.h"
//
static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;
	// user addition
	struct Inform_mmap_file *aux = (struct Inform_mmap_file *)page->uninit.aux;
	memset(&page->uninit, 0, sizeof(struct uninit_page));
	struct file_page *file_page = &page->file;

	file_page->file = aux->file;

	file_page->file_offset = aux->ofs;

	file_page->length = aux->page_read_bytes;

	file_page->mmap_addr = aux->mmap_addr;

	file_page->number_of_pages = aux->number_of_page;

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {

	struct file_page *file_page = &page->file;
	if (file_read_at(file_page->file, 
				     kva, 
					 file_page->length, 
					 file_page->file_offset) != file_page->length) {
		return false;
	}
	if (PGSIZE > file_page->length) 
		memset (kva + file_page->length, 0, PGSIZE - file_page->length);
	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {

	struct file_page *file_page = &page->file;
	if (!pml4_is_dirty(thread_current()->pml4, page->va)) {
		pml4_clear_page(thread_current()->pml4, page->va);
		page->frame->page = NULL;
		page->frame = NULL;
		return true; 
	}

	if (file_write_at(file_page->file, 
					  page->frame->kva, 
					  file_page->length, 
					  file_page->file_offset) != file_page->length) {
		return false;
	}
	pml4_clear_page(thread_current()->pml4, page->va);
	page->frame->page = NULL;
	page->frame = NULL;
	return true;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page = &page->file;
	if (pml4_is_dirty(thread_current()->pml4, page->va)) {
		// content write back
		do_munmap(file_page->mmap_addr); 
	}
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {

	size_t f_len = file_length(file);
	size_t number_of_page = length % PGSIZE == 0 ? length / PGSIZE : length / PGSIZE + 1;
	void *page_addr = addr;
	struct file *re_open_file = file_reopen(file);
	
	for (int i = 0; i < number_of_page; i++) {
		struct Inform_mmap_file *imf = (struct Inform_mmap_file *)malloc(sizeof(struct Inform_mmap_file));
		imf->file = re_open_file;
		imf->ofs = offset + PGSIZE * i;
		imf->page_read_bytes = f_len < PGSIZE ? f_len : PGSIZE;
		imf->page_zero_bytes = PGSIZE - imf->page_read_bytes;
		imf->number_of_page = number_of_page;
		imf->mmap_addr = addr;
		if (!vm_alloc_page_with_initializer(VM_FILE, page_addr, writable, lazy_load_segment_file, imf))
			return NULL;
		f_len -= PGSIZE;
		page_addr += PGSIZE;
	}

	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	struct thread *curr = thread_current();
	struct page *page = spt_find_page(&curr->spt, addr);
	// addr is not mmap addr
	if (page->file.mmap_addr != addr) {
		syscall_exit(-1);
	}

	//
	if (VM_TYPE(page_get_type(page)) != VM_FILE) {
		return;
	}

	// Then page is first page of mmap file
	struct file *file = page->file.file;
	int number_of_pages = page->file.number_of_pages;

	for (int i = 0; i < number_of_pages; i++) {
		struct page *next_page = spt_find_page(&curr->spt, addr + PGSIZE * i);
		
		if (pml4_is_dirty(curr->pml4, next_page->va)) {
			if (next_page->file.length != file_write_at(file, next_page->va, next_page->file.length, next_page->file.file_offset))
				// error
				return;
		}

	}

	return;
}

bool
lazy_load_segment_file (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	struct Inform_mmap_file *imf = (struct Inform_mmap_file *)aux;
	file_seek(imf->file, imf->ofs);
	int read_byte = file_read(imf->file, page->frame->kva, imf->page_read_bytes);
	if(read_byte != (int)imf->page_read_bytes) 
	{	
		// error handling USERTODO
		return false;
	}
	memset (page->frame->kva + imf->page_read_bytes, 0, imf->page_zero_bytes);

	return true;
}