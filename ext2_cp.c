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

    if(argc != 4) {
        fprintf(stderr, "Usage: %s <original path> <destination path>\n", argv[0]);
        exit(1);
    }
    
    int fd = open(argv[1], O_RDWR);
    if (init_global(fd) == -1) {
        perror("mmap");
        exit(1);
    }

    // Open the local file
    char *source_path = argv[2];
    FILE *source;
    struct stat statbuf;
    if (lstat(source_path, &statbuf)) {
        perror("lstat");
        exit(1);
    }
    if (!S_ISREG(statbuf.st_mode)) {
        fprintf(stderr, "ERROR: Not a reg file.\n");  
        return ENOENT;
    }
    int size = (int) statbuf.st_size;
    int blocks_num = ceil((double)size/EXT2_BLOCK_SIZE);
    if (blocks_num > sb->s_free_blocks_count) {
        fprintf(stderr, "ERROR: File is too big.\n");
        return EFBIG;
    }
    source = fopen(source_path,"rb");
    if (source == NULL){
        fprintf(stderr, "ERROR: In valid source path.\n");     
        return(ENOENT);
    }
    
    // Getting file name to copy and parent path
    char *path = argv[3];
    char *copy_path = malloc(sizeof(char)* (strlen(path)+4));
    make_valid_path(path, copy_path);   
    path = copy_path;
    char *f_name;
    char *parent_path;
    if (strlen(path) == 0){
        f_name = get_name(source_path);
        parent_path = path;
    } else {
    	f_name = get_name(path);
        parent_path = get_parent_path(path);
    }
    
    if (strlen(f_name) > EXT2_NAME_LEN) {
        fprintf(stderr, "%s\n", "Error: Name too long.");
        return ENAMETOOLONG;
    }

    int parent_inode_num = get_dir_inode(parent_path);
    if (parent_inode_num == -1) {
        fprintf(stderr,"Destination does not exist.\n"); 
        return ENOENT;
    }
    // Checking if the name to copy already exists.
    struct ext2_inode *parent_inode = &inode_table[parent_inode_num];
    if (check_exist(parent_inode, f_name, EXT2_FT_REG_FILE) > 0) {
        fprintf(stderr,"Already Exists.\n"); 
        return EEXIST;
    } else if (check_exist(parent_inode, f_name, EXT2_FT_DIR) > 0) {
        // if the user provide only a directory, we have to copy the file
        // to the given directory path with the same name in the source path.
        char *temp_path = malloc(sizeof(char)* (strlen(path)+4)); 
        strcpy(temp_path, path);
        strcat(temp_path, "/");
        parent_path = temp_path;
        f_name = get_name(source_path);

        if (strlen(f_name) > EXT2_NAME_LEN) {
            fprintf(stderr, "%s\n", "Error: Name too long.");
            return ENAMETOOLONG;
        }

        parent_inode_num = get_dir_inode(parent_path);
        parent_inode = &inode_table[parent_inode_num];
        if (parent_inode_num == -1) {
            fprintf(stderr,"Unexpected error.\n"); 
            return ENOENT;
        }
        if (check_exist(parent_inode, f_name, EXT2_FT_REG_FILE) > 0) {
            fprintf(stderr,"Already Exists.\n"); 
            return EEXIST;
        }
    }
    // OK to copy, allocate inode and create entry.
    int f_inode_num = allocate_f_inode(blocks_num, size);
    if (f_inode_num == -1) {
        fprintf(stderr,"Cannot allocate new inode.\n"); 
        return ENOENT;
    }
    struct ext2_dir_entry* self_dir_entry = NULL;
    create_entry(self_dir_entry, f_inode_num, parent_inode, f_name, EXT2_FT_REG_FILE);

    // copy the file to the corresponding data block using fread.
    struct ext2_inode *f_inode = &inode_table[f_inode_num];    
    int f_block_num = 0;
    while (1) {
        if (f_inode->i_block[f_block_num] == 0 || f_block_num == 12) {
            break;
        }
        char *buff = (char *) (disk + (f_inode->i_block[f_block_num]) * EXT2_BLOCK_SIZE);
        fread(buff, 1, EXT2_BLOCK_SIZE, source);
        f_block_num++;
    }

    int indirect_block_size = ceil((double)f_inode->i_size/EXT2_BLOCK_SIZE) - 12;
    if (indirect_block_size > 0) {
        int k;
        int indirect_block_num = f_inode->i_block[12];
        int *block_num_array = (int *) (disk + (indirect_block_num) * EXT2_BLOCK_SIZE);
        for (k = 0; k < indirect_block_size; k++){
            char *buff = (char *) (disk + (block_num_array[k]) * EXT2_BLOCK_SIZE);
            fread(buff, 1, EXT2_BLOCK_SIZE, source);
        }
    }
    fclose(source);
    return 0;
}