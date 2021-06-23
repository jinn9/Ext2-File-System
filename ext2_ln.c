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

    /* Check the arguments */

    if(argc < 4 || argc > 5) {
        fprintf(stderr, "Usage: ext2_ln <image file name> <absolute path to source file object> <absolute path to target file object> [-s]\n");
        return -1;
    }

    if(argv[2][0] != '/') {        
        return ENOENT;
    }

    if(argv[3][0] != '/') {
        return ENOENT;
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

    /* Check if source path and target path are valid. */

    unsigned int source_inum;           // inode number for source file object
    char *source_name;                  // name for source file object
    unsigned int source_type;           // type for source file object
    unsigned int target_inum;           // inode number for target file object
    unsigned int target_type;           // type for target file object
    unsigned int dest_inum;             // inode number for the directory to which new link entry is added 
    char *link_name;                    // name for new link
    struct ext2_inode *inode;

    // Source path
    source_inum = pathwalk(argv[2]);

    // Case 1: source path is valid
    if(source_inum > 0) {
        inode = &inode_table[source_inum - 1];
        source_type = inode->i_mode & EXT2_IMODE_MASK;
        // Check if it is trying to hardlink to a directory
        if(source_type == EXT2_S_IFDIR && argc == 4) {
            return EISDIR;
        }

        source_name = find_name(argv[2]);
    }

    // Case 2: source path is invalid
    else {
        return ENOENT;
    }

    // Target path
    target_inum = pathwalk(argv[3]);

    // Case 1: target path exists
    if(target_inum > 0) {
        inode = &inode_table[target_inum - 1];
        target_type = inode->i_mode & EXT2_IMODE_MASK;

        // Case 1-1: target is a directory
        if(target_type == EXT2_S_IFDIR) {
            // Check if there is already a file that has same name as source file in this path
            if(search_directory(target_inum, source_name) > 0) {
                return EEXIST;
            }

            dest_inum = target_inum;
            link_name = source_name;
        }

        // Case 1-2: target path is a file/link
        else {
            // There is already a file object in this path that has same name as source file name 
            return EEXIST;
        }
    }

    // Case 2: target path does not exist
    else {
        
        // Check if subpath up to target file object exists and is a directory
        // e.g. if full path is /path/to/target, we are checking /path/to/.
        char *subpath = find_subpath(argv[3]);
        int subpath_inum = pathwalk(subpath);
        int path_len = strlen(argv[3]); // length of full path

        // If subpath does not exist or subpath is not a directory or
        // (full) path ends with '/', (full) path is invalid.
        // By the design, subpath ends with '/'. So if subpath_inum > 0 (i.e., subpath exists),
        // subpath must be a direcotry since a file/link cannot end with '/'.
        if(subpath_inum == 0 || argv[3][path_len - 1] == '/') {
            return ENOENT;
        }

        // subpath exists and is a directory.

        // Obtain name for new link and inode number for its directory
        dest_inum = subpath_inum;
        link_name = find_name(argv[3]);
    }
        
    /* Create a hard link if the flag is not given or a soft link if the flag is given */

    struct ext2_dir_entry *new_entry = malloc(sizeof(struct ext2_dir_entry));
    if(new_entry == NULL) {
        perror("malloc");
        return -1;
    }
    struct ext2_inode *source_inode = &inode_table[source_inum - 1];

    // Hard link
    if(argc == 4) {
        // Create an entry for hard link which is linked to the source file object
        new_entry->inode = source_inum;
        new_entry->rec_len = -1;
        new_entry->name_len = strlen(link_name);
        strncpy(new_entry->name, link_name, new_entry->name_len);
        
        // Note that file type cannot be a directory
        if(source_type == EXT2_S_IFREG) 
            new_entry->file_type = EXT2_FT_REG_FILE;
        else 
            new_entry->file_type = EXT2_FT_SYMLINK;
        
        // Add an entry for new link in the specified directory
        add_new_entry(dest_inum, new_entry);

        // Incremement link count for the source file object
        source_inode->i_links_count++;
    }

    // Symbolic link
    else {
        // Allocate an inode to link
        int new_inum = allocate_inode();
        if(new_inum == 0) {
            fprintf(stderr, "There is no more space in inode table.\n");
            return ENOMEM;
        }
        struct ext2_inode *new_inode = &inode_table[new_inum - 1];
        
        new_inode->i_mode = EXT2_S_IFLNK;
        new_inode->i_size = 1024;
        new_inode->i_links_count = 1;
        new_inode->i_block[0] = allocate_block();
        if(new_inode->i_block[0] == 0) {
            fprintf(stderr, "There is no more available data blocks.\n");
            return ENOMEM;
        }
        new_inode->i_blocks = 2;
        new_inode->i_dtime = 0;

        // Write pathname to the block
        char *block = (char *)(disk + new_inode->i_block[0] * 1024);
        int source_len = strlen(argv[2]);
        strncpy(block, argv[2], source_len);
        block[source_len] = '\0';

        // Add an entry for new link in the specfied directory
        new_entry->inode = new_inum;
        new_entry->rec_len = -1;
        new_entry->file_type = EXT2_FT_SYMLINK;
        new_entry->name_len = strlen(link_name);
        strncpy(new_entry->name, link_name, new_entry->name_len);
        add_new_entry(dest_inum, new_entry);
    }

    return 0;
}    
