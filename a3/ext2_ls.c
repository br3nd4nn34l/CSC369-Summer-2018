#include "ext2_helper.h"
#include<fcntl.h>




int main(int argc, char* argv[]) {
    int file_descriptor = open(argv[1], O_RDWR);
    unsigned char* disk = load_disk_to_mem(file_descriptor);
    ls_proto(disk, argv[2]);
}
