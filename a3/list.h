#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


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