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
        fprintf(stderr,"Attempting to restore directory. Not support.\n");  
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
    char *parent_path = get_parent_path(path);
    int parent_inode_num = get_dir_inode(parent_path);
    if (parent_inode_num == -1) {
        fprintf(stderr,"Directory does not exist.\n"); 
        return ENOENT;
    }

    // Make sure it does not exist.
    struct ext2_inode *parent_inode = &inode_table[parent_inode_num];
    if (check_exist(parent_inode, f_name, EXT2_FT_REG_FILE) > 0 
        || check_exist(parent_inode, f_name, EXT2_FT_DIR) > 0 
        || check_exist(parent_inode, f_name, EXT2_FT_SYMLINK) > 0) {
        fprintf(stderr,"%s alreay exists.\n", f_name); 
        return EEXIST;
    }

    // Try restore first.
    int j = 0;
    struct ext2_dir_entry *prev_entry;
    struct ext2_dir_entry *restore_entry;
    int rs_inode_num = 0;
    int actual_rec;
    int actual;
    int sum_prev_rec;
    int found = 0;
    
    // Finding the inode
    // Get the previous entry by finding the 'gap'
    while (j < EXT2_BLOCK_SIZE) {
        prev_entry = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * parent_inode->i_block[0] + j);
        actual_rec = ceil((double)(sizeof(struct ext2_dir_entry *) + prev_entry->name_len) / 4) * 4;
        // Gap found in this case
        // summing the previous reclen for later use.
        if (prev_entry->rec_len > actual_rec) {
            sum_prev_rec = 0;
            int k = j;
            while (k < (j + prev_entry->rec_len)) {
                restore_entry = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * parent_inode->i_block[0] + k);
                actual = ceil((double)(sizeof(struct ext2_dir_entry *) + restore_entry->name_len) / 4) * 4;

                char *copy_name = malloc(sizeof(char)* (restore_entry->name_len+4));
                strcpy(copy_name, restore_entry->name);
                copy_name[restore_entry->name_len] = '\0';

                if (strcmp(f_name, copy_name) == 0) {
                    if (restore_entry->file_type == EXT2_FT_DIR) {
                        fprintf(stderr,"Attempting to restore directory. Not support.\n");  
                        return EISDIR;
                    } 
                    rs_inode_num = restore_entry->inode;
                    found = 1;
                    break;
                }
                sum_prev_rec += actual;
                k += actual;
            } 
            if (found == 1) {
                break; 
            }
        }
        j += prev_entry->rec_len;
    }

    rs_inode_num -= 1;
    if (rs_inode_num <= 0) {
        fprintf(stderr,"%s does not exist.\n", f_name);
        return ENOENT;
    }
    
    struct ext2_inode *rs_inode = &inode_table[rs_inode_num];
    int block_num = 0;
    int i; 
    j = (int) rs_inode_num % 8;
    i = (int) rs_inode_num / 8;
    // Checking if this inode has been taken
    // In this case, cannot restore
    if ((inode_bitmap[i] >> j) & 1) {
        fprintf(stderr, "Error. Cannot restore.\n");
        return ENOENT;
    }

    // Checking if this data block has been taken
    // In this case, cannot restore
    while (1) {
        if (rs_inode->i_block[block_num] == 0 || block_num == 12) {
            break;
        }
        int rs_block = (int) rs_inode->i_block[block_num] - 1;
        
        j = (int) rs_block % 8;
        i = (int) (rs_block - j) / 8;
        if ((block_bitmap[i] >> j) & 1) {
            fprintf(stderr, "Error. Cannot restore.\n");
            return ENOENT;
        }
        block_num++;
    }

    int indirect_block_size = ceil((double)rs_inode->i_size/EXT2_BLOCK_SIZE) - 12;
    if (indirect_block_size > 0) {
        int k;
        int indirect_block_num = rs_inode->i_block[12];
        int *block_num_array = (int *) (disk + (indirect_block_num) * EXT2_BLOCK_SIZE);

        j = (int) (indirect_block_num-1) % 8;
        i = (int) ((indirect_block_num-1) - j) / 8;
        if ((block_bitmap[i] >> j) & 1) {
            fprintf(stderr, "Error. Cannot restore.\n");
            return ENOENT;
        }
        block_num++;

        for (k = 0; k < indirect_block_size; k++) {
            int rs_block = (int) block_num_array[k] - 1;
            j = (int) rs_block % 8;
            i = (int) (rs_block - j) / 8;

            if ((block_bitmap[i] >> j) & 1) {
                return ENOENT;
            }
            block_num++;
        }
    }

    sb->s_free_inodes_count--;
    gd->bg_free_inodes_count--;
    sb->s_free_blocks_count -= block_num;
    gd->bg_free_blocks_count -= block_num;
    // Right now, we have make sure we can restore the file.
    // Starting to recover the inode and data block
    j = (int) rs_inode_num % 8;
    i = (int) rs_inode_num / 8;
    inode_bitmap[i] = inode_bitmap[i] | (1 << j);
    block_num = 0;
    while (1) {
        if (rs_inode->i_block[block_num] == 0 || block_num == 12) {
            break;
        }
        int rs_block = (int) rs_inode->i_block[block_num] - 1;
        j = (int) rs_block % 8;
        i = (int) (rs_block - j) / 8;
        block_bitmap[i] = block_bitmap[i] | (1 << j);
        block_num++;
    }

    if (indirect_block_size > 0) {
        int k;
        int indirect_block_num = rs_inode->i_block[12];
        int *block_num_array = (int *) (disk + (indirect_block_num) * EXT2_BLOCK_SIZE);
        j = (int) (indirect_block_num-1) % 8;
        i = (int) ((indirect_block_num-1) - j) / 8;
        block_bitmap[i] = block_bitmap[i] | (1 << j);

        for (k = 0; k < indirect_block_size; k++){
            int rs_block = (int) block_num_array[k] - 1;
            j = (int) rs_block % 8;
            i = (int) (rs_block - j) / 8;
            block_bitmap[i] = block_bitmap[i] | (1 << j);   
        }
    }
    
    rs_inode->i_dtime = 0;
    rs_inode->i_links_count += 1;
    
    restore_entry->rec_len = prev_entry->rec_len - sum_prev_rec;
    prev_entry->rec_len = sum_prev_rec;
    return 0;
}