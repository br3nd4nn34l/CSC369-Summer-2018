#include "ext2_helper.h"
#include<fcntl.h>
#include <getopt.h>


int ext2_mkdir(unsigned char* disk, char* path) {

    int ret = 0;

    // Split path into components, then get the last element
    List* path_components = split_path(path);
    char* new_dir = listPop(path_components);
    struct ext2_dir_entry_2* parent_entry;

    // Abort if we were only given "/"
    if (path_components->count == 1){
        fprintf(stderr, "/ already exists.\n");
        ret = EEXIST;
    }

    else if (((parent_entry = traverse_path(disk, path_components)) == NULL)) {
        fprintf(stderr, "Ancestors of %s do not exist.\n", path);
        ret = ENOENT;
    }

    else if (!is_directory(parent_entry)){
        fprintf(stderr, "Parent not a directory.\n");
        ret = ENOENT;
    }

    // Restore the list to see if the directory already exists
    listAppend(path_components, new_dir);
    if (traverse_path(disk, path_components) != NULL){
        fprintf(stderr, "%s already exists.\n", path);
        ret = EEXIST;
    }

    // Get the parent inode,
    struct ext2_inode* parent_inode = get_inode_from_entry(disk, parent_entry);


//    // find the free directory entry in the destination directory
//    struct ext2_inode* dst_parent_inode = get_inode_from_entry(disk, dst_parent_entry);
//
//    struct ext2_dir_entry_2* attempt;
//    if (is_sym_link) {
//
//        // Soft link
//        one_index sym_inode_idx = find_free_inode_num(disk);
//
//        // create a new inode with sym link type at a free inode index
//        struct ext2_inode* sym_node = allocate_link_inode(disk, sym_inode_idx, source);
//
//
//        // copy the source path into new sym inode's data blocks
//        write_str_to_new_inode(disk, sym_node, source);
//
//        attempt = make_entry_in_inode(
//                disk,
//                dst_parent_inode,
//                sym_inode_idx,
//                dst_file_name,
//                EXT2_FT_SYMLINK
//        );
//
//        if (attempt == NULL) {
//            // cannot insert file name into parents' inode
//            revert_inode(disk, sym_inode_idx);
//        }
//
//    } else {
//        // Hard link
//        attempt = make_entry_in_inode(
//                disk,
//                dst_parent_inode,
//                src_entry->inode,
//                dst_file_name,
//                EXT2_FT_REG_FILE
//        );
//
//        // increase source file's link count
//        get_inode_from_entry(disk, src_entry)->i_links_count++;
//
//    }
//
//    if (attempt == NULL) {
//        ret = 1;
//        fprintf(stderr, "Failed to link.\n");
//    }
//
//
//    // Free all lists
//    destroyList(src_components);
//    destroyList(dst_components);
//
//
//    return ret;
}


int main(int argc, char* argv[]) {
    char* img = argv[1];
    char* source;
    char* dest;
    bool is_sym_link = false;
    int ch = 0;

    char err_msg[100];
    sprintf(err_msg, "Usage: %s [disk] [option -s] [source path] [destination path]", argv[0]);
    switch(argc) {
        case 4:
            if (strcmp(argv[2], "-s") == 0 || strcmp(argv[3], "-s") == 0) {
                crash_with_usage(err_msg);
            }
            source = argv[2];
            dest = argv[3];
            break;
        case 5:
            source = argv[3];
            dest = argv[4];
            break;
        default:
            crash_with_usage(err_msg);
    }

    while ((ch = getopt(argc, argv, "s")) != -1) {
        switch(ch) {
            case 's':
                // option to print everything
                is_sym_link = true;
                fprintf(stderr, "I got symlink\n");
                break;
            default:
                crash_with_usage(err_msg);
        }
    }

    unsigned char* disk = load_disk_to_mem(img);

    ext2_mkdir(disk, dest);
}
