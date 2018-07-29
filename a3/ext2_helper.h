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

struct ext2_dir_entry_2* get_next_dir_entry(struct ext2_dir_entry_2* entry) {
    char* byte_wise = (char*) entry;
    char* shifted = byte_wise + entry->rec_len;

    return (struct ext2_dir_entry_2*) shifted;
}

bool is_valid_dir_entry(struct ext2_dir_entry_2* start, struct ext2_dir_entry_2* current) {

    unsigned long distance = ((char* ) current) - ((char*) start);

    return (distance < EXT2_BLOCK_SIZE) &&
           (current->rec_len > 0);
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



//endregion

//region Bitmap Manipulations

bool get_bitmap_val(unsigned char* bitmap, int bit){
    int result = (bitmap[bit/8] >> (bit % 8)) & 0x1;
    return result != 0;
}

void set_bitmap_val(unsigned char* bitmap, int bit, bool value){
    int binary = 0;
    if (value){
        binary = 1;
    }

    bitmap[bit-1 / 8] ^= (binary << (bit-1 % 8));
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

//region Path Traversal

bool entry_name_comparison(struct ext2_dir_entry_2* dir, char* name){

    if (dir->name_len != strlen(name)) {
        return false;
    }

    for (int i = 0; i < dir->name_len; i++){
        if (dir->name[i] != name[i]){
            return false;
        }
    }

    return true;
}

// Finds the directory entry in the data block starting at start,
// whose file name is equal to name
// If no such entry exists, return NULL
struct ext2_dir_entry_2* block_matching_entry(struct ext2_dir_entry_2* start, char* name) {

    for (struct ext2_dir_entry_2* cur = start; is_valid_dir_entry(start, cur); cur = get_next_dir_entry(cur)) {
        // TODO : THIS NAME COMPARISON MAY BE INCORRECT
        if (entry_name_comparison(cur, name)) {
            return cur;
        }
    }
    return NULL;
}

// Finds the directory entry in inode whose name is name
struct ext2_dir_entry_2* find_matching_entry(unsigned char* disk, struct ext2_inode* inode, char* name) {
    if (name == NULL) return NULL;

    // Look through direct data blocks (0 to 11 inclusive)
    for (int i = 0; i <= 11; i++) {
        one_index block_num = inode->i_block[i];

        // Skip absent blocks if there is no block
        if (block_num == 0) {
            continue;
        }

        // Get the first directory entry, and look for a matching directory
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        struct ext2_dir_entry_2* match = block_matching_entry(first_entry, name);
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

        // Get the first directory entry, and look for a matching directory
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        struct ext2_dir_entry_2* match = block_matching_entry(first_entry, name);
        if (match != NULL) {
            return match;
        }
    }

    return NULL;
}



void print_dir_name(struct ext2_dir_entry_2* dir){

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
        if (cur->name_len != 0){
            print_dir_name(cur);
        }
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
        if (!is_directory(entry)){
            break;
        }

        // Update inode
        entry_inode = get_inode_from_entry(disk, entry);
    }

    return entry;
}

void free_file_inode(unsigned char* disk, struct ext2_dir_entry_2* entry) {

    struct ext2_inode* inode = get_inode_from_entry(disk, entry);
    unsigned char* inode_bitmap = get_inode_bitmap(disk);
    struct ext2_super_block* sb = get_super_block(disk);
    struct ext2_group_desc* gd = get_group_descriptor(disk);
    unsigned char* block_bitmap = get_block_bitmap(disk);

    inode->i_dtime = (unsigned int) time(0);
    increase_free_inodes_count(sb, gd, 1);
    set_bitmap_val(inode_bitmap, entry->inode, 1);

    for (int i = 0; i <= 11; i++) {
        increase_free_blocks_count(sb, gd, 1);
        set_bitmap_val(block_bitmap, inode->i_block[i], 1);
    }

    // Skip absent indirect block
    if (inode->i_block[12] == 0) {
        return;
    }

    // Look through indirect data block
    one_index* block_numbers = (one_index*) get_block(disk, inode->i_block[12]);
    for (int i = 0; i < EXT2_BLOCK_SIZE; i++) {
        increase_free_blocks_count(sb, gd, 1);
        set_bitmap_val(block_bitmap, block_numbers[i], 1);
    }

}

void free_parent_inode_block(unsigned char* disk, struct ext2_dir_entry_2* parent_entry, char* name) {
    struct ext2_inode* parent_inode = get_inode(disk, parent_entry->inode-1);
    struct ext2_super_block* sb = get_super_block(disk);
    struct ext2_group_desc* gd = get_group_descriptor(disk);
    unsigned char* block_bitmap = get_block_bitmap(disk);
    struct ext2_dir_entry_2* prev_de = NULL;

    // Look through direct data blocks (0 to 11 inclusive)
    for (int i = 0; i <= 11; i++) {
        one_index block_num = parent_inode->i_block[i];

        // Skip absent blocks if there is no block
        if (block_num == 0) {
            continue;
        }

        // Get the first directory entry, and look for a matching directory
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        struct ext2_dir_entry_2* match = block_matching_entry(first_entry, name);

        if (match != NULL) {
            if (prev_de != NULL) {
                // make sure previous entry will include the current entry after deletion
                prev_de->rec_len += match->rec_len;
                return;
            }
            // if match is found at the first entry

//            parent_inode->i_block[i] = 0;
            match->name_len = 0;
            increase_free_blocks_count(sb, gd, 1);
            set_bitmap_val(block_bitmap, parent_inode->i_block[i], 1);
            return;
        }
        // keep track of previous directory entry
        prev_de = first_entry;
    }

    // Skip absent indirect block
    if (parent_inode->i_block[12] == 0) {
        return;
    }

    // Look through indirect data block for matching directory
    one_index* block_numbers = (one_index*) get_block(disk, parent_inode->i_block[12]);
    for (int i = 0; i < EXT2_BLOCK_SIZE; i++) {
        one_index block_num = block_numbers[i];

        // Skip absent blocks if there is no block
        if (block_num == 0) {
            continue;
        }

        // Get the first directory entry, and look for a matching directory
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        struct ext2_dir_entry_2* match = block_matching_entry(first_entry, name);

        if (match != NULL) {
            if (prev_de != NULL) {
                // make sure previous entry will include the current entry after deletion
                prev_de->rec_len += match->rec_len;
                return;
            }
            // if match is found at the first entry
            parent_inode->i_block[i] = 0;
            increase_free_blocks_count(sb, gd, 1);
            set_bitmap_val(block_bitmap, parent_inode->i_block[i], 1);
            return;
        }
    }

    return;
}

//endregion

//region System Utils

void crash_with_usage(char* prog_name){
    fprintf(stderr, "Usage: %s [disk] [option -a] [path]\n", prog_name);
    exit(1);
}

//endregion

