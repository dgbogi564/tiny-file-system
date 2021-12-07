/*
 *  Copyright (C) 2019 CS416 Spring 2019
 *	
 *	Tiny File System
 *
 *	File:	tfs.c
 *  Author: Yujie REN
 *	Date:	April 2019
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "tfs.h"

char diskfile_path[PATH_MAX];
static double dirent_blocksize_ratio = (double)BLOCK_SIZE/(double)sizeof(struct dirent);

// Declare your in-memory data structures here

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

    int avail_ino = -1;

    struct superblock *superblock = malloc(BLOCK_SIZE);
    bio_read(0, superblock);

    // Step 1: Read inode bitmap from disk
    bitmap_t i_bitmap = malloc(BLOCK_SIZE);
    bio_read(superblock->i_bitmap_blk, i_bitmap);

    // Step 2: Traverse inode bitmap to find an available slot
    for (int i = 0; i < MAX_INUM; ++i) {
        if(!i_bitmap[i]) {
            avail_ino = i;
            break;
        }
    }

    // Step 3: Update inode bitmap and write to disk
    set_bitmap(i_bitmap, avail_ino);
    bio_write(superblock->i_bitmap_blk, i_bitmap);

    free(superblock);
    free(i_bitmap);

    return avail_ino;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

    int avail_blkno = -1;

    struct superblock *superblock = malloc(BLOCK_SIZE);
    bio_read(0, superblock);

    // Step 1: Read data block bitmap from disk
    bitmap_t d_bitmap = malloc(BLOCK_SIZE);
    bio_read(superblock->d_start_blk, d_bitmap);

    // Step 2: Traverse data block bitmap to find an available slot
    for (int i = 0; i < MAX_DNUM; ++i) {
        if(!d_bitmap[i]) {
            avail_blkno = i;
            break;
        }
    }

    // Step 3: Update data block bitmap and write to disk
    set_bitmap(d_bitmap, avail_blkno);
    bio_write(superblock->d_bitmap_blk, d_bitmap);

    free(superblock);
    free(d_bitmap);

    return avail_blkno;
}

/*
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

    // Step 1: Get the inode's on-disk block number

    // Step 2: Get offset of the inode in the inode on-disk block

    // Step 3: Read the block from disk and then copy into inode structure

    return 0;
}

int writei(uint16_t ino, struct inode *inode) {

    // Step 1: Get the block number where this inode resides on disk

    // Step 2: Get the offset in the block where this inode resides on disk

    // Step 3: Write inode to disk

    return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

    // Step 1: Call readi() to get the inode using ino (inode number of current directory)

    // Step 2: Get data block of current directory from inode

    // Step 3: Read directory's data block and check each directory entry.
    //If the name matches, then copy directory entry to dirent structure

    return 0;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

    // Step 1: Read dir_inode's data block and check each directory entry of dir_inode

    // Step 2: Check if fname (directory name) is already used in other entries

    // Step 3: Add directory entry in dir_inode's data block and write to disk

    // Allocate a new data block for this directory if it does not exist

    // Update directory inode

    // Write directory entry

    return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

    // Step 1: Read dir_inode's data block and checks each directory entry of dir_inode

    // Step 2: Check if fname exist

    // Step 3: If exist, then remove it from dir_inode's data block and write to disk

    return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {

    // Step 1: Resolve the path name, walk through path, and finally, find its inode.
    // Note: You could either implement it in a iterative way or recursive way

    return 0;
}

/* 
 * Make file system
 */
int tfs_mkfs() {

    // Call dev_init() to initialize (Create) Diskfile
    dev_init(diskfile_path);

    // write superblock information
    static const struct superblock sb_info = {
            .magic_num = MAGIC_NUM,
            .max_inum = MAX_INUM,
            .max_dnum = MAX_DNUM,
            .i_bitmap_blk = 1,
            .d_bitmap_blk = 2,
            .i_start_blk = 3,
            .d_start_blk = (2 + ((MAX_INUM*1024)/BLOCK_SIZE)) + 1
    };
    struct superblock *superblock = malloc(BLOCK_SIZE);
    memcpy(superblock, &sb_info, sizeof(struct superblock));
    bio_write(0, superblock);

    // initialize inode bitmap
    bitmap_t i_bitmap = calloc(1, BLOCK_SIZE);

    // initialize data block bitmap
    bitmap_t d_bitmap = calloc(1, BLOCK_SIZE);

    // update bitmap information for root directory
    const int dirents_per_block = (int)dirent_blocksize_ratio;
    int dirent_blocks = (int)(3.0/dirents_per_block);
    if((3.0/dirents_per_block) != dirent_blocks) {
        dirent_blocks += 1;

    static struct dirent dir_info[3];

    dir_info[0].ino = 0;
    dir_info[0].valid = 1;
    strcpy(dir_info[0].name, "/");

    dir_info[1].ino = 1;
    dir_info[1].valid = 1;
    strcpy(dir_info[1].name, ".");

    dir_info[2].ino = 2;
    dir_info[2].valid = 1;
    strcpy(dir_info[2].name, "..");

    int dir_counter = 0;
    for(int i = 0; i < dirent_blocks; ++i) {
        struct dirent *dirent = calloc(1, BLOCK_SIZE);
        for(int k = 0; k < dirents_per_block; ++k) {
            memcpy(&dirent[k], &dir_info[dir_counter], sizeof(struct dirent));
            dir_counter++;
        }
        bio_write((superblock->d_start_blk + i), dirent);
        free(dirent);
    }

    // update inode for root directory
    struct inode root_inode_info = {
            .ino = 0,
            .valid = 1,
            .size = BLOCK_SIZE*(dirent_blocks-1),
            .type = directory,
            .link = 0,
    };
    for (int i = 0; i < 8; ++i) {
        root_inode_info.direct_ptr[i] = -1;
        root_inode_info.direct_ptr[16-i] = -1;
        root_inode_info.indirect_ptr[i] = -1;
    }
    for(int i = 0; i < dirent_blocks; ++i) {
        root_inode_info.direct_ptr[i] = i;
        root_inode_info.link++;
    }

    struct inode *root_inode = malloc(BLOCK_SIZE);
    memcpy(root_inode, &root_inode_info, sizeof(struct inode));
    set_bitmap(i_bitmap, 0);
    bio_write(superblock->i_start_blk, root_inode);

    free(superblock);
    free(i_bitmap);
    free(d_bitmap);
    free(root_inode);

    return 0;
}


/*
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {

    // Step 1a: If disk file is not found, call mkfs
    struct stat info;
    if (stat(diskfile_path, &info)) {
        tfs_mkfs();
        return NULL;
    }

    // Step 1b: If disk file is found, just initialize in-memory data structures
    // and read superblock from disk


    return NULL;
}

static void tfs_destroy(void *userdata) {

    // Step 1: De-allocate in-memory data structures

    // Step 2: Close diskfile

}

static int tfs_getattr(const char *path, struct stat *stbuf) {

    // Step 1: call get_node_by_path() to get inode from path

    // Step 2: fill attribute of file into stbuf from inode

    stbuf->st_mode   = S_IFDIR | 0755;
    stbuf->st_nlink  = 2;
    time(&stbuf->st_mtime);

    return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {

    // Step 1: Call get_node_by_path() to get inode from path

    // Step 2: If not find, return -1

    return 0;
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

    // Step 1: Call get_node_by_path() to get inode from path

    // Step 2: Read directory entries from its data blocks, and copy them to filler

    return 0;
}


static int tfs_mkdir(const char *path, mode_t mode) {

    // Step 1: Use dirname() and basename() to separate parent directory path and target directory name

    // Step 2: Call get_node_by_path() to get inode of parent directory

    // Step 3: Call get_avail_ino() to get an available inode number

    // Step 4: Call dir_add() to add directory entry of target directory to parent directory

    // Step 5: Update inode for target directory

    // Step 6: Call writei() to write inode to disk


    return 0;
}

static int tfs_rmdir(const char *path) {

    // Step 1: Use dirname() and basename() to separate parent directory path and target directory name

    // Step 2: Call get_node_by_path() to get inode of target directory

    // Step 3: Clear data block bitmap of target directory

    // Step 4: Clear inode bitmap and its data block

    // Step 5: Call get_node_by_path() to get inode of parent directory

    // Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

    return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

    // Step 1: Use dirname() and basename() to separate parent directory path and target file name

    // Step 2: Call get_node_by_path() to get inode of parent directory

    // Step 3: Call get_avail_ino() to get an available inode number

    // Step 4: Call dir_add() to add directory entry of target file to parent directory

    // Step 5: Update inode for target file

    // Step 6: Call writei() to write inode to disk

    return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {

    // Step 1: Call get_node_by_path() to get inode from path

    // Step 2: If not find, return -1

    return 0;
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

    // Step 1: You could call get_node_by_path() to get inode from path

    // Step 2: Based on size and offset, read its data blocks from disk

    // Step 3: copy the correct amount of data from offset to buffer

    // Note: this function should return the amount of bytes you copied to buffer
    return 0;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    // Step 1: You could call get_node_by_path() to get inode from path

    // Step 2: Based on size and offset, read its data blocks from disk

    // Step 3: Write the correct amount of data from offset to disk

    // Step 4: Update the inode info and write it to disk

    // Note: this function should return the amount of bytes you write to disk
    return size;
}

static int tfs_unlink(const char *path) {

    // Step 1: Use dirname() and basename() to separate parent directory path and target file name

    // Step 2: Call get_node_by_path() to get inode of target file

    // Step 3: Clear data block bitmap of target file

    // Step 4: Clear inode bitmap and its data block

    // Step 5: Call get_node_by_path() to get inode of parent directory

    // Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

    return 0;
}

static int tfs_truncate(const char *path, off_t size) {
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int tfs_release(const char *path, struct fuse_file_info *fi) {
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int tfs_flush(const char * path, struct fuse_file_info * fi) {
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}

static int tfs_utimens(const char *path, const struct timespec tv[2]) {
    // For this project, you don't need to fill this function
    // But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations tfs_ope = {
        .init		= tfs_init,
        .destroy	= tfs_destroy,

        .getattr	= tfs_getattr,
        .readdir	= tfs_readdir,
        .opendir	= tfs_opendir,
        .releasedir	= tfs_releasedir,
        .mkdir		= tfs_mkdir,
        .rmdir		= tfs_rmdir,

        .create		= tfs_create,
        .open		= tfs_open,
        .read 		= tfs_read,
        .write		= tfs_write,
        .unlink		= tfs_unlink,

        .truncate   = tfs_truncate,
        .flush      = tfs_flush,
        .utimens    = tfs_utimens,
        .release	= tfs_release
};


int main(int argc, char *argv[]) {
    int fuse_stat;

    getcwd(diskfile_path, PATH_MAX);
    strcat(diskfile_path, "/DISKFILE");

    fuse_stat = fuse_main(argc, argv, &tfs_ope, NULL);

    return fuse_stat;
}

