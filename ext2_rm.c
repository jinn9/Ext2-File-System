#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include "ext2.h"
#include "ext2_helper.h"

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;
unsigned char *block_bitmap;
unsigned char *inode_bitmap;
struct ext2_inode *inode_table;

int main(int argc, char *argv[]) {

    /* Check if arguments are valid */

    if(argc != 3) {
        fprintf(stderr, "Usage: ext2_rm <image file name> <absolute path to target file>\n");
        return -1;
    }

    if(argv[2][0] != '/') {        
        return ENOENT;
    }

    if(strncmp(argv[2], "/", strlen(argv[2])) == 0) {
        return EISDIR;
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

    /* Check if path is valid */

    char *path = find_subpath(argv[2]);             // pathname for a directory that has target file
    char *name = find_name(argv[2]);                // name of target file
    unsigned int path_inum = pathwalk(path);        // inode number for path
    unsigned int target_inum;                       // inode number for target file 
    struct ext2_inode *target_inode;                // inode for target file 
    unsigned int target_type;                       // type for target file object

    // Case 1: path does not exist or path is not a directory

    // By the design if path ends with / and path_inum == 0,
    // then either path does not exist or path is not a directory.
    if(path_inum == 0) {
        return ENOENT;
    }

    // Case 2: path exists and is a directory
    else {
        
        // Check if path has target file
        target_inum = search_directory(path_inum, name);
        if(target_inum == 0) {
            return ENOENT;
        }
           
        // Check if target file is a directory
        target_inode = &inode_table[target_inum - 1];
        target_type = target_inode->i_mode & EXT2_IMODE_MASK;
        if(target_type == EXT2_S_IFDIR) {
            return EISDIR;
        }
     }
            

    /* Remove the entry for the file */

    // Inode number for directory that has the target file
    struct ext2_inode path_inode = inode_table[path_inum - 1];

    // Current data block info
    unsigned int cur_block_idx = 0;     // idx for i_block
    unsigned int cur_block_offset = 0;  // offset at current block

    struct ext2_dir_entry *cur_entry = (struct ext2_dir_entry *)(disk + path_inode.i_block[0] * 1024);
    struct ext2_dir_entry *prev_entry = cur_entry;

    while(cur_entry != NULL) {

        // Check if we found the file
        if(strncmp(cur_entry->name, name, max(cur_entry->name_len, strlen(name))) == 0 && 
                cur_entry->inode != 0) {
            
            // Check if this entry is the first entry in the block
            if(cur_entry == prev_entry) {

                // Simply set inode number for this entry to 0 to mark this entry is removed
                cur_entry->inode = 0;

                // Note that current block cannot be the first block.
                // For the sake of contradiction, suppose it was the first block.
                // Then the target file must be "." since the first entry of the 
                // first block is "." But since "." is a direcotry, the program 
                // must have returned error above, so there is a contradiction.
                // Therefore this block is not the first block.
            }

            // This entry is not the first entry
            else {
                // Modify rec_len for the previous entry
                prev_entry->rec_len += cur_entry->rec_len;
            } 

            break;
        }

        // Did not find the entry for the file so move to the next entry in the block
        // or move to the first entry in the next block 
        else {
            // Case 1: current entry points to the first entry in the next block
            if(cur_block_offset + cur_entry->rec_len == 1024) {
                cur_entry = move_entry(cur_entry, path_inum, &cur_block_idx, &cur_block_offset);
                prev_entry = cur_entry;
            }
            // Case 2: current entry points to the next entry in current block
            else {
                prev_entry = cur_entry;
                cur_entry = move_entry(cur_entry, path_inum, &cur_block_idx, &cur_block_offset);
            }
        }
    }
 
    /* Deallocate associated inode and blocks for the file if links count became 0 */

    target_inode->i_links_count -= 1;
    if(target_inode->i_links_count == 0) {

        // Record deletion time
        target_inode->i_dtime = time(0);

        // Deallocate inode
        deallocate_inode(target_inum);

        // Deallocate blocks associated with the file
        unsigned int block_idx = 0;                             // index for current block
        unsigned int indirect_idx = 0;                          // current index for indirect block
        unsigned int num_blocks = target_inode->i_blocks / 2;   // total number of blocks allocated to the target file
        
        unsigned int block_num;                                 // block number for current block
        unsigned int indirect_num;                              // block number for indirect block
        unsigned int *indirect_block;                           // indirect block for the target file

        while(num_blocks > 0) {
           
            // Freeing direct blocks
            if(block_idx < 12) {
                block_num = target_inode->i_block[block_idx];
                deallocate_block(block_num);
                block_idx += 1;
                num_blocks -= 1;
            }
            
            // Freeing blocks in indirect block
            else {
                // Free indirect block first
                if(indirect_idx == 0) {
                    indirect_num = target_inode->i_block[block_idx];
                    indirect_block = (unsigned int *)(disk + indirect_num * 1024);

                    deallocate_block(indirect_num);
                    num_blocks -= 1;
    
                }

                // deallocate blocks in indirect block
                block_num = indirect_block[indirect_idx];
                deallocate_block(block_num);
                indirect_idx += 1;
                num_blocks -= 1; 
            }
        }
    }
          
    return 0;
}    
