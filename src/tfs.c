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
    if (bio_read(superblock.i_bitmap_blk, i_bitmap) < 0) {
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
    if (bio_write(superblock.i_bitmap_blk, i_bitmap) < 0) {
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
    if (bio_read(superblock.d_bitmap_blk, d_bitmap) < 0) {
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
    if (bio_write(superblock.i_bitmap_blk, d_bitmap) < 0) {
        ERROR("Failed to write to disk");
        return -1;
    }

    return avail_blkno;
}

/*
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

    struct inode *i_blk = malloc(BLOCK_SIZE);
    if (i_blk == NULL) {
        ERROR("Failed to allocate memory");
        return -1;
    }

    // Step 1: Get the inode's on-disk block number
    // Step 2: Get offset of the inode in the inode on-disk block
    int i_offset = ino%i_per_blk;
    int i_blkno = superblock.i_start_blk + ((ino/i_per_blk) + (i_offset != 0));

    // Step 3: Read the block from disk and then copy into inode structure
    if (bio_read(i_blkno, i_blk) < 0) {
        free(i_blk);
        ERROR("Failed to read disk");
        return -1;
    }
    memcpy(inode, &i_blk[i_offset], sizeof(struct inode));

    free(i_blk);
    return 0;
}

int writei(uint16_t ino, struct inode *inode) {

    struct inode *i_blk = malloc(BLOCK_SIZE);
    if (i_blk == NULL) {
        ERROR("Failed to allocate memory");
        return -1;
    }

    // Step 1: Get the block number where this inode resides on disk
    // Step 2: Get the offset in the block where this inode resides on disk
    int i_offset = ino%i_per_blk;
    int i_blkno = superblock.i_start_blk + ((ino/i_per_blk) + (i_offset != 0));

    // Step 3: Write inode to disk
    if (bio_read(i_blkno, i_blk) < 0) {
        free(i_blk);
        ERROR("Failed to read disk");
        return -1;
    }
    memcpy(&i_blk[i_offset], inode, sizeof(struct inode));
    if (bio_write(i_blkno, i_blk) < 0) {
        ERROR("Failed to write to disk");
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
    int at_base = 0;
    int found = 0;
    int stop = 0;

    char delim [2] = "/";
    char *fname_CPY1 = strdup(fname);
    char *fname_CPY2 = strdup(fname);
    struct inode inode = {0};
    struct inode *i_blk = malloc(BLOCK_SIZE);
    struct dirent *dirent_blk = malloc(BLOCK_SIZE);
    int *ptr_blk = malloc(BLOCK_SIZE);

    // Step 1: Call readi() to get the inode using ino (inode number of current directory)
    if (   fname_CPY1   == NULL
        || fname_CPY2   == NULL
        || dirent_blk   == NULL
        || ptr_blk      == NULL
        || i_blk        == NULL
        || readi(ino, i_blk) < 0
       ) {
        if(fname_CPY1   != NULL) free(fname_CPY1);
        if(fname_CPY2   != NULL) free(fname_CPY2);
        if(i_blk        != NULL) free(i_blk);
        if(dirent_blk   != NULL) free(dirent_blk);
        if(ptr_blk      != NULL) free(ptr_blk);
    }
    memcpy(&inode, i_blk, sizeof(struct inode));
    char *path = strtok(fname_CPY1, delim);
    char *f_basename = basename(fname_CPY2);

    // Step 2: Get data block of current directory from inode
    // Step 3: Read directory's data block and check each directory entry.
    // If the name matches, then copy directory entry to dirent structure
    memcpy(ptr_blk, inode.direct_ptr, sizeof(inode.direct_ptr));
    for (; path != NULL; path = strtok(NULL, delim)) {
        int goto_next_subdir = 0;
        for (int i = -1; i < 8; ) {
            for (int j = 0; j < 16; ++j) {
                /* If we reached unused section of direct array. */
                if (ptr_blk[j] < 0) {
                    stop = 1;
                    break;
                }

                /* Read block pointed to at the direct array index, j. */
                if (bio_read(ptr_blk[j], dirent_blk) < 0) {
                    ERROR("Failed to read disk");
                    stop = 1;
                    DISK_ERROR = 1;
                    break;
                }

                /* Search block for the directory/subdirectory. */
                for (int k = 0; k < dirents_per_blk; ++k) {
                    /* Break if we reach an invalid directory entries.
                     * We separate invalid and valid directory entries. */
                    if (dirent_blk[k].valid == 0) {
                        break;
                    }

                    /* If dirent matches the directory we're looking for. */
                    if (strcmp(path, dirent_blk[k].name) == 0) {
                        /* If we're at the base directory,
                         * copy the directory entry and break out of all loops. */
                        if ((at_base = (strcmp(path, f_basename) == 0))) {
                            found = 1;
                            stop = 1;
                            memcpy(dirent, &dirent_blk[k], sizeof(struct dirent));
                            break;
                        }

                        /* Otherwise, setup inode and ptr_blk go and search next subdirectory. */
                        if (readi(dirent_blk[k].ino, i_blk) < 0) {
                            stop = 1;
                            DISK_ERROR = 1;
                            break;
                        }
                        memcpy(&inode, i_blk, sizeof(struct inode));
                        memcpy(ptr_blk, inode.direct_ptr, sizeof(inode.direct_ptr));
                        goto_next_subdir = 1;
                        break;
                    }
                }

                /* If we reached unused section of array or if we
                 * found next subdir, break out of direct array loop. */
                if (stop || goto_next_subdir) break;
            }

            /* If we reached unused section of array or if we
             * found next subdir, break out of indirect array loop. */
            if (stop || goto_next_subdir) break;

            /* If we haven't searched all valid indirect pointer entries,
             * get next indirect pointer and set ptr_blk for next iteration.
             * Otherwise, break out of indirect pointer loop. */
            if((++i) < 8) {
                /* If we've reached an invalid indirect pointer entries section */
                if(inode.indirect_ptr[i] == -1) {
                    stop = 1;
                    break;
                }
                if (bio_read(inode.indirect_ptr[i], ptr_blk) < 0) {
                    ERROR("Failed to read disk");
                    stop = 1;
                    DISK_ERROR = 1;
                }
            } else break;
        }
        if (stop) break;
    }

    free(fname_CPY1);
    free(fname_CPY2);
    free(i_blk);
    free(dirent_blk);
    free(ptr_blk);
    if (DISK_ERROR == 0) return -1;
    if (found == 0) {
        if(at_base) ERROR("Invalid path: %s doesn't exist", path);
        return -1;
    }
    return 0;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

    int DISK_ERROR = 0;
    int duplicate = 0;
    int stop = 0;

    int first_invalid_direct_blk = -1;
    int last_valid_direct_blk = -1;
    int last_valid_indirect_blk = -2;
    int first_invalid_indirect_blk = -1;
    int first_invalid_dirent_slot = -1;

    char delim [2] = "/";
    char *fname_CPY1 = strdup(fname);
    char *fname_CPY2 = strdup(fname);
    char *fname_CPY3 = strdup(fname);
    struct inode inode = {0};
    struct dirent dirent = {0};
    struct dirent *dirent_blk = malloc(BLOCK_SIZE);
    int *ptr_blk = malloc(BLOCK_SIZE);

    // Step 1: Read dir_inode's data block and check each directory entry of dir_inode
    if (   fname_CPY1   == NULL
        || fname_CPY2   == NULL
        || fname_CPY3   == NULL
        || dirent_blk   == NULL
        || ptr_blk      == NULL
       ) {
        if(fname_CPY1   != NULL) free(fname_CPY1);
        if(fname_CPY2   != NULL) free(fname_CPY2);
        if(fname_CPY3   != NULL) free(fname_CPY3);
        if(dirent_blk   != NULL) free(dirent_blk);
        if(ptr_blk      != NULL) free(ptr_blk);
    }
    char *path = strtok(fname_CPY1, delim);
    char *f_basename = basename(fname_CPY2);
    char *f_dirname = dirname(fname_CPY3);

    /* Get immediate subdirectory's inode */
    if (dir_find(dir_inode.ino, f_dirname, strlen(f_dirname), &dirent) < 0) {
        TRACEBACK;
        free(fname_CPY1);
        free(fname_CPY2);
        free(fname_CPY3);
        free(dirent_blk);
        free(ptr_blk);
        return -1;
    }
    readi(dirent.ino, &inode);

    // Step 2: Check if fname (directory name) is already used in other entries
    memcpy(ptr_blk, inode.direct_ptr, sizeof(inode.direct_ptr));
    for (int i = -1; i < 8; ) {
        for (int j = 0; j < 16; ++j) {
            /* If we reached unused section of direct array. */
            if (ptr_blk[j] < 0) {
                if(first_invalid_direct_blk < 0) {
                    last_valid_indirect_blk = i;
                    first_invalid_direct_blk = j;
                }
                stop = 1;
                break;
            }

            /* Read block pointed to at the direct array index, j.*/
            if (bio_read(ptr_blk[j], dirent_blk) < 0) {
                ERROR("Failed to read disk");
                stop = 1;
                DISK_ERROR = 1;
                break;
            }

            /* Search all blocks for the directory/subdirectory. */
            for (int k = 0; k < dirents_per_blk; ++k) {
                /* Break if we reach an invalid directory entries.
                 * We separate invalid and valid directory entries. */
                if (dirent_blk[k].valid == 0) {
                    if(first_invalid_dirent_slot < 0) {
                        last_valid_indirect_blk = i;
                        last_valid_direct_blk = j;
                        first_invalid_dirent_slot = k;
                        break;
                    }
                }

                /* If a duplicate directory is found. */
                if (strcmp(path, dirent_blk[k].name) == 0) {
                    duplicate = 1;
                    stop = 1;
                    break;
                }
            }

            /* If we reached unused section of array, break out of
             * direct array loop.                                   */
            if (stop) break;
        }

        /* If we haven't searched all valid indirect pointer entries,
         * get next indirect pointer and set ptr_blk for next iteration.
         * Otherwise, break out of indirect pointer loop.                */
        if((++i) < 8) {
            /* If we've reached invalid indirect pointer entries
             * section.                                             */
            if(inode.indirect_ptr[i] == -1) {
                first_invalid_indirect_blk = i;
                stop = 1;
                break;
            }
            if (bio_read(inode.indirect_ptr[i], ptr_blk) < 0) {
                ERROR("Failed to read disk");
                stop = 1;
                DISK_ERROR = 1;
            }
            last_valid_indirect_blk = i;
        } else break;
    }


    // Step 3: Add directory entry in dir_inode's data block and write to disk
    // Allocate a new data block for this directory if it does not exist
    // Update directory inode
    // Write directory entry
    /* Use single iteration loop for quickly exiting function to avoid
     * duplicate lines frees/returns and to avoid unwieldy if statement
     * logic for disk error handling.                                   */
    while (duplicate == 0) {

        int dirent_indx = first_invalid_dirent_slot;
        int ptr_indx = last_valid_indirect_blk;

        /* Get new available inode number. */
        int new_ino = get_avail_ino();
        if(new_ino < 0) {
            TRACEBACK;
            DISK_ERROR = 1;
            break;
        }

        /* Create directory entry. */
        dirent.ino = new_ino;
        dirent.valid = 0;
        strcpy(dirent.name, f_basename);

        /* If there doesn't exist an invalid directory entry in already allocated blocks.
         * Else, read the dirent block into memory.  */
        if (first_invalid_dirent_slot < 0) {

            /* Get free data block address and initialize it. */
            int new_dirent_blkno = get_avail_blkno();
            for(int i = 0; i < dirents_per_blk; ++i) {
                dirent_blk[i].valid = 0;
            }

            /* If there exists an unset direct pointer block.
             * Else if there exists indirect blocks that haven't been set. */
            if (first_invalid_direct_blk > 0) {
                /* Set the direct pointer to the new dirent block. */
                inode.direct_ptr[first_invalid_direct_blk] =  new_dirent_blkno;
            } else if (first_invalid_indirect_blk >= 0) {
                /* Get a free data block's address and link to indirect pointer array. */
                int new_direct_blkno = get_avail_blkno();
                inode.indirect_ptr[first_invalid_indirect_blk] = new_direct_blkno;

                /* Initialize direct array block. */
                ptr_blk[0] = new_dirent_blkno;
                for (int i = 1; i < 16; ++i) {
                    ptr_blk[i] = -1;
                }

                /* Write direct array block to the now, not free, data block's address. */
                if(bio_write(new_direct_blkno, ptr_blk) < 0) {
                    ERROR("Failed to write to disk");
                    DISK_ERROR = -1;
                    break;
                }

                /* Set pointer and dirent indexes for write operation. */
                dirent_indx = 0;
                ptr_indx = 0;
            } else {
                ERROR("Max amount of directory entries reached.");
                DISK_ERROR = -1;
                break;
            }
        } else if (bio_read(last_valid_direct_blk, dirent_blk) < 0) {
            ERROR("Failed to read disk");
            DISK_ERROR = -1;
            break;
        }

        /* Copy new dirent into dirent block */
        memcpy(&dirent_blk[dirent_indx], &dirent, sizeof(struct dirent));

        /* Write dirent block to disk */
        if(bio_write(ptr_blk[ptr_indx], dirent_blk) < 0) {
            ERROR("Failed to write to disk");
            DISK_ERROR = 1;
            break;
        }

        /* Update links in inode */
        inode.link++;

        /* Exit loop normally */
        duplicate = 1;
    }

    free(fname_CPY1);
    free(fname_CPY2);
    free(fname_CPY3);
    free(dirent_blk);
    free(ptr_blk);
    if (DISK_ERROR) return -1;
    if (duplicate) {
        ERROR("Directory of the same name already exists");
        return -1;
    }
    return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

    // TODO Want to keep valid and invalid blocks in pointer arrays separated into left and right sections.
    // TODO Separate invalid and valid dirents into sections.
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
    if (bio_write(0, blk) < 0) {
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
            if (pos > 2) {
                while (j < dirents_per_blk) {
                    ((struct dirent *)blk)[pos].valid = 0;
                    j++;
                }
                break;
            }
            memcpy(&((struct dirent *)blk)[pos], &dirent[pos], sizeof(struct dirent));
        }
        if (bio_write((superblock.d_start_blk+1), blk) < 0) {
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
    if (bio_write(superblock.i_start_blk, blk) < 0) {
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

