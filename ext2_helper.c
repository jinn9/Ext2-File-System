#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include "ext2.h"
#include "ext2_helper.h"

extern unsigned char *disk;
extern struct ext2_super_block *sb;
extern struct ext2_group_desc *gd;
extern unsigned char *block_bitmap;
extern unsigned char *inode_bitmap;
extern struct ext2_inode *inode_table;

int max(int a, int b) {
    if(a > b)
        return a;
    return b;
}

// Checks if the block located at pos in bitmap is allocated
// Returns 1 if it is allocated
// Returns 0 if it is not allocated
int check_allocation(unsigned char *bitmap, int num) {
    int byte_pos = (num - 1) / 8;
    int bit_pos = (num - 1) % 8;

    if(bitmap[byte_pos] & (1 << bit_pos)) {
        return 1;
    }

    return 0;
}


// Set inode or block that has the number num to used
void set_to_used(unsigned char *bitmap, int num) {

    int byte_pos = (num - 1) / 8;
    int bit_pos = (num - 1) % 8;
    bitmap[byte_pos] = bitmap[byte_pos] | (1 << bit_pos);
}

// Finds an empty inode in the table and allocates it.
// It returns inode number for inode if it is found or
// it returns 0 if there is no more empty inodes.
int allocate_inode() {

    int inum;
    struct ext2_inode *inode;
    for(inum = 12; inum <= 32; inum++) {
        // Check if current inode is avaliable
        if(check_allocation(inode_bitmap, inum) == 0) {
            set_to_used(inode_bitmap, inum);
            inode = &inode_table[inum - 1];
            memset(inode, 0, 128);
            sb->s_free_inodes_count--;
            gd->bg_free_inodes_count--; 
            return inum;
        }
    }
    return 0;
}

// Finds an empty block and allocates it.
// It returns block number for data block if it is found or
// it returns 0 if there is no more empty data blocks.
int allocate_block() {

    int block_num;
    unsigned char *block;

    for(block_num = 1; block_num <= 128; block_num++) { 
        // Check if current block is avaiable
        if(check_allocation(block_bitmap, block_num) == 0) {
            block = disk + block_num * 1024;
            memset(block, 0, 1024);
            set_to_used(block_bitmap, block_num);
            sb->s_free_blocks_count--;
            gd->bg_free_blocks_count--;
            return block_num;
        }
    }
    return 0;
}

// Deallocate inode at inum
void deallocate_inode(int inum) {

    int byte_pos = (inum - 1) / 8;
    int bit_pos = (inum - 1) % 8;
    inode_bitmap[byte_pos] = inode_bitmap[byte_pos] & ~(1 << bit_pos);
    sb->s_free_inodes_count++;
    gd->bg_free_inodes_count++;
}

// Deallocate block at block_num
void deallocate_block(int block_num) {

    int byte_pos = (block_num - 1) / 8;
    int bit_pos = (block_num - 1) % 8;
    block_bitmap[byte_pos] = block_bitmap[byte_pos] & ~(1 << bit_pos);
    sb->s_free_blocks_count++;
    gd->bg_free_blocks_count++;
}


// cur_entry:  a pointer to current entry
// dir_inum: inode numbfor for directory that contains current entry
// block_idx: an index for the block that contains current entry 
// offset: an offest for current entry in this block
// Move from current entry to the next entry in this directory
// Returns a pointer to the next entry if there is one.
// Returns NULL if current entry is the last entry in this directory.
struct ext2_dir_entry *move_entry(struct ext2_dir_entry *cur_entry, unsigned int dir_inum, unsigned int *block_idx, unsigned int *offset)  {

    struct ext2_inode dir_inode = inode_table[dir_inum - 1];
    // Move to next position in current block
    *offset += cur_entry->rec_len;
    
    // Case 1: More entries left in current block
    if(*offset < 1024) {
        cur_entry = (struct ext2_dir_entry *)((unsigned char *)cur_entry + cur_entry->rec_len);
        
    }

    // Case 2: No more entries left in current block
    // Go to next block if it exists
    else {
        *offset = 0;
        *block_idx += 1;
        unsigned int block_num = dir_inode.i_block[*block_idx];

        // Check if next block has entries
        if(block_num > 0) {
            cur_entry = (struct ext2_dir_entry *)(disk + block_num * 1024);
        }
        // Next block is empty 
        else {
            return NULL;
        }    
    }    
    return cur_entry;
}


// dir_inum: inode number for directory
// name: name of the file 
// Checks if there is a file object with name 'name' is in this directory
// Returns inode number for the file object if found.
// Returns 0 if not found.
// Returns -1 if dir_inum is not inode number for directory
int search_directory(unsigned int dir_inum, char *name) {

    struct ext2_inode dir_inode = inode_table[dir_inum - 1];
    if((dir_inode.i_mode & EXT2_IMODE_MASK) != EXT2_S_IFDIR) {
        return -1;
    }

    // Current data block info
    unsigned int cur_block_idx = 0;  // index for current block
    unsigned int cur_block_offset = 0;  // offset at current block    
    struct ext2_dir_entry *cur_entry = (struct ext2_dir_entry *)(disk + dir_inode.i_block[0]* 1024);

    while(cur_entry != NULL) {

        if(strncmp(cur_entry->name, name, max(cur_entry->name_len, strlen(name))) == 0 && \
                cur_entry->inode != 0) {

            return cur_entry->inode;
        }
        else {
            cur_entry = move_entry(cur_entry, dir_inum, &cur_block_idx, &cur_block_offset);
        }
    }
 
    // Matching entry not found
    return 0;
}

// Returns inode number for last file obtject in path if path is valid.
// Returns 0 if path is not valid.
int pathwalk(char *path) {

    // path must start from the root directory
    if(path[0] != '/')
        return 0;
  
    int cur_inum = 2;   // inode number for current file object (start from the root)

    int path_len = strlen(path);  
    char last_char = path[path_len - 1];    // last chararacter of path

    char *path_temp = malloc(path_len + 1); // duplicate of path (prevent from modifying path)
    strncpy(path_temp, path, path_len);
    path_temp[path_len] = '\0';

    char *token = strtok(path_temp, "/");   // name of file object
    int rv; // return value

    // Examine each name on path one by one
    while(token != NULL) {
        
        rv = search_directory(cur_inum, token);
        
        // Case 1: Found an entry that matches token
        if(rv > 0) {
            // Go to next level in path
            cur_inum = rv;
        }

        // Case 2: Did not find an entry that matches token
        else if(rv == 0) {
            return 0;
        }        

        // Case 3: We are dealing with a file/link, not a directory
        else {
            // Since there are more file objects in path after current file/link
            // this path is invalid.
            // * path would look like .../current_file/next_token...

            return 0;
        }

        token = strtok(NULL, "/");
    }

    // If path is not a directory, it cannot end with /
    struct ext2_inode inode = inode_table[cur_inum - 1];
    unsigned short file_type = inode.i_mode & EXT2_IMODE_MASK;
    if(file_type != EXT2_S_IFDIR && last_char == '/') 
        return 0;

    return cur_inum;
}

// dir_inum: inode number for directory to which new entry is added
// new_etnry: Entry that is newly added to this directory
// Adds new entry to a directory. 
// Return 0 on success.
// Return -1 if dir_inum is not inode number for a directory.
int add_new_entry(unsigned int dir_inum, struct ext2_dir_entry *new_entry) {
   
    // Obtain info for directory
    struct ext2_inode *dir_inode = &inode_table[dir_inum - 1];   

    // Return if type is not a directory
    if((dir_inode->i_mode & EXT2_IMODE_MASK) != EXT2_S_IFDIR) 
        return -1;

    // Current data block info
    unsigned int cur_block_idx = 0;  // index for current block
    unsigned int cur_block_offset = 0;  // offset at current block
    int cur_block_num = dir_inode->i_block[cur_block_idx];
    struct ext2_dir_entry *cur_entry = (struct ext2_dir_entry *)(disk + dir_inode->i_block[0] * 1024);;
    struct ext2_dir_entry *hidden_entry;    // a pointer to a removed entry

    // Space info
    int space_need = ceil((double)(8 + new_entry->name_len) / 4) * 4; // space needed for new entry
                                                                      // need to be aligned on 4 bytes boundaries
    int space_used;     // Space current entry actually uses
    int space_have;     // Extra space current entry has

    // Loop until we find a place to add new entry 
    // Assumption: There will always be a place for new entry
    //             so loop wiil terminate eventually
    while(1) {

        // Case 1: Current block is empty
        if(cur_entry->inode == 0 && cur_entry->rec_len == 0) {
            cur_entry->inode = new_entry->inode;
            cur_entry->rec_len = 1024;
            cur_entry->name_len = new_entry->name_len;
            cur_entry->file_type = new_entry->file_type;
            strncpy(cur_entry->name, new_entry->name, cur_entry->name_len);
            return 0;
        }

        // Case 2: Current block is not empty
        else {

            // Move to the last entry in current block
            while(cur_block_offset + cur_entry->rec_len < 1024) {
                cur_entry = move_entry(cur_entry, dir_inum, &cur_block_idx, &cur_block_offset);
            }

            space_used = ceil((double)(8 + cur_entry->name_len) / 4) * 4;   
            space_have = cur_entry->rec_len - space_used;                   
        
            // Check if new entry can fit in current entry
            if(space_have >= space_need) {
                
                hidden_entry = cur_entry;

                // Add a new entry after all hidden (or removed) entries 
                // Loop variant: space_have >= space_used
                while(space_have >= space_need) {

                    // Move to next position and see if there is a hidden entry there
                    hidden_entry = (struct ext2_dir_entry *)((unsigned char *)hidden_entry + space_used);

                    // If cur_entry does not point to a hidden entry, add a new entry here
                    if(hidden_entry->inode == 0) {
                        hidden_entry->inode = new_entry->inode;
                        hidden_entry->rec_len = space_have;
                        hidden_entry->name_len = new_entry->name_len;
                        hidden_entry->file_type = new_entry->file_type;
                        strncpy(hidden_entry->name, new_entry->name, hidden_entry->name_len);

                        // Modify last entry's rec_len
                        cur_entry->rec_len -= space_have;
                        return 0;
                    }

                    // cur_entry points to a hidden entry, so new entry cannot be added here
                    else {
                        space_used = ceil((double)(8 + cur_entry->name_len) / 4) * 4;
                        space_have -= space_used;
                    }
                }   
            }

            // If new entry cannot fit in the last entry in current block, move to the next block
            else {
               
                cur_block_idx += 1;
                
                // Allocate new block if needed
                if(dir_inode->i_block[cur_block_idx] == 0) {
                    dir_inode->i_block[cur_block_idx] = allocate_block();
                    if(dir_inode->i_block[cur_block_idx] == 0) {
                        fprintf(stderr, "There is no more available data blocks.\n");
                        exit(-1);
                    }
                    dir_inode->i_size += 1024;
                    dir_inode->i_blocks += 2;
                }
                    
                cur_block_num = dir_inode->i_block[cur_block_idx];
                cur_block_offset = 0;
                cur_entry = (struct ext2_dir_entry *)(disk + cur_block_num * 1024);
            }    
        }     
    }

    return 0;
}    
// Ruturns name of the last file object in this path
char *find_name(char *path) {

    char *name = malloc(EXT2_NAME_LEN);
    if(name == NULL) {
        perror("malloc");
        exit(-1);
    }

    char *token = strtok(path, "/");
    while(token != NULL) {
        memset(name, '\0', EXT2_NAME_LEN);
        strncpy(name, token, strlen(token));
        token = strtok(NULL, "/");
    }
    return name;
}

// Returns path to the parent directory of the last file object in this path.
// If path is /path/to/some/target, it returns /path/to/some/
char *find_subpath(char *path) {

    int i;
    int subpath_len = strlen(path);
    char *subpath = malloc(subpath_len + 1);
    if(subpath == NULL) {
        perror("malloc");
        exit(-1);
    }

    strncpy(subpath, path, subpath_len);
    subpath[subpath_len] = '\0';
    if(subpath[subpath_len - 1] == '/') 
        subpath[subpath_len - 1] = '\0';

    // Get rid of the name of the last file object in this path
    for(i = subpath_len - 1; subpath[i] != '/'; i--) {
        subpath[i] = '\0';
    }

    return subpath;
}


/* From below is helper functions for checker program */

// dir_inum: inode number for directory
// Checks each entry in this directory for any corruption
// Returns the total number of inconsistencies
int check_directory(unsigned int dir_inum) {

    int total = 0;
    struct ext2_inode dir_inode = inode_table[dir_inum - 1];

    // Current data block info
    unsigned int cur_block_idx = 0;  // idx for i_block
    unsigned int cur_block_offset = 0;  // offset at current block
    struct ext2_dir_entry *cur_entry = (struct ext2_dir_entry *)(disk + dir_inode.i_block[0] * 1024);

    while(cur_entry != NULL) {
       
        // Should not check a blank space (deleted file) or a parent direcotry (to avoid overlap)
        if(cur_entry->inode != 0 && \
                strncmp(cur_entry->name, "..", max(cur_entry->name_len, 2)) != 0) {
    
            // Case 1: current entry is . or current entry is a file/link.
            if(strncmp(cur_entry->name, ".", cur_entry->name_len) == 0 || \
                    cur_entry->file_type != EXT2_FT_DIR) {

                total += check_type(cur_entry);
                total += check_inode(cur_entry);
                total += check_dtime(cur_entry);
                total += check_blocks(cur_entry);
            }

            // Case 2: current entry is a directory (but not . entry)
            else {
                total += check_directory(cur_entry->inode);
            }
        }      
        
        // Move to next entry
        cur_entry = move_entry(cur_entry, dir_inum, &cur_block_idx, &cur_block_offset);
    }
   
    return total;
}

// Checks if this entry's file_type matches its imode.
// If not match, file_type follows imode.
// Returns 1 if there is an inconsistency. 
// Returns 0 if there is not an inconsistency.
int check_type(struct ext2_dir_entry *dir_entry) {

    struct ext2_inode inode = inode_table[dir_entry->inode - 1];
    unsigned short imode = inode.i_mode & EXT2_IMODE_MASK;
    unsigned char imode_converted;

    // Convert imode into file type
    if(imode == EXT2_S_IFREG)
        imode_converted = EXT2_FT_REG_FILE;    
    else if(imode == EXT2_S_IFDIR)
        imode_converted = EXT2_FT_DIR;
    else if(imode == EXT2_S_IFLNK)
        imode_converted = EXT2_FT_SYMLINK;
    else
        imode_converted = EXT2_FT_UNKNOWN;
         
    if(imode_converted != dir_entry->file_type) {
        dir_entry->file_type = imode_converted;
        printf("Fixed: Entry type vs inode mismatch: inode[%d]\n", dir_entry->inode);
        return 1;
    }
   
    return 0;
}

// Checks if this entry's inode is marked as in-use in the bitmap.
// If it is not, it is set to in-use and the counters are modified.
// Returns 1 if there is an inconsistency.
// Returns 0 if there is not an inconsistency.
int check_inode(struct ext2_dir_entry *dir_entry) {

    if(check_allocation(inode_bitmap, dir_entry->inode) == 0) {
        set_to_used(inode_bitmap, dir_entry->inode);
        sb->s_free_inodes_count -= 1;
        gd->bg_free_inodes_count -= 1;
        printf("Fixed: inode[%d] not marked as in-use\n", dir_entry->inode);
        return 1;
    }
    return 0;
}


// Checks if this entry's dtime is set to 0.
// If not, it is reset to 0. 
// Returns 1 if there is an inconsistency.
// Returns 0 if there is not an inconsistency.
int check_dtime(struct ext2_dir_entry *dir_entry) {
    
    struct ext2_inode *inode = &inode_table[dir_entry->inode - 1];

    if(inode->i_dtime != 0) {
        inode->i_dtime = 0;
        printf("Fixed: valid inode marked for deletion: [%d]\n", dir_entry->inode);
        return 1;
    }

    return 0;
}

// Checks if this entry's blocks are marked as in-use.
// If not, they are marked as in-use and the counters are modified.
// Returns the total number of inconsistencies.
int check_blocks(struct ext2_dir_entry *dir_entry) {

    struct ext2_inode inode = inode_table[dir_entry->inode - 1];    // inode for current block  
    unsigned int block_idx = 0;                                     // index of current block 
    unsigned int indirect_idx = 0;                                  // current index for indirect block
    unsigned int num_blocks = inode.i_blocks / 2;                   // total number of blocks allocated to this entry
    unsigned int num_errors = 0;                                    // total number of inconsistencies

    unsigned int block_num;                                         // block number for current block
    unsigned int indirect_num;                                      // block number for indirect block
    unsigned int *indirect_block;                                   // indirect block for current entry

    while(num_blocks > 0) {
        // Direct blocks
        if(block_idx < 12) {

            block_num = inode.i_block[block_idx];

            if(check_allocation(block_bitmap, block_num) == 0) {
                set_to_used(block_bitmap, block_num);
                sb->s_free_blocks_count -= 1;
                gd->bg_free_blocks_count -= 1;
                num_errors += 1;
            }
            block_idx += 1;
            num_blocks -= 1;
        }
        // Blocks in the indirect block
        else {
            // First check the block allocated to the indirect block
            if(indirect_idx == 0) {

                indirect_num = inode.i_block[block_idx];
                indirect_block = (unsigned int *)(disk + indirect_num * 1024);

                if(check_allocation(block_bitmap, indirect_num) == 0) {
                    set_to_used(block_bitmap, indirect_num);
                    sb->s_free_blocks_count -= 1;
                    gd->bg_free_blocks_count -= 1;
                    num_errors += 1;
                }

                num_blocks -= 1;
            }

            block_num = indirect_block[indirect_idx]; // block number for the first block in in indirect block

            // Check blocks in the indirect block
            if(check_allocation(block_bitmap, block_num) == 0) {
                set_to_used(block_bitmap, block_num);
                sb->s_free_blocks_count -= 1;
                gd->bg_free_blocks_count -= 1;
                num_errors += 1;
            }

            indirect_idx += 1;                    
            num_blocks -= 1;

        }
    }

    if(num_errors > 0)
        printf("Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", num_errors, dir_entry->inode);
   
    return num_errors;
}


