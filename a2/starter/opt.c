#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include "pagetable.h"


extern int memsize;

extern int debug;

extern struct frame *coremap;

// frame -> hash(frame) -> hash_table[hash(frame)]

//region Linked List Implementation

typedef struct LinkedListNode {
	void* value;
	struct LinkedListNode* next;
} LinkedListNode;

LinkedListNode* makeLinkedListNode(void* value){
	LinkedListNode* ret = malloc(sizeof(LinkedListNode));
	ret->value = value;
	ret->next = NULL;
	return ret;
}

int isTerminal(LinkedListNode* node){
	return node->next == NULL;
}

typedef struct LinkedList {
	LinkedListNode* first;
	LinkedListNode* last;
} LinkedList;

LinkedList* makeLinkedList(void* value){
	LinkedListNode* node = makeLinkedListNode(value);
	LinkedList* ret = malloc(sizeof(LinkedList));
	ret->first = node;
	ret->last = node;
	return ret;
}

void append(LinkedList* lst, void* value){
	LinkedListNode* newLast = makeLinkedListNode(value);
	lst->last->next = newLast;
	lst->last = newLast;
}

void* popFirst(LinkedList* lst){
	void* ret = lst->first->value;

	LinkedListNode* oldFirst = lst->first;
	lst->first = oldFirst->next;

	free(oldFirst);

	return ret;
}

//endregion

//region Hash Table of Linked Lists of Integers

// Represents a key : LinkedList<value> pairing
typedef struct {
	int key;
	LinkedList* list;
} HashTableEntry;

HashTableEntry* makeHashTableEntry(int key){
	HashTableEntry* ret = malloc(sizeof(HashTableEntry));
	ret->key = key;
	ret->list = NULL;
	return ret;
}

void addValueToEntry(HashTableEntry* entry, int* value){
	if (entry->list == NULL){
		entry->list = makeLinkedList(value);
	} else {
		append(entry->list, value);
	}
}

// Represents a collection of HashEntries with the same hash key
typedef struct {
	LinkedList* list; // List of HashTableEntries
} HashTableBucket;

HashTableBucket* makeHashTableBucket(){
	HashTableBucket* ret = malloc(sizeof(HashTableBucket));
	ret->list = NULL;
	return ret;
}

void addEntryToBucket(HashTableBucket* bucket, HashTableEntry* entry){
	if (bucket->list == NULL){
		bucket->list = makeLinkedList(entry);
	} else {
		append(bucket->list, entry);
	}
}

HashTableEntry* findEntry(HashTableBucket* bucket, int key){
	if (bucket->list == NULL){
		return NULL;
	}

	LinkedListNode* cur;
	for(cur = bucket->list->first; cur != NULL; cur = cur->next){
		HashTableEntry* entry = (HashTableEntry*) cur->value;
		if (entry->key == key){
			return entry;
		}
	}

	return NULL;
}

void addValueToBucket(HashTableBucket* bucket, int key, int* value){

	// Find the entry in the bucket
	HashTableEntry* entry = findEntry(bucket, key);

	// Make a new entry for the bucket and insert value into it
	if (entry == NULL) {
		HashTableEntry* newEntry = makeHashTableEntry(key);
		addValueToEntry(newEntry, value);
		addEntryToBucket(bucket, newEntry);
		return;
	}

	// Add value into the entry
	addValueToEntry(entry, value);
}

typedef struct {
	int size;
	HashTableBucket** buckets;
} HashTable;

int hashFunction(HashTable* table, int key){
	return key % table->size;
}

HashTableBucket** initBuckets(int size){
	HashTableBucket** ret = malloc(size * sizeof(HashTableBucket*));
	int i;
	for (i = 0; i < size; i++){
		ret[i] = NULL;
	}
	return ret;
}

HashTable* makeHashTable(int size){
	HashTable* ret = malloc(sizeof(HashTable));
	ret->size = size;
	ret->buckets = initBuckets(size);
	return ret;
}

// Appends value to the LinkedList referred to by key
void appendValue(HashTable* table, int key, int* value){
	int hash = hashFunction(table, key);
	HashTableBucket* bucket = table->buckets[hash];
	if (bucket == NULL){
		table->buckets[hash] = makeHashTableBucket();
	}

	addValueToBucket(bucket, key, value);
}

// Gets the LinkedList referred to by key
LinkedList* get(HashTable* table, int key){
	int hash = hashFunction(table, key);

	// Try to get the bucket or abort
	HashTableBucket* bucket = table->buckets[hash];
	if (bucket == NULL){
		return NULL;
	}

	// Try to get the entry or abort
	HashTableEntry* entry = findEntry(bucket, key);
	if (entry == NULL){
		return NULL;
	}

	// Return the list of the entry
	return entry->list;
}

//endregion

int frame_number(addr_t vaddr){

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
void opt_ref(pgtbl_entry_t *p) {

	return;
}

/* Initializes any data structures needed for this
 * replacement algorithm.
 */
void opt_init() {
}

