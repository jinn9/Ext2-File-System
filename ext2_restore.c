#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <unistd.h>
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
        fprintf(stderr, "Usage: ext2_restore <imagefile name> <absolute path to file>\n");
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
    
    char *path = find_subpath(argv[2]);         // path to the last file object's parent directory
    char *target_name = find_name(argv[2]);     // name of the target file
    unsigned int path_inum = pathwalk(path);             // inode number for the parent directory

    // Case 1: path exists and is a directory
    if(path_inum > 0) {
        // Check if there is a file that has same name as 
        // the target file in this directory
        if(search_directory(path_inum, target_name) > 0) {
            return EEXIST;
        }
    }

    // Case 2: path does not exist or is a file/link
    else {
        return ENOENT;
    }

    /* Look for the target file entry in this directory */
    
    // Directory info
    struct ext2_inode *dir_inode = &inode_table[path_inum - 1];
    
    // Current data block info
    unsigned int cur_block_idx = 0;     // idx for i_block
    unsigned int cur_block_offset = 0;  // offset at current block    
    struct ext2_dir_entry *cur_entry = (struct ext2_dir_entry *)(disk + dir_inode->i_block[0] * 1024);

    int space_used;                         // space current entry actually uses (including padding)
    int space_have;                         // extra space current entry has
    struct ext2_dir_entry *hidden_entry;    // a pointer to a removed entry in current entry
    unsigned int target_inum = 0;                    // inode number for the target file

    while(cur_entry != NULL) {

        // Check if current entry is blank and matches the target file
        if(cur_entry->inode == 0 && \
                strncmp(cur_entry->name, target_name, max(cur_entry->name_len, strlen(target_name))) == 0) {

            return ENOENT;
        }

        space_used = ceil((double)(8 + cur_entry->name_len) / 4) * 4;
        space_have = cur_entry->rec_len - space_used;

        // Search current entry (could be blank or not) if it has enough space to contain more entries
        if(space_have >= 12) {

            hidden_entry = cur_entry;

            while(space_have > 0) {
                // move to position where hidden entry might exist
                hidden_entry = (struct ext2_dir_entry *)((unsigned char *)hidden_entry + space_used);

                // Check if there is hidden entry at this position
                if(hidden_entry->inode != 0) {

                    // Check if current entry is the one we are looking for
                    if(strncmp(hidden_entry->name, target_name, \
                                max(hidden_entry->name_len, strlen(target_name))) == 0) {
                        // Found a match
                        
                        // Cannot be a directory
                        if(hidden_entry->file_type == EXT2_FT_DIR) {
                            return EISDIR;
                        }
                        // Save inode number for the target file for later
                        target_inum = hidden_entry->inode;
                        break;
                    }
                    // If not a match, update info for next use 
                    else {
                        space_used = ceil((double)(8 + hidden_entry->name_len) / 4) * 4;
                        space_have -= space_used;
                    }
                } 
                // If there is no hidden entry at this position, current entry must be the last entry
                // because only last entries can have extra space without containing hidden entries.
                // There cannot be more hidden entries from here in this entry, so move to next block.
                else {
                    break;
                }
            }
            // If a match has been found, proceed to next step
            if(target_inum > 0) 
                break;
            // If a match has not been found, move to next entry
            else 
                goto notfound;
        }

        // Move to next entry if current entry has not enough space
        else {
            notfound:
            cur_entry = move_entry(cur_entry, path_inum, &cur_block_idx, &cur_block_offset);
        }
    }


    /* Restore inode and blocks */

    // Check if target file has been found
    if(target_inum == 0) {
        return ENOENT;
    }

    // Check if target file's inode has been reused
    if(check_allocation(inode_bitmap, target_inum) == 1) {
        return ENOENT;
    } 


    // Check if target file's blocks have been reused
    struct ext2_inode *target_inode = &inode_table[target_inum - 1];
    cur_block_idx = 0;
    unsigned int cur_indirect_idx = 0;
    unsigned int num_blocks = target_inode->i_blocks / 2;   // total number of blocks allocated to the target file
    
    unsigned int cur_block_num;
    unsigned int indirect_num;                              // block number for indirect block
    unsigned int *indirect_block;                           // indirect block for the target file

    while(num_blocks > 0) {
        // Direct blocks
        if(cur_block_idx < 12) {

            cur_block_num = target_inode->i_block[cur_block_idx];

            if(check_allocation(block_bitmap, cur_block_num) == 1) {
                return ENOENT;
            }
            cur_block_idx += 1;
            num_blocks -= 1;
        }
        // Blocks in the indirect block
        else {
            // First check the block allocated to the indirect block
            if(cur_indirect_idx == 0) {
                indirect_num = target_inode->i_block[cur_block_idx];
                indirect_block = (unsigned int *)(disk + indirect_num * 1024);

                if(check_allocation(block_bitmap, indirect_num) == 1) {
                    return ENOENT;
                }
                num_blocks -= 1;
            }

            // Check blocks in the indirect block
            cur_block_num = indirect_block[cur_indirect_idx];
            if(check_allocation(block_bitmap, cur_block_num) == 1) {
                return ENOENT;
            }
            
            cur_indirect_idx += 1;
            num_blocks -= 1;           
        }
    }

    // Restore target file's entry
    hidden_entry->rec_len = space_have;
    cur_entry->rec_len -= space_have;

    // Set target file's inode to used
    set_to_used(inode_bitmap, target_inum);
    sb->s_free_inodes_count -= 1;
    gd->bg_free_inodes_count -= 1;
    target_inode->i_links_count = 1;
    target_inode->i_dtime = 0;
    
    // Set target file's blocks to used
    cur_block_idx = 0;
    cur_indirect_idx = 0;
    num_blocks = target_inode->i_blocks / 2;

    while(num_blocks > 0) {
        cur_block_num = target_inode->i_block[cur_block_idx];

        if(cur_block_idx < 12) {
            set_to_used(block_bitmap, cur_block_num);
            sb->s_free_blocks_count -= 1;
            gd->bg_free_blocks_count -= 1;
            cur_block_idx += 1;
            num_blocks -= 1;
        }

        else {
            if(cur_indirect_idx == 0) {
                set_to_used(block_bitmap, indirect_num);
                sb->s_free_blocks_count -= 1;
                gd->bg_free_blocks_count -= 1;
                num_blocks -= 1;
            }
            
            cur_block_num = indirect_block[cur_indirect_idx];
            set_to_used(block_bitmap, cur_block_num);
            sb->s_free_blocks_count -= 1;
            gd->bg_free_blocks_count -= 1;
            cur_indirect_idx += 1;
            num_blocks -= 1;
        }
    }

    return 0;
}    
