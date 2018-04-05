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
    char *copy_path = malloc(sizeof(char)* (strlen(path)+4));
    make_valid_path(path, copy_path);   
    path = copy_path;

    // Getting directory name to create and parent path
    char *dir_name;
    char *parent_path;
    if (strlen(path) == 0){
        fprintf(stderr,"Invalid path.\n"); 
        return ENOENT;
    } else {
        dir_name = get_name(path);
        parent_path = get_parent_path(path);
    }
    
    if (strlen(dir_name) > EXT2_NAME_LEN) {
        fprintf(stderr, "%s\n", "Error: Name too long.");
        return ENAMETOOLONG;
    }

    int parent_inode_num = get_dir_inode(parent_path);
    if (parent_inode_num == -1) {
        fprintf(stderr,"Given path does not exist.\n"); 
        return ENOENT;
    }

    struct ext2_inode *parent_inode = &inode_table[parent_inode_num];
    // Checking if the directory name to create already exists.
    if (check_exist(parent_inode, dir_name, EXT2_FT_REG_FILE) > 0 
        || check_exist(parent_inode, dir_name, EXT2_FT_DIR) > 0 
        || check_exist(parent_inode, dir_name, EXT2_FT_SYMLINK) > 0) {
        fprintf(stderr,"Already Exists.\n"); 
        return EEXIST;
    }
    // OK to create new dir, allocate inode and create entry.
    int dir_inode_num = allocate_dir_inode(parent_inode_num);
    if (dir_inode_num == -1) {
        fprintf(stderr,"Cannot allocate new inode.\n"); 
        return ENOENT;
    }
    parent_inode->i_links_count++;
    struct ext2_dir_entry* self_dir_entry = NULL;
    create_entry(self_dir_entry, dir_inode_num, parent_inode, dir_name, EXT2_FT_DIR);
    return 0;
}