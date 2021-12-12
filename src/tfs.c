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

// Declare your in-memory data structures here
struct superblock superblock;
unsigned char i_bitmap[MAX_INUM] = {0};
unsigned char d_bitmap[MAX_DNUM] = {0};

int i_per_blk = (int)((double)BLOCK_SIZE/sizeof(struct dirent));
int dirents_per_blk = (int)((double)BLOCK_SIZE/sizeof(struct dirent));

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

    int avail_ino = -1;


	// Step 1: Read inode bitmap from disk
    int *bitmap_blk = calloc(1, BLOCK_SIZE);
    if(!bitmap_blk) {
        perror("Failed to allocate memory");
        return -1;
    }
    if(bio_read(superblock.i_bitmap_blk, bitmap_blk) < 0) {
        free(bitmap_blk);
        return -1;
    }
    memcpy(i_bitmap, bitmap_blk, sizeof(i_bitmap));
    free(bitmap_blk);
	
	// Step 2: Traverse inode bitmap to find an available slot
    for(int i = 0; i < MAX_DNUM; ++i) {
        if(!get_bitmap(i_bitmap, i)) {
            avail_ino = i;
            break;
        }
    }


	// Step 3: Update inode bitmap and write to disk
    set_bitmap(i_bitmap, avail_ino);
    if(bio_write(superblock.i_bitmap_blk, i_bitmap) < 0) {
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
    int *bitmap_blk = calloc(1, BLOCK_SIZE);
    if(!bitmap_blk) {
        perror("Failed to allocate memory");
        return -1;
    }
    if(bio_read(superblock.d_bitmap_blk, bitmap_blk) < 0) {
        free(bitmap_blk);
        return -1;
    }
    memcpy(d_bitmap, bitmap_blk, sizeof(d_bitmap));
    free(bitmap_blk);


	// Step 2: Traverse data block bitmap to find an available slot
    for(int i = 0; i < MAX_DNUM; ++i) {
        if(!get_bitmap(d_bitmap, i)) {
            avail_blkno = i;
            break;
        }
    }

    // if no available data block has been found
    if(avail_blkno < 0) {
        perror("No available data block");
        return -1;
    }


	// Step 3: Update data block bitmap and write to disk
    set_bitmap(d_bitmap, avail_blkno);
    if(bio_write(superblock.d_bitmap_blk, d_bitmap) < 0) return -1;


	return avail_blkno;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

    struct inode *i_blk = malloc(BLOCK_SIZE);
    if(!i_blk) {
        perror("Failed to allocate memory");
        return -1;
    }


    // Step 1: Get the inode's on-disk block number
    // Step 2: Get offset of the inode in the inode on-disk block
    int indx = ino%i_per_blk;
    int i_blkno = (ino/i_per_blk) + (!indx);


    // Step 3: Read the block from disk and then copy into inode structure
    if(bio_read((superblock.i_start_blk + i_blkno), i_blk) < 0) {
        free(i_blk);
        return -1;
    }
    memcpy(inode, &i_blk[indx], sizeof(struct inode));


    free(i_blk);
    return 0;
}

int writei(uint16_t ino, struct inode *inode) {

    struct inode *i_blk = malloc(BLOCK_SIZE);
    if(!i_blk) {
        perror("Failed to allocate memory");
        return -1;
    }


	// Step 1: Get the block number where this inode resides on disk
	// Step 2: Get the offset in the block where this inode resides on disk
    int indx = ino%i_per_blk;
    int i_blkno = (ino/i_per_blk) + (!indx);


	// Step 3: Write inode to disk
    if(bio_read((superblock.i_start_blk + i_blkno), i_blk) < 0) {
        free(i_blk);
        return -1;
    }

    memcpy(&i_blk[indx], inode, sizeof(struct inode));
    if(bio_write((superblock.i_start_blk + i_blkno), i_blk) < 0) {
        free(i_blk);
        return -1;
    }


    free(i_blk);
	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

    int DISK_ERROR = 0;
    int FOUND = 0;

    char delim [2] = "/";
    char *fname_CPY1 = strdup(fname);
    char *fname_CPY2 = strdup(fname);
    struct inode inode = {0};
    struct dirent *dirent_blk = malloc(BLOCK_SIZE);
    int *ptr_blk = calloc(1, BLOCK_SIZE);

    // Step 1: Call readi() to get the inode using ino (inode number of current directory)
    // Step 2: Get data block of current directory from inode
    if(!fname_CPY1
    || !fname_CPY2
    || !dirent_blk
    || !ptr_blk
    || readi(ino, &inode) < 0
   ) {
        if(!fname_CPY1) free(fname_CPY1);
        if(!fname_CPY2) free(fname_CPY2);
        if(!dirent_blk) free(dirent_blk);
        if(!ptr_blk)    free(ptr_blk);
        ERROR("Failed to allocate memory");
    }
    char *f_basename = basename(fname_CPY2);


    // Step 3: Read directory's data block and check each directory entry.
    // If the name matches, then copy directory entry to dirent structure
    for(char *path = strtok(fname_CPY1, delim); path != NULL; path = strtok(NULL, delim)) {

        // reset found to 0
        FOUND = 0;

        // search inode for directory/subdirectory
        memcpy(ptr_blk, inode.direct_ptr, sizeof(inode.direct_ptr));
        for(int i = -1; i < 8; ) {
            for(int j = 0; j < 16; ++j) {

                // if unused section of direct array has been reached
                if(ptr_blk[j] < 0) break;

                // clear block
                memset(dirent_blk, 0, BLOCK_SIZE);

                // read block from the direct array entry
                if(bio_read((superblock.d_start_blk + ptr_blk[j]), dirent_blk) < 0) {
                    DISK_ERROR = 1;
                    break;
                }

                // search block for the directory/subdirectory entry
                for(int k = 0; k < dirents_per_blk; ++k) {

                    // if we reached unused section of directory entry block
                    if(!dirent_blk[k].valid) break;

                    // if the directory entry matches the directory/subdirectory name
                    if(!strcmp(path, dirent_blk[k].name)) {

                        // if the basename has been found
                        if(!strcmp(path, f_basename)) memcpy(dirent, &dirent_blk[k], sizeof(struct dirent));

                        // read inode
                        readi(dirent_blk[k].ino, &inode);

                        // set found to 1 and exit loop
                        FOUND = 1;
                        break;
                    }
                }

                // if directory/subdirectory has been found
                if(FOUND) break;
            }

            // if the directory/subdirectory has been found or if a disk error occurred
            if(FOUND || DISK_ERROR) break;

            // if we haven't checked all indirect array entries
            if((++i) < 8) {

                // if the unused pointer section of the indirect pointer array has been reached
                if(inode.indirect_ptr[i] < 0) break;

                // read array block from the indirect array entry
                if(bio_read((superblock.d_start_blk + inode.indirect_ptr[i]), ptr_blk)) {
                    DISK_ERROR = 1;
                    break;
                }
            }
        }

        // if directory/subdirectory has not been found
        if(!FOUND) break;
    }


    free(fname_CPY1);
    free(fname_CPY2);
    free(dirent_blk);
    free(ptr_blk);
    if(DISK_ERROR) {
        perror("Failed to find directory");
        return -1;
    }
    if(!FOUND) {
        perror("Directory doesn't exists");
        return -1;
    }
	return 0;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

    int DISK_ERROR = 0;
    int DIRECTORY_ADDED = 0;

    char delim [2] = "/";
    char *fname_CPY1 = strdup(fname);
    char *fname_CPY2 = strdup(fname);
    struct inode inode = {0};
    struct dirent dirent = {0};
    struct dirent *dirent_blk = malloc(BLOCK_SIZE);
    int *ptr_blk = calloc(1, BLOCK_SIZE);
    if(!fname_CPY1
    || !fname_CPY2
    || !dirent_blk
    || !ptr_blk
    ) {
        if(!fname_CPY1) free(fname_CPY1);
        if(!fname_CPY2) free(fname_CPY2);
        if(!dirent_blk) free(dirent_blk);
        if(!ptr_blk)    free(ptr_blk);
        ERROR("Failed to allocate memory.");
    }
    char *f_basename = basename(fname_CPY1);
    char *f_dirname = dirname(fname_CPY2);
    struct dirent f_dirent = { .valid = 1, .ino = f_ino };
    strcpy(f_dirent.name, fname);


    // Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	// Step 2: Check if fname (directory name) is already used in other entries
    if(dir_find(dir_inode.ino, f_dirname, strlen(f_dirname), &dirent)) {
        free(fname_CPY1);
        free(fname_CPY2);
        free(dirent_blk);
        free(ptr_blk);
        return -1;
    }

    readi(dirent.ino, &dir_inode);
    if(!dir_find(dir_inode.ino, f_basename, strlen(f_basename), &dirent)) {
        perror("Directory already exists");
        free(fname_CPY1);
        free(fname_CPY2);
        free(dirent_blk);
        free(ptr_blk);
        return -1;
    }


	// Step 3: Add directory entry in dir_inode's data block and write to disk
	// Allocate a new data block for this directory if it does not exist
	// Update directory inode
	// Write directory entry
    memcpy(ptr_blk, dir_inode.direct_ptr, sizeof(dir_inode.direct_ptr));
    for(int i = -1; i < 8; ) {
        for(int j = 0; j < 16; ++j) {

            // if array is not in use
            if(ptr_blk[j] < 0)  {

                // clear dirent block
                memset(dirent_blk, 0, BLOCK_SIZE);

                // set array entry to a new data block
                ptr_blk[j] = get_avail_blkno();
            } else {

                // read block from pointer array entry
                bio_read(ptr_blk[j], dirent_blk);

                // search the directory entry block
                for(int k = 0; k < dirents_per_blk; ++k) {

                    // if the entry is not in use
                    if(!dirent_blk[k].valid) {

                        // add the directory entry to the directory entry block
                        memcpy(&dirent_blk[k], &f_dirent, sizeof(struct dirent));

                        DIRECTORY_ADDED = 1;
                    }
                }

                // if the directory was not added, go to next iteration
                if(!DIRECTORY_ADDED) continue;
            }

            // write directory entry to disk
            if(bio_write((superblock.d_start_blk + ptr_blk[j]), dirent_blk) < 0) {
                DISK_ERROR = 1;
                break;
            }

            // if it's a data block, copy the pointer array block to the direct pointer array
            // else write pointer array block to disk
            if(i < 0) memcpy(dir_inode.direct_ptr, ptr_blk, sizeof(dir_inode.direct_ptr));
            else if(bio_write((superblock.d_start_blk + dir_inode.indirect_ptr[i]), ptr_blk) < 0) {
                DISK_ERROR = 1;
                break;
            }

            // update directory inode
            dir_inode.link++;
            if(writei(dir_inode.ino, &dir_inode) < 0) {
                DISK_ERROR = -1;
                break;
            }


            DIRECTORY_ADDED = 1;
            break;
        }

        // if the directory has been added or if a disk error occurred
        if(DIRECTORY_ADDED || DISK_ERROR) break;

        // if we haven't checked all indirect array entries
        // else, all directory entries are in use
        if((++i) < 8) {

            // if indirect array entry is unused
            if(dir_inode.indirect_ptr[i] < 0) {

                // get available data block address
                int array_blkno = get_avail_blkno();
                if(array_blkno < 0) {
                    DISK_ERROR = 1;
                    break;
                }

                // clear block
                memset(ptr_blk, 0, BLOCK_SIZE);

                // set unused entries to -1
                for(int j = 1; j < 16; ++j) ptr_blk[j] = -1;

                // set indirect pointer entry to pointer array block number
                dir_inode.indirect_ptr[i] = array_blkno;

                // update inode size
                dir_inode.size += BLOCK_SIZE;

                //update bitmap
                set_bitmap(d_bitmap, array_blkno);
                if(bio_write(superblock.d_bitmap_blk, d_bitmap) < 0) {
                    DISK_ERROR = -1;
                    break;
                }
            }

            // read pointer array block from the indirect array entry
            if(bio_read((superblock.d_start_blk + dir_inode.indirect_ptr[i]), ptr_blk)) {
                DISK_ERROR = 1;
                break;
            }
        } else perror("All directory entries are in use");
    }


    free(fname_CPY1);
    free(fname_CPY2);
    free(dirent_blk);
    free(ptr_blk);
    if(DISK_ERROR) {
        perror("Failed to make directory");
        return -1;
    }
	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

    int DISK_ERROR = 0;
    int DIRECTORY_REMOVED = 0;


    char delim [2] = "/";
    char *fname_CPY1 = strdup(fname);
    char *fname_CPY2 = strdup(fname);
    struct inode inode = {0};
    struct dirent dirent = {0};
    struct dirent *dirent_blk = malloc(BLOCK_SIZE);
    int *ptr_blk = calloc(1, BLOCK_SIZE);
    if(fname_CPY1 == NULL
    || fname_CPY2 == NULL
    || dirent_blk == NULL
    || ptr_blk    == NULL
    ) {
        if(fname_CPY1   != NULL) free(fname_CPY1);
        if(fname_CPY2   != NULL) free(fname_CPY2);
        if(dirent_blk   != NULL) free(dirent_blk);
        if(ptr_blk      != NULL) free(ptr_blk);
        ERROR("Failed to allocate memory.");
    }
    char *f_basename = basename(fname_CPY1);
    char *f_dirname = dirname(fname_CPY2);

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	// Step 2: Check if fname exist
    if(dir_find(dir_inode.ino, f_dirname, strlen(f_dirname), &dirent)) {
        fprintf(stderr, "Could not open %s\n", f_dirname);
        free(fname_CPY1);
        free(fname_CPY2);
        free(dirent_blk);
        free(ptr_blk);
        return -1;
    }

    readi(dirent.ino, &dir_inode);
    if(dir_find(dir_inode.ino, f_basename, strlen(f_basename), &dirent)) {
        perror("Directory doesn't exists");
        free(fname_CPY1);
        free(fname_CPY2);
        free(dirent_blk);
        free(ptr_blk);
        return -1;
    }


	// Step 3: If exist, then remove it from dir_inode's data block and write to disk
    for(int i = -1; i < 8; ) {
        for(int j = 0; j < 16; ++j) {

            // if we reached unused section of the pointer array
            if(ptr_blk[j] < 0) break;

            // read the directory entry block from the pointer array entry
            if(bio_read(superblock.d_start_blk + ptr_blk[j], dirent_blk) < 0) {
                DISK_ERROR = 1;
                break;
            }

            // check directory entry block for the directory we want to remove
            for(int k = 0; k < dirents_per_blk; ++k) {

                // if we reached unused section of directory entry block
                if(!dirent_blk[k].valid) break;

                // if the directory entry matches the directory we want to remove
                if(!strcmp(dirent_blk[k].name, f_basename)) {

                    int DELETE_BLOCK = 1;

                    // invalidate directory
                    dirent_blk[k].valid = 0;

                    // check if there are no valid directory entries left in the block
                    for(int l = 0; l < k; ++l) {

                        // if there exists valid directory entries
                        if(!dirent_blk[l].valid) {
                            DELETE_BLOCK = 0;
                            break;
                        }
                    }

                    // continue checking for directory entries and shift valid entries down by one
                    for(int l = k+1; l < dirents_per_blk; ++l) {

                        // if the directory entry is valid
                        if(dirent_blk[l].valid) {
                            DELETE_BLOCK = 0;
                            memcpy(&dirent_blk[l-1], &dirent_blk[l], sizeof(struct dirent));
                        } else break;
                    }

                    // if there are no valid directory entries in the block
                    // and if the pointer block is from an indirect pointer entry
                    if(DELETE_BLOCK && (i >= 0)) {

                        // clear block in disk
                        memset(ptr_blk, 0, BLOCK_SIZE);
                        if (bio_write((superblock.d_start_blk + dir_inode.indirect_ptr[i]), ptr_blk) < 0) {
                            DISK_ERROR = 1;
                            break;
                        }

                        // update data bitmap
                        unset_bitmap(d_bitmap, dir_inode.indirect_ptr[i]);
                        if(bio_write(superblock.d_bitmap_blk, d_bitmap) < 0) {
                            DISK_ERROR = 1;
                            break;
                        }

                        // update inode size
                        dir_inode.size -= BLOCK_SIZE;
                    } else {

                        // clear direct pointer block
                        memset(dir_inode.direct_ptr, 0, sizeof(dir_inode.direct_ptr));
                    }

                    // update directory inode
                    dir_inode.link--;
                    if(writei(dir_inode.ino, &dir_inode) < 0) {
                        DISK_ERROR = 1;
                        break;
                    }


                    DIRECTORY_REMOVED = 1;
                    break;
                }
            }
        }

        // if the directory has been added or if a disk error occurred
        if(DIRECTORY_REMOVED || DISK_ERROR) break;

        // if we haven't checked all indirect array entries
        // else, all directory entries are in use
        if((++i) < 8) {

            // if the unused section of the indirect array has been reached, break outer loop
            if(dir_inode.indirect_ptr[i] < 0) break;

            // read pointer array block from the indirect array entry
            if(bio_read((superblock.d_start_blk + dir_inode.indirect_ptr[i]), ptr_blk)) {
                DISK_ERROR = 1;
                break;
            }
        } else perror("All directory entries are in use");
    }

    free(fname_CPY1);
    free(fname_CPY2);
    free(dirent_blk);
    free(ptr_blk);
    if(DISK_ERROR) {
        perror("Failed to remove directory");
        return -1;
    }
    if(!DIRECTORY_REMOVED) {
        perror("Directory doesn't exists");
        return -1;
    }
	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {

    struct dirent dirent = {0};

	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way
    if(dir_find(0, path, strlen(path), &dirent) < 0) return -1;
    readi(dirent.ino, inode);


	return 0;
}

/* 
 * Make file system
 */
int tfs_mkfs() {

    int DISK_ERROR = 0;
    void *blk = calloc(1, BLOCK_SIZE);
    if(!blk) {
        perror("Failed to allocate memory");
        return -1;
    }

	// Call dev_init() to initialize (Create) Diskfile
    dev_init(diskfile_path);


	// write superblock information
    superblock = (struct superblock) {
        .magic_num = MAGIC_NUM,
        .max_inum = MAX_INUM,
        .max_dnum = MAX_DNUM,
        .i_bitmap_blk = 1,
        .d_bitmap_blk = 2,
        .i_start_blk = 3,
        .d_start_blk = (2 + ((MAX_INUM/i_per_blk) + (!(MAX_INUM%i_per_blk)))) + 1
    };
    memcpy(blk, &superblock, sizeof(struct superblock));
    if(bio_write(0, blk) < 0) {
        free(blk);
        return -1;
    }


    // initialize inode bitmap
    memset(blk, 0, BLOCK_SIZE);
    memcpy(blk, i_bitmap, sizeof(i_bitmap));
    if (bio_write(superblock.i_bitmap_blk, blk) < 0) {
        free(blk);
        return -1;
    }


	// initialize data block bitmap
    memset(blk, 0, BLOCK_SIZE);
    memcpy(blk, d_bitmap, sizeof(d_bitmap));
    if (bio_write(superblock.d_bitmap_blk, blk) < 0) {
        free(blk);
        return -1;
    }


    int dirent_blks = (3/dirents_per_blk) + ((3%dirents_per_blk) != 0);


    // update inode for root directory
    struct inode root_inode = {
        .ino = 0,
        .valid = 1,
        .size = dirent_blks*BLOCK_SIZE,
        .type = directory,
        .link = 3,
        .vstat = {
                .st_ino = 0,
                .st_mode = S_IFDIR | 0755,
                .st_nlink = 2,
                .st_blksize = BLOCK_SIZE,
                .st_blocks = dirent_blks,
                .st_size = dirent_blks*BLOCK_SIZE
        }
    };
    time(&root_inode.vstat.st_atime);
    time(&root_inode.vstat.st_mtime);
    time(&root_inode.vstat.st_ctime);



    // initialize inode direct and indirect pointer arrays
    for(int i = 0; i < 8; ++i) {
        root_inode.direct_ptr[i] = -1;
        root_inode.indirect_ptr[i] = -1;
        root_inode.indirect_ptr[15-i] = -1;
    }


    // write root inode to disk
    writei(root_inode.ino, &root_inode);

    // link directory entry blocks to pointer arrays
    if(dir_add(root_inode, root_inode.ino, "/", strlen("/"))
    || dir_add(root_inode, root_inode.ino, ".", strlen("."))
    || dir_add(root_inode, root_inode.ino, "..", strlen(".."))
    ) DISK_ERROR = -1;


    free(blk);
    if(DISK_ERROR) {
        perror("Failed to initialize disk");
        return -1;
    }
	return 0;
}


/* 
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {

    // Step 1a: If disk file is not found, call mkfs
    if(dev_open(diskfile_path) < 0) {
        if(tfs_mkfs() < 0) exit(EXIT_FAILURE);
        return NULL;
    }


    // Step 1b: If disk file is found, just initialize in-memory data structures
    // and read superblock from disk
    if((bio_read(0, &superblock)) < 0) {
        ERROR("Failed to read disk");
        exit(EXIT_FAILURE);
    }

    if(superblock.magic_num != MAGIC_NUM) {
        ERROR( "Disk's filesystem is not recognized");
        exit(EXIT_FAILURE);
    }


	return NULL;
}

static void tfs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures (skipped, all on stack)


	// Step 2: Close diskfile
    dev_close(diskfile_path);
}

static int tfs_getattr(const char *path, struct stat *stbuf) {

    struct inode inode;


    // Step 1: call get_node_by_path() to get inode from path
    if(get_node_by_path(path, 0, &inode) < 0) return -1;

	// Step 2: fill attribute of file into stbuf from inode
    memcpy(stbuf, &inode.vstat, sizeof(struct stat));
    time(&stbuf->st_mtime);


	return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {

    struct inode inode = {0};


	// Step 1: Call get_node_by_path() to get inode from path
    // Step 2: If not find, return -1
    if(get_node_by_path(path, 0, &inode) < 0) return -1;


    return 0;
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

    int DISK_ERROR = 0;
    offset = 0;

    struct inode *inode = {0};
    struct inode *i_blk = malloc(BLOCK_SIZE);
    struct dirent *dirent_blk = malloc(BLOCK_SIZE);
    int *ptr_blk = malloc(BLOCK_SIZE);


	// Step 1: Call get_node_by_path() to get inode from path
    if(get_node_by_path(path, 0, &inode) < 0) return -1;

	// Step 2: Read directory entries from its data blocks, and copy them to filler
    memcpy(ptr_blk, inode->direct_ptr, sizeof(inode->direct_ptr));
    for(int i = 0; i < 8; ) {
        for(int j = 0; j < 16; ++j) {

            // if you reach unused section of pointer array
            if(ptr_blk[j] < 0) break;

            // clear block
            memset(dirent_blk, 0, BLOCK_SIZE);

            // read block from the direct array entry
            if(bio_read((superblock.d_start_blk+ ptr_blk[j]), dirent_blk) < 0) {
                DISK_ERROR = -1;
                break;
            }

            // search for directory entries in the block
            for(int k = 0; k < dirents_per_blk; ++k) {

                // if we've reached unused section of the directory entries block
                if(!dirent_blk[k].valid) break;

                //get block's inode
                struct inode temp_inode = {0};
                readi(dirent_blk[k].ino, &temp_inode);

                // add entry to buffer
                if(!filler(buffer, dirent_blk[k].name, &temp_inode.vstat, (((++offset)*sizeof(struct dirent))))) {
                    DISK_ERROR = -1;
                }
            }
        }

        // if stop is set, break outer loop
        if(DISK_ERROR) break;

        // if we haven't checked all indirect array entries
        if((++i) < 8) {

            // if the unused pointer section of the indirect pointer array has been reached
            if(inode->indirect_ptr[i] < 0) break;

            // read array block from the indirect array entry
            if(bio_read((superblock.d_start_blk + inode->indirect_ptr[i]), ptr_blk)) {
                DISK_ERROR = 1;
                break;
            }
        }
    }


    free(ptr_blk);
    free(dirent_blk);
    if(DISK_ERROR) {
        perror("Failed to read directory");
        return -1;
    }
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

