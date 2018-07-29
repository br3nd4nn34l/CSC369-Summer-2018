#include "ext2_helper.h"
#include<fcntl.h>
#include <getopt.h>

// TODO PRINTF SHOULD BE FPRINTF ON STDERR

int ext2_ln(unsigned char* disk, char* source, char* dest, bool sym_link) {

    int ret = 0;

    List* src_components = split_path(source);
    struct ext2_dir_entry_2* src_entry = traverse_path(disk, src_components);

    List* dst_components = split_path(source);
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
    listPop(dst_components);
    struct ext2_dir_entry_2* dst_parent = traverse_path(disk, dst_components);
    if (dst_parent == NULL){
        printf("Parent of %s does not exist\n", dest);
        ret = EEXIST;
    }


    // Execute the algorithm
    // Free all lists
    destroyList(src_components);
    destroyList(dst_components);


    return ret;
}


int main(int argc, char* argv[]) {
    char* img = argv[1];
    char* source;
    char* dest;
    bool sym_link = false;
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
                sym_link = true;
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

    ext2_ln(disk, source, dest, sym_link);
}
