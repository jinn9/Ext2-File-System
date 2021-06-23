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

    if(argc !=  4) {
        fprintf(stderr, "Usage: ext2_cp <image file name> <path to source file> <absolute path to target file>\n");
        return -1;
    }

    if(argv[3][0] != '/') {        
        return ENOENT;
    }

    /* Initialize disk and other structures */
    
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
    gd = (struct ext2_group_desc *)(disk + 2048);
    block_bitmap = disk + 3072;
    inode_bitmap = disk + 4096;
    inode_table = (struct ext2_inode *)(disk + 5120);   

    int file_fd = open(argv[2], O_RDONLY);
    if(file_fd == -1) {
        perror("open");
        return ENOENT;
    }

    /* Check if target path is valid */

    char *file_name;             // name of the copied file
    unsigned int dest_inum;      // inode number for the directoy to which the copied file is added

    // Full path info
    int path_len = strlen(argv[3]);
    unsigned int path_inum = pathwalk(argv[3]);  // inode number for the last file object in path
    struct ext2_inode path_inode;       
    unsigned short path_type;

    // Subpath info 
    // Subpath is a subpath of path leading up to the last file object
    // We need subpath when path includes path AND new name for the copied file
    unsigned int subpath_inum;
    char *subpath;

    // Check if path exists
        
    // Case 1: path exists
    if(path_inum > 0) {
        path_inode = inode_table[path_inum - 1];
        path_type = path_inode.i_mode & EXT2_IMODE_MASK;

        // Case 1-1: path is a directory
        if(path_type == EXT2_S_IFDIR) {
            // Check if a file with same name as the source file already exists in parent's directory
            char *source_name = find_name(argv[2]);
            if(search_directory(path_inum, source_name) > 0) {                
                return EEXIST;
            }

            file_name = source_name;
            dest_inum = path_inum;

        }
        // Case 1-2: path is a file/link.
        else {
            // There is already a file that has the same name in parent's diretory            
            return EEXIST;

        }

    }
 
    // Case 2: path does not exist
    else {

        // If path ends with '/', path does not exist.
        if(argv[3][path_len - 1] == '/') {
            return ENOENT;
        }

        // If subpath is a valid directory, copy the file in parent's direcotry with
        // with its name as last token in path
       
        // Obtain parent's directory
        subpath = find_subpath(argv[3]);
        subpath_inum = pathwalk(subpath);

        // If subpath does not exist or subpath is not a directory, path is invalid.
        // Note that by the design, subpath ends with '/'.
        // If subpath_inum > 0 (i.e., subpath exists), subpath must be a direcotry
        // since subpath to a file/link that ends with '/' cannot exist.
        if(subpath_inum == 0) {            
           return ENOENT;
       }
        
        // subpath is a valid directory and path does not end with '/'
        
        file_name = find_name(argv[3]);
        dest_inum = subpath_inum;

    }        

    /* Allocate an inode to the new file */

    unsigned int new_inum = allocate_inode();
    if(new_inum == 0) {
        fprintf(stderr, "There is no more space in inode table.\n");
        return ENOMEM;
    }
    struct ext2_inode *new_inode = &inode_table[new_inum - 1];

    struct stat file_stats;
    if(stat(argv[2], &file_stats) == -1) {
        perror("stat");
        return -1;
    }
    unsigned int file_size = file_stats.st_size;
    unsigned int file_blocks;
    if(file_size <= 12 * 1024)
        file_blocks = (ceil((double)file_size / 1024)) * 2;
    else
        file_blocks = (ceil((double)file_size / 1024) + 1) * 2;

    new_inode->i_mode = EXT2_S_IFREG;
    new_inode->i_size = file_size;
    new_inode->i_links_count = 1;
    new_inode->i_blocks = file_blocks;
    new_inode->i_dtime = 0;

    /* Add entry for the new file in the parent's directory */

    struct ext2_dir_entry *new_entry = malloc(sizeof(struct ext2_dir_entry));
    if(new_entry == NULL) {
        perror("malloc");
        return -1;
    }
    new_entry->inode = new_inum;
    new_entry->rec_len = -1;
    new_entry->name_len = strlen(file_name);
    new_entry->file_type = EXT2_FT_REG_FILE;
    strncpy(new_entry->name, file_name, new_entry->name_len);
    add_new_entry(dest_inum, new_entry);

    /* Copy the source file into empty data blocks */
    
    // Find empty data blocks and copy the file there
    unsigned int block_num;                 // block number for current block
    unsigned int cur_block_idx = 0;         // index for current block
    unsigned int cur_indirect_idx = 0;      // index into the single indirect block    
    unsigned char *cur_block;               // a pointer to the start of the current block   
    unsigned int *indirect_block;           // a pointer to the start of the single indirect block
    int read_bytes = 0;                     // bytes read from the file
    int total = 0;                          // total bytes read from the file 

    while(total < file_size) {     
        // Only direct blocks are needed
        if(cur_block_idx < 12) {
            // Find an empty block
            block_num = allocate_block();
            if(block_num == 0) {
                fprintf(stderr, "There is no more available data blocks.\n");
                return ENOMEM;
            }
            new_inode->i_block[cur_block_idx] = block_num;
    
            // Copy the file to the current block
            cur_block = disk + block_num * 1024;           
            read_bytes = read(file_fd, cur_block, 1024);
            if(read_bytes == -1) {
                perror("read");
                return -1;
            }
            cur_block_idx++;
        }
        // Need a single indirect block (cur_block_idx == 12)
        else {
            // A block has not been allocated to indirect block yet (i.e., total bytes read so far == 1024 * 12)
            if(cur_indirect_idx == 0) {
                // Allocate new block for indirect block
                block_num = allocate_block();
                if(block_num == 0) {
                    fprintf(stderr, "There is no more available data blocks.\n");
                    return ENOMEM;
                }
                new_inode->i_block[cur_block_idx] = block_num;
                indirect_block = (unsigned int *)(disk + block_num * 1024);                
            }

            // Allocate a block that goes in the indirect block
            block_num = allocate_block();
            if(block_num == 0) {
                fprintf(stderr, "There is no more available data blocks.\n");
                return ENOMEM;
            }
            // Copy the file to the current block
            cur_block = disk + block_num * 1024;
            read_bytes = read(file_fd, cur_block, 1024);
            if(read_bytes == -1) {
                perror("read");
                return -1;
            }
            // Save a pointer to the new block in the indirect block
            indirect_block[cur_indirect_idx] = block_num;
            cur_indirect_idx++;
        }
        total += read_bytes;
    }

	return 0;
}
