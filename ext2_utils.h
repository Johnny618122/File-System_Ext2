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

// Global variables
unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;
struct ext2_inode *inode_table;
unsigned char *block_bitmap;
unsigned char *inode_bitmap;

// Public use Functions
int init_global(int fd);
void make_valid_path(char *path, char *copy_path);
int check_exist(struct ext2_inode *parent_inode, char *name, unsigned char type);
char *get_name(char *path);
char *get_parent_path(char *path);
int allocate_block(unsigned char *bitmap, unsigned int amount);
int get_dir_inode(char* path);
void create_entry(struct ext2_dir_entry* new, int inode_num, struct ext2_inode *parent_inode, char *name, unsigned char type);

// Functions only used in the ext2_cp.c
int allocate_f_inode(int blocks_num, int size);
// Functions only used in the ext2_mkdir.c
int allocate_dir_inode(int parent_inode_num);
// Functions only used in the ext2_ln.c
int allocate_l_inode(char *src_path, int size);
// Functions only used in the ext2_rm.c
int remove_file_inode(char *f_name, int rm_inode_num, struct ext2_inode *parent_inode);
int ext2_remove(char *f_name, struct ext2_inode *parent_inode);
// Functions only used in the ext2_checker.c
int count_free(unsigned char *bitmap);
int fix_type(struct ext2_dir_entry *entry);
int fix_block(unsigned int inode_num);