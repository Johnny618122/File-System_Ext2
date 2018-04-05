#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"
#include <string.h>
#include <sys/errno.h>
#include <time.h>
#include <math.h>
#include "ext2_utils.h"

int main(int argc, char **argv) {
    if(argc != 3) {
        fprintf(stderr, "Usage: %s <image file name> <path>\n", argv[0]);
        exit(EINVAL);
    }

    int fd = open(argv[1], O_RDWR);
    if (init_global(fd) == -1) {
        perror("mmap");
        exit(1);
    }

    char *path = argv[2];
    if (path[0] == '/' && strlen(path) == 1) {
        fprintf(stderr,"Attempting to remove root directory. \n");  
        return EISDIR;
    }

    // Getting the path and making some adjustments.
    char *copy_path = malloc(sizeof(char)* (strlen(path)+4));
    make_valid_path(path, copy_path);   
    path = copy_path;

    char *f_name = get_name(path);
    if (strlen(f_name) > EXT2_NAME_LEN) {
        fprintf(stderr, "%s\n", "Error: Name too long.");
        return ENAMETOOLONG;
    }

    // Finding the parent
    char *parent_path = get_parent_path(path);
    int parent_inode_num = get_dir_inode(parent_path);
    if (parent_inode_num == -1) {
        fprintf(stderr,"Does not exist.\n"); 
        return ENOENT;
    }

    struct ext2_inode *parent_inode = &inode_table[parent_inode_num];
    ext2_remove(f_name, parent_inode);
    return 0;
}