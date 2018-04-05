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

    if (argc != 4 && argc !=5) {
        fprintf(stderr, "Usage: <image file name> <source file path> <linking path> -s(optional for symbolic link)\n");
        exit(EINVAL);
    }
    
    int fd = open(argv[1], O_RDWR);
    if (init_global(fd) == -1) {
        perror("mmap");
        exit(1);
    }

    // Getting the source and dest path and making some adjustments.
    char *src_path;
    char *dest_path;
    int is_symlnk = 0;
    if (argc == 4) {
        src_path = argv[2];
        dest_path = argv[3];
    }

    if (argc == 5) {
        if (strcmp(argv[2], "-s") != 0) {
            fprintf(stderr, "Usage: <image file name> <source file path> <linking path> -s(optional for symbolic link)\n");
            return EINVAL;
        }
        is_symlnk = 1;
        src_path = argv[3];
        dest_path = argv[4];
    }
    
    char *copy_s_path = malloc(sizeof(char)* (strlen(src_path)+4));
    make_valid_path(src_path, copy_s_path);
    src_path = copy_s_path;
    
    char *copy_d_path = malloc(sizeof(char)* (strlen(dest_path)+4));
    make_valid_path(dest_path, copy_d_path);
    dest_path = copy_d_path;
    
    if(strlen(src_path) == 0) {
        fprintf(stderr,"Source is a directory.\n");
        return EISDIR;
    }

    char *src_file_name = get_name(src_path);
    char *src_parent_path = get_parent_path(src_path);

    int src_parent_inode_num = get_dir_inode(src_parent_path);
    if (src_parent_inode_num == -1) {
        fprintf(stderr,"Directory of source file does not exist.\n"); 
        return ENOENT;
    }

    struct ext2_inode *src_parent_inode = &inode_table[src_parent_inode_num];
    struct ext2_dir_entry *entry;
    // Make sure source is not a directory.
    if(check_exist(src_parent_inode, src_file_name, EXT2_FT_DIR) > 0) {
        	  fprintf(stderr,"%s is a directory.\n", src_file_name);
        	  return EISDIR;
    }
    // Make sure source exists.
    if (check_exist(src_parent_inode, src_file_name, EXT2_FT_REG_FILE) == -1) {
    	  if(check_exist(src_parent_inode, src_file_name, EXT2_FT_SYMLINK) == -1) {
        	  fprintf(stderr,"%s does not exist.\n", src_file_name);
        	  return ENOENT;
        }
    }
	
    // Make sure the destination path is ok.
	char *dest_lnk_name;
    char *dest_parent_path;
	if (strlen(dest_path) == 0){
        dest_lnk_name = src_file_name;
        dest_parent_path = dest_path;
    } else {
    	  dest_lnk_name = get_name(dest_path);
        dest_parent_path = get_parent_path(dest_path);
    }
    
    if (strlen(dest_lnk_name) > EXT2_NAME_LEN) {
        fprintf(stderr, "%s\n", "Error: Name too long.");
        return ENAMETOOLONG;
    }

    int dest_parent_inode_num = get_dir_inode(dest_parent_path);
    if (dest_parent_inode_num == -1) {
        fprintf(stderr,"Directory of destination does not exist.\n"); 
        return ENOENT;
    }
    struct ext2_inode *dest_parent_inode = &inode_table[dest_parent_inode_num]; 
	if (check_exist(dest_parent_inode, dest_lnk_name, EXT2_FT_SYMLINK) > 0){
        fprintf(stderr,"Already Exists.\n"); 
        return EEXIST;
    }
    
    // Checking if the name to link already exists.
    if (check_exist(dest_parent_inode, dest_lnk_name, EXT2_FT_REG_FILE) > 0) {
        fprintf(stderr,"Already Exists.\n"); 
        return EEXIST;
    } else if (check_exist(dest_parent_inode, dest_lnk_name, EXT2_FT_DIR) > 0) {
        // if the user provide only a directory, we have to link the file
        // to the given directory path with the same name in the source path.
        dest_lnk_name = src_file_name;
        char *temp_path = malloc(sizeof(char)* (strlen(dest_path)+4)); 
        strcpy(temp_path, dest_path);
        strcat(dest_path, "/");
        dest_parent_path = temp_path;
        dest_lnk_name = get_name(src_file_name);  

        if (strlen(dest_lnk_name) > EXT2_NAME_LEN) {
            fprintf(stderr, "%s\n", "Error: Name too long.");
            return ENAMETOOLONG;
        }
        strcat(dest_path, dest_lnk_name);
        dest_parent_inode_num = get_dir_inode(dest_parent_path);
        dest_parent_inode = &inode_table[dest_parent_inode_num];
        if (dest_parent_inode_num == -1) {
            fprintf(stderr,"Unexpected error.\n"); 
            return ENOENT;
        }
        if (check_exist(dest_parent_inode, dest_lnk_name, EXT2_FT_SYMLINK) > 0) {
            fprintf(stderr,"Already Exists.\n"); 
            return EEXIST;
        }
        if (check_exist(dest_parent_inode, dest_lnk_name, EXT2_FT_REG_FILE) > 0) {
            fprintf(stderr,"Already Exists.\n"); 
            return EEXIST;
        }

    }

    // Finding the inode number to link.
    int j = 0;
    int lnk_inode_num = -1;
    while (j < EXT2_BLOCK_SIZE) {
        entry = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * src_parent_inode->i_block[0] + j);
        char *copy_name = malloc(sizeof(char)* (entry->name_len+4));
        strcpy(copy_name, entry->name);
        copy_name[entry->name_len] = '\0';
        if (strcmp(copy_name, src_file_name) == 0) {
            lnk_inode_num = entry->inode - 1;
            break;
        } 
        j += entry->rec_len;
    }

    struct ext2_dir_entry* self_dir_entry = NULL;
    // hard link
    if (is_symlnk == 0) {
        struct ext2_inode *lnk_inode = &inode_table[lnk_inode_num];
        lnk_inode->i_links_count++;
        create_entry(self_dir_entry, lnk_inode_num, dest_parent_inode, dest_lnk_name, EXT2_FT_REG_FILE);
    } else {
    // soft link
        int size = strlen(src_path);
        int symlnk_inode_num = allocate_l_inode(src_path, size);
        create_entry(self_dir_entry, symlnk_inode_num, dest_parent_inode, dest_lnk_name, EXT2_FT_SYMLINK);

    }
    return 0;
}