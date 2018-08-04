#include "ext2.h"
#include "list.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdbool.h>
#include <time.h>
#include <limits.h>
#include <fcntl.h>


List* split_path(char* path) {
    List* tokens = makeList(5);
    listAppend(tokens, ".");

    char* token;
    token = strtok(path, "/");
    while (token != NULL) {
        listAppend(tokens, token);
        token = strtok(NULL, "/");
    }

    return tokens;
}

typedef unsigned int zero_index;
typedef unsigned int one_index;

//region Constants

#define BLOCK_NUMS_PER_BLOCK (EXT2_BLOCK_SIZE / sizeof(one_index))
#define INDIRECT1_INDEX 12

const unsigned int INODE_BLOCK_LIMIT = INDIRECT1_INDEX + BLOCK_NUMS_PER_BLOCK;

const unsigned int LAST_RESERVED_BLOCK = 22;

const unsigned int LAST_RESERVED_INODE = EXT2_GOOD_OLD_FIRST_INO;

//endregion

//region Disk Fetching Functions

unsigned char* load_disk_to_mem(char* image) {

    int file_descriptor = open(image, O_RDWR);
    if (!file_descriptor) {
        fprintf(stderr, "Disk image '%s' not found.", image);
        exit(ENOENT);
    }

    unsigned char* disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, file_descriptor, 0);
    if (disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    return disk;
}

// We only need to deal with 1 block group, thus
// structure of disk is as follows (by block number):
// 0 = BOOT
// 1 = SUPER BLOCK
// 2 = GROUP DESCRIPTOR (gives block numbers for bitmaps and inode table)
unsigned char* get_block(unsigned char* disk, one_index block_ind) {
    return (disk + (EXT2_BLOCK_SIZE * block_ind));
}

struct ext2_super_block* get_super_block(unsigned char* disk) {
    return (struct ext2_super_block*) get_block(disk, 1);
}

struct ext2_group_desc* get_group_descriptor(unsigned char* disk) {
    return (struct ext2_group_desc*) get_block(disk, 2);
}

unsigned char* get_block_bitmap(unsigned char* disk) {
    struct ext2_group_desc* group_desc = get_group_descriptor(disk);
    return get_block(disk, group_desc->bg_block_bitmap);
}

unsigned char* get_inode_bitmap(unsigned char* disk) {
    struct ext2_group_desc* group_desc = get_group_descriptor(disk);
    return get_block(disk, group_desc->bg_inode_bitmap);
}

struct ext2_inode* get_inode_table(unsigned char* disk) {
    struct ext2_group_desc* group_desc = get_group_descriptor(disk);
    return (struct ext2_inode*) get_block(disk, group_desc->bg_inode_table);
}

struct ext2_inode* get_inode(unsigned char* disk, one_index inode_num) {
    return &get_inode_table(disk)[inode_num - 1];
}

struct ext2_inode* get_root_inode(unsigned char* disk) {
    // Handout specifies that root inode is inode 2
    return get_inode(disk, 2);
}

struct ext2_inode* get_inode_from_entry(unsigned char* disk, struct ext2_dir_entry_2* entry) {
    if (entry == NULL) {
        return NULL;
    }
    return get_inode(disk, entry->inode);
}

// Returns a pointer to the directory entry that is shift bytes after entry
// Not entirely safe, may result in invalid pointers
struct ext2_dir_entry_2* get_shifted_dir_entry(struct ext2_dir_entry_2* entry, unsigned int shift) {
    char* byte_wise = (char*) entry;
    char* shifted = byte_wise + shift;
    return (struct ext2_dir_entry_2*) shifted;
}

// Returns the directory entry that is rec_len bytes after entry
struct ext2_dir_entry_2* get_next_dir_entry(struct ext2_dir_entry_2* entry) {
    return get_shifted_dir_entry(entry, entry->rec_len);
}

// Treats the inode's data block pointers as a flat array for retrieval of block numbers
one_index get_inode_block_number(unsigned char* disk, struct ext2_inode* inode, zero_index index) {

    // Return direct block number
    if (index < INDIRECT1_INDEX) {
        return inode->i_block[index];
    }

        // Return indirect block number
    else if (INDIRECT1_INDEX <= index && index < INODE_BLOCK_LIMIT) {

        // Get the block number (abort if 0)
        one_index indir_block_num = inode->i_block[INDIRECT1_INDEX];
        if (indir_block_num == 0) {
            return 0;
        }

        // Get the block, then look at the index
        one_index* block = (one_index*) get_block(disk, indir_block_num);
        zero_index shifted_ind = index - INDIRECT1_INDEX;

        return block[shifted_ind];
    }

    // Out of bounds, return 0 to indicate no block number
    return 0;
}

// Treats the inode's data block pointers as a flat array for setting of block numbers
void set_inode_block_number(unsigned char* disk, struct ext2_inode* inode, zero_index index, one_index block_num) {

    // Set direct block number
    if (index < INDIRECT1_INDEX) {
        inode->i_block[index] = block_num;
    }

        // Set indirect block number
    else if (INDIRECT1_INDEX <= index && index < INODE_BLOCK_LIMIT) {

        // Get the block number (abort if 0)
        one_index indir_block_num = inode->i_block[INDIRECT1_INDEX];
        if (indir_block_num == 0) {
            return;
        }

        // Get the block, then set the index
        one_index* block = (one_index*) get_block(disk, indir_block_num);
        zero_index shifted_ind = index - INDIRECT1_INDEX;

        block[shifted_ind] = block_num;
    }
}

//endregion

//region Predicates

bool is_file(struct ext2_dir_entry_2* entry) {
    return (entry != NULL) &&
           entry->file_type == EXT2_FT_REG_FILE;
}

bool is_directory(struct ext2_dir_entry_2* entry) {
    return (entry != NULL) &&
           (entry->file_type == EXT2_FT_DIR);
}

bool is_link(struct ext2_dir_entry_2* entry) {
    return (entry != NULL) &&
           entry->file_type == EXT2_FT_SYMLINK;
}

bool is_entry_in_block(struct ext2_dir_entry_2* start, struct ext2_dir_entry_2* entry) {
    unsigned long distance = ((char*) entry) - ((char*) start);

    return (distance < EXT2_BLOCK_SIZE);
}

bool is_valid_dir_entry(struct ext2_dir_entry_2* start, struct ext2_dir_entry_2* entry) {
    return is_entry_in_block(start, entry) &&
           (entry->rec_len > 0);
}

bool entry_name_comparison(struct ext2_dir_entry_2* entry, char* name) {

    if (entry->name_len != strlen(name)) {
        return false;
    }

    for (int i = 0; i < entry->name_len; i++) {
        if (entry->name[i] != name[i]) {
            return false;
        }
    }

    return true;
}

// Return whether the given block of directory entries is empty or not
// (whether any names are still in use)
bool is_entry_block_empty(struct ext2_dir_entry_2* start) {

    // Search for any names in use
    for (struct ext2_dir_entry_2* cur = start; is_valid_dir_entry(start, cur); cur = get_next_dir_entry(cur)) {
        if (cur->name_len > 0) {
            return false;
        }
    }

    return true;
}

//endregion

//region Path Traversal

// For maintaining a pair of previous and current
typedef struct {
    struct ext2_dir_entry_2* prev;
    struct ext2_dir_entry_2* cur;
} ext2_dir_entry_pair;

// Makes a pair of directory entries
ext2_dir_entry_pair* make_entry_pair(struct ext2_dir_entry_2* prev, struct ext2_dir_entry_2* cur) {
    ext2_dir_entry_pair* ret = malloc(sizeof(ext2_dir_entry_pair));
    ret->prev = prev;
    ret->cur = cur;
    return ret;
}

// Finds the pair of directory entries in the block beginning at start
// where the current entry's name is name
// Returns NULL if no such entry exists
ext2_dir_entry_pair* block_matching_pair(struct ext2_dir_entry_2* start, char* name) {

    ext2_dir_entry_pair* ret = make_entry_pair(NULL, NULL);

    for (struct ext2_dir_entry_2* cur = start; is_valid_dir_entry(start, cur); cur = get_next_dir_entry(cur)) {

        if (entry_name_comparison(cur, name)) {
            ret->cur = cur;
            return ret;
        }

        ret->prev = cur;
    }

    // No pair found, free the pair and return null
    free(ret);
    return NULL;
}

// Finds the pair of directory entries in inode
// where the current entry's name is name
// Returns NULL if no such entry exists
ext2_dir_entry_pair* find_matching_pair(unsigned char* disk, struct ext2_inode* inode, char* name) {
    if (name == NULL) return NULL;

    // Look for block with matching directory
    for (zero_index i = 0; i < INODE_BLOCK_LIMIT; i++) {

        // Get block number, skip if absent
        one_index block_num = get_inode_block_number(disk, inode, i);
        if (block_num == 0) {
            continue;
        }

        // Get the first directory entry, and look for a matching pair
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        ext2_dir_entry_pair* match = block_matching_pair(first_entry, name);
        if (match != NULL) {
            return match;
        }
    }

    return NULL;
}

// Finds the directory entry in inode whose name is name
// Returns NULL if no such entry exists
struct ext2_dir_entry_2* find_matching_entry(unsigned char* disk, struct ext2_inode* inode, char* name) {

    // Try to get the pair that matches name, or abort with NULL
    ext2_dir_entry_pair* pair = find_matching_pair(disk, inode, name);
    if (pair == NULL) {
        return NULL;
    }

    struct ext2_dir_entry_2* ret = pair->cur;
    free(pair);
    return ret;
}

// Traverses the given list of path components to find the final directory entry
// Returns NULL if no such entry exists
struct ext2_dir_entry_2* traverse_path(unsigned char* disk, List* path_components) {

    struct ext2_dir_entry_2* entry = NULL;
    struct ext2_inode* entry_inode = get_root_inode(disk);

    for (zero_index ind = 0; ind < path_components->count; ind++) {
        // Try to find the matching entry, abort if not found
        entry = find_matching_entry(disk, entry_inode, path_components->contents[ind]);
        if (entry == NULL) {
            break;
        }

        // If entry is not a directory, abort - we can't traverse into it
        if (!is_directory(entry)) {

            // Only return entry if we went through all components
            return (ind == path_components->count - 1) ?
                   entry : NULL;
        }

        // Update inode
        entry_inode = get_inode_from_entry(disk, entry);
    }

    // Return entry, it should be a directory
    return entry;
}

//endregion

//region Allocation and Freeing

bool get_bitmap_val(unsigned char* bitmap, zero_index bit) {

    int index = bit / CHAR_BIT;
    int shift = bit % CHAR_BIT;

    int result = (bitmap[index] >> shift) & 0x1;
    return result != 0;
}

void set_bitmap_val(unsigned char* bitmap, zero_index bit, bool value) {

    int index = bit / CHAR_BIT;
    int shift = bit % CHAR_BIT;
    int one_hot = (1 << shift);

    // Set to 1
    if (value) {
        bitmap[index] |= (unsigned char) one_hot;
    }

        // Set to 0
    else {
        bitmap[index] &= (unsigned char) ~one_hot;
    }
}

bool get_block_usage(unsigned char* disk, one_index block_num) {
    unsigned char* block_bitmap = get_block_bitmap(disk);
    return get_bitmap_val(block_bitmap, block_num - 1);
}

bool get_inode_usage(unsigned char* disk, one_index inode_num) {
    unsigned char* inode_bitmap = get_inode_bitmap(disk);
    return get_bitmap_val(inode_bitmap, inode_num - 1);
}

void set_block_usage(unsigned char* disk, one_index block_num, bool is_used) {
    unsigned char* block_bitmap = get_block_bitmap(disk);
    set_bitmap_val(block_bitmap, block_num - 1, is_used);
}

void set_inode_usage(unsigned char* disk, one_index inode_num, bool is_used) {
    unsigned char* inode_bitmap = get_inode_bitmap(disk);
    set_bitmap_val(inode_bitmap, inode_num - 1, is_used);
}

void increase_free_blocks_count(unsigned char* disk, int count) {
    get_super_block(disk)->s_free_blocks_count += count;
    get_group_descriptor(disk)->bg_free_blocks_count += count;
}

void increase_free_inodes_count(unsigned char* disk, int count) {
    get_super_block(disk)->s_free_inodes_count += count;
    get_group_descriptor(disk)->bg_free_inodes_count += count;
}

// Attempts to allocate num_blocks on disk
// Returns an array of the allocated block numbers if successful
// Exits if not enough free data blocks exist
one_index* allocate_blocks(unsigned char* disk, unsigned int num_blocks) {

    struct ext2_super_block* super_block = get_super_block(disk);

    // CRITICAL ERROR: NOT ENOUGH FREE BLOCKS
    if (num_blocks > super_block->s_free_blocks_count) {
        fprintf(stderr, "Not enough free data blocks.\n");
        exit(1);
    }

    // Decrement number of free blocks, grab all free block numbers
    increase_free_blocks_count(disk, -num_blocks);

    // Gather up all free block numbers
    one_index* ret = malloc(sizeof(one_index) * num_blocks);
    zero_index cur_ind = 0;
    for (one_index block_num = LAST_RESERVED_BLOCK + 1; cur_ind < num_blocks; block_num++) {
        if (!get_block_usage(disk, block_num)) {

            // Insert the block into the array, mark usage in bitmap
            ret[cur_ind] = block_num;
            set_block_usage(disk, block_num, 1);

            memset(get_block(disk, block_num), 0, EXT2_BLOCK_SIZE);

            // Move to the next index for the next insertion
            cur_ind++;
        }
    }

    return ret;
}

// Allocates one block on disk
// Returns the block number of the allocated block
// Exits if no free data blocks exist
one_index allocate_one_block(unsigned char* disk) {
    one_index* arr = allocate_blocks(disk, 1);
    one_index ret = arr[0];
    free(arr);
    return ret;
}

// Allocates an inode
// Returns the inode number of the allocated inode
// Exits if no free inodes exist
one_index allocate_inode(unsigned char* disk) {

    struct ext2_super_block* super_block = get_super_block(disk);

    // CRITICAL ERROR: NO FREE INODES
    if (super_block->s_free_inodes_count == 0) {
        fprintf(stderr, "No free inodes.\n");
        exit(1);
    }

    // Decrement number of free inodes
    increase_free_inodes_count(disk, -1);

    // Look for a free inode number in the bitmap
    for (one_index inode_num = LAST_RESERVED_INODE + 1; inode_num <= super_block->s_inodes_count; inode_num++) {

        // Mark the inode as used, return the number
        if (!get_inode_usage(disk, inode_num)) {
            set_inode_usage(disk, inode_num, 1);

            // Grab the inode and zero out all the data block numbers
            struct ext2_inode* inode = get_inode(disk, inode_num);
            for (zero_index i = 0; i < INODE_BLOCK_LIMIT; i++){
                set_inode_block_number(disk, inode, i, 0);
            }

            return inode_num;
        }
    }

    return 0;
}

// Frees the block with block_number on disk
void free_block(unsigned char* disk, one_index block_num) {
    increase_free_blocks_count(disk, 1);
    set_block_usage(disk, block_num, 0);
}

// Frees an inode and zeroes out its associated data blocks on disk
void free_inode(unsigned char* disk, one_index inode_num) {
    struct ext2_inode* inode = get_inode(disk, inode_num);

    inode->i_dtime = (unsigned int) time(0);
    increase_free_inodes_count(disk, 1);
    set_inode_usage(disk, inode_num, 0);

    // Free all block numbers in the inode (skip absent block numbers)
    for (int i = 0; i < INODE_BLOCK_LIMIT; i++) {
        one_index block_num = get_inode_block_number(disk, inode, i);
        if (block_num == 0) continue;
        free_block(disk, block_num);
        set_inode_block_number(disk, inode, i, 0);
    }
}

// Deletes the directory entry of parent_entry with name equal to name
void delete_child_entry(unsigned char* disk, struct ext2_dir_entry_2* parent_entry, char* name) {

    struct ext2_inode* parent_inode = get_inode(disk, parent_entry->inode);

    for (zero_index i = 0; i < INODE_BLOCK_LIMIT; i++) {

        // Get block number and skip if absent
        one_index block_num = get_inode_block_number(disk, parent_inode, i);
        if (block_num == 0) {
            continue;
        }

        // Get the first directory entry, and look for a pair of entries where cur matches name
        struct ext2_dir_entry_2* start = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        ext2_dir_entry_pair* pair = block_matching_pair(start, name);
        if (pair == NULL) {
            continue;
        }

            // There is a matching pair, so manipulate the entries then return
        else {

            // Matching entry was the first in the block, skip over
            if (pair->prev == NULL) {
                pair->cur->name_len = 0;
            }

                // Matching entry within block
            else {
                pair->prev->rec_len += pair->cur->rec_len;
            }

            // Check to see if the block is completely empty
            // Remove it from the inode and mark it as free
            if (is_entry_block_empty(start)) {
                set_inode_block_number(disk, parent_inode, i, 0);
                free_block(disk, block_num);
            }

            free(pair);
            return;
        }
    }
}

// Calculates the number of bytes needed for a directory entry
unsigned short total_entry_length(unsigned char name_len) {

    size_t raw_length = sizeof(unsigned int) +
                        sizeof(unsigned short) +
                        2 * sizeof(unsigned char) +
                        name_len * sizeof(char);

    size_t padding = 4 - (raw_length % 4);

    return (unsigned short) (raw_length + padding);
}

// Calculates the minimum rec_len for a directory entry
unsigned short min_rec_len(struct ext2_dir_entry_2* entry) {
    return total_entry_length(entry->name_len);
}

// Attempts to make a directory entry in the same block as start
// Returns NULL if unsuccessful
struct ext2_dir_entry_2* make_entry_in_existing_block(struct ext2_dir_entry_2* start,
                                                      one_index inode_number,
                                                      char* name,
                                                      unsigned char file_type) {

    // ASSUME:
    // NAME IS NON-NULL,
    // NAME IS NON-EMPTY
    // ENTRY DOES NOT EXIST ALREADY

    // Try to find an entry with enough succeeding empty space to fit the new entry
    unsigned char name_len = (unsigned char) strlen(name);
    unsigned short space_needed = total_entry_length(name_len);

    for (struct ext2_dir_entry_2* cur = start; is_valid_dir_entry(start, cur); cur = get_next_dir_entry(cur)) {

        unsigned short cur_min_rec_len = min_rec_len(cur);
        unsigned short space_left = (cur->rec_len - cur_min_rec_len);

        if (space_left >= space_needed) {

            // Look at the entry right after cur's name, abort if it's out of bounds
            struct ext2_dir_entry_2* ret = get_shifted_dir_entry(cur, cur_min_rec_len);
            if (!is_entry_in_block(start, ret)) {
                return NULL;
            }

            // Cut cur off at the end of it's name
            unsigned short old_rec_len = cur->rec_len;
            cur->rec_len = cur_min_rec_len;

            // Fill in the fields for the new entry
            strncpy(ret->name, name, name_len);
            ret->name_len = name_len;
            ret->rec_len = old_rec_len - cur->rec_len;
            ret->file_type = file_type;
            ret->inode = inode_number;

            return ret;
        }
    }

    // Couldn't find a space to put the new entry into
    return NULL;
}


// Attempts to attach a newly allocated data block to inode at index
// Returns the block number of the allocated block,
// or 0 if the index was already occupied
one_index allocate_block_on_inode(unsigned char* disk, struct ext2_inode* inode,
                                  zero_index block_ind) {

    // Abort if index is already occupied
    if (get_inode_block_number(disk, inode, block_ind) != 0) {
        return 0;
    }

    // Otherwise set the index to an allocated block, increment number of blocks
    one_index block_num = allocate_one_block(disk);
    set_inode_block_number(disk, inode, block_ind, block_num);
    inode->i_blocks += 2;

    return block_num;
}


// Attaches directory entry with name to inode. Returns the resulting directory.
// Attempts to find a place in the existing data blocks of inode to put the entry
// If no such place is found, allocates blocks to fit the new entry
// Will exit if no blocks are available
// Returns NULL if all inode is completely occupied
struct ext2_dir_entry_2* make_entry_in_inode(unsigned char* disk,
                                             struct ext2_inode* inode,
                                             one_index child_inode_num,
                                             char* name,
                                             unsigned char file_type) {

    // See if we can squeeze the entry into the existing blocks of the inode
    for (zero_index i = 0; i < INODE_BLOCK_LIMIT; i++) {

        // Get the block number, skip if it doesn't exist
        one_index block_num = get_inode_block_number(disk, inode, i);
        if (block_num == 0) {
            continue;
        }

        // Try to make an entry within the given block, return it if possible
        struct ext2_dir_entry_2* start = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        struct ext2_dir_entry_2* attempt = make_entry_in_existing_block(start, child_inode_num, name, file_type);
        if (attempt != NULL) {
            return attempt;
        }
    }

    // Otherwise look for the first free block number that we can use
    for (zero_index i = 0; i < INODE_BLOCK_LIMIT; i++) {

        // Attempt to allocate a block number on inode at the given index
        // Skip if allocation is unsuccessful
        one_index block_num = allocate_block_on_inode(disk, inode, i);
        if (block_num == 0) {
            continue;
        }

        // Get the first entry of the new block
        struct ext2_dir_entry_2* start = (struct ext2_dir_entry_2*) get_block(disk, block_num);

        // Fill in various information to complete   the entry
        size_t name_len = strlen(name);
        start->inode = child_inode_num;
        strncpy(start->name, name, name_len);
        start->name_len = (unsigned char) name_len;
        start->rec_len = EXT2_BLOCK_SIZE;
        start->file_type = file_type;

        return start;
    }

    return NULL;
}

one_index allocate_dir_inode(unsigned char* disk) {

    // Allocate the inode and grab it
    one_index dir_inode_num = allocate_inode(disk);
    struct ext2_inode* dir_inode = get_inode(disk, dir_inode_num);

    dir_inode->i_mode = EXT2_S_IFDIR;
    dir_inode->i_size = EXT2_BLOCK_SIZE;
    dir_inode->i_ctime = (unsigned int) time(0);
    dir_inode->i_dtime = 0;
    dir_inode->i_blocks = 0;
    dir_inode->i_links_count = 1;

    // Increment number of used directories
    get_group_descriptor(disk)->bg_used_dirs_count++;

    return dir_inode_num;
}

// Allocates an inode for a symlink and returns the number of the inode
one_index allocate_link_inode(unsigned char* disk, char* source) {

    // Allocate the inode and grab it
    one_index inode_num = allocate_inode(disk);
    struct ext2_inode* sym_inode = get_inode(disk, inode_num);

    sym_inode->i_mode = EXT2_S_IFLNK;
    sym_inode->i_size = (unsigned int) strlen(source);
    sym_inode->i_ctime = (unsigned int) time(0);
    sym_inode->i_dtime = 0;
    sym_inode->i_blocks = 0;
    sym_inode->i_links_count = 1;

    return inode_num;
}

void write_path_to_symlink_inode(unsigned char* disk, struct ext2_inode* inode, char* content) {

    // Assume the content (link name) can always fit into one block
    if (strlen(content) < EXT2_BLOCK_SIZE) {

        // Allocate the first block on the inode, grab it and dump content into it
        one_index block_num = allocate_block_on_inode(disk, inode, 0);
        unsigned char* data_block = get_block(disk, block_num);
        strcpy((char*) data_block, content);

    } else {
        // throw error if the file path is too long
        exit(ENAMETOOLONG);
    }

}

// Count the number of non-zero data blocks in an inode
unsigned int count_non_zero_blocks(unsigned char* disk, struct ext2_inode* inode){
    unsigned int ret = 0;

    for (zero_index i = 0; i < INODE_BLOCK_LIMIT; i++){
        if(get_inode_block_number(disk, inode, i) != 0){
            ret++;
        }
    }

    return ret;
}

// Allocates an inode that is an exact duplicate of the given inode
one_index allocate_duplicate_inode(unsigned char* disk, struct ext2_inode* source){

    // Allocate a new inode
    one_index dst_inode_num = allocate_inode(disk);

    // Allocate as many non-zero blocks as the source has
    unsigned int num_non_zero = count_non_zero_blocks(disk, source);
    one_index* new_block_nums = allocate_blocks(disk, num_non_zero);

    // Go through the new inode and set it up
    struct ext2_inode* dest = get_inode(disk, dst_inode_num);

    // Copy the layout of the blocks in source
    zero_index cur_ind = 0;
    for (zero_index i = 0; cur_ind < num_non_zero && i < INODE_BLOCK_LIMIT; i++) {
        if (get_inode_block_number(disk, source, i) != 0) {
            set_inode_block_number(disk, dest, i, new_block_nums[cur_ind]);
            cur_ind++;
        }
    }

    // No longer need the collection of new block numbers
    free(new_block_nums);

    // Transfer the contents of each source block to each destination block
    for (zero_index i = 0; i < INODE_BLOCK_LIMIT; i++){

        // Skip zeros (guaranteed to be same across both)
        one_index src_num = get_inode_block_number(disk, source, i);
        if (src_num == 0) continue;
        one_index dst_num = get_inode_block_number(disk, source, i);

        // Grab the blocks
        unsigned char* src_block = get_block(disk, src_num);
        unsigned char* dst_block = get_block(disk, dst_num);

        // Copy everything into the destination block
        strncpy((char*) dst_block, (char*) src_block, EXT2_BLOCK_SIZE);
    }

    // TODO SET UP THE ATTRIBUTES OF THE CLONED INODE

    // Return the inode number of the cloned inode
    return dst_inode_num;
}



//endregion

//region Printing

void print_dir_name(struct ext2_dir_entry_2* dir) {

    // Print each character up to name_len
    for (int u = 0; u < dir->name_len; u++) {
        printf("%c", dir->name[u]);
    }

    // Finish with newline
    printf("\n");
}

void print_block_contents(struct ext2_dir_entry_2* start, bool show_dots) {
    for (struct ext2_dir_entry_2* cur = start; is_valid_dir_entry(start, cur); cur = get_next_dir_entry(cur)) {

        // Skip over dots if asked
        if (!show_dots) {
            if ((strcmp(cur->name, ".") == 0) || (strcmp(cur->name, "..") == 0)) {
                continue;
            }
        }

        // TODO THIS ASSUMES CUR IS PROPER
        if (cur->name_len != 0) {
            print_dir_name(cur);
        }
    }
}

void print_dir_contents(unsigned char* disk, struct ext2_dir_entry_2* entry, bool show_dots) {

    struct ext2_inode* inode = get_inode_from_entry(disk, entry);

    // Print the contents of each block
    for (int i = 0; i < INODE_BLOCK_LIMIT; i++) {

        // Grab block number, skip if absent
        one_index block_num = get_inode_block_number(disk, inode, i);
        if (block_num == 0) {
            continue;
        }

        // Get the first directory entry, and print the contents
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        print_block_contents(first_entry, show_dots);
    }
}

//endregion

//region System Utils

void crash_with_usage(char* err_msg) {
    fprintf(stderr, "%s\n", err_msg);
    exit(1);
}

//endregion