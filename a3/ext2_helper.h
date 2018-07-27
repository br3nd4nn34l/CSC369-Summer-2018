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

List* split_path(char* path) {
    List* tokens = makeList(5);

    char* token;
    token = strtok(path, "/");;
    while (token != NULL) {
        listAppend(tokens, token);
        token = strtok(NULL, "/");
    }

    return tokens;
}

typedef unsigned int zero_index;
typedef unsigned int one_index;


//region Load disk to memory

unsigned char* load_disk_to_mem(int file_descriptor) {
    unsigned char* disk = mmap(NULL, 128*1024, PROT_READ | PROT_WRITE, MAP_SHARED, file_descriptor, 0);
    if (disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }
    return disk;
}

//endregion


//region Disk Fetching functions

// We only need to deal with 1 block group, thus
// structure of disk is as follows (by block number):
// 0 = BOOT
// 1 = SUPER BLOCK
// 2 = GROUP DESCRIPTOR (gives block numbers for bitmaps and inode table)

unsigned char* get_block(unsigned char* disk, one_index block_ind) {
    return (disk + (EXT2_BLOCK_SIZE * block_ind));
}

struct ext2_super_block* get_super_block(unsigned char* disk){
    return (struct ext2_super_block*) get_block(disk, 1);
}

struct ext2_group_desc* get_group_descriptor(unsigned char* disk){
    return (struct ext2_group_desc*) get_block(disk, 2);
}

unsigned char* get_block_bitmap(unsigned char* disk){
    struct ext2_group_desc* group_desc = get_group_descriptor(disk);
    return get_block(disk, group_desc->bg_block_bitmap);
}

unsigned char* get_inode_bitmap(unsigned char* disk){
    struct ext2_group_desc* group_desc = get_group_descriptor(disk);
    return get_block(disk, group_desc->bg_inode_bitmap);
}

struct ext2_inode* get_inode_table(unsigned char* disk){
    struct ext2_group_desc* group_desc = get_group_descriptor(disk);
    return (struct ext2_inode *) get_block(disk, group_desc->bg_inode_table);
}

struct ext2_inode* get_inode(unsigned char* disk, zero_index inode_ind){
    return &get_inode_table(disk)[inode_ind];
}

struct ext2_inode* get_root_inode(unsigned char* disk){
    // Handout specifies that root inode is at index 1
    return get_inode(disk, 1);
}

struct ext2_inode* get_inode_from_entry(unsigned char* disk, struct ext2_dir_entry_2* entry){
    if (entry == NULL){
        return NULL;
    }
    return get_inode(disk, entry->inode - 1);
}

struct ext2_dir_entry_2* get_next_dir_entry(struct ext2_dir_entry_2* entry){
    return entry + (entry->rec_len);
}

bool is_valid_dir_entry(struct ext2_dir_entry_2* start, struct ext2_dir_entry_2* current){
    return (current - start) <= EXT2_BLOCK_SIZE;
}

//endregion

//region Predicates

bool is_file(struct ext2_dir_entry_2* dir){
    return dir->file_type == EXT2_FT_REG_FILE;
}

bool is_directory(struct ext2_dir_entry_2* dir){
    return dir->file_type == EXT2_FT_DIR;
}

bool is_link(struct ext2_dir_entry_2* dir){
    return dir->file_type == EXT2_FT_SYMLINK;
}



//endregion


// Finds the directory entry in the data block starting at start,
// whose file name is equal to name
// If no such entry exists, return NULL
struct ext2_dir_entry_2* block_matching_dir(struct ext2_dir_entry_2* start, char* name){
    for (struct ext2_dir_entry_2* cur = start; is_valid_dir_entry(start, cur); get_next_dir_entry(cur)){
        // TODO : THIS NAME COMPARISON MAY BE INCORRECT
        if (is_directory(cur) && (strcmp(cur->name, name) == 0)){
            return cur;
        }
    }
    return NULL;
}


// Finds the directory entry in inode whose name is name
struct ext2_dir_entry_2* find_matching_dir(unsigned char* disk, struct ext2_inode* inode, char* name){

    // Look through direct data blocks (0 to 11 inclusive)
    for (int i = 0; i <= 11; i++){
        one_index block_num = inode->i_block[i];

        // Skip absent blocks if there is no block
        if (block_num == 0){
            continue;
        }

        // Get the first directory entry, and look for a matching directory
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        struct ext2_dir_entry_2* match = block_matching_dir(first_entry, name);
        if (match != NULL){
            return match;
        }
    }

    // Skip absent indirect block
    if (inode->i_block[12] == 0){
        return NULL;
    }

    // Look through indirect data block for matching directory
    one_index* block_numbers = (one_index*) get_block(disk, inode->i_block[12]);
    for (int i = 0; i < EXT2_BLOCK_SIZE; i++){
        one_index block_num = block_numbers[i];

        // Skip absent blocks if there is no block
        if (block_num == 0){
            continue;
        }

        // Get the first directory entry, and look for a matching directory
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        struct ext2_dir_entry_2* match = block_matching_dir(first_entry, name);
        if (match != NULL){
            return match;
        }
    }

    return NULL;
}

void print_block_contents(struct ext2_dir_entry_2* start){
    for (struct ext2_dir_entry_2* cur = start; is_valid_dir_entry(start, cur); get_next_dir_entry(cur)){
        // TODO THIS ASSUMES CUR IS PROPER
        printf("%s\n", cur->name);
    }
}

void print_dir_contents(char* disk, struct ext2_dir_entry_2* entry){

    struct ext2_inode* inode = get_inode_from_entry(disk, entry);

    // Look through direct data blocks (0 to 11 inclusive)
    for (int i = 0; i <= 11; i++){
        one_index block_num = inode->i_block[i];

        // Skip absent blocks if there is no block
        if (block_num == 0){
            continue;
        }

        // Get the first directory entry, and print the contents
        struct ext2_dir_entry_2* first_entry = (struct ext2_dir_entry_2*) get_block(disk, block_num);
        print_block_contents(first_entry);
    }

    // Skip absent indirect block
    if (inode->i_block[12] == 0){
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
        print_block_contents(first_entry);
    }
}

int ls_proto(unsigned char* disk, char* path){
    List* path_components = split_path(path);

    struct ext2_inode* cur = get_root_inode(disk);
    struct ext2_dir_entry_2* entry;
    zero_index ind = 0;

    while (cur != NULL){
        entry = find_matching_dir(disk, cur, path_components->contents[ind]);
        cur = get_inode_from_entry(disk, entry);
        ind++;
    }

    if (ind != path_components->count - 1) {
        printf("%s\n", "No such file or directory");
        return ENOENT;
    }

    if (is_file(entry) || is_link(entry)) {
        printf("%s\n", listPeek(path_components));
    }
    else if (is_directory(entry)) {
        print_dir_contents(disk, entry);
    }

}

