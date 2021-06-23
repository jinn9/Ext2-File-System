#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <string.h>
#include "ext2.h"

unsigned char *disk;

int check_allocation(unsigned char *bitmap, int pos);

int main(int argc, char **argv) {

    if(argc != 2) {
        fprintf(stderr, "Usage: readimg <image file name>\n");
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
	perror("mmap");
	exit(1);
    }

    struct ext2_super_block *sb = (struct ext2_super_block *)(disk + 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2048);
    unsigned char *block_bitmap = disk + (gd->bg_block_bitmap * 1024);
    unsigned char *inode_bitmap = disk + (gd->bg_inode_bitmap * 1024);
    struct ext2_inode *inode_table = (struct ext2_inode *)(disk + gd->bg_inode_table * 1024);

    printf("Super block:\n");
    printf("    Inodes: %d\n", sb->s_inodes_count);
    printf("    Blocks: %d\n", sb->s_blocks_count);
    printf("    free blocks: %d\n", sb->s_free_blocks_count);
    printf("    free inodes: %d\n", sb->s_free_inodes_count);
 
    printf("Block group:\n");
    printf("    block bitmap: %d\n", gd->bg_block_bitmap);
    printf("    inode bitmap: %d\n", gd->bg_inode_bitmap);
    printf("    inode table: %d\n", gd->bg_inode_table);
    printf("    free blocks: %d\n", gd->bg_free_blocks_count);
    printf("    free inodes: %d\n", gd->bg_free_inodes_count);
    printf("    used_dirs: %d\n", gd->bg_used_dirs_count); 

    int i, j; 
    
    // Print block bitmap
    printf("Block bitmap: ");
    for(i = 0; i < sb->s_blocks_count; i++) {
        if(check_allocation(block_bitmap, i))
            printf("1");
        else
            printf("0");

        if(i > 0 && i % 8 == 7)
            printf(" ");
    }

    printf("\n");

    // Print inode bitmap
    printf("Inode bitmap: ");
    for(i = 0; i < sb->s_inodes_count; i++) {
        if(check_allocation(inode_bitmap, i))
            printf("1");
        else 
            printf("0");

        if(i > 0 && i % 8 == 7)
            printf(" ");
    }

    printf("\n\n");


    // Print metadata
    
    // Print root info

    // Since no file image occupies more than one block for root entry, I will simply hard code it here.
    printf("Inodes:\n");
    printf("[2] type: d size: %d links: %d blocks: %d\n", inode_table[1].i_size, inode_table[1].i_links_count, inode_table[1].i_blocks);
    printf("[2] Blocks:  %d ", inode_table[1].i_block[0]);
    for(i = 1; i < 15; i++) {
        printf("%d ", inode_table[1].i_block[i]);
    }
    printf("\n");    
      
    // Print other files' info
    int i_num;
    char type;
    struct ext2_inode inode;
    for(i = 10; i < sb->s_inodes_count; i++) {	// First 11 inodes are reserved
        // Print info for inode if the bit is on for this inode
        if(check_allocation(inode_bitmap, i)) {
            
            // Obtain inode at i_th position in inode table
            inode = inode_table[i];            

            // Obtain the type for this inode
            switch(inode.i_mode & 0xf000) {	// Mask lower bits
                case EXT2_S_IFDIR :
                    type = 'd';
                    break;            
                case EXT2_S_IFREG :
                    type = 'f';
                    break;
                case EXT2_S_IFLNK :
                    type = 'l';
                    break;
            }

            // Obtain inode number for this inode
            i_num = i + 1;	// inode number starts at 1
       
            // Print info
            printf("[%d] type: %c size: %d links: %d blocks: %d\n", i_num, type, inode.i_size, inode.i_links_count, inode.i_blocks);
            printf("[%d] Blocks:  ", i_num);
            
            for(j = 0; j < 15; j++) {
                if(inode.i_block[j] > 0)
                    printf("%d ", inode.i_block[j]);
                if(j > 12 && inode.i_block[11] > 0)
                    printf("%d ", inode.i_block[j]);
            }
            printf("\n");   
        }
    }

    printf("\n");

    // Print directory entries

    printf("Directory Blocks: \n");
    
    // Print root first (some hardcoding)
    printf("   DIR BLOCK NUM: 9 (for inode 2)\n");
    // Remaining bytes from the total bytes allocated to this directory
    int byte_left = 1024;  // for all images, root contains all the entries in one block
    char name[EXT2_NAME_LEN];
    int cur_block_idx = 0;
    int cur_block_num = 9;
    struct ext2_dir_entry *cur_entry;

    while(cur_block_num != 0) {
        byte_left = 1024;
        // current entry in cur_block
        cur_entry = (struct ext2_dir_entry *)(disk + cur_block_num * 1024);
        while(byte_left > 0) {
    
            // Find the type of current entry
            if(cur_entry->file_type == EXT2_FT_UNKNOWN)
                type = 'u';
            else if(cur_entry->file_type == EXT2_FT_REG_FILE)
                type = 'f';
            else if(cur_entry->file_type == EXT2_FT_DIR)
                type = 'd';
            else if(cur_entry->file_type == EXT2_FT_SYMLINK)
                type = 's';
            else
                type = 'm';
 
            // Obtain name upto name_len
            memset(name, '\0', sizeof(name));
            strncpy(name, cur_entry->name, cur_entry->name_len); 

            printf("Inode: %d rec_len: %d name_len: %d type= %c name=%s\n", cur_entry->inode, cur_entry->rec_len, cur_entry->name_len, type, name);
    
            // Move to next entry
            byte_left -= cur_entry->rec_len;
            cur_entry = (struct ext2_dir_entry *)((unsigned char *)cur_entry + cur_entry->rec_len);
        }    
        
        cur_block_idx++;
        cur_block_num = inode_table[1].i_block[cur_block_idx];
    }
    printf("\n");
    
    // Print lost+found (some hardcoding)
    cur_block_num = inode_table[10].i_block[0];
    printf("   DIR BLOCK NUM: %d (for inode 11)\n", cur_block_num);
    // Remaining bytes from the total bytes allocated to this directory
    byte_left = 1024;  // for all images, root contains all the entries in one block
    cur_block_idx = 0;


    while(cur_block_num != 0) {
        byte_left = 1024;
        // current entry in cur_block
        cur_entry = (struct ext2_dir_entry *)(disk + cur_block_num * 1024);
        while(byte_left > 0) {
    
            // Find the type of current entry
            if(cur_entry->file_type == EXT2_FT_UNKNOWN)
                type = 'u';
            else if(cur_entry->file_type == EXT2_FT_REG_FILE)
                type = 'f';
            else if(cur_entry->file_type == EXT2_FT_DIR)
                type = 'd';
            else if(cur_entry->file_type == EXT2_FT_SYMLINK)
                type = 's';
            else
                type = 'm';
 
            // Obtain name upto name_len
            memset(name, '\0', sizeof(name));
            strncpy(name, cur_entry->name, cur_entry->name_len); 

            printf("Inode: %d rec_len: %d name_len: %d type= %c name=%s\n", cur_entry->inode, cur_entry->rec_len, cur_entry->name_len, type, name);
    
            // Move to next entry
            byte_left -= cur_entry->rec_len;
            cur_entry = (struct ext2_dir_entry *)((unsigned char *)cur_entry + cur_entry->rec_len);
        }    
        
        cur_block_idx++;
        cur_block_num = inode_table[10].i_block[cur_block_idx];
    }
    printf("\n");

    // Print entries for non-root directories
    // Here I will only deal with the case where each directory takes no more than one block.
    for(i = 11; i < sb->s_inodes_count; i++) {

        inode = inode_table[i];
        i_num = i + 1;
        // Check if current inode is occupied and its type is directory
        if(check_allocation(inode_bitmap, i) && (inode.i_mode & 0xf000) == EXT2_S_IFDIR) {
            
            printf("   DIR BLOCK NUM: %d (for inode %d)\n", inode.i_block[0], i_num);
            byte_left = 1024;
            // First entry in block[j]
            cur_entry = (struct ext2_dir_entry *)(disk + inode.i_block[0] * 1024);
            while(byte_left > 0) {
                // Find the type of current entry
                if(cur_entry->file_type == EXT2_FT_UNKNOWN)
                    type = 'u';
                else if(cur_entry->file_type == EXT2_FT_REG_FILE)
                    type = 'f';
                else if(cur_entry->file_type == EXT2_FT_DIR)
                    type = 'd';
                else if(cur_entry->file_type == EXT2_FT_SYMLINK)
                    type = 's';
                else
                    type = 'm';
                          
                // Obtain name upto name_len
                memset(name, '\0', sizeof(name));
                strncpy(name, cur_entry->name, cur_entry->name_len); 

                printf("Inode: %d rec_len: %d name_len: %d type= %c name=%s\n", cur_entry->inode, cur_entry->rec_len, cur_entry->name_len, type, name);
    
                // Move to next entry
                byte_left -= cur_entry->rec_len;
                cur_entry = (struct ext2_dir_entry *)((unsigned char *)cur_entry + cur_entry->rec_len);

            }
        }
    }    

    return 0;
}

// Check if the block located at pos in bitmap is allocated.
// Used = 1 Unused = 0
int check_allocation(unsigned char *bitmap, int pos) {
    int byte_pos = pos / 8; 
    int bit_pos = pos % 8;

    if(bitmap[byte_pos] & (1 << bit_pos)) {
        return 1;
    }

    return 0;
}

        
