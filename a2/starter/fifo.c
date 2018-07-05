#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

// a list of pages in order in which they were paged in
int *page_list;

/* Page to evict is chosen using the fifo algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

void update_page_list() {
    int index;
    // move all entry in the page_list to the left by one after eviction
    for (index = 0; index < memsize - 1; index++) {
        page_list[index] = page_list[index+1];
        // if the next entry is -1, which is not used, everything after that index are all -1
        if (page_list[index + 1] == -1) break;
    }
    // we did not loop to the last entry in the page_list
    page_list[memsize - 1] = -1;
}

int fifo_evict() {
    int evicted_frame = page_list[0];
    update_page_list();

	return evicted_frame;
}

/* This function is called on each access to a page to update any information
 * needed by the fifo algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void fifo_ref(pgtbl_entry_t *p) {
    int index;
    int base_frame_number = p->frame >> PAGE_SHIFT;

    // insert the base_frame_number into the last unused entry in page_list
    for (index = 0; index < memsize; index++) {
        if (page_list[index] == -1) {
            // update page_list with the new base frame number
            page_list[index] = base_frame_number;
            return;
        }
        // if there's a same frame already being used, no action required
        else if (page_list[index] == base_frame_number) return;
    }
}

/* Initialize any data structures needed for this 
 * replacement algorithm 
 */
void fifo_init() {
    int index;
    // memory allocation for page_list, one integer space (frame) per memory size
    page_list = malloc(memsize * sizeof(int));
    // initialize all entry of the list to -1 at beginning
    for (index = 0; index < memsize; index++) page_list[index] = -1;
}
