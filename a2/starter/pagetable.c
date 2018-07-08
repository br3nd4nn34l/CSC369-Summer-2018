#include <assert.h>
#include <string.h>
#include "sim.h"
#include "pagetable.h"

// The top-level page table (also known as the 'page directory')
pgdir_entry_t pgdir[PTRS_PER_PGDIR];

// Counters for various events.
// Your code must increment these when the related events occur.
int hit_count = 0;
int miss_count = 0;
int ref_count = 0;
int evict_clean_count = 0;
int evict_dirty_count = 0;

/*
 * Allocates a frame to be used for the virtual page represented by p.
 * If all frames are in use, calls the replacement algorithm's evict_fcn to
 * select a victim frame.  Writes victim to swap if needed, and updates 
 * pagetable entry for victim to indicate that virtual page is no longer in
 * (simulated) physical memory.
 *
 * Counters for evictions should be updated appropriately in this function.
 */
int allocate_frame(pgtbl_entry_t* p) {
    int i;

    // Renaming frame -> frame_number because frame is a type
    int frame_number = -1;
    for (i = 0; i < memsize; i++) {
        if (!coremap[i].in_use) {
            frame_number = i;
            break;
        }
    }

    if (frame_number == -1) { // Didn't find a free page.
        // Call replacement algorithm's evict function to select victim
        frame_number = evict_fcn();

        // All frames were in use, so victim frame must hold some page
        // Write victim page to swap, if needed, and update pagetable
        // IMPLEMENTATION NEEDED

        // Grab the victim
        struct frame* victim = &coremap[frame_number];
        pgtbl_entry_t* victim_entry = victim->pte;

        // Dirty = 1 -> page is modified and must be written to disk
        if (coremap[frame_number].pte->frame & PG_DIRTY) {
            coremap[frame_number].pte->swap_off = swap_pageout(
                    (unsigned) frame_number,
                    (int) victim_entry->swap_off
            );
            evict_dirty_count++;
        } else {
            evict_clean_count++;
        }

        // Set bits to appropriate values
        victim_entry->frame &= ~PG_VALID; // VALID = 0 (evicted page cannot be valid)
        victim_entry->frame &= ~PG_REF; // REFERENCE = 0 (evicted cannot be in use)
        victim_entry->frame |= PG_ONSWAP; // ONSWAP = 1 (evicted is now on swap)
    }

    // Record information for virtual page that will now be stored in frame
    coremap[frame_number].in_use = 1;
    coremap[frame_number].pte = p;

    return frame_number;
}

/*
 * Initializes the top-level pagetable.
 * This function is called once at the start of the simulation.
 * For the simulation, there is a single "process" whose reference trace is 
 * being simulated, so there is just one top-level page table (page directory).
 * To keep things simple, we use a global array of 'page directory entries'.
 *
 * In a real OS, each process would have its own page directory, which would
 * need to be allocated and initialized as part of process creation.
 */
void init_pagetable() {
    int i;
    // Set all entries in top-level pagetable to 0, which ensures valid
    // bits are all 0 initially.
    for (i = 0; i < PTRS_PER_PGDIR; i++) {
        pgdir[i].pde = 0;
    }
}

// For simulation, we get second-level pagetables from ordinary memory
pgdir_entry_t init_second_level() {
    int i;
    pgdir_entry_t new_entry;
    pgtbl_entry_t* pgtbl;

    // Allocating aligned memory ensures the low bits in the pointer must
    // be zero, so we can use them to store our status bits, like PG_VALID
    if (posix_memalign((void**) &pgtbl, PAGE_SIZE,
                       PTRS_PER_PGTBL * sizeof(pgtbl_entry_t)) != 0) {
        perror("Failed to allocate aligned memory for page table");
        exit(1);
    }

    // Initialize all entries in second-level pagetable
    for (i = 0; i < PTRS_PER_PGTBL; i++) {
        pgtbl[i].frame = 0; // sets all bits, including valid, to zero
        pgtbl[i].swap_off = INVALID_SWAP;
    }

    // Mark the new page directory entry as valid
    new_entry.pde = (uintptr_t) pgtbl | PG_VALID;

    return new_entry;
}

/* 
 * Initializes the content of a (simulated) physical memory frame when it 
 * is first allocated for some virtual address.  Just like in a real OS,
 * we fill the frame with zero's to prevent leaking information across
 * pages. 
 * 
 * In our simulation, we also store the the virtual address itself in the 
 * page frame to help with error checking.
 *
 */
void init_frame(int frame, addr_t vaddr) {
    // Calculate pointer to start of frame in (simulated) physical memory
    char* mem_ptr = &physmem[frame * SIMPAGESIZE];
    // Calculate pointer to location in page where we keep the vaddr
    addr_t* vaddr_ptr = (addr_t*) (mem_ptr + sizeof(int));

    memset(mem_ptr, 0, SIMPAGESIZE); // zero-fill the frame
    *vaddr_ptr = vaddr;             // record the vaddr for error checking

    return;
}

/*
 * Locate the physical frame number for the given vaddr using the page table.
 *
 * If the entry is invalid and not on swap, then this is the first reference 
 * to the page and a (simulated) physical frame should be allocated and 
 * initialized (using init_frame).  
 *
 * If the entry is invalid and on swap, then a (simulated) physical frame
 * should be allocated and filled by reading the page data from swap.
 *
 * Counters for hit, miss and reference events should be incremented in
 * this function.
 */
char* find_physpage(addr_t vaddr, char type) {
    pgtbl_entry_t* table_entry_ptr = NULL; // pointer to the full page table entry for vaddr

    // Get the index for the directory entry
    unsigned dir_index = PGDIR_INDEX(vaddr);

    // Initialize second level if directory entry is invalid
    if (!(pgdir[dir_index].pde & PG_VALID)) {
        pgdir[dir_index] = init_second_level();
    }

    // Use top-level page directory to get pointer to 2nd-level page table
    pgdir_entry_t dir_entry = pgdir[dir_index]; // Grabbing the directory entry
    pgtbl_entry_t* table_start = (pgtbl_entry_t*) (dir_entry.pde & PAGE_MASK); // FRAME number of dir entry

    // Determine pointer to table entry
    unsigned table_index = PGTBL_INDEX(vaddr); // Index for the entry in the page table
    table_entry_ptr = &(table_start[table_index]); // Pointer to the page table entry

    // Check if table_entry_ptr is valid or not, on swap or not, and handle appropriately
    int is_valid = table_entry_ptr->frame & PG_VALID;
    int is_swapped = table_entry_ptr->frame & PG_ONSWAP;

    // Entry is in memory, which means we've hit it
    if (is_valid) {
        hit_count++;
    }

    // Entry is not in memory, handle according to swap status
    else {

        miss_count++;  // Not in memory -> counts as miss!

        // Allocate frame, retrieve frame and it's number
        int frame_number = allocate_frame(table_entry_ptr);
        unsigned frame = (unsigned) (frame_number << PAGE_SHIFT);

        if (is_swapped) {
            swap_pagein(frame_number, table_entry_ptr->swap_off); // Get page off swap
            frame &= ~PG_ONSWAP; // Page is now off the swap -> ONSWAP = 0
        } else {
            init_frame(frame_number, vaddr); // need to make the actual frame
            frame |= PG_DIRTY; // Page is in memory, still needs to be swapped -> DIRTY = 1
            table_entry_ptr->swap_off = INVALID_SWAP; // Page still needs a swap offset
        }

        // Put the frame into the entry
        table_entry_ptr->frame = frame;
    }

    // Make sure that frame of table_entry_ptr is marked valid and referenced
    table_entry_ptr->frame |= PG_VALID; // VALID = 1
    table_entry_ptr->frame |= PG_REF; // REF = 1

    // Mark frame of table_entry_ptr as dirty if the access type indicates that the page will be written to.
    if (type == 'M' || type == 'S') {
        // Store (S) or Modify (M) instructions imply the page is being written to
        table_entry_ptr->frame |= PG_DIRTY; // DIRTY = 1
    }

    // Call replacement algorithm's ref_fcn for this page
    ref_fcn(table_entry_ptr);

    // Increment ref count
    ref_count++;

    // Return pointer into (simulated) physical memory at start of frame
    return &physmem[(table_entry_ptr->frame >> PAGE_SHIFT) * SIMPAGESIZE];
}

void print_pagetbl(pgtbl_entry_t* pgtbl) {
    int i;
    int first_invalid, last_invalid;
    first_invalid = last_invalid = -1;

    for (i = 0; i < PTRS_PER_PGTBL; i++) {
        if (!(pgtbl[i].frame & PG_VALID) &&
            !(pgtbl[i].frame & PG_ONSWAP)) {
            if (first_invalid == -1) {
                first_invalid = i;
            }
            last_invalid = i;
        } else {
            if (first_invalid != -1) {
                printf("\t[%d] - [%d]: INVALID\n",
                       first_invalid, last_invalid);
                first_invalid = last_invalid = -1;
            }
            printf("\t[%d]: ", i);
            if (pgtbl[i].frame & PG_VALID) {
                printf("VALID, ");
                if (pgtbl[i].frame & PG_DIRTY) {
                    printf("DIRTY, ");
                }
                printf("in frame %d\n", pgtbl[i].frame >> PAGE_SHIFT);
            } else {
                assert(pgtbl[i].frame & PG_ONSWAP);
                printf("ONSWAP, at offset %lu\n", pgtbl[i].swap_off);
            }
        }
    }
    if (first_invalid != -1) {
        printf("\t[%d] - [%d]: INVALID\n", first_invalid, last_invalid);
        first_invalid = last_invalid = -1;
    }
}

void print_pagedirectory() {
    int i; // index into pgdir
    int first_invalid, last_invalid;
    first_invalid = last_invalid = -1;

    pgtbl_entry_t* pgtbl;

    for (i = 0; i < PTRS_PER_PGDIR; i++) {
        if (!(pgdir[i].pde & PG_VALID)) {
            if (first_invalid == -1) {
                first_invalid = i;
            }
            last_invalid = i;
        } else {
            if (first_invalid != -1) {
                printf("[%d]: INVALID\n  to\n[%d]: INVALID\n",
                       first_invalid, last_invalid);
                first_invalid = last_invalid = -1;
            }
            pgtbl = (pgtbl_entry_t*) (pgdir[i].pde & PAGE_MASK);
            printf("[%d]: %p\n", i, pgtbl);
            print_pagetbl(pgtbl);
        }
    }
}
