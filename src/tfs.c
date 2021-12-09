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
static int i_per_blk, dirents_per_blk;

// Declare your in-memory data structures here
struct superblock superblock;
unsigned char i_bitmap[MAX_INUM];
unsigned char d_bitmap[MAX_DNUM];


/*
 * Get available inode number from bitmap
 */
int get_avail_ino() {

    int avail_ino = -1;

    // Step 1: Read inode bitmap from disk
    errno = 0;
    if ((bio_read(superblock.i_bitmap_blk, i_bitmap) < 0) && errno) {
        ERROR("Failed to read disk");
        return -1;
    }

    // Step 2: Traverse inode bitmap to find an available slot
    for (int i = 0; i < MAX_INUM; ++i) {
        if (get_bitmap(i_bitmap, i) == 0) {
            avail_ino = i;
            break;
        }
    }

    // Step 3: Update inode bitmap and write to disk
    set_bitmap(i_bitmap, avail_ino);
    if ((bio_write(superblock.i_bitmap_blk, i_bitmap) < 0) && errno) {
        ERROR("Failed to write to disk");
        return -1;
    }

    return avail_ino;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

    int avail_blkno = -1;

    // Step 1: Read data block bitmap from disk
    errno = 0;
    if ((bio_read(superblock.d_bitmap_blk, d_bitmap) < 0) && errno) {
        ERROR("Failed to read disk");
        return -1;
    }

    // Step 2: Traverse data block bitmap to find an available slot
    for (int i = 0; i < MAX_DNUM; ++i) {
        if (get_bitmap(d_bitmap, i) == 0) {
            avail_blkno = i;
            d_bitmap[i] = 1;
            break;
        }
    }

    // Step 3: Update data block bitmap and write to disk
    set_bitmap(d_bitmap, avail_blkno);
    if ((bio_write(superblock.i_bitmap_blk, d_bitmap) < 0) && errno) {
        ERROR("Failed to write to disk");
        return -1;
    }

    return avail_blkno;
}

/*
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

    void *blk = malloc(BLOCK_SIZE);
    if (blk == NULL) {
        ERROR("Failed to allocate memory");
        return -1;
    }

    // Step 1: Get the inode's on-disk block number
    // Step 2: Get offset of the inode in the inode on-disk block
    int i_offset = ino%i_per_blk;
    int i_blkno = superblock.i_start_blk + ((ino/i_per_blk) + (i_offset != 0));

    // Step 3: Read the block from disk and then copy into inode structure
    if ((bio_read(i_blkno, blk) < 0) && errno) {
        free(blk);
        ERROR("Failed to read disk");
        return -1;
    }
    memcpy(inode, &((struct inode *)blk)[i_offset], sizeof(struct inode));

    free(blk);
    return 0;
}

int writei(uint16_t ino, struct inode *inode) {

    void *blk = malloc(BLOCK_SIZE);
    if (blk == NULL) {
        ERROR("Failed to allocate memory");
        return -1;
    }

    // Step 1: Get the block number where this inode resides on disk
    // Step 2: Get the offset in the block where this inode resides on disk
    int i_offset = ino%i_per_blk;
    int i_blkno = superblock.i_start_blk + ((ino/i_per_blk) + (i_offset != 0));

    // Step 3: Write inode to disk
    if ((bio_read(i_blkno, blk) < 0) && errno) {
        free(blk);
        ERROR("Failed to read disk");
        return -1;
    }
    memcpy(&((struct inode *)blk)[i_offset], inode, sizeof(struct inode));
    if ((bio_write(i_blkno, blk) < 0) && errno) {
        ERROR("Failed to write to disk");
        free(blk);
        return -1;
    }

    free(blk);
    return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

    int found = 0;
    char *fname_cpy = strdup(fname);
    if (fname_cpy == NULL) {
        ERROR("Failed to allocate memory");
        return -1;
    }
    char *f_basename = basename(fname_cpy);

    struct dirent *dirent_blk = malloc(BLOCK_SIZE);
    if (dirent_blk == NULL) {
        ERROR("Failed to allocate memory");
        free(fname_cpy);
        return -1;
    }
    int *ptr_blk = malloc(BLOCK_SIZE);
    if (ptr_blk == NULL) {
        ERROR("Failed to allocate memory");
        free(fname_cpy);
        free(dirent_blk);
        return -1;
    }

    // Step 1: Call readi() to get the inode using ino (inode number of current directory)
    struct inode inode = { };
    if (readi(ino, &inode) < 0) {
        free(fname_cpy);
        free(dirent_blk);
        free(ptr_blk);
        return -1;
    }

    // Step 2: Get data block of current directory from inode
    // Step 3: Read directory's data block and check each directory entry.
    //If the name matches, then copy directory entry to dirent structure
    memcpy(ptr_blk, inode.direct_ptr, sizeof(inode.direct_ptr));
    for (int i = -1; i < 8; ) {
        for (int j = 0; j < 16; ++j) {
            if (ptr_blk[j] == -1) {
                continue;
            }

            if (bio_read(ptr_blk[i], dirent_blk) < 0) {
                ERROR("Failed to read disk");
                free(fname_cpy);
                free(dirent_blk);
                free(ptr_blk);
                return -1;
            }

            if (ino < dirent_blk[0].ino || (dirent_blk[0].ino+(dirents_per_blk-1) < ino)) {
                continue;
            }

            for (int k = 0; k < dirents_per_blk; k++) {
                if (dirent_blk[k].ino == ino && strcmp(dirent_blk[k].name, f_basename) == 0) {
                    memcpy(dirent, &dirent_blk[k], sizeof(struct dirent));
                    found = 1;
                    break;
                }
            }
        }

        // breaks if file has been found or it's the last block to check
        if (found == 1) {
            break;
        }
        while (i < 7 && inode.indirect_ptr[i+1] == -1) {
            i++;
        }
        if (i > 6) {
            break;
        }

        // puts next block to read into ptr_blk
        if (bio_read(inode.indirect_ptr[i+1], ptr_blk) < 0) {
            ERROR("Failed to read disk");
            free(fname_cpy);
            free(dirent_blk);
            free(ptr_blk);
            return -1;
        }
        for (int k = 0; k < 16; ++k) {
            if (((int *)ptr_blk)[k] == -1) {
                continue;
            }
            if (bio_read(((int *)ptr_blk)[k], dirent_blk) < 0) {
                ERROR("Failed to read disk");
                free(fname_cpy);
                free(dirent_blk);
                free(ptr_blk);
                return -1;
            }
        }

        i++;
    }

    if (found == 0) {
        ERROR("Failed to find directory");
        return -1;
    }

    free(dirent_blk);
    free(ptr_blk);
    free(fname_cpy);
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

    void *blk = malloc(BLOCK_SIZE);
    if (blk == NULL) {
        ERROR("Failed to allocate memory");
        return -1;
    }

    // write superblock information
    superblock = (struct superblock) {
            .magic_num      = MAGIC_NUM,
            .max_inum       = MAX_INUM,
            .max_dnum       = MAX_DNUM,
            .i_bitmap_blk   = 1,
            .d_bitmap_blk   = 2,
            .i_start_blk    = 3,
            .d_start_blk    = (2 + ((MAX_INUM/i_per_blk) + (MAX_INUM%i_per_blk != 0))) + 1
    };

    memcpy(blk, &superblock, sizeof(struct superblock));
    if ((bio_write(0, blk) < 0) && errno) {
        ERROR("Failed to write to disk");
        free(blk);
        return -1;
    }

    // initialize inode bitmap (skipped, statically defined)
    // initialize data block bitmap (skipped, statically defined)

    // update bitmap information for root directory
    int dirent_blks = (3/dirents_per_blk) + ((3%dirents_per_blk) != 0);
    static struct dirent dirent[3];
    dirent[0] = (struct dirent) { .ino = 0, .valid = 1, .name = "/"  };
    dirent[1] = (struct dirent) { .ino = 1, .valid = 1, .name = "."  };
    dirent[2] = (struct dirent) { .ino = 2, .valid = 1, .name = ".." };
    for (int i = 0; i < dirent_blks; ++i) {
        for (int j = 0; j < dirents_per_blk; ++j) {
            int pos = (i*dirent_blks) + j;
            if (pos >= 2) {
                while (j < dirents_per_blk) {
                    ((struct dirent *)blk)[pos].valid = 0;
                    j++;
                }
                break;
            }
            memcpy(&((struct dirent *)blk)[pos], &dirent[pos], sizeof(struct dirent));
        }
        if ((bio_write((superblock.d_start_blk+1), blk) < 0) && errno) {
            ERROR("Failed to write to disk");
            free(blk);
            return -1;
        }
    }

    // update inode for root directory
    struct inode i_root = {
            .ino    = 0,
            .valid  = 1,
            .size   = dirent_blks*BLOCK_SIZE,
            .type   = directory,
            .link   = 0
    };
    for (int i = 0; i < 8; ++i) {
        if (i < dirent_blks) {
            i_root.direct_ptr[i]    =  i;
            i_root.link++;
        } else {
            i_root.direct_ptr[i]    = -1;
        }
            i_root.direct_ptr[16-i] = -1;
            i_root.indirect_ptr[i]  = -1;
    }
    memcpy(blk, &i_root, sizeof(struct inode));
    if ((bio_write(superblock.i_start_blk, blk) < 0) && errno) {
        ERROR("Failed to write to disk");
        free(blk);
        return -1;
    }

    free(blk);
    return 0;
}


/*
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {

    // Step 1a: If disk file is not found, call mkfs
    if (dev_open(diskfile_path)) {
        if (tfs_mkfs()) {
            exit(EXIT_FAILURE);
        }
        return NULL;
    }

    // Step 1b: If disk file is found, just initialize in-memory data structures
    // and read superblock from disk
    if ((bio_read(0, &superblock)) < 0) {
        ERROR("Failed to read disk");
        exit(EXIT_FAILURE);
    }
    if (superblock.magic_num != MAGIC_NUM) {
        ERROR( "disk's filesystem is not recognized");
        exit(EXIT_FAILURE);
    }

    i_per_blk = (int)((double)BLOCK_SIZE/sizeof(struct dirent));
    dirents_per_blk = (int)((double)BLOCK_SIZE/sizeof(struct dirent));

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

