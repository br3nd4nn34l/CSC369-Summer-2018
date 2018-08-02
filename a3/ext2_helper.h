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

//endregion

//region Disk Fetching Functions

unsigned char* load_disk_to_mem(int file_descriptor) {
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

struct ext2_dir_entry_2* make_directory_entry(unsigned char* disk, one_index block_ind, int used_len) {
    return (struct ext2_dir_entry_2*) (disk + (EXT2_BLOCK_SIZE * block_ind) + used_len);
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

struct ext2_inode* get_inode(unsigned char* disk, zero_index inode_ind) {
    return &get_inode_table(disk)[inode_ind];
}

struct ext2_inode* get_root_inode(unsigned char* disk) {
    // Handout specifies that root inode is at index 1
    return get_inode(disk, 1);
}

struct ext2_inode* get_inode_from_entry(unsigned char* disk, struct ext2_dir_entry_2* entry) {
    if (entry == NULL) {
        return NULL;
    }
    return get_inode(disk, entry->inode - 1);
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

one_index get_inode_block_number(unsigned char* disk, struct ext2_inode* inode, zero_index index) {

    // Return direct block number
    if (0 <= index && index < INDIRECT1_INDEX) {
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

void set_inode_block_number(unsigned char* disk, struct ext2_inode* inode, zero_index index, one_index block_num) {

    // Set direct block number
    if (0 <= index && index < INDIRECT1_INDEX) {
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

bool is_file(struct ext2_dir_entry_2* dir) {
    return (dir != NULL) &&
           dir->file_type == EXT2_FT_REG_FILE;
}

bool is_directory(struct ext2_dir_entry_2* dir) {
    return (dir != NULL) &&
           (dir->file_type == EXT2_FT_DIR);
}

bool is_link(struct ext2_dir_entry_2* dir) {
    return (dir != NULL) &&
           dir->file_type == EXT2_FT_SYMLINK;
}

bool is_valid_dir_entry(struct ext2_dir_entry_2* start, struct ext2_dir_entry_2* current) {

    unsigned long distance = ((char*) current) - ((char*) start);

    return (distance < EXT2_BLOCK_SIZE) &&
           (current->rec_len > 0);
}

bool entry_name_comparison(struct ext2_dir_entry_2* dir, char* name) {

    if (dir->name_len != strlen(name)) {
        return false;
    }

    for (int i = 0; i < dir->name_len; i++) {
        if (dir->name[i] != name[i]) {
            return false;
        }
    }

    return true;
}

//endregion

//region Bitmap Manipulations

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

void increase_free_blocks_count(struct ext2_super_block* sb, struct ext2_group_desc* gd, int count) {
    sb->s_free_blocks_count += count;
    gd->bg_free_blocks_count += count;
}

void increase_free_inodes_count(struct ext2_super_block* sb, struct ext2_group_desc* gd, int count) {
    sb->s_free_inodes_count += count;
    gd->bg_free_inodes_count += count;
}

//endregion

//region Allocation

one_index find_free_block_num(unsigned char* disk) {


    struct ext2_super_block* super_block = get_super_block(disk);
    unsigned char* block_bitmap = get_block_bitmap(disk);

    // TODO POSSIBLE OFF BY 1 ERROR
    one_index i;
    one_index reserved_blocks = 22;
    for (i = reserved_blocks + 1; i < super_block->s_blocks_count + 1; i++) {
        if (!get_bitmap_val(block_bitmap, i - 1)) {
            return i;
        }
    }

    if (i == super_block->s_inodes_count) {
        fprintf(stderr, "All data blocks are used.\n");
        exit(1);
    }

    return 0;
}

one_index find_free_inode_num(unsigned char* disk) {
    struct ext2_super_block* super_block = get_super_block(disk);
    unsigned char* inode_bitmap = get_inode_bitmap(disk);

    // TODO POSSIBLE OFF BY 1 ERROR
    // the first 11 inodes are reserved
    one_index i;
    for (i = EXT2_GOOD_OLD_FIRST_INO + 1; i < super_block->s_inodes_count + 1; i++) {
        if (!get_bitmap_val(inode_bitmap, i - 1)) {
            return i;
        }
    }

    if (i == super_block->s_inodes_count) {
        fprintf(stderr, "All inodes are used.\n");
        exit(1);
    }

    return 0;
}

//endregion

//region Path Traversal

// For maintaining a pair of previous and current
typedef struct {
    struct ext2_dir_entry_2* prev;
    struct ext2_dir_entry_2* cur;
} ext2_dir_entry_pair;

ext2_dir_entry_pair* make_entry_pair(struct ext2_dir_entry_2* prev, struct ext2_dir_entry_2* cur) {
    ext2_dir_entry_pair* ret = malloc(sizeof(ext2_dir_entry_pair));
    ret->prev = prev;
    ret->cur = cur;
    return ret;
}

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

// Finds the directory entry in the data block starting at start,
// whose file name is equal to name
// If no such entry exists, return NULL
struct ext2_dir_entry_2* block_matching_entry(struct ext2_dir_entry_2* start, char* name) {

    // Try to get the pair that matches name, or abort with NULL
    ext2_dir_entry_pair* pair = block_matching_pair(start, name);
    if (pair == NULL) {
        return NULL;
    }

    struct ext2_dir_entry_2* ret = pair->cur;
    free(pair);
    return ret;
}

// TODO SWAP OLD VERSION OUT WITH THIS
ext2_dir_entry_pair* find_matching_pair_new(unsigned char* disk, struct ext2_inode* inode, char* name) {
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

// Finds the directory entry pair in inode where cur's name is name
ext2_dir_entry_pair* find_matching_pair(unsigned char* disk, struct ext2_inode* inode, char* name) {

    if (name == NULL) return NULL;

    // Look through direct data blocks (0 to 11 inclusive)
    for (int i = 0; i <= 11; i++) {
        one_index block_num = inode->i_block[i];

        // Skip absent blocks if there is no block
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

    // Skip absent indirect block
    if (inode->i_block[12] == 0) {
        return NULL;
    }

    // Look through indirect data block for matching directory
    one_index* block_numbers = (one_index*) get_block(disk, inode->i_block[12]);
    for (int i = 0; i < EXT2_BLOCK_SIZE; i++) {
        one_index block_num = block_numbers[i];

        // Skip absent blocks if there is no block
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

// TODO SWAP OLD VERSION OUT WITH THIS
void print_dir_contents_new(unsigned char* disk, struct ext2_dir_entry_2* entry, bool show_dots) {

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

void print_dir_contents(unsigned char* disk, struct ext2_dir_entry_2* entry, bool show_dots) {

    struct ext2_inode* inode = get_inode_from_entry(disk, entry);

    // Look through direct data blocks (0 to 11 inclusive)
    for (int i = 0; i <= 11; i++) {
        one_index block_num = inode->i_block[i];

        // Skip absent blocks if there is no block
        if (block_num == 0) {
            continue;
        }

        // Get the first directory entry, and print the contents
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        print_block_contents(first_entry, show_dots);
    }

    // Skip absent indirect block
    if (inode->i_block[12] == 0) {
        return;
    }

    // Go through first indirect layer
    one_index* block_numbers = (one_index*) get_block(disk, inode->i_block[12]);
    for (int i = 0; i < EXT2_BLOCK_SIZE; i++) {
        one_index block_num = block_numbers[i];

        // Skip absent blocks if there is no block
        if (block_num == 0) {
            continue;
        }

        // Get the first directory entry, and print the contents
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        print_block_contents(first_entry, show_dots);
    }
}

//endregion

//region Freeing

// TODO SWAP OLD VERSION OUT WITH THIS
void free_file_inode_new(unsigned char* disk, struct ext2_dir_entry_2* entry) {

    struct ext2_inode* inode = get_inode_from_entry(disk, entry);
    unsigned char* inode_bitmap = get_inode_bitmap(disk);
    struct ext2_super_block* sb = get_super_block(disk);
    struct ext2_group_desc* gd = get_group_descriptor(disk);
    unsigned char* block_bitmap = get_block_bitmap(disk);

    inode->i_dtime = (unsigned int) time(0);
    increase_free_inodes_count(sb, gd, 1);
    set_bitmap_val(inode_bitmap, entry->inode - 1, 0);

    for (int i = 0; i < INODE_BLOCK_LIMIT; i++) {

        // Get block number, skip if absent
        one_index block_num = get_inode_block_number(disk, inode, i);
        if (block_num == 0) {
            continue;
        }

        // Indicate the the block is free (set bitmap value to 0, increment number of free blocks)
        increase_free_blocks_count(sb, gd, 1);
        set_bitmap_val(block_bitmap, block_num - 1, 0);
    }

}

void free_file_inode(unsigned char* disk, struct ext2_dir_entry_2* entry) {

    struct ext2_inode* inode = get_inode_from_entry(disk, entry);
    unsigned char* inode_bitmap = get_inode_bitmap(disk);
    struct ext2_super_block* sb = get_super_block(disk);
    struct ext2_group_desc* gd = get_group_descriptor(disk);
    unsigned char* block_bitmap = get_block_bitmap(disk);

    inode->i_dtime = (unsigned int) time(0);
    increase_free_inodes_count(sb, gd, 1);
    set_bitmap_val(inode_bitmap, entry->inode - 1, 0);

    for (int i = 0; i <= 11; i++) {
        one_index block_num = inode->i_block[i];

        // Block not in use, skip it
        if (block_num == 0) {
            continue;
        }

        // Indicate the the block is free (set bitmap value to 1, increment number of free blocks)
        increase_free_blocks_count(sb, gd, 1);
        set_bitmap_val(block_bitmap, block_num - 1, 0);
    }

    // Skip absent indirect block
    if (inode->i_block[12] == 0) {
        return;
    }

    // Look through indirect data block
    one_index* block_numbers = (one_index*) get_block(disk, inode->i_block[12]);
    for (int i = 0; i < EXT2_BLOCK_SIZE; i++) {
        one_index block_num = block_numbers[i];

        // Block not in use, skip it
        if (block_num == 0) {
            continue;
        }

        increase_free_blocks_count(sb, gd, 1);
        set_bitmap_val(block_bitmap, block_num - 1, 0);
    }

}

// TODO SWAP OLD VERSION OUT WITH THIS
void free_parent_inode_block_new(unsigned char* disk, struct ext2_dir_entry_2* parent_entry, char* name) {
    struct ext2_inode* parent_inode = get_inode(disk, parent_entry->inode - 1);
    struct ext2_super_block* sb = get_super_block(disk);
    struct ext2_group_desc* gd = get_group_descriptor(disk);
    unsigned char* block_bitmap = get_block_bitmap(disk);

    for (int i = 0; i < INODE_BLOCK_LIMIT; i++) {

        // Get block number and skip if absent
        one_index block_num = get_inode_block_number(disk, parent_inode, i);
        if (block_num == 0) {
            continue;
        }

        // Get the first directory entry, and look for a pair of entries where cur matches name
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        ext2_dir_entry_pair* pair = block_matching_pair(first_entry, name);
        if (pair == NULL) {
            continue;
        }

            // There is a matching pair, so manipulate the entries then return
        else {

            // Matching entry was the first in the block, skip over
            if (pair->prev == NULL) {
                pair->cur->name_len = 0;
                increase_free_blocks_count(sb, gd, 1);
                set_bitmap_val(block_bitmap, block_num - 1, 0);
            }

                // Matching entry within block
            else {
                pair->prev->rec_len += pair->cur->rec_len;
            }

            free(pair);
            return;
        }
    }
}

void free_parent_inode_block(unsigned char* disk, struct ext2_dir_entry_2* parent_entry, char* name) {
    struct ext2_inode* parent_inode = get_inode(disk, parent_entry->inode - 1);
    struct ext2_super_block* sb = get_super_block(disk);
    struct ext2_group_desc* gd = get_group_descriptor(disk);
    unsigned char* block_bitmap = get_block_bitmap(disk);


    // Look through direct data blocks (0 to 11 inclusive)
    for (int i = 0; i <= 11; i++) {

        one_index block_num = parent_inode->i_block[i];

        // Skip absent blocks
        if (block_num == 0) {
            continue;
        }

        // Get the first directory entry, and look for a pair of entries where cur matches name
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        ext2_dir_entry_pair* pair = block_matching_pair(first_entry, name);
        if (pair == NULL) {
            continue;
        }

            // There is a matching pair, so manipulate the entries then return
        else {

            // Matching entry was the first in the block, skip over
            if (pair->prev == NULL) {
                pair->cur->name_len = 0;
                increase_free_blocks_count(sb, gd, 1);
                set_bitmap_val(block_bitmap, block_num - 1, 0);
            }

                // Matching entry within block
            else {
                pair->prev->rec_len += pair->cur->rec_len;
            }

            free(pair);
            return;
        }
    }

    // Skip absent indirect block
    if (parent_inode->i_block[12] == 0) {
        return;
    }

    // Look through indirect data block for matching directory
    one_index* block_numbers = (one_index*) get_block(disk, parent_inode->i_block[12]);
    for (int i = 0; i < EXT2_BLOCK_SIZE; i++) {
        one_index block_num = block_numbers[i];

        // Skip absent blocks
        if (block_num == 0) {
            continue;
        }

        // Get the first directory entry, and look for a pair of entries where cur matches name
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        ext2_dir_entry_pair* pair = block_matching_pair(first_entry, name);
        if (pair == NULL) {
            continue;
        }

            // There is a matching pair, so manipulate the entries then return
        else {

            // Matching entry was the first in the block, skip over
            if (pair->prev == NULL) {
                pair->cur->name_len = 0;
                increase_free_blocks_count(sb, gd, 1);
                set_bitmap_val(block_bitmap, block_num - 1, 0);
            }

                // Matching entry within block
            else {
                pair->prev->rec_len += pair->cur->rec_len;
            }

            free(pair);
            return;
        }
    }

    return;
}

// Calculates the number of bits needed for a directory entry
size_t total_entry_length(char* name) {
    return 8 + strlen(name);
}

// Attempts to make a directory entry in the same block as start
// Returns NULL if unsuccessful
struct ext2_dir_entry_2* make_entry_within_block(struct ext2_dir_entry_2* start,
                                                 one_index inode_number,
                                                 char* name,
                                                 unsigned char file_type) {

    // ASSUME:
    // NAME IS NON-NULL,
    // NAME IS NON-EMPTY
    // ENTRY DOES NOT EXIST ALREADY

    // Try to find an entry with enough succeeding empty space to fit the new entry
    size_t name_len = strlen(name);
    size_t space_needed = total_entry_length(name);
    for (struct ext2_dir_entry_2* cur = start; is_valid_dir_entry(start, cur); cur = get_next_dir_entry(cur)) {

        unsigned char space_left = (unsigned char) (cur->rec_len - cur->name_len);

        if (space_left >= space_needed) {

            // Look at the entry right after cur's name, abort if it's out of bounds
            struct ext2_dir_entry_2* ret = get_shifted_dir_entry(cur, cur->name_len);
            if (!is_valid_dir_entry(start, ret)) {
                return NULL;
            }

            // Cut cur off at the end of it's name
            unsigned short old_rec_len = cur->rec_len;
            cur->rec_len = cur->name_len;

            // Fill in the fields for the new entry
            strncpy(ret->name, name, name_len);
            ret->name_len = (unsigned char) name_len;
            ret->rec_len = old_rec_len - ret->name_len;
            ret->file_type = file_type;
            ret->inode = inode_number;

            return ret;
        }
    }

    // Couldn't find a space to put the new entry into
    return NULL;
}


struct ext2_dir_entry_2* make_entry_within_inode(unsigned char* disk,
                                                 struct ext2_inode* inode,
                                                 one_index child_inode,
                                                 char* name,
                                                 unsigned char file_type) {

    // TODO ABORT IN EDGE CASES

    // See if we can squeeze the entry into the existing blocks of the inode
    struct ext2_dir_entry_2* new_entry = NULL;
    for (zero_index i = 0; i < INODE_BLOCK_LIMIT; i++){

        // Get the block number, skip if it doesn't exist
        one_index block_num = get_inode_block_number(disk, inode, i);
        if (block_num == 0){
            continue;
        }

        // Try to make an entry within the given block
        struct ext2_dir_entry_2* start = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        struct ext2_dir_entry_2* attempt = make_entry_within_block(start, child_inode, name, file_type);
        if (attempt != NULL){
            new_entry = attempt;
            break;
        }
    }

    // Return what we just made if we can
    if (new_entry != NULL) return new_entry;

    // Otherwise look for the first free block number that we can use
    for (zero_index i = 0; i < INODE_BLOCK_LIMIT; i++){

    }


}

// TODO SWAP OLD VERSION OUT WITH THIS
struct ext2_dir_entry_2* find_free_directory_entry_new(unsigned char* disk, struct ext2_inode* inode, char* name) {



    // look through direct blocks
    one_index block_num = 1; // will be the last used block in this inode
    int i; // will be the first unused block in this inode
    for (i = 0; block_num != 0; i++) {
        block_num = inode->i_block[i];
    }

    struct ext2_dir_entry_2* new_de;
    int required_size = 8 + (int) strlen(name);
    if (i <= 11) {
        // there are unused block in direct blocks
        struct ext2_dir_entry_2* last_used_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);

        int total_used_rec_len = 0;

        while (total_used_rec_len < EXT2_BLOCK_SIZE) {
            total_used_rec_len += last_used_entry->rec_len;
            last_used_entry = get_next_dir_entry(last_used_entry);
        }

        // size of an entry is 8 + name length
        int last_used_entry_size = sizeof(last_used_entry) + last_used_entry->name_len;
        int rounding = 4 - (last_used_entry_size % 4);
        total_used_rec_len += last_used_entry_size + rounding;
        int space_left_in_block = EXT2_BLOCK_SIZE - total_used_rec_len;

        if (space_left_in_block < required_size) {
            // not enough space to insert the file name
            struct ext2_super_block* sb = get_super_block(disk);
            struct ext2_group_desc* gd = get_group_descriptor(disk);

            one_index new_block = find_free_block_num(disk);
            set_bitmap_val(get_block_bitmap(disk), new_block - 1, 1);
            increase_free_blocks_count(sb, gd, -1);
            inode->i_blocks += 2;
            inode->i_block[i] = new_block;
            new_de = make_directory_entry(disk, block_num, 0);
            new_de->rec_len = EXT2_BLOCK_SIZE;
        } else {
            // enough space to insert the file name
            last_used_entry->rec_len = (unsigned short) (last_used_entry_size + rounding);
            new_de = make_directory_entry(disk, block_num, total_used_rec_len);
            new_de->rec_len = (unsigned short) (EXT2_BLOCK_SIZE - total_used_rec_len);
        }

        return new_de;

    } else if (i == 12) {
        // have to look through indirect blocks
        // TODO look at through pointer blocks
        return NULL;
    }

    return NULL;
}

struct ext2_dir_entry_2* find_free_directory_entry(unsigned char* disk, struct ext2_inode* inode, char* file_name) {

    // look through direct blocks
    one_index block_num = 1; // will be the last used block in this inode
    int i; // will be the first unused block in this inode
    for (i = 0; block_num != 0; i++) {
        block_num = inode->i_block[i];
    }

    struct ext2_dir_entry_2* new_de;
    int required_size = 8 + (int) strlen(file_name);
    if (i <= 11) {
        // there are unused block in direct blocks
        struct ext2_dir_entry_2* last_used_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);

        int total_used_rec_len = 0;

        while (total_used_rec_len < EXT2_BLOCK_SIZE) {
            total_used_rec_len += last_used_entry->rec_len;
            last_used_entry = get_next_dir_entry(last_used_entry);
        }

        // size of an entry is 8 + name length
        int last_used_entry_size = sizeof(last_used_entry) + last_used_entry->name_len;
        int rounding = 4 - (last_used_entry_size % 4);
        total_used_rec_len += last_used_entry_size + rounding;
        int space_left_in_block = EXT2_BLOCK_SIZE - total_used_rec_len;

        if (space_left_in_block < required_size) {
            // not enough space to insert the file name
            struct ext2_super_block* sb = get_super_block(disk);
            struct ext2_group_desc* gd = get_group_descriptor(disk);

            one_index new_block = find_free_block_num(disk);
            set_bitmap_val(get_block_bitmap(disk), new_block - 1, 1);
            increase_free_blocks_count(sb, gd, -1);
            inode->i_blocks += 2;
            inode->i_block[i] = new_block;
            new_de = make_directory_entry(disk, block_num, 0);
            new_de->rec_len = EXT2_BLOCK_SIZE;
        } else {
            // enough space to insert the file name
            last_used_entry->rec_len = (unsigned short) (last_used_entry_size + rounding);
            new_de = make_directory_entry(disk, block_num, total_used_rec_len);
            new_de->rec_len = (unsigned short) (EXT2_BLOCK_SIZE - total_used_rec_len);
        }

        return new_de;

    } else if (i == 12) {
        // have to look through indirect blocks
        // TODO look at through pointer blocks
        return NULL;
    }

    return NULL;
}
//endregion

//region System Utils

void crash_with_usage(char* err_msg) {
    fprintf(stderr, "%s\n", err_msg);
    exit(1);
}

//endregion

//region Inodes and Datablocks

struct ext2_inode* create_sym_inode(unsigned char* disk, one_index inode_index, char* source) {

    struct ext2_super_block* sb = get_super_block(disk);
    struct ext2_group_desc* gd = get_group_descriptor(disk);
    unsigned char* inode_bitmap = get_inode_bitmap(disk);

    increase_free_inodes_count(sb, gd, -1);
    set_bitmap_val(inode_bitmap, inode_index - 1, 1);

    struct ext2_inode* sym_inode = get_inode(disk, inode_index);

    sym_inode->i_mode = EXT2_S_IFLNK;
    sym_inode->i_size = (unsigned int) strlen(source); //size is the same as src node
    sym_inode->i_ctime = (unsigned int) time(0);
    sym_inode->i_dtime = 0;
    sym_inode->i_blocks = 0;
    sym_inode->i_links_count = 2; // Itself and from parent.

    return sym_inode;
}

void write_str_to_new_inode(unsigned char* disk, struct ext2_inode* inode, char* content) {

    struct ext2_super_block* sb = get_super_block(disk);
    struct ext2_group_desc* gd = get_group_descriptor(disk);
    unsigned char* block_bitmap = get_block_bitmap(disk);


    // Assume the content (link name) can always fit into one block
    if (strlen(content) < EXT2_BLOCK_SIZE) {
        inode->i_blocks = 2;
        inode->i_block[0] = find_free_block_num(disk);
        increase_free_blocks_count(sb, gd, -1);
        set_bitmap_val(block_bitmap, inode->i_block[0] - 1, 1);

        unsigned char* data_block = get_block(disk, inode->i_block[0]);
        strcpy((char*) data_block, content);
    } else {
        // throw error if the file path is too long
        exit(ENAMETOOLONG);
    }

}

//endregion
