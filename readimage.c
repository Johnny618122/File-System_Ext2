#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include "ext2.h"

unsigned char *disk;

void printBitmap(unsigned char * bitmap, unsigned int amount){
	int i; int j;
    for (i = 0; i < amount; i++) {
    	for (j = 0; j < 8; j++) {
       		printf("%d", (bitmap[i] >> j) & 1);
       }
       printf(" ");
    }
}

int main(int argc, char **argv) {

    if(argc != 2) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);

    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + EXT2_BLOCK_SIZE * 2);


    printf("Inodes: %d\n", sb->s_inodes_count);
    printf("Blocks: %d\n", sb->s_blocks_count);
    printf("Block group: \n");
    printf("    block bitmap: %d\n", gd->bg_block_bitmap);
    printf("    inode bitmap: %d\n", gd->bg_inode_bitmap);
    printf("    inode table: %d\n", gd->bg_inode_table);
    printf("    free blocks: %d\n", gd->bg_free_blocks_count);
    printf("    free inodes: %d\n", gd->bg_free_inodes_count);
    printf("    used_dirs: %d\n", gd->bg_used_dirs_count);

	unsigned char * block_bitmap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_block_bitmap);
	int block_amount = sb->s_blocks_count / 8;
    printf("Block bitmap: ");
    printBitmap(block_bitmap, block_amount);
    printf("\n");

    unsigned char * inode_bitmap = (unsigned char *) (disk + EXT2_BLOCK_SIZE * gd->bg_inode_bitmap);
    int inode_amount = sb->s_inodes_count / 8;
    printf("Inode bitmap: ");
    printBitmap(inode_bitmap, inode_amount);
    printf("\n");

    printf("Inodes:\n");
    struct ext2_inode *inode_table = (struct ext2_inode *) (disk + EXT2_BLOCK_SIZE * gd->bg_inode_table);
    
    struct ext2_inode *inode = &inode_table[EXT2_ROOT_INO - 1];
    char type;
    if (inode->i_mode & EXT2_S_IFREG) {
        type = 'f';
    } else if (inode->i_mode & EXT2_S_IFDIR) {
        type = 'd';
    } else if (inode->i_mode & EXT2_S_IFLNK) {
        type = 'l';
    }
    printf("[%d] type: %c size: %d links: %d blocks: %d\n", EXT2_ROOT_INO, type, inode->i_size, inode->i_links_count, inode->i_blocks);
    printf("[%d] Blocks: ", EXT2_ROOT_INO);
    printf(" %d", inode->i_block[0]);
    printf("\n");

    int i;
    for (i = sb->s_first_ino; i < sb->s_inodes_count; i++) {
        inode = &inode_table[i];

        int i_byte = i / 8;
        int i_byte_index = i - (8 * i_byte);
        if (((inode_bitmap[i_byte] >> i_byte_index) & 1)) {
            if (inode->i_mode & EXT2_S_IFREG) {
                type = 'f';
            } else if (inode->i_mode & EXT2_S_IFDIR) {
                type = 'd';
            } else if (inode->i_mode & EXT2_S_IFLNK) {
                type = 'l';
            }
            printf("[%d] type: %c size: %d links: %d blocks: %d\n", i+1, type, inode->i_size, inode->i_links_count, inode->i_blocks);
            printf("[%d] Blocks: ", i+1);
            printf(" %d", inode->i_block[0]);
            printf("\n");
        }
    }

    printf("Directory Blocks:\n");
    inode = &inode_table[EXT2_ROOT_INO - 1];
    printf("    DIR BLOCK NUM: %d (for inode %d)\n", inode->i_block[0], EXT2_ROOT_INO);
    int j = 0;
    struct ext2_dir_entry *entry;
    while (j < EXT2_BLOCK_SIZE) {
        entry = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * inode->i_block[0] + j);

        if (entry->file_type == EXT2_FT_REG_FILE) {
                type = 'f';
            } else if (entry->file_type == EXT2_FT_DIR) {
                type = 'd';
            } else if (entry->file_type == EXT2_FT_SYMLINK) {
                type = 'l';
            }

        printf("Inode: %d rec_len: %d name_len: %d type= %c name=%.*s\n",
            entry->inode, entry->rec_len, entry->name_len, type, entry->name_len, entry->name);
        j += entry->rec_len;
    }

    for (i = sb->s_first_ino; i < sb->s_inodes_count; i++) {
        inode = &inode_table[i];
        int i_byte = i / 8;
        int i_byte_index = i - (8 * i_byte);
        if (((inode_bitmap[i_byte] >> i_byte_index) & 1)) {
           if (inode->i_mode & EXT2_S_IFDIR) {
                printf("    DIR BLOCK NUM: %d (for inode %d)\n", inode->i_block[0], i+1);
                j = 0;
                while (j < EXT2_BLOCK_SIZE) {
                    entry = (struct ext2_dir_entry *) (disk + EXT2_BLOCK_SIZE * inode->i_block[0] + j);
                    if (entry->file_type == EXT2_FT_REG_FILE) {
                        type = 'f';
                    } else if (entry->file_type == EXT2_FT_DIR) {
                        type = 'd';
                    } else if (entry->file_type == EXT2_FT_SYMLINK) {
                        type = 'l';
                    }
                    printf("Inode: %d rec_len: %d name_len: %d type= %c name=%.*s\n",
                        entry->inode, entry->rec_len, entry->name_len, type, entry->name_len, entry->name);
                    j += entry->rec_len;
                }
            } 
        }
    }

    // char *buff = (char *)(disk + EXT2_BLOCK_SIZE * 24);
    // printf("%s\n", buff);
    return 0;
}
