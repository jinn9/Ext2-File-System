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

int main(int argc, char *argv[]) {

    /* Check if arguments are valid */

    if(argc != 3) {
        fprintf(stderr, "Usage: ext2_mkdir <image file name> <absolute path on disk>\n");
        return -1;
    }

    if(argv[2][0] != '/') {
        return ENOENT;
    }

    if(strncmp(argv[2], "/", strlen(argv[2])) == 0) {
        return EEXIST;
    }

    /* Intiailize disk and other structure */

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

    /* Check path to the directory to which a new directory is to be added. */
    /* If the path exists and is a directory, add entry for new directory   */
    /* and allocate inode, and a block to it.                               */ 

    // Obtain path to the new directory's parent direcotry.
    char *path = find_subpath(argv[2]);

    // Check if path exists.
    // Note that by the design, path always ends with '/'.
    // If path_inum > 0 (meaning path exists), path must be a directory
    // since valid path to a file/link cannot end with /.
    unsigned int path_inum = pathwalk(path);     // inode number for parent directory
    unsigned int new_inum;                       // inode number for new directory
    struct ext2_inode *new_inode;

    // Case 1: path does not exist.
    if(path_inum == 0) {
        return ENOENT;
    }

    // Case 2: path exists (and path is a directory).
    else {
        // Case 2-1: Same file name already exists in path.
        char *new_name = find_name(argv[2]);    // name of new directory
        if(search_directory(path_inum, new_name) > 0) {
            return EEXIST;
        }
        // Case 2-2: Same file name does not exist in path.
        else {
            // Create inode for new directory
            new_inum = allocate_inode();
            if(new_inum == 0) {
                fprintf(stderr, "There is no more space in inode table.\n");
                return ENOMEM;
            }
            new_inode = &inode_table[new_inum - 1];
            new_inode->i_mode = EXT2_S_IFDIR;
            new_inode->i_size = 1024;
            new_inode->i_links_count = 2;
            new_inode->i_blocks = 2;
            new_inode->i_dtime = 0;

            // Add entry for new directory in its parent directory
            struct ext2_dir_entry *new_entry = malloc(sizeof(struct ext2_dir_entry));
            if(new_entry == NULL) {
                perror("malloc");
                return -1;
            }
            new_entry->inode = new_inum;
            new_entry->rec_len = -1;
            new_entry->name_len = strlen(new_name);
            new_entry->file_type = EXT2_FT_DIR;
            strncpy(new_entry->name, new_name, new_entry->name_len);
            add_new_entry(path_inum, new_entry);


            // Allocate a block to new directory
            unsigned int block_num = allocate_block();
            if(block_num == 0) {
                fprintf(stderr, "There is no more available data blocks.\n");
                return ENOMEM;
            }
            new_inode->i_block[0] = block_num;
            
            // Add current entry "." and parent entry '..' in new directory
          
            // add . entry
            struct ext2_dir_entry *cur_entry = malloc(sizeof(struct ext2_dir_entry));
            if(cur_entry == NULL) {
                perror("malloc");
                return -1;
            }
            cur_entry->inode = new_inum;
            cur_entry->rec_len = -1;
            cur_entry->name_len = 1;
            cur_entry->file_type = EXT2_FT_DIR;
            char cur_name[] = ".";
            strncpy(cur_entry->name, cur_name, cur_entry->name_len);
            add_new_entry(new_inum, cur_entry);

            // add .. entry
            struct ext2_dir_entry *parent_entry = malloc(sizeof(struct ext2_dir_entry));
            if(cur_entry == NULL) {
                perror("malloc");
                return -1;
            }
            parent_entry->inode = path_inum;
            parent_entry->rec_len = -1;
            parent_entry->name_len = 2;
            parent_entry->file_type = EXT2_FT_DIR;
            char parent_name[] = "..";
            strncpy(parent_entry->name, parent_name, parent_entry->name_len);
            add_new_entry(new_inum, parent_entry);

            gd->bg_used_dirs_count++; 
            // Also increment links count for parent's directory
            inode_table[path_inum - 1].i_links_count++;
        }
    }
    return 0;
}    
