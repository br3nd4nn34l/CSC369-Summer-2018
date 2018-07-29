#include "ext2_helper.h"
#include<fcntl.h>
#include <getopt.h>


int ext2_ls(unsigned char* disk, char* path, bool show_dots) {
    int ret;

    List* path_components = split_path(path);
    struct ext2_dir_entry_2* entry = traverse_path(disk, path_components);

    if (entry == NULL) {
        printf("%s\n", "No such file or directory");
        ret = ENOENT;
    }

    else {
        if (is_file(entry) || is_link(entry)) {
            printf("%s\n", (char*) listPeek(path_components));
        } else if (is_directory(entry)) {
            print_dir_contents(disk, entry, show_dots);
        }
        ret = 0;
    }

    destroyList(path_components);
    return ret;

}


int main(int argc, char* argv[]) {
    int ch = 0;
    bool show_dots = false;

    char* img = argv[1];
    char* path;

    switch(argc) {
        case 3:
            if (strcmp(argv[2], "-a") == 0) {
                crash_with_usage(argv[0]);
            }
            path = argv[2];
            break;
        case 4:
            path = argv[3];
            break;
        default:
            crash_with_usage(argv[0]);
    }


    while ((ch = getopt(argc, argv, "a")) != -1) {
        switch(ch) {
            case 'a':
                // option to print everything
                show_dots = true;
                break;
            default:
                crash_with_usage(argv[0]);
        }
    }


    int file_descriptor = open(img, O_RDWR);

    // Opening disk
    if (!file_descriptor) {
        fprintf(stderr, "Disk image '%s' not found.", img);
        exit(ENOENT);
    }

    unsigned char* disk = load_disk_to_mem(file_descriptor);
    ext2_ls(disk, path, show_dots);
}
