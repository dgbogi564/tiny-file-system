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
    if(bio_read(superblock.i_bitmap_blk, i_bitmap) < 0) {
        ERROR("Failed to read disk");
        return -1;
    }

    // Step 2: Traverse inode bitmap to find an available slot
    for(int i = 0; i < MAX_INUM; ++i) {
        if(get_bitmap(i_bitmap, i) == 0) {
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
    errno = 0;
    if(bio_read(superblock.d_bitmap_blk, d_bitmap) < 0) {
        ERROR("Failed to read disk");
        return -1;
    }

    // Step 2: Traverse data block bitmap to find an available slot
    for(int i = 0; i < MAX_DNUM; ++i) {
        if(get_bitmap(d_bitmap, i) == 0) {
            avail_blkno = i;
            d_bitmap[i] = 1;
            break;
        }
    }

    // Step 3: Update data block bitmap and write to disk
    set_bitmap(d_bitmap, avail_blkno);
    if(bio_write(superblock.i_bitmap_blk, d_bitmap) < 0) {
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
    if(i_blk == NULL) {
        ERROR("Failed to allocate memory");
        return -1;
    }

    // Step 1: Get the inode's on-disk block number
    // Step 2: Get offset of the inode in the inode on-disk block
    int i_offset = ino%i_per_blk;
    int i_blkno = superblock.i_start_blk + ((ino/i_per_blk) + (i_offset != 0));

    // Step 3: Read the block from disk and then copy into inode structure
    if(bio_read(i_blkno, i_blk) < 0) {
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
    if(i_blk == NULL) {
        ERROR("Failed to allocate memory");
        return -1;
    }

    // Step 1: Get the block number where this inode resides on disk
    // Step 2: Get the offset in the block where this inode resides on disk
    int i_offset = ino%i_per_blk;
    int i_blkno = superblock.i_start_blk + ((ino/i_per_blk) + (i_offset != 0));

    // Step 3: Write inode to disk
    if(bio_read(i_blkno, i_blk) < 0) {
        free(i_blk);
        ERROR("Failed to read disk");
        return -1;
    }
    memcpy(&i_blk[i_offset], inode, sizeof(struct inode));
    if(bio_write(i_blkno, i_blk) < 0) {
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
    struct dirent *dirent_blk = malloc(BLOCK_SIZE);
    int *ptr_blk = malloc(BLOCK_SIZE);

    // Step 1: Call readi() to get the inode using ino (inode number of current directory)
    if(fname_CPY1   == NULL
    || fname_CPY2   == NULL
    || dirent_blk   == NULL
    || ptr_blk      == NULL
    || readi(ino, &inode) < 0
    ) {
        if(fname_CPY1   != NULL) free(fname_CPY1);
        if(fname_CPY2   != NULL) free(fname_CPY2);
        if(dirent_blk   != NULL) free(dirent_blk);
        if(ptr_blk      != NULL) free(ptr_blk);
        ERROR("Failed to allocate memory.");
    }
    char *path = strtok(fname_CPY1, delim);
    char *f_basename = basename(fname_CPY2);

    // Step 2: Get data block of current directory from inode
    // Step 3: Read directory's data block and check each directory entry.
    // If the name matches, then copy directory entry to dirent structure
    memcpy(ptr_blk, inode.direct_ptr, sizeof(inode.direct_ptr));
    for(; path != NULL; path = strtok(NULL, delim)) {
        int goto_next_subdir = 0;
        for(int i = -1; i < 8; ) {
            for(int j = 0; j < 16; ++j) {
                /* If we reached unused section of direct array. */
                if(ptr_blk[j] < 0) {
                    stop = 1;
                    break;
                }

                /* Read block pointed to at the direct array index, j. */
                if(bio_read((superblock.d_start_blk + ptr_blk[j]), dirent_blk) < 0) {
                    ERROR("Failed to read disk");
                    stop = 1;
                    DISK_ERROR = 1;
                    break;
                }

                /* Search block for the directory/subdirectory. */
                for(int k = 0; k < dirents_per_blk; ++k) {
                    /* Break if we reach an invalid directory entries.
                     * We separate invalid and valid directory entries. */
                    if(dirent_blk[k].valid == 0) {
                        break;
                    }

                    /* If dirent matches the directory we're looking for. */
                    if(strcmp(path, dirent_blk[k].name) == 0) {
                        /* If we're at the base directory,
                         * copy the directory entry and break out of all loops. */
                        if((at_base = (strcmp(path, f_basename) == 0))) {
                            found = 1;
                            stop = 1;
                            memcpy(dirent, &dirent_blk[k], sizeof(struct dirent));
                            break;
                        }

                        /* Otherwise, setup inode and ptr_blk go and search next subdirectory. */
                        if(readi(dirent_blk[k].ino, &inode) < 0) {
                            stop = 1;
                            DISK_ERROR = 1;
                            break;
                        }
                        memcpy(ptr_blk, inode.direct_ptr, sizeof(inode.direct_ptr));
                        goto_next_subdir = 1;
                        break;
                    }
                }

                /* If we reached unused section of array or if we
                 * found next subdir, break out of direct array loop. */
                if(stop || goto_next_subdir) break;
            }

            /* If we reached unused section of array or if we
             * found next subdir, break out of indirect array loop. */
            if(stop || goto_next_subdir) break;

            /* If we haven't searched all valid indirect pointer entries,
             * get next indirect pointer and set ptr_blk for next iteration.
             * Otherwise, break out of indirect pointer loop. */
            if((++i) < 8) {
                /* If we've reached an invalid indirect pointer entries section */
                if(inode.indirect_ptr[i] == -1) {
                    stop = 1;
                    break;
                }
                if(bio_read( (superblock.d_start_blk + inode.indirect_ptr[i]), ptr_blk) < 0) {
                    ERROR("Failed to read disk");
                    stop = 1;
                    DISK_ERROR = 1;
                }
            } else break;
        }
        if(stop) {
            break;
        }
    }

    free(fname_CPY1);
    free(fname_CPY2);
    free(dirent_blk);
    free(ptr_blk);
    if(DISK_ERROR) {
        return -1;
    }
    if(found == 0) {
        /*
        if(at_base) {
            ERROR("Invalid path: %s doesn't exist", path);
        }
         */
        return -1;
    }
    return 0;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

    int DISK_ERROR = 0;
    int duplicate = 0;
    int stop = 0;

    char *fname_CPY1 = strdup(fname);
    char *fname_CPY2 = strdup(fname);
    struct inode inode = {0};
    struct dirent dirent = {0};
    struct dirent *dirent_blk = malloc(BLOCK_SIZE);
    int *ptr_blk = malloc(BLOCK_SIZE);

    if(fname_CPY1   == NULL
    || fname_CPY2   == NULL
    || dirent_blk   == NULL
    || ptr_blk      == NULL
    ) {
        if(fname_CPY1   != NULL) free(fname_CPY1);
        if(fname_CPY2   != NULL) free(fname_CPY2);
        if(dirent_blk   != NULL) free(dirent_blk);
        if(ptr_blk      != NULL) free(ptr_blk);
        ERROR("Failed to allocate memory.");
    }
    char *f_basename = basename(fname_CPY1);
    char *f_dirname = dirname(fname_CPY2);

    /* Get immediate subdirectory's inode. */
    if(dir_find(dir_inode.ino, f_dirname, strlen(f_dirname), &dirent) < 0) {
        TRACEBACK;
        free(fname_CPY1);
        free(fname_CPY2);
        free(dirent_blk);
        free(ptr_blk);
        return -1;
    }
    readi(dirent.ino, &inode);

    // Step 1: Read dir_inode's data block and check each directory entry of dir_inode
    // Step 2: Check if fname (directory name) is already used in other entries
    if(dir_find(inode.ino, f_basename, strlen(f_basename), &dirent) >= 0) {
        duplicate = 1;
    }

    // Step 3: Add directory entry in dir_inode's data block and write to disk
    // Allocate a new data block for this directory if it does not exist
    // Update directory inode
    // Write directory entry
    if(duplicate == 0) {

        // TODO should we make new inode?

        /* Create new inode and write to disk. */
        struct inode new_inode = {
                .ino = f_ino,
                .valid = 1,
                .size = 0,
                .type = directory,
                .link = 0
        };
        for(int i = 0; i < 8; ++i) {
            new_inode.indirect_ptr[i] = -1;
            new_inode.direct_ptr[i] = -1;
            new_inode.direct_ptr[15-i] = -1;
        }
        writei((superblock.i_start_blk + new_inode.ino), &new_inode);

        /* Create new directory entry. */
        dirent.ino = new_inode.ino;
        dirent.valid = 1;
        strcpy(dirent.name, f_basename);

        for(int i = 0; i < 8; ) {
            for(int j = 0; j < 16; ++j) {

                /* If unused section of direct array has been reached. */
                if(ptr_blk[j] < 0) {

                    /* Create new dirent block and write to disk. */
                    int new_blkno = get_avail_blkno();
                    memcpy(&dirent_blk[0], &new_inode, sizeof(struct dirent));
                    for(int k = 0; k < dirents_per_blk; ++k) {
                        dirent_blk[0].valid = 0;
                    }
                    bio_write(superblock.d_start_blk + f_ino, dirent_blk);

                    /* Link to direct array. */
                    ptr_blk[j] = new_blkno;

                    stop = 1;
                    break;
                }

                /* Read dirent block. */
                if(bio_read((superblock.d_start_blk + ptr_blk[j]), dirent_blk) < 0) {
                    ERROR("Failed to read disk");
                    DISK_ERROR = 1;
                    stop = 1;
                    break;
                }

                /* Search block for space. */
                for(int k = 0; k < dirents_per_blk; ++k) {

                    /* If space is found. */
                    if(dirent_blk[k].valid == 0) {

                        /* Place new entry into dirent block. */
                        memcpy(&dirent_blk[k], &dirent, sizeof(struct dirent));

                        /* Write dirent block to disk. */
                        bio_write((superblock.d_start_blk + ptr_blk[j]), dirent_blk);

                        stop = 1;
                        break;
                    }
                }
            }

            if(stop) {
                break;
            }

            /* If all indirect pointer entries have been checked. */
            if((++i) > 7) {
                ERROR("Max amount of directory entries reached.");
                break;
            }

            /* If we've reached invalid section of the indirect pointer array. */
            if(inode.indirect_ptr[i] == -1) {
                /* Create and initialize new direct pointer array data block. */
                int new_blkno = get_avail_blkno();
                bio_read((superblock.d_start_blk + new_blkno), ptr_blk);
                for(int j = 0; j < 16; ++j) {
                    ptr_blk[j] = -1;
                }
                /* Write new direct pointer array block. */
                bio_write((superblock.d_start_blk + new_blkno), ptr_blk);


                /* Set indirect pointer entry to the new block's number. */
                inode.indirect_ptr[i] = new_blkno;
                break;
            }

            /* Get data block of the indirect pointer entry. */
            if(bio_read((superblock.d_start_blk + inode.indirect_ptr[i]), ptr_blk) < 0) {
                ERROR("Failed to read disk");
                stop = 1;
                DISK_ERROR = 1;
                break;
            }
        }
    }

    free(fname_CPY1);
    free(fname_CPY2);
    free(dirent_blk);
    free(ptr_blk);
    if(DISK_ERROR) {
        return -1;
    }
    if(duplicate) {
        ERROR("Directory of the same name already exists");
        return -1;
    }
    return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

    // TODO Want to keep valid and invalid blocks in pointer arrays separated into sections.
    // TODO Separate invalid and valid dirents into sections.
    // TODO Need to remove extra data blocks if no more dirents are used in that block
    // TODO have to handle ".", "..", <dirname> entries
    int DISK_ERROR = 0;
    int has_files = 0;
    int found = 0;
    int stop = 0;

    char *fname_CPY1 = strdup(fname);
    char *fname_CPY2 = strdup(fname);
    struct inode inode = {0};
    struct dirent dirent = {0};
    struct dirent *dirent_blk = malloc(BLOCK_SIZE);
    int *ptr_blk = malloc(BLOCK_SIZE);

    if(   fname_CPY1   == NULL
        || fname_CPY2   == NULL
        || dirent_blk   == NULL
        || ptr_blk      == NULL
       ) {
        if(fname_CPY1   != NULL) free(fname_CPY1);
        if(fname_CPY2   != NULL) free(fname_CPY2);
        if(dirent_blk   != NULL) free(dirent_blk);
        if(ptr_blk      != NULL) free(ptr_blk);
        ERROR("Failed to allocate memory.");
    }
    char *f_basename = basename(fname_CPY1);
    char *f_dirname = dirname(fname_CPY2);
    char *fname_CPY3 = strdup(f_basename);
    if(fname_CPY3 == NULL) {
        free(fname_CPY1);
        free(fname_CPY2);
        free(dirent_blk);
        free(ptr_blk);
    }
    char *f_subdir_basename = basename(fname_CPY3);


    // Step 1: Read dir_inode's data block and checks each directory entry of dir_inode

    /* Get immediate subdirectory of directory we want to remove. */
    if(dir_find(dir_inode.ino, f_dirname, name_len, &dirent) < 0) {
        TRACEBACK;
        free(fname_CPY1);
        free(fname_CPY2);
        free(fname_CPY3);
        free(dirent_blk);
        free(ptr_blk);
        return -1;
    }
    readi(dirent.ino, &inode);


    // Step 2: Check if fname exist
    // Step 3: If exist, then remove it from dir_inode's data block and write to disk

    /* Copy the immediate subdirectory's direct pointer to ptr_blk. */
    memcpy(ptr_blk, inode.direct_ptr, sizeof(inode.direct_ptr));

    /* Begin searching all valid direct pointers and indirect pointers for directory. */
    for(int i = -1; i < 8; ) {
        for(int j = 0; j < 16; ++j) {
            /* If we reached unused section of direct array. */
            if(ptr_blk[j] < 0) {
                stop = 1;
                break;
            }

            /* Read block pointed to at the direct array index, j. */
            if(bio_read((superblock.d_start_blk + ptr_blk[j]), dirent_blk) < 0) {
                ERROR("Failed to read disk");
                DISK_ERROR = 1;
                stop = 1;
                break;
            }

            /* Search the block for the directory we want to remove. */
            for(int k = 0; k < dirents_per_blk; ++k) {

                /* Skip block if we reached invalid section of the directory entries. */
                if(dirent_blk[k].valid == 0) {
                    break;
                }

                /* If we find the directory we want to remove. */
                if(strcmp(f_basename, dirent_blk[k].name) == 0) {
                    found = 1;

                    /* Read the inode of the directory to be removed. */
                    struct inode to_be_removed_inode = {0};
                    if(readi(dirent_blk[k].ino, &to_be_removed_inode) < 0) {
                        DISK_ERROR = -1;
                        ERROR("Failed to read disk");
                        stop = 1;
                        break;
                    }

                    /* Check if directory to be removed is empty . */
                    if(to_be_removed_inode.link > 3) {
                        has_files = 1;
                        stop = 1;
                        break;
                    }

                    /* Check if the dirent block will still be in use after removal. */
                    int all_dirents_invalid = 1;
                    for(int l = 0; l < k; ++l) {
                        if(dirent_blk[l].valid && strcmp(dirent_blk[l].name, f_subdir_basename)!= 0) {
                            all_dirents_invalid = 0;
                        }
                    }
                    for(int l = k+1; l < dirents_per_blk; ++l) {
                        if(dirent_blk[l].valid && strcmp(dirent_blk[l].name, f_subdir_basename)!= 0) {
                            all_dirents_invalid = 0;
                        }
                        /* Shifts all directory entries after the removed directory. */
                        dirent_blk[l-1] = dirent_blk[l];
                        /* TODO don't know if this copies the struct info or just a pointer. */
                        // memcpy(&dirent_blk[l-1], &dirent_blk[l], sizeof(struct dirent));
                    }

                    /* Invalidate last directory entry in block. */
                    dirent_blk[dirents_per_blk-1].valid = 0;

                    /* If all entries in the dirent block are invalid. */
                    if(all_dirents_invalid) {

                        /* Invalidate dirent block. */
                        unset_bitmap(d_bitmap, inode.direct_ptr[j]);

                        /* Check if direct pointer will still be used after removal
                         * and shift pointers after the directory to be removed down 1. */
                        int all_direct_pointers_invalid = 1;
                        if(i < 0) {
                            all_direct_pointers_invalid = 0;
                        }
                        for(int l = 0; l < j; ++l) {
                            if(ptr_blk[l] >= 0) {
                                all_direct_pointers_invalid = 0;
                            }
                            all_direct_pointers_invalid = 0;
                        }
                        for(int l = j+1; l < 16; ++l) {
                            if(ptr_blk[l] >= 0) {
                                all_direct_pointers_invalid = 0;
                            }
                            /* Shift pointers down. */
                            ptr_blk[l-1] = ptr_blk[l];
                        }

                        /* Invalidate last direct pointer. */
                        inode.direct_ptr[15] = -1;


                        /* If all entries in the pointer block are invalid and if the block
                         * is not the direct pointer in the inode.                          */
                        if(all_direct_pointers_invalid) {

                            /* Invalidate data block. */
                            unset_bitmap(d_bitmap, inode.indirect_ptr[i]);

                            /* Shift all indirect pointer entries. */
                            for(int l = i+1; l < 8; ++l) {
                                inode.indirect_ptr[l-1] = inode.indirect_ptr[l];
                            }

                            /* Invalidate last entry of indirect pointer array. */
                            inode.indirect_ptr[7] = -1;
                        } else {
                            /* If ptr_blk refers to a block pointed to in an indirect pointer. */
                            if(i >= 0) {
                                /* Write ptr_blk to the direct block address of the indirect pointer entry. */
                                bio_write((superblock.d_start_blk + inode.indirect_ptr[i]), ptr_blk);
                            } else {
                                /* Copy ptr_blk to the direct pointer array. */
                                memcpy(inode.direct_ptr, ptr_blk, sizeof(inode.direct_ptr));
                            }
                        }

                        /* Write data blocks and inode back to disk. */
                        writei(inode.ino, &inode);
                    } else {
                        /* Write dirent block to disk. */
                        bio_write(superblock.d_start_blk + ptr_blk[j], dirent_blk);
                    }

                    /* Invalidate all blocks pointed to by the inode of the directory
                     * to be removed.                                                       */
                    for(int l = 0; l < 16; ++l) {
                        if(to_be_removed_inode.direct_ptr[l] == -1) {
                            break;
                        }
                        unset_bitmap(d_bitmap, to_be_removed_inode.direct_ptr[l]);
                    }

                    /* Invalidate the inode of the directory to be removed. */
                    unset_bitmap(i_bitmap, to_be_removed_inode.ino);

                    /* Write bitmaps back to disk. */
                    char *d_bitmap_blk = malloc(BLOCK_SIZE);
                    char *i_bitmap_blk = malloc(BLOCK_SIZE);
                    if(d_bitmap_blk == NULL || i_bitmap_blk == NULL) {
                        DISK_ERROR = -1;
                        stop = 1;
                        break;
                    }
                    memcpy(d_bitmap_blk, d_bitmap, sizeof(d_bitmap));
                    memcpy(i_bitmap_blk, i_bitmap, sizeof(i_bitmap));
                    bio_write(superblock.d_bitmap_blk, d_bitmap_blk);
                    bio_write(superblock.i_bitmap_blk, i_bitmap_blk);
                    free(d_bitmap_blk);
                    free(i_bitmap_blk);

                    stop = 1;
                    found = 1;
                    break;
                }

                if(stop) {
                    break;
                }
            }
        }

        if(stop) {
            break;
        }

        /* If we haven't searched all valid indirect pointer entries,
         * get next indirect pointer and set ptr_blk for next iteration.
         * Otherwise, break out of indirect pointer loop.                */
        if((++i) < 8) {
            /* If we've reached invalid indirect pointer entries
             * section.                                             */
            if(inode.indirect_ptr[i] == -1) {
                stop = 1;
                break;
            }

            /* Get data pointed to in indirect pointer block and write it to ptr_blk */
            if(bio_read((superblock.d_start_blk + inode.indirect_ptr[i]), ptr_blk) < 0) {
                ERROR("Failed to read disk");
                stop = 1;
                DISK_ERROR = 1;
            }
        } else {
            break;
        }
    }


    free(fname_CPY1);
    free(fname_CPY2);
    free(fname_CPY3);
    free(dirent_blk);
    free(ptr_blk);
    if(DISK_ERROR) {
        return -1;
    }
    if(has_files) {
        ERROR("Directory is not empty");
        return -1;
    }
    if(found == 0) {
        ERROR("Failed to find directory to be deleted");
        return -1;
    }
    return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {

    // Step 1: Resolve the path name, walk through path, and finally, find its inode.
    // Note: You could either implement it in a iterative way or recursive way
    struct dirent dirent = {0};
    dir_find(ino, path, strlen(path), &dirent);
    readi(dirent.ino, inode);

    return 0;
}

/* 
 * Make file system
 */
int tfs_mkfs() {
    // Call dev_init() to initialize (Create) Diskfile
    dev_init(diskfile_path);

    void *blk = malloc(BLOCK_SIZE);
    if(blk == NULL) {
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
            .d_start_blk    = (2 + ((MAX_INUM/i_per_blk) + ((MAX_INUM%i_per_blk) != 0))) + 1
    };

    memcpy(blk, &superblock, sizeof(struct superblock));
    if(bio_write(0, blk) < 0) {
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

    for(int i = 0; i < dirent_blks; ++i) {
        for(int j = 0; j < dirents_per_blk; ++j) {
            int pos = (i*dirent_blks) + j;
            if(pos > 2) {
                while(j < dirents_per_blk) {
                    ((struct dirent *)blk)[pos].valid = 0;
                    j++;
                }
                break;
            }
            memcpy(&((struct dirent *)blk)[j], &dirent[pos], sizeof(struct dirent));
        }
        if(bio_write((superblock.d_start_blk + i), blk) < 0) {
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
    for(int i = 0; i < 8; ++i) {
        if(i < dirent_blks) {
            i_root.direct_ptr[i]    =  i;
            i_root.link++;
        } else {
            i_root.direct_ptr[i]    = -1;
        }
            i_root.direct_ptr[16-i] = -1;
            i_root.indirect_ptr[i]  = -1;
    }
    memcpy(blk, &i_root, sizeof(struct inode));
    if(bio_write(superblock.i_start_blk, blk) < 0) {
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
    i_per_blk = (int)((double)BLOCK_SIZE/sizeof(struct dirent));
    dirents_per_blk = (int)((double)BLOCK_SIZE/sizeof(struct dirent));

    // Step 1a: If disk file is not found, call mkfs
    if(dev_open(diskfile_path) < 0) {
        if(tfs_mkfs() < 0) {
            exit(EXIT_FAILURE);
        }
        return NULL;
    }
    // Step 1b: If disk file is found, just initialize in-memory data structures
    // and read superblock from disk
    if((bio_read(0, &superblock)) < 0) {
        ERROR("Failed to read disk");
        exit(EXIT_FAILURE);
    }
    if(superblock.magic_num != MAGIC_NUM) {
        ERROR( "disk's filesystem is not recognized");
        exit(EXIT_FAILURE);
    }

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

