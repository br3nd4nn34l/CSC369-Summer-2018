#include "ext2_helper.h"
#include<fcntl.h>
#include<sys/stat.h>

unsigned long get_file_size(FILE* file){
    long start = ftell(file);
    fseek(file, 0L, SEEK_END);
    long ret = ftell(file);
    fseek(file, start, SEEK_SET);


    return (unsigned long) ret;
}

unsigned int get_required_block_count(unsigned long f_size) {
    return (unsigned int) (f_size + EXT2_BLOCK_SIZE - 1)/ EXT2_BLOCK_SIZE;
}


//unsigned int num_nonzero_blocks(struct ext2_inode* )

int ext2_cp(unsigned char* disk, char* source, char* dest) {

    // Grab the name of the source file (if dest is
    List* src_components = split_path(source);
    char* src_name = listPop(src_components);
    destroyList(src_components);

    List* dest_components = split_path(dest);

    // Destination entry is explicit path if file name is given
    struct ext2_dir_entry_2* dest_entry = traverse_path(disk, dest_components);

    // Destination entry has inferred path if file name is not given
    if (is_directory(dest_entry)) {
        listAppend(dest_components, src_name);
        dest_entry = traverse_path(disk, dest_components);
    }

    char* dest_name = listPop(dest_components);

    // Open the file, crash if no such file
    FILE* opened_file;

    if ((opened_file = fopen(source, "r")) == NULL) {
        fprintf(stderr, "%s does not exist.\n", source);
        exit(ENOENT);
    }

    // Determine number of blocks for the file
    unsigned long f_size = get_file_size(opened_file);
    unsigned int num_blocks_needed = get_required_block_count(f_size);
    one_index* blocks = allocate_blocks(disk, num_blocks_needed);

    struct ext2_inode* inode;

    struct ext2_dir_entry_2* dst_parent_entry = traverse_path(disk, dest_components);
    if (dst_parent_entry == NULL){
        fprintf(stderr, "Parent of %s does not exist\n", dest);
        exit(EEXIST);
    }

    // File already exists
    if (dest_entry != NULL) {
        inode = get_inode_from_entry(disk, dest_entry);
    }
    else {
        if (num_blocks_needed > INODE_BLOCK_LIMIT) {
            fprintf(stderr, "Cannot fit data into single inode");
            exit(1);
        }

        one_index inode_num = allocate_file_inode(disk, (unsigned int) f_size);
        inode = get_inode(disk, inode_num);
        // inserting destination file's name into parent's directory entry
        make_entry_in_inode(disk, get_inode_from_entry(disk, dst_parent_entry), inode_num, dest_name, EXT2_FT_REG_FILE);
    }


    // Allocate inode and blocks for the file
    zero_index cur_ind = 0;
    for (zero_index i = 0; cur_ind < num_blocks_needed; i++){
        if (get_inode_block_number(disk, inode, i) == 0) {
            set_inode_block_number(disk, inode, i, blocks[cur_ind]);
            cur_ind++;
        }
    }


    unsigned char buffer[EXT2_BLOCK_SIZE];

    for (zero_index i = 0; i < INODE_BLOCK_LIMIT; i++){

        // get part of the file that be fit into one block
        size_t read_size = fread(buffer, 1, EXT2_BLOCK_SIZE, opened_file);
        if (read_size > 0) {
            // Attach block number to inode
            set_inode_block_number(disk, inode, i, blocks[i]);

            // Copy data from file into inode
            unsigned char* data_block = get_block(disk, blocks[i]);
            memcpy(data_block, buffer, read_size);
        }
        else {
            set_inode_block_number(disk, inode, i, 0);
        }

    }

    inode->i_mode = EXT2_FT_REG_FILE;
    inode->i_ctime = (unsigned int) time(0);
    inode->i_dtime = 0;
    inode->i_blocks = num_blocks_needed * 2;
    inode->i_size = (unsigned int) f_size;

    free(blocks);
    destroyList(dest_components);

    return 0;
}


int main(int argc, char* argv[]) {

    if (argc != 4) {
        fprintf(stderr, "Usage: %s [disk] [source file path] [destination file path]\n", argv[0]);
        exit(1);
    }

    char* img = argv[1];
    char* source_path = argv[2];
    char* dest_path = argv[3];


    unsigned char* disk = load_disk_to_mem(img);

    ext2_cp(disk, source_path, dest_path);
}
