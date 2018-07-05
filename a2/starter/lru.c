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
 * We are going to be implementing exact LRU
 * Thus each PTE needs a timestamp
 * Luckily the provided PTEs have a section of unused bits
 * Therefore we will leverage this section of bits to maintain our timestamp
 * Timestamps will be dictated by a cyclical counter that increments every reference
 * */

//endregion

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
}

// Returns the last value of ticker and increments ticker's value
int tick(Ticker* ticker) {
    int ret = ticker->value;
    ticker->value = (ticker->value + 1) % ticker->maxValue;
    return ret;
}

//endregion

//region Timestamp Utilities

unsigned TIMESTAMP_MASK = ~0 & // Start with all 1
                          ~PAGE_MASK & // Frame Number -> 0
                          ~PG_ONSWAP & // SWAP -> 0
                          ~PG_REF & // REF -> 0
                          ~PG_DIRTY & // DIRTY -> 0
                          ~PG_VALID; // VALID -> 0

static int NUM_INDICATOR_BITS = 4; // S, R, D, V
static int MAX_TIMESTAMP = (int) (TIMESTAMP_MASK >> NUM_INDICATOR_BITS); // Shifted to cover indicator bits

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
    int i =
    return 0;
}

/* This function is called on each access to a page to update any information
 * needed by the lru algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void lru_ref(pgtbl_entry_t* p) {
    set_timestamp(p, tick(ticker));
    return;
}


//region Global Variables

Ticker* ticker; // For grabbing timestamp values

//endregion

/* Initialize any data structures needed for this 
 * replacement algorithm 
 */
void lru_init() {
    ticker = makeTicker(MAX_TIMESTAMP);
}
