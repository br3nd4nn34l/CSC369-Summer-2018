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

typedef struct {
    unsigned key;
    void* value;
} KeyValuePair;

KeyValuePair* makeKeyValuePair(unsigned key, void* value) {
    KeyValuePair* ret = malloc(sizeof(KeyValuePair));
    ret->key = key;
    ret->value = value;
    return ret;
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

        // Otherwise insert into the bucket
    else {
        KeyValuePair* newPair = makeKeyValuePair(key, value);
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

    void* ret = matching->value;
    free(matching);
    return ret;
}

//endregion

//region BELADY IMPLEMENTATION

int curRef;
List* pageList;
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
HashTable* makeBeladyTable() {

    // Instantiate a hash table proportional
    // in size to number of frames in memory
    int BUCKET_SIZE = 5;
    HashTable* ret = makeHashTable((size_t) memsize / BUCKET_SIZE);

    // Create lists of DECREASING reference times for every
    int i;
    for (i = 0; i < pageList->count; i++){
        int time = (int) pageList->count - i - 1;
        unsigned* pageNumPtr = (unsigned*) pageList->contents[time];
        addBeladyPage(ret, *pageNumPtr, time);
    }

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

//endregion


/* Page to evict is chosen using the optimal (aka MIN) algorithm. 
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int opt_evict() {

    int victim = 0;
    int maxRefTime = -1;

    // Find the page that won't be referenced in the longest time
    int i;
    for (i = 0; i < memsize; i++){
        struct frame f = coremap[i];

        int nextRefTime = peekNextRefTime(f.page);

        // We never see the page again, so get rid of the frame
        if (nextRefTime == -1){
            return i;
        }

        // Update to new victim
        if (maxRefTime < nextRefTime){
            victim = i;
            maxRefTime = nextRefTime;
        }
    }

    return victim;
}

/* This function is called on each access to a page to update any information
 * needed by the opt algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void opt_ref(pgtbl_entry_t* p) {

    // Figure out the frame of this page
    unsigned frameNumber = (p->frame >> PAGE_SHIFT);

    // Figure out which page is being referenced
    unsigned* pageNumberPtr = (unsigned*) pageList->contents[curRef];

    // Have the frame refer to this page number
    struct frame* f = &(coremap[frameNumber]);
    f->page = *pageNumberPtr;

    // Pop off the next occurrence for this page
    popNextRefTime(*pageNumberPtr);

    // Ensure that the frames and pages are kept in sync
    curRef += 1;
}

/* Initializes any data structures needed for this
 * replacement algorithm.
 */
void opt_init() {
    curRef = 0;
    pageList = makePageList(tracefile);
    beladyTable = makeBeladyTable();
}

