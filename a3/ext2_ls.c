#include "ext2.h"
#include "ext2_helper.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <stdbool.h>



bool is_valid_path(List* path_tokens) {
    return true;
}

int main(int argc, char* argv[]) {
    List* tokens = split_path(argv[1]);
    for (int i = 0; i < tokens->count; i++){
        printf("%s\n", tokens->contents[i]);
    }
    destroyList(tokens);
}
