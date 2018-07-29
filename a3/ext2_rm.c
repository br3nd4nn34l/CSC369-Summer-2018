#include "ext2_helper.h"
#include <fcntl.h>


int ext2_rm(unsigned char* disk, char* path) {
    int ret;

    List* path_components = split_path(path);
    char* file_name = (char*) listPeek(path_components);

    struct ext2_dir_entry_2* entry = traverse_path(disk, path_components);
    struct ext2_inode* inode = get_inode_from_entry(disk, entry);

    // Abort if we cannot find the entry
    if (entry == NULL) {
        printf("%s\n", "No such file or directory");
        ret = ENOENT;
    }

    // Abort if entry is a directory
    else if (is_directory(entry)){
        printf("%s\n", "Path is a directory");
        ret = EISDIR;
    }

    // Delete file or link
    else {
        // get parent diretory's entry
//        path_components->count--;
        listPop(path_components);

        struct ext2_dir_entry_2* parent_entry = traverse_path(disk, path_components);
        free_parent_inode_block(disk, parent_entry, file_name);

        inode->i_links_count--;
        if (inode->i_links_count <= 0) {
            free_file_inode(disk, entry);
        }

        ret = 0;
    }

    destroyList(path_components);
    return ret;

}

int main(int argc, char* argv[]) {

    if (argc != 3) {
        crash_with_usage(argv[0]);
    }

    char* img = argv[1];
    char* path = argv[2];

    int file_descriptor = open(img, O_RDWR);

    // Opening disk
    if (!file_descriptor) {
        fprintf(stderr, "Disk image '%s' not found.", img);
        exit(ENOENT);
    }

    unsigned char* disk = load_disk_to_mem(file_descriptor);
    ext2_rm(disk, path);
}