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
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <virtual disk>\n", argv[0]);
        exit(1);
    }

    // Initialize global variables.
    // Including disk, super block, blocks group descriptor, inode table
    // and two bitmaps for blocks and inodes
    int fd = open(argv[1], O_RDWR);
    if (init_global(fd) == -1) {
        perror("mmap");
        exit(1);
    }

    int total = 0;
    // a: check free blocks/inodes counter
    int diff;
    // Count the free block number
    int free_num = count_free(block_bitmap);
    if (free_num != sb->s_free_blocks_count) {
        diff = free_num - sb->s_free_blocks_count;
        diff = (int) diff;
        diff = abs(diff);
        printf("Fixed: superblock's free blocks counter was off by %d compared to the bitmap\n", diff);
        sb->s_free_blocks_count = free_num;
        total += diff;
    }
    // Count the free inode number
    free_num = count_free(inode_bitmap);
    if (free_num != sb->s_free_inodes_count) {
        diff = free_num - sb->s_free_inodes_count;
        diff = (int) diff;
        diff = abs(diff);
        printf("Fixed: superblock's free inodes counter was off by %d compared to the bitmap\n", diff);
        sb->s_free_inodes_count = free_num;
        total += diff;
    }
    // Count the free inode number
    free_num = count_free(block_bitmap);
    if (free_num != gd->bg_free_blocks_count) {
        diff = free_num - gd->bg_free_blocks_count;
        diff = (int) diff;
        diff = abs(diff);
        printf("Fixed: block group's free blocks counter was off by %d compared to the bitmap\n", diff);
        gd->bg_free_blocks_count = free_num;
        total += diff;
    }
    // Count the free inode number
    free_num = count_free(inode_bitmap);
    if (free_num != gd->bg_free_inodes_count) {
        diff = free_num - gd->bg_free_inodes_count;
        diff = (int) diff;
        diff = abs(diff);
        printf("Fixed: block group's free inodes counter was off by %d compared to the bitmap\n", diff);
        gd->bg_free_inodes_count = free_num;
        total += diff;
    }

    // b: check type (i_mode and file_type)  
    int i;
    int j;
    struct ext2_inode *inode = &inode_table[EXT2_ROOT_INO - 1];
    struct ext2_dir_entry *entry;
    int inode_num;
    j = 0;
    // Loop over the root directory to check the type
    while (j < EXT2_BLOCK_SIZE) {
        entry = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * inode->i_block[0] + j);
        inode_num = fix_type(entry);
        if (inode_num != -1) {
            printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", inode_num);
            total += 1;
        }
        j += entry->rec_len;
    }
    // Loop over the inode table
    for (i = sb->s_first_ino; i < sb->s_inodes_count; i++) {
        inode = &inode_table[i];
        // If the inode is in used.
        // Check the entries in this inode.
        if (inode->i_size) {
           if (inode->i_mode & EXT2_S_IFDIR) {
                j = 0;
                // Loop over the used inode to check the type of each entry.
                while (j < EXT2_BLOCK_SIZE) {
                    entry = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * inode->i_block[0] + j);
                    inode_num = fix_type(entry); 
                    if (inode_num != -1) {
                        printf("Fixed: Entry type vs inode mismatch: inode [%d]\n", inode_num);
                        total += 1;
                    }
                    j += entry->rec_len;
                }
            } 
        }
    }
    
    // c: check inode bitmap
    int i_byte;
    int i_byte_index;
    // Loop over the inode table from the root inode
    for (i = EXT2_ROOT_INO - 1; i < sb->s_inodes_count; i++) {
        inode = &inode_table[i];
        // If the inode is in used.
        // Check the entries in this inode.
        if (inode->i_size) {
           if (inode->i_mode & EXT2_S_IFDIR) {
                j = 0;
                while (j < EXT2_BLOCK_SIZE) {
                    entry = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * inode->i_block[0] + j);
                    inode_num = entry->inode - 1; 
                    i_byte = (int) inode_num / 8;
                    i_byte_index = (int) inode_num % 8;
                    // If the inode is in used, but it is marked free,
                    // then fix it, record the total fixed times.
                    if (!((inode_bitmap[i_byte] >> i_byte_index) & 1)) {
                        inode_bitmap[i_byte] = inode_bitmap[i_byte] | (1 << i_byte_index);
                        printf("Fixed: inode [%d] not marked as in-use\n", inode_num);
                        sb->s_free_inodes_count--;
                        gd->bg_free_inodes_count--;
                        total += 1;
                    }
                    j += entry->rec_len;
                }
            } 
        }
    }
   
    // d: check i_dtime
    // Loop over the inode table from the root inode
    for (i = EXT2_ROOT_INO - 1; i < sb->s_inodes_count; i++){
        i_byte = (int) i / 8;
        i_byte_index = (int) i % 8;
        // If the inode is in used in bitmap.
        // Check the dtime and fix it if needed.
        if ((inode_bitmap[i_byte] >> i_byte_index) & 1) {
            inode = &inode_table[i];            
            if (inode->i_dtime != 0) {
                printf("Fixed: valid inode marked for deletion: [%d]\n", i+1);
                inode->i_dtime = 0;
                total += 1;
            }
        }
    }

    // e: check block bitmap
    for (i = EXT2_ROOT_INO - 1; i < sb->s_inodes_count; i++) {
        inode = &inode_table[i];
        // If the inode is in used.
        if (inode->i_size) {
            // Try to fix the blocks used of the inode.
            int block = fix_block(i);
            // If some blocks have been fixed,
            // update the counter and total fixed times.
            if (block != 0) {
                printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", block, i+1);
                sb->s_free_blocks_count -= block;
                gd->bg_free_blocks_count -= block;
                total += 1;
            }
        }
    }

    if (total == 0) {
        printf("No file system inconsistencies detected!\n");
    } else {
        printf("%d file system inconsistencies repaired!\n", total);
    }  
}