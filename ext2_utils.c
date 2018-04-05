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

// Global variables
unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;
struct ext2_inode *inode_table;
unsigned char *block_bitmap;
unsigned char *inode_bitmap;

/*
* Initialize global variables for general use,
* including disk, super block, blocks group descriptor, inode table
* and two bitmaps for block and inode info
*/
int init_global(int fd){
	disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        return -1;
    }
    sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    gd = (struct ext2_group_desc *)(disk + EXT2_BLOCK_SIZE * 2);
    block_bitmap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_block_bitmap);
    inode_bitmap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_inode_bitmap);
    inode_table = (struct ext2_inode *) (disk + EXT2_BLOCK_SIZE * gd->bg_inode_table);
    return 1;
}

/*
* Strip the last slash of path if the path string has, 
* and add a slash for at the beginning of the string if it doesn't have
*/
void make_valid_path(char *path, char *copy_path){
    // Add a slash at the beginning
    if (path[0] != '/') {
        strcpy(copy_path, "/");
        strcat(copy_path, path);
    }
    // Strip the last slash
    if (path[strlen(path)-1] == '/') {
        strcpy(copy_path, path);
        copy_path[strlen(path)-1] = '\0';    
    }
    else{
        strcpy(copy_path, path);
    }
}

/*
* Give inode of the parent directory, file name and type,
* return inode num of the file iff there exists the file 
* with the same type and name in the directory,
* else return -1.
*/
int check_exist(struct ext2_inode *parent_inode, char *name, unsigned char type) {
    struct ext2_dir_entry *entry;
    int j = 0;
    // Loop over the parent directory inode to check the file exists or not.
    while (j < EXT2_BLOCK_SIZE) {
        entry = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * parent_inode->i_block[0] + j);
        char *copy_name = malloc(sizeof(char)* (entry->name_len+4));
        strcpy(copy_name, entry->name);
        copy_name[entry->name_len] = '\0';
        if (strcmp(copy_name, name) == 0 && entry->file_type == type) {
            return entry->inode-1;
        } 
        j += entry->rec_len;
    }
    return -1;
}

/*
* Give the path string, return the name of the file.
*/
char *get_name(char *path) {
    char *copy_path = malloc(sizeof(char)* (strlen(path)+4));
    strcpy(copy_path, path);
    char *token = strtok(copy_path, "/");
    char *result;
    while (token != NULL) {
        result = token;
        token = strtok(NULL, "/");
    } return result;
}

/*
* Give the path string, return the path of the parent directory.
*/
char *get_parent_path(char *path) {
    int total_len = strlen(path);
    char *base_name = get_name(path);
    int name_len = strlen(base_name);
    char *copy_path = malloc(sizeof(char)* (strlen(path)+4));
    strcpy(copy_path, path);
    copy_path[total_len-name_len] = '\0';
    return copy_path;
}

/*
* Allocate a block for use and change the block bitmap and the free block counter,
* return the allocated block number if successed else return -1.
*/
int allocate_block(unsigned char *bitmap, unsigned int amount) {
    int i; int j;
    for (i = 0; i < amount; i++) {
        for (j = 0; j < 8; j++) {
            // Loop over the bitmap to find the first free block
            if (((bitmap[i] >> j) & 1) == 0) {
                // Change the bitmap and the free counter
                block_bitmap[i] = block_bitmap[i] | (1 << j);
                sb->s_free_blocks_count--;
                gd->bg_free_blocks_count--;
                return i*8+j;
           }
        }
    } return -1;
}

/*
* Given the path, return the inode number of the directory wih the path,
* if the path is invalid, return -1.
*/
int get_dir_inode(char* path) {
    char *copy_path = malloc(sizeof(char)* (strlen(path)+4));
    strcpy(copy_path, path);
    char *token = strtok(copy_path, "/");
    char *result;
    int inode_num = EXT2_ROOT_INO - 1;
    struct ext2_inode *inode;
    // Parsing based on slash，start at root directory
    // Loop over to find each level of folder 
    while (token != NULL) {
        result = token;
        int found = 0;       
        inode = &inode_table[inode_num];
        struct ext2_dir_entry *entry;
        int j = 0;
        while (j < EXT2_BLOCK_SIZE) {
            entry = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * inode->i_block[0] + j);
            // Finaly find the final level directory
            if (entry->file_type == EXT2_FT_DIR && strcmp(entry->name, result) == 0) {
                 found = 1;
                 inode_num = entry->inode - 1;
            } 
            j += entry->rec_len;
        }
        if (!found) {
            return -1;
        } 
        token = strtok(NULL, "/");
    }
    return inode_num;
}

/*
* Create a new entry in the disk with the given inode number, 
* parent inode, file name and file type.
*/
void create_entry(struct ext2_dir_entry* new, int inode_num, struct ext2_inode *parent_inode, char *name, unsigned char type){
	struct ext2_dir_entry* p_dir_entry;
    int j = 0;
    while (j < EXT2_BLOCK_SIZE) {
        p_dir_entry = (struct ext2_dir_entry *) (disk + (parent_inode->i_block[0]) * EXT2_BLOCK_SIZE + j);
        j += p_dir_entry->rec_len;
    }
    int prev_len = EXT2_BLOCK_SIZE - p_dir_entry->rec_len;
    // Calculate the rec length for the new entry
    int p_rec_len = ceil((double)(sizeof(struct ext2_dir_entry *) + p_dir_entry->name_len) / 4) * 4;
    // Update the rec length of the file entry in front
    p_dir_entry->rec_len = p_rec_len;
    int offset = prev_len + p_dir_entry->rec_len;
    // Update the entry information
    new = (struct ext2_dir_entry *) (disk + (parent_inode->i_block[0]) * EXT2_BLOCK_SIZE + offset);
    new->inode = inode_num + 1;
    new->rec_len = EXT2_BLOCK_SIZE - (prev_len + p_dir_entry->rec_len);  
    new->name_len = strlen(name);
    new->file_type = type;
    strcpy(new->name, name);
}

/*
* Allocate inode for the file and change the inode bitmap and the free inode counter.
* Call allocate_block to allocate blocks for the file, including indirect blocks.
* Return the allocated inode number if successed else return -1.
*/
int allocate_f_inode(int blocks_num, int size) {
    int inode_num;
    struct ext2_inode *inode;
    // Loop over the inode table from the inode we can use
    for (inode_num = sb->s_first_ino; inode_num < sb->s_inodes_count; inode_num++) {
        inode = &inode_table[inode_num];
        int i_byte = inode_num / 8;
        int i_byte_index = inode_num - (8 * i_byte);
        // Find the first free inode
        if (!((inode_bitmap[i_byte] >> i_byte_index) & 1)) {
            inode_bitmap[i_byte] = inode_bitmap[i_byte] | (1 << i_byte_index);
            int b;
            int blocks_need = blocks_num;
            int free_block;
            // Allocate direct blocks for the file
            for (b = 0; b < 12; b ++){
                free_block = allocate_block(block_bitmap, sb->s_blocks_count / 8);
                if (free_block == -1) {
                    fprintf(stderr,"Cannot allocate new free block.\n"); 
                    return -1;
                }
                inode->i_block[b] = free_block + 1;
                blocks_need --;
                if (blocks_need == 0) {
                    inode->i_block[b+1] = 0;
                    break;
                }
            }
            // Allocate indirect blocks if it needs
            if (blocks_need > 0) {
                // Allocate the block for saving indirect block num
                free_block = allocate_block(block_bitmap, sb->s_blocks_count / 8);
                if (free_block == -1) {
                    fprintf(stderr,"Cannot allocate new free block.\n"); 
                    return -1;
                }
                inode->i_block[12] = free_block + 1;
                int *block_array = (int *) (disk + EXT2_BLOCK_SIZE * inode->i_block[12]);
                for (b = 0; b < EXT2_BLOCK_SIZE/sizeof(int); b++) {
                    free_block = allocate_block(block_bitmap, sb->s_blocks_count / 8);
                    if (free_block == -1) {
                        fprintf(stderr,"Cannot allocate new free block.\n"); 
                        return -1;
                    }
                    block_array[b] = free_block + 1;
                    blocks_need --;
                    if (blocks_need == 0) {
                        break;
                    }
                }
            }
            int i_blocks;
            // Calculate the i_blocks
            if (blocks_num > 12) {
                i_blocks = (blocks_num + 1) * 2;
            } else {
                i_blocks = blocks_num * 2;
            }
            // Update the inode information
            inode->i_mode = EXT2_S_IFREG;
            inode->i_uid = 0; 
            inode->i_gid = 0; 
            inode->i_size = size;  
            inode->i_links_count = 1; 
            inode->osd1 = 0;
            inode->i_blocks = i_blocks;
            inode->i_dtime = 0;
            // Decrease the free inode counter
            sb->s_free_inodes_count--;
            gd->bg_free_inodes_count--;
            return inode_num;
        }
     }
     return -1;
}

/*
* Allocate inode for the directory and change the inode bitmap and the free inode counter.
* Call allocate_block to allocate one block for the directory.
* Return the allocated inode number if successed else return -1.
*/
int allocate_dir_inode(int parent_inode_num) {
    int inode_num;
    struct ext2_inode *inode;
    // Loop over the inode table from the inode we can use
    for (inode_num = sb->s_first_ino; inode_num < sb->s_inodes_count; inode_num++) {
    	inode = &inode_table[inode_num];
        int i_byte = inode_num / 8;
        int i_byte_index = inode_num - (8 * i_byte);
        // Find the first free inode
        if (!((inode_bitmap[i_byte] >> i_byte_index) & 1)) {
            inode_bitmap[i_byte] = inode_bitmap[i_byte] | (1 << i_byte_index);
            // Allocate a block for the directory
            int free_block = allocate_block(block_bitmap, sb->s_blocks_count / 8);
            if (free_block == -1) {
                fprintf(stderr,"Cannot allocate new free block.\n"); 
                return -1;
            }
            // Update the inode information
            inode->i_mode = EXT2_S_IFDIR;
            inode->i_uid = 0; 
            inode->i_gid = 0; 
            inode->i_size = EXT2_BLOCK_SIZE;  
            inode->i_links_count = 2; 
            inode->osd1 = 0;
            inode->i_block[0] = free_block + 1; 
            inode->i_blocks = 2;
            inode->i_dtime = 0;
            // Create the "." entry for the new directory
            struct ext2_dir_entry* dir_entry1 = (struct ext2_dir_entry *) (disk + (inode->i_block[0]) * EXT2_BLOCK_SIZE);     
            dir_entry1->inode = inode_num + 1;
            dir_entry1->rec_len = 12;  
            dir_entry1->name_len = 1;
            dir_entry1->file_type = EXT2_FT_DIR;
            dir_entry1->name[0] = '.';
            // Create the ".." entry for the new directory
            struct ext2_dir_entry* dir_entry2 = (struct ext2_dir_entry *) (disk + (inode->i_block[0]) * EXT2_BLOCK_SIZE + 12);
            dir_entry2->inode = parent_inode_num + 1;
            dir_entry2->rec_len = 1012;  
            dir_entry2->name_len = 2;
            dir_entry2->file_type = EXT2_FT_DIR;
            dir_entry2->name[0] = '.';
            dir_entry2->name[1] = '.';
            // Decrease the free inode counter
            sb->s_free_inodes_count--;
            gd->bg_free_inodes_count--;
            // Increase the used dir counter
            gd->bg_used_dirs_count++;

            return inode_num;
        }
    }
	return -1;
}

/*
* Allocate inode for the symbolic link, change the inode bitmap and the free inode counter.
* Call allocate_block to allocate one block for the link.
* Return the allocated inode number if successed else return -1.
*/
int allocate_l_inode(char *src_path, int size) {
    int inode_num;
    struct ext2_inode *inode;
    // Loop over the inode table from the inode we can use
    for (inode_num = sb->s_first_ino; inode_num < sb->s_inodes_count; inode_num++) {
        inode = &inode_table[inode_num];
        int i_byte = inode_num / 8;
        int i_byte_index = inode_num - (8 * i_byte);
        // Find the first free inode
        if (!((inode_bitmap[i_byte] >> i_byte_index) & 1)) {
            inode_bitmap[i_byte] = inode_bitmap[i_byte] | (1 << i_byte_index);
            // Allocate a block for the link
            int free_block = allocate_block(block_bitmap, sb->s_blocks_count / 8);
            if (free_block == -1) {
                fprintf(stderr,"Cannot allocate new free block.\n"); 
                return -1;
            }
            char *buff = (char *) (disk + (free_block + 1) * EXT2_BLOCK_SIZE);
            strcpy(buff, src_path);
            // Update the inode information
            inode->i_mode = EXT2_S_IFLNK;
            inode->i_uid = 0; 
            inode->i_gid = 0; 
            inode->i_size = size;  
            inode->i_links_count = 1; 
            inode->osd1 = 0;
            inode->i_blocks = 2;
            inode->i_block[0] = free_block + 1;
            inode->i_dtime = 0;
            // Decrease the free inode counter
            sb->s_free_inodes_count--;
            gd->bg_free_inodes_count--;
            return inode_num;
        }
    }
    return -1;
}

/*
* Remove inode with the inode number and parent inode, update the inode bitmap and the free inode counter.
* Also remove the blocks which used for this inode, and change the rec length for the previous entry.
* Return the allocated inode number if successed else return -1.
*/
int remove_file_inode(char *f_name, int rm_inode_num, struct ext2_inode *parent_inode) {
    struct ext2_inode *rm_inode = &inode_table[rm_inode_num];
    int block_num = 0;
    int i; int j;
    j = (int) rm_inode_num % 8;
    i = (int) rm_inode_num / 8;
    // Mark the inode free in the bitmap 
    inode_bitmap[i] = inode_bitmap[i] & ~(1 << j);
    // Update the free inode counter
    sb->s_free_inodes_count++;
    gd->bg_free_inodes_count++;
    if (rm_inode->i_block[0] == 0) {
        return -1;
    }
    // Remove the direct blocks
    while (1) {
        if (rm_inode->i_block[block_num] == 0 || block_num == 12) {
            break;
        }
        int remove_block = (int) rm_inode->i_block[block_num] - 1;
        j = (int) remove_block % 8;
        i = (int) (remove_block - j) / 8;
        // Update the block bitmap and free block counter
        block_bitmap[i] = block_bitmap[i] & ~(1 << j);
        sb->s_free_blocks_count++;
        gd->bg_free_blocks_count++;
        block_num++;
    }
    // Remove the indirect blocks
    int indirect_block_size = ceil((double)rm_inode->i_size/EXT2_BLOCK_SIZE) - 12;
    if (indirect_block_size > 0) {
        int k;
        int indirect_block_num = rm_inode->i_block[12];
        int *block_num_array = (int *) (disk + (indirect_block_num) * EXT2_BLOCK_SIZE);
        j = (int) (indirect_block_num-1) % 8;
        i = (int) ((indirect_block_num-1) - j) / 8;
        // Remove the block, which saves the indirect block numbers 
        block_bitmap[i] = block_bitmap[i] & ~(1 << j);
        sb->s_free_blocks_count++;
        gd->bg_free_blocks_count++;
        // Mark the indirect used blocks free
        for (k = 0; k < indirect_block_size; k++){
            int remove_block = (int) block_num_array[k] - 1;
            j = (int) remove_block % 8;
            i = (int) (remove_block - j) / 8;
            // Update the block bitmap and free block counter
            block_bitmap[i] = block_bitmap[i] & ~(1 << j);
            sb->s_free_blocks_count++;
            gd->bg_free_blocks_count++;
        }
    }
    struct ext2_dir_entry* p_dir_entry;
    int prev_entry;
    j = 0;
    // Find the previous entry of the removed entry
    while (j < EXT2_BLOCK_SIZE) {
        p_dir_entry = (struct ext2_dir_entry *) (disk + (parent_inode->i_block[0]) * EXT2_BLOCK_SIZE + j);
        char *copy_name = malloc(sizeof(char)* (p_dir_entry->name_len+4));
        strcpy(copy_name, p_dir_entry->name);
        copy_name[p_dir_entry->name_len] = '\0';
        if (strcmp(copy_name, f_name) == 0) {
            break;
        }
        prev_entry = j;
        j += p_dir_entry->rec_len;
    }
    // Update the rec length of the previous entry
    // If the removed entry is the last one in the inode.
    if ((j + p_dir_entry->rec_len) == 1024) {
        p_dir_entry = (struct ext2_dir_entry *) (disk + (parent_inode->i_block[0]) * EXT2_BLOCK_SIZE + prev_entry);
        int p_rec_len = EXT2_BLOCK_SIZE - prev_entry;
        p_dir_entry->rec_len = p_rec_len;
    } else {
    // If the removed entry is not the last one in the inode.
        struct ext2_dir_entry* next_dir_entry = (struct ext2_dir_entry *) (disk + (parent_inode->i_block[0]) * EXT2_BLOCK_SIZE + j + p_dir_entry->rec_len);
        struct ext2_dir_entry* prev_dir_entry = (struct ext2_dir_entry *) (disk + (parent_inode->i_block[0]) * EXT2_BLOCK_SIZE + prev_entry);
        prev_dir_entry->rec_len += p_dir_entry->rec_len;
        // Update the next entry information
        p_dir_entry = (struct ext2_dir_entry *) (disk + (parent_inode->i_block[0]) * EXT2_BLOCK_SIZE + prev_entry + prev_dir_entry->rec_len);
        p_dir_entry->inode = next_dir_entry->inode;
        p_dir_entry->rec_len = next_dir_entry->rec_len;  
        p_dir_entry->name_len = next_dir_entry->name_len;
        p_dir_entry->file_type = next_dir_entry->file_type;
        char *copy_name = malloc(sizeof(char) * strlen(next_dir_entry->name) + 4);
        strcpy(copy_name, next_dir_entry->name);
        copy_name[next_dir_entry->name_len] = '\0';
        strcpy(p_dir_entry->name, copy_name);
    }
    return 0;
}

/*
* Remove the file with the file name and the parent inode.
* Remove file and link directly as the base case,
* remove directory by recursion for all files in the directory.
*/
int ext2_remove(char *f_name, struct ext2_inode *parent_inode) {
    int rm_inode_num;
    // Base case:
    // The "file" is regular file or link, call remove_file_inode directly.
    if ((rm_inode_num = check_exist(parent_inode, f_name, EXT2_FT_REG_FILE)) > 0) {
        remove_file_inode(f_name, rm_inode_num, parent_inode);
        return 1;
    } else if ((rm_inode_num = check_exist(parent_inode, f_name, EXT2_FT_SYMLINK)) > 0) {
        remove_file_inode(f_name, rm_inode_num, parent_inode);
        return 1;
    } else if ((rm_inode_num = check_exist(parent_inode, f_name, EXT2_FT_DIR)) > 0) {
    // Recursion:
    // The "file" is directroy, for each file in the directory call the function itself.
        struct ext2_dir_entry *entry;
        int j = 0;
        struct ext2_inode *curr_inode = &inode_table[rm_inode_num];
        while (j < EXT2_BLOCK_SIZE) {
            entry = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * curr_inode->i_block[0] + j);
            char *copy_name = malloc(sizeof(char)* (entry->name_len+4));
            strcpy(copy_name, entry->name);
            copy_name[entry->name_len] = '\0';
            if (entry->name_len > 2) {
                ext2_remove(copy_name, curr_inode);
            }
            j += entry->rec_len;
        }
        // Finally, remove the directory inode.
        remove_file_inode(f_name, rm_inode_num, parent_inode);
    } else {
        fprintf(stderr,"Does not exist.\n"); 
    }
    return ENOENT;
}

/*
* Part a
* Count the free bit number with the given bitmap.
* (Available for both inode bitmap and block bitmap)
*/
int count_free(unsigned char *bitmap){
    int amount = sb->s_blocks_count / 8;
    int i; int j;
    int free_num;
    free_num = 0;
    for (i = 0; i < amount; i++) {
        for (j = 0; j < 8; j++) {
            if (((bitmap[i] >> j) & 1) == 0) {
                free_num += 1;
           }
        }
    }
    return free_num;
}

/*
* Part b
* Fix the type information of the entry compared with i_mode
* Return the fixed inode number, return -1 if there is nothing to fix.
*/
int fix_type(struct ext2_dir_entry *entry) {
    int inode_num = entry->inode;
    struct ext2_inode *inode = &inode_table[inode_num - 1];
    if (inode->i_mode & EXT2_S_IFDIR) {
        // If the actual file type is directory.
        if (entry->file_type != EXT2_FT_DIR) {
            entry->file_type = EXT2_FT_DIR;
            return inode_num;
        }
    } else if (inode->i_mode & EXT2_S_IFREG) {
        // If the actual file type is file.
        if (entry->file_type != EXT2_FT_REG_FILE) {
            entry->file_type = EXT2_FT_REG_FILE;
            return inode_num;
        }
    } else if (inode->i_mode & EXT2_S_IFLNK) {
        // If the actual file type is link.
        if (entry->file_type != EXT2_FT_SYMLINK) {
            entry->file_type = EXT2_FT_REG_FILE;
            return inode_num;
        }
    }
    return -1;
}

/*
* Part e
* Fix the block use information of the inode with the given inode number
* Return the fixed times, return 0 if there is nothing to fix.
*/
int fix_block(unsigned int inode_num) {
    struct ext2_inode *inode = &inode_table[inode_num];
    int index = 0;
    int i;
    int j;
    int fix = 0;
    // Fix the direct blocks
    if (inode->i_block[index] == 0) {
        return 0;
    }
    while (1) {
        if (inode->i_block[index] == 0 || index == 12) {
            break;
        }
        int block_num = (int) inode->i_block[index] - 1;
        j = (int) block_num % 8;
        i = (int) (block_num - j) / 8;
        // If the direct used blocks are marked free, change it in used
        if (!((block_bitmap[i] >> j) & 1)) {
            block_bitmap[i] = block_bitmap[i] | (1 << j);
            fix++;
        }
        index++;
    }
    // Fix the indirect blocks
    int indirect_block_size = ceil((double)inode->i_size/EXT2_BLOCK_SIZE) - 12;
    if (indirect_block_size > 0) {
        int k;
        int indirect_block_num = inode->i_block[12] - 1;
        int *block_num_array = (int *) (disk + (indirect_block_num+1) * EXT2_BLOCK_SIZE);
        j = (int) indirect_block_num % 8;
        i = (int) (indirect_block_num - j) / 8;
        // If the block, which saves the indriect block numbers, is marked free, change it in used
        if (!((block_bitmap[i] >> j) & 1)) {
            block_bitmap[i] = block_bitmap[i] | (1 << j);
            fix++;
        }
        // If the indirect used blocks are marked free, change it in used
        for (k = 0; k < indirect_block_size; k++){
            int block = (int) block_num_array[k] - 1;
            j = (int) block % 8;
            i = (int) (block - j) / 8;
            if (!((block_bitmap[i] >> j) & 1)) {
                block_bitmap[i] = block_bitmap[i] | (1 << j);
                fix++;
            }
        }
    }
    return fix;
}