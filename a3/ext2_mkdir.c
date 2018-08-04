#include "ext2_helper.h"
#include<fcntl.h>
#include <getopt.h>


int ext2_mkdir(unsigned char* disk, char* path) {

    // Split path into components, then get the last element
    List* path_components = split_path(path);
    char* new_dir = listPop(path_components);
    struct ext2_dir_entry_2* parent_entry;

    // Abort if we were only given "/"
    if (path_components->count == 0) {
        fprintf(stderr, "/ already exists.\n");
        exit(EEXIST);
    }

    else if (((parent_entry = traverse_path(disk, path_components)) == NULL)) {
        fprintf(stderr, "Ancestors of %s do not exist.\n", path);
        exit(ENOENT);
    }

    else if (!is_directory(parent_entry)){
        fprintf(stderr, "Parent not a directory.\n");
        exit(ENOENT);
    }

    // Restore the list to see if the directory already exists
    listAppend(path_components, new_dir);
    if (traverse_path(disk, path_components) != NULL){
        fprintf(stderr, "%s already exists.\n", path);
        exit(EEXIST);
    }

    // Get the parent inode

    // Allocate new directory inode, make . and .. entries
    one_index dir_inode_num = allocate_dir_inode(disk);
    struct ext2_inode* dir_inode = get_inode(disk, dir_inode_num);


    if (make_entry_in_inode(disk, dir_inode, dir_inode_num, ".", EXT2_FT_DIR) == NULL){
        fprintf(stderr, "Unable to create self (.) directory.\n");
        free_inode(disk, dir_inode_num);
        exit(1);
    }

    if (make_entry_in_inode(disk, dir_inode, parent_entry->inode, "..", EXT2_FT_DIR) == NULL){
        fprintf(stderr, "Unable to create parent (..) directory.\n");
        free_inode(disk, dir_inode_num);
        exit(1);
    }

    // Make directory entry in parent
    struct ext2_inode* parent_inode = get_inode_from_entry(disk, parent_entry);
    if (make_entry_in_inode(disk, parent_inode, dir_inode_num, new_dir, EXT2_FT_DIR) == NULL){
        fprintf(stderr, "Unable to create directory in parent.\n");
        free_inode(disk, dir_inode_num);
        exit(1);
    }

    destroyList(path_components);
    return 0;
}


int main(int argc, char* argv[]) {
    char* img = argv[1];
    char* dir_path = argv[2];


    if (argc != 3) {
        fprintf(stderr, "Usage: %s [disk] [directory path]\n", argv[0]);
        exit(1);
    }


    unsigned char* disk = load_disk_to_mem(img);

    ext2_mkdir(disk, dir_path);
}
