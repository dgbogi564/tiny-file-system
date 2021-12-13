/*
 *  Copyright (C) 2019 CS416 Spring 2019
 *	
 *	Tiny File System
 *
 *	File:	block.h
 *  Author: Yujie REN
 *	Date:	April 2019
 *
 */

#ifndef _BLOCK_H_
#define _BLOCK_H_


//Disk size set to 32MB
#define DISK_SIZE	32*1024*1024
//Block size set to 4KB
#define BLOCK_SIZE 4096

void dev_init(const char* diskfile_path);
int dev_open(const char* diskfile_path);
void dev_close();
int bio_read(const int block_num, void *buf);
int bio_write(const int block_num, const void *buf);

#endif
