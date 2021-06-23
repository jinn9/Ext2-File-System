#ifndef __EXT2_HELPER_H__
#define __EXT2_HELPER_H__

#include "ext2.h"

#define EXT2_IMODE_MASK  0xf000 /* mask for imode */

extern unsigned char *disk;
extern struct ext2_super_block *sb;
extern struct ext2_group_desc *gd;
extern unsigned char *block_bitmap;
extern unsigned char *inode_bitmap;
extern struct ext2_inode *inode_table;

int max(int a, int b);
int check_allocation(unsigned char *bitmap, int num);
void set_to_used(unsigned char *bitmap, int num);
int allocate_inode();
int allocate_block();
void deallocate_inode(int inum);
void deallocate_block(int block_num);
struct ext2_dir_entry *move_entry(struct ext2_dir_entry *cur_entry, unsigned int dir_num, \
        unsigned int *block_idx, unsigned int *offset);
int search_directory(unsigned int dir_inum, char *name);
int pathwalk(char *path);
int add_new_entry(unsigned int dir_inum, struct ext2_dir_entry *new_entry);
char *find_name(char *path);
char *find_subpath(char *path);

// From below is helper functions for checker

int check_directory(unsigned int dir_inum);
int check_type(struct ext2_dir_entry *dir_entry);
int check_inode(struct ext2_dir_entry *dir_entry);
int check_dtime(struct ext2_dir_entry *dir_entry);
int check_blocks(struct ext2_dir_entry *dir_entry);

#endif
