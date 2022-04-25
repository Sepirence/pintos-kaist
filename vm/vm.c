/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

// addition
#include "lib/kernel/hash.h" 
#include "threads/mmu.h"
#include "filesys/page_cache.h"
// #include "iib/threads/synch.h" 
//

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	struct frame_table *frame_table;
	list_init(&frame_table->frames);
	lock_init(&frame_table->frame_lock);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *new_page = (struct page*)malloc(sizeof(struct page));
		
		typedef bool (*page_inintializer) (struct page *, enum vm_type, void *kva);
		page_inintializer initializer;
		
		if(type == VM_ANON)	initializer = anon_initializer;	
		if(type == VM_FILE) initializer = file_backed_initializer;
		
		// project 4
		if(type == VM_PAGE_CACHE) = page_cache_initializer;
		//
		
		new_page->t = thread_current();
		new_page->writable = writable;

		uninit_new(new_page, upage, init, type, aux, initializer);

		/* TODO: Insert the page into the spt. */

		if(!spt_insert_page(thread_current()->spt, new_page)){
			// error handling USERTODO
			free(new_page);
			goto err;
		}
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt, void *va) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	page = page_lookup(spt, va);
	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	bool succ = false;
	/* TODO: Fill this function. */
	if (hash_insert(&spt, &page->spt_elem) == NULL)
		succ = true;
	
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
		
	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame*)malloc(sizeof(struct frame));
	/* TODO: Fill this function. */

	void *addr_new_allocated_page = palloc_get_page(PAL_USER);
	
	// 0x4000000 ~ 0x80040000 유저영역
	// 0x800400000 ~ 끝 커널
	// kva 0x80040000 + 0x123 == physical memeory 0x123

	// no available page
	if (addr_new_allocated_page == NULL){
		// evict the page
		free(frame);
		return vm_evict_frame();
		// USERTODO
	}
	frame->kva = addr_new_allocated_page;
	frame->page = NULL;

	list_push_back(&frame_table->frames, frame->ft_elem);
	
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va) { 
	struct page *page = spt_find_page(thread_current()->spt, va);
	/* TODO: Fill this function */
	
	// pml4 -> kva 주소
	// spt -> page 정보
	
	if(page == NULL)
	{
		//USERTODO
		return false;
	}
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;
	
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if(!pml4_set_page(page->t->pml4, page->va, frame->kva, page->writable)){
		//USERTODO
		return false;
	}

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt) {
	hash_init(&spt->pages, page_hash_func, page_less_func, NULL);
}


/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
}

// addition for spt hash table
uint64_t page_hash_func (const struct hash_elem *e, void *aux UNUSED){
	const struct page *p = hash_entry(e, struct page, spt_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}
bool page_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
	const struct page *page_a = hash_entry(a, struct page, spt_elem);
	const struct page *page_b = hash_entry(b, struct page, spt_elem);

	return page_a-> va < page_b->va;
}
struct page* page_lookup (struct supplemental_page_table *spt, const void *address) {
  struct page p;
  struct hash_elem *e;

  p.va = address;
  e = hash_find (&spt, &p.spt_elem);
  return e != NULL ? hash_entry (e, struct page, spt_elem) : NULL;
}