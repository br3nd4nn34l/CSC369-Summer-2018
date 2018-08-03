#include "ext2_helper.h"
#include<fcntl.h>
#include <getopt.h>

// TODO PRINTF SHOULD BE FPRINTF ON STDERR

int ext2_ln(unsigned char* disk, char* source, char* dest, bool is_sym_link) {

    int ret = 0;

    List* src_components = split_path(source);
    struct ext2_dir_entry_2* src_entry = traverse_path(disk, src_components);

    List* dst_components = split_path(dest);
    struct ext2_dir_entry_2* dst_entry = traverse_path(disk, dst_components);

    // Abort if source file does not exist
    if (src_entry == NULL) {
        printf("No such file\n");
        ret = ENOENT;
    }
    // Abort if source is a directory
    else if (is_directory(src_entry)){
        printf("%s is a directory\n", source);
        ret = EISDIR;
    }

    // Abort if destination is a directory
    else if (is_directory(dst_entry)){
        printf("%s is a directory\n", dest);
        ret = EISDIR;
    }

    // Abort if destination entry already exists
    else if (dst_entry != NULL){
        printf("%s already exists.\n", dest);
        ret = EEXIST;
    }

    // Abort if destination's parent does not exist
    char* dst_file_name = listPop(dst_components);
    struct ext2_dir_entry_2* dst_parent_entry = traverse_path(disk, dst_components);
    if (dst_parent_entry == NULL){
        printf("Parent of %s does not exist\n", dest);
        ret = EEXIST;
    }

    // find the free directory entry in the destination directory
    struct ext2_inode* dst_parent_inode = get_inode_from_entry(disk, dst_parent_entry);

    struct ext2_dir_entry_2* attempt;
    if (is_sym_link) {

        // Soft link
        one_index sym_inode_idx = find_free_inode_num(disk);

        // create a new inode with sym link type at a free inode index
        struct ext2_inode* sym_node = create_sym_inode(disk, sym_inode_idx, source);
        // copy the source path into new sym inode's data blocks
        write_str_to_new_inode(disk, sym_node, source);

        attempt = make_entry_in_inode(
                disk,
                dst_parent_inode,
                sym_inode_idx,
                dst_file_name,
                EXT2_FT_SYMLINK
        );

        if (attempt == NULL) {
            // cannot insert file name into parents' inode
            revert_inode(disk, sym_inode_idx);
        }

    } else {
        // Hard link
        attempt = make_entry_in_inode(
                disk,
                dst_parent_inode,
                src_entry->inode,
                dst_file_name,
                EXT2_FT_REG_FILE
        );

        // increase source file's link count
        get_inode_from_entry(disk, src_entry)->i_links_count++;

    }

    if (attempt == NULL) {
        ret = 1;
        fprintf(stderr, "Failed to link.\n");
    }


    // Free all lists
    destroyList(src_components);
    destroyList(dst_components);


    return ret;
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

    int file_descriptor = open(img, O_RDWR);

    // Opening disk
    if (!file_descriptor) {
        fprintf(stderr, "Disk image '%s' not found.", img);
        exit(ENOENT);
    }

    unsigned char* disk = load_disk_to_mem(file_descriptor);

    ext2_ln(disk, source, dest, is_sym_link);
}
