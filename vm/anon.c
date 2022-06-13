/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
// user addition
#include "threads/vaddr.h"
#include "lib/kernel/bitmap.h"
#include "threads/mmu.h"
/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

// user addition
static struct bitmap *swap_table;
const size_t SECTORS_IN_PAGE = PGSIZE/DISK_SECTOR_SIZE;
//


/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
	swap_table = bitmap_create(disk_size(swap_disk)/SECTORS_IN_PAGE);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	// printf("anon initializer\n");
	struct uninit_page *uninit = &page->uninit;
    memset(uninit, 0, sizeof(struct uninit_page));  
	/* Set up the handler */

	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;

	anon_page->swap_idx = -1;
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	// printf("swap in\n");
	struct anon_page *anon_page = &page->anon;

	if (bitmap_test(swap_table, anon_page->swap_idx) == false) {
		return false;
	}
	for (int i = 0; i < SECTORS_IN_PAGE; i++) {
		disk_read(swap_disk, anon_page->swap_idx * SECTORS_IN_PAGE + i, kva + DISK_SECTOR_SIZE * i);
	}
	bitmap_set(swap_table, anon_page->swap_idx, false);
	anon_page->swap_idx = -1;
	return true; 
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	size_t bitmap_idx;
	bitmap_idx = bitmap_scan_and_flip(swap_table, 0, 1, false);

	if (bitmap_idx == BITMAP_ERROR) {
		// error handling
		return false;
	}

	for (int i = 0; i < SECTORS_IN_PAGE; i++) {
		disk_write(swap_disk, 
				   bitmap_idx * SECTORS_IN_PAGE + i, 
				   page->frame->kva + DISK_SECTOR_SIZE * i);
	}
	page->anon.swap_idx = bitmap_idx;
	// printf("anon swap out: %d\n", bitmap_idx);
	pml4_clear_page(page->t->pml4, page->va);
	page->frame->page = NULL;
	page->frame = NULL;
	return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	return; 
}
