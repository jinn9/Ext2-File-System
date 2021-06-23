# Ext2 File System
**INTRODUCTION**\
The ext2 is a file system for the Linux Kernel. This repository contains a set of programs that modify ext2-format virtual disks.

**PROGRAMS**
-	**ext2_ cp**: This program copies the file on your native file system onto the specified location on the disk. It takes three command line arguments. The first is the name of an ext2 formatted virtual disk. The second is the path to a file on your native operating system, and the third is an absolute path on your ext2 formatted disk. 
-	**ext2_mkdir**: This program creates the final directory on the specified path on the disk. It takes two command line arguments. The first is the name of an ext2 formatted virtual disk. The second is an absolute path on your ext2 formatted disk.
-	**ext2_ln**: This program creates a link from the first specified file to the second specified path. It takes three command line arguments. The first is the name of an ext2 formatted virtual disk. The other two are absolute paths on your ext2 formatted disk. Additionally, it may take a “-s” flag, after the disk image argument. When this flag is used, the program creates a symlink instead.
-	**ext2_rm**: This program removes the specified file from the disk. It takes two command line arguments. The first is the name of an ext2 formatted virtual disk, and the second is an absolute path to a file or link (not a directory) on that disk.
-	**ext2_restore**: This program restores the specified file that has been previously removed. It takes two command line arguments. The first is the name of an ext2 formatted virtual disk, and the second is an absolute path to a file or link on that disk. 
-	**ext2_checker**: This program implements a file system checker, which detects a file system inconsistencies and takes appropriate actions to fix them (as well as counts the number of fixes). It takes one command line argument: the name of an ext2 formatted virtual disk. 

**DISK IMAGES SPECIFICATION**
-	A disk is 128 blocks where the block size is 1024 bytes.
-	There is only one block group.
-	There are 32 inodes.

**PLAYING WITH VIRTUAL IMAGES USING THE PROGRAMS**\
To interface with virtual images, you first need to mount the file system (instruction is provided below). Then you can use standards commands (_mkdir_, _cp_, _rm_, _ln_) to interact with these images.

**MOUNTING A FILE SYSTEM**\
If you have root access on a Linux machine (or Linux virtual machine), you can use _mount_ to mount the disk into your file system and to peruse its contents. Note: this requires _sudo_, so you will need to do this on a machine (or virtual machine) that you administer. 
You can also use a tool called _fuse_ that allows you to mount a file system at user-level.

1. Create a directory in /tmp and go there
```
mkdir –m 700 /tmp
cd /tmp
```

2. Create your own disk image
```
dd if=/dev/zero of=DISKNAME.img bs=1024 count=128
/sbin/mke2fs –N 32 –F DISKNAME.img
```

3. Create a mount point and mount the image
#current working directory is /tmp
```
mkdir mnt
fuseext2 –o rw+ DISKNAME.img mnt
```

4. Check to see if it is mounted
```
df –hl
```

5. Now you can use the mounted file system, for example
```
mkdir mnt/test
```
6. Unmount the image once you are done
```
fusermount –u mnt
```

