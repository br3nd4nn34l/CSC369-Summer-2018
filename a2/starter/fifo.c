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

//region Circular Queue Implementation

// Queue of NON-NEGATIVE integers
typedef struct {
    int size;
    int front; // Index of the front of the queue
    int back; // Index of the front of the queue
    int* contents; // Array of NON-NEGATIVE integers (-1 implies emptiness)
} Queue;
static int EMPTY_QUEUE_SLOT = -1;

// Initializes the initial contents for the queue
int* initContents(int size){
    int i;
    int* ret = malloc(sizeof(int) * size);
    for (i = 0; i < size; i++){
        ret[i] = EMPTY_QUEUE_SLOT;
    }
    return ret;
}

// Constructs an empty Queue
Queue* makeQueue(int size){
    Queue* ret = malloc(sizeof(Queue));
    ret->size = size;
    ret->front = 0;
    ret->back = 0;
    ret->contents = malloc(size * sizeof(int));
    return ret;
}

// Puts value into q
void enqueue(Queue* q, int value){
    // Set the value at the back of the queue
    q->contents[q->back] = value;

    // Move the back rightwards, but make sure to loop around
    q->back = (q->back + 1) % (q->size);
}

// Gets the back value out of queue
int dequeue(Queue* q){

    // Get the value at the front of the queue
    int ret = q->contents[q->front];

    // Only move the front if it's non-empty
    if (ret != EMPTY_QUEUE_SLOT){
        q->contents[q->front] = EMPTY_QUEUE_SLOT; // Remove the old front value
        q->front = (q->front + 1) % (q->size); // Move front towards back
    }

    return ret;
}

// Return whether value is in q
int contains(Queue* q, int value){
    int i;
    // Loop from front (leftmost), to back (rightmost)
    for (i = q->front; i != q->back; i = (i + 1) % q->size){
        if (q->contents[i] == value){
            return 1;
        }
    }
    return 0;
}

//endregion

// Circular queue of page numbers
Queue* queue;

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

    // Dequeue a frame number (should be the oldest in the queue)
    int victim = dequeue(queue);

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

    // Enqueue base_frame_number if it's new
    if (!contains(queue, base_frame_number)){
        enqueue(queue, base_frame_number);
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

    // Initialize circular queue
    queue = makeQueue(memsize);
}
