#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"

extern int memsize;

extern int debug;

extern struct frame* coremap;

unsigned* timestamp_list;
unsigned timestamp;

//region DESCRIPTION OF LRU IMPLEMENTATION

/*
 * We are going to be implementing exact LRU with timestamps
 * Thus each PTE needs a timestamp, so we will maintain an array of unsigned timestamps.
 * The indices of this array will be frame numbers.
 * Unsigned int timestamps should be accurate within 2^32 - 1 references, which is very large.
 *
 * We are using the timestamp implementation because
 *      eviction is much less frequent than referencing (when memory is sufficiently large)
 * Timestamp gives us O(n) eviction and O(1) referencing
 * Stack implementation would give O(1) eviction but O(n) referencing
 * */

//endregion

unsigned get_timestamp(pgtbl_entry_t* p){
    int frame_number = p->frame >> PAGE_SHIFT;
    return timestamp_list[frame_number];
}

void set_timestamp(pgtbl_entry_t* p) {
    int frame_number = p->frame >> PAGE_SHIFT;
    timestamp_list[frame_number] = timestamp;
    timestamp++;
}

/* Page to evict is chosen using the accurate LRU algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int lru_evict() {

    int i;
    int oldest_ind = 0; // Index of oldest frame

    // Loop through all page tables
    for (i = 0; i < memsize; i++){
        struct frame cur_frame = coremap[i];
        struct frame oldest_frame = coremap[oldest_ind];
        if (get_timestamp(cur_frame.pte) < get_timestamp(oldest_frame.pte)){
            oldest_ind = i;
        }
    }

    // Frame bits of the oldest frame
    unsigned frame = coremap[oldest_ind].pte->frame;

    // Return the frame number of the oldest frame
    return frame >> PAGE_SHIFT;
}

/* This function is called on each access to a page to update any information
 * needed by the lru algorithm.
 * Input: The page table entry for the page that is being accessed.
 */

void lru_ref(pgtbl_entry_t* p) {
    set_timestamp(p);
    return;
}

/* Initialize any data structures needed for this 
 * replacement algorithm 
 */
void lru_init() {
    timestamp = 0;
    timestamp_list = calloc((size_t) memsize, sizeof(unsigned));
}
