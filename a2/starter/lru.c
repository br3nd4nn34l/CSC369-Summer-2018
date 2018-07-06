#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"

extern int memsize;

extern int debug;

extern struct frame* coremap;

//region DESCRIPTION OF LRU IMPLEMENTATION

/*
 * We are going to be implementing exact LRU with timestamps
 * Thus each PTE needs a timestamp
 * Luckily the provided PTEs have a section of unused bits
 * Therefore we will leverage this section of bits to maintain our timestamp
 * Timestamps will be dictated by a cyclical counter that increments every reference
 *
 * We are using the timestamp implementation because
 *      eviction is much less frequent than referencing (when memory is sufficiently large)
 * Timestamp gives us O(n) eviction and O(1) referencing
 * Stack implementation would give O(1) eviction but O(n) referencing
 * */

//endregion

// TODO use an array to keep track of timestamp to avoid memory limit

//region Ticker Implementation

// Ticker that maintains a value and maximum
// If value exceeds maxValue, it is reset to 0
typedef struct {
    int value;
    int maxValue;
} Ticker;

// To construct a Ticker with maxValue
Ticker* makeTicker(int maxValue) {
    Ticker* ret = malloc(sizeof(Ticker));
    ret->value = 0;
    ret->maxValue = maxValue;

    return ret;
}

// Returns the last value of ticker and increments ticker's value
int tick(Ticker* ticker) {
    int ret = ticker->value;
    ticker->value = (ticker->value + 1) % ticker->maxValue;
    return ret;
}

//endregion

//region Timestamp Utilities


const int NUM_INDICATOR_BITS = 4; // S, R, D, V
const unsigned TIMESTAMP_MASK = ~0 & // Start with all 1s
                          ~PAGE_MASK & // Frame Number -> 0
                          ~PG_ONSWAP & // SWAP -> 0
                          ~PG_REF & // REF -> 0
                          ~PG_DIRTY & // DIRTY -> 0
                          ~PG_VALID; // VALID -> 0

const int MAX_TIMESTAMP = (int) (TIMESTAMP_MASK >> NUM_INDICATOR_BITS); // Shifted to cover indicator bits

unsigned get_timestamp(pgtbl_entry_t* p){
    return p->frame & TIMESTAMP_MASK;
}

void set_timestamp(pgtbl_entry_t* p, int timestamp) {

    // Assuming that timestamp has all zeros in LHS
    unsigned shifted_stamp = (unsigned) (timestamp << NUM_INDICATOR_BITS);

    // Making sure that the given stamp can fit into the timestamp mask
    unsigned masked_stamp = TIMESTAMP_MASK & shifted_stamp;

    // Replacing timestamp of p
    p->frame |= masked_stamp;
}

//endregion

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

//region Global Variables

Ticker* ticker; // For grabbing timestamp values

//endregion

void lru_ref(pgtbl_entry_t* p) {
    set_timestamp(p, tick(ticker));
    return;
}

/* Initialize any data structures needed for this 
 * replacement algorithm 
 */
void lru_init() {
    ticker = makeTicker(MAX_TIMESTAMP);
}
