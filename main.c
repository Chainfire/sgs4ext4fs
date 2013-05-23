#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#ifdef WIN32
	#include <io.h>
#else
	#include <unistd.h>
#endif

#define __le64 u64
#define __le32 u32
#define __le16 u16

#define __be64 u64
#define __be32 u32
#define __be16 u16

#define __u64 u64
#define __u32 u32
#define __u16 u16
#define __u8 u8

typedef unsigned long long u64;
typedef signed long long s64;
typedef unsigned int u32;
typedef unsigned short int u16;
typedef unsigned char u8;

typedef struct sparse_header {
  __le32	magic;		/* 0xed26ff3a */
  __le16	major_version;	/* (0x1) - reject images with higher major versions */
  __le16	minor_version;	/* (0x0) - allow images with higer minor versions */
  __le16	file_hdr_sz;	/* 28 bytes for first revision of the file format */
  __le16	chunk_hdr_sz;	/* 12 bytes for first revision of the file format */
  __le32	blk_sz;		/* block size in bytes, must be a multiple of 4 (4096) */
  __le32	total_blks;	/* total blocks in the non-sparse output image */
  __le32	total_chunks;	/* total chunks in the sparse input image */
  __le32	image_checksum; /* CRC32 checksum of the original data, counting "don't care" */
				/* as 0. Standard 802.3 polynomial, use a Public Domain */
				/* table implementation */
} sparse_header_t;

#define SPARSE_HEADER_MAGIC	0xed26ff3a

#define CHUNK_TYPE_RAW		0xCAC1
#define CHUNK_TYPE_FILL		0xCAC2
#define CHUNK_TYPE_DONT_CARE	0xCAC3
#define CHUNK_TYPE_CRC32    0xCAC4

typedef struct chunk_header {
  __le16	chunk_type;	/* 0xCAC1 -> raw; 0xCAC2 -> fill; 0xCAC3 -> don't care */
  __le16	reserved1;
  __le32	chunk_sz;	/* in blocks in output image */
  __le32	total_sz;	/* in bytes of chunk input file including chunk header and data */
} chunk_header_t;

/* Following a Raw or Fill or CRC32 chunk is data.
 *  For a Raw chunk, it's the data in chunk_sz * blk_sz.
 *  For a Fill chunk, it's 4 bytes of the fill data.
 *  For a CRC32 chunk, it's 4 bytes of CRC32
 */

#define BUF_SIZE 65536

int listFile(char* filename) {
	int fdIn = open(filename, O_RDONLY);
	if (fdIn < 0) {
		printf("Error: could not open file\n");
		return 1;
	}
	
	sparse_header_t hFile;
	read(fdIn, &hFile, sizeof(sparse_header_t));
	
	printf("--- file header ---\n\n");
	printf("magic: 0x%08X\n", hFile.magic);
	printf("version: %d.%d\n", hFile.major_version, hFile.minor_version);
	printf("file_hdr_sz: %d\n", hFile.file_hdr_sz);
	printf("chunk_hdr_sz: %d\n", hFile.chunk_hdr_sz);
	printf("blk_sz: %d\n", hFile.blk_sz);
	printf("total_blks: %d\n", hFile.total_blks);
	printf("total_chunks: %d\n", hFile.total_chunks);
	printf("image_checksum: %d\n", hFile.image_checksum);
	
	int file_hdr_sz_extra = hFile.file_hdr_sz - sizeof(sparse_header_t);
	int chunk_hdr_sz_extra = hFile.chunk_hdr_sz - sizeof(chunk_header_t);
	printf("\n");
	printf("file_hdr_sz extra: %d\n", file_hdr_sz_extra);
	printf("chunk_hdr_sz extra: %d\n", chunk_hdr_sz_extra);
	
	if (file_hdr_sz_extra > 0) {
		printf("\n");
		printf("extra bytes 0x ");
		int i;
		__u8 b;
		for (i = 0; i < file_hdr_sz_extra; i++) {
			read(fdIn, &b, 1);
			printf("%02X ", b);
		}
		printf("\n");
	}
	
	printf("\n");
	printf("--- chunks ---\n");
	
	int chunk_idx;
	for (chunk_idx = 0; chunk_idx < hFile.total_chunks; chunk_idx++) {
		chunk_header_t hChunk;
		read(fdIn, &hChunk, sizeof(chunk_header_t));
		
		printf("chunk 0x%04X / chunk_sz %8d / total_sz %8d / extra bytes 0x ", hChunk.chunk_type, hChunk.chunk_sz, hChunk.total_sz);
		int i;
		__u8 b;
		for (i = 0; i < chunk_hdr_sz_extra; i++) {
			read(fdIn, &b, 1);
			printf("%02X ", b);
		}
		printf("\n");
		
		lseek(fdIn, hChunk.total_sz - hFile.chunk_hdr_sz, SEEK_CUR);
	}
	
	close(fdIn);
	
	return 0;
}

int bloatFile(char* inFile, char* outFile) {
	// I9500 images have 4 bytes padding to both file and chunk headers, add them as 0s if not present
	
	int fdIn = open(inFile, O_RDONLY);
	int fdOut = open(outFile, O_WRONLY | O_CREAT);
	
	if ((fdIn < 0) || (fdOut < 0)) {
		close(fdIn);
		close(fdOut);
		printf("Error: could not open file\n");
		return 1;
	}
	
	sparse_header_t hFile;
	read(fdIn, &hFile, sizeof(sparse_header_t));
	
	int file_hdr_sz_org = hFile.file_hdr_sz;
	int file_hdr_sz_new = 32;
	int file_hdr_sz_extra_read = file_hdr_sz_org - sizeof(sparse_header_t);
	int file_hdr_sz_extra_write = file_hdr_sz_new - sizeof(sparse_header_t);
	int chunk_hdr_sz_org = hFile.chunk_hdr_sz;
	int chunk_hdr_sz_new = 16;
	int chunk_hdr_sz_extra_read = chunk_hdr_sz_org - sizeof(chunk_header_t);
	int chunk_hdr_sz_extra_write = chunk_hdr_sz_new - sizeof(chunk_header_t);
	
	hFile.file_hdr_sz = file_hdr_sz_new;
	hFile.chunk_hdr_sz = chunk_hdr_sz_new;
	write(fdOut, &hFile, sizeof(sparse_header_t));
	
	if (file_hdr_sz_extra_read > 0) {
		int i;
		__u8 b;
		for (i = 0; i < file_hdr_sz_extra_read; i++) read(fdIn, &b, 1);
	}
	if (file_hdr_sz_extra_write > 0) {
		int i;
		__u8 b = 0;
		for (i = 0; i < file_hdr_sz_extra_write; i++) write(fdOut, &b, 1);
	}
	
	void* buf = (void*)malloc(BUF_SIZE);
	
	int chunk_idx;
	for (chunk_idx = 0; chunk_idx < hFile.total_chunks; chunk_idx++) {
		printf(".");
		
		chunk_header_t hChunk;
		read(fdIn, &hChunk, sizeof(chunk_header_t));
		if (chunk_hdr_sz_extra_read > 0) {
			int i;
			__u8 b;
			for (i = 0; i < chunk_hdr_sz_extra_read; i++) read(fdIn, &b, 1);
		}
		int left = hChunk.total_sz - chunk_hdr_sz_org;
		hChunk.total_sz = hChunk.total_sz - chunk_hdr_sz_extra_read + chunk_hdr_sz_extra_write;
		write(fdOut, &hChunk, sizeof(chunk_header_t));
		if (chunk_hdr_sz_extra_write > 0) {
			int i;
			__u8 b = 0;
			for (i = 0; i < chunk_hdr_sz_extra_write; i++) write(fdOut, &b, 1);
		}
		
		while (left > 0) {
			int size = (left > BUF_SIZE ? BUF_SIZE : left);
			size = read(fdIn, buf, size);
			write(fdOut, buf, size);
			left -= size;
		}
	}
	printf("\n");
	
	free(buf);
	
	close(fdIn);
	
	return 0;
}

int trimFile(char* inFile, char* outFile) {
	// I9500 images have 4 bytes padding to both file and chunk headers, strip them if present
	
	int fdIn = open(inFile, O_RDONLY);
	int fdOut = open(outFile, O_WRONLY | O_CREAT);
	
	if ((fdIn < 0) || (fdOut < 0)) {
		close(fdIn);
		close(fdOut);
		printf("Error: could not open file\n");
		return 1;
	}
	
	sparse_header_t hFile;
	read(fdIn, &hFile, sizeof(sparse_header_t));
	
	int file_hdr_sz_org = hFile.file_hdr_sz;
	int file_hdr_sz_new = 28;
	int file_hdr_sz_extra = file_hdr_sz_org - sizeof(sparse_header_t);
	int chunk_hdr_sz_org = hFile.chunk_hdr_sz;
	int chunk_hdr_sz_new = 12;
	int chunk_hdr_sz_extra = chunk_hdr_sz_org - sizeof(chunk_header_t);
	
	hFile.file_hdr_sz = file_hdr_sz_new;
	hFile.chunk_hdr_sz = chunk_hdr_sz_new;
	write(fdOut, &hFile, sizeof(sparse_header_t));
	
	{
		int i;
		__u8 b;
		for (i = 0; i < file_hdr_sz_extra; i++) read(fdIn, &b, 1);
	}
	
	void* buf = (void*)malloc(BUF_SIZE);
	
	int chunk_idx;
	for (chunk_idx = 0; chunk_idx < hFile.total_chunks; chunk_idx++) {
		printf(".");
		
		chunk_header_t hChunk;
		read(fdIn, &hChunk, sizeof(chunk_header_t));
		{
			int i;
			__u8 b;
			for (i = 0; i < chunk_hdr_sz_extra; i++) read(fdIn, &b, 1);
		}
		int left = hChunk.total_sz - chunk_hdr_sz_org;
		hChunk.total_sz -= chunk_hdr_sz_extra;
		write(fdOut, &hChunk, sizeof(chunk_header_t));
		
		while (left > 0) {
			int size = (left > BUF_SIZE ? BUF_SIZE : left);
			size = read(fdIn, buf, size);
			write(fdOut, buf, size);
			left -= size;
		}
	}
	printf("\n");
	
	free(buf);
	
	close(fdIn);
	
	return 0;
}

int main(int argc, char **argv)
{
	printf("sgs4ext4fs - Copyright (C) 2013 - Chainfire\n\n");
	
	int go = 0;
	if (argc == 3) {
		if (strcmp(argv[1], "--list") == 0) go = 1;
	} else if (argc == 4) {
		if (strcmp(argv[1], "--bloat") == 0) go = 2;
		if (strcmp(argv[1], "--trim") == 0) go = 3;
	}
	
	if (go == 0) {
		printf("Usage:\n");
		printf("    sgs4ext4fs --list infile\n");
		printf("    sgs4ext4fs --bloat infile outfile\n");
		printf("    sgs4ext4fs --trim infile outfile\n");
		return 1;
	}
	
	if (go == 1) {
		return listFile(argv[2]);
	} else if (go == 2) {
		return bloatFile(argv[2], argv[3]);
	} else if (go == 3) {
		return trimFile(argv[2], argv[3]);
	}
	
	return 1;
}
