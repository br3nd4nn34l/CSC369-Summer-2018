#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame* coremap;

//region Linked List Implementation

typedef struct LinkedListNode {
    void* value;
    struct LinkedListNode* next;
} LinkedListNode;

LinkedListNode* makeLinkedListNode(void* value) {
    LinkedListNode* ret = malloc(sizeof(LinkedListNode));
    ret->value = value;
    ret->next = NULL;
    return ret;
}

int isTerminal(LinkedListNode* node) {
    return node->next == NULL;
}

typedef struct LinkedList {
    LinkedListNode* first;
    LinkedListNode* last;
} LinkedList;

LinkedList* makeLinkedList(void* value) {
    LinkedListNode* node = makeLinkedListNode(value);
    LinkedList* ret = malloc(sizeof(LinkedList));
    ret->first = node;
    ret->last = node;
    return ret;
}

void append(LinkedList* lst, void* value) {
    LinkedListNode* newLast = makeLinkedListNode(value);
    lst->last->next = newLast;
    lst->last = newLast;
}

// TODO NEED TO CONSIDER WHAT HAPPENS WHEN A ONE-ELEMENT LIST IS POPPED
void* popFirst(LinkedList* lst) {
    void* ret = lst->first->value;

    LinkedListNode* oldFirst = lst->first;

    lst->first = oldFirst->next;

    free(oldFirst);

    return ret;
}

void* popLast(LinkedList* lst) {

    // Hold onto the last node
    LinkedListNode* oldLast = lst->last;

    // Find the second last node
    LinkedListNode* prior;
    for (prior = lst->first; prior->next != oldLast; prior = prior->next) { ; }

    // Replace the last node
    lst->last = prior;

    // Grab the value from the old last before freeing it
    void* oldValue = oldLast->value;
    free(oldLast);

    return oldValue;
}

void* linkedListDelete(LinkedList* lst, LinkedListNode* node) {

    // Node is on front, must use method to update list attributes
    if (node == lst->first) {
        return popFirst(lst);
    }

    // Node is on back, must use method to update list attributes
    if (node == lst->last) {
        return popLast(lst);
    }


    // Node if probably in the list, must go through and find it
    LinkedListNode* prev = lst->first;
    for (cur; cur != NULL; cur = cur->next) {
        if (cur )

        LinkedListNode* prev = cur;
    }
}

//endregion

//region Hash Table Implementation (int->void*) (does not support overwriting values)

// Represents a (key, value) pair
typedef struct {
    int key;
    void* value;
} HashTableEntry;

HashTableEntry* makeHashTableEntry(int key, void* value) {
    HashTableEntry* ret = malloc(sizeof(HashTableEntry));
    ret->key = key;
    ret->value = value;
    return ret;
}

// Maps Integers against values
typedef struct {
    int num_buckets;
    LinkedList** buckets;
} HashTable;

LinkedList** initBuckets(int num_buckets) {
    LinkedList** ret = malloc(num_buckets * sizeof(*LinkedList));
    int i;
    for (i = 0; i < num_buckets; i++) {
        ret[i] = NULL;
    }
    return ret;
}

HashTable* makeHashTable(int num_buckets) {
    HashTable* ret = malloc(sizeof(HashTable));
    ret->num_buckets = num_buckets;
    ret->buckets = initBuckets(num_buckets);
    return ret;
}

int hashFunction(HashTable* table, int key) {
    return key % table->num_buckets;
}

LinkedList* getBucket(HashTable* table, int key) {
    int hash = hashFunction(table, key);
    return table->buckets[hash];
}

LinkedListNode* getEntryNode(LinkedList* bucket, int key) {
    if (bucket == NULL) {
        return NULL;
    }

    LinkedListNode* node = bucket->first;
    for (node; node != NULL; node = node->next) {
        HashTableEntry* entry = (HashTableEntry*) node->value;
        if (entry->key == key) {
            return node;
        }
    }

    return NULL;
}

HashTableEntry* getEntry(LinkedList* bucket, int key) {

    LinkedListNode* node = getEntryNode(bucket, key);
    if (node == NULL) {
        return NULL;
    }

    return (HashTableEntry*) node->value;
}

// Return pointer to value that was overwritten, or NULL if nothing was overwritten
void* addEntryToBucket(LinkedList* bucket, int key, void* value) {

    // The entry with the same key
    HashTableEntry* duplicate = getEntry(bucket, key);

    // Replace the value of the duplicate and return previous value
    if (duplicate != NULL) {
        oldValue = duplicate->value;
        duplicate->value = value;
        return oldValue;
    }

    // We don't overwrite anything, so append
    append(bucket, makeHashTableEntry(key, value));
    return NULL;
}

// Return pointer to value that was deleted, or NULL if nothing was deleted
void* removeEntryFromBucket(LinkedList* bucket, int key) {

    // Find the linked list node to delete
    LinkedListNode* target = getEntryNode(bucket, key);
    if (target == NULL) {
        return NULL;
    }

    // Special case : removing from front of the list (must update list attributes)
    if (target == bucket->first) {
        HashTableEntry* entry = (HashTableEntry*) popFirst(bucket);

        // Grab the entry's value before freeing it
        void* ret = entry->value;
        free(entry);

        // Return entry's value
        return ret;
    }

    // Special case : removing from the back of the list (must update list attributes)
    if (target == bucket->last) {

        // Go to the very back of the list
        LinkedListNode* node = bucket->first;
        for (node; node != NULL; node = node->next) {
            LinkedListNode* prev = node;
        }

        HashTableEntry* entry = (HashTableEntry*)
    }

}

// Return 1 iff key is in table
int has(HashTable* table, int key) {

    // Get the bucket or abort with 0
    LinkedList* bucket = getBucket(table, key);
    if (bucket == NULL) {
        return 0;
    }

    // Find matching entry or abort with 0
    HashTableEntry* entry = getEntry(bucket, key);
    if (entry == NULL) {
        return 0;
    }

    // We found a matching entry
    return 1;
}


// Gets the value referred to by key or NULL
void* get(HashTable* table, int key) {
    int hash = hashFunction(table, key);

    // Try to get the bucket or abort
    LinkedList* bucket = table->buckets[hash];
    if (bucket == NULL) {
        return NULL;
    }

    // Try to find matching entry
    LinkedListNode* node = bucket->first;
    for (node; node != NULL; node = node->next) {
        HashTableEntry* entry = (HashTableEntry*) node->value;
        if (entry->key == key) {
            return entry->value;
        }
    }

    // COULD NOT
    return NULL;
}

// Returns the HashTableEntry removed
HashTableEntry* remove(HashTable* table, int key) {
    if (!has)


        int hash = hashFunction(table, key);

}

// Maps key to value - returns the HashTableEntry that was overwritten or NULL
HashTableEntry* set(HashTable* table, int key, void* value) {

    // Determine hash and entry
    int hash = hashFunction(table, key);
    HashTableEntry* entry = makeHashTableEntry(key, value);

    // Add to the bucket, or make a new one
    LinkedList* bucket = table->buckets[hash];
    if (bucket != NULL) {
        return addEntryToBucket(bucket, entry);
    } else {
        table->buckets[hash] = makeLinkedList((void*) entry);
        return NULL;
    }
}

//endregion

//region Implementing Belady's Algorithm

// Makes an empty hash table for Belady's Algorithm
HashTable* emptyBeladyTable(int size) {
    int LIST_SIZE = 5;
    HashTable* ret = makeHashTable(size / LIST_SIZE);
    return ret;
}

// Enters a reference into a Belady Hash Table
void addBeladyReference(HashTable* beladyTable, int time, int reference) {

    // Wrap the given time in a pointer
    int* time_ptr = malloc(sizeof(int));
    *time_ptr = time;

    // Attempt to get the linked list for the reference
    LinkedList* lst = (LinkedList*) get(beladyTable, reference);

    // If the list doesn't exist, make a new one starting at time_ptr
    if (lst == NULL) {
        LinkedList* newLst = makeLinkedList((void*) time_ptr);
        set(beladyTable, reference, (void*) newLst);
    }

        // If it does exist, append time_ptr to it
    else {
        append(lst, time_ptr);
    }
}

// Fills a hash table with entries of form (reference : LinkedList<ref_time>)
// Note that the LinkedList elements will be INCREASING
HashTable* makeBeladyTable(int* references, int size) {
    HashTable* beladyTable = makeBeladyTable(size);

    // Add increasing times to each respective linked list
    int time;
    for (time = 0; time < size; time++) {
        int ref = sequence[time];
        addBeladyReference(beladyTable, time, ref);
    }


    return beladyTable;
}


// Finds the reference that will not be called
// for the longest time in a Belady table
int longestUncalledReference(HashTable* beladyTable, int* references, int size) {
    int winner = references[0];
    for (int i = 0; i < size; i++) {
        LinkedList* lst = get(beladyTable, references[i]);

        // The reference is never called,
        if (lst == NULL) {
            return references[i];
        }


        int* headPtr = (int*) lst->first->value;

        if (lst == NULL || *headPtr >) {

        }
    }
}

//endregion

int frame_number(addr_t vaddr) {

}

/* Page to evict is chosen using the optimal (aka MIN) algorithm. 
 * Returns the page frame number (which is also the index in the coremap)
 * for the page that is to be evicted.
 */
int opt_evict() {

    return 0;
}

/* This function is called on each access to a page to update any information
 * needed by the opt algorithm.
 * Input: The page table entry for the page that is being accessed.
 */
void opt_ref(pgtbl_entry_t* p) {

    return;
}

/* Initializes any data structures needed for this
 * replacement algorithm.
 */
void opt_init() {
}

