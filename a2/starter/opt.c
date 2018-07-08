#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"

extern int memsize;

extern int debug;

extern struct frame* coremap;

extern char* tracefile;

extern pgdir_entry_t pgdir[PTRS_PER_PGDIR];

//region DYNAMIC ARRAY IMPLEMENTATION

typedef struct {
    void** contents;
    size_t count;
    size_t capacity;
} List;

void** initNullContents(size_t size) {
    void** ret = malloc(sizeof(void*) * size);
    int i;
    for (i = 0; i < size; i++) {
        ret[i] = NULL;
    }
    return ret;
}

List* makeList(size_t size) {
    List* ret = malloc(sizeof(List));
    ret->contents = initNullContents(size);
    ret->count = 0;
    ret->capacity = size;
    return ret;
}

void destroyList(List* list) {
    free(list->contents);
    list->contents = NULL;
    list->count = list->capacity = 0;
    free(list);
}

// TODO SEE IF REALLOC WORKS
void** transferContents(size_t oldSize, size_t newSize, void** oldContents) {

    // Initialize new bin of contents
    void** ret = initNullContents(newSize);

    // Copy all content from the old contents over to the new one
    int i;
    for (i = 0; i < oldSize; i++) {
        ret[i] = oldContents[i];
    }

    // Tear down the old content array
    free(oldContents);

    // Return the new content array
    return ret;
}

// Appends value to the end of list
void listAppend(List* list, void* value) {

    // How much to grow the array by
    int GROWTH_RATE = 2;

    // The array is now full, so expand it
    if (list->count == list->capacity) {
        size_t oldSize = list->capacity;
        size_t newSize = oldSize * GROWTH_RATE;
        void** newContents = transferContents(oldSize, newSize, list->contents);
        list->contents = newContents;
        list->capacity = newSize;
    }

    list->contents[list->count] = value;
    list->count += 1;
}

// Finds the index that value occurs in list, or -1 if it does not
int listFind(List* list, void* value) {
    int i;
    for (i = 0; i < list->count; i++) {
        if (list->contents[i] == value) {
            return i;
        }
    }
    return -1;
}

// Removes the value at index from the list and returns it
void* listRemove(List* list, int index) {

    // Index out of bounds
    if (index < 0 || index > list->count - 1) {
        return NULL;
    }

    // Get what we're going to delete
    void* deleted = list->contents[index];

    // Shift everything over top of it from the right
    int i;
    for (i = index; i < list->count - 1; i++) {
        list->contents[i] = list->contents[i + 1];
    }
    list->contents[list->count - 1] = NULL;

    // Update list count to indicate that the list is now one shorter
    list->count -= 1;

    return deleted;
}

// Deletes a given value from the list
void listDelete(List* list, void* value) {

    // Find the index of the value
    int ind = listFind(list, value);

    // Abort if we could not find the value
    if (ind == -1) {
        return;
    }

    // Otherwise remove at the index
    listRemove(list, ind);
}

// Removes the value from the end of the list and returns it
void* listPop(List* list) {
    int lastInd = (int) list->count - 1;
    return listRemove(list, lastInd);
}

// Returns the last value in the list
void* listPeek(List* list) {

    // Abort, nothing is in the list
    if (list->count == 0) {
        return NULL;
    }

    size_t lastInd = list->count - 1;
    return list->contents[lastInd];
}

//endregion

//region HASH TABLE IMPLEMENTATION

typedef struct KeyValuePair {
    unsigned key;
    void* value;
    struct KeyValuePair* prev;
    struct KeyValuePair* next;
} KeyValuePair;

KeyValuePair* makeKeyValuePair(unsigned key, void* value) {
    KeyValuePair* ret = malloc(sizeof(KeyValuePair));
    ret->key = key;
    ret->value = value;
    ret->prev = NULL;
    ret->next = NULL;
    return ret;
}

// Frees the space for a key value pair (ASSUMED NOT TO HAVE PREV OR NEXT)
void* destroyDisconnectedKvp(KeyValuePair* pair) {

    // Abort if there is no pair
    if (pair == NULL) {
        return NULL;
    }

    // Extract value
    void* value = pair->value;

    // Free pair
    free(pair);

    // Return value
    return value;
}

// Disconnects KVP from its neighbours and vice versa
void disconnectKvp(KeyValuePair* pair) {

    KeyValuePair* prev = pair->prev;
    KeyValuePair* next = pair->next;

    // Make next previous' next
    if (prev != NULL) {
        prev->next = next;
    }

    // Make prev next's prev
    if (next != NULL) {
        next->prev = prev;
    }

    // Disconnect the given pair from next and prev
    pair->prev = NULL;
    pair->next = NULL;
}

typedef struct {
    size_t numBuckets;
    List** buckets;
    KeyValuePair* entries;
} HashTable;

int hashFunction(HashTable* table, unsigned key) {
    return key % ((int) table->numBuckets);
}

List** initBuckets(size_t numBuckets) {
    List** ret = malloc(sizeof(List*) * numBuckets);
    int i;
    for (i = 0; i < numBuckets; i++) {
        ret[i] = NULL;
    }
    return ret;
}

HashTable* makeHashTable(size_t numBuckets) {
    HashTable* ret = malloc(sizeof(HashTable));
    ret->numBuckets = numBuckets;
    ret->buckets = initBuckets(numBuckets);
    ret->entries = NULL;
    return ret;
}

KeyValuePair* findMatchingPair(List* bucket, unsigned key) {
    int i;
    for (i = 0; i < bucket->count; i++) {
        KeyValuePair* pair = (KeyValuePair*) bucket->contents[i];
        if (pair->key == key) {
            return pair;
        }
    }
    return NULL;
}

void* hashTableGet(HashTable* table, unsigned key) {

    // Get the hash
    int hash = hashFunction(table, key);

    // Fetch the appropriate bucket, or abort
    List* bucket = table->buckets[hash];
    if (bucket == NULL) {
        return NULL;
    }

    // Find the matching KVP, or abort
    KeyValuePair* matching = findMatchingPair(bucket, key);
    if (matching == NULL) {
        return NULL;
    }

    // Get the value of the matching pair
    return matching->value;
}

// Prepends a KVP to table's entries (key is assumed NOT to be in table)
void prependKvp(HashTable* table, KeyValuePair* pair) {

    KeyValuePair* oldHead = table->entries;

    // Old head is now after the new pair
    pair->next = oldHead;

    // New pair is now before the old head
    if (oldHead != NULL) {
        oldHead->prev = pair;
    }

    // Replace the head
    table->entries = pair;
}

// Deletes a KVP from table's entries (key is assumed to be in table)
void removeKvp(HashTable* table, KeyValuePair* pair) {

    // Special case: pair is the head entry -> shift head to next element
    if (pair == table->entries) {
        table->entries = table->entries->next;
    }

    // Disconnect KVP from it's neighbours and vice versa
    disconnectKvp(pair);
}

// Sets key to map to value in table.
// Returns the value overwritten if key is already in table
void* hashTableSet(HashTable* table, unsigned key, void* value) {
    size_t HASH_TABLE_BUCKET_SIZE = 5;

    // Fetch the appropriate bucket, or make it if it doesn't exist
    int hash = hashFunction(table, key);
    if (table->buckets[hash] == NULL) {
        table->buckets[hash] = makeList(HASH_TABLE_BUCKET_SIZE);
    }
    List* bucket = table->buckets[hash];

    // Find matching KVP, and if one exists modify it
    KeyValuePair* toModify = findMatchingPair(bucket, key);
    if (toModify != NULL) {
        void* ret = toModify->value;
        toModify->value = value;
        return ret;
    }

        // Otherwise insert into the bucket, and prepend to entries
    else {
        KeyValuePair* newPair = makeKeyValuePair(key, value);
        prependKvp(table, newPair);
        listAppend(bucket, newPair);
        return NULL;
    }

}

void* hashTableRemove(HashTable* table, unsigned key) {

    // Get the hash
    int hash = hashFunction(table, key);

    // Fetch the appropriate bucket, or abort
    List* bucket = table->buckets[hash];
    if (bucket == NULL) {
        return NULL;
    }

    // Find the matching KVP or abort
    KeyValuePair* matching = findMatchingPair(bucket, key);
    if (matching == NULL) {
        return NULL;
    }

    // Delete the KVP from the bucket, and if the bucket is empty delete it
    listDelete(bucket, matching);
    if (bucket->count == 0) {
        destroyList(bucket);
        table->buckets[hash] = NULL;
    }



    // Remove the KVP from table
    removeKvp(table, matching);

    // KVP is now disconnected, so we can return the result of its destruction
    return destroyDisconnectedKvp(matching);
}

//endregion

//region BELADY IMPLEMENTATION

HashTable* beladyTable;

// Creates a pointer that points to val
// This is used to insert into int Lists.
int* createIntPtr(int val){
    int* ret = malloc(sizeof(int));
    *ret = val;
    return ret;
}

// Destroys an int pointer. Returns the value that the pointer held.
// This is used to pop Lists of ints.
int destroyIntPtr(int* ptr){
    int ret = *ptr;
    free(ptr);
    return ret;
}

// Creates a pointer that points to val
// This is used to insert into unsigned Lists.
unsigned* createUnsignedPtr(unsigned val){
    unsigned * ret = malloc(sizeof(unsigned));
    *ret = val;
    return ret;
}

// Creates a pointer that points to val
// This is used to insert into unsigned Lists.
unsigned destroyUnsignedPtr(unsigned* ptr){
    unsigned ret = *ptr;
    free(ptr);
    return ret;
}

// Makes a list of all page numbers appearing in the given trace path
// Stops program with an error if file interactions result in an error
List* makePageList(char* trace_path) {

    // Open up the file or fail
    FILE* file_ptr = fopen(trace_path, "r");
    if (file_ptr == NULL) {
        perror("Error Opening Trace File");
        exit(1);
    }

    int INIT_LIST_SIZE = 100;
    List* ret = makeList(INIT_LIST_SIZE);

    // Load the page numbers into the list
    addr_t virtualAddress;
    while (fscanf(file_ptr, "%*c %lx\n", &virtualAddress) != EOF) {
        // Find the page number by right shifting (it should now fit into an unsigned value)
        unsigned pageNum = (unsigned) (virtualAddress >> PAGE_SHIFT);
        listAppend(ret, (void*) createUnsignedPtr(pageNum));
    }

    // Close file or fail
    if (fclose(file_ptr) != 0) {
        destroyList(ret);
        perror("Error Closing Trace File");
        exit(1);
    }

    return ret;
}

// Inserts a reference time for a given page number into table
void addBeladyPage(HashTable* table, unsigned pageNum, int time) {
    size_t LIST_SIZE = 4;

    // Make a list at the page number
    if (hashTableGet(table, pageNum) == NULL) {
        hashTableSet(table, pageNum, (void*) makeList(LIST_SIZE));
    }

    // Put the reference time into the list
    List* list = (List*) hashTableGet(table, pageNum);
    listAppend(list, (void*) createIntPtr(time));
}

// Turns a trace file into a hash table that maps
// (page numbers) to (list of DECREASING reference times)
HashTable* makeBeladyTable(char* trace_path) {

    // List of ALL page references
    List* pageList = makePageList(trace_path);

    // Instantiate a hash table proportional
    // in size to number of frames in memory
    int BUCKET_SIZE = 5;
    HashTable* ret = makeHashTable((size_t) memsize / BUCKET_SIZE);

    // Repeatedly pop off the end of the page list to
    // create lists of DECREASING reference times
    while(pageList->count > 0){
        unsigned* pageNumPtr = listPop(pageList);
        unsigned pageNum = destroyUnsignedPtr(pageNumPtr);
        addBeladyPage(ret, pageNum, (int) pageList->count);
    }

    // Destroy the page list since it is now empty
    destroyList(pageList);

    return ret;
}


// Pops the next time a page will be referenced from it's reference time list
// If no such page exists, returns -1
int popNextRefTime(unsigned pageNum) {

    // Get list of reference times
    List* refTimes = (List*) hashTableGet(beladyTable, pageNum);

    // Abort indicating we'll never see the page number again
    if (refTimes == NULL) {
        return -1;
    }

    // Pop the value off the list
    int* popped = (int*) listPop(refTimes);

    // Remove and destroy empty lists
    if (refTimes->count == 0){
        hashTableRemove(beladyTable, pageNum);
        destroyList(refTimes);
    }

    return destroyIntPtr(popped);
}

// Returns the next time a page will be referenced from it's reference time list
// If no such page exists, returns -1
int peekNextRefTime(unsigned pageNum) {

    // Get list of reference times
    List* refTimes = (List*) hashTableGet(beladyTable, pageNum);

    // Abort indicating we'll never see the page number again
    if (refTimes == NULL) {
        return -1;
    }

    // Look at the last value in the list,
    // it will be the smallest for this page number
    int* last = (int*) listPeek(refTimes);
    return *last;
}

// Determines the page number with the MINIMUM reference time in beladyTable
unsigned closestPageNumber() {

    // Note: peekNextRefTime never returns -1
    // because KVPs are guaranteed to be in the table
    KeyValuePair* pair = beladyTable->entries;

    unsigned soonestPage = pair->key;
    int soonestTime = peekNextRefTime(soonestPage);

    for (; pair != NULL; pair = pair->next) {
        unsigned curPage = pair->key;
        int curTime = peekNextRefTime(curPage);

        // Update values to determine soonest page number
        if (curTime < soonestTime) {
            soonestTime = curTime;
            soonestPage = curPage;
        }
    }

    return soonestPage;
}

// Return 1 iff pageNum is in physical memory
int inPhysicalMemory(unsigned pageNum){

    // Shift page number to determine virtual address
    addr_t vaddr = pageNum << PAGE_SHIFT;

    // Find PDE, abort if invalid
    unsigned dirIndex = PGDIR_INDEX(vaddr);
    pgdir_entry_t dirEntry = pgdir[dirIndex];
    if (!(dirEntry.pde & PG_VALID)){
        return 0;
    }

    // Find start of the page table, then the pointer to the entry
    unsigned tableIndex = PGTBL_INDEX(vaddr);
    pgtbl_entry_t* tableStart = (pgtbl_entry_t*) (dirEntry.pde & PAGE_MASK);
    pgtbl_entry_t* tableEntry = &(tableStart[tableIndex]);

    // Abort if the PTE is invalid
    if (!(tableEntry->frame & PG_VALID)){
        return 0;
    }

    return 1;
}

// Returns the PAGE number to evict from memory
unsigned victimPageNumber(){

    KeyValuePair* pair = beladyTable->entries;
    unsigned furthestPage = pair->key;
    int furthestTime = peekNextRefTime(furthestPage);

    // We're never going to see the first page again, so abort
    if (furthestTime == -1){
        return furthestPage;
    }

    for (;pair != NULL; pair = pair->next) {
        unsigned pageNum = pair->key;

        // Skip over pages not currently in physical memory
        if (!inPhysicalMemory(pageNum)){
            continue;
        }

        // Get the next time the page will be called
        int nextTime = peekNextRefTime(pageNum);

        // At this point, page is in memory but will never be seen again, so evict it
        if (nextTime == -1){
            return pageNum;
        }

        // Update the current champion
        if (nextTime > furthestTime){
            furthestPage = pageNum;
            furthestTime = nextTime;
        }
    }

    return furthestPage;
}

// Returns the FRAME number to evict from memory
unsigned victimFrameNumber(){

    // This is guaranteed to be in memory
    unsigned pageNum = victimPageNumber();

    // Shift page number to determine virtual address
    addr_t vaddr = pageNum << PAGE_SHIFT;

    // Find PDE
    unsigned dirIndex = PGDIR_INDEX(vaddr);
    pgdir_entry_t dirEntry = pgdir[dirIndex];

    // Find start of the page table, then the pointer to the entry
    unsigned tableIndex = PGTBL_INDEX(vaddr);
    pgtbl_entry_t* tableStart = (pgtbl_entry_t*) (dirEntry.pde & PAGE_MASK);
    pgtbl_entry_t* tableEntry = &(tableStart[tableIndex]);

    // Return the frame number of the entry
    return tableEntry->frame >> PAGE_SHIFT;
}

//endregion


/* Page to evict is chosen using the optimal (aka MIN) algorithm. 
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int opt_evict() {

    // Figure out which page to evict
    unsigned victim = victimFrameNumber();

    // Pop the next reference time from this page to keep the data structure in sync
    popNextRefTime(victim);

    return victim;
}

/* This function is called on each access to a page to update any information
 * needed by the opt algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void opt_ref(pgtbl_entry_t* p) {

    // Figure out which page is being referenced
    unsigned nextPage = closestPageNumber();

    // Pop the next reference time from this page to keep the data structure in sync
    popNextRefTime(nextPage);
}

/* Initializes any data structures needed for this
 * replacement algorithm.
 */
void opt_init() {
    beladyTable = makeBeladyTable(tracefile);
}

