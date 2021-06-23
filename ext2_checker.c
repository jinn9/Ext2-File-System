#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include "ext2.h"
#include "ext2_helper.h"

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;
unsigned char *block_bitmap;
unsigned char *inode_bitmap;
struct ext2_inode *inode_table;

int main(int argc, char *argv[]){

    if(argc != 2) {
        fprintf(stderr, "Usage: ext2_checker <image file name>\n");
        return -1;
    }

    /* Intiailize disk and other structures */

    int image_fd = open(argv[1], O_RDWR);
    if(image_fd == -1) {
        perror("open");
        return -1;
    }

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, image_fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    sb = (struct ext2_super_block *)(disk + 1024);
    gd = (struct ext2_group_desc*)(disk + 2048);
    block_bitmap = disk + 3072;
    inode_bitmap = disk + 4096;
    inode_table = (struct ext2_inode *)(disk + 5120);

    /* Check inconsistencies */

    int total = 0;  // total number of inconsistencies

    // Part a
    int bitmap_free_inodes_count = 0;
    int bitmap_free_blocks_count = 0;
    int diff;

    // Count the number of free inodes in the bitmap
    int i;
    for(i = 1; i <= sb->s_inodes_count; i++) {
       if(check_allocation(inode_bitmap, i) == 0) 
           bitmap_free_inodes_count++;
    }

    for(i = 1; i <= sb->s_blocks_count; i++) {
        if(check_allocation(block_bitmap, i) == 0)
            bitmap_free_blocks_count++;
    }
    
    if(sb->s_free_inodes_count != bitmap_free_inodes_count) {
        diff = abs(sb->s_free_inodes_count - bitmap_free_inodes_count);        
        printf("Fixed: superblock's free inodes was off by %d compared to the bitmap\n", diff);
        sb->s_free_inodes_count = bitmap_free_inodes_count;        
        total += diff;
    }

    if(sb->s_free_blocks_count != bitmap_free_blocks_count) {
        diff = abs(sb->s_free_blocks_count - bitmap_free_blocks_count);
        printf("Fixed: superblock's free blocks was off by %d compared to the bitmap\n", diff);
        sb->s_free_blocks_count = bitmap_free_blocks_count;
        total += diff;
    }

    if(gd->bg_free_inodes_count != bitmap_free_inodes_count) {
        diff = abs(gd->bg_free_inodes_count - bitmap_free_inodes_count);
        printf("Fixed: block group's free inodes was off by %d compared to the bitmap\n", diff);
        gd->bg_free_inodes_count = bitmap_free_inodes_count;
        total += diff;
    }

    if(gd->bg_free_blocks_count != bitmap_free_blocks_count) {        
        diff = abs(gd->bg_free_blocks_count - bitmap_free_blocks_count);
        printf("Fixed: block group's free blocks was off by %d compared to the bitmap\n", diff);
        gd->bg_free_blocks_count = bitmap_free_blocks_count;
        total += diff;
    }


    // Traverse each entry in the root direcotry and fix corrupted files
    total += check_directory(2);


    if(total > 0) 
        printf("%d file system inconsistencies repaired!\n", total);
    else 
        printf("No file system inconsistencies detected!\n");
    

    return 0;
}    
  
