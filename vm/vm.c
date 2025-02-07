/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"

// addition
#include "lib/kernel/hash.h" 
#include "threads/mmu.h"
#include "filesys/page_cache.h"
struct list frame_table;

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
	list_init(&frame_table);
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
	// printf("vm type: %d upage: %p writble: %d\n", type, upage, writable);
	ASSERT (VM_TYPE(type) != VM_UNINIT)
	struct supplemental_page_table *spt = &thread_current ()->spt;
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *new_page = (struct page*)malloc(sizeof(struct page));
		typedef bool (*page_inintializer) (struct page *, enum vm_type, void *kva);
		page_inintializer initializer = NULL;
		
		if(VM_TYPE(type) == VM_ANON) {
			initializer = anon_initializer;	
			// writable = 1;
		}
		else if(VM_TYPE(type) == VM_FILE) initializer = file_backed_initializer;
		
		// project 4
		else if(VM_TYPE(type) == VM_PAGE_CACHE) initializer = page_cache_initializer;
		//
		uninit_new(new_page, upage, init, type, aux, initializer);
		
		new_page->t = thread_current();
		new_page->writable = writable;
		new_page->type = type;

		/* TODO: Insert the page into the spt. */
	
		if(!spt_insert_page(spt, new_page)){
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
	page = page_lookup(spt, pg_round_down(va));
	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt,
		struct page *page) {
	bool succ = false;
	/* TODO: Fill this function. */
	if (hash_insert(&spt->pages, &page->spt_elem) == NULL){
		succ = true;
	}
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
	
	// FIFO
	struct list_elem *e = list_pop_front(&frame_table);
	victim = list_entry(e, struct frame, ft_elem);
	
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	if (swap_out(victim->page)) {
		return victim;
	}
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
		free(frame);
		struct frame *evicted_frame = vm_evict_frame();
		// evict the page
		// if (evicted_frame == NULL) {
		// 	//error
		// 	return NULL;
		// }
		ASSERT(evicted_frame != NULL);

		list_push_back(&frame_table, &evicted_frame->ft_elem);
		return evicted_frame;
		// USERTODO
	}
	frame->kva = addr_new_allocated_page;
	frame->page = NULL;

	list_push_back(&frame_table, &frame->ft_elem);
	
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static bool
vm_stack_growth (void *addr) {
	bool succ = vm_alloc_page(VM_ANON | VM_MARKER_0, addr, true);
	if(succ){ 
		thread_current()->stack_bottom -= PGSIZE;
	}
	return succ;
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f, void *addr,
		bool user, bool write, bool not_present) {
	struct supplemental_page_table *spt = &thread_current ()->spt;
	struct page *page = NULL;
	// printf("vm try handle fault\n");
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	// printf("start %p\n", addr);
	// writing r/o page
	// if (!not_present) return false;
	// printf("end\n");
	
	// kernel_vaddr => false 
	if (is_kernel_vaddr(addr)) return false;
	
	// addr not in spt 
	if ((page = spt_find_page(spt, addr)) == NULL) {
		// check stack
		void *rsp = user ? f->rsp : thread_current()->stack_rsp;
		void *stack_addr = thread_current()->stack_bottom - PGSIZE;

		// base conditions
		if(addr >= rsp - 8 && addr >= USER_STACK - 0x100000 && addr <= USER_STACK) {
			//stack growth
			if(vm_stack_growth(stack_addr)){
				page = spt_find_page(spt, stack_addr); 
			}
			else {
				return false;
			}
		}
		else {
			return false; 
		}
	}
	// printf("page: %p type: %d va: %p writable: %d\n",page, page->type, page->va, page->writable);
	// write && read_only => false
	if (write && !page->writable) return false;
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
	struct page *page = spt_find_page(&thread_current()->spt, va);
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
supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src) {
	if (hash_empty(&src->pages)) 
		return true;

	struct hash_iterator iter;
	hash_first(&iter, &src->pages);

	while (hash_next(&iter)) {
		// parent's spt's page

		struct page *page = hash_entry(hash_cur(&iter), struct page, spt_elem);

		if (VM_TYPE(page->operations->type) == VM_UNINIT) {
			
			if (!vm_alloc_page_with_initializer(page->type, page->va, page->writable, page->uninit.init, page->uninit.aux)) {
				// errore handling USERTODO 
				return false;
			}
		}
		
		else if (VM_TYPE(page->operations->type) == VM_ANON) {
			if (!vm_alloc_page_with_initializer(page->type, page->va, page->writable, 0, 0)) {
				// errore handling USERTODO 
				return false;
			} 
		}
		else if (VM_TYPE(page->operations->type) == VM_FILE) {
			struct Inform_mmap_file *imf = (struct Inform_mmap_file *)malloc(sizeof(struct Inform_mmap_file));
			if (imf == NULL) return false;
			struct file_page *file_page = &page->file;
			
			imf->file = file_page->file;
			imf->mmap_addr = file_page->mmap_addr;
			imf->number_of_page = file_page->number_of_pages;
			imf->ofs = file_page->file_offset;
			imf->page_read_bytes = file_page->length;
			imf->page_zero_bytes = PGSIZE - imf->page_read_bytes;

			if (!vm_alloc_page_with_initializer(page->type, page->va, page->writable, lazy_load_segment_file, imf)) {
				// errore handling USERTODO 
				free(imf);
				return false;
			}
		}

		struct page *child = spt_find_page(dst,page->va);
		
		if (!vm_do_claim_page(child)) {
			// errore handling USERTODO
			return false;
		} 

		if (page->frame == NULL)
		{
			vm_do_claim_page(page);
		}

		memcpy(child->frame->kva, page->frame->kva, PGSIZE);

		// for COW
		// if (page->frame == NULL) {
		// 	if (!vm_do_claim_page(page)) {
		// 		// error hanlding
		// 	}
		// }
		// child->frame = page->frame;
		// if (!pml4_set_page(child->t->pml4, child->va, child->frame->kva, child->writable)){
		// 	//USERTODO
		// 	return false;
		// }
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	if(hash_empty(&spt->pages)) 
		return;

	struct hash_iterator iter;
	hash_first(&iter, &spt->pages);

	while(hash_next(&iter)) {
		struct page *page = hash_entry(hash_cur(&iter), struct page, spt_elem);
		destroy(page);
	}
}

// addition for spt hash table
uint64_t page_hash_func (const struct hash_elem *e, void *aux UNUSED){
	const struct page *p = hash_entry(e, struct page, spt_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}
bool page_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
	const struct page *page_a = hash_entry(a, struct page, spt_elem);
	const struct page *page_b = hash_entry(b, struct page, spt_elem);

	return page_a->va < page_b->va;
}
struct page* page_lookup (struct supplemental_page_table *spt, const void *address) {
  struct page p;
  struct hash_elem *e;

  p.va = address;
  e = hash_find (&spt->pages, &p.spt_elem);
  return e != NULL ? hash_entry (e, struct page, spt_elem) : NULL;
}