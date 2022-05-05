#ifndef __FS_H__
#define __FS_H__

#include "types.h"
// On-disk file system format.
// Both the kernel and user programs use this header file.

#define NFILE 100 // open files per system
#define NINODE 50 // maximum number of active i-nodes
#define NDEV 10 // maximum major device number
#define ROOTDEV 1 // device number of file system root disk
#define MAXOPBLOCKS 10 // max # of blocks any FS op writes
#define NBUF (MAXOPBLOCKS * 3) // size of disk block cache
#define FSSIZE 1000 // size of file system in blocks
#define MAXPATH 128 // maximum file path name

#define ROOTINO 1 // root i-number
#define BSIZE 1024 // block size

// Disk layout:
// [ boot block | super block | inode blocks | free bit map | data blocks]
//
// mkfs computes the super block and builds an initial file system. The
// super block describes the disk layout:
struct superblock {
	uint magic; // Must be FSMAGIC
	uint size; // Size of file system image (blocks)
	uint nblocks; // Number of data blocks
	uint ninodes; // Number of inodes.
	uint inodestart; // Block number of first inode block
	uint bmapstart; // Block number of first free map block
};

#define FSMAGIC 0x10203040

#define NDIRECT 12
#define NINDIRECT (BSIZE / sizeof(uint))
#define MAXFILE (NDIRECT + NINDIRECT)

// File type
#define T_DIR 1 // Directory
#define T_FILE 2 // File

// On-disk inode structure
struct dinode {
	short type; // File type
	short pad[3];
	// LAB4: you can reduce size of pad array and add link count below,
	//       or you can just regard a pad as link count.
	//       But keep in mind that you'd better keep sizeof(dinode) unchanged
	uint size; // Size of file (bytes)
	uint addrs[NDIRECT + 1]; // Data block addresses
};

// Inodes per block.
#define IPB (BSIZE / sizeof(struct dinode))

// Block containing inode i
#define IBLOCK(i, sb) ((i) / IPB + sb.inodestart)

// Bitmap bits per block
#define BPB (BSIZE * 8)

// Block of free map containing bit for block b
#define BBLOCK(b, sb) ((b) / BPB + sb.bmapstart)

// Directory is a file containing a sequence of dirent structures.
#define DIRSIZ 14

struct dirent {
	ushort inum;
	char name[DIRSIZ];
};

// file.h
struct inode;

void fsinit();
int dirlink(struct inode *, char *, uint);
struct inode *dirlookup(struct inode *, char *, uint *);
struct inode *ialloc(uint, short);
struct inode *idup(struct inode *);
void iinit();
void ivalid(struct inode *);
void iput(struct inode *);
void iunlock(struct inode *);
void iunlockput(struct inode *);
void iupdate(struct inode *);
struct inode *namei(char *);
struct inode *root_dir();
int readi(struct inode *, int, uint64, uint, uint);
int writei(struct inode *, int, uint64, uint, uint);
void itrunc(struct inode *);
int dirls(struct inode *);
#endif //!__FS_H__
