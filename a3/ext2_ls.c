#include "ext2_helper.h"
#include<fcntl.h>
#include <getopt.h>

// TODO PRINTF SHOULD BE FPRINTF ON STDERR
int ext2_ls(unsigned char* disk, char* path, bool show_dots) {

    List* path_components = split_path(path);
    struct ext2_dir_entry_2* entry = traverse_path(disk, path_components);

    if (entry == NULL) {
        fprintf(stderr, "%s\n", "No such file or directory");
        exit(ENOENT);
    }

    if (is_file(entry) || is_link(entry)) {
        printf("%s\n", (char*) listPeek(path_components));
    } else if (is_directory(entry)) {
        print_dir_contents(disk, entry, show_dots);
    }

    destroyList(path_components);
    return 0;
}


int main(int argc, char* argv[]) {
    int ch = 0;
    bool show_dots = false;

    char* img = argv[1];
    char* path;

    char err_msg[100];
    sprintf(err_msg, "Usage: %s [disk] [option -a] [path]", argv[0]);

    switch(argc) {
        case 3:
            if (strcmp(argv[2], "-a") == 0) {
                crash_with_usage(err_msg);
            }
            path = argv[2];
            break;
        case 4:
            path = argv[3];
            break;
        default:
            crash_with_usage(err_msg);
    }


    while ((ch = getopt(argc, argv, "a")) != -1) {
        switch(ch) {
            case 'a':
                // option to print everything
                show_dots = true;
                break;
            default:
                crash_with_usage(err_msg);
        }
    }

    unsigned char* disk = load_disk_to_mem(img);
    ext2_ls(disk, path, show_dots);
}
