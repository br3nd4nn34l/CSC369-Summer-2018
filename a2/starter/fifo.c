#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;


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
    ret->contents = initContents(size);

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
    for (i = 0; i < q->size; i++){
        if (q->contents[i] == value){
            return 1;
        }
    }
    return 0;
}

//endregion

// Circular queue of page numbers
Queue* queue;


int fifo_evict() {
    // Dequeue a frame number (should be the oldest in the queue)
	return dequeue(queue);
}

/* This function is called on each access to a page to update any information
 * needed by the fifo algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void fifo_ref(pgtbl_entry_t *p) {
    int base_frame_number = p->frame >> PAGE_SHIFT;

    // Enqueue base_frame_number if it's new
    if (!contains(queue, base_frame_number)){
        enqueue(queue, base_frame_number);
    }
}

/* Initialize any data structures needed for this 
 * replacement algorithm 
 */
void fifo_init() {
    // Initialize circular queue
    queue = makeQueue(memsize);
}
