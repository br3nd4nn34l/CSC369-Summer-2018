#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

int clock_arm;

/* Page to evict is chosen using the clock algorithm.
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */

int is_referenced() {
    return coremap[clock_arm].pte->frame & PG_REF;
}

void turn_off_reference() {
    coremap[clock_arm].pte->frame &= ~PG_REF;
}

void sweep_clock_arm(){
    clock_arm = (clock_arm + 1) % memsize;
}

int clock_evict() {

    while (is_referenced()) {
        turn_off_reference();
        // move clock_arm in clock wised direction after turning off current frame's ref bit
        sweep_clock_arm();
    }
    // evict current frame, advance to next frame
    int current_frame = clock_arm;
    sweep_clock_arm();

	return current_frame;
}



/* This function is called on each access to a page to update any information
 * needed by the clock algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void clock_ref(pgtbl_entry_t *p) {
    // Don't need to do anything here,
    // R is the only thing that needs to be updated for clock,
    // and it's already updated when we reference a page
	return;
}

/* Initialize any data structures needed for this replacement
 * algorithm. 
 */
void clock_init() {
	// start the clock by pointing to 0th frame
	clock_arm = 0;
}
